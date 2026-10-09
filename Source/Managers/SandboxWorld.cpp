// Changing the world: painting and building, effects, incoming fire, lightning, spawning units, items and structures, and the stroke queue that applies every tool use.

#include "SandboxInternal.h"

namespace SandboxDetail {
	void Detonate(const char* presetName, const Vector& position) {
		if (MovableObject* object = CreateBaseObject("TDExplosive", presetName)) {
			object->SetPos(position);
			MOSRotating* explosive = dynamic_cast<MOSRotating*>(object);
			AddObject(object);
			if (explosive) {
				explosive->GibThis();
			}
		}
	}

	void SpawnPuffs(const char* presetName, const Vector& position, int radius, int count) {
		for (int i = 0; i < count; ++i) {
			if (MovableObject* puff = CreateBaseObject("MOSParticle", presetName)) {
				puff->SetPos(position + Vector((Random01() - 0.5F) * 2.0F * static_cast<float>(radius), (Random01() - 0.5F) * 2.0F * static_cast<float>(radius)));
				puff->SetVel(Vector((Random01() - 0.5F) * 2.0F, (Random01() - 0.5F) * 2.0F));
				g_MovableMan.AddParticle(puff);
			}
		}
	}

	/// The foreground colour painted ground gets at a scene pixel: the material's own terrain texture, tiled across the scene the way generated terrain is, so painted ground matches the real thing. A material without a texture gets its flat colour with a darker speckle.
	int PaintedColor(const Material* material, int x, int y, int color, int speckleColor) {
		if (BITMAP* texture = material ? material->GetFGTexture() : nullptr; texture && texture->w > 0 && texture->h > 0 && bitmap_color_depth(texture) == 8) {
			int texel = _getpixel(texture, x % texture->w, y % texture->h);
			if (texel != ColorKeys::g_MaskColor) {
				return texel;
			}
		}
		return Random01() < 0.25F ? speckleColor : color;
	}


	/// Closes the open undo step once its stroke has paused (or always): what it saw is only needed while it records, and is most of what a
	/// big step holds. Called each sandbox update, so the last stroke's set doesn't stay until the next stroke begins.
	void ClosePaintUndoStep(bool always) {
		if (s_PaintUndo.empty() || s_PaintUndo.back().Seen.empty()) {
			return;
		}
		if (always || g_TimerMan.GetSimUpdateCount() - s_PaintUndo.back().LastUpdate > 15) {
			std::unordered_set<long long>().swap(s_PaintUndo.back().Seen);
			s_PaintUndo.back().Pixels.shrink_to_fit();
		}
	}

	/// Keeps a pixel as it is, before a paint or build stroke changes it, for the undo.
	void RecordPaintPixel(const SLTerrain* terrain, int x, int y) {
		if (!s_RecordPaint || x < 0 || y < 0 || x > 0xFFFF || y > 0xFFFF) {
			return;
		}
		long long update = g_TimerMan.GetSimUpdateCount();
		// A new step for a new stroke, or when this stroke's step is full.
		if (s_PaintUndo.empty() || update - s_PaintUndo.back().LastUpdate > 15 || s_PaintUndo.back().Pixels.size() >= c_PaintUndoPixelsPerStep) {
			ClosePaintUndoStep(true);
			s_PaintUndo.emplace_back();
			size_t pixelsKept = 0;
			for (const PaintUndoStep& kept: s_PaintUndo) {
				pixelsKept += kept.Pixels.size();
			}
			while (s_PaintUndo.size() > 1 && (s_PaintUndo.size() > c_PaintUndoSteps || pixelsKept > c_PaintUndoPixels)) {
				pixelsKept -= s_PaintUndo.front().Pixels.size();
				s_PaintUndo.pop_front();
			}
		}
		PaintUndoStep& step = s_PaintUndo.back();
		step.LastUpdate = update;
		if (!step.Seen.insert((static_cast<long long>(y) << 32) | static_cast<unsigned int>(x)).second) {
			return;
		}
		step.Pixels.push_back({static_cast<unsigned short>(x), static_cast<unsigned short>(y), static_cast<unsigned char>(terrain->GetMaterialPixel(x, y)), static_cast<unsigned char>(terrain->GetFGColorPixel(x, y))});
		step.Left = std::min(step.Left, x);
		step.Top = std::min(step.Top, y);
		step.Right = std::max(step.Right, x);
		step.Bottom = std::max(step.Bottom, y);
	}

	/// Puts back what the last paint or build stroke changed, as any change to the terrain is made: the pathfinder and the lighting told,
	/// hanging ground and liquid round it woken.
	void UndoPaint() {
		while (!s_PaintUndo.empty() && s_PaintUndo.back().Pixels.empty()) {
			s_PaintUndo.pop_back();
		}
		if (s_PaintUndo.empty() || !g_SceneMan.GetScene()) {
			return;
		}
		SLTerrain* terrain = g_SceneMan.GetScene()->GetTerrain();
		const PaintUndoStep& step = s_PaintUndo.back();
		Box area(Vector(static_cast<float>(step.Left), static_cast<float>(step.Top)), static_cast<float>(step.Right - step.Left + 1), static_cast<float>(step.Bottom - step.Top + 1));
		Vector center = area.GetCenter();
		float reach = static_cast<float>(std::max(area.GetWidth(), area.GetHeight())) * 0.75F;
		// (Undoing a paint takes ground away, which can leave what is over it hanging, as a dig does.)
		TerrainCollapse::BeginChange(center, reach + 30.0F);
		for (auto pixel = step.Pixels.rbegin(); pixel != step.Pixels.rend(); ++pixel) {
			terrain->SetMaterialPixel(pixel->X, pixel->Y, pixel->Material);
			terrain->SetFGColorPixel(pixel->X, pixel->Y, pixel->Color);
		}
		terrain->AddUpdatedMaterialArea(area);
		FluidSim::Disturb(center, reach + 2.0F);
		s_PaintUndo.pop_back();
	}


	void NotePaint(const Box& area, const char* kind, const char* material, bool toldCollapse, bool toldLiquid, bool changed) {
		if (!g_SettingsMan.ShowSandboxPaintAudit()) {
			s_PaintRecords.clear();
			return;
		}
		s_PaintRecords.push_back({area, kind, material ? material : "air", toldCollapse, toldLiquid, changed, g_TimerMan.GetSimUpdateCount()});
		while (s_PaintRecords.size() > 24) {
			s_PaintRecords.pop_front();
		}
	}

	/// Paints a disc of terrain material into the air, or digs one out when there's no material.
	void PaintTerrain(const Vector& center, int radius, const char* materialName) {
		SLTerrain* terrain = g_SceneMan.GetScene()->GetTerrain();
		int width = terrain->GetBitmap()->w;
		int height = terrain->GetBitmap()->h;
		int material = g_MaterialAir;
		const Material* paintMaterial = nullptr;
		int color = ColorKeys::g_MaskColor;
		int speckleColor = color;
		if (materialName) {
			const Material* found = g_SceneMan.GetMaterial(materialName);
			if (!found || found->GetIndex() == g_MaterialAir) {
				return;
			}
			material = found->GetIndex();
			paintMaterial = found;
			Color materialColor = found->GetColor();
			materialColor.RecalculateIndex();
			color = materialColor.GetIndex();
			Color darker = materialColor;
			darker.SetRGB(materialColor.GetR() * 4 / 5, materialColor.GetG() * 4 / 5, materialColor.GetB() * 4 / 5);
			darker.RecalculateIndex();
			speckleColor = darker.GetIndex() > 1 ? darker.GetIndex() : color;
		}
		int centerX = center.GetFloorIntX();
		int centerY = center.GetFloorIntY();
		if (!materialName) {
			// Dug-out ground may be left hanging. Told before the digging, so it knows what was hanging already.
			TerrainCollapse::BeginChange(center, static_cast<float>(radius + 30));
		}
		bool changed = false;
		for (int dy = -radius; dy <= radius; ++dy) {
			for (int dx = -radius; dx <= radius; ++dx) {
				if (dx * dx + dy * dy > radius * radius) {
					continue;
				}
				int x = centerX + dx;
				int y = centerY + dy;
				if (g_SceneMan.SceneWrapsX()) {
					x = ((x % width) + width) % width;
				}
				if (x < 0 || y < 0 || x >= width || y >= height) {
					continue;
				}
				int existing = terrain->GetMaterialPixel(x, y);
				// Painting only fills air; digging removes anything but the indestructible edge of the world.
				if (materialName ? existing != g_MaterialAir : (existing == g_MaterialAir || existing == g_MaterialOutOfBounds)) {
					continue;
				}
				RecordPaintPixel(terrain, x, y);
				terrain->SetMaterialPixel(x, y, material);
				terrain->SetFGColorPixel(x, y, materialName ? PaintedColor(paintMaterial, x, y, color, speckleColor) : color);
				changed = true;
			}
		}
		if (changed) {
			terrain->AddUpdatedMaterialArea(Box(Vector(static_cast<float>(centerX - radius), static_cast<float>(centerY - radius)), static_cast<float>(radius * 2 + 1), static_cast<float>(radius * 2 + 1)));
			// Liquid around the change may flow into it, and dug-out ground may be left hanging.
			FluidSim::Disturb(center, static_cast<float>(radius + 2));

		}
		NotePaint(Box(Vector(static_cast<float>(centerX - radius), static_cast<float>(centerY - radius)), static_cast<float>(radius * 2 + 1), static_cast<float>(radius * 2 + 1)), materialName ? "paint" : "dig", materialName, !materialName, changed, changed);
	}

