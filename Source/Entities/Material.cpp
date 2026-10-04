#include "Material.h"
#include "Constants.h"

#include <algorithm>
#include <string>

using namespace RTE;

ConcreteClassInfo(Material, Entity, 0);

namespace {
	/// How materials look when their INI doesn't say, by name: part of the name, metalness, gloss. The first match wins, so the more particular names come first.
	struct SurfaceGuess {
		const char* NamePart;
		float Metalness;
		float Gloss;
	};
	constexpr SurfaceGuess c_SurfaceGuesses[] = {
	    {"Scrap Metal", 0.7F, 0.35F},
	    {"Mangled Metal", 0.7F, 0.35F},
	    {"Mega Metal", 1.0F, 0.85F},
	    {"Bullet Casing", 1.0F, 0.9F},
	    {"Metal", 0.9F, 0.7F},
	    {"Gold", 1.0F, 0.9F},
	    {"Xenocronium", 0.8F, 0.8F},
	    // The "stuff" materials are used for soldiers' bodies and kit alike, so they're only mildly metallic. Robots and craft say what they are themselves, and held devices count as steel.
	    {"Armoured Military Stuff", 0.45F, 0.45F},
	    {"Military Stuff", 0.3F, 0.35F},
	    {"Civilian Stuff", 0.15F, 0.25F},
	    {"Glass", 0.15F, 1.0F},
	    {"Ice", 0.1F, 0.9F},
	    {"Armoured Flesh", 0.25F, 0.35F},
	    {"Shelled Flesh", 0.15F, 0.45F},
	    {"Kevlared Flesh", 0.1F, 0.2F},
	    {"Flesh", 0.0F, 0.12F},
	    {"Bone", 0.0F, 0.2F},
	    {"Wet Concrete", 0.0F, 0.6F},
	    {"Concrete", 0.0F, 0.3F},
	    {"Stone", 0.0F, 0.2F},
	    {"Bedrock", 0.0F, 0.2F},
	    {"Rubber", 0.0F, 0.25F},
	    {"Bouncy", 0.0F, 0.3F},
	    {"Snow", 0.0F, 0.3F}};

	const SurfaceGuess& GuessSurface(const std::string& materialName) {
		static constexpr SurfaceGuess matte{"", 0.0F, 0.05F};
		for (const SurfaceGuess& guess: c_SurfaceGuesses) {
			if (materialName.find(guess.NamePart) != std::string::npos) {
				return guess;
			}
		}
		return matte;
	}
} // namespace

float Material::GetMetalness() const {
	if (m_Metalness < 0.0F) {
		m_Metalness = GuessSurface(GetPresetName()).Metalness;
	}
	return m_Metalness;
}

float Material::GetGloss() const {
	if (m_Gloss < 0.0F) {
		m_Gloss = GuessSurface(GetPresetName()).Gloss;
	}
	return m_Gloss;
}

void Material::Clear() {
	m_Index = 0;
	m_Priority = -1;
	m_Piling = 0;
	m_Integrity = 0.0F;
	m_Restitution = 0.0F;
	m_Friction = 0.0F;
	m_Stickiness = 0.0F;
	m_VolumeDensity = 0.0F;
	m_PixelDensity = 0.0F;
	m_GibImpulseLimitPerLiter = 0.0F;
	m_GibWoundLimitPerLiter = 0.0F;
	m_SettleMaterialIndex = 0;
	m_SpawnMaterialIndex = 0;
	m_IsScrap = false;
	m_Metalness = -1.0F;
	m_Gloss = -1.0F;
	m_Color.Reset();
	m_UseOwnColor = false;
	m_FGTextureFile.Reset();
	m_BGTextureFile.Reset();
	m_TerrainFGTexture = nullptr;
	m_TerrainBGTexture = nullptr;
}

int Material::Create(const Material& reference) {
	Entity::Create(reference);

	m_Index = reference.m_Index;
	m_Priority = reference.m_Priority;
	m_Piling = reference.m_Piling;
	m_Integrity = reference.m_Integrity;
	m_Restitution = reference.m_Restitution;
	m_Friction = reference.m_Friction;
	m_Stickiness = reference.m_Stickiness;
	m_VolumeDensity = reference.m_VolumeDensity;
	m_PixelDensity = reference.m_PixelDensity;
	m_GibImpulseLimitPerLiter = reference.m_GibImpulseLimitPerLiter;
	m_GibWoundLimitPerLiter = reference.m_GibWoundLimitPerLiter;
	m_SettleMaterialIndex = reference.m_SettleMaterialIndex;
	m_SpawnMaterialIndex = reference.m_SpawnMaterialIndex;
	m_IsScrap = reference.m_IsScrap;
	m_Metalness = reference.m_Metalness;
	m_Gloss = reference.m_Gloss;
	m_Color = reference.m_Color;
	m_UseOwnColor = reference.m_UseOwnColor;
	m_FGTextureFile = reference.m_FGTextureFile;
	m_BGTextureFile = reference.m_BGTextureFile;
	m_TerrainFGTexture = reference.m_TerrainFGTexture;
	m_TerrainBGTexture = reference.m_TerrainBGTexture;

	return 0;
}

