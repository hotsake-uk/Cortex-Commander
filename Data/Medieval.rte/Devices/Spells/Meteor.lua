-- A meteor of Meteor Storm: a burning rock out of the sky with a flaming, smoking tail, that bursts where it lands in a great fiery
-- explosion, blasting a crater out of the ground.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local function crash(self, pos)
	if self.crashed then
		return;
	end
	self.crashed = true;
	FX.FireBlast(pos, self.Team, 1.4);
	for pixel in SceneMan:DislodgePixelCircle(pos, 7) do
		pixel.Vel = Vector(FX.Rand(-7, 7), FX.Rand(-14, -4));
	end
	EmitVisualParticles("Debris", pos, Vector(0, -14), 0.7, 24, 0x6E5434);
	EmitVisualParticles("Dust", pos, Vector(0, -4), 1, 16, 0x8C7A5C);
	PostProcessMan:AddFog(pos + Vector(0, -10), 50, 0.35);
	CameraMan:AddScreenShake(9, pos);
	FX.PlaySound("Medieval Spell Fire Blast", pos);
	if self.whistle then
		self.whistle:Stop();
	end
	self.ToDelete = true;
end

function Create(self)
	self.ignoreID = FX.NoMOID;
	self.frame = 0;
	self.whistle = FX.PlaySound("Medieval Spell Meteor Fall", self.Pos);
end

function Update(self)
	if self.crashed then
		return;
	end
	self.frame = self.frame + 1;
	if self.whistle then
		self.whistle.Pos = self.Pos;
	end
	local hitPos = FX.ImpactAhead(self, true);
	if hitPos then
		local back = self.Vel.Magnitude > 0.1 and self.Vel.Normalized * 4 or Vector();
		crash(self, hitPos - back);
		return;
	end
	if self.Lifetime > 0 and self.Age >= self.Lifetime - TimerMan.DeltaTimeMS * 2 then
		crash(self, self.Pos);
		return;
	end
	-- The tail: fire puffs and rolling smoke left behind it, embers and the air bent by the heat.
	if self.frame % 2 == 0 then
		FX.Spawn("MOSParticle", "Medieval Spell Trail Puff", FX.Module, self.Pos, self.Vel * 0.1);
	else
		FX.Spawn("MOSParticle", "Medieval Spell Meteor Smoke", FX.Module, self.Pos - self.Vel.Normalized * 6, self.Vel * 0.05 + Vector(0, -0.5));
	end
	EmitVisualParticles("Embers", self.Pos, self.Vel * -0.15, 0.5, 3, 0xFF7A20);
	EmitVisualParticles("Smoke", self.Pos, self.Vel * -0.05, 0.5, 1, 0);
	PostProcessMan:AddShimmer(self.Pos, 18, 0.8);
end

function OnCollideWithTerrain(self, terrainID)
	crash(self, self.Pos);
end
