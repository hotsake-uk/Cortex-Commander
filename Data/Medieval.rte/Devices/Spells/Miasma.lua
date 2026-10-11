-- What a Poison Cloud leaves: for five seconds the gas is topped up, a green fog hangs, bubbles rise and a sickly light pulses.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local LIFE = 5000;

function Create(self)
	self.start = FX.Now();
	self.frame = 0;
end

function Update(self)
	local t = FX.Now() - self.start;
	self.frame = self.frame + 1;
	local fade = math.max(1 - t / LIFE, 0);
	PostProcessMan:AddLight(self.Pos + Vector(0, -10), 110, 120, 255, 80, (0.5 + 0.2 * math.sin(t * 0.008)) * fade);
	if self.frame % 10 == 0 then
		SceneMan:AddGas(self.Pos + Vector(FX.Rand(-30, 30), FX.Rand(-24, 4)), 1, 1.2);
		PostProcessMan:AddFog(self.Pos + Vector(FX.Rand(-20, 20), -8), 50, 0.18 * fade);
	end
	if self.frame % 2 == 0 then
		EmitVisualParticles("Embers", self.Pos + Vector(FX.Rand(-35, 35), FX.Rand(-6, 4)), Vector(0, -1.5), 0.4, 1, 0x9CFF6A);
	end
	if self.frame % 4 == 0 then
		EmitVisualParticles("Mist", self.Pos + Vector(FX.Rand(-30, 30), FX.Rand(-20, 0)), Vector(0, -0.3), 0.6, 1, 0x8CFF5A);
	end
	if t >= LIFE then
		self.ToDelete = true;
	end
end
