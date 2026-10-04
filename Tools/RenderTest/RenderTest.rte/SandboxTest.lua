function SandboxTestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.log = io.open("sandboxtest.log", "w");
end

function SandboxTestScript:Log(text)
	if self.log then
		self.log:write(string.format("%6.1f %s\n", self.timer.ElapsedSimTimeMS / 1000, text));
		self.log:flush();
	end
end

local function Ground(x, y)
	local pos = Vector(x, y);
	for i = 1, 400 do
		if SceneMan:GetTerrMatter(pos.X, pos.Y + 1) ~= 0 then
			break;
		end
		pos.Y = pos.Y + 1;
	end
	return pos + Vector(0, -20);
end

function SandboxTestScript:UpdateScript()
	-- Sets up a three-way battle through the sandbox tools, the way a player would, and logs how it goes.
	local t = self.timer.ElapsedSimTimeMS;
	local origin = CameraMan:GetOffset(0);
	local w = FrameMan.PlayerScreenWidth;
	local h = FrameMan.PlayerScreenHeight;
	if self.stage == 0 and t > 3000 then
		self.stage = 1;
		local ok = true;
		ok = SandboxDo("Brain", Ground(origin.X + w * 0.15, origin.Y), 0, 0, 1, "Brain Case") and ok;
		ok = SandboxDo("Units", Ground(origin.X + w * 0.22, origin.Y), 0, 0, 3, "Soldier Light") and ok;
		ok = SandboxDo("Units", Ground(origin.X + w * 0.85, origin.Y), 1, 2, 4, "Soldier Light") and ok;
		ok = SandboxDo("Units", Ground(origin.X + w * 0.55, origin.Y), 2, 1, 2, "Browncoat") and ok;
		ok = SandboxDo("Structure", Vector(origin.X + w * 0.4, origin.Y + h * 0.25), 0, 0, 1, "Wall") and ok;
		ok = SandboxDo("Item", Vector(origin.X + w * 0.5, origin.Y + h * 0.2), 0, 0, 1, "Napalm Flamer") and ok;
		ok = SandboxDo("Nonsense", Vector(), 0, 0, 1, "") == false and ok;
		self:Log("setup " .. tostring(ok));
	elseif self.stage == 1 and t > 12000 then
		self.stage = 2;
		SandboxDo("Lightning", Vector(origin.X + w * 0.7, origin.Y + h * 0.3), 0, 0, 1, "");
		self:Log("lightning");
	elseif self.stage == 2 and t > 16000 then
		self.stage = 3;
		for side = 0, 3 do
			SandboxDo("Orders", Vector(), side, 1, 1, "");
		end
		self:Log("everyone attack");
	elseif self.stage == 3 and t > 18000 then
		self.stage = 4;
		for actor in MovableMan.Actors do
			if actor.Team == 0 and actor.ClassName == "AHuman" then
				self:Log("take control: " .. tostring(SandboxDo("Take control", actor.Pos, 0, 0, 1, "")));
				break;
			end
		end
	elseif self.stage == 5 and t > 50000 then
		self.stage = 6;
		self:Log("build mode after closing " .. tostring(SandboxBuildMode(false)) .. ", state " .. ActivityMan:GetActivity().ActivityState);
	elseif self.stage == 4 and t > 22000 then
		self.stage = 5;
		local ok, err = pcall(function()
			self:Log("build mode " .. tostring(SandboxBuildMode(true)) .. ", state " .. ActivityMan:GetActivity().ActivityState);
		end);
		if not ok then
			self:Log("build mode failed: " .. tostring(err));
		end
	end
	if self.stage >= 1 and self.logTimer:IsPastSimMS(2000) then
		self.logTimer:Reset();
		local activity = ActivityMan:GetActivity();
		local controlled = activity:GetControlledActor(0);
		self:Log(string.format("blue %d  red %d  green %d  yellow %d  view %d  controlling %s", SandboxCountUnits(0), SandboxCountUnits(1), SandboxCountUnits(2), SandboxCountUnits(3), activity:GetViewState(0), controlled and controlled.PresetName or "nobody"));
	end
end

function SandboxTestScript:EndScript()
	if self.log then
		self.log:close();
		self.log = nil;
	end
end
