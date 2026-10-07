function AIBunkerScript:StartScript()
	self.timer = Timer();
	-- An indoor gym: a three-storey bunker built of the game's own modules in the sky over Ketanot Hills (the sandbox map has no bunker of
	-- its own): a shaft from the bottom corridor to a room at the top, a hatch between the bottom and middle storeys, a hub where the
	-- middle corridor crosses a column that runs from the bottom corridor up to a gallery, steep stairs from the middle storey to a
	-- landing room at the top, dead ends and a corner. Every opening is meant: the modules' ports (each has 48 px openings on set sides)
	-- were read off their material bitmaps, and the whole is sealed, with nothing open to the sky. Units are put down inside it and sent
	-- to other places inside it, one course each at the same time, and how they do is written up as AIBUNKER lines: the path the finder
	-- gives, a line a second per unit, and arrived / gave up. CCCP_BUNKER_TRACE is the course whose unit writes AITRACE lines.
	-- (The first layout had Hub A everywhere a junction was wanted; a hub is open on all four sides, so stacked hubs were chimneys open
	-- to the sky at both ends, the bottom storey had holes in its floor, the shaft's top was capped by the tunnel over it, and one course's
	-- goal hung in a chimney, where the pather dropped it to the hills below. Units fell out of the bunker and climbed back in all day.)
	-- On a real map (the scenario's scene) nothing is built: the map's own bunker is the course, with courses of its own below.
	-- CCCP_BUNKER_LOOK as "x,y,zoom" puts the camera somewhere at a zoom, for a capture of a layout, and runs no courses.
	self.lookAt = Vector(1848, 330);
	self.traceCourse = 1;
	if os and os.getenv and tonumber(os.getenv("CCCP_BUNKER_TRACE") or "") then
		self.traceCourse = tonumber(os.getenv("CCCP_BUNKER_TRACE"));
	end
	-- CCCP_RECORD=1: a replay of the traced course's unit (Replay.ps1): the camera follows it with the navigation overlay on, a frame of the
	-- screen is saved five times a second, and each is marked in the log ("REPLAY frame n") so the trace lines can be laid against them.
	if os and os.getenv and os.getenv("CCCP_RECORD") == "1" then
		self.record = true;
		pcall(function() SettingsMan.NavDebugOverlay = tonumber(os.getenv("CCCP_RECORD_OVERLAY") or "2") or 2; end);
	end
	-- CCCP_NAV_DEBUG as a level (1 the grid, 2 the routes and flights too) turns the navigation debug overlay on for this run, for a capture
	-- of what the units make of the place. (The harness force-closes the game, so Settings.ini is not written with it.)
	if os and os.getenv and tonumber(os.getenv("CCCP_NAV_DEBUG") or "") then
		pcall(function() SettingsMan.NavDebugOverlay = tonumber(os.getenv("CCCP_NAV_DEBUG")); end);
		-- (And a picture of it, from the game's own screen, at 15 s and 30 s: the window is hidden in the background runs.)
		self.navShotTimer = Timer();
		self.navShots = { 15000, 30000 };
	end
	-- (CCCP_BUNKER_VIEW is the same camera and zoom with the courses run.)
	local look = os and os.getenv and (os.getenv("CCCP_BUNKER_LOOK") or os.getenv("CCCP_BUNKER_VIEW")) or nil;
	if look then
		local x, y, zoom = look:match("(-?%d+),(-?%d+),([%d%.]+)");
		if x then
			self.lookAt = Vector(tonumber(x), tonumber(y));
			self.lookZoom = tonumber(zoom);
			self.lookOnly = os.getenv("CCCP_BUNKER_LOOK") ~= nil;
		end
	end
	self.runners = {};
	-- The courses on the real maps: points inside rooms, dropped to the floor under them when the units are placed.
	self.courseTable = {
		["Hemslock Hold"] = {
			-- (From the route comparison: pairs of standing spots the original pathfinder routes between, with a storey or more of height.)
			{ from = Vector(2268, 472), to = Vector(1308, 280), name = "east hall to the far west upper floor" },
			{ from = Vector(2748, 424), to = Vector(2484, 304), name = "east wing up a storey" },
			{ from = Vector(2412, 124), to = Vector(2604, 472), name = "roof down into the east wing" },
			{ from = Vector(1476, 172), to = Vector(1260, 376), name = "west roof down two floors" },
			{ from = Vector(2220, 808), to = Vector(2676, 616), name = "lower hall up to the east" },
			{ from = Vector(2532, 616), to = Vector(2316, 808), name = "east down to the lower hall" },
		},
		["Bywater Barracks"] = {
			{ from = Vector(1695, 1040), to = Vector(1920, 150), name = "bottom corridor to the top room" },
			{ from = Vector(1620, 700), to = Vector(2170, 580), name = "mid left room to the right column" },
			{ from = Vector(1920, 150), to = Vector(1695, 1040), name = "top room to the bottom corridor" },
			{ from = Vector(1950, 590), to = Vector(1620, 700), name = "middle to the mid left room" }, -- ("The middle" was the shaft at 1800 with a swinging floor hatch, Doors B, whose leaves gibbed units started on them; now the room east of it.)
			{ from = Vector(2130, 1070), to = Vector(1950, 590), name = "bottom right to the middle" }, -- (From 2070 it was put inside a pillar, and died there. The goal is the room east of the hatch shaft, as above.)
		},
	};
end

function AIBunkerScript:Place(preset, x, y)
	SandboxDo("Structure", Vector(x, y), 0, 0, 0, preset);
end

-- A standing spot for a unit near a point inside a bunker: down out of any slab the point is in, then down to the floor, and up off it
-- by a fifth of a body. (Dropping a point straight to "ground" from inside a slab left the unit inside the slab.)
function AIBunkerScript:Settle(point, height)
	local x, y = math.floor(point.X), math.floor(point.Y);
	local limit = 0;
	while SceneMan:GetTerrMatter(x, y) ~= rte.airID and limit < 200 do
		y = y + 1;
		limit = limit + 1;
	end
	limit = 0;
	while SceneMan:GetTerrMatter(x, y) == rte.airID and limit < 400 do
		y = y + 1;
		limit = limit + 1;
	end
	return Vector(x, y - math.floor(height * 0.2));
end

-- What the grid and the route look like where a unit has stopped: the unit's state, the points left on its route, and the grid's view of
-- the nodes two either way of it. Written when a unit has been still for four seconds and when it gives up, so a stall can be read from
-- the log of an ordinary run, without a second run with CCCP_BUNKER_DUMP aimed at the spot.
function AIBunkerScript:DumpStall(runner, why)
	local a = runner.actor;
	local x, y = math.floor(a.Pos.X), math.floor(a.Pos.Y);
	local prone = a.ClassName == "AHuman" and tostring(ToAHuman(a).ProneState) or "-";
	local stuck = a:NumberValueExists("AI_StuckForTime") and math.floor(a:GetNumberValue("AI_StuckForTime")) or 0;
	ConsoleMan:PrintString("AIBUNKER stall " .. runner.name .. " (" .. why .. ") at " .. x .. "," .. y .. " vel " .. math.floor(a.Vel.X * 10) / 10 .. "," .. math.floor(a.Vel.Y * 10) / 10 .. " aim " .. math.floor(a:GetAimAngle(false) * 100) / 100 .. " facing " .. (a.HFlipped and "left" or "right") .. " prone " .. prone .. " fuel " .. (a.Jetpack and math.floor(a.Jetpack.JetTimeLeft) or -1) .. " path " .. a.MovePathSize .. " first step kind " .. tostring(a.MovePathStepKind) .. " stuck " .. stuck .. " ms");
	local points = "";
	local count = 0;
	for p in a.MovePath do
		count = count + 1;
		if count <= 12 then
			points = points .. " " .. math.floor(p.X) .. "," .. math.floor(p.Y);
		end
	end
	ConsoleMan:PrintString("AIBUNKER stall route left (" .. count .. "):" .. points);
	for gy = y - 48, y + 48, 24 do
		for gx = x - 48, x + 48, 24 do
			ConsoleMan:PrintString("AIBUNKER grid " .. SceneMan.Scene:DescribePathNodeAt(Vector(gx, gy)));
		end
	end
end

-- CCCP_ROUTE_COMPARE=1: no courses; instead the routes between many pairs of standing spots in the map's bunker are asked for, as a
-- Soldier Light would ask, and each is graded, one ROUTECMP line a pair. Run with this build and with the original AI's
-- (../cccp-ai-baseline, CCCP_ROUTE_BUILD=base, whose pathfinder takes a jump height and dig strength instead of the actor's sizes),
-- the two logs show the routes our grid refuses that the original grid takes (Tools/RenderTest/RouteCompare.py).
-- Grades: ok (reaches the goal), cut (ends short of it: our grid cuts a route at what it can't get through), none (no route), wall
-- (a leg of it goes through ground a soldier can't dig: the original grid's way of saying impossible).
function AIBunkerScript:RouteCompare()
	local boxes = {
		["Bywater Barracks"] = { 1236, 96, 2304, 1164 },
		["Hemslock Hold"] = { 192, 84, 2880, 996 },
	};
	local box = boxes[self.sceneName] or { 0, 0, SceneMan.SceneWidth - 1, SceneMan.SceneHeight - 1 };
	local base = os.getenv("CCCP_ROUTE_BUILD") == "base";
	local actor = CreateAHuman("Soldier Light", "Coalition.rte");
	-- As the team the scene's doors belong to, which they open for in both builds: the comparison is of the route finding, not of how
	-- each build treats a door of another team (the original routes through those as if they were air).
	local team = Activity.TEAM_1;
	for actor in MovableMan.Actors do
		if actor.ClassName == "ADoor" then
			team = actor.Team;
			break;
		end
	end
	-- Standing spots: a floor (solid with air on it) with room over it for a body (44 px up, at the spot and 8 px either side), one per
	-- 48 px cell, the spot 20 px over the floor (where a soldier's Pos is).
	local points, cells = {}, {};
	for x = box[1] + 12, box[3], 24 do
		for y = box[2] + 1, box[4] do
			if SceneMan:GetTerrMatter(x, y) ~= rte.airID and SceneMan:GetTerrMatter(x, y - 1) == rte.airID then
				local room = true;
				for _, dx in ipairs({ -8, 0, 8 }) do
					for up = 1, 44, 3 do
						if SceneMan:GetTerrMatter(x + dx, y - up) ~= rte.airID then
							room = false;
							break;
						end
					end
					if not room then break; end
				end
				local cell = math.floor(x / 48) .. ":" .. math.floor(y / 48);
				if room and not cells[cell] then
					cells[cell] = true;
					table.insert(points, Vector(x, y - 20));
				end
			end
		end
	end
	ConsoleMan:PrintString("ROUTECMP points " .. #points .. " build " .. (base and "base" or "ours") .. " scene " .. tostring(self.sceneName));
	local function grade(A, B)
		local n;
		if base then
			n = SceneMan.Scene:CalculatePath(A, B, actor.JumpHeight, 35, team);
		else
			n = SceneMan.Scene:CalculatePathForActor(actor, A, B, team);
		end
		if not n or n < 0 then
			return "none", 0;
		end
		local Last, Prev = nil, nil;
		local wall = false;
		for node in SceneMan.Scene:GetScenePath() do
			local Here = Vector(node.X, node.Y);
			-- (Only on the original's routes, which aren't cut, and only for a run of more than 10 px of ground too strong to dig along
			-- one leg: a wall or a floor, not a corner clipped by a diagonal or a merged run of nodes.)
			if base and Prev and not wall then
				local Leg = SceneMan:ShortestDistance(Prev, Here, false);
				local steps = math.max(1, math.floor(Leg.Magnitude / 2));
				local run = 0;
				for k = 0, steps do
					local P = Prev + Leg * (k / steps);
					local id = SceneMan:GetTerrMatter(P.X, P.Y);
					if id ~= rte.airID and id ~= rte.doorID and SceneMan:GetMaterialFromID(id).StructuralIntegrity > 35 then
						run = run + 2;
						if run > 10 then
							wall = true;
							break;
						end
					else
						run = 0;
					end
				end
			end
			Prev = Here;
			Last = Here;
		end
		if wall then
			return "wall", n;
		end
		if not Last or SceneMan:ShortestDistance(Last, B, false).Magnitude > 40 then
			return "cut", n, Last;
		end
		return "ok", n;
	end
	-- Pairs: each spot with eight others spread through the list (the same pairs in both builds, the spots being the same terrain).
	local count = #points;
	local tally = { ok = 0, cut = 0, none = 0, wall = 0 };
	for i = 1, count do
		for k = 1, 8 do
			local j = ((i - 1 + math.floor(k * count / 9)) % count) + 1;
			if j ~= i then
				local result, n, End = grade(points[i], points[j]);
				-- (CCCP_ROUTE_SHOW="x,y,x,y": that pair's route printed in full.)
				local show = os.getenv("CCCP_ROUTE_SHOW");
				if show and show == (math.floor(points[i].X) .. "," .. math.floor(points[i].Y) .. "," .. math.floor(points[j].X) .. "," .. math.floor(points[j].Y)) then
					local nodes = "";
					for node in SceneMan.Scene:GetScenePath() do nodes = nodes .. " " .. math.floor(node.X) .. "," .. math.floor(node.Y); end
					ConsoleMan:PrintString("ROUTESHOW " .. result .. ":" .. nodes);
				end
				tally[result] = tally[result] + 1;
				ConsoleMan:PrintString("ROUTECMP " .. i .. " " .. j .. " " .. math.floor(points[i].X) .. "," .. math.floor(points[i].Y) .. " " .. math.floor(points[j].X) .. "," .. math.floor(points[j].Y) .. " " .. result .. " " .. n .. (End and (" end " .. math.floor(End.X) .. "," .. math.floor(End.Y)) or ""));
			end
		end
	end
	ConsoleMan:PrintString("ROUTECMP tally ok " .. tally.ok .. " cut " .. tally.cut .. " none " .. tally.none .. " wall " .. tally.wall);
end

function AIBunkerScript:UpdateScript()
	if self.navShotTimer and #self.navShots > 0 and self.navShotTimer:IsPastSimMS(self.navShots[1]) then
		table.remove(self.navShots, 1);
		FrameMan:SaveScreenToPNG("NavDebug");
	end
	local t = self.timer.ElapsedSimTimeMS;
	if self.record and self.started then
		if not self.recordTimer then
			self.recordTimer = Timer();
			self.recordFrame = 0;
		end
		if self.recordTimer:IsPastSimMS(200) then
			self.recordTimer:Reset();
			for _, runner in ipairs(self.runners) do
				local a = runner.actor;
				if not runner.done and MovableMan:ValidMO(a) and a:NumberValueExists("AITrace") then
					SandboxDo("Look around", a.Pos, 0, 0, 1, "");
					-- (Close in, so the limbs can be seen: CCCP_RECORD_ZOOM, 2 by default.)
					pcall(function() FrameMan.CameraZoom = tonumber(os.getenv("CCCP_RECORD_ZOOM") or "2") or 2; end);
					self.recordFrame = self.recordFrame + 1;
					ConsoleMan:PrintString(string.format("REPLAY frame %d t %.1f at %d,%d vel %.1f,%.1f", self.recordFrame, t / 1000, math.floor(a.Pos.X), math.floor(a.Pos.Y), a.Vel.X, a.Vel.Y));
					FrameMan:SaveScreenToPNG(string.format("Replay_%05d", self.recordFrame));
					break;
				end
			end
		end
	end
	if not self.built and t > 2500 then
		self.built = true;
		SandboxDo("Look around", self.lookAt, 0, 0, 1, "");
		if self.lookZoom then
			FrameMan.CameraZoom = self.lookZoom;
		end
		self.sceneName = SceneMan.Scene.PresetName;
		self.skyBunker = self.sceneName == "Ketanot Hills";
		-- CCCP_BUNKER_TOWER=1: instead of the sky bunker, a four-storey tower with two shafts, ladders up both, and courses that climb and
		-- drop one to three storeys (the user: every vertical way in a real bunker has a ladder, and a lot of vertical paths need testing).
		self.tower = self.skyBunker and os and os.getenv and os.getenv("CCCP_BUNKER_TOWER") == "1";
	end
	if self.built and not self.skyBunker then
		-- A real map: its bunker is the course. (Nothing to build.)
	elseif self.built and self.tower and not self.builtModules then
		self.builtModules = true;
		-- The tower, on the sky bunker's grid (columns' corners at x 1464 + 96 k, storeys' corners at y 96, 192, 288, 384; floors at 168,
		-- 264, 360, 456). Shaft one (k1) runs from the bottom storey A up through a hub in B to the mouth in C; shaft two (k3) from its
		-- foot in B up a plain Shaft A (walled both sides) through C to the mouth in D. Each storey's corridor joins its shafts.
		-- A (bottom)
		self:Place("End B", 1512, 432); self:Place("T-Junction B", 1608, 432); self:Place("Tunnel A", 1704, 432); self:Place("Tunnel A", 1800, 432); self:Place("End D", 1896, 432);
		-- B
		self:Place("End B", 1512, 336); self:Place("Hub A", 1608, 336); self:Place("Tunnel A", 1704, 336); self:Place("T-Junction B", 1800, 336); self:Place("End D", 1896, 336);
		-- C (the shaft-two column is a plain shaft here: no way off it at C)
		self:Place("End B", 1512, 240); self:Place("T-Junction D", 1608, 240); self:Place("Tunnel A", 1704, 240); self:Place("Shaft A", 1800, 240);
		-- D (top)
		self:Place("End B", 1704, 144); self:Place("T-Junction D", 1800, 144); self:Place("End D", 1896, 144);
		-- Courses: points 20 px over the floors (A 436, B 340, C 244, D 148).
		self.towerCourses = {
			{ from = Vector(1512, 436), to = Vector(1512, 244), name = "A up shaft one to C" },
			{ from = Vector(1512, 436), to = Vector(1896, 148), name = "A up both shafts to D" },
			{ from = Vector(1896, 148), to = Vector(1512, 436), name = "D down both shafts to A" },
			{ from = Vector(1896, 340), to = Vector(1704, 148), name = "B up shaft two to D" },
			{ from = Vector(1512, 244), to = Vector(1896, 340), name = "C down shaft one to B" },
			{ from = Vector(1896, 436), to = Vector(1704, 244), name = "A right up shaft one to C middle" },
			{ from = Vector(1704, 148), to = Vector(1704, 436), name = "D down both shafts to A middle" },
			{ from = Vector(1512, 340), to = Vector(1512, 244), name = "B up one storey to C" },
		};
		self.ladders = true;
	elseif self.built and self.skyBunker and not self.builtModules then
		self.builtModules = true;
		-- Modules are 96 px squares (Steep Stairs D is 96 x 192), placed by their centres, which the sandbox snaps to the 24 px grid: a
		-- centre of corner + 48 snaps to itself. Module columns k = 0..7 have their corners at x 1464 + 96 k; the storeys' corners are at
		-- y 192, 288 and 384, their floors at 264, 360 and 456, their corridors 48 px tall.
		-- Each module's ports: End B opens right, End D left; T-Junction B opens left, right and up (a full floor: the foot of a shaft or
		-- hatch); T-Junction D opens left, right and down (a hole in its floor: the mouth); Hub A opens all four ways; L-Junction D opens
		-- left and down; Shaft A top and bottom; Steep Stairs D takes the lower corridor in on its left and lets out at the top on its right.
		-- Bottom storey: a closed corridor the whole width, with the feet of the shaft (k1), the hatch (k3) and the hub's column (k5).
		self:Place("End B", 1512, 432);
		self:Place("T-Junction B", 1608, 432);
		self:Place("Tunnel A", 1704, 432);
		self:Place("T-Junction B", 1800, 432);
		self:Place("Tunnel A", 1896, 432);
		self:Place("T-Junction B", 1992, 432);
		self:Place("Tunnel A", 2088, 432);
		self:Place("End D", 2184, 432);
		-- Middle storey: the shaft passes through at k1 (walled off from the corridor); the corridor runs from a dead end at k2 over the
		-- hatch's mouth (k3) to the hub (k5) and into the stairs (k6).
		-- (The shaft's middle piece is "Doors B": a shaft piece with two swinging leaves across it, no one's team, so they open for
		-- whoever comes. It is here to measure the AI's door manners: a leaf in motion gibs what stands in its sweep, and a unit should
		-- wait short of a closed door for it to open, on the ground or hovering in the shaft, and never jet up into the leaves. Before
		-- this it was "Shaft A", a plain shaft.)
		-- (CCCP_BUNKER_LADDERS=1: a plain Shaft A here instead, with a base-game ladder up its left wall, put in once the modules stand; the
		-- user found that every route past a ladder came back impossible.)
		self.ladders = os and os.getenv and os.getenv("CCCP_BUNKER_LADDERS") == "1";
		self:Place(self.ladders and "Shaft A" or "Doors B", 1608, 336);
		self:Place("End B", 1704, 336);
		self:Place("T-Junction D", 1800, 336);
		self:Place("Tunnel A", 1896, 336);
		self:Place("Hub A", 1992, 336);
		-- Top storey: the room at the top of the shaft (k0-k2, reached only by the shaft), a gallery over the hub's column (k4-k5, with the
		-- corner at k5 opening down onto the hub), the stairs (k6) and the landing room they let out into (k7).
		self:Place("End B", 1512, 240);
		self:Place("T-Junction D", 1608, 240);
		self:Place("End D", 1704, 240);
		self:Place("End B", 1896, 240);
		self:Place("L-Junction D", 1992, 240);
		self:Place("Steep Stairs D", 2088, 288);
		self:Place("End D", 2184, 240);
		-- The doors just placed become the units' as soon as they exist: a team's own doors are erased from its grid only as their areas
		-- are re-sampled, a few nodes a frame behind the modules' own boxes, and set at 5 s the Doors B leaves still cost the first route
		-- 600000. (The modules go in on the next sim update and their doors join MovableMan.Actors a frame after that, so this is done on
		-- the ticks after, until a door is found.)
		self.doorsPending = true;
	end
	if self.ladders and self.builtModules and not self.laddersPlaced and t > 3800 then
		local middles = self.tower and { 1608, 1800 } or { 1608, 1800, 1992 };
		local topY = self.tower and 96 or 240;
		-- A ladder up every vertical way in the bunker: the shaft (k1), the hatch (k3) and the hub's column (k5), against the left wall wherever
		-- there is one, found by looking left from the column's middle at each 24 px step; "Left Ladder" pieces (12 x 24, placed by their
		-- top-left corner). The pieces are kept, so each course can say whether its route goes past one.
		self.laddersPlaced = true;
		self.ladderPieces = {};
		for _, middle in ipairs(middles) do
			for y = topY, 432, 24 do
				local x = middle;
				while x > middle - 60 and SceneMan:GetTerrMatter(x, y + 12) == rte.airID do
					x = x - 1;
				end
				if x > middle - 60 and SceneMan:GetTerrMatter(x, y + 12) ~= rte.airID and SceneMan:GetTerrMatter(x, y + 2) ~= rte.airID and SceneMan:GetTerrMatter(x, y + 22) ~= rte.airID then
					local ladder = CreateTerrainObject("Left Ladder", "Base.rte");
					if ladder then
						ladder.Pos = Vector(x + 1, y);
						SceneMan:AddSceneObject(ladder);
						table.insert(self.ladderPieces, Vector(x + 7, y + 12));
					end
				end
			end
		end
		local where = "";
		for _, piece in ipairs(self.ladderPieces) do where = where .. " " .. math.floor(piece.X) .. "," .. math.floor(piece.Y); end
		ConsoleMan:PrintString("AIBUNKER ladders placed: " .. #self.ladderPieces .. " (centres:" .. where .. ")");
	end
	if self.doorsPending then
		local found = false;
		for actor in MovableMan.Actors do
			if actor.ClassName == "ADoor" then
				actor.Team = 0;
				found = true;
			end
		end
		for actor in MovableMan.AddedActors do
			if actor.ClassName == "ADoor" then
				actor.Team = 0;
				found = true;
			end
		end
		if found then
			self.doorsPending = false;
		end
	end
	if not self.started and t > 5000 and not self.lookOnly then
		self.started = true;
		-- The scene's own garrison goes, so nothing shoots the units under test; its doors become theirs, as a player's own bunker's are.
		for actor in MovableMan.Actors do
			-- (Not in the route comparison, which asks as the doors' own team: see RouteCompare.)
			if actor.ClassName ~= "ADoor" then
				actor.ToDelete = true;
			elseif not (os and os.getenv and os.getenv("CCCP_ROUTE_COMPARE") == "1") then
				actor.Team = 0;
			end
		end
		-- CCCP_BUNKER_ONLY runs just that course. CCCP_BUNKER_DUMP as "x,y" prints what the grid makes of the nodes around a point.
		local only = os and os.getenv and tonumber(os.getenv("CCCP_BUNKER_ONLY") or "") or nil;
		local dump = os and os.getenv and os.getenv("CCCP_BUNKER_DUMP") or nil;
		if dump then
			-- (Several points, separated by ";".)
			for dx, dy in dump:gmatch("(-?%d+),(-?%d+)") do
				ConsoleMan:PrintString("AIBUNKER dump at " .. dx .. "," .. dy);
				for y = tonumber(dy) - 48, tonumber(dy) + 48, 24 do
					for x = tonumber(dx) - 48, tonumber(dx) + 48, 24 do
						ConsoleMan:PrintString("AIBUNKER grid " .. SceneMan.Scene:DescribePathNodeAt(Vector(x, y)));
					end
				end
			end
		end
		-- Each point is 20 px over a solid stretch of floor with 48 px of head room, never over a hole (the T-junctions' floors are only the
		-- 24 px strips either side of their holes; the rooms' floors are whole). What the pather makes of each, from the offline grid model:
		-- the shaft is one 192 px jet with a landing onto the mouth's floor strip; the hatch up is a 96 px jet; the hatch down a walk into
		-- the hole and a fall; the hub is crossed by a hop over its hole; the stairs are climbed as two jet hops and come down as falls.
		if os and os.getenv and os.getenv("CCCP_ROUTE_COMPARE") == "1" then
			-- (Five seconds on, once the doors just handed to the units' team are out of that team's grid: a team's own doors are
			-- erased from its grid only as their areas are re-sampled, and compared in the same frame they read as solid.)
			self.compareAt = t + 5000;
			self.routeCompared = true;
		end
		local courses = {
			{ from = Vector(1700, 436), to = Vector(1520, 244), name = "bottom corridor to the top room" },
			{ from = Vector(1700, 436), to = Vector(1700, 340), name = "up the hatch" },
			{ from = Vector(1700, 340), to = Vector(1700, 436), name = "down the hatch" },
			{ from = Vector(1900, 340), to = Vector(2060, 340), name = "across the hub" },
			{ from = Vector(2060, 340), to = Vector(2160, 244), name = "up the stairs" },
			{ from = Vector(2160, 244), to = Vector(2060, 340), name = "down the stairs" },
			{ from = Vector(1900, 436), to = Vector(1900, 244), name = "bottom to the top gallery" },
		};
		if self.tower then
			courses = self.towerCourses;
		end
		if self.routeCompared then
			courses = {};
			self.courseTable = nil;
			self.skyBunker = true; -- (No "no courses" line: the comparison was the run.)
		end
		-- Courses made in play with the sandbox's Gym tab (Userdata/Gyms/<scene>.txt) come first; the sandbox runs and times them itself.
		local gymFile = io.open("Userdata/Gyms/" .. tostring(self.sceneName) .. ".txt", "r");
		if gymFile then
			gymFile:close();
			if SandboxDo("Run gym", Vector(), 0, 0, 0, "") then
				ConsoleMan:PrintString("AIBUNKER running the gym courses of " .. tostring(self.sceneName));
				self.gymRun = true;
				courses = {};
			end
		end
		if self.gymRun then
			-- (Nothing of our own.)
		elseif self.courseTable and self.courseTable[self.sceneName] then
			courses = self.courseTable[self.sceneName];
		elseif not self.skyBunker then
			courses = {};
			ConsoleMan:PrintString("AIBUNKER no courses for " .. tostring(self.sceneName));
		end
		for i, course in ipairs(courses) do
			if only and i ~= only then
				course = nil;
			end
			-- CCCP_BUNKER_UNIT=crab sends the Dreadnought instead of the soldier, and CCCP_BUNKER_NOJET=1 empties every jetpack: experiments
			-- on what the legs alone can do (the steep stairs, say), asked for in Results/REQUESTS.md.
			local crab = os and os.getenv and os.getenv("CCCP_BUNKER_UNIT") == "crab";
			local actor = course and (crab and CreateACrab("Dreadnought", "Dummy.rte") or CreateAHuman("Soldier Light", "Coalition.rte")) or nil;
			-- CCCP_BUNKER_NOJET=1: soldiers with no jetpack, so every way up is a ladder or a mantle (the ladders' climb, see AHuman::UpdateLadder).
			if actor and os and os.getenv and os.getenv("CCCP_BUNKER_NOJET") == "1" and IsAHuman(actor) then
				local human = ToAHuman(actor);
				if human.Jetpack then
					local removed = pcall(function() human:RemoveAttachable(human.Jetpack, false, false); end);
					if not removed then pcall(function() human.Jetpack.JetTimeTotal = 1; end); end
					ConsoleMan:PrintString("AIBUNKER no jetpack: " .. (removed and "removed" or "tank cut to 1 ms") .. " for " .. course.name);
				end
			end
			if actor then
			if not crab then
				actor:AddInventoryItem(CreateHDFirearm("Assault Rifle", "Coalition.rte"));
			end
			if os and os.getenv and os.getenv("CCCP_BUNKER_NOJET") == "1" and actor.Jetpack then
				actor.Jetpack.JetTimeTotal = 0;
				actor.Jetpack.JetTimeLeft = 0;
				ConsoleMan:PrintString("AIBUNKER jetpack emptied for " .. course.name);
			end
			if not self.skyBunker then
				-- Onto the floor under the point, standing (the point is somewhere in the room's air).
				course.from = self:Settle(course.from, actor.Height);
				course.to = self:Settle(course.to, actor.Height);
			end
			actor.Pos = course.from;
			actor.Team = 0;
			actor.AIMode = Actor.AIMODE_SENTRY;
			if i == self.traceCourse then
				actor:SetNumberValue("AITrace", 1);
			end
			MovableMan:AddActor(actor);
			-- The unit's own route (its sizes, not the header's defaults), each point with the kind of the step that reaches it.
			local found = SceneMan.Scene:CalculatePathForActor(actor, SceneMan:MovePointToGround(actor.Pos, actor.Height * 0.2, 3), course.to, Activity.TEAM_1);
			local kinds = {};
			for kind in SceneMan.Scene:GetScenePathStepKinds() do table.insert(kinds, kind); end
			local nodes = "";
			local count = 0;
			for node in SceneMan.Scene:GetScenePath() do
				count = count + 1;
				nodes = nodes .. " " .. math.floor(node.X) .. "," .. math.floor(node.Y) .. (count > 1 and kinds[count - 1] and ("(" .. kinds[count - 1] .. ")") or "");
			end
			ConsoleMan:PrintString("AIBUNKER path for " .. course.name .. ": " .. tostring(found) .. " nodes:" .. nodes);
			if self.ladderPieces then
				local passes = 0;
				local Last = nil;
				for node in SceneMan.Scene:GetScenePath() do
					if Last then
						for _, piece in ipairs(self.ladderPieces) do
							-- (Sampled along the segment every 6 px.)
							local Seg = SceneMan:ShortestDistance(Last, node, false);
							local steps = math.max(1, math.floor(Seg.Magnitude / 6));
							for k = 0, steps do
								if SceneMan:ShortestDistance(Last + Seg * (k / steps), piece, false):MagnitudeIsLessThan(30) then
									passes = passes + 1;
									break;
								end
							end
						end
					end
					Last = Vector(node.X, node.Y);
				end
				ConsoleMan:PrintString("AIBUNKER ladder check " .. course.name .. ": the route passes " .. passes .. " ladder piece(s)");
			end
			table.insert(self.runners, { actor = actor, goal = course.to, name = course.name, start = t, lastPos = Vector(actor.Pos.X, actor.Pos.Y), still = 0, sent = false, done = false });
			end
		end
	end
	if self.compareAt and t > self.compareAt then
		self.compareAt = nil;
		self:RouteCompare();
	end
	if self.started then
		for i, runner in ipairs(self.runners) do
			local a = runner.actor;
			if not runner.done then
				if not MovableMan:ValidMO(a) then
					runner.done = true;
					local last = runner.last;
					ConsoleMan:PrintString("AIBUNKER " .. runner.name .. ": died" .. (last and (" after " .. math.floor((last.t - runner.start) / 100) / 10 .. " s, last seen at " .. math.floor(last.x) .. "," .. math.floor(last.y) .. " vel " .. math.floor(last.vx * 10) / 10 .. "," .. math.floor(last.vy * 10) / 10 .. " health " .. math.floor(last.hp)) or ""));
				else
					-- What the unit was doing when it was last seen alive, and every knock that cost it health (the speed it had says whether
					-- it was a fall, a crush or a shot).
					if runner.last and a.Health < runner.last.hp - 5 then
						ConsoleMan:PrintString("AIBUNKER hurt " .. runner.name .. " health " .. math.floor(runner.last.hp) .. " -> " .. math.floor(a.Health) .. " at " .. math.floor(a.Pos.X) .. "," .. math.floor(a.Pos.Y) .. " vel before " .. math.floor(runner.last.vx * 10) / 10 .. "," .. math.floor(runner.last.vy * 10) / 10 .. " now " .. math.floor(a.Vel.X * 10) / 10 .. "," .. math.floor(a.Vel.Y * 10) / 10);
					end
					runner.last = { t = t, x = a.Pos.X, y = a.Pos.Y, vx = a.Vel.X, vy = a.Vel.Y, hp = a.Health };
				end
				if not MovableMan:ValidMO(a) then
					-- (Written up above.)
				elseif not runner.sent and t - runner.start > 1500 then
					runner.sent = true;
					a:ClearAIWaypoints();
					if os and os.getenv and os.getenv("CCCP_BUNKER_WALK") == "1" then
						-- The legs alone: no AI, the walk key held towards the goal every frame (and up, for climbing arms).
						runner.walk = true;
						a:GetController().InputMode = Controller.CIM_DISABLED;
					else
						a:AddAISceneWaypoint(runner.goal);
						a.AIMode = Actor.AIMODE_GOTO;
					end
				elseif runner.sent then
					if runner.walk then
						local ctrl = a:GetController();
						local right = SceneMan:ShortestDistance(a.Pos, runner.goal, false).X > 0;
						ctrl:SetState(right and Controller.MOVE_RIGHT or Controller.MOVE_LEFT, true);
						ctrl:SetState(Controller.MOVE_UP, true);
					end
					if not runner.tick then
						runner.tick = Timer();
					end
					-- What the grid makes of the route's first nodes, once the unit has one (the traced course only).
					if i == self.traceCourse and not runner.dumped and a.MovePathSize > 0 then
						runner.dumped = true;
						local count = 0;
						for node in a.MovePath do
							count = count + 1;
							if count <= 12 then
								ConsoleMan:PrintString("AIBUNKER grid " .. SceneMan.Scene:DescribePathNodeAt(node));
							end
						end
					end
					if runner.tick:IsPastSimMS(1000) then
						runner.tick:Reset();
						local moved = SceneMan:ShortestDistance(runner.lastPos, a.Pos, false).Magnitude;
						runner.still = moved < 4 and runner.still + 1 or 0;
						runner.lastPos = Vector(a.Pos.X, a.Pos.Y);
						if runner.still == 4 then
							self:DumpStall(runner, "still 4 s");
						end
						local left = SceneMan:ShortestDistance(a.Pos, runner.goal, false).Magnitude;
						if i == self.traceCourse or math.floor((t - runner.start) / 1000) % 3 == 0 then
							ConsoleMan:PrintString("AIBUNKER trace " .. runner.name .. " " .. math.floor((t - runner.start) / 1000) .. "s pos " .. math.floor(a.Pos.X) .. "," .. math.floor(a.Pos.Y) .. " vel " .. math.floor(a.Vel.X * 10) / 10 .. "," .. math.floor(a.Vel.Y * 10) / 10 .. " fuel " .. (a.Jetpack and math.floor(a.Jetpack.JetTimeLeft) or 0) .. " path " .. a.MovePathSize .. " left " .. math.floor(left) .. " aim " .. math.floor(a:GetAimAngle(false) * 100) / 100 .. " facing " .. (a.HFlipped and "left" or "right"));
						end
						if left < 40 then
							runner.done = true;
							ConsoleMan:PrintString("AIBUNKER " .. runner.name .. ": arrived in " .. math.floor((t - runner.start) / 100) / 10 .. " s, stood still " .. runner.still .. " s");
						elseif t - runner.start > 60000 then
							runner.done = true;
							ConsoleMan:PrintString("AIBUNKER " .. runner.name .. ": GAVE UP after 60 s, " .. math.floor(left) .. " px short, stood still " .. runner.still .. " s, at " .. math.floor(a.Pos.X) .. "," .. math.floor(a.Pos.Y));
							self:DumpStall(runner, "gave up");
						end
					end
				end
			end
		end
		if self.gymRun then
			-- The sandbox reports GYM lines itself.
		elseif not self.finished then
			local allDone = true;
			for _, runner in ipairs(self.runners) do
				if not runner.done then
					allDone = false;
				end
			end
			if allDone then
				self.finished = true;
				ConsoleMan:PrintString("AIBUNKER done");
			end
		end
	end
end
