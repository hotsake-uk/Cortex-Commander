#include "Lockstep.h"

#include "ActivityMan.h"
#include "ConsoleMan.h"
#include "DeterminismChecker.h"
#include "GameVersion.h"
#include "SceneMan.h"
#include "Scene.h"
#include "System.h"
#include "UInputMan.h"
#include "RTETools.h"

#include "BitStream.h"
#include "MessageIdentifiers.h"
#include "RakPeerInterface.h"
// RakNet includes Windows.h, whose macros clash with our method names.
#undef GetClassName

#include <array>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <format>
#include <map>
#include <optional>
#include <random>
#include <thread>

using namespace RTE;

namespace {
	enum LockstepMessage : unsigned char {
		ID_LOCKSTEP_HELLO = ID_USER_PACKET_ENUM + 80, //!< Says which match this machine is set up for; the other refuses one that differs.
		ID_LOCKSTEP_INPUT, //!< One player's input for one sim update.
		ID_LOCKSTEP_HASH, //!< The hash of the simulation after a sim update.
		ID_LOCKSTEP_BYE //!< The other player has left.
	};

	constexpr int c_InputDelay = 4; //!< How many sim updates ahead each machine sends its player's input.
	constexpr int c_HashInterval = 60; //!< How many sim updates go between comparing the simulation's hash.
	constexpr int c_MaxPlayers = 2;

	RakNet::RakPeerInterface* s_Peer = nullptr;
	RakNet::SystemAddress s_Other = RakNet::UNASSIGNED_SYSTEM_ADDRESS;
	bool s_Hosting = false;
	std::string s_HostAddress;
	unsigned short s_Port = 0;
	bool s_Connected = false; //!< Whether the other player is connected.
	bool s_HelloSent = false;
	std::optional<std::string> s_TheirDescription; //!< Which match the other machine is set up for, once it has said.
	bool s_Agreed = false; //!< Whether both machines are set up for the same match.
	bool s_OtherLeft = false; //!< Whether the other player has left (or the connection was lost).
	bool s_Stopped = false; //!< Whether the match was stopped (a desync, or the other player left): no more sim updates.
	uint32_t s_SimUpdate = 0; //!< The match's next sim update.
	std::map<uint32_t, std::array<std::optional<UInputMan::LockstepInput>, c_MaxPlayers>> s_Inputs; //!< The players' input by sim update.
	std::map<uint32_t, uint64_t> s_LocalHashes; //!< This machine's hashes waiting for the other's.
	std::map<uint32_t, uint64_t> s_OtherHashes; //!< The other machine's hashes waiting for this one's.
	std::chrono::steady_clock::time_point s_LastWaitNote;
	std::mt19937 s_TestInputRandom; //!< For CCCP_LOCKSTEP_TEST_INPUT: made-up input, for test runs with nobody at the keys.

	std::string MatchDescription() {
		const Activity* activity = g_ActivityMan.GetActivity();
		const Scene* scene = g_SceneMan.GetScene();
		return std::format("{}|{}|{}|{}", c_VersionString, activity ? activity->GetPresetName() : "", scene ? scene->GetPresetName() : "", GetRNGSeedOffset());
	}

