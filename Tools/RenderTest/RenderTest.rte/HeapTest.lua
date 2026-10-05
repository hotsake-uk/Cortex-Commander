function HeapTestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.pours = 0;
	self.log = io.open("heaptest.log", "w");
end

function HeapTestScript:UpdateScript()
	-- Builds a long flat concrete trough in the sky and pours a lot of water in at one end. It must spread out and come to one level, not rest as a heap.
	local t = self.timer.ElapsedSimTimeMS;
	if self.stage == 0 and t > 2000 then
		self.stage = 1;
		local origin = CameraMan:GetOffset(0);
		self.base = Vector(origin.X + FrameMan.PlayerScreenWidth * 0.5, origin.Y + FrameMan.PlayerScreenHeight * 0.4);
		for x = -300, 300, 6 do
			SandboxDo("Concrete", self.base + Vector(x, 0), 0, 0, 5, "");
		end
		for y = -60, 0, 6 do
			SandboxDo("Concrete", self.base + Vector(-300, y), 0, 0, 5, "");
			SandboxDo("Concrete", self.base + Vector(300, y), 0, 0, 5, "");
		end
	elseif self.stage == 1 and t > 3000 and self.pours < 240 then
		self.pours = self.pours + 1;
		SceneMan:PourLiquid(self.base + Vector(230, -45), 7, "Water");
	end
	if self.stage == 1 and self.logTimer:IsPastSimMS(2000) then
		self.logTimer:Reset();
		-- Depth of water in each column across the trough.
		local least, most, total = nil, nil, 0;
		for x = -285, 285, 3 do
			local depth = 0;
			for y = -70, -5 do
				if SceneMan:GetTerrMatter(self.base.X + x, self.base.Y + y) == 160 then depth = depth + 1; end
			end
			total = total + depth;
			least = least and math.min(least, depth) or depth;
			most = most and math.max(most, depth) or depth;
		end
		self.log:write(string.format("%5.1f  depth %d..%d  total %d  moving %d\n", t / 1000, least, most, total, SceneMan:GetFlowingLiquidPixelCount()));
		self.log:flush();
	end
end
