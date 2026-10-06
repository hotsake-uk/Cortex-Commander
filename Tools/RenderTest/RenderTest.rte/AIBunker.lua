function AIBunkerScript:StartScript()
	self.timer = Timer();
	-- An indoor gym: a three-storey bunker built of the game's own modules in the sky over Ketanot Hills (the sandbox map has no bunker of
	-- its own), with hubs for junctions, a shaft, a stair, a gap in the middle storey and dead ends. Units are put down inside it and sent
	-- to other places inside it, one course each at the same time, and how they do is written up as AIBUNKER lines: the path the finder
	-- gives, a line a second per unit, and arrived / gave up. CCCP_BUNKER_TRACE is the course whose unit writes AITRACE lines.
	-- On a real map (the scenario's scene) nothing is built: the map's own bunker is the course, with courses of its own below.
	-- CCCP_BUNKER_LOOK as "x,y,zoom" puts the camera somewhere at a zoom, for a capture of a layout, and runs no courses.
	self.lookAt = Vector(1792, 330);
	self.traceCourse = 1;
	if os and os.getenv and tonumber(os.getenv("CCCP_BUNKER_TRACE") or "") then
		self.traceCourse = tonumber(os.getenv("CCCP_BUNKER_TRACE"));
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
		["Bywater Barracks"] = {
			{ from = Vector(1695, 1040), to = Vector(1920, 150), name = "bottom corridor to the top room" },
			{ from = Vector(1620, 700), to = Vector(2170, 580), name = "mid left room to the right column" },
			{ from = Vector(1920, 150), to = Vector(1695, 1040), name = "top room to the bottom corridor" },
			{ from = Vector(1820, 580), to = Vector(1620, 700), name = "middle to the mid left room" },
			{ from = Vector(2070, 1040), to = Vector(1820, 580), name = "bottom right to the middle" },
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

function AIBunkerScript:UpdateScript()
	local t = self.timer.ElapsedRealTimeMS;
	if not self.built and t > 2500 then
		self.built = true;
		SandboxDo("Look around", self.lookAt, 0, 0, 1, "");
		if self.lookZoom then
			FrameMan.CameraZoom = self.lookZoom;
		end
		self.sceneName = SceneMan.Scene.PresetName;
		self.skyBunker = self.sceneName == "Ketanot Hills";
	end
	if self.built and not self.skyBunker then
		-- A real map: its bunker is the course. (Nothing to build.)
	elseif self.built and self.skyBunker and not self.builtModules then
		self.builtModules = true;
		-- Modules are 96 px squares (stairs 96 x 144), placed by their centres. Three storeys: 228, 324 and 420.
		local top, mid, low = 228, 324, 420;
		-- Top storey: a corridor the whole width, with hubs at the shaft and the right end.
		for _, x in ipairs({ 1504, 1600, 1792, 1984, 2080 }) do self:Place("Tunnel A", x, top); end
		self:Place("Hub A", 1696, top);
		self:Place("Hub A", 1888, top);
		-- Middle storey: a hub at each end and in the middle, a gap (nothing at 1600: open air down to the low storey's roof), a shaft up
		-- and down at 1792, and a stair down to the low storey at 1984.
		self:Place("Hub A", 1504, mid);
		self:Place("Hub A", 1696, mid);
		self:Place("Shaft A", 1792, mid);
		self:Place("Hub A", 1888, mid);
		self:Place("Stairs A", 1984, mid + 24);
		self:Place("Tunnel A", 2080, mid);
		-- Low storey: a corridor the whole width.
		for _, x in ipairs({ 1504, 1600, 1984, 2080 }) do self:Place("Tunnel A", x, low); end
		self:Place("Hub A", 1696, low);
		self:Place("Hub A", 1792, low);
		self:Place("Hub A", 1888, low);
	end
	if not self.started and t > 5000 and not self.lookOnly then
		self.started = true;
		-- The scene's own garrison goes, so nothing shoots the units under test; its doors become theirs, as a player's own bunker's are.
		for actor in MovableMan.Actors do
			if actor.ClassName ~= "ADoor" then
				actor.ToDelete = true;
			else
				actor.Team = 0;
			end
		end
		-- CCCP_BUNKER_ONLY runs just that course. CCCP_BUNKER_DUMP as "x,y" prints what the grid makes of the nodes around a point.
		local only = os and os.getenv and tonumber(os.getenv("CCCP_BUNKER_ONLY") or "") or nil;
		local dump = os and os.getenv and os.getenv("CCCP_BUNKER_DUMP") or nil;
		if dump then
			local dx, dy = dump:match("(-?%d+),(-?%d+)");
			if dx then
				for y = tonumber(dy) - 48, tonumber(dy) + 48, 24 do
					for x = tonumber(dx) - 48, tonumber(dx) + 48, 24 do
						ConsoleMan:PrintString("AIBUNKER grid " .. SceneMan.Scene:DescribePathNodeAt(Vector(x, y)));
					end
				end
			end
		end
		local courses = {
			{ from = Vector(1520, 444), to = Vector(2070, 252), name = "low left to top right" },
			{ from = Vector(1510, 348), to = Vector(1900, 348), name = "across the gap" },
			{ from = Vector(1520, 252), to = Vector(2070, 444), name = "top left to low right" },
			{ from = Vector(1792, 444), to = Vector(1792, 252), name = "up the shaft" },
			{ from = Vector(2080, 444), to = Vector(1510, 348), name = "low right to mid left" },
		};
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
			local actor = course and CreateAHuman("Soldier Light", "Coalition.rte") or nil;
			if actor then
			actor:AddInventoryItem(CreateHDFirearm("Assault Rifle", "Coalition.rte"));
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
			local found = SceneMan.Scene:CalculatePath(SceneMan:MovePointToGround(actor.Pos, actor.Height * 0.2, 3), course.to, actor.JumpHeight, 35, Activity.TEAM_1);
			local nodes = "";
			for node in SceneMan.Scene:GetScenePath() do nodes = nodes .. " " .. math.floor(node.X) .. "," .. math.floor(node.Y); end
			ConsoleMan:PrintString("AIBUNKER path for " .. course.name .. ": " .. tostring(found) .. " nodes:" .. nodes);
			table.insert(self.runners, { actor = actor, goal = course.to, name = course.name, start = t, lastPos = Vector(actor.Pos.X, actor.Pos.Y), still = 0, sent = false, done = false });
			end
		end
	end
	if self.started then
		for i, runner in ipairs(self.runners) do
			local a = runner.actor;
			if not runner.done then
				if not MovableMan:ValidMO(a) then
					runner.done = true;
					ConsoleMan:PrintString("AIBUNKER " .. runner.name .. ": died");
				elseif not runner.sent and t - runner.start > 1500 then
					runner.sent = true;
					a:ClearAIWaypoints();
					a:AddAISceneWaypoint(runner.goal);
					a.AIMode = Actor.AIMODE_GOTO;
				elseif runner.sent then
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
					if runner.tick:IsPastRealMS(1000) then
						runner.tick:Reset();
						local moved = SceneMan:ShortestDistance(runner.lastPos, a.Pos, false).Magnitude;
						runner.still = moved < 4 and runner.still + 1 or 0;
						runner.lastPos = Vector(a.Pos.X, a.Pos.Y);
						local left = SceneMan:ShortestDistance(a.Pos, runner.goal, false).Magnitude;
						if i == self.traceCourse or math.floor((t - runner.start) / 1000) % 3 == 0 then
							ConsoleMan:PrintString("AIBUNKER trace " .. runner.name .. " " .. math.floor((t - runner.start) / 1000) .. "s pos " .. math.floor(a.Pos.X) .. "," .. math.floor(a.Pos.Y) .. " vel " .. math.floor(a.Vel.X * 10) / 10 .. "," .. math.floor(a.Vel.Y * 10) / 10 .. " fuel " .. (a.Jetpack and math.floor(a.Jetpack.JetTimeLeft) or 0) .. " path " .. a.MovePathSize .. " left " .. math.floor(left));
						end
						if left < 40 then
							runner.done = true;
							ConsoleMan:PrintString("AIBUNKER " .. runner.name .. ": arrived in " .. math.floor((t - runner.start) / 100) / 10 .. " s, stood still " .. runner.still .. " s");
						elseif t - runner.start > 60000 then
							runner.done = true;
							ConsoleMan:PrintString("AIBUNKER " .. runner.name .. ": GAVE UP after 60 s, " .. math.floor(left) .. " px short, stood still " .. runner.still .. " s, at " .. math.floor(a.Pos.X) .. "," .. math.floor(a.Pos.Y));
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
