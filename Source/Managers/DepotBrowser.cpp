// The Depot screen: the main menu's window onto Cortex Depot (the Commander Hub site), where mods, maps, game modes, battle setups and
// the rest are shared. It reads the site's public API (/api/v1, see the hub's docs/api.md) through DepotClient, so nothing here waits on
// the network: lists, items and pictures arrive a frame at a time. Pictures come as PNG from /api/v1/png (the site's own are WebP) and
// are kept in Userdata/DepotPreviews so they show at once next time.

#include "DepotBrowser.h"
#include "DepotClient.h"
#include "GameVersion.h"
#include "MenuMan.h"
#include "PresetMan.h"
#include "SettingsMan.h"
#include "ToolWidgets.h"

#include "glad/gl.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <unordered_map>

using namespace RTE;
using Json = nlohmann::json;

namespace {
	constexpr const char* c_PictureFolder = "Userdata/DepotPreviews/";
	constexpr int c_PerPage = 24;
	constexpr int c_CoverWidth = 480; //!< Covers are fetched this wide (or narrower), for the cards and the detail pane.
	constexpr size_t c_MaxPictureBytes = 16 * 1024 * 1024;

	/// The kinds of item on the Depot: the API's name, the site's URL folder and the tab's label.
	struct Kind {
		const char* Name;
		const char* Folder;
		const char* Label;
	};
	constexpr Kind c_Kinds[] = {
	    {"", "browse", "Everything"},
	    {"mod", "mods", "Mods"},
	    {"map", "maps", "Maps"},
	    {"mode", "modes", "Game modes"},
	    {"battle", "battles", "Battle setups"},
	    {"powerup", "powerups", "Powerups"},
	    {"spell", "spells", "Spells"},
	    {"unit", "units", "Units"},
	    {"weapon", "weapons", "Weapons"},
	    {"tool", "tools", "Tools"},
	};

	constexpr const char* c_Sorts[] = {"trending", "new", "updated", "downloads", "likes", "name"};
	constexpr const char* c_SortNames[] = {"Trending", "Newest", "Recently updated", "Most downloaded", "Most liked", "Name"};
	constexpr const char* c_SourceNames[] = {"Shared by players", "Comes with the game", "Both"};

	std::string Str(const Json& object, const char* key) {
		auto found = object.find(key);
		return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
	}

	long long Num(const Json& object, const char* key) {
		auto found = object.find(key);
		return found != object.end() && found->is_number() ? found->get<long long>() : 0;
	}

	std::string KindLabel(const std::string& kind) {
		for (const Kind& entry: c_Kinds) {
			if (kind == entry.Name && *entry.Name) {
				std::string label = entry.Label;
				// "Game modes" → "Game mode": the label for one.
				return label.back() == 's' ? label.substr(0, label.size() - 1) : label;
			}
		}
		return kind;
	}

	std::string Bytes(long long bytes) {
		char text[32];
		if (bytes >= 1024 * 1024) {
			std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
		} else {
			std::snprintf(text, sizeof(text), "%lld KB", std::max(1LL, bytes / 1024));
		}
		return text;
	}

#pragma region Pictures
	/// A picture from the Depot, uploaded for drawing.
	struct Picture {
		enum class State {
			None,
			Queued,
			Loading,
			Ready,
			Failed
		};
		State Now = State::None;
		GLuint Texture = 0;
		int Width = 0;
		int Height = 0;
	};

	/// A picture decoded on the worker thread, waiting to be uploaded.
	struct Decoded {
		std::vector<unsigned char> Pixels; //!< RGBA.
		int Width = 0;
		int Height = 0;
	};

	struct PictureCache {
		std::unordered_map<std::string, Picture> Pictures; //!< By the URL they're fetched from.
		std::deque<std::string> Wanted; //!< Waiting to be fetched, the soonest first.
		bool Fetching = false; //!< One at a time, so lists and items never wait behind a page of pictures.
		int Generation = 0; //!< Raised when the screen closes, so pictures still arriving are dropped.
	};
	PictureCache s_Pictures;

	/// The URL a picture is fetched from: the Depot's PNG of it.
	std::string PictureUrl(const std::string& source, int width, bool pixelArt) {
		return "/api/v1/png?src=" + DepotClient::Encode(source) + (width > 0 ? "&w=" + std::to_string(width) : "") + (pixelArt ? "&pixel=1" : "");
	}

	std::string CacheFile(const std::string& url) {
		char name[32];
		std::snprintf(name, sizeof(name), "%016llx.png", static_cast<unsigned long long>(std::hash<std::string>()(DepotClient::s_Address + url)));
		return std::string(c_PictureFolder) + name;
	}

	bool Decode(const std::string& bytes, Decoded& decoded) {
		SDL_IOStream* stream = SDL_IOFromConstMem(bytes.data(), bytes.size());
		SDL_Surface* loaded = stream ? IMG_Load_IO(stream, true) : nullptr;
		if (!loaded) {
			return false;
		}
		SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
		SDL_DestroySurface(loaded);
		if (!rgba) {
			return false;
		}
		decoded.Width = rgba->w;
		decoded.Height = rgba->h;
		decoded.Pixels.resize(static_cast<size_t>(rgba->w) * rgba->h * 4);
		for (int y = 0; y < rgba->h; ++y) {
			std::memcpy(decoded.Pixels.data() + static_cast<size_t>(y) * rgba->w * 4, static_cast<const unsigned char*>(rgba->pixels) + static_cast<size_t>(y) * rgba->pitch, static_cast<size_t>(rgba->w) * 4);
		}
		SDL_DestroySurface(rgba);
		return true;
	}

