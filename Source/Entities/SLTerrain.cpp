#include "SLTerrain.h"
#include "TerrainFrosting.h"
#include "TerrainDebris.h"
#include "TerrainObject.h"
#include "PostProcessMan.h"
#include "EffectsParticles.h"
#include "RTETools.h"

#include <functional>
#include "SceneObject.h"
#include "MOSprite.h"
#include "MOPixel.h"
#include "Atom.h"
#include "DataModule.h"
#include "PresetMan.h"
#include "Draw.h"
#include "tracy/Tracy.hpp"

#include <array>
#include <execution>

using namespace RTE;

ConcreteClassInfo(SLTerrain, SceneLayer, 0);

SLTerrain::SLTerrain() {
	Clear();
}

SLTerrain::~SLTerrain() {
	Destroy(true);
}

const std::string TerrainLight::c_ClassName = "TerrainLight";

void TerrainLight::Clear() {
	m_Pos.Reset();
	m_Color.SetRGB(255, 225, 180);
	m_Radius = 110.0F;
	m_Intensity = 1.0F;
	m_Flicker = 0.0F;
	m_Pulse = 0.0F;
	m_ConeAngle = 0.0F;
	m_ConeDirection = 90.0F;
	m_Anchored = -1;
	m_AnchorOffset.Reset();
}

int TerrainLight::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Serializable::ReadProperty(propName, reader));

	MatchProperty("Offset", { reader >> m_Pos; });
	MatchProperty("Position", { reader >> m_Pos; });
	MatchProperty("Color", { reader >> m_Color; });
	MatchProperty("Radius", { reader >> m_Radius; });
	MatchProperty("Intensity", { reader >> m_Intensity; });
	MatchProperty("Flicker", { reader >> m_Flicker; });
	MatchProperty("Pulse", { reader >> m_Pulse; });
	MatchProperty("ConeAngle", { reader >> m_ConeAngle; });
	MatchProperty("ConeDirection", { reader >> m_ConeDirection; });
	MatchProperty("Anchored", { reader >> m_Anchored; });
	MatchProperty("AnchorOffset", { reader >> m_AnchorOffset; });

	EndPropertyList;
}

int TerrainLight::Save(Writer& writer) const {
	Serializable::Save(writer);

	writer.NewPropertyWithValue("Position", m_Pos);
	writer.NewPropertyWithValue("Color", m_Color);
	writer.NewPropertyWithValue("Radius", m_Radius);
	writer.NewPropertyWithValue("Intensity", m_Intensity);
	if (m_Flicker > 0.0F) {
		writer.NewPropertyWithValue("Flicker", m_Flicker);
	}
	if (m_Pulse > 0.0F) {
		writer.NewPropertyWithValue("Pulse", m_Pulse);
	}
	if (m_ConeAngle > 0.0F) {
		writer.NewPropertyWithValue("ConeAngle", m_ConeAngle);
		writer.NewPropertyWithValue("ConeDirection", m_ConeDirection);
	}
	if (m_Anchored >= 0) {
		writer.NewPropertyWithValue("Anchored", m_Anchored);
		writer.NewPropertyWithValue("AnchorOffset", m_AnchorOffset);
	}
	return 0;
}

void SLTerrain::Clear() {
	m_Width = 0;
	m_Height = 0;
	m_LayerToDraw = LayerType::ForegroundLayer;
	m_FGColorLayer = nullptr;
	m_BGColorLayer = nullptr;
	m_DefaultBGTextureFile.Reset();
	m_TerrainFrostings.clear();
	m_TerrainDebris.clear();
	m_TerrainObjects.clear();
	m_Lights.clear();
	m_LightCheckCounter = 0;
	m_LightCells.clear();
	m_LightCellColumns = 0;
	m_LightCellsStale = true;
	m_LightBreaks.clear();
	m_UpdatedMaterialAreas.clear();
	m_OrbitDirection = Directions::Up;

	m_ScrollInfo = Vector(0.0f, 0.0f);
	m_ScrollRatio = Vector(0.0f, 0.0f);
}

int SLTerrain::Create() {
	SceneLayer::Create();

	m_Width = m_BitmapFile.GetImageWidth();
	m_Height = m_BitmapFile.GetImageHeight();

	if (!m_FGColorLayer.get()) {
		m_FGColorLayer = std::make_unique<SceneLayer>();
		m_FGColorLayer->SetZOrder(c_DefaultDrawDepth);
	}
	if (!m_BGColorLayer.get()) {
		m_BGColorLayer = std::make_unique<SceneLayer>();
		m_BGColorLayer->SetZOrder(c_TerrainBGDepth);
	}

	return 0;
}

