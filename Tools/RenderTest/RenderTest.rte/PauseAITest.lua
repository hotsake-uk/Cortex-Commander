function PauseAITestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.log = io.open("pauseaitest.log", "w");
end

function PauseAITestScript:Log(text)
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

function PauseAITestScript:UpdateScript()
	-- Two squads on attack orders face to face with the AI paused, then the AI is let loose.
	local t = self.timer.ElapsedSimTimeMS;
	if self.stage == 0 and t > 3000 then
		self.stage = 1;
		SandboxPauseAI(true);
		local origin = CameraMan:GetOffset(0);
		local w = FrameMan.PlayerScreenWidth;
		SandboxDo("Units", Ground(origin.X + w * 0.4, origin.Y), 0, 1, 4, "Soldier Light");
		SandboxDo("Units", Ground(origin.X + w * 0.6, origin.Y), 1, 1, 4, "Soldier Light");
		self:Log("AI paused, squads spawned");
	elseif self.stage == 1 and t > 13000 then
		self.stage = 2;
		SandboxPauseAI(false);
		self:Log("AI resumed");
	end
	if self.stage >= 1 and self.logTimer:IsPastSimMS(2000) then
		self.logTimer:Reset();
		local moved = 0;
		for actor in MovableMan.Actors do
			if actor.ClassName == "AHuman" then
				if not self.start then
					self.start = {};
				end
				local key = actor.UniqueID;
				if self.start[key] == nil then
					self.start[key] = actor.Pos.X;
				end
				moved = math.max(moved, math.abs(actor.Pos.X - self.start[key]));
			end
		end
		self:Log(string.format("red %d  green %d  most anyone walked %.0f px", SandboxCountUnits(0), SandboxCountUnits(1), moved));
	end
end

function PauseAITestScript:EndScript()
	if self.log then
		self.log:close();
		self.log = nil;
	end
end
