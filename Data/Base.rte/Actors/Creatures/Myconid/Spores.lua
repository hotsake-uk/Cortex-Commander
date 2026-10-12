-- The Myconid's spores (Bestiary II): with an enemy unit near, every so often it puffs a cloud of toxic spores out of its cap (SceneMan:AddGas,
-- the gas the Plague Pot and the Stink Flask make: it hurts whoever breathes it), lit violet. The myconid's number values:
--   SporeRange     how near an enemy must be, in pixels (default 100)
--   SporeCooldown  how long between clouds, in ms (default 3000)
--   SporeAmount    how much gas each puff of the five is (default 2.5)

local TOXIC = 1; -- GasGrid::Kind::Toxic

local function Number(self, key, default)
	return self:NumberValueExists(key) and self:GetNumberValue(key) or default;
end

function Create(self)
	self.sporeTimer = Timer();
	self.sporeTimer.ElapsedSimTimeMS = 1500;
end

function Update(self)
	if self.Status >= Actor.DYING or not self.sporeTimer:IsPastSimMS(Number(self, "SporeCooldown", 3000)) then
		return;
	end
	local range = Number(self, "SporeRange", 100);
	local near = false;
	for actor in MovableMan.Actors do
		if actor.Team ~= self.Team and actor.Status < Actor.DYING and not SceneMan:ShortestDistance(self.Pos, actor.Pos, false):MagnitudeIsGreaterThan(range) then
			near = true;
			break;
		end
	end
	if not near then
		return;
	end
	self.sporeTimer:Reset();
	local amount = Number(self, "SporeAmount", 2.5);
	local top = self.Pos + Vector(0, -self.Radius);
	for _ = 1, 5 do
		SceneMan:AddGas(top + Vector(RangeRand(-18, 18), RangeRand(-20, -2)), TOXIC, amount);
	end
	EmitVisualParticles("Mist", top, Vector(0, -2), 1.2, 8, 0x8A4C9A);
	PostProcessMan:AddLight(top, 50, 170, 90, 200, 0.8);
end
