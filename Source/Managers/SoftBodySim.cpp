#include "SoftBodySim.h"
#include "Actor.h"
#include "AirPressure.h"
#include "Atom.h"
#include "Color.h"
#include "Constants.h"
#include "FluidSim.h"
#include "FrameMan.h"
#include "MOPixel.h"
#include "Material.h"
#include "MovableMan.h"
#include "SLTerrain.h"
#include "Scene.h"
#include "SceneMan.h"
#include "TimerMan.h"
#include "Vector.h"

#include "glm/gtc/constants.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <mutex>
#include <sstream>

using namespace RTE;

namespace {
	/// What a kind of blob is made of and how it behaves.
	struct TypeData {
		SoftBodySim::TypeInfo Info;
		const char* Material; //!< The terrain material it is drawn into the ground as (Base.rte's Materials.ini).
		float Density; //!< Its weight, in kg per pixel of its area.
		float Edge; //!< How hard its edge holds its length, a share of the stretch taken up each pass: low stretches and wobbles.
		float Pressure; //!< How hard the air inside holds its area, a share of the difference made up each pass.
		float Shape; //!< How hard it is pulled back to its own shape each update: goo next to nothing, jelly firmly.
		bool RoundsUp; //!< A piece cut off it pulls itself round (goo); else it keeps the shape it was cut to (jelly).
		float Keep; //!< How much of its points' speed is kept each update, against the air.
		float GroundKeep; //!< How much of a point's speed is kept when it meets the ground.
		float Bounce; //!< How much of the speed its surface springs back at it gives a unit standing on it: above 1 throws them higher.
		float Give; //!< How deep a unit's weight dents it, at most, as a share of its radius.
		std::array<unsigned char, 3> Light; //!< Its shine.
		std::array<unsigned char, 3> Base;
		std::array<unsigned char, 3> Shadow; //!< Its lower part.
		std::array<unsigned char, 3> Dark; //!< Its rim.
	};

	// Appended to only, so saved games keep their kinds.
	const TypeData c_Types[] = {
	    {{"Goo", "Green goo: soft and wobbly, squashes flat under a unit and throws it back up, and pulls itself round again after it's cut in two.", 96, 196, 72}, "Goo", 0.012F, 0.15F, 0.6F, 0.004F, true, 0.996F, 0.5F, 1.1F, 0.4F, {196, 250, 160}, {96, 196, 72}, {64, 150, 52}, {36, 96, 34}},
	    {{"Jelly", "Pink jelly: firmer, holds its shape and wobbles back to it, and stays cut where it's cut.", 232, 96, 140}, "Jelly", 0.016F, 0.5F, 0.9F, 0.05F, false, 0.994F, 0.4F, 0.9F, 0.15F, {255, 200, 220}, {232, 96, 140}, {184, 64, 108}, {120, 36, 70}},
	    {{"Bouncy gel", "Blue gel: a trampoline. It keeps nearly all its bounce, and throws whatever lands on it higher than it fell from.", 80, 170, 240}, "Bouncy Gel", 0.012F, 0.4F, 1.0F, 0.02F, true, 0.999F, 0.7F, 1.6F, 0.3F, {210, 240, 255}, {80, 170, 240}, {52, 124, 196}, {30, 76, 140}}};
	constexpr int c_TypeCount = static_cast<int>(std::size(c_Types));

	constexpr int c_MaxBlobs = 200;
	constexpr int c_MaxPoints = 8000; //!< All the blobs' points together.
	constexpr int c_MinBlobPoints = 10;
	constexpr int c_MaxBlobPoints = 64;
	constexpr float c_Spacing = 4.0F; //!< How far apart a blob's points are made, in pixels along its edge.
	constexpr float c_MinRadius = 3.0F;
	constexpr float c_MaxRadius = 60.0F;
	constexpr float c_MinArea = 30.0F; //!< A blob or a piece cut off one smaller than this, in pixels, bursts into drops of slime.
	constexpr int c_Iterations = 10; //!< Passes of the edge, pressure and ground constraints each update.
	constexpr int c_SplitCooldown = 8; //!< Updates after it is split before a blob (and each piece) can split again: a blast's crater is dug over a few.
	constexpr float c_MaxPointSpeed = 24.0F; //!< In pixels per update.
	constexpr float c_ReboundMin = 0.25F; //!< How fast, in pixels per update, its surface has to spring out to throw a unit on it.
	constexpr const char* c_SlimeMaterial = "Slime"; //!< The liquid what is knocked out of a blob becomes.

	struct Point {
		glm::vec2 Pos{0.0F};
		glm::vec2 Prev{0.0F};
		glm::vec2 Safe{0.0F}; //!< The last place it was out of the ground.
	};

	struct Blob {
		int Id = 0;
		int Type = 0;
		std::vector<Point> Points;
		std::vector<float> Rest; //!< Rest[i]: the length of the edge from point i to the next.
		std::vector<glm::vec2> Shape; //!< Each point's place in its own shape, from its middle.
		float ShapeArea = 1.0F; //!< The area of Shape, signed by which way round its points go.
		float RestArea = 1.0F; //!< The area it keeps, signed as ShapeArea.
		std::vector<int> Stamped; //!< The terrain pixels its material is in, as keys.
		std::vector<int> Colors; //!< The colour each of Stamped is to be drawn in.
		std::vector<std::pair<int, int>> Drawn; //!< The terrain pixels it is drawn in as the world sees it (key and colour, by key), as of its last update.
		std::vector<glm::ivec2> Lost; //!< Its pixels knocked out of the terrain (shot, blasted, dug) since it was last drawn.
		int Cooldown = 0;
		int Contacts = 0;
		bool Done = false;
	};

	struct Request {
		enum class Kind {
			Create,
			Clear,
			Blast
		} What;
		int Type = 0;
		Vector Position;
		Vector Velocity;
		float Radius = 0.0F;
		float Energy = 0.0F;
	};

	/// A unit pressing on a blob this update, and where.
	struct Contact {
		Actor* Unit;
		std::vector<int> Points;
		glm::vec2 Normal{0.0F}; //!< The blob's outward normal at those points, averaged.
	};

	std::vector<Blob> s_Blobs;
	std::atomic<int> s_NextId{1};
	std::atomic<int> s_Count{0};
	std::mutex s_RequestMutex;
	std::vector<Request> s_Requests;
	std::string s_PendingLoadState;
	const Scene* s_Scene = nullptr;
	unsigned int s_SceneGeneration = 0;
	unsigned int s_Random = 0x2545F491u;
	std::array<unsigned char, 256> s_Passable{}; //!< By material: what a blob goes through (air, liquids, leaves and grass).
	std::array<unsigned char, 256> s_Liquid{};
	std::array<int, c_TypeCount> s_Material{}; //!< Each kind's terrain material, 0 if the data has none (it is then not drawn in, and nothing stands on it).
	std::array<std::array<int, 4>, c_TypeCount> s_Shades{}; //!< Each kind's palette colours: shine, body, lower part, rim.
	int s_Slime = 0;
	bool s_TablesBuilt = false;
	int s_Width = 0;
	int s_Height = 0;
	bool s_WrapX = false;

	float Random01() {
		s_Random ^= s_Random << 13;
		s_Random ^= s_Random >> 17;
		s_Random ^= s_Random << 5;
		return static_cast<float>(s_Random & 0xFFFFFF) / static_cast<float>(0x1000000);
	}

	const TypeData& TypeOf(const Blob& blob) { return c_Types[std::clamp(blob.Type, 0, c_TypeCount - 1)]; }

