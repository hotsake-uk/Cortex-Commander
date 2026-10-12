-- A chilling touch (Bestiary III, the Frost Drake's breath and the Shade's touch): a unit not of the attacker's side that it strikes is
-- "Chilled" (a StatusEffect, SE-1: slowed, drawn frosted in the "Creature Frozen" shader, lit pale blue) for the particle's number values:
--   ChillSeconds  how long (default 4)
--   ChillSpeed    its pace while chilled, as a share of its own (default 0.6)
-- The Kingdom's Dispel lifts it. A chilled unit struck again is chilled afresh for the full time.

local function Number(self, key, default)
	return self:NumberValueExists(key) and self:GetNumberValue(key) or default;
end

function Create(self)
	self.frame = math.random(0, 3);
	self.struck = false;
end

function Update(self)
	self.frame = self.frame + 1;
	if self.frame % 3 == 0 then
		EmitVisualParticles("Mist", self.Pos, self.Vel * -0.05, 0.3, 1, 0xC8E8FF);
	end
end

function OnCollideWithMO(self, mo, rootMO)
	if self.struck then
		return;
	end
	self.struck = true;
	if rootMO and IsActor(rootMO) and rootMO.Team ~= self.Team then
		local actor = ToActor(rootMO);
		local e = StatusEffect("Chilled");
		e.Label = "Chilled";
		e.Harmful = true;
		e.SpeedMultiplier = Number(self, "ChillSpeed", 0.6);
		e.Shader = "Creature Frozen";
		e:SetLightColor(150, 200, 255);
		e.LightRadius = 40;
		e.LightIntensity = 0.4;
		actor:AddStatusEffect(e, Number(self, "ChillSeconds", 4));
		EmitVisualParticles("Mist", actor.Pos, Vector(0, -1), 0.5, 4, 0xB0D8FF);
	end
	self.ToDelete = true;
end

function OnCollideWithTerrain(self, terrainID)
	self.ToDelete = true;
end
