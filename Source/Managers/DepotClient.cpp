// The game's line to Cortex Depot (the Commander Hub site): HTTP requests on one worker thread, their answers handed back to the main
// thread each frame. Windows talks HTTP through WinHTTP, which comes with Windows; everywhere else through libcurl.

#include "DepotClient.h"
#include "GameVersion.h"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <iterator>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <curl/curl.h>
#endif

using namespace RTE;

std::string DepotClient::s_Address;

namespace {
	constexpr long c_ConnectTimeoutSeconds = 10;
	constexpr long c_StallTimeoutSeconds = 30; //!< A request that gets nothing for this long is given up.

	std::string UserAgent() { return std::string("CortexCommander/") + c_VersionString; }

	struct Job {
		std::function<void()> Work;
		std::function<void()> Then;
		bool Dropped = false;
	};

	/// The worker thread and the queues either side of it.
	struct Worker {
		std::mutex Lock;
		std::condition_variable Wake;
		std::deque<std::shared_ptr<Job>> Waiting;
		std::deque<std::shared_ptr<Job>> Finished;
		std::shared_ptr<Job> Running;
		std::thread Thread;
		bool Stopping = false;

		void Loop() {
			for (;;) {
				std::shared_ptr<Job> job;
				{
					std::unique_lock<std::mutex> lock(Lock);
					Wake.wait(lock, [this] { return Stopping || !Waiting.empty(); });
					if (Stopping) {
						return;
					}
					job = Waiting.front();
					Waiting.pop_front();
					Running = job;
				}
				job->Work();
				std::lock_guard<std::mutex> lock(Lock);
				Running.reset();
				if (!job->Dropped) {
					Finished.push_back(job);
				}
			}
		}

		void Add(std::shared_ptr<Job> job) {
			std::lock_guard<std::mutex> lock(Lock);
			if (!Thread.joinable()) {
				Stopping = false;
				Thread = std::thread([this] { Loop(); });
			}
			Waiting.push_back(std::move(job));
			Wake.notify_one();
		}
	};
	// Never destroyed: a request still running when the game quits is left to the end of the process rather than waited for.
	Worker& s_Worker = *new Worker;
	Worker& s_Downloader = *new Worker; //!< Downloads, which can take a while, so browsing doesn't queue behind them.

	std::string Lowered(std::string text) {
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	/// Whether a host name is this computer or on the local network: localhost, a private IPv4 address, a .local or .lan name, or a
	/// bare machine name with no dots.
	bool LocalHost(const std::string& host) {
		if (host == "localhost" || host.ends_with(".local") || host.ends_with(".lan") || host.ends_with(".home.arpa") || host == "[::1]") {
			return true;
		}
		int a = -1, b = -1, c = -1, d = -1;
		char tail = 0;
		if (std::sscanf(host.c_str(), "%d.%d.%d.%d%c", &a, &b, &c, &d, &tail) == 4) {
			return a == 127 || a == 10 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31) || (a == 169 && b == 254);
		}
		return host.find('.') == std::string::npos && host.find(':') == std::string::npos && !host.empty();
	}

	/// Splits "scheme://host:port/path" into its scheme (lowered) and host (lowered, no port). False when it isn't a URL like that.
	bool SplitUrl(const std::string& url, std::string& scheme, std::string& host) {
		size_t schemeEnd = url.find("://");
		if (schemeEnd == std::string::npos) {
			return false;
		}
		scheme = Lowered(url.substr(0, schemeEnd));
		size_t hostStart = schemeEnd + 3;
		size_t hostEnd = url.find_first_of("/?#", hostStart);
		std::string authority = url.substr(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart);
		if (authority.find('@') != std::string::npos) {
			return false; // No user names or passwords in the address.
		}
		if (!authority.empty() && authority.front() == '[') {
			host = Lowered(authority.substr(0, authority.find(']') + 1));
		} else {
			host = Lowered(authority.substr(0, authority.find(':')));
		}
		return !host.empty();
	}

