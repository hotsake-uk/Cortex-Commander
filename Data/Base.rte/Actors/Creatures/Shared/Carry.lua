-- Carrying prey off (Bestiary III, the Harpy's and the Giant Eagle's talons): on a creature whose natural weapon grabs (Shared/Grab.lua:
-- the unit struck is held fast for a moment). While it hunts, a held enemy unit against it that is light enough is snatched up and
-- carried, hanging under it as it flies (the fly hunt's pull away after a dive takes it up and off), then let go to fall. The creature's
-- number values:
--   CarryMaxMass   the heaviest unit it lifts, in kg (default 120)
--   CarrySeconds   how long it holds on (default 2)
--   CarryCooldown  the least time between one snatch and the next, in ms (default 5000)

local function Number(self, key, default)
	return self:NumberValueExists(key) and self:GetNumberValue(key) or default;
end

function Create(self)
	self.carryTimer = Timer();
	self.pickTimer = Timer();
	self.pickTimer.ElapsedSimTimeMS = 100000;
	self.carry = nil;
end

local function Drop(self, victim)
	if victim and MovableMan:ValidMO(victim) then
		victim.Vel = Vector(self.Vel.X, self.Vel.Y + 3);
	end
	self.carry = nil;
	self.pickTimer:Reset();
end

function Update(self)
	if self.carry then
		local victim = self.carry;
		if not MovableMan:ValidMO(victim) or victim:IsDead() or self.Status >= Actor.DYING
			or self.carryTimer:IsPastSimMS(Number(self, "CarrySeconds", 2) * 1000) then
			return Drop(self, victim);
		end
		victim.Pos = self.Pos + Vector(0, self.Radius * 0.6 + victim.Radius * 0.5);
		victim.Vel = Vector(self.Vel.X, self.Vel.Y);
		victim.AngularVel = 0;
		return;
	end
	if self.Status ~= Actor.STABLE or not self.pickTimer:IsPastSimMS(Number(self, "CarryCooldown", 5000)) or not self:NumberValueExists("CreatureHuntID") then
		return;
	end
	local maxMass = Number(self, "CarryMaxMass", 120);
	for actor in MovableMan.Actors do
		if actor.Team ~= self.Team and actor.IsEnsnared and actor.Mass <= maxMass and actor.Status < Actor.DYING
			and not SceneMan:ShortestDistance(self.Pos, actor.Pos, false):MagnitudeIsGreaterThan(self.Radius + actor.Radius + 8) then
			self.carry = actor;
			self.carryTimer:Reset();
			actor:FlashWhite(60);
			break;
		end
	end
end
