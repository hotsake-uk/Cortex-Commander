function Create(self)
	self.checkTimer = Timer();
end

function Update(self)
	-- Once alight, it sputters for a moment and then goes up.
	if self.fuse then
		if self.flameTimer:IsPastSimMS(120) then
			self.flameTimer:Reset();
			local flame = CreateMOSParticle("Body Flame", "Base.rte");
			flame.Pos = self.Pos + Vector(math.random(-4, 4), -6);
			flame.Vel = Vector(math.random() - 0.5, -1.5);
			MovableMan:AddParticle(flame);
		end
		if self.fuse:IsPastSimMS(self.fuseTime) then
			self:GibThis();
		end
		return;
	end
	if self.checkTimer:IsPastSimMS(250) then
		self.checkTimer:Reset();
		-- Set alight by flames and napalm hitting it, burning ground around it, or a lucky shot when it's already riddled.
		if self:GetNumberValue("Ignited") > 0 or SceneMan:IsBurningNear(self.Pos, 9) or (self.WoundCount >= 6 and math.random() < 0.1) then
			self.fuse = Timer();
			self.flameTimer = Timer();
			self.fuseTime = 500 + math.random() * 1000;
		end
	end
end