	/// The "scheme://host:port" a URL starts with, for messages.
	std::string HostOf(const std::string& url) {
		size_t schemeEnd = url.find("://");
		return schemeEnd == std::string::npos ? url : url.substr(0, url.find_first_of("/?#", schemeEnd + 3));
	}

#ifdef _WIN32
	std::wstring Wide(const std::string& text) {
		if (text.empty()) {
			return std::wstring();
		}
		int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
		std::wstring wide(length, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
		return wide;
	}

	struct InternetHandle {
		HINTERNET Handle = nullptr;
		explicit InternetHandle(HINTERNET handle) : Handle(handle) {}
		~InternetHandle() {
			if (Handle) {
				WinHttpCloseHandle(Handle);
			}
		}
	};

	DepotClient::Response FetchNow(const std::string& url, size_t maxBytes, const std::string& toFile, DepotClient::Progress* progress, const std::string& method, const std::string& json, const std::string& token) {
		DepotClient::Response response;
		std::wstring wideUrl = Wide(url);
		URL_COMPONENTS parts{};
		parts.dwStructSize = sizeof(parts);
		wchar_t hostName[256] = {};
		wchar_t urlPath[4096] = {};
		parts.lpszHostName = hostName;
		parts.dwHostNameLength = static_cast<DWORD>(std::size(hostName));
		parts.lpszUrlPath = urlPath;
		parts.dwUrlPathLength = static_cast<DWORD>(std::size(urlPath));
		wchar_t extra[4096] = {};
		parts.lpszExtraInfo = extra;
		parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
		if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
			response.Error = "That address isn't a web address.";
			return response;
		}
		InternetHandle session(WinHttpOpen(Wide(UserAgent()).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
		if (!session.Handle) {
			response.Error = "Couldn't start a web connection.";
			return response;
		}
		WinHttpSetTimeouts(session.Handle, c_ConnectTimeoutSeconds * 1000, c_ConnectTimeoutSeconds * 1000, c_StallTimeoutSeconds * 1000, c_StallTimeoutSeconds * 1000);
		InternetHandle connection(WinHttpConnect(session.Handle, hostName, parts.nPort, 0));
		std::wstring path = std::wstring(urlPath) + extra;
		InternetHandle request(connection.Handle ? WinHttpOpenRequest(connection.Handle, Wide(method).c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr);
		std::wstring headers;
		if (!json.empty()) {
			headers += L"Content-Type: application/json\r\n";
		}
		if (!token.empty()) {
			headers += L"Authorization: Bearer " + Wide(token) + L"\r\n";
		}
		DWORD bodySize = static_cast<DWORD>(json.size());
		void* body = json.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(json.data());
		if (!request.Handle || !WinHttpSendRequest(request.Handle, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), headers.empty() ? 0 : static_cast<DWORD>(-1L), body, bodySize, bodySize, 0) || !WinHttpReceiveResponse(request.Handle, nullptr)) {
			response.Error = "Couldn't reach the Depot at " + HostOf(url) + ". Is it running?";
			return response;
		}
		DWORD status = 0;
		DWORD statusSize = sizeof(status);
		WinHttpQueryHeaders(request.Handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
		response.Status = static_cast<int>(status);
		if (progress) {
			wchar_t lengthText[32] = {};
			DWORD lengthSize = sizeof(lengthText);
			if (WinHttpQueryHeaders(request.Handle, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &lengthSize, WINHTTP_NO_HEADER_INDEX)) {
				progress->Total = _wtoi64(lengthText);
			}
		}
		FILE* file = nullptr;
		if (!toFile.empty() && response.Ok()) {
			file = _wfopen(Wide(toFile).c_str(), L"wb");
			if (!file) {
				response.Status = 0;
				response.Error = "Couldn't write " + toFile + ".";
				return response;
			}
		}
		size_t total = 0;
		std::vector<char> buffer(64 * 1024);
		for (;;) {
			if (progress && progress->Cancel) {
				response.Status = 0;
				response.Error = "Cancelled.";
				break;
			}
			DWORD read = 0;
			if (!WinHttpReadData(request.Handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
				response.Status = 0;
				response.Error = "The connection to the Depot dropped.";
				break;
			}
			if (read == 0) {
				break;
			}
			total += read;
			if (total > maxBytes) {
				response.Status = 0;
				response.Error = "The Depot sent more than expected.";
				break;
			}
			if (file) {
				if (std::fwrite(buffer.data(), 1, read, file) != read) {
					response.Status = 0;
					response.Error = "Couldn't write " + toFile + " (is the disk full?).";
					break;
				}
			} else {
				response.Body.append(buffer.data(), read);
			}
			if (progress) {
				progress->Got = static_cast<long long>(total);
			}
		}
		if (file) {
			std::fclose(file);
		}
		return response;
	}
#else
	struct Sink {
		std::string* Body = nullptr;
		FILE* File = nullptr;
		size_t Total = 0;
		size_t Max = 0;
		bool TooBig = false;
		bool WriteFailed = false;
	};

	size_t Write(char* data, size_t size, size_t count, void* user) {
		Sink& sink = *static_cast<Sink*>(user);
		size_t bytes = size * count;
		sink.Total += bytes;
		if (sink.Total > sink.Max) {
			sink.TooBig = true;
			return 0;
		}
		if (sink.File) {
			if (std::fwrite(data, 1, bytes, sink.File) != bytes) {
				sink.WriteFailed = true;
				return 0;
			}
		} else {
			sink.Body->append(data, bytes);
		}
		return bytes;
	}

	int Report(void* user, curl_off_t total, curl_off_t got, curl_off_t, curl_off_t) {
		if (DepotClient::Progress* progress = static_cast<DepotClient::Progress*>(user)) {
			progress->Total = static_cast<long long>(total);
			progress->Got = static_cast<long long>(got);
			return progress->Cancel ? 1 : 0;
		}
		return 0;
	}

	DepotClient::Response FetchNow(const std::string& url, size_t maxBytes, const std::string& toFile, DepotClient::Progress* progress, const std::string& method, const std::string& json, const std::string& token) {
		static std::once_flag curlReady;
		std::call_once(curlReady, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

		DepotClient::Response response;
		CURL* curl = curl_easy_init();
		if (!curl) {
			response.Error = "Couldn't start a web connection.";
			return response;
		}
		Sink sink;
		sink.Body = &response.Body;
		sink.Max = maxBytes;
		if (!toFile.empty()) {
			sink.File = std::fopen(toFile.c_str(), "wb");
			if (!sink.File) {
				curl_easy_cleanup(curl);
				response.Error = "Couldn't write " + toFile + ".";
				return response;
			}
		}
		std::string agent = UserAgent();
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_USERAGENT, agent.c_str());
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
		curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, c_ConnectTimeoutSeconds);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, c_StallTimeoutSeconds);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Write);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, progress ? 0L : 1L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Report);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, progress);
		curl_slist* headers = nullptr;
		if (!json.empty()) {
			headers = curl_slist_append(headers, "Content-Type: application/json");
		}
		std::string authorization = "Authorization: Bearer " + token;
		if (!token.empty()) {
			// (curl keeps a header like this to the host it was meant for, should a redirect lead elsewhere.)
			headers = curl_slist_append(headers, authorization.c_str());
		}
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		if (method == "POST" || !json.empty()) {
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.data());
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(json.size()));
		}
		if (method != "GET" && method != "POST") {
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
		}
		CURLcode result = curl_easy_perform(curl);
		curl_slist_free_all(headers);
		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		curl_easy_cleanup(curl);
		if (sink.File) {
			std::fclose(sink.File);
		}
		if (result == CURLE_OK) {
			response.Status = static_cast<int>(status);
		} else if (sink.TooBig) {
			response.Error = "The Depot sent more than expected.";
		} else if (sink.WriteFailed) {
			response.Error = "Couldn't write " + toFile + " (is the disk full?).";
		} else if (result == CURLE_ABORTED_BY_CALLBACK) {
			response.Error = "Cancelled.";
		} else if (result == CURLE_COULDNT_CONNECT || result == CURLE_COULDNT_RESOLVE_HOST || result == CURLE_OPERATION_TIMEDOUT) {
			response.Error = "Couldn't reach the Depot at " + HostOf(url) + ". Is it running?";
		} else {
			response.Error = std::string("The connection to the Depot failed: ") + curl_easy_strerror(result) + ".";
		}
		return response;
	}