int SLTerrain::Create(const SLTerrain& reference) {
	SceneLayer::Create(reference);

	m_Width = reference.m_Width;
	m_Height = reference.m_Height;

	// Copy the layers but not the layer BITMAPs because they will be loaded later by LoadData.
	m_FGColorLayer.reset(dynamic_cast<SceneLayer*>(reference.m_FGColorLayer->Clone()));
	m_BGColorLayer.reset(dynamic_cast<SceneLayer*>(reference.m_BGColorLayer->Clone()));

	m_DefaultBGTextureFile = reference.m_DefaultBGTextureFile;

	m_TerrainFrostings.clear();
	for (TerrainFrosting* terrainFrosting: reference.m_TerrainFrostings) {
		m_TerrainFrostings.emplace_back(terrainFrosting);
	}
	m_TerrainDebris.clear();
	for (TerrainDebris* terrainDebris: reference.m_TerrainDebris) {
		m_TerrainDebris.emplace_back(terrainDebris);
	}
	m_TerrainObjects.clear();
	for (TerrainObject* terrainObject: reference.m_TerrainObjects) {
		m_TerrainObjects.emplace_back(terrainObject);
	}

	m_Lights = reference.m_Lights;
	m_LightCellsStale = true;

	m_OrbitDirection = reference.m_OrbitDirection;

	return 0;
}

int SLTerrain::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return SceneLayer::ReadProperty(propName, reader));

	MatchProperty("BackgroundTexture", { reader >> m_DefaultBGTextureFile; });
	MatchProperty("FGColorLayer", {
		m_FGColorLayer = std::make_unique<SceneLayer>();
		m_FGColorLayer->SetZOrder(c_DefaultDrawDepth);
		reader >> m_FGColorLayer.get();
	});
	MatchProperty("BGColorLayer", {
		m_BGColorLayer = std::make_unique<SceneLayer>();
		m_BGColorLayer->SetZOrder(c_TerrainBGDepth);
		reader >> m_BGColorLayer.get();
	});
	MatchProperty("AddTerrainFrosting", {
		std::unique_ptr<TerrainFrosting> terrainFrosting = std::make_unique<TerrainFrosting>();
		reader >> terrainFrosting.get();
		m_TerrainFrostings.emplace_back(terrainFrosting.release());
	});
	MatchProperty("AddTerrainDebris", {
		std::unique_ptr<TerrainDebris> terrainDebris = std::make_unique<TerrainDebris>();
		reader >> terrainDebris.get();
		m_TerrainDebris.emplace_back(terrainDebris.release());
	});
	MatchProperty("PlaceTerrainObject", {
		std::unique_ptr<TerrainObject> terrainObject = std::make_unique<TerrainObject>();
		reader >> terrainObject.get();
		m_TerrainObjects.emplace_back(terrainObject.release());
	});
	MatchProperty("AddLight", {
		TerrainLight light;
		reader >> light;
		m_Lights.emplace_back(light);
		m_LightCellsStale = true;
	});
	MatchProperty("OrbitDirection", {
		std::string orbitDirection;
		reader >> orbitDirection;
		if (orbitDirection == "Up") {
			m_OrbitDirection = Directions::Up;
		} else if (orbitDirection == "Down") {
			m_OrbitDirection = Directions::Down;
		} else if (orbitDirection == "Left") {
			m_OrbitDirection = Directions::Left;
		} else if (orbitDirection == "Right") {
			m_OrbitDirection = Directions::Right;
		} else {
			reader.ReportError("Unknown OrbitDirection '" + orbitDirection + "'!");
		}
	});

	EndPropertyList;
}

