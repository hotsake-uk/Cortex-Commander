#include "RopeSim.h"
#include "ActorFire.h"
#include "AirPressure.h"
#include "Camera.h"
#include "Color.h"
#include "Constants.h"
#include "EffectsParticles.h"
#include "FluidSim.h"
#include "FrameMan.h"
#include "LightingSettings.h"
#include "MOSprite.h"
#include "Material.h"
#include "MovableMan.h"
#include "MovableObject.h"
#include "PostProcessMan.h"
#include "SLTerrain.h"
#include "Scene.h"
#include "SceneMan.h"
#include "Shapes.h"
#include "TerrainCollapse.h"
#include "TerrainFire.h"
#include "TimerMan.h"
#include "Vector.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <unordered_map>

using namespace RTE;

namespace {
	/// How a kind of rope is drawn.
	enum class Look {
		Twisted, //!< Two pixels wide, its strands twisting along it in two shades: rope.
		Thread, //!< One pixel, pale.
		Chain, //!< Links, face on and edge on by turns.
		Cable, //!< One pixel of steel, a glint every few pixels along the braid.
		Banded //!< Two pixels wide, dark with flecks of colour: a bungee cord.
	};

	/// What a kind of rope is made of and how it behaves.
	struct TypeData {
		RopeSim::TypeInfo Info;
		float Segment; //!< How far apart its points are, in pixels: the length of a chain's link.
		float NodeMass; //!< The weight of a point's length of it, in kg: what hangs from a unit when it is tied to one.
		float StrengthKg; //!< What it holds hanging from it before it snaps, in kg.
		float Stiffness; //!< 1 doesn't stretch; lower stretches like elastic (each pull is taken up a part at a time).
		float MaxStretch; //!< An elastic one: how far past its length it stretches before it holds hard, as a share of that length.
		float Keep; //!< How much of its points' speed is kept each update, against the air.
		float WindCatch; //!< How quickly the wind carries it along, per second.
		float Float; //!< In liquid: its lift against its weight (1 floats level, more floats up, 0 sinks).
		bool Burns;
		float BurnSeconds; //!< How long fire takes to burn through a point of it.
		float SpreadSeconds; //!< How long fire on a point takes to reach the next.
		float CutPower; //!< What a bullet takes to cut it: its mass times speed times sharpness.
		float BlastCut; //!< How close to a blast, as a share of its reach, it is cut.
		float Hardness; //!< 0 fibre to 1 metal: what flies off a bullet hitting it.
		Look Style;
		std::array<unsigned char, 3> Light; //!< Its colours, lighter and darker.
		std::array<unsigned char, 3> Dark;
	};

	// Appended to only, so saved games keep their kinds.
	const TypeData c_Types[] = {
	    {{"Rope", "Hemp rope: holds a few soldiers' weight, sways in the wind, burns, and a rifle bullet can cut it.", 176, 138, 88}, 4.0F, 0.04F, 600.0F, 1.0F, 0.0F, 0.995F, 0.6F, 1.1F, true, 4.0F, 0.9F, 150.0F, 0.6F, 0.0F, Look::Twisted, {182, 144, 92}, {120, 90, 54}},
	    {{"Thread", "Thin string: weighs next to nothing and blows about, holds a gun or a grenade but not a unit, any bullet cuts it and fire runs along it fast.", 226, 220, 200}, 4.0F, 0.004F, 12.0F, 1.0F, 0.0F, 0.99F, 2.0F, 1.2F, true, 0.8F, 0.15F, 3.0F, 0.9F, 0.0F, Look::Thread, {228, 222, 204}, {196, 188, 166}},
	    {{"Chain", "Heavy steel links: hangs straight and heavy, holds anything, doesn't burn, and only a blast close to it breaks it.", 160, 164, 170}, 3.0F, 0.35F, 5000.0F, 1.0F, 0.0F, 0.998F, 0.04F, 0.0F, false, 0.0F, 0.0F, 4000.0F, 0.18F, 1.0F, Look::Chain, {196, 200, 206}, {92, 95, 102}},
	    {{"Steel cable", "Braided steel wire: thin and light for its strength, holds a dropship's worth, doesn't burn, and takes a heavy round to cut.", 168, 174, 182}, 4.0F, 0.08F, 2500.0F, 1.0F, 0.0F, 0.997F, 0.15F, 0.1F, false, 0.0F, 0.0F, 1500.0F, 0.35F, 0.8F, Look::Cable, {200, 206, 214}, {118, 122, 130}},
	    {{"Bungee cord", "Elastic cord: stretches to three times its length and springs back, so what hangs from it bounces. Burns.", 70, 70, 76}, 4.0F, 0.03F, 400.0F, 0.06F, 2.0F, 0.995F, 0.4F, 1.05F, true, 3.0F, 0.8F, 120.0F, 0.6F, 0.0F, Look::Banded, {52, 52, 58}, {200, 70, 60}}};
	constexpr int c_TypeCount = static_cast<int>(std::size(c_Types));

	constexpr int c_MaxRopes = 400;
	constexpr int c_MaxNodes = 40000; //!< All the ropes' points together.
	constexpr int c_MaxRopeNodes = 4000; //!< One rope's.
	constexpr int c_Iterations = 20; //!< Passes of the length constraints each update.
	constexpr float c_HashCell = 16.0F; //!< The grid bullets and flames are tested against the links in.

	struct Node {
		glm::vec2 Pos;
		glm::vec2 Prev;
		glm::vec2 Safe; //!< The last place it was out of the ground.
		float Burn = 0.0F; //!< How long it has burnt; 0 when not alight.
		float Char = 0.0F; //!< How burnt it is, 0 to 1, as drawn.
		short Anchor = -1; //!< Its tie in Rope::Anchors, -1 for none.
		bool Grounded = false; //!< Lying on or against the ground this update.
		bool Sheltered = false; //!< In the lee of the ground, out of the wind.
		bool Gone = false; //!< Burnt away.
	};

	struct Link {
		float Rest; //!< Its length, slack.
		bool Cut = false;
		float Load = 0.0F; //!< 0 slack to 1 snapping, for the debug overlay.
	};

	struct Anchor {
		int Node = 0;
		bool Object = false; //!< Tied to a unit or a thing, else to the ground (or a loose piece of it).
		bool Piece = false; //!< Tied to a loose piece of terrain that is falling or tumbling (Object is false).
		glm::ivec2 Pixel{0, 0}; //!< The ground: the pixel of ground holding it (wrapped).
		long ObjectID = 0; //!< A unit or thing: its unique ID. A loose piece: its ID (TerrainCollapse::FindPiece).
		Vector Local; //!< A unit, thing or loose piece: where on it, from its middle, as it is upright and unflipped (a piece: unturned).
		float Tension = 0.0F; //!< How hard it's pulled, in kg, smoothed over a few updates.
	};

	struct Rope {
		int Id = 0;
		int Type = 0;
		float Slack = 0.1F;
		int Points = 0; //!< How many points it was put down with.
		std::vector<Node> Nodes;
		std::vector<Link> Links; //!< Links[i] joins Nodes[i] and Nodes[i + 1].
		std::vector<Anchor> Anchors;
		float StrengthMult = 1.0F; //!< Scales what the kind holds before it snaps.
		float AnchorKg = 0.0F; //!< How hard a tie can be pulled before it lets go, in kg; 0 never.
		float StillSeconds = 0.0F; //!< How long it has lain still tied to nothing.
	};

	/// A change asked for from a script or another thread, made at the next update in the order asked.
	struct Request {
		enum class Kind { Create, AddPoint, Remove, Cut, Ignite, Blast } What;
		int Id = 0;
		int Type = 0;
		float Slack = 0.0F;
		float Strength = 1.0F;
		float AnchorKg = 0.0F;
		std::vector<Vector> Points;
		Vector Position;
		float Radius = 0.0F;
		float Energy = 0.0F;
	};

