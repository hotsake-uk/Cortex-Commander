#pragma once

#include "Entity.h"
#include "ContentFile.h"
#include "Color.h"

#include <cmath>
#include <string>
#include <vector>

namespace RTE {

	/// How a material behaves in the terrain's simulations (SB-1): the liquids and powders (FluidSim), fire (TerrainFire) and bodies in liquid
	/// (ActorWater). Set in the material's INI block; anything left unset (below 0, or empty) keeps the stock rule, which goes by the material's
	/// name, so old mods behave as they did. The names follow the Powder Toy's where they mean the same.
	struct MaterialBehaviour {
		int Flows = -1; //!< 1 for a liquid: it falls, runs level and pools (FluidSim).
		int Powder = -1; //!< 1 for a powder: it falls and slides down slopes but doesn't run level (sand, snow, rubble, ash).
		int FlowSpeed = -1; //!< How far a pixel may run sideways a step, in pixels.
		int FallSpeed = -1; //!< How far a pixel may fall a step, in pixels.
		int MoveEvery = -1; //!< Moves every this many sim updates: the thicker, the higher.
		int Gravity = -1; //!< Falling speed gained a step, in quarter pixels.
		int Viscosity = -1; //!< Sideways speed gained a step while it has somewhere to run, in quarter pixels (low is thick).
		int LiquidWeight = -1; //!< Heavier sinks through lighter.
		float SlideChance = -1.0F; //!< For a powder: the chance a step of sliding down a slope.
		float Scuffs = -1.0F; //!< For loose ground: how readily a unit walking or running on it knocks surface pixels loose and shoves them along (TerrainCollapse), 0 to 1. 0 or unset: not at all. Sand 1.
		int Sticky = -1; //!< For a powder: 1 to slide only off a drop two deep, so it stands steeper (snow).
		std::string BreakStyle; //!< How a falling piece of it breaks when it lands hard (TerrainCollapse): "Shatter" (concrete, glass), "Crack" (earth, stone), "Crumble" (sand, snow),
		                        //!< "Splinter" (wood: lands whole unless the hit is huge) or "Bend" (metal: doesn't break). Each style's threshold is a setting (F6, Falling ground).
		float ImpactStrength = -1.0F; //!< How hard a landing it takes to break, as a multiple of its style's threshold: 2 takes twice as hard a hit, 0.5 half.
		int NeckWidth = -1; //!< A piece held on by a neck of it no wider than this many pixels snaps off and falls (TerrainCollapse). 0: it holds until cut right through (wood).
		                    //!< Unset: the Falling ground setting.
		std::string Burns; //!< "Grass", "Wood", "Oil" or "Ember" (smoulders, glowing: charcoal) for how it burns (TerrainFire), "None" for not at all.
		int BurnMinTicks = -1; //!< How long a pixel of it burns, in fire ticks (a twentieth of a second), at the least and the most.
		int BurnMaxTicks = -1;
		float BurnSpread = -1.0F; //!< The chance a fire tick of setting each flammable neighbour alight.
		int LeavesAsh = -1; //!< 1 to leave ash where it burned out.
		float BurnBlast = -1.0F; //!< The chance a pixel of it going up in flames sets off a blast (fuel), 0 to 1.
		int Douses = -1; //!< 1 if it puts fire out, and quenches what settles in it (water).
		std::string FreezesTo; //!< What it freezes into, still and under snowfall (water: "Ice").
		std::string MeltsTo; //!< What it melts into beside something hot (ice and snow: "Water").
		std::string BoilsTo; //!< What it boils into against something hot ("Air" for steam, water's).
		std::string SettlesTo; //!< What it sets into where it meets something that douses it (lava: "Stone").
		std::string DriesTo; //!< What a liquid dries into where it lies still with air over it (mud: "Earth"), from the top down.
		float DryChance = -1.0F; //!< The chance, each time the terrain's sweep passes a still surface pixel of it (every few seconds), that it dries.
		int Chills = -1; //!< 1 if it freezes what it touches that freezes (water to ice), and frosts bodies in it (cryogenic fluid).
		float Evaporates = -1.0F; //!< The chance a step that a pixel of it at the surface boils off into mist (cryogenic fluid: gone in seconds).
		int LiquidsPassThrough = -1; //!< 1 if liquids flow through it as if it weren't there (grass, foliage; the stock rule goes by plant names), unless the liquid's PassThrough says otherwise.
		std::string PassThrough; //!< For a liquid, what it does to what it flows through (LiquidsPassThrough): "Keep" (unset) leaves it there for when the liquid has gone,
		                         //!< "Destroy" does away with it (acid eats it, lava burns it), "Collide" stops at it as at anything solid.
		int Look = -1; //!< The liquid look it's drawn with (RenderMan::SetLiquidPaletteColor: 1 water, 2 lava, 3 acid, 4 oil...), 0 for plain.
		int Glow = -1; //!< How brightly a liquid of it glows, 0 to 255 (lava 230).
		int Stains = -1; //!< 1 if drops of it leave stains where they land (blood, oil).
		int Breathable = -1; //!< 1 if a body can breathe in it; liquids aren't.
		float TouchDamage = -1.0F; //!< Health a second it takes from a body in it, for each level of depth (acid 5).
		/// What happens where it meets another material (SB-3), one AddReaction line each: "Other, Chance, ThisBecomes, OtherBecomes[, Effects]".
		/// Other is a material's name, or AnyLiquid, AnyFlammable or AnySoft; the products are a material's name, Air, or Same for unchanged;
		/// Chance is per step a liquid pixel of either touches the other; Effects is any of Steam, Flash, Ignite, Explosion and Fizz joined with +.
		/// They add to, or replace for the same pair, the stock reactions that come from the rest of the behaviour (FluidSim).
		std::vector<std::string> Reactions;
		int SightDepth = -1; //!< For a liquid, how many pixels of it a look sees through, to what's in it or beyond (stock: water 200, oil 6, lava none).
		int ShotDepth = -1; //!< For a liquid, how many pixels of it a shot (a bullet, tracer, shrapnel) goes on through, slowing to half by the end, before
		                    //!< it is spent (stock: water 60, oil 30, lava 10).
	};

