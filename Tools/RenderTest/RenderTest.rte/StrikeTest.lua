function StrikeTestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.log = io.open("striketest.log", "w");
end

function StrikeTestScript:UpdateScript()
	-- Builds a tower on the hill and drops two boulders beside it. Once they've come to rest: a grenade beside them (they must be thrown), a rocket into the tower, then a stick of bombs.
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
		SceneMan:SpawnTerrainChunk(self.spot + Vector(-110, -60), 9, "Stone");
		SceneMan:SpawnTerrainChunk(self.spot + Vector(-135, -80), 7, "Stone");
	elseif self.stage == 1 and t > 10000 then
		self.stage = 2;
		self.log:write("grenade beside the rested boulders\n");
		SandboxDo("Grenade blast", self.spot + Vector(-122, -14), 0, 0, 1, "");
	elseif self.stage == 2 and t > 13000 then
		self.stage = 3;
		self.log:write("rocket strike on the tower\n");
		SandboxDo("Rocket strike", self.spot + Vector(0, -150), 0, 0, 1, "");
	elseif self.stage == 3 and t > 18000 then
		self.stage = 4;
		self.log:write("carpet bombing\n");
		SandboxDo("Carpet bombing", self.spot + Vector(0, -40), 0, 0, 1, "");
	end
	if self.stage >= 1 and self.logTimer:IsPastSimMS(500) then
		self.logTimer:Reset();
		self.log:write(string.format("%5.1f  pieces moving %d\n", t / 1000, SceneMan:GetFallingTerrainChunkCount()));
		self.log:flush();
	end
end
