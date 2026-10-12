-- Hex Bolt: a Shadow Bolt's smaller cousin (ShadowBolt.lua), a violet orb that homes on the nearest enemy ahead of it. It hurts little, but
-- the unit it bursts on is Hexed (FellEffects.lua): everything they hit for eight seconds does little more than half.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");
local Fell = dofile("FellLegion.rte/Devices/Spells/FellEffects.lua");

local SEEK_RANGE = 440;
local SPEED = 24;
local DAMAGE = 8;

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
					-- One not yet hexed is the better mark.
					local score = dist * (1.6 - facing) * (actor:HasStatusEffect("Hexed") and 2 or 1);
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
		if Fell.Apply(actor, "Hexed") then
			FX.Say(actor, "Hexed");
		end
		-- The hex's rune: a ring of violet sparks round the unit and a glyph of light over them for a moment.
		FX.Ring("Sparks", actor.Pos, 14, 4, 0xC060FF, 8);
		PostProcessMan:AddEnergyBeam(actor.Pos + Vector(-10, -22), actor.Pos + Vector(10, -22), 2, 190, 90, 255, 1.0, 20);
		PostProcessMan:AddEnergyBeam(actor.Pos + Vector(0, -30), actor.Pos + Vector(0, -14), 2, 190, 90, 255, 1.0, 20);
	end
	EmitVisualParticles("Sparks", pos, Vector(0, -3), 1, 10, 0xC060FF);
	EmitVisualParticles("Mist", pos, Vector(0, -2), 1, 6, 0x50207A);
	PostProcessMan:AddLight(pos, 70, 190, 90, 255, 1.5);
	FX.Spawn("MOPixel", "Fell Spell Flash Shadow", "FellLegion.rte", pos, Vector());
	local sound = CreateSoundContainer("Fell Hex Zap", "FellLegion.rte");
	if sound then
		sound:Play(pos);
	end
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
	EmitVisualParticles("Mist", self.Pos, self.Vel * -0.05, 0.5, 1, 0x6A30A0);
	if self.frame % 3 == 0 then
		EmitVisualParticles("Sparks", self.Pos, Vector(FX.Rand(-1, 1), FX.Rand(-1, 1)), 1, 1, 0xE0A0FF);
	end
end

function OnCollideWithTerrain(self, terrainID)
	pop(self, self.Pos, nil);
end
