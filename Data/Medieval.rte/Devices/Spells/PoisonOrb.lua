-- Poison Cloud's orb: a lobbed, bubbling green ball that bursts where it lands (or on an enemy, or at the end of its flight) into a great
-- cloud of toxic gas, a green fog and a sickly light that lingers and bubbles for a while (Miasma.lua keeps it topped up).

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local function burst(self, pos)
	if self.burst then
		return;
	end
	self.burst = true;
	for _ = 1, 12 do
		SceneMan:AddGas(pos + Vector(FX.Rand(-34, 34), FX.Rand(-28, 6)), 1, 2.5);
	end
	PostProcessMan:AddFog(pos, 70, 0.55);
	EmitVisualParticles("Mist", pos, Vector(0, -1), 1, 30, 0x8CFF5A);
	EmitVisualParticles("Droplets", pos, Vector(0, -6), 0.9, 16, 0x7CE040);
	EmitVisualParticles("Embers", pos, Vector(0, -2), 1, 20, 0xA0FF70);
	FX.HurtRadius(pos, 30, 8, self.Team, 0, 0);
	FX.Flash("Green", pos);
	FX.Controller("Medieval Spell Miasma", pos, self.Team, self.casterUID);
	FX.PlaySound("Medieval Spell Poison Burst", pos);
	self.ToDelete = true;
end

function Create(self)
	FX.RememberCaster(self);
	self.frame = 0;
end

function Update(self)
	if self.burst then
		return;
	end
	self.frame = self.frame + 1;
	local hitPos = FX.ImpactAhead(self, true);
	if hitPos then
		local back = self.Vel.Magnitude > 0.1 and self.Vel.Normalized * 4 or Vector();
		burst(self, hitPos - back);
		return;
	end
	if self.Lifetime > 0 and self.Age >= self.Lifetime - TimerMan.DeltaTimeMS * 2 then
		burst(self, self.Pos);
		return;
	end
	if self.frame % 2 == 0 then
		EmitVisualParticles("Droplets", self.Pos, self.Vel * -0.05, 0.6, 1, 0x7CE040);
	end
	EmitVisualParticles("Mist", self.Pos, Vector(0, -0.5), 0.5, 1, 0x8CFF5A);
end

function OnCollideWithTerrain(self, terrainID)
	burst(self, self.Pos);
end
