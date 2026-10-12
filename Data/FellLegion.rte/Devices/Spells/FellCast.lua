-- The Fell Legion spells (Spells.ini): what happens at the caster's hand each cast. The round a spell fires does the rest (a shadow orb, a
-- bone spear, the shadowflame's sparks), or for the spells cast on the spot (Hellfire Rain, Doom, Chains of the Damned) the round is an
-- invisible marker that only tells the AI how far the spell reaches, and this script makes the effect.
-- Each cast: the caster says the spell's cast line (Speech/FellLegion.ini; the engine's chance and cooldown apply), and the hand flashes
-- in the spell's colour. Built as Medieval.rte/Devices/Spells/SpellCast.lua is, on the same SpellFX.lua.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");
local MODULE = "FellLegion.rte";

-- An invisible controller of this module's (Spells.ini), with the caster and team.
local function controller(name, pos, team, casterUID, values)
	local c = CreateMOPixel(name, MODULE);
	if c then
		c.Pos = Vector(pos.X, pos.Y);
		c.Vel = Vector();
		c.Team = team or -1;
		c:SetNumberValue("CasterUID", casterUID or 0);
		for key, value in pairs(values or {}) do
			c:SetNumberValue(key, value);
		end
		MovableMan:AddParticle(c);
	end
	return c;
end

-- Finds what a bolt from the hand along the aim strikes within reach: the first unit not of the caster's team (looking past friends), or
-- the ground. Out of reach, the ground under the end of the aim. Returns the point and the unit struck, if one was.
local function strikePoint(start, aim, reach, caster, team)
	local from = Vector(start.X, start.Y);
	local left = reach;
	local ignore = caster and caster.ID or FX.NoMOID;
	for _ = 1, 4 do
		local ray = aim * left;
		local id = SceneMan:CastMORay(from, ray, ignore, team, 0, false, 1);
		if id ~= FX.NoMOID then
			local mo = MovableMan:GetMOFromID(id);
			local root = mo and (MovableMan:GetMOFromID(mo.RootID) or mo);
			local hit = SceneMan:GetLastRayHitPos();
			local hitPos = Vector(hit.X, hit.Y);
			if root and (root.Team ~= team or team < 0) then
				return hitPos, IsActor(root) and ToActor(root) or nil;
			end
			local gone = SceneMan:ShortestDistance(from, hitPos, true).Magnitude + 12;
			left = left - gone;
			if left <= 0 or not root then
				break;
			end
			from = from + aim * gone;
			ignore = root.ID;
		else
			break;
		end
	end
	local hit, free = Vector(), Vector();
	if SceneMan:CastObstacleRay(start, aim * reach, hit, free, caster and caster.ID or FX.NoMOID, team, 0, 1) >= 0 then
		local id = SceneMan:GetMOIDPixel(hit.X, hit.Y);
		if id == FX.NoMOID then
			return hit, nil;
		end
	end
	local ground = FX.GroundBelow(start + aim * reach, 300);
	return ground, nil;
end

-- The ground a little in front of the caster's feet.
local function groundInFront(caster, device, dirX, ahead)
	local base = caster and caster.Pos or device.Pos;
	local ground = FX.GroundBelow(base + Vector(dirX * ahead, -12), 80);
	return ground;
end

local SPELLS = {
	["Shadow Bolt"] = {speech = "CastShadow", colour = {150, 70, 220}, hex = 0x9646DC},
	["Bone Spear"] = {speech = "CastBone", colour = {220, 214, 190}, hex = 0xDCD6BE},
	["Hellfire Rain"] = {speech = "CastHellfire", colour = {110, 255, 110}, hex = 0x6EFF6E},
	["Shadowflame"] = {speech = "CastHellfire", colour = {90, 220, 90}, hex = 0x5ADC5A, channel = true},
	["Doom"] = {speech = "CastDoom", colour = {230, 40, 30}, hex = 0xE6281E},
	["Chains of the Damned"] = {speech = "CastChains", colour = {120, 120, 140}, hex = 0x78788C},
};
local DEFAULT = {speech = "CastShadow", colour = {150, 70, 220}, hex = 0x9646DC};

SPELLS["Shadow Bolt"].Cast = function(self, caster, aim)
	EmitVisualParticles("Mist", self.MuzzlePos, aim * 4, 0.6, 6, 0x50207A);
end

SPELLS["Bone Spear"].Cast = function(self, caster, aim)
	EmitVisualParticles("Dust", self.MuzzlePos, aim * 5, 0.5, 8, 0xD0C8A8);
