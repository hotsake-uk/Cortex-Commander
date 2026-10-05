function ScrapTestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.log = io.open("scraptest.log", "w");
end

function ScrapTestScript:UpdateScript()
	-- A big boulder dropped onto a few loose scraps hanging in the air and a thin stub standing on a floor. It must go through them to the floor, not rest on them.
	local t = self.timer.ElapsedSimTimeMS;
	if self.stage == 0 and t > 2000 then
		self.stage = 1;
		local origin = CameraMan:GetOffset(0);
		self.base = Vector(origin.X + FrameMan.PlayerScreenWidth * 0.5, origin.Y + FrameMan.PlayerScreenHeight * 0.5);
		for x = -90, 90, 6 do
			SandboxDo("Concrete", self.base + Vector(x, 0), 0, 0, 5, "");
		end
		-- Scraps in mid-air, 40 and 60 pixels above the floor.
		SandboxDo("Concrete", self.base + Vector(-12, -45), 0, 0, 1, "");
		SandboxDo("Concrete", self.base + Vector(14, -62), 0, 0, 1, "");
		-- A stub of wall two pixels thick standing on the floor.
		for y = -24, -6, 1 do
			SandboxDo("Concrete", self.base + Vector(2, y), 0, 0, 0, "");
		end
	elseif self.stage == 1 and t > 3500 then
		self.stage = 2;
		SceneMan:SpawnTerrainChunk(self.base + Vector(0, -150), 32, "Stone");
	end
	if self.stage >= 1 and self.logTimer:IsPastSimMS(500) then
		self.logTimer:Reset();
		-- The lowest row with anything solid in it above the floor tells how far down the boulder got: 6 is the floor's top.
		local lowestGap = 0;
		for y = -6, -80, -1 do
			local solid = 0;
			for x = -30, 30, 2 do
				if SceneMan:GetTerrMatter(self.base.X + x, self.base.Y + y) ~= 0 then solid = solid + 1; end
			end
			if solid >= 8 then lowestGap = -y - 6; break; end
		end
		self.log:write(string.format("%5.1f  moving %d  boulder's underside %d px above the floor\n", t / 1000, SceneMan:GetFallingTerrainChunkCount(), lowestGap));
		self.log:flush();
	end
end
