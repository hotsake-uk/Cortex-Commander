-- The moment a healing draught breaks (an invisible gib of it): a red, sweet vapour that heals whoever breathes it within a couple of paces,
-- the thrower's side or not: thirty health and a wound or two each, in a shower of warm sparkles.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local reach = 40;

function Create(self)
	self.Vel = Vector();
	for _, near in ipairs(FX.ActorsNear(self.Pos, reach)) do
		local a = near.actor;
		if FX.Alive(a) then
			a.Health = math.min(a.Health + 30, a.MaxHealth);
			a:RemoveWounds(2);
			a:FlashWhite(40);
			a:Say("Healed");
			EmitVisualParticles("Sparks", a.Pos + Vector(0, -6), Vector(0, -2), 0.8, 8, 0xFFB0C0);
		end
	end
	FX.Ring("Mist", self.Pos, 12, 4, 0xFFC0D0, 3);
	PostProcessMan:AddLight(self.Pos, 100, 255, 140, 160, 1.4);
end

function Update(self)
	self.Vel = Vector();
	local fade = math.max(1 - self.Age / self.Lifetime, 0);
	PostProcessMan:AddLight(self.Pos, 30 + 50 * fade, 255, 140, 160, 0.7 * fade);
	if math.random() < 0.4 * fade then
		EmitVisualParticles("Sparks", self.Pos + Vector(RangeRand(-reach, reach) * 0.5, RangeRand(-16, 4)), Vector(0, -1), 0.9, 1, 0xFFD0D8);
	end
end
