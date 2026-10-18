// Battle layouts: any of the Battle Director's preset modes laid out on whatever map is loaded, from a seed, rather than drawn by hand. The
// map is surveyed for ground a unit can stand on (runs of it, column by column), each team gets a base on open ground spread across the
// map, and the mode's flags, hills, objectives, goals, ship positions or point go where its game wants them. Then it is checked: the mode's
// own Ready passes, every team can get from its base to every objective, and in the modes where every team plays the same game the teams'
// routes to the objectives cost within c_FairRatio of each other. A few tries are made with the seed, the best kept. What it makes is an
// ordinary setup on the Battle tab, to move, redraw, start or save like one drawn by hand.

#include "SandboxInternal.h"

#include <map>
#include <tuple>
#include <random>

namespace SandboxDetail {
	namespace {
		constexpr float c_Column = 16.0F; //!< The survey's columns, this far apart.
		constexpr float c_Headroom = 56.0F; //!< Air over a piece of ground for a unit to stand on it.
		constexpr float c_Climb = 24.0F; //!< The most the ground rises or falls from one column to the next along a run.
		constexpr float c_FairRatio = 1.15F; //!< The most the dearest team's routes may cost over the cheapest's.
		constexpr int c_Tries = 6; //!< Layouts tried for a seed, the best kept.

		// Layouts are drawn from RandomGenerator rather than the standard library's distributions and shuffle, which each library (MSVC's, libstdc++,
		// libc++) works out differently, so a seed gives every machine in a game the same layout.

		/// A pick in [Min, Max] from a layout's generator.
		struct Uniform {
			float Min;
			float Max;
			float operator()(RandomGenerator& random) const { return random.RandomNum(Min, Max); }
		};

		/// Puts the elements in a random order (Fisher-Yates), the same way with every standard library.
		template <typename Element>
		void Shuffle(std::vector<Element>& elements, RandomGenerator& random) {
			for (int i = static_cast<int>(elements.size()) - 1; i > 0; --i) {
				std::swap(elements[i], elements[random.RandomNum<int>(0, i)]);
			}
		}

		/// A piece of ground a unit can stand on: the top of the ground in a survey column, with air over it.
		struct Ground {
			float X = 0.0F;
			float Y = 0.0F; //!< The first solid pixel.
			bool Open = false; //!< Nothing over it all the way up: out on the surface, not in a cave or under a roof.
		};

		/// The ground of the map, as runs: each the ground across one column after another that a unit walks along, one piece a column.
		struct Survey {
			std::string Scene;
			int Width = 0;
			int Height = 0;
			bool Wraps = false;
			std::vector<std::vector<Ground>> Runs;
		};

		/// Part of a run: columns From to To (not included).
		struct Place {
			int Run = -1;
			int From = 0;
			int To = 0;
			bool Valid() const { return Run >= 0 && To > From; }
		};

		/// Something a team's units are sent to, for the checks: whose it is (-1 everyone's), so a team is judged by its routes to the others'.
		struct Target {
			Vector At;
			int Owner = -1;
		};

		/// One layout made, and how it fared.
		struct Made {
			BattleModeSettings Setup;
			std::array<Vector, c_Sides> Bases;
			std::vector<Target> Targets;
			bool Fair = true; //!< Whether fairness is checked at all (not in the one-against-the-rest modes).
			int Unreachable = 0;
			float Ratio = 1.0F;
			std::string Problem;
			float Badness() const { return static_cast<float>(Unreachable) * 1000.0F + std::max(Ratio - 1.0F, 0.0F) * 100.0F + (Problem.empty() ? 0.0F : 1.0F); }
		};

		Survey s_Survey;

		bool Solid(const Survey& survey, int x, int y) {
			if (y < 0) {
				return false;
			}
			if (y >= survey.Height) {
				return true;
			}
			if (survey.Wraps) {
				x = ((x % survey.Width) + survey.Width) % survey.Width;
			} else if (x < 0 || x >= survey.Width) {
				return false;
			}
			const unsigned char matter = g_SceneMan.GetTerrMatter(x, y);
			return matter != g_MaterialAir && !FluidSim::IsLiquid(matter);
		}

		bool Wet(const Survey& survey, int x, int y) {
			if (y < 0 || y >= survey.Height) {
				return false;
			}
			if (survey.Wraps) {
				x = ((x % survey.Width) + survey.Width) % survey.Width;
			} else if (x < 0 || x >= survey.Width) {
				return false;
			}
			return FluidSim::IsLiquid(g_SceneMan.GetTerrMatter(x, y));
		}

		/// The map's ground, surveyed afresh (trees not counted as ground, as units walk through them).
		const Survey& SurveyMap() {
			Survey& survey = s_Survey;
			survey = Survey();
			Scene* scene = g_SceneMan.GetScene();
			if (!scene) {
				return survey;
			}
			SceneMan::TreesPassable trees;
			survey.Scene = scene->GetPresetName();
			survey.Width = g_SceneMan.GetSceneWidth();
			survey.Height = g_SceneMan.GetSceneHeight();
			survey.Wraps = g_SceneMan.SceneWrapsX();
			const int columns = std::max(static_cast<int>(static_cast<float>(survey.Width) / c_Column), 1);
			std::vector<std::vector<Ground>> byColumn(columns);
			for (int column = 0; column < columns; ++column) {
				const int x = static_cast<int>((static_cast<float>(column) + 0.5F) * c_Column);
				bool covered = false;
				bool wasAir = !Solid(survey, x, 0);
				for (int y = 4; y < survey.Height - 8; y += 4) {
					const bool solid = Solid(survey, x, y);
					if (solid && wasAir) {
						int top = y;
						while (top > 0 && Solid(survey, x, top - 1)) {
							--top;
						}
						bool room = top > 30;
						for (int up = 1; room && up <= static_cast<int>(c_Headroom); up += 4) {
							room = !Solid(survey, x, top - up) && !Wet(survey, x, top - up);
						}
						if (room) {
							byColumn[column].push_back({static_cast<float>(x), static_cast<float>(top), !covered});
						}
					}
					covered |= solid;
					wasAir = !solid;
				}
			}
			// Runs: from each piece of ground no run has taken yet, along the next columns while there is ground within a climb of it.
			std::vector<std::vector<bool>> taken(columns);
			for (int column = 0; column < columns; ++column) {
				taken[column].assign(byColumn[column].size(), false);
			}
			for (int column = 0; column < columns; ++column) {
				for (size_t start = 0; start < byColumn[column].size(); ++start) {
					if (taken[column][start]) {
						continue;
					}
					std::vector<Ground> run;
					int at = column;
					size_t piece = start;
					while (true) {
						taken[at][piece] = true;
						run.push_back(byColumn[at][piece]);
						if (at + 1 >= columns) {
							break;
						}
						const float y = byColumn[at][piece].Y;
						int next = -1;
						float nearest = c_Climb;
						for (size_t other = 0; other < byColumn[at + 1].size(); ++other) {
							const float climb = std::abs(byColumn[at + 1][other].Y - y);
							if (!taken[at + 1][other] && climb <= nearest) {
								nearest = climb;
								next = static_cast<int>(other);
							}
						}
						if (next < 0) {
							break;
						}
						++at;
						piece = static_cast<size_t>(next);
					}
					survey.Runs.push_back(std::move(run));
				}
			}
			return survey;
		}

