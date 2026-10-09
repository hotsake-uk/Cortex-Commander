-- A Battle Director mode played between two drawn bases on whatever scene is loaded, logged to battlemode.log every 3 s: for each team, how
-- many units, how far they are from their base on average, and their AI modes; with each Red unit's place, path and order. CCCP_MODE picks the
-- mode (1 capture the flag, the default; 2 king of the hill needs a zone, so isn't set up here); CCCP_WATCH the team whose base the camera
-- watches.
function BattleModeTestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.log = io.open("battlemode.log", "w");
end

function BattleModeTestScript:Log(text)
	if self.log then
		self.log:write(string.format("%6.1f %s\n", self.timer.ElapsedSimTimeMS / 1000, text));
		self.log:flush();
	end
end

function BattleModeTestScript:Base(team, x)
	local ground = SceneMan:MovePointToGround(Vector(x, 0), 0, 3);
	local y = ground.Y;
	local corners = { Vector(x - 150, y - 200), Vector(x + 150, y - 200), Vector(x + 150, y + 30), Vector(x - 150, y + 30) };
	for _, c in ipairs(corners) do
		SandboxDo("Team's base", c, team, 0, 1, "");
	end
	SandboxDo("Team's base", corners[1], team, 0, 1, "");
	self.centres = self.centres or {};
	self.centres[team] = Vector(x, y - 50);
	self:Log(string.format("base %d at %.0f, ground %.0f", team, x, y));
end

function BattleModeTestScript:UpdateScript()
	local t = self.timer.ElapsedSimTimeMS;
	self.updates = (self.updates or 0) + 1;
	if self.updates % 200 == 1 then
		self:Log(string.format("update %d real %.1f", self.updates, self.timer.ElapsedRealTimeMS / 1000));
	end
	if self.stage == 0 and t > 2000 then
		self.stage = 1;
		local w = SceneMan.SceneWidth;
		self:Log("scene " .. SceneMan.Scene.PresetName .. " width " .. w);
		self:Base(0, w * 0.25);
		self:Base(1, w * 0.75);
	elseif self.stage == 1 and t > 3000 then
		self.stage = 2;
		SandboxBattleMode(tonumber(os.getenv("CCCP_MODE") or "1"), 10, false);
		self:Log("mode started");
	end
	if self.stage >= 1 and self.centres then
		CameraMan:SetScrollTarget(self.centres[tonumber(os.getenv("CCCP_WATCH") or "0")], 1.0, 0);
	end
	if self.stage >= 2 and self.logTimer:IsPastSimMS(3000) then
		local o = CameraMan:GetOffset(0);
		self:Log(string.format("camera %.0f,%.0f screen %d", o.X, o.Y, FrameMan.PlayerScreenWidth));
		self.logTimer:Reset();
		for team = 0, 1 do
			local n, far, modes, attack, post, wp, target = 0, 0, {}, 0, 0, 0, 0;
			local sum = 0;
			for actor in MovableMan.Actors do
				if actor.Team == team and (actor.ClassName == "AHuman" or actor.ClassName == "ACrab") and not actor:IsInGroup("Brains") then
					n = n + 1;
					local d = SceneMan:ShortestDistance(actor.Pos, self.centres[team], SceneMan.SceneWrapsX).Magnitude;
					sum = sum + d;
					if d > 400 then far = far + 1; end
					modes[actor.AIMode] = (modes[actor.AIMode] or 0) + 1;
					if actor.OrderAttack then attack = attack + 1; end
					if actor.OrderHasPost then post = post + 1; end
					if actor:GetWaypointListSize() > 0 then wp = wp + 1; end
					if actor.MOMoveTarget then target = target + 1; end
				end
			end
			local m = "";
			for k, v in pairs(modes) do m = m .. string.format(" m%d=%d", k, v); end
			if team == 0 then
				local follow = nil;
				local best = -1;
				for actor in MovableMan.Actors do
					if actor.Team == 0 and actor.ClassName == "AHuman" then
						local d = SceneMan:ShortestDistance(actor.Pos, self.centres[0], SceneMan.SceneWrapsX).Magnitude;
						self:Log(string.format("   u%d %s at %.0f,%.0f vel %.1f,%.1f mode %d wp %d post %s path %d waiting %s pathEnd %.0f,%.0f lastWp %.0f,%.0f", actor.UniqueID, actor.PresetName, actor.Pos.X, actor.Pos.Y, actor.Vel.X, actor.Vel.Y, actor.AIMode, actor:GetWaypointListSize(), tostring(actor.OrderHasPost), actor.MovePathSize, tostring(actor.IsWaitingOnNewMovePath), actor.MovePathEnd.X, actor.MovePathEnd.Y, actor:GetLastAIWaypoint().X, actor:GetLastAIWaypoint().Y));
						if d > best then best = d; follow = actor; end
					end
				end
				if follow then CameraMan:SetScrollTarget(follow.Pos, 1, 0); end
			end
			self:Log(string.format("team %d: %d units, mean dist %.0f, %d far;%s attack=%d post=%d wp=%d target=%d", team, n, n > 0 and sum / n or 0, far, m, attack, post, wp, target));
		end
	end
end

function BattleModeTestScript:EndScript()
	if self.log then self.log:close(); self.log = nil; end
end