	/// Fills a box with a terrain material, where there's air (or everything, to build over what's there).
	void PaintBox(const Vector& topLeft, int boxWidth, int boxHeight, const char* materialName) {
		SLTerrain* terrain = g_SceneMan.GetScene()->GetTerrain();
		int width = terrain->GetBitmap()->w;
		int height = terrain->GetBitmap()->h;
		const Material* found = g_SceneMan.GetMaterial(materialName);
		if (!found || found->GetIndex() == g_MaterialAir) {
			return;
		}
		Color materialColor = found->GetColor();
		materialColor.RecalculateIndex();
		int color = materialColor.GetIndex();
		Color darker = materialColor;
		darker.SetRGB(materialColor.GetR() * 4 / 5, materialColor.GetG() * 4 / 5, materialColor.GetB() * 4 / 5);
		darker.RecalculateIndex();
		int speckleColor = darker.GetIndex() > 1 ? darker.GetIndex() : color;
		int left = topLeft.GetFloorIntX();
		int top = topLeft.GetFloorIntY();
		for (int dy = 0; dy < boxHeight; ++dy) {
			for (int dx = 0; dx < boxWidth; ++dx) {
				int x = left + dx;
				int y = top + dy;
				if (g_SceneMan.SceneWrapsX()) {
					x = ((x % width) + width) % width;
				}
				if (x < 0 || y < 0 || x >= width || y >= height || terrain->GetMaterialPixel(x, y) != g_MaterialAir) {
					continue;
				}
				RecordPaintPixel(terrain, x, y);
				terrain->SetMaterialPixel(x, y, found->GetIndex());
				terrain->SetFGColorPixel(x, y, PaintedColor(found, x, y, color, speckleColor));
			}
		}
		terrain->AddUpdatedMaterialArea(Box(topLeft, static_cast<float>(boxWidth), static_cast<float>(boxHeight)));
		// Liquid round it takes the new shape (as PaintTerrain's): a box built into a stream was dry, the water left standing where it had been.
		FluidSim::Disturb(topLeft + Vector(static_cast<float>(boxWidth) * 0.5F, static_cast<float>(boxHeight) * 0.5F), static_cast<float>(std::max(boxWidth, boxHeight)) * 0.75F + 2.0F);
		NotePaint(Box(topLeft, static_cast<float>(boxWidth), static_cast<float>(boxHeight)), "fill box", materialName, false, true, true);
	}

	/// Whether what a tool makes belongs to a side, so the side is shown with it and the ring of sides is offered.
	bool TakesSide(Tool kind) {
		return kind == Tool::Unit || kind == Tool::Drop || kind == Tool::Brain || kind == Tool::RallyPoint || kind == Tool::Structure || kind == Tool::Barracks || kind == Tool::Extractor || kind == Tool::OrderMove;
	}

	/// Clears a box of the terrain to air.
	void ClearBox(const Vector& topLeft, int boxWidth, int boxHeight) {
		SLTerrain* terrain = g_SceneMan.GetScene()->GetTerrain();
		int width = terrain->GetBitmap()->w;
		int height = terrain->GetBitmap()->h;
		int left = topLeft.GetFloorIntX();
		int top = topLeft.GetFloorIntY();
		TerrainCollapse::BeginChange(topLeft + Vector(static_cast<float>(boxWidth) * 0.5F, static_cast<float>(boxHeight) * 0.5F), static_cast<float>(std::max(boxWidth, boxHeight)) * 0.75F + 30.0F);
		for (int dy = 0; dy < boxHeight; ++dy) {
			for (int dx = 0; dx < boxWidth; ++dx) {
				int x = left + dx;
				int y = top + dy;
				if (g_SceneMan.SceneWrapsX()) {
					x = ((x % width) + width) % width;
				}
				if (x < 0 || y < 0 || x >= width || y >= height) {
					continue;
				}
				int existing = terrain->GetMaterialPixel(x, y);
				if (existing == g_MaterialAir || existing == g_MaterialOutOfBounds) {
					continue;
				}
				RecordPaintPixel(terrain, x, y);
				terrain->SetMaterialPixel(x, y, g_MaterialAir);
				terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
			}
		}
		terrain->AddUpdatedMaterialArea(Box(topLeft, static_cast<float>(boxWidth), static_cast<float>(boxHeight)));
		// Liquid above or beside the cleared box flows into it (as PaintTerrain's dig does); it stayed put until something else woke it.
		FluidSim::Disturb(topLeft + Vector(static_cast<float>(boxWidth) * 0.5F, static_cast<float>(boxHeight) * 0.5F), static_cast<float>(std::max(boxWidth, boxHeight)) * 0.75F + 2.0F);
		NotePaint(Box(topLeft, static_cast<float>(boxWidth), static_cast<float>(boxHeight)), "clear box", nullptr, true, true, true);
	}



	/// Queues a change the window asks for, to be made in the next simulation update like a click on the world. (Made from the window
	/// directly, gym units appeared with no sim step and their timers started on the spot, and the effect and spring lists were cleared
	/// under the update that walks them.)
	void QueueSimChange(Tool kind, int count) {
		Stroke stroke;
		stroke.Kind = kind;
		stroke.Count = count;
		s_Queue.push_back(stroke);
	}

	glm::vec3 Hue(float turn) {
		turn -= std::floor(turn);
		return glm::vec3(255.0F) * glm::clamp(glm::abs(glm::fract(glm::vec3(turn) + glm::vec3(0.0F, 2.0F / 3.0F, 1.0F / 3.0F)) * 6.0F - 3.0F) - 1.0F, 0.0F, 1.0F);
	}

