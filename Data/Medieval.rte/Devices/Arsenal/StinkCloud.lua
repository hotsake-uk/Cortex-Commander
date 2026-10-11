-- The heart of a stinkpot's cloud (an invisible gib of it): for a few seconds it keeps pouring choking gas into the air and bubbling
-- up slime, under a sickly yellow-green haze, and everyone breathing it near the middle chokes a little twice a second as it thins out.

function Create(self)
	self.gasTimer = Timer();
	self.hurtTimer = Timer();
	self.bubbleTimer = Timer();
	self.radius = 60;
	self.Vel = Vector();
	SceneMan:AddGas(self.Pos + Vector(0, -4), 1, 6);
end

function Update(self)
	self.Vel = Vector();
	local strength = math.max(1 - self.Age / self.Lifetime, 0);

	if self.Age < 6500 and self.gasTimer:IsPastSimMS(300) then
		self.gasTimer:Reset();
		SceneMan:AddGas(self.Pos + Vector(RangeRand(-20, 20), RangeRand(-16, 2)), 1, 1.5 * strength + 0.3);
	end

	PostProcessMan:AddFog(self.Pos + Vector(0, -10), self.radius * (0.8 + 0.6 * (1 - strength)), 0.35 * strength);
	PostProcessMan:AddLight(self.Pos + Vector(0, -6), 40 + 30 * strength, 150, 210, 40, 0.55 * strength);

	if self.Age < 8000 and self.bubbleTimer:IsPastSimMS(110) then
		self.bubbleTimer:Reset();
		local bubble = CreateMOPixel("Arsenal Stink Bubble", "Medieval.rte");
		if bubble then
			bubble.Pos = self.Pos + Vector(RangeRand(-16, 16), RangeRand(-4, 0));
			bubble.Vel = Vector(RangeRand(-0.6, 0.6), RangeRand(-3, -1));
			MovableMan:AddParticle(bubble);
		end
	end

	if self.hurtTimer:IsPastSimMS(500) then
		self.hurtTimer:Reset();
		local radius = self.radius * (0.7 + 0.3 * strength);
		for actor in MovableMan.Actors do
			if actor.Status < Actor.DYING and SceneMan:ShortestDistance(self.Pos, actor.Pos, SceneMan.SceneWrapsX):MagnitudeIsLessThan(radius) then
				actor.Health = actor.Health - 1.6 * (0.4 + 0.6 * strength);
			end
		end
	end
end
