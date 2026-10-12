-- Bone Spear: a spear of bone flying flat from the hand (Medieval.rte's Arrow.lua, which the preset keeps, does the sticking and the
-- pinning). This adds the look: a trail of bone dust and a faint green light, and a flash of green sparks when it strikes.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

function Create(self)
	FX.RememberCaster(self);
	self.frame = 0;
	self.flashed = false;
end

function Update(self)
	self.frame = self.frame + 1;
	if self.Vel.Magnitude > 8 then
		if self.frame % 2 == 0 then
			EmitVisualParticles("Dust", self.Pos, self.Vel * -0.05, 0.4, 1, 0xD0C8A8);
		end
		PostProcessMan:AddLight(self.Pos, 30, 120, 255, 120, 0.5);
	elseif not self.flashed then
		self.flashed = true;
		EmitVisualParticles("Sparks", self.Pos, Vector(0, -3), 1, 8, 0x90FF90);
		EmitVisualParticles("Dust", self.Pos, Vector(0, -2), 1, 6, 0xD0C8A8);
	end
end
