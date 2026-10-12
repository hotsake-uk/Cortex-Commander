-- A web glob (CR-6, spat by the Giant Spider): a unit not of the spitter's side that it strikes has its legs held fast (Actor:Ensnare) for
-- the glob's WebSeconds number value (default 2); it can still turn, but not move or use its weapons. A glob that hits the ground leaves a wisp of web there.

function Create(self)
	self.seconds = self:NumberValueExists("WebSeconds") and self:GetNumberValue("WebSeconds") or 2;
	self.frame = 0;
end

function Update(self)
	self.frame = self.frame + 1;
	if self.frame % 3 == 0 then
		EmitVisualParticles("Mist", self.Pos, self.Vel * -0.05, 0.3, 1, 0xE8E8F0);
	end
end

function OnCollideWithMO(self, mo, rootMO)
	if self.struck then
		return;
	end
	if rootMO and IsActor(rootMO) and rootMO.Team ~= self.Team then
		self.struck = true;
		ToActor(rootMO):Ensnare(self.seconds);
		EmitVisualParticles("Mist", self.Pos, Vector(0, -0.5), 0.8, 4, 0xE8E8F0);
		self.ToDelete = true;
	end
end

function OnCollideWithTerrain(self, terrainID)
	if not self.struck then
		self.struck = true;
		EmitVisualParticles("Mist", self.Pos, Vector(0, -0.3), 0.8, 3, 0xE8E8F0);
		self.ToDelete = true;
	end
end
