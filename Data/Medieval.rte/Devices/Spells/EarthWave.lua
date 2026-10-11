-- Earth Shatter's wave: from the caster's feet it races away along the ground, 25 pixels at a step, ten steps (250 pixels) in under a
-- second. At each step the ground erupts: rocks torn out and thrown up, dust and debris, a ripple in the air, the screen shaking, and
-- whoever stands there (the caster's friends too, not the caster) knocked up off their feet and hurt a little (the caster's side spared
-- the hurt). It follows the ground up and down slopes, and stops at a drop or a wall it can't climb.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local STEP = 25;
local STEPS = 10;
local STEP_MS = 75;

local function erupt(self, ground)
	local caster = FX.ActorByUID(self.casterUID);
	SceneMan:MagicPush(ground + Vector(0, -2), Vector(0, -1), 15, 42, 140, caster);
	FX.HurtRadius(ground + Vector(0, -10), 30, 14, self.Team, 0, 0);
	for pixel in SceneMan:DislodgePixelCircle(ground + Vector(0, 2), 3) do
		pixel.Vel = Vector(FX.Rand(-3, 3) + self.dir * 2, FX.Rand(-14, -6));
	end
	EmitVisualParticles("Debris", ground, Vector(self.dir * 2, -12), 0.5, 14, 0x6E5434);
	EmitVisualParticles("Dust", ground + Vector(0, -4), Vector(self.dir * 2, -3), 0.8, 10, 0x9C8A6A);
	PostProcessMan:AddFog(ground + Vector(0, -8), 26, 0.28);
	PostProcessMan:AddShockwave(ground, 3000);
	CameraMan:AddScreenShake(3, ground);
	for _, hit in ipairs(FX.ActorsNear(ground + Vector(0, -12), 28, -1)) do
		if hit.actor.UniqueID ~= self.casterUID then
			hit.actor.Status = Actor.UNSTABLE;
		end
	end
end

function Create(self)
	self.dir = self:GetNumberValue("Dir");
	if self.dir == 0 then
		self.dir = 1;
	end
	self.casterUID = self:GetNumberValue("CasterUID");
	self.step = 0;
	self.groundY = self.Pos.Y;
	self.nextStep = FX.Now();
	self.rumble = FX.PlaySound("Medieval Spell Rumble", self.Pos);
end

function Update(self)
	if FX.Now() < self.nextStep then
		return;
	end
	self.step = self.step + 1;
	local probe = Vector(self.Pos.X + self.dir * STEP, self.groundY - 40);
	local ground, found = FX.GroundBelow(probe, 100);
	if self.step > STEPS or not found or ground.Y <= probe.Y + 1 then
		self.ToDelete = true;
		return;
	end
	self.Pos = ground;
	self.groundY = ground.Y;
	if self.rumble then
		self.rumble.Pos = ground;
	end
	erupt(self, ground);
	self.nextStep = FX.Now() + STEP_MS;
end
