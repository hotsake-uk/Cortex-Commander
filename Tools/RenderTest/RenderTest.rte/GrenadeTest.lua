function GrenadeTestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.log = io.open("grenadetest.log", "w");
end

function GrenadeTestScript:Log(text)
	if self.log then
		self.log:write(string.format("%6.1f %s\n", self.timer.ElapsedSimTimeMS / 1000, text));
		self.log:flush();
	end
end

local function SpawnEnemy(pos)
	local soldier = CreateAHuman("Soldier Light", "Coalition.rte");
	soldier:AddInventoryItem(CreateHDFirearm("Assault Rifle", "Coalition.rte"));
	soldier.Pos = pos;
	soldier.Team = 1;
	soldier.HFlipped = true;
	soldier.AIMode = Actor.AIMODE_SENTRY;
	MovableMan:AddActor(soldier);
	return soldier;
end

local function Throw(name, pos, vel)
	local grenade = CreateTDExplosive(name, "Base.rte");
	grenade.Pos = pos;
	grenade.Vel = vel;
	grenade:Activate();
	MovableMan:AddItem(grenade);
end

function GrenadeTestScript:UpdateScript()
	-- An enemy rifleman faces player 1's soldier with a smoke screen between them; a second enemy stands in toxic gas.
	local player = ActivityMan:GetActivity():GetControlledActor(0);
	if not player then
		return;
	end
	if self.stage == 0 and self.timer:IsPastSimMS(14000) then
		self.stage = 1;
		self:Log("start, player health " .. player.Health);
		Throw("Smoke Grenade", player.Pos + Vector(90, -30), Vector(0, 0));
		Throw("Smoke Grenade", player.Pos + Vector(130, -30), Vector(0, 0));
		self.gasVictim = SpawnEnemy(player.Pos + Vector(-150, -20));
		self.gasVictim.HFlipped = false;
		self.gasVictim.AIMode = Actor.AIMODE_NONE;
		Throw("Toxic Gas Grenade", player.Pos + Vector(-150, -40), Vector(0, 0));
	elseif self.stage == 1 and self.timer:IsPastSimMS(16000) then
		self.stage = 2;
		self.rifleman = SpawnEnemy(player.Pos + Vector(220, -20));
	end
	if self.stage >= 1 and self.logTimer:IsPastSimMS(1000) then
		self.logTimer:Reset();
		local gas = (self.gasVictim and MovableMan:ValidMO(self.gasVictim)) and string.format("%.1f", self.gasVictim.Health) or "gone";
		local shots = "-";
		if self.rifleman and MovableMan:ValidMO(self.rifleman) and self.rifleman.EquippedItem and IsHDFirearm(self.rifleman.EquippedItem) then
			local gun = ToHDFirearm(self.rifleman.EquippedItem);
			self.lastRounds = self.lastRounds or gun.RoundInMagCount;
			if gun.RoundInMagCount < self.lastRounds then
				self.fired = (self.fired or 0) + self.lastRounds - gun.RoundInMagCount;
			end
			self.lastRounds = gun.RoundInMagCount;
			shots = tostring(self.fired or 0);
		end
		local cloud = "none";
		for particle in MovableMan.Particles do
			if particle.PresetName == "Toxic Gas Cloud" then
				cloud = string.format("%.0f,%.0f", particle.Pos.X - player.Pos.X, particle.Pos.Y - player.Pos.Y);
			end
		end
		self:Log(string.format("player %.1f  gas victim %s  rifleman rounds fired %s  gas cloud %s", player.Health, gas, shots, cloud));
	end
end

function GrenadeTestScript:EndScript()
	if self.log then
		self.log:close();
		self.log = nil;
	end
end