	/// Runs the effects that have been put down, once per sim update. They are lights registered afresh each update and visual particles, so removing one leaves nothing behind
	/// (but for smoke, gas and fire already let out, which are real).
	void UpdateEffects() {
		if (s_Effects.empty()) {
			return;
		}
		long long update = g_TimerMan.GetSimUpdateCount();
		float time = static_cast<float>(update) * g_TimerMan.GetDeltaTimeSecs();
		for (PlacedEffect& effect: s_Effects) {
			const Vector& at = effect.Position;
			float phase = time + effect.Seed * 20.0F;
			auto every = [&](int updates) { return (update + static_cast<long long>(effect.Seed * 997.0F)) % updates == 0; };
			switch (effect.Kind) {
				case EffectKind::NuclearGlow:
					g_PostProcessMan.RegisterLight(at, glm::vec3(80.0F, 255.0F, 60.0F), 320.0F, 2.2F + 0.7F * std::sin(phase * 1.7F), LightSource::Sandbox);
					g_PostProcessMan.RegisterLight(at, glm::vec3(170.0F, 255.0F, 130.0F), 70.0F, 3.0F, LightSource::Sandbox);
					g_PostProcessMan.RegisterShimmer(at, 90.0F, 0.7F);
					if (every(5)) {
						EffectsParticles::Emit("Embers", at + Vector((Random01() - 0.5F) * 90.0F, (Random01() - 0.5F) * 30.0F), Vector(0.0F, -1.0F), 1.0F, 1, 0x60FF40);
					}
					break;
				case EffectKind::StormCell:
					if (--effect.Wait <= 0) {
						effect.Flash = 0.6F + Random01() * 0.6F;
						effect.Wait = 15 + static_cast<int>(Random01() * 150.0F);
						if (Random01() < 0.3F) {
							StrikeLightning(at + Vector((Random01() - 0.5F) * 260.0F, 0.0F));
						}
					}
					effect.Flash *= 0.8F;
					g_PostProcessMan.RegisterLight(at + Vector(0.0F, -90.0F), glm::vec3(195.0F, 215.0F, 255.0F), 560.0F, 0.12F + effect.Flash * 7.0F, LightSource::Sandbox);
					break;
				case EffectKind::RedAlarm: {
					Vector direction(std::cos(phase * 4.0F), std::sin(phase * 4.0F));
					g_PostProcessMan.RegisterConeLight(at, direction, 26.0F, glm::vec3(255.0F, 28.0F, 18.0F), 280.0F, 3.2F, LightSource::Sandbox);
					g_PostProcessMan.RegisterConeLight(at, direction * -1.0F, 26.0F, glm::vec3(255.0F, 28.0F, 18.0F), 280.0F, 3.2F, LightSource::Sandbox);
					g_PostProcessMan.RegisterLight(at, glm::vec3(255.0F, 40.0F, 25.0F), 26.0F, 1.5F, LightSource::Sandbox);
					break;
				}
				case EffectKind::PoliceLights: {
					bool red = std::fmod(phase * 3.0F, 1.0F) < 0.5F;
					bool lit = std::fmod(phase * 12.0F, 1.0F) < 0.6F;
					if (lit) {
						g_PostProcessMan.RegisterLight(at + Vector(red ? -8.0F : 8.0F, 0.0F), red ? glm::vec3(255.0F, 25.0F, 20.0F) : glm::vec3(30.0F, 80.0F, 255.0F), 240.0F, 3.0F, LightSource::Sandbox);
					}
					break;
				}
				case EffectKind::BlueBeacon:
					g_PostProcessMan.RegisterLight(at, glm::vec3(40.0F, 120.0F, 255.0F), 220.0F, 0.2F + 3.0F * std::pow(std::max(std::sin(phase * 2.6F), 0.0F), 4.0F), LightSource::Sandbox);
					break;
				case EffectKind::Floodlight:
					g_PostProcessMan.RegisterConeLight(at, Vector(0.0F, 1.0F), 36.0F, glm::vec3(255.0F, 244.0F, 222.0F), 460.0F, 3.2F, LightSource::Sandbox);
					g_PostProcessMan.RegisterLight(at, glm::vec3(255.0F, 244.0F, 222.0F), 22.0F, 1.6F, LightSource::Sandbox);
					break;
				case EffectKind::Searchlight: {
					float angle = 1.5708F + 0.95F * std::sin(phase * 0.8F);
					g_PostProcessMan.RegisterConeLight(at, Vector(std::cos(angle), std::sin(angle)), 8.0F, glm::vec3(225.0F, 238.0F, 255.0F), 640.0F, 4.5F, LightSource::Sandbox);
					g_PostProcessMan.RegisterLight(at, glm::vec3(225.0F, 238.0F, 255.0F), 20.0F, 1.5F, LightSource::Sandbox);
					break;
				}
				case EffectKind::Strobe:
					if (std::fmod(phase * 9.0F, 1.0F) < 0.22F) {
						g_PostProcessMan.RegisterLight(at, glm::vec3(255.0F, 255.0F, 255.0F), 340.0F, 4.0F, LightSource::Sandbox);
					}
					break;
				case EffectKind::Disco:
					for (int beam = 0; beam < 3; ++beam) {
						float angle = phase * (1.3F + 0.4F * static_cast<float>(beam)) * (beam == 1 ? -1.0F : 1.0F) + static_cast<float>(beam) * 2.1F;
						g_PostProcessMan.RegisterConeLight(at, Vector(std::cos(angle), std::sin(angle)), 14.0F, Hue(phase * 0.25F + static_cast<float>(beam) / 3.0F), 320.0F, 3.4F, LightSource::Sandbox);
					}
					g_PostProcessMan.RegisterLight(at, Hue(phase * 0.5F), 30.0F, 1.6F, LightSource::Sandbox);
					break;
				case EffectKind::Campfire: {
					float flicker = 0.6F * std::sin(phase * 11.0F) + 0.4F * std::sin(phase * 23.0F + 1.3F);
					g_PostProcessMan.RegisterLight(at, glm::vec3(255.0F, 150.0F, 60.0F), 160.0F + 10.0F * flicker, 1.7F + 0.45F * flicker, LightSource::Sandbox);
					g_PostProcessMan.RegisterShimmer(at + Vector(0.0F, -14.0F), 24.0F, 0.5F);
					if (every(3)) {
						EffectsParticles::Emit("Embers", at + Vector((Random01() - 0.5F) * 10.0F, -2.0F), Vector(0.0F, -1.5F), 0.7F, 1, 0);
					}
					if (every(10)) {
						EffectsParticles::Emit("Sparks", at, Vector((Random01() - 0.5F) * 2.0F, -5.0F), 0.6F, 2, 0);
					}
					if (every(120)) {
						SpawnPuffs("Thick Smoke Ball", at + Vector(0.0F, -8.0F), 3, 1);
					}
					break;
				}
				case EffectKind::Candle:
					g_PostProcessMan.RegisterLight(at, glm::vec3(255.0F, 180.0F, 95.0F), 62.0F, 1.0F + 0.2F * std::sin(phase * 9.0F) + 0.1F * std::sin(phase * 31.0F), LightSource::Sandbox);
					break;
				case EffectKind::LavaGlow:
					g_PostProcessMan.RegisterLight(at, glm::vec3(255.0F, 85.0F, 18.0F), 240.0F, 1.7F + 0.35F * std::sin(phase * 0.9F), LightSource::Sandbox);
					g_PostProcessMan.RegisterShimmer(at + Vector(0.0F, -20.0F), 60.0F, 0.6F);
					if (every(8)) {
						EffectsParticles::Emit("Embers", at + Vector((Random01() - 0.5F) * 120.0F, 0.0F), Vector(0.0F, -1.0F), 1.0F, 1, 0);
					}
					break;
				case EffectKind::WeldingArc:
					if (Random01() < 0.6F) {
						g_PostProcessMan.RegisterLight(at, glm::vec3(170.0F, 200.0F, 255.0F), 150.0F, 2.5F + Random01() * 3.5F, LightSource::Sandbox);
						EffectsParticles::Emit("Sparks", at, Vector((Random01() - 0.5F) * 6.0F, -2.0F - Random01() * 4.0F), 1.0F, 2, 0xCFE4FF);
					}
					break;
				case EffectKind::Fireflies:
					for (int fly = 0; fly < 7; ++fly) {
						float own = static_cast<float>(fly) * 1.618F + effect.Seed * 6.0F;
						Vector where = at + Vector(std::sin(phase * (0.5F + 0.13F * static_cast<float>(fly)) + own) * 60.0F, std::cos(phase * (0.37F + 0.09F * static_cast<float>(fly)) + own * 2.0F) * 34.0F);
						float glow = std::pow(std::max(std::sin(phase * 2.3F + own * 3.0F), 0.0F), 2.0F);
						if (glow > 0.05F) {
							g_PostProcessMan.RegisterLight(where, glm::vec3(190.0F, 255.0F, 90.0F), 18.0F, 1.6F * glow, LightSource::Sandbox);
						}
					}
					break;
				case EffectKind::Portal:
					g_PostProcessMan.RegisterLight(at, glm::vec3(170.0F, 60.0F, 255.0F), 200.0F, 1.8F + 0.8F * std::sin(phase * 3.1F), LightSource::Sandbox);
					g_PostProcessMan.RegisterShimmer(at, 46.0F, 1.2F);
					if (every(2)) {
						float angle = Random01() * 6.2832F;
						EffectsParticles::Emit("Sparks", at + Vector(std::cos(angle), std::sin(angle)) * 30.0F, Vector(-std::cos(angle) * 3.0F, -std::sin(angle) * 3.0F), 0.2F, 1, 0xC070FF);
					}
					break;
				case EffectKind::SparkFountain:
					EffectsParticles::Emit("Sparks", at, Vector((Random01() - 0.5F) * 2.0F, -9.0F), 0.4F, 3, 0);
					g_PostProcessMan.RegisterLight(at + Vector(0.0F, -10.0F), glm::vec3(255.0F, 205.0F, 130.0F), 80.0F, 1.3F, LightSource::Sandbox);
					break;
				case EffectKind::EmberVent:
					if (every(2)) {
						EffectsParticles::Emit("Embers", at + Vector((Random01() - 0.5F) * 30.0F, 0.0F), Vector(0.0F, -2.0F), 0.8F, 1, 0);
					}
					break;
				case EffectKind::SmokeStack:
					if (every(14)) {
						SpawnPuffs("Thick Smoke Ball", at, 4, 1);
					}
					break;
				case EffectKind::SmokePlume:
					EffectsParticles::Emit("Smoke", at + Vector(std::sin(phase * 5.0F) * 12.0F, -std::fmod(phase * 16.0F, 30.0F)), Vector(std::cos(phase * 5.0F) * 3.0F, -2.5F), 0.4F, 1, 0);
					break;
				case EffectKind::ToxicVent:
					g_PostProcessMan.RegisterLight(at, glm::vec3(120.0F, 255.0F, 70.0F), 90.0F, 0.9F, LightSource::Sandbox);
					if (every(22)) {
						SpawnPuffs("Toxic Gas Ball", at, 4, 1);
					}
					break;
				case EffectKind::MistVent:
					EffectsParticles::Emit("Mist", at + Vector((Random01() - 0.5F) * 8.0F, 0.0F), Vector((Random01() - 0.5F) * 2.0F, -3.5F), 0.8F, 1, 0);
					break;
				case EffectKind::DustDevil:
					EffectsParticles::Emit("Dust", at + Vector(std::sin(phase * 6.0F) * 14.0F, -std::fmod(phase * 20.0F, 40.0F)), Vector(std::cos(phase * 6.0F) * 4.0F, -3.0F), 0.4F, 1, 0);
					break;
				case EffectKind::FireJet:
					g_PostProcessMan.RegisterLight(at + Vector(0.0F, -20.0F), glm::vec3(255.0F, 140.0F, 50.0F), 130.0F, 1.8F + 0.4F * std::sin(phase * 17.0F), LightSource::Sandbox);
					if (every(2)) {
						if (MovableObject* flame = CreateBaseObject("MOSParticle", "Flame Hurt Short")) {
							flame->SetPos(at);
							flame->SetVel(Vector((Random01() - 0.5F) * 2.0F, -7.0F - Random01() * 3.0F));
							g_MovableMan.AddParticle(flame);
						}
					}
					break;
				case EffectKind::HeatShimmer:
					g_PostProcessMan.RegisterShimmer(at, 80.0F, 1.3F);
					break;
				case EffectKind::ShockwavePulse:
					if (every(90)) {
						g_PostProcessMan.RegisterShockwave(at, 6000.0F);
					}
					break;
				default:
					break;
			}
		}
	}



