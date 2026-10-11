-- Raise Dead's bolt: a green necrotic spark that flies through units and comes down on the ground (where it lands, or below the end of
-- its flight). There a portal opens and the dead claw up out of the ground (RaiseDeadPortal.lua).

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local function land(self, pos)
	if self.landed then
		return;
	end
	self.landed = true;
	local ground, found = FX.GroundBelow(pos, 300);
	if found then
		FX.Controller("Medieval Spell Necrotic Portal", ground, self.Team, self.casterUID);
	else
		EmitVisualParticles("Mist", pos, Vector(0, -1), 1, 12, 0x60FF70);
	end
	self.ToDelete = true;
end

function Create(self)
	FX.RememberCaster(self);
end

function Update(self)
	if self.landed then
		return;
	end
	local hitPos = FX.ImpactAhead(self, false);
	if hitPos then
		local back = self.Vel.Magnitude > 0.1 and self.Vel.Normalized * 2 or Vector();
		land(self, hitPos - back);
		return;
	end
	if self.Lifetime > 0 and self.Age >= self.Lifetime - TimerMan.DeltaTimeMS * 2 then
		land(self, self.Pos);
		return;
	end
	EmitVisualParticles("Embers", self.Pos, self.Vel * -0.1, 0.6, 1, 0x60FF70);
	EmitVisualParticles("Mist", self.Pos, Vector(0, -0.3), 0.5, 1, 0x40C050);
end

function OnCollideWithTerrain(self, terrainID)
	land(self, self.Pos);
end
