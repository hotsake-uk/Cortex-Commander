-- Tornado: a whirling column of wind on the ground that travels on away from the caster for four seconds, following the ground. Dust and
-- debris spiral up it, the air shimmers and gusts upward, and it drags in, lifts and flings units and loose things (anyone's but the
-- caster's). A wall stops it where it stands. Then it dies away.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local LIFE = 4000;
local SPEED = 1.5; -- Pixels an update.
local HEIGHT = 90;

function Create(self)
	self.dir = self:GetNumberValue("Dir");
	if self.dir == 0 then
		self.dir = 1;
	end
	self.casterUID = self:GetNumberValue("CasterUID");
	self.start = FX.Now();
	self.frame = 0;
	self.spin = 0;
	self.groundY = self.Pos.Y;
	self.howl = FX.PlaySound("Medieval Spell Wind", self.Pos);
end

function Update(self)
	local t = FX.Now() - self.start;
	self.frame = self.frame + 1;
	if t >= LIFE then
		EmitVisualParticles("Dust", self.Pos + Vector(0, -30), Vector(0, -2), 1, 20, 0xB8A888);
		self.ToDelete = true;
		return;
	end
	-- Swell up, and die away at the end.
	local strength = math.min(t / 400, 1) * math.min((LIFE - t) / 500, 1);
	-- Travel along the ground.
	local probe = Vector(self.Pos.X + self.dir * SPEED, self.groundY - 30);
	local ground, found = FX.GroundBelow(probe, 80);
	if found and ground.Y > probe.Y + 1 then
		self.Pos = ground;
		self.groundY = ground.Y;
	elseif not found then
		self.Pos = Vector(probe.X, probe.Y + 30);
	end
	if self.howl then
		self.howl.Pos = self.Pos;
	end
	local base = self.Pos;
	local caster = FX.ActorByUID(self.casterUID);
	-- The wind (one push an update, taking turns): pull in round the column, lift up it, and now and then fling out of the top.
	if self.frame % 30 == 0 then
		SceneMan:MagicPush(base + Vector(0, -20), Vector(self.dir, -1.2), 9 * strength, 55, 120, caster);
	elseif self.frame % 2 == 0 then
		SceneMan:MagicPush(base + Vector(0, -30), Vector(0, -1), -1.0 * strength, 70, 360, caster);
	else
		SceneMan:MagicPush(base + Vector(0, -2), Vector(0, -1), 1.2 * strength, 75, 110, caster);
	end
	if self.frame % 5 == 0 then
		SceneMan:AddAirGust(base, Vector(0, -1), 6 * strength, 120, 40);
	end
	if self.frame % 12 == 0 then
		for _, hit in ipairs(FX.ActorsNear(base + Vector(0, -30), 55, -1)) do
			if hit.actor.UniqueID ~= self.casterUID then
				hit.actor:Say("Lifted");
			end
		end
	end
	-- The look: dust whirled round a funnel that widens upward, debris, haze.
	self.spin = self.spin + 0.35;
	for k = 1, math.floor(4 * strength + 0.5) do
		local h = math.random() * HEIGHT;
		local radius = 5 + h * 0.32;
		local angle = self.spin + k * 1.7 + h * 0.08;
		local pos = base + Vector(math.cos(angle) * radius, -h);
		local vel = Vector(-math.sin(angle) * radius * 0.5, -2.5);
		EmitVisualParticles("Dust", pos, vel, 0.1, 1, k % 2 == 0 and 0xB8A888 or 0x8C7E6A);
	end
	if self.frame % 4 == 0 then
		local h = math.random() * HEIGHT * 0.7;
		local angle = self.spin + h * 0.1;
		EmitVisualParticles("Debris", base + Vector(math.cos(angle) * (4 + h * 0.3), -h), Vector(-math.sin(angle) * 8, -6), 0.2, 1, 0x6E5434);
	end
	if self.frame % 10 == 0 then
		PostProcessMan:AddFog(base + Vector(0, -10), 30, 0.15 * strength);
	end
	PostProcessMan:AddShimmer(base + Vector(0, -40), 45 * strength, 0.8);
end

function Destroy(self)
	if self.howl then
		pcall(function() self.howl:Stop(); end);
	end
end