		float Across(const Survey& survey, float from, float to) {
			float dx = to - from;
			if (survey.Wraps) {
				const float width = static_cast<float>(survey.Width);
				dx = std::fmod(dx + width * 1.5F, width) - width * 0.5F;
			}
			return dx;
		}

		float Apart(const Vector& a, const Vector& b) { return g_SceneMan.ShortestDistance(a, b, g_SceneMan.SceneWrapsX()).GetMagnitude(); }

		const Ground& MiddleOf(const Survey& survey, const Place& place) { return survey.Runs[place.Run][(place.From + place.To - 1) / 2]; }

		/// Where a unit stands on a piece of ground: just over it.
		Vector Standing(const Ground& ground) { return Vector(ground.X, ground.Y - 20.0F); }

		/// A place of so many columns (fewer on a map with no run that long) whose middle is nearest a point across the map, on flat ground,
		/// out in the open if open is asked for; a little at random, by the seed.
		Place PlaceNear(const Survey& survey, float x, int columns, bool open, float reach, RandomGenerator& random, float preferHigh = 0.0F) {
			int longest = 0;
			for (const std::vector<Ground>& run: survey.Runs) {
				longest = std::max(longest, static_cast<int>(run.size()));
			}
			columns = std::clamp(columns, 1, std::max(longest, 1));
			Uniform jitter{0.0F, 1.2F};
			Place best;
			float bestScore = -std::numeric_limits<float>::max();
			for (int r = 0; r < static_cast<int>(survey.Runs.size()); ++r) {
				const std::vector<Ground>& run = survey.Runs[r];
				const int length = static_cast<int>(run.size());
				if (length < std::min(columns, 4)) {
					continue;
				}
				const int wide = std::min(columns, length);
				for (int from = 0; from + wide <= length; from += 2) {
					const Ground& middle = run[from + wide / 2];
					const float off = std::abs(Across(survey, x, middle.X));
					if (off > reach) {
						continue;
					}
					float low = middle.Y;
					float high = middle.Y;
					int openCount = 0;
					for (int i = from; i < from + wide; ++i) {
						low = std::min(low, run[i].Y);
						high = std::max(high, run[i].Y);
						openCount += run[i].Open ? 1 : 0;
					}
					float score = -off / std::max(reach, 1.0F) * 3.0F - (high - low) / 80.0F - static_cast<float>(columns - wide) * 0.15F + jitter(random);
					if (open) {
						score -= (1.0F - static_cast<float>(openCount) / static_cast<float>(wide)) * 3.0F;
					}
					score += preferHigh * (1.0F - middle.Y / static_cast<float>(std::max(survey.Height, 1)));
					if (score > bestScore) {
						bestScore = score;
						best = {r, from, from + wide};
					}
				}
			}
			return best;
		}

		/// A zone over a place: a polygon that follows the ground, so tall above it and a little into it.
		std::vector<Vector> ZoneOver(const Survey& survey, const Place& place, float tall) {
			std::vector<Vector> zone;
			if (!place.Valid()) {
				return zone;
			}
			const std::vector<Ground>& run = survey.Runs[place.Run];
			const int count = place.To - place.From;
			const int step = std::max(count / 6, 1);
			std::vector<int> picks;
			for (int i = place.From; i < place.To; i += step) {
				picks.push_back(i);
			}
			if (picks.back() != place.To - 1) {
				picks.push_back(place.To - 1);
			}
			const float half = c_Column * 0.5F;
			for (size_t p = 0; p < picks.size(); ++p) {
				const Ground& ground = run[picks[p]];
				const float x = ground.X + (p == 0 ? -half : p + 1 == picks.size() ? half : 0.0F);
				zone.emplace_back(x, ground.Y - tall);
			}
			for (size_t p = picks.size(); p-- > 0;) {
				const Ground& ground = run[picks[p]];
				const float x = ground.X + (p == 0 ? -half : p + 1 == picks.size() ? half : 0.0F);
				zone.emplace_back(x, ground.Y + 12.0F);
			}
			return zone;
		}

		/// So many columns of the same run on one side of a place (dir -1 left, 1 right), touching it; as many as there are, else the place itself.
		Place Beside(const Survey& survey, const Place& place, int dir, int columns) {
			if (!place.Valid()) {
				return place;
			}
			const int length = static_cast<int>(survey.Runs[place.Run].size());
			Place beside = place;
			if (dir < 0) {
				beside.To = place.From;
				beside.From = std::max(place.From - columns, 0);
			} else {
				beside.From = place.To;
				beside.To = std::min(place.To + columns, length);
			}
			return beside.To - beside.From >= std::max(columns / 2, 2) ? beside : place;
		}