	void BuildTables() {
		for (int id = 0; id < 256; ++id) {
			bool liquid = id != g_MaterialAir && FluidSim::IsLiquid(id);
			s_Liquid[id] = liquid ? 1 : 0;
			s_Passable[id] = (id == g_MaterialAir || liquid || FluidSim::LetsLiquidsThrough(id)) ? 1 : 0;
		}
		const PALETTE& palette = g_FrameMan.GetDefaultPalette();
		for (int type = 0; type < c_TypeCount; ++type) {
			const Material* material = g_SceneMan.GetMaterial(c_Types[type].Material);
			s_Material[type] = material && material->GetIndex() != g_MaterialAir ? material->GetIndex() : 0;
			const TypeData& data = c_Types[type];
			s_Shades[type] = {bestfit_color(palette, data.Light[0], data.Light[1], data.Light[2]), bestfit_color(palette, data.Base[0], data.Base[1], data.Base[2]), bestfit_color(palette, data.Shadow[0], data.Shadow[1], data.Shadow[2]), bestfit_color(palette, data.Dark[0], data.Dark[1], data.Dark[2])};
		}
		const Material* slime = g_SceneMan.GetMaterial(c_SlimeMaterial);
		s_Slime = slime && slime->GetIndex() != g_MaterialAir ? slime->GetIndex() : 0;
		s_TablesBuilt = true;
	}

	void ReadTerrain() {
		SLTerrain* terrain = g_SceneMan.GetScene() ? g_SceneMan.GetScene()->GetTerrain() : nullptr;
		s_Width = terrain ? terrain->GetMaterialBitmap()->w : 0;
		s_Height = terrain ? terrain->GetMaterialBitmap()->h : 0;
		s_WrapX = g_SceneMan.SceneWrapsX();
	}

	bool WrapInWorld(int& x, int y) {
		if (s_WrapX && s_Width > 0) {
			x = ((x % s_Width) + s_Width) % s_Width;
		}
		return x >= 0 && y >= 0 && x < s_Width && y < s_Height;
	}

