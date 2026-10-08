#pragma once

#include "Singleton.h"
#include "Box.h"
#include "glad/gl.h"
#include "glm/fwd.hpp"
#include "SceneMan.h"
#include "Shader.h"
#include "LightingSettings.h"

#include <array>
#include <atomic>
#include <memory>
#include <list>
#include <vector>
#include <mutex>
#include <algorithm>
#include <cmath>

#define g_PostProcessMan PostProcessMan::Instance()

namespace RTE {
	class RenderTarget;
	class SceneLighting;
	class Scene;
	/// Struct for storing GL information in the BITMAP->extra field.

	/// Structure for storing a post-process screen effect to be applied at the last stage of 32bpp rendering.
	struct PostEffect {
		std::shared_ptr<BitmapTexture> m_Bitmap = nullptr; //!< The bitmap to blend, not owned.
		size_t m_BitmapHash = 0; //!< Hash used to transmit glow events over the network.
		float m_Angle = 0.0F; // Post effect angle in radians.
		int m_Strength = 128; //!< Scalar float for how hard to blend it in, 0 - 255.
		Vector m_Pos; //!< Post effect position. Can be relative to the scene, or to the screen, depending on context.
		bool m_NoLight = false; //!< The glow is drawn but casts no light on the scene (aiming dots, when their light is turned off).

		/// Constructor method used to instantiate a PostEffect object in system memory.
		PostEffect(const Vector& pos, std::shared_ptr<BitmapTexture> bitmap, size_t bitmapHash, int strength, float angle, bool noLight = false) :
		    m_Bitmap(bitmap), m_BitmapHash(bitmapHash), m_Angle(angle), m_Strength(strength), m_Pos(pos), m_NoLight(noLight) {}
	};

	/// What registered a scene light, for the lighting-by-source readout (SettingsMan::ShowLightsBySource).
	enum class LightSource : unsigned char {
		Other, //!< Not said.
		Objects, //!< An object's own light (its LightRadius/LightIntensity), round or cone.
		Hot, //!< The glow of an object's hot spots.
		Headlamps, //!< Units' headlamp beams and the glow round the lamp.
		Tracers, //!< Tracers and their trails.
		Lamps, //!< Scenery lamps placed in the terrain.
		Fire, //!< Burning ground and burning units.
		Sandbox, //!< Effects put down in the sandbox.
		Scripts, //!< Lua, through AddLight.
		Count
	};

	/// Singleton manager responsible for all 32bpp post-process effect drawing.
	/// A dynamic light in the scene, registered for the current frame by MovableObjects with light properties or from Lua.
	struct SceneLight {
		Vector m_Pos; //!< Light position. Scene coordinates, or relative to a screen, depending on context.
		glm::vec3 m_Color{1.0F}; //!< Linear light color, intensity included.
		float m_Radius = 0.0F; //!< Radius in pixels, where the light reaches zero.
		glm::vec2 m_Direction{1.0F, 0.0F}; //!< For cone lights (flashlights): the direction the cone points, screen space (Y down).
		float m_ConeCos = -2.0F; //!< Cosine of the cone's half angle; below -1 is an ordinary all-round light.
		LightSource m_Source = LightSource::Other; //!< What registered it.
	};

	/// A shockwave ring as seen by one player screen this frame.
	struct ScreenShockwave {
		glm::vec2 m_Pos; //!< Position relative to the screen.
		float m_Radius; //!< Maximum radius of the ring, in pixels.
		float m_Amplitude; //!< Maximum displacement, in pixels.
		float m_Progress; //!< How far the ring has expanded, 0 to 1.
	};

	class PostProcessMan : public Singleton<PostProcessMan> {

	public:
#pragma region Creation
		/// Constructor method used to instantiate a PostProcessMan object in system memory. Create() should be called before using the object.
		PostProcessMan();

		/// Makes the PostProcessMan object ready for use.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int Initialize();

		/// (Re-)Initializes the GL backbuffers to the current render resolution for post-processing.
		void CreateGLBackBuffers();
#pragma endregion

#pragma region Destruction
		/// Destructor method used to clean up a PostProcessMan object before deletion from system memory.
		~PostProcessMan();

