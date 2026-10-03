function LiquidWeaponTestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.log = io.open("liquidweapontest.log", "w");
end

function LiquidWeaponTestScript:Log(text)
	if self.log then
		self.log:write(string.format("%6.1f %s\n", self.timer.ElapsedSimTimeMS / 1000, text));
		self.log:flush();
	end
end

-- A gun pinned in mid-air, aimed down at the ground ahead and firing by itself.
function LiquidWeaponTestScript:PlaceGun(name, offset, angle)
	if self.gun and MovableMan:ValidMO(self.gun) then
		self.gun.ToDelete = true;
	end
	local gun = CreateHDFirearm(name, "Base.rte");
	gun.Pos = self.origin + offset;
	gun.RotAngle = angle;
	gun.PinStrength = 10000;
	gun.HitsMOs = false;
	gun.GetsHitByMOs = false;
	MovableMan:AddItem(gun);
	self.gun = gun;
	self:Log("firing " .. name);
end

function LiquidWeaponTestScript:UpdateScript()
	-- Napalm onto the grassy slope, then the water cannon onto the fire, then acid into the dirt.
	local player = ActivityMan:GetActivity():GetControlledActor(0);
	if not player then
		return;
	end
	local t = self.timer.ElapsedSimTimeMS;
	if self.stage == 0 and t > 12000 then
		self.stage = 1;
		self.origin = Vector(player.Pos.X, player.Pos.Y);
		self:PlaceGun("Napalm Flamer", Vector(70, -70), -0.7);
	elseif self.stage == 1 and t > 15000 then
		self.stage = 2;
		self.gun.ToDelete = true;
		self.gun = nil;
		self:Log("napalm stopped");
	elseif self.stage == 2 and t > 19000 then
		self.stage = 3;
		self:PlaceGun("Water Cannon", Vector(70, -70), -1.0);
	elseif self.stage == 3 and t > 22000 then
		self.stage = 4;
		self:PlaceGun("Acid Sprayer", Vector(-60, -50), -2.3);
	elseif self.stage == 4 and t > 26000 then
		self.stage = 5;
		self.gun.ToDelete = true;
		self.gun = nil;
		self:Log("done");
	end
	if self.gun and MovableMan:ValidMO(self.gun) then
		self.gun:Activate();
	end
	if self.stage >= 1 and self.logTimer:IsPastSimMS(500) then
		self.logTimer:Reset();
		self:Log(string.format("burning %d  flowing %d  player %.1f", SceneMan:GetBurningPixelCount(), SceneMan:GetFlowingLiquidPixelCount(), player.Health));
	end
end

function LiquidWeaponTestScript:EndScript()
	if self.log then
		self.log:close();
		self.log = nil;
	end
end
