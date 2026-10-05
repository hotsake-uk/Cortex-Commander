function CrashTestScript:StartScript()
	self.timer = Timer();
	self.stage = 0;
end

function CrashTestScript:UpdateScript()
	-- A tower on the hill; a rocket crashes into it, then a dropship.
	local t = self.timer.ElapsedSimTimeMS;
	if self.stage == 0 and t > 2000 then
		self.stage = 1;
		local origin = CameraMan:GetOffset(0);
		self.spot = Vector(origin.X + FrameMan.PlayerScreenWidth * 0.5, origin.Y + FrameMan.PlayerScreenHeight * 0.3);
		for i = 1, 500 do
			if SceneMan:GetTerrMatter(self.spot.X, self.spot.Y + 1) ~= 0 then break; end
			self.spot.Y = self.spot.Y + 1;
		end
		SandboxDo("Tower", self.spot + Vector(0, 4), 0, 0, 1, "");
	elseif self.stage == 1 and t > 3500 then
		self.stage = 2;
		SandboxDo("Crashing rocket", self.spot + Vector(0, -200), 0, 0, 1, "");
	elseif self.stage == 2 and t > 8000 then
		self.stage = 3;
		SandboxDo("Crashing dropship", self.spot + Vector(0, -90), 0, 0, 1, "");
	end
end
