#pragma once

#include <atomic>
#include <climits>
#include "SceneLayer.h"
#include "Matrix.h"
#include "Color.h"

#include <mutex>
#include <vector>
#include <memory>

namespace RTE {

	class MOPixel;
	class TerrainFrosting;
	class TerrainObject;
	class TerrainDebris;
	class Camera;

	/// A light that is part of the scenery: a lamp on a bunker wall, a floodlight, a warning light. It stays where it is and shines until what holds it is destroyed.
	/// On a TerrainObject its position is an offset from the top left corner of the object's bitmaps; on the terrain it's a scene position.
	class TerrainLight : public Serializable {

	public:
		SerializableClassNameGetter;
		SerializableOverrideMethods;

		TerrainLight() { Clear(); }

		void Reset() override { Clear(); }

		Vector m_Pos; //!< Where the light is.
		Color m_Color; //!< Its color, 0-255.
		float m_Radius; //!< How far it reaches, in pixels.
		float m_Intensity; //!< How bright it is.
		float m_Flicker; //!< How much it flickers at random, 0 to 1.
		float m_Pulse; //!< How many times a second it swells and fades, 0 for a steady light.
		float m_ConeAngle; //!< Half-angle of its beam in degrees, 0 for a light that shines all round.
		float m_ConeDirection; //!< Which way the beam points, in degrees clockwise from pointing right (90 is straight down).
		int m_Anchored; //!< Whether it hangs on something solid, and so goes out when that is destroyed. -1 until that's been looked up.
		Vector m_AnchorOffset; //!< Where the solid thing it hangs on is, from the light.
		int m_AnchorSolid; //!< How many solid pixels there were in the fixture around the anchor when it was looked up. The lamp goes out when fewer than half are left.

	private:
		static const std::string c_ClassName; //!< A string with the friendly-formatted type name of this.

		void Clear();
	};

	/// Collection of scrolling layers that compose the terrain of the Scene.
	class SLTerrain : public SceneLayer {

	public:
		EntityAllocation(SLTerrain);
		SerializableOverrideMethods;
		ClassInfoGetters;

		/// Enumeration for the different type of layers in the SLTerrain.
		enum class LayerType {
			ForegroundLayer,
			BackgroundLayer,
			MaterialLayer
		};

#pragma region Creation
		/// Constructor method used to instantiate a SLTerrain object in system memory. Create() should be called before using the object.
		SLTerrain();

		/// Makes the SLTerrain object ready for use.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int Create() override;

		/// Creates a SLTerrain to be identical to another, by deep copy.
		/// @param reference A reference to the SLTerrain to deep copy.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int Create(const SLTerrain& reference);
#pragma endregion

#pragma region Destruction
		/// Destructor method used to clean up a SLTerrain object before deletion from system memory.
		~SLTerrain() override;

		/// Destroys and resets (through Clear()) the SLTerrain object.
		/// @param notInherited Whether to only destroy the members defined in this derived class, or to destroy all inherited members also.
		void Destroy(bool notInherited = false) override {
			if (!notInherited) {
				SceneLayer::Destroy();
			}
			Clear();
		}
#pragma endregion

#pragma region Data Handling
		/// Whether this SLTerrain's bitmap data is loaded from a file or was generated at runtime.
		/// @return Whether this SLTerrain's bitmap data was loaded from a file or was generated at runtime.
		bool IsLoadedFromDisk() const override { return (m_FGColorLayer && m_FGColorLayer->IsLoadedFromDisk()) && (m_BGColorLayer && m_BGColorLayer->IsLoadedFromDisk()); }

		/// Loads previously specified/created bitmap data into memory. Has to be done before using this SLTerrain if the bitmap was not generated at runtime.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int LoadData() override;

		/// Saves bitmap data currently in memory to disk.
		/// @param pathBase The filepath base to the where to save the Bitmap data. This means everything up to the extension. "FG" and "Mat" etc will be added.
		/// @param doAsyncSaves Whether or not to save asynchronously.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int SaveData(const std::string& pathBase) override;

		/// Copies bitmap data into layerInfos.
		/// @param layerInfos List of SceneLayerInfo to emplace our copied data into.
		void CopyBitmapData(std::vector<SceneLayerInfo>& layerInfos) const;

