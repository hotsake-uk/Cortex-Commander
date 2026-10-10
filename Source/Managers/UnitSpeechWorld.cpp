#include "UnitSpeech.h"

#include "ACraft.h"
#include "ADoor.h"
#include "AEJetpack.h"
#include "AHuman.h"
#include "AVehicle.h"
#include "Actor.h"
#include "ActorFire.h"
#include "Arm.h"
#include "Colony.h"
#include "FluidSim.h"
#include "GasGrid.h"
#include "HDFirearm.h"
#include "Leg.h"
#include "Material.h"
#include "MovableMan.h"
#include "SceneMan.h"
#include "SmokeGrid.h"
#include "TerrainCollapse.h"
#include "TerrainFire.h"
#include "TimerMan.h"
#include "AirPressure.h"
#include "PostProcessMan.h"
#include "WeatherEffects.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

using namespace RTE;

// Unit speech about the world (US-2): what a unit notices of its surroundings and itself that its AI doesn't decide, and says something about.
// Each unit looks a few times a second. Most lines come when something changes (it wades into acid, catches fire, loses an arm, rock comes
// down over it); the weather, the night and a generator's hum are remarked on now and then while they last, as each trigger's cooldown in
// Speech.ini allows. Only reads the world: nothing here changes the simulation or its random numbers.
namespace {
	/// How many sim updates between one unit's looks (about 7 a second at 60 updates a second).
	constexpr int c_LookEvery = 8;

	/// What a unit can be in up to its knees or over its head.
	enum Liquid {
		NoLiquid,
		Water,
		Acid,
		Lava,
		Oil,
		Mud,
		Tar,
		Blood,
		ToxicSludge,
		Mercury,
		Fuel,
		Cryo,
		Concrete,
		OtherLiquid,
		LiquidCount
	};
	/// The trigger for wading into each, and for coming out of it covered (none for those it comes out of as it went in, or dead).
	constexpr std::array<const char*, LiquidCount> c_IntoLiquid{"", "InWater", "InAcid", "InLava", "InOil", "InMud", "InTar", "InBlood", "InToxicSludge", "InMercury", "InFuel", "InCryo", "InConcrete", "InLiquid"};
	constexpr std::array<const char*, LiquidCount> c_OutOfLiquid{"", "OutOfWater", "CoveredInAcid", "", "CoveredInOil", "CoveredInMud", "CoveredInTar", "CoveredInBlood", "CoveredInSludge", "", "CoveredInFuel", "Frostbite", "CoveredInConcrete", ""};

	/// What a unit can be standing on that's worth a word.
	enum Ground {
		NoGround,
		Snow,
		Ice,
		Sand,
		Glass,
		Ashes,
		Bones,
		Gold,
		Moon,
		Rubble,
		Scrap,
		Brass,
		Grass,
		Metal,
		Dirt, //!< Anything else: nothing said about it, but stepping off something else onto it counts as a change.
		GroundCount
	};
	constexpr std::array<const char*, GroundCount> c_OnGround{"", "OnSnow", "OnIce", "OnSand", "OnGlass", "OnAshes", "OnBones", "OnGold", "OnMoonDust", "OnRubble", "OnScrap", "OnBrass", "OnGrass", "OnMetal", ""};

	/// What held at a unit's last look (UnitSpeech::Senses::Flags).
	enum Bit : unsigned long long {
		Burning = 1ULL << 0,
		NearFire = 1ULL << 1,
		OnEmbers = 1ULL << 2,
		InToxicGas = 1ULL << 3,
		InSteam = 1ULL << 4,
		InMethane = 1ULL << 5,
		InSmoke = 1ULL << 6,
		Sooty = 1ULL << 7,
		Soaked = 1ULL << 8,
		SnowCovered = 1ULL << 9,
		Falling = 1ULL << 10,
		Digging = 1ULL << 11,
		Night = 1ULL << 12,
		Underground = 1ULL << 13,
		Fast = 1ULL << 14,
		Flipped = 1ULL << 15,
		Swimming = 1ULL << 16,
		FriendBurning = 1ULL << 17,
		RockOverhead = 1ULL << 18,
		Drowning = 1ULL << 19,
		Hot = 1ULL << 20,
		Stuck = 1ULL << 21,
	};

