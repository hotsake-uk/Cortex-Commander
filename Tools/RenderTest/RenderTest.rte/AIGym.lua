function AIGymScript:StartScript()
	self.timer = Timer();
	self.runners = {};
	self.report = {};
	self.traceCourse = 2; -- Which course's unit writes a trace line every second.
	self.traceAll = true; -- Every course's unit writes one every two seconds.
end

-- The AI gym: units are given set courses to get round, and how they do is written to the console as AIGYM lines.
-- Run with CCCP_CONSOLE_LOG set and read the lines: the pathfinder's answer for each course, a trace of one unit, one line per unit when it
-- arrives or gives up, and "AIGYM done" at the end.
-- The ground straight below a point, from a height below the beams of the sky courses.
function AIGymScript:GroundAt(x, fromY)
	local y = fromY or 700;
	while y < SceneMan.SceneHeight - 1 and SceneMan:GetTerrMatter(x, y) == rte.airID do
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

function AIGymScript:UpdateScript()
	local t = self.timer.ElapsedRealTimeMS;
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
	end
	if not self.started and t > 3500 then
		self.started = true;
		local middle = SceneMan.SceneWidth * 0.5;
		local left = middle - 420;
		local courses = {
			{ from = Vector(left - 60, 32), to = Vector(left + 680, 32), name = "flat run" },
			{ from = Vector(left - 60, 352), to = Vector(left + 680, 226), name = "steps up" },
			{ from = Vector(left - 960, 372), to = Vector(left - 290, 372), name = "gap" },
			{ from = Vector(left - 960, 652), to = Vector(left - 360, 652), name = "low tunnel" },
			{ from = self:GroundAt(middle - 260), to = self:GroundAt(middle + 260), name = "over the hill" },
			{ from = self:GroundAt(middle - 40), to = self:CaveFloorAt(middle), name = "down into the cave" },
		};
		for i, course in ipairs(courses) do
			local actor = CreateAHuman("Soldier Light", "Coalition.rte");
			actor.Pos = course.from + Vector(0, -20);
			actor.Team = 0;
			actor.AIMode = Actor.AIMODE_SENTRY;
			if i == self.traceCourse then
				actor:SetNumberValue("AITrace", 1); -- The movement AI says why it jets, as AITRACE lines.
			end
			MovableMan:AddActor(actor);
			if i == 1 then
				ConsoleMan:PrintString("AIGYM unit " .. actor.PresetName .. " height " .. math.floor(actor.Height) .. " jump height " .. math.floor(actor.JumpHeight * 20) .. " px");
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
			table.insert(self.runners, { actor = actor, goal = course.to, name = course.name, start = t, lastPos = Vector(actor.Pos.X, actor.Pos.Y), still = 0, sent = false, done = false });
		end
		SandboxDo("Look around", Vector(left + 300, 360), 0, 0, 1, "");
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
					actor:AddAISceneWaypoint(runner.goal + Vector(0, -actor.Height * 0.5));
					actor.AIMode = Actor.AIMODE_GOTO;
				end
			else
				-- Measured from the feet, since the goal is a point on the ground.
				local distance = SceneMan:ShortestDistance(actor.Pos + Vector(0, actor.Height * 0.45), runner.goal, false).Magnitude;
				if distance < 40 then
					runner.done = true;
					table.insert(self.report, "AIGYM " .. runner.name .. ": arrived in " .. math.floor((t - runner.start) / 100) / 10 .. " s, stood still " .. runner.still .. " s");
					ConsoleMan:PrintString(self.report[#self.report]);
				elseif t - runner.start > 60000 then
					runner.done = true;
					table.insert(self.report, "AIGYM " .. runner.name .. ": GAVE UP after 60 s, " .. math.floor(distance) .. " px short, stood still " .. runner.still .. " s, mode " .. actor.AIMode .. ", at " .. math.floor(actor.Pos.X) .. "," .. math.floor(actor.Pos.Y) .. " goal " .. math.floor(runner.goal.X) .. "," .. math.floor(runner.goal.Y));
					ConsoleMan:PrintString(self.report[#self.report]);
				elseif not runner.lastTick or t - runner.lastTick > 1000 then
					runner.lastTick = t;
					if self.traceCourse == i or (self.traceAll and math.floor((t - runner.start) / 1000) % 2 == 0) then
						local fuel = actor.Jetpack and math.floor(actor.Jetpack.JetTimeLeft) or -1;
						ConsoleMan:PrintString("AIGYM trace " .. runner.name .. " " .. math.floor((t - runner.start) / 1000) .. "s pos " .. math.floor(actor.Pos.X) .. "," .. math.floor(actor.Pos.Y) .. " vel " .. math.floor(actor.Vel.X * 10) / 10 .. "," .. math.floor(actor.Vel.Y * 10) / 10 .. " fuel " .. fuel .. " path " .. actor.MovePathSize .. " health " .. math.floor(actor.Health));
					end
					if SceneMan:ShortestDistance(actor.Pos, runner.lastPos, false).Magnitude < 4 then
						runner.still = runner.still + 1;
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