		/// Columns a place of so many pixels across takes.
		int ColumnsFor(float pixels) { return std::max(static_cast<int>(pixels / c_Column), 2); }

		/// How wide a team's spawn zone is, for a team of its size.
		float BaseWidth(const BattleModeSettings& setup, int side) { return std::clamp(100.0F + 12.0F * static_cast<float>(setup.TeamSize[side]), 160.0F, 420.0F); }

		/// What the route from one place to another costs a soldier on a team, as the pathfinder prices it; -1 for none. Remembered for the layout under way.
		std::map<std::tuple<int, int, int, int, int>, float> s_Costs;
		float RouteCostFor(int side, const Vector& from, const Vector& to, std::list<Vector>* path = nullptr) {
			Scene* scene = g_SceneMan.GetScene();
			if (!scene) {
				return -1.0F;
			}
			const auto key = std::make_tuple(side, from.GetFloorIntX() / 8, from.GetFloorIntY() / 8, to.GetFloorIntX() / 8, to.GetFloorIntY() / 8);
			if (!path) {
				if (auto found = s_Costs.find(key); found != s_Costs.end()) {
					return found->second;
				}
			}
			// (A soldier with a modest jetpack, as the navigation overlay draws for one: what most units can manage.)
			PathAgent agent;
			agent.JumpHeight = 6.0F;
			agent.StandHeight = 44.0F;
			agent.CrawlHeight = 24.0F;
			agent.CrouchHeight = 32.0F;
			agent.HalfWidth = 8.0F;
			agent.WalksStairs = true;
			agent.ClimbsLadders = true;
			agent.Scrambles = true;
			agent.MantleHeight = 30.0F;
			std::list<Vector> route;
			const float cost = scene->CalculatePath(from, to, path ? *path : route, agent, static_cast<Activity::Teams>(side));
			s_Costs[key] = cost;
			return cost;
		}

		/// The ground nearest a point, of the pieces surveyed (the point itself, with none near).
		Vector GroundNear(const Survey& survey, const Vector& point, float within = 300.0F) {
			Vector best = point;
			float nearest = within;
			for (const std::vector<Ground>& run: survey.Runs) {
				for (const Ground& ground: run) {
					const float apart = Apart(Vector(ground.X, ground.Y), point);
					if (apart < nearest) {
						nearest = apart;
						best = Vector(ground.X, ground.Y);
					}
				}
			}
			return best;
		}

		/// Places across the map for the teams' bases: each end for two (or spread evenly across a map that wraps), and spread between them for more.
		std::vector<float> BaseSpots(const Survey& survey, int count, RandomGenerator& random) {
			std::vector<float> spots;
			const float width = static_cast<float>(survey.Width);
			if (survey.Wraps) {
				const float first = Uniform{0.0F, width}(random);
				for (int i = 0; i < count; ++i) {
					spots.push_back(std::fmod(first + width * static_cast<float>(i) / static_cast<float>(count), width));
				}
			} else {
				// (How far in from the ends, by the seed: the same both ends, so neither team is nearer the middle.)
				const float margin = std::max(150.0F, width * Uniform{0.04F, 0.22F}(random));
				Uniform nudge{-width * 0.04F, width * 0.04F};
				for (int i = 0; i < count; ++i) {
					const bool end = i == 0 || i == count - 1;
					spots.push_back(count == 1 ? width * 0.5F : margin + (width - margin * 2.0F) * static_cast<float>(i) / static_cast<float>(count - 1) + (end ? 0.0F : nudge(random)));
				}
			}
			return spots;
		}

		/// Which way is away from a team's enemies, across the map: -1 left, 1 right.
		int AwayFrom(const Survey& survey, const Vector& base, const std::vector<Vector>& enemies) {
			float toward = 0.0F;
			for (const Vector& enemy: enemies) {
				toward += Across(survey, base.m_X, enemy.m_X);
			}
			if (enemies.empty() || std::abs(toward) < 1.0F) {
				return base.m_X < static_cast<float>(survey.Width) * 0.5F ? -1 : 1;
			}
			return toward > 0.0F ? -1 : 1;
		}