	std::vector<Rope> s_Ropes;
	std::atomic<int> s_NextId{1};
	std::atomic<int> s_Count{0};
	std::mutex s_RequestMutex;
	std::vector<Request> s_Requests;
	std::string s_PendingLoadState;
	const Scene* s_Scene = nullptr;
	unsigned int s_SceneGeneration = 0;
	unsigned int s_Random = 0x9E3779B9u;
	std::array<unsigned char, 256> s_Passable{}; //!< By material: what a rope goes through (air, liquids, leaves and grass).
	std::array<unsigned char, 256> s_Liquid{};
	std::array<unsigned char, 256> s_Hot{}; //!< Lava: sets what burns alight.
	std::array<unsigned char, 256> s_Douses{}; //!< Puts fire out.
	bool s_TablesBuilt = false;
	float s_Reaction = 1.0F; //!< How strongly ropes react to wind, blast waves and explosions.
	float s_SettleSeconds = 5.0F; //!< How long a rope tied to nothing lies still before it becomes terrain; 0 never.

	float Random01() {
		s_Random ^= s_Random << 13;
		s_Random ^= s_Random >> 17;
		s_Random ^= s_Random << 5;
		return static_cast<float>(s_Random & 0xFFFFFF) / static_cast<float>(0x1000000);
	}

	const TypeData& TypeOf(const Rope& rope) { return c_Types[std::clamp(rope.Type, 0, c_TypeCount - 1)]; }

	void BuildTables() {
		for (int id = 0; id < 256; ++id) {
			const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
			bool liquid = id != g_MaterialAir && FluidSim::IsLiquid(id);
			s_Liquid[id] = liquid ? 1 : 0;
			s_Passable[id] = (id == g_MaterialAir || liquid || FluidSim::LetsLiquidsThrough(id)) ? 1 : 0;
			s_Douses[id] = TerrainFire::IsDousing(id) ? 1 : 0;
			s_Hot[id] = (material && id != g_MaterialAir && material->GetPresetName().find("Lava") != std::string::npos) ? 1 : 0;
		}
		s_TablesBuilt = true;
	}