		/// Destroys and resets (through Clear()) the PostProcessMan object.
		void Destroy();

		/// Clears the list of registered post-processing screen effects and glow boxes.
		void ClearScreenPostEffects() {
			m_PostScreenEffects.clear();
			m_PostScreenGlowBoxes.clear();
		}

		/// Clears the list of registered post-processing scene effects and glow areas.
		void ClearScenePostEffects() {
			m_PostSceneEffects.clear();
			m_GlowAreas.clear();
			ClearSceneLights();
		}

		/// Clears the lights and shimmers registered so far. They're registered on every sim update and the light pass adds them up, so only the
		/// last update's set may reach the draw: a frame that runs several sim updates would otherwise draw every light that many times over.
		void ClearSceneLights() {
			m_SceneLights.clear();
			std::scoped_lock lock(m_ShockwaveMutex);
			m_Shimmers.clear();
		}
#pragma endregion

#pragma region Concrete Methods
		/// Takes the current state of the 8bpp back-buffer and copies it to the 32bpp post-processing buffer, and steps the palette animation. Glows are drawn by the scene lighting.
		void PostProcess();
#pragma endregion

#pragma region Post Effect Handling
		/// Registers a post effect to be added at the very last stage of 32bpp rendering by the FrameMan.
		/// @param effectPos The absolute scene coordinates of the center of the effect.
		/// @param effect A 32bpp BITMAP screen should be drawn centered on the above scene location in the final frame buffer. Ownership is NOT transferred!
		/// @param hash Hash value of the effect for transmitting over the network.
		/// @param strength The intensity level this effect should have when blended in post. 0 - 255.
		/// @param angle The angle this effect should be rotated at in radians.
		void RegisterPostEffect(const Vector& effectPos, std::shared_ptr<BitmapTexture> effect, size_t hash, int strength = 255, float angle = 0);

		/// Gets all screen effects that are located within a box in the scene.
		/// Their coordinates will be returned relative to the upper left corner of the box passed in here. Wrapping of the box will be taken care of.
		/// @param boxPos The top left coordinates of the box to get post effects for.
		/// @param boxWidth The width of the box.
		/// @param boxHeight The height of the box.
		/// @param effectsList The list to add the screen effects that fall within the box to. The coordinates of the effects returned here will be relative to the boxPos passed in above.
		/// @param team The team whose unseen layer should obscure the screen effects here.
		/// @return Whether any active post effects were found in that box.
		bool GetPostScreenEffectsWrapped(const Vector& boxPos, int boxWidth, int boxHeight, std::list<PostEffect>& effectsList, int team = -1);

		/// Gets a temporary bitmap of specified size to rotate post effects in.
		/// @param bitmapSize Size of bitmap to get.
		/// @return Pointer to the temporary bitmap.
		BITMAP* GetTempEffectBitmap(BITMAP* bitmap) const;
#pragma endregion

#pragma region Post Pixel Glow Handling

		/// Registers a specific IntRect to be post-processed and have special pixel colors lit up by glow effects in it.
		/// @param glowArea The IntRect to have special color pixels glow in, in scene coordinates.
		void RegisterGlowArea(const IntRect& glowArea) {
			if (g_TimerMan.DrawnSimUpdate() && g_TimerMan.SimUpdatesSinceDrawn() >= 0) {
				m_GlowAreas.push_back(glowArea);
			}
		}

		/// Creates an IntRect and registers it to be post-processed and have special pixel colors lit up by glow effects in it.
		/// @param center The center of the IntRect.
		/// @param radius The radius around it to add as an area.
		void RegisterGlowArea(const Vector& center, float radius) {
			RegisterGlowArea(IntRect(static_cast<int>(center.m_X - radius), static_cast<int>(center.m_Y - radius), static_cast<int>(center.m_X + radius), static_cast<int>(center.m_Y + radius)));
		}

