-- Where a sandbox move order sends a unit, for points clicked inside a bunker. Run beside the AI Bunker script (which builds the sky
-- bunker; with CCCP_BUNKER_ONLY=99 it runs no courses of its own): a soldier stands in the bottom corridor, and is ordered with the
-- sandbox's "Move a side here" to points along the corridor at several heights, from just over the floor to inside the floor and
-- ceiling slabs. Each order is written up as an ORDER line: the point clicked, the waypoint the unit was given, and the end of its route.
-- The bottom corridor's floor is at y 456 and its ceiling's underside at y 408 (modules at y 384 to 480).

function OrderTestScript:StartScript()
	self.timer = Timer();
	self.points = {
		{ x = 1704, y = 452, what = "just over the floor" },
		{ x = 1704, y = 436, what = "mid corridor" },
		{ x = 1704, y = 412, what = "just under the ceiling" },
		{ x = 1704, y = 400, what = "inside the ceiling slab" },
		{ x = 1704, y = 462, what = "inside the floor slab" },
		{ x = 1896, y = 450, what = "further along, over the floor" },
	};
	self.index = 0;
end

function OrderTestScript:UpdateScript()
	local t = self.timer.ElapsedSimTimeMS;
	if not self.unit and t > 5000 then
		self.unit = CreateAHuman("Soldier Light", "Coalition.rte");
		self.unit.Pos = Vector(1560, 436);
		self.unit.Team = 0;
		self.unit.AIMode = Actor.AIMODE_SENTRY;
		MovableMan:AddActor(self.unit);
		self.nextAt = t + 2000;
	end
	if not self.unit or not self.nextAt or t < self.nextAt then
		return;
	end
	if not MovableMan:ValidMO(self.unit) then
		ConsoleMan:PrintString("ORDER unit died");
		self.nextAt = nil;
		return;
	end
	local a = self.unit;
	if self.pending then
		-- The order given a moment ago: its waypoint and route.
		local p = self.pending;
		local wp = a:GetLastAIWaypoint();
		local last = nil;
		local count = 0;
		for node in a.MovePath do
			last = Vector(node.X, node.Y);
			count = count + 1;
		end
		ConsoleMan:PrintString("ORDER " .. p.what .. " clicked " .. p.x .. "," .. p.y .. ": waypoint " .. math.floor(wp.X) .. "," .. math.floor(wp.Y) .. ", route " .. count .. " points ending " .. (last and (math.floor(last.X) .. "," .. math.floor(last.Y)) or "-") .. ", unit at " .. math.floor(a.Pos.X) .. "," .. math.floor(a.Pos.Y) .. ", mode " .. a.AIMode);
		self.pending = nil;
		-- Back to the start for the next one.
		a:ClearAIWaypoints();
		a.AIMode = Actor.AIMODE_SENTRY;
		a.Pos = Vector(1560, 436);
		a.Vel = Vector();
		self.nextAt = t + 1500;
		return;
	end
	self.index = self.index + 1;
	local p = self.points[self.index];
	if not p then
		ConsoleMan:PrintString("ORDER done");
		self.nextAt = nil;
		return;
	end
	SandboxDo("Move a side here", Vector(p.x, p.y), 0, 0, 1, "");
	self.pending = p;
	self.nextAt = t + 2500;
end
