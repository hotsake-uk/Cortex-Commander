-- The Medieval spells (Spells.ini): what happens at the caster's hand each cast, for every spell. The round a spell fires does the rest
-- (a fireball, a sigil, shards...), or for the spells cast on the spot (Call Lightning, Earth Shatter, Tornado, Time Warp, Arcane Nova) the
-- round is an invisible marker that only tells the AI how far the spell reaches, and this script makes the effect.
-- Each cast: the caster says the spell's cast line (the engine's chance and cooldown apply), and the hand flashes in the spell's colour.
-- The support spells (Heal, Blessing, Haste... Spells.ini's second half) are cast on the friend under the aim within reach, or on the caster
-- when there is none; their effects are SpellEffects.lua's status effects, and what they leave behind (a light, a levitation, the mirror
-- images, a grab) is a controller with a script of its own.
-- The second batch (Spells2.ini: Chain Lightning, Ice Lance, Blizzard, Flame Wall, Holy Smite, Sunbeam, Stone Spikes, Thunderclap,
-- Petrify, Water Jet, Wall of Stone, Bridge of Light, Summon Wolf) follows the same shapes: a cast on the spot, a controller, or a held beam.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");
local Effects = dofile("Medieval.rte/Devices/Spells/SpellEffects.lua");

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

-- The friend a support spell is cast on: the first unit of the caster's team a bolt from the hand along the aim strikes within reach (looking
-- past enemies and loose things in the way; a wall stops it), else the friend nearest the aim's line within reach and a few paces of it,
-- else the caster. Returns the unit and whether it is the caster.
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
	-- Near the line of the aim: the friend closest to it, within reach.
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

-- The glitter and light of a support spell landing on a unit.
local function bless(target, hex, r, g, b, count)
	if not target then
		return;
	end
	FX.Ring("Sparks", target.Pos, count or 14, 5, hex, 10);
	EmitVisualParticles("Sparks", target.Pos + Vector(0, -8), Vector(0, -3), 0.8, 8, hex);
	PostProcessMan:AddLight(target.Pos, 120, r, g, b, 1.6);
end

-- The friend a support spell helps, from the spell's custom values: SupportRange, and the engine-chosen caster.
local function supportTarget(self, caster, aim)
	local reach = self:NumberValueExists("SupportRange") and self:GetNumberValue("SupportRange") or 250;
	return friendUnderAim(self, caster, aim, reach);
end

