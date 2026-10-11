-- Gravity Well's vortex: for three seconds everything around (units, items, gibs, particles, the caster's friends too; not the caster) is
-- dragged in toward it, the air bends round it, purple light throbs and motes of light spiral in. Then it bursts outward, throwing all it
-- gathered away with a shockwave and a little damage (sparing the caster's side).

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local PULL_TIME = 3000;
local RANGE = 170;

function Create(self)
	self.start = FX.Now();
	self.frame = 0;
	self.casterUID = self:GetNumberValue("CasterUID");
	self.hum = FX.PlaySound("Medieval Spell Vortex", self.Pos);
	FX.Flash("Purple", self.Pos);
	PostProcessMan:AddShockwave(self.Pos, 4000);
end

function Update(self)
	if self.done then
		return;
	end
	local t = FX.Now() - self.start;
	self.frame = self.frame + 1;
	local caster = FX.ActorByUID(self.casterUID);
	if t < PULL_TIME then
		local grow = math.min(t / 400, 1);
		SceneMan:MagicPush(self.Pos, Vector(0, -1), -1.3 * grow, RANGE, 360, caster);
		PostProcessMan:AddShimmer(self.Pos, (45 + 12 * math.sin(t * 0.01)) * grow, 1.2);
		PostProcessMan:AddLight(self.Pos, 140, 170, 80, 255, (0.9 + 0.3 * math.sin(t * 0.015)) * grow);
		-- Motes spiralling in.
		for _ = 1, 4 do
			local angle = math.random() * math.pi * 2;
			local dir = Vector(math.cos(angle), math.sin(angle));
			local radius = FX.Rand(40, 150);
			local inward = dir * (-radius * 0.12) + Vector(-dir.Y, dir.X) * 9;
			EmitVisualParticles("Sparks", self.Pos + dir * radius, inward, 0.05, 1, math.random() < 0.7 and 0xA050FF or 0xFFFFFF);
		end
		if self.frame % 3 == 0 then
			local angle = math.random() * math.pi * 2;
			local dir = Vector(math.cos(angle), math.sin(angle));
			EmitVisualParticles("Dust", self.Pos + dir * FX.Rand(60, 140), dir * -10, 0.1, 1, 0x302040);
		end
	else
		self.done = true;
		SceneMan:MagicPush(self.Pos, Vector(0, -1), 30, 200, 360, caster);
		FX.HurtRadius(self.Pos, 90, 25, self.Team, 0, 80);
		PostProcessMan:AddShockwave(self.Pos, 16000);
		SceneMan:AddAirBlast(self.Pos, 5000);
		FX.Ring("Sparks", self.Pos, 44, 30, 0xA050FF, 6);
		FX.Ring("Sparks", self.Pos, 20, 18, 0xFFFFFF, 2);
		FX.Flash("Violet", self.Pos);
		CameraMan:AddScreenShake(6, self.Pos);
		FX.PlaySound("Medieval Spell Nova", self.Pos);
		if self.hum then
			self.hum:Stop();
		end
		self.ToDelete = true;
	end
end
