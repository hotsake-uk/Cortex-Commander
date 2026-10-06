function AICombatScript:StartScript()
	self.timer = Timer();
	self.courses = {};
	self.lookAt = nil; -- Where the camera is put (nil for the first course).
end

-- The combat gym: small fights set up on concrete beams in the sky, one per rule of the fighting AI, written up as AICOMBAT lines (and
-- the AI's own AITRACE lines, which say when a unit closes in, holds its range, takes cover, flanks or falls back). Run with
-- CCCP_CONSOLE_LOG and CCCP_AI_LOG set; Tools\RenderTest\AICombat.ps1 does.
--   firefight:  an attacker sent at a defender along a bare beam. The attacker should close to about half its weapon's reach and stop
--               there, shifting a step now and then; the defender stands its ground.
--   cover:      the same with a pillar behind each unit. Reloads should be done behind the pillar, with a step out again after.
--   dug in:     the defender is up on a ledge the attacker's shots can't reach it on. The attacker should go round and up to a spot
--               it can be shot from.
--   move:       a unit on a plain move order walks the length of a beam past an enemy up on a ledge. It should fire back on the way
--               and arrive, not stop for the fight.
--   retreat:    a badly hurt unit with its brain down the beam. It should fall back to the brain, wait, and then take up its post again.
function AICombatScript:Spawn(preset, pos, team, facingLeft)
	local actor = CreateAHuman(preset, "Coalition.rte");
	actor.Pos = pos;
	actor.Team = team;
	actor.HFlipped = facingLeft == true;
	actor.AIMode = Actor.AIMODE_SENTRY;
	actor:AddInventoryItem(CreateHDFirearm("Assault Rifle", "Coalition.rte"));
	actor:SetNumberValue("AITrace", 1);
	MovableMan:AddActor(actor);
	return actor;
end

function AICombatScript:UpdateScript()
	local t = self.timer.ElapsedRealTimeMS;
	if not self.built and t > 1500 then
		self.built = true;
		-- Beams of 160 x 10, centred on the point. Each course on its own, well apart, and well inside the scene (a waypoint past its right
		-- edge wrapped to x 0, and a beam at the top edge sent every jet off the top). Teams 1 and 2, both run by the AI: the sandbox
		-- counts team 0 as the player's, and a sentry posted by a player stands whatever its health.
		local left = SceneMan.SceneWidth * 0.5 - 420;
		local west = left - 900;
		local east = left + 1000;
		-- firefight: an 800 px beam.
		for x = -320, 320, 160 do SandboxDo("Concrete beam", Vector(left + x, 200), 0, 0, 1, ""); end
		-- cover: a 640 px beam, and a ledge 150 px over its right half for the enemy (beams are 160 wide, so the ledge's lip is at
		-- west + 400). From where the unit starts it sees the enemy's
		-- head over the ledge's edge; a few steps right, under the ledge, it is out of the enemy's sight. (A wall to get behind is no use
		-- in a side view: the far side of it can't be got to.)
		for x = 0, 640, 160 do SandboxDo("Concrete beam", Vector(west + x, 480), 0, 0, 1, ""); end
		for x = 480, 800, 160 do SandboxDo("Concrete beam", Vector(west + x, 330), 0, 0, 1, ""); end
		-- dug in: a long low beam for the attacker, and a ledge 170 px up at its far end for the defender, which stands back from the lip:
		-- from the beam its head shows over the edge but its body doesn't.
		for x = 0, 1120, 160 do SandboxDo("Concrete beam", Vector(west + x, 760), 0, 0, 1, ""); end
		for x = 480, 800, 160 do SandboxDo("Concrete beam", Vector(west + x, 590), 0, 0, 1, ""); end
		-- move: an 800 px beam, and a ledge over its middle for the enemy.
		for x = 0, 800, 160 do SandboxDo("Concrete beam", Vector(east + x, 300), 0, 0, 1, ""); end
		SandboxDo("Concrete beam", Vector(east + 400, 160), 0, 0, 1, "");
		-- retreat: a 640 px beam.
		for x = 0, 640, 160 do SandboxDo("Concrete beam", Vector(east + x, 560), 0, 0, 1, ""); end
	end
	if not self.started and t > 3500 then
		self.started = true;
		local left = SceneMan.SceneWidth * 0.5 - 420;
		local west = left - 900;
		local east = left + 1000;
		local up = Vector(0, -30);
		-- firefight: the attacker is sent at the defender's end (an attack towards a place: the sandbox then only ever sends it after
		-- enemies near that place).
		local a = self:Spawn("Soldier Light", Vector(left - 380, 200) + up, 1);
		local d = self:Spawn("Soldier Light", Vector(left + 380, 200) + up, 2, true);
		a:SetNumberValue("SandboxAttack", 1);
		a:SetNumberValue("SandboxAttackX", left + 360);
		a:SetNumberValue("SandboxAttackY", 180);
		a:ClearAIWaypoints();
		a:AddAISceneWaypoint(Vector(left + 360, 180));
		a.AIMode = Actor.AIMODE_GOTO;
		d:SetNumberValue("SandboxDefendX", d.Pos.X);
		d:SetNumberValue("SandboxDefendY", d.Pos.Y);
		table.insert(self.courses, { name = "firefight", units = { a, d }, start = t });
		-- cover: two sentries, which go for each other when out of reach.
		-- (Tough ones, so the fight lasts long enough for a reload or two.)
		a = self:Spawn("Soldier Light", Vector(west + 150, 480) + up, 1);
		d = self:Spawn("Soldier Light", Vector(west + 440, 330) + up, 2, true);
		a.MaxHealth = 400; a.Health = 400;
		d.MaxHealth = 400; d.Health = 400;
		d:SetNumberValue("SandboxDefendX", d.Pos.X);
		d:SetNumberValue("SandboxDefendY", d.Pos.Y);
		table.insert(self.courses, { name = "cover", units = { a, d }, start = t });
		-- dug in: the attacker is a sentry (it goes for what it can't hit from where it is); the defender stands 40 px back from the lip,
		-- its head showing over it from the attacker's spot but not its body.
		a = self:Spawn("Soldier Light", Vector(west + 150, 760) + up, 1);
		d = self:Spawn("Soldier Light", Vector(west + 440, 590) + up, 2, true);
		d:SetNumberValue("SandboxDefendX", d.Pos.X);
		d:SetNumberValue("SandboxDefendY", d.Pos.Y);
		table.insert(self.courses, { name = "dug in", units = { a, d }, start = t });
		-- move
		a = self:Spawn("Soldier Light", Vector(east - 60, 300) + up, 1);
		d = self:Spawn("Soldier Light", Vector(east + 400, 160) + up, 2, true);
		a:ClearAIWaypoints();
		a:AddAISceneWaypoint(Vector(east + 820, 280));
		a.AIMode = Actor.AIMODE_GOTO;
		d:SetNumberValue("SandboxDefendX", d.Pos.X);
		d:SetNumberValue("SandboxDefendY", d.Pos.Y);
		table.insert(self.courses, { name = "move", units = { a, d }, goal = Vector(east + 820, 280), start = t });
		-- retreat
		a = self:Spawn("Soldier Light", Vector(east + 20, 560) + up, 1);
		local brain = CreateAHuman("Brain Robot", "Base.rte");
		brain.Pos = Vector(east + 620, 560) + up;
		brain.Team = 1;
		brain.AIMode = Actor.AIMODE_SENTRY;
		MovableMan:AddActor(brain);
		a.Health = 20;
		table.insert(self.courses, { name = "retreat", units = { a, brain }, start = t });
		SandboxDo("Look around", self.lookAt or Vector(left, 220), 0, 0, 1, "");
		ConsoleMan:PrintString("AICOMBAT started " .. #self.courses .. " courses");
	end
	if self.started and not self.finished then
		if not self.reportTimer then
			self.reportTimer = Timer();
		end
		if self.reportTimer:IsPastRealMS(2000) then
			self.reportTimer:Reset();
			for _, course in ipairs(self.courses) do
				local line = "AICOMBAT " .. course.name .. " " .. math.floor((t - course.start) / 1000) .. "s:";
				for i, unit in ipairs(course.units) do
					if MovableMan:ValidMO(unit) then
						local tags = "";
						if unit:NumberValueExists("AIRetreat") then tags = tags .. " retreating"; end
						if unit:NumberValueExists("AIFlank") then tags = tags .. " flanking"; end
						local other = course.units[3 - i];
						if other and MovableMan:ValidMO(other) and unit.EyePos then
							local Trace = SceneMan:ShortestDistance(unit.EyePos, other.EyePos or other.Pos, false);
							local id = SceneMan:CastMORay(unit.EyePos, Trace, unit.ID, unit.IgnoresWhichTeam, rte.grassID, false, 5);
							tags = tags .. (id ~= rte.NoMOID and " sees" or " blind") .. (unit.HFlipped and " L" or " R") .. math.floor(math.deg(unit:GetAimAngle(false)));
						end
						line = line .. " [" .. unit.Team .. " at " .. math.floor(unit.Pos.X) .. "," .. math.floor(unit.Pos.Y) .. " hp " .. math.floor(unit.Health) .. " mode " .. unit.AIMode .. tags .. "]";
					else
						line = line .. " [" .. i .. " gone]";
					end
				end
				ConsoleMan:PrintString(line);
			end
		end
		if t - self.courses[1].start > 70000 then
			self.finished = true;
			for _, course in ipairs(self.courses) do
				local alive = {};
				for i, unit in ipairs(course.units) do
					if MovableMan:ValidMO(unit) and unit.Health > 0 then
						table.insert(alive, "team " .. unit.Team .. " hp " .. math.floor(unit.Health) .. " at " .. math.floor(unit.Pos.X) .. "," .. math.floor(unit.Pos.Y));
					end
				end
				local extra = "";
				if course.goal and MovableMan:ValidMO(course.units[1]) then
					extra = " mover " .. math.floor(SceneMan:ShortestDistance(course.units[1].Pos, course.goal, false).Magnitude) .. " px from goal";
				end
				ConsoleMan:PrintString("AICOMBAT " .. course.name .. " result: " .. table.concat(alive, "; ") .. extra);
			end
			ConsoleMan:PrintString("AICOMBAT done");
		end
	end
end
