#pragma once

#include <string>

namespace RTE {

	/// The player's account on Cortex Depot (DepotAccount.cpp), for liking and commenting from the game. Signing in is the device-code
	/// way: the game shows a short code, the player approves it on the Depot's site in a web browser (on this computer, or a phone), and
	/// the game is handed a session token. The token is kept in Userdata/Depot/Session.dat, sealed to the Windows user with DPAPI on
	/// Windows and readable only by its owner elsewhere, with the Depot address it's for.
	class DepotAccount {

	public:
		/// Whether the player is signed in to the Depot at the address now set.
		static bool SignedIn();

		/// The name of the player signed in, or empty.
		static const std::string& Name();

		/// The session token for requests that need signing in, or empty.
		static const std::string& Token();

		/// Starts signing in: asks the Depot for a code to show the player.
		static void StartSignIn();

		/// Whether signing in is under way: a code is being asked for, or waits to be approved.
		static bool SigningIn();

		/// The code the player approves on the site, or empty until it arrives.
		static const std::string& Code();

		/// The page of the Depot where the player approves the code, with the code filled in.
		static std::string SignInPage();

		/// Why signing in or checking the session last failed, or empty.
		static const std::string& Problem();

		/// Stops signing in.
		static void CancelSignIn();

		/// Signs out, here and on the Depot.
		static void SignOut();

		/// Reads the session kept from last time and checks it with the Depot, once per address; asks whether a waiting code has been
		/// approved yet. Call each frame while the Depot screen is open.
		static void Update();

		/// Has the session read and checked with the Depot afresh at the next Update: the screen was opened again, or the address changed.
		static void CheckAgain();
	};
} // namespace RTE
