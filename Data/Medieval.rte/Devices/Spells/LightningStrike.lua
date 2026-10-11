-- Call Lightning's strike (made by SpellCast.lua where the bolt comes down): the bolt out of the sky, flickering a few times, a blue-white
-- flash, thunder, a shower of sparks, a shockwave and a scorch on the ground, a small fire, and heavy damage where it strikes (more to the
-- unit it was called down on), every unit hurt flashing white. The caster's own side is spared.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local LIFE = 320;

-- A jagged line of light from the sky down to the strike, drawn as energy beams (shows even with the game's lightning bolts off).
local function drawBolt(self, brightness)
	local from = self.sky;
	local segments = 7;
	for i = 1, segments do
		local s = i / segments;
		local to = self.sky + (self.Pos - self.sky) * s;
		if i < segments then
			to = to + Vector(self.jag[i], 0);
		end
		PostProcessMan:AddEnergyBeam(from, to, 2, 200, 225, 255, brightness, 60);
		from = to;
	end
end

function Create(self)
	local team = self.Team;
	local struck = FX.ActorByUID(self:GetNumberValue("StruckUID"));
	self.start = FX.Now();
	self.sky = Vector(self.Pos.X + FX.Rand(-70, 70), self.Pos.Y - 560);
	self.jag = {};
	for i = 1, 7 do
		self.jag[i] = FX.Rand(-18, 18);
	end
	PostProcessMan:AddLightningBolt(self.sky, self.Pos, self:GetNumberValue("Seed"));
	-- The strike.
	if struck and struck.Team ~= team then
		FX.Hurt(struck, 30, 250);
	end
	for _, hit in ipairs(FX.HurtRadius(self.Pos, 45, 55, team, 6, 250)) do
		hit.actor:FlashWhite(300);
	end
	SceneMan:IgniteArea(self.Pos, 16);
	PostProcessMan:AddShockwave(self.Pos, 9000);
	PostProcessMan:AddScorchMark(self.Pos, 5000);
	SceneMan:AddAirBlast(self.Pos, 2500);
	CameraMan:AddScreenShake(8, self.Pos);
	EmitVisualParticles("Sparks", self.Pos, Vector(0, -10), 1, 50, 0xCFE6FF);
	EmitVisualParticles("Sparks", self.Pos, Vector(0, -4), 1, 20, 0xFFFFFF);
	EmitVisualParticles("Smoke", self.Pos, Vector(0, -2), 0.8, 6, 0);
	FX.Flash("White", self.Pos);
	for _ = 1, 3 do
		FX.Spawn("MOPixel", "Spark Blue 1", "Base.rte", self.Pos, Vector(FX.Rand(-8, 8), FX.Rand(-12, -3)));
	end
	FX.PlaySound("Medieval Spell Thunder", self.Pos);
end

function Update(self)
	local t = FX.Now() - self.start;
	-- Flickers: bright, dim, bright again, gone.
	local on = t < 70 or (t > 120 and t < 170) or (t > 220 and t < 260);
	if on then
		drawBolt(self, t < 70 and 1.6 or 1.0);
		PostProcessMan:AddLight(self.Pos + Vector(0, -20), 260, 190, 215, 255, t < 70 and 3 or 1.6);
	end
	if t >= LIFE then
		self.ToDelete = true;
	end
end
