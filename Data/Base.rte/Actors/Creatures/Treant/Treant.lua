-- The Treant's roots in water (Bestiary II): with its feet in liquid (Actor.LiquidDepth: 1 feet in, 2 body in, 3 head under) it drinks,
-- and its wounds close: TreantDrinkRate health a second (a number value, default 3), green motes rising off it. A treant on fire drinks
-- nothing (the fire is the water's to put out).

local function Number(self, key, default)
	return self:NumberValueExists(key) and self:GetNumberValue(key) or default;
end

function Create(self)
	self.drinkTimer = Timer();
end

function Update(self)
	if self.Status >= Actor.DYING or self.Health >= self.MaxHealth or self.LiquidDepth < 1 then
		return;
	end
	if self.drinkTimer:IsPastSimMS(500) then
		self.drinkTimer:Reset();
		self.Health = math.min(self.MaxHealth, self.Health + Number(self, "TreantDrinkRate", 3) * 0.5);
		EmitVisualParticles("Sparks", self.Pos + Vector(RangeRand(-10, 10), -self.Radius * 0.3), Vector(0, -2), 0.8, 3, 0x6EDC50);
	end
end
