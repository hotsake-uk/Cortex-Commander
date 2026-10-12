-- A unit under Levitate (SpellCast.lua): for five seconds it weighs nothing (its GlobalAccScalar held at a feather's), its falls slowed, so
-- it drifts, leaps high and comes down softly, with glimmers trailing from it. Then its weight is given back. It ends early if the unit is gone.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local SECONDS = 5;

local function finish(self)
	if self.finished then
		return;
	end
	self.finished = true;
	local target = FX.ActorByUID(self.targetUID);
	if target then
		target.GlobalAccScalar = self.before or 1;
		target:RemoveNumberValue("MedievalLevitating");
		EmitVisualParticles("Mist", target.Pos + Vector(0, 8), Vector(0, 1), 0.6, 6, 0xE8F0FF);
	end
	self.ToDelete = true;
end

function Create(self)
	self.targetUID = self:GetNumberValue("TargetUID");
	self.untilMS = FX.Now() + SECONDS * 1000;
	self.frame = 0;
	local target = FX.ActorByUID(self.targetUID);
	if not target then
		self.finished = true;
		self.ToDelete = true;
		return;
	end
	if target:NumberValueExists("MedievalLevitating") then
		-- Already floating: this cast only lengthens the first one, which keeps the unit's real weight.
		target:SetNumberValue("MedievalLevitating", self.untilMS);
		self.finished = true;
		self.ToDelete = true;
		return;
	end
	self.before = target.GlobalAccScalar;
	target:SetNumberValue("MedievalLevitating", self.untilMS);
	target.GlobalAccScalar = 0.08;
	target.Vel = Vector(target.Vel.X, math.min(target.Vel.Y, 0) - 1.5);
end

function Update(self)
	if self.finished then
		return;
	end
	local target = FX.ActorByUID(self.targetUID);
	if not target or not target:NumberValueExists("MedievalLevitating") or FX.Now() > target:GetNumberValue("MedievalLevitating") or target.Status >= Actor.DYING then
		finish(self);
		return;
	end
	self.frame = self.frame + 1;
	self.Pos = target.Pos;
	-- Held to a drift: a fall slows to a feather's, and a leap carries, but not for ever.
	if target.Vel.Y > 2 then
		target.Vel = Vector(target.Vel.X, 2);
	end
	target.GlobalAccScalar = 0.08;
	PostProcessMan:AddLight(target.Pos, 40, 220, 230, 255, 0.4);
	if self.frame % 5 == 0 then
		EmitVisualParticles("Sparks", target.Pos + Vector(FX.Rand(-6, 6), FX.Rand(0, 14)), Vector(0, 0.8), 0.7, 1, 0xDCE6FF);
	end
end

function Destroy(self)
	pcall(finish, self);
end