-- Puts a status effect (SpellEffects.lua) on the friend under the aim, with its glitter, and has them say thanks. A Buff spell's whole cast.
local function castBuff(self, caster, aim, effectName, hex, r, g, b, say)
	local target, isSelf = supportTarget(self, caster, aim);
	if not target then
		return nil;
	end
	Effects.Apply(target, effectName);
	bless(target, hex, r, g, b);
	if not isSelf and say then
		target:Say(say);
	end
	return target, isSelf;
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
	-- The support spells.
	["Heal"] = {speech = "CastHoly", colour = {255, 230, 140}, hex = 0xFFE68C},
	["Mass Heal"] = {speech = "CastHoly", colour = {255, 230, 140}, hex = 0xFFE68C},
	["Blessing"] = {speech = "CastHoly", colour = {255, 220, 120}, hex = 0xFFDC78},
	["Shield of Faith"] = {speech = "CastHoly", colour = {150, 200, 255}, hex = 0x96C8FF},
	["Haste"] = {speech = "CastBuff", colour = {120, 240, 255}, hex = 0x78F0FF},
	["Stoneskin"] = {speech = "CastBuff", colour = {160, 160, 150}, hex = 0xA0A096},
	["Fire Ward"] = {speech = "CastBuff", colour = {255, 150, 60}, hex = 0xFF963C},
	["Regeneration"] = {speech = "CastBuff", colour = {120, 255, 140}, hex = 0x78FF8C},
	["Enchant Weapon"] = {speech = "CastBuff", colour = {200, 140, 255}, hex = 0xC88CFF},
	["Levitate"] = {speech = "CastBuff", colour = {220, 230, 255}, hex = 0xDCE6FF},
	["Mirror Image"] = {speech = "CastBlink", colour = {190, 150, 255}, hex = 0xBE96FF},
	["Blink"] = {speech = "CastBlink", colour = {190, 150, 255}, hex = 0xBE96FF},
	["Dispel"] = {speech = "CastHoly", colour = {255, 255, 255}, hex = 0xFFFFFF},
	["Light"] = {speech = "CastBuff", colour = {255, 255, 230}, hex = 0xFFFFE6},
	["Telekinetic Grab"] = {speech = "CastArcane", colour = {180, 120, 255}, hex = 0xB478FF},
	-- The second batch (Spells2.ini).
	["Chain Lightning"] = {speech = "CastLightning", colour = {170, 200, 255}, hex = 0xCFE4FF},
	["Ice Lance"] = {speech = "CastFrost", colour = {160, 210, 255}, hex = 0xD0F0FF},
	["Blizzard"] = {speech = "CastFrost", colour = {150, 190, 255}, hex = 0xC0DCFF},
	["Flame Wall"] = {speech = "CastFire", colour = {255, 130, 30}, hex = 0xFF8A20},
	["Holy Smite"] = {speech = "CastHoly", colour = {255, 235, 160}, hex = 0xFFEBA0},
	["Sunbeam"] = {speech = "CastHoly", colour = {255, 230, 140}, hex = 0xFFE68C, channel = true},
	["Stone Spikes"] = {speech = "CastStone", colour = {170, 160, 140}, hex = 0xA8A090},
	["Thunderclap"] = {speech = "CastLightning", colour = {220, 230, 255}, hex = 0xDCE6FF},
	["Petrify"] = {speech = "CastStone", colour = {150, 160, 140}, hex = 0x96A08C},
	["Water Jet"] = {speech = "CastWater", colour = {120, 190, 255}, hex = 0x78BEFF, channel = true},
	["Wall of Stone"] = {speech = "CastStone", colour = {160, 160, 160}, hex = 0xA0A0A0},
	["Bridge of Light"] = {speech = "CastArcane", colour = {255, 240, 190}, hex = 0xFFF0BE},
	["Summon Wolf"] = {speech = "CastSummon", colour = {150, 220, 180}, hex = 0x96DCB4},
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

---------------------------------------------------------------------------------------------------------------------------------------
-- The support spells.

-- Heal: forty health and the three worst wounds, on the friend under the aim or the caster.
SPELLS["Heal"].Cast = function(self, caster, aim)
	local target, isSelf = supportTarget(self, caster, aim);
	if not target or not FX.Alive(target) then
		return;
	end
	target.Health = math.min(target.Health + 40, target.MaxHealth);
	target:RemoveWounds(3);
	bless(target, 0xFFE68C, 255, 230, 140, 18);
	FX.Flash("Gold", target.Pos);
	if not isSelf then
		target:Say("Healed");
	end
end

-- Mass Heal: twenty-five health and a wound on every friend within reach of the caster, and on the caster.
SPELLS["Mass Heal"].Cast = function(self, caster, aim)
	local centre = caster and caster.Pos or self.Pos;
	local team = caster and caster.Team or self.Team;
	local reach = self:NumberValueExists("SupportRange") and self:GetNumberValue("SupportRange") or 120;
	local healed = 0;
	for _, near in ipairs(FX.ActorsNear(centre, reach)) do
		local a = near.actor;
		if a.Team == team and FX.Alive(a) then
			a.Health = math.min(a.Health + 25, a.MaxHealth);
			a:RemoveWounds(1);
			bless(a, 0xFFE68C, 255, 230, 140, 10);
			if caster == nil or a.UniqueID ~= caster.UniqueID then
				a:Say("Healed");
			end
			healed = healed + 1;
		end
	end
	FX.Ring("Sparks", centre, 36, 30, 0xFFE68C, 12);
	FX.Ring("Mist", centre, 16, 10, 0xFFF4C8, 8);
	FX.Flash("Gold", centre);
	PostProcessMan:AddLight(centre, 2 * reach, 255, 230, 140, 1.8);
	PostProcessMan:AddShockwave(centre, 6000);
