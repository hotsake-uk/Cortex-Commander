-- A drop of hellfire falling out of the sky (Hellfire Rain), trailing green flame and black smoke, lit by its own light. It bursts on
-- the first thing it meets that isn't a friend, or when it lands: a green fiery explosion, smaller than a fireball's, and every enemy
-- within two paces of it is Burning (FellEffects.lua): green flames clinging for four seconds, their health away a little at a time.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");
local Fell = dofile("FellLegion.rte/Devices/Spells/FellEffects.lua");

local function burst(self, pos, struck)
	if self.burst then
		return;
	end
	self.burst = true;
	if struck and IsActor(struck) then
		FX.Hurt(ToActor(struck), 14, 120);
	end
	FX.FireBlast(pos, self.Team, 0.6);
	for _, near in ipairs(FX.ActorsNear(pos, 40, self.Team)) do
		if not near.actor.FireProof then
			Fell.Apply(near.actor, "Burning");
		end
	end
	-- Green over the orange.
	EmitVisualParticles("Sparks", pos, Vector(0, -8), 1, 30, 0x6EFF6E);
	EmitVisualParticles("Embers", pos, Vector(0, -4), 1, 20, 0x40C040);
	EmitVisualParticles("Smoke", pos, Vector(0, -2), 0.9, 8, 0x101010);
	FX.Spawn("MOPixel", "Fell Spell Flash Hellfire", "FellLegion.rte", pos, Vector());
	PostProcessMan:AddLight(pos, 160, 90, 255, 90, 2.5);
	FX.PlaySound("Medieval Spell Fire Blast", pos);
	self.ToDelete = true;
end

function Create(self)
	self.frame = 0;
end

function Update(self)
	if self.burst then
		return;
	end
	self.frame = self.frame + 1;
	local hitPos, struck = FX.ImpactAhead(self, true);
	if hitPos then
		local back = self.Vel.Magnitude > 0.1 and self.Vel.Normalized * 4 or Vector();
		burst(self, hitPos - back, struck);
		return;
	end
	if self.Lifetime > 0 and self.Age >= self.Lifetime - TimerMan.DeltaTimeMS * 2 then
		burst(self, self.Pos, nil);
		return;
	end
	if self.frame % 2 == 0 then
		FX.Spawn("MOSParticle", "Medieval Spell Trail Puff", "Medieval.rte", self.Pos, self.Vel * 0.1 + Vector(FX.Rand(-1, 1), FX.Rand(-1.5, 0.5)));
	end
	EmitVisualParticles("Embers", self.Pos, self.Vel * -0.1, 0.5, 2, 0x6EFF6E);
	if self.frame % 3 == 0 then
		EmitVisualParticles("Smoke", self.Pos, Vector(0, -1), 0.4, 1, 0x101010);
	end
	PostProcessMan:AddShimmer(self.Pos, 12, 0.6);
end

function OnCollideWithTerrain(self, terrainID)
	burst(self, self.Pos, nil);
end
