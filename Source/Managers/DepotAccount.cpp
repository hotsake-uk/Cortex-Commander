// Signing in to Cortex Depot from the game, the device-code way (RFC 8628, as better-auth's deviceAuthorization plugin serves it on the
// hub): POST /api/auth/device/code for a code, show it, and POST /api/auth/device/token every few seconds until the player has approved it
// on the site's /device page. What comes back is a session token, sent as "Authorization: Bearer" to what needs signing in to (/api/v1/me,
// likes, comments).

#include "DepotAccount.h"
#include "DepotClient.h"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")
#endif

using namespace RTE;
using Json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
	constexpr const char* c_SessionFile = "Userdata/Depot/Session.dat";
	constexpr const char* c_SessionHeader = "CortexDepotSession1";
	constexpr const char* c_ClientID = "cortex-commander"; //!< The hub's name for the game (GAME_CLIENT_ID), which only it may sign in as.

	struct Account {
		bool Read = false; //!< Whether the session kept has been read for the address now set.
		std::string ReadFor; //!< The address it was read for.
		std::string Token;
		std::string Name;
		bool Checked = false; //!< Whether the Depot has said the token is good.
		std::string Problem;

		bool SigningIn = false;
		bool Asking = false; //!< A request about signing in is on its way.
		std::string DeviceCode;
		std::string Code;
		double NextPoll = 0.0;
		double Interval = 5.0;
		double ExpiresAt = 0.0;
		int Generation = 0; //!< Raised when signing in stops, so answers still arriving for it are dropped.
	};
	Account s_Account;

	double Now() { return static_cast<double>(SDL_GetTicks()) / 1000.0; }

	std::string Str(const Json& object, const char* key) {
		auto found = object.find(key);
		return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
	}

#ifdef _WIN32
	/// Seals text to this Windows user with DPAPI, so the file is no use copied elsewhere.
	std::string Seal(const std::string& text) {
		DATA_BLOB in{static_cast<DWORD>(text.size()), reinterpret_cast<BYTE*>(const_cast<char*>(text.data()))};
		DATA_BLOB out{};
		if (!CryptProtectData(&in, L"Cortex Depot session", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
			return "";
		}
		std::string sealed(reinterpret_cast<char*>(out.pbData), out.cbData);
		LocalFree(out.pbData);
		return sealed;
	}

	std::string Unseal(const std::string& sealed) {
		DATA_BLOB in{static_cast<DWORD>(sealed.size()), reinterpret_cast<BYTE*>(const_cast<char*>(sealed.data()))};
		DATA_BLOB out{};
		if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
			return "";
		}
		std::string text(reinterpret_cast<char*>(out.pbData), out.cbData);
		SecureZeroMemory(out.pbData, out.cbData);
		LocalFree(out.pbData);
		return text;
	}
#else
	// Elsewhere the file is made readable by its owner alone.
	std::string Seal(const std::string& text) { return text; }
	std::string Unseal(const std::string& sealed) { return sealed; }