		/// Clears out any previously loaded bitmap data from memory.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int ClearData() override;
#pragma endregion

#pragma region Getters and Setters
		/// Gets the width of this SLTerrain as determined by the main (material) bitmap.
		/// @return The width of this SLTerrain, in pixels.
		int GetWidth() const { return m_Width; }

		/// Gets the height of this SLTerrain as determined by the main (material) bitmap.
		/// @return The height of this SLTerrain, in pixels.
		int GetHeight() const { return m_Height; }

		/// Sets the layer of this SLTerrain that should be drawn to the screen when Draw() is called.
		/// @param layerToDraw The layer that should be drawn. See LayerType enumeration.
		void SetLayerToDraw(LayerType layerToDraw) { m_LayerToDraw = layerToDraw; }

		/// Gets the foreground scenelayer of this SLTerrain.
		/// @return A pointer to the foreground scenelayer.
		SceneLayer* GetFGSceneLayer() { return m_FGColorLayer.get(); }

		/// Gets the background scenelayer of this SLTerrain.
		/// @return A pointer to the background scenelayer.
		SceneLayer* GetBGSceneLayer() { return m_BGColorLayer.get(); }

		/// Gets the foreground color bitmap of this SLTerrain.
		/// @return A pointer to the foreground color bitmap.
		BITMAP* GetFGColorBitmap() {
			m_FGColorLayer->SetUpdated();
			return m_FGColorLayer->GetBitmap();
		}

		/// Gets the background color bitmap of this SLTerrain.
		/// @return A pointer to the background color bitmap.
		BITMAP* GetBGColorBitmap() {
			m_BGColorLayer->SetUpdated();
			return m_BGColorLayer->GetBitmap();
		}

		/// Gets the material bitmap of this SLTerrain.
		/// @return A pointer to the material bitmap.
		BITMAP* GetMaterialBitmap() { return m_MainBitmap; }

		/// Gets the GPU copy of the material bitmap, for the terrain shader to tell what each pixel is made of (Terrain.frag's rteMaterialMap).
		/// Kept current for what's on screen while the colour layers are drawn.
		/// @return The texture, or 0 if there's none or the scene is too big for it to be a single texture.
		unsigned int GetMaterialTextureId() const;

		/// Gets a specific pixel from the foreground color bitmap of this.
		/// @param pixelX The X coordinate of the pixel to get.
		/// @param pixelY The Y coordinate of the pixel to get.
		/// @return An int specifying the requested pixel's foreground color index.
		int GetFGColorPixel(int pixelX, int pixelY) const { return m_FGColorLayer->GetPixel(pixelX, pixelY); }

		/// Sets a specific pixel on the foreground color bitmap of this SLTerrain to a specific color.
		/// @param pixelX The X coordinate of the pixel to set.
		/// @param pixelY The Y coordinate of the pixel to set.
		/// @param materialID The color index to set the pixel to.
		void SetFGColorPixel(int pixelX, int pixelY, const int materialID) const { m_FGColorLayer->SetPixel(pixelX, pixelY, materialID); }

		/// Gets a specific pixel from the background color bitmap of this.
		/// @param pixelX The X coordinate of the pixel to get.
		/// @param pixelY The Y coordinate of the pixel to get.
		/// @return An int specifying the requested pixel's background color index.
		int GetBGColorPixel(int pixelX, int pixelY) const { return m_BGColorLayer->GetPixel(pixelX, pixelY); }

		/// Sets a specific pixel on the background color bitmap of this SLTerrain to a specific color.
		/// @param pixelX The X coordinate of the pixel to set.
		/// @param pixelY The Y coordinate of the pixel to set.
		/// @param materialID The color index to set the pixel to.
		void SetBGColorPixel(int pixelX, int pixelY, int materialID) const { m_BGColorLayer->SetPixel(pixelX, pixelY, materialID); }

		/// Gets a specific pixel from the material bitmap of this SceneLayer.
		/// @param pixelX The X coordinate of the pixel to get.
		/// @param pixelY The Y coordinate of the pixel to get.
		/// @return An int specifying the requested pixel's material index.
		int GetMaterialPixel(int pixelX, int pixelY) const { return GetPixel(pixelX, pixelY); }

