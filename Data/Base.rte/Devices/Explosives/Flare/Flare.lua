function Create(self)
	self.lit = false;
end

function Update(self)
	-- Once thrown, the flare burns with a flickering red light until it fizzles out.
	-- Lit once thrown: activated, or flying free of whoever held it.
	if not self.lit and (self:IsActivated() or (self.ID == self.RootID and self.Age > 150 and self.Vel.Magnitude > 0.5)) then
		self.lit = true;
		self.LightRadius = 320;
		self.LightIntensity = 3.0;
		self.LightFlicker = 0.25;
		self:SetLightColor(255, 70, 50);
		self.PostEffectEnabled = true;
		self.burnTimer = Timer();
	end
	if self.lit and self.burnTimer then
		-- Dim over the last few seconds of its life.
		local remaining = 1 - self.burnTimer.ElapsedSimTimeMS / 45000;
		if remaining < 0.15 then
			self.LightIntensity = 3.0 * math.max(remaining / 0.15, 0);
		end
	end
end