	void Launch(int delay, const Vector& from, const Vector& target, float speed, const char* preset, int crater, const char* className, int team) {
		if (s_Incoming.size() < 200) {
			Incoming incoming;
			incoming.ClassName = className;
			incoming.Team = team;
			incoming.Delay = delay;
			incoming.From = from;
			incoming.Target = target;
			incoming.Speed = speed;
			incoming.Preset = preset;
			incoming.Crater = crater;
			incoming.LastPos = from;
			s_Incoming.push_back(incoming);
		}
	}

	void UpdateIncoming() {
		for (size_t i = 0; i < s_Incoming.size();) {
			Incoming& incoming = s_Incoming[i];
			if (incoming.Delay > 0) {
				--incoming.Delay;
				++i;
				continue;
			}
			Vector line = g_SceneMan.ShortestDistance(incoming.From, incoming.Target, g_SceneMan.SceneWrapsX());
			Vector direction = line.GetMagnitude() > 0.01F ? line / line.GetMagnitude() : Vector(0.0F, 1.0F);
			// Object speeds are in metres a second: 20 pixels to the metre, 60 updates a second.
			Vector velocity = direction * (incoming.Speed * 3.0F);
			if (incoming.Id == 0) {
				MovableObject* object = CreateBaseObject(incoming.ClassName.c_str(), incoming.Preset.c_str());
				if (!object) {
					s_Incoming.erase(s_Incoming.begin() + static_cast<std::ptrdiff_t>(i));
					continue;
				}
				object->SetPos(incoming.From);
				object->SetVel(velocity);
				if (Actor* craft = dynamic_cast<Actor*>(object)) {
					// A craft under its own AI, engines burning, that isn't going to make it.
					craft->SetTeam(incoming.Team);
					craft->SetControllerMode(Controller::CIM_AI);
				}
				if (incoming.ClassName != "ACDropShip") {
					object->SetRotAngle(incoming.ClassName == "ACRocket" ? direction.GetAbsRadAngle() + 1.5708F : direction.GetAbsRadAngle());
				}
				incoming.Id = object->GetUniqueID();
				AddObject(object);
				++i;
				continue;
			}
			MovableObject* object = g_MovableMan.FindObjectByUniqueID(incoming.Id);
			// Gone before it got there: the game blew it up on the way (it hit something) or it was removed. Its blast, if any, has been.
			const bool goneEarly = object == nullptr;
			bool arrived = goneEarly || --incoming.Life <= 0;
			Vector position = object ? object->GetPos() : incoming.LastPos;
			if (object) {
				incoming.LastPos = position;
				// Kept on its line, whatever gravity and the air would do to it.
				object->SetVel(velocity);
				if (incoming.ClassName == "ACDropShip") {
					// A dropship comes down level but out of control, rocking as it goes.
					object->SetRotAngle(0.35F * std::sin(static_cast<float>(incoming.Life) * 0.21F) + (direction.m_X > 0.0F ? -0.25F : 0.25F));
				} else {
					object->SetRotAngle(incoming.ClassName == "ACRocket" ? direction.GetAbsRadAngle() + 1.5708F : direction.GetAbsRadAngle());
				}
				EffectsParticles::Emit("Sparks", position - direction * 6.0F, Vector(-velocity.m_X * 0.15F, -velocity.m_Y * 0.15F), 0.5F, 2, 0);
				EffectsParticles::Emit("Dust", position - direction * 8.0F, Vector(0.0F, -0.5F), 1.0F, 1, 0x8C8C8C);
				Vector left = g_SceneMan.ShortestDistance(position, incoming.Target, g_SceneMan.SceneWrapsX());
				arrived = arrived || left.GetMagnitude() < incoming.Speed * 1.5F || left.Dot(direction) < 0.0F;
				// Or it has run into the ground, or a building, on the way.
				for (float ahead = 0.0F; ahead <= incoming.Speed && !arrived; ahead += 3.0F) {
					Vector probe = position + direction * ahead;
					arrived = probe.m_Y > 0.0F && g_SceneMan.GetTerrMatter(probe.GetFloorIntX(), probe.GetFloorIntY()) != g_MaterialAir;
				}
			}
			if (!arrived) {
				++i;
				continue;
			}
			if (incoming.Crater > 0) {
				PaintTerrain(position, incoming.Crater, nullptr);
			}
			if (MOSRotating* explosive = dynamic_cast<MOSRotating*>(object)) {
				explosive->GibThis();
			} else if (incoming.ClassName == "TDExplosive" && !goneEarly) {
				// (Not again for one the game already set off: that was a second blast where the first had been.)
				Detonate(incoming.Preset.c_str(), position);
			}
			if (incoming.ClassName != "TDExplosive") {
				// A craft full of fuel hitting the ground goes up harder than its wreckage alone.
				Detonate("Standard Bomb", position);
				Detonate("Napalm Bomb", position + Vector(0.0F, -6.0F));
			}
			s_Incoming.erase(s_Incoming.begin() + static_cast<std::ptrdiff_t>(i));
		}
	}

	/// A lightning strike: a jagged bolt from the sky to the first thing below the point, a flash, fire and harm where it lands, and thunder.
	void StrikeLightning(const Vector& target) {
		WeatherLightning::Strike(target, Random01, true);
	}

	void GiveLoadout(Actor* actor, const Preset& unit, int loadout) {
		if (loadout == 1) {
			return;
		}
		if (loadout >= 2) {
			if (size_t weapon = static_cast<size_t>(loadout - 2); weapon < s_Weapons.size()) {
				if (MovableObject* gun = CreateObject(s_Weapons[weapon]->ClassName, s_Weapons[weapon]->PresetName, s_Weapons[weapon]->ModuleID)) {
					actor->AddInventoryItem(gun);
				}
			}
			return;
		}
		// The faction's own kit, or the Coalition's for factions without guns of their own.
		const FactionArmoury* armoury = &ArmouryOf(unit.ModuleID);
		if (armoury->Primaries.empty()) {
			armoury = &ArmouryOf(g_PresetMan.GetModuleID("Coalition.rte"));
		}
		auto pick = [](const std::vector<const Preset*>& from) { return from.empty() ? nullptr : from[std::min(from.size() - 1, static_cast<size_t>(Random01() * static_cast<float>(from.size())))]; };
		for (const Preset* item: {pick(armoury->Primaries), pick(armoury->Secondaries), Random01() < 0.5F ? pick(armoury->Grenades) : nullptr}) {
			if (item) {
				if (MovableObject* object = CreateObject(item->ClassName, item->PresetName, item->ModuleID)) {
					actor->AddInventoryItem(object);
				}
			}
		}
	}