	int MaterialAt(float x, float y) { return g_SceneMan.GetTerrMatter(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))); }

	bool Solid(const glm::vec2& at) { return !s_Passable[MaterialAt(at.x, at.y)]; }

	glm::vec2 ToGlm(const Vector& vector) { return glm::vec2(vector.m_X, vector.m_Y); }

	Vector ToVector(const glm::vec2& vector) { return Vector(vector.x, vector.y); }

	float Cross(const glm::vec2& a, const glm::vec2& b) { return a.x * b.y - a.y * b.x; }

	int PointCount() {
		int count = 0;
		for (const Blob& blob: s_Blobs) {
			count += static_cast<int>(blob.Points.size());
		}
		return count;
	}

	float SignedArea(const std::vector<glm::vec2>& ring) {
		float area = 0.0F;
		for (size_t i = 0, count = ring.size(); i < count; ++i) {
			area += Cross(ring[i], ring[(i + 1) % count]);
		}
		return area * 0.5F;
	}

	float SignedArea(const Blob& blob) {
		float area = 0.0F;
		for (size_t i = 0, count = blob.Points.size(); i < count; ++i) {
			area += Cross(blob.Points[i].Pos, blob.Points[(i + 1) % count].Pos);
		}
		return area * 0.5F;
	}

	glm::vec2 Middle(const Blob& blob) {
		glm::vec2 sum(0.0F);
		for (const Point& point: blob.Points) {
			sum += point.Pos;
		}
		return blob.Points.empty() ? sum : sum / static_cast<float>(blob.Points.size());
	}

	/// The way out of the blob at a point, of length one: the way its area grows fastest.
	glm::vec2 Outward(const Blob& blob, size_t i) {
		size_t count = blob.Points.size();
		const glm::vec2& before = blob.Points[(i + count - 1) % count].Pos;
		const glm::vec2& after = blob.Points[(i + 1) % count].Pos;
		glm::vec2 grow(after.y - before.y, before.x - after.x);
		if (blob.RestArea < 0.0F) {
			grow = -grow;
		}
		float length = glm::length(grow);
		return length > 1e-5F ? grow / length : glm::vec2(0.0F, -1.0F);
	}

	/// Gives a blob made from a ring of points its shape, edge lengths and area: its shape round, of the area it is to keep (round is true), or
	/// the shape it has now.
	void SetShape(Blob& blob, float area, bool round) {
		size_t count = blob.Points.size();
		glm::vec2 middle = Middle(blob);
		std::vector<glm::vec2> ring(count);
		for (size_t i = 0; i < count; ++i) {
			ring[i] = blob.Points[i].Pos;
		}
		float sign = SignedArea(ring) < 0.0F ? -1.0F : 1.0F;
		blob.Shape.assign(count, glm::vec2(0.0F));
		if (round) {
			float radius = std::sqrt(std::abs(area) / glm::pi<float>());
			for (size_t i = 0; i < count; ++i) {
				glm::vec2 way = blob.Points[i].Pos - middle;
				float angle = glm::length(way) > 1e-4F ? std::atan2(way.y, way.x) : static_cast<float>(i) * glm::two_pi<float>() / static_cast<float>(count);
				blob.Shape[i] = glm::vec2(std::cos(angle), std::sin(angle)) * radius;
			}
		} else {
			for (size_t i = 0; i < count; ++i) {
				blob.Shape[i] = blob.Points[i].Pos - middle;
			}
		}
		blob.ShapeArea = SignedArea(blob.Shape);
		if (std::abs(blob.ShapeArea) < 1.0F || (blob.ShapeArea < 0.0F) != (sign < 0.0F)) {
			// (Points out of order round a ragged cut: its shape from the order they're in.)
			for (size_t i = 0; i < count; ++i) {
				blob.Shape[i] = blob.Points[i].Pos - middle;
			}
			blob.ShapeArea = SignedArea(blob.Shape);
		}
		blob.RestArea = std::abs(area) * (blob.ShapeArea < 0.0F ? -1.0F : 1.0F);
		// Its edges' lengths as its shape has them, at the size it is to keep.
		float scale = std::abs(blob.ShapeArea) > 1e-3F ? std::sqrt(std::abs(blob.RestArea) / std::abs(blob.ShapeArea)) : 1.0F;
		blob.Rest.assign(count, c_Spacing);
		for (size_t i = 0; i < count; ++i) {
			blob.Rest[i] = std::max(glm::length(blob.Shape[(i + 1) % count] - blob.Shape[i]) * scale, 0.5F);
		}
	}

	int MakeRound(int type, const glm::vec2& center, float radius, const glm::vec2& velocity) {
		if (!g_SceneMan.GetScene() || static_cast<int>(s_Blobs.size()) >= c_MaxBlobs) {
			return 0;
		}
		radius = std::clamp(radius, c_MinRadius, c_MaxRadius);
		int count = std::clamp(static_cast<int>(std::round(glm::two_pi<float>() * radius / c_Spacing)), c_MinBlobPoints, c_MaxBlobPoints);
		if (PointCount() + count > c_MaxPoints) {
			return 0;
		}
		Blob blob;
		blob.Id = s_NextId++;
		blob.Type = std::clamp(type, 0, c_TypeCount - 1);
		for (int i = 0; i < count; ++i) {
			float angle = static_cast<float>(i) * glm::two_pi<float>() / static_cast<float>(count);
			Point point;
			point.Pos = center + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
			// Put down against the ground: drawn in from it towards its middle, so it doesn't start in it.
			for (int step = 0; step < static_cast<int>(radius) && Solid(point.Pos); ++step) {
				point.Pos += (center - point.Pos) / std::max(glm::length(center - point.Pos), 1.0F);
			}
			point.Prev = point.Pos - velocity;
			point.Safe = point.Pos;
			blob.Points.push_back(point);
		}
		SetShape(blob, glm::pi<float>() * radius * radius, true);
		s_Blobs.push_back(std::move(blob));
		s_Count = static_cast<int>(s_Blobs.size());
		return s_Blobs.back().Id;
	}

	/// Makes a blob from a closed outline (a piece cut off another): its points put evenly round it, each moving as the outline did there.
	/// @return Whether there was room for it.
	bool MakeFromOutline(int type, const std::vector<glm::vec2>& outline, const std::vector<glm::vec2>& velocities, float area, std::vector<Blob>& into, int reservedPoints) {
		size_t corners = outline.size();
		if (corners < 3 || static_cast<int>(s_Blobs.size() + into.size()) >= c_MaxBlobs) {
			return false;
		}
		std::vector<float> along(corners + 1, 0.0F);
		for (size_t i = 0; i < corners; ++i) {
			along[i + 1] = along[i] + glm::length(outline[(i + 1) % corners] - outline[i]);
		}
		float perimeter = along[corners];
		if (perimeter < 1.0F) {
			return false;
		}
		int count = std::clamp(static_cast<int>(std::round(perimeter / c_Spacing)), c_MinBlobPoints, c_MaxBlobPoints);
		if (reservedPoints + count > c_MaxPoints) {
			return false;
		}
		Blob blob;
		blob.Id = s_NextId++;
		blob.Type = type;
		blob.Cooldown = c_SplitCooldown;
		size_t corner = 0;
		for (int i = 0; i < count; ++i) {
			float at = perimeter * static_cast<float>(i) / static_cast<float>(count);
			while (corner + 1 < corners && along[corner + 1] < at) {
				++corner;
			}
			float span = along[corner + 1] - along[corner];
			float t = span > 1e-5F ? (at - along[corner]) / span : 0.0F;
			size_t next = (corner + 1) % corners;
			Point point;
			point.Pos = outline[corner] + (outline[next] - outline[corner]) * t;
			point.Prev = point.Pos - (velocities[corner] + (velocities[next] - velocities[corner]) * t);
			point.Safe = point.Pos;
			blob.Points.push_back(point);
		}
		SetShape(blob, area, TypeOf(blob).RoundsUp);
		into.push_back(std::move(blob));
		return true;
	}

	/// Throws drops of slime (or of the blob itself, where the data has no slime) from points in an area.
	void SprayDrops(const Blob& blob, const glm::vec2& middle, const glm::vec2& velocity, int drops) {
		int material = s_Slime != 0 ? s_Slime : s_Material[blob.Type];
		const Material* sceneMaterial = material != 0 ? g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(material)) : nullptr;
		if (!sceneMaterial || blob.Points.empty()) {
			return;
		}
		for (int i = 0; i < drops; ++i) {
			const Point& point = blob.Points[static_cast<size_t>(Random01() * static_cast<float>(blob.Points.size())) % blob.Points.size()];
			glm::vec2 at = middle + (point.Pos - middle) * (0.3F + 0.6F * Random01());
			if (Solid(at)) {
				continue;
			}
			float angle = Random01() * glm::two_pi<float>();
			float speed = 0.5F + 2.5F * Random01();
			glm::vec2 throwVelocity = velocity + glm::vec2(std::cos(angle), std::sin(angle) - 0.6F) * speed;
			Color color;
			color.SetRGBWithIndex(s_Shades[blob.Type][Random01() < 0.7F ? 1 : 2]);
			// Particle speeds are in metres a second: 20 pixels to the metre, 60 updates a second.
			MOPixel* pixel = new MOPixel(color, sceneMaterial->GetPixelDensity(), ToVector(at), Vector(throwVelocity.x * 3.0F, throwVelocity.y * 3.0F), new Atom(Vector(), sceneMaterial->GetIndex(), nullptr, color, 2), 0);
			pixel->SetToHitMOs(false);
			g_MovableMan.AddParticle(pixel);
		}
	}

	/// Bursts a blob too small to hold together into drops.
	void Burst(Blob& blob) {
		glm::vec2 middle = Middle(blob);
		glm::vec2 velocity(0.0F);
		for (const Point& point: blob.Points) {
			velocity += point.Pos - point.Prev;
		}
		velocity /= static_cast<float>(std::max<size_t>(blob.Points.size(), 1));
		SprayDrops(blob, middle, velocity, std::clamp(static_cast<int>(std::abs(blob.RestArea) / 3.0F), 3, 40));
		blob.Done = true;
	}

	/// The units close enough to a blob to touch it: their middles within its box grown by their size.
	std::vector<Actor*> NearUnits(const Blob& blob, glm::vec2& low, glm::vec2& high) {
		low = glm::vec2(1e9F);
		high = glm::vec2(-1e9F);
		for (const Point& point: blob.Points) {
			low = glm::min(low, point.Pos);
			high = glm::max(high, point.Pos);
		}
		std::vector<Actor*> near;
		for (Actor* actor: g_MovableMan.GetActorList()) {
			if (!actor || actor->IsSetToDelete() || actor->GetPinStrength() > 0.0F || actor->GetMass() <= 0.0F) {
				continue;
			}
			glm::vec2 at = ToGlm(actor->GetPos());
			float reach = actor->GetRadius() + 4.0F;
			if (s_WrapX && s_Width > 0) {
				// (Its x taken the short way round to the blob's.)
				float middle = (low.x + high.x) * 0.5F;
				float width = static_cast<float>(s_Width);
				at.x = middle + std::remainder(at.x - middle, width);
			}
			if (at.x + reach >= low.x && at.x - reach <= high.x && at.y + reach >= low.y && at.y - reach <= high.y) {
				near.push_back(actor);
			}
		}
		return near;
	}

	/// The unit (the whole of it) whose body is at a pixel, if it's one of those near.
	Actor* UnitAt(const glm::vec2& at, const std::vector<Actor*>& near) {
		MOID id = g_SceneMan.GetMOIDPixel(static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)));
		if (id == g_NoMOID) {
			return nullptr;
		}
		MovableObject* object = g_MovableMan.GetMOFromID(id);
		MovableObject* root = object ? object->GetRootParent() : nullptr;
		for (Actor* actor: near) {
			if (actor == root) {
				return actor;
			}
		}
		return nullptr;
	}

	/// Lifts a blob's material out of the terrain. With noteLost, the pixels of it that are no longer its own (shot, blasted or dug away, or
	/// painted over) are noted as lost, and no longer drawn by it; what is there in their place is left.
	/// Only the material: its colours stay as they are until Finish, so a blob lying still and lifted and drawn in again each update changes
	/// nothing the world sees (the lighting is worked out again wherever the terrain changes).
	void Unstamp(SLTerrain* terrain, Blob& blob, bool noteLost) {
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		int material = s_Material[blob.Type];
		std::vector<int> lostKeys;
		for (int key: blob.Stamped) {
			int x = key % s_Width;
			int y = key / s_Width;
			if (y < 0 || y >= s_Height) {
				continue;
			}
			if (materialBitmap->line[y][x] == material) {
				terrain->SetPixel(x, y, g_MaterialAir);
			} else if (noteLost) {
				if (blob.Lost.size() < 4096) {
					blob.Lost.emplace_back(x, y);
				}
				lostKeys.push_back(key);
			}
		}
		blob.Stamped.clear();
		blob.Colors.clear();
		if (!lostKeys.empty()) {
			std::sort(lostKeys.begin(), lostKeys.end());
			std::erase_if(blob.Drawn, [&lostKeys](const std::pair<int, int>& drawn) { return std::binary_search(lostKeys.begin(), lostKeys.end(), drawn.first); });
		}
	}

	/// Makes what the world sees of a blob what it is now: the pixels it has left cleared, those it has come to drawn in, those that changed
	/// shade redrawn, and the terrain's changes noted only where its material came or went.
	void Finish(SLTerrain* terrain, Blob& blob) {
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		std::vector<std::pair<int, int>> now(blob.Stamped.size());
		for (size_t i = 0; i < blob.Stamped.size(); ++i) {
			now[i] = {blob.Stamped[i], blob.Colors[i]};
		}
		std::sort(now.begin(), now.end());
		auto noteChange = [&](int key) {
			int x = key % s_Width;
			int y = key / s_Width;
			SLTerrain::NoteMaterialChangeBox(x, y, x, y);
		};
		size_t was = 0;
		size_t is = 0;
		while (was < blob.Drawn.size() || is < now.size()) {
			if (is >= now.size() || (was < blob.Drawn.size() && blob.Drawn[was].first < now[is].first)) {
				// Left: cleared, unless something else is there now (another blob).
				int key = blob.Drawn[was++].first;
				int x = key % s_Width;
				int y = key / s_Width;
				if (materialBitmap->line[y][x] == g_MaterialAir) {
					terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
					noteChange(key);
				}
			} else if (was >= blob.Drawn.size() || now[is].first < blob.Drawn[was].first) {
				// Come to.
				int key = now[is].first;
				terrain->SetFGColorPixel(key % s_Width, key / s_Width, now[is].second);
				noteChange(key);
				++is;
			} else {
				if (now[is].second != blob.Drawn[was].second) {
					terrain->SetFGColorPixel(now[is].first % s_Width, now[is].first / s_Width, now[is].second);
				}
				++was;
				++is;
			}
		}
		blob.Drawn.swap(now);
	}

	/// Takes a blob out of the terrain altogether: its material, where it is still its own, and its colours.
	void Erase(SLTerrain* terrain, Blob& blob) {
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		int material = s_Material[blob.Type];
		Unstamp(terrain, blob, false);
		for (const auto& [key, color]: blob.Drawn) {
			int x = key % s_Width;
			int y = key / s_Width;
			if (y >= 0 && y < s_Height && (materialBitmap->line[y][x] == material || materialBitmap->line[y][x] == g_MaterialAir)) {
				terrain->SetMaterialPixel(x, y, g_MaterialAir);
				terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
			}
		}
		blob.Drawn.clear();
	}

	/// Draws a blob into the terrain where it is, as its material, shaded: a darker rim, a darker lower part and a shine up on one side. Only into
	/// air, and never over a unit's body (one drawn into would be stuck fast in the ground). Its points are kept out of the units by it and
	/// a unit's weight dents it only so far, so a unit on it stands on its surface rather than in a hole of its own shape.
	void Stamp(SLTerrain* terrain, Blob& blob) {
		int material = s_Material[blob.Type];
		if (material == 0 || blob.Points.size() < 3) {
			return;
		}
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		glm::vec2 low;
		glm::vec2 high;
		std::vector<Actor*> near = NearUnits(blob, low, high);
		struct UnitBox {
			float Left, Top, Right, Bottom;
		};
		std::vector<UnitBox> unitBoxes;
		for (Actor* actor: near) {
			glm::vec2 at = ToGlm(actor->GetPos());
			if (s_WrapX && s_Width > 0) {
				float middle = (low.x + high.x) * 0.5F;
				at.x = middle + std::remainder(at.x - middle, static_cast<float>(s_Width));
			}
			float reach = actor->GetRadius() + 1.0F;
			unitBoxes.push_back({at.x - reach, at.y - reach, at.x + reach, at.y + reach});
		}
		int left = static_cast<int>(std::floor(low.x)) - 1;
		int top = static_cast<int>(std::floor(low.y)) - 1;
		int width = static_cast<int>(std::ceil(high.x)) - left + 2;
		int height = static_cast<int>(std::ceil(high.y)) - top + 2;
		if (width <= 0 || height <= 0 || width > 2048 || height > 2048) {
			return;
		}
		// Filled a row at a time between the crossings of its outline (as the hold zones are drawn), into a mask of its box.
		static std::vector<unsigned char> inside;
		inside.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
		static std::vector<float> crossings;
		size_t count = blob.Points.size();
		for (int row = 0; row < height; ++row) {
			float middleY = static_cast<float>(top + row) + 0.5F;
			crossings.clear();
			for (size_t i = 0, j = count - 1; i < count; j = i++) {
				const glm::vec2& a = blob.Points[i].Pos;
				const glm::vec2& b = blob.Points[j].Pos;
				if ((a.y > middleY) != (b.y > middleY)) {
					crossings.push_back(a.x + (b.x - a.x) * (middleY - a.y) / (b.y - a.y));
				}
			}
			std::sort(crossings.begin(), crossings.end());
			for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
				int from = std::max(static_cast<int>(std::ceil(crossings[i] - 0.5F)) - left, 0);
				int to = std::min(static_cast<int>(std::floor(crossings[i + 1] - 0.5F)) - left, width - 1);
				for (int column = from; column <= to; ++column) {
					inside[static_cast<size_t>(row) * width + column] = 1;
				}
			}
		}
		glm::vec2 middle = Middle(blob);
		float radius = std::sqrt(std::abs(SignedArea(blob)) / glm::pi<float>());
		glm::vec2 shine = middle + glm::vec2(-0.38F, -0.42F) * radius;
		float shineReach = std::max(radius * 0.2F, 1.0F);
		const std::array<int, 4>& shades = s_Shades[blob.Type];
		auto in = [&](int column, int row) { return column >= 0 && row >= 0 && column < width && row < height && inside[static_cast<size_t>(row) * width + column]; };
		for (int row = 0; row < height; ++row) {
			int y = top + row;
			if (y < 0 || y >= s_Height) {
				continue;
			}
			for (int column = 0; column < width; ++column) {
				if (!inside[static_cast<size_t>(row) * width + column]) {
					continue;
				}
				int x = left + column;
				int rawX = x;
				if (!WrapInWorld(x, y) || materialBitmap->line[y][x] != g_MaterialAir) {
					continue;
				}
				glm::vec2 at(static_cast<float>(rawX) + 0.5F, static_cast<float>(y) + 0.5F);
				bool underUnit = false;
				for (const UnitBox& box: unitBoxes) {
					if (at.x >= box.Left && at.x <= box.Right && at.y >= box.Top && at.y <= box.Bottom) {
						underUnit = true;
						break;
					}
				}
				if (underUnit && g_SceneMan.GetMOIDPixel(x, y) != g_NoMOID) {
					continue;
				}
				int color = shades[1];
				if (!in(column - 1, row) || !in(column + 1, row) || !in(column, row - 1) || !in(column, row + 1)) {
					color = shades[3];
				} else if (glm::length(at - shine) <= shineReach) {
					color = shades[0];
				} else if (at.y - middle.y > radius * 0.4F) {
					color = shades[2];
				}
				terrain->SetPixel(x, y, material);
				blob.Stamped.push_back(y * s_Width + x);
				blob.Colors.push_back(color);
			}
		}
	}

	/// Splits a blob along a line, into the pieces either side; a piece too small bursts into drops.
	/// @return Whether it was split.
	bool Split(Blob& blob, const glm::vec2& through, glm::vec2 way, std::vector<Blob>& into, int& reservedPoints) {
		float length = glm::length(way);
		if (length < 1e-4F) {
			return false;
		}
		way /= length;
		size_t count = blob.Points.size();
		struct Crossing {
			size_t Edge;
			glm::vec2 At;
			glm::vec2 Velocity;
			float Along;
		};
		std::vector<Crossing> crossings;
		for (size_t i = 0; i < count; ++i) {
			const Point& a = blob.Points[i];
			const Point& b = blob.Points[(i + 1) % count];
			float sideA = Cross(way, a.Pos - through);
			float sideB = Cross(way, b.Pos - through);
			if ((sideA > 0.0F) != (sideB > 0.0F)) {
				float t = sideA / (sideA - sideB);
				glm::vec2 at = a.Pos + (b.Pos - a.Pos) * t;
				glm::vec2 velocity = (a.Pos - a.Prev) + ((b.Pos - b.Prev) - (a.Pos - a.Prev)) * t;
				crossings.push_back({i, at, velocity, glm::dot(at - through, way)});
			}
		}
		if (crossings.size() < 2) {
			return false;
		}
		std::sort(crossings.begin(), crossings.end(), [](const Crossing& a, const Crossing& b) { return a.Along < b.Along; });
		// The stretch of the line inside the blob that the hit is on: crossings go in and out by turns along it.
		size_t pick = 0;
		float best = 1e9F;
		for (size_t k = 0; k + 1 < crossings.size(); k += 2) {
			float from = crossings[k].Along;
			float to = crossings[k + 1].Along;
			float off = from <= 0.0F && to >= 0.0F ? 0.0F : std::min(std::abs(from), std::abs(to));
			if (off < best) {
				best = off;
				pick = k;
			}
		}
		const Crossing& first = crossings[pick];
		const Crossing& second = crossings[pick + 1];
		// The two outlines: from one crossing round the points to the other, and from that one round the rest back.
		std::array<std::vector<glm::vec2>, 2> outlines;
		std::array<std::vector<glm::vec2>, 2> velocities;
		auto walk = [&](const Crossing& from, const Crossing& to, int piece) {
			outlines[piece].push_back(from.At);
			velocities[piece].push_back(from.Velocity);
			size_t i = (from.Edge + 1) % count;
			for (size_t steps = 0; steps < count; ++steps) {
				outlines[piece].push_back(blob.Points[i].Pos);
				velocities[piece].push_back(blob.Points[i].Pos - blob.Points[i].Prev);
				if (i == to.Edge) {
					break;
				}
				i = (i + 1) % count;
			}
			outlines[piece].push_back(to.At);
			velocities[piece].push_back(to.Velocity);
		};
		walk(first, second, 0);
		walk(second, first, 1);
		std::array<float, 2> areas{std::abs(SignedArea(outlines[0])), std::abs(SignedArea(outlines[1]))};
		float total = areas[0] + areas[1];
		if (total < 1.0F) {
			return false;
		}
		glm::vec2 across(-way.y, way.x);
		const TypeData& type = TypeOf(blob);
		for (int piece = 0; piece < 2; ++piece) {
			float share = std::abs(blob.RestArea) * areas[piece] / total;
			// Which side of the cut it's on: the side most of it is.
			float side = 0.0F;
			for (const glm::vec2& at: outlines[piece]) {
				side += Cross(way, at - through);
			}
			side = side < 0.0F ? -1.0F : 1.0F;
			// The pieces part along the cut a little, and move apart.
			for (size_t i = 0; i < outlines[piece].size(); ++i) {
				outlines[piece][i] += across * side * 1.0F;
				velocities[piece][i] += across * side * (0.3F + 0.2F * type.Edge);
			}
			if (share < c_MinArea || !MakeFromOutline(blob.Type, outlines[piece], velocities[piece], share, into, reservedPoints)) {
				Blob drop;
				drop.Type = blob.Type;
				for (size_t i = 0; i < outlines[piece].size(); ++i) {
					drop.Points.push_back({outlines[piece][i], outlines[piece][i] - velocities[piece][i], outlines[piece][i]});
				}
				drop.RestArea = share;
				Burst(drop);
			} else {
				reservedPoints += static_cast<int>(into.back().Points.size());
			}
		}
		blob.Done = true;
		return true;
	}

	/// What the pixels knocked out of a blob since it was drawn do to it: it dents and shrinks by them, and a shot right through it or a big
	/// bite out of it splits it.
	void TakeDamage(Blob& blob, std::vector<Blob>& into, int& reservedPoints) {
		if (blob.Lost.empty()) {
			return;
		}
		size_t lost = blob.Lost.size();
		float area = std::abs(blob.RestArea);
		glm::vec2 hit(0.0F);
		for (const glm::ivec2& pixel: blob.Lost) {
			hit += glm::vec2(static_cast<float>(pixel.x) + 0.5F, static_cast<float>(pixel.y) + 0.5F);
		}
		hit /= static_cast<float>(lost);
		glm::vec2 middle = Middle(blob);
		if (s_WrapX && s_Width > 0) {
			hit.x = middle.x + std::remainder(hit.x - middle.x, static_cast<float>(s_Width));
		}
		// Knocked in where it was hit.
		size_t stride = std::max<size_t>(1, lost / 256);
		for (size_t i = 0; i < blob.Points.size(); ++i) {
			Point& point = blob.Points[i];
			for (size_t j = 0; j < lost; j += stride) {
				glm::vec2 pixel(static_cast<float>(blob.Lost[j].x) + 0.5F, static_cast<float>(blob.Lost[j].y) + 0.5F);
				if (s_WrapX && s_Width > 0) {
					pixel.x = middle.x + std::remainder(pixel.x - middle.x, static_cast<float>(s_Width));
				}
				if (glm::length(pixel - point.Pos) < 3.0F) {
					point.Prev += Outward(blob, i) * 0.6F;
					break;
				}
			}
		}
		// Smaller by what was knocked out of it.
		float left = area - static_cast<float>(lost) * 0.9F;
		if (left < c_MinArea) {
			blob.Lost.clear();
			Burst(blob);
			return;
		}
		float scale = std::sqrt(left / area);
		blob.RestArea *= left / area;
		for (float& rest: blob.Rest) {
			rest *= scale;
		}
		if (blob.Cooldown > 0 || lost < 4) {
			blob.Lost.clear();
			return;
		}
		// The hit's long way, and how far along it the lost pixels go: a shot through leaves a long thin line of them.
		float xx = 0.0F;
		float xy = 0.0F;
		float yy = 0.0F;
		std::vector<glm::vec2> pixels;
		pixels.reserve(lost);
		for (const glm::ivec2& raw: blob.Lost) {
			glm::vec2 pixel(static_cast<float>(raw.x) + 0.5F, static_cast<float>(raw.y) + 0.5F);
			if (s_WrapX && s_Width > 0) {
				pixel.x = middle.x + std::remainder(pixel.x - middle.x, static_cast<float>(s_Width));
			}
			glm::vec2 off = pixel - hit;
			xx += off.x * off.x;
			xy += off.x * off.y;
			yy += off.y * off.y;
			pixels.push_back(pixel);
		}
		float angle = 0.5F * std::atan2(2.0F * xy, xx - yy);
		glm::vec2 way(std::cos(angle), std::sin(angle));
		float lowHit = 1e9F;
		float highHit = -1e9F;
		for (const glm::vec2& pixel: pixels) {
			float along = glm::dot(pixel - hit, way);
			lowHit = std::min(lowHit, along);
			highHit = std::max(highHit, along);
		}
		float lowBlob = 1e9F;
		float highBlob = -1e9F;
		for (const Point& point: blob.Points) {
			float along = glm::dot(point.Pos - hit, way);
			lowBlob = std::min(lowBlob, along);
			highBlob = std::max(highBlob, along);
		}
		blob.Lost.clear();
		if (highHit - lowHit >= 0.6F * (highBlob - lowBlob)) {
			// Shot through: cut along the shot.
			Split(blob, hit, way, into, reservedPoints);
		} else if (static_cast<float>(lost) >= 0.12F * area) {
			// A big bite: what is beyond it comes off (all of it, if the bite is in its middle, splits it in two).
			glm::vec2 out = hit - middle;
			glm::vec2 cut = glm::length(out) > 1.0F ? glm::vec2(-out.y, out.x) : way;
			Split(blob, hit, cut, into, reservedPoints);
		}
	}

	/// Keeps a point out of the ground: back to the last free place along the way it came, then slid along the ground from there.
	/// @return Whether it met the ground.
	bool Collide(Point& point, float groundKeep) {
		glm::vec2 way = point.Pos - point.Safe;
		bool hit = Solid(point.Pos);
		glm::vec2 free = point.Safe;
		if (!hit && std::max(std::abs(way.x), std::abs(way.y)) > 1.5F) {
			// Moved more than a pixel: what it passed through on the way counts too (a thin floor).
			int steps = static_cast<int>(std::ceil(std::max(std::abs(way.x), std::abs(way.y))));
			for (int step = 1; step < steps; ++step) {
				glm::vec2 at = point.Safe + way * (static_cast<float>(step) / static_cast<float>(steps));
				if (Solid(at)) {
					hit = true;
					break;
				}
				free = at;
			}
		} else if (hit) {
			int steps = static_cast<int>(std::ceil(std::max(std::abs(way.x), std::abs(way.y))));
			for (int step = 1; step < steps; ++step) {
				glm::vec2 at = point.Safe + way * (static_cast<float>(step) / static_cast<float>(steps));
				if (Solid(at)) {
					break;
				}
				free = at;
			}
		}
		if (!hit) {
			point.Safe = point.Pos;
			return false;
		}
		glm::vec2 alongX(point.Pos.x, free.y);
		glm::vec2 alongY(free.x, point.Pos.y);
		if (!Solid(alongX)) {
			point.Pos = alongX;
		} else if (!Solid(alongY)) {
			point.Pos = alongY;
		} else {
			point.Pos = free;
		}
		point.Prev = point.Pos - (point.Pos - point.Prev) * groundKeep;
		if (!Solid(point.Pos)) {
			point.Safe = point.Pos;
		}
		return true;
	}

	/// Where its own shape, turned to how it lies now and at the size it keeps, puts each point.
	void ShapeTargets(const Blob& blob, std::vector<glm::vec2>& targets) {
		size_t count = blob.Points.size();
		glm::vec2 middle = Middle(blob);
		float dot = 0.0F;
		float cross = 0.0F;
		for (size_t i = 0; i < count; ++i) {
			glm::vec2 now = blob.Points[i].Pos - middle;
			dot += glm::dot(blob.Shape[i], now);
			cross += Cross(blob.Shape[i], now);
		}
		float angle = std::atan2(cross, dot);
		float c = std::cos(angle);
		float s = std::sin(angle);
		float scale = std::abs(blob.ShapeArea) > 1e-3F ? std::sqrt(std::abs(blob.RestArea) / std::abs(blob.ShapeArea)) : 1.0F;
		targets.resize(count);
		for (size_t i = 0; i < count; ++i) {
			const glm::vec2& shape = blob.Shape[i];
			targets[i] = middle + glm::vec2(shape.x * c - shape.y * s, shape.x * s + shape.y * c) * scale;
		}
	}

	/// Pulls the points towards its own shape.
	void MatchShape(Blob& blob, float stiffness) {
		if (stiffness <= 0.0F) {
			return;
		}
		static std::vector<glm::vec2> targets;
		ShapeTargets(blob, targets);
		for (size_t i = 0; i < blob.Points.size(); ++i) {
			blob.Points[i].Pos += (targets[i] - blob.Points[i].Pos) * stiffness;
		}
	}

	/// Holds each edge to its length and the area to what it keeps: a pass of each.
	void SolveShape(Blob& blob, const TypeData& type) {
		size_t count = blob.Points.size();
		for (size_t i = 0; i < count; ++i) {
			Point& a = blob.Points[i];
			Point& b = blob.Points[(i + 1) % count];
			glm::vec2 way = b.Pos - a.Pos;
			float length = glm::length(way);
			if (length < 1e-4F) {
				continue;
			}
			glm::vec2 correction = way * ((length - blob.Rest[i]) / length * 0.5F * type.Edge);
			a.Pos += correction;
			b.Pos -= correction;
		}
		// The air inside pushes each point out (or draws it in) along the way the area grows, as much as makes the area up.
		float area = SignedArea(blob);
		static std::vector<glm::vec2> grow;
		grow.resize(count);
		float sum = 0.0F;
		for (size_t i = 0; i < count; ++i) {
			const glm::vec2& before = blob.Points[(i + count - 1) % count].Pos;
			const glm::vec2& after = blob.Points[(i + 1) % count].Pos;
			grow[i] = glm::vec2(after.y - before.y, before.x - after.x) * 0.5F;
			sum += glm::dot(grow[i], grow[i]);
		}
		if (sum < 1e-6F) {
			return;
		}
		float lambda = (blob.RestArea - area) / sum * type.Pressure;
		for (size_t i = 0; i < count; ++i) {
			glm::vec2 move = grow[i] * lambda;
			float moveLength = glm::length(move);
			if (moveLength > 2.0F) {
				move *= 2.0F / moveLength;
			}
			blob.Points[i].Pos += move;
		}
	}

	/// The units pressing on a blob, and where: a unit's body just outside a point (or on it).
	std::vector<Contact> FindContacts(const Blob& blob, const std::vector<Actor*>& near) {
		std::vector<Contact> contacts;
		if (near.empty()) {
			return contacts;
		}
		for (size_t i = 0; i < blob.Points.size(); ++i) {
			glm::vec2 out = Outward(blob, i);
			Actor* unit = nullptr;
			for (float reach: {0.0F, 1.5F, 3.0F}) {
				if ((unit = UnitAt(blob.Points[i].Pos + out * reach, near))) {
					break;
				}
			}
			if (!unit) {
				continue;
			}
			auto found = std::find_if(contacts.begin(), contacts.end(), [unit](const Contact& contact) { return contact.Unit == unit; });
			if (found == contacts.end()) {
				contacts.push_back({unit, {}, glm::vec2(0.0F)});
				found = contacts.end() - 1;
			}
			found->Points.push_back(static_cast<int>(i));
			found->Normal += out;
		}
		for (Contact& contact: contacts) {
			float length = glm::length(contact.Normal);
			contact.Normal = length > 1e-4F ? contact.Normal / length : glm::vec2(0.0F, -1.0F);
		}
		return contacts;
	}

	/// The units on a blob press it in: their weight where they stand on it, and how fast they came down onto it.
	void PressedByUnits(Blob& blob, const std::vector<Contact>& contacts, float seconds, const glm::vec2& gravity) {
		size_t count = blob.Points.size();
		float pointMass = std::max(std::abs(blob.RestArea) * TypeOf(blob).Density / static_cast<float>(count), 0.01F);
		float perUpdate = seconds * c_PPM; // m/s to pixels per update.
		// How far in from where its shape would have them its points may be pushed by weight: past that it holds the unit up. (The air inside
		// only keeps its area, which a dent alone doesn't change much, so without this a unit would sink on through it.)
		float give = std::max(std::sqrt(std::abs(blob.RestArea) / glm::pi<float>()) * TypeOf(blob).Give, 1.0F);
		static std::vector<glm::vec2> targets;
		if (!contacts.empty()) {
			ShapeTargets(blob, targets);
		}
		for (const Contact& contact: contacts) {
			float mass = contact.Unit->GetMass();
			float share = 1.0F / static_cast<float>(contact.Points.size());
			glm::vec2 velocity = ToGlm(contact.Unit->GetVel()) * perUpdate;
			glm::vec2 before = ToGlm(contact.Unit->GetPrevVel()) * perUpdate;
			// How fast it came at the blob: before its last move (when it was stopped by it) or now.
			float coming = std::max({glm::dot(before, -contact.Normal), glm::dot(velocity, -contact.Normal), 0.0F});
			bool leaving = glm::dot(velocity, contact.Normal) > 0.5F;
			for (int index: contact.Points) {
				Point& point = blob.Points[index];
				glm::vec2 out = Outward(blob, static_cast<size_t>(index));
				if (!leaving) {
					// Its weight, where it stands on top: a push down on the points under it, as much as their weight against its.
					float onTop = std::max(-out.y, 0.0F);
					float dent = glm::dot(targets[index] - point.Pos, out);
					float room = std::clamp(1.0F - dent / give, 0.0F, 1.0F);
					float push = std::min(mass * share / pointMass, 40.0F) * onTop * room;
					point.Pos += gravity * push * 0.15F;
				}
				if (coming > 0.05F) {
					// Landing on it: the points under it are driven in, the more the heavier it is against them, and their neighbours half as much.
					float driven = std::min(coming * std::min(mass * share / pointMass, 4.0F) * 0.5F, 10.0F);
					point.Prev += out * driven;
					for (int side: {-1, 1}) {
						Point& neighbour = blob.Points[(static_cast<size_t>(index) + count + side) % count];
						neighbour.Prev += Outward(blob, (static_cast<size_t>(index) + count + side) % count) * driven * 0.5F;
					}
				}
			}
		}
	}

	/// The units on a blob are thrown back by its surface as it springs out under them.
	void ThrowUnits(const Blob& blob, const std::vector<Contact>& contacts, float seconds) {
		const TypeData& type = TypeOf(blob);
		for (const Contact& contact: contacts) {
			float springing = 0.0F;
			for (int index: contact.Points) {
				const Point& point = blob.Points[index];
				springing += glm::dot(point.Pos - point.Prev, contact.Normal);
			}
			springing /= static_cast<float>(contact.Points.size());
			if (springing < c_ReboundMin) {
				continue;
			}
			// In m/s, as the unit's speed is.
			float wanted = std::min(springing * type.Bounce / (seconds * c_PPM), 30.0F);
			Vector velocity = contact.Unit->GetVel();
			float along = velocity.m_X * contact.Normal.x + velocity.m_Y * contact.Normal.y;
			if (along < wanted) {
				contact.Unit->SetVel(velocity + ToVector(contact.Normal * (wanted - along)));
			}
		}
	}

	/// Keeps a blob's points out of the units by it: a point inside a unit's body is drawn back into the blob until it is clear of it, so the blob
	/// is pressed in round a unit standing or landing on it (or one it falls on) rather than closing over it.
	void KeepOutOfUnits(Blob& blob, const std::vector<Actor*>& near, float groundKeep) {
		if (near.empty()) {
			return;
		}
		for (size_t i = 0; i < blob.Points.size(); ++i) {
			Point& point = blob.Points[i];
			if (!UnitAt(point.Pos, near)) {
				continue;
			}
			glm::vec2 in = -Outward(blob, i);
			for (int step = 0; step < 8; ++step) {
				glm::vec2 next = point.Pos + in;
				if (Solid(next)) {
					break;
				}
				point.Pos = next;
				if (!UnitAt(point.Pos, near)) {
					break;
				}
			}
			point.Prev = point.Pos - (point.Pos - point.Prev) * groundKeep;
			if (!Solid(point.Pos)) {
				point.Safe = point.Pos;
			}
		}
	}

	/// Moves a blob on one update: its points on at their speed with gravity, liquid and the wind's blasts on them, pressed by the units on it,
	/// pulled back to its shape, its edges and area held, kept out of the ground, and the units on it thrown back as it springs out.
	void Step(Blob& blob, float seconds, const glm::vec2& gravity) {
		const TypeData& type = TypeOf(blob);
		for (Point& point: blob.Points) {
			glm::vec2 velocity = (point.Pos - point.Prev) * type.Keep;
			if (float speed = glm::length(velocity); speed > c_MaxPointSpeed) {
				velocity *= c_MaxPointSpeed / speed;
			}
			glm::vec2 accel = gravity;
			if (s_Liquid[MaterialAt(point.Pos.x, point.Pos.y)]) {
				// Held up a little more than its weight, and dragged hard: blobs float.
				velocity *= 0.9F;
				accel -= gravity * 1.15F;
			}
			point.Prev = point.Pos;
			point.Pos += velocity + accel;
			if (glm::vec2 push = ToGlm(AirPressure::GetPush(ToVector(point.Pos))); push.x != 0.0F || push.y != 0.0F) {
				point.Pos += push * c_PPM * seconds * 0.3F;
			}
		}
		glm::vec2 low;
		glm::vec2 high;
		std::vector<Actor*> near = NearUnits(blob, low, high);
		std::vector<Contact> contacts = FindContacts(blob, near);
		blob.Contacts = 0;
		for (const Contact& contact: contacts) {
			blob.Contacts += static_cast<int>(contact.Points.size());
		}
		PressedByUnits(blob, contacts, seconds, gravity);
		MatchShape(blob, type.Shape);
		for (int pass = 0; pass < c_Iterations; ++pass) {
			SolveShape(blob, type);
			for (Point& point: blob.Points) {
				Collide(point, type.GroundKeep);
			}
		}
		ThrowUnits(blob, contacts, seconds);
		KeepOutOfUnits(blob, near, type.GroundKeep);
		if (blob.Cooldown > 0) {
			--blob.Cooldown;
		}
	}

	void Blast(const glm::vec2& at, float reach, float energy) {
		float push = std::clamp(std::sqrt(std::max(energy, 0.0F)) * 0.05F, 1.0F, 12.0F);
		for (Blob& blob: s_Blobs) {
			float weight = std::max(std::sqrt(TypeOf(blob).Density * 60.0F), 0.3F);
			for (Point& point: blob.Points) {
				glm::vec2 away = point.Pos - at;
				if (s_WrapX && s_Width > 0) {
					away.x = std::remainder(away.x, static_cast<float>(s_Width));
				}
				float distance = glm::length(away);
				if (distance >= reach || distance < 0.01F) {
					continue;
				}
				point.Prev -= away / distance * push * (1.0F - distance / reach) / weight;
			}
		}
	}

	/// Takes every blob away; out of the terrain too, when it is still the terrain they were drawn into.
	void RemoveAll(bool unstamp) {
		SLTerrain* terrain = g_SceneMan.GetScene() ? g_SceneMan.GetScene()->GetTerrain() : nullptr;
		if (unstamp && terrain && g_SceneMan.GetScene() == s_Scene && g_SceneMan.GetSceneGeneration() == s_SceneGeneration) {
			ReadTerrain();
			for (Blob& blob: s_Blobs) {
				Erase(terrain, blob);
			}
		}
		s_Blobs.clear();
		s_Count = 0;
	}

	void TakeRequests() {
		std::vector<Request> requests;
		{
			std::scoped_lock lock(s_RequestMutex);
			requests.swap(s_Requests);
		}
		for (const Request& request: requests) {
			switch (request.What) {
				case Request::Kind::Create:
					MakeRound(request.Type, ToGlm(request.Position), request.Radius, ToGlm(request.Velocity));
					break;
				case Request::Kind::Clear:
					// Their pictures too, as Clear does: lifted out already, their materials are gone but their colours would stay painted on.
					RemoveAll(true);
					break;
				case Request::Kind::Blast:
					Blast(ToGlm(request.Position), request.Radius, request.Energy);
					break;
			}
		}
	}

	/// Takes out of the terrain what a saved game's blobs left drawn in it, where the blobs are: they are drawn in afresh.
	void ClearSavedPixels(SLTerrain* terrain) {
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		for (const Blob& blob: s_Blobs) {
			int material = s_Material[blob.Type];
			if (material == 0) {
				continue;
			}
			glm::vec2 low(1e9F);
			glm::vec2 high(-1e9F);
			for (const Point& point: blob.Points) {
				low = glm::min(low, point.Pos);
				high = glm::max(high, point.Pos);
			}
			for (int y = std::max(static_cast<int>(std::floor(low.y)) - 4, 0); y <= std::min(static_cast<int>(std::ceil(high.y)) + 4, s_Height - 1); ++y) {
				for (int rawX = static_cast<int>(std::floor(low.x)) - 4; rawX <= static_cast<int>(std::ceil(high.x)) + 4; ++rawX) {
					int x = rawX;
					if (WrapInWorld(x, y) && materialBitmap->line[y][x] == material) {
						terrain->SetMaterialPixel(x, y, g_MaterialAir);
						terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
					}
				}
			}
		}
	}

	void LoadState(const std::string& state) {
		std::istringstream stream(state);
		std::string word;
		int version = 0;
		if (!(stream >> word >> version) || word != "softbodies" || version != 1) {
			return;
		}
		int blobs = 0;
		stream >> blobs;
		for (int b = 0; b < blobs && stream; ++b) {
			Blob blob;
			int points = 0;
			stream >> blob.Type >> blob.RestArea >> blob.ShapeArea >> points;
			if (!stream || points < 3 || points > c_MaxBlobPoints || blob.Type < 0 || blob.Type >= c_TypeCount) {
				break;
			}
			for (int i = 0; i < points; ++i) {
				Point point;
				glm::vec2 shape;
				float rest = c_Spacing;
				stream >> point.Pos.x >> point.Pos.y >> point.Prev.x >> point.Prev.y >> shape.x >> shape.y >> rest;
				point.Safe = point.Pos;
				blob.Points.push_back(point);
				blob.Shape.push_back(shape);
				blob.Rest.push_back(rest);
			}
			if (stream && static_cast<int>(s_Blobs.size()) < c_MaxBlobs && PointCount() + points <= c_MaxPoints) {
				blob.Id = s_NextId++;
				s_Blobs.push_back(std::move(blob));
			}
		}
		s_Count = static_cast<int>(s_Blobs.size());
	}
} // namespace

