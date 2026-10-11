-- Fireball: a roaring orb of fire flying from the hand, trailing flame, embers and heat haze, lit by its own flickering light (Spells.ini).
-- It bursts on the first thing it meets that isn't a friend, or at the end of its flight: FX.FireBlast, plus a burn for whoever it struck.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local function burst(self, pos, struck)
	if self.burst then
		return;
	end
	self.burst = true;
	if struck and IsActor(struck) then
		FX.Hurt(ToActor(struck), 20, 120);
	end
	FX.FireBlast(pos, self.Team, 1);
	FX.PlaySound("Medieval Spell Fire Blast", pos);
	self.ToDelete = true;
end

function Create(self)
	FX.RememberCaster(self);
	self.frame = 0;
	self.roar = FX.PlaySound("Medieval Spell Fire Roar", self.Pos);
end

function Update(self)
	if self.burst then
		return;
	end
	self.frame = self.frame + 1;
	if self.roar then
		self.roar.Pos = self.Pos;
	end
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
	-- The trail: puffs of fire left behind, embers, a wisp of smoke and the air shimmering with heat.
	if self.frame % 2 == 0 then
		FX.Spawn("MOSParticle", "Medieval Spell Trail Puff", FX.Module, self.Pos, self.Vel * 0.15 + Vector(FX.Rand(-1, 1), FX.Rand(-1.5, 0.5)));
	end
	EmitVisualParticles("Embers", self.Pos, self.Vel * -0.1, 0.5, 2, 0xFF9030);
	if self.frame % 4 == 0 then
		EmitVisualParticles("Smoke", self.Pos, Vector(0, -1), 0.4, 1, 0);
	end
	PostProcessMan:AddShimmer(self.Pos, 12, 0.6);
end

function OnCollideWithTerrain(self, terrainID)
	burst(self, self.Pos, nil);
end