		/// Registers a specific glow dot effect to be added at the very last stage of 32bpp rendering by the FrameMan.
		/// @param effectPos The absolute scene coordinates of the center of the effect.
		/// @param color Which glow dot color to register, see the DotGlowColor enumerator.
		/// @param strength The intensity level this effect should have when blended in post. 0 - 255.
		void RegisterGlowDotEffect(const Vector& effectPos, DotGlowColor color, int strength = 255);

		/// Gets all glow areas that affect anything within a box in the scene.
		/// Their coordinates will be returned relative to the upper left corner of the box passed in here. Wrapping of the box will be taken care of.
		/// @param boxPos The top left coordinates of the box to get post effects for.
		/// @param boxWidth The width of the box.
		/// @param boxHeight The height of the box.
		/// @param areaList The list to add the glow Boxes that intersect to. The coordinates of the Boxes returned here will be relative to the boxPos passed in above.
		/// @return Whether any active post effects were found in that box.
		bool GetGlowAreasWrapped(const Vector& boxPos, int boxWidth, int boxHeight, std::list<Box>& areaList) const;
#pragma endregion

		/// Gets the backbuffer texture for indexed drawings.
		/// @return The opengl backbuffer texture for indexed drawings.
		std::shared_ptr<RenderTarget> GetPostProcessColorBuffer() { return m_PostProcessFramebuffer; }

		/// Registers a dynamic light for the current frame. Lights persist until the next sim update after a drawn frame, like scene post effects.
		/// @param pos Scene position of the light.
		/// @param color Light color in 0-255 gamma space, like palette and INI colors.
		/// @param radius Radius in pixels, where the light reaches zero.
		/// @param intensity Brightness multiplier.
		/// @param source What is registering it, for the lighting-by-source readout.
		void RegisterLight(const Vector& pos, const glm::vec3& color, float radius, float intensity, LightSource source = LightSource::Other);

		/// Registers a cone light (flashlight, headlamp) for the current frame.
		/// @param pos Where the light comes from, scene coordinates.
		/// @param direction Direction the cone points (Y down), any length.
		/// @param halfAngleDegrees Half the cone's width.
		void RegisterConeLight(const Vector& pos, const Vector& direction, float halfAngleDegrees, const glm::vec3& color, float radius, float intensity, LightSource source = LightSource::Other);

		/// Registers a dynamic light for the current frame, from Lua. See RegisterLight.
		void AddLight(const Vector& pos, float radius, float red, float green, float blue, float intensity) { RegisterLight(pos, glm::vec3(red, green, blue), radius, intensity, LightSource::Scripts); }

		/// Makes a palette colour's glow pulse, from Lua (animated palette flags; see RenderMan::SetPalettePulse). A period of 0 stops it.
		void SetPalettePulse(int paletteIndex, float low, float high, float period, float phase);

		/// Rotates a run of palette colours through each other, from Lua (see RenderMan::SetPaletteCycle). A period of 0 stops it.
		void SetPaletteCycle(int from, int to, float period);

		/// Stops every palette pulse and cycle asked for by scripts or PaletteAnimation.ini. Glowing liquids keep theirs.
		void ClearPaletteAnimation();

		/// Gets the scene lights registered for the frame about to be drawn, in scene coordinates, for the lighting-by-source readout. Main thread only.
		const std::vector<SceneLight>& GetSceneLights() const { return m_SceneLights; }

		/// Gets the scene lights that may affect a box, with positions relative to the box. Handles scene wrapping.
		/// @param boxPos Scene position of the box's top left corner.
		/// @param boxWidth Width of the box.
		/// @param boxHeight Height of the box.
		/// @param lights Out parameter the lights are appended to.
		void GetLightsWrapped(const Vector& boxPos, int boxWidth, int boxHeight, std::vector<SceneLight>& lights) const;

		/// Registers an explosion shockwave. Ring size and strength scale with the energy released.
		/// @param pos Scene position of the explosion.
		/// @param energy Energy released, as computed for gib screen shake.
		void RegisterShockwave(const Vector& pos, float energy);