	int MaterialAt(float x, float y) { return g_SceneMan.GetTerrMatter(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))); }

	bool Solid(float x, float y) { return !s_Passable[MaterialAt(x, y)]; }

	bool Solid(const glm::vec2& at) { return Solid(at.x, at.y); }

	glm::vec2 ToGlm(const Vector& vector) { return glm::vec2(vector.m_X, vector.m_Y); }

	Vector ToVector(const glm::vec2& vector) { return Vector(vector.x, vector.y); }

	/// The way from one point to another, the short way round a map that wraps.
	glm::vec2 Between(const glm::vec2& from, const glm::vec2& to) { return ToGlm(g_SceneMan.ShortestDistance(ToVector(from), ToVector(to), g_SceneMan.SceneWrapsX())); }

	int NodeCount() {
		int count = 0;
		for (const Rope& rope: s_Ropes) {
			count += static_cast<int>(rope.Nodes.size());
		}
		return count;
	}

	Rope* Find(int id) {
		for (Rope& rope: s_Ropes) {
			if (rope.Id == id) {
				return &rope;
			}
		}
		return nullptr;
	}

	MovableObject* ObjectOf(const Anchor& anchor) {
		MovableObject* object = anchor.Object ? g_MovableMan.FindObjectByUniqueID(anchor.ObjectID) : nullptr;
		return object && !object->IsSetToDelete() ? object : nullptr;
	}

	/// Where on a unit or thing a tie is now, as it has moved, turned and faced about since.
	Vector ObjectPoint(const MovableObject& object, const Vector& local) {
		const MOSprite* sprite = dynamic_cast<const MOSprite*>(&object);
		return object.GetPos() + (sprite ? sprite->RotateOffset(local) : local);
	}

	/// Ties a new point to what is at a place: a unit or a thing there or right beside it, else the ground, else nothing.
	/// @return Whether it is tied to anything.
	bool TieAt(Anchor& anchor, const Vector& at) {
		anchor.Piece = false;
		int x = at.GetFloorIntX();
		int y = at.GetFloorIntY();
		for (int reach = 0; reach <= 4; ++reach) {
			// Rings out from the point: what's at the point itself first, a unit before the ground at each distance.
			for (int pass = 0; pass < 2; ++pass) {
				for (int dy = -reach; dy <= reach; ++dy) {
					for (int dx = -reach; dx <= reach; ++dx) {
						if (std::max(std::abs(dx), std::abs(dy)) != reach) {
							continue;
						}
						if (pass == 0 && reach <= 2) {
							MOID id = g_SceneMan.GetMOIDPixel(x + dx, y + dy);
							MovableObject* object = id != g_NoMOID ? g_MovableMan.GetMOFromID(id) : nullptr;
							if (object && !object->IsSetToDelete()) {
								const MOSprite* sprite = dynamic_cast<const MOSprite*>(object);
								Vector offset = g_SceneMan.ShortestDistance(object->GetPos(), at, g_SceneMan.SceneWrapsX());
								anchor.Object = true;
								anchor.ObjectID = object->GetUniqueID();
								anchor.Local = sprite ? sprite->UnRotateOffset(offset) : offset;
								return true;
							}
						} else if (pass == 1 && Solid(static_cast<float>(x + dx), static_cast<float>(y + dy))) {
							int px = x + dx;
							int py = y + dy;
							g_SceneMan.WrapPosition(px, py);
							anchor.Object = false;
							// Ground that is falling or tumbling as a loose piece is tied to as that piece, which it then follows.
							Vector onPiece(static_cast<float>(px) + 0.5F, static_cast<float>(py) + 0.5F);
							if (TerrainCollapse::FindPiece(px, py, onPiece, anchor.ObjectID, anchor.Local)) {
								anchor.Piece = true;
								return true;
							}
							anchor.Pixel = glm::ivec2(px, py);
							return true;
						}
					}
				}
			}
		}
		return false;
	}

	void AddTie(Rope& rope, int node, const Vector& at) {
		Anchor anchor;
		anchor.Node = node;
		if (TieAt(anchor, at)) {
			rope.Nodes[node].Anchor = static_cast<short>(rope.Anchors.size());
			rope.Anchors.push_back(anchor);
		}
	}

	void DropAnchor(Rope& rope, int index) {
		rope.Nodes[rope.Anchors[index].Node].Anchor = -1;
		rope.Anchors.erase(rope.Anchors.begin() + index);
		for (size_t i = static_cast<size_t>(index); i < rope.Anchors.size(); ++i) {
			rope.Nodes[rope.Anchors[i].Node].Anchor = static_cast<short>(i);
		}
	}

	Node MakeNode(const glm::vec2& at) {
		Node node;
		node.Pos = at;
		node.Prev = at;
		node.Safe = at;
		return node;
	}

	int CreateRope(int id, int type, float slack, const Vector& at, float strength = 1.0F, float anchorKg = 0.0F) {
		if (!g_SceneMan.GetScene() || static_cast<int>(s_Ropes.size()) >= c_MaxRopes || NodeCount() >= c_MaxNodes) {
			return 0;
		}
		if (!s_TablesBuilt) {
			BuildTables();
		}
		Rope rope;
		rope.Id = id;
		rope.Type = std::clamp(type, 0, c_TypeCount - 1);
		rope.Slack = std::clamp(slack, 0.0F, 1.0F);
		rope.Points = 1;
		rope.StrengthMult = std::clamp(strength, 0.05F, 1000.0F);
		rope.AnchorKg = std::clamp(anchorKg, 0.0F, 1e6F);
		Vector start = at;
		g_SceneMan.WrapPosition(start);
		rope.Nodes.push_back(MakeNode(ToGlm(start)));
		AddTie(rope, 0, start);
		s_Ropes.push_back(std::move(rope));
		s_Count = static_cast<int>(s_Ropes.size());
		return id;
	}

	bool AddRopePoint(int id, const Vector& at) {
		Rope* rope = Find(id);
		if (!rope || rope->Nodes.empty()) {
			return false;
		}
		const TypeData& type = TypeOf(*rope);
		glm::vec2 from = rope->Nodes.back().Pos;
		glm::vec2 way = Between(from, ToGlm(at));
		float distance = glm::length(way);
		if (distance < 1.0F) {
			return true;
		}
		float rest = distance * (1.0F + rope->Slack);
		int count = std::max(1, static_cast<int>(std::round(rest / type.Segment)));
		if (static_cast<int>(rope->Nodes.size()) + count > c_MaxRopeNodes || NodeCount() + count > c_MaxNodes) {
			return false;
		}
		for (int i = 1; i <= count; ++i) {
			rope->Links.push_back({rest / static_cast<float>(count)});
			rope->Nodes.push_back(MakeNode(from + way * (static_cast<float>(i) / static_cast<float>(count))));
		}
		++rope->Points;
		AddTie(*rope, static_cast<int>(rope->Nodes.size()) - 1, at);
		return true;
	}

	void RemoveRope(int id) {
		s_Ropes.erase(std::remove_if(s_Ropes.begin(), s_Ropes.end(), [id](const Rope& rope) { return rope.Id == id; }), s_Ropes.end());
		s_Count = static_cast<int>(s_Ropes.size());
	}

	void Ignite(Node& node, const TypeData& type) {
		if (type.Burns && !node.Gone && node.Burn <= 0.0F) {
			node.Burn = 0.0001F;
		}
	}

	/// The nearest distance between two segments.
	float SegmentDistance(const glm::vec2& p1, const glm::vec2& q1, const glm::vec2& p2, const glm::vec2& q2) {
		glm::vec2 d1 = q1 - p1;
		glm::vec2 d2 = q2 - p2;
		glm::vec2 r = p1 - p2;
		float a = glm::dot(d1, d1);
		float e = glm::dot(d2, d2);
		float f = glm::dot(d2, r);
		float s = 0.0F;
		float t = 0.0F;
		if (a <= 1e-6F && e <= 1e-6F) {
			return glm::length(r);
		}
		if (a <= 1e-6F) {
			t = std::clamp(f / e, 0.0F, 1.0F);
		} else {
			float c = glm::dot(d1, r);
			if (e <= 1e-6F) {
				s = std::clamp(-c / a, 0.0F, 1.0F);
			} else {
				float b = glm::dot(d1, d2);
				float denominator = a * e - b * b;
				s = denominator > 1e-6F ? std::clamp((b * f - c * e) / denominator, 0.0F, 1.0F) : 0.0F;
				t = (b * s + f) / e;
				if (t < 0.0F) {
					t = 0.0F;
					s = std::clamp(-c / a, 0.0F, 1.0F);
				} else if (t > 1.0F) {
					t = 1.0F;
					s = std::clamp((b - c) / a, 0.0F, 1.0F);
				}
			}
		}
		return glm::length((p1 + d1 * s) - (p2 + d2 * t));
	}

	/// Cuts the links within a circle (their nearest point to its middle).
	void CutNear(const glm::vec2& at, float radius) {
		for (Rope& rope: s_Ropes) {
			for (size_t i = 0; i < rope.Links.size(); ++i) {
				if (rope.Links[i].Cut) {
					continue;
				}
				glm::vec2 a = at + Between(at, rope.Nodes[i].Pos);
				glm::vec2 b = a + (rope.Nodes[i + 1].Pos - rope.Nodes[i].Pos);
				if (SegmentDistance(at, at, a, b) <= radius) {
					rope.Links[i].Cut = true;
				}
			}
		}
	}

	void Blast(const glm::vec2& at, float reach, float energy) {
		float push = std::clamp(std::sqrt(std::max(energy, 0.0F)) * 0.05F, 1.0F, 12.0F) * s_Reaction;
		for (Rope& rope: s_Ropes) {
			const TypeData& type = TypeOf(rope);
			for (size_t i = 0; i < rope.Nodes.size(); ++i) {
				Node& node = rope.Nodes[i];
				glm::vec2 away = Between(at, node.Pos);
				float distance = glm::length(away);
				if (distance >= reach) {
					continue;
				}
				float closeness = 1.0F - distance / reach;
				if (node.Anchor < 0 && distance > 0.01F) {
					// Thrown out from it, the lighter kinds further.
					node.Prev -= away / distance * push * closeness / std::max(std::sqrt(type.NodeMass * 25.0F), 0.5F);
				}
				if (distance < reach * 0.3F && Random01() < 0.5F) {
					Ignite(node, type);
				}
				if (i < rope.Links.size() && distance < reach * type.BlastCut) {
					rope.Links[i].Cut = true;
				}
			}
		}
	}

	void IgniteArea(const glm::vec2& at, float radius) {
		for (Rope& rope: s_Ropes) {
			const TypeData& type = TypeOf(rope);
			if (!type.Burns) {
				continue;
			}
			for (Node& node: rope.Nodes) {
				if (glm::length(Between(at, node.Pos)) <= radius && Random01() < 0.7F) {
					Ignite(node, type);
				}
			}
		}
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
					if (!request.Points.empty() && CreateRope(request.Id, request.Type, request.Slack, request.Points.front(), request.Strength, request.AnchorKg) != 0) {
						for (size_t i = 1; i < request.Points.size(); ++i) {
							AddRopePoint(request.Id, request.Points[i]);
						}
					}
					break;
				case Request::Kind::AddPoint:
					AddRopePoint(request.Id, request.Position);
					break;
				case Request::Kind::Remove:
					RemoveRope(request.Id);
					break;
				case Request::Kind::Cut:
					CutNear(ToGlm(request.Position), request.Radius);
					break;
				case Request::Kind::Ignite:
					IgniteArea(ToGlm(request.Position), request.Radius);
					break;
				case Request::Kind::Blast:
					Blast(ToGlm(request.Position), request.Radius, request.Energy);
					break;
			}
		}
	}

	void LoadState(const std::string& state) {
		std::istringstream stream(state);
		std::string word;
		int version = 0;
		if (!(stream >> word >> version) || word != "ropes" || (version != 1 && version != 2)) {
			return;
		}
		int ropes = 0;
		stream >> ropes;
		for (int r = 0; r < ropes && stream; ++r) {
			Rope rope;
			int nodes = 0;
			int anchors = 0;
			stream >> rope.Type >> rope.Slack >> rope.Points >> nodes;
			if (version >= 2) {
				stream >> rope.StrengthMult >> rope.AnchorKg;
			}
			if (!stream || nodes < 1 || nodes > c_MaxRopeNodes || rope.Type < 0 || rope.Type >= c_TypeCount) {
				break;
			}
			for (int i = 0; i < nodes; ++i) {
				glm::vec2 at;
				int gone = 0;
				stream >> at.x >> at.y >> gone;
				Node node = MakeNode(at);
				node.Gone = gone != 0;
				node.Char = node.Gone ? 1.0F : 0.0F;
				rope.Nodes.push_back(node);
			}
			for (int i = 0; i + 1 < nodes; ++i) {
				Link link{4.0F};
				int cut = 0;
				stream >> link.Rest >> cut;
				link.Cut = cut != 0;
				rope.Links.push_back(link);
			}
			stream >> anchors;
			for (int i = 0; i < anchors && stream; ++i) {
				Anchor anchor;
				stream >> anchor.Node >> anchor.Pixel.x >> anchor.Pixel.y;
				if (anchor.Node >= 0 && anchor.Node < nodes) {
					rope.Nodes[anchor.Node].Anchor = static_cast<short>(rope.Anchors.size());
					rope.Anchors.push_back(anchor);
				}
			}
			if (stream && static_cast<int>(s_Ropes.size()) < c_MaxRopes) {
				rope.Id = s_NextId++;
				s_Ropes.push_back(std::move(rope));
			}
		}
		s_Count = static_cast<int>(s_Ropes.size());
	}

	/// Keeps a tie's point where its unit or thing is now, or lets it go: a unit or thing gone, or the ground it was in dug, burnt or blown away.
	void UpdateAnchors(Rope& rope, bool checkGround) {
		for (int i = static_cast<int>(rope.Anchors.size()) - 1; i >= 0; --i) {
			Anchor& anchor = rope.Anchors[i];
			Node& node = rope.Nodes[anchor.Node];
			if (node.Gone) {
				DropAnchor(rope, i);
				continue;
			}
			if (anchor.Object) {
				MovableObject* object = ObjectOf(anchor);
				if (!object) {
					DropAnchor(rope, i);
					continue;
				}
				glm::vec2 point = ToGlm(ObjectPoint(*object, anchor.Local));
				// Kept on the side of the map the rope is on.
				node.Prev = node.Pos;
				node.Pos = node.Pos + Between(node.Pos, point);
				node.Safe = node.Pos;
			} else if (anchor.Piece) {
				Vector point;
				Vector velocity;
				float mass = 0.0F;
				if (TerrainCollapse::GetPiece(anchor.ObjectID, anchor.Local, point, velocity, mass)) {
					node.Prev = node.Pos;
					node.Pos = node.Pos + Between(node.Pos, ToGlm(point));
					node.Safe = node.Pos;
				} else if (!TieAt(anchor, ToVector(node.Pos))) {
					// It has come to rest (and is ground again, which TieAt finds) or broken up and left nothing there.
					DropAnchor(rope, i);
				}
			} else if (checkGround && !Solid(static_cast<float>(anchor.Pixel.x), static_cast<float>(anchor.Pixel.y))) {
				DropAnchor(rope, i);
			} else {
				node.Prev = node.Pos;
			}
		}
	}

	/// Turns a rope that is tied to nothing and has lain still for a while into terrain: its links drawn into the ground as pixels of wood (steel
	/// for the metal kinds) in its own colours, wherever there is air.
	/// @return Whether it settled, and is to be taken away.
	bool Settle(Rope& rope, float seconds) {
		if (s_SettleSeconds <= 0.0F || !rope.Anchors.empty()) {
			rope.StillSeconds = 0.0F;
			return false;
		}
		float fastest = 0.0F;
		for (const Node& node: rope.Nodes) {
			if (node.Gone) {
				continue;
			}
			// A burning rope burns on, one in liquid floats or sinks on.
			if (node.Burn > 0.0F || s_Liquid[MaterialAt(node.Pos.x, node.Pos.y)]) {
				rope.StillSeconds = 0.0F;
				return false;
			}
			fastest = std::max(fastest, glm::length(node.Pos - node.Prev));
		}
		if (fastest > 0.4F) {
			rope.StillSeconds = 0.0F;
			return false;
		}
		rope.StillSeconds += seconds;
		SLTerrain* terrain = g_SceneMan.GetScene() ? g_SceneMan.GetScene()->GetTerrain() : nullptr;
		if (rope.StillSeconds < s_SettleSeconds || !terrain) {
			return false;
		}
		const TypeData& type = TypeOf(rope);
		const Material* material = g_SceneMan.GetMaterial(type.Burns ? "Wood" : "Metal");
		if (!material) {
			return false;
		}
		const PALETTE& palette = g_FrameMan.GetDefaultPalette();
		int light = bestfit_color(palette, type.Light[0], type.Light[1], type.Light[2]);
		int dark = bestfit_color(palette, type.Dark[0], type.Dark[1], type.Dark[2]);
		auto lay = [&](int x, int y, int color) {
			if (g_SceneMan.WrapPosition(x, y) && terrain->GetMaterialPixel(x, y) == g_MaterialAir) {
				terrain->SetMaterialPixel(x, y, material->GetIndex());
				terrain->SetFGColorPixel(x, y, color);
			}
		};
		bool wide = type.Style == Look::Twisted || type.Style == Look::Banded;
		float along = 0.0F;
		for (size_t i = 0; i < rope.Links.size(); ++i) {
			const Node& a = rope.Nodes[i];
			const Node& b = rope.Nodes[i + 1];
			glm::vec2 way = b.Pos - a.Pos;
			float length = glm::length(way);
			if (!rope.Links[i].Cut && !a.Gone && !b.Gone) {
				int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::abs(way.x), std::abs(way.y)))));
				glm::ivec2 across = std::abs(way.x) >= std::abs(way.y) ? glm::ivec2(0, 1) : glm::ivec2(1, 0);
				for (int step = 0; step < steps; ++step) {
					glm::vec2 at = a.Pos + way * (static_cast<float>(step) / static_cast<float>(steps));
					int x = static_cast<int>(std::floor(at.x));
					int y = static_cast<int>(std::floor(at.y));
					bool strand = static_cast<int>(std::floor((along + length * static_cast<float>(step) / static_cast<float>(steps)) * 0.5F)) % 2 == 0;
					lay(x, y, strand ? light : dark);
					if (wide) {
						lay(x + across.x, y + across.y, strand ? dark : light);
					}
				}
			}
			along += length;
		}
		return true;
	}

	/// Moves the loose points on: their speed kept, less the air's drag, and gravity, the wind, blast waves and liquid on them.
	void Integrate(Rope& rope, float seconds, const glm::vec2& gravity, float wind, long long update) {
		const TypeData& type = TypeOf(rope);
		float time = static_cast<float>(update) * seconds;
		for (size_t i = 0; i < rope.Nodes.size(); ++i) {
			Node& node = rope.Nodes[i];
			if (node.Anchor >= 0) {
				continue;
			}
			glm::vec2 velocity = (node.Pos - node.Prev) * type.Keep;
			glm::vec2 accel = gravity;
			int material = MaterialAt(node.Pos.x, node.Pos.y);
			if (s_Liquid[material]) {
				// Held up as much as it floats, and dragged hard.
				velocity *= 0.85F;
				accel -= gravity * type.Float;
				if (node.Burn > 0.0F && s_Douses[material]) {
					node.Burn = 0.0F;
				}
			} else if (wind != 0.0F && s_Reaction > 0.0F) {
				if ((update + static_cast<long long>(i)) % 4 == 0) {
					node.Sheltered = AirPressure::IsSheltered(ToVector(node.Pos), wind);
				}
				if (!node.Sheltered) {
					// Carried along at the wind's speed, and turning over in it a little.
					float speedX = velocity.x / std::max(seconds, 1e-4F);
					accel.x += (wind - speedX) * type.WindCatch * s_Reaction;
					accel.y += std::sin(node.Pos.x * 0.03F + node.Pos.y * 0.05F + time) * std::abs(wind) * 0.15F * type.WindCatch * s_Reaction;
				}
			}
			if (s_Hot[material]) {
				Ignite(node, type);
			}
			node.Prev = node.Pos;
			node.Pos += velocity + accel * seconds * seconds;
			if (glm::vec2 push = ToGlm(AirPressure::GetPush(ToVector(node.Pos))); push.x != 0.0F || push.y != 0.0F) {
				node.Pos += push * c_PPM * seconds * std::clamp(type.WindCatch, 0.2F, 1.0F) * s_Reaction;
			}
		}
	}

	/// Holds each link to no longer than its length (a rope goes slack, it doesn't push), the ties fixed and the loose points shared between.
	void Solve(Rope& rope) {
		const TypeData& type = TypeOf(rope);
		size_t links = rope.Links.size();
		for (int pass = 0; pass < c_Iterations; ++pass) {
			bool forward = pass % 2 == 0;
			for (size_t step = 0; step < links; ++step) {
				size_t i = forward ? step : links - 1 - step;
				Link& link = rope.Links[i];
				if (link.Cut) {
					continue;
				}
				Node& a = rope.Nodes[i];
				Node& b = rope.Nodes[i + 1];
				float wa = a.Anchor >= 0 ? 0.0F : 1.0F;
				float wb = b.Anchor >= 0 ? 0.0F : 1.0F;
				if (wa + wb <= 0.0F) {
					continue;
				}
				glm::vec2 way = b.Pos - a.Pos;
				float length = glm::length(way);
				if (length <= link.Rest || length < 1e-4F) {
					continue;
				}
				// An elastic one gives, up to a point; past it, it holds hard.
				float stiffness = length > link.Rest * (1.0F + type.MaxStretch) ? 1.0F : type.Stiffness;
				glm::vec2 correction = way * ((length - link.Rest) / length * stiffness / (wa + wb));
				a.Pos += correction * wa;
				b.Pos -= correction * wb;
			}
		}
	}

	/// Keeps the loose points out of the ground: slid along it, held back by it.
	void Collide(Rope& rope) {
		for (Node& node: rope.Nodes) {
			if (node.Anchor >= 0) {
				continue;
			}
			bool hit = false;
			if (Solid(node.Pos)) {
				hit = true;
				glm::vec2 alongX(node.Pos.x, node.Safe.y);
				glm::vec2 alongY(node.Safe.x, node.Pos.y);
				if (!Solid(alongX)) {
					node.Pos = alongX;
				} else if (!Solid(alongY)) {
					node.Pos = alongY;
				} else if (!Solid(node.Safe)) {
					node.Pos = node.Safe;
				}
				// Friction: most of its speed along the ground is lost.
				node.Prev = node.Pos - (node.Pos - node.Prev) * 0.3F;
			}
			if (!Solid(node.Pos)) {
				node.Safe = node.Pos;
			}
			node.Grounded = hit || Solid(node.Pos.x, node.Pos.y + 1.0F);
		}
	}

	/// What a tie is fastened to, as far as pulling on it goes.
	struct Fastened {
		MovableObject* Object = nullptr; //!< The unit or thing tied to.
		MovableObject* Root = nullptr; //!< The unit or the whole thing it is a part of.
		bool Piece = false; //!< A loose piece of terrain.
		long PieceID = 0;
		bool Movable = false; //!< Whether pulling moves it: not a pinned thing, nor something with no weight.
		float Mass = 0.0F;
		Vector Vel; //!< In m/s.
	};

	Fastened FastenedTo(const Anchor& anchor) {
		Fastened fastened;
		if (anchor.Object) {
			fastened.Object = ObjectOf(anchor);
			fastened.Root = fastened.Object ? fastened.Object->GetRootParent() : nullptr;
			if (fastened.Root && fastened.Root->GetPinStrength() <= 0.0F && fastened.Root->GetMass() > 0.0F) {
				fastened.Movable = true;
				fastened.Mass = fastened.Root->GetMass();
				fastened.Vel = fastened.Root->GetVel();
			}
		} else if (anchor.Piece) {
			Vector point;
			if (TerrainCollapse::GetPiece(anchor.ObjectID, anchor.Local, point, fastened.Vel, fastened.Mass) && fastened.Mass > 0.0F) {
				fastened.Piece = true;
				fastened.PieceID = anchor.ObjectID;
				fastened.Movable = true;
			}
		}
		return fastened;
	}

	/// Pulls each unit and thing a rope is tied to: once a stretch of rope from it to the next tie is taut, what moves it further away along
	/// the rope is stopped, as the grapple gun's line stops its user; a loose end hanging from it hangs its weight on it. Snaps the rope where
	/// it is pulled harder than it holds.
	void Pull(Rope& rope, float seconds, float gravity) {
		const TypeData& type = TypeOf(rope);
		const float holds = type.StrengthKg * rope.StrengthMult;
		for (Link& link: rope.Links) {
			link.Load = 0.0F;
		}
		for (size_t index = 0; index < rope.Anchors.size(); ++index) {
			Anchor& anchor = rope.Anchors[index];
			Fastened self = FastenedTo(anchor);
			float tension = 0.0F;
			int worstLink = -1;
			for (int direction: {-1, 1}) {
				int node = anchor.Node;
				int first = direction < 0 ? node - 1 : node;
				if (first < 0 || first >= static_cast<int>(rope.Links.size()) || rope.Links[first].Cut) {
					continue;
				}
				// Along the rope to the next tie, or to a cut or its end.
				float length = 0.0F;
				float rest = 0.0F;
				float hanging = 0.0F;
				float stretchiest = -1.0F;
				int stretchiestLink = first;
				int other = -1;
				int at = node;
				while (true) {
					int linkIndex = direction < 0 ? at - 1 : at;
					if (linkIndex < 0 || linkIndex >= static_cast<int>(rope.Links.size()) || rope.Links[linkIndex].Cut) {
						break;
					}
					const Link& link = rope.Links[linkIndex];
					at += direction;
					float span = glm::length(rope.Nodes[at].Pos - rope.Nodes[at - direction].Pos);
					length += span;
					rest += link.Rest;
					if (span / link.Rest > stretchiest) {
						stretchiest = span / link.Rest;
						stretchiestLink = linkIndex;
					}
					if (rope.Nodes[at].Anchor >= 0) {
						other = rope.Nodes[at].Anchor;
						break;
					}
					if (!rope.Nodes[at].Grounded) {
						hanging += type.NodeMass;
					}
				}
				float excess = length - rest;
				float load = 0.0F;
				if (other < 0) {
					// A loose end: only its weight, where it hangs.
					load = hanging;
				} else if (excess > 0.0F) {
					load = (excess / std::max(rest, 1.0F)) * 10.0F;
				}
				if (self.Movable) {
					glm::vec2 toward = rope.Nodes[node + direction].Pos - rope.Nodes[node].Pos;
					float distance = glm::length(toward);
					if (distance > 1e-4F) {
						Vector way = ToVector(toward / distance);
						float massA = self.Mass;
						float force = 0.0F;
						if (other < 0) {
							force = hanging * gravity;
						} else if (excess > 0.0F) {
							const Anchor& end = rope.Anchors[other];
							Fastened endTie = FastenedTo(end);
							bool sameBody = (self.Root && endTie.Root == self.Root) || (self.Piece && endTie.Piece && endTie.PieceID == self.PieceID);
							if (!sameBody) {
								bool endMoves = endTie.Movable;
								float share = endMoves ? endTie.Mass / (massA + endTie.Mass) : 1.0F;
								Vector relative = self.Vel - (endMoves ? endTie.Vel : Vector());
								float away = -relative.Dot(way);
								if (type.Stiffness >= 0.5F || excess > rest * type.MaxStretch) {
									// Stopped going further, and drawn back to its length a little at a time.
									float overBy = type.Stiffness >= 0.5F ? excess : excess - rest * type.MaxStretch;
									float change = std::min(std::max(away, 0.0F) + overBy * c_MPP * 0.25F / seconds, 40.0F);
									force = massA * change * share / seconds;
								}
								if (type.Stiffness < 0.5F) {
									// An elastic one pulls harder the further it's stretched, its bounce damped a little.
									float stretch = std::min(excess, rest * type.MaxStretch) / std::max(rest * type.MaxStretch, 1.0F);
									force += (stretch * type.StrengthKg * 0.5F * gravity + std::max(away, 0.0F) * massA * 0.5F) * share;
								}
							}
						}
						if (force > 0.0F) {
							// A unit is pulled at its middle (pulled at a hand or a foot it spun); a thing at the point it's tied.
							if (self.Piece) {
								// A loose piece is pulled where it's tied, and turns if that is off its middle.
								TerrainCollapse::PullPiece(self.PieceID, ToVector(rope.Nodes[anchor.Node].Pos), Vector(way.m_X, way.m_Y) * (force * seconds / massA));
							} else if (self.Root->IsActor()) {
								self.Root->AddForce(way * force);
							} else {
								self.Root->AddAbsForce(way * force, ObjectPoint(*self.Object, anchor.Local));
							}
							load = force / std::max(gravity, 1.0F);
						}
					}
				}
				if (load > tension) {
					tension = load;
					worstLink = stretchiestLink;
				}
				// The stretch's links show how hard it's pulled, on the debug overlay.
				float share = std::clamp(load / holds, 0.0F, 1.0F);
				for (int link = first, count = 0; link >= 0 && link < static_cast<int>(rope.Links.size()) && count < c_MaxRopeNodes; link += direction, ++count) {
					rope.Links[link].Load = std::max(rope.Links[link].Load, share);
					int next = direction < 0 ? link : link + 1;
					if (rope.Links[link].Cut || rope.Nodes[next].Anchor >= 0) {
						break;
					}
				}
			}
			// Smoothed over a few updates, so a single jolt's spike doesn't snap it but a real fall's does.
			anchor.Tension += (tension - anchor.Tension) * 0.35F;
			if (anchor.Tension > holds && worstLink >= 0) {
				rope.Links[worstLink].Cut = true;
				anchor.Tension = 0.0F;
				EffectsParticles::SpawnImpact(ToVector(rope.Nodes[worstLink].Pos), Vector(), (type.Light[0] << 16) | (type.Light[1] << 8) | type.Light[2], type.Hardness);
			} else if (rope.AnchorKg > 0.0F && anchor.Tension > rope.AnchorKg) {
				// The tie gives way (pulled out of the ground, or off the unit) and the rope is left hanging from the rest.
				EffectsParticles::SpawnImpact(ToVector(rope.Nodes[anchor.Node].Pos), Vector(), (type.Dark[0] << 16) | (type.Dark[1] << 8) | type.Dark[2], 0.2F);
				DropAnchor(rope, static_cast<int>(index));
				--index;
			}
		}
	}

	/// Burns the points alight: they spread it to the next along, set burning what they touch, and burn through.
	void Burn(Rope& rope, float seconds, long long update, int& lights) {
		const TypeData& type = TypeOf(rope);
		if (!type.Burns) {
			return;
		}
		for (size_t i = 0; i < rope.Nodes.size(); ++i) {
			Node& node = rope.Nodes[i];
			if (node.Gone) {
				continue;
			}
			if (node.Burn <= 0.0F) {
				// Catches from burning ground beside it, or a burning unit it's tied to: looked at a few points each update.
				if ((update + static_cast<long long>(i)) % 8 == 0) {
					bool fire = TerrainFire::IsBurningNear(ToVector(node.Pos), 2);
					if (!fire && node.Anchor >= 0 && rope.Anchors[node.Anchor].Object) {
						if (MovableObject* object = ObjectOf(rope.Anchors[node.Anchor])) {
							fire = ActorFire::IsBurning(object->GetRootParent());
						}
					}
					if (fire && Random01() < 0.6F) {
						Ignite(node, type);
					}
				}
				continue;
			}
			float before = node.Burn;
			node.Burn += seconds;
			node.Char = std::max(node.Char, std::min(node.Burn / type.BurnSeconds, 1.0F));
			if (before < type.SpreadSeconds && node.Burn >= type.SpreadSeconds) {
				if (i > 0 && !rope.Links[i - 1].Cut) {
					Ignite(rope.Nodes[i - 1], type);
				}
				if (i < rope.Links.size() && !rope.Links[i].Cut) {
					Ignite(rope.Nodes[i + 1], type);
				}
			}
			if (Random01() < 0.01F) {
				// What it touches catches too: the ground under it, the unit it's tied to.
				TerrainFire::QueueIgnite(static_cast<int>(std::floor(node.Pos.x)), static_cast<int>(std::floor(node.Pos.y)) + 1);
				if (node.Anchor >= 0 && rope.Anchors[node.Anchor].Object) {
					ActorFire::QueueIgniteArea(ToVector(node.Pos), 3.0F);
				}
			}
			if (Random01() < 0.03F) {
				EffectsParticles::SpawnEmber(ToVector(node.Pos));
			}
			if (lights < 64 && i % 3 == 0) {
				++lights;
				float flicker = 0.8F + 0.2F * std::sin(static_cast<float>(update) * 0.7F + static_cast<float>(i));
				g_PostProcessMan.RegisterLight(ToVector(node.Pos), glm::vec3(255.0F, 140.0F, 50.0F), 30.0F, 0.35F * flicker, LightSource::Fire);
			}
			if (node.Burn >= type.BurnSeconds) {
				// Burnt through: the links either side fall away.
				node.Gone = true;
				node.Burn = 0.0F;
				if (i > 0) {
					rope.Links[i - 1].Cut = true;
				}
				if (i < rope.Links.size()) {
					rope.Links[i].Cut = true;
				}
			}
		}
	}

	/// What's flying through the ropes this update: bullets cut them, by how hard they hit against how tough the rope is, and flames set the
	/// kinds that burn alight.
	void HitByParticles() {
		bool anyBurns = false;
		for (const Rope& rope: s_Ropes) {
			anyBurns = anyBurns || TypeOf(rope).Burns;
		}
		// What's flying fast enough to cut, or alight: most updates there's nothing, and the grid isn't built.
		struct Flying {
			const MovableObject* Particle;
			bool Bullet;
			bool Flame;
		};
		std::vector<Flying> flying;
		for (const MovableObject* particle: g_MovableMan.GetParticleList()) {
			if (!particle || particle->IsSetToDelete()) {
				continue;
			}
			bool bullet = particle->HitsMOs() && particle->GetVel().GetSqrMagnitude() > 25.0F * 25.0F;
			bool flame = anyBurns && TerrainFire::IsFireSource(particle);
			if (bullet || flame) {
				flying.push_back({particle, bullet, flame});
			}
		}
		if (flying.empty()) {
			return;
		}
		struct LinkRef {
			int Rope;
			int Link;
		};
		std::unordered_map<long long, std::vector<LinkRef>> grid;
		int sceneWidth = g_SceneMan.GetSceneWidth();
		bool wraps = g_SceneMan.SceneWrapsX();
		auto cellOf = [&](const glm::vec2& at) {
			float x = at.x;
			if (wraps && sceneWidth > 0) {
				x = std::fmod(x, static_cast<float>(sceneWidth));
				if (x < 0.0F) {
					x += static_cast<float>(sceneWidth);
				}
			}
			long long cx = static_cast<long long>(std::floor(x / c_HashCell));
			long long cy = static_cast<long long>(std::floor(at.y / c_HashCell));
			return (cy << 32) ^ (cx & 0xFFFFFFFFLL);
		};
		for (int r = 0; r < static_cast<int>(s_Ropes.size()); ++r) {
			const Rope& rope = s_Ropes[r];
			for (int i = 0; i < static_cast<int>(rope.Links.size()); ++i) {
				if (rope.Links[i].Cut) {
					continue;
				}
				glm::vec2 a = rope.Nodes[i].Pos;
				glm::vec2 b = rope.Nodes[i + 1].Pos;
				long long first = cellOf(a);
				grid[first].push_back({r, i});
				if (long long second = cellOf(b); second != first) {
					grid[second].push_back({r, i});
				}
			}
		}
		if (grid.empty()) {
			return;
		}
		std::vector<std::pair<int, int>> cuts;
		std::vector<std::pair<int, int>> ignites;
		for (const auto& [particle, bullet, flame]: flying) {
			float speed = particle->GetVel().GetMagnitude();
			glm::vec2 end = ToGlm(particle->GetPos());
			glm::vec2 start = bullet ? end + Between(end, ToGlm(particle->GetPrevPos())) : end;
			float power = particle->GetMass() * speed * std::max(particle->GetSharpness(), 1.0F);
			// The cells along its path this update.
			glm::vec2 path = end - start;
			int steps = std::min(static_cast<int>(glm::length(path) / (c_HashCell * 0.5F)) + 1, 32);
			long long lastCell = LLONG_MIN;
			bool done = false;
			for (int s = 0; s <= steps && !done; ++s) {
				long long cell = cellOf(start + path * (static_cast<float>(s) / static_cast<float>(steps)));
				if (cell == lastCell) {
					continue;
				}
				lastCell = cell;
				auto found = grid.find(cell);
				if (found == grid.end()) {
					continue;
				}
				for (const LinkRef& ref: found->second) {
					const Rope& rope = s_Ropes[ref.Rope];
					const TypeData& type = TypeOf(rope);
					glm::vec2 a = start + Between(start, rope.Nodes[ref.Link].Pos);
					glm::vec2 b = a + (rope.Nodes[ref.Link + 1].Pos - rope.Nodes[ref.Link].Pos);
					if (SegmentDistance(start, end, a, b) > (bullet ? 1.2F : 2.5F)) {
						continue;
					}
					if (flame && type.Burns) {
						ignites.emplace_back(ref.Rope, ref.Link);
					}
					if (bullet) {
						float chance = std::clamp((power / type.CutPower - 0.6F) / 0.8F, 0.0F, 1.0F);
						if (Random01() < chance) {
							cuts.emplace_back(ref.Rope, ref.Link);
						}
						if (type.Hardness > 0.5F) {
							EffectsParticles::SpawnImpact(ToVector(a), particle->GetVel(), (type.Light[0] << 16) | (type.Light[1] << 8) | type.Light[2], type.Hardness);
						}
						done = true;
						break;
					}
				}
			}
		}
		for (const auto& [rope, link]: cuts) {
			s_Ropes[rope].Links[link].Cut = true;
		}
		for (const auto& [rope, link]: ignites) {
			Ignite(s_Ropes[rope].Nodes[link], TypeOf(s_Ropes[rope]));
		}
	}

	/// The colour a rope is drawn in at a point: its lighter or darker shade, charred as it burns and glowing where it is alight.
	glm::vec3 Shade(const TypeData& type, bool light, const Node& node, float flicker) {
		const std::array<unsigned char, 3>& base = light ? type.Light : type.Dark;
		glm::vec3 color(base[0], base[1], base[2]);
		// Charred towards black as it burns, and glowing where alight.
		color = glm::mix(color, glm::vec3(38.0F, 32.0F, 28.0F), std::clamp(node.Char, 0.0F, 1.0F) * 0.85F);
		if (node.Burn > 0.0F) {
			color = glm::mix(color, glm::vec3(255.0F, 120.0F + 60.0F * flicker, 40.0F), 0.55F + 0.3F * flicker);
		}
		return color;
	}
} // namespace

