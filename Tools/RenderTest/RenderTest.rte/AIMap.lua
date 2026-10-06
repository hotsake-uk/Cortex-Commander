function AIMapScript:StartScript()
	self.timer = Timer();
	self.runners = {};
	self.traceCourse = 1;
	self.lookAt = Vector(2050, 560); -- Where the camera is put (nil for the middle of the map).
end

-- The AI on a real map: Zekarra Mining Outpost, the attackers' landing zone on the left to the brain room deep in the bunker on the
-- right, 2200 px away through corridors and doors that belong to the other team. Reports AIMAP lines like the gym's AIGYM ones.
function AIMapScript:GroundAt(x)
	local y = 0;
	while y < SceneMan.SceneHeight - 1 and SceneMan:GetTerrMatter(x, y) == rte.airID do
		y = y + 2;
	end
	return Vector(x, y);
end

function AIMapScript:UpdateScript()
	local t = self.timer.ElapsedRealTimeMS;
	if not self.started and t > 3000 then
		self.started = true;
		-- The scene's own garrison is cleared out: this measures the way in, not the fight for it.
		local garrison = 0;
		for actor in MovableMan.Actors do
			if actor.ClassName ~= "ADoor" then
				actor.ToDelete = true;
				garrison = garrison + 1;
			end
		end
		ConsoleMan:PrintString("AIMAP removed " .. garrison .. " of the scene's actors");
		local brain = SceneMan.Scene:GetArea("Brain");
		local goal = brain and brain:GetCenterPoint() or Vector(2663, 432);
		local courses = {
			{ from = self:GroundAt(400), to = goal, name = "soldier to the brain" },
			{ from = self:GroundAt(500), to = goal, name = "digger to the brain", digger = true },
			{ from = self:GroundAt(600), to = goal, name = "crab to the brain", crab = true },
		};
		for i, course in ipairs(courses) do
			local actor = course.crab and CreateACrab("Dreadnought", "Dummy.rte") or CreateAHuman("Soldier Light", "Coalition.rte");
			actor.Pos = course.from + Vector(0, -30);
			actor.Team = 1; -- The attackers: the bunker's doors are not theirs.
			actor.AIMode = Actor.AIMODE_SENTRY;
			if course.digger then
				actor:AddInventoryItem(CreateHDFirearm("Heavy Digger", "Base.rte"));
			end
			if i == self.traceCourse then
				actor:SetNumberValue("AITrace", 1);
			end
			MovableMan:AddActor(actor);
			local found = SceneMan.Scene:CalculatePath(SceneMan:MovePointToGround(actor.Pos, actor.Height * 0.2, 3), course.to, actor.JumpHeight, course.digger and 100 or 35, Activity.TEAM_2);
			local nodes = "";
			for node in SceneMan.Scene:GetScenePath() do nodes = nodes .. " " .. math.floor(node.X) .. "," .. math.floor(node.Y); end
			ConsoleMan:PrintString("AIMAP path for " .. course.name .. ": " .. tostring(found) .. " nodes from " .. math.floor(actor.Pos.X) .. "," .. math.floor(actor.Pos.Y) .. " to " .. math.floor(course.to.X) .. "," .. math.floor(course.to.Y) .. ":" .. nodes);
			table.insert(self.runners, { actor = actor, goal = course.to, name = course.name, digger = course.digger, start = t, lastPos = Vector(actor.Pos.X, actor.Pos.Y), still = 0, sent = false, done = false });
		end
		SandboxDo("Look around", self.lookAt or Vector(1400, 500), 0, 0, 1, "");
		for y = 740, 860, 24 do
			local row = "";
			for x = 852, 972, 24 do row = row .. " | " .. SceneMan.Scene:DescribePathNodeAt(Vector(x, y)); end
			ConsoleMan:PrintString("AIMAP grid" .. row);
		end
		for _, y in ipairs({ 640, 655, 665, 675 }) do
			local row = "";
			for x = 900, 1010, 6 do row = row .. " " .. x .. ":" .. SceneMan:GetTerrMatter(x, y); end
			ConsoleMan:PrintString("AIMAP matter y " .. y .. ":" .. row);
		end
		for actor in MovableMan.Actors do
			if actor.ClassName == "ADoor" and actor.Pos.X > 800 and actor.Pos.X < 1100 then
				ConsoleMan:PrintString("AIMAP door " .. actor.PresetName .. " at " .. math.floor(actor.Pos.X) .. "," .. math.floor(actor.Pos.Y) .. " team " .. actor.Team);
			end
		end
	end
	if not self.started then
		return;
	end
	local allDone = true;
	for i, runner in ipairs(self.runners) do
		if not runner.done then
			allDone = false;
			local actor = runner.actor;
			if not MovableMan:IsActor(actor) then
				runner.done = true;
				ConsoleMan:PrintString("AIMAP " .. runner.name .. ": died after " .. math.floor((t - runner.start) / 1000) .. " s");
			elseif not runner.sent then
				if t - runner.start > 600 then
					runner.sent = true;
					runner.start = t;
					actor:ClearAIWaypoints();
					actor:AddAISceneWaypoint(runner.goal);
					actor.AIMode = Actor.AIMODE_GOTO;
				end
			else
				local offset = SceneMan:ShortestDistance(actor.Pos, runner.goal, false);
				if math.abs(offset.X) < 60 and math.abs(offset.Y) < actor.Height * 0.8 then
					runner.done = true;
					ConsoleMan:PrintString("AIMAP " .. runner.name .. ": arrived in " .. math.floor((t - runner.start) / 100) / 10 .. " s, stood still " .. runner.still .. " s");
				elseif t - runner.start > 150000 then
					runner.done = true;
					ConsoleMan:PrintString("AIMAP " .. runner.name .. ": GAVE UP after 150 s, " .. math.floor(offset.Magnitude) .. " px short, stood still " .. runner.still .. " s, mode " .. actor.AIMode .. ", at " .. math.floor(actor.Pos.X) .. "," .. math.floor(actor.Pos.Y));
				elseif not runner.lastTick or t - runner.lastTick > 2000 then
					runner.lastTick = t;
					local fuel = actor.Jetpack and math.floor(actor.Jetpack.JetTimeLeft) or -1;
					ConsoleMan:PrintString("AIMAP trace " .. runner.name .. " " .. math.floor((t - runner.start) / 1000) .. "s pos " .. math.floor(actor.Pos.X) .. "," .. math.floor(actor.Pos.Y) .. " vel " .. math.floor(actor.Vel.X * 10) / 10 .. "," .. math.floor(actor.Vel.Y * 10) / 10 .. " fuel " .. fuel .. " path " .. actor.MovePathSize .. " health " .. math.floor(actor.Health));
					if SceneMan:ShortestDistance(actor.Pos, runner.lastPos, false).Magnitude < 4 then
						runner.still = runner.still + 2;
					end
					runner.lastPos = Vector(actor.Pos.X, actor.Pos.Y);
				end
			end
		end
	end
	if allDone and not self.summarised then
		self.summarised = true;
		ConsoleMan:PrintString("AIMAP done");
	end
end