		/// Registers a shimmer for the current frame: the scene behind is bent in a wobbling ring, as around an energy shield or a cloaked unit. Call every update to keep it up.
		/// @param pos Scene position of its centre.
		/// @param radius How far out it reaches, in pixels.
		/// @param strength How strongly it bends, around 1.
		void RegisterShimmer(const Vector& pos, float radius, float strength);

		/// Puts mist or dust into the air (the fog volume, LightingSettings::FogVolume): it drifts with the wind, is lit by the sky and lamps, and clears over time.
		/// Safe from any thread. Ignored with the fog volume off.
		/// @param pos Scene position of its centre.
		/// @param radius How far it spreads, in pixels.
		/// @param amount How thick, 0 to 1 (1 hides what's behind at full fog strength).
		void RegisterFog(const Vector& pos, float radius, float amount);

		/// Puts mist into the air, from Lua. See RegisterFog.
		void AddFog(const Vector& pos, float radius, float amount) { RegisterFog(pos, radius, amount); }

		/// Takes the mist and dust put into the air since the last call: x, y (scene pixels), radius (pixels), amount.
		std::vector<glm::vec4> TakeFogPuffs();

		/// Registers a scorch mark: soot stamped into the terrain that glows hot for a few seconds. Size and darkness scale with the energy released.
		/// @param pos Scene position of the explosion.
		/// @param energy Energy released, as computed for gib screen shake.
		void RegisterScorchMark(const Vector& pos, float energy);

		/// Gets the active shockwaves that may affect a box, with positions relative to the box, and forgets expired ones. Handles scene wrapping.
		void GetShockwavesWrapped(const Vector& boxPos, int boxWidth, int boxHeight, std::vector<ScreenShockwave>& shockwaves);

		/// A soot mark to stamp into the world, and the hot spot it leaves.
		struct ScorchMark {
			Vector m_Pos;
			float m_Radius;
			float m_Darkness;
			float m_StartTime;
		};

		/// Takes the scorch marks registered since the last call, for stamping.
		std::vector<ScorchMark> TakePendingScorchMarks();

		/// Gets a copy of the recent scorch marks that are still hot, and forgets the cooled ones. A copy, because gibbing can add marks from other threads while the caller reads them.
		/// @param duration How long marks stay hot, in seconds.
		std::vector<ScorchMark> GetHotScorchMarks(float duration);

		/// Forgets the recent scorch marks, so the last scene's don't glow in a new one.
		void ClearHotScorchMarks() {
			std::scoped_lock lock(m_ShockwaveMutex);
			m_HotScorchMarks.clear();
		}

		/// Gets the current simulation time in seconds, including the fraction of the current sim update, for smooth time based effects that pause and slow down with the game.
		static float GetSmoothSimTime();

		/// Gets the smooth sim time in double precision, for time based effects that would lose precision as a float after a few hours of play.
		static double GetSmoothSimTimePrecise();

		/// Gets the smooth sim time for animating render-only effects (shaders, lamp pulse, flames, embers): wrapped to c_EffectTimePeriod seconds so it keeps sub-millisecond precision as a float however long the game runs.
		/// Effects animated by it skip once every period, which is about an hour, rather than starting to step at the sim rate after a few hours.
		static float GetEffectTime();

		static constexpr double c_EffectTimePeriod = 4096.0; //!< The period GetEffectTime wraps at, in seconds.