float RopeSim::GetReaction() {
	return s_Reaction;
}

void RopeSim::SetReaction(float reaction) {
	s_Reaction = std::clamp(reaction, 0.0F, 5.0F);
}

float RopeSim::GetSettleSeconds() {
	return s_SettleSeconds;
}

void RopeSim::SetSettleSeconds(float seconds) {
	s_SettleSeconds = std::clamp(seconds, 0.0F, 600.0F);
}

int RopeSim::GetTypeCount() {
	return c_TypeCount;
}

const RopeSim::TypeInfo& RopeSim::GetType(int type) {
	return c_Types[std::clamp(type, 0, c_TypeCount - 1)].Info;
}

int RopeSim::FindType(const std::string& name) {
	for (int i = 0; i < c_TypeCount; ++i) {
		std::string typeName = c_Types[i].Info.Name;
		if (typeName.size() == name.size() && std::equal(typeName.begin(), typeName.end(), name.begin(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); })) {
			return i;
		}
	}
	return -1;
}

int RopeSim::Create(int type, float slack, const Vector& position, float strength, float anchorKg) {
	return CreateRope(s_NextId++, type, slack, position, strength, anchorKg);
}

bool RopeSim::AddPoint(int rope, const Vector& position) {
	return AddRopePoint(rope, position);
}

