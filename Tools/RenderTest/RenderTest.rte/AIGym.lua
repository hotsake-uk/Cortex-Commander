function AIGymScript:StartScript()
	self.timer = Timer();
	self.runners = {};
	self.report = {};
	self.traceCourse = 16; -- Which course's unit writes a trace line every second.
	self.traceAll = true; -- Every course's unit writes one every two seconds.
	self.lookAt = Vector(SceneMan.SceneWidth * 0.5 + 940, 260); -- Where the camera is put, for a look at a course (nil for the default).
end

-- The AI gym: units are given set courses to get round, and how they do is written to the console as AIGYM lines.
-- Run with CCCP_CONSOLE_LOG set and read the lines: the pathfinder's answer for each course, a trace of one unit, one line per unit when it
-- arrives or gives up, and "AIGYM done" at the end.
-- The scene's own ground straight below a point: the first thing from the top that isn't air or the concrete of the courses.
function AIGymScript:GroundAt(x, fromY)
	local y = fromY or 0;
	while y < SceneMan.SceneHeight - 1 do
		local matter = SceneMan:GetTerrMatter(x, y);
		if matter ~= rte.airID and matter ~= 177 then
			break;
		end
		y = y + 2;
	end
	return Vector(x, y);
end

-- The floor of the first open space under the ground at x.
function AIGymScript:CaveFloorAt(x)
	local y = self:GroundAt(x).Y + 4;
	while y < SceneMan.SceneHeight - 1 and SceneMan:GetTerrMatter(x, y) ~= rte.airID do
		y = y + 2;
	end
	return self:GroundAt(x, y);
end

-- What the grid and the route look like where a unit has stopped: the unit's state, the points left on its route, and the grid's view of
-- the nodes two either way of it. Written when a unit has been still for four seconds and when it gives up, so a stall can be read from
-- the log of an ordinary run, without a second run with a grid dump aimed at the spot.
function AIGymScript:DumpStall(runner, why)
	local a = runner.actor;
	local x, y = math.floor(a.Pos.X), math.floor(a.Pos.Y);
	local prone = a.ClassName == "AHuman" and tostring(ToAHuman(a).ProneState) or "-";
	local stuck = a:NumberValueExists("AI_StuckForTime") and math.floor(a:GetNumberValue("AI_StuckForTime")) or 0;
	ConsoleMan:PrintString("AIGYM stall " .. runner.name .. " (" .. why .. ") at " .. x .. "," .. y .. " vel " .. math.floor(a.Vel.X * 10) / 10 .. "," .. math.floor(a.Vel.Y * 10) / 10 .. " aim " .. math.floor(a:GetAimAngle(false) * 100) / 100 .. " facing " .. (a.HFlipped and "left" or "right") .. " prone " .. prone .. " fuel " .. (a.Jetpack and math.floor(a.Jetpack.JetTimeLeft) or -1) .. " path " .. a.MovePathSize .. " first step kind " .. tostring(a.MovePathStepKind) .. " stuck " .. stuck .. " ms");
	local points = "";
	local count = 0;
	for p in a.MovePath do
		count = count + 1;
		if count <= 12 then
			points = points .. " " .. math.floor(p.X) .. "," .. math.floor(p.Y);
		end
	end
	ConsoleMan:PrintString("AIGYM stall route left (" .. count .. "):" .. points);
	for gy = y - 48, y + 48, 24 do
		for gx = x - 48, x + 48, 24 do
			ConsoleMan:PrintString("AIGYM grid " .. SceneMan.Scene:DescribePathNodeAt(Vector(gx, gy)));
		end
	end
end

