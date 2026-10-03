function Create(self)
	self.hurtTimer = Timer();
	self.radius = 70;
end

function Update(self)
	-- Twice a second, everyone close enough to the cloud's centre chokes a little. The cloud thins out as it ages.
	if self.hurtTimer:IsPastSimMS(500) then
		self.hurtTimer:Reset();
		local strength = math.max(1 - self.Age / self.Lifetime, 0);
		local radius = self.radius * (0.7 + 0.3 * strength);
		for actor in MovableMan.Actors do
			if actor.Status < Actor.DYING and SceneMan:ShortestDistance(self.Pos, actor.Pos, SceneMan.SceneWrapsX):MagnitudeIsLessThan(radius) then
				actor.Health = actor.Health - 2.5 * (0.4 + 0.6 * strength);
			end
		end
	end
end