int RopeSim::GetPointCount(int rope) {
	const Rope* found = Find(rope);
	return found ? found->Points : 0;
}

void RopeSim::Remove(int rope) {
	RemoveRope(rope);
}

void RopeSim::Clear() {
	s_Ropes.clear();
	s_Count = 0;
}

int RopeSim::GetCount() {
	return s_Count;
}

int RopeSim::QueueRope(int type, float slack, const std::vector<Vector>& points, float strength, float anchorKg) {
	Request request{Request::Kind::Create};
	request.Id = s_NextId++;
	request.Type = type;
	request.Slack = slack;
	request.Strength = strength;
	request.AnchorKg = anchorKg;
	request.Points = points;
	std::scoped_lock lock(s_RequestMutex);
	s_Requests.push_back(request);
	return request.Id;
}

void RopeSim::QueueAddPoint(int rope, const Vector& position) {
	Request request{Request::Kind::AddPoint};
	request.Id = rope;
	request.Position = position;
	std::scoped_lock lock(s_RequestMutex);
	s_Requests.push_back(request);
}

void RopeSim::QueueRemove(int rope) {
	Request request{Request::Kind::Remove};
	request.Id = rope;
	std::scoped_lock lock(s_RequestMutex);
	s_Requests.push_back(request);
}