		/// Gets the scene lighting, creating it on first use (it needs a GL context).
		/// @return The scene lighting.
		SceneLighting* GetSceneLighting();

#pragma region Atmosphere Lua Accessors
		float GetTimeOfDay() const { return m_LightingSettings.TimeOfDay; }
		void SetTimeOfDay(float hours) { m_LightingSettings.TimeOfDay = std::fmod(std::fmod(hours, 24.0F) + 24.0F, 24.0F); }
		float GetDayLengthMinutes() const { return m_LightingSettings.DayLengthMinutes; }
		void SetDayLengthMinutes(float minutes) { m_LightingSettings.DayLengthMinutes = std::max(minutes, 0.0F); }
		int GetWeatherType() const { return m_LightingSettings.WeatherType; }
		void SetWeatherType(int weatherType) { m_LightingSettings.WeatherType = std::clamp(weatherType, 0, 4); }
		float GetWeatherIntensity() const { return m_LightingSettings.WeatherIntensity; }
		void SetWeatherIntensity(float intensity) { m_LightingSettings.WeatherIntensity = std::clamp(intensity, 0.0F, 1.0F); }
		float GetWind() const { return m_LightingSettings.Wind; }
		void SetWind(float wind) { m_LightingSettings.Wind = wind; }
		bool GetLightingEnabled() const { return m_LightingSettings.Enabled; }
		void SetLightingEnabled(bool enabled) { m_LightingSettings.Enabled = enabled; }
		/// Sets the sky light color, 0-255 gamma space per channel. Values above 255 brighten.
		void SetSkyColor(float red, float green, float blue) { m_LightingSettings.SkyColor = glm::vec3(std::pow(red / 255.0F, 2.2F), std::pow(green / 255.0F, 2.2F), std::pow(blue / 255.0F, 2.2F)); }
		/// Sets the color grade: white balance (-1 cool .. 1 warm), tint (-1 green .. 1 magenta) and contrast (1 neutral).
		/// Sets the grading, grain, vignette and bloom to a ready-made look: 0 natural, 1 gritty, 2 vivid, 3 noir.
		void ApplyLook(int look) { m_LightingSettings.ApplyLook(look); }

		void SetColorGrade(float temperature, float tint, float contrast) {
			m_LightingSettings.Temperature = temperature;
			m_LightingSettings.Tint = tint;
			m_LightingSettings.Contrast = contrast;
		}
		/// Sets the shadow and highlight tints for split toning, as linear multipliers (1 = neutral).
		void SetSplitToning(float shadowR, float shadowG, float shadowB, float highlightR, float highlightG, float highlightB) {
			m_LightingSettings.ShadowTint = glm::vec3(shadowR, shadowG, shadowB);
			m_LightingSettings.HighlightTint = glm::vec3(highlightR, highlightG, highlightB);
		}

		/// Sets the ambient light color where no sky light reaches, 0-255 gamma space per channel.
		void SetAmbientColor(float red, float green, float blue) { m_LightingSettings.Ambient = glm::vec3(std::pow(red / 255.0F, 2.2F), std::pow(green / 255.0F, 2.2F), std::pow(blue / 255.0F, 2.2F)); }
#pragma endregion

		/// Resets the atmosphere (time of day, weather) to the player's settings and applies the overrides of the newly loaded Scene, if any.
		/// Also undoes any atmosphere changes scripts made during the previous Scene.
		void ApplySceneAtmosphere(const Scene* scene);

		/// Gets the settings to save to Settings.ini: the player's own atmosphere, not a Scene's or script's.
		LightingSettings GetLightingSettingsToSave() const;

		/// Makes the current atmosphere the player's own, so it's what gets saved and restored between Scenes (used by the Graphics Lab).
		void AdoptAtmosphereAsPlayers() {
			m_PlayerAtmosphere = m_LightingSettings;
			m_PlayerAtmosphereCaptured = true;
		}

		/// Gets the lighting and post-processing settings. These persist in Settings.ini and can be changed live.
		/// @return The lighting settings.
		LightingSettings& GetLightingSettings() { return m_LightingSettings; }

		GLuint GetPaletteTexture() { return m_Palette8Texture; }

	protected:
		std::list<PostEffect> m_PostScreenEffects; //!< List of effects to apply at the end of each frame. This list gets cleared out and re-filled each frame.
		std::list<PostEffect> m_PostSceneEffects; //!< All post-processing effects registered for this draw frame in the scene.

		std::list<Box> m_PostScreenGlowBoxes; //!< List of areas that will be processed with glow.
		std::list<IntRect> m_GlowAreas; //!< All the areas to do post glow pixel effects on, in scene coordinates.