end

-- Hellfire Rain: a sigil on the ground under the aim; the rune controller calls the fire down (HellfireRune.lua).
SPELLS["Hellfire Rain"].Cast = function(self, caster, aim)
	local team = caster and caster.Team or self.Team;
	local point = strikePoint(self.MuzzlePos, aim, 400, caster, team);
	local ground = FX.GroundBelow(point, 200);
	controller("Fell Spell Hellfire Rune", ground, team, caster and caster.UniqueID or 0);
	EmitVisualParticles("Embers", self.MuzzlePos, Vector(0, -6), 1, 12, 0x6EFF6E);
end

-- Shadowflame: each round throws a puff of black smoke and green embers along the cone; the round's own invisible sparks do the
-- burning. A few times a second the ground and whoever stands in the cone is set alight.
SPELLS["Shadowflame"].Cast = function(self, caster, aim)
	for _ = 1, 2 do
		local vel = Vector(aim.X, aim.Y):RadRotate(FX.Rand(-0.22, 0.22)) * FX.Rand(12, 19);
		FX.Spawn("MOSParticle", "Medieval Spell Breath Puff", "Medieval.rte", self.MuzzlePos + aim * 3, vel + self.Vel * 0.5);
	end
	EmitVisualParticles("Smoke", self.MuzzlePos + aim * 12, aim * 12, 0.4, 3, 0x101010);
	EmitVisualParticles("Embers", self.MuzzlePos + aim * 10, aim * 14, 0.35, 4, 0x5ADC5A);
	local now = FX.Now();
	if now - (self.lastIgnite or 0) > 180 then
		self.lastIgnite = now;
		local reach = aim * FX.Rand(55, 105);
		local hit = Vector();
		local at = SceneMan:CastNotMaterialRay(self.MuzzlePos, reach, 0, hit, 2, false) and Vector(hit.X, hit.Y) - aim * 4 or self.MuzzlePos + reach;
		SceneMan:IgniteArea(at, 20);
	end
end

SPELLS["Shadowflame"].Update = function(self)
	if FX.Now() - self.lastFire < 120 then
		local aim = FX.AimOf(self);
		PostProcessMan:AddLight(self.MuzzlePos + aim * 14, 140, 90, 230, 90, 1.2 + math.random() * 0.6);
		PostProcessMan:AddShimmer(self.MuzzlePos + aim * 50, 36, 0.8);
	end
end

-- Doom: the mark on the first enemy under the aim, or on the ground there (DoomMark.lua).
SPELLS["Doom"].Cast = function(self, caster, aim)
	local team = caster and caster.Team or self.Team;
	local point, struck = strikePoint(self.MuzzlePos, aim, 450, caster, team);
	controller("Fell Spell Doom Mark", point, team, caster and caster.UniqueID or 0, {TargetUID = struck and struck.UniqueID or 0});
	EmitVisualParticles("Sparks", self.MuzzlePos, aim * 6, 0.5, 10, 0xE6281E);
	PostProcessMan:AddEnergyBeam(self.MuzzlePos, point, 1, 230, 40, 30, 0.8, 30);
end

-- Chains of the Damned: chains out of the ground in front (Chains.lua).
SPELLS["Chains of the Damned"].Cast = function(self, caster, aim)
	local dirX = aim.X >= 0 and 1 or -1;
	local ground = groundInFront(caster, self, dirX, 16);
	controller("Fell Spell Chains", ground, caster and caster.Team or self.Team, caster and caster.UniqueID or 0, {Dir = dirX});
	EmitVisualParticles("Dust", ground, Vector(dirX * 5, -4), 0.8, 12, 0x5A5A66);
end

function Create(self)
	self.spell = SPELLS[self.PresetName] or DEFAULT;
	self.lastFire = -100000;
end

function Update(self)
	if self.spell.Update then
		self.spell.Update(self);
	end
end

function OnFire(self)
	local spell = self.spell;
	local caster = FX.HolderOf(self);
	local aim = FX.AimOf(self);
	local now = FX.Now();
	local fresh = not spell.channel or now - self.lastFire > 400;
	self.lastFire = now;
	if fresh then
		if caster then
			caster:Say(spell.speech);
		end
		local c = spell.colour;
		PostProcessMan:AddLight(self.MuzzlePos, 80, c[1], c[2], c[3], 1.8);
		EmitVisualParticles("Sparks", self.MuzzlePos, aim * 6, 0.6, 10, spell.hex);
	end
	if spell.Cast then
		spell.Cast(self, caster, aim);
	end
end
