-- The Cyclops' one eye (Bestiary II): hurt below half its health, it sees half as far (its Perceptiveness halved) until it is back above
-- six tenths, if it ever is.

function Create(self)
	self.sight = self.Perceptiveness;
	self.eyeHurt = false;
end

function Update(self)
	if not self.eyeHurt and self.Health < self.MaxHealth * 0.5 then
		self.eyeHurt = true;
		self.Perceptiveness = self.sight * 0.5;
		self:FlashWhite(120);
	elseif self.eyeHurt and self.Health >= self.MaxHealth * 0.6 then
		self.eyeHurt = false;
		self.Perceptiveness = self.sight;
	end
end