	std::array<unsigned char, 256> s_LiquidOf{};
	std::array<unsigned char, 256> s_GroundOf{};
	const Scene* s_TablesScene = nullptr;

	struct Strike {
		Vector Pos;
		long long MS = 0;
	};
	std::array<Strike, 8> s_Strikes;
	int s_NextStrike = 0;
	std::mutex s_StrikeMutex;

	/// The words of a material's name, in lower case.
	std::vector<std::string> Words(const std::string& name) {
		std::string lower;
		for (char c: name) {
			lower += static_cast<char>(std::isalnum(static_cast<unsigned char>(c)) ? std::tolower(static_cast<unsigned char>(c)) : ' ');
		}
		std::vector<std::string> words;
		std::istringstream stream(lower);
		for (std::string word; stream >> word;) {
			words.push_back(word);
		}
		return words;
	}

	/// Sorts every material into the liquids and grounds above, by its name (so a mod's "Hot Acid" or "Thin Ice" counts too).
	void BuildTables() {
		s_TablesScene = g_SceneMan.GetScene();
		s_LiquidOf.fill(NoLiquid);
		s_GroundOf.fill(NoGround);
		for (int id = 0; id < 256; ++id) {
			const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
			if (!material || id == g_MaterialAir || (id != 0 && material->GetIndex() != id)) {
				continue;
			}
			std::vector<std::string> words = Words(material->GetPresetName());
			auto has = [&words](const char* word) { return std::find(words.begin(), words.end(), word) != words.end(); };
			auto starts = [&words](const char* start) {
				return std::any_of(words.begin(), words.end(), [start](const std::string& word) { return word.rfind(start, 0) == 0; });
			};
			// (By name first: the liquids' own table may not be built yet, and solid "Concrete" isn't wet concrete.)
			Liquid liquid = NoLiquid;
			if (has("acid")) {
				liquid = Acid;
			} else if (has("lava") || has("magma")) {
				liquid = Lava;
			} else if (has("water")) {
				liquid = Water;
			} else if (has("oil")) {
				liquid = Oil;
			} else if (has("mud") || (has("sludge") && !has("toxic"))) {
				liquid = Mud;
			} else if (has("tar") || has("pitch")) {
				liquid = Tar;
			} else if (has("blood")) {
				liquid = Blood;
			} else if (has("toxic")) {
				liquid = ToxicSludge;
			} else if (has("mercury")) {
				liquid = Mercury;
			} else if (has("fuel") || has("petrol") || has("napalm")) {
				liquid = Fuel;
			} else if (starts("cryo")) {
				liquid = Cryo;
			} else if ((has("concrete") || has("cement")) && has("wet")) {
				liquid = Concrete;
			} else if (FluidSim::IsLiquid(id)) {
				liquid = OtherLiquid;
			}
			if (liquid != NoLiquid) {
				s_LiquidOf[id] = static_cast<unsigned char>(liquid);
				continue;
			}
			Ground ground = Dirt;
			if (starts("snow")) {
				ground = Snow;
			} else if (has("ice") || has("frost")) {
				ground = Ice;
			} else if (has("sand")) {
				ground = Sand;
			} else if (has("glass") && (has("shards") || has("broken"))) {
				ground = Glass;
			} else if (has("ash") || has("ashes") || has("charcoal") || has("soot")) {
				ground = Ashes;
			} else if (has("bone") || has("bones") || has("scraps") || has("gore")) {
				ground = Bones;
			} else if (has("gold")) {
				ground = Gold;
			} else if (has("lunar") || has("moon")) {
				ground = Moon;
			} else if (has("rubble") || has("gravel")) {
				ground = Rubble;
			} else if (has("scrap") || has("mangled") || has("wreckage")) {
				ground = Scrap;
			} else if (has("casing") || has("casings")) {
				ground = Brass;
			} else if (has("grass") || has("topsoil")) {
				ground = Grass;
			} else if (has("metal") || has("steel") || has("plate")) {
				ground = Metal;
			}
			s_GroundOf[id] = static_cast<unsigned char>(ground);
		}
	}

