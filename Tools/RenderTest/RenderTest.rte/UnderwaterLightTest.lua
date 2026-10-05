function UnderwaterLightTestScript:StartScript()
	self.timer = Timer();
	self.stage = 0;
	self.pours = 0;
end

function UnderwaterLightTestScript:UpdateScript()
	-- A deep tank of water at night with an orange lamp in the middle of the water and a blue one on the bottom, and the same orange lamp in the open air beside
	-- the tank to compare with: the lamps in the water must light the water, the walls and the floor about them.
	local t = self.timer.ElapsedSimTimeMS;
	if self.stage == 0 and t > 2000 then
		self.stage = 1;
		local origin = CameraMan:GetOffset(0);
		self.base = Vector(origin.X + FrameMan.PlayerScreenWidth * 0.42, origin.Y + FrameMan.PlayerScreenHeight * 0.62);
		for x = -90, 90, 6 do
			SandboxDo("Concrete", self.base + Vector(x, 0), 0, 0, 5, "");
		end
		for y = -170, 0, 6 do
			SandboxDo("Concrete", self.base + Vector(-90, y), 0, 0, 5, "");
			SandboxDo("Concrete", self.base + Vector(90, y), 0, 0, 5, "");
		end
		-- A post in the water for the light to fall on.
		for y = -70, -8, 6 do
			SandboxDo("Concrete", self.base + Vector(40, y), 0, 0, 3, "");
		end
	elseif self.stage == 1 and t > 2500 and self.pours < 110 then
		self.pours = self.pours + 1;
		for x = -60, 60, 30 do
			SceneMan:PourLiquid(self.base + Vector(x, -150), 12, "Water");
		end
	elseif self.stage == 1 and t > 7000 then
		self.stage = 2;
		SceneMan:AddTerrainLight(self.base + Vector(-20, -70), 255, 170, 80, 110, 2.2);
		SceneMan:AddTerrainLight(self.base + Vector(-60, -12), 90, 160, 255, 70, 2.0);
		SceneMan:AddTerrainLight(self.base + Vector(190, -70), 255, 170, 80, 110, 2.2);
	end
end