int SoftBodySim::GetTypeCount() {
	return c_TypeCount;
}

const SoftBodySim::TypeInfo& SoftBodySim::GetType(int type) {
	return c_Types[type >= 0 && type < c_TypeCount ? type : 0].Info;
}

int SoftBodySim::FindType(const std::string& name) {
	auto lower = [](std::string text) {
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	};
	std::string wanted = lower(name);
	for (int type = 0; type < c_TypeCount; ++type) {
		if (lower(c_Types[type].Info.Name) == wanted) {
			return type;
		}
	}
	return -1;
}

int SoftBodySim::Create(int type, const Vector& center, float radius, const Vector& velocity) {
	if (!s_TablesBuilt) {
		BuildTables();
	}
	Vector at = center;
	g_SceneMan.WrapPosition(at);
	return MakeRound(type, ToGlm(at), radius, ToGlm(velocity));
}

void SoftBodySim::Clear() {
	RemoveAll(true);
}

int SoftBodySim::GetCount() {
	return s_Count;
}

void SoftBodySim::QueueBlob(int type, const Vector& center, float radius, const Vector& velocity) {
	std::scoped_lock lock(s_RequestMutex);
	Request request{Request::Kind::Create};
	request.Type = type;
	request.Position = center;
	request.Radius = radius;
	request.Velocity = velocity;
	s_Requests.push_back(request);
}

