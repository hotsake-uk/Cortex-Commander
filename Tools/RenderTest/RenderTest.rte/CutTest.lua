function CutTestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.log = io.open("cuttest.log", "w");
end

function CutTestScript:UpdateScript()
	-- Drops a boulder on the hill and, once it has landed but before it becomes ground, digs a slot down through its middle. The two halves must fall apart as two pieces.
	local t = self.timer.ElapsedSimTimeMS;
	if self.stage == 0 and t > 3000 then
		self.stage = 1;
		local origin = CameraMan:GetOffset(0);
		self.spot = Vector(origin.X + FrameMan.PlayerScreenWidth * 0.5, origin.Y + FrameMan.PlayerScreenHeight * 0.5);
		for i = 1, 400 do
			if SceneMan:GetTerrMatter(self.spot.X, self.spot.Y + 1) ~= 0 then break; end
			self.spot.Y = self.spot.Y + 1;
		end
		SceneMan:SpawnTerrainChunk(self.spot + Vector(0, -40), 26, "Stone");
	elseif self.stage == 1 and t > 3250 then
		self.stage = 2;
		for y = -80, 0, 3 do
			SandboxDo("Dig", self.spot + Vector(0, y), 0, 0, 4, "");
		end
		self.log:write("cut\n");
	end
	if self.stage >= 1 and self.logTimer:IsPastSimMS(200) then
		self.logTimer:Reset();
		self.log:write(string.format("%5.1f  pieces %d\n", t / 1000, SceneMan:GetFallingTerrainChunkCount()));
		self.log:flush();
	end
end