	/// Makes a unit ready to go: armed, on a side, run by the AI, with orders to follow once it's in the world.
	Actor* CreateUnit(const Preset& preset, int team, int loadout, Order order) {
		Actor* actor = dynamic_cast<Actor*>(CreateObject(preset.ClassName, preset.PresetName, preset.ModuleID));
		if (!actor) {
			return nullptr;
		}
		GiveLoadout(actor, preset, loadout);
		actor->SetTeam(team);
		actor->SetControllerMode(Controller::CIM_AI);
		switch (order) {
			case Order::Attack:
				// Gets its target once it's out among the enemy.
				actor->SetOrderAttack(true);
				actor->SetAIMode(Actor::AIMODE_SENTRY);
				break;
			case Order::HuntBrains:
				actor->SetAIMode(Actor::AIMODE_BRAINHUNT);
				break;
			case Order::Patrol:
				actor->SetAIMode(Actor::AIMODE_PATROL);
				break;
			case Order::Rally:
				if (team >= 0 && team < c_Sides && s_RallySet[team]) {
					actor->AddAISceneWaypoint(s_RallyPoints[team]);
					actor->SetAIMode(Actor::AIMODE_GOTO);
				}
				break;
			case Order::Idle:
				actor->SetAIMode(Actor::AIMODE_NONE);
				break;
			case Order::DigGold:
				actor->ClearAIWaypoints();
				actor->SetAIMode(Actor::AIMODE_GOLDDIG);
				break;
			default:
				actor->SetAIMode(Actor::AIMODE_SENTRY);
				break;
		}
		return actor;
	}

	/// Sends units in by dropship or rocket, which comes down from the sky over a point, unloads and leaves. Returns what the units cost.
	/// @param invincible Whether the craft takes no harm, and is taken away once it has unloaded and left (KeepCraftWhole).
	float DropUnits(std::vector<Actor*>& units, int team, float x, int craft, bool invincible) {
		const CraftChoice& choice = c_Crafts[std::clamp(craft, 0, static_cast<int>(std::size(c_Crafts)) - 1)];
		ACraft* ship = dynamic_cast<ACraft*>(CreateBaseObject(choice.ClassName, choice.PresetName));
		float cost = 0.0F;
		if (!ship) {
			for (Actor* unit: units) {
				delete unit;
			}
			units.clear();
			return cost;
		}
		ActivateSide(team);
		for (Actor* unit: units) {
			cost += unit->GetTotalValue(unit->GetModuleID(), 1.0F);
			ship->AddInventoryItem(unit);
		}
		units.clear();
		bool fromBelow = g_SceneMan.GetTerrain() && g_SceneMan.GetTerrain()->GetOrbitDirection() == Directions::Down;
		ship->SetPos(Vector(x, fromBelow ? static_cast<float>(g_SceneMan.GetSceneHeight()) : 0.0F));
		ship->SetTeam(team);
		ship->SetControllerMode(Controller::CIM_AI);
		ship->SetAIMode(Actor::AIMODE_DELIVER);
		ship->ResetAllTimers();
		if (invincible) {
			KeepCraftWhole(ship);
		}
		g_MovableMan.AddActor(ship);
		return cost;
	}

	void SpawnUnits(const Stroke& stroke, bool brain) {
		const Preset* preset = ChosenPreset(brain ? Tool::Brain : Tool::Unit, stroke.Choice);
		if (!preset) {
			return;
		}
		ActivateSide(stroke.Team);
		int count = brain ? 1 : stroke.Count;
		for (int i = 0; i < count; ++i) {
			Actor* actor = dynamic_cast<Actor*>(CreateObject(preset->ClassName, preset->PresetName, preset->ModuleID));
			if (!actor) {
				return;
			}
			if (!brain) {
				GiveLoadout(actor, *preset, stroke.Loadout);
			}
			// A squad spreads out sideways from the click.
			float spread = (static_cast<float>(i) - static_cast<float>(count - 1) * 0.5F) * 16.0F;
			actor->SetPos(stroke.Position + Vector(spread, 0.0F));
			actor->SetTeam(stroke.Team);
			actor->SetControllerMode(Controller::CIM_AI);
			// (Facing the middle of the view as it was at the click: read from the camera here, in the sim, a replay faced them by
			// wherever the view happened to be.)
			actor->SetHFlipped(stroke.HasView && g_SceneMan.ShortestDistance(stroke.Position, Vector(stroke.ViewMiddleX, stroke.Position.m_Y), g_SceneMan.SceneWrapsX()).m_X < 0.0F);
			g_MovableMan.AddActor(actor);
			GiveOrder(actor, brain ? Order::Hold : stroke.Orders);
		}
	}


	/// The units random picks are made from: every faction's (turrets aside, as for FactionUnits), or only those marked as favourites in
	/// the unit or drop lists. With no favourite units marked, every faction's, rather than nothing at all.
	std::vector<const Preset*> RandomUnitPool(bool favouritesOnly) {
		std::vector<const Preset*> all;
		std::vector<const Preset*> favourites;
		for (const Preset& unit: s_Units) {
			const Entity* entity = g_PresetMan.GetEntityPreset(unit.ClassName, unit.PresetName, unit.ModuleID);
			if (!entity || entity->IsInGroup("Actors - Turrets")) {
				continue;
			}
			all.push_back(&unit);
			if (favouritesOnly && (FindFavourite(Tool::Unit, unit.PresetName) >= 0 || FindFavourite(Tool::Drop, unit.PresetName) >= 0)) {
				favourites.push_back(&unit);
			}
		}
		return favourites.empty() ? all : favourites;
	}

	const Preset* RandomPick(const std::vector<const Preset*>& pool) {
		return pool.empty() ? nullptr : pool[std::min(pool.size() - 1, static_cast<size_t>(Random01() * static_cast<float>(pool.size())))];
	}

	void DropSquad(const Stroke& stroke) {
		// Random: each unit picked on its own from every faction's units, or from the favourites.
		std::vector<const Preset*> pool = stroke.Random ? RandomUnitPool(stroke.FavouritesOnly) : std::vector<const Preset*>();
		if (stroke.JetpackOnly) {
			DropJetless(pool);
		}
		const Preset* preset = stroke.Random ? nullptr : ChosenPreset(Tool::Unit, stroke.Choice);
		if (stroke.Random ? pool.empty() : !preset) {
			return;
		}
		std::vector<Actor*> units;
		for (int i = 0; i < stroke.Count; ++i) {
			const Preset* pick = stroke.Random ? RandomPick(pool) : preset;
			if (Actor* unit = pick ? CreateUnit(*pick, stroke.Team, stroke.Loadout, stroke.Orders) : nullptr) {
				units.push_back(unit);
			}
		}
		DropUnits(units, stroke.Team, stroke.Position.m_X, stroke.Craft);
	}

	void SpawnItem(const Stroke& stroke) {
		const Preset* preset = ChosenPreset(Tool::Item, stroke.Choice);
		MovableObject* item = preset ? CreateObject(preset->ClassName, preset->PresetName, preset->ModuleID) : nullptr;
		if (!item) {
			return;
		}
		item->SetPos(stroke.Position);
		if (stroke.LitGrenade) {
			if (TDExplosive* explosive = dynamic_cast<TDExplosive*>(item)) {
				explosive->Activate();
			}
		}
		AddObject(item);
	}

	Vector StructureCorner(const Preset& preset, const Vector& center, bool snap) {
		Vector topLeft = center - Vector(static_cast<float>(preset.Width) * 0.5F, static_cast<float>(preset.Height) * 0.5F);
		if (snap) {
			topLeft.SetXY(std::round(topLeft.m_X / 24.0F) * 24.0F, std::round(topLeft.m_Y / 24.0F) * 24.0F);
		}
		return topLeft;
	}

	/// Where a bunker piece's position goes for a click at a place: pieces of terrain are centred on it (and can snap to the bunker grid), doors and the like sit on it.
	Vector StructurePosition(const Preset& preset, const Vector& click, bool snap) {
		if (preset.Width > 0) {
			return StructureCorner(preset, click, snap) - Vector(preset.OffsetX, preset.OffsetY);
		}
		return snap ? Vector(std::round(click.m_X / 12.0F) * 12.0F, std::round(click.m_Y / 12.0F) * 12.0F) : click;
	}

