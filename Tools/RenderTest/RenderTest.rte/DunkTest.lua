function DunkTestScript:StartScript()
	self.timer = Timer();
	self.stage = 0;
	self.pours = 0;
end

function DunkTestScript:UpdateScript()
	-- Builds a deep concrete tank in the sky, fills it with water and drops a boulder and a lump of concrete in. They must sink to the bottom and the water rise around them.
	local t = self.timer.ElapsedSimTimeMS;
	if self.stage == 0 and t > 2000 then
		self.stage = 1;
		local origin = CameraMan:GetOffset(0);
		self.base = Vector(origin.X + FrameMan.PlayerScreenWidth * 0.5, origin.Y + FrameMan.PlayerScreenHeight * 0.62);
		for x = -90, 90, 6 do
			SandboxDo("Concrete", self.base + Vector(x, 0), 0, 0, 5, "");
		end
		for y = -170, 0, 6 do
			SandboxDo("Concrete", self.base + Vector(-90, y), 0, 0, 5, "");
			SandboxDo("Concrete", self.base + Vector(90, y), 0, 0, 5, "");
		end
	elseif self.stage == 1 and t > 2500 and self.pours < 110 then
		self.pours = self.pours + 1;
		for x = -60, 60, 30 do
			SceneMan:PourLiquid(self.base + Vector(x, -150), 12, "Water");
		end
	elseif self.stage == 1 and t > 8000 then
		self.stage = 2;
		-- A lid of ice on the water, which the pieces must break through.
		local top = self.base.Y - 165;
		for i = 1, 170 do
			if SceneMan:GetTerrMatter(self.base.X, top + 1) ~= 0 then break; end
			top = top + 1;
		end
		for x = -84, 84, 2 do
			SandboxDo("Ice", Vector(self.base.X + x, top - 1), 0, 0, 2, "");
		end
		SceneMan:SpawnTerrainChunk(self.base + Vector(0, -260), 50, "Stone");
	end
	if self.base and (self.countTimer == nil or self.countTimer:IsPastSimMS(1000)) then
		-- How much water there is in and above the tank, to see that what falls in loses none.
		self.countTimer = self.countTimer or Timer();
		self.countTimer:Reset();
		local water = 0;
		for y = -420, 0 do
			for x = -110, 110 do
				if SceneMan:GetTerrMatter(self.base.X + x, self.base.Y + y) == 160 then water = water + 1; end
			end
		end
		self.log = self.log or io.open("dunktest.log", "w");
		self.log:write(string.format("%5.1f  water %6d  pieces %d\n", t / 1000, water, SceneMan:GetFallingTerrainChunkCount()));
		self.log:flush();
	end
end
