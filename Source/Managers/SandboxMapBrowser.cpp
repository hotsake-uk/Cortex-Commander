// The map browser: the main menu's screen for picking the map the Sandbox game mode or Battle Command is played on, in place of the planet.
// Every playable map is a card with a big picture of the map itself, drawn from its terrain, backdrop and bunkers the first time it's shown
// and kept in Userdata/MapPreviews after that. The cards filter by name, mod, mod type, size, whether the map has saved battles (on by
// default for Battle) and whether it's a favourite: favourites are starred on their cards, kept in Userdata/MapFavourites.txt and listed
// first. The map picked shows bigger beside them with its saved battles: the one picked is drawn over the picture (its spawn zones, bases,
// flags, goals and objectives), and Play starts the game on the map with it set up.
// The first card makes a new map instead (MapGen): a style, a size, a seed and a few sliders, with a picture of the map that follows them,
// and Play makes it and starts the game on it.
// The last ten maps played are kept in Userdata/MapRecent.txt for the "Recently played" filter, the filters and order are kept in the
// settings between launches, and Random map picks one of the maps shown (U-3).

#include "MapBrowser.h"
#include "MapGen.h"
#include "SandboxInternal.h"
#include "DataModule.h"
#include "MenuMan.h"
#include "System.h"
#include "SLBackground.h"

#include <SDL3_image/SDL_image.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <set>

namespace SandboxDetail {
	namespace {
		constexpr int c_PictureLongSide = 640; //!< The longer side of a map's picture, in pixels.
		constexpr int c_PictureVersion = 1; //!< Kept in the cached pictures' names: raise it when the way they're drawn changes, so they're drawn again.
		constexpr const char* c_PictureFolder = "Userdata/MapPreviews/";

		constexpr const char* c_ModeNames[] = {"Custom", "Capture the flag", "King of the hill", "Assault", "Last team standing", "VIP hunt", "One flag", "Capture the ship", "Wave defence", "Escort", "Extraction", "The point"};
		static_assert(std::size(c_ModeNames) == static_cast<size_t>(BattleMode::Count), "c_ModeNames must name each BattleMode.");

		/// Where a map comes from.
		enum class MapSource {
			Official,
			Mod,
			Editor,
			Count
		};
		constexpr const char* c_SourceNames[] = {"Official", "Mod", "Made in the editor"};

		/// How big a map is, by its width (the sandbox maps of the main menu's sizes are one of each).
		constexpr const char* c_SizeNames[] = {"Small", "Medium", "Large", "Extra large"};
		constexpr int c_SizeWidths[] = {3500, 6500, 10000}; //!< The widest a map of each size but the last is, in pixels (less than).

		int SizeOf(int width) {
			int size = 0;
			while (size < static_cast<int>(std::size(c_SizeWidths)) && width >= c_SizeWidths[size]) {
				++size;
			}
			return size;
		}

		enum class SortBy {
			Name,
			Size,
			Mod,
			Battles,
			Count
		};
		constexpr const char* c_SortNames[] = {"Name", "Size", "Mod", "Saved battles"};

		/// A map's picture, uploaded.
		struct MapPicture {
			GLuint Texture = 0;
			int Width = 0;
			int Height = 0;
			bool Queued = false; //!< Waiting to be loaded or drawn.
			bool Failed = false; //!< Couldn't be drawn: not tried again while the browser is open.
		};

		/// A map in the browser.
		struct MapEntry {
			std::string Name;
			std::string Description;
			std::string ModName; //!< The mod it's in, as the mod names itself.
			int ModuleID = -1;
			MapSource Source = MapSource::Official;
			int Width = 0; //!< In pixels.
			int Height = 0;
			int Size = 0; //!< Index into c_SizeNames.
			std::vector<SavedBattle> Battles;
			MapPicture Picture;
		};

		struct Browser {
			bool Open = false;
			MapBrowser::Purpose For = MapBrowser::Purpose::Sandbox;
			std::vector<MapEntry> Maps;
			std::vector<int> Shown; //!< The maps the filters let through, in the order they're sorted.
			bool FiltersChanged = true;
			char Search[64] = "";
			int Module = -1; //!< Only maps of this mod (its ID); -1 for any.
			int Source = -1; //!< Only maps of this MapSource; -1 for any.
			int Size = -1; //!< Only maps of this size; -1 for any.
			bool OnlyWithBattles = false;
			bool OnlyFavourites = false;
			bool OnlyRecent = false; //!< Only the maps played lately, the latest first, whatever the sort.
			bool QuickBattle = false; //!< Opened from the main menu's Quick Battle: Play starts the battle picked (MapBrowser::Choice::StartBattle).
			SortBy Sort = SortBy::Name;
			int Picked = -1; //!< Index into Maps of the map picked, or -1.
			int PickedBattle = -1; //!< Index into its Battles of the battle picked, or -1 for none: the map as it is.
			bool ScrollToPicked = false;
			std::deque<int> ToDraw; //!< Maps whose pictures are wanted, by index into Maps, the soonest first.
			MapBrowser::Choice Choice;
		};
		Browser s_Browser;

		/// The map maker's card: its recipe, and the picture of the map it would make.
		struct Generator {
			MapGen::Recipe Recipe;
			bool Seeded = false;
			char SeedText[9] = "";
			std::string PictureOf; //!< The recipe (its code) the picture shows.
			MapEntry Entry; //!< The map as a card shows one: its picture and size.
			bool PlayWhenMade = false; //!< Play was pressed: the game starts on the map once it's made.
			std::string Error;
		};
		Generator s_Generator;
		constexpr int c_GeneratorPick = -2; //!< Picked: the map maker's card.

		constexpr const char* c_FavouritesFile = "Userdata/MapFavourites.txt"; //!< The maps marked as favourites, by name, one to a line.
		std::set<std::string> s_Favourites;
		bool s_FavouritesRead = false;

		/// The maps marked as favourites, read from their file the first time.
		std::set<std::string>& Favourites() {
			if (!s_FavouritesRead) {
				s_FavouritesRead = true;
				std::ifstream file(c_FavouritesFile);
				std::string line;
				while (std::getline(file, line)) {
					if (!line.empty() && line.back() == '\r') {
						line.pop_back();
					}
					if (!line.empty()) {
						s_Favourites.insert(line);
					}
				}
			}
			return s_Favourites;
		}

		bool IsFavourite(const std::string& mapName) { return Favourites().count(mapName) != 0; }

		/// Marks a map as a favourite or not, and keeps the list in its file.
		void ToggleFavourite(const std::string& mapName) {
			std::set<std::string>& favourites = Favourites();
			if (!favourites.erase(mapName)) {
				favourites.insert(mapName);
			}
			std::error_code error;
			std::filesystem::create_directories(std::filesystem::path(c_FavouritesFile).parent_path(), error);
			std::ofstream file(c_FavouritesFile, std::ios::trunc);
			for (const std::string& name: favourites) {
				file << name << '\n';
			}
			s_Browser.FiltersChanged = true;
		}

		constexpr const char* c_RecentMapsFile = "Userdata/MapRecent.txt"; //!< The maps played lately, by name, the latest first, one to a line.
		constexpr size_t c_RecentKept = 10; //!< How many maps the recently played list keeps.
		std::vector<std::string> s_Recent;
		bool s_RecentRead = false;

		/// The maps played lately, the latest first, read from their file the first time.
		std::vector<std::string>& RecentMaps() {
			if (!s_RecentRead) {
				s_RecentRead = true;
				std::ifstream file(c_RecentMapsFile);
				std::string line;
				while (std::getline(file, line) && s_Recent.size() < c_RecentKept) {
					if (!line.empty() && line.back() == '\r') {
						line.pop_back();
					}
					if (!line.empty() && std::find(s_Recent.begin(), s_Recent.end(), line) == s_Recent.end()) {
						s_Recent.push_back(line);
					}
				}
			}
			return s_Recent;
		}

		/// Where a map is in the recently played list: 0 for the latest, or -1 if it isn't in it.
		int RecentRank(const std::string& mapName) {
			const std::vector<std::string>& recent = RecentMaps();
			auto found = std::find(recent.begin(), recent.end(), mapName);
			return found == recent.end() ? -1 : static_cast<int>(found - recent.begin());
		}

		/// Puts the browser's filters and order as the settings keep them (U-3). The saved battles filter is set by the purpose instead.
		void ReadKeptFilters() {
			const SettingsMan::MapBrowserFilters& kept = g_SettingsMan.MapBrowserFilterSettings();
			s_Browser.Module = kept.Mod.empty() ? -1 : g_PresetMan.GetModuleID(kept.Mod);
			s_Browser.Source = kept.Source >= 0 && kept.Source < static_cast<int>(MapSource::Count) ? kept.Source : -1;
			s_Browser.Size = kept.Size >= 0 && kept.Size < static_cast<int>(std::size(c_SizeNames)) ? kept.Size : -1;
			s_Browser.OnlyFavourites = kept.OnlyFavourites;
			s_Browser.OnlyRecent = kept.OnlyRecent;
			s_Browser.Sort = kept.Sort >= 0 && kept.Sort < static_cast<int>(SortBy::Count) ? static_cast<SortBy>(kept.Sort) : SortBy::Name;
		}

		/// Keeps the browser's filters and order in the settings, for the next time it opens, after a restart too.
		void KeepFilters() {
			SettingsMan::MapBrowserFilters& kept = g_SettingsMan.MapBrowserFilterSettings();
			const DataModule* module = s_Browser.Module >= 0 ? g_PresetMan.GetDataModule(s_Browser.Module) : nullptr;
			kept.Mod = module ? module->GetFileName() : std::string();
			kept.Source = s_Browser.Source;
			kept.Size = s_Browser.Size;
			kept.OnlyFavourites = s_Browser.OnlyFavourites;
			kept.OnlyRecent = s_Browser.OnlyRecent;
			kept.Sort = static_cast<int>(s_Browser.Sort);
		}

