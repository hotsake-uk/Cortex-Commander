-- A wisp's light (phase 4): the wisp itself, and the bolts it casts, give off a faint pale blue shimmer as they go. A bolt that strikes
-- anything fizzles out in a burst of it.

function Create(self)
	self.frame = math.random(0, 5);
	self.bolt = self.ClassName == "MOPixel";
end

function Update(self)
	self.frame = self.frame + 1;
	if self.frame % (self.bolt and 2 or 5) == 0 then
		EmitVisualParticles("Mist", self.Pos, self.Vel * -0.05, 0.3, 1, 0x9CC8FF);
	end
end

function OnCollideWithMO(self, mo, rootMO)
	if self.bolt and not self.struck then
		self.struck = true;
		EmitVisualParticles("Mist", self.Pos, Vector(0, -0.3), 0.8, 4, 0x9CC8FF);
	end
end

function OnCollideWithTerrain(self, terrainID)
	if self.bolt and not self.struck then
		self.struck = true;
		EmitVisualParticles("Mist", self.Pos, Vector(0, -0.3), 0.8, 3, 0x9CC8FF);
		self.ToDelete = true;
	end
end