		/// Sets a specific pixel on the material bitmap of this SLTerrain to a specific material.
		/// @param pixelX The X coordinate of the pixel to set.
		/// @param pixelY The Y coordinate of the pixel to set.
		/// @param materialID The material index to set the pixel to.
		void SetMaterialPixel(int pixelX, int pixelY, int materialID) {
			SetPixel(pixelX, pixelY, materialID);
			NoteMaterialChange(pixelX, pixelY);
		}

		/// Records that the material pixels in a box changed, for terrain changed by drawing straight to the material bitmap rather than through SetMaterialPixel. Safe to call from any thread.
		/// The box may be unwrapped (running past the scene's edges): on a wrapping axis it is wrapped round, on the other the part outside is dropped.
		/// @param minX, minY, maxX, maxY The box, in scene pixels, both ends included.
		static void NoteMaterialChangeBox(int minX, int minY, int maxX, int maxY);

		/// The size of a tile of the changed-terrain map, in scene pixels (see TakeChangedTiles).
		static constexpr int c_ChangeTileSize = 32;

		/// Takes the tiles of the changed-terrain map that had a material pixel changed since this was last called, for whatever keeps its own picture of the
		/// terrain up to date (the lighting's grid), and clears them. Changes in separate places stay separate: two craters at either end of the map are two
		/// tiles, not the scene between them.
		/// @param tiles Filled with one bit per tile, row by row, 64 tiles a word, bit (index % 64) of word (index / 64) for tile index y * tilesWide + x.
		/// @param tilesWide, tilesHigh Set to the map's size in tiles (c_ChangeTileSize pixels each); 0 when no terrain has been loaded.
		/// @return Whether any tile had changed.
		static bool TakeChangedTiles(std::vector<uint64_t>& tiles, int& tilesWide, int& tilesHigh);

		/// Indicates whether a terrain pixel is of Air or Cavity material.
		/// @param pixelX The X coordinate of the pixel to check.
		/// @param pixelY The Y coordinate of the pixel to check.
		/// @return Whether the terrain pixel is of Air or Cavity material.
		bool IsAirPixel(int pixelX, int pixelY) const;

		/// Checks whether a bounding box is completely buried in the terrain.
		/// @param checkBox The box to check.
		/// @return Whether the box is completely buried, i.e. no corner sticks out in the Air or Cavity.
		bool IsBoxBuried(const Box& checkBox) const;
#pragma endregion

#pragma region Concrete Methods
		/// Gets a deque of unwrapped boxes which show the areas where the material layer has had objects applied to it since last call to ClearUpdatedMaterialAreas().
		/// @return Reference to the deque that has been filled with Boxes which are unwrapped and may be out of bounds of the scene!
		std::deque<Box>& GetUpdatedMaterialAreas() { return m_UpdatedMaterialAreas; }

		/// Adds a notification that an area of the material terrain has been updated.
		/// @param newArea The Box defining the newly updated material area that can be unwrapped and may be out of bounds of the scene.
		void AddUpdatedMaterialArea(const Box& newArea) { m_UpdatedMaterialAreas.emplace_back(newArea); }

		/// Adds a light to the scenery. One already at the same spot is replaced.
		/// @param light The light, with its position in scene coordinates.
		void AddLight(const TerrainLight& light);

		/// Queues a light to be added to the scenery on the next light update, as AddLight would. Safe to call from any thread, for Lua scripts (ThreadedUpdate runs them in parallel).
		/// @param light The light, with its position in scene coordinates.
		void QueueLight(const TerrainLight& light);

		/// Removes the scenery lights for which the test says so.
		/// @return How many were removed.
		int RemoveLights(const std::function<bool(const TerrainLight&)>& shouldRemove);

		/// Gets the lights of the scenery.
		const std::vector<TerrainLight>& GetLights() const { return m_Lights; }

		/// Smashes the scenery's lights within a distance of a point: they go out for good, on the next update. For explosions. Safe to call from any thread.
		void BreakLightsNear(const Vector& pos, float radius);

		/// Smashes any of the scenery's lights that a shot passes through on its way between two points. Cheap when there's no lamp near. Safe to call from any thread.
		void ShootLightsAlong(const Vector& from, const Vector& to);

		/// Makes the scenery's lights shine this update, and puts out the ones whose fixture has been destroyed. Only for the terrain of the Scene being played.
		void UpdateLights();

