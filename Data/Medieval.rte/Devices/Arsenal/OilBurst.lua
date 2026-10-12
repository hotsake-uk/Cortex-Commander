-- The moment an oil flask breaks (an invisible gib of it): a slick of lamp oil is poured out where it lies, which runs and pools in the
-- terrain's liquid simulation and burns when fire reaches it. Nothing lights it here: that's a torch's or a firepot's job.

function Create(self)
	SceneMan:PourLiquid(self.Pos + Vector(0, -2), 9, "Oil");
	self.Vel = Vector();
	self.second = false;
end

function Update(self)
	self.Vel = Vector();
	-- The rest of the flask's worth a moment later, so it spreads rather than stacks.
	if not self.second and self.Age > 150 then
		self.second = true;
		SceneMan:PourLiquid(self.Pos + Vector(RangeRand(-6, 6), -4), 7, "Oil");
	end
end
