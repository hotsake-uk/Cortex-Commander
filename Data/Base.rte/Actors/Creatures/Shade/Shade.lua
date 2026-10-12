-- A shade's unlight (Bestiary III): it gives off a faint cold blue-grey shimmer and a dim light as it drifts, and killed it has no
-- body to leave: it goes up in a burst of mist.

function Create(self)
	self.frame = math.random(0, 5);
	self.gone = false;
end

function Update(self)
	self.frame = self.frame + 1;
	if self.frame % 6 == 0 then
		EmitVisualParticles("Mist", self.Pos + Vector(0, self.Radius * 0.5), Vector(0, -0.4), 0.4, 1, 0x6070A0);
	end
	PostProcessMan:AddLight(self.Pos, 50, 110, 130, 200, 0.5);
	if not self.gone and (self.Health <= 0 or self.Status >= Actor.DYING) then
		self.gone = true;
		EmitVisualParticles("Mist", self.Pos, Vector(0, -1), 1.2, 14, 0x8090C0);
		PostProcessMan:AddLight(self.Pos, 120, 150, 170, 240, 1.2);
		self.ToDelete = true;
	end
end
