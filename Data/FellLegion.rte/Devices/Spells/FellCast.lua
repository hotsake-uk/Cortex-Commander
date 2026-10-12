-- The Fell Legion spells (Spells.ini): what happens at the caster's hand each cast. The round a spell fires does the rest (a shadow orb, a
-- bone spear, the shadowflame's sparks), or for the spells cast on the spot (Hellfire Rain, Doom, Chains of the Damned) the round is an
-- invisible marker that only tells the AI how far the spell reaches, and this script makes the effect.
-- Each cast: the caster says the spell's cast line (Speech/FellLegion.ini; the engine's chance and cooldown apply), and the hand flashes
-- in the spell's colour. Built as Medieval.rte/Devices/Spells/SpellCast.lua is, on the same SpellFX.lua.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");
local Fell = dofile("FellLegion.rte/Devices/Spells/FellEffects.lua");
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

-- The friend a support spell is for: the first friendly unit the aim strikes within reach (looking past enemies and loose things in the way;
-- a wall stops it), else the friend nearest the aim's line within reach and a few paces of it, else the caster. Returns the unit and
-- whether it is the caster. (Medieval.rte's SpellCast.lua, the same.)
local function friendUnderAim(self, caster, aim, reach)
	local team = caster and caster.Team or self.Team;
	local from = Vector(self.MuzzlePos.X, self.MuzzlePos.Y);
	local left = reach;
	local ignore = caster and caster.ID or FX.NoMOID;
	for _ = 1, 4 do
		local id = SceneMan:CastMORay(from, aim * left, ignore, -1, 0, false, 1);
		if id == FX.NoMOID then
			break;
		end
		local mo = MovableMan:GetMOFromID(id);
		local root = mo and (MovableMan:GetMOFromID(mo.RootID) or mo);
		local hit = SceneMan:GetLastRayHitPos();
		if root and IsActor(root) and root.Team == team and root.UniqueID ~= (caster and caster.UniqueID or -1) and FX.Alive(ToActor(root)) then
			return ToActor(root), false;
		end
		local gone = SceneMan:ShortestDistance(from, Vector(hit.X, hit.Y), true).Magnitude + 12;
		left = left - gone;
		if left <= 0 or not root then
			break;
		end
		from = from + aim * gone;
		ignore = root.ID;
	end
	local best, bestOff;
	for _, near in ipairs(FX.ActorsNear(self.MuzzlePos, reach)) do
		local a = near.actor;
		if a.Team == team and a.UniqueID ~= (caster and caster.UniqueID or -1) and FX.Alive(a) then
			local along = near.offset.X * aim.X + near.offset.Y * aim.Y;
			if along > 0 then
				local off = (near.offset - aim * along).Magnitude;
				if off < 40 and (not bestOff or off < bestOff) then
					best, bestOff = a, off;
				end
			end
		end
	end
	if best then
		return best, false;
	end
	return caster, true;
end

-- The dark glitter of a support spell landing on a unit: a ring of sparks and a flare of light.
local function darkBless(target, hex, r, g, b, count)
	if not target then
		return;
	end
	FX.Ring("Sparks", target.Pos, count or 14, 5, hex, 10);
	EmitVisualParticles("Mist", target.Pos + Vector(0, -8), Vector(0, -2), 0.8, 6, 0x201030);
	PostProcessMan:AddLight(target.Pos, 120, r, g, b, 1.6);
end

-- The friend a support spell helps, from the spell's custom values: SupportRange, and the engine-chosen caster.
local function supportTarget(self, caster, aim)
	local reach = self:NumberValueExists("SupportRange") and self:GetNumberValue("SupportRange") or 250;
	return friendUnderAim(self, caster, aim, reach);
end

-- Puts a Fell status effect (FellEffects.lua) on the friend under the aim, with its glitter, and has them say their line. A Buff's whole cast.
local function castBuff(self, caster, aim, effectName, hex, r, g, b, say)
	local target, isSelf = supportTarget(self, caster, aim);
	if not target then
		return nil;
	end
	Fell.Apply(target, effectName);
	darkBless(target, hex, r, g, b);
	if say then
		target:Say(say);
	end
	return target, isSelf;
end

local SPELLS = {
	["Shadow Bolt"] = {speech = "CastShadow", colour = {150, 70, 220}, hex = 0x9646DC},
	["Bone Spear"] = {speech = "CastBone", colour = {220, 214, 190}, hex = 0xDCD6BE},
	["Hellfire Rain"] = {speech = "CastHellfire", colour = {110, 255, 110}, hex = 0x6EFF6E},
	["Shadowflame"] = {speech = "CastHellfire", colour = {90, 220, 90}, hex = 0x5ADC5A, channel = true},
	["Doom"] = {speech = "CastDoom", colour = {230, 40, 30}, hex = 0xE6281E},
	["Chains of the Damned"] = {speech = "CastChains", colour = {120, 120, 140}, hex = 0x78788C},
	-- The second book of the dark (Fell Legion II).
	["Hex Bolt"] = {speech = "CastHex", colour = {190, 90, 255}, hex = 0xBE5AFF},
	["Curse of Weakness"] = {speech = "CastCurse", colour = {120, 30, 160}, hex = 0x781EA0},
	["Fear"] = {speech = "CastFear", colour = {90, 40, 120}, hex = 0x5A2878},
	["Life Drain"] = {speech = "CastDrain", colour = {120, 255, 90}, hex = 0x78FF5A, channel = true},
	["Soul Harvest"] = {speech = "CastHarvest", colour = {90, 255, 110}, hex = 0x5AFF6E},
	["Blood Pact"] = {speech = "CastPact", colour = {200, 20, 30}, hex = 0xC8141E},
	["Unholy Frenzy"] = {speech = "CastFrenzy", colour = {255, 60, 40}, hex = 0xFF3C28},
	["Dark Shield"] = {speech = "CastDarkShield", colour = {90, 60, 140}, hex = 0x5A3C8C},
	["Shadow Step"] = {speech = "CastStep", colour = {60, 30, 90}, hex = 0x3C1E5A},
	["Shroud"] = {speech = "CastShroud", colour = {50, 40, 70}, hex = 0x322846},
	["Darkness"] = {speech = "CastDarkness", colour = {40, 40, 120}, hex = 0x282878},
	["Raise Ghoul"] = {speech = "CastGhoul", colour = {110, 255, 110}, hex = 0x6EFF6E},
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

-- Hex Bolt: the orb does it all (HexBolt.lua); a wisp of violet at the hand.
SPELLS["Hex Bolt"].Cast = function(self, caster, aim)
	EmitVisualParticles("Mist", self.MuzzlePos, aim * 4, 0.6, 6, 0x6A30A0);
end

-- Curse of Weakness: the sigil where the aim strikes (CurseField.lua), a thread of dark from the hand to it.
SPELLS["Curse of Weakness"].Cast = function(self, caster, aim)
	local team = caster and caster.Team or self.Team;
	local point = strikePoint(self.MuzzlePos, aim, 400, caster, team);
	controller("Fell Spell Curse Field", point, team, caster and caster.UniqueID or 0);
	PostProcessMan:AddEnergyBeam(self.MuzzlePos, point, 2, 120, 30, 160, 0.9, 25);
	EmitVisualParticles("Mist", self.MuzzlePos, aim * 3, 0.6, 6, 0x40186A);
end

-- Fear: the pulse out of the caster (FearPulse.lua).
SPELLS["Fear"].Cast = function(self, caster, aim)
	local at = caster and caster.Pos or self.Pos;
	controller("Fell Spell Fear Pulse", at, caster and caster.Team or self.Team, caster and caster.UniqueID or 0);
	EmitVisualParticles("Mist", at, Vector(0, -2), 1, 12, 0x301848);
end

-- Life Drain: held, a tether on the enemy under the aim within ten paces (each round, ten a second): their life a little at a time, and all of
-- it into the caster. The tether is drawn while the spell fires (Update).
SPELLS["Life Drain"].Cast = function(self, caster, aim)
	local team = caster and caster.Team or self.Team;
	local point, struck = strikePoint(self.MuzzlePos, aim, 200, caster, team);
	self.drainAt = point;
	self.drainUID = struck and struck.UniqueID or 0;
	if struck and FX.Alive(struck) then
		FX.Hurt(struck, 0.8, 0);
		if caster and FX.Alive(caster) then
			caster.Health = math.min(caster.Health + 0.8, caster.MaxHealth);
		end
		FX.Say(struck, "Drained");
		if math.random() < 0.3 then
			struck:FlashWhite(40);
		end
		EmitVisualParticles("Sparks", point, Vector(0, -2), 0.6, 1, 0x78FF5A);
	else
		self.drainUID = 0;
		EmitVisualParticles("Mist", point, Vector(0, -1), 0.4, 1, 0x206030);
	end
end

SPELLS["Life Drain"].Update = function(self)
	if FX.Now() - self.lastFire < 150 and self.drainAt then
		local target = FX.ActorByUID(self.drainUID or 0);
		local to = target and FX.Alive(target) and target.Pos + Vector(0, -4) or self.drainAt;
		local pulse = 0.8 + 0.4 * math.sin(FX.Now() * 0.03);
		PostProcessMan:AddEnergyBeam(to, self.MuzzlePos, 2, 120, 255, 90, pulse, 30);
		PostProcessMan:AddEnergyBeam(to, self.MuzzlePos, 1, 150, 80, 230, 0.6, 30);
		PostProcessMan:AddLight(self.MuzzlePos, 60, 120, 255, 90, 1.0);
		if target then
			-- Beads of stolen life running up the tether.
			local f = (FX.Now() % 300) / 300;
			local offset = SceneMan:ShortestDistance(to, self.MuzzlePos, true);
			EmitVisualParticles("Sparks", to + offset * f, Vector(0, 0), 0.3, 1, 0x78FF5A);
		end
	end
end

-- Soul Harvest: the caster is Harvesting (FellEffects.lua), and the harvester keeps with him (SoulHarvest.lua).
SPELLS["Soul Harvest"].Cast = function(self, caster, aim)
	if not caster or not FX.Alive(caster) then
		return;
	end
	Fell.Apply(caster, "Harvesting");
	controller("Fell Spell Soul Harvest", caster.Pos, caster.Team, caster.UniqueID);
	darkBless(caster, 0x5AFF6E, 90, 255, 110, 18);
	FX.Spawn("MOPixel", "Fell Spell Flash Hellfire", MODULE, caster.Pos + Vector(0, -10), Vector());
end

-- Blood Pact: twenty-five of the caster's health for sixty energy, if they have more than thirty to give.
SPELLS["Blood Pact"].Cast = function(self, caster, aim)
	if not caster or not FX.Alive(caster) then
		return;
	end
	if caster.Health <= 30 then
		-- Too near death to open a vein: a flicker and nothing.
		EmitVisualParticles("Mist", self.MuzzlePos, Vector(0, -1), 0.5, 4, 0x400810);
		return;
	end
	caster.Health = caster.Health - 25;
	caster.Energy = math.min(caster.Energy + 60, caster.MaxEnergy);
	caster:FlashWhite(100);
	local sound = CreateSoundContainer("Fell Blood Pact", MODULE);
	if sound then
		sound:Play(caster.Pos);
	end
	-- Blood out of the hand, and the power of it back into the body as crimson light.
	for _ = 1, 6 do
		FX.Spawn("MOPixel", "Drop Blood", "Base.rte", self.MuzzlePos, Vector(FX.Rand(-3, 3), FX.Rand(-6, -2)));
	end
	EmitVisualParticles("Sparks", self.MuzzlePos, Vector(0, -4), 1, 10, 0xC8141E);
	FX.Ring("Sparks", caster.Pos, 16, 4, 0xC8141E, 20);
	PostProcessMan:AddEnergyBeam(self.MuzzlePos, caster.Pos + Vector(0, -6), 2, 200, 20, 30, 1.2, 30);
	PostProcessMan:AddLight(caster.Pos, 100, 200, 20, 30, 1.8);
end

-- Unholy Frenzy: the friend under the aim (or the caster) is Frenzied (FellEffects.lua).
SPELLS["Unholy Frenzy"].Cast = function(self, caster, aim)
	local target = castBuff(self, caster, aim, "Frenzied", 0xFF3C28, 255, 60, 40, "Frenzied");
	if target then
		EmitVisualParticles("Embers", target.Pos + Vector(0, -10), Vector(0, -4), 1, 12, 0xFF3C28);
		FX.Spawn("MOPixel", "Fell Spell Flash Doom", MODULE, target.Pos + Vector(0, -10), Vector());
	end
end

-- Dark Shield: a shield powerup's bubble, six seconds, on the friend or the caster, in dark violet.
SPELLS["Dark Shield"].Cast = function(self, caster, aim)
	local target, isSelf = supportTarget(self, caster, aim);
	if not target or not FX.Alive(target) then
		return;
	end
	target:AddPowerupSeconds("Shield", 6);
	darkBless(target, 0x5A3C8C, 90, 60, 140, 16);
	FX.Spawn("MOPixel", "Fell Spell Flash Shadow", MODULE, target.Pos + Vector(0, -8), Vector());
	if not isSelf then
		target:Say("Blessed");
	end
end

-- Shadow Step: the caster is at once as far along the aim as there is room, in a burst of black smoke (Medieval.rte's Blink, in the dark).
SPELLS["Shadow Step"].Cast = function(self, caster, aim)
	if not caster or not FX.Alive(caster) then
		return;
	end
	local reach = self:NumberValueExists("SupportRange") and self:GetNumberValue("SupportRange") or 160;
	local from = Vector(caster.Pos.X, caster.Pos.Y);
	local goal = from + aim * reach;
	local dest;
	for step = 0, 6 do
		local at = goal - aim * (step * 16);
		if SceneMan:ShortestDistance(from, at, true):MagnitudeIsLessThan(24) then
			break;
		end
		local clear = true;
		for _, off in ipairs({Vector(0, -14), Vector(0, 0), Vector(0, 12), Vector(-6, -6), Vector(6, -6)}) do
			local probe = at + off;
			if SceneMan:GetTerrMatter(math.floor(probe.X), math.floor(probe.Y)) ~= 0 then
				clear = false;
				break;
			end
		end
		if clear then
			dest = at;
			break;
		end
	end
	if not dest then
		EmitVisualParticles("Smoke", from, Vector(0, -1), 0.6, 6, 0x101010);
		return;
	end
	EmitVisualParticles("Smoke", from, Vector(0, -2), 1, 14, 0x0A0A12);
	EmitVisualParticles("Mist", from, Vector(0, -1), 0.8, 8, 0x201030);
	PostProcessMan:AddLight(from, 80, 90, 50, 140, 1.2);
	caster.Pos = dest;
	caster.Vel = caster.Vel * 0.3;
	EmitVisualParticles("Smoke", dest, Vector(0, -2), 1, 14, 0x0A0A12);
	EmitVisualParticles("Mist", dest, Vector(0, -1), 0.8, 8, 0x201030);
	FX.Ring("Sparks", dest, 12, 5, 0x3C1E5A, 6);
	PostProcessMan:AddLight(dest, 80, 90, 50, 140, 1.2);
	FX.Spawn("MOPixel", "Fell Spell Flash Shadow", MODULE, dest, Vector());
end

-- Shroud: the caster is cloaked (the cloak powerup) for six seconds, and Shrouded for the AI's sake (FellEffects.lua).
SPELLS["Shroud"].Cast = function(self, caster, aim)
	if not caster or not FX.Alive(caster) then
		return;
	end
	caster:AddPowerupSeconds("Cloak", 6);
	Fell.Apply(caster, "Shrouded");
	EmitVisualParticles("Smoke", caster.Pos, Vector(0, -1.5), 1, 16, 0x0A0A12);
	EmitVisualParticles("Mist", caster.Pos + Vector(0, -8), Vector(0, -1), 1, 10, 0x201030);
	PostProcessMan:AddLight(caster.Pos, 70, 50, 40, 70, 1.0);
end

-- Darkness: night on the whole field (Darkness.lua), and the caster under Nightfall for the AI's sake.
SPELLS["Darkness"].Cast = function(self, caster, aim)
	local at = caster and caster.Pos or self.Pos;
	controller("Fell Spell Darkness", at, caster and caster.Team or self.Team, caster and caster.UniqueID or 0);
	if caster then
		Fell.Apply(caster, "Nightfall");
	end
	EmitVisualParticles("Smoke", self.MuzzlePos, Vector(0, -3), 1, 10, 0x080410);
end

-- Raise Ghoul: a grave on the ground under the aim (RaiseGhoul.lua).
SPELLS["Raise Ghoul"].Cast = function(self, caster, aim)
	local team = caster and caster.Team or self.Team;
	local point = strikePoint(self.MuzzlePos, aim, 250, caster, team);
	local ground = FX.GroundBelow(point, 200);
	controller("Fell Spell Ghoul Grave", ground, team, caster and caster.UniqueID or 0);
	EmitVisualParticles("Embers", self.MuzzlePos, Vector(0, -5), 1, 10, 0x6EFF6E);
	PostProcessMan:AddEnergyBeam(self.MuzzlePos, ground, 1, 110, 255, 110, 0.7, 25);
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
