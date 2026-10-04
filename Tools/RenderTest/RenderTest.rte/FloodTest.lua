function FloodTestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.log = io.open("floodtest.log", "w");
	self.worst = 0;
	self.most = 0;
end

function FloodTestScript:UpdateScript()
	-- Pours a great deal of water from the sky across the view, to measure what a big flood costs.
	local origin = CameraMan:GetOffset(0);
	local w = FrameMan.PlayerScreenWidth;
	if self.timer:IsPastSimMS(2000) and not self.timer:IsPastSimMS(22000) then
		for i = 0, 24 do
			SceneMan:PourLiquid(Vector(origin.X + w * i / 24, origin.Y + 20), 7, "Water");
		end
	end
	self.worst = math.max(self.worst, SceneMan:GetLiquidUpdateMS());
	self.most = math.max(self.most, SceneMan:GetFlowingLiquidPixelCount());
	if self.logTimer:IsPastSimMS(2000) then
		self.logTimer:Reset();
		self.log:write(string.format("%5.1f flowing %d  update %.2f ms  (most %d, worst %.2f ms)\n", self.timer.ElapsedSimTimeMS / 1000, SceneMan:GetFlowingLiquidPixelCount(), SceneMan:GetLiquidUpdateMS(), self.most, self.worst));
		self.log:flush();
	end
end