		/// Removes any color pixel in the color layer of this SLTerrain wherever there is an air material pixel in the material layer.
		void CleanAir();

		/// Removes any color pixel in the color layer of this SLTerrain wherever there is an air material pixel in the material layer inside the specified box.
		/// @param box Box to clean.
		/// @param wrapsX Whether the scene is X-wrapped.
		/// @param wrapsY Whether the scene is Y-wrapped.
		void CleanAirBox(const Box& box, bool wrapsX, bool wrapsY);

		/// Takes a BITMAP and scans through the pixels on this terrain for pixels which overlap with it. Erases them from the terrain and can optionally generate MOPixels based on the erased or 'dislodged' terrain pixels.
		/// @param sprite A pointer to the source BITMAP whose silhouette will be used as a cookie-cutter on the terrain.
		/// @param pos The position coordinates of the sprite.
		/// @param pivot The pivot coordinate of the sprite.
		/// @param rotation The sprite's current rotation in radians.
		/// @param scale The sprite's current scale coefficient.
		/// @param makeMOPs Whether to generate any MOPixels from the erased terrain pixels.
		/// @param skipMOP How many pixels to skip making MOPixels from, between each that gets made. 0 means every pixel turns into an MOPixel.
		/// @param maxMOPs The max number of MOPixels to make, if they are to be made.
		/// @return A deque filled with the MOPixels of the terrain that are now dislodged. This will be empty if makeMOPs is false. Note that ownership of all the MOPixels in the deque IS transferred!
		std::deque<MOPixel*> EraseSilhouette(BITMAP* sprite, const Vector& pos, const Vector& pivot, const Matrix& rotation, float scale, bool makeMOPs = true, int skipMOP = 2, int maxMOPs = 150);

		/// Returns the direction of the out-of-bounds "orbit" for this scene, where the brain must path to and where dropships/rockets come from.
		/// @return The orbit direction, either Up, Down, Left or Right..
		Directions GetOrbitDirection() { return m_OrbitDirection; }
#pragma endregion

#pragma region Virtual Override Methods
		/// Updates the state of this SLTerrain.
		void Update() override;

		/// Draws this SLTerrain's current scrolled position to a bitmap.
		/// @param targetDimensions Dimensions of the draw target.
		/// @param targetBox The box on the target bitmap to limit drawing to, with the corner of box being where the scroll position lines up.
		/// @param offsetNeedsScrollRatioAdjustment Whether the offset of this SceneLayer or the passed in offset override need to be adjusted to scroll ratio.
		void Draw(const Box& targetDimensions, Box& targetBox, bool offsetNeedsScrollRatioAdjustment = false) override;
		void Draw(const Camera& camera) override;
#pragma endregion

	private:
		static Entity::ClassInfo m_sClass; //!< ClassInfo for this class.

		int m_Width; //!< The width of this SLTerrain as determined by the main (material) bitmap, in pixels.
		int m_Height; //!< The height of this SLTerrain as determined by the main (material) bitmap, in pixels.

		std::unique_ptr<SceneLayer> m_FGColorLayer; //!< The foreground color layer of this SLTerrain.
		std::unique_ptr<SceneLayer> m_BGColorLayer; //!< The background color layer of this SLTerrain.

		LayerType m_LayerToDraw; //!< The layer of this SLTerrain that should be drawn to the screen when Draw() is called. See LayerType enumeration.

		ContentFile m_DefaultBGTextureFile; //!< The background texture file that will be used to texturize Materials that have no defined background texture.

		std::vector<TerrainFrosting*> m_TerrainFrostings; //!< The TerrainFrostings that need to be placed on this SLTerrain.
		std::vector<TerrainDebris*> m_TerrainDebris; //!< The TerrainDebris that need to be  placed on this SLTerrain.
		std::vector<TerrainObject*> m_TerrainObjects; //!< The TerrainObjects that need to be placed on this SLTerrain.

