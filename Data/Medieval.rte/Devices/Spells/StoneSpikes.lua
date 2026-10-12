-- Stone Spikes' line (made by SpellCast.lua at the caster's feet): six spikes of rock punch up out of the ground ahead one after the
-- other, a pace apart, each in a tenth of a second: whoever stands over one (not the caster) is impaled, hurt (the caster's side spared
-- the hurt) and thrown up off their feet, the ground torn and dust blown out. Each spike stands two seconds and crumbles to dust. The
-- spikes are "Medieval Spell Stone Spike" props, pinned where they rise; they follow the ground up and down and stop at a drop or a wall.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local STEP = 20;
local COUNT = 6;
local STEP_MS = 90;
local RISE_MS = 120;
local STAND_MS = 2200;
local DAMAGE = 20;

local function pierce(self, ground)
	local caster = FX.ActorByUID(self.casterUID);
	for _, near in ipairs(FX.ActorsNear(ground + Vector(0, -14), 26, -1)) do
		local a = near.actor;
		if FX.Alive(a) and a.UniqueID ~= self.casterUID and math.abs(near.offset.X) < 14 then
			if a.Team ~= self.Team or self.Team < 0 then
				FX.Hurt(a, DAMAGE, 150);
			end
			a.Vel = Vector(a.Vel.X * 0.5 + self.dir * 2, math.min(a.Vel.Y, -9));
			a.Status = Actor.UNSTABLE;
		end
	end
	for pixel in SceneMan:DislodgePixelCircle(ground + Vector(0, 1), 2) do
		pixel.Vel = Vector(FX.Rand(-3, 3), FX.Rand(-10, -5));
	end
	EmitVisualParticles("Debris", ground, Vector(0, -9), 0.6, 10, 0x6E6660);
	EmitVisualParticles("Dust", ground + Vector(0, -4), Vector(0, -3), 0.8, 8, 0x9C948A);
	CameraMan:AddScreenShake(2, ground);
end

local function raiseSpike(self)
	local probe = Vector(self.Pos.X + self.dir * STEP * self.step, self.groundY - 40);
	local ground, found = FX.GroundBelow(probe, 100);
	if not found or ground.Y <= probe.Y + 1 then
		return false;
	end
	self.groundY = ground.Y;
	local spike = FX.Spawn("MOSRotating", "Medieval Spell Stone Spike", FX.Module, ground + Vector(0, 22), Vector());
	if spike then
		spike.RotAngle = FX.Rand(-0.08, 0.08);
		self.spikes[#self.spikes + 1] = {uid = spike.UniqueID, born = FX.Now(), ground = ground, done = false};
	end
	pierce(self, ground);
	return true;
end

local function crumble(spike, ground)
	EmitVisualParticles("Dust", ground + Vector(0, -10), Vector(0, -1), 1, 12, 0xA09890);
	EmitVisualParticles("Debris", ground + Vector(0, -8), Vector(0, -4), 0.6, 8, 0x6E6660);
	spike.ToDelete = true;
end

function Create(self)
	self.dir = self:GetNumberValue("Dir");
	if self.dir == 0 then
		self.dir = 1;
	end
	self.casterUID = self:GetNumberValue("CasterUID");
	self.start = FX.Now();
	self.step = 0;
	self.groundY = self.Pos.Y;
	self.nextStep = self.start;
	self.spikes = {};
	self.stopped = false;
	FX.PlaySound("Medieval Spell Stone Spikes", self.Pos);
end

function Update(self)
	local now = FX.Now();
	if not self.stopped and now >= self.nextStep then
		self.step = self.step + 1;
		if self.step > COUNT or not raiseSpike(self) then
			self.stopped = true;
		end
		self.nextStep = now + STEP_MS;
	end
	local standing = false;
	for _, entry in ipairs(self.spikes) do
		if not entry.done then
			local mo = MovableMan:FindObjectByUniqueID(entry.uid);
			if not mo or mo.ToDelete then
				entry.done = true;
			else
				local age = now - entry.born;
				local rise = math.min(age / RISE_MS, 1);
				mo.Pos = entry.ground + Vector(0, 22 - 23 * rise);
				mo.Vel = Vector();
				if age >= STAND_MS then
					entry.done = true;
					crumble(mo, entry.ground);
				else
					standing = true;
					if age > STAND_MS - 400 and math.random() < 0.3 then
						EmitVisualParticles("Dust", entry.ground + Vector(FX.Rand(-3, 3), -FX.Rand(0, 18)), Vector(0, 0.5), 0.5, 1, 0xA09890);
					end
				end
			end
		end
	end
	if self.stopped and not standing then
		self.ToDelete = true;
	end
end

function Destroy(self)
	-- (The game ending with spikes still up: they go with the controller.)
	for _, entry in ipairs(self.spikes or {}) do
		if not entry.done then
			local mo = MovableMan:FindObjectByUniqueID(entry.uid);
			if mo then
				mo.ToDelete = true;
			end
		end
	end
end
