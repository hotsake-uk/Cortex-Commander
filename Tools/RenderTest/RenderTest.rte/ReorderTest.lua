function ReorderTestScript:StartScript()
	self.timer = Timer();
	self.stage = 0;
end

-- A unit is sent somewhere, lets arrive, and sent somewhere else: the second order must be acted on. (After arriving, the AI's mode stayed
-- GOTO while its behaviour was Sentry, and a new order with new waypoints looked like no change, so the unit sat there.)
function ReorderTestScript:UpdateScript()
	local middle = Vector(SceneMan.SceneWidth * 0.5, 0);
	while middle.Y < SceneMan.SceneHeight - 1 and SceneMan:GetTerrMatter(middle.X, middle.Y) == rte.airID do
		middle.Y = middle.Y + 4;
	end
	local t = self.timer.ElapsedRealTimeMS;
	if self.stage == 0 and t > 3000 then
		self.stage = 1;
		SandboxDo("Units", middle + Vector(-100, -30), 0, 0, 1, "Soldier Light");
		SandboxDo("Look around", middle + Vector(0, -40), 0, 0, 1, "");
	elseif self.stage == 1 and t > 4500 then
		self.stage = 2;
		SandboxDo("Move a side here", middle + Vector(60, -20), 0, 0, 1, "");
	elseif self.stage == 2 and t > 14000 then
		self.stage = 3;
		for actor in MovableMan.Actors do
			if actor.Team == 0 and actor.ClassName == "AHuman" then
				self.unit = actor;
				self.before = Vector(actor.Pos.X, actor.Pos.Y);
				ConsoleMan:PrintString("REORDER first order: unit at " .. math.floor(actor.Pos.X) .. " (sent to " .. math.floor(middle.X + 60) .. "), mode " .. actor.AIMode);
			end
		end
		SandboxDo("Move a side here", middle + Vector(-260, -20), 0, 0, 1, "");
	elseif self.stage == 3 and t > 26000 then
		self.stage = 4;
		if self.unit and MovableMan:IsActor(self.unit) then
			local moved = self.before.X - self.unit.Pos.X;
			ConsoleMan:PrintString("REORDER second order: unit at " .. math.floor(self.unit.Pos.X) .. " (sent to " .. math.floor(middle.X - 260) .. "), moved " .. math.floor(moved) .. " px, mode " .. self.unit.AIMode .. (moved > 150 and " OK" or " STUCK"));
		end
	end
end