int SLTerrain::Save(Writer& writer) const {
	SceneLayer::Save(writer);

	// Only write the background texture info if the background itself is not saved out as a file already, since saved, pre-rendered bitmaps don't need texturing.
	if (m_BGColorLayer->IsLoadedFromDisk()) {
		writer.NewPropertyWithValue("BGColorLayer", m_BGColorLayer.get());
	} else {
		writer.NewPropertyWithValue("BackgroundTexture", m_DefaultBGTextureFile);
	}

	// Only write the procedural parameters if the foreground itself is not saved out as a file already, since saved, pre-rendered bitmaps don't need procedural generation.
	if (m_FGColorLayer->IsLoadedFromDisk()) {
		writer.NewPropertyWithValue("FGColorLayer", m_FGColorLayer.get());
	} else {
		for (const TerrainFrosting* terrainFrosting: m_TerrainFrostings) {
			writer.NewPropertyWithValue("AddTerrainFrosting", terrainFrosting);
		}
		for (const TerrainDebris* terrainDebris: m_TerrainDebris) {
			writer.NewPropertyWithValue("AddTerrainDebris", terrainDebris);
		}
		for (const TerrainObject* terrainObject: m_TerrainObjects) {
			// Write out only what is needed to place a copy of this in the Terrain
			writer.NewProperty("PlaceTerrainObject");
			writer.ObjectStart(terrainObject->GetClassName());
			writer.NewPropertyWithValue("CopyOf", terrainObject->GetModuleAndPresetName());
			writer.NewPropertyWithValue("Position", terrainObject->GetPos());
			writer.ObjectEnd();
		}
	}

	// Lights that came with placed objects are written too. Should the objects be placed again on loading, each just replaces its own.
	for (const TerrainLight& light: m_Lights) {
		writer.NewProperty("AddLight");
		writer << light;
	}

	writer.NewProperty("OrbitDirection");
	switch (m_OrbitDirection) {
		default:
		case Directions::Up:
			writer << "Up";
			break;
		case Directions::Down:
			writer << "Down";
			break;
		case Directions::Left:
			writer << "Left";
			break;
		case Directions::Right:
			writer << "Right";
			break;
	}

	return 0;
}

// TODO: Break this down and refactor.
void SLTerrain::TexturizeTerrain() {
	BITMAP* defaultBGLayerTexture = m_DefaultBGTextureFile.GetAsBitmap();

	BITMAP* fgLayerTexture = m_FGColorLayer->GetBitmap();
	BITMAP* bgLayerTexture = m_BGColorLayer->GetBitmap();

	const std::array<Material*, c_PaletteEntriesNumber>& materialPalette = g_SceneMan.GetMaterialPalette();
	const std::array<unsigned char, c_PaletteEntriesNumber>& materialMappings = g_PresetMan.GetDataModule(m_BitmapFile.GetDataModuleID())->GetAllMaterialMappings();

	std::array<BITMAP*, c_PaletteEntriesNumber> materialFGTextures;
	materialFGTextures.fill(nullptr);
	std::array<BITMAP*, c_PaletteEntriesNumber> materialBGTextures;
	materialBGTextures.fill(nullptr);
	std::array<int, c_PaletteEntriesNumber> materialColors;
	materialColors.fill(0);

	// We want to multithread this, however parallel fors only work on container types
	// This is sorta ugly, but a necessary evil for now :)
	std::vector<int> rows(m_MainBitmap->h); // we loop through h first, because we want each thread to have sequential memory that they're touching
	std::iota(std::begin(rows), std::end(rows), 0);

	// Go through each pixel on the main bitmap, which contains all the material pixels loaded from the bitmap.
	// Place texture pixels on the FG layer corresponding to the materials on the main material bitmap.
	std::for_each(std::execution::par_unseq, std::begin(rows), std::end(rows),
	              [&](int yPos) {
		              for (int xPos = 0; xPos < m_MainBitmap->w; ++xPos) {
			              int matIndex = _getpixel(m_MainBitmap, xPos, yPos);

			              // Map any materials defined in this data module but initially collided with other material ID's and thus were displaced to other ID's.
			              if (materialMappings[matIndex] != 0) {
				              // Assign the mapping and put it onto the material bitmap too.
				              matIndex = materialMappings[matIndex];
				              _putpixel(m_MainBitmap, xPos, yPos, matIndex);
			              }

			              RTEAssert(matIndex >= 0 && matIndex < c_PaletteEntriesNumber, "Invalid material index!");

			              // Validate the material, or fallback to default material.
			              const Material* material = materialPalette[matIndex] ? materialPalette[matIndex] : materialPalette[MaterialColorKeys::g_MaterialOutOfBounds];

			              BITMAP* fgTexture = materialFGTextures[matIndex];
			              BITMAP* bgTexture = materialBGTextures[matIndex];

			              // If haven't read a pixel of this material before, then get its texture so we can quickly access it.
			              if (!fgTexture && material->GetFGTexture()) {
				              fgTexture = materialFGTextures[matIndex] = material->GetFGTexture();
			              }

			              if (!bgTexture && material->GetBGTexture()) {
				              bgTexture = materialBGTextures[matIndex] = material->GetBGTexture();
			              }

			              int fgPixelColor = 0;

			              // If actually no texture for the material, then use the material's solid color instead.
			              if (!fgTexture) {
				              if (materialColors[matIndex] == 0) {
					              materialColors[matIndex] = material->GetColor().GetIndex();
				              }
				              fgPixelColor = materialColors[matIndex];
			              } else {
				              fgPixelColor = _getpixel(fgTexture, xPos % fgTexture->w, yPos % fgTexture->h);
			              }
			              _putpixel(fgLayerTexture, xPos, yPos, fgPixelColor);

			              int bgPixelColor = 0;
			              if (matIndex == 0) {
				              bgPixelColor = ColorKeys::g_MaskColor;
			              } else {
				              if (!bgTexture) {
					              bgPixelColor = _getpixel(defaultBGLayerTexture, xPos % defaultBGLayerTexture->w, yPos % defaultBGLayerTexture->h);
				              } else {
					              bgPixelColor = _getpixel(bgTexture, xPos % bgTexture->w, yPos % bgTexture->h);
				              }
			              }

			              _putpixel(bgLayerTexture, xPos, yPos, bgPixelColor);
		              }
	              });
}