void RopeSim::QueueCut(const Vector& position, float radius) {
	Request request{Request::Kind::Cut};
	request.Position = position;
	request.Radius = radius;
	std::scoped_lock lock(s_RequestMutex);
	s_Requests.push_back(request);
}

void RopeSim::QueueIgniteArea(const Vector& position, float radius) {
	if (s_Count == 0) {
		return;
	}
	Request request{Request::Kind::Ignite};
	request.Position = position;
	request.Radius = radius;
	std::scoped_lock lock(s_RequestMutex);
	s_Requests.push_back(request);
}

void RopeSim::QueueBlast(const Vector& position, float reach, float energy) {
	if (s_Count == 0) {
		return;
	}
	Request request{Request::Kind::Blast};
	request.Position = position;
	request.Radius = reach;
	request.Energy = energy;
	std::scoped_lock lock(s_RequestMutex);
	s_Requests.push_back(request);
}

void RopeSim::Update() {
	const Scene* scene = g_SceneMan.GetScene();
	if (scene != s_Scene || g_SceneMan.GetSceneGeneration() != s_SceneGeneration) {
		// A new scene: the old one's ropes are gone, and a saved game's put back.
		Clear();
		s_Scene = scene;
		s_SceneGeneration = g_SceneMan.GetSceneGeneration();
		s_Random = 0x9E3779B9u;
		s_TablesBuilt = false;
		{
			std::scoped_lock lock(s_RequestMutex);
			s_Requests.clear();
		}
		if (scene && !s_PendingLoadState.empty()) {
			LoadState(s_PendingLoadState);
		}
		s_PendingLoadState.clear();
	}
	if (!scene || !g_SceneMan.GetScene()->GetTerrain()) {
		return;
	}
	if (!s_TablesBuilt) {
		BuildTables();
	}
	TakeRequests();
	if (s_Ropes.empty()) {
		return;
	}
	float seconds = std::max(g_TimerMan.GetDeltaTimeSecs(), 1e-4F);
	Vector globalAcc = g_SceneMan.GetGlobalAcc();
	glm::vec2 gravity = ToGlm(globalAcc) * c_PPM;
	float gravityMagnitude = globalAcc.GetMagnitude();
	// The weather's wind as the air gives it, whether or not it is set to carry smoke: ropes blow about in it all the same.
	float wind = AirPressure::IsOn() ? AirPressure::GetNaturalWind() * std::max(AirPressure::GetTuning().WindStrength, 0.0F) * AirPressure::GetOverall() : 0.0F;
	long long update = g_TimerMan.GetSimUpdateCount();
	bool checkGround = update % 8 == 0;
	int lights = 0;
	for (Rope& rope: s_Ropes) {
		UpdateAnchors(rope, checkGround);
		Integrate(rope, seconds, gravity, wind, update);
		Solve(rope);
		Collide(rope);
		Pull(rope, seconds, gravityMagnitude);
		Burn(rope, seconds, update, lights);
	}
	HitByParticles();
	std::erase_if(s_Ropes, [seconds](Rope& rope) { return Settle(rope, seconds); });
	// Kept on the map: a rope that has gone round a wrapping map's seam is moved back a map's width, and one that has fallen off the bottom
	// is gone.
	float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	float sceneHeight = static_cast<float>(g_SceneMan.GetSceneHeight());
	for (Rope& rope: s_Ropes) {
		if (g_SceneMan.SceneWrapsX() && sceneWidth > 0.0F && !rope.Nodes.empty()) {
			float shift = rope.Nodes.front().Pos.x < 0.0F ? sceneWidth : (rope.Nodes.front().Pos.x >= sceneWidth ? -sceneWidth : 0.0F);
			if (shift != 0.0F) {
				for (Node& node: rope.Nodes) {
					node.Pos.x += shift;
					node.Prev.x += shift;
					node.Safe.x += shift;
				}
			}
		}
	}
	s_Ropes.erase(std::remove_if(s_Ropes.begin(), s_Ropes.end(), [sceneHeight](const Rope& rope) { return std::all_of(rope.Nodes.begin(), rope.Nodes.end(), [sceneHeight](const Node& node) { return node.Gone || node.Pos.y > sceneHeight + 100.0F; }); }), s_Ropes.end());
	s_Count = static_cast<int>(s_Ropes.size());
}

