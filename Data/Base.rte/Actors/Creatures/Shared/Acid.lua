-- Acid (phase 3, the Ooze's spit and touch): a unit not of the attacker's side that it strikes is eaten away (Actor:Corrode), losing
-- health for a few seconds as the acid works. The particle's number values say how badly:
--   AcidDamage   health a second (default 5)
--   AcidSeconds  how long (default 3)
--   AcidGlob     1 for a spat glob, which bursts in a fizz of acid on whatever it hits (default 0, a touch)

local function Number(self, key, default)
	return self:NumberValueExists(key) and self:GetNumberValue(key) or default;
end

function Create(self)
	self.glob = Number(self, "AcidGlob", 0) > 0;
	self.frame = 0;
end

function Update(self)
	if self.glob then
		self.frame = self.frame + 1;
		if self.frame % 3 == 0 then
			EmitVisualParticles("Mist", self.Pos, self.Vel * -0.05, 0.3, 1, 0xB8F020);
		end
	end
end

function OnCollideWithMO(self, mo, rootMO)
	if self.struck then
		return;
	end
	self.struck = true;
	if rootMO and IsActor(rootMO) and rootMO.Team ~= self.Team then
		ToActor(rootMO):Corrode(Number(self, "AcidDamage", 5), Number(self, "AcidSeconds", 3));
	end
	if self.glob then
		EmitVisualParticles("Mist", self.Pos, Vector(0, -0.5), 0.8, 4, 0xB8F020);
		self.ToDelete = true;
	end
end

function OnCollideWithTerrain(self, terrainID)
	if self.glob and not self.struck then
		self.struck = true;
		EmitVisualParticles("Mist", self.Pos, Vector(0, -0.3), 0.8, 3, 0xB8F020);
		self.ToDelete = true;
	end
end