void SLTerrain::AddLight(const TerrainLight& light) {
	if (light.m_Radius <= 0.0F || light.m_Intensity <= 0.0F) {
		return;
	}
	m_LightCellsStale = true;
	TerrainLight newLight = light;
	if (m_WrapX && m_MainBitmap) {
		newLight.m_Pos.m_X = std::fmod(std::fmod(newLight.m_Pos.m_X, static_cast<float>(m_MainBitmap->w)) + static_cast<float>(m_MainBitmap->w), static_cast<float>(m_MainBitmap->w));
	}
	for (TerrainLight& existing: m_Lights) {
		// One lamp to a spot: background pieces are painted with the glow of the lamp of the module they belong in, and both bring it.
		if (std::abs(existing.m_Pos.m_X - newLight.m_Pos.m_X) < 10.0F && std::abs(existing.m_Pos.m_Y - newLight.m_Pos.m_Y) < 10.0F) {
			existing = newLight;
			return;
		}
	}
	m_Lights.emplace_back(newLight);
}

int SLTerrain::RemoveLights(const std::function<bool(const TerrainLight&)>& shouldRemove) {
	size_t before = m_Lights.size();
	std::erase_if(m_Lights, shouldRemove);
	m_LightCellsStale = true;
	return static_cast<int>(before - m_Lights.size());
}

namespace {
	constexpr int c_LightCellSize = 16;
}

void SLTerrain::BreakLightsNear(const Vector& pos, float radius) {
	if (m_Lights.empty()) {
		return;
	}
	std::lock_guard<std::mutex> lock(m_LightBreaksMutex);
	m_LightBreaks.emplace_back(pos, radius);
}

void SLTerrain::ShootLightsAlong(const Vector& from, const Vector& to) {
	if (m_Lights.empty() || m_LightCells.empty()) {
		return;
	}
	Vector path = to - from;
	float length = path.GetMagnitude();
	// A jump this long is a wrap across the scene's seam, not a flight.
	if (length > 400.0F) {
		return;
	}
	int steps = std::max(static_cast<int>(std::ceil(length / 5.0F)), 1);
	int rows = static_cast<int>(m_LightCells.size()) / m_LightCellColumns;
	for (int step = 0; step <= steps; ++step) {
		Vector point = from + path * (static_cast<float>(step) / static_cast<float>(steps));
		int cellX = static_cast<int>(std::floor(point.m_X)) / c_LightCellSize;
		int cellY = static_cast<int>(std::floor(point.m_Y)) / c_LightCellSize;
		if (point.m_X < 0.0F || point.m_Y < 0.0F || cellX >= m_LightCellColumns || cellY >= rows || !m_LightCells[cellY * m_LightCellColumns + cellX]) {
			continue;
		}
		for (const TerrainLight& light: m_Lights) {
			if (std::abs(light.m_Pos.m_X - point.m_X) < 4.5F && std::abs(light.m_Pos.m_Y - point.m_Y) < 4.5F) {
				BreakLightsNear(light.m_Pos, 1.0F);
				return;
			}
		}
	}
}

