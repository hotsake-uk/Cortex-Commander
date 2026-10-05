function DigTestScript:StartScript()
	self.timer = Timer();
	self.stepTimer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.cut = -30;
	self.log = io.open("digtest.log", "w");
end

function DigTestScript:UpdateScript()
	-- A lump of earth hanging under a concrete beam is cut away from it a few pixels at a time, the way a digger or gunfire wears ground away (no explosion, no sandbox tool).
	-- When the last of it is cut through, the lump must fall.
	local t = self.timer.ElapsedSimTimeMS;
	if self.stage == 0 and t > 2000 then
		self.stage = 1;
		local origin = CameraMan:GetOffset(0);
		self.base = Vector(origin.X + FrameMan.PlayerScreenWidth * 0.5, origin.Y + FrameMan.PlayerScreenHeight * 0.3);
		for x = -80, 80, 6 do
			SandboxDo("Concrete", self.base + Vector(x, 0), 0, 0, 6, "");
		end
		SandboxDo("Earth", self.base + Vector(0, 24), 0, 0, 20, "");
	elseif self.stage == 1 and t > 4000 and self.cut <= 30 and self.stepTimer:IsPastSimMS(120) then
		self.stepTimer:Reset();
		SceneMan:DislodgePixelBox(self.base + Vector(self.cut, 7), self.base + Vector(self.cut + 4, 11), true);
		self.cut = self.cut + 4;
		if self.cut > 30 then self.log:write("cut through\n"); end
	end
	if self.stage >= 1 and self.logTimer:IsPastSimMS(500) then
		self.logTimer:Reset();
		local lump = 0;
		for y = 14, 44, 2 do
			for x = -20, 20, 2 do
				if SceneMan:GetTerrMatter(self.base.X + x, self.base.Y + y) ~= 0 then lump = lump + 4; end
			end
		end
		self.log:write(string.format("%5.1f  moving %d  lump still there %d\n", t / 1000, SceneMan:GetFallingTerrainChunkCount(), lump));
		self.log:flush();
	end
end