		std::array<std::list<PostEffect>, c_MaxScreenCount> m_ScreenRelativeEffects; //!< List of screen relative effects for each player in online multiplayer.
		std::array<std::mutex, c_MaxScreenCount> ScreenRelativeEffectsMutex; //!< Mutex for the ScreenRelativeEffects list when accessed by multiple threads in online multiplayer.

		std::shared_ptr<BitmapTexture> m_YellowGlow; //!< Bitmap for the yellow dot glow effect.
		std::shared_ptr<BitmapTexture> m_RedGlow; //!< Bitmap for the red dot glow effect.
		std::shared_ptr<BitmapTexture> m_BlueGlow; //!< Bitmap for the blue dot glow effect.

		size_t m_YellowGlowHash; //!< Hash value for the yellow dot glow effect bitmap.
		size_t m_RedGlowHash; //!< Hash value for the red dot glow effect bitmap.
		size_t m_BlueGlowHash; //!< Hash value for the blue dot glow effect bitmap.

		std::unordered_map<int, BITMAP*> m_TempEffectBitmaps; //!< Stores temporary bitmaps to rotate post effects in for quick access.

	private:
		GLuint m_BackBuffer8; //!< Backbuffer texture for incoming indexed drawings.
		GLuint m_Palette8Texture; //!< Palette texture for incoming indexed drawings.
		std::shared_ptr<RenderTarget> m_BlitFramebuffer; //!< Framebuffer for blitting the 8bpp backbuffer to the 32bpp backbuffer.
		std::shared_ptr<RenderTarget> m_PostProcessFramebuffer; //!< Framebuffer for post-processing effects.
		std::unique_ptr<SceneLighting> m_SceneLighting; //!< Scene lighting, bloom and tonemapping applied to each player screen.
		LightingSettings m_LightingSettings; //!< Settings for the scene lighting.
		LightingSettings m_PlayerAtmosphere; //!< The player's own atmosphere settings, captured when the first Scene loads.
		bool m_PaletteAnimationLoaded = false; //!< Base.rte/PaletteAnimation.ini has been read.

		/// Reads the animated palette colours in Base.rte/PaletteAnimation.ini, once: lines "Pulse = index, low, high, period, phase" and "Cycle = from, to, period".
		void LoadPaletteAnimation();
		bool m_PlayerAtmosphereCaptured = false;
		std::vector<SceneLight> m_SceneLights; //!< Dynamic lights registered for the current frame, in scene coordinates. Pushed to under m_SceneLightsMutex.
		std::mutex m_SceneLightsMutex; //!< Lights can be registered from Lua, and Lua's ThreadedUpdate runs scripts in parallel.

		/// An active explosion shockwave.
		struct Shockwave {
			Vector m_Pos;
			float m_Radius;
			float m_Amplitude;
			float m_StartTime;
		};
		std::vector<Shockwave> m_Shockwaves; //!< Active shockwaves, in scene coordinates.
		std::vector<Shockwave> m_Shimmers; //!< Shimmers registered for the current frame (the start time isn't used).
		float m_ActivityTimeOfDay = -1.0F; //!< Time of day chosen for the current activity, negative for none.
		int m_ActivityWeather = -1; //!< Weather chosen for the current activity, negative for none.

	public:
		/// Gets the active shockwaves in scene coordinates, as xy position, z current wavefront radius, w remaining strength. For vegetation pushed by blasts.
		void GetActiveShockwaves(std::vector<glm::vec4>& shockwaves);

		/// Sets the time of day and weather for the activity about to start, overriding the scene's and the player's settings. Negative values leave them alone.
		/// Kept for restarts of the same activity, until set again.
		/// @param timeOfDay Hours, or negative for the default.
		/// @param weatherType 0 clear, 1 rain, 2 snow, or negative for the default.
		/// Applies the activity's chosen atmosphere, if any, over the current settings.
		void ApplyActivityAtmosphere();

		void SetActivityAtmosphere(float timeOfDay, int weatherType) {
			m_ActivityTimeOfDay = timeOfDay;
			m_ActivityWeather = weatherType;
		}