	void Send(RakNet::BitStream& stream) {
		if (s_Peer && s_Other != RakNet::UNASSIGNED_SYSTEM_ADDRESS) {
			s_Peer->Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, s_Other, false);
		}
	}

	void Stop(const std::string& why) {
		if (!s_Stopped) {
			s_Stopped = true;
			g_ConsoleMan.PrintString("ONLINE: " + why);
			if (System::IsInHeadlessMode()) {
				// (Headless, nobody is there to leave the stopped match.)
				System::SetQuit();
			}
		}
	}

	void WriteInput(RakNet::BitStream& stream, const UInputMan::LockstepInput& input) {
		stream.WriteAlignedBytes(input.Elements.data(), static_cast<unsigned int>(input.Elements.size()));
		stream.WriteAlignedBytes(input.Mouse.data(), static_cast<unsigned int>(input.Mouse.size()));
		for (float value: {input.Move.m_X, input.Move.m_Y, input.Aim.m_X, input.Aim.m_Y, input.MouseMovement.m_X, input.MouseMovement.m_Y, input.DigitalAimSpeed}) {
			stream.Write(value);
		}
		stream.Write(static_cast<int32_t>(input.Wheel));
		stream.Write(static_cast<int32_t>(input.Device));
	}

	bool ReadInput(RakNet::BitStream& stream, UInputMan::LockstepInput& input) {
		bool ok = stream.ReadAlignedBytes(input.Elements.data(), static_cast<unsigned int>(input.Elements.size()));
		ok = ok && stream.ReadAlignedBytes(input.Mouse.data(), static_cast<unsigned int>(input.Mouse.size()));
		for (float* value: {&input.Move.m_X, &input.Move.m_Y, &input.Aim.m_X, &input.Aim.m_Y, &input.MouseMovement.m_X, &input.MouseMovement.m_Y, &input.DigitalAimSpeed}) {
			ok = ok && stream.Read(*value);
		}
		int32_t wheel = 0;
		int32_t device = 0;
		ok = ok && stream.Read(wheel) && stream.Read(device);
		input.Wheel = wheel;
		input.Device = device;
		return ok;
	}

	/// Made-up input for test runs: walks one way or the other for a while, jumps now and then, fires in bursts.
	UInputMan::LockstepInput TestInput() {
		UInputMan::LockstepInput input;
		static int s_Direction = 0;
		static int s_Left = 0;
		if (--s_Left <= 0) {
			s_Direction = static_cast<int>(s_TestInputRandom() % 3);
			s_Left = 30 + static_cast<int>(s_TestInputRandom() % 90);
		}
		auto hold = [&input](int element, bool pressed) { input.Elements[element] |= pressed ? 3 : 1; };
		if (s_Direction == 1) {
			hold(InputElements::INPUT_L_LEFT, s_Left == 1);
		} else if (s_Direction == 2) {
			hold(InputElements::INPUT_L_RIGHT, s_Left == 1);
		}
		if (s_TestInputRandom() % 40 == 0) {
			hold(InputElements::INPUT_JUMP, true);
		}
		if (s_TestInputRandom() % 3 == 0) {
			hold(InputElements::INPUT_FIRE, s_TestInputRandom() % 4 == 0);
		}
		return input;
	}

	void HandlePacket(RakNet::Packet* packet) {
		switch (packet->data[0]) {
			case ID_NEW_INCOMING_CONNECTION:
			case ID_CONNECTION_REQUEST_ACCEPTED:
				s_Other = packet->systemAddress;
				s_Connected = true;
				g_ConsoleMan.PrintString(std::string("ONLINE: connected to ") + packet->systemAddress.ToString(true) + ".");
				break;
			case ID_CONNECTION_ATTEMPT_FAILED:
				g_ConsoleMan.PrintString("ONLINE: couldn't reach the host; trying again.");
				s_Peer->Connect(s_HostAddress.c_str(), s_Port, nullptr, 0);
				break;
			case ID_NO_FREE_INCOMING_CONNECTIONS:
				Stop("the host already has a player.");
				break;
			case ID_DISCONNECTION_NOTIFICATION:
			case ID_CONNECTION_LOST:
			case ID_LOCKSTEP_BYE:
				// (The match goes on as far as the input they sent before going reaches.)
				s_OtherLeft = true;
				break;
			case ID_LOCKSTEP_HELLO: {
				RakNet::BitStream stream(packet->data, packet->length, false);
				stream.IgnoreBytes(1);
				RakNet::RakString theirs;
				stream.Read(theirs);
				s_TheirDescription = theirs.C_String();
				break;
			}
			case ID_LOCKSTEP_INPUT: {
				RakNet::BitStream stream(packet->data, packet->length, false);
				stream.IgnoreBytes(1);
				uint32_t simUpdate = 0;
				uint8_t player = 0;
				UInputMan::LockstepInput input;
				if (stream.Read(simUpdate) && stream.Read(player) && player < c_MaxPlayers && player != Lockstep::GetLocalPlayer() && ReadInput(stream, input)) {
					s_Inputs[simUpdate][player] = input;
				}
				break;
			}
			case ID_LOCKSTEP_HASH: {
				RakNet::BitStream stream(packet->data, packet->length, false);
				stream.IgnoreBytes(1);
				uint32_t simUpdate = 0;
				uint64_t hash = 0;
				if (stream.Read(simUpdate) && stream.Read(hash)) {
					s_OtherHashes[simUpdate] = hash;
				}
				break;
			}
			default:
				break;
		}
	}

	void CompareHashes() {
		while (!s_LocalHashes.empty() && !s_OtherHashes.empty()) {
			auto mine = s_LocalHashes.begin();
			auto theirs = s_OtherHashes.begin();
			if (mine->first < theirs->first) {
				s_LocalHashes.erase(mine);
			} else if (theirs->first < mine->first) {
				s_OtherHashes.erase(theirs);
			} else {
				if (mine->second != theirs->second) {
					Stop(std::format("out of sync at sim update {} (hash {:016x} here, {:016x} there). The match is stopped; each machine's determinism trace shows where.", mine->first, mine->second, theirs->second));
				}
				s_LocalHashes.erase(mine);
				s_OtherHashes.erase(theirs);
			}
		}
	}
} // namespace

