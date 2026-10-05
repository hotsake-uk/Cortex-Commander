function SplashTestScript:StartScript()
	self.timer = Timer();
	self.blastTimer = Timer();
	self.stage = 0;
	self.pours = 0;
end

function SplashTestScript:UpdateScript()
	-- Digs a pit in the hill near the camera and fills it with water; then, turn about, sets a grenade off in it and drops a boulder into it, so each throws a splash.
	local t = self.timer.ElapsedSimTimeMS;
	if self.stage == 0 and t > 2000 then
		self.stage = 1;
		local origin = CameraMan:GetOffset(0);
		self.pit = Vector(origin.X + FrameMan.PlayerScreenWidth * 0.5, origin.Y + FrameMan.PlayerScreenHeight * 0.5);
		for i = 1, 400 do
			if SceneMan:GetTerrMatter(self.pit.X, self.pit.Y + 1) ~= 0 then break; end
			self.pit.Y = self.pit.Y + 1;
		end
		self.pit.Y = self.pit.Y + 34;
		for x = -30, 30, 6 do
			for y = -40, 0, 8 do
				SandboxDo("Dig", self.pit + Vector(x * (1 - y / 60), y), 0, 0, 22, "");
			end
		end
	end
	if self.pit and (self.countTimer == nil or self.countTimer:IsPastSimMS(500)) then
		-- How much water there is in and around the pit, to see that splashing loses none.
		self.countTimer = self.countTimer or Timer();
		self.countTimer:Reset();
		local water, flying = 0, 0;
		for y = -200, 60, 2 do
			for x = -160, 160, 2 do
				if SceneMan:GetTerrMatter(self.pit.X + x, self.pit.Y + y) == 160 then water = water + 4; end
			end
		end
		for particle in MovableMan.Particles do
			if particle.Material.PresetName == "Water" then flying = flying + 1; end
		end
		self.log = self.log or io.open("splashtest.log", "w");
		self.log:write(string.format("%5.1f  water %5d  flying %4d  %s\n", t / 1000, water, flying, self.note or ""));
		self.log:flush();
		self.note = nil;
	end
	if self.stage == 1 and t > 3000 then
		if self.pours < 70 then
			self.pours = self.pours + 1;
			SceneMan:PourLiquid(self.pit + Vector(-20, -10), 6, "Water");
			SceneMan:PourLiquid(self.pit + Vector(20, -10), 6, "Water");
		elseif self.blastTimer:IsPastSimMS(2200) and (self.turn or 0) < 4 then
			self.blastTimer:Reset();
			self.turn = (self.turn or 0) + 1;
			if self.turn % 2 == 1 then
				self.note = "grenade";
				SandboxDo("Grenade blast", self.pit + Vector(-10, 8), 0, 0, 1, "");
			else
				self.note = "boulder";
				SceneMan:SpawnTerrainChunk(self.pit + Vector(15, -150), 10, "Stone");
			end
		end
	end
end