	/// Represents a material and holds all the relevant data.
	class Material : public Entity {

	public:
		EntityAllocation(Material);
		SerializableOverrideMethods;
		ClassInfoGetters;

#pragma region Creation
		/// Constructor method used to instantiate a Material object in system memory. Create() should be called before using the object.
		Material() { Clear(); }

		/// Copy constructor method used to instantiate a Material object identical to an already existing one.
		/// @param reference A Material object which is passed in by reference.
		Material(const Material& reference) {
			if (this != &reference) {
				Clear();
				Create(reference);
			}
		}

		/// Creates a Material to be identical to another, by deep copy.
		/// @param reference A reference to the Material to deep copy.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int Create(const Material& reference);
#pragma endregion

#pragma region Destruction
		/// Resets the entire Material, including its inherited members, to it's default settings or values.
		void Reset() override {
			Clear();
			Entity::Reset();
		}
#pragma endregion

#pragma region Getters and Setters
		/// Gets the foreground texture bitmap of this Material, if any is associated with it.
		/// @return Pointer to the foreground texture bitmap of this Material.
		BITMAP* GetFGTexture() const { return m_TerrainFGTexture; }

		/// Gets the background texture bitmap of this Material, if any is associated with it.
		/// @return Pointer to the background texture bitmap of this Material.
		BITMAP* GetBGTexture() const { return m_TerrainBGTexture; }

		/// Gets the index of this Material in the material palette.
		/// @return The index of this Material in the material palette. 0 - 255.
		unsigned char GetIndex() const { return m_Index; }

		/// Sets the index of this Material in the material palette to the next specified value.
		/// @param newIndex The new index of this Material in the material palette. 0 - 255.
		void SetIndex(unsigned char newIndex) { m_Index = newIndex; }