	unsigned char MatterAt(const Vector& point) {
		const int x = point.GetFloorIntX();
		const int y = point.GetFloorIntY();
		if (y < 0 || y >= g_SceneMan.GetSceneHeight()) {
			return static_cast<unsigned char>(g_MaterialAir);
		}
		return g_SceneMan.GetTerrMatter(x, y);
	}

	/// How much solid ground is over a point, up to the open sky (or 480 px up), in pixels: 0 under the open sky.
	int RoofOver(const Vector& point) {
		int solid = 0;
		for (int up = 4; up <= 480 && point.m_Y - static_cast<float>(up) >= 0.0F; up += 4) {
			if (MatterAt(point - Vector(0.0F, static_cast<float>(up))) != g_MaterialAir) {
				solid += 4;
			}
		}
		return solid;
	}

	/// Whether two points are close, the scene's wrapping taken into account.
	bool Within(const Vector& from, const Vector& to, float range) {
		return g_SceneMan.ShortestDistance(from, to, g_SceneMan.SceneWrapsX() || g_SceneMan.SceneWrapsY()).MagnitudeIsLessThan(range);
	}

	/// What every unit's look this update shares: built once, the first time a unit looks.
	struct Shared {
		bool Built = false;
		std::vector<const Actor*> Bodies;
		std::vector<const Actor*> BurningUnits;
		std::vector<TerrainCollapse::FallingPiece> Pieces;
		std::vector<Strike> Strikes;
		float Night = 0.0F;
		float Rain = 0.0F;
		float Snow = 0.0F;
		float Dust = 0.0F;
		float Wind = 0.0F;

		void Build(long long nowMS) {
			Built = true;
			for (const Actor* actor: g_MovableMan.GetActorList()) {
				if (dynamic_cast<const ADoor*>(actor) || dynamic_cast<const ACraft*>(actor)) {
					continue;
				}
				if (actor->GetStatus() >= Actor::DYING) {
					Bodies.push_back(actor);
				} else if (ActorFire::IsEnabled() && ActorFire::IsBurning(actor)) {
					BurningUnits.push_back(actor);
				}
			}
			if (TerrainCollapse::GetFallingCount() > 0) {
				TerrainCollapse::GetFallingPieces(Pieces);
			}
			{
				std::scoped_lock lock(s_StrikeMutex);
				for (const Strike& strike: s_Strikes) {
					if (strike.MS > 0 && strike.MS <= nowMS && nowMS - strike.MS < 1000) {
						Strikes.push_back(strike);
					}
				}
			}
			Night = Actor::GetNightAmount();
			Rain = WeatherEffects::GetRain();
			Snow = WeatherEffects::GetSnow();
			Dust = WeatherEffects::GetDust();
			Wind = WeatherEffects::GetWind();
		}
	};

