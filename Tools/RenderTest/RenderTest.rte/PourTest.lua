function PourTestScript:StartScript()
	self.timer = Timer();
	self.stage = 0;
end

function PourTestScript:UpdateScript()
	-- A concrete shelf in the sky with water poured onto it all the while, so it streams off the end and falls: for looking at how pouring water is drawn.
	local t = self.timer.ElapsedSimTimeMS;
	if self.stage == 0 and t > 2000 then
		self.stage = 1;
		local origin = CameraMan:GetOffset(0);
		self.base = Vector(origin.X + FrameMan.PlayerScreenWidth * 0.35, origin.Y + FrameMan.PlayerScreenHeight * 0.25);
		for x = -90, 60, 6 do
			SandboxDo("Concrete", self.base + Vector(x, 0), 0, 0, 5, "");
		end
		for y = -40, 0, 6 do
			SandboxDo("Concrete", self.base + Vector(-90, y), 0, 0, 5, "");
		end
		for x = 40, 200, 6 do
			SandboxDo("Concrete", self.base + Vector(x, 200), 0, 0, 5, "");
		end
	elseif self.stage == 1 and t > 3000 then
		SceneMan:PourLiquid(self.base + Vector(-60, -16), 4, "Water");
	end
end
