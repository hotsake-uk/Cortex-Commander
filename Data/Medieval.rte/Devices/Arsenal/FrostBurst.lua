-- The moment a frost flask breaks (an invisible gib of it): a burst of killing cold. Everyone within reach is chilled as Frost Shards chills
-- them (SpellFX.Chill: slowed, frosted over, steaming), the ground is rimed white, and the air fogs and glitters for a second.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local reach = 48;

function Create(self)
	self.Vel = Vector();
	for _, near in ipairs(FX.ActorsNear(self.Pos, reach)) do
		if near.actor.Team ~= self.Team then
			FX.Chill(near.actor, 3000, self.Team);
		end
	end
	FX.Ring("Mist", self.Pos, 16, 6, 0xDDF2FF, 4);
	FX.Ring("Sparks", self.Pos, 14, 9, 0xEAF8FF, 4);
	PostProcessMan:AddFog(self.Pos, reach, 0.5);
	PostProcessMan:AddLight(self.Pos, 120, 150, 210, 255, 1.6);
	FX.PlaySound("Medieval Spell Ice Shatter", self.Pos);
	-- Rime on the ground: snow laid where the ground is, a few spots about.
	for i = 1, 6 do
		local at = self.Pos + Vector(RangeRand(-reach, reach) * 0.8, 0);
		local ground = FX.GroundBelow(at, 30);
		if ground then
			EmitVisualParticles("Mist", ground, Vector(0, -0.3), 0.8, 2, 0xF0F8FF);
		end
	end
end

function Update(self)
	self.Vel = Vector();
	local fade = math.max(1 - self.Age / self.Lifetime, 0);
	PostProcessMan:AddLight(self.Pos, 40 + 60 * fade, 150, 210, 255, 0.8 * fade);
	if self.Age < 400 and math.random() < 0.5 then
		EmitVisualParticles("Sparks", self.Pos + Vector(RangeRand(-reach, reach) * 0.6, RangeRand(-20, 4)), Vector(0, -0.5), 0.9, 1, 0xEAF8FF);
	end
end