		/// Gets the drawing priority of this Material. The higher the number, the higher chances that a pixel of this material will be drawn on top of others. Will default to Integrity if no Priority has been defined.
		/// @return The drawing priority of this Material.
		int GetPriority() const { return m_Priority < 0 ? static_cast<int>(std::ceil(m_Integrity)) : m_Priority; }

		/// Gets the amount of times a dislodged pixel of this Material will attempt to relocate to an open position.
		/// @return The amount of attempts at relocating.
		int GetPiling() const { return m_Piling; }

		/// The impulse force that a particle needs to knock loose a terrain pixel of this material. In kg * m/s.
		/// @return The impulse force that a particle needs to knock loose a terrain pixel of this material.
		float GetIntegrity() const { return m_Integrity; }

		/// Gets the scalar value that defines the restitution of this Material. 1.0 = no kinetic energy is lost in a collision, 0.0 = all energy is lost (plastic).
		/// @return A float scalar value that defines the restitution of this Material.
		float GetRestitution() const { return m_Restitution; }

		/// Gets the scalar value that defines the friction of this Material. 1.0 = will snag onto everything, 0.0 = will glide with no friction.
		/// @return A float scalar value that defines the friction of this Material.
		float GetFriction() const { return m_Friction; }

		/// Gets the scalar value that defines the stickiness of this Material. 1.0 = will stick to everything, 0.0 = will never stick to anything.
		/// @return A float scalar value that defines the stickiness of this Material.
		float GetStickiness() const { return m_Stickiness; }

		/// Gets the density of this Material in Kg/L.
		/// @return The density of this Material.
		float GetVolumeDensity() const { return m_VolumeDensity; }

		/// Gets the density of this Material in kg/pixel, usually calculated from the KG per Volume L property.
		/// @return The pixel density of this Material.
		float GetPixelDensity() const { return m_PixelDensity; }

		/// If this material transforms into something else when settling into the terrain, this will return that different material index. If not, it will just return the regular index of this material.
		/// @return The settling material index of this or the regular index.
		unsigned char GetSettleMaterial() const { return (m_SettleMaterialIndex != 0) ? m_SettleMaterialIndex : m_Index; }

		/// The material a particle or sprite of this is drawn into the terrain's material layer as when it settles: GetSettleMaterial, or this
		/// material itself when the object has SettleMaterialDisabled. A body's material (see IsBody) settles as Earth instead while the
		/// BodiesSettleAsEarth setting is on, so the remains of the fallen keep their colours but dig, burn and collapse like the ground.
		/// @param settleMaterialDisabled Whether the settling object keeps its own material rather than this one's SettleMaterial.
		/// @return The material index to write into the terrain.
		unsigned char GetTerrainSettleMaterial(bool settleMaterialDisabled = false) const;

		/// Whether this is what bodies are made of: flesh and bone. Set with IsBody in INI; when it isn't, any material named Bone or with
		/// Flesh in its name counts.
		bool IsBody() const;

		/// Gets the material index to spawn instead of this one for special effects.
		/// @return The material index to spawn instead of this one for special effects. 0 means to spawn the same material as this.
		unsigned char GetSpawnMaterial() const { return m_SpawnMaterialIndex; }

		/// Whether this material is scrap material made from gibs of things that have already been blown apart.
		/// @return Whether this material is scrap material.
		bool IsScrap() const { return m_IsScrap; }

		/// Gets the Color of this Material.
		/// @return The color of this material.
		Color GetColor() const { return m_Color; }

		/// Gets the color index of this Material.
		/// @return The color index of this material.
		int GetColorIndex() const { return m_Color.GetIndex(); }

		/// Indicates whether or not to use the Material's own color when a pixel of this Material is knocked loose from the terrain.
		/// @return Whether the Material's color, or the terrain pixel's color should be applied.
		bool UsesOwnColor() const { return m_UseOwnColor; }

		/// Gets how metallic things made of this Material look under the lighting: metal mirrors its surroundings and tints its highlights.
		/// Set with Metalness in INI; when it isn't, a value that suits the Material's name is used (the metals, "Military Stuff" and so on).
		/// @return The metalness, 0 to 1.
		float GetMetalness() const;

