-- A unit chilled by Frost Shards (made by FX.Chill): for as long as the unit's MedievalChillUntil says, it is slowed to a crawl (its
-- movement damped every update), drawn frosted over (the "Medieval Frozen" shader) and steams with cold mist and glitter. Then the shader
-- it had before is put back. The controller follows the unit about; it ends early if the unit is gone.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local FROZEN = "Medieval Frozen";

local function finish(self)
	if self.finished then
		return;
	end
	self.finished = true;
	local target = FX.ActorByUID(self.targetUID);
	if target then
		if target.Shader == FROZEN then
			target.Shader = target:StringValueExists("MedievalShaderBeforeChill") and target:GetStringValue("MedievalShaderBeforeChill") or "";
		end
		target:RemoveStringValue("MedievalShaderBeforeChill");
		target:RemoveNumberValue("MedievalChillUntil");
		EmitVisualParticles("Sparks", target.Pos, Vector(0, -2), 1, 10, 0xDDF4FF);
		EmitVisualParticles("Mist", target.Pos, Vector(0, -0.5), 1, 6, 0xE8F8FF);
	end
	self.ToDelete = true;
end

function Create(self)
	self.targetUID = self:GetNumberValue("TargetUID");
	self.frame = 0;
	local target = FX.ActorByUID(self.targetUID);
	if not target then
		self.finished = true;
		self.ToDelete = true;
		return;
	end
	if not target:StringValueExists("MedievalShaderBeforeChill") then
		target:SetStringValue("MedievalShaderBeforeChill", target.Shader == FROZEN and "" or target.Shader);
	end
	target.Shader = FROZEN;
	target:Say("Chilled");
	PostProcessMan:AddFog(target.Pos, 22, 0.35);
end

function Update(self)
	if self.finished then
		return;
	end
	local target = FX.ActorByUID(self.targetUID);
	if not target or not target:NumberValueExists("MedievalChillUntil") or FX.Now() > target:GetNumberValue("MedievalChillUntil") then
		finish(self);
		return;
	end
	self.frame = self.frame + 1;
	self.Pos = target.Pos;
	if target.Status < Actor.DYING then
		target.Vel = target.Vel * 0.86;
		target.AngularVel = target.AngularVel * 0.86;
	end
	PostProcessMan:AddLight(target.Pos, 40, 140, 200, 255, 0.4);
	if self.frame % 8 == 0 then
		PostProcessMan:AddFog(target.Pos, 18, 0.2);
		EmitVisualParticles("Mist", target.Pos + Vector(FX.Rand(-6, 6), FX.Rand(-10, 8)), Vector(0, 0.4), 0.6, 2, 0xDDF2FF);
	end
	if self.frame % 5 == 0 then
		EmitVisualParticles("Sparks", target.Pos + Vector(FX.Rand(-6, 6), FX.Rand(-12, 8)), Vector(0, -0.5), 1, 1, 0xEAF8FF);
	end
end

function Destroy(self)
	-- (Also when the game ends with the unit still chilled, when the unit may already be gone.)
	pcall(finish, self);
end