	/// One unit's look: what it notices, in the order a player would most want to hear about it; the first that's said is said.
	void Look(Actor& actor, Shared& shared, long long nowMS) {
		UnitSpeech::Senses& senses = actor.GetSpeech().World;
		const bool primed = senses.Primed;
		senses.Primed = true;
		std::vector<const char*> say;
		auto edge = [&senses, primed](Bit bit, bool now) {
			const bool was = (senses.Flags & bit) != 0;
			senses.Flags = now ? (senses.Flags | bit) : (senses.Flags & ~static_cast<unsigned long long>(bit));
			return primed && now && !was;
		};
		auto fell = [&senses, primed](Bit bit, bool now) {
			const bool was = (senses.Flags & bit) != 0;
			senses.Flags = now ? (senses.Flags | bit) : (senses.Flags & ~static_cast<unsigned long long>(bit));
			return primed && !now && was;
		};

		const Vector& position = actor.GetPos();
		const int team = actor.GetTeam();

		// A cart or other vehicle: only how it's driven and what's happening to it.
		if (AVehicle* vehicle = dynamic_cast<AVehicle*>(&actor)) {
			if (!vehicle->HasDriver()) {
				senses.Flags = 0;
				senses.Health = vehicle->GetHealth();
				return;
			}
			const float rotation = std::remainder(vehicle->GetRotAngle(), 2.0F * c_PI);
			if (edge(Flipped, std::abs(rotation) > 1.4F && vehicle->GetVel().MagnitudeIsLessThan(2.0F))) {
				say.push_back("VehicleFlipped");
			}
			if (primed && senses.Health - vehicle->GetHealth() > vehicle->GetMaxHealth() * 0.08F) {
				say.push_back("VehicleHit");
			}
			const bool fast = vehicle->GetVel().MagnitudeIsGreaterThan((senses.Flags & Fast) ? 6.0F : 9.0F);
			if (edge(Fast, fast)) {
				say.push_back("VehicleFast");
			}
			senses.Health = vehicle->GetHealth();
			for (const char* trigger: say) {
				if (actor.Say(trigger)) {
					break;
				}
			}
			return;
		}

		AHuman* human = dynamic_cast<AHuman*>(&actor);
		const float height = actor.GetHeight() > 0.0F ? actor.GetHeight() : actor.GetRadius() * 2.0F;
		const Vector feet = position + Vector(0.0F, height * 0.2F);
		const Vector head = position - Vector(0.0F, height * 0.24F);

		// Fire first: on fire, it's all it thinks about.
		const bool burning = ActorFire::IsEnabled() && ActorFire::IsBurning(&actor);
		if (edge(Burning, burning)) {
			say.push_back("OnFire");
		} else if (fell(Burning, burning)) {
			say.push_back("FireOut");
		}

		// Liquids: which, how deep, and air.
		const int depth = actor.GetLiquidDepth();
		int liquid = NoLiquid;
		if (depth > 0) {
			for (const Vector& point: {feet + Vector(0.0F, 3.0F), feet, position, head}) {
				if (int kind = s_LiquidOf[MatterAt(point)]; kind != NoLiquid) {
					liquid = kind;
					break;
				}
			}
			if (liquid == NoLiquid) {
				liquid = OtherLiquid;
			}
		}
		const float air = actor.GetAirLeft();
		if (primed && air < 0.4F && senses.Air >= 0.4F) {
			say.push_back("Drowning");
		}
		if (primed && depth < 3 && senses.Depth >= 3 && senses.Air < 0.85F) {
			say.push_back("Surfaced");
		}
		if (primed && liquid != senses.Liquid) {
			if (liquid != NoLiquid) {
				say.push_back(c_IntoLiquid[liquid]);
			} else if (senses.Liquid != NoLiquid && nowMS - senses.LiquidSinceMS > 1500) {
				if (const char* covered = c_OutOfLiquid[senses.Liquid]; covered[0] != '\0') {
					say.push_back(covered);
				}
			}
		}
		if (primed && depth >= 3 && senses.Depth < 3 && air > 0.5F) {
			say.push_back("GoesUnder");
		}
		// (Swimming in what can be swum in: not lava or acid, nor mud, tar, mercury or concrete, which a body floats on but doesn't swim.)
		const bool swimmable = liquid == Water || liquid == Oil || liquid == Blood || liquid == ToxicSludge || liquid == Fuel || liquid == OtherLiquid;
		if (edge(Swimming, depth == 2 && actor.IsFloater() && swimmable)) {
			say.push_back("Swimming");
		}
		if (liquid != senses.Liquid) {
			senses.LiquidSinceMS = nowMS;
		}
		senses.Liquid = liquid;
		senses.Depth = depth;
		senses.Air = air;

		// Its body: limbs lost, falling, a jetpack run dry.
		if (human) {
			const int arms = (human->GetFGArm() ? 1 : 0) + (human->GetBGArm() ? 1 : 0);
			const int legs = (human->GetFGLeg() ? 1 : 0) + (human->GetBGLeg() ? 1 : 0);
			if (primed && senses.Legs >= 0 && legs < senses.Legs) {
				say.push_back(legs == 0 ? "LostLegs" : "LostLeg");
			}
			if (primed && senses.Arms >= 0 && arms < senses.Arms) {
				say.push_back(arms == 0 ? "LostArms" : "LostArm");
			}
			senses.Arms = arms;
			senses.Legs = legs;
		}
		if (depth == 0 && actor.GetVel().m_Y > 2.0F) {
			if (!(senses.Flags & Falling) && senses.FallStartY == 0.0F) {
				senses.FallStartY = position.m_Y;
			}
			const float safe = actor.GetMaxSafeFallHeight();
			const float fallen = position.m_Y - senses.FallStartY;
			if (edge(Falling, fallen > std::max(std::min(safe, 2000.0F) * 0.7F, 72.0F))) {
				say.push_back("Falling");
			}
		} else {
			senses.FallStartY = 0.0F;
			senses.Flags &= ~static_cast<unsigned long long>(Falling);
		}
		if (human) {
			if (const AEJetpack* jetpack = human->GetJetpack(); jetpack && jetpack->GetJetTimeTotal() > 0.0F) {
				const float left = jetpack->GetJetTimeLeft() / jetpack->GetJetTimeTotal();
				if (primed && left <= 0.02F && senses.JetLeft > 0.02F && depth == 0) {
					say.push_back("JetpackEmpty");
				}
				senses.JetLeft = left;
			}
		}

		// Danger overhead: loose rock or a tree coming down on it, lightning.
		bool rockOverhead = false;
		bool treeOverhead = false;
		for (const TerrainCollapse::FallingPiece& piece: shared.Pieces) {
			if (piece.VelY < 1.0F) {
				continue;
			}
			Vector toPiece = g_SceneMan.ShortestDistance(position, Vector(piece.X, piece.Y), g_SceneMan.SceneWrapsX());
			if (toPiece.m_Y < 0.0F && toPiece.m_Y > -240.0F && std::abs(toPiece.m_X + piece.VelX * 10.0F) < piece.Radius + 40.0F) {
				(piece.Tree ? treeOverhead : rockOverhead) = true;
			}
		}
		if (edge(RockOverhead, rockOverhead || treeOverhead)) {
			say.push_back(treeOverhead ? "TreeFalling" : "RockFall");
		}
		for (const Strike& strike: shared.Strikes) {
			if (Within(position, strike.Pos, 70.0F)) {
				say.push_back("LightningClose");
				break;
			} else if (Within(position, strike.Pos, 600.0F)) {
				say.push_back("Lightning");
				break;
			}
		}

		// Gas and smoke where it breathes.
		if (GasGrid::IsEnabled()) {
			if (edge(InToxicGas, GasGrid::Get(head, GasGrid::Toxic) > ((senses.Flags & InToxicGas) ? 0.05F : 0.15F))) {
				say.push_back("ToxicGas");
			}
			if (edge(InSteam, GasGrid::Get(head, GasGrid::Steam) > ((senses.Flags & InSteam) ? 0.08F : 0.25F))) {
				say.push_back("Steam");
			}
			if (edge(InMethane, GasGrid::Get(head, GasGrid::Methane) > ((senses.Flags & InMethane) ? 0.08F : 0.25F))) {
				say.push_back("Methane");
			}
		}
		const float smoke = std::max(SmokeGrid::IsEnabled() ? SmokeGrid::GetDensity(head) : 0.0F, GasGrid::IsEnabled() ? GasGrid::Get(head, GasGrid::Smoke) : 0.0F);
		if (edge(InSmoke, smoke > ((senses.Flags & InSmoke) ? 0.3F : 0.7F))) {
			say.push_back("ThickSmoke");
		}

		// Fire around it.
		if (!burning) {
			if (edge(OnEmbers, depth == 0 && TerrainFire::IsBurningNear(feet + Vector(0.0F, 3.0F), 6))) {
				say.push_back("OnEmbers");
			}
			if (edge(NearFire, TerrainFire::IsBurningNear(position, 70))) {
				say.push_back("FireNearby");
			}
			bool friendBurning = false;
			for (const Actor* other: shared.BurningUnits) {
				if (other != &actor && other->GetTeam() == team && Within(position, other->GetPos(), 160.0F)) {
					friendBurning = true;
					break;
				}
			}
			if (edge(FriendBurning, friendBurning)) {
				say.push_back("FriendOnFire");
			}
		}
		if (edge(Hot, actor.GetHeat() > 0.6F && !burning)) {
			say.push_back("Overheating");
		}

		// The ground under its feet.
		if (depth == 0) {
			for (float down = 2.0F; down <= 10.0F; down += 4.0F) {
				unsigned char matter = MatterAt(feet + Vector(0.0F, down));
				if (matter == g_MaterialAir) {
					continue;
				}
				int ground = s_GroundOf[matter];
				if (ground != NoGround && ground != senses.Ground) {
					if (primed && senses.Ground != NoGround && c_OnGround[ground][0] != '\0') {
						say.push_back(c_OnGround[ground]);
					}
					senses.Ground = ground;
				}
				break;
			}
		}
		if (edge(Stuck, actor.NumberValueExists("LiquidStick") && actor.GetNumberValue("LiquidStick") > 0.5 && depth > 0)) {
			say.push_back("StuckFast");
		}

		// What it's covered in, out of the liquids: rain or spray, soot, snow.
		// (Not straight out of a liquid: that has its own line, and acid or mud on it isn't rain.)
		if (edge(Soaked, depth == 0 && liquid == NoLiquid && nowMS - senses.LiquidSinceMS > 3000 && actor.GetWetness() > ((senses.Flags & Soaked) ? 0.4F : 0.75F))) {
			say.push_back("Soaked");
		}
		if (edge(Sooty, actor.GetSoot() > ((senses.Flags & Sooty) ? 0.35F : 0.6F))) {
			say.push_back("Sooty");
		}
		if (edge(SnowCovered, actor.GetSnowCover() > ((senses.Flags & SnowCovered) ? 0.35F : 0.6F))) {
			say.push_back("SnowCovered");
		}

		// Digging.
		if (human) {
			const HeldDevice* held = human->GetEquippedItem();
			if (edge(Digging, held && held->IsInGroup("Tools - Diggers") && held->IsActivated())) {
				say.push_back("Digging");
			}
		}

		// Under the open sky, or a roof, or deep underground.
		const int roof = RoofOver(head);
		const bool underground = (senses.Flags & Underground) ? roof >= 24 : roof >= 64;
		if (underground && senses.CoveredSinceMS == 0) {
			senses.CoveredSinceMS = nowMS;
		}
		if (edge(Underground, underground)) {
			say.push_back("Underground");
		} else if (!underground && senses.CoveredSinceMS != 0) {
			if (primed && roof == 0 && nowMS - senses.CoveredSinceMS > 15000) {
				say.push_back("BackOutside");
			}
			if (roof == 0) {
				senses.CoveredSinceMS = 0;
			}
		}

		// Bodies it comes across.
		for (const Actor* body: shared.Bodies) {
			if (body->GetUniqueID() != senses.BodyID && Within(position, body->GetPos(), 50.0F)) {
				senses.BodyID = body->GetUniqueID();
				if (primed && body->GetTeam() >= 0) {
					say.push_back(body->GetTeam() == team ? "FriendBody" : "EnemyBody");
				}
				break;
			}
		}

		// Its side's buildings: a new recruit off the line, one standing idle for power, a generator's hum.
		for (const Colony::Building& building: Colony::Buildings()) {
			if (building.Team != team) {
				continue;
			}
			if (!primed && building.What == Colony::Kind::Barracks && actor.GetAge() < 3000) {
				for (const std::pair<Actor*, long>& unit: building.Alive) {
					if (unit.first == &actor) {
						say.push_back("ReportingForDuty");
						break;
					}
				}
			}
			if (primed && building.What == Colony::Kind::Barracks && building.NoPower && Within(position, building.Ground, 220.0F)) {
				say.push_back("NoPower");
			}
			if (primed && building.What == Colony::Kind::Generator && Within(position, building.Ground, 90.0F)) {
				say.push_back("Generator");
			}
		}

		// Night falling and day breaking (said by one or two on a side; the trigger's team cooldown sees to that).
		if (shared.Night > 0.65F && !(senses.Flags & Night)) {
			senses.Flags |= Night;
			if (primed) {
				say.push_back("Nightfall");
			}
		} else if (shared.Night < 0.3F && (senses.Flags & Night)) {
			senses.Flags &= ~static_cast<unsigned long long>(Night);
			if (primed) {
				say.push_back("Dawn");
			}
		}

		// Now and then, while it lasts: the weather where it's out in it, the dark.
		if (primed) {
			const bool exposed = roof == 0;
			if (exposed && shared.Rain > 0.7F) {
				say.push_back("Downpour");
			} else if (exposed && shared.Rain > 0.15F) {
				say.push_back("Rain");
			}
			if (exposed && shared.Snow > 0.7F) {
				say.push_back("Blizzard");
			} else if (exposed && shared.Snow > 0.15F) {
				say.push_back("Snowing");
			}
			if (exposed && shared.Dust > 0.2F) {
				say.push_back("DustStorm");
			}
			if (exposed && std::abs(shared.Wind) > 0.35F && !AirPressure::IsSheltered(position, shared.Wind)) {
				say.push_back(std::abs(shared.Wind) > 0.75F ? "Gale" : "Windy");
			}
			if (senses.Flags & Night) {
				say.push_back(underground ? "DarkUnderground" : "NightWatch");
			} else if (underground) {
				say.push_back("DarkUnderground");
			}
		}

		for (const char* trigger: say) {
			if (actor.Say(trigger)) {
				break;
			}
		}
	}
} // namespace