		/// A five-pointed star centred on a point: filled gold for a favourite, else an outline (brighter when hovered).
		void DrawStar(ImDrawList* drawList, ImVec2 middle, float radius, bool filled, bool hovered) {
			ImVec2 points[10];
			for (int i = 0; i < 10; ++i) {
				float reach = i % 2 == 0 ? radius : radius * 0.45F;
				float angle = -c_HalfPI + static_cast<float>(i) * c_PI / 5.0F;
				points[i] = ImVec2(middle.x + std::cos(angle) * reach, middle.y + std::sin(angle) * reach);
			}
			drawList->AddCircleFilled(middle, radius * 1.25F, IM_COL32(0, 0, 0, hovered ? 200 : 140), 16);
			if (filled) {
				drawList->AddConcavePolyFilled(points, 10, ToolTheme::Gold);
			}
			drawList->AddPolyline(points, 10, filled ? ToolTheme::GoldShadow : hovered ? ToolTheme::Gold : IM_COL32(200, 200, 210, 220), ImDrawFlags_Closed, 1.5F);
		}

		const char* ActivityName(MapBrowser::Purpose purpose) { return purpose == MapBrowser::Purpose::Battle ? "Battle Command" : "Sandbox"; }

		std::string Lowered(std::string text) {
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		/// The battle a map starts with when it's picked: the one marked to load with it, else (for Battle) its first, else none.
		int DefaultBattle(const MapEntry& map, MapBrowser::Purpose purpose) {
			for (int i = 0; i < static_cast<int>(map.Battles.size()); ++i) {
				if (map.Battles[i].WithMap) {
					return i;
				}
			}
			return purpose == MapBrowser::Purpose::Battle && !map.Battles.empty() ? 0 : -1;
		}

#pragma region Map pictures
		void FreePicture(MapPicture& picture) {
			if (picture.Texture != 0) {
				glDeleteTextures(1, &picture.Texture);
			}
			picture = MapPicture();
		}

		/// Uploads a picture, its rows rowLength pixels apart.
		void UploadPicture(MapPicture& picture, const unsigned char* pixels, int width, int height, int rowLength) {
			if (picture.Texture == 0) {
				glGenTextures(1, &picture.Texture);
			}
			glBindTexture(GL_TEXTURE_2D, picture.Texture);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
			glBindTexture(GL_TEXTURE_2D, 0);
			picture.Width = width;
			picture.Height = height;
		}

		std::string PictureFile(const std::string& mapName) {
			std::string file = mapName;
			for (char& c: file) {
				if (std::strchr("<>:\"/\\|?*", c) || static_cast<unsigned char>(c) < 32) {
					c = '_';
				}
			}
			return c_PictureFolder + file + ".v" + std::to_string(c_PictureVersion) + ".png";
		}

		/// Loads an image file the map is drawn from, as it is on disk (none if it isn't there). Ownership IS transferred.
		std::unique_ptr<BITMAP, void (*)(BITMAP*)> LoadMapImage(ContentFile& file) {
			std::unique_ptr<BITMAP, void (*)(BITMAP*)> bitmap(nullptr, destroy_bitmap);
			const std::string& path = file.GetDataPath();
			// (ContentFile gives up on the game when a file of the game's own isn't there: only ask for one that is.)
			if (!path.empty() && System::PathExistsCaseSensitive(path)) {
				bitmap.reset(file.GetAsBitmap(COLORCONV_NONE, false));
			}
			return bitmap;
		}

		/// A pixel of a bitmap of any depth as RGB, and how opaque it is, 0 to 255: 0 for the mask colour (8-bit) or a clear pixel (32-bit).
		int PixelColor(BITMAP* bitmap, int x, int y, unsigned char* rgb) {
			int depth = bitmap_color_depth(bitmap);
			int pixel = getpixel(bitmap, x, y);
			if (pixel < 0) {
				return 0;
			}
			if (depth == 8) {
				if (pixel == ColorKeys::g_MaskColor) {
					return 0;
				}
				rgb[0] = static_cast<unsigned char>(getr8(pixel));
				rgb[1] = static_cast<unsigned char>(getg8(pixel));
				rgb[2] = static_cast<unsigned char>(getb8(pixel));
				return 255;
			}
			if (depth == 32) {
				// (Loaded as RGBA, the way the game's Allegro keeps 32-bit pixels.)
				rgb[0] = static_cast<unsigned char>(getr32(pixel));
				rgb[1] = static_cast<unsigned char>(getg32(pixel));
				rgb[2] = static_cast<unsigned char>(getb32(pixel));
				return geta32(pixel);
			}
			if (pixel == bitmap_mask_color(bitmap)) {
				return 0;
			}
			rgb[0] = static_cast<unsigned char>(getr_depth(depth, pixel));
			rgb[1] = static_cast<unsigned char>(getg_depth(depth, pixel));
			rgb[2] = static_cast<unsigned char>(getb_depth(depth, pixel));
			return 255;
		}

		/// The picture being drawn: RGB, a row after another.
		struct Canvas {
			int Width = 0;
			int Height = 0;
			std::vector<unsigned char> Pixels; //!< RGBA.

			unsigned char* At(int x, int y) { return &Pixels[(static_cast<size_t>(y) * Width + x) * 4]; }

			/// Draws a bitmap stretched over a rectangle of the picture (which may reach past its edges), its mask colour left out.
			void Stretch(BITMAP* bitmap, float left, float top, float width, float height, float brightness = 1.0F) {
				if (!bitmap || width < 0.5F || height < 0.5F) {
					return;
				}
				int fromX = std::max(0, static_cast<int>(std::floor(left)));
				int toX = std::min(Width, static_cast<int>(std::ceil(left + width)));
				int fromY = std::max(0, static_cast<int>(std::floor(top)));
				int toY = std::min(Height, static_cast<int>(std::ceil(top + height)));
				unsigned char rgb[3];
				for (int y = fromY; y < toY; ++y) {
					int sourceY = static_cast<int>((static_cast<float>(y) + 0.5F - top) / height * static_cast<float>(bitmap->h));
					if (sourceY < 0 || sourceY >= bitmap->h) {
						continue;
					}
					for (int x = fromX; x < toX; ++x) {
						int sourceX = static_cast<int>((static_cast<float>(x) + 0.5F - left) / width * static_cast<float>(bitmap->w));
						if (sourceX < 0 || sourceX >= bitmap->w) {
							continue;
						}
						if (int opacity = PixelColor(bitmap, sourceX, sourceY, rgb); opacity > 0) {
							unsigned char* pixel = At(x, y);
							float cover = static_cast<float>(opacity) / 255.0F;
							for (int channel = 0; channel < 3; ++channel) {
								float color = std::min(255.0F, static_cast<float>(rgb[channel]) * brightness);
								pixel[channel] = static_cast<unsigned char>(static_cast<float>(pixel[channel]) * (1.0F - cover) + color * cover);
							}
						}
					}
				}
			}
		};

		/// Draws a map's picture: its backdrop, its terrain as textured in the game and the bunkers and other pieces placed on it.
		bool DrawPicture(const MapEntry& map, Canvas& canvas) {
			Scene* scene = dynamic_cast<Scene*>(const_cast<Entity*>(g_PresetMan.GetEntityPreset("Scene", map.Name, map.ModuleID)));
			SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
			if (!terrain || map.Width <= 0 || map.Height <= 0) {
				return false;
			}
			const float scale = static_cast<float>(c_PictureLongSide) / static_cast<float>(std::max(map.Width, map.Height));
			canvas.Width = std::max(1, static_cast<int>(std::round(static_cast<float>(map.Width) * scale)));
			canvas.Height = std::max(1, static_cast<int>(std::round(static_cast<float>(map.Height) * scale)));
			canvas.Pixels.assign(static_cast<size_t>(canvas.Width) * canvas.Height * 4, 255);

			// The sky, in case the map has no backdrop of its own: a deep blue, lighter towards the ground.
			for (int y = 0; y < canvas.Height; ++y) {
				float down = static_cast<float>(y) / static_cast<float>(canvas.Height);
				for (int x = 0; x < canvas.Width; ++x) {
					unsigned char* pixel = canvas.At(x, y);
					pixel[0] = static_cast<unsigned char>(20 + 50 * down);
					pixel[1] = static_cast<unsigned char>(30 + 70 * down);
					pixel[2] = static_cast<unsigned char>(60 + 90 * down);
				}
			}
			// The backdrop, farthest first: the farthest layer (the sky) covering the picture, those nearer across its width along the bottom.
			std::vector<SLBackground*> layers(scene->GetBackLayers().begin(), scene->GetBackLayers().end());
			for (int i = static_cast<int>(layers.size()) - 1; i >= 0; --i) {
				if (!layers[i]) {
					continue;
				}
				auto bitmap = LoadMapImage(layers[i]->GetContentFile());
				if (!bitmap || bitmap->w <= 0 || bitmap->h <= 0) {
					continue;
				}
				float fit = static_cast<float>(canvas.Width) / static_cast<float>(bitmap->w);
				if (i == static_cast<int>(layers.size()) - 1) {
					fit = std::max(fit, static_cast<float>(canvas.Height) / static_cast<float>(bitmap->h));
				}
				float width = static_cast<float>(bitmap->w) * fit;
				float height = static_cast<float>(bitmap->h) * fit;
				canvas.Stretch(bitmap.get(), (static_cast<float>(canvas.Width) - width) * 0.5F, static_cast<float>(canvas.Height) - height, width, height);
			}

			// The terrain: each pixel of the picture from four points of the map's materials, textured as the game textures them (or as
			// painted, for a map saved with its own colours), over the backdrop as much as the points are ground.
			auto materials = LoadMapImage(terrain->GetContentFile());
			if (materials && bitmap_color_depth(materials.get()) == 8) {
				std::unique_ptr<BITMAP, void (*)(BITMAP*)> painted(nullptr, destroy_bitmap);
				if (SceneLayer* foreground = terrain->GetFGSceneLayer(); foreground && foreground->IsLoadedFromDisk()) {
					painted = LoadMapImage(foreground->GetContentFile());
					if (painted && (painted->w != materials->w || painted->h != materials->h)) {
						painted.reset();
					}
				}
				const DataModule* module = g_PresetMan.GetDataModule(terrain->GetContentFile().GetDataModuleID());
				const std::array<Material*, c_PaletteEntriesNumber>& palette = g_SceneMan.GetMaterialPalette();
				const float step = static_cast<float>(materials->w) / static_cast<float>(canvas.Width);
				const float stepY = static_cast<float>(materials->h) / static_cast<float>(canvas.Height);
				constexpr float c_Points[2] = {0.25F, 0.75F};
				unsigned char rgb[3];
				for (int y = 0; y < canvas.Height; ++y) {
					for (int x = 0; x < canvas.Width; ++x) {
						int sum[3] = {0, 0, 0};
						int ground = 0;
						for (float pointY: c_Points) {
							int mapY = std::clamp(static_cast<int>((static_cast<float>(y) + pointY) * stepY), 0, materials->h - 1);
							for (float pointX: c_Points) {
								int mapX = std::clamp(static_cast<int>((static_cast<float>(x) + pointX) * step), 0, materials->w - 1);
								int index = materials->line[mapY][mapX];
								if (module && module->GetAllMaterialMappings()[index] != 0) {
									index = module->GetAllMaterialMappings()[index];
								}
								if (index == g_MaterialAir) {
									continue;
								}
								bool drawn = false;
								if (painted) {
									drawn = PixelColor(painted.get(), mapX, mapY, rgb) > 0;
								} else {
									const Material* material = palette[index] ? palette[index] : palette[g_MaterialOutOfBounds];
									if (BITMAP* texture = material ? material->GetFGTexture() : nullptr) {
										drawn = PixelColor(texture, mapX % texture->w, mapY % texture->h, rgb) > 0;
									} else if (material) {
										int color = material->GetColor().GetIndex();
										rgb[0] = static_cast<unsigned char>(getr8(color));
										rgb[1] = static_cast<unsigned char>(getg8(color));
										rgb[2] = static_cast<unsigned char>(getb8(color));
										drawn = true;
									}
								}
								if (drawn) {
									sum[0] += rgb[0];
									sum[1] += rgb[1];
									sum[2] += rgb[2];
									++ground;
								}
							}
						}
						if (ground > 0) {
							unsigned char* pixel = canvas.At(x, y);
							float cover = static_cast<float>(ground) / 4.0F;
							for (int channel = 0; channel < 3; ++channel) {
								pixel[channel] = static_cast<unsigned char>(static_cast<float>(pixel[channel]) * (1.0F - cover) + static_cast<float>(sum[channel]) / static_cast<float>(ground) * cover);
							}
						}
					}
				}
			}

			// The bunkers and other pieces placed on the map: their walls behind, a little darker, and their fronts.
			for (const SceneObject* placed: *scene->GetPlacedObjects(Scene::PLACEONLOAD)) {
				if (const TerrainObject* piece = dynamic_cast<const TerrainObject*>(placed)) {
					Vector corner = piece->GetPos() + piece->GetBitmapOffset();
					for (BITMAP* bitmap: {piece->GetBGColorBitmap(), piece->GetFGColorBitmap()}) {
						if (bitmap) {
							canvas.Stretch(bitmap, corner.m_X * scale, corner.m_Y * scale, static_cast<float>(bitmap->w) * scale, static_cast<float>(bitmap->h) * scale, bitmap == piece->GetBGColorBitmap() ? 0.75F : 1.0F);
						}
					}
				}
			}
			return true;
		}

		/// Whether a map's cached picture is there and newer than the files it was drawn from.
		bool CachedPictureFresh(const MapEntry& map, const std::string& file) {
			std::error_code error;
			auto cached = std::filesystem::last_write_time(file, error);
			if (error) {
				return false;
			}
			const Scene* scene = dynamic_cast<const Scene*>(g_PresetMan.GetEntityPreset("Scene", map.Name, map.ModuleID));
			SLTerrain* terrain = scene ? const_cast<Scene*>(scene)->GetTerrain() : nullptr;
			if (terrain) {
				for (SceneLayer* layer: {static_cast<SceneLayer*>(terrain), terrain->GetFGSceneLayer()}) {
					if (layer && !layer->GetContentFile().GetDataPath().empty()) {
						auto source = std::filesystem::last_write_time(layer->GetContentFile().GetDataPath(), error);
						if (!error && source > cached) {
							return false;
						}
					}
				}
			}
			return true;
		}

		/// Loads a map's picture from its cache if it's there and fresh. Returns whether it was.
		bool LoadCachedPicture(MapEntry& map) {
			std::string file = PictureFile(map.Name);
			if (!CachedPictureFresh(map, file)) {
				return false;
			}
			SDL_Surface* loaded = IMG_Load(file.c_str());
			if (!loaded) {
				return false;
			}
			SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
			SDL_DestroySurface(loaded);
			if (!rgba) {
				return false;
			}
			UploadPicture(map.Picture, static_cast<const unsigned char*>(rgba->pixels), rgba->w, rgba->h, rgba->pitch / 4);
			SDL_DestroySurface(rgba);
			return true;
		}

		/// Draws a map's picture afresh, uploads it and keeps it in the cache.
		bool MakePicture(MapEntry& map) {
			Canvas canvas;
			if (!DrawPicture(map, canvas)) {
				return false;
			}
			UploadPicture(map.Picture, canvas.Pixels.data(), canvas.Width, canvas.Height, canvas.Width);
			std::error_code error;
			std::filesystem::create_directories(c_PictureFolder, error);
			if (SDL_Surface* surface = SDL_CreateSurfaceFrom(canvas.Width, canvas.Height, SDL_PIXELFORMAT_RGBA32, canvas.Pixels.data(), canvas.Width * 4)) {
				IMG_SavePNG(surface, PictureFile(map.Name).c_str());
				SDL_DestroySurface(surface);
			}
			return true;
		}

		/// Asks for a map's picture, soonest if it's wanted now (the map picked).
		void WantPicture(int index, bool now) {
			MapPicture& picture = s_Browser.Maps[index].Picture;
			if (picture.Texture != 0 || picture.Failed) {
				return;
			}
			if (picture.Queued) {
				if (now) {
					s_Browser.ToDraw.erase(std::remove(s_Browser.ToDraw.begin(), s_Browser.ToDraw.end(), index), s_Browser.ToDraw.end());
					s_Browser.ToDraw.push_front(index);
				}
				return;
			}
			picture.Queued = true;
			if (now) {
				s_Browser.ToDraw.push_front(index);
			} else {
				s_Browser.ToDraw.push_back(index);
			}
		}

		/// Works through the pictures wanted: those cached load quickly, a few a frame; one that has to be drawn afresh takes a frame of its own.
		void MakeWantedPictures() {
			int loaded = 0;
			while (!s_Browser.ToDraw.empty() && loaded < 8) {
				int index = s_Browser.ToDraw.front();
				MapEntry& map = s_Browser.Maps[index];
				if (LoadCachedPicture(map)) {
					s_Browser.ToDraw.pop_front();
					map.Picture.Queued = false;
					++loaded;
					continue;
				}
				// (Drawing one can take a while for a big map: only one a frame, and not after loading others this frame.)
				if (loaded > 0) {
					break;
				}
				s_Browser.ToDraw.pop_front();
				map.Picture.Queued = false;
				map.Picture.Failed = !MakePicture(map);
				break;
			}
		}
#pragma endregion

#pragma region The list
		/// Finds every map the game modes can be played on, and the battles saved with each.
		void ListMaps() {
			for (MapEntry& map: s_Browser.Maps) {
				FreePicture(map.Picture);
			}
			s_Browser.Maps.clear();
			s_Browser.ToDraw.clear();
			std::list<Entity*> scenes;
			g_PresetMan.GetAllOfType(scenes, "Scene");
			for (Entity* entity: scenes) {
				const Scene* scene = dynamic_cast<const Scene*>(entity);
				if (!scene || scene->IsMetagameInternal() || scene->IsSavedGameInternal() || (!scene->GetMetasceneParent().empty() && !g_SettingsMan.ShowMetascenes())) {
					continue;
				}
				const std::string& name = scene->GetPresetName();
				if (name == "Null Scene" || name == "Editor Scene" || MapGen::IsGenerated(name)) {
					continue;
				}
				Vector dimensions = scene->GetDimensions();
				if (dimensions.m_X < 1.0F || dimensions.m_Y < 1.0F) {
					continue;
				}
				MapEntry map;
				map.Name = name;
				map.Description = scene->GetDescription();
				map.ModuleID = scene->GetModuleID();
				map.Width = static_cast<int>(dimensions.m_X);
				map.Height = static_cast<int>(dimensions.m_Y);
				map.Size = SizeOf(map.Width);
				if (const DataModule* module = g_PresetMan.GetDataModule(map.ModuleID)) {
					map.ModName = module->GetFriendlyName().empty() ? module->GetFileName() : module->GetFriendlyName();
					map.Source = module->IsUserdata() || g_PresetMan.IsModuleUserdata(module->GetFileName()) ? MapSource::Editor : g_PresetMan.IsModuleOfficial(module->GetFileName()) ? MapSource::Official : MapSource::Mod;
				}
				map.Battles = ReadSavedBattles(name);
				s_Browser.Maps.push_back(std::move(map));
			}
			s_Browser.FiltersChanged = true;
		}

		/// Sorts the maps the filters let through into Shown.
		void FilterMaps() {
			s_Browser.FiltersChanged = false;
			s_Browser.Shown.clear();
			std::string search = Lowered(s_Browser.Search);
			for (int i = 0; i < static_cast<int>(s_Browser.Maps.size()); ++i) {
				const MapEntry& map = s_Browser.Maps[i];
				if ((s_Browser.Module >= 0 && map.ModuleID != s_Browser.Module) || (s_Browser.Source >= 0 && static_cast<int>(map.Source) != s_Browser.Source) || (s_Browser.Size >= 0 && map.Size != s_Browser.Size) ||
				    (s_Browser.OnlyWithBattles && map.Battles.empty()) || (s_Browser.OnlyFavourites && !IsFavourite(map.Name)) || (s_Browser.OnlyRecent && RecentRank(map.Name) < 0)) {
					continue;
				}
				if (!search.empty() && Lowered(map.Name).find(search) == std::string::npos && Lowered(map.ModName).find(search) == std::string::npos) {
					continue;
				}
				s_Browser.Shown.push_back(i);
			}
			const std::vector<MapEntry>& maps = s_Browser.Maps;
			std::stable_sort(s_Browser.Shown.begin(), s_Browser.Shown.end(), [&maps](int a, int b) {
				const MapEntry& first = maps[a];
				const MapEntry& second = maps[b];
				// Recently played: the latest first, whatever the order.
				if (s_Browser.OnlyRecent) {
					return RecentRank(first.Name) < RecentRank(second.Name);
				}
				// Favourites first, whatever the order.
				if (bool favourite = IsFavourite(first.Name); favourite != IsFavourite(second.Name)) {
					return favourite;
				}
				switch (s_Browser.Sort) {
					case SortBy::Size:
						if (first.Width * static_cast<long long>(first.Height) != second.Width * static_cast<long long>(second.Height)) {
							return first.Width * static_cast<long long>(first.Height) < second.Width * static_cast<long long>(second.Height);
						}
						break;
					case SortBy::Mod:
						if (first.ModName != second.ModName) {
							return Lowered(first.ModName) < Lowered(second.ModName);
						}
						break;
					case SortBy::Battles:
						if (first.Battles.size() != second.Battles.size()) {
							return first.Battles.size() > second.Battles.size();
						}
						break;
					default:
						break;
				}
				return Lowered(first.Name) < Lowered(second.Name);
			});
		}

		void Pick(int index) {
			if (index == s_Browser.Picked) {
				return;
			}
			s_Browser.Picked = index;
			s_Browser.PickedBattle = index >= 0 ? DefaultBattle(s_Browser.Maps[index], s_Browser.For) : -1;
		}

		/// Starts making the map maker's map; the game starts on it once it's made (DrawGUI).
		void MakeAndPlay() {
			if (MapGen::Busy()) {
				return;
			}
			s_Generator.Error.clear();
			s_Generator.PlayWhenMade = true;
			MapGen::Start(s_Generator.Recipe);
		}

		void Play() {
			if (s_Browser.Picked == c_GeneratorPick) {
				MakeAndPlay();
				return;
			}
			if (s_Browser.Picked < 0) {
				return;
			}
			const MapEntry& map = s_Browser.Maps[s_Browser.Picked];
			MapBrowser::Choice& choice = s_Browser.Choice;
			choice = MapBrowser::Choice();
			choice.Play = true;
			choice.For = s_Browser.For;
			choice.Scene = map.Name;
			choice.StartBattle = s_Browser.QuickBattle;
			if (s_Browser.PickedBattle >= 0 && s_Browser.PickedBattle < static_cast<int>(map.Battles.size())) {
				choice.Battle = map.Battles[s_Browser.PickedBattle].Name;
			} else {
				choice.NoBattle = true;
			}
		}
#pragma endregion

#pragma region Drawing
		/// A map's picture fitted into a box, centred, on a dark well. Returns where it went (nothing drawn if it isn't ready).
		bool DrawPictureIn(ImDrawList* drawList, const MapEntry& map, ImVec2 from, ImVec2 to, ImVec2& pictureFrom, float& scenePixel) {
			drawList->AddRectFilled(from, to, ToolTheme::Well);
			const MapPicture& picture = map.Picture;
			float boxWidth = to.x - from.x;
			float boxHeight = to.y - from.y;
			float aspect = static_cast<float>(map.Width) / static_cast<float>(std::max(1, map.Height));
			float width = boxWidth;
			float height = width / aspect;
			if (height > boxHeight) {
				height = boxHeight;
				width = height * aspect;
			}
			pictureFrom = ImVec2(std::floor(from.x + (boxWidth - width) * 0.5F), std::floor(from.y + (boxHeight - height) * 0.5F));
			scenePixel = width / static_cast<float>(std::max(1, map.Width));
			if (picture.Texture == 0) {
				const char* note = picture.Failed ? "No picture" : "Drawing the map...";
				ImVec2 size = ImGui::CalcTextSize(note);
				drawList->AddText(ImVec2(from.x + (boxWidth - size.x) * 0.5F, from.y + (boxHeight - size.y) * 0.5F), IM_COL32(150, 160, 190, 255), note);
				return false;
			}
			drawList->AddImage(static_cast<ImTextureID>(picture.Texture), pictureFrom, ImVec2(pictureFrom.x + width, pictureFrom.y + height));
			return true;
		}

		/// A label on a dark plate, centred on a point.
		void Tag(ImDrawList* drawList, ImVec2 at, ImU32 color, const std::string& text) {
			ImVec2 size = ImGui::CalcTextSize(text.c_str());
			ImVec2 from(at.x - size.x * 0.5F - 3.0F, at.y - size.y * 0.5F - 1.0F);
			drawList->AddRectFilled(from, ImVec2(from.x + size.x + 6.0F, from.y + size.y + 2.0F), IM_COL32(0, 0, 0, 170));
			drawList->AddText(ImVec2(from.x + 3.0F, from.y + 1.0F), color, text.c_str());
		}

		ImVec2 ZoneMiddle(const std::vector<ImVec2>& corners) {
			ImVec2 middle(0.0F, 0.0F);
			for (const ImVec2& corner: corners) {
				middle.x += corner.x;
				middle.y += corner.y;
			}
			float count = static_cast<float>(std::max<size_t>(1, corners.size()));
			return ImVec2(middle.x / count, middle.y / count);
		}

		/// Whether a mode's game uses its own zones as set up (a mode switched to keeps the zones drawn for another, unused).
		bool ZonesUsed(const BattleModeSettings& mode) {
			switch (mode.Mode) {
				case BattleMode::KingOfTheHill:
				case BattleMode::Assault:
				case BattleMode::WaveDefence:
				case BattleMode::Escort:
				case BattleMode::Extraction:
					return true;
				case BattleMode::OneFlag:
					return mode.FlagByZones;
				default:
					return false;
			}
		}

		/// What a mode calls its own zones, by their number from 1.
		std::string ZoneName(BattleMode mode, int number, int count) {
			switch (mode) {
				case BattleMode::KingOfTheHill:
					return count > 1 ? "Hill " + std::to_string(number) : "Hill";
				case BattleMode::Assault:
					return "Objective " + std::to_string(number);
				case BattleMode::OneFlag:
					return "Flag zone";
				case BattleMode::WaveDefence:
					return number == 1 ? "Defend here" : "Zone " + std::to_string(number);
				case BattleMode::Escort:
					return number == 1 ? "Destination" : "Zone " + std::to_string(number);
				case BattleMode::Extraction:
					return number == 1 ? "Extraction" : "Zone " + std::to_string(number);
				default:
					return "Zone " + std::to_string(number);
			}
		}

		/// A saved battle drawn over its map's picture: each team's spawn zones, base point (or flag), goal, drop line and place to defend in
		/// its colour, and the mode's own zones and flag positions, labelled.
		void DrawBattle(ImDrawList* drawList, const SavedBattle& battle, ImVec2 pictureFrom, float scenePixel, bool labels) {
			auto toScreen = [&](const Vector& point) { return ImVec2(pictureFrom.x + point.m_X * scenePixel, pictureFrom.y + point.m_Y * scenePixel); };
			auto zoneOnScreen = [&](const std::vector<Vector>& zone) {
				std::vector<ImVec2> corners;
				corners.reserve(zone.size());
				for (const Vector& corner: zone) {
					corners.push_back(toScreen(corner));
				}
				return corners;
			};
			auto drawZone = [&](const std::vector<Vector>& zone, ImU32 color, int fill, float thickness) {
				if (zone.size() < 3) {
					return std::vector<ImVec2>();
				}
				std::vector<ImVec2> corners = zoneOnScreen(zone);
				drawList->AddConcavePolyFilled(corners.data(), static_cast<int>(corners.size()), (color & 0x00FFFFFF) | (static_cast<ImU32>(fill) << 24));
				drawList->AddPolyline(corners.data(), static_cast<int>(corners.size()), color, ImDrawFlags_Closed, thickness);
				return corners;
			};
			const float size = std::max(0.6F, ImGui::GetFontSize() / 16.0F);
			const bool custom = battle.Mode.Mode == BattleMode::Custom;
			const BattleModeSettings& mode = battle.Mode;

			if (ZonesUsed(mode)) {
				// The mode's own zones, in the gold of the menus.
				int count = static_cast<int>(mode.Zones.size());
				for (int i = 0; i < count; ++i) {
					std::vector<ImVec2> corners = drawZone(mode.Zones[i], ToolTheme::Gold, 45, 2.0F);
					if (labels && !corners.empty()) {
						Tag(drawList, ZoneMiddle(corners), ToolTheme::Gold, ZoneName(mode.Mode, i + 1, count));
					}
				}
			}
			for (int side = 0; side < c_Sides; ++side) {
				const ImU32 color = c_SideColors[side];
				const BattleSettings& card = battle.Cards[side];
				if (custom) {
					if (!card.Active) {
						continue;
					}
					for (const std::vector<Vector>& zone: card.SpawnZones) {
						drawZone(zone, color, 55, 2.0F);
					}
					if (card.DropOnLine && card.HasLine) {
						ImVec2 from = toScreen(card.LineA);
						ImVec2 to = toScreen(card.LineB);
						drawList->AddLine(from, to, color, 3.0F);
						for (int i = 0; i <= 4; ++i) {
							float t = static_cast<float>(i) / 4.0F;
							ImVec2 at(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
							drawList->AddTriangleFilled(ImVec2(at.x - 4.0F * size, at.y - 10.0F * size), ImVec2(at.x + 4.0F * size, at.y - 10.0F * size), ImVec2(at.x, at.y - 3.0F * size), color);
						}
					}
					if (card.Style == BattleStyle::Defend && card.HasDefendPos) {
						ImVec2 middle = toScreen(card.DefendPos);
						drawList->AddCircle(middle, std::max(3.0F, static_cast<float>(card.DefendRadius) * scenePixel), color, 40, 2.0F);
						if (labels) {
							Tag(drawList, middle, color, std::string(c_SideNames[side]) + " defends");
						}
					}
					continue;
				}
				if (!mode.Plays[side]) {
					continue;
				}
				for (const std::vector<Vector>& zone: mode.SpawnZones[side]) {
					std::vector<ImVec2> corners = drawZone(zone, color, 55, 2.0F);
					if (labels && !corners.empty()) {
						Tag(drawList, ZoneMiddle(corners), color, std::string(c_SideNames[side]) + " spawn");
					}
				}
				if (mode.Mode == BattleMode::OneFlag || mode.Mode == BattleMode::CaptureTheShip) {
					if (std::vector<ImVec2> corners = drawZone(mode.Goals[side], color, 25, 3.0F); labels && !corners.empty()) {
						Tag(drawList, ZoneMiddle(corners), color, std::string(c_SideNames[side]) + " goal");
					}
				}
				if (mode.HasPoint[side]) {
					// A flag on a pole: the team's flag (capture the flag) or the point in its base.
					ImVec2 foot = toScreen(mode.Points[side]);
					float pole = 22.0F * size;
					drawList->AddLine(foot, ImVec2(foot.x, foot.y - pole), IM_COL32(230, 230, 230, 240), 2.0F);
					drawList->AddTriangleFilled(ImVec2(foot.x, foot.y - pole), ImVec2(foot.x + 13.0F * size, foot.y - pole + 4.5F * size), ImVec2(foot.x, foot.y - pole + 9.0F * size), color);
					drawList->AddTriangle(ImVec2(foot.x, foot.y - pole), ImVec2(foot.x + 13.0F * size, foot.y - pole + 4.5F * size), ImVec2(foot.x, foot.y - pole + 9.0F * size), IM_COL32(20, 20, 20, 220), 1.0F);
					if (labels) {
						Tag(drawList, ImVec2(foot.x, foot.y + ImGui::GetFontSize() * 0.7F), color, std::string(c_SideNames[side]) + (mode.Mode == BattleMode::CaptureTheFlag ? " flag" : mode.Mode == BattleMode::VipHunt ? " VIP" : " base"));
					}
				}
			}
			if (mode.Mode == BattleMode::ThePoint && !mode.FlagSpots.empty()) {
				// The point: its bullseye and rings, to scale.
				const ImVec2 at = toScreen(mode.FlagSpots.front());
				const int rings = std::clamp(mode.PointRings, 0, c_MaxPointRings);
				float outer = 4.0F;
				for (int band = rings; band >= 0; --band) {
					const float radius = std::max(static_cast<float>(mode.RingRadius[band]) * scenePixel, 2.0F + 2.0F * static_cast<float>(band));
					outer = std::max(outer, radius);
					drawList->AddCircleFilled(at, radius, band == 0 ? IM_COL32(255, 210, 80, 140) : IM_COL32(255, 255, 255, 35), 32);
					drawList->AddCircle(at, radius, IM_COL32(255, 255, 255, 230), 32, 1.5F);
				}
				if (labels) {
					Tag(drawList, ImVec2(at.x, at.y + outer + ImGui::GetFontSize() * 0.7F), IM_COL32(255, 255, 255, 255), "The point");
				}
			}
			if ((mode.Mode == BattleMode::OneFlag && !mode.FlagByZones) || mode.Mode == BattleMode::CaptureTheShip) {
				// The flag positions (or the ship's), numbered in the order the flag comes in at them.
				for (int i = 0; i < static_cast<int>(mode.FlagSpots.size()); ++i) {
					ImVec2 at = toScreen(mode.FlagSpots[i]);
					float radius = 7.0F * size;
					drawList->AddCircleFilled(at, radius, IM_COL32(255, 255, 255, 230), 16);
					drawList->AddCircle(at, radius, IM_COL32(20, 20, 20, 230), 16, 1.5F);
					std::string number = std::to_string(i + 1);
					ImVec2 textSize = ImGui::CalcTextSize(number.c_str());
					drawList->AddText(ImVec2(at.x - textSize.x * 0.5F, at.y - textSize.y * 0.5F), IM_COL32(20, 20, 20, 255), number.c_str());
				}
			}
		}

		/// One line about a saved battle: its mode and the teams in it.
		std::string BattleSummary(const SavedBattle& battle) {
			std::string teams;
			for (int side = 0; side < c_Sides; ++side) {
				bool in = battle.Mode.Mode == BattleMode::Custom ? battle.Cards[side].Active : battle.Mode.Plays[side];
				if (in) {
					teams += (teams.empty() ? "" : ", ") + std::string(c_SideNames[side]);
				}
			}
			return std::string(c_ModeNames[std::clamp(static_cast<int>(battle.Mode.Mode), 0, static_cast<int>(BattleMode::Count) - 1)]) + (teams.empty() ? "" : ": " + teams);
		}

		/// A combo box of names with "Any" first: index -1 for it.
		bool AnyCombo(const char* label, int& value, const char* const* names, int count, float width) {
			bool changed = false;
			ImGui::SetNextItemWidth(width);
			if (ImGui::BeginCombo(label, value >= 0 && value < count ? names[value] : "Any")) {
				if (ImGui::Selectable("Any", value < 0)) {
					value = -1;
					changed = true;
				}
				for (int i = 0; i < count; ++i) {
					if (ImGui::Selectable(names[i], value == i)) {
						value = i;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			return changed;
		}

		/// The row of filters along the top.
		void Filters() {
			const float font = ImGui::GetFontSize();
			bool changed = false;
			ImGui::SetNextItemWidth(font * 14.0F);
			changed |= ImGui::InputTextWithHint("##mapSearch", "Search maps", s_Browser.Search, sizeof(s_Browser.Search));
			ImGui::SameLine();
			ImGui::TextUnformatted("Mod");
			ImGui::SameLine();
			{
				std::string current = "Any";
				std::vector<std::pair<int, std::string>> mods;
				for (const MapEntry& map: s_Browser.Maps) {
					if (std::none_of(mods.begin(), mods.end(), [&map](const auto& mod) { return mod.first == map.ModuleID; })) {
						mods.emplace_back(map.ModuleID, map.ModName);
					}
					if (map.ModuleID == s_Browser.Module) {
						current = map.ModName;
					}
				}
				std::sort(mods.begin(), mods.end(), [](const auto& a, const auto& b) { return Lowered(a.second) < Lowered(b.second); });
				ImGui::SetNextItemWidth(font * 11.0F);
				if (ImGui::BeginCombo("##mapMod", current.c_str())) {
					if (ImGui::Selectable("Any", s_Browser.Module < 0)) {
						s_Browser.Module = -1;
						changed = true;
					}
					for (const auto& [id, name]: mods) {
						ImGui::PushID(id);
						if (ImGui::Selectable(name.c_str(), s_Browser.Module == id)) {
							s_Browser.Module = id;
							changed = true;
						}
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}
			}
			ImGui::SameLine();
			ImGui::TextUnformatted("Type");
			ImGui::SameLine();
			changed |= AnyCombo("##mapSource", s_Browser.Source, c_SourceNames, static_cast<int>(MapSource::Count), font * 10.0F);
			ImGui::SetItemTooltip("Official: the game's own maps. Mod: maps from mods. Made in the editor: maps you saved from the scene editor.");
			ImGui::SameLine();
			ImGui::TextUnformatted("Size");
			ImGui::SameLine();
			changed |= AnyCombo("##mapSize", s_Browser.Size, c_SizeNames, static_cast<int>(std::size(c_SizeNames)), font * 7.5F);
			ImGui::SetItemTooltip("By the map's width: small under 3500 pixels (70 m), medium under 6500, large under 10000, extra large past that.");
			ImGui::SameLine();
			changed |= ToolUI::Checkbox("With saved battles", &s_Browser.OnlyWithBattles);
			ImGui::SetItemTooltip("Only maps with battles saved on them from the Battle tab or the battle panel.");
			ImGui::SameLine();
			changed |= ToolUI::Checkbox("Favourites only", &s_Browser.OnlyFavourites);
			ImGui::SetItemTooltip("Only the maps starred as favourites (click the star on a map's card). Favourites are listed first either way.");
			ImGui::SameLine();
			changed |= ToolUI::Checkbox("Recently played", &s_Browser.OnlyRecent);
			ImGui::SetItemTooltip("Only the last ten maps played, in the Sandbox, Battle Command or any other game, the latest first.");
			ImGui::SameLine();
			ImGui::TextUnformatted("Sort");
			ImGui::SameLine();
			int sort = static_cast<int>(s_Browser.Sort);
			ImGui::SetNextItemWidth(font * 8.5F);
			if (ImGui::Combo("##mapSort", &sort, c_SortNames, static_cast<int>(SortBy::Count))) {
				s_Browser.Sort = static_cast<SortBy>(sort);
				changed = true;
			}
			ImGui::SetItemTooltip("Favourites are listed first in any order. With \"Recently played\" ticked, the latest played comes first instead.");
			ImGui::SameLine();
			ImGui::BeginDisabled(s_Browser.Shown.empty());
			if (ToolUI::Button("Random map")) {
				// One of the maps shown, not the one picked already if there's another; Play (or Enter) then starts it as usual.
				static std::mt19937 s_Random(std::random_device{}());
				std::vector<int> others;
				for (int index: s_Browser.Shown) {
					if (index != s_Browser.Picked) {
						others.push_back(index);
					}
				}
				if (!others.empty()) {
					Pick(others[std::uniform_int_distribution<size_t>(0, others.size() - 1)(s_Random)]);
					s_Browser.ScrollToPicked = true;
				}
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Picks one of the maps shown, at random, with the filters as they are. Play (or Enter) starts it.");
			if (changed) {
				s_Browser.FiltersChanged = true;
				KeepFilters();
			}
		}

#pragma region Map maker
		/// Draws the map maker's picture again if its recipe has changed since.
		void RefreshGeneratorPicture() {
			Generator& generator = s_Generator;
			if (!generator.Seeded) {
				generator.Seeded = true;
				generator.Recipe.Seed = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count() * 2654435761ULL);
				std::snprintf(generator.SeedText, sizeof(generator.SeedText), "%08X", generator.Recipe.Seed);
			}
			std::string code = generator.Recipe.Code();
			if (code == generator.PictureOf && generator.Entry.Picture.Texture != 0) {
				return;
			}
			generator.PictureOf = code;
			MapGen::Picture picture = MapGen::Preview(generator.Recipe);
			UploadPicture(generator.Entry.Picture, picture.Pixels.data(), picture.Width, picture.Height, picture.Width);
			generator.Entry.Width = picture.MapWidth;
			generator.Entry.Height = picture.MapHeight;
			generator.Entry.Name = MapGen::MapName(generator.Recipe);
		}

		/// The map maker's card, first among the maps.
		void GeneratorCard(ImDrawList* drawList, float cardWidth, float cardHeight, float pictureHeight) {
			const ImGuiStyle& style = ImGui::GetStyle();
			const float lineHeight = ImGui::GetTextLineHeight();
			ImGui::PushID("##generator");
			ImVec2 from = ImGui::GetCursorScreenPos();
			bool clicked = ImGui::InvisibleButton("##card", ImVec2(cardWidth, cardHeight));
			bool hovered = ImGui::IsItemHovered();
			bool picked = s_Browser.Picked == c_GeneratorPick;
			if (clicked) {
				Pick(c_GeneratorPick);
			}
			ImGui::SetItemTooltip("Make a new map: pick a style, a size and a seed, and play on it.");
			ImVec2 to(from.x + cardWidth, from.y + cardHeight);
			if (ImGui::IsRectVisible(from, to)) {
				RefreshGeneratorPicture();
				drawList->AddRectFilled(from, to, picked ? ToolTheme::WellHover : hovered ? ImGui::GetColorU32(ImLerp(ToolTheme::Vec(ToolTheme::Panel), ToolTheme::Vec(ToolTheme::WellHover), 0.6F)) : ToolTheme::Panel);
				ImVec2 pictureFrom;
				float scenePixel = 1.0F;
				DrawPictureIn(drawList, s_Generator.Entry, ImVec2(from.x + style.FramePadding.y, from.y + style.FramePadding.y), ImVec2(to.x - style.FramePadding.y, from.y + style.FramePadding.y + pictureHeight), pictureFrom, scenePixel);
				float textY = from.y + style.FramePadding.y * 2.0F + pictureHeight;
				drawList->PushClipRect(from, to, true);
				drawList->AddText(ImVec2(from.x + style.FramePadding.x, textY), ToolTheme::Gold, "Make a new map");
				std::string details = std::string(MapGen::StyleName(s_Generator.Recipe.MapStyle)) + ", " + MapGen::SizeName(s_Generator.Recipe.MapSize);
				drawList->AddText(ImVec2(from.x + style.FramePadding.x, textY + lineHeight), IM_COL32(190, 196, 220, 255), details.c_str());
				const char* badge = "New";
				ImVec2 size = ImGui::CalcTextSize(badge);
				ImVec2 at(to.x - size.x - style.FramePadding.x * 2.0F - style.FramePadding.y, from.y + style.FramePadding.y * 2.0F);
				drawList->AddRectFilled(at, ImVec2(at.x + size.x + style.FramePadding.x * 2.0F, at.y + size.y + 2.0F), IM_COL32(0, 0, 0, 190));
				drawList->AddText(ImVec2(at.x + style.FramePadding.x, at.y + 1.0F), ToolTheme::Gold, badge);
				drawList->PopClipRect();
				drawList->AddRect(from, to, picked ? ToolTheme::Gold : ToolTheme::Edge, 0.0F, 0, picked ? 2.0F : 1.0F);
			}
			ImGui::PopID();
		}

		/// The map maker picked: its picture big, its choices, and the button that makes the map and plays it.
		void GeneratorDetails(float width, float height) {
			Generator& generator = s_Generator;
			MapGen::Recipe& recipe = generator.Recipe;
			RefreshGeneratorPicture();
			const float font = ImGui::GetFontSize();
			const float room = ImGui::GetContentRegionAvail().x;
			const float pictureHeight = std::min(room * 0.6F, height * 0.42F);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 from = ImGui::GetCursorScreenPos();
			ImVec2 to(from.x + room, from.y + pictureHeight);
			ImGui::Dummy(ImVec2(room, pictureHeight));
			ImVec2 pictureFrom;
			float scenePixel = 1.0F;
			DrawPictureIn(drawList, generator.Entry, from, to, pictureFrom, scenePixel);
			drawList->AddRect(from, to, ToolTheme::Edge);

			ImGui::PushStyleColor(ImGuiCol_Text, ToolTheme::Vec(ToolTheme::Gold));
			ImGui::TextUnformatted("Make a new map");
			ImGui::PopStyleColor();
			ImGui::TextDisabled("%d x %d m. The picture shows the map's layout; the map itself is finer.", generator.Entry.Width / static_cast<int>(c_PPM), generator.Entry.Height / static_cast<int>(c_PPM));

			const float playHeight = ImGui::GetFrameHeight() * 1.6F;
			ImGui::BeginChild("##generatorChoices", ImVec2(0.0F, -(playHeight + ImGui::GetStyle().ItemSpacing.y)), ImGuiChildFlags_None);
			const float labelWidth = font * 6.0F;
			const float fieldWidth = std::max(font * 8.0F, ImGui::GetContentRegionAvail().x - labelWidth);
			auto label = [labelWidth](const char* text) {
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(text);
				ImGui::SameLine(labelWidth);
			};

			label("Style");
			ImGui::SetNextItemWidth(fieldWidth);
			if (ImGui::BeginCombo("##genStyle", MapGen::StyleName(recipe.MapStyle))) {
				for (int i = 0; i < static_cast<int>(MapGen::Style::Count); ++i) {
					MapGen::Style style = static_cast<MapGen::Style>(i);
					if (ImGui::Selectable(MapGen::StyleName(style), recipe.MapStyle == style)) {
						recipe.MapStyle = style;
					}
					ImGui::SetItemTooltip("%s", MapGen::StyleBlurb(style));
				}
				ImGui::EndCombo();
			}
			ImGui::Indent(labelWidth);
			ImGui::PushTextWrapPos(0.0F);
			ImGui::TextDisabled("%s", MapGen::StyleBlurb(recipe.MapStyle));
			ImGui::PopTextWrapPos();
			ImGui::Unindent(labelWidth);

			label("Size");
			ImGui::SetNextItemWidth(fieldWidth);
			if (ImGui::BeginCombo("##genSize", MapGen::SizeName(recipe.MapSize))) {
				for (int i = 0; i < static_cast<int>(MapGen::Size::Count); ++i) {
					MapGen::Size size = static_cast<MapGen::Size>(i);
					if (ImGui::Selectable(MapGen::SizeName(size), recipe.MapSize == size)) {
						recipe.MapSize = size;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SetItemTooltip("Sized like the game's own maps: small like Grasslands, medium like Fredeleig Plains, large like Alezer Canyon, extra large like Decision Day. The seed picks the exact size within that.");

			label("Seed");
			const float diceWidth = ImGui::CalcTextSize("New seed").x + ImGui::GetStyle().FramePadding.x * 4.0F;
			ImGui::SetNextItemWidth(std::max(font * 5.0F, fieldWidth - diceWidth - ImGui::GetStyle().ItemSpacing.x));
			if (ImGui::InputText("##genSeed", generator.SeedText, sizeof(generator.SeedText), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase)) {
				recipe.Seed = static_cast<uint32_t>(std::strtoul(generator.SeedText, nullptr, 16));
			}
			ImGui::SetItemTooltip("The same seed with the same choices always makes the same map.");
			ImGui::SameLine();
			if (ToolUI::Button("New seed", ImVec2(diceWidth, 0.0F))) {
				recipe.Seed = recipe.Seed * 1664525U + 1013904223U + static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
				std::snprintf(generator.SeedText, sizeof(generator.SeedText), "%08X", recipe.Seed);
			}
			ImGui::SetItemTooltip("Another map of the same style and size.");

			label("Roughness");
			ImGui::SetNextItemWidth(fieldWidth);
			ImGui::SliderInt("##genRough", &recipe.Roughness, 0, 100, "%d%%");
			ImGui::SetItemTooltip("How hilly, steep and broken the ground is.");
			label("Caves");
			ImGui::SetNextItemWidth(fieldWidth);
			ImGui::SliderInt("##genCaves", &recipe.Caves, 0, 100, "%d%%");
			ImGui::SetItemTooltip("How many caves and tunnels run through the ground (and how open the Caves and Honeycomb styles are).");
			bool water = MapGen::HasGround(recipe.MapStyle) && recipe.MapStyle != MapGen::Style::Caves && recipe.MapStyle != MapGen::Style::Honeycomb;
			ImGui::BeginDisabled(!water);
			label("Water");
			ImGui::SetNextItemWidth(fieldWidth);
			ImGui::SliderInt("##genWater", &recipe.Water, 0, 100, "%d%%");
			ImGui::EndDisabled();
			ImGui::SetItemTooltip(water ? "How full the hollows of the ground are with water." : "This style has no hollows for water to lie in.");

			ImGui::Indent(labelWidth);
			ToolUI::Checkbox("Bunkers", &recipe.Bunkers);
			ImGui::SetItemTooltip("A simple concrete bunker at each end, lit, with a doorway each side.");
			ImGui::SameLine();
			ToolUI::Checkbox("Wrap round", &recipe.WrapX);
			ImGui::SetItemTooltip("The map wraps round: going off one end brings you in at the other.");
			ImGui::Unindent(labelWidth);

			if (!generator.Error.empty()) {
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 110, 100, 255));
				ImGui::TextWrapped("Couldn't make the map: %s", generator.Error.c_str());
				ImGui::PopStyleColor();
			}
			ImGui::EndChild();

			bool busy = MapGen::Busy();
			ImGui::BeginDisabled(busy);
			if (ToolUI::Button(busy ? "Making the map..." : "Make the map and play", ImVec2(-1.0F, playHeight))) {
				MakeAndPlay();
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip(s_Browser.For == MapBrowser::Purpose::Battle ? "Makes the map and starts Battle Command on it." : "Makes the map and starts the Sandbox on it.");
		}

		/// Takes a map once it's made, and starts the game on it if Play asked for that.
		void UpdateGenerator() {
			std::string name;
			std::string error;
			if (!MapGen::Update(name, error)) {
				return;
			}
			if (!error.empty()) {
				s_Generator.Error = error;
				s_Generator.PlayWhenMade = false;
				return;
			}
			if (s_Generator.PlayWhenMade && s_Browser.Open) {
				MapBrowser::Choice& choice = s_Browser.Choice;
				choice = MapBrowser::Choice();
				choice.Play = true;
				choice.For = s_Browser.For;
				choice.Scene = name;
				choice.NoBattle = true;
			}
			s_Generator.PlayWhenMade = false;
		}
#pragma endregion

		/// The grid of cards, in a scrolling area.
		void Cards(float width, float height) {
			const float font = ImGui::GetFontSize();
			if (!ImGui::BeginChild("##mapCards", ImVec2(width, height), ImGuiChildFlags_None)) {
				ImGui::EndChild();
				return;
			}
			const ImGuiStyle& style = ImGui::GetStyle();
			const float room = ImGui::GetContentRegionAvail().x;
			const int columns = std::max(1, static_cast<int>((room + style.ItemSpacing.x) / (font * 17.0F + style.ItemSpacing.x)));
			const float cardWidth = std::floor((room - style.ItemSpacing.x * static_cast<float>(columns - 1)) / static_cast<float>(columns));
			const float pictureHeight = std::floor(cardWidth * 0.5F);
			const float lineHeight = ImGui::GetTextLineHeight();
			const float cardHeight = pictureHeight + lineHeight * 2.0F + style.FramePadding.y * 3.0F;
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			// The map maker's card first, whatever the filters.
			GeneratorCard(drawList, cardWidth, cardHeight, pictureHeight);
			for (int i = 0; i < static_cast<int>(s_Browser.Shown.size()); ++i) {
				int index = s_Browser.Shown[i];
				MapEntry& map = s_Browser.Maps[index];
				if ((i + 1) % columns != 0) {
					ImGui::SameLine();
				}
				ImGui::PushID(index);
				ImVec2 from = ImGui::GetCursorScreenPos();
				bool clicked = ImGui::InvisibleButton("##card", ImVec2(cardWidth, cardHeight));
				bool hovered = ImGui::IsItemHovered();
				bool picked = index == s_Browser.Picked;
				if (picked && s_Browser.ScrollToPicked) {
					ImGui::SetScrollHereY(0.3F);
					s_Browser.ScrollToPicked = false;
				}
				// The favourite star, in the picture's top left corner: a click there stars the map rather than picking it.
				const float starRadius = std::floor(font * 0.6F);
				const ImVec2 star(from.x + style.FramePadding.y * 2.0F + starRadius * 1.3F, from.y + style.FramePadding.y * 2.0F + starRadius * 1.3F);
				const bool overStar = hovered && ImGui::IsMouseHoveringRect(ImVec2(star.x - starRadius * 1.3F, star.y - starRadius * 1.3F), ImVec2(star.x + starRadius * 1.3F, star.y + starRadius * 1.3F));
				if (clicked && overStar) {
					ToggleFavourite(map.Name);
				} else if (clicked) {
					Pick(index);
				}
				if (overStar) {
					ImGui::SetTooltip(IsFavourite(map.Name) ? "A favourite: click to unstar it" : "Click to star it as a favourite");
				}
				if (hovered && !overStar && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					Pick(index);
					Play();
				}
				ImVec2 to(from.x + cardWidth, from.y + cardHeight);
				if (ImGui::IsRectVisible(from, to)) {
					WantPicture(index, false);
					drawList->AddRectFilled(from, to, picked ? ToolTheme::WellHover : hovered ? ImGui::GetColorU32(ImLerp(ToolTheme::Vec(ToolTheme::Panel), ToolTheme::Vec(ToolTheme::WellHover), 0.6F)) : ToolTheme::Panel);
					ImVec2 pictureFrom;
					float scenePixel = 1.0F;
					ImVec2 pictureTo(to.x - style.FramePadding.y, from.y + style.FramePadding.y + pictureHeight);
					if (DrawPictureIn(drawList, map, ImVec2(from.x + style.FramePadding.y, from.y + style.FramePadding.y), pictureTo, pictureFrom, scenePixel) && s_Browser.For == MapBrowser::Purpose::Battle) {
						// Battle: the battle it would start with, small, without labels.
						if (int battle = picked ? s_Browser.PickedBattle : DefaultBattle(map, s_Browser.For); battle >= 0 && battle < static_cast<int>(map.Battles.size())) {
							drawList->PushClipRect(ImVec2(from.x + style.FramePadding.y, from.y + style.FramePadding.y), pictureTo, true);
							DrawBattle(drawList, map.Battles[battle], pictureFrom, scenePixel, false);
							drawList->PopClipRect();
						}
					}
					float textY = from.y + style.FramePadding.y * 2.0F + pictureHeight;
					drawList->PushClipRect(from, to, true);
					drawList->AddText(ImVec2(from.x + style.FramePadding.x, textY), ToolTheme::Gold, map.Name.c_str());
					std::string details = map.ModName + "  " + std::to_string(map.Width / static_cast<int>(c_PPM)) + " x " + std::to_string(map.Height / static_cast<int>(c_PPM)) + " m";
					drawList->AddText(ImVec2(from.x + style.FramePadding.x, textY + lineHeight), IM_COL32(190, 196, 220, 255), details.c_str());
					if (!map.Battles.empty()) {
						std::string battles = std::to_string(map.Battles.size()) + (map.Battles.size() == 1 ? " battle" : " battles");
						ImVec2 size = ImGui::CalcTextSize(battles.c_str());
						ImVec2 badge(to.x - size.x - style.FramePadding.x * 2.0F - style.FramePadding.y, from.y + style.FramePadding.y * 2.0F);
						drawList->AddRectFilled(badge, ImVec2(badge.x + size.x + style.FramePadding.x * 2.0F, badge.y + size.y + 2.0F), IM_COL32(0, 0, 0, 190));
						drawList->AddText(ImVec2(badge.x + style.FramePadding.x, badge.y + 1.0F), ToolTheme::Gold, battles.c_str());
					}
					drawList->PopClipRect();
					DrawStar(drawList, star, starRadius, IsFavourite(map.Name), overStar);
					drawList->AddRect(from, to, picked ? ToolTheme::Gold : ToolTheme::Edge, 0.0F, 0, picked ? 2.0F : 1.0F);
				}
				ImGui::PopID();
			}
			if (s_Browser.Shown.empty()) {
				ImGui::Spacing();
				ImGui::TextDisabled(s_Browser.OnlyFavourites && Favourites().empty() ? "No favourites yet. Click the star on a map's card to make it one." : s_Browser.OnlyRecent && RecentMaps().empty() ? "No maps played yet. The maps you play are listed here, the latest first." : s_Browser.OnlyWithBattles ? "No map matches. Untick \"With saved battles\" to see maps with no battles saved yet." : "No map matches.");
			}
			ImGui::EndChild();
		}

		/// The map picked, big, with its saved battles and the Play button.
		void Details(float width, float height) {
			if (!ImGui::BeginChild("##mapDetails", ImVec2(width, height), ImGuiChildFlags_Borders)) {
				ImGui::EndChild();
				return;
			}
			if (s_Browser.Picked == c_GeneratorPick) {
				GeneratorDetails(width, height);
				ImGui::EndChild();
				return;
			}
			if (s_Browser.Picked < 0) {
				ImGui::TextWrapped("Pick a map to see it bigger, with the battles saved on it.");
				ImGui::EndChild();
				return;
			}
			MapEntry& map = s_Browser.Maps[s_Browser.Picked];
			WantPicture(s_Browser.Picked, true);
			const float room = ImGui::GetContentRegionAvail().x;
			const float pictureHeight = std::min(room * 0.6F, height * 0.5F);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 from = ImGui::GetCursorScreenPos();
			ImVec2 to(from.x + room, from.y + pictureHeight);
			ImGui::Dummy(ImVec2(room, pictureHeight));
			ImVec2 pictureFrom;
			float scenePixel = 1.0F;
			if (DrawPictureIn(drawList, map, from, to, pictureFrom, scenePixel) && s_Browser.PickedBattle >= 0 && s_Browser.PickedBattle < static_cast<int>(map.Battles.size())) {
				drawList->PushClipRect(from, to, true);
				DrawBattle(drawList, map.Battles[s_Browser.PickedBattle], pictureFrom, scenePixel, true);
				drawList->PopClipRect();
			}
			drawList->AddRect(from, to, ToolTheme::Edge);

			{
				// The favourite star, then the name beside it.
				const float side = ImGui::GetFrameHeight();
				ImVec2 at = ImGui::GetCursorScreenPos();
				if (ImGui::InvisibleButton("##favourite", ImVec2(side, side))) {
					ToggleFavourite(map.Name);
				}
				bool hovered = ImGui::IsItemHovered();
				ImGui::SetItemTooltip(IsFavourite(map.Name) ? "A favourite: click to unstar it" : "Click to star it as a favourite");
				DrawStar(drawList, ImVec2(at.x + side * 0.5F, at.y + side * 0.5F), side * 0.38F, IsFavourite(map.Name), hovered);
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
			}
			ImGui::PushStyleColor(ImGuiCol_Text, ToolTheme::Vec(ToolTheme::Gold));
			ImGui::TextWrapped("%s", map.Name.c_str());
			ImGui::PopStyleColor();
			ImGui::TextDisabled("%s (%s)  %d x %d m, %s", map.ModName.c_str(), c_SourceNames[static_cast<int>(map.Source)], map.Width / static_cast<int>(c_PPM), map.Height / static_cast<int>(c_PPM), c_SizeNames[map.Size]);

			// Play stays at the bottom; the description and the battles scroll above it.
			const float playHeight = ImGui::GetFrameHeight() * 1.6F;
			ImGui::BeginChild("##mapInfo", ImVec2(0.0F, -(playHeight + ImGui::GetStyle().ItemSpacing.y)), ImGuiChildFlags_None);
			if (!map.Description.empty()) {
				ImGui::TextWrapped("%s", map.Description.c_str());
			}
			ImGui::Spacing();
			ImGui::SeparatorText(map.Battles.empty() ? "No saved battles" : "Saved battles");
			if (map.Battles.empty()) {
				ImGui::TextWrapped("Battles saved on this map from the Battle tab (in the Sandbox) or the battle panel (in Battle Command) show here.");
			} else {
				if (ImGui::Selectable("Map only, no battle set up", s_Browser.PickedBattle < 0)) {
					s_Browser.PickedBattle = -1;
				}
				for (int i = 0; i < static_cast<int>(map.Battles.size()); ++i) {
					const SavedBattle& battle = map.Battles[i];
					ImGui::PushID(i);
					std::string label = battle.Name + (battle.WithMap ? "  (loads with the map)" : "") + (battle.Linked ? "  (linked)" : "");
					if (ImGui::Selectable(label.c_str(), s_Browser.PickedBattle == i)) {
						s_Browser.PickedBattle = i;
					}
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						s_Browser.PickedBattle = i;
						Play();
					}
					ImGui::Indent();
					ImGui::TextDisabled("%s", BattleSummary(battle).c_str());
					ImGui::Unindent();
					ImGui::PopID();
				}
			}
			ImGui::EndChild();

			std::string play = "Play";
			if (s_Browser.PickedBattle >= 0 && s_Browser.PickedBattle < static_cast<int>(map.Battles.size())) {
				play += ": " + map.Battles[s_Browser.PickedBattle].Name;
			}
			if (ToolUI::Button(play.c_str(), ImVec2(-1.0F, playHeight))) {
				Play();
			}
			ImGui::SetItemTooltip("%s", s_Browser.QuickBattle ? "Starts Battle Command on this map with the battle picked, and the battle itself at once (if it's ready to start; else it waits on the battle panel)." : s_Browser.For == MapBrowser::Purpose::Battle ? "Starts Battle Command on this map, with the battle picked set up on the battle panel." : "Starts the Sandbox on this map, with the battle picked set up on the Battle tab.");
			ImGui::EndChild();
		}
#pragma endregion
	} // namespace
} // namespace SandboxDetail

using namespace SandboxDetail;

void MapBrowser::Open(Purpose purpose, bool quickBattle) {
	s_Browser.Open = true;
	s_Browser.For = purpose;
	s_Browser.QuickBattle = quickBattle && purpose == Purpose::Battle;
	s_Browser.Choice = Choice();
	ReadKeptFilters();
	s_Browser.OnlyWithBattles = purpose == Purpose::Battle;
	// Read afresh each time: battles may have been saved, or mods turned on, since it was last open.
	std::string picked = s_Browser.Picked >= 0 && s_Browser.Picked < static_cast<int>(s_Browser.Maps.size()) ? s_Browser.Maps[s_Browser.Picked].Name : "";
	bool generatorPicked = s_Browser.Picked == c_GeneratorPick;
	s_Generator.PlayWhenMade = false;
	ListMaps();
	s_Browser.Picked = generatorPicked ? c_GeneratorPick : -1;
	for (int i = 0; i < static_cast<int>(s_Browser.Maps.size()); ++i) {
		if (s_Browser.Maps[i].Name == picked) {
			Pick(i);
			s_Browser.ScrollToPicked = true;
		}
	}
	FilterMaps();
	// The map picked last time, if the filters still show it; else the first shown.
	if (!generatorPicked && std::find(s_Browser.Shown.begin(), s_Browser.Shown.end(), s_Browser.Picked) == s_Browser.Shown.end()) {
		s_Browser.Picked = -1;
		Pick(s_Browser.Shown.empty() ? -1 : s_Browser.Shown.front());
	}
}

void MapBrowser::Close() {
	s_Browser.Open = false;
	for (MapEntry& map: s_Browser.Maps) {
		FreePicture(map.Picture);
	}
	FreePicture(s_Generator.Entry.Picture);
	s_Generator.PictureOf.clear();
	s_Generator.PlayWhenMade = false;
	s_Browser.ToDraw.clear();
}

bool MapBrowser::IsOpen() {
	return s_Browser.Open;
}

MapBrowser::Choice MapBrowser::TakeChoice() {
	Choice choice = s_Browser.Choice;
	s_Browser.Choice = Choice();
	return choice;
}

bool MapBrowser::SetStartChoice(const Choice& choice) {
	if (!choice.Play || !g_ActivityMan.SetStartSandboxActivity(choice.Scene, ActivityName(choice.For))) {
		return false;
	}
	LoadBattleOnStart(choice.Battle, choice.NoBattle);
	StartBattleOnStart(choice.StartBattle && !choice.NoBattle);
	return true;
}

void MapBrowser::NoteMapPlayed(const std::string& mapName) {
	if (mapName.empty()) {
		return;
	}
	g_SettingsMan.SetLastMap(mapName);
	std::vector<std::string>& recent = RecentMaps();
	std::erase(recent, mapName);
	recent.insert(recent.begin(), mapName);
	if (recent.size() > c_RecentKept) {
		recent.resize(c_RecentKept);
	}
	std::error_code error;
	std::filesystem::create_directories(std::filesystem::path(c_RecentMapsFile).parent_path(), error);
	std::ofstream file(c_RecentMapsFile, std::ios::trunc);
	for (const std::string& name: recent) {
		file << name << '\n';
	}
	s_Browser.FiltersChanged = true;
}

void MapBrowser::DrawGUI() {
	UpdateGenerator();
	if (!s_Browser.Open || !g_MenuMan.GetIsInMenuScreen()) {
		return;
	}
	// (The menu's own pointer is under the window: ImGui draws one over it.)
	ImGui::GetIO().MouseDrawCursor = true;
	if (s_Browser.FiltersChanged) {
		FilterMaps();
	}
	MakeWantedPictures();

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float margin = std::floor(std::min(viewport->Size.x, viewport->Size.y) * 0.03F);
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + margin, viewport->Pos.y + margin));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - margin * 2.0F, viewport->Size.y - margin * 2.0F));
	std::string title = std::string(s_Browser.QuickBattle ? "Quick battle: pick a map and a saved battle" : s_Browser.For == Purpose::Battle ? "Battle Command: pick a map" : "Sandbox: pick a map") + "###MapBrowser";
	if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) {
		const ImGuiStyle& style = ImGui::GetStyle();
		const float backWidth = ImGui::CalcTextSize("Back").x + style.FramePadding.x * 4.0F;
		if (ToolUI::Button("Back", ImVec2(backWidth, 0.0F)) || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput)) {
			s_Browser.Choice = Choice();
			s_Browser.Choice.Back = true;
			s_Generator.PlayWhenMade = false;
		}
		ImGui::SameLine();
		Filters();
		ImGui::TextDisabled("%d of %d maps", static_cast<int>(s_Browser.Shown.size()), static_cast<int>(s_Browser.Maps.size()));
		if (!s_Browser.ToDraw.empty()) {
			ImGui::SameLine();
			ImGui::TextDisabled("   Drawing map pictures: %d to go (kept for next time)", static_cast<int>(s_Browser.ToDraw.size()));
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::GetIO().WantTextInput) {
			Play();
		}

		ImVec2 room = ImGui::GetContentRegionAvail();
		float detailsWidth = std::floor(std::clamp(room.x * 0.36F, std::min(room.x * 0.5F, ImGui::GetFontSize() * 18.0F), ImGui::GetFontSize() * 40.0F));
		Cards(room.x - detailsWidth - style.ItemSpacing.x, room.y);
		ImGui::SameLine();
		Details(detailsWidth, room.y);
	}
	ImGui::End();
}
