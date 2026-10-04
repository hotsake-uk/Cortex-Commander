function SandboxStressScript:StartScript()
	self.timer = Timer();
	self.spawnTimer = Timer();
	self.logTimer = Timer();
	self.waves = 0;
	self.log = io.open("sandboxstress.log", "w");
end

function SandboxStressScript:Log(text)
	if self.log then
		self.log:write(string.format("%6.1f %s\n", self.timer.ElapsedSimTimeMS / 1000, text));
		self.log:flush();
	end
end

local function Ground(x, y)
	local pos = Vector(x, y);
	for i = 1, 600 do
		if SceneMan:GetTerrMatter(pos.X, pos.Y + 1) ~= 0 then
			break;
		end
		pos.Y = pos.Y + 1;
	end
	return pos + Vector(0, -20);
end

function SandboxStressScript:UpdateScript()
	-- A huge two-sided battle through the sandbox tools: squads of ten on attack orders every second for 25 seconds.
	local origin = CameraMan:GetOffset(0);
	local w = FrameMan.PlayerScreenWidth;
	if self.waves < 25 and self.timer:IsPastSimMS(3000) and self.spawnTimer:IsPastSimMS(1000) then
		self.spawnTimer:Reset();
		self.waves = self.waves + 1;
		-- Spawned holding position, like a player setting up armies, then all told to attack at once.
		SandboxDo("Units", Ground(origin.X + w * (0.1 + 0.02 * self.waves), origin.Y), 0, 0, 10, "Soldier Heavy");
		SandboxDo("Units", Ground(origin.X + w * (0.9 - 0.02 * self.waves), origin.Y), 1, 0, 10, "Whitebot");
		if self.waves == 25 then
			for side = 0, 1 do
				SandboxDo("Orders", Vector(), side, 1, 1, "");
			end
			self:Log("everyone attack");
		end
	end
	if self.logTimer:IsPastSimMS(2000) then
		self.logTimer:Reset();
		local items = 0;
		for item in MovableMan.Items do
			items = items + 1;
		end
		self:Log(string.format("waves %d  red %d  green %d  loose items %d", self.waves, SandboxCountUnits(0), SandboxCountUnits(1), items));
	end
end

function SandboxStressScript:EndScript()
	if self.log then
		self.log:close();
		self.log = nil;
	end
end