	void Upload(Picture& picture, const Decoded& decoded, bool pixelArt) {
		glGenTextures(1, &picture.Texture);
		glBindTexture(GL_TEXTURE_2D, picture.Texture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, decoded.Width, decoded.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, decoded.Pixels.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, pixelArt ? GL_NEAREST : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, pixelArt ? GL_NEAREST : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glBindTexture(GL_TEXTURE_2D, 0);
		picture.Width = decoded.Width;
		picture.Height = decoded.Height;
		picture.Now = Picture::State::Ready;
	}

	/// Starts fetching the next picture wanted, if none is on its way. Pictures already on disk are read from there instead.
	void FetchPictures() {
		if (s_Pictures.Fetching || s_Pictures.Wanted.empty()) {
			return;
		}
		std::string url = s_Pictures.Wanted.front();
		s_Pictures.Wanted.pop_front();
		Picture& picture = s_Pictures.Pictures[url];
		if (picture.Now != Picture::State::Queued) {
			return;
		}
		picture.Now = Picture::State::Loading;
		s_Pictures.Fetching = true;
		bool pixelArt = url.find("&pixel=1") != std::string::npos;
		auto decoded = std::make_shared<Decoded>();
		auto ok = std::make_shared<bool>(false);
		int generation = s_Pictures.Generation;
		DepotClient::Run(
		    [url, decoded, ok] {
			    std::string file = CacheFile(url);
			    std::string bytes;
			    if (std::ifstream cached(file, std::ios::binary); cached) {
				    bytes.assign(std::istreambuf_iterator<char>(cached), std::istreambuf_iterator<char>());
			    }
			    if (bytes.empty() || !Decode(bytes, *decoded)) {
				    DepotClient::Response response = DepotClient::Fetch(url, c_MaxPictureBytes);
				    if (!response.Ok() || !Decode(response.Body, *decoded)) {
					    return;
				    }
				    std::error_code error;
				    std::filesystem::create_directories(c_PictureFolder, error);
				    std::ofstream(file, std::ios::binary).write(response.Body.data(), static_cast<std::streamsize>(response.Body.size()));
			    }
			    *ok = true;
		    },
		    [url, decoded, ok, pixelArt, generation] {
			    s_Pictures.Fetching = false;
			    if (generation != s_Pictures.Generation) {
				    return;
			    }
			    Picture& picture = s_Pictures.Pictures[url];
			    if (*ok) {
				    Upload(picture, *decoded, pixelArt);
			    } else {
				    picture.Now = Picture::State::Failed;
			    }
		    });
	}

	/// The picture at a URL, asked for if it isn't here yet.
	/// @param soon Put it ahead of the others waiting (it's the one picked).
	Picture& Want(const std::string& url, bool soon = false) {
		Picture& picture = s_Pictures.Pictures[url];
		if (picture.Now == Picture::State::None) {
			picture.Now = Picture::State::Queued;
			if (soon) {
				s_Pictures.Wanted.push_front(url);
			} else {
				s_Pictures.Wanted.push_back(url);
			}
		}
		return picture;
	}

	void FreePictures() {
		for (auto& [url, picture]: s_Pictures.Pictures) {
			if (picture.Texture != 0) {
				glDeleteTextures(1, &picture.Texture);
			}
		}
		s_Pictures.Pictures.clear();
		s_Pictures.Wanted.clear();
		s_Pictures.Fetching = false;
		++s_Pictures.Generation;
	}

	/// Draws a picture to fit a box, centred, keeping its shape. Pixel art is scaled by whole numbers when it's smaller than the box.
	/// Draws a dark box, and "Loading" or nothing, while it isn't there.
	void DrawFitted(ImDrawList* drawList, const Picture& picture, ImVec2 from, ImVec2 to, bool pixelArt) {
		drawList->AddRectFilled(from, to, IM_COL32(8, 12, 24, 255));
		float boxWidth = to.x - from.x;
		float boxHeight = to.y - from.y;
		if (picture.Now != Picture::State::Ready || picture.Width <= 0 || picture.Height <= 0) {
			if (picture.Now == Picture::State::Queued || picture.Now == Picture::State::Loading) {
				const char* loading = "Loading";
				ImVec2 size = ImGui::CalcTextSize(loading);
				drawList->AddText(ImVec2(from.x + (boxWidth - size.x) * 0.5F, from.y + (boxHeight - size.y) * 0.5F), IM_COL32(120, 128, 160, 255), loading);
			}
			return;
		}
		float scale = std::min(boxWidth / static_cast<float>(picture.Width), boxHeight / static_cast<float>(picture.Height));
		if (pixelArt && scale > 1.0F) {
			scale = std::floor(scale);
		}
		float width = static_cast<float>(picture.Width) * scale;
		float height = static_cast<float>(picture.Height) * scale;
		ImVec2 at(std::floor(from.x + (boxWidth - width) * 0.5F), std::floor(from.y + (boxHeight - height) * 0.5F));
		drawList->AddImage(static_cast<ImTextureID>(picture.Texture), at, ImVec2(at.x + width, at.y + height));
	}
#pragma endregion

	/// An item as a card shows it.
	struct Card {
		std::string Slug;
		std::string Kind;
		std::string Title;
		std::string Summary;
		std::string Author;
		std::string ModuleID;
		std::string Cover; //!< The picture URL, empty for none.
		bool PixelArt = false;
		bool Official = false;
		long long Downloads = 0;
		long long Likes = 0;
	};

	/// The item picked, as /api/v1/items/{slug} has it.
	struct Detail {
		bool Loading = false;
		std::string Error;
		Json Item;
		bool ContentsAsked = false;
		bool ContentsLoading = false;
		std::string ContentsStatus;
		Json Contents;
	};

	struct Browser {
		bool Open = false;
		bool Back = false;
		bool AskAddress = false; //!< Showing the address box: none is set yet, or the player asked to change it.
		char Address[256] = "";
		std::string AddressProblem;

		int Kind = 0; //!< Index into c_Kinds.
		char Search[96] = "";
		std::string SearchSent; //!< The search the cards are for.
		double SearchTypedAt = -1.0; //!< When the search box last changed, to send it once typing pauses; -1 when sent.
		int Sort = 0;
		int Source = 0; //!< Index into c_SourceNames.
		bool OnlyCompatible = true;
		std::string Tag;

		std::vector<Card> Cards;
		int Total = 0;
		int Page = 0; //!< Pages of cards fetched so far.
		int Pages = 0;
		bool ListLoading = false;
		std::string ListError;
		int ListGeneration = 0; //!< Raised when the filters change, so lists still arriving for the old ones are dropped.
		bool FiltersChanged = true;

		std::string Picked; //!< The slug of the item picked, or empty.
		Detail Picked_;
		int DetailGeneration = 0;
	};
	Browser s_Browser;

	std::string ListUrl(int page) {
		const Kind& kind = c_Kinds[s_Browser.Kind];
		std::string url = "/api/v1/items?perPage=" + std::to_string(c_PerPage) + "&page=" + std::to_string(page) + "&sort=" + c_Sorts[s_Browser.Sort];
		if (*kind.Name) {
			url += std::string("&kind=") + kind.Name;
		}
		if (!s_Browser.SearchSent.empty()) {
			url += "&q=" + DepotClient::Encode(s_Browser.SearchSent);
			// (Searching sorts by best match unless another sort is picked.)
		}
		if (!s_Browser.Tag.empty()) {
			url += "&tag=" + DepotClient::Encode(s_Browser.Tag);
		}
		if (s_Browser.Source == 0) {
			url += "&source=community";
		} else if (s_Browser.Source == 1) {
			url += "&source=official";
		}
		if (s_Browser.OnlyCompatible) {
			url += std::string("&compatibleWith=") + c_VersionString;
		}
		return url;
	}

	void FetchPage(int page) {
		s_Browser.ListLoading = true;
		s_Browser.ListError.clear();
		int generation = s_Browser.ListGeneration;
		DepotClient::Get(ListUrl(page), [generation, page](DepotClient::Response& response) {
			if (generation != s_Browser.ListGeneration) {
				return;
			}
			s_Browser.ListLoading = false;
			Json body = Json::parse(response.Body, nullptr, false);
			if (!response.Ok() || !body.is_object() || !body.contains("items") || !body["items"].is_array()) {
				s_Browser.ListError = !response.Error.empty() ? response.Error : body.is_object() && body.contains("error") ? Str(body["error"], "message") : "The Depot's answer didn't make sense (" + std::to_string(response.Status) + ").";
				return;
			}
			if (page == 1) {
				s_Browser.Cards.clear();
			}
			for (const Json& item: body["items"]) {
				if (!item.is_object()) {
					continue;
				}
				Card card;
				card.Slug = Str(item, "slug");
				card.Kind = Str(item, "kind");
				card.Title = Str(item, "title");
				card.Summary = Str(item, "summary");
				card.ModuleID = Str(item, "moduleId");
				card.Official = item.value("official", false);
				card.Downloads = Num(item, "downloads");
				card.Likes = Num(item, "likes");
				if (item.contains("author") && item["author"].is_object()) {
					card.Author = Str(item["author"], "name");
				}
				if (item.contains("cover") && item["cover"].is_object()) {
					card.Cover = Str(item["cover"], "url");
					card.PixelArt = item["cover"].value("pixelArt", false);
				}
				if (!card.Slug.empty()) {
					s_Browser.Cards.push_back(std::move(card));
				}
			}
			s_Browser.Total = static_cast<int>(Num(body, "total"));
			s_Browser.Pages = static_cast<int>(Num(body, "pages"));
			s_Browser.Page = page;
		});
	}

	void Refresh() {
		++s_Browser.ListGeneration;
		s_Browser.FiltersChanged = false;
		s_Browser.Cards.clear();
		s_Browser.Total = 0;
		s_Browser.Page = 0;
		s_Browser.Pages = 0;
		FetchPage(1);
	}

	void Pick(const std::string& slug) {
		if (slug == s_Browser.Picked) {
			return;
		}
		s_Browser.Picked = slug;
		s_Browser.Picked_ = Detail();
		if (slug.empty()) {
			return;
		}
		s_Browser.Picked_.Loading = true;
		int generation = ++s_Browser.DetailGeneration;
		DepotClient::Get("/api/v1/items/" + DepotClient::Encode(slug), [generation](DepotClient::Response& response) {
			if (generation != s_Browser.DetailGeneration) {
				return;
			}
			Detail& detail = s_Browser.Picked_;
			detail.Loading = false;
			Json body = Json::parse(response.Body, nullptr, false);
			if (!response.Ok() || !body.is_object()) {
				detail.Error = !response.Error.empty() ? response.Error : body.is_object() && body.contains("error") ? Str(body["error"], "message") : "Couldn't read this item (" + std::to_string(response.Status) + ").";
				return;
			}
			detail.Item = std::move(body);
		});
	}

	void FetchContents() {
		Detail& detail = s_Browser.Picked_;
		detail.ContentsAsked = true;
		detail.ContentsLoading = true;
		int generation = s_Browser.DetailGeneration;
		DepotClient::Get("/api/v1/items/" + DepotClient::Encode(s_Browser.Picked) + "/contents", [generation](DepotClient::Response& response) {
			if (generation != s_Browser.DetailGeneration) {
				return;
			}
			Detail& detail = s_Browser.Picked_;
			detail.ContentsLoading = false;
			Json body = Json::parse(response.Body, nullptr, false);
			if (!response.Ok() || !body.is_object()) {
				detail.ContentsStatus = !response.Error.empty() ? response.Error : "Couldn't read what's inside (" + std::to_string(response.Status) + ").";
				return;
			}
			detail.ContentsStatus = Str(body, "status");
			if (body.contains("contents") && body["contents"].is_array()) {
				detail.Contents = std::move(body["contents"]);
			}
		});
	}

	void SaveAddress(const std::string& address) {
		std::string trimmed = address;
		while (!trimmed.empty() && (trimmed.back() == '/' || trimmed.back() == ' ')) {
			trimmed.pop_back();
		}
		while (!trimmed.empty() && trimmed.front() == ' ') {
			trimmed.erase(trimmed.begin());
		}
		if (trimmed.find("://") == std::string::npos && !trimmed.empty()) {
			trimmed = "http://" + trimmed;
		}
		std::string why;
		if (!DepotClient::AddressAllowed(trimmed, why)) {
			s_Browser.AddressProblem = why;
			return;
		}
		DepotClient::CancelWaiting();
		FreePictures();
		DepotClient::s_Address = trimmed;
		g_SettingsMan.UpdateSettingsFile();
		s_Browser.AskAddress = false;
		s_Browser.AddressProblem.clear();
		s_Browser.Picked.clear();
		s_Browser.Picked_ = Detail();
		Refresh();
	}

	/// Opens a page of the Depot in the player's web browser.
	void OpenOnSite(const std::string& path) {
		SDL_OpenURL(DepotClient::Absolute(path).c_str());
	}

	std::string SitePath(const std::string& kind, const std::string& slug) {
		for (const Kind& entry: c_Kinds) {
			if (kind == entry.Name && *entry.Name) {
				return std::string("/") + entry.Folder + "/" + slug;
			}
		}
		return "/browse";
	}

	/// Whether a module is loaded in this game, so the item that is it can be shown as installed.
	bool ModuleInstalled(const std::string& moduleID) {
		return !moduleID.empty() && g_PresetMan.GetModuleID(moduleID) >= 0;
	}

#pragma region Drawing
	void AddressBox() {
		const float font = ImGui::GetFontSize();
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, ToolTheme::Vec(ToolTheme::Gold));
		ImGui::TextUnformatted("Where is your Depot?");
		ImGui::PopStyleColor();
		ImGui::TextWrapped("Type the address of the computer running Commander Hub, as you'd open it in a web browser, e.g. http://192.168.1.20:3000 for another PC on your network, or http://localhost:3000 for this one.");
		ImGui::Spacing();
		ImGui::SetNextItemWidth(font * 24.0F);
		bool enter = ImGui::InputTextWithHint("##depotAddress", "http://192.168.1.20:3000", s_Browser.Address, sizeof(s_Browser.Address), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		if (ToolUI::Button("Connect") || enter) {
			SaveAddress(s_Browser.Address);
		}
		if (!DepotClient::s_Address.empty()) {
			ImGui::SameLine();
			if (ToolUI::Button("Cancel")) {
				s_Browser.AskAddress = false;
				s_Browser.AddressProblem.clear();
			}
		}
		if (!s_Browser.AddressProblem.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.55F, 0.45F, 1.0F));
			ImGui::TextWrapped("%s", s_Browser.AddressProblem.c_str());
			ImGui::PopStyleColor();
		}
	}

	void Filters() {
		const float font = ImGui::GetFontSize();
		for (int i = 0; i < static_cast<int>(std::size(c_Kinds)); ++i) {
			if (i > 0) {
				ImGui::SameLine();
			}
			bool on = s_Browser.Kind == i;
			if (on) {
				ImGui::PushStyleColor(ImGuiCol_Button, ToolTheme::Vec(ToolTheme::WellHover));
				ImGui::PushStyleColor(ImGuiCol_Text, ToolTheme::Vec(ToolTheme::Gold));
			}
			if (ToolUI::Button(c_Kinds[i].Label) && !on) {
				s_Browser.Kind = i;
				s_Browser.Tag.clear();
				s_Browser.FiltersChanged = true;
			}
			if (on) {
				ImGui::PopStyleColor(2);
			}
		}

		ImGui::SetNextItemWidth(font * 16.0F);
		if (ImGui::InputTextWithHint("##depotSearch", "Search the Depot", s_Browser.Search, sizeof(s_Browser.Search))) {
			s_Browser.SearchTypedAt = ImGui::GetTime();
		}
		// Searched once typing pauses, so each letter isn't a request.
		if (s_Browser.SearchTypedAt >= 0.0 && ImGui::GetTime() - s_Browser.SearchTypedAt > 0.4) {
			s_Browser.SearchTypedAt = -1.0;
			if (s_Browser.SearchSent != s_Browser.Search) {
				s_Browser.SearchSent = s_Browser.Search;
				s_Browser.FiltersChanged = true;
			}
		}
		ImGui::SameLine();
		ImGui::TextUnformatted("Sort");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(font * 10.0F);
		if (ImGui::Combo("##depotSort", &s_Browser.Sort, c_SortNames, static_cast<int>(std::size(c_SortNames)))) {
			s_Browser.FiltersChanged = true;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(font * 11.0F);
		if (ImGui::Combo("##depotSource", &s_Browser.Source, c_SourceNames, static_cast<int>(std::size(c_SourceNames)))) {
			s_Browser.FiltersChanged = true;
		}
		ImGui::SetItemTooltip("Shared by players: what people have uploaded. Comes with the game: the game's own content, which you already have.");
		ImGui::SameLine();
		if (ToolUI::Checkbox("Only for this version", &s_Browser.OnlyCompatible)) {
			s_Browser.FiltersChanged = true;
		}
		ImGui::SetItemTooltip("Hides things made for a newer version of the game, or one older than version 6, which load with a warning and may not work fully.");
		if (!s_Browser.Tag.empty()) {
			ImGui::SameLine();
			std::string tag = "Tag: " + s_Browser.Tag + "  (clear)";
			if (ToolUI::Button(tag.c_str())) {
				s_Browser.Tag.clear();
				s_Browser.FiltersChanged = true;
			}
		}
	}

	void Cards(float width, float height) {
		const float font = ImGui::GetFontSize();
		if (!ImGui::BeginChild("##depotCards", ImVec2(width, height), ImGuiChildFlags_None)) {
			ImGui::EndChild();
			return;
		}
		if (!s_Browser.ListError.empty() && s_Browser.Cards.empty()) {
			ImGui::Spacing();
			ImGui::TextWrapped("%s", s_Browser.ListError.c_str());
			if (ToolUI::Button("Try again")) {
				Refresh();
			}
			ImGui::SameLine();
			if (ToolUI::Button("Change the Depot's address")) {
				s_Browser.AskAddress = true;
			}
			ImGui::EndChild();
			return;
		}
		if (s_Browser.Cards.empty()) {
			ImGui::Spacing();
			ImGui::TextDisabled(s_Browser.ListLoading ? "Asking the Depot..." : "Nothing here matches.");
			ImGui::EndChild();
			return;
		}
		const ImGuiStyle& style = ImGui::GetStyle();
		const float room = ImGui::GetContentRegionAvail().x;
		const int columns = std::max(1, static_cast<int>((room + style.ItemSpacing.x) / (font * 15.0F + style.ItemSpacing.x)));
		const float cardWidth = std::floor((room - style.ItemSpacing.x * static_cast<float>(columns - 1)) / static_cast<float>(columns));
		const float pictureHeight = std::floor(cardWidth * 9.0F / 16.0F);
		const float lineHeight = ImGui::GetTextLineHeight();
		const float cardHeight = pictureHeight + lineHeight * 3.0F + style.FramePadding.y * 3.0F;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		for (int i = 0; i < static_cast<int>(s_Browser.Cards.size()); ++i) {
			const Card& card = s_Browser.Cards[i];
			if (i % columns != 0) {
				ImGui::SameLine();
			}
			ImGui::PushID(i);
			ImVec2 from = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("##card", ImVec2(cardWidth, cardHeight))) {
				Pick(card.Slug);
			}
			bool hovered = ImGui::IsItemHovered();
			bool picked = card.Slug == s_Browser.Picked;
			ImVec2 to(from.x + cardWidth, from.y + cardHeight);
			if (ImGui::IsRectVisible(from, to)) {
				drawList->AddRectFilled(from, to, picked ? ToolTheme::WellHover : hovered ? IM_COL32(40, 46, 66, 255) : ToolTheme::Panel);
				ImVec2 pictureFrom(from.x + style.FramePadding.y, from.y + style.FramePadding.y);
				ImVec2 pictureTo(to.x - style.FramePadding.y, pictureFrom.y + pictureHeight);
				if (!card.Cover.empty()) {
					DrawFitted(drawList, Want(PictureUrl(card.Cover, c_CoverWidth, card.PixelArt)), pictureFrom, pictureTo, card.PixelArt);
				} else {
					drawList->AddRectFilled(pictureFrom, pictureTo, IM_COL32(8, 12, 24, 255));
				}
				float textY = pictureTo.y + style.FramePadding.y;
				drawList->PushClipRect(from, to, true);
				drawList->AddText(ImVec2(from.x + style.FramePadding.x, textY), ToolTheme::Gold, card.Title.c_str());
				std::string byline = KindLabel(card.Kind) + (card.Author.empty() ? card.Official ? ", comes with the game" : "" : " by " + card.Author);
				drawList->AddText(ImVec2(from.x + style.FramePadding.x, textY + lineHeight), IM_COL32(190, 196, 220, 255), byline.c_str());
				std::string stats = std::to_string(card.Downloads) + " downloads, " + std::to_string(card.Likes) + " likes";
				drawList->AddText(ImVec2(from.x + style.FramePadding.x, textY + lineHeight * 2.0F), IM_COL32(140, 148, 176, 255), stats.c_str());
				if (ModuleInstalled(card.ModuleID)) {
					const char* badge = "Installed";
					ImVec2 size = ImGui::CalcTextSize(badge);
					ImVec2 at(to.x - size.x - style.FramePadding.x * 2.0F - style.FramePadding.y, from.y + style.FramePadding.y * 2.0F);
					drawList->AddRectFilled(at, ImVec2(at.x + size.x + style.FramePadding.x * 2.0F, at.y + size.y + 2.0F), IM_COL32(0, 0, 0, 190));
					drawList->AddText(ImVec2(at.x + style.FramePadding.x, at.y + 1.0F), IM_COL32(120, 220, 120, 255), badge);
				}
				drawList->PopClipRect();
				drawList->AddRect(from, to, picked ? ToolTheme::Gold : ToolTheme::Edge, 0.0F, 0, picked ? 2.0F : 1.0F);
			}
			ImGui::PopID();
		}
		if (s_Browser.Page < s_Browser.Pages) {
			if (s_Browser.ListLoading) {
				ImGui::TextDisabled("Loading more...");
			} else if (ToolUI::Button("Load more", ImVec2(-1.0F, 0.0F))) {
				FetchPage(s_Browser.Page + 1);
			}
			// More come on their own when the bottom comes into view.
			if (!s_Browser.ListLoading && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - cardHeight) {
				FetchPage(s_Browser.Page + 1);
			}
		}
		if (!s_Browser.ListError.empty()) {
			ImGui::TextWrapped("%s", s_Browser.ListError.c_str());
		}
		ImGui::EndChild();
	}

	/// Markdown, as plain text with headings, lists and links tidied: enough to read it by.
	void Markdown(const std::string& text) {
		auto tidy = [](std::string line) {
			// Links: [words](address) → words.
			for (size_t open = line.find('['); open != std::string::npos; open = line.find('[', open + 1)) {
				size_t close = line.find("](", open);
				size_t end = close == std::string::npos ? std::string::npos : line.find(')', close);
				if (end == std::string::npos) {
					break;
				}
				line = line.substr(0, open) + line.substr(open + 1, close - open - 1) + line.substr(end + 1);
			}
			for (const char* mark: {"**", "__", "`"}) {
				for (size_t at = line.find(mark); at != std::string::npos; at = line.find(mark, at)) {
					line.erase(at, std::strlen(mark));
				}
			}
			return line;
		};
		size_t start = 0;
		bool fence = false;
		while (start <= text.size()) {
			size_t end = text.find('\n', start);
			std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
			start = end == std::string::npos ? text.size() + 1 : end + 1;
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (line.starts_with("```")) {
				fence = !fence;
				continue;
			}
			if (fence) {
				ImGui::TextDisabled("%s", line.c_str());
			} else if (line.starts_with("#")) {
				size_t text = line.find_first_not_of("# ");
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, ToolTheme::Vec(ToolTheme::Gold));
				ImGui::TextWrapped("%s", text == std::string::npos ? "" : tidy(line.substr(text)).c_str());
				ImGui::PopStyleColor();
			} else if (line.starts_with("- ") || line.starts_with("* ")) {
				ImGui::Bullet();
				ImGui::SameLine();
				ImGui::TextWrapped("%s", tidy(line.substr(2)).c_str());
			} else if (line.starts_with("> ")) {
				ImGui::TextDisabled("%s", tidy(line.substr(2)).c_str());
			} else if (line.empty()) {
				ImGui::Spacing();
			} else {
				ImGui::TextWrapped("%s", tidy(line).c_str());
			}
		}
	}

	void Contents(Detail& detail) {
		if (!detail.ContentsAsked) {
			FetchContents();
		}
		if (detail.ContentsLoading) {
			ImGui::TextDisabled("Asking the Depot...");
			return;
		}
		if (detail.ContentsStatus == "pending") {
			ImGui::TextDisabled("The Depot is still reading this upload. Look again in a minute.");
			return;
		}
		if (!detail.Contents.is_array() || detail.Contents.empty()) {
			ImGui::TextDisabled("%s", detail.ContentsStatus.empty() || detail.ContentsStatus == "ready" ? "Nothing listed." : detail.ContentsStatus.c_str());
			return;
		}
		const float font = ImGui::GetFontSize();
		const float tile = font * 3.2F;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		std::string lastKind;
		int shown = 0;
		for (const Json& content: detail.Contents) {
			if (!content.is_object() || shown >= 300) {
				continue;
			}
			std::string kind = Str(content, "kind");
			if (kind != lastKind) {
				ImGui::SeparatorText(kind == "module" ? "Modules" : KindLabel(kind == "unit" || kind == "weapon" || kind == "map" || kind == "spell" || kind == "powerup" ? kind : kind == "mode" ? "mode" : kind).append("s").c_str());
				lastKind = kind;
			}
			++shown;
			ImGui::PushID(shown);
			ImVec2 from = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(tile, tile));
			if (content.contains("image") && content["image"].is_object()) {
				std::string url = Str(content["image"], "url");
				if (!url.empty()) {
					DrawFitted(drawList, Want(PictureUrl(url, 0, true)), from, ImVec2(from.x + tile, from.y + tile), true);
				}
			} else {
				drawList->AddRectFilled(from, ImVec2(from.x + tile, from.y + tile), IM_COL32(8, 12, 24, 255));
			}
			ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::TextUnformatted(Str(content, "name").c_str());
			std::string line = Str(content, "className");
			if (long long cost = Num(content, "cost"); cost > 0) {
				line += (line.empty() ? "" : ", ") + std::to_string(cost) + " gold";
			}
			if (!line.empty()) {
				ImGui::TextDisabled("%s", line.c_str());
			}
			ImGui::EndGroup();
			std::string description = Str(content, "description");
			if (!description.empty()) {
				ImGui::SetItemTooltip("%s", description.c_str());
			}
			ImGui::PopID();
		}
		if (shown >= 300) {
			ImGui::TextDisabled("And more: see the full list on the Depot.");
		}
	}

	void Details(float width, float height) {
		if (!ImGui::BeginChild("##depotDetails", ImVec2(width, height), ImGuiChildFlags_Borders)) {
			ImGui::EndChild();
			return;
		}
		Detail& detail = s_Browser.Picked_;
		if (s_Browser.Picked.empty()) {
			ImGui::TextWrapped("Pick something to see more about it: its description, its versions, what it needs and what's inside it.");
			ImGui::EndChild();
			return;
		}
		if (detail.Loading) {
			ImGui::TextDisabled("Asking the Depot...");
			ImGui::EndChild();
			return;
		}
		if (!detail.Error.empty() || !detail.Item.is_object()) {
			ImGui::TextWrapped("%s", detail.Error.c_str());
			ImGui::EndChild();
			return;
		}
		const Json& item = detail.Item;
		const std::string kind = Str(item, "kind");
		const std::string slug = Str(item, "slug");
		const float room = ImGui::GetContentRegionAvail().x;

		// Open on the Depot stays at the bottom; the rest scrolls above it.
		const float buttonHeight = ImGui::GetFrameHeight() * 1.6F;
		ImGui::BeginChild("##depotInfo", ImVec2(0.0F, -(buttonHeight + ImGui::GetStyle().ItemSpacing.y)), ImGuiChildFlags_None);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		if (item.contains("cover") && item["cover"].is_object()) {
			bool pixelArt = item["cover"].value("pixelArt", false);
			ImVec2 from = ImGui::GetCursorScreenPos();
			float pictureHeight = std::floor(std::min(room * 9.0F / 16.0F, height * 0.4F));
			ImGui::Dummy(ImVec2(room, pictureHeight));
			DrawFitted(drawList, Want(PictureUrl(Str(item["cover"], "url"), c_CoverWidth, pixelArt), true), from, ImVec2(from.x + room, from.y + pictureHeight), pixelArt);
		}
		ImGui::PushStyleColor(ImGuiCol_Text, ToolTheme::Vec(ToolTheme::Gold));
		ImGui::TextWrapped("%s", Str(item, "title").c_str());
		ImGui::PopStyleColor();
		std::string byline = KindLabel(kind);
		if (item.contains("author") && item["author"].is_object()) {
			byline += " by " + Str(item["author"], "name");
		} else if (item.value("official", false)) {
			byline += ", comes with the game";
		}
		ImGui::TextDisabled("%s", byline.c_str());
		ImGui::TextDisabled("%lld downloads, %lld likes, %lld comments", Num(item, "downloads"), Num(item, "likes"), Num(item, "comments"));
		if (std::string module = Str(item, "moduleId"); !module.empty()) {
			ImGui::TextDisabled("%s%s", module.c_str(), ModuleInstalled(module) ? "  (installed)" : "");
		}
		if (std::string version = Str(item, "gameVersion"); !version.empty()) {
			ImGui::TextDisabled("Made for game version %s (this is %s)", version.c_str(), c_VersionString);
		}
		if (item.contains("tags") && item["tags"].is_array() && !item["tags"].empty()) {
			ImGui::Spacing();
			int i = 0;
			for (const Json& tag: item["tags"]) {
				if (!tag.is_string()) {
					continue;
				}
				if (i++ > 0) {
					ImGui::SameLine();
				}
				if (ToolUI::SmallButton(tag.get<std::string>().c_str())) {
					s_Browser.Tag = tag.get<std::string>();
					s_Browser.FiltersChanged = true;
				}
				ImGui::SetItemTooltip("Show everything tagged %s", tag.get<std::string>().c_str());
			}
		}
		if (std::string summary = Str(item, "summary"); !summary.empty()) {
			ImGui::Spacing();
			ImGui::TextWrapped("%s", summary.c_str());
		}

		if (item.contains("media") && item["media"].is_array() && !item["media"].empty()) {
			ImGui::SeparatorText("Pictures");
			const float thumbWidth = std::floor((room - ImGui::GetStyle().ItemSpacing.x) * 0.5F);
			int i = 0;
			for (const Json& media: item["media"]) {
				std::string url = media.is_object() ? Str(media, "url") : "";
				if (url.empty()) {
					continue;
				}
				if (i++ % 2 != 0) {
					ImGui::SameLine();
				}
				ImVec2 from = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(thumbWidth, std::floor(thumbWidth * 9.0F / 16.0F)));
				DrawFitted(drawList, Want(PictureUrl(url, c_CoverWidth, false)), from, ImVec2(from.x + thumbWidth, from.y + std::floor(thumbWidth * 9.0F / 16.0F)), false);
				if (std::string caption = Str(media, "caption"); !caption.empty()) {
					ImGui::SetItemTooltip("%s", caption.c_str());
				}
			}
		}

		if (item.contains("dependencies") && item["dependencies"].is_array() && !item["dependencies"].empty()) {
			ImGui::SeparatorText("Needs");
			for (const Json& dependency: item["dependencies"]) {
				if (!dependency.is_object()) {
					continue;
				}
				std::string module = Str(dependency, "moduleId");
				bool installed = ModuleInstalled(module);
				ImGui::Bullet();
				ImGui::SameLine();
				if (dependency.contains("item") && dependency["item"].is_object()) {
					std::string label = Str(dependency["item"], "title") + " (" + module + ")" + (installed ? ", installed" : "");
					if (ImGui::Selectable(label.c_str())) {
						Pick(Str(dependency["item"], "slug"));
						ImGui::EndChild();
						ImGui::EndChild();
						return;
					}
				} else {
					ImGui::TextUnformatted((module + (installed ? ", installed" : ", not on the Depot")).c_str());
				}
			}
		}

		if (std::string description = Str(item, "description"); !description.empty()) {
			ImGui::SeparatorText("About");
			Markdown(description);
		}

		if (item.contains("versions") && item["versions"].is_array() && !item["versions"].empty()) {
			ImGui::SeparatorText("Versions");
			for (const Json& version: item["versions"]) {
				if (!version.is_object()) {
					continue;
				}
				std::string date = Str(version, "createdAt").substr(0, 10);
				ImGui::Text("%s", Str(version, "version").c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("%s, %s%s", date.c_str(), Bytes(Num(version, "fileSize")).c_str(), Str(version, "gameVersion").empty() ? "" : (", for game " + Str(version, "gameVersion")).c_str());
				if (std::string changelog = Str(version, "changelog"); !changelog.empty()) {
					ImGui::Indent();
					Markdown(changelog);
					ImGui::Unindent();
				}
			}
		}

		if (kind != "tool" && kind != "battle") {
			ImGui::Spacing();
			if (ImGui::CollapsingHeader("What's inside")) {
				Contents(detail);
			}
		}
		ImGui::EndChild();

		if (ToolUI::Button("Open on the Depot", ImVec2(-1.0F, buttonHeight))) {
			OpenOnSite(SitePath(kind, slug));
		}
		ImGui::SetItemTooltip("Opens this page of the Depot in your web browser.");
		ImGui::EndChild();
	}
