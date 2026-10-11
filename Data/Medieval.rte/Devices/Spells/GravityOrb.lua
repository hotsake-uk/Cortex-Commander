-- Gravity Well's orb: a dark, swirling ball lobbed from the hand. Where it stops (on the ground, a wall or a unit, or at the end of its
-- flight) it opens a vortex (GravityVortex.lua).

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local function open(self, pos)
	if self.opened then
		return;
	end
	self.opened = true;
	FX.Controller("Medieval Spell Gravity Vortex", pos, self.Team, self.casterUID);
	self.ToDelete = true;
end

function Create(self)
	FX.RememberCaster(self);
end

function Update(self)
	if self.opened then
		return;
	end
	local hitPos = FX.ImpactAhead(self, true);
	if hitPos then
		local back = self.Vel.Magnitude > 0.1 and self.Vel.Normalized * 8 or Vector(0, -8);
		open(self, hitPos - back + Vector(0, -4));
		return;
	end
	if self.Lifetime > 0 and self.Age >= self.Lifetime - TimerMan.DeltaTimeMS * 2 then
		open(self, self.Pos);
		return;
	end
	local angle = self.Age * 0.02;
	EmitVisualParticles("Sparks", self.Pos + Vector(math.cos(angle), math.sin(angle)) * 6, Vector(-math.sin(angle), math.cos(angle)) * 4, 0.1, 1, 0xB060FF);
	PostProcessMan:AddShimmer(self.Pos, 14, 0.7);
end

function OnCollideWithTerrain(self, terrainID)
	open(self, self.Pos + Vector(0, -6));
end
