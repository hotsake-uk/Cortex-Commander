function IslandTestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.log = io.open("islandtest.log", "w");
end

local function Solid(base, x1, x2, y1, y2)
	local count = 0;
	for y = y1, y2, 2 do
		for x = x1, x2, 2 do
			if SceneMan:GetTerrMatter(base.X + x, base.Y + y) ~= 0 then count = count + 4; end
		end
	end
	return count;
end

function IslandTestScript:UpdateScript()
	-- A floating island with a lump hanging under it on a thick stalk. 1: chip its corner; nothing may fall. 2: whittle the stalk to three pixels; the lump must snap off.
	-- 3: cut the island in two off centre; the smaller part must fall and the bigger stay.
	local t = self.timer.ElapsedSimTimeMS;
	if self.stage == 0 and t > 2000 then
		self.stage = 1;
		local origin = CameraMan:GetOffset(0);
		self.base = Vector(origin.X + FrameMan.PlayerScreenWidth * 0.5, origin.Y + FrameMan.PlayerScreenHeight * 0.3);
		for x = -60, 60, 6 do
			SandboxDo("Earth", self.base + Vector(x, 0), 0, 0, 12, "");
		end
		for y = 14, 36, 4 do
			SandboxDo("Earth", self.base + Vector(0, y), 0, 0, 4, "");
		end
		SandboxDo("Earth", self.base + Vector(0, 50), 0, 0, 14, "");
	elseif self.stage == 1 and t > 4000 then
		self.stage = 2;
		self.log:write("chip the corner\n");
		SandboxDo("Dig", self.base + Vector(-68, -8), 0, 0, 8, "");
	elseif self.stage == 2 and t > 7000 then
		self.stage = 3;
		self.log:write("whittle the stalk\n");
		SandboxDo("Dig", self.base + Vector(-5, 25), 0, 0, 3, "");
		SandboxDo("Dig", self.base + Vector(5, 25), 0, 0, 3, "");
	elseif self.stage == 3 and t > 10000 then
		self.stage = 4;
		self.log:write("cut the island in two\n");
		for y = -16, 16, 3 do
			SandboxDo("Dig", self.base + Vector(30, y), 0, 0, 4, "");
		end
	end
	if self.stage >= 1 and self.logTimer:IsPastSimMS(500) then
		self.logTimer:Reset();
		self.log:write(string.format("%5.1f  moving %d  island left %5d  island right %5d  lump %5d\n", t / 1000, SceneMan:GetFallingTerrainChunkCount(),
			Solid(self.base, -74, 24, -14, 13), Solid(self.base, 36, 74, -14, 13), Solid(self.base, -16, 16, 37, 66)));
		self.log:flush();
	end
end
