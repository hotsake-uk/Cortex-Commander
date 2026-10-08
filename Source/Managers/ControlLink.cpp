#include "ControlLink.h"

#include "ActivityMan.h"
#include "Actor.h"
#include "Activity.h"
#include "ConsoleMan.h"
#include "FrameMan.h"
#include "LuaMan.h"
#include "MovableMan.h"
#include "PostProcessMan.h"
#include "Reader.h"
#include "Writer.h"
#include "Scene.h"
#include "SceneMan.h"
#include "SettingsMan.h"
#include "System.h"
#include "TimerMan.h"

#include <atomic>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
// Windows renames anything called GetClassName, which the game's own classes have.
#undef GetClassName
using SocketHandle = SOCKET;
static const SocketHandle c_NoSocket = INVALID_SOCKET;
static void CloseSocket(SocketHandle socket) { closesocket(socket); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
static const SocketHandle c_NoSocket = -1;
static void CloseSocket(SocketHandle socket) { close(socket); }
#endif

using namespace RTE;

int ControlLink::s_SettingsPort = 0;

namespace {
	/// One command waiting for the main thread, and where its answer goes.
	struct PendingCommand {
		std::string Line;
		std::string Reply;
		bool Done = false;
		std::mutex Mutex;
		std::condition_variable Ready;
	};

	std::atomic<bool> s_Running = false;
	SocketHandle s_Listener = c_NoSocket;
	std::thread s_AcceptThread;
	std::mutex s_QueueMutex;
	std::deque<std::shared_ptr<PendingCommand>> s_Queue;
	// Frames per second, counted here since this is called once a frame.
	int s_FramesThisSecond = 0;
	int s_FramesPerSecond = 0;
	std::chrono::steady_clock::time_point s_SecondStarted = std::chrono::steady_clock::now();

	std::string JsonText(const std::string& text) {
		std::string out = "\"";
		for (char character: text) {
			if (character == '"' || character == '\\') {
				out += '\\';
				out += character;
			} else if (character == '\n') {
				out += "\\n";
			} else if (static_cast<unsigned char>(character) >= 0x20) {
				out += character;
			}
		}
		return out + "\"";
	}

	/// A reply is one line: line breaks inside it are written as backslash n.
	std::string OneLine(const std::string& text) {
		std::string out;
		for (char character: text) {
			if (character == '\n') {
				out += "\\n";
			} else if (character != '\r') {
				out += character;
			}
		}
		return out;
	}

	std::string Unescape(const std::string& text) {
		std::string out;
		for (size_t i = 0; i < text.size(); ++i) {
			if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n') {
				out += '\n';
				++i;
			} else {
				out += text[i];
			}
		}
		return out;
	}

	/// Serves one connection: read a line, hand it to the main thread, wait for the answer, send it back.
	void ServeClient(SocketHandle client) {
		std::string buffered;
		char chunk[4096];
		while (s_Running) {
			size_t lineEnd = buffered.find('\n');
			if (lineEnd == std::string::npos) {
				int received = recv(client, chunk, sizeof(chunk), 0);
				if (received <= 0 || buffered.size() > 4 * 1024 * 1024) {
					break;
				}
				buffered.append(chunk, received);
				continue;
			}
			std::string line = buffered.substr(0, lineEnd);
			buffered.erase(0, lineEnd + 1);
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (line.empty()) {
				continue;
			}
			auto command = std::make_shared<PendingCommand>();
			command->Line = line;
			{
				std::lock_guard<std::mutex> lock(s_QueueMutex);
				s_Queue.push_back(command);
			}
			std::string reply;
			{
				std::unique_lock<std::mutex> lock(command->Mutex);
				// The main thread answers between frames. If it's stuck (loading, a dialog), say so instead of hanging the other side.
				if (command->Ready.wait_for(lock, std::chrono::seconds(20), [&command] { return command->Done; })) {
					reply = command->Reply;
				} else {
					reply = "err the game did not answer in time (busy loading, or stopped)";
				}
			}
			reply += "\n";
			if (send(client, reply.c_str(), static_cast<int>(reply.size()), 0) <= 0) {
				break;
			}
		}
		CloseSocket(client);
	}

	void AcceptLoop() {
		while (s_Running) {
			SocketHandle client = accept(s_Listener, nullptr, nullptr);
			if (client == c_NoSocket) {
				break;
			}
			std::thread(ServeClient, client).detach();
		}
	}
} // namespace

void ControlLink::Start() {
	int port = s_SettingsPort;
	if (const char* portText = std::getenv("CCCP_CONTROL_PORT")) {
		port = std::atoi(portText);
	}
	if (port <= 0 || port > 65535 || s_Running) {
		return;
	}
#ifdef _WIN32
	WSADATA winsockData;
	if (WSAStartup(MAKEWORD(2, 2), &winsockData) != 0) {
		return;
	}
#endif
	s_Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s_Listener == c_NoSocket) {
		return;
	}
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(static_cast<unsigned short>(port));
	// This computer only: nothing on the network can reach it.
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(s_Listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(s_Listener, 4) != 0) {
		g_ConsoleMan.PrintString("ERROR: The control link could not listen on port " + std::to_string(port) + " (is another copy of the game using it?).");
		CloseSocket(s_Listener);
		s_Listener = c_NoSocket;
		return;
	}
	s_Running = true;
	s_AcceptThread = std::thread(AcceptLoop);
	g_ConsoleMan.PrintString("SYSTEM: Control link listening on this computer, port " + std::to_string(port) + ".");
}

