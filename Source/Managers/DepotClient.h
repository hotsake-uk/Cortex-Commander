#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace RTE {

	/// Talks to Cortex Depot (the Commander Hub site) over HTTP: its JSON API under /api/v1 and the files it hands out.
	/// Requests run one at a time on a worker thread, so the game never waits on the network; what each one finds is handed back to the
	/// main thread by Update, which the screen using it calls each frame.
	class DepotClient {

	public:
		/// What a request got back.
		struct Response {
			int Status = 0; //!< The HTTP status, or 0 when no answer came (Error says why).
			std::string Body; //!< Empty when the body went to a file.
			std::string Error; //!< Why there's no answer, in words for the player.
			bool Ok() const { return Status >= 200 && Status < 300; }
		};

		/// How far a download has got, for a progress bar. Shared between the worker and whoever asked.
		struct Progress {
			std::atomic<long long> Got = 0;
			std::atomic<long long> Total = 0; //!< 0 while unknown.
			std::atomic<bool> Cancel = false; //!< Set to stop the download.
		};

		/// A request to send as it is: any method, a JSON body, and the player's session for what needs signing in to.
		struct Request {
			std::string Method = "GET";
			std::string Url; //!< Absolute, or relative to the hub.
			std::string Json; //!< Sent as the body, as JSON, when set.
			std::string Token; //!< The session token from signing in, sent as "Authorization: Bearer", when set.
			size_t MaxBytes = 4 * 1024 * 1024;
		};

		/// The hub's address, e.g. "http://192.168.1.20:3000", from DepotAddress in Settings.ini. Empty until the player sets it.
		static std::string s_Address;

		/// Whether an address is one the game may talk to: http or https, and plain http only on this computer or the local network.
		/// @param address The address, as the player typed it.
		/// @param why Set to the reason when it isn't.
		static bool AddressAllowed(const std::string& address, std::string& why);

		/// A URL the API gave (possibly relative, like "/api/v1/…"), made absolute against the hub's address.
		static std::string Absolute(const std::string& url);

		/// Percent-encodes a value for a query string.
		static std::string Encode(const std::string& value);

		/// Fetches a URL on the worker thread, then calls done on the main thread (from Update) with what came back.
		/// @param url An absolute URL, or one relative to the hub.
		/// @param done Called once with the response.
		/// @param maxBytes A body any bigger is cut off and reported as an error.
		static void Get(const std::string& url, std::function<void(Response&)> done, size_t maxBytes = 32 * 1024 * 1024);

		/// Posts JSON to a URL on the worker thread, then calls done on the main thread (from Update) with what came back.
		static void Post(const std::string& url, const std::string& json, std::function<void(Response&)> done, size_t maxBytes = 32 * 1024 * 1024);

		/// Runs work on a worker thread (after any work already waiting there), then then on the main thread.
		/// @param download Run it on the downloads' own thread instead: it may take a while, so browsing doesn't wait behind it, and
		/// CancelWaiting leaves it be.
		static void Run(std::function<void()> work, std::function<void()> then, bool download = false);

		/// Fetches a URL right away, on the calling thread. For work already on a worker thread.
		/// @param toFile When set, the body is written to this file instead of kept, and progress reports on it.
		/// @param postJson When set, it's posted as JSON instead of the URL fetched with GET.
		static Response Fetch(const std::string& url, size_t maxBytes, const std::string& toFile = "", Progress* progress = nullptr, const std::string& postJson = "");

		/// Sends a request right away, on the calling thread.
		static Response Send(const Request& request);

		/// Sends a request on the downloads' thread (so CancelWaiting leaves it be), then calls done on the main thread (from Update).
		static void Send(Request request, std::function<void(Response&)> done);

		/// Calls the done of every request that has finished. Call from the main thread, each frame.
		static void Update();

		/// Drops every request still waiting to start, but not downloads; the one running finishes, but its done isn't called.
		static void CancelWaiting();

		/// Stops the worker threads. Called at shutdown.
		static void Shutdown();
	};
} // namespace RTE
