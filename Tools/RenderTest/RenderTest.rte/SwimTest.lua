function SwimTestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.log = io.open("swimtest.log", "w");
end

function SwimTestScript:Log(text)
	self.log:write(string.format("%6.1f %s\n", self.timer.ElapsedSimTimeMS / 1000, text));
	self.log:flush();
end

function SwimTestScript:UpdateScript()
	-- Digs a pit, fills it with water, and drops in a soldier (breathes, light), a heavy soldier and a dummy robot, all told to do nothing.
	local t = self.timer.ElapsedSimTimeMS;
	local origin = CameraMan:GetOffset(0);
	local w = FrameMan.PlayerScreenWidth;
	local h = FrameMan.PlayerScreenHeight;
	if self.stage == 0 and t > 2000 then
		self.stage = 1;
		self.pit = Vector(origin.X + w * 0.5, origin.Y + h * 0.5);
		for i = 1, 400 do
			if SceneMan:GetTerrMatter(self.pit.X, self.pit.Y + 1) ~= 0 then break; end
			self.pit.Y = self.pit.Y + 1;
		end
		self.pit.Y = self.pit.Y + 45;
		for x = -50, 50, 12 do
			for y = -40, 40, 12 do
				SandboxDo("Dig", self.pit + Vector(x, y), 0, 0, 12, "");
			end
		end
	elseif self.stage == 1 and t > 3000 then
		self.stage = 2;
		self.pours = 0;
	elseif self.stage == 2 then
		if self.pours < 260 then
			self.pours = self.pours + 1;
			for x = -40, 40, 16 do
				SceneMan:PourLiquid(self.pit + Vector(x, -34), 6, "Water");
			end
		elseif t > 12000 then
			self.stage = 3;
			SandboxDo("Units", self.pit + Vector(-30, -75), 0, 5, 1, "Soldier Light");
			SandboxDo("Units", self.pit + Vector(0, -75), 0, 5, 1, "Soldier Heavy");
			SandboxDo("Units", self.pit + Vector(30, -75), 0, 5, 1, "Dummy");
			self:Log("units dropped in, flowing " .. SceneMan:GetFlowingLiquidPixelCount());
		end
	end
	if self.stage >= 3 and self.logTimer:IsPastSimMS(1500) then
		self.logTimer:Reset();
		local line = "";
		for actor in MovableMan.Actors do
			line = line .. string.format("%s m%.0f: depth %d hp %.0f air %s y %.0f vy %.1f | ", actor.PresetName, actor.Mass, actor:GetNumberValue("LiquidDepth"), actor.Health, actor:NumberValueExists("AirLeft") and string.format("%.1f", actor:GetNumberValue("AirLeft")) or "-", actor.Pos.Y - self.pit.Y, actor.Vel.Y);
		end
		self:Log(line);
	end
end