void SLTerrain::UpdateLights() {
	if (m_Lights.empty()) {
		return;
	}
	auto solidAt = [](const Vector& point) { return g_SceneMan.GetTerrMatter(point.GetFloorIntX(), point.GetFloorIntY()) > MaterialColorKeys::g_MaterialCavity; };

	// Lamps that were shot or blown up since the last update go out.
	{
		std::lock_guard<std::mutex> lock(m_LightBreaksMutex);
		if (!m_LightBreaks.empty()) {
			for (auto light = m_Lights.begin(); light != m_Lights.end();) {
				bool smashed = false;
				for (const auto& [where, radius]: m_LightBreaks) {
					if (g_SceneMan.ShortestDistance(where, light->m_Pos, m_WrapX).MagnitudeIsLessThan(radius)) {
						smashed = true;
						break;
					}
				}
				if (smashed) {
					EffectsParticles::Emit("Sparks", light->m_Pos, Vector(0.0F, 2.0F), 1.0F, 14, 0);
					light = m_Lights.erase(light);
					m_LightCellsStale = true;
				} else {
					++light;
				}
			}
			m_LightBreaks.clear();
		}
	}
	if (m_LightCellsStale && m_MainBitmap) {
		// Mark the cell of each lamp and the ones around it.
		m_LightCellsStale = false;
		m_LightCellColumns = m_MainBitmap->w / c_LightCellSize + 1;
		int rows = m_MainBitmap->h / c_LightCellSize + 1;
		m_LightCells.assign(static_cast<size_t>(m_LightCellColumns) * rows, 0);
		for (const TerrainLight& light: m_Lights) {
			int cellX = static_cast<int>(light.m_Pos.m_X) / c_LightCellSize;
			int cellY = static_cast<int>(light.m_Pos.m_Y) / c_LightCellSize;
			for (int y = std::max(cellY - 1, 0); y <= std::min(cellY + 1, rows - 1); ++y) {
				for (int x = std::max(cellX - 1, 0); x <= std::min(cellX + 1, m_LightCellColumns - 1); ++x) {
					m_LightCells[y * m_LightCellColumns + x] = 1;
				}
			}
		}
	}
	if (m_Lights.empty()) {
		return;
	}

	bool checkFixtures = ++m_LightCheckCounter >= 20;
	if (checkFixtures) {
		m_LightCheckCounter = 0;
	}
	float time = PostProcessMan::GetSmoothSimTime();
	for (auto light = m_Lights.begin(); light != m_Lights.end();) {
		if (light->m_Anchored < 0) {
			// What does it hang on? Something solid right at it, else the nearest solid thing within a few pixels: above first, as lamps mostly hang from ceilings.
			light->m_Anchored = 0;
			static const std::array<Vector, 4> directions = {Vector(0.0F, -1.0F), Vector(-1.0F, 0.0F), Vector(1.0F, 0.0F), Vector(0.0F, 1.0F)};
			for (int distance = 0; distance <= 5 && light->m_Anchored == 0; ++distance) {
				for (const Vector& direction: directions) {
					if (solidAt(light->m_Pos + direction * static_cast<float>(distance))) {
						light->m_Anchored = 1;
						light->m_AnchorOffset = direction * static_cast<float>(distance);
						break;
					}
					if (distance == 0) {
						break;
					}
				}
			}
		} else if (checkFixtures && light->m_Anchored == 1 && !solidAt(light->m_Pos + light->m_AnchorOffset)) {
			// What it hung on has been shot or blown away: it goes out in a shower of sparks.
			EffectsParticles::Emit("Sparks", light->m_Pos, Vector(0.0F, 2.0F), 1.0F, 14, 0);
			light = m_Lights.erase(light);
			m_LightCellsStale = true;
			continue;
		}
		float brightness = light->m_Intensity;
		if (light->m_Flicker > 0.0F) {
			brightness *= 1.0F - light->m_Flicker * RandomNum(0.0F, 1.0F);
		}
		if (light->m_Pulse > 0.0F) {
			brightness *= 0.5F + 0.5F * std::sin(time * light->m_Pulse * c_TwoPI + light->m_Pos.m_X);
		}
		// The player's settings for scenery lamps: brighter or dimmer, further or nearer, and a tint (applied to the displayed color, so it works like a gel over the lamp).
		const LightingSettings& lighting = g_PostProcessMan.GetLightingSettings();
		brightness *= lighting.LampBrightness;
		float reach = light->m_Radius * lighting.LampReach;
		glm::vec3 gel = glm::pow(glm::max(lighting.LampTint, glm::vec3(0.0F)), glm::vec3(1.0F / 2.2F));
		glm::vec3 color = glm::vec3(light->m_Color.GetR(), light->m_Color.GetG(), light->m_Color.GetB()) * gel;
		if (light->m_ConeAngle > 0.0F) {
			float direction = light->m_ConeDirection * c_PI / 180.0F;
			g_PostProcessMan.RegisterConeLight(light->m_Pos, Vector(std::cos(direction), std::sin(direction)), light->m_ConeAngle, color, reach, brightness);
		} else {
			g_PostProcessMan.RegisterLight(light->m_Pos, color, reach, brightness);
		}
		++light;
	}
}

