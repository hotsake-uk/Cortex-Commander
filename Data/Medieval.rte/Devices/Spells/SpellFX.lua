-- SpellFX: what the Medieval spells' scripts share. Each script loads it with
--   local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");
-- and gets this table back: finding the caster, looking ahead for what a projectile is about to hit, hurting and chilling what is in
-- reach, the big explosions, and spawning effect particles and the invisible controllers that run an effect on after the cast.

local FX = {};

FX.Module = "Medieval.rte";
FX.NoMOID = 255;

-- Pixels an object moving at 1 m/s covers in one sim update.
function FX.PxPerFrame()
	return GetPPM() * TimerMan.DeltaTimeSecs;
end

function FX.Now()
	return TimerMan.SimTimeMS;
end

function FX.Rand(lo, hi)
	return lo + math.random() * (hi - lo);
end

-- Which way a held spell points, as a unit vector (as Base.rte's TelekineticPush.lua works it out).
function FX.AimOf(device)
	return Vector(device.FlipFactor * math.cos(device.RotAngle), -device.FlipFactor * math.sin(device.RotAngle));
end

-- The unit holding a device, or nil.
function FX.HolderOf(device)
	local root = device:GetRootParent();
	if root and root.UniqueID ~= device.UniqueID and IsActor(root) then
		return ToActor(root);
	end
	return nil;
end

-- An actor by its UniqueID, if it is still about.
function FX.ActorByUID(uid)
	if not uid or uid == 0 then
		return nil;
	end
	local mo = MovableMan:FindObjectByUniqueID(uid);
	if mo and IsActor(mo) and not mo.ToDelete then
		return ToActor(mo);
	end
	return nil;
end

-- Whether an actor is alive enough to be affected (not dying, not dead).
function FX.Alive(actor)
	return actor ~= nil and MovableMan:ValidMO(actor) and not actor.ToDelete and actor.Status < Actor.DYING;
end

-- The unit that cast a projectile: from the NumberValue CasterUID when a script set it, else from the spell that fired it (the engine
-- has a fired round ignore its gun), else the nearest unit of the projectile's team close by.
function FX.CasterOf(self)
	if self:NumberValueExists("CasterUID") then
		return FX.ActorByUID(self:GetNumberValue("CasterUID"));
	end
	local ok, gun = pcall(self.GetWhichMOToNotHit, self);
	if ok and gun then
		local rootOK, rootID = pcall(function() return gun.RootID; end);
		if rootOK and rootID and rootID ~= FX.NoMOID then
			local root = MovableMan:GetMOFromID(rootID);
			if root and IsActor(root) then
				return ToActor(root);
			end
		end
	end
	local best, bestDist = nil, 80;
	for actor in MovableMan.Actors do
		if actor.Team == self.Team then
			local dist = SceneMan:ShortestDistance(self.Pos, actor.Pos, true).Magnitude;
			if dist < bestDist then
				best, bestDist = actor, dist;
			end
		end
	end
	return best;
end

-- Remembers the caster on a projectile or controller (its UniqueID and MOID) so later updates don't have to look again.
function FX.RememberCaster(self)
	local caster = FX.CasterOf(self);
	self.casterUID = caster and caster.UniqueID or 0;
	self.ignoreID = caster and caster.ID or FX.NoMOID;
	if caster and not self:NumberValueExists("CasterUID") then
		self:SetNumberValue("CasterUID", caster.UniqueID);
	end
	return caster;
end

-- Says a speech trigger on an actor (the engine applies the chance and cooldown).
function FX.Say(actor, trigger)
	if actor and MovableMan:ValidMO(actor) and IsActor(actor) then
		ToActor(actor):Say(trigger);
	end
end

-- The ground straight below a point, within maxDown pixels: the first solid pixel, and whether one was found.
function FX.GroundBelow(pos, maxDown)
	local hit = Vector();
	if SceneMan:CastNotMaterialRay(pos, Vector(0, maxDown), 0, hit, 1, false) then
		return Vector(hit.X, hit.Y), true;
	end
	return Vector(pos.X, pos.Y + maxDown), false;
end

-- Looks along where a moving object goes this update for terrain and, if hitMOs, for a unit or object not of its own team. Returns where
-- it struck and the root object it struck (nil for terrain), or nil if the way is clear.
function FX.ImpactAhead(self, hitMOs, lookScale)
	local step = self.Vel * (FX.PxPerFrame() * (lookScale or 1.2));
	local length = step.Magnitude;
	if length < 2 then
		step = length > 0.01 and step:SetMagnitude(2) or Vector(0, 2);
	end
	if hitMOs then
		local id = SceneMan:CastMORay(self.Pos, step, self.ignoreID or FX.NoMOID, self.Team, 0, true, 0);
		if id ~= FX.NoMOID then
			local mo = MovableMan:GetMOFromID(id);
			if mo then
				local root = MovableMan:GetMOFromID(mo.RootID) or mo;
				if root.Team ~= self.Team or self.Team < 0 then
					local hitPos = SceneMan:GetLastRayHitPos();
					return Vector(hitPos.X, hitPos.Y), root;
				end
			end
		end
	end
	local hit = Vector();
	if SceneMan:CastNotMaterialRay(self.Pos, step, 0, hit, 0, false) then
		return Vector(hit.X, hit.Y), nil;
	end
	return nil;
end

-- The units in reach of a point, not of a team (-1 for none spared): a list of {actor, offset (from the point), distance}, each unit once.
function FX.ActorsNear(pos, radius, spareTeam)
	local found, seen = {}, {};
	for mo in MovableMan:GetMOsInRadius(pos, radius, spareTeam or -1) do
		local root = MovableMan:GetMOFromID(mo.RootID);
		if root and IsActor(root) and not seen[root.UniqueID] and (spareTeam == nil or spareTeam < 0 or root.Team ~= spareTeam) then
			seen[root.UniqueID] = true;
			local actor = ToActor(root);
			local offset = SceneMan:ShortestDistance(pos, actor.Pos, true);
			found[#found + 1] = {actor = actor, offset = offset, distance = offset.Magnitude};
		end
	end
	return found;
end

-- Hurts one unit: takes health and flashes it white. Returns whether it was hurt.
function FX.Hurt(actor, damage, flashMS)
	if not FX.Alive(actor) or damage <= 0 then
		return false;
	end
	actor.Health = actor.Health - damage;
	if flashMS and flashMS > 0 then
		actor:FlashWhite(flashMS);
	end
	return true;
end

-- Hurts the units in a radius, the nearer the harder (full at the centre, a quarter at the edge), sparing a team, and shoves them away
-- from the centre by up to knock metres a second. Returns the list ActorsNear gave.
function FX.HurtRadius(pos, radius, damage, spareTeam, knock, flashMS)
	local list = FX.ActorsNear(pos, radius, spareTeam);
	for _, hit in ipairs(list) do
		local share = 1 - 0.75 * math.min(hit.distance / radius, 1);
		FX.Hurt(hit.actor, damage * share, flashMS);
		if knock and knock > 0 then
			local away = hit.distance > 0.5 and hit.offset / hit.distance or Vector(0, -1);
			hit.actor.Vel = hit.actor.Vel + (away + Vector(0, -0.4)) * (knock * share);
		end
	end
	return list;
end

-- Adds a particle preset (MOPixel, MOSParticle or MOSRotating, from Base.rte or this module) at a point, moving.
local creators = {
	MOPixel = function(name, module) return CreateMOPixel(name, module); end,
	MOSParticle = function(name, module) return CreateMOSParticle(name, module); end,
	MOSRotating = function(name, module) return CreateMOSRotating(name, module); end,
	AEmitter = function(name, module) return CreateAEmitter(name, module); end,
};

function FX.Spawn(kind, name, module, pos, vel, team, values)
	local particle = creators[kind](name, module or FX.Module);
	if particle then
		particle.Pos = Vector(pos.X, pos.Y);
		particle.Vel = vel and Vector(vel.X, vel.Y) or Vector();
		if team then
			particle.Team = team;
			particle.IgnoresTeamHits = true;
		end
		for key, value in pairs(values or {}) do
			particle:SetNumberValue(key, value);
		end
		MovableMan:AddParticle(particle);
	end
	return particle;
end

-- A short glowing flash (one of Spells.ini's "Medieval Spell Flash ..." presets: Fire, Red, Violet, Ice, Green, White, Gold, Purple).
function FX.Flash(colourName, pos)
	return FX.Spawn("MOPixel", "Medieval Spell Flash " .. colourName, FX.Module, pos, Vector());
end

-- An invisible controller that runs an effect on after the cast (one of Spells.ini's controller presets), with the caster and team.
function FX.Controller(name, pos, team, casterUID, values)
	local controller = CreateMOPixel(name, FX.Module);
	if controller then
		controller.Pos = Vector(pos.X, pos.Y);
		controller.Vel = Vector();
		controller.Team = team or -1;
		controller:SetNumberValue("CasterUID", casterUID or 0);
		for key, value in pairs(values or {}) do
			controller:SetNumberValue(key, value);
		end
		MovableMan:AddParticle(controller);
	end
	return controller;
end

function FX.PlaySound(name, pos)
	local sound = CreateSoundContainer(name, FX.Module);
	if sound then
		sound:Play(pos);
	end
	return sound;
end

-- A ring of visual particles flying out from a point (render only, cheap).
function FX.Ring(kind, pos, count, speed, colour, startRadius)
	for i = 1, count do
		local angle = (i / count) * math.pi * 2 + math.random() * 0.2;
		local dir = Vector(math.cos(angle), math.sin(angle));
		EmitVisualParticles(kind, pos + dir * (startRadius or 0), dir * speed, 0.08, 1, colour);
	end
end

-- Chills a unit for a while (Frost Shards): slowed, frosted over (the "Medieval Frozen" shader) and steaming with cold, run by a "Medieval
-- Spell Frost Chill" controller (FrostChill.lua). A unit already chilled just stays chilled longer. The time it lasts until is kept on the
-- unit (NumberValue MedievalChillUntil), so a second shard doesn't start a second controller.
function FX.Chill(actor, durationMS, team)
	if not FX.Alive(actor) then
		return;
	end
	local now = FX.Now();
	local untilMS = now + durationMS;
	local chilled = actor:NumberValueExists("MedievalChillUntil") and actor:GetNumberValue("MedievalChillUntil") > now - 500;
	if chilled then
		untilMS = math.max(untilMS, actor:GetNumberValue("MedievalChillUntil"));
	end
	actor:SetNumberValue("MedievalChillUntil", untilMS);
	if not chilled then
		FX.Controller("Medieval Spell Frost Chill", actor.Pos, team, 0, {TargetUID = actor.UniqueID});
	end
end

-- The fiery explosion of a fireball or a meteor: fire, smoke, burning flames, sparks and embers, a flash, shockwave, scorch and blast of
-- air, the ground set alight, the screen shaken, and the units in reach hurt and thrown (sparing the caster's team). Scale 1 is a fireball.
function FX.FireBlast(pos, team, scale)
	scale = scale or 1;
	local radius = 60 * scale;
	-- The fireball itself: a few big puffs of fire and rolling fireballs (capped, they are real particles).
	for i = 1, math.floor(4 + 2 * scale) do
		local dir = Vector(FX.Rand(-1, 1), FX.Rand(-1, 0.3)):SetMagnitude(FX.Rand(3, 9) * scale);
		FX.Spawn("MOSParticle", "Fire Puff Large", "Base.rte", pos, dir);
	end
	for i = 1, math.floor(2 + 2 * scale) do
		FX.Spawn("MOSParticle", "Fire Ball " .. math.random(1, 4), "Base.rte", pos + Vector(FX.Rand(-8, 8), FX.Rand(-8, 4)) * scale, Vector(FX.Rand(-4, 4), FX.Rand(-6, -1)));
	end
	for i = 1, math.floor(2 + scale) do
		FX.Spawn("MOSParticle", "Explosion Smoke " .. math.random(1, 2), "Base.rte", pos, Vector(FX.Rand(-5, 5), FX.Rand(-7, -2)));
	end
	-- Burning flames thrown out that stick and burn where they land.
	for i = 1, math.floor(2 + 2 * scale) do
		FX.Spawn("MOSParticle", "Flame Hurt Short", "Base.rte", pos + Vector(0, -3), Vector(FX.Rand(-9, 9), FX.Rand(-12, -3)) * (0.6 + 0.4 * scale));
	end
	-- Render-only sparks, embers, smoke and debris: lots of them for nothing.
	EmitVisualParticles("Sparks", pos, Vector(0, -8), 1, math.floor(40 * scale), 0xFFB040);
	EmitVisualParticles("Embers", pos, Vector(0, -4), 1, math.floor(30 * scale), 0xFF7A20);
	EmitVisualParticles("Smoke", pos, Vector(0, -2), 0.9, math.floor(12 * scale), 0);
	EmitVisualParticles("Debris", pos, Vector(0, -10), 0.8, math.floor(12 * scale), 0);
	FX.Flash("Fire", pos);
	PostProcessMan:AddShockwave(pos, 12000 * scale);
	PostProcessMan:AddScorchMark(pos, 6000 * scale);
	SceneMan:AddAirBlast(pos, 4000 * scale);
	SceneMan:IgniteArea(pos, 35 * scale);
	CameraMan:AddScreenShake(6 * scale, pos);
	FX.HurtRadius(pos, radius, 55 * scale, team, 12 * scale, 0);
end

return FX;