end

SPELLS["Blessing"].Cast = function(self, caster, aim)
	local target = castBuff(self, caster, aim, "Blessed", 0xFFDC78, 255, 220, 120, "Blessed");
	if target then
		FX.Flash("Gold", target.Pos);
		EmitVisualParticles("Sparks", target.Pos + Vector(0, -30), Vector(0, 3), 0.6, 12, 0xFFF0B0);
	end
end

-- Shield of Faith: a shield powerup's bubble, six seconds, on the friend or the caster.
SPELLS["Shield of Faith"].Cast = function(self, caster, aim)
	local target, isSelf = supportTarget(self, caster, aim);
	if not target or not FX.Alive(target) then
		return;
	end
	target:AddPowerupSeconds("Shield", 6);
	bless(target, 0x96C8FF, 150, 200, 255, 16);
	FX.Flash("White", target.Pos);
	if not isSelf then
		target:Say("Blessed");
	end
end

SPELLS["Haste"].Cast = function(self, caster, aim)
	local target = castBuff(self, caster, aim, "Hasted", 0x78F0FF, 120, 240, 255, "Hasted");
	if target then
		EmitVisualParticles("Sparks", target.Pos, Vector(target.HFlipped and -12 or 12, 0), 0.3, 12, 0xC0F8FF);
	end
end

SPELLS["Stoneskin"].Cast = function(self, caster, aim)
	local target = castBuff(self, caster, aim, "Stoneskin", 0xA0A096, 160, 160, 150, "Blessed");
	if target then
		EmitVisualParticles("Dust", target.Pos, Vector(0, -2), 0.8, 16, 0x9C9890);
		CameraMan:AddScreenShake(1, target.Pos);
	end
end

-- Fire Ward: put out, and warded for fifteen seconds.
SPELLS["Fire Ward"].Cast = function(self, caster, aim)
	local target = castBuff(self, caster, aim, "Fire Ward", 0xFF963C, 255, 150, 60, "Blessed");
	if target then
		SceneMan:Extinguish(target);
		FX.Ring("Embers", target.Pos, 14, 4, 0xFF9030, 12);
		FX.Flash("Fire", target.Pos);
	end
end

SPELLS["Regeneration"].Cast = function(self, caster, aim)
	castBuff(self, caster, aim, "Regenerating", 0x78FF8C, 120, 255, 140, "Healed");
end

-- Enchant Weapon: half again the damage from everything the friend hits with, the weapon crackling.
SPELLS["Enchant Weapon"].Cast = function(self, caster, aim)
	local target = castBuff(self, caster, aim, "Enchanted Weapon", 0xC88CFF, 200, 140, 255, "Blessed");
	if target and IsAHuman(target) then
		local held = ToAHuman(target).EquippedItem;
		if held then
			EmitVisualParticles("Sparks", held.Pos, Vector(0, -2), 1, 14, 0xE0B0FF);
			FX.Flash("Violet", held.Pos);
		end
	end
end

-- Levitate: a controller holds the friend's weight off for five seconds (Levitate.lua).
SPELLS["Levitate"].Cast = function(self, caster, aim)
	local target, isSelf = supportTarget(self, caster, aim);
	if not target or not FX.Alive(target) then
		return;
	end
	FX.Controller("Medieval Spell Levitation", target.Pos, target.Team, caster and caster.UniqueID or 0, {TargetUID = target.UniqueID});
	bless(target, 0xDCE6FF, 220, 230, 255, 12);
	EmitVisualParticles("Mist", target.Pos + Vector(0, 10), Vector(0, 1), 0.6, 8, 0xE8F0FF);
	if not isSelf then
		target:Say("Lifted");
	end