void RopeSim::Draw(const Camera& camera) {
	if (s_Ropes.empty() || !g_SceneMan.GetScene()) {
		return;
	}
	const Box& view = camera.GetViewport();
	float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	bool wraps = g_SceneMan.SceneWrapsX();
	bool pixelFire = g_PostProcessMan.GetLightingSettings().FireStyle != LightingSettings::FireShaderOnly;
	bool shaderFire = g_PostProcessMan.GetLightingSettings().FireStyle != LightingSettings::FirePixel;
	float time = static_cast<float>(g_TimerMan.GetSimUpdateCount());
	auto put = [](int x, int y, const glm::vec3& color) {
		RTE::Draw::PixelBatched(glm::vec2(static_cast<float>(x), static_cast<float>(y)), RTE::Color(static_cast<int>(color.r), static_cast<int>(color.g), static_cast<int>(color.b), 255));
	};
	for (const Rope& rope: s_Ropes) {
		const TypeData& type = TypeOf(rope);
		float left = 1e9F;
		float right = -1e9F;
		float top = 1e9F;
		float bottom = -1e9F;
		for (const Node& node: rope.Nodes) {
			left = std::min(left, node.Pos.x);
			right = std::max(right, node.Pos.x);
			top = std::min(top, node.Pos.y);
			bottom = std::max(bottom, node.Pos.y);
		}
		for (float shift: {0.0F, -sceneWidth, sceneWidth}) {
			if (shift != 0.0F && !wraps) {
				continue;
			}
			if (right + shift < view.GetCorner().m_X - 8.0F || left + shift > view.GetCorner().m_X + view.GetWidth() + 8.0F || bottom < view.GetCorner().m_Y - 8.0F || top > view.GetCorner().m_Y + view.GetHeight() + 8.0F) {
				continue;
			}
			float along = 0.0F;
			for (size_t i = 0; i < rope.Links.size(); ++i) {
				const Node& a = rope.Nodes[i];
				const Node& b = rope.Nodes[i + 1];
				float length = glm::length(b.Pos - a.Pos);
				if (rope.Links[i].Cut || a.Gone || b.Gone) {
					along += length;
					continue;
				}
				glm::vec2 way = b.Pos - a.Pos;
				int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::abs(way.x), std::abs(way.y)))));
				// Which way across the rope its second pixel goes: the axis nearest square to it.
				glm::ivec2 across = std::abs(way.x) >= std::abs(way.y) ? glm::ivec2(0, 1) : glm::ivec2(1, 0);
				int lastX = INT_MIN;
				int lastY = INT_MIN;
				for (int step = 0; step < steps; ++step) {
					float t = static_cast<float>(step) / static_cast<float>(steps);
					glm::vec2 at = a.Pos + way * t;
					int x = static_cast<int>(std::floor(at.x + shift));
					int y = static_cast<int>(std::floor(at.y));
					float s = along + length * t;
					if (x == lastX && y == lastY) {
						continue;
					}
					lastX = x;
					lastY = y;
					const Node& near = t < 0.5F ? a : b;
					float flicker = 0.5F + 0.5F * std::sin(time * 0.6F + s * 0.9F);
					switch (type.Style) {
						case Look::Twisted: {
							// The strands twist along it: each pixel's shade follows the twist, the second row half a twist on.
							bool strand = static_cast<int>(std::floor(s * 0.5F)) % 2 == 0;
							put(x, y, Shade(type, strand, near, flicker));
							put(x + across.x, y + across.y, Shade(type, !strand, near, flicker));
							break;
						}
						case Look::Thread:
							put(x, y, Shade(type, static_cast<int>(std::floor(s * 0.25F)) % 3 != 0, near, flicker));
							break;
						case Look::Chain: {
							// A link face on (its two sides either side of the line, its ends on it) and the next edge on (a short bar).
							float phase = std::fmod(s, 5.0F);
							if (phase < 3.5F) {
								if (phase < 0.75F || phase > 2.75F) {
									put(x, y, Shade(type, true, near, flicker));
								} else {
									put(x + across.x, y + across.y, Shade(type, false, near, flicker));
									put(x - across.x, y - across.y, Shade(type, true, near, flicker));
								}
							} else {
								put(x, y, Shade(type, false, near, flicker));
							}
							break;
						}
						case Look::Cable:
							put(x, y, Shade(type, static_cast<int>(std::floor(s)) % 3 == 0, near, flicker));
							break;
						case Look::Banded: {
							bool fleck = static_cast<int>(std::floor(s)) % 4 == 0;
							put(x, y, Shade(type, !fleck, near, flicker));
							put(x + across.x, y + across.y, Shade(type, true, near, flicker));
							break;
						}
					}
				}
				along += length;
			}
			// The knots where it's tied, a shade darker; and the flames where it burns.
			for (const Anchor& anchor: rope.Anchors) {
				const Node& node = rope.Nodes[anchor.Node];
				int x = static_cast<int>(std::floor(node.Pos.x + shift));
				int y = static_cast<int>(std::floor(node.Pos.y));
				glm::vec3 knot = Shade(type, false, node, 0.5F) * 0.8F;
				put(x, y, knot);
				put(x + 1, y, knot);
				put(x - 1, y, knot);
				put(x, y + 1, knot);
				put(x, y - 1, knot);
			}
			for (size_t i = 0; i < rope.Nodes.size(); ++i) {
				const Node& node = rope.Nodes[i];
				if (node.Burn <= 0.0F || node.Gone) {
					continue;
				}
				if (shaderFire) {
					EffectsParticles::RegisterFlame(&node, glm::vec2(node.Pos.x + shift, node.Pos.y), 0.35F, 0.8F);
				}
				if (pixelFire) {
					// A flickering tongue a few pixels high over the burning point.
					int x = static_cast<int>(std::floor(node.Pos.x + shift));
					int y = static_cast<int>(std::floor(node.Pos.y));
					int height = 1 + static_cast<int>((std::sin(time * 0.9F + static_cast<float>(i) * 1.7F) * 0.5F + 0.5F) * 3.0F);
					for (int up = 1; up <= height; ++up) {
						put(x, y - up, up == height ? glm::vec3(255.0F, 120.0F, 40.0F) : glm::vec3(255.0F, 220.0F, 110.0F));
					}
				}
			}
		}
	}
}