		/// Gets how glossy things made of this Material look under the lighting: how strong and tight the highlights lights throw on them are.
		/// Set with Gloss in INI; when it isn't, a value that suits the Material's name is used.
		/// @return The gloss, 0 to 1.
		float GetGloss() const;

		/// Gets how this Material behaves in the terrain's simulations (see MaterialBehaviour): unset values keep the stock rule.
		const MaterialBehaviour& GetBehaviour() const { return m_Behaviour; }
#pragma endregion

#pragma region Operator Overloads
		/// An assignment operator for setting one Material equal to another.
		/// @param rhs A Material reference.
		/// @return A reference to the changed Material.
		Material& operator=(const Material& rhs) {
			if (this != &rhs) {
				Destroy();
				Create(rhs);
			}
			return *this;
		}
#pragma endregion

	protected:
		static Entity::ClassInfo m_sClass; //!< ClassInfo for this class.

		unsigned char m_Index; //!< Index of this in the material palette. 0 - 255.
		int m_Priority; //!< The priority that a pixel of this material has to be displayed. The higher the number, the higher chances that a pixel of this material will be drawn on top of others.
		int m_Piling; //! The amount of times a dislodged pixel of this Material will attempt to relocate upwards, when intersecting a terrain pixel of the same Material. TODO: Better property name?

		float m_Integrity; //!< The impulse force that a particle needs to knock loose a terrain pixel of this material. In kg * m/s.
		float m_Restitution; //!< A scalar value that defines the restitution (elasticity). 1.0 = no kinetic energy is lost in a collision, 0.0 = all energy is lost (plastic).
		float m_Friction; //!< A scalar value that defines the friction coefficient. 1.0 = will snag onto everything, 0.0 = will glide with no friction.
		float m_Stickiness; //!< A scalar value that defines the stickiness coefficient (no sticky 0.0 - 1.0 max). Determines the likelihood of something of this material sticking to terrain when a collision occurs.

		float m_VolumeDensity; //!< Density in Kg/L.
		float m_PixelDensity; //!< Density in kg/pixel, usually calculated from the KG per Volume L property.

		// TODO: Implement these properties maybe? They aren't referenced anywhere or do anything as of now.
		float m_GibImpulseLimitPerLiter; //!< How much impulse gib limit of an object increases per liter of this material.
		float m_GibWoundLimitPerLiter; //!< How much wound gib limit of an object increases per liter of this material.

		unsigned char m_SettleMaterialIndex; //!< The material to turn particles of this into when they settle on the terrain. 0 here means to spawn this material.
		unsigned char m_SpawnMaterialIndex; //!< The material to spawn instead of this one for special effects, etc. 0 here means to spawn this material.
		bool m_IsScrap; //!< Whether this material is scrap material made from gibs of things that have already been blown apart.
		mutable int m_IsBody; //!< Whether this is what bodies are made of (see IsBody). Below 0 until set or first asked for, when it's worked out from the name.

		mutable float m_Metalness; //!< How metallic this looks, 0 to 1. Below 0 until set or first asked for, when it's worked out from the name.
		MaterialBehaviour m_Behaviour; //!< How this behaves in the terrain's liquid, powder and fire simulations.
		mutable float m_Gloss; //!< How glossy this looks, 0 to 1. Below 0 until set or first asked for, when it's worked out from the name.

		Color m_Color; //!< The natural color of this material.
		bool m_UseOwnColor; //!< Whether or not to use the own color when a pixel of this material is knocked loose from the terrain. If 0, then the terrain pixel's color will be applied instead.

		ContentFile m_FGTextureFile; //!< The file pointing to the terrain foreground texture of this Material.
		ContentFile m_BGTextureFile; //!< The file pointing to the terrain background texture of this Material.
		BITMAP* m_TerrainFGTexture; //!< The foreground texture of this Material, used when building an SLTerrain. Not owned.
		BITMAP* m_TerrainBGTexture; //!< The background texture of this Material, used when building an SLTerrain. Not owned.

	private:
		/// Clears all the member variables of this Material, effectively resetting the members of this abstraction level only.
		void Clear();
	};
} // namespace RTE