bool Lockstep::s_Active = false;
int Lockstep::s_LocalPlayer = 0;

void Lockstep::Host(int port) {
	s_Active = true;
	s_Hosting = true;
	s_LocalPlayer = 0;
	s_Port = static_cast<unsigned short>(port);
}

void Lockstep::Join(const std::string& address) {
	s_Active = true;
	s_Hosting = false;
	s_LocalPlayer = 1;
	const size_t colon = address.rfind(':');
	s_HostAddress = colon == std::string::npos ? address : address.substr(0, colon);
	s_Port = static_cast<unsigned short>(colon == std::string::npos ? 47100 : std::atoi(address.substr(colon + 1).c_str()));
}

void Lockstep::Update() {
	if (!s_Active) {
		return;
	}
	if (!s_Peer) {
		s_Peer = RakNet::RakPeerInterface::GetInstance();
		s_TestInputRandom.seed(1234u + static_cast<unsigned int>(s_LocalPlayer));
		if (s_Hosting) {
			RakNet::SocketDescriptor socket(s_Port, nullptr);
			if (s_Peer->Startup(1, &socket, 1) != RakNet::RAKNET_STARTED) {
				Stop(std::format("couldn't open port {} to host on.", s_Port));
				return;
			}
			s_Peer->SetMaximumIncomingConnections(1);
			g_ConsoleMan.PrintString(std::format("ONLINE: hosting on port {}, waiting for the other player.", s_Port));
		} else {
			RakNet::SocketDescriptor socket;
			if (s_Peer->Startup(1, &socket, 1) != RakNet::RAKNET_STARTED) {
				Stop("couldn't start the network.");
				return;
			}
			s_Peer->Connect(s_HostAddress.c_str(), s_Port, nullptr, 0);
			g_ConsoleMan.PrintString(std::format("ONLINE: joining {}:{}.", s_HostAddress, s_Port));
		}
		// (A dropped connection is noticed in a few seconds rather than RakNet's default of ten.)
		s_Peer->SetTimeoutTime(5000, RakNet::UNASSIGNED_SYSTEM_ADDRESS);
	}
	for (RakNet::Packet* packet = s_Peer->Receive(); packet; s_Peer->DeallocatePacket(packet), packet = s_Peer->Receive()) {
		HandlePacket(packet);
	}
	// Once connected and in the match, say which match this is.
	if (s_Connected && !s_HelloSent && g_ActivityMan.GetActivity() && g_SceneMan.GetScene()) {
		s_HelloSent = true;
		RakNet::BitStream stream;
		stream.Write(static_cast<RakNet::MessageID>(ID_LOCKSTEP_HELLO));
		stream.Write(RakNet::RakString(MatchDescription().c_str()));
		Send(stream);
	}
	if (s_HelloSent && s_TheirDescription && !s_Agreed && !s_Stopped) {
		const std::string mine = MatchDescription();
		if (mine == *s_TheirDescription) {
			s_Agreed = true;
			g_ConsoleMan.PrintString("ONLINE: both players are set up for " + mine + "; starting.");
		} else {
			Stop("the other player is set up for a different match (" + *s_TheirDescription + ", here " + mine + ").");
		}
	}
	CompareHashes();
}

