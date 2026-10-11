-- Meteor Storm's sigil: a glowing red bolt from the hand. Where it lands (or, at the end of its flight, on the ground below it) a rune
-- circle opens (MeteorRune.lua) and calls the meteors down.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local function land(self, pos)
	if self.landed then
		return;
	end
	self.landed = true;
	local ground, found = FX.GroundBelow(pos, 400);
	FX.Controller("Medieval Spell Meteor Rune", found and ground or pos, self.Team, self.casterUID);
	EmitVisualParticles("Sparks", pos, Vector(0, -6), 1, 18, 0xFF4020);
	FX.Flash("Red", found and ground or pos);
	self.ToDelete = true;
end

function Create(self)
	FX.RememberCaster(self);
end

function Update(self)
	if self.landed then
		return;
	end
	local hitPos = FX.ImpactAhead(self, true);
	if hitPos then
		local back = self.Vel.Magnitude > 0.1 and self.Vel.Normalized * 3 or Vector();
		land(self, hitPos - back);
		return;
	end
	if self.Lifetime > 0 and self.Age >= self.Lifetime - TimerMan.DeltaTimeMS * 2 then
		land(self, self.Pos);
		return;
	end
	EmitVisualParticles("Sparks", self.Pos, self.Vel * -0.15, 0.4, 1, 0xFF5030);
	EmitVisualParticles("Embers", self.Pos, Vector(0, -1), 0.6, 1, 0xFF3010);
end

function OnCollideWithTerrain(self, terrainID)
	land(self, self.Pos);
end
