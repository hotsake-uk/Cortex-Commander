-- Holds the Sandbox's Blood brush down on the ground in front of the camera for half a minute, then watches the pool for half a minute more:
-- for measuring what pouring blood costs (CCCP_PERF_LOG). "Render Test Water Pour" does the same with the Water brush, to tell what is blood's own.
local function StartPour(self, tool)
	self.tool = tool;
	self.timer = Timer();
	self.pourTimer = Timer();
	self.logTimer = Timer();
	self.log = io.open(string.lower(tool) .. "pour.log", "w");
	self.most = 0;
end

local function UpdatePour(self)
	local origin = CameraMan:GetOffset(0);
	local w = FrameMan.PlayerScreenWidth;
	local h = FrameMan.PlayerScreenHeight;
	-- The brush pours every 0.03 s while held (Sandbox tool interval), size 10, swept slowly back and forth across the middle of the view.
	if self.timer:IsPastSimMS(3000) and not self.timer:IsPastSimMS(33000) and self.pourTimer:IsPastSimMS(30) then
		self.pourTimer:Reset();
		local sweep = math.sin(self.timer.ElapsedSimTimeMS / 3000) * w * 0.15;
		SandboxDo(self.tool, Vector(origin.X + w * 0.5 + sweep, origin.Y + h * 0.3), 0, 0, 10, "");
	end
	self.most = math.max(self.most, SceneMan:GetFlowingLiquidPixelCount());
	if self.logTimer:IsPastSimMS(2000) then
		self.logTimer:Reset();
		self.log:write(string.format("%5.1f flowing %d  update %.2f ms  (most %d)  particles %d\n", self.timer.ElapsedSimTimeMS / 1000, SceneMan:GetFlowingLiquidPixelCount(), SceneMan:GetLiquidUpdateMS(), self.most, MovableMan:GetParticleCount()));
		self.log:flush();
	end
end

-- (Only the class the enabled preset names exists, so each is set up only if it is there.)
for class, tool in pairs({BloodPourTestScript = "Blood", WaterPourTestScript = "Water"}) do
	local script = rawget(_G, class);
	if script then
		script.StartScript = function(self) StartPour(self, tool); end
		script.UpdateScript = UpdatePour;
	end
end
