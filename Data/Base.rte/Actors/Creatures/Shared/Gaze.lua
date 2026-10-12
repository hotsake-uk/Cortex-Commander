-- A petrifying gaze (Bestiary II, the Basilisk's): spat as the creature AI spits (CreatureBehaviors.SpitAt, CreatureSpit), a fast,
-- weightless pixel drawn as a beam from where it left the eye to where it is. A unit not of the creature's side that it strikes is
-- "Petrified" (a StatusEffect, SE-1: held fast, silenced, half the harm, drawn in the "Creature Petrified" shader; the Kingdom's Dispel
-- takes it off, and its Petrify spell puts the same effect on) for the pixel's GazeSeconds number value (default 4). A unit already stone
-- is left be. Gone on striking anything.

local function Number(self, key, default)
	return self:NumberValueExists(key) and self:GetNumberValue(key) or default;
end

function Create(self)
	self.from = Vector(self.Pos.X, self.Pos.Y);
	self.struck = false;
end

function Update(self)
	PostProcessMan:AddEnergyBeam(self.from, self.Pos, 1, 160, 255, 110, 0.7, 20);
	PostProcessMan:AddLight(self.Pos, 40, 160, 255, 110, 0.8);
end

function OnCollideWithMO(self, mo, rootMO)
	if self.struck then
		return;
	end
	self.struck = true;
	if rootMO and IsActor(rootMO) and rootMO.Team ~= self.Team then
		local actor = ToActor(rootMO);
		if not actor:HasStatusEffect("Petrified") then
			local e = StatusEffect("Petrified");
			e.Label = "Petrified";
			e.Harmful = true;
			e.Held = true;
			e.Silenced = true;
			e.DamageTakenMultiplier = 0.5;
			e.Shader = "Creature Petrified";
			actor:AddStatusEffect(e, Number(self, "GazeSeconds", 4));
			actor:FlashWhite(200);
			EmitVisualParticles("Dust", actor.Pos, Vector(0, -2), 0.8, 10, 0x8A8C84);
			PostProcessMan:AddLight(actor.Pos, 90, 160, 255, 110, 1.2);
		end
	end
	self.ToDelete = true;
end

function OnCollideWithTerrain(self, terrainID)
	self.ToDelete = true;
end
