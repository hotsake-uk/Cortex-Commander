-- Arcane Missiles: each violet bolt weaves out of the hand and then homes on the nearest enemy unit ahead of it (looked for a few times a
-- second), trailing sparkles. On a unit it bursts for a little damage; on the ground it fizzles in sparks.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local SEEK_RANGE = 420;
local SPEED = 20;

local function findTarget(self)
	local heading = self.Vel.Magnitude > 0.1 and self.Vel.Normalized or Vector(1, 0);
	local best, bestScore = nil, math.huge;
	for actor in MovableMan.Actors do
		if actor.Team ~= self.Team and actor.Status < Actor.DYING and not actor.ToDelete then
			local offset = SceneMan:ShortestDistance(self.Pos, actor.Pos, true);
			local dist = offset.Magnitude;
			if dist < SEEK_RANGE and dist > 0.5 then
				local facing = (offset.X * heading.X + offset.Y * heading.Y) / dist;
				if facing > 0.2 then
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
		FX.Hurt(actor, 9, 60);
		actor.Vel = actor.Vel + (self.Vel.Magnitude > 0.1 and self.Vel.Normalized * 1.5 or Vector());
	end
	EmitVisualParticles("Sparks", pos, Vector(0, -3), 1, 14, 0xC080FF);
	EmitVisualParticles("Sparks", pos, Vector(0, -2), 1, 5, 0xFFFFFF);
	PostProcessMan:AddLight(pos, 70, 190, 110, 255, 1.5);
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
	if self.frame % 6 == 0 then
		local target = findTarget(self);
		self.targetUID = target and target.UniqueID or 0;
	end
	local target = FX.ActorByUID(self.targetUID);
	if target and self.Age > 120 then
		local want = SceneMan:ShortestDistance(self.Pos, target.Pos, true).Normalized * SPEED;
		self.Vel = self.Vel + (want - self.Vel) * 0.14;
	elseif self.Age < 300 then
		-- Weaving out of the hand before they pick a mark.
		self.Vel = Vector(self.Vel.X, self.Vel.Y):RadRotate(self.wobble * 0.03);
	end
	EmitVisualParticles("Sparks", self.Pos, self.Vel * -0.1, 0.7, 1, 0xB070FF);
	if self.frame % 3 == 0 then
		EmitVisualParticles("Embers", self.Pos, Vector(0, -0.5), 0.8, 1, 0xE0C0FF);
	end
end

function OnCollideWithTerrain(self, terrainID)
	pop(self, self.Pos, nil);
end
