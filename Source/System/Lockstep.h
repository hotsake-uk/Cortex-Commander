#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace RTE {

	/// An online match played in lockstep: every machine runs the same simulation, and only the players' input for each sim update goes over the
	/// network. A sim update runs only once every player's input for it has arrived; each machine sends its own player's input a few updates
	/// ahead (the input delay), so on a good connection nothing waits. Every so often the machines compare a hash of their simulation, and stop
	/// the match if they differ. Two players for now: the host is player one, the one who joins is player two.
	class Lockstep {

	public:
		/// Sets this machine up to host a match, from the command line, before the activity starts.
		/// @param port The port to wait for the other player on.
		static void Host(int port);

		/// Sets this machine up to join a match, from the command line, before the activity starts.
		/// @param address Where the host is, as "address:port".
		static void Join(const std::string& address);

		/// Gets whether this is an online match.
		static bool IsActive() { return s_Active; }

		/// Gets the player in the match who plays on this machine.
		static int GetLocalPlayer() { return s_LocalPlayer; }

		/// Gets how many players the match has.
		static int GetPlayerCount() { return 2; }

		/// Sets the match up with the other player, before it starts: waits for them, and then the host tells the one who joined what to play,
		/// with which seed and rules (the host's settings, but for the ones that only change how the game looks, sounds or is controlled), and the
		/// one who joined checks that their game and data are the same. Returns at once outside an online match.
		/// @param pollEvents Keeps the window answering while it waits.
		/// @return Whether the match is set up; if not, the console says why.
		static bool SetUpMatch(const std::function<void()>& pollEvents);

		/// Takes in whatever has arrived from the other player. Once a frame, from the game loop.
		static void Update();

		/// Gets whether the next sim update can run: the players are connected, nothing has gone wrong, and every player's input for it is here.
		static bool ReadyForSimUpdate();

		/// Starts a sim update, after the input manager's update: sends this machine's player's input for a later update, and sets every
		/// player's input for this one.
		static void BeginSimUpdate();

		/// Ends a sim update: every so often sends the hash of the simulation, to check against the other machine's.
		static void EndSimUpdate();

		/// Leaves the match, telling the other player.
		static void Shutdown();

	private:
		static bool s_Active; //!< Whether this is an online match.
		static int s_LocalPlayer; //!< The player who plays on this machine.
	};
} // namespace RTE
