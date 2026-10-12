-- The ball of light the Light spell leaves (SpellCast.lua): it floats over the caster's head for Seconds (30), following them about, lighting
-- the scene a good way round and glittering a little; without a caster it hangs where it was cast. Then it winks out.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

function Create(self)
	self.casterUID = self:GetNumberValue("CasterUID");
	self.untilMS = FX.Now() + (self:NumberValueExists("Seconds") and self:GetNumberValue("Seconds") or 30) * 1000;
	self.frame = 0;
	self.bob = math.random() * math.pi * 2;
end

function Update(self)
	local now = FX.Now();
	if now > self.untilMS then
		EmitVisualParticles("Sparks", self.Pos, Vector(0, -1), 0.8, 12, 0xFFFFE6);
		self.ToDelete = true;
		return;
	end
	self.frame = self.frame + 1;
	local caster = FX.ActorByUID(self.casterUID);
	if caster then
		local goal = caster.Pos + Vector(0, -38 + 3 * math.sin(now * 0.003 + self.bob));
		local off = SceneMan:ShortestDistance(self.Pos, goal, true);
		self.Pos = self.Pos + off * 0.15;
	end
	local flicker = 0.9 + 0.1 * math.sin(now * 0.011 + self.bob);
	PostProcessMan:AddLight(self.Pos, 320, 255, 250, 220, 1.6 * flicker);
	PostProcessMan:AddLight(self.Pos, 60, 255, 255, 255, 2.2);
	if self.frame % 6 == 0 then
		EmitVisualParticles("Sparks", self.Pos + Vector(FX.Rand(-3, 3), FX.Rand(-3, 3)), Vector(FX.Rand(-0.5, 0.5), -0.6), 0.9, 1, 0xFFFFF0);
	end
end
