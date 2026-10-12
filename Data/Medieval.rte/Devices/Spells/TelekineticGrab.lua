-- A unit seized by Telekinetic Grab (SpellCast.lua): held (the Grabbed status effect, SpellEffects.lua, keeps it from moving, jumping or
-- firing) and hauled through the air to the caster's feet over a second and a half, violet light playing along the pull, then let go.
-- Let go early if the caster or the held unit is gone, or the caster is more than twenty paces off.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local SECONDS = 1.5;

function Create(self)
	self.targetUID = self:GetNumberValue("TargetUID");
	self.casterUID = self:GetNumberValue("CasterUID");
	self.untilMS = FX.Now() + SECONDS * 1000;
	self.frame = 0;
end

function Update(self)
	local target = FX.ActorByUID(self.targetUID);
	local caster = FX.ActorByUID(self.casterUID);
	if not target or not caster or FX.Now() > self.untilMS or not target:HasStatusEffect("Grabbed") then
		if target then
			target:RemoveStatusEffect("Grabbed");
			EmitVisualParticles("Sparks", target.Pos, Vector(0, -1), 0.6, 8, 0xB478FF);
		end
		self.ToDelete = true;
		return;
	end
	self.frame = self.frame + 1;
	self.Pos = target.Pos;
	-- Hauled: towards a spot just in front of the caster, up off the ground a little so it doesn't drag.
	local goal = caster.Pos + Vector(caster.HFlipped and -22 or 22, -6);
	local away = SceneMan:ShortestDistance(target.Pos, goal, true);
	local distance = away.Magnitude;
	if distance > 400 then
		target:RemoveStatusEffect("Grabbed");
		self.ToDelete = true;
		return;
	end
	if distance > 18 then
		local pull = away / distance * math.min(14, 4 + distance * 0.06);
		target.Vel = pull;
	else
		target.Vel = target.Vel * 0.5;
	end
	target.AngularVel = target.AngularVel * 0.8;
	-- The pull drawn: glimmer along the line and light at both ends.
	if self.frame % 2 == 0 then
		local t = math.random();
		local at = caster.Pos + Vector(0, -8) + SceneMan:ShortestDistance(caster.Pos + Vector(0, -8), target.Pos, true) * t;
		EmitVisualParticles("Sparks", at, Vector(0, 0), 0.4, 1, 0xB478FF);
	end
	PostProcessMan:AddLight(target.Pos, 70, 180, 120, 255, 0.9);
	PostProcessMan:AddLight(caster.Pos + Vector(caster.HFlipped and -10 or 10, -8), 40, 180, 120, 255, 0.8);
end