bool Lockstep::ReadyForSimUpdate() {
	if (!s_Active) {
		return true;
	}
	bool ready = s_Agreed && !s_Stopped;
	if (ready && s_SimUpdate >= c_InputDelay) {
		auto inputs = s_Inputs.find(s_SimUpdate);
		for (int player = 0; ready && player < c_MaxPlayers; ++player) {
			ready = inputs != s_Inputs.end() && inputs->second[player].has_value();
		}
	}
	if (!ready && s_OtherLeft && !s_Stopped) {
		Stop("the other player left the match.");
	}
	if (!ready) {
		auto now = std::chrono::steady_clock::now();
		if (!s_Stopped && now - s_LastWaitNote > std::chrono::seconds(3)) {
			s_LastWaitNote = now;
			g_ConsoleMan.PrintString(s_Agreed ? std::format("ONLINE: waiting for the other player's input for sim update {}.", s_SimUpdate) : "ONLINE: waiting for the other player.");
		}
		if (System::IsInHeadlessMode()) {
			// (Headless, the game loop doesn't wait for anything: without this it would spin a core flat out while waiting.)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	return ready;
}

void Lockstep::BeginSimUpdate() {
	if (!s_Active) {
		return;
	}
	// This machine's player's input, for the update the input delay ahead.
	const uint32_t sendFor = s_SimUpdate + c_InputDelay;
	UInputMan::LockstepInput input = std::getenv("CCCP_LOCKSTEP_TEST_INPUT") ? TestInput() : g_UInputMan.SampleLockstepInput(0);
	s_Inputs[sendFor][s_LocalPlayer] = input;
	RakNet::BitStream stream;
	stream.Write(static_cast<RakNet::MessageID>(ID_LOCKSTEP_INPUT));
	stream.Write(sendFor);
	stream.Write(static_cast<uint8_t>(s_LocalPlayer));
	WriteInput(stream, input);
	Send(stream);

	// Every player's input for this update (none for the first few, before anything sent could arrive).
	static const UInputMan::LockstepInput c_NoInput;
	auto inputs = s_Inputs.find(s_SimUpdate);
	for (int player = 0; player < c_MaxPlayers; ++player) {
		const UInputMan::LockstepInput* playerInput = (inputs != s_Inputs.end() && inputs->second[player]) ? &*inputs->second[player] : &c_NoInput;
		g_UInputMan.SetLockstepInput(player, playerInput, player == s_LocalPlayer ? 0 : -1);
	}
}

void Lockstep::EndSimUpdate() {
	if (!s_Active) {
		return;
	}
	s_Inputs.erase(s_Inputs.begin(), s_Inputs.upper_bound(s_SimUpdate));
	if (s_SimUpdate % c_HashInterval == 0) {
		const uint64_t hash = DeterminismChecker::GetRunningHash();
		s_LocalHashes[s_SimUpdate] = hash;
		RakNet::BitStream stream;
		stream.Write(static_cast<RakNet::MessageID>(ID_LOCKSTEP_HASH));
		stream.Write(s_SimUpdate);
		stream.Write(hash);
		Send(stream);
		CompareHashes();
	}
	++s_SimUpdate;
}

void Lockstep::Shutdown() {
	if (s_Peer) {
		RakNet::BitStream stream;
		stream.Write(static_cast<RakNet::MessageID>(ID_LOCKSTEP_BYE));
		Send(stream);
		s_Peer->Shutdown(300);
		RakNet::RakPeerInterface::DestroyInstance(s_Peer);
		s_Peer = nullptr;
	}
}