end

-- Mirror Image: two copies of the caster step out to either side (MirrorImage.lua keeps them and pops them).
SPELLS["Mirror Image"].Cast = function(self, caster, aim)
	if not caster or not FX.Alive(caster) or not IsAHuman(caster) then
		return;
	end
	for _, side in ipairs({-1, 1}) do
		local image = ToAHuman(ToAHuman(caster):Clone());
		if image then
			image.Pos = caster.Pos + Vector(side * 14, -2);
			image.Vel = Vector(side * 4, -2);
			image.Team = caster.Team;
			image.Health = image.MaxHealth;
			image.AIMode = Actor.AIMODE_SENTRY;
			image:SetNumberValue("MedievalIllusion", 1);
			image:SetNumberValue("MedievalIllusionOf", caster.UniqueID);
			-- (Its own spells and tricks are the real caster's business.)
			image.MaxEnergy = 0;
			image.Energy = 0;
			Effects.Apply(image, "Illusion", 0);
			MovableMan:AddActor(image);
			FX.Controller("Medieval Spell Mirror Image", image.Pos, caster.Team, caster.UniqueID, {TargetUID = image.UniqueID, Seconds = 10});
			FX.Flash("Violet", image.Pos);
			EmitVisualParticles("Sparks", image.Pos, Vector(side * 3, -2), 0.8, 10, 0xBE96FF);
		end
	end
	PostProcessMan:AddLight(caster.Pos, 120, 190, 150, 255, 1.5);
end

-- Blink: the caster is at once as far along the aim as there is room, a wall or a gap or a line crossed.
SPELLS["Blink"].Cast = function(self, caster, aim)
	if not caster or not FX.Alive(caster) then
		return;
	end
	local reach = self:NumberValueExists("SupportRange") and self:GetNumberValue("SupportRange") or 160;
	local from = Vector(caster.Pos.X, caster.Pos.Y);
	local goal = from + aim * reach;
	-- The furthest point along the aim with room for a body: stepped back from the far end until the spot is clear.
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
		-- No room anywhere there: the spell fizzles, a flash and nothing.
		FX.Flash("Violet", from);
		return;
	end
	FX.Ring("Sparks", from, 16, 6, 0xBE96FF, 6);
	EmitVisualParticles("Mist", from, Vector(0, -1), 0.6, 8, 0xD0C0FF);
	PostProcessMan:AddLight(from, 100, 190, 150, 255, 1.6);
	caster.Pos = dest;
	caster.Vel = caster.Vel * 0.3;
	-- (Whatever the caster carries comes with them; the engine moves the attached parts with the body.)
	FX.Ring("Sparks", dest, 16, 6, 0xBE96FF, 6);
	EmitVisualParticles("Mist", dest, Vector(0, -1), 0.6, 8, 0xD0C0FF);
	PostProcessMan:AddLight(dest, 100, 190, 150, 255, 1.6);
	FX.Flash("Violet", dest);
end

-- Dispel: every curse off the friend under the aim (or the caster), and every blessing off the enemies near them.
SPELLS["Dispel"].Cast = function(self, caster, aim)
	local target = supportTarget(self, caster, aim);
	if not target or not FX.Alive(target) then
		return;
	end
	target:RemoveStatusEffects(true, false);
	-- The older afflictions too: Frost Shards' chill ends on its next look, and venom and snares are cured.
	if target:NumberValueExists("MedievalChillUntil") then
		target:SetNumberValue("MedievalChillUntil", 0);
	end
	if target.IsEnvenomed then
		target:Envenom(0, 0, 1);
	end
	if target.IsEnsnared then
		target:Ensnare(0);
	end
	bless(target, 0xFFFFFF, 255, 255, 255, 20);
	FX.Flash("White", target.Pos);
	local team = target.Team;
	for _, near in ipairs(FX.ActorsNear(target.Pos, 60, team)) do
		if FX.Alive(near.actor) then
			near.actor:RemoveStatusEffects(false, true);
			-- (A shield powerup too: a Shield of Faith on an enemy stripped.)
			near.actor:AddPowerupSeconds("Shield", -1000);
			EmitVisualParticles("Sparks", near.actor.Pos, Vector(0, 2), 0.8, 10, 0xFFFFFF);
		end
	end
	PostProcessMan:AddShockwave(target.Pos, 3000);
end

-- Light: a ball of light over the caster's head for thirty seconds (LightOrb.lua).
SPELLS["Light"].Cast = function(self, caster, aim)
	local at = caster and caster.Pos + Vector(0, -40) or self.Pos + Vector(0, -30);
	FX.Controller("Medieval Spell Light Orb", at, caster and caster.Team or self.Team, caster and caster.UniqueID or 0, {Seconds = 30});
	EmitVisualParticles("Sparks", at, Vector(0, -1), 0.5, 10, 0xFFFFE6);
	PostProcessMan:AddLight(at, 200, 255, 255, 230, 2);
end

-- Telekinetic Grab: the enemy under the aim is held and hauled to the caster's feet (TelekineticGrab.lua).
SPELLS["Telekinetic Grab"].Cast = function(self, caster, aim)
	local team = caster and caster.Team or self.Team;
	local point, struck = strikePoint(self.MuzzlePos, aim, 300, caster, team);
	if struck and FX.Alive(struck) then
		Effects.Apply(struck, "Grabbed", 1.6);
		FX.Controller("Medieval Spell Telekinetic Grab", struck.Pos, team, caster and caster.UniqueID or 0, {TargetUID = struck.UniqueID});
		struck:Say("Lifted");
		FX.Flash("Purple", struck.Pos);
		EmitVisualParticles("Sparks", struck.Pos, Vector(0, -3), 1, 14, 0xB478FF);
	else
		-- Nothing there to seize: the hand closes on air.
		EmitVisualParticles("Sparks", point, Vector(0, -1), 0.5, 6, 0xB478FF);
	end
	PostProcessMan:AddLight(self.MuzzlePos, 90, 180, 120, 255, 1.5);
end

---------------------------------------------------------------------------------------------------------------------------------------
-- The second batch of attack and control spells (Spells2.ini).

-- Where a held beam ends: the first enemy along the aim within reach (looking past friends), else the wall or ground it strikes, else
-- the end of its reach. Returns the point and the unit struck, if one was.
local function beamEnd(self, caster, aim, reach)
	local team = caster and caster.Team or self.Team;
	local start = Vector(self.MuzzlePos.X, self.MuzzlePos.Y);
	local point, struck = strikePoint(start, aim, reach, caster, team);
	if SceneMan:ShortestDistance(start, point, true).Magnitude > reach + 2 then
		point = start + aim * reach;
	end
	return point, struck, team;
end

-- Chain Lightning: a bolt into the enemy under the aim that leaps on to those near them (ChainLightning.lua).
SPELLS["Chain Lightning"].Cast = function(self, caster, aim)
	local team = caster and caster.Team or self.Team;
	local point, struck = strikePoint(self.MuzzlePos, aim, 300, caster, team);
	FX.Controller("Medieval Spell Chain Lightning", point, team, caster and caster.UniqueID or 0,
		{StruckUID = struck and struck.UniqueID or 0, HandX = self.MuzzlePos.X, HandY = self.MuzzlePos.Y});
	EmitVisualParticles("Sparks", self.MuzzlePos, aim * 8, 0.8, 14, 0xCFE4FF);
	PostProcessMan:AddLight(self.MuzzlePos, 120, 190, 215, 255, 2);
end

SPELLS["Ice Lance"].Cast = function(self, caster, aim)
	EmitVisualParticles("Mist", self.MuzzlePos, aim * 6, 0.6, 8, 0xD8F2FF);
	FX.Flash("Ice", self.MuzzlePos);
end

-- Blizzard: a storm over whatever is under the aim (Blizzard.lua).
SPELLS["Blizzard"].Cast = function(self, caster, aim)
	local team = caster and caster.Team or self.Team;
	local point = strikePoint(self.MuzzlePos, aim, 350, caster, team);
	-- (Over the ground there, not the unit: the storm sits on the ground below.)
	local ground, found = FX.GroundBelow(point + Vector(0, -20), 200);
	FX.Controller("Medieval Spell Blizzard Cloud", found and ground or point, team, caster and caster.UniqueID or 0);
	EmitVisualParticles("Mist", self.MuzzlePos, Vector(0, -4), 0.8, 10, 0xD8E8FF);
	FX.Ring("Sparks", self.MuzzlePos, 12, 5, 0xC0DCFF, 4);
end

-- Flame Wall: a wall of fire on the ground in front (FlameWall.lua).
SPELLS["Flame Wall"].Cast = function(self, caster, aim)
	local dirX = aim.X >= 0 and 1 or -1;
	local ground = groundInFront(caster, self, dirX, 50);
	FX.Controller("Medieval Spell Flame Wall", ground, caster and caster.Team or self.Team, caster and caster.UniqueID or 0, {Dir = dirX});
	EmitVisualParticles("Embers", self.MuzzlePos, aim * 6, 0.6, 12, 0xFF8A20);
	FX.Flash("Fire", self.MuzzlePos);
end

-- Holy Smite: a pillar of light out of the sky on what is under the aim (HolySmite.lua).
SPELLS["Holy Smite"].Cast = function(self, caster, aim)
	local team = caster and caster.Team or self.Team;
	local point, struck = strikePoint(self.MuzzlePos, aim, 400, caster, team);
	local ground, found = FX.GroundBelow(point + Vector(0, -10), 120);
	FX.Controller("Medieval Spell Holy Pillar", found and ground or point, team, caster and caster.UniqueID or 0, {StruckUID = struck and struck.UniqueID or 0});
	EmitVisualParticles("Sparks", self.MuzzlePos, Vector(0, -6), 0.8, 14, 0xFFEBA0);
	FX.Flash("Gold", self.MuzzlePos);
end

-- Sunbeam: every pulse of the held beam burns what it touches and lights the ground; the beam itself is drawn by Update while it pours.
SPELLS["Sunbeam"].Cast = function(self, caster, aim)
	local point, struck, team = beamEnd(self, caster, aim, 350);
	self.beamEnd = point;
	if struck and FX.Alive(struck) then
		FX.Hurt(struck, 3.5, 40);
		SceneMan:IgniteArea(struck.Pos, 6);
		EmitVisualParticles("Sparks", point, Vector(0, -3), 0.8, 4, 0xFFF0B0);
	else
		SceneMan:IgniteArea(point - aim * 2, 5);
		EmitVisualParticles("Embers", point - aim * 2, Vector(0, -3), 0.5, 3, 0xFFB040);
	end
	local now = FX.Now();
	if now - (self.lastScorch or 0) > 400 then
		self.lastScorch = now;
		PostProcessMan:AddScorchMark(point, 600);
	end
end

SPELLS["Sunbeam"].Update = function(self)
	if FX.Now() - self.lastFire < 150 and self.beamEnd then
		local aim = FX.AimOf(self);
		local from = self.MuzzlePos + aim * 4;
		PostProcessMan:AddEnergyBeam(from, self.beamEnd, 5, 255, 220, 120, 1.6, 60);
		PostProcessMan:AddEnergyBeam(from, self.beamEnd, 2, 255, 255, 240, 2.2, 30);
		PostProcessMan:AddLight(self.beamEnd, 140, 255, 230, 150, 1.8);
		PostProcessMan:AddLight(self.MuzzlePos, 90, 255, 230, 150, 1.4);
		PostProcessMan:AddShimmer(self.MuzzlePos + (self.beamEnd - self.MuzzlePos) * 0.5, 24, 0.6);
	end
end

-- Stone Spikes: a line of rock spikes up out of the ground ahead (StoneSpikes.lua).
SPELLS["Stone Spikes"].Cast = function(self, caster, aim)
	local dirX = aim.X >= 0 and 1 or -1;
	local ground = groundInFront(caster, self, dirX, 4);
	FX.Controller("Medieval Spell Stone Spikes", ground, caster and caster.Team or self.Team, caster and caster.UniqueID or 0, {Dir = dirX});
	EmitVisualParticles("Dust", ground, Vector(dirX * 3, -3), 0.8, 10, 0x9C948A);
	CameraMan:AddScreenShake(3, ground);
end

-- Thunderclap: a stunning clap all round the caster (and a push spell: the AI throws grenades back with it).
SPELLS["Thunderclap"].Cast = function(self, caster, aim)
	local centre = caster and caster.Pos or self.Pos;
	local team = caster and caster.Team or self.Team;
	SceneMan:MagicPush(centre, Vector(0, -1), 18, 90, 360, caster);
	for _, hit in ipairs(FX.HurtRadius(centre, 80, 8, team, 6, 100)) do
		if FX.Alive(hit.actor) then
			hit.actor:Ensnare(1.5);
			hit.actor:Say("Stunned");
		end
	end
	FX.Ring("Sparks", centre, 32, 26, 0xDCE6FF, 6);
	FX.Ring("Mist", centre, 14, 8, 0xE8F0FF, 8);
	PostProcessMan:AddShockwave(centre, 14000);
	PostProcessMan:AddLight(centre, 180, 220, 230, 255, 2.5);
	FX.Flash("White", centre);
	CameraMan:AddScreenShake(6, centre);
end

-- Petrify: the enemy under the aim turned to stone for five seconds (SpellEffects.lua's Petrified).
SPELLS["Petrify"].Cast = function(self, caster, aim)
	local team = caster and caster.Team or self.Team;
	local point, struck = strikePoint(self.MuzzlePos, aim, 250, caster, team);
	if struck and FX.Alive(struck) then
		Effects.Apply(struck, "Petrified", 5);
		struck.Vel = struck.Vel * 0.3;
		struck.AngularVel = 0;
		EmitVisualParticles("Dust", struck.Pos, Vector(0, -2), 1, 16, 0xA09890);
		EmitVisualParticles("Sparks", struck.Pos, Vector(0, -3), 0.8, 10, 0x96A08C);
		FX.Flash("White", struck.Pos);
		FX.PlaySound("Medieval Spell Stone Grind", struck.Pos);
		-- (A word when it wears off: the statue can't speak. Petrified.lua sees to it.)
		FX.Controller("Medieval Spell Petrify Watch", struck.Pos, team, caster and caster.UniqueID or 0, {TargetUID = struck.UniqueID});
	else
		EmitVisualParticles("Dust", point, Vector(0, -1), 0.5, 6, 0xA09890);
	end
	PostProcessMan:AddLight(self.MuzzlePos, 80, 150, 160, 140, 1.2);
end

-- Water Jet: every pulse of the held jet douses, shoves and pours; the spray itself is drawn by Update while it pours.
SPELLS["Water Jet"].Cast = function(self, caster, aim)
	local point, struck, team = beamEnd(self, caster, aim, 220);
	self.jetEnd = point;
	if struck and FX.Alive(struck) then
		SceneMan:Extinguish(struck);
		struck.Vel = struck.Vel + aim * 1.4 + Vector(0, -0.3);
		FX.Hurt(struck, 0.4, 0);
		if math.random() < 0.3 then
			struck.Status = Actor.UNSTABLE;
		end
	end
	local now = FX.Now();
	if now - (self.lastPour or 0) > 120 then
		self.lastPour = now;
		SceneMan:PourLiquid(point - aim * 2, 4, "Water");
	end
	-- The jet itself: real droplets' worth of look, render only.
	for _ = 1, 3 do
		local along = FX.Rand(0.2, 1);
		local at = self.MuzzlePos + (point - self.MuzzlePos) * along;
		EmitVisualParticles("Sparks", at, aim * 6 + Vector(FX.Rand(-1, 1), FX.Rand(-1, 1)), 0.3, 1, 0x78BEFF);
	end
	EmitVisualParticles("Mist", point, Vector(-aim.X, -aim.Y - 0.5), 0.5, 2, 0xC8E8FF);
end

SPELLS["Water Jet"].Update = function(self)
	if FX.Now() - self.lastFire < 150 and self.jetEnd then
		local aim = FX.AimOf(self);
		local from = self.MuzzlePos + aim * 3;
		PostProcessMan:AddEnergyBeam(from, self.jetEnd, 4, 120, 190, 255, 0.7, 30);
		PostProcessMan:AddLight(self.jetEnd, 60, 120, 190, 255, 0.6);
	end
end

-- Wall of Stone: a wall of real stone up out of the ground a pace in front (WallOfStone.lua).
SPELLS["Wall of Stone"].Cast = function(self, caster, aim)
	local dirX = aim.X >= 0 and 1 or -1;
	local ground = groundInFront(caster, self, dirX, 18);
	FX.Controller("Medieval Spell Wall Raiser", ground, caster and caster.Team or self.Team, caster and caster.UniqueID or 0, {Dir = dirX});
	EmitVisualParticles("Dust", ground, Vector(0, -3), 0.8, 10, 0x9C948A);
end

-- Bridge of Light: a plank of light laid along the aim from the caster's feet (BridgeOfLight.lua keeps it).
SPELLS["Bridge of Light"].Cast = function(self, caster, aim)
	local team = caster and caster.Team or self.Team;
	local feet = caster and caster.Pos + Vector(0, 6) or self.Pos;
	local length = 100;
	-- Up to the first wall along the way, a little short of it.
	local hit, free = Vector(), Vector();
	local dist = SceneMan:CastObstacleRay(feet + aim * 8, aim * length, hit, free, caster and caster.ID or FX.NoMOID, team, 0, 2);
	if dist >= 0 then
		length = math.max(24, dist - 4);
	end
	local angle = math.atan2(-aim.Y, aim.X);
	local centre = feet + aim * (length / 2 + 6);
	local bridge = FX.Spawn("MOSRotating", "Medieval Spell Light Bridge", FX.Module, centre, Vector());
	if bridge then
		bridge.RotAngle = angle;
		FX.Controller("Medieval Spell Bridge Keeper", centre, team, caster and caster.UniqueID or 0, {BridgeUID = bridge.UniqueID, Angle = angle});
	end
	for i = 0, 8 do
		EmitVisualParticles("Sparks", feet + aim * (6 + length * i / 8), Vector(0, -2), 0.5, 2, 0xFFF0BE);
	end
	PostProcessMan:AddLight(centre, 140, 255, 240, 190, 1.6);
	FX.Flash("Gold", centre);
end

-- Summon Wolf: two wolves out of the mist in front (SummonWolf.lua).
SPELLS["Summon Wolf"].Cast = function(self, caster, aim)
	local dirX = aim.X >= 0 and 1 or -1;
	local ground = groundInFront(caster, self, dirX, 36);
	FX.Controller("Medieval Spell Wolf Mist", ground, caster and caster.Team or self.Team, caster and caster.UniqueID or 0, {Dir = dirX});
	EmitVisualParticles("Mist", ground, Vector(0, -1), 0.8, 12, 0xB8C8C0);
	PostProcessMan:AddLight(ground, 100, 150, 220, 180, 1.2);
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