#pragma endregion
} // namespace

void DepotBrowser::Open(const std::string& kind) {
	s_Browser.Open = true;
	s_Browser.Back = false;
	for (int i = 0; i < static_cast<int>(std::size(c_Kinds)); ++i) {
		if (!kind.empty() && kind == c_Kinds[i].Name) {
			s_Browser.Kind = i;
		}
	}
	s_Browser.AskAddress = DepotClient::s_Address.empty();
	std::snprintf(s_Browser.Address, sizeof(s_Browser.Address), "%s", DepotClient::s_Address.c_str());
	// Asked afresh each time: things may have been uploaded since.
	s_Browser.FiltersChanged = true;
	s_Browser.Picked.clear();
	s_Browser.Picked_ = Detail();
}

void DepotBrowser::Close() {
	s_Browser.Open = false;
	DepotClient::CancelWaiting();
	++s_Browser.ListGeneration;
	++s_Browser.DetailGeneration;
	s_Browser.ListLoading = false;
	FreePictures();
}

bool DepotBrowser::IsOpen() {
	return s_Browser.Open;
}

bool DepotBrowser::TakeBack() {
	bool back = s_Browser.Back;
	s_Browser.Back = false;
	return back;
}

void DepotBrowser::DrawGUI() {
	DepotClient::Update();
	if (!s_Browser.Open || !g_MenuMan.GetIsInMenuScreen()) {
		return;
	}
	// (The menu's own pointer is under the window: ImGui draws one over it.)
	ImGui::GetIO().MouseDrawCursor = true;
	if (s_Browser.FiltersChanged && !s_Browser.AskAddress) {
		Refresh();
	}
	FetchPictures();

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float margin = std::floor(std::min(viewport->Size.x, viewport->Size.y) * 0.03F);
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + margin, viewport->Pos.y + margin));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - margin * 2.0F, viewport->Size.y - margin * 2.0F));
	if (ImGui::Begin("Cortex Depot###DepotBrowser", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) {
		const ImGuiStyle& style = ImGui::GetStyle();
		const float backWidth = ImGui::CalcTextSize("Back").x + style.FramePadding.x * 4.0F;
		if (ToolUI::Button("Back", ImVec2(backWidth, 0.0F)) || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput)) {
			s_Browser.Back = true;
		}
		ImGui::SameLine();
		if (s_Browser.AskAddress) {
			ImGui::NewLine();
			AddressBox();
		} else {
			Filters();
			ImGui::TextDisabled("%d found on the Depot at %s", s_Browser.Total, DepotClient::s_Address.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("Change")) {
				s_Browser.AskAddress = true;
				s_Browser.AddressProblem.clear();
			}
			ImVec2 room = ImGui::GetContentRegionAvail();
			float detailsWidth = std::floor(std::clamp(room.x * 0.36F, std::min(room.x * 0.5F, ImGui::GetFontSize() * 18.0F), ImGui::GetFontSize() * 40.0F));
			Cards(room.x - detailsWidth - style.ItemSpacing.x, room.y);
			ImGui::SameLine();
			Details(detailsWidth, room.y);
		}
	}
	ImGui::End();
}