		std::vector<TerrainLight> m_Lights; //!< The lights of the scenery, in scene coordinates.
		int m_LightCheckCounter; //!< Counts updates between checks of whether the lights' fixtures are still there.
		std::vector<unsigned char> m_LightCells; //!< A coarse grid over the scene marking where there's a light nearby, so shots can tell quickly that they aren't hitting one.
		int m_LightCellColumns; //!< How many cells wide that grid is.
		bool m_LightCellsStale; //!< Whether the lights have changed since the grid was made.
		std::vector<std::pair<Vector, float>> m_LightBreaks; //!< Places and distances where lights are to be smashed on the next update.
		std::vector<TerrainLight> m_QueuedLights; //!< Lights added from scripts, waiting for the next light update.
		std::mutex m_LightBreaksMutex; //!< Guards m_LightBreaks and m_QueuedLights.

		std::deque<Box> m_UpdatedMaterialAreas; //!< List of areas of the material layer (main bitmap) which have been affected by new objects copied to it. These boxes are NOT wrapped, and can be out of bounds!

		Directions m_OrbitDirection; //!< The direction of the out-of-bounds "orbit" for this scene, where the brain must path to and where dropships/rockets come from.

		/// Applies Material textures to the foreground and background color layers, based on the loaded material layer (main bitmap).
		void TexturizeTerrain();

		/// Clears all the member variables of this SLTerrain, effectively resetting the members of this abstraction level only.
		void Clear();

		// Disallow the use of some implicit methods.
		SLTerrain(const SLTerrain& reference) = delete;
		SLTerrain& operator=(const SLTerrain& rhs) = delete;
	private:
		/// The changed-terrain map: one bit per c_ChangeTileSize square of the loaded scene, set by any thread that changes material, taken by the lighting.
		struct ChangeTiles {
			int SceneWidth = 0;
			int SceneHeight = 0;
			int TilesWide = 0;
			int TilesHigh = 0;
			bool WrapX = false;
			bool WrapY = false;
			size_t WordCount = 0;
			std::unique_ptr<std::atomic<uint64_t>[]> Words;
		};

		/// Makes a new, empty changed-terrain map for a scene being loaded (main thread, nothing else running). The one it replaces is kept until the next
		/// load, so a thread that had just looked it up still writes to live memory.
		static void ResetChangeTiles(int sceneWidth, int sceneHeight, bool wrapX, bool wrapY);

		/// Marks one tile of the map as changed, by its tile coordinates in the map (both in range).
		static void MarkChangeTile(ChangeTiles& map, int tileX, int tileY) {
			size_t index = static_cast<size_t>(tileY) * map.TilesWide + tileX;
			std::atomic<uint64_t>& word = map.Words[index / 64];
			uint64_t bit = uint64_t{1} << (index % 64);
			// Mostly already set while the lighting hasn't taken it: a plain read then, no write to share between threads.
			if ((word.load(std::memory_order_relaxed) & bit) == 0) {
				word.fetch_or(bit, std::memory_order_relaxed);
			}
		}

		/// Marks the tile of one changed material pixel. Called from wherever terrain is changed, which may be several threads at once.
		static void NoteMaterialChange(int x, int y) {
			ChangeTiles* map = s_ChangeTiles.load(std::memory_order_acquire);
			if (!map) {
				return;
			}
			// The pixel is wrapped into the scene before it is tiled (not the tile into the map: the last tile is a partial one when the scene isn't
			// a multiple of the tile size, so a wrapped tile index would name the wrong pixels). Off a non-wrapping edge there is nothing to mark.
			if (x < 0 || x >= map->SceneWidth) {
				if (!map->WrapX) {
					return;
				}
				x = ((x % map->SceneWidth) + map->SceneWidth) % map->SceneWidth;
			}
			if (y < 0 || y >= map->SceneHeight) {
				if (!map->WrapY) {
					return;
				}
				y = ((y % map->SceneHeight) + map->SceneHeight) % map->SceneHeight;
			}
			MarkChangeTile(*map, x / c_ChangeTileSize, y / c_ChangeTileSize);
		}

		static inline std::atomic<ChangeTiles*> s_ChangeTiles{nullptr}; //!< The current changed-terrain map, or null before any terrain is loaded.
		static inline std::unique_ptr<ChangeTiles> s_CurrentChangeTiles; //!< Owns the current map.
		static inline std::unique_ptr<ChangeTiles> s_RetiredChangeTiles; //!< Owns the map before it, kept for one more load (see ResetChangeTiles).
	};
} // namespace RTE
