-- Shadow Bolt: an orb of darkness that weaves out of the hand and then homes on the nearest enemy unit ahead of it (looked for a few
-- times a second), trailing violet mist. On a unit it bursts for a fair wound and sends a thread of their life back to the caster
-- (LifeDrain.lua); on the ground it fizzles in dark sparks.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local SEEK_RANGE = 440;
local SPEED = 24;
local DAMAGE = 18;

local function findTarget(self)
	local heading = self.Vel.Magnitude > 0.1 and self.Vel.Normalized or Vector(1, 0);
	local best, bestScore = nil, math.huge;
	for actor in MovableMan.Actors do
		if actor.Team ~= self.Team and actor.Status < Actor.DYING and not actor.ToDelete then
			local offset = SceneMan:ShortestDistance(self.Pos, actor.Pos, true);
			local dist = offset.Magnitude;
			if dist < SEEK_RANGE and dist > 0.5 then
				local facing = (offset.X * heading.X + offset.Y * heading.Y) / dist;
				if facing > 0.1 then
					local score = dist * (1.6 - facing);
					if score < bestScore then
						best, bestScore = actor, score;
					end
				end
			end
		end
	end
	return best;
end

local function pop(self, pos, actor)
	if self.popped then
		return;
	end
	self.popped = true;
	if actor then
		FX.Hurt(actor, DAMAGE, 120);
		actor.Vel = actor.Vel + (self.Vel.Magnitude > 0.1 and self.Vel.Normalized * 1.5 or Vector());
		local caster = FX.CasterOf(self);
		if caster then
			local drain = CreateMOPixel("Fell Spell Life Drain", "FellLegion.rte");
			if drain then
				drain.Pos = Vector(pos.X, pos.Y);
				drain.Team = self.Team;
				drain:SetNumberValue("CasterUID", caster.UniqueID);
				drain:SetNumberValue("Heal", 8);
				MovableMan:AddParticle(drain);
			end
		end
	end
	EmitVisualParticles("Sparks", pos, Vector(0, -3), 1, 12, 0xA060FF);
	EmitVisualParticles("Mist", pos, Vector(0, -2), 1, 8, 0x40186A);
	PostProcessMan:AddLight(pos, 70, 160, 80, 240, 1.5);
	FX.Spawn("MOPixel", "Fell Spell Flash Shadow", "FellLegion.rte", pos, Vector());
	self.ToDelete = true;
end

function Create(self)
	FX.RememberCaster(self);
	self.frame = math.random(0, 5);
	self.wobble = FX.Rand(-1, 1);
	self.targetUID = 0;
end

function Update(self)
	if self.popped then
		return;
	end
	self.frame = self.frame + 1;
	local hitPos, struck = FX.ImpactAhead(self, true, 1.4);
	if hitPos then
		pop(self, hitPos, struck and IsActor(struck) and ToActor(struck) or nil);
		return;
	end
	if self.frame % 8 == 0 then
		local target = findTarget(self);
		self.targetUID = target and target.UniqueID or 0;
	end
	local target = FX.ActorByUID(self.targetUID);
	local heading = self.Vel.Magnitude > 0.1 and self.Vel.Normalized or Vector(1, 0);
	if target and FX.Alive(target) then
		local want = SceneMan:ShortestDistance(self.Pos, target.Pos + Vector(0, -4), true).Normalized;
		heading = (heading * 5 + want):SetMagnitude(1);
	else
		heading = heading:RadRotate(math.sin(self.frame * 0.25 + self.wobble) * 0.06);
	end
	self.Vel = heading * SPEED;
	EmitVisualParticles("Mist", self.Pos, self.Vel * -0.05, 0.5, 1, 0x50207A);
	if self.frame % 3 == 0 then
		EmitVisualParticles("Sparks", self.Pos, Vector(FX.Rand(-1, 1), FX.Rand(-1, 1)), 1, 1, 0xC090FF);
	end
end

function OnCollideWithTerrain(self, terrainID)
	pop(self, self.Pos, nil);
end