int SLTerrain::LoadData() {
	SceneLayer::LoadData();

	RTEAssert(m_FGColorLayer.get(), "Terrain's foreground layer not instantiated before trying to load its data!");
	RTEAssert(m_BGColorLayer.get(), "Terrain's background layer not instantiated before trying to load its data!");

	if (m_FGColorLayer->IsLoadedFromDisk() && m_BGColorLayer->IsLoadedFromDisk()) {
		m_FGColorLayer->LoadData();
		m_BGColorLayer->LoadData();
	} else {
		m_FGColorLayer->Destroy();
		m_FGColorLayer->Create(create_bitmap_ex(8, m_MainBitmap->w, m_MainBitmap->h), m_Offset, m_WrapX, m_WrapY, m_ScrollInfo);

		m_BGColorLayer->Destroy();
		m_BGColorLayer->Create(create_bitmap_ex(8, m_MainBitmap->w, m_MainBitmap->h), m_Offset, m_WrapX, m_WrapY, m_ScrollInfo);
	}
	// However the layers were made (Destroy above clears it), the background walls are drawn behind the objects and the foreground in front. The lighting tells them apart by this depth.
	m_FGColorLayer->SetZOrder(c_DefaultDrawDepth);
	m_BGColorLayer->SetZOrder(c_TerrainBGDepth);
	if (!(m_FGColorLayer->IsLoadedFromDisk() && m_BGColorLayer->IsLoadedFromDisk())) {

		TexturizeTerrain();

		for (const TerrainFrosting* terrainFrosting: m_TerrainFrostings) {
			terrainFrosting->FrostTerrain(this);
		}
		for (TerrainDebris* terrainDebris: m_TerrainDebris) {
			terrainDebris->ScatterOnTerrain(this);
		}
		for (TerrainObject* terrainObject: m_TerrainObjects) {
			terrainObject->PlaceOnTerrain(this);
		}
		CleanAir();
	}

	m_ScrollInfo.SetXY(0.0f, 0.0f);
	m_ScrollRatio.SetXY(0.0f, 0.0f);

	m_FGColorLayer->SetScrollRatio(Vector(0.0f, 0.0f));
	m_BGColorLayer->SetScrollRatio(Vector(0.0f, 0.0f));

	return 0;
}

int SLTerrain::SaveData(const std::string& pathBase) {
	if (pathBase.empty()) {
		return -1;
	}
	SceneLayer::SaveData(pathBase + " Mat.png");
	m_FGColorLayer->SaveData(pathBase + " FG.png");
	m_BGColorLayer->SaveData(pathBase + " BG.png");
	return 0;
}

void SLTerrain::CopyBitmapData(std::vector<SceneLayerInfo>& layerInfos) const {
	layerInfos.emplace_back(std::string("Mat"), SceneLayer::CopyBitmap());
	layerInfos.emplace_back(std::string("FG"), m_FGColorLayer->CopyBitmap());
	layerInfos.emplace_back(std::string("BG"), m_BGColorLayer->CopyBitmap());
}

int SLTerrain::ClearData() {
	RTEAssert(SceneLayer::ClearData() == 0, "Failed to clear material bitmap data of an SLTerrain!");
	RTEAssert(m_FGColorLayer && m_FGColorLayer->ClearData() == 0, "Failed to clear the foreground color bitmap data of an SLTerrain!");
	RTEAssert(m_BGColorLayer && m_BGColorLayer->ClearData() == 0, "Failed to clear the background color bitmap data of an SLTerrain!");
	return 0;
}

bool SLTerrain::IsAirPixel(const int pixelX, const int pixelY) const {
	int checkPixel = GetPixel(pixelX, pixelY);
	return checkPixel == MaterialColorKeys::g_MaterialAir || checkPixel == MaterialColorKeys::g_MaterialCavity;
}

bool SLTerrain::IsBoxBuried(const Box& checkBox) const {
	bool buried = true;
	buried = buried && !IsAirPixel(checkBox.GetCorner().GetFloorIntX(), checkBox.GetCorner().GetFloorIntY());
	buried = buried && !IsAirPixel(static_cast<int>(checkBox.GetCorner().GetX() + checkBox.GetWidth()), checkBox.GetCorner().GetFloorIntY());
	buried = buried && !IsAirPixel(checkBox.GetCorner().GetFloorIntX(), static_cast<int>(checkBox.GetCorner().GetY() + checkBox.GetHeight()));
	buried = buried && !IsAirPixel(static_cast<int>(checkBox.GetCorner().GetX() + checkBox.GetWidth()), static_cast<int>(checkBox.GetCorner().GetY() + checkBox.GetHeight()));
	return buried;
}

