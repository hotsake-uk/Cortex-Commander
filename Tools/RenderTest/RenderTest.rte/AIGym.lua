function AIGymScript:StartScript()
	self.timer = Timer();
	self.runners = {};
	self.report = {};
end

-- The AI gym: units are given places to get to over the hill and down into the cave, and how they do is written to the console as AIGYM lines.
-- Run with CCCP_CONSOLE_LOG set and read the lines: one per unit when it arrives or gives up, and a summary at the end.
function AIGymScript:GroundAt(x, fromY)
	local y = fromY or 0;
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
	if not self.started and t > 3000 then
		self.started = true;
		local middle = SceneMan.SceneWidth * 0.5;
		-- Courses: from the left slope to the right slope (over the top), from the right slope to the left (back over), and from the top down to the cave floor.
		local courses = {
			{ from = self:GroundAt(middle - 260), to = self:GroundAt(middle + 260), name = "over the hill" },
			{ from = self:GroundAt(middle + 320), to = self:GroundAt(middle - 320), name = "back over the hill" },
			{ from = self:GroundAt(middle - 40), to = self:CaveFloorAt(middle), name = "down into the cave" },
		};
		for i, course in ipairs(courses) do
			local actor = CreateAHuman("Soldier Light", "Coalition.rte");
			actor.Pos = course.from + Vector(0, -20);
			actor.Team = 0;
			actor.AIMode = Actor.AIMODE_SENTRY;
			MovableMan:AddActor(actor);
			table.insert(self.runners, { actor = actor, goal = course.to, name = course.name, start = t, lastPos = Vector(actor.Pos.X, actor.Pos.Y), still = 0, sent = false, done = false });
		end
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
				local distance = SceneMan:ShortestDistance(actor.Pos, runner.goal, false).Magnitude;
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
