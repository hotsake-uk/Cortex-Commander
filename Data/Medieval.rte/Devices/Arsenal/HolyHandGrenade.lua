-- The Holy Hand Grenade: pulling the pin (starting the throw; ActivatesWhenReleased = 0) starts the count of three, and the thrower says so,
-- loudly. While it counts it glows ever brighter, sheds golden sparkles and, near the end, bends the air around it. The blast itself is
-- the Arsenal Holy Blast gib (HolyBlast.lua).

function Create(self)
	self.announced = false;
	self.countTimer = Timer();
	self.sparkleTimer = Timer();
end

function Update(self)
	if not self:IsActivated() then
		return;
	end
	if not self.announced then
		self.announced = true;
		self.countTimer:Reset();
		local root = self:GetRootParent();
		if root and root.UniqueID ~= self.UniqueID and IsActor(root) then
			ToActor(root):Say("HolyGrenade");
		end
	end

	local fuse = math.max(self.TriggerDelay, 1);
	local t = math.min(self.countTimer.ElapsedSimTimeMS / fuse, 1);
	local pulse = 0.5 + 0.5 * math.sin(self.countTimer.ElapsedSimTimeMS * (0.008 + 0.02 * t));
	PostProcessMan:AddLight(self.Pos, 24 + 50 * t, 255, 220, 120, 0.5 + (0.6 + 1.2 * pulse) * t);
	if t > 0.6 then
		PostProcessMan:AddShimmer(self.Pos, 14 + 16 * t, (t - 0.6) * 2.5);
	end
	if self.sparkleTimer:IsPastSimMS(140 - 100 * t) then
		self.sparkleTimer:Reset();
		local sparkle = CreateMOPixel("Arsenal Holy Sparkle", "Medieval.rte");
		if sparkle then
			sparkle.Pos = self.Pos + Vector(RangeRand(-3, 3), RangeRand(-4, 2));
			sparkle.Vel = self.Vel * 0.5 + Vector(RangeRand(-1.5, 1.5), RangeRand(-2.5, -0.5));
			MovableMan:AddParticle(sparkle);
		end
	end
end