		/// The piece of ground that bases reach most evenly, among those not too near any of them and filtered as asked: a few likely ones by
		/// how far they are as the crow flies, then the fairest of those by the routes there. -1 run for none.
		Place FairPlace(const Survey& survey, const std::vector<int>& sides, const std::array<Vector, c_Sides>& bases, int columns, bool open, float preferHigh, const std::vector<Vector>& avoid, float avoidBy, RandomGenerator& random) {
			struct Candidate {
				Place At;
				float Score;
			};
			std::vector<Candidate> candidates;
			Uniform jitter{0.0F, 0.35F};
			for (int r = 0; r < static_cast<int>(survey.Runs.size()); ++r) {
				const std::vector<Ground>& run = survey.Runs[r];
				const int length = static_cast<int>(run.size());
				if (length < std::min(columns, 4)) {
					continue;
				}
				const int wide = std::min(columns, length);
				for (int from = 0; from + wide <= length; from += 3) {
					const Ground& middle = run[from + wide / 2];
					if (open && !middle.Open) {
						continue;
					}
					const Vector at = Standing(middle);
					if (std::any_of(avoid.begin(), avoid.end(), [&](const Vector& other) { return Apart(other, at) < avoidBy; })) {
						continue;
					}
					float low = std::numeric_limits<float>::max();
					float high = 0.0F;
					float sum = 0.0F;
					for (int side: sides) {
						const float apart = Apart(bases[side], at);
						low = std::min(low, apart);
						high = std::max(high, apart);
						sum += apart;
					}
					const float mean = sum / static_cast<float>(std::max(sides.size(), size_t(1)));
					const float score = -(high - low) / std::max(mean, 1.0F) - mean / static_cast<float>(survey.Width) * 0.5F + preferHigh * (1.0F - middle.Y / static_cast<float>(survey.Height)) + jitter(random);
					candidates.push_back({{r, from, from + wide}, score});
				}
			}
			if (candidates.empty()) {
				return Place();
			}
			std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.Score > b.Score; });
			candidates.resize(std::min(candidates.size(), size_t(8)));
			Place best = candidates.front().At;
			float bestScore = std::numeric_limits<float>::max();
			std::vector<Place> fair; //!< Those every team reaches with routes within c_FairRatio: one of them is picked at random, by the seed.
			for (const Candidate& candidate: candidates) {
				const Vector at = Standing(MiddleOf(survey, candidate.At));
				float low = std::numeric_limits<float>::max();
				float high = 0.0F;
				int missing = 0;
				for (int side: sides) {
					const float cost = RouteCostFor(side, bases[side], at);
					if (!RouteReachable(cost)) {
						++missing;
						continue;
					}
					low = std::min(low, cost);
					high = std::max(high, cost);
				}
				const float ratio = low > 0.0F && low < std::numeric_limits<float>::max() ? high / low : 1.0F;
				const float score = static_cast<float>(missing) * 1000.0F + ratio - candidate.Score * 0.1F;
				if (missing == 0 && ratio <= c_FairRatio) {
					fair.push_back(candidate.At);
				}
				if (score < bestScore) {
					bestScore = score;
					best = candidate.At;
				}
			}
			if (!fair.empty()) {
				return fair[random.RandomNum<int>(0, static_cast<int>(fair.size()) - 1)];
			}
			return best;
		}

		/// A place in open sky for a ship over a piece of ground: high enough over it, with air all round. False if there is no room.
		bool SkyOver(const Survey& survey, const Ground& ground, Vector& at) {
			at = Vector(ground.X, std::max(ground.Y - 220.0F, 100.0F));
			if (ground.Y - at.m_Y < 120.0F) {
				return false;
			}
			for (float dx = -100.0F; dx <= 100.0F; dx += 20.0F) {
				for (float dy = -60.0F; dy <= 60.0F; dy += 20.0F) {
					if (Solid(survey, static_cast<int>(at.m_X + dx), static_cast<int>(at.m_Y + dy))) {
						return false;
					}
				}
			}
			return true;
		}

		/// Points evenly along a route, from one share of the way to another.
		std::vector<Vector> AlongRoute(const std::list<Vector>& route, int count, float from, float to) {
			std::vector<Vector> points;
			if (route.size() < 2 || count <= 0) {
				return points;
			}
			const bool wraps = g_SceneMan.SceneWrapsX();
			std::vector<float> lengths(1, 0.0F);
			for (auto a = route.begin(), b = std::next(route.begin()); b != route.end(); ++a, ++b) {
				lengths.push_back(lengths.back() + g_SceneMan.ShortestDistance(*a, *b, wraps).GetMagnitude());
			}
			const std::vector<Vector> steps(route.begin(), route.end());
			for (int i = 0; i < count; ++i) {
				const float share = count == 1 ? (from + to) * 0.5F : from + (to - from) * static_cast<float>(i) / static_cast<float>(count - 1);
				const float wanted = lengths.back() * share;
				const size_t at = static_cast<size_t>(std::lower_bound(lengths.begin(), lengths.end(), wanted) - lengths.begin());
				points.push_back(steps[std::min(at, steps.size() - 1)]);
			}
			return points;
		}

		float RouteLength(const std::list<Vector>& route) {
			float length = 0.0F;
			const bool wraps = g_SceneMan.SceneWrapsX();
			for (auto a = route.begin(), b = route.begin(); b != route.end(); a = b++) {
				if (a != b) {
					length += g_SceneMan.ShortestDistance(*a, *b, wraps).GetMagnitude();
				}
			}
			return length;
		}

		/// A place on the run under the ground nearest a point, so many columns wide.
		Place PlaceAt(const Survey& survey, const Vector& point, int columns, RandomGenerator& random) {
			const Vector ground = GroundNear(survey, point);
			return PlaceNear(survey, ground.m_X, columns, false, 400.0F, random, 0.0F);
		}

		/// Puts a team's base down: its spawn zone over a place, and where it is.
		void PutBase(Made& made, const Survey& survey, int side, const Place& place) {
			if (!place.Valid()) {
				return;
			}
			made.Setup.SpawnZones[side].push_back(ZoneOver(survey, place, 80.0F));
			if (made.Bases[side].IsZero()) {
				made.Bases[side] = Standing(MiddleOf(survey, place));
			}
		}

		/// The middle of a zone drawn as a polygon (its corners' average).
		Vector CornersMiddle(const std::vector<Vector>& zone) {
			Vector middle;
			for (const Vector& corner: zone) {
				middle += corner / static_cast<float>(std::max(zone.size(), size_t(1)));
			}
			return middle;
		}

		/// More spawn zones for each team, up to so many a team: round its base, nearer it than any enemy base, apart from every zone
		/// already drawn, that the team can walk from to its base. Out in the open mostly, now and then in a cave or under cover.
		void ExtraZones(Made& made, const Survey& survey, const std::vector<int>& sides, int perTeam, RandomGenerator& random) {
			perTeam = std::clamp(perTeam, 1, 4);
			const float width = static_cast<float>(survey.Width);
			for (int side: sides) {
				std::vector<std::vector<Vector>>& zones = made.Setup.SpawnZones[side];
				if (zones.empty() || static_cast<int>(zones.size()) >= perTeam) {
					continue;
				}
				const Vector base = made.Bases[side];
				float nearestEnemy = width;
				for (int other: sides) {
					if (other != side && !made.Bases[other].IsZero()) {
						nearestEnemy = std::min(nearestEnemy, Apart(base, made.Bases[other]));
					}
				}
				Uniform distance{250.0F, std::max(400.0F, std::min(nearestEnemy * 0.35F, 900.0F))};
				for (int tries = 0; tries < 30 && static_cast<int>(zones.size()) < perTeam; ++tries) {
					const float dir = random.RandomNum<int>(0, 1) == 0 ? -1.0F : 1.0F;
					float x = base.m_X + dir * distance(random);
					if (!survey.Wraps) {
						x = std::clamp(x, 0.0F, width);
					}
					const bool open = Uniform{0.0F, 1.0F}(random) > 0.25F;
					const Place place = PlaceNear(survey, x, ColumnsFor(BaseWidth(made.Setup, side) * 0.7F), open, 250.0F, random);
					if (!place.Valid()) {
						continue;
					}
					const Vector at = Standing(MiddleOf(survey, place));
					bool clear = std::none_of(made.Targets.begin(), made.Targets.end(), [&at](const Target& target) { return Apart(target.At, at) < 250.0F; });
					for (int other = 0; other < c_Sides && clear; ++other) {
						for (const std::vector<Vector>& zone: made.Setup.SpawnZones[other]) {
							clear &= Apart(CornersMiddle(zone), at) > 180.0F;
						}
						if (other != side && !made.Bases[other].IsZero() && std::find(sides.begin(), sides.end(), other) != sides.end()) {
							clear &= Apart(at, base) < Apart(at, made.Bases[other]) * 0.6F;
						}
					}
					if (clear && RouteReachable(RouteCostFor(side, at, base))) {
						zones.push_back(ZoneOver(survey, place, 80.0F));
					}
				}
			}
		}

		/// One layout of the mode on the map, with a seed. The teams' bases first, then the mode's own.
		Made LayOut(const BattleModeSettings& start, const Survey& survey, const std::vector<int>& sides, unsigned seed, int zonesPerTeam) {
			RandomGenerator random;
			random.Seed(seed);
			Made made;
			BattleModeSettings& setup = made.Setup;
			setup = start;
			for (int side = 0; side < c_Sides; ++side) {
				if (setup.Plays[side]) {
					setup.SpawnZones[side].clear();
					setup.ZoneSpawnOrders[side].clear();
				}
				setup.HasPoint[side] = false;
				setup.Goals[side].clear();
			}
			setup.Zones.clear();
			setup.FlagSpots.clear();
			setup.FlagByZones = false;
			const float width = static_cast<float>(survey.Width);
			const float reach = std::max(width * 0.2F, 400.0F);
			std::vector<int> order = sides;
			Shuffle(order, random);
			auto enemiesOf = [&](int side) {
				std::vector<Vector> enemies;
				for (int other: sides) {
					if (other != side) {
						enemies.push_back(made.Bases[other]);
					}
				}
				return enemies;
			};
			// The one team set against the rest, in the modes with one: it takes one end of the map and the rest the other.
			int lone = -1;
			switch (setup.Mode) {
				case BattleMode::Assault: lone = std::clamp(setup.Attacker, 0, c_Sides - 1); break;
				case BattleMode::WaveDefence: lone = std::clamp(setup.Defender, 0, c_Sides - 1); break;
				case BattleMode::Escort: lone = std::clamp(setup.EscortSide, 0, c_Sides - 1); break;
				case BattleMode::Extraction: lone = std::clamp(setup.ExtractSide, 0, c_Sides - 1); break;
				default: break;
			}
			made.Fair = lone < 0;
			std::vector<Place> basePlaces(c_Sides);
			if (lone >= 0) {
				// (Either end, by the seed.)
				std::vector<float> ends = BaseSpots(survey, 2, random);
				if (random.RandomNum<int>(0, 1) == 1) {
					std::swap(ends[0], ends[1]);
				}
				const float endA = ends[0];
				const float endB = ends[1];
				const float middle = survey.Wraps ? std::fmod(endA + width * 0.25F, width) : width * 0.5F;
				std::vector<int> rest;
				for (int side: order) {
					if (side != lone) {
						rest.push_back(side);
					}
				}
				const float spread = std::min(width * 0.08F, 400.0F);
				// (Each of the rest a little apart round a place, when there are a few of them.)
				auto restSpot = [&](float around, size_t i) { return around + (static_cast<float>(i) - static_cast<float>(rest.size() - 1) * 0.5F) * spread; };
				if (setup.Mode == BattleMode::WaveDefence) {
					// The defence zone on high ground away from the ends, under cover if there is any; the defenders in it, the attackers at both ends.
					Place hold = PlaceNear(survey, middle, ColumnsFor(260.0F), false, width * 0.3F, random, 1.5F);
					if (hold.Valid()) {
						setup.Zones.push_back(ZoneOver(survey, hold, 110.0F));
						Place inside = hold;
						const int trim = (hold.To - hold.From) / 5;
						inside.From += trim;
						inside.To -= trim;
						PutBase(made, survey, lone, inside);
						made.Targets.push_back({Standing(MiddleOf(survey, hold)), lone});
					}
					for (size_t i = 0; i < rest.size(); ++i) {
						const int side = rest[i];
						PutBase(made, survey, side, PlaceNear(survey, restSpot(endA, i), ColumnsFor(BaseWidth(setup, side)), true, reach, random));
						PutBase(made, survey, side, PlaceNear(survey, restSpot(endB, i), ColumnsFor(BaseWidth(setup, side)), true, reach, random));
					}
				} else {
					basePlaces[lone] = PlaceNear(survey, endA, ColumnsFor(BaseWidth(setup, lone)), true, reach, random);
					PutBase(made, survey, lone, basePlaces[lone]);
					// The far end: where the destination or extraction zone goes, or the defenders' base past the last objective.
					const Place farEnd = PlaceNear(survey, endB, ColumnsFor(220.0F), true, reach, random);
					const Vector farMiddle = farEnd.Valid() ? Standing(MiddleOf(survey, farEnd)) : Vector(endB, static_cast<float>(survey.Height) * 0.5F);
					if (setup.Mode == BattleMode::Assault) {
						for (size_t i = 0; i < rest.size(); ++i) {
							PutBase(made, survey, rest[i], PlaceNear(survey, restSpot(endB, i), ColumnsFor(BaseWidth(setup, rest[i])), true, reach, random));
						}
						// Objectives along the attackers' way to the defenders, spaced out, the last short of the defenders' base.
						const int defender = rest.empty() ? lone : rest.front();
						std::list<Vector> route;
						const float cost = RouteCostFor(lone, made.Bases[lone], made.Bases[defender], &route);
						const float length = RouteReachable(cost) ? RouteLength(route) : std::abs(Across(survey, made.Bases[lone].m_X, made.Bases[defender].m_X));
						const int count = std::clamp(static_cast<int>(std::lround(length / 1000.0F)), 3, 5);
						std::vector<Vector> points = RouteReachable(cost) ? AlongRoute(route, count, 0.2F, 0.8F) : std::vector<Vector>();
						if (points.empty()) {
							for (int i = 0; i < count; ++i) {
								const float share = 0.2F + 0.6F * static_cast<float>(i) / static_cast<float>(count - 1);
								points.emplace_back(made.Bases[lone].m_X + Across(survey, made.Bases[lone].m_X, made.Bases[defender].m_X) * share, made.Bases[lone].m_Y);
							}
						}
						for (const Vector& point: points) {
							const Place objective = PlaceAt(survey, point, ColumnsFor(200.0F), random);
							if (objective.Valid()) {
								setup.Zones.push_back(ZoneOver(survey, objective, 100.0F));
								made.Targets.push_back({Standing(MiddleOf(survey, objective)), defender});
							}
						}
					} else if (setup.Mode == BattleMode::Escort) {
						if (farEnd.Valid()) {
							setup.Zones.push_back(ZoneOver(survey, farEnd, 100.0F));
							made.Targets.push_back({farMiddle, lone});
						}
						// The hunters near the middle and toward the far end.
						for (size_t i = 0; i < rest.size(); ++i) {
							const int side = rest[i];
							PutBase(made, survey, side, PlaceNear(survey, restSpot(middle, i), ColumnsFor(BaseWidth(setup, side)), true, reach, random));
							const float nearFar = survey.Wraps ? endB - width * 0.1F : endB + (endB > endA ? -1.0F : 1.0F) * width * 0.12F;
							PutBase(made, survey, side, PlaceNear(survey, restSpot(nearFar, i), ColumnsFor(BaseWidth(setup, side)), true, reach, random));
						}
					} else if (setup.Mode == BattleMode::Extraction) {
						if (farEnd.Valid()) {
							setup.Zones.push_back(ZoneOver(survey, farEnd, 100.0F));
							made.Targets.push_back({farMiddle, lone});
						}
						// The holders round the zone, either side of it.
						for (size_t i = 0; i < rest.size(); ++i) {
							const int side = rest[i];
							const float off = 300.0F + 150.0F * static_cast<float>(i);
							PutBase(made, survey, side, PlaceNear(survey, farMiddle.m_X - off, ColumnsFor(BaseWidth(setup, side)), true, reach, random));
							PutBase(made, survey, side, PlaceNear(survey, farMiddle.m_X + off, ColumnsFor(BaseWidth(setup, side)), true, reach, random));
						}
					}
				}
			} else {
				const std::vector<float> spots = BaseSpots(survey, static_cast<int>(order.size()), random);
				for (size_t i = 0; i < order.size(); ++i) {
					const int side = order[i];
					basePlaces[side] = PlaceNear(survey, spots[i], ColumnsFor(BaseWidth(setup, side)), true, reach, random);
					PutBase(made, survey, side, basePlaces[side]);
				}
				std::vector<Vector> baseList;
				for (int side: sides) {
					baseList.push_back(made.Bases[side]);
				}
				switch (setup.Mode) {
					case BattleMode::CaptureTheFlag:
					case BattleMode::VipHunt:
						// The flag behind the spawn zone, away from the enemy; the VIP in the middle of its base.
						for (int side: sides) {
							if (!basePlaces[side].Valid()) {
								continue;
							}
							Vector at = made.Bases[side];
							if (setup.Mode == BattleMode::CaptureTheFlag) {
								const Place behind = Beside(survey, basePlaces[side], AwayFrom(survey, made.Bases[side], enemiesOf(side)), ColumnsFor(120.0F));
								const Ground& ground = MiddleOf(survey, behind);
								at = Vector(ground.X, ground.Y - 8.0F);
							}
							setup.Points[side] = at;
							setup.HasPoint[side] = true;
							made.Targets.push_back({at, side});
						}
						break;
					case BattleMode::KingOfTheHill: {
						// One hill, or a few along the middle when the hill moves on or hills are played for.
						const int hills = setup.HillMoveSeconds > 0 || setup.HillsToWin > 0 ? 3 : 1;
						std::vector<Vector> avoid = baseList;
						for (int i = 0; i < hills; ++i) {
							const Place hill = FairPlace(survey, sides, made.Bases, ColumnsFor(240.0F), false, 0.6F, avoid, i == 0 ? 400.0F : 500.0F, random);
							if (!hill.Valid()) {
								break;
							}
							setup.Zones.push_back(ZoneOver(survey, hill, 100.0F));
							const Vector at = Standing(MiddleOf(survey, hill));
							made.Targets.push_back({at, -1});
							avoid.push_back(at);
						}
						break;
					}
					case BattleMode::OneFlag: {
						std::vector<Vector> avoid = baseList;
						for (int i = 0; i < 3; ++i) {
							const Place spot = FairPlace(survey, sides, made.Bases, 4, false, 0.0F, avoid, i == 0 ? 400.0F : 300.0F, random);
							if (!spot.Valid()) {
								break;
							}
							const Ground& ground = MiddleOf(survey, spot);
							setup.FlagSpots.emplace_back(ground.X, ground.Y - 8.0F);
							made.Targets.push_back({Standing(ground), -1});
							avoid.push_back(Standing(ground));
						}
						for (int side: sides) {
							if (basePlaces[side].Valid()) {
								setup.Goals[side] = ZoneOver(survey, Beside(survey, basePlaces[side], AwayFrom(survey, made.Bases[side], enemiesOf(side)), ColumnsFor(140.0F)), 90.0F);
							}
						}
						break;
					}
					case BattleMode::CaptureTheShip: {
						// Ship positions in open sky over the ground most evenly between the bases; each team's landing zone on wide flat open ground at its base.
						std::vector<Vector> avoid = baseList;
						for (int tries = 0; tries < 12 && setup.FlagSpots.size() < 3; ++tries) {
							const Place under = FairPlace(survey, sides, made.Bases, 6, true, 0.0F, avoid, 350.0F, random);
							if (!under.Valid()) {
								break;
							}
							const Ground& ground = MiddleOf(survey, under);
							avoid.push_back(Standing(ground));
							Vector sky;
							if (SkyOver(survey, ground, sky)) {
								setup.FlagSpots.push_back(sky);
								made.Targets.push_back({Standing(ground), -1});
							}
						}
						for (int side: sides) {
							if (basePlaces[side].Valid()) {
								const Place pad = Beside(survey, basePlaces[side], -AwayFrom(survey, made.Bases[side], enemiesOf(side)), ColumnsFor(240.0F));
								setup.Goals[side] = ZoneOver(survey, pad, 160.0F);
							}
						}
						break;
					}
					case BattleMode::ThePoint: {
						const Place point = FairPlace(survey, sides, made.Bases, ColumnsFor(200.0F), true, 0.0F, baseList, 500.0F, random);
						if (point.Valid()) {
							const Ground& ground = MiddleOf(survey, point);
							setup.FlagSpots.assign(1, Vector(ground.X, ground.Y - 8.0F));
							made.Targets.push_back({Standing(ground), -1});
							// The rings cut down to fit the ground round the point, so its outer ring isn't mostly in the air or the hills.
							const int length = static_cast<int>(survey.Runs[point.Run].size());
							const int middle = (point.From + point.To - 1) / 2;
							const float room = static_cast<float>(std::min(middle, length - 1 - middle)) * c_Column * 1.3F;
							const int rings = std::clamp(setup.PointRings, 0, c_MaxPointRings);
							const float outer = static_cast<float>(setup.RingRadius[rings]);
							if (outer > room && outer > 0.0F) {
								const float scale = std::clamp(room / outer, 0.5F, 1.0F);
								for (int band = 0; band <= rings; ++band) {
									setup.RingRadius[band] = std::max(static_cast<int>(static_cast<float>(setup.RingRadius[band]) * scale), band == 0 ? 30 : setup.RingRadius[band - 1] + 20);
								}
							}
						}
						break;
					}
					default:
						// (Last team standing: the bases are all.)
						break;
				}
			}
			ExtraZones(made, survey, sides, zonesPerTeam, random);
			// Pickups, if the battle has them: spots along the way between the first two bases.
			if (setup.Pickups && sides.size() >= 2) {
				std::list<Vector> route;
				if (RouteReachable(RouteCostFor(sides[0], made.Bases[sides[0]], made.Bases[sides[1]], &route))) {
					setup.PickupSpots.clear();
					for (const Vector& point: AlongRoute(route, 4, 0.2F, 0.8F)) {
						const Vector ground = GroundNear(survey, point, 200.0F);
						setup.PickupSpots.emplace_back(ground.m_X, ground.m_Y - 10.0F);
					}
					if (!setup.PickupSpots.empty()) {
						setup.PickupPlaces |= PickupAtSpots;
					}
				}
			}
			for (int side = 0; side < c_Sides; ++side) {
				for (std::vector<Vector>& zone: setup.SpawnZones[side]) {
					for (Vector& corner: zone) {
						corner.m_Y = std::clamp(corner.m_Y, 0.0F, static_cast<float>(survey.Height) - 1.0F);
					}
				}
			}
			return made;
		}

		/// How a layout fares: the mode's own Ready, every team's way to every objective, and how fairly the teams' ways cost.
		void Judge(Made& made, const std::vector<int>& sides) {
			std::string why;
			if (!BattleModeSetupReady(made.Setup, why)) {
				made.Problem = why;
			}
			for (int side: sides) {
				if (made.Bases[side].IsZero()) {
					made.Problem = std::string(c_SideNames[side]) + " has no ground for a base.";
					++made.Unreachable;
				}
			}
			std::array<float, c_Sides> total{};
			std::array<int, c_Sides> counted{};
			std::string lost;
			for (int side: sides) {
				for (const Target& target: made.Targets) {
					const float cost = RouteCostFor(side, made.Bases[side], target.At);
					if (!RouteReachable(cost)) {
						++made.Unreachable;
						if (lost.empty()) {
							lost = std::string(c_SideNames[side]) + " can't reach every objective";
						}
						continue;
					}
					if (target.Owner != side) {
						total[side] += cost;
						++counted[side];
					}
				}
			}
			if (!lost.empty() && made.Problem.empty()) {
				made.Problem = lost + ".";
			}
			if (made.Fair) {
				float low = std::numeric_limits<float>::max();
				float high = 0.0F;
				int lowSide = -1;
				int highSide = -1;
				for (int side: sides) {
					if (counted[side] == 0) {
						continue;
					}
					const float mean = total[side] / static_cast<float>(counted[side]);
					if (mean < low) {
						low = mean;
						lowSide = side;
					}
					if (mean > high) {
						high = mean;
						highSide = side;
					}
				}
				if (lowSide >= 0 && low > 0.0F) {
					made.Ratio = high / low;
					if (made.Ratio > c_FairRatio && made.Problem.empty()) {
						made.Problem = std::string(c_SideNames[highSide]) + " is " + std::to_string(static_cast<int>(std::lround((made.Ratio - 1.0F) * 100.0F))) + "% further from the objectives than " + c_SideNames[lowSide] + ".";
					}
				}
			}
		}
	} // namespace

	bool LayoutBattle(BattleModeSettings& setup, unsigned seed, int zonesPerTeam, std::string& note) {
		if (setup.Mode == BattleMode::Custom) {
			note = "Pick a mode first.";
			return false;
		}
		const Survey& survey = SurveyMap();
		if (survey.Runs.empty()) {
			note = "There's no ground on this map to lay a battle out on.";
			return false;
		}
		std::vector<int> sides;
		for (int side = 0; side < c_Sides; ++side) {
			if (setup.Plays[side]) {
				sides.push_back(side);
			}
		}
		if (sides.size() < 2) {
			// (Two teams at least: red and green, as a fresh Battle tab has.)
			setup.Plays[0] = setup.Plays[1] = true;
			sides = {0, 1};
			for (int side = 2; side < c_Sides; ++side) {
				if (setup.Plays[side]) {
					sides.push_back(side);
				}
			}
		}
		// (The team set against the rest has to play, in the modes with one.)
		for (int lone: {setup.Mode == BattleMode::Assault ? setup.Attacker : -1, setup.Mode == BattleMode::WaveDefence ? setup.Defender : -1, setup.Mode == BattleMode::Escort ? setup.EscortSide : -1, setup.Mode == BattleMode::Extraction ? setup.ExtractSide : -1}) {
			if (lone >= 0 && lone < c_Sides && !setup.Plays[lone]) {
				setup.Plays[lone] = true;
				sides.push_back(lone);
				std::sort(sides.begin(), sides.end());
			}
		}
		s_Costs.clear();
		Made best;
		bool any = false;
		for (int attempt = 0; attempt < c_Tries; ++attempt) {
			Made made = LayOut(setup, survey, sides, seed * 7919U + static_cast<unsigned>(attempt), zonesPerTeam);
			Judge(made, sides);
			if (!any || made.Badness() < best.Badness()) {
				best = std::move(made);
				any = true;
			}
			if (best.Problem.empty()) {
				break;
			}
		}
		s_Costs.clear();
		setup = best.Setup;
		note = "Laid out with seed " + std::to_string(seed) + (best.Problem.empty() ? "." : ", but: " + best.Problem);
		return true;
	}

	void BattleLayoutPanel(BattleModeSettings& setup, bool& changed) {
		static int s_Seed = 1;
		static int s_ZonesPerTeam = 1;
		static std::string s_Note;
		static std::string s_NoteScene;
		Scene* scene = g_SceneMan.GetScene();
		const std::string sceneName = scene ? scene->GetPresetName() : "";
		if (s_NoteScene != sceneName) {
			s_Note.clear();
			s_NoteScene = sceneName;
		}
		ImGui::SeparatorText("Generate layout");
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0F);
		ImGui::InputInt("Seed##layoutSeed", &s_Seed);
		s_Seed = std::max(s_Seed, 0);
		ImGui::SetItemTooltip("The same seed on the same map lays the same mode out the same way.");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0F);
		ImGui::SliderInt("Spawn zones##layoutZones", &s_ZonesPerTeam, 1, 4, s_ZonesPerTeam == 1 ? "1 a team" : "%d a team");
		ImGui::SetItemTooltip("Spawn zones each team gets: its base, and more round it (nearer it than any enemy, mostly out in the open, now and then in a cave or under cover).");
		const bool regenerate = ToolUI::Button("New seed##layoutDice");
		ImGui::SetItemTooltip("A new seed, picked at random, and the mode laid out with it.");
		if (regenerate) {
			s_Seed = static_cast<int>(std::random_device()() % 1000000U);
		}
		ImGui::SameLine();
		const bool generate = ToolUI::Button("Generate layout##layoutGo", ImVec2(-1.0F, 0.0F));
		ImGui::SetItemTooltip("This mode's spawn zones, flags, hills, objectives, goals or ship positions, placed on this map for the teams ticked: bases spread across the map on open ground, and the rest where every team can get to it, as evenly as the map allows. It replaces what is drawn for the mode now. You're asked first when something is (Ctrl+click: not).");
		bool drawn = !setup.Zones.empty() || !setup.FlagSpots.empty();
		for (int side = 0; side < c_Sides; ++side) {
			drawn |= setup.Plays[side] && (!setup.SpawnZones[side].empty() || setup.HasPoint[side] || !setup.Goals[side].empty());
		}
		const bool clicked = generate || regenerate;
		const bool go = drawn ? AskFirst(clicked, "Generate layout?##battleLayout", "Lay the mode out afresh? What is drawn for it now (spawn zones, flags, hills, objectives, goals) is replaced.", "Yes, lay it out") : clicked;
		if (go) {
			if (IsBattleTool(CurrentTool().Kind)) {
				PutDownBattleTool();
			}
			if (LayoutBattle(setup, static_cast<unsigned>(s_Seed), s_ZonesPerTeam, s_Note)) {
				for (int side = 0; side < c_Sides; ++side) {
					if (setup.Plays[side]) {
						s_BattleSetup[side].SpawnZones = setup.SpawnZones[side];
						SendBattleSettings(side);
					}
				}
				changed = true;
			}
		}
		if (!s_Note.empty()) {
			ImGui::TextWrapped("%s", s_Note.c_str());
		}
	}
} // namespace SandboxDetail

bool Sandbox::LayoutBattleMode(int mode, int seed, int zonesPerTeam) {
	using namespace SandboxDetail;
	if (!InGame() || s_ModeRun.Running) {
		return false;
	}
	BattleModeSettings settings = s_ModeRun.Settings;
	settings.Mode = static_cast<BattleMode>(std::clamp(mode, 0, static_cast<int>(BattleMode::Count) - 1));
	std::string note;
	if (!LayoutBattle(settings, static_cast<unsigned>(std::max(seed, 0)), zonesPerTeam, note)) {
		return false;
	}
	for (int side = 0; side < c_Sides; ++side) {
		if (settings.Plays[side]) {
			s_BattleTeams[side].Settings.SpawnZones = settings.SpawnZones[side];
			s_BattleSetup[side].SpawnZones = settings.SpawnZones[side];
		}
	}
	s_ModeSetup = settings;
	Stroke stroke;
	stroke.Kind = Tool::BattleTeam;
	stroke.Team = -1;
	stroke.Count = BattleModeSet;
	stroke.Mode = settings;
	ApplyBattleMode(stroke);
	return true;
}