#endif
} // namespace

bool DepotClient::AddressAllowed(const std::string& address, std::string& why) {
	std::string scheme;
	std::string host;
	if (!SplitUrl(address, scheme, host) || (scheme != "http" && scheme != "https")) {
		why = "Give the Depot's address with http:// or https:// at the front, e.g. http://192.168.1.20:3000";
		return false;
	}
	// Plain http can be read and changed by anyone between here and the server, so it's only for this computer and the local network.
	if (scheme == "http" && !LocalHost(host)) {
		why = "Plain http:// only works for a Depot on this computer or your local network. Use https:// for one on the internet.";
		return false;
	}
	return true;
}

std::string DepotClient::Absolute(const std::string& url) {
	if (url.find("://") != std::string::npos) {
		return url;
	}
	std::string base = s_Address;
	while (!base.empty() && base.back() == '/') {
		base.pop_back();
	}
	return base + (url.empty() || url.front() != '/' ? "/" : "") + url;
}

std::string DepotClient::Encode(const std::string& value) {
	static const char* hex = "0123456789ABCDEF";
	std::string encoded;
	for (unsigned char c: value) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			encoded += static_cast<char>(c);
		} else {
			encoded += '%';
			encoded += hex[c >> 4];
			encoded += hex[c & 15];
		}
	}
	return encoded;
}