#endif

	void ForgetSessionFile() {
		std::error_code error;
		fs::remove(c_SessionFile, error);
	}

	void KeepSession(const std::string& address, const std::string& token) {
		std::string sealed = Seal(std::string(c_SessionHeader) + "\n" + address + "\n" + token);
		if (sealed.empty()) {
			return;
		}
		std::error_code error;
		fs::create_directories(fs::path(c_SessionFile).parent_path(), error);
		// Emptied and closed to the world before the token goes in.
		std::ofstream(c_SessionFile, std::ios::binary | std::ios::trunc).close();
		fs::permissions(c_SessionFile, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, error);
		std::ofstream(c_SessionFile, std::ios::binary | std::ios::trunc).write(sealed.data(), static_cast<std::streamsize>(sealed.size()));
	}

	/// The token kept for an address, or empty.
	std::string KeptSession(const std::string& address) {
		std::ifstream stream(c_SessionFile, std::ios::binary);
		if (!stream) {
			return "";
		}
		std::string text = Unseal(std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()));
		size_t first = text.find('\n');
		size_t second = first == std::string::npos ? std::string::npos : text.find('\n', first + 1);
		if (second == std::string::npos || text.substr(0, first) != c_SessionHeader || text.substr(first + 1, second - first - 1) != address) {
			return "";
		}
		return text.substr(second + 1);
	}

	/// Asks the Depot who the token is for. A token it turns down is forgotten.
	void CheckSession() {
		DepotClient::Request request;
		request.Url = "/api/v1/me";
		request.Token = s_Account.Token;
		std::string token = s_Account.Token;
		DepotClient::Send(request, [token](DepotClient::Response& response) {
			if (token != s_Account.Token) {
				return;
			}
			Json me = Json::parse(response.Body, nullptr, false);
			if (response.Ok() && me.is_object()) {
				s_Account.Checked = true;
				s_Account.Name = Str(me, "name").empty() ? Str(me, "username") : Str(me, "name");
			} else if (response.Status == 401) {
				s_Account.Token.clear();
				s_Account.Name.clear();
				s_Account.Problem = "Your Depot session has ended. Sign in again to like and comment.";
				ForgetSessionFile();
			}
			// (No answer at all: the token is kept, and checked when the screen next opens.)
		});
	}

	void StopSigningIn(const std::string& problem) {
		s_Account.SigningIn = false;
		s_Account.Asking = false;
		s_Account.DeviceCode.clear();
		s_Account.Code.clear();
		s_Account.Problem = problem;
		++s_Account.Generation;
	}

	/// Asks whether the code has been approved yet.
	void Poll() {
		s_Account.Asking = true;
		DepotClient::Request request;
		request.Method = "POST";
		request.Url = "/api/auth/device/token";
		request.Json = Json({{"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}, {"device_code", s_Account.DeviceCode}, {"client_id", c_ClientID}}).dump();
		int generation = s_Account.Generation;
		std::string address = DepotClient::s_Address;
		DepotClient::Send(request, [generation, address](DepotClient::Response& response) {
			if (generation != s_Account.Generation) {
				return;
			}
			s_Account.Asking = false;
			Json answer = Json::parse(response.Body, nullptr, false);
			std::string token = answer.is_object() ? Str(answer, "access_token") : "";
			if (response.Ok() && !token.empty()) {
				StopSigningIn("");
				s_Account.Token = token;
				s_Account.Checked = false;
				KeepSession(address, token);
				CheckSession();
				return;
			}
			std::string error = answer.is_object() ? Str(answer, "error") : "";
			if (error == "authorization_pending" || (response.Status == 0 && error.empty())) {
				// Not yet, or the Depot didn't answer this time: ask again in a bit.
				s_Account.NextPoll = Now() + s_Account.Interval;
			} else if (error == "slow_down") {
				s_Account.Interval += 5.0;
				s_Account.NextPoll = Now() + s_Account.Interval;
			} else if (error == "access_denied") {
				StopSigningIn("Signing in was turned down on the Depot's site.");
			} else if (error == "expired_token") {
				StopSigningIn("The code ran out before it was approved. Try again.");
			} else {
				std::string why = answer.is_object() ? Str(answer, "error_description") : "";
				StopSigningIn(!why.empty() ? why : !response.Error.empty() ? response.Error : "Signing in didn't work (" + std::to_string(response.Status) + ").");
			}
		});
	}
} // namespace

bool DepotAccount::SignedIn() {
	return !s_Account.Token.empty() && s_Account.Checked;
}

const std::string& DepotAccount::Name() {
	return s_Account.Name;
}

const std::string& DepotAccount::Token() {
	return s_Account.Token;
}

void DepotAccount::StartSignIn() {
	if (s_Account.SigningIn || DepotClient::s_Address.empty()) {
		return;
	}
	StopSigningIn("");
	s_Account.SigningIn = true;
	s_Account.Asking = true;
	s_Account.Interval = 5.0;
	DepotClient::Request request;
	request.Method = "POST";
	request.Url = "/api/auth/device/code";
	request.Json = Json({{"client_id", c_ClientID}}).dump();
	int generation = s_Account.Generation;
	DepotClient::Send(request, [generation](DepotClient::Response& response) {
		if (generation != s_Account.Generation) {
			return;
		}
		s_Account.Asking = false;
		Json answer = Json::parse(response.Body, nullptr, false);
		if (!response.Ok() || !answer.is_object() || Str(answer, "device_code").empty() || Str(answer, "user_code").empty()) {
			std::string why = answer.is_object() ? Str(answer, "error_description") : "";
			StopSigningIn(!why.empty() ? why : !response.Error.empty() ? response.Error : "The Depot couldn't start signing in (" + std::to_string(response.Status) + ").");
			return;
		}
		s_Account.DeviceCode = Str(answer, "device_code");
		s_Account.Code = Str(answer, "user_code");
		if (answer.contains("interval") && answer["interval"].is_number()) {
			s_Account.Interval = (std::max)(1.0, answer["interval"].get<double>()); // (In brackets: windows.h has a max macro.)
		}
		double expiresIn = answer.contains("expires_in") && answer["expires_in"].is_number() ? answer["expires_in"].get<double>() : 900.0;
		s_Account.ExpiresAt = Now() + expiresIn;
		s_Account.NextPoll = Now() + s_Account.Interval;
	});
}

bool DepotAccount::SigningIn() {
	return s_Account.SigningIn;
}

const std::string& DepotAccount::Code() {
	return s_Account.Code;
}

std::string DepotAccount::SignInPage() {
	// (Built from the address the game uses, not the one the Depot names: that may be its own localhost.)
	return DepotClient::Absolute("/device?user_code=" + DepotClient::Encode(s_Account.Code));
}

const std::string& DepotAccount::Problem() {
	return s_Account.Problem;
}

void DepotAccount::CancelSignIn() {
	StopSigningIn("");
}

void DepotAccount::SignOut() {
	if (!s_Account.Token.empty()) {
		DepotClient::Request request;
		request.Method = "POST";
		request.Url = "/api/auth/sign-out";
		request.Json = "{}";
		request.Token = s_Account.Token;
		DepotClient::Send(request, [](DepotClient::Response&) {});
	}
	s_Account.Token.clear();
	s_Account.Name.clear();
	s_Account.Checked = false;
	s_Account.Problem.clear();
	ForgetSessionFile();
}

void DepotAccount::Update() {
	if (DepotClient::s_Address.empty()) {
		return;
	}
	if (!s_Account.Read || s_Account.ReadFor != DepotClient::s_Address) {
		if (s_Account.ReadFor != DepotClient::s_Address) {
			StopSigningIn("");
		}
		s_Account.Read = true;
		s_Account.ReadFor = DepotClient::s_Address;
		s_Account.Token = KeptSession(DepotClient::s_Address);
		s_Account.Name.clear();
		s_Account.Checked = false;
		if (!s_Account.Token.empty()) {
			CheckSession();
		}
	}
	if (s_Account.SigningIn && !s_Account.Asking && !s_Account.DeviceCode.empty()) {
		if (Now() > s_Account.ExpiresAt) {
			StopSigningIn("The code ran out before it was approved. Try again.");
		} else if (Now() >= s_Account.NextPoll) {
			Poll();
		}
	}
}

void DepotAccount::CheckAgain() {
	if (!SignedIn()) {
		s_Account.Read = false;
	}
}
