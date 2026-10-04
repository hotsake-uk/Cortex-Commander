function FireUnitsTestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.log = io.open("fireunitstest.log", "w");
end

function FireUnitsTestScript:Log(text)
	if self.log then
		self.log:write(string.format("%6.1f %s\n", self.timer.ElapsedSimTimeMS / 1000, text));
		self.log:flush();
	end
end

local function Ground(x, y)
	local pos = Vector(x, y);
	for i = 1, 600 do
		if SceneMan:GetTerrMatter(pos.X, pos.Y + 1) ~= 0 then
			break;
		end
		pos.Y = pos.Y + 1;
	end
	return pos;
end

function FireUnitsTestScript:UpdateScript()
	-- Napalm on a squad, water on a burning soldier, and fire beside fuel barrels.
	local t = self.timer.ElapsedSimTimeMS;
	if self.stage == 0 and t > 3000 then
		self.stage = 1;
		local origin = CameraMan:GetOffset(0);
		local w = FrameMan.PlayerScreenWidth;
		self.squadPos = Ground(origin.X + w * 0.3, origin.Y);
		self.barrelPos = Ground(origin.X + w * 0.62, origin.Y);
		SandboxDo("Units", self.squadPos + Vector(0, -20), 0, 0, 4, "Soldier Light");
		for i = 0, 2 do
			SandboxDo("Item", self.barrelPos + Vector(i * 14 - 14, -10), 0, 0, 1, "Fuel Barrel");
		end
		self:Log("setup");
	elseif self.stage == 1 and t > 6000 then
		self.stage = 2;
		SandboxDo("Napalm burst", self.squadPos + Vector(0, -4), 0, 0, 1, "");
		self:Log("napalm");
	elseif self.stage == 2 and t > 9000 then
		self.stage = 3;
		for actor in MovableMan.Actors do
			if actor:NumberValueExists("OnFire") then
				SandboxDo("Water", actor.Pos + Vector(0, -12), 0, 0, 8, "");
				self:Log("water on a burning " .. actor.PresetName);
				break;
			end
		end
	elseif self.stage == 3 and t > 12000 then
		self.stage = 4;
		SandboxDo("Fire", self.barrelPos + Vector(0, -2), 0, 0, 10, "");
		self:Log("fire by the barrels");
	end
	if self.stage >= 1 and self.logTimer:IsPastSimMS(1000) then
		self.logTimer:Reset();
		local barrels, steam = 0, 0;
		for item in MovableMan.Items do
			if item.PresetName == "Fuel Barrel" then
				barrels = barrels + 1;
			end
		end
		for particle in MovableMan.Particles do
			if particle.PresetName == "Steam Puff" then
				steam = steam + 1;
			end
		end
		self:Log(string.format("burning units %d  burning ground %d  barrels %d  steam %d  red %d", SceneMan:GetBurningUnitCount(), SceneMan:GetBurningPixelCount(), barrels, steam, SandboxCountUnits(0)));
	end
end

function FireUnitsTestScript:EndScript()
	if self.log then
		self.log:close();
		self.log = nil;
	end
end