void SLTerrain::CleanAir() {
	std::vector<int> rows(m_MainBitmap->h); // we loop through h first, because we want each thread to have sequential memory that they're touching
	std::iota(std::begin(rows), std::end(rows), 0);

	std::for_each(std::execution::par_unseq, std::begin(rows), std::end(rows),
	              [&](int yPos) {
		              for (int xPos = 0; xPos < m_MainBitmap->w; ++xPos) {
			              int matPixel = _getpixel(m_MainBitmap, xPos, yPos);
			              if (matPixel == MaterialColorKeys::g_MaterialCavity) {
				              _putpixel(m_MainBitmap, xPos, yPos, MaterialColorKeys::g_MaterialAir);
				              matPixel = MaterialColorKeys::g_MaterialAir;
			              }
			              if (matPixel == MaterialColorKeys::g_MaterialAir) {
				              _putpixel(m_FGColorLayer->GetBitmap(), xPos, yPos, ColorKeys::g_MaskColor);
			              }
		              }
	              });
}

void SLTerrain::CleanAirBox(const Box& box, bool wrapsX, bool wrapsY) {
	int width = m_MainBitmap->w;
	int height = m_MainBitmap->h;

	for (int y = box.m_Corner.GetFloorIntY(); y < static_cast<int>(box.m_Corner.GetY() + box.m_Height); ++y) {
		for (int x = box.m_Corner.GetFloorIntX(); x < static_cast<int>(box.m_Corner.GetX() + box.m_Width); ++x) {
			int wrappedX = x;
			int wrappedY = y;

			if (wrapsX) {
				if (wrappedX < 0) {
					wrappedX += width;
				}
				if (wrappedX >= width) {
					wrappedX -= width;
				}
			}
			if (wrapsY) {
				if (wrappedY < 0) {
					wrappedY += height;
				}
				if (wrappedY >= height) {
					wrappedY -= height;
				}
			}
			if (wrappedX >= 0 && wrappedX < width && wrappedY >= 0 && wrappedY < height) {
				int matPixel = _getpixel(m_MainBitmap, wrappedX, wrappedY);
				if (matPixel == MaterialColorKeys::g_MaterialCavity) {
					_putpixel(m_MainBitmap, wrappedX, wrappedY, MaterialColorKeys::g_MaterialAir);
					matPixel = MaterialColorKeys::g_MaterialAir;
				}
				if (matPixel == MaterialColorKeys::g_MaterialAir) {
					_putpixel(m_FGColorLayer->GetBitmap(), wrappedX, wrappedY, ColorKeys::g_MaskColor);
				}
			}
		}
	}
}

