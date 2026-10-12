-- The Doom (made by FellCast.lua on the enemy under the aim, or the ground there): a burning red sigil, a turning pentacle of light, hangs
-- over the marked unit's head for three seconds, following them, pulsing faster as the end comes; then it bursts, hardest on them and
-- hard on everyone near, sparing the caster's side. On bare ground the sigil burns there and bursts the same, a little weaker.
-- NumberValues: TargetUID (0 for the ground), CasterUID.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");
local MODULE = "FellLegion.rte";

local FUSE = 3000;
local RADIUS = 80;

local function playSound(name, pos)
	local sound = CreateSoundContainer(name, MODULE);
	if sound then
		sound:Play(pos);
	end
end

local function pentacle(centre, radius, angle, brightness)
	local points = {};
	for i = 0, 4 do
		local a = angle + i * (math.pi * 2 / 5);
		points[i + 1] = centre + Vector(math.cos(a) * radius, math.sin(a) * radius * 0.5);
	end
	for i = 1, 5 do
		local from = points[i];
		local to = points[(i + 1) % 5 + 1];
		PostProcessMan:AddEnergyBeam(from, to, 1, 230, 40, 30, brightness, 24);
	end
end

function Create(self)
	self.start = FX.Now();
	self.targetUID = self:GetNumberValue("TargetUID");
	self.angle = 0;
	self.burst = false;
	self.tolls = 0;
	local target = FX.ActorByUID(self.targetUID);
	if target then
		FX.Say(target, "Doomed");
	end
	self.groundMark = not target;
end

local function markPos(self)
	local target = FX.ActorByUID(self.targetUID);
	if target and FX.Alive(target) then
		self.Pos = target.Pos;
		return target.Pos + Vector(0, -28), target;
	end
	return self.Pos + Vector(0, self.groundMark and -14 or -28), nil;
end

local function doom(self, at, target)
	self.burst = true;
	local team = self.Team;
	local scale = self.groundMark and 0.7 or 1;
	if target then
		FX.Hurt(target, 55, 300);
		target.Vel = target.Vel + Vector(0, -6);
	end
	FX.HurtRadius(at, RADIUS * scale, 45 * scale, team, 10 * scale, 200);
	FX.Spawn("MOPixel", "Fell Spell Flash Doom", MODULE, at, Vector());
	FX.Ring("Sparks", at, 36, 30, 0xE6281E, 6);
	FX.Ring("Mist", at, 20, 10, 0x30080A, 10);
	EmitVisualParticles("Smoke", at, Vector(0, -3), 1, 16, 0x100408);
	EmitVisualParticles("Debris", at, Vector(0, -8), 0.8, 10, 0);
	for _ = 1, 3 do
		FX.Spawn("MOSParticle", "Explosion Smoke 1", "Base.rte", at, Vector(FX.Rand(-4, 4), FX.Rand(-6, -2)));
	end
	PostProcessMan:AddShockwave(at, 13000 * scale);
	PostProcessMan:AddScorchMark(at, 4000 * scale);
	PostProcessMan:AddLight(at, 220, 255, 60, 40, 2.5);
	SceneMan:AddAirBlast(at, 3000 * scale);
	CameraMan:AddScreenShake(7 * scale, at);
	playSound("Fell Doom Burst", at);
	self.ToDelete = true;
end

function Update(self)
	if self.burst then
		return;
	end
	local t = FX.Now() - self.start;
	local at, target = markPos(self);
	if t >= FUSE then
		doom(self, target and target.Pos or self.Pos, target);
		return;
	end
	local urgency = t / FUSE;
	self.angle = self.angle + 0.06 + 0.25 * urgency;
	local pulse = 0.7 + 0.5 * math.sin(t * (0.01 + 0.03 * urgency));
	pentacle(at, 10 + 4 * urgency, self.angle, 0.8 + 0.6 * pulse);
	PostProcessMan:AddLight(at, 60 + 40 * urgency, 255, 60, 40, pulse * (0.8 + urgency));
	if math.random() < 0.3 + 0.5 * urgency then
		EmitVisualParticles("Embers", at + Vector(FX.Rand(-8, 8), FX.Rand(-3, 3)), Vector(0, -1.5), 0.4, 1, 0xFF4030);
	end
	-- A toll at each heartbeat.
	local beat = math.floor(t / 1000);
	if beat > self.tolls then
		self.tolls = beat;
		FX.Ring("Sparks", at, 10, 6, 0xFF6040, 4);
	end
end
