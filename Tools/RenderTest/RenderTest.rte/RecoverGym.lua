-- The recovery gym: how a soldier comes back from being knocked over. Concrete pads hang in the open sky over the sandbox map; a
-- soldier stands on each, is knocked in one way (flung along the pad, dropped onto it, set upside down, spun, thrown up and back, slammed
-- down), and is ordered to a point 200 px along the pad. Each case is written up as a RECOVER line: how long it was unstable, how long
-- until it stood upright, whether and when it reached the point, and how many times it went unstable again on the way.
-- Uses only calls the original AI's build has too, so the same file measures both builds (Tools/RenderTest/Bench.ps1 -Suites Recover).

local Cases = {
	{ name = "flung along the pad", vel = Vector(18, 0) },
	{ name = "dropped from 260 px", drop = 260 },
	{ name = "set upside down", rot = math.pi },
	{ name = "spun", angVel = 14 },
	{ name = "thrown up and back", vel = Vector(-10, -14) },
	{ name = "slammed down", vel = Vector(0, 30), lift = 40 },
};

function RecoverGymScript:StartScript()
	self.timer = Timer();
	self.runners = {};
end

function RecoverGymScript:Pad(x, y, blocks)
	for i = 0, blocks - 1 do
		local block = CreateTerrainObject("Concrete Block", "Base.rte");
		if block then
			block.Pos = Vector(x + i * 24, y);
			SceneMan:AddSceneObject(block);
		end
	end
end

function RecoverGymScript:UpdateScript()
	local t = self.timer.ElapsedSimTimeMS;
	if not self.built and t > 2000 then
		self.built = true;
		-- A pad of 16 blocks (384 px) per case, in a row at y 380, 120 px apart, well above the hills (which top out at y 559).
		for i, case in ipairs(Cases) do
			local x = 100 + (i - 1) * 520;
			case.padX = x;
			case.padY = 380;
			self:Pad(x, 380, 16);
		end
		SandboxDo("Look around", Vector(900, 380), 0, 0, 1, "");
	end
	if self.built and not self.spawned and t > 3500 then
		self.spawned = true;
		for i, case in ipairs(Cases) do
			local actor = CreateAHuman("Soldier Light", "Coalition.rte");
			actor:AddInventoryItem(CreateHDFirearm("Assault Rifle", "Coalition.rte"));
			local h = actor.Height;
			local standY = case.padY - h * 0.5;
			actor.Pos = Vector(case.padX + 60, standY - (case.drop or case.lift or 0));
			actor.Team = 0;
			actor.AIMode = Actor.AIMODE_SENTRY;
			MovableMan:AddActor(actor);
			table.insert(self.runners, { actor = actor, case = case, goal = Vector(case.padX + 300, standY), start = t, knocked = false, sent = false, done = false, unstableMS = 0, uprightAt = nil, wasUnstable = false, relapses = 0 });
		end
	end
	if not self.spawned then
		return;
	end
	local allDone = true;
	for _, r in ipairs(self.runners) do
		if not r.done then
			allDone = false;
			local a = r.actor;
			if not MovableMan:ValidMO(a) then
				r.done = true;
				ConsoleMan:PrintString("RECOVER " .. r.case.name .. ": died");
			elseif not r.knocked then
				if t - r.start > 1000 then
					r.knocked = true;
					r.knockedAt = t;
					local c = r.case;
					if c.vel then
						a.Vel = Vector(c.vel.X, c.vel.Y);
					end
					if c.rot then
						a.RotAngle = c.rot;
					end
					if c.angVel then
						a.AngularVel = c.angVel;
					end
					a:ClearAIWaypoints();
					a:AddAISceneWaypoint(r.goal);
					a.AIMode = Actor.AIMODE_GOTO;
					r.sent = true;
				end
			else
				local dt = TimerMan.DeltaTimeMS;
				local unstable = a.Status ~= Actor.STABLE;
				if unstable then
					r.unstableMS = r.unstableMS + dt;
					if not r.wasUnstable and r.uprightAt then
						r.relapses = r.relapses + 1;
					end
				end
				r.wasUnstable = unstable;
				local rot = a.RotAngle;
				while rot > math.pi do rot = rot - 2 * math.pi; end
				while rot < -math.pi do rot = rot + 2 * math.pi; end
				if not r.uprightAt and not unstable and math.abs(rot) < 0.35 and t - r.knockedAt > 200 then
					r.uprightAt = t;
				end
				local left = SceneMan:ShortestDistance(a.Pos, r.goal, false).Magnitude;
				local stats = "unstable " .. math.floor(r.unstableMS) .. " ms, upright after " .. (r.uprightAt and math.floor(r.uprightAt - r.knockedAt) .. " ms" or "never") .. ", relapses " .. r.relapses;
				if left < 40 then
					r.done = true;
					ConsoleMan:PrintString("RECOVER " .. r.case.name .. ": arrived in " .. math.floor((t - r.knockedAt) / 100) / 10 .. " s, " .. stats);
				elseif t - r.knockedAt > 20000 then
					r.done = true;
					ConsoleMan:PrintString("RECOVER " .. r.case.name .. ": GAVE UP after 20 s, " .. math.floor(left) .. " px short, " .. stats);
				end
			end
		end
	end
	if allDone and not self.finished then
		self.finished = true;
		ConsoleMan:PrintString("RECOVER done");
	end
end