	private:
		std::mutex m_ShockwaveMutex; //!< Gibbing can happen off the main thread.
		std::vector<ScorchMark> m_PendingScorchMarks; //!< Scorch marks not stamped yet. Guarded by m_ShockwaveMutex.
		std::vector<ScorchMark> m_HotScorchMarks; //!< Recent scorch marks, for the cooling glow. Guarded by m_ShockwaveMutex.
		std::vector<glm::vec4> m_FogPuffs; //!< Mist and dust put into the air and not yet taken by the fog volume. Guarded by m_ShockwaveMutex.
		std::unique_ptr<glm::mat4> m_ProjectionMatrix; //!< Projection matrix for post-processing effects.
		GLuint m_VertexBuffer; //!< Vertex buffer for post-processing effects.
		GLuint m_VertexArray; //!< Vertex array for post-processing effects.
		std::unique_ptr<Shader> m_Blit8; //!< Shader for blitting the 8bpp backbuffer to the 32bpp backbuffer.
		std::unique_ptr<Shader> m_PostProcessShader; //!< Shader for drawing bitmap post effects.

		/// Fills in a scene light from what RegisterLight was given, without registering it.
		/// @return Whether the light is worth registering (a positive radius and intensity, in a sim update that will be drawn).
		bool MakeSceneLight(const Vector& pos, const glm::vec3& color, float radius, float intensity, SceneLight& light) const;

#pragma region Post Effect Handling
		/// Gets all screen effects that are located within a box in the scene. Their coordinates will be returned relative to the upper left corner of the box passed in here.
		/// @param boxPos The top left coordinates of the box to get post effects for.
		/// @param boxWidth The width of the box.
		/// @param boxHeight The height of the box.
		/// @param effectsList The list to add the screen effects that fall within the box to. The coordinates of the effects returned here will be relative to the boxPos passed in above.
		/// @param team The team whose unseen area should block the glows.
		/// @return Whether any active post effects were found in that box.
		bool GetPostScreenEffects(Vector boxPos, int boxWidth, int boxHeight, std::list<PostEffect>& effectsList, int team = -1);

		/// Gets all screen effects that are located within a box in the scene. Their coordinates will be returned relative to the upper left corner of the box passed in here.
		/// @param left Position of box left plane (X start).
		/// @param top Position of box top plane (Y start).
		/// @param right Position of box right plane (X end).
		/// @param bottom Position of box bottom plane (Y end).
		/// @param effectsList The list to add the screen effects that fall within the box to. The coordinates of the effects returned here will be relative to the boxPos passed in above.
		/// @param team The team whose unseen area should block the glows.
		/// @return Whether any active post effects were found in that box.
		bool GetPostScreenEffects(int left, int top, int right, int bottom, std::list<PostEffect>& effectsList, int team = -1);
#pragma endregion

#pragma region Post Pixel Glow Handling
		/// Gets a specific standard dot glow effect for making pixels glow.
		/// @param which Which of the dot glow colors to get, see the DotGlowColor enumerator.
		/// @return The requested glow dot BITMAP.
		std::shared_ptr<BitmapTexture> GetDotGlowEffect(DotGlowColor whichColor) const;

		/// Gets the hash value of a specific standard dot glow effect for making pixels glow.
		/// @param which Which of the dot glow colors to get, see the DotGlowColor enumerator.
		/// @return The hash value of the requested glow dot BITMAP.
		size_t GetDotGlowEffectHash(DotGlowColor whichColor) const;
#pragma endregion

		/// Clears all the member variables of this PostProcessMan, effectively resetting the members of this abstraction level only.
		void Clear();

		/// Initializes all the GL pointers used by this PostProcessMan.
		void InitializeGLPointers();

		/// Destroys all the GL pointers used by this PostProcessMan.
		void DestroyGLPointers();

		/// Updates the palette texture with the current palette.
		void UpdatePalette();

		// Disallow the use of some implicit methods.
		PostProcessMan(const PostProcessMan& reference) = delete;
		PostProcessMan& operator=(const PostProcessMan& rhs) = delete;
	};
} // namespace RTE