int Material::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Entity::ReadProperty(propName, reader));

	MatchProperty("Index", {
		// TODO: Check for index collisions here
		reader >> m_Index;
	});
	MatchProperty("Priority", { reader >> m_Priority; });
	MatchProperty("Piling", { reader >> m_Piling; });
	MatchForwards("Integrity") MatchProperty("StructuralIntegrity", {
		reader >> m_Integrity;
		m_Integrity = (m_Integrity == -1.0F) ? std::numeric_limits<float>::max() : m_Integrity;
	});
	MatchForwards("Restitution") MatchProperty("Bounce", { reader >> m_Restitution; });
	MatchProperty("Friction", { reader >> m_Friction; });
	MatchProperty("Stickiness", { reader >> m_Stickiness; });
	MatchProperty("DensityKGPerVolumeL", {
		reader >> m_VolumeDensity;
		// Overrides the pixel density
		m_PixelDensity = m_VolumeDensity * c_LPP;
	});
	MatchProperty("DensityKGPerPixel", {
		reader >> m_PixelDensity;
		// Overrides the volume density
		m_VolumeDensity = m_PixelDensity * c_PPL;
	});
	MatchProperty("GibImpulseLimitPerVolumeL", { reader >> m_GibImpulseLimitPerLiter; });
	MatchProperty("GibWoundLimitPerVolumeL", { reader >> m_GibWoundLimitPerLiter; });
	MatchProperty("SettleMaterial", { reader >> m_SettleMaterialIndex; });
	MatchForwards("SpawnMaterial") MatchProperty("TransformsInto", { reader >> m_SpawnMaterialIndex; });
	MatchProperty("IsScrap", { reader >> m_IsScrap; });
	MatchProperty("Metalness", {
		reader >> m_Metalness;
		m_Metalness = std::clamp(m_Metalness, 0.0F, 1.0F);
	});
	MatchProperty("Gloss", {
		reader >> m_Gloss;
		m_Gloss = std::clamp(m_Gloss, 0.0F, 1.0F);
	});
	MatchProperty("Color", { reader >> m_Color; });
	MatchProperty("UseOwnColor", { reader >> m_UseOwnColor; });
	MatchProperty("FGTextureFile", {
		reader >> m_FGTextureFile;
		m_TerrainFGTexture = m_FGTextureFile.GetAsBitmap();
	});
	MatchProperty("BGTextureFile", {
		reader >> m_BGTextureFile;
		m_TerrainBGTexture = m_BGTextureFile.GetAsBitmap();
	});

	EndPropertyList;
}

int Material::Save(Writer& writer) const {
	Entity::Save(writer);
	// Materials should never be altered, so no point in saving additional properties when it's a copy
	if (m_IsOriginalPreset) {
		writer.NewPropertyWithValue("Priority", m_Priority);
		writer.NewPropertyWithValue("Piling", m_Piling);
		writer.NewPropertyWithValue("StructuralIntegrity", m_Integrity);
		writer.NewPropertyWithValue("Restitution", m_Restitution);
		writer.NewPropertyWithValue("Friction", m_Friction);
		writer.NewPropertyWithValue("Stickiness", m_Stickiness);
		writer.NewPropertyWithValue("DensityKGPerVolumeL", m_VolumeDensity);
		writer.NewPropertyWithValue("GibImpulseLimitPerVolumeL", m_GibImpulseLimitPerLiter);
		writer.NewPropertyWithValue("GibWoundLimitPerVolumeL", m_GibWoundLimitPerLiter);
		writer.NewPropertyWithValue("SettleMaterial", m_SettleMaterialIndex);
		writer.NewPropertyWithValue("SpawnMaterial", m_SpawnMaterialIndex);
		writer.NewPropertyWithValue("IsScrap", m_IsScrap);
		writer.NewPropertyWithValue("Color", m_Color);
		writer.NewPropertyWithValue("UseOwnColor", m_UseOwnColor);
		writer.NewPropertyWithValue("FGTextureFile", m_FGTextureFile);
		writer.NewPropertyWithValue("BGTextureFile", m_BGTextureFile);
	}
	return 0;
}
