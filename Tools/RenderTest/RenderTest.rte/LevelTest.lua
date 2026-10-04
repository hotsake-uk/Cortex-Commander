function LevelTestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.log = io.open("leveltest.log", "w");
end

local WATER = 160;

-- The water surface in each column of a span: returns the highest and lowest surface rows found, and how many columns hold water.
local function Surface(x1, x2, yTop, yBottom)
	local highest, lowest, columns = nil, nil, 0;
	for x = x1, x2, 2 do
		for y = yTop, yBottom do
			if SceneMan:GetTerrMatter(x, y) == WATER then
				columns = columns + 1;
				highest = highest and math.min(highest, y) or y;
				lowest = lowest and math.max(lowest, y) or y;
				break;
			end
		end
	end
	return highest, lowest, columns;
end

function LevelTestScript:UpdateScript()
	-- 1: a band of water painted down a slope must run down and pool level. 2: two pits joined by a tunnel at the bottom, one filled: both must come to the same level.
	local t = self.timer.ElapsedSimTimeMS;
	local origin = CameraMan:GetOffset(0);
	local w = FrameMan.PlayerScreenWidth;
	local h = FrameMan.PlayerScreenHeight;
	if self.stage == 0 and t > 2000 then
		self.stage = 1;
		-- Two pits and a tunnel, dug into the hill top left of centre.
		self.base = Vector(origin.X + w * 0.5, origin.Y + h * 0.5);
		for i = 1, 400 do
			if SceneMan:GetTerrMatter(self.base.X, self.base.Y + 1) ~= 0 then break; end
			self.base.Y = self.base.Y + 1;
		end
		self.base.Y = self.base.Y + 60;
		for y = -50, 30, 8 do
			SandboxDo("Dig", self.base + Vector(-60, y), 0, 0, 14, "");
			SandboxDo("Dig", self.base + Vector(60, y), 0, 0, 14, "");
		end
		for x = -60, 60, 6 do
			SandboxDo("Dig", self.base + Vector(x, 34), 0, 0, 7, "");
		end
		-- The slope: a band of water laid along the hillside to the right.
		self.slopeStart = Vector(self.base.X + 150, self.base.Y - 60);
	elseif self.stage == 1 and t > 3500 then
		self.stage = 2;
		self.pours = 0;
	elseif self.stage == 2 then
		if self.pours < 220 then
			self.pours = self.pours + 1;
			SceneMan:PourLiquid(self.base + Vector(-60, -40), 6, "Water");
			-- Follow the ground down the slope and lay water on it.
			if self.pours <= 120 then
				local p = Vector(self.slopeStart.X + self.pours * 2.5, self.slopeStart.Y - 80);
				for i = 1, 500 do
					if SceneMan:GetTerrMatter(p.X, p.Y + 1) ~= 0 then break; end
					p.Y = p.Y + 1;
				end
				SandboxDo("Water", p + Vector(0, -6), 0, 0, 10, "");
			end
		else
			self.stage = 3;
			self.log:write("poured\n");
		end
	end
	if self.stage >= 3 and self.logTimer:IsPastSimMS(2500) then
		self.logTimer:Reset();
		local lh, ll, lc = Surface(self.base.X - 73, self.base.X - 47, self.base.Y - 70, self.base.Y + 45);
		local rh, rl, rc = Surface(self.base.X + 47, self.base.X + 73, self.base.Y - 70, self.base.Y + 45);
		self.log:write(string.format("%5.1f  moving %5d (%.2f ms)  left pit surface %s..%s (%d cols)  right pit surface %s..%s (%d cols)\n", t / 1000, SceneMan:GetFlowingLiquidPixelCount(), SceneMan:GetLiquidUpdateMS(),
			tostring(lh and lh - self.base.Y), tostring(ll and ll - self.base.Y), lc, tostring(rh and rh - self.base.Y), tostring(rl and rl - self.base.Y), rc));
		self.log:flush();
	end
end
