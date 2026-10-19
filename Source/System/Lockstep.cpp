#include "Lockstep.h"

#include "ActivityMan.h"
#include "ConsoleMan.h"
#include "DataModule.h"
#include "DeterminismChecker.h"
#include "GameVersion.h"
#include "LoadingScreen.h"
#include "PresetMan.h"
#include "SettingsMan.h"
#include "System.h"
#include "UInputMan.h"
#include "RTETools.h"

#include "BitStream.h"
#include "MessageIdentifiers.h"
#include "RakPeerInterface.h"
// RakNet includes Windows.h, whose macros clash with our method names.
#undef GetClassName

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

using namespace RTE;

namespace {
	enum LockstepMessage : unsigned char {
		ID_LOCKSTEP_MATCH = ID_USER_PACKET_ENUM + 80, //!< From the host: the match, and everything the other machine has to have the same for it.
		ID_LOCKSTEP_READY, //!< From the player who joined: set up for the host's match.
		ID_LOCKSTEP_REFUSE, //!< From the player who joined: can't play the host's match, and why.
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
	bool s_MatchSent = false; //!< Whether the host has sent the match.
	bool s_Agreed = false; //!< Whether both machines are set up for the same match.
	bool s_OtherLeft = false; //!< Whether the other player has left (or the connection was lost).
	bool s_Stopped = false; //!< Whether the match was stopped (a desync, or the other player left): no more sim updates.
	uint32_t s_SimUpdate = 0; //!< The match's next sim update.
	std::map<uint32_t, std::array<std::optional<UInputMan::LockstepInput>, c_MaxPlayers>> s_Inputs; //!< The players' input by sim update.
	std::map<uint32_t, uint64_t> s_LocalHashes; //!< This machine's hashes waiting for the other's.
	std::map<uint32_t, uint64_t> s_OtherHashes; //!< The other machine's hashes waiting for this one's.
	std::chrono::steady_clock::time_point s_LastWaitNote;
	std::mt19937 s_TestInputRandom; //!< For CCCP_LOCKSTEP_TEST_INPUT: made-up input, for test runs with nobody at the keys.

	/// The settings that can't be taken on once the game has started (the data and the Lua states are loaded by then), as "Name = value" lines.
	std::string StartUpSettings() {
		std::istringstream lines(g_SettingsMan.GetOnlineMatchSettingsText());
		std::string found;
		for (std::string line; std::getline(lines, line);) {
			if (const size_t equals = line.find(" = "); line.find_first_not_of('\t') == 1 && equals != std::string::npos && SettingsMan::IsStartUpSetting(line.substr(1, equals - 1))) {
				found += line.substr(1) + "\n";
			}
		}
		return found;
	}

