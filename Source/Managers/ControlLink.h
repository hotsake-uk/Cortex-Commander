#pragma once

#include <string>

namespace RTE {

	/// A link for a companion program on this computer (the Workbench) to watch and steer the running game: read its state, change settings, run console lines, take screenshots.
	/// It is off unless asked for: the CCCP_CONTROL_PORT environment variable, or ControlLinkPort in Settings.ini, names the port to listen on. It only accepts connections from this computer.
	/// The other side sends one line of text per command and gets one line back, starting with "ok" or "err". Commands are carried out on the game's main thread, between frames.
	///   ping                      -> ok pong
	///   state                     -> ok {...}   (JSON: scene, game mode, frames per second, counts, time of day, weather...)
	///   set Key = Value           -> ok         (any Settings.ini key; several can be given, separated by \n written as the two characters backslash and n)
	///   dump relative/path.ini    -> ok         (writes all current settings to a file under Instances/ or Userdata/Workbench/)
	///   lua <code>                -> ok | err <message>
	///   shot                      -> ok         (a screenshot into the ScreenShots folder)
	///   quit                      -> ok
	class ControlLink {

	public:
		/// Starts listening, if a port was asked for. Call once, after the settings are loaded.
		static void Start();

		/// Carries out the commands that have come in since the last call. Call once a frame from the main thread, in the menus as well as in play.
		static void Update();

		/// Stops listening and closes any connections.
		static void Stop();

		/// The port to listen on as set in Settings.ini (ControlLinkPort), 0 for none. The environment variable, when set, wins.
		static int s_SettingsPort;

	private:
		static std::string Execute(const std::string& commandLine);
	};
} // namespace RTE
