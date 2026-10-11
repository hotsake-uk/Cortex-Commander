-- The Medieval spells (Spells.ini): what happens at the caster's hand each cast, for every spell. The round a spell fires does the rest
-- (a fireball, a sigil, shards...), or for the spells cast on the spot (Call Lightning, Earth Shatter, Tornado, Time Warp, Arcane Nova) the
-- round is an invisible marker that only tells the AI how far the spell reaches, and this script makes the effect.
-- Each cast: the caster says the spell's cast line (the engine's chance and cooldown apply), and the hand flashes in the spell's colour.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

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
			-- A friend in the way: look on past them.
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
	local ground, found = FX.GroundBelow(base + Vector(dirX * ahead, -12), 80);
	return ground, found;
end

local SPELLS = {
	["Fireball"] = {speech = "CastFire", colour = {255, 140, 40}, hex = 0xFF9030},
	["Dragon's Breath"] = {speech = "CastFire", colour = {255, 120, 30}, hex = 0xFF8020, channel = true},
	["Meteor Storm"] = {speech = "CastMeteor", colour = {255, 60, 30}, hex = 0xFF4020},
	["Call Lightning"] = {speech = "CastLightning", colour = {170, 200, 255}, hex = 0xCFE4FF},
	["Arcane Missiles"] = {speech = "CastArcane", colour = {190, 110, 255}, hex = 0xB070FF},
	["Frost Shards"] = {speech = "CastFrost", colour = {160, 210, 255}, hex = 0xD0F0FF},
	["Poison Cloud"] = {speech = "CastPoison", colour = {120, 255, 80}, hex = 0x8CFF5A},
	["Gravity Well"] = {speech = "CastGravity", colour = {150, 70, 255}, hex = 0x9050FF},
	["Earth Shatter"] = {speech = "CastEarth", colour = {200, 150, 90}, hex = 0x9C8A6A},
	["Raise Dead"] = {speech = "CastSummon", colour = {80, 255, 110}, hex = 0x60FF70},
	["Tornado"] = {speech = "CastWind", colour = {190, 230, 220}, hex = 0xC8E6DC},
	["Time Warp"] = {speech = "CastTime", colour = {255, 200, 90}, hex = 0xFFD070},
	["Arcane Nova"] = {speech = "CastArcane", colour = {190, 110, 255}, hex = 0xB070FF},
};
local DEFAULT = {speech = "CastArcane", colour = {200, 200, 255}, hex = 0xFFFFFF};