void UnitSpeech::NoteLightning(const Vector& position) {
	std::scoped_lock lock(s_StrikeMutex);
	s_Strikes[s_NextStrike] = {position, std::max(g_TimerMan.GetSimTimeMS(), 1LL)};
	s_NextStrike = (s_NextStrike + 1) % static_cast<int>(s_Strikes.size());
}

void UnitSpeech::UpdateWorld() {
	if (!s_Enabled || s_ChancePercent <= 0 || !g_SceneMan.GetScene()) {
		return;
	}
	const long long update = g_TimerMan.GetSimUpdateCount();
	// (Again now and then: a mod's liquid is known for one only once the liquids have read their materials.)
	if (s_TablesScene != g_SceneMan.GetScene() || update % 600 == 0) {
		BuildTables();
	}
	const long long nowMS = g_TimerMan.GetSimTimeMS();
	Shared shared;
	for (Actor* actor: g_MovableMan.GetActorList()) {
		if (actor->GetStatus() >= Actor::DYING || actor->GetTeam() < 0 || dynamic_cast<ADoor*>(actor) || dynamic_cast<ACraft*>(actor)) {
			continue;
		}
		// (A brain in a bunker has nothing to say about the rain.)
		if (actor->IsInGroup("Brains") && !dynamic_cast<AHuman*>(actor)) {
			continue;
		}
		Senses& senses = actor->GetSpeech().World;
		// Spread over the updates by unit, so a crowd doesn't all look on the same one.
		if (senses.NextLookUpdate > update + c_LookEvery) {
			senses.NextLookUpdate = 0;
		}
		if (senses.NextLookUpdate == 0) {
			senses.NextLookUpdate = update + actor->GetUniqueID() % c_LookEvery;
		}
		if (update < senses.NextLookUpdate) {
			continue;
		}
		senses.NextLookUpdate = update + c_LookEvery;
		if (!shared.Built) {
			shared.Build(nowMS);
		}
		Look(*actor, shared, nowMS);
	}
}