// TODO: OPTIMIZE THIS, IT'S A TIME HOG. MAYBE JSUT STAMP THE OUTLINE AND SAMPLE SOME RANDOM PARTICLES?
std::deque<MOPixel*> SLTerrain::EraseSilhouette(BITMAP* sprite, const Vector& pos, const Vector& pivot, const Matrix& rotation, float scale, bool makeMOPs, int skipMOP, int maxMOPs) {
	RTEAssert(sprite, "Null BITMAP passed to SLTerrain::EraseSilhouette");

	int maxWidth = static_cast<int>(static_cast<float>(sprite->w + std::abs(pivot.GetFloorIntX() - (sprite->w / 2))) * scale);
	int maxHeight = static_cast<int>(static_cast<float>(sprite->h + std::abs(pivot.GetFloorIntY() - (sprite->h / 2))) * scale);
	int maxDiameter = static_cast<int>(std::sqrt(static_cast<float>(maxWidth * maxWidth + maxHeight * maxHeight)) * 2.0F);
	int skipCount = skipMOP;

	BITMAP* tempBitmap = g_SceneMan.GetIntermediateBitmapForSettlingIntoTerrain(maxDiameter);
	clear_bitmap(tempBitmap);
	pivot_scaled_sprite(tempBitmap, sprite, tempBitmap->w / 2, tempBitmap->h / 2, pivot.GetFloorIntX(), pivot.GetFloorIntY(), ftofix(rotation.GetAllegroAngle()), ftofix(scale));

	std::deque<MOPixel*> dislodgedMOPixels;

	// Test intersection between color pixels of the test bitmap and non-air pixels of the terrain, then generate and collect MOPixels that represent the terrain overlap and clear the same pixels out of the terrain.
	for (int testY = 0; testY < tempBitmap->h; ++testY) {
		for (int testX = 0; testX < tempBitmap->w; ++testX) {
			int terrX = pos.GetFloorIntX() - (tempBitmap->w / 2) + testX;
			int terrY = pos.GetFloorIntY() - (tempBitmap->h / 2) + testY;

			if (terrX < 0) {
				if (m_WrapX) {
					while (terrX < 0) {
						terrX += m_MainBitmap->w;
					}
				} else {
					continue;
				}
			}
			if (terrY < 0) {
				if (m_WrapY) {
					while (terrY < 0) {
						terrY += m_MainBitmap->h;
					}
				} else {
					continue;
				}
			}
			if (terrX >= m_MainBitmap->w) {
				if (m_WrapX) {
					terrX %= m_MainBitmap->w;
				} else {
					continue;
				}
			}
			if (terrY >= m_MainBitmap->h) {
				if (m_WrapY) {
					terrY %= m_MainBitmap->h;
				} else {
					continue;
				}
			}
			int matPixel = getpixel(m_MainBitmap, terrX, terrY);
			int colorPixel = getpixel(m_FGColorLayer->GetBitmap(), terrX, terrY);

			if (getpixel(tempBitmap, testX, testY) != ColorKeys::g_MaskColor) {
				// Only add PixelMO if we're not due to skip any.
				if (makeMOPs && matPixel != MaterialColorKeys::g_MaterialAir && colorPixel != ColorKeys::g_MaskColor && ++skipCount > skipMOP && dislodgedMOPixels.size() < maxMOPs) {
					skipCount = 0;
					const Material* sceneMat = g_SceneMan.GetMaterialFromID(matPixel);
					const Material* spawnMat = sceneMat->GetSpawnMaterial() ? g_SceneMan.GetMaterialFromID(sceneMat->GetSpawnMaterial()) : sceneMat;

					std::unique_ptr<Atom> terrainPixelAtom = std::make_unique<Atom>(Vector(), spawnMat->GetIndex(), nullptr, colorPixel, 2);
					std::unique_ptr<MOPixel> terrainPixel = std::make_unique<MOPixel>(colorPixel, spawnMat->GetPixelDensity(), Vector(static_cast<float>(terrX), static_cast<float>(terrY)), Vector(), terrainPixelAtom.release(), 0);
#ifndef RELEASE_BUILD
					terrainPixel->SetDescription("Dislodged Terrain Pixel from Material " + std::to_string(sceneMat->GetIndex()));
#endif
					terrainPixel->SetToHitMOs(false);
					dislodgedMOPixels.emplace_back(terrainPixel.release());
				}

				// Clear the terrain pixels.
				if (matPixel != MaterialColorKeys::g_MaterialAir) {
					putpixel(m_MainBitmap, terrX, terrY, MaterialColorKeys::g_MaterialAir);
				}
				if (colorPixel != ColorKeys::g_MaskColor) {
					putpixel(m_FGColorLayer->GetBitmap(), terrX, terrY, ColorKeys::g_MaskColor);
				}
			}
		}
	}
	// TODO: improve fit/tightness of box here.
	m_UpdatedMaterialAreas.emplace_back(Box(pos - pivot, static_cast<float>(maxWidth), static_cast<float>(maxHeight)));

	return dislodgedMOPixels;
}

void SLTerrain::Update() {
	SceneLayer::Update();

	m_FGColorLayer->SetOffset(m_Offset);
	m_BGColorLayer->SetOffset(m_Offset);
}

void SLTerrain::Draw(const Box& targetDimensions, Box& targetBox, bool offsetNeedsScrollRatioAdjustment) {
	switch (m_LayerToDraw) {
		case LayerType::MaterialLayer:
			SceneLayer::Draw(targetDimensions, targetBox);
			break;
		case LayerType::ForegroundLayer:
			m_FGColorLayer->Draw(targetDimensions, targetBox);
			break;
		case LayerType::BackgroundLayer:
			m_BGColorLayer->Draw(targetDimensions, targetBox);
			break;
		default:
			RTEAbort("Invalid LayerType was set to draw in SLTerrain::Draw!");
			break;
	}
}

void SLTerrain::Draw(const Camera& camera) {
	ZoneScoped;
	switch (m_LayerToDraw) {
		case LayerType::MaterialLayer:
			SceneLayer::Draw(camera);
			break;
		case LayerType::ForegroundLayer:
			m_FGColorLayer->Draw(camera);
			break;
		case LayerType::BackgroundLayer:
			m_BGColorLayer->Draw(camera);
			break;
		default:
			RTEAbort("Invalid LayerType was set to draw in SLTerrain::Draw");
			break;
		}
}
