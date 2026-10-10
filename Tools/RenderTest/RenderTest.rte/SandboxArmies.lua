function SandboxArmiesScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.log = io.open("sandboxarmies.log", "w");
end

function SandboxArmiesScript:Log(text)
	if self.log then
		self.log:write(string.format("%6.1f %s\n", self.timer.ElapsedSimTimeMS / 1000, text));
		self.log:flush();
	end
end

function SandboxArmiesScript:UpdateScript()
	-- A squad dropped by dropship, a battle between two factions (the Battle Director), and the dropped squad commanded across the map.
	local t = self.timer.ElapsedSimTimeMS;
	local origin = CameraMan:GetOffset(0);
	local w = FrameMan.PlayerScreenWidth;
	local h = FrameMan.PlayerScreenHeight;
	if self.stage == 0 and t > 3000 then
		self.stage = 1;
		self.dropX = origin.X + w * 0.5;
		self:Log("drop " .. tostring(SandboxDo("Drop squad", Vector(self.dropX, 0), 0, 0, 3, "Soldier Light")));
		-- Green and Blue attack, a dropship of five every 15 seconds each, until their 2500 oz are spent.
		SandboxBattleTeam(1, "Browncoats", 0, 2500);
		SandboxBattleTeam(2, "Techion", 0, 2500);
		SandboxBattleDrops(1, 0, 1, 15, 5, false);
		SandboxBattleDrops(2, 0, 1, 15, 5, false);
		SandboxBattleStart();
		self:Log("battle started");
	elseif self.stage == 1 and t > 14000 then
		self.stage = 2;
		local red = nil;
		for actor in MovableMan.Actors do
			if actor.Team == 0 and actor.ClassName == "AHuman" then
				red = actor;
				break;
			end
		end
		if red then
			SandboxDo("Select", red.Pos, 0, 0, 60, "");
			self.commandTarget = red.Pos + Vector(-w * 0.4, 0);
			SandboxDo("Command", self.commandTarget, 0, 0, 1, "");
			self.watched = red;
			self.watchedStart = Vector(red.Pos.X, red.Pos.Y);
			self:Log(string.format("commanded red units from %.0f towards %.0f", red.Pos.X, self.commandTarget.X));
		else
			self:Log("no red unit to command");
		end
	end
	if self.stage >= 1 and self.logTimer:IsPastSimMS(3000) then
		self.logTimer:Reset();
		local craft = 0;
		for actor in MovableMan.Actors do
			if actor.ClassName == "ACDropShip" or actor.ClassName == "ACRocket" then
				craft = craft + 1;
			end
		end
		local moved = "";
		if self.watched and MovableMan:IsActor(self.watched) then
			moved = string.format("  watched red moved %.0f", self.watched.Pos.X - self.watchedStart.X);
		end
		self:Log(string.format("red %d  green %d  blue %d  craft %d%s", SandboxCountUnits(0), SandboxCountUnits(1), SandboxCountUnits(2), craft, moved));
	end
end

function SandboxArmiesScript:EndScript()
	if self.log then
		self.log:close();
		self.log = nil;
	end
end