	void PlaceStructure(const Stroke& stroke) {
		const Preset* preset = ChosenPreset(Tool::Structure, stroke.Choice);
		const Entity* entity = preset ? g_PresetMan.GetEntityPreset(preset->ClassName, preset->PresetName, preset->ModuleID) : nullptr;
		SceneObject* object = entity ? dynamic_cast<SceneObject*>(entity->Clone()) : nullptr;
		if (!object) {
			return;
		}
		// The same place the preview showed it.
		object->SetPos(StructurePosition(*preset, stroke.Position, stroke.Count > 0));
		if (!dynamic_cast<TerrainObject*>(object)) {
			// Doors and other moving bunker parts belong to a side, and open for it.
			object->SetTeam(stroke.Team);
			ActivateSide(stroke.Team);
			// Marked as placed, so the game mode's start-up (Sandbox.lua) leaves its side alone when a saved game is loaded.
			if (Actor* placedActor = dynamic_cast<Actor*>(object)) {
				placedActor->SetNumberValue("SandboxPlaced", 1.0);
			}
		}
		g_SceneMan.AddSceneObject(object);
	}


	void Apply(const Stroke& stroke) {
		LogStroke(stroke);
		const Vector& at = stroke.Position;
		float radius = static_cast<float>(stroke.Radius);
		// The terrain brushes and the things to knock down are kept for the undo while they are applied (see RecordPaintPixel).
		struct RecordingPaint {
			explicit RecordingPaint(Tool kind) {
				switch (kind) {
					case Tool::Dig:
					case Tool::Earth:
					case Tool::Sand:
					case Tool::Ice:
					case Tool::Grass:
					case Tool::Wood:
					case Tool::Concrete:
					case Tool::BuildBeam:
					case Tool::BuildPillar:
					case Tool::BuildRoom:
					case Tool::BuildTower:
					case Tool::BuildBridge:
					case Tool::BuildIsland:
					case Tool::BuildTank:
						s_RecordPaint = true;
						break;
					default:
						s_RecordPaint = false;
						break;
				}
			}
			~RecordingPaint() { s_RecordPaint = false; }
		} recordingPaint(stroke.Kind);
		switch (stroke.Kind) {
			case Tool::Possess:
				TakeControl(at);
				break;
			case Tool::Release:
				ReleaseControl();
				break;
			case Tool::PlayCharacter:
				EnterPlayer(stroke.Count > 0, at);
				break;
			case Tool::Barracks:
				if (const Preset* unit = ChosenPreset(Tool::Unit, stroke.Choice)) {
					ActivateSide(stroke.Team);
					// ("Move to a place" has no place for trainees: they hold where they come out instead.)
					Colony::Place(Colony::Kind::Barracks, at, stroke.Team, unit->PresetName, static_cast<int>(UnitOrder(static_cast<int>(stroke.Orders))), stroke.Count);
				}
				break;
			case Tool::Extractor:
				ActivateSide(stroke.Team);
				Colony::Place(Colony::Kind::Extractor, at, stroke.Team, "", 0, 1);
				break;
			case Tool::PlayerRemake:
				if (Actor* old = GetRef(s_PlayerUnit)) {
					Vector place = old->GetPos();
					StopFlying();
					old->SetToDelete(true);
					s_PlayerUnit = UnitRef();
					MakePlayer(place);
				}
				break;
			case Tool::PlayerRemove:
				if (Actor* old = GetRef(s_PlayerUnit)) {
					StopFlying();
					old->SetToDelete(true);
				}
				s_PlayerUnit = UnitRef();
				s_PlayerEnterPending = 0;
				break;
			case Tool::Remove:
				if (MovableObject* object = ObjectUnder(at, false)) {
					// (Not your character while it can't be hurt: it vanished, and the god view came back with nothing said.)
					if (s_Player.Unkillable && object->GetRootParent() == GetRef(s_PlayerUnit)) {
						g_ConsoleMan.PrintString("SANDBOX: Your character can't be removed while it can't be hurt; turn that off first, or use Remove it on the You tab.");
						break;
					}
					object->SetToDelete(true);
				}
				break;
			case Tool::RallyPoint:
				if (stroke.Team >= 0 && stroke.Team < c_Sides) {
					s_RallyPoints[stroke.Team] = at;
					s_RallySet[stroke.Team] = true;
				}
				break;
			case Tool::Unit:
				SpawnUnits(stroke, false);
				break;
			case Tool::Brain:
				SpawnUnits(stroke, true);
				break;
			case Tool::Item:
				SpawnItem(stroke);
				break;
			case Tool::Drop:
				DropSquad(stroke);
				break;
			case Tool::Select:
				SelectInBox(stroke.Position, stroke.Position2);
				break;
			case Tool::Command:
				if (stroke.Count == 10 || stroke.Count == 11) {
					// Defend at (RC-4): the point, the way dragged to face, and 11 with Shift.
					DefendAtSelected(at, stroke.Position2, stroke.Count == 11);
				} else if (stroke.Count == 20 || stroke.Count == 21) {
					// A patrol route (RC-4): 20 a loop, 21 back and forth.
					PatrolSelected(stroke.Points, stroke.Count == 21);
				} else if (stroke.Count == 41 || stroke.Count == 42) {
					// A right click on the map (RC-8): the mode's order at the place, 42 with Shift.
					MapOrder(at, stroke.Count == 42);
				} else if (stroke.Count == 40) {
					// A "no route" marker clicked (RC-7): its units sent there again.
					ReissueNoRoute(stroke.Position);
				} else if (stroke.Count == 30 || stroke.Count == 31) {
					// A move or attack-move facing the way dragged (RC-5), 31 with Shift.
					FacingMoveSelected(at, stroke.Position2, stroke.Count == 31);
				} else {
					CommandSelected(at, stroke.Count);
				}
				break;
			case Tool::OrderSelected:
				if (stroke.Count == 400) {
					// A step dropped from a unit's plan (RC-3).
					DropPlanStep(stroke.UnitID, stroke.Choice);
					break;
				}
				if (stroke.Count >= 200) {
					// From the ring or the command row: an engagement rule for the selected units (RC-1), 200 + a weapons rule, 300 + a movement rule.
					for (const UnitRef& ref: s_Selected) {
						if (Actor* unit = GetRef(ref)) {
							if (stroke.Count >= 300) {
								unit->SetMovementRule(stroke.Count - 300);
								const char* const answers[] = {nullptr, "RuleEngage", "RuleMoveOnly", "RuleHoldGround"};
								int rule = stroke.Count - 300;
								AnswerOrder(unit, rule >= 0 && rule < static_cast<int>(std::size(answers)) ? answers[rule] : nullptr);
							} else {
								unit->SetWeaponRule(stroke.Count - 200);
								const char* const answers[] = {"RuleFireAtWill", "RuleReturnFire", "RuleHoldFire"};
								int rule = stroke.Count - 200;
								AnswerOrder(unit, rule >= 0 && rule < static_cast<int>(std::size(answers)) ? answers[rule] : nullptr);
							}
						}
					}
					break;
				}
				if (stroke.Count >= 100) {
					// From the command ring: move, attack or hold, about a point.
					OrderSelectedUnits(stroke.Count - 100, at);
					break;
				}
				for (const UnitRef& ref: s_Selected) {
					if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled()) {
						GiveOrder(unit, stroke.Orders);
						AnswerOrder(unit, OrderTrigger(stroke.Orders));
					}
				}
				break;
			case Tool::Follow:
				s_FollowTarget = MakeRef(dynamic_cast<Actor*>(ObjectUnder(at, true)));
				s_FollowAction = false;
				break;
			case Tool::Structure:
				PlaceStructure(stroke);
				break;
			case Tool::OrderSide:
				if (stroke.Orders == Order::MoveTo) {
					break;
				}
				// Not your character (an AI unit only while you're out of it: told to attack with the rest, it ran off to fight), nor craft
				// (a dropship delivering was sent off with its squad still in it, and tagged to attack, yanked about every second after).
				for (Actor* actor: SandboxAccess::Actors()) {
					if (actor->GetTeam() == stroke.Team && IsCombatant(actor) && !actor->IsPlayerControlled() && actor != GetRef(s_PlayerUnit) && !dynamic_cast<const ACraft*>(actor)) {
						GiveOrder(actor, stroke.Orders);
					}
				}
				break;
			case Tool::OrderMove:
				MoveUnitsTo(UnitsToMove(stroke.Team, false), at);
				break;
			case Tool::GymStart:
				s_GymFrom = at;
				s_GymFromSet = true;
				break;
			case Tool::GymGoal:
				s_GymTo = at;
				s_GymToSet = true;
				break;
			case Tool::GymRun:
				if (stroke.Count < 0) {
					GymRunAll();
				} else {
					GymRunCourse(stroke.Count);
				}
				break;
			case Tool::GymRemove:
				GymRemoveUnits();
				break;
			case Tool::ClearWaterSpawners:
				// All of them, or only those that pour the material named.
				if (stroke.Material.empty()) {
					s_WaterSpawners.clear();
				} else {
					s_WaterSpawners.erase(std::remove_if(s_WaterSpawners.begin(), s_WaterSpawners.end(), [&stroke](const WaterSpawner& spring) { return spring.Liquid == stroke.Material; }), s_WaterSpawners.end());
				}
				break;
			case Tool::UndoTerrain:
				UndoPaint();
				break;
			case Tool::BattleTeam:
			case Tool::BattleDefendPoint:
			case Tool::BattleDropLine:
			case Tool::BattleSpawnZone:
			case Tool::BattleModePoint:
			case Tool::BattleModeBase:
			case Tool::BattleModeZone:
				ApplyBattleStroke(stroke);
				break;
			case Tool::ClearEffects:
				if (stroke.Count == 1) {
					if (!s_Effects.empty()) {
						s_Effects.pop_back();
					}
				} else {
					s_Effects.clear();
				}
				break;
			case Tool::RemoveSide:
				// (Not your character: it's yours, not the side's.)
				for (Actor* actor: SandboxAccess::Actors()) {
					if (actor->GetTeam() == stroke.Team && !dynamic_cast<ADoor*>(actor) && actor != GetRef(s_PlayerUnit)) {
						actor->SetToDelete(true);
					}
				}
				break;
			case Tool::Fire:
				TerrainFire::QueueIgniteArea(at, radius);
				// Something to see even over rock, which doesn't burn. (Only to see: a stroke of the brush is a dozen of these a second, and as
				// they were they hit and hurt units the fire wasn't painted on. What burns is the fire itself.)
				if (MovableObject* flame = CreateBaseObject("MOSParticle", "Flame Hurt Short")) {
					flame->SetToHitMOs(false);
					flame->SetPos(at + Vector((Random01() - 0.5F) * radius, (Random01() - 0.5F) * radius));
					flame->SetVel(Vector((Random01() - 0.5F) * 1.5F, -1.0F - Random01()));
					g_MovableMan.AddParticle(flame);
				}
				break;
			case Tool::Water:
				FluidSim::Pour(at, radius * 0.5F, "Water");
				break;
			case Tool::Lava:
				FluidSim::Pour(at, radius * 0.5F, "Lava");
				break;
			case Tool::Acid:
				FluidSim::Pour(at, radius * 0.5F, "Acid");
				break;
			case Tool::Oil:
				FluidSim::Pour(at, radius * 0.5F, "Oil");
				break;
			case Tool::Mud:
				FluidSim::Pour(at, radius * 0.5F, "Mud");
				break;
			case Tool::Tar:
				FluidSim::Pour(at, radius * 0.5F, "Tar");
				break;
			case Tool::Mercury:
				FluidSim::Pour(at, radius * 0.5F, "Mercury");
				break;
			case Tool::Gravel:
				FluidSim::Pour(at, radius * 0.5F, "Gravel");
				break;
			case Tool::GlassShards:
				FluidSim::Pour(at, radius * 0.5F, "Glass Shards");
				break;
			case Tool::Fuel:
				FluidSim::Pour(at, radius * 0.5F, "Fuel");
				break;
			case Tool::Cryo:
				FluidSim::Pour(at, radius * 0.5F, "Cryogenic Fluid");
				break;
			case Tool::Blood:
				// Blood only flows with the setting on (it stays where it fell otherwise), so the brush turns it on.
				if (!FluidSim::BloodFlows()) {
					FluidSim::SetBloodFlows(true);
				}
				FluidSim::Pour(at, radius * 0.5F, "Blood");
				break;
			case Tool::PourOther:
				if (!stroke.Material.empty()) {
					FluidSim::Pour(at, radius * 0.5F, stroke.Material.c_str());
				}
				break;
			case Tool::WaterSpawner:
				if (s_WaterSpawners.size() < 64) {
					WaterSpawner spring;
					spring.Position = at;
					spring.Radius = std::max(1, stroke.Radius / 2);
					spring.Liquid = stroke.Material.empty() ? "Water" : stroke.Material;
					spring.Rate = std::clamp(stroke.Rate, 0.05F, 1.0F);
					s_WaterSpawners.push_back(spring);
				}
				break;
			case Tool::LooseSand:
				FluidSim::Pour(at, radius * 0.5F, "Sand");
				break;
			case Tool::LooseSnow:
				FluidSim::Pour(at, radius * 0.5F, "Snow");
				break;
			case Tool::Boulder:
				TerrainCollapse::SpawnChunk(at, radius * 1.5F + 4.0F, "Stone");
				break;
			case Tool::Slab:
				TerrainCollapse::SpawnChunk(at, radius * 1.5F + 4.0F, "Concrete");
				break;
			case Tool::Smoke:
				SpawnPuffs("Thick Smoke Ball", at, stroke.Radius, 2);
				break;
			case Tool::ToxicGas:
				SpawnPuffs("Toxic Gas Ball", at, stroke.Radius, 2);
				// The invisible cloud that does the harm, now and then so painting doesn't stack hundreds of them.
				if (Random01() < 0.15F) {
					SpawnPuffs("Toxic Gas Cloud", at, 0, 1);
				}
				break;
			case Tool::Dig:
				PaintTerrain(at, stroke.Radius, nullptr);
				break;
			case Tool::Earth:
				PaintTerrain(at, stroke.Radius, "Earth");
				break;
			case Tool::Sand:
				PaintTerrain(at, stroke.Radius, "Sand");
				break;
			case Tool::Ice:
				PaintTerrain(at, stroke.Radius, "Ice");
				break;
			case Tool::Grass:
				PaintTerrain(at, stroke.Radius, "Grass");
				break;
			case Tool::Wood:
				PaintTerrain(at, stroke.Radius, "Wood");
				break;
			case Tool::Concrete:
				PaintTerrain(at, stroke.Radius, "Concrete");
				break;
			case Tool::Grenade:
				Detonate("Frag Grenade", at);
				break;
			case Tool::RocketStrike:
				// One heavy rocket out of the sky, from one side or the other, into the point marked.
				Launch(0, at + Vector(Random01() < 0.5F ? -320.0F : 320.0F, -560.0F), at, 10.0F, "Standard Bomb", 26);
				break;
			case Tool::RocketBarrage:
				for (int i = 0; i < 8; ++i) {
					Vector target = at + Vector((Random01() - 0.5F) * 160.0F, (Random01() - 0.5F) * 40.0F);
					Launch(i * 9, target + Vector(-380.0F + Random01() * 120.0F, -560.0F), target, 11.0F, i % 3 == 0 ? "Standard Bomb" : "Frag Grenade", 14);
				}
				break;
			case Tool::CarpetBomb:
				// A stick of bombs dropped in a line across the point, one after another.
				for (int i = 0; i < 10; ++i) {
					Vector target = at + Vector(-225.0F + 50.0F * static_cast<float>(i), 0.0F);
					Launch(i * 7, target + Vector(-60.0F, -520.0F), target + Vector(0.0F, 400.0F), 8.0F, "Standard Bomb", 10);
				}
				break;
			case Tool::Artillery:
				// Shells lobbed in from far off to one side, landing around the point.
				for (int i = 0; i < 5; ++i) {
					Vector target = at + Vector((Random01() - 0.5F) * 90.0F, 0.0F);
					Launch(i * 28, target + Vector(-760.0F, -430.0F), target + Vector(120.0F, 68.0F), 13.0F, "Standard Bomb", 18);
				}
				break;
			case Tool::NapalmRain:
				for (int i = 0; i < 7; ++i) {
					Vector target = at + Vector((Random01() - 0.5F) * 260.0F, 0.0F);
					Launch(i * 10, target + Vector(0.0F, -520.0F), target + Vector(0.0F, 400.0F), 7.0F, "Napalm Bomb", 0);
				}
				break;
			case Tool::OrbitalBeam: {
				// A beam straight down from the sky: it bores a shaft through whatever is under the point, a long way down, and sets fire to what will burn.
				StrikeLightning(at);
				int depth = 0;
				for (float y = 0.0F; y < static_cast<float>(g_SceneMan.GetSceneHeight()) && depth < 420; y += 6.0F) {
					Vector point(at.m_X, y);
					bool ground = g_SceneMan.GetTerrMatter(point.GetFloorIntX(), point.GetFloorIntY()) != g_MaterialAir;
					if (!ground && depth == 0) {
						continue;
					}
					depth += 6;
					PaintTerrain(point, 6, nullptr);
					if (depth % 72 == 6) {
						Detonate("Frag Grenade", point);
						TerrainFire::QueueIgniteArea(point, 14.0F);
					}
				}
				break;
			}
			case Tool::Effect:
				if (s_Effects.size() < 120) {
					s_Effects.push_back({static_cast<EffectKind>(std::clamp(stroke.Choice, 0, static_cast<int>(EffectKind::Count) - 1)), at, Random01(), 0.0F, 0});
				}
				break;
			case Tool::CrashRocket:
				Launch(0, at + Vector(Random01() < 0.5F ? -260.0F : 260.0F, -620.0F), at, 7.5F, "Rocket MK2", 24, "ACRocket", stroke.Team);
				break;
			case Tool::CrashDropship:
				Launch(0, at + Vector(Random01() < 0.5F ? -620.0F : 620.0F, -420.0F), at, 6.5F, "Dropship MK1", 30, "ACDropShip", stroke.Team);
				break;
			case Tool::BoulderRain:
				for (int i = 0; i < 8; ++i) {
					TerrainCollapse::SpawnChunk(at + Vector((Random01() - 0.5F) * 300.0F, -260.0F - Random01() * 220.0F), 8.0F + Random01() * 16.0F, "Stone");
				}
				break;
			case Tool::BuildBeam:
				PaintBox(at + Vector(-80.0F, -5.0F), 160, 10, "Concrete");
				break;
			case Tool::BuildPillar:
				PaintBox(at + Vector(-6.0F, -70.0F), 12, 140, "Concrete");
				break;
			case Tool::BuildRoom:
				// Four walls with a doorway in each side.
				PaintBox(at + Vector(-70.0F, -45.0F), 140, 8, "Concrete");
				PaintBox(at + Vector(-70.0F, 37.0F), 140, 8, "Concrete");
				PaintBox(at + Vector(-70.0F, -45.0F), 8, 52, "Concrete");
				PaintBox(at + Vector(62.0F, -45.0F), 8, 52, "Concrete");
				break;
			case Tool::BuildTower:
				// Four storeys of concrete floors and walls, standing on the point marked: something tall to bring down.
				for (int floor = 0; floor < 4; ++floor) {
					float top = -72.0F * static_cast<float>(floor + 1);
					PaintBox(at + Vector(-50.0F, top), 100, 8, "Concrete");
					PaintBox(at + Vector(-50.0F, top), 8, floor % 2 == 0 ? 72 : 44, "Concrete");
					PaintBox(at + Vector(42.0F, top), 8, floor % 2 == 0 ? 44 : 72, "Concrete");
				}
				PaintBox(at + Vector(-50.0F, -8.0F), 100, 8, "Concrete");
				break;
			case Tool::BuildIsland: {
				// A lump of earth and rock hanging in the air, to chip at and cut up.
				static constexpr float lumps[7][3] = {{-42.0F, 0.0F, 20.0F}, {-14.0F, 4.0F, 24.0F}, {16.0F, 2.0F, 24.0F}, {44.0F, -2.0F, 18.0F}, {-4.0F, 22.0F, 16.0F}, {22.0F, 20.0F, 12.0F}, {-26.0F, -14.0F, 12.0F}};
				for (const auto& lump: lumps) {
					PaintTerrain(at + Vector(lump[0], lump[1]), static_cast<int>(lump[2]), lump[2] > 17.0F ? "Earth" : "Stone");
				}
				break;
			}
			case Tool::BuildTank:
				// An open concrete tank, filled with water, or what the springs pour (Paint > Springs).
				PaintBox(at + Vector(-70.0F, 40.0F), 140, 8, "Concrete");
				PaintBox(at + Vector(-70.0F, -48.0F), 8, 90, "Concrete");
				PaintBox(at + Vector(62.0F, -48.0F), 8, 90, "Concrete");
				for (float y = -30.0F; y <= 26.0F; y += 14.0F) {
					for (float x = -48.0F; x <= 48.0F; x += 16.0F) {
						FluidSim::Pour(at + Vector(x, y), 9.0F, stroke.Material.empty() ? "Water" : stroke.Material.c_str());
					}
				}
				break;
			case Tool::BuildBridge:
				PaintBox(at + Vector(-110.0F, -3.0F), 220, 6, "Wood");
				for (float x = -100.0F; x <= 100.0F; x += 50.0F) {
					PaintBox(at + Vector(x - 2.0F, 3.0F), 4, 14, "Wood");
				}
				break;
			case Tool::Demolition:
			case Tool::BunkerBuster:
			case Tool::Meteor: {
				// Blasts that take out a real hole: everything within the crater goes (but for the edge of the world), with bombs going off across it for the fire, the flying debris and the harm.
				int crater = stroke.Kind == Tool::Demolition ? 34 : (stroke.Kind == Tool::BunkerBuster ? 62 : 100);
				PaintTerrain(at, crater, nullptr);
				Detonate("Standard Bomb", at);
				int extra = stroke.Kind == Tool::Demolition ? 2 : (stroke.Kind == Tool::BunkerBuster ? 5 : 9);
				for (int i = 0; i < extra; ++i) {
					float angle = 6.2832F * static_cast<float>(i) / static_cast<float>(extra);
					Detonate(i % 2 == 0 ? "Standard Bomb" : "Frag Grenade", at + Vector(std::cos(angle), std::sin(angle)) * (static_cast<float>(crater) * 0.6F));
				}
				break;
			}
			case Tool::BigBomb:
				Detonate("Standard Bomb", at);
				break;
			case Tool::Napalm:
				Detonate("Napalm Bomb", at);
				break;
			case Tool::Lightning:
				StrikeLightning(at);
				break;
			default:
				break;
		}
	}

	void QueueStroke(Tool kind, const Vector& position) {
		if (kind == Tool::BattleDefendPoint) {
			// The team being set up on the Battle tab defends here from now on.
			BattleSettings& setup = s_BattleSetup[std::clamp(s_BattleEditTeam, 0, c_Sides - 1)];
			setup.DefendPos = position;
			g_SceneMan.WrapPosition(setup.DefendPos);
			setup.HasDefendPos = true;
			SendBattleSettings(s_BattleEditTeam);
			return;
		}
		if (kind == Tool::BattleModePoint) {
			// The team being set up in the Battle tab's mode panel has its point (capture the flag: its flag) here from now on.
			const int team = std::clamp(s_BattleEditTeam, 0, c_Sides - 1);
			Vector at = position;
			g_SceneMan.WrapPosition(at);
			s_ModeSetup.Points[team] = at;
			s_ModeSetup.HasPoint[team] = true;
			SendBattleMode();
			return;
		}
		if (kind == Tool::BattleModeBase || kind == Tool::BattleModeZone) {
			// The next corner of the team's base being drawn; sent once it's closed.
			ModeBaseCorner(position, ZoneCloseDistance());
			return;
		}
		if (kind == Tool::BattleSpawnZone) {
			// The next corner of the zone being drawn; sent once it's closed.
			if (AddZoneCorner(s_ZoneDraft, s_BattleSetup[std::clamp(s_BattleEditTeam, 0, c_Sides - 1)], position, ZoneCloseDistance())) {
				SendBattleSettings(s_BattleEditTeam);
			}
			return;
		}
		Stroke stroke;
		stroke.Kind = kind;
		stroke.Position = position;
		stroke.Radius = s_Radius;
		stroke.Choice = ChoiceFor(kind);
		stroke.Team = s_Team;
		stroke.Orders = static_cast<Order>(s_Order);
		stroke.Loadout = s_Loadout;
		stroke.Count = kind == Tool::Structure ? (s_SnapToGrid ? 1 : 0) : s_SquadSize;
		if (kind == Tool::PlayCharacter) {
			stroke.Count = 1;
		} else if (kind == Tool::Barracks) {
			stroke.Count = s_ColonyKeep;
		}
		stroke.LitGrenade = s_LitGrenade;
		stroke.Craft = s_Craft;
		stroke.Random = kind == Tool::Drop && s_DropRandom;
		stroke.FavouritesOnly = s_DropFavourites;
		stroke.JetpackOnly = s_JetpackOnly;
		if (kind == Tool::WaterSpawner || kind == Tool::BuildTank) {
			stroke.Material = s_SpringLiquid;
			stroke.Rate = s_SpringRate;
		} else if (kind == Tool::PourOther) {
			stroke.Material = s_OtherPourable;
		}
		stroke.HasView = true;
		stroke.ViewMiddleX = g_CameraMan.GetOffset(0).m_X + static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) * 0.5F;
		s_Queue.push_back(stroke);
	}
} // namespace SandboxDetail