void SoftBodySim::QueueClear() {
	std::scoped_lock lock(s_RequestMutex);
	s_Requests.push_back(Request{Request::Kind::Clear});
}

void SoftBodySim::QueueBlast(const Vector& position, float reach, float energy) {
	if (s_Count == 0) {
		return;
	}
	std::scoped_lock lock(s_RequestMutex);
	Request request{Request::Kind::Blast};
	request.Position = position;
	request.Radius = reach;
	request.Energy = energy;
	s_Requests.push_back(request);
}

void SoftBodySim::Lift() {
	if (s_Blobs.empty() || g_SceneMan.GetScene() != s_Scene || g_SceneMan.GetSceneGeneration() != s_SceneGeneration) {
		return;
	}
	SLTerrain* terrain = g_SceneMan.GetScene() ? g_SceneMan.GetScene()->GetTerrain() : nullptr;
	if (!terrain) {
		return;
	}
	ReadTerrain();
	for (Blob& blob: s_Blobs) {
		Unstamp(terrain, blob, true);
	}
}

void SoftBodySim::Update() {
	Scene* scene = g_SceneMan.GetScene();
	SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
	bool loaded = false;
	if (scene != s_Scene || g_SceneMan.GetSceneGeneration() != s_SceneGeneration) {
		// A new scene: the old one's blobs are gone (with the terrain they were drawn into), and a saved game's put back.
		RemoveAll(false);
		s_Scene = scene;
		s_SceneGeneration = g_SceneMan.GetSceneGeneration();
		s_Random = 0x2545F491u;
		s_TablesBuilt = false;
		{
			std::scoped_lock lock(s_RequestMutex);
			s_Requests.clear();
		}
		if (scene && !s_PendingLoadState.empty()) {
			LoadState(s_PendingLoadState);
			loaded = true;
		}
		s_PendingLoadState.clear();
	}
	if (!terrain) {
		return;
	}
	if (!s_TablesBuilt) {
		BuildTables();
	}
	ReadTerrain();
	if (loaded) {
		ClearSavedPixels(terrain);
	}
	TakeRequests();
	if (s_Blobs.empty()) {
		s_Count = 0;
		return;
	}
	float seconds = std::max(g_TimerMan.GetDeltaTimeSecs(), 1e-4F);
	glm::vec2 gravity = ToGlm(g_SceneMan.GetGlobalAcc()) * c_PPM * seconds * seconds;
	// What was knocked out of them since they were drawn: dents, shrinking, and splits into new blobs.
	{
		std::vector<Blob> pieces;
		int reservedPoints = PointCount();
		for (Blob& blob: s_Blobs) {
			TakeDamage(blob, pieces, reservedPoints);
			if (blob.Done) {
				Erase(terrain, blob);
			}
		}
		std::erase_if(s_Blobs, [](const Blob& blob) { return blob.Done; });
		for (Blob& piece: pieces) {
			s_Blobs.push_back(std::move(piece));
		}
	}
	// All drawn in where they are, so each meets the others as ground as it moves; then each lifted, moved and drawn in again, in turn.
	for (Blob& blob: s_Blobs) {
		Stamp(terrain, blob);
	}
	for (Blob& blob: s_Blobs) {
		Unstamp(terrain, blob, false);
		Step(blob, seconds, gravity);
		Stamp(terrain, blob);
	}
	// Kept on the map: one that has gone round a wrapping map's seam is moved back a map's width, and one that has fallen off the bottom is gone.
	float sceneWidth = static_cast<float>(s_Width);
	for (Blob& blob: s_Blobs) {
		glm::vec2 middle = Middle(blob);
		if (s_WrapX && sceneWidth > 0.0F) {
			float shift = middle.x < 0.0F ? sceneWidth : (middle.x >= sceneWidth ? -sceneWidth : 0.0F);
			if (shift != 0.0F) {
				for (Point& point: blob.Points) {
					point.Pos.x += shift;
					point.Prev.x += shift;
					point.Safe.x += shift;
				}
			}
		}
		if (middle.y > static_cast<float>(s_Height) + 100.0F) {
			blob.Done = true;
		}
	}
	for (Blob& blob: s_Blobs) {
		if (blob.Done) {
			Erase(terrain, blob);
		} else {
			Finish(terrain, blob);
		}
	}
	std::erase_if(s_Blobs, [](const Blob& blob) { return blob.Done; });
	s_Count = static_cast<int>(s_Blobs.size());
}