-- Dragon's Breath: each round throws a puff of fire (render-heavy, harmless) along the cone; the round's own invisible sparks do the
-- burning. A few times a second the ground and whoever stands in the cone is set alight.
SPELLS["Dragon's Breath"].Cast = function(self, caster, aim)
	for _ = 1, 2 do
		local vel = Vector(aim.X, aim.Y):RadRotate(FX.Rand(-0.22, 0.22)) * FX.Rand(12, 19);
		FX.Spawn("MOSParticle", "Medieval Spell Breath Puff", FX.Module, self.MuzzlePos + aim * 3, vel + self.Vel * 0.5);
	end
	EmitVisualParticles("Embers", self.MuzzlePos + aim * 10, aim * 14, 0.35, 3, 0xFF9030);
	local now = FX.Now();
	if now - (self.lastIgnite or 0) > 180 then
		self.lastIgnite = now;
		-- (Not through a wall: on the wall's face instead.)
		local reach = aim * FX.Rand(55, 105);
		local hit = Vector();
		local at = SceneMan:CastNotMaterialRay(self.MuzzlePos, reach, 0, hit, 2, false) and Vector(hit.X, hit.Y) - aim * 4 or self.MuzzlePos + reach;
		SceneMan:IgniteArea(at, 20);
	end
end

SPELLS["Dragon's Breath"].Update = function(self)
	if FX.Now() - self.lastFire < 120 then
		local aim = FX.AimOf(self);
		PostProcessMan:AddLight(self.MuzzlePos + aim * 14, 150, 255, 140, 40, 1.2 + math.random() * 0.6);
		PostProcessMan:AddShimmer(self.MuzzlePos + aim * 50, 36, 0.8);
	end
end

SPELLS["Frost Shards"].Cast = function(self, caster, aim)
	EmitVisualParticles("Mist", self.MuzzlePos, aim * 8, 0.5, 10, 0xD8F2FF);
end

-- Call Lightning: a bolt out of the sky onto whatever is under the aim (LightningStrike.lua does the strike).
SPELLS["Call Lightning"].Cast = function(self, caster, aim)
	local team = caster and caster.Team or self.Team;
	local point, struck = strikePoint(self.MuzzlePos, aim, 450, caster, team);
	FX.Controller("Medieval Spell Lightning Strike", point, team, caster and caster.UniqueID or 0, {StruckUID = struck and struck.UniqueID or 0, Seed = math.random(1, 1000000)});
	EmitVisualParticles("Sparks", self.MuzzlePos, Vector(0, -6), 1, 12, 0xCFE4FF);
end

-- Arcane Nova: a ring of force out from the caster all round, throwing back everything near (and a live grenade: it's in Spells - Push).
SPELLS["Arcane Nova"].Cast = function(self, caster, aim)
	local centre = caster and caster.Pos or self.Pos;
	local team = caster and caster.Team or self.Team;
	SceneMan:MagicPush(centre, Vector(0, -1), 26, 150, 360, caster);
	FX.HurtRadius(centre, 110, 12, team, 0, 80);
	FX.Ring("Sparks", centre, 40, 32, 0xB070FF, 8);
	FX.Ring("Sparks", centre, 24, 20, 0xFFFFFF, 4);
	FX.Ring("Mist", centre, 16, 10, 0xC8A0FF, 10);
	PostProcessMan:AddShockwave(centre, 11000);
	PostProcessMan:AddLight(centre, 200, 190, 110, 255, 2);
	FX.Flash("Violet", centre);
	CameraMan:AddScreenShake(3, centre);
end

-- Earth Shatter: a wave through the ground racing away in front (EarthWave.lua).
SPELLS["Earth Shatter"].Cast = function(self, caster, aim)
	local dirX = aim.X >= 0 and 1 or -1;
	local ground = groundInFront(caster, self, dirX, 10);
	FX.Controller("Medieval Spell Earth Wave", ground, caster and caster.Team or self.Team, caster and caster.UniqueID or 0, {Dir = dirX});
	EmitVisualParticles("Dust", ground, Vector(0, -4), 0.8, 14, 0x9C8A6A);
	CameraMan:AddScreenShake(4, ground);
end

-- Tornado: a whirlwind on the ground in front, travelling on (Tornado.lua).
SPELLS["Tornado"].Cast = function(self, caster, aim)
	local dirX = aim.X >= 0 and 1 or -1;
	local ground = groundInFront(caster, self, dirX, 30);
	FX.Controller("Medieval Spell Tornado", ground, caster and caster.Team or self.Team, caster and caster.UniqueID or 0, {Dir = dirX});
	EmitVisualParticles("Dust", ground, Vector(dirX * 6, -3), 0.7, 16, 0xB8A888);
end

-- Time Warp: a field of slow time round the caster for five seconds (TimeWarpField.lua, which outlives the caster if need be).
SPELLS["Time Warp"].Cast = function(self, caster, aim)
	local centre = caster and caster.Pos or self.Pos;
	FX.Controller("Medieval Spell Time Warp Field", centre, caster and caster.Team or self.Team, caster and caster.UniqueID or 0);
	FX.Ring("Sparks", centre, 30, 14, 0xFFD070, 6);
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
	-- (A channelled spell fires every few frames: its line and its flash come once, when the channelling starts.)
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