function AIGymScript:UpdateScript()
	local t = self.timer.ElapsedSimTimeMS;
	if not self.built and t > 1500 then
		-- Courses built in the sky out of concrete beams (160 x 10, centred on the point), so what each tests is known exactly.
		self.built = true;
		local left = SceneMan.SceneWidth * 0.5 - 420;
		-- A flat run of 800 px. High up, out of the way of the climbs on the course below it.
		for x = 0, 640, 160 do SandboxDo("Concrete beam", Vector(left + x, 40), 0, 0, 1, ""); end
		-- Steps up: 36 px (a hop), then 90 px (a jet).
		for x = 0, 160, 160 do SandboxDo("Concrete beam", Vector(left + x, 360), 0, 0, 1, ""); end
		SandboxDo("Concrete beam", Vector(left + 320, 324), 0, 0, 1, "");
		SandboxDo("Concrete beam", Vector(left + 480, 234), 0, 0, 1, "");
		SandboxDo("Concrete beam", Vector(left + 640, 234), 0, 0, 1, "");
		-- The other two courses are off to the left, where the ground is deep: the hill in the middle of the scene reaches up to y 538, and a
		-- course near it gets used as a short cut by the hill course (or built into the hill).
		local west = left - 900;
		-- A gap of 70 px.
		SandboxDo("Concrete beam", Vector(west, 380), 0, 0, 1, "");
		SandboxDo("Concrete beam", Vector(west + 160, 380), 0, 0, 1, "");
		SandboxDo("Concrete beam", Vector(west + 390, 380), 0, 0, 1, "");
		SandboxDo("Concrete beam", Vector(west + 550, 380), 0, 0, 1, "");
		-- A low tunnel: a floor with a ceiling 44 px above it along the middle.
		for x = 0, 480, 160 do SandboxDo("Concrete beam", Vector(west + x, 660), 0, 0, 1, ""); end
		for x = 160, 320, 160 do SandboxDo("Concrete beam", Vector(west + x, 616), 0, 0, 1, ""); end
		-- A wall on the ceiling, so the only way is through.
		SandboxDo("Concrete pillar", Vector(west + 240, 541), 0, 0, 1, "");
		-- A wall with a 56 px doorway at its foot, shut by a sliding door of the unit's own team, which opens for it. Off to the right over
		-- the far valley, clear of everything else (and of the scene's seam, which a start near x = 0 was over).
		local east = left + 1200;
		local doorAt = Vector(east + 160, 300);
		SandboxDo("Concrete beam", Vector(east + 80, 300), 0, 0, 1, "");
		SandboxDo("Concrete beam", Vector(east + 240, 300), 0, 0, 1, "");
		SandboxDo("Concrete pillar", Vector(doorAt.X, 169), 0, 0, 1, "");
		-- The long slide, stood on end (as the bunker pieces have it): its bar slides 53 px, enough to clear a doorway a unit can crawl through.
		local door = CreateADoor("Door Slide Long", "Base.rte");
		door.RotAngle = math.pi * 0.5;
		door.Pos = Vector(doorAt.X, 165); -- The bar (68 px) hangs 102 px under Pos when shut, filling the doorway, and 49 when open, clear of it.
		door.Team = 0;
		MovableMan:AddActor(door);
		self.door = door;
		-- A low room whose ceiling stops short of a wall, leaving a 70 px gap up to the floor above (a bunker's upper storey): the unit starts
		-- under the ceiling's end and has to go up through the gap and back over onto the roof.
		for x = 80, 400, 160 do SandboxDo("Concrete beam", Vector(east + x, 600), 0, 0, 1, ""); end
		for x = 80, 240, 160 do SandboxDo("Concrete beam", Vector(east + x, 520), 0, 0, 1, ""); end
		SandboxDo("Concrete pillar", Vector(east + 396, 530), 0, 0, 1, "");
		-- A room on a floor, with a 30 px doorway at the foot of each side wall: a bunker's corridor, to be crawled through.
		for x = 0, 480, 160 do SandboxDo("Concrete beam", Vector(west + x, 200), 0, 0, 1, ""); end
		SandboxDo("Concrete room", Vector(west + 240, 150), 0, 0, 1, "");
	end
	if not self.started and t > 3500 then
		self.started = true;
		local middle = SceneMan.SceneWidth * 0.5;
		local left = middle - 420;
		local east = left + 1200;
		local courses = {
			{ from = Vector(left - 60, 32), to = Vector(left + 680, 32), name = "flat run" },
			{ from = Vector(left - 60, 352), to = Vector(left + 680, 226), name = "steps up" },
			{ from = Vector(left - 960, 372), to = Vector(left - 290, 372), name = "gap" },
			{ from = Vector(left - 960, 652), to = Vector(left - 360, 652), name = "low tunnel" },
			{ from = Vector(left - 960, 192), to = Vector(left - 360, 192), name = "through the room" },
			{ from = Vector(left + 1220, 292), to = Vector(left + 1500, 292), name = "through the door" },
			{ from = self:GroundAt(middle - 260), to = self:GroundAt(middle + 260), name = "over the hill" },
			{ from = self:GroundAt(middle - 40), to = self:CaveFloorAt(middle), name = "down into the cave" },
			-- The scene's own slopes: the far side of the hill drops 500 px over 450.
			{ from = self:GroundAt(middle + 40), to = self:GroundAt(middle + 480), name = "down the slope" },
			{ from = self:GroundAt(middle + 520), to = self:GroundAt(middle + 80), name = "up the slope" },
			-- The scene's own cliff on the far side of the hill (166 px, sheer): straight onto its top from its foot, and across the
			-- dip behind the hill's crest (a jet across, or a walk down and up).
			{ from = self:GroundAt(middle + 450), to = self:GroundAt(middle + 330), name = "onto the ledge" },
			{ from = self:GroundAt(middle + 320), to = self:GroundAt(middle + 120), name = "across the dip" },
			-- A digger: the goal is 140 px straight down into the valley floor on the left, and the only way is through.
			{ from = self:GroundAt(left - 300), to = self:GroundAt(left - 300) + Vector(0, 140), name = "dig down", digger = true },
			-- A crab (legs both sides, no head, no jetpack) on the easy courses.
			{ from = Vector(left - 60, 32), to = Vector(left + 680, 32), name = "crab flat run", crab = true },
			{ from = self:GroundAt(middle - 260), to = self:GroundAt(middle + 260), name = "crab over the hill", crab = true },
			-- Up through the gap between a low room's ceiling and the wall beside it, onto the roof.
			{ from = Vector(east + 290, 590), to = Vector(east + 200, 510), name = "up through the gap" },
		};
		for i, course in ipairs(courses) do
			local actor = course.crab and CreateACrab("Dreadnought", "Dummy.rte") or CreateAHuman("Soldier Light", "Coalition.rte");
			actor.Pos = course.from + Vector(0, -20);
			if course.digger then
				actor:AddInventoryItem(CreateHDFirearm("Heavy Digger", "Base.rte"));
			end
			actor.Team = 0;
			actor.AIMode = Actor.AIMODE_SENTRY;
			if i == self.traceCourse then
				actor:SetNumberValue("AITrace", 1); -- The movement AI says why it jets, as AITRACE lines.
			end
			MovableMan:AddActor(actor);
			if i == 1 or (course.crab and not self.crabDescribed) then
				self.crabDescribed = self.crabDescribed or course.crab;
				ConsoleMan:PrintString("AIGYM unit " .. actor.PresetName .. " height " .. math.floor(actor.Height) .. " radius " .. math.floor(actor.Radius) .. " jump height " .. math.floor(actor.JumpHeight * 20) .. " px aim range " .. math.floor(actor.AimRange * 100) / 100);
			end
			-- What the pathfinder makes of the course, before the unit tries it.
			local found = SceneMan.Scene:CalculatePath(SceneMan:MovePointToGround(actor.Pos, actor.Height * 0.2, 10), course.to + Vector(0, -actor.Height * 0.5), actor.JumpHeight, 35, Activity.TEAM_1);
			local nodes = "";
			local count = 0;
			for node in SceneMan.Scene:GetScenePath() do
				count = count + 1;
				if count <= 40 then
					nodes = nodes .. " " .. math.floor(node.X) .. "," .. math.floor(node.Y);
				end
			end
			ConsoleMan:PrintString("AIGYM path for " .. course.name .. ": result " .. tostring(found) .. ", " .. count .. " nodes:" .. nodes);
			local a = SceneMan.Scene:CalculatePath(SceneMan:MovePointToGround(actor.Pos, actor.Height * 0.2, 10), course.to, actor.JumpHeight, 35, Activity.TEAM_1);
			local b = SceneMan.Scene:CalculatePath(course.from, course.to + Vector(0, -actor.Height * 0.5), actor.JumpHeight, 35, Activity.TEAM_1);
			local c = SceneMan.Scene:CalculatePath(course.from, course.to + Vector(0, -24), actor.JumpHeight, 35, Activity.TEAM_1);
			ConsoleMan:PrintString("AIGYM path variants for " .. course.name .. ": ground start " .. a .. ", raised end " .. b .. ", end 24 up " .. c);
			table.insert(self.runners, { actor = actor, goal = course.to, name = course.name, digger = course.digger, start = t, lastPos = Vector(actor.Pos.X, actor.Pos.Y), still = 0, sent = false, done = false });
		end
		SandboxDo("Look around", self.lookAt or Vector(left + 300, 360), 0, 0, 1, "");
		-- Where the scene's own ground is, so a course isn't built into it by mistake.
		local profile = "";
		for x = left - 1000, left + 1000, 100 do
			local y = 0;
			while y < SceneMan.SceneHeight - 1 do
				local m = SceneMan:GetTerrMatter(x, y);
				if m ~= rte.airID and m ~= 177 then break; end
				y = y + 2;
			end
			profile = profile .. " " .. x .. ":" .. y;
		end
		ConsoleMan:PrintString("AIGYM ground profile (ignoring concrete):" .. profile);
		local cliff = "";
		for x = middle + 300, middle + 500, 12 do cliff = cliff .. " " .. x .. ":" .. self:GroundAt(x).Y; end
		ConsoleMan:PrintString("AIGYM cliff profile:" .. cliff);
		local slope = "";
		for x = middle + 60, middle + 320, 12 do slope = slope .. " " .. x .. ":" .. self:GroundAt(x).Y; end
		ConsoleMan:PrintString("AIGYM slope profile:" .. slope);
		for x = middle + 100, middle + 300, 24 do
			local g = self:GroundAt(x).Y;
			ConsoleMan:PrintString("AIGYM grid " .. SceneMan.Scene:DescribePathNodeAt(Vector(x, g - 2)));
			ConsoleMan:PrintString("AIGYM grid " .. SceneMan.Scene:DescribePathNodeAt(Vector(x, g - 26)));
		end
		ConsoleMan:PrintString("AIGYM grid room " .. SceneMan.Scene:DescribePathNodeAt(Vector(left - 960, 192)) .. " | " .. SceneMan.Scene:DescribePathNodeAt(Vector(left - 960, 216)) .. " | " .. SceneMan.Scene:DescribePathNodeAt(Vector(left - 900, 192)));
		-- Debug: what the pather makes of the upper slope on its own, from a few footings along it.
		for _, fromX in ipairs({middle + 290, middle + 260, middle + 230, middle + 200}) do
			local from = self:GroundAt(fromX) + Vector(0, -20);
			SceneMan.Scene:CalculatePath(from, self:GroundAt(middle + 80) + Vector(0, -20), 22, 35, Activity.TEAM_1);
			local nodes = "";
			for node in SceneMan.Scene:GetScenePath() do nodes = nodes .. " " .. math.floor(node.X) .. "," .. math.floor(node.Y); end
			ConsoleMan:PrintString("AIGYM slope path from " .. math.floor(from.X) .. "," .. math.floor(from.Y) .. ":" .. nodes);
		end
		if self.door then
			local column = "";
			for y = 240, 320, 5 do column = column .. " " .. y .. ":" .. SceneMan:GetTerrMatter(left + 1360, y); end
			local bar = self.door.Door;
			ConsoleMan:PrintString("AIGYM door at " .. self.door.Pos.X .. "," .. self.door.Pos.Y .. " bar " .. (bar and (bar.Pos.X .. "," .. bar.Pos.Y .. " " .. ToMOSprite(bar):GetSpriteWidth() .. "x" .. ToMOSprite(bar):GetSpriteHeight()) or "none") .. " column at " .. (left + 1360) .. ":" .. column);
		end
		local r = SceneMan.Scene:CalculatePath(self:GroundAt(middle + 460), self:GroundAt(middle + 340), 22, 35, Activity.TEAM_1);
		local n = "";
		for node in SceneMan.Scene:GetScenePath() do n = n .. " " .. math.floor(node.X) .. "," .. math.floor(node.Y); end
		ConsoleMan:PrintString("AIGYM cliff path: " .. r .. " nodes:" .. n);
		r = SceneMan.Scene:CalculatePath(self:GroundAt(middle + 340), self:GroundAt(middle + 80), 22, 35, Activity.TEAM_1);
		n = "";
		for node in SceneMan.Scene:GetScenePath() do n = n .. " " .. math.floor(node.X) .. "," .. math.floor(node.Y); end
		ConsoleMan:PrintString("AIGYM upper slope path: " .. r .. " nodes:" .. n);
	end
	if not self.started then
		return;
	end
	for i, runner in ipairs(self.runners) do
		if not runner.done then
			local actor = runner.actor;
			if not MovableMan:IsActor(actor) then
				runner.done = true;
				table.insert(self.report, "AIGYM " .. runner.name .. ": died after " .. math.floor((t - runner.start) / 1000) .. " s");
				ConsoleMan:PrintString(self.report[#self.report]);
			elseif not runner.sent then
				-- A moment to land first.
				if t - runner.start > 600 then
					runner.sent = true;
					runner.start = t;
					actor:ClearAIWaypoints();
					-- Half a body over the goal, which is a point on the ground; into the ground (digging), the goal is where the body goes.
					actor:AddAISceneWaypoint(runner.goal + Vector(0, runner.digger and 0 or -actor.Height * 0.5));
					actor.AIMode = Actor.AIMODE_GOTO;
				end
			else
				-- Arrived when alongside the goal, which is a point on the ground, and not far above or below it (crabs and humans carry
				-- their Pos at different heights over their feet).
				local offset = SceneMan:ShortestDistance(actor.Pos, runner.goal, false);
				local distance = offset.Magnitude;
				if math.abs(offset.X) < 40 and math.abs(offset.Y) < (runner.digger and 40 or actor.Height * 0.7) then
					runner.done = true;
					table.insert(self.report, "AIGYM " .. runner.name .. ": arrived in " .. math.floor((t - runner.start) / 100) / 10 .. " s, stood still " .. runner.still .. " s");
					ConsoleMan:PrintString(self.report[#self.report]);
				elseif t - runner.start > 60000 then
					runner.done = true;
					table.insert(self.report, "AIGYM " .. runner.name .. ": GAVE UP after 60 s, " .. math.floor(distance) .. " px short, stood still " .. runner.still .. " s, mode " .. actor.AIMode .. ", at " .. math.floor(actor.Pos.X) .. "," .. math.floor(actor.Pos.Y) .. " goal " .. math.floor(runner.goal.X) .. "," .. math.floor(runner.goal.Y));
					ConsoleMan:PrintString(self.report[#self.report]);
					self:DumpStall(runner, "gave up");
				elseif not runner.lastTick or t - runner.lastTick > 1000 then
					runner.lastTick = t;
					if self.traceCourse == i or (self.traceAll and math.floor((t - runner.start) / 1000) % 2 == 0) then
						local fuel = actor.Jetpack and math.floor(actor.Jetpack.JetTimeLeft) or -1;
						local extra = "";
						if runner.name == "through the door" and self.door and MovableMan:IsActor(self.door) then
							extra = " door team " .. self.door.Team .. " state " .. self.door:GetDoorState() .. " target " .. (actor.AIMode == Actor.AIMODE_GOTO and "goto" or tostring(actor.AIMode));
						end
						ConsoleMan:PrintString("AIGYM trace " .. runner.name .. " " .. math.floor((t - runner.start) / 1000) .. "s pos " .. math.floor(actor.Pos.X) .. "," .. math.floor(actor.Pos.Y) .. " vel " .. math.floor(actor.Vel.X * 10) / 10 .. "," .. math.floor(actor.Vel.Y * 10) / 10 .. " fuel " .. fuel .. " path " .. actor.MovePathSize .. " health " .. math.floor(actor.Health) .. " aim " .. math.floor(actor:GetAimAngle(false) * 100) / 100 .. " facing " .. (actor.HFlipped and "left" or "right") .. extra);
					end
					if SceneMan:ShortestDistance(actor.Pos, runner.lastPos, false).Magnitude < 4 then
						runner.still = runner.still + 1;
						runner.stillRun = (runner.stillRun or 0) + 1;
						if runner.stillRun == 4 then
							self:DumpStall(runner, "still 4 s");
						end
					else
						runner.stillRun = 0;
					end
					runner.lastPos = Vector(actor.Pos.X, actor.Pos.Y);
				end
			end
		end
	end
	if not self.summarised then
		local allDone = #self.runners > 0;
		for i, runner in ipairs(self.runners) do
			if not runner.done then
				allDone = false;
			end
		end
		if allDone then
			self.summarised = true;
			ConsoleMan:PrintString("AIGYM done");
		end
	end
end