void SoftBodySim::GetDebug(std::vector<DebugBlob>& blobs) {
	for (const Blob& blob: s_Blobs) {
		DebugBlob debug;
		for (const Point& point: blob.Points) {
			debug.Ring.push_back(point.Pos);
		}
		debug.Center = Middle(blob);
		debug.Squash = std::abs(blob.RestArea) > 1e-3F ? std::abs(SignedArea(blob)) / std::abs(blob.RestArea) : 1.0F;
		debug.Contacts = blob.Contacts;
		blobs.push_back(std::move(debug));
	}
}

std::string SoftBodySim::GetSaveState() {
	std::ostringstream stream;
	stream << "softbodies 1 " << s_Blobs.size();
	for (const Blob& blob: s_Blobs) {
		stream << ' ' << blob.Type << ' ' << blob.RestArea << ' ' << blob.ShapeArea << ' ' << blob.Points.size();
		for (size_t i = 0; i < blob.Points.size(); ++i) {
			const Point& point = blob.Points[i];
			stream << ' ' << point.Pos.x << ' ' << point.Pos.y << ' ' << point.Prev.x << ' ' << point.Prev.y << ' ' << blob.Shape[i].x << ' ' << blob.Shape[i].y << ' ' << blob.Rest[i];
		}
	}
	return stream.str();
}

void SoftBodySim::SetPendingLoadState(const std::string& state) {
	s_PendingLoadState = state;
}