DepotClient::Response DepotClient::Fetch(const std::string& url, size_t maxBytes, const std::string& toFile, Progress* progress, const std::string& postJson) {
	std::string absolute = Absolute(url);
	std::string why;
	if (!AddressAllowed(absolute, why)) {
		Response response;
		response.Error = why;
		return response;
	}
	return FetchNow(absolute, maxBytes, toFile, progress, postJson.empty() ? "GET" : "POST", postJson, "");
}

DepotClient::Response DepotClient::Send(const Request& request) {
	std::string absolute = Absolute(request.Url);
	std::string why;
	if (!AddressAllowed(absolute, why)) {
		Response response;
		response.Error = why;
		return response;
	}
	return FetchNow(absolute, request.MaxBytes, "", nullptr, request.Method, request.Json, request.Token);
}

void DepotClient::Send(Request request, std::function<void(Response&)> done) {
	request.Url = Absolute(request.Url);
	auto response = std::make_shared<Response>();
	Run([request, response] { *response = Send(request); }, [done, response] { done(*response); }, true);
}

void DepotClient::Get(const std::string& url, std::function<void(Response&)> done, size_t maxBytes) {
	auto response = std::make_shared<Response>();
	// (Made absolute now: the address may change before the request runs.)
	Run([absolute = Absolute(url), maxBytes, response] { *response = Fetch(absolute, maxBytes); }, [done, response] { done(*response); });
}

void DepotClient::Post(const std::string& url, const std::string& json, std::function<void(Response&)> done, size_t maxBytes) {
	auto response = std::make_shared<Response>();
	Run([absolute = Absolute(url), json, maxBytes, response] { *response = Fetch(absolute, maxBytes, "", nullptr, json); }, [done, response] { done(*response); });
}

void DepotClient::Run(std::function<void()> work, std::function<void()> then, bool download) {
	auto job = std::make_shared<Job>();
	job->Work = std::move(work);
	job->Then = std::move(then);
	(download ? s_Downloader : s_Worker).Add(std::move(job));
}

void DepotClient::Update() {
	for (Worker* worker: {&s_Worker, &s_Downloader}) {
		std::deque<std::shared_ptr<Job>> finished;
		{
			std::lock_guard<std::mutex> lock(worker->Lock);
			finished.swap(worker->Finished);
		}
		for (const std::shared_ptr<Job>& job: finished) {
			if (job->Then) {
				job->Then();
			}
		}
	}
}

void DepotClient::CancelWaiting() {
	std::lock_guard<std::mutex> lock(s_Worker.Lock);
	s_Worker.Waiting.clear();
	s_Worker.Finished.clear();
	if (s_Worker.Running) {
		s_Worker.Running->Dropped = true;
	}
}

void DepotClient::Shutdown() {
	CancelWaiting();
	for (Worker* worker: {&s_Worker, &s_Downloader}) {
		std::lock_guard<std::mutex> lock(worker->Lock);
		worker->Waiting.clear();
		worker->Stopping = true;
		worker->Wake.notify_all();
		if (worker->Thread.joinable()) {
			worker->Thread.detach(); // Its request gives up on its own timeouts; nothing waits for it.
		}
	}
}
