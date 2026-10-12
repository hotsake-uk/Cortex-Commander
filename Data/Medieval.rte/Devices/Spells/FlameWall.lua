-- Flame Wall's wall (made by SpellCast.lua on the ground in front of the caster): a line of fire 60 pixels wide and as tall as a man,
-- following the ground, for eight seconds. Tall flames, embers and heat shimmer the whole time; the ground under it is set alight, the
-- ground scorched, and every fifth of a second whoever stands in it (not of the caster's side) is hurt and set alight. Real fire puffs
-- are thrown now and then for the look; the rest is render-only, so it costs little.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local LIFE = 8000;
local HALF = 30;
local HEIGHT = 40;

function Create(self)
	self.start = FX.Now();
	self.frame = 0;
	self.nextBurn = self.start;
	self.nextPuff = self.start;
	self.casterUID = self:GetNumberValue("CasterUID");
	-- The wall's feet along the ground, 15 pixels apart, each on the ground below or above the cast point.
	self.feet = {};
	for x = -HALF, HALF, 15 do
		local ground, found = FX.GroundBelow(Vector(self.Pos.X + x, self.Pos.Y - 30), 70);
		self.feet[#self.feet + 1] = found and ground or Vector(self.Pos.X + x, self.Pos.Y);
	end
	for _, foot in ipairs(self.feet) do
		PostProcessMan:AddScorchMark(foot, 1500);
		SceneMan:IgniteArea(foot, 8);
	end
	FX.Flash("Fire", self.Pos + Vector(0, -HEIGHT / 2));
	PostProcessMan:AddShockwave(self.Pos, 2500);
	self.roar = FX.PlaySound("Medieval Spell Fire Roar", self.Pos);
end

local function footAt(self, x)
	local i = math.max(1, math.min(#self.feet, math.floor((x + HALF) / 15 + 1.5)));
	return self.feet[i];
end

function Update(self)
	local now = FX.Now();
	local t = now - self.start;
	self.frame = self.frame + 1;
	if t >= LIFE then
		self.ToDelete = true;
		return;
	end
	local strength = math.min(t / 400, 1) * math.min((LIFE - t) / 800, 1);
	-- Flames and embers.
	for _ = 1, 3 do
		local x = FX.Rand(-HALF, HALF);
		local foot = footAt(self, x);
		local at = Vector(foot.X + (x - (foot.X - self.Pos.X)), foot.Y - FX.Rand(0, 6));
		EmitVisualParticles("Embers", at, Vector(FX.Rand(-1, 1), -FX.Rand(4, 9) * strength), 0.4, 2, 0xFF9030);
	end
	if self.frame % 2 == 0 then
		local x = FX.Rand(-HALF, HALF);
		local foot = footAt(self, x);
		EmitVisualParticles("Sparks", Vector(self.Pos.X + x, foot.Y - FX.Rand(4, HEIGHT) * strength), Vector(0, -3), 0.6, 2, 0xFFD060);
		EmitVisualParticles("Smoke", Vector(self.Pos.X + x, foot.Y - HEIGHT * strength), Vector(0, -2), 0.7, 1, 0);
	end
	PostProcessMan:AddLight(self.Pos + Vector(0, -HEIGHT / 2), 160, 255, 140, 40, (1.2 + 0.3 * math.sin(t * 0.02)) * strength);
	PostProcessMan:AddShimmer(self.Pos + Vector(0, -HEIGHT * 0.7), 40 * strength, 0.9);
	if now >= self.nextPuff then
		self.nextPuff = now + 500;
		local x = FX.Rand(-HALF, HALF);
		local foot = footAt(self, x);
		FX.Spawn("MOSParticle", "Fire Puff Large", "Base.rte", Vector(self.Pos.X + x, foot.Y - 6), Vector(FX.Rand(-0.5, 0.5), -FX.Rand(2, 4)));
	end
	-- The burning.
	if now >= self.nextBurn then
		self.nextBurn = now + 200;
		local x = FX.Rand(-HALF, HALF);
		SceneMan:IgniteArea(Vector(self.Pos.X + x, footAt(self, x).Y - 2), 6);
		for _, near in ipairs(FX.ActorsNear(self.Pos + Vector(0, -HEIGHT / 2), HALF + 24, self.Team)) do
			local a = near.actor;
			if FX.Alive(a) and math.abs(near.offset.X) < HALF + 6 and near.offset.Y > -HEIGHT - 10 and near.offset.Y < HEIGHT / 2 + 12 then
				FX.Hurt(a, 5 * strength, 60);
				SceneMan:IgniteArea(a.Pos, 8);
				if a.UniqueID ~= self.casterUID and math.random() < 0.2 then
					a:Say("OnFire");
				end
			end
		end
	end
end

function Destroy(self)
	if self.roar then
		pcall(function() self.roar:Stop(); end);
	end
end