void ControlLink::Stop() {
	if (!s_Running) {
		return;
	}
	s_Running = false;
	CloseSocket(s_Listener);
	s_Listener = c_NoSocket;
	if (s_AcceptThread.joinable()) {
		s_AcceptThread.join();
	}
}

void ControlLink::Update() {
	if (!s_Running) {
		return;
	}
	++s_FramesThisSecond;
	auto now = std::chrono::steady_clock::now();
	if (now - s_SecondStarted >= std::chrono::seconds(1)) {
		s_FramesPerSecond = s_FramesThisSecond;
		s_FramesThisSecond = 0;
		s_SecondStarted = now;
	}
	for (;;) {
		std::shared_ptr<PendingCommand> command;
		{
			std::lock_guard<std::mutex> lock(s_QueueMutex);
			if (s_Queue.empty()) {
				break;
			}
			command = s_Queue.front();
			s_Queue.pop_front();
		}
		std::string reply = Execute(command->Line);
		{
			std::lock_guard<std::mutex> lock(command->Mutex);
			command->Reply = OneLine(reply);
			command->Done = true;
		}
		command->Ready.notify_one();
	}
}

std::string ControlLink::Execute(const std::string& commandLine) {
	size_t space = commandLine.find(' ');
	std::string verb = commandLine.substr(0, space);
	std::string rest = space == std::string::npos ? "" : commandLine.substr(space + 1);

	if (verb == "ping") {
		return "ok pong";
	}
	if (verb == "state") {
		const LightingSettings& lighting = g_PostProcessMan.GetLightingSettings();
		Activity* activity = g_ActivityMan.GetActivity();
		bool inActivity = g_ActivityMan.IsInActivity() && activity;
		std::ostringstream state;
		state << "ok {\"inGame\": " << (inActivity ? "true" : "false")
		      << ", \"scene\": " << JsonText(inActivity && g_SceneMan.GetScene() ? g_SceneMan.GetScene()->GetPresetName() : "")
		      << ", \"activity\": " << JsonText(inActivity ? activity->GetPresetName() : "")
		      << ", \"activityClass\": " << JsonText(inActivity ? activity->GetClassName() : "")
		      << ", \"paused\": " << (inActivity && activity->IsPaused() ? "true" : "false")
		      << ", \"fps\": " << s_FramesPerSecond
		      << ", \"units\": " << (inActivity ? g_MovableMan.GetActorCount() : 0)
		      << ", \"particles\": " << (inActivity ? g_MovableMan.GetParticleCount() : 0)
		      << ", \"timeOfDay\": " << lighting.TimeOfDay
		      << ", \"weatherType\": " << lighting.WeatherType
		      << ", \"weatherIntensity\": " << lighting.WeatherIntensity
		      << ", \"wind\": " << lighting.Wind
		      << ", \"lighting\": " << (lighting.Enabled ? "true" : "false")
		      << ", \"zoom\": " << g_FrameMan.GetCameraZoom()
		      << ", \"simSpeed\": " << g_TimerMan.GetTimeScale()
		      << "}";
		return state.str();
	}
	if (verb == "set") {
		// The same reader that reads Settings.ini, given the lines as though they were the file. Unknown keys are passed over.
		int count = g_SettingsMan.SetFromLines(Unescape(rest));
		if (count == 0) {
			return "err nothing to set: give Key = Value";
		}
		return "ok " + std::to_string(count);
	}
	if (verb == "dump") {
		std::string path = rest;
		bool allowed = (path.rfind("Instances/", 0) == 0 || path.rfind("Userdata/Workbench/", 0) == 0) && path.find("..") == std::string::npos && path.find(':') == std::string::npos;
		if (!allowed) {
			return "err the file must be under Instances/ or Userdata/Workbench/";
		}
		Writer writer(path);
		if (!writer.WriterOK()) {
			return "err could not write " + path;
		}
		g_SettingsMan.Save(writer);
		writer.EndWrite();
		return "ok";
	}
	if (verb == "inspect") {
		// A unit's debug state as JSON, by its unique id; with no id, every inspected unit's (pinned, selected in the sandbox, or player controlled).
		if (!g_ActivityMan.IsInActivity()) {
			return "err not in a game";
		}
		if (!rest.empty()) {
			long id = std::strtol(rest.c_str(), nullptr, 10);
			const Actor* actor = dynamic_cast<const Actor*>(g_MovableMan.FindObjectByUniqueID(id));
			return actor ? "ok " + actor->DescribeDebugState(true) : "err no unit with id " + rest;
		}
		std::string list;
		for (const Actor* actor: g_MovableMan.GetActorList()) {
			if (actor->IsDebugInspected()) {
				list += (list.empty() ? "" : ", ") + actor->DescribeDebugState(true);
			}
		}
		return "ok [" + list + "]";
	}
	if (verb == "lua") {
		LuaStateWrapper& lua = g_LuaMan.GetMasterScriptState();
		if (lua.RunScriptString(Unescape(rest), true) < 0) {
			return "err " + lua.GetLastError();
		}
		return "ok";
	}
	if (verb == "shot") {
		return g_FrameMan.SaveScreenToPNG("Workbench") == 0 ? "ok" : "err the screenshot could not be saved";
	}
	if (verb == "quit") {
		System::SetQuit(true);
		return "ok";
	}
	return "err unknown command: " + verb;
}