void RopeSim::GetDebug(std::vector<DebugLink>& links, std::vector<DebugAnchor>& anchors, int& nodes, int& burning) {
	nodes = 0;
	burning = 0;
	for (const Rope& rope: s_Ropes) {
		nodes += static_cast<int>(rope.Nodes.size());
		for (size_t i = 0; i < rope.Links.size(); ++i) {
			if (!rope.Links[i].Cut) {
				links.push_back({rope.Nodes[i].Pos, rope.Nodes[i + 1].Pos, rope.Links[i].Load, rope.Nodes[i].Burn > 0.0F || rope.Nodes[i + 1].Burn > 0.0F});
			}
		}
		for (const Node& node: rope.Nodes) {
			burning += node.Burn > 0.0F ? 1 : 0;
		}
		for (const Anchor& anchor: rope.Anchors) {
			anchors.push_back({rope.Nodes[anchor.Node].Pos, anchor.Object || anchor.Piece ? 1 : 0});
		}
	}
}

std::string RopeSim::GetSaveState() {
	std::ostringstream stream;
	stream << "ropes 2 " << s_Ropes.size();
	for (const Rope& rope: s_Ropes) {
		stream << ' ' << rope.Type << ' ' << rope.Slack << ' ' << rope.Points << ' ' << rope.Nodes.size() << ' ' << rope.StrengthMult << ' ' << rope.AnchorKg;
		for (const Node& node: rope.Nodes) {
			stream << ' ' << node.Pos.x << ' ' << node.Pos.y << ' ' << (node.Gone ? 1 : 0);
		}
		for (const Link& link: rope.Links) {
			stream << ' ' << link.Rest << ' ' << (link.Cut ? 1 : 0);
		}
		// Only the ground ties: a unit, thing or loose piece comes back from the saved game as a new one, so that end is loose.
		int groundTies = static_cast<int>(std::count_if(rope.Anchors.begin(), rope.Anchors.end(), [](const Anchor& anchor) { return !anchor.Object && !anchor.Piece; }));
		stream << ' ' << groundTies;
		for (const Anchor& anchor: rope.Anchors) {
			if (!anchor.Object && !anchor.Piece) {
				stream << ' ' << anchor.Node << ' ' << anchor.Pixel.x << ' ' << anchor.Pixel.y;
			}
		}
	}
	return stream.str();
}

void RopeSim::SetPendingLoadState(const std::string& state) {
	s_PendingLoadState = state;
}
