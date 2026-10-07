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

-- CCCP_ORDER_BUMPS=1 instead: a concrete bump of 10, 16 and 22 px stands on the bottom corridor's floor in each of its plain stretches
-- (x 1704, 1896, 2088), and the route a soldier is given from one side of each to the other is written up as a BUMP line: its cost and
-- points, and whether it goes over or round.
function OrderTestScript:Bumps(t)
	if not self.bumpsPlaced then
		self.bumpsPlaced = true;
		self.bumps = { { x = 1704, h = 10 }, { x = 1896, h = 16 }, { x = 2088, h = 22 } };
		for _, b in ipairs(self.bumps) do
			local block = CreateTerrainObject("Concrete Block", "Base.rte");
			block.Pos = Vector(b.x - 12, 456 - b.h);
			SceneMan:AddSceneObject(block);
		end
		self.unit = CreateAHuman("Soldier Light", "Coalition.rte");
		self.unit.Pos = Vector(1560, 436);
		self.unit.Team = 0;
		self.unit.AIMode = Actor.AIMODE_SENTRY;
		MovableMan:AddActor(self.unit);
		self.bumpsAt = t + 3000;
		return;
	end
	if self.bumpsAt and t > self.bumpsAt then
		self.bumpsAt = nil;
		for _, b in ipairs(self.bumps) do
			local from = Vector(b.x - 60, 445);
			local to = Vector(b.x + 60, 445);
			local cost = SceneMan.Scene:CalculatePathForActor(self.unit, from, to, Activity.TEAM_1);
			local nodes = "";
			local lowest = 0;
			for node in SceneMan.Scene:GetScenePath() do
				nodes = nodes .. " " .. math.floor(node.X) .. "," .. math.floor(node.Y);
				lowest = math.max(lowest, 456 - node.Y);
			end
			ConsoleMan:PrintString("BUMP " .. b.h .. " px at " .. b.x .. ": cost " .. tostring(cost) .. ", " .. (lowest > 100 and "goes round" or "goes over") .. ", points" .. nodes);
			for _, gy in ipairs({ 420, 444 }) do
				for gx = b.x - 36, b.x + 36, 24 do
					ConsoleMan:PrintString("BUMP grid " .. b.h .. ": " .. SceneMan.Scene:DescribePathNodeAt(Vector(gx, gy)));
				end
			end
		end
		ConsoleMan:PrintString("BUMP unit height " .. math.floor(self.unit.Height) .. ", radius " .. math.floor(self.unit.IndividualRadius));
		self.cross = 0;
		self.crossAt = t;
	end
	-- Then across each, for real: the unit put down left of it and sent to the right of it, 15 s each.
	if self.crossAt and t >= self.crossAt then
		local a = self.unit;
		if self.crossing then
			local b = self.bumps[self.cross];
			local over = a.Pos.X > b.x + 20 and math.abs(a.Pos.Y - 445) < 30;
			if over or t - self.crossing > 15000 then
				ConsoleMan:PrintString("BUMP cross " .. b.h .. " px: " .. (over and ("over in " .. math.floor((t - self.crossing) / 100) / 10 .. " s") or ("not over, at " .. math.floor(a.Pos.X) .. "," .. math.floor(a.Pos.Y))));
				self.crossing = nil;
			else
				return;
			end
		end
		self.cross = self.cross + 1;
		local b = self.bumps[self.cross];
		if not b then
			self.crossAt = nil;
			ConsoleMan:PrintString("BUMP done");
			return;
		end
		a:ClearAIWaypoints();
		a.Pos = Vector(b.x - 60, 436);
		a.Vel = Vector();
		a:AddAISceneWaypoint(Vector(b.x + 60, 452));
		a.AIMode = Actor.AIMODE_GOTO;
		self.crossing = t;
	end
end

function OrderTestScript:UpdateScript()
	local t = self.timer.ElapsedSimTimeMS;
	if os and os.getenv and os.getenv("CCCP_ORDER_BUMPS") == "1" then
		if t > 5000 then
			self:Bumps(t);
		end
		return;
	end
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