	/// A fingerprint of each loaded module's files, as "Name.rte=hash" lines: the text files' contents (their line endings aside, which a
	/// Windows checkout changes) and the other files' sizes. The player's own Userdata is left out.
	std::string DataFingerprint() {
		static const std::array<std::string_view, 12> c_TextExtensions = {".ini", ".lua", ".txt", ".md", ".json", ".cfg", ".csv", ".xml", ".glsl", ".vert", ".frag", ".shader"};
		std::string fingerprint;
		for (int module = 0; module < g_PresetMan.GetTotalModuleCount(); ++module) {
			const DataModule* dataModule = g_PresetMan.GetDataModule(module);
			if (!dataModule || g_PresetMan.IsModuleUserdata(dataModule->GetFileName())) {
				continue;
			}
			const std::filesystem::path folder = g_PresetMan.GetFullModulePath(dataModule->GetFileName());
			std::vector<std::filesystem::path> files;
			std::error_code error;
			for (std::filesystem::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
				if (it->is_regular_file(error)) {
					files.push_back(it->path());
				}
			}
			std::sort(files.begin(), files.end());
			uint64_t hash = 14695981039346656037ULL;
			auto add = [&hash](const char* bytes, size_t count) {
				for (size_t i = 0; i < count; ++i) {
					hash = (hash ^ static_cast<unsigned char>(bytes[i])) * 1099511628211ULL;
				}
			};
			for (const std::filesystem::path& file: files) {
				const std::string relative = file.lexically_relative(folder).generic_string();
				add(relative.data(), relative.size() + 1);
				std::string extension = file.extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char letter) { return static_cast<char>(std::tolower(letter)); });
				if (std::find(c_TextExtensions.begin(), c_TextExtensions.end(), extension) != c_TextExtensions.end()) {
					std::ifstream stream(file, std::ios::binary);
					std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
					std::erase(contents, '\r');
					add(contents.data(), contents.size());
				} else {
					const std::string size = std::to_string(std::filesystem::file_size(file, error));
					add(size.data(), size.size() + 1);
				}
			}
			fingerprint += std::format("{}={:016x}\n", dataModule->GetFileName(), hash);
		}
		return fingerprint;
	}

	/// Splits "key=value" or "key = value" lines into a map.
	std::map<std::string, std::string> Lines(const std::string& text, const std::string& separator) {
		std::map<std::string, std::string> found;
		std::istringstream lines(text);
		for (std::string line; std::getline(lines, line);) {
			if (const size_t at = line.find(separator); at != std::string::npos) {
				found[line.substr(0, at)] = line.substr(at + separator.size());
			}
		}
		return found;
	}

	/// What differs between the host's data and this machine's, for saying why the match can't start, or empty if nothing does.
	std::string DataDifference(const std::string& theirs, const std::string& mine, const std::string& separator, const std::string& what) {
		const std::map<std::string, std::string> theirLines = Lines(theirs, separator);
		const std::map<std::string, std::string> myLines = Lines(mine, separator);
		for (const auto& [key, value]: theirLines) {
			if (auto found = myLines.find(key); found == myLines.end()) {
				return std::format("the host has {} {}, which isn't here", what, key);
			} else if (found->second != value) {
				return std::format("{} {} differs between the host and here", what, key);
			}
		}
		for (const auto& [key, value]: myLines) {
			if (!theirLines.contains(key)) {
				return std::format("{} {} is here but not on the host", what, key);
			}
		}
		return "";
	}

	/// Says something about the connection on the console, and on the loading screen while the match isn't under way yet.
	void Note(const std::string& text) {
		g_ConsoleMan.PrintString("ONLINE: " + text);
		if (!s_Agreed && !System::IsInHeadlessMode()) {
			LoadingScreen::LoadingSplashProgressReport("Online: " + text, true);
		}
	}

	void Send(RakNet::BitStream& stream) {
		if (s_Peer && s_Other != RakNet::UNASSIGNED_SYSTEM_ADDRESS) {
			s_Peer->Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, s_Other, false);
		}
	}

	void Stop(const std::string& why) {
		if (!s_Stopped) {
			s_Stopped = true;
			Note(why);
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

	/// Writes text of any length (RakString's own writing keeps no more than 65535 characters, and the settings are longer).
	void WriteText(RakNet::BitStream& stream, const std::string& text) {
		stream.Write(static_cast<uint32_t>(text.size()));
		stream.WriteAlignedBytes(reinterpret_cast<const unsigned char*>(text.data()), static_cast<unsigned int>(text.size()));
	}

	bool ReadText(RakNet::BitStream& stream, std::string& text) {
		uint32_t size = 0;
		if (!stream.Read(size) || size > stream.GetNumberOfUnreadBits() / 8) {
			return false;
		}
		text.resize(size);
		return size == 0 || stream.ReadAlignedBytes(reinterpret_cast<unsigned char*>(text.data()), size);
	}

	/// The host sends the match: what to play, the seed, the settings and what the data has to be.
	void SendMatch() {
		// (The host's settings as the one who joins will read them, to the same places.)
		const std::string settings = g_SettingsMan.GetOnlineMatchSettingsText();
		g_SettingsMan.AdoptOnlineMatchSettings(settings, false);
		RakNet::BitStream stream;
		stream.Write(static_cast<RakNet::MessageID>(ID_LOCKSTEP_MATCH));
		for (const std::string& text: {std::string(c_VersionString), g_ActivityMan.GetActivityTypeToLaunch(), g_ActivityMan.GetActivityNameToLaunch(), g_ActivityMan.GetSceneNameToLaunch(), DataFingerprint(), StartUpSettings(), settings}) {
			WriteText(stream, text);
		}
		stream.Write(GetRNGSeedOffset());
		Send(stream);
	}

	/// The player who joined takes on the host's match, or says why not.
	void SetUpHostsMatch(RakNet::Packet* packet) {
		RakNet::BitStream stream(packet->data, packet->length, false);
		stream.IgnoreBytes(1);
		std::array<std::string, 7> texts;
		uint32_t seed = 0;
		bool ok = true;
		for (std::string& text: texts) {
			ok = ok && ReadText(stream, text);
		}
		ok = ok && stream.Read(seed);
		const std::string version = texts[0];
		std::string why;
		if (!ok) {
			why = "its message about the match didn't arrive whole";
		} else if (version != c_VersionString) {
			why = std::format("the host has version {} and this is {}", version, c_VersionString);
		} else if (texts[1].empty()) {
			why = "the host didn't say which activity to play (start it with -activity)";
		} else if (why = DataDifference(texts[4], DataFingerprint(), "=", "the module"); why.empty()) {
			why = DataDifference(texts[5], StartUpSettings(), " = ", "the setting");
			if (why.empty() && !g_SettingsMan.AdoptOnlineMatchSettings(texts[6], true)) {
				why = "the host's settings couldn't be read";
			}
		}
		RakNet::BitStream reply;
		if (!why.empty()) {
			reply.Write(static_cast<RakNet::MessageID>(ID_LOCKSTEP_REFUSE));
			reply.Write(RakNet::RakString(why.c_str()));
			Send(reply);
			Stop("can't play the host's match: " + why + ".");
			return;
		}
		SetRNGSeedOffset(seed);
		g_ActivityMan.SetActivityToLaunch(texts[1], texts[2], texts[3]);
		reply.Write(static_cast<RakNet::MessageID>(ID_LOCKSTEP_READY));
		Send(reply);
		s_Agreed = true;
		Note(std::format("set up for the host's match ({} on {}, seed {}, the host's rules); starting.", texts[2], texts[3], seed));
	}

	void HandlePacket(RakNet::Packet* packet) {
		switch (packet->data[0]) {
			case ID_NEW_INCOMING_CONNECTION:
			case ID_CONNECTION_REQUEST_ACCEPTED:
				s_Other = packet->systemAddress;
				s_Connected = true;
				Note(std::string("connected to ") + packet->systemAddress.ToString(true) + ".");
				break;
			case ID_CONNECTION_ATTEMPT_FAILED:
				Note("couldn't reach the host; trying again.");
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
			case ID_LOCKSTEP_MATCH:
				if (!s_Hosting && !s_Agreed) {
					SetUpHostsMatch(packet);
				}
				break;
			case ID_LOCKSTEP_READY:
				if (s_Hosting && !s_Agreed) {
					s_Agreed = true;
					Note("the other player is set up; starting.");
				}
				break;
			case ID_LOCKSTEP_REFUSE: {
				RakNet::BitStream stream(packet->data, packet->length, false);
				stream.IgnoreBytes(1);
				RakNet::RakString why;
				stream.Read(why);
				Stop(std::string("the other player can't play this match: ") + why.C_String() + ".");
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
			Note(std::format("hosting on port {}, waiting for the other player.", s_Port));
		} else {
			RakNet::SocketDescriptor socket;
			if (s_Peer->Startup(1, &socket, 1) != RakNet::RAKNET_STARTED) {
				Stop("couldn't start the network.");
				return;
			}
			s_Peer->Connect(s_HostAddress.c_str(), s_Port, nullptr, 0);
			Note(std::format("joining {}:{}.", s_HostAddress, s_Port));
		}
		// (A dropped connection is noticed in a few seconds rather than RakNet's default of ten.)
		s_Peer->SetTimeoutTime(5000, RakNet::UNASSIGNED_SYSTEM_ADDRESS);
	}
	for (RakNet::Packet* packet = s_Peer->Receive(); packet; s_Peer->DeallocatePacket(packet), packet = s_Peer->Receive()) {
		HandlePacket(packet);
	}
	// Once connected, the host sends the match.
	if (s_Hosting && s_Connected && !s_MatchSent && !s_Stopped) {
		s_MatchSent = true;
		if (g_ActivityMan.GetActivityNameToLaunch().empty()) {
			Stop("the host has to say which activity to play, with -activity.");
		} else {
			SendMatch();
		}
	}
	CompareHashes();
}

bool Lockstep::SetUpMatch(const std::function<void()>& pollEvents) {
	if (!s_Active) {
		return true;
	}
	while (!s_Agreed && !s_Stopped && !System::IsSetToQuit()) {
		pollEvents();
		Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return s_Agreed;
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
			g_ConsoleMan.PrintString(std::format("ONLINE: waiting for the other player's input for sim update {}.", s_SimUpdate));
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
