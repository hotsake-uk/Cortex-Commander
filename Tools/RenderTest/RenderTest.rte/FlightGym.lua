-- The flight gym: how well the AI flies its jetpack, with nothing else in the way. Concrete pads hang in the open sky over the sandbox
-- map, in pairs; a soldier stands on each start pad and is sent to its pair's target pad, all at once. Each flight is written up as a
-- FLIGHT line: whether it landed on the pad, how long it took, the fuel it burned, how far it went past the pad sideways and above it,
-- how many times it reversed its sideways speed in the air, and whether it fell.
-- The courses come in four grades (easy, medium, hard, precision) and two batches, one game each (CCCP_FLIGHT_BATCH=1 or 2), so the
-- pads fit the map without crowding. CCCP_FLIGHT_ONLY=n runs only course n of the batch; CCCP_FLIGHT_TRACE=n gives course n's unit the
-- AITrace value (AITRACE lines in the log).
-- CCCP_FLIGHT_PROOF=1: no AI. A scripted pilot, with the controller's keys pressed by this script, flies each course over and over with
-- different settings until one lands, and writes PROOF lines: which courses can be flown at all, and how. A course no setting lands is
-- marked as such, so a failure in the gym proper is the AI's and not the course's.
-- Uses only calls the original AI's build has too, so the same file measures both builds (Tools/RenderTest/Bench.ps1 -Suites Flight).

local AllCourses = {
	-- Batch 1.
	{ batch = 1, grade = "easy", dx = 240, dy = 0, width = 5, name = "level 240" },
	{ batch = 1, grade = "easy", dx = 240, dy = -120, width = 5, name = "up 120 across 240" },
	{ batch = 1, grade = "medium", dx = 480, dy = 0, width = 5, name = "level 480" },
	{ batch = 1, grade = "medium", dx = 216, dy = -240, width = 5, name = "up 240 across 216" },
	{ batch = 1, grade = "medium", dx = 360, dy = 168, width = 5, name = "down 168 across 360" },
	{ batch = 1, grade = "hard", dx = 480, dy = 360, width = 4, name = "down 360 across 480" },
	{ batch = 1, grade = "precision", dx = 288, dy = -96, width = 2, name = "narrow ledge up 96 across 288" },
	{ batch = 1, grade = "precision", dx = 240, dy = 216, width = 2, name = "narrow ledge down 216 across 240" },
	{ batch = 1, grade = "run-up", dx = 312, dy = -96, width = 4, runup = true, name = "run then up 96 across 312" },
	-- Walks: a floor all the way, in steps a soldier climbs or steps down. No jet is needed at all; the fuel burned is the jet used for nothing.
	{ batch = 1, grade = "walk", dx = 336, dy = -80, width = 4, steps = 3, name = "walk up three 20 px steps" },
	-- Batch 2.
	{ batch = 2, grade = "medium", dx = -264, dy = -72, width = 3, name = "left up 72 across 264" },
	{ batch = 2, grade = "hard", dx = 600, dy = 0, width = 5, name = "level 600" },
	-- (A level gap of 720 px was tried and no setting of the scripted pilot made it: not a fair course.)
	{ batch = 2, grade = "hard", dx = 168, dy = -336, width = 5, name = "up 336 across 168" },
	{ batch = 2, grade = "hard", dx = -456, dy = -216, width = 5, name = "up 216 across 456 left" },
	{ batch = 2, grade = "hard", dx = 360, dy = -288, width = 4, name = "up 288 across 360" },
	{ batch = 2, grade = "precision", dx = 96, dy = -144, width = 2, name = "narrow ledge up 144 across 96" },
	{ batch = 2, grade = "run-up", dx = 384, dy = 0, width = 5, runup = true, name = "run then level 384" },
	{ batch = 2, grade = "run-up", dx = 336, dy = 120, width = 4, runup = true, name = "run then down 120 across 336" },
	{ batch = 2, grade = "walk", dx = 336, dy = 80, width = 4, steps = 3, name = "walk down three 20 px steps" },
};

function FlightGymScript:StartScript()
	self.timer = Timer();
	self.runners = {};
	local env = function(name) return os and os.getenv and os.getenv(name) or nil; end
	self.batch = tonumber(env("CCCP_FLIGHT_BATCH") or "") or 1;
	self.only = tonumber(env("CCCP_FLIGHT_ONLY") or "");
	self.traceCourse = tonumber(env("CCCP_FLIGHT_TRACE") or "");
	self.proof = env("CCCP_FLIGHT_PROOF") == "1";
	self.courses = {};
	for _, course in ipairs(AllCourses) do
		if course.batch == self.batch then
			table.insert(self.courses, course);
		end
	end
	self.startWidth = 5;
	-- The scripted pilot's settings, tried in turn: how far over the pad's top the feet are carried while crossing (px), the top
	-- sideways speed (m/s), and the braking the sideways speed is planned to stop with (px/s^2).
	self.proofSettings = {};
	for _, margin in ipairs({ 12, 36, 72 }) do
		for _, speed in ipairs({ 3, 5, 7 }) do
			for _, brake in ipairs({ 120, 240, 480 }) do
				table.insert(self.proofSettings, { margin = margin, speed = speed, brake = brake });
			end
		end
	end
end

function FlightGymScript:Pad(x, y, blocks)
	for i = 0, blocks - 1 do
		local block = CreateTerrainObject("Concrete Block", "Base.rte");
		if block then
			block.Pos = Vector(x + i * 24, y);
			SceneMan:AddSceneObject(block);
		end
	end
end

-- Lays the batch's courses out in two rows, left to right, a gap between them. Row 1 hangs at y 380 and row 2 at y 860; nothing goes
-- over the hills in the middle of the map (x 1350 to 2250, tops up to y 559) unless it stays above y 500, and row 2 only takes courses
-- that don't go down (the ground elsewhere is below y 950).
function FlightGymScript:Build()
	local nextX = { 48, 48 };
	local rowY = { 380, 860 };
	for i, course in ipairs(self.courses) do
		local left = math.min(0, course.dx);
		course.startBlocks = course.runup and 14 or self.startWidth;
		local padEndOffset = course.dx >= 0 and (course.startBlocks - self.startWidth) * 24 or 0;
		local right = math.max(course.startBlocks * 24, padEndOffset + course.dx + course.width * 24);
		local span = right - left;
		local row = (course.dy > 0 or nextX[2] + span > 3550) and 1 or 2;
		if row == 1 and nextX[1] + span > 3550 then
			row = 2;
		end
		local x = nextX[row];
		local deep = rowY[row] + math.max(0, course.dy) + 24 > 500;
		if deep and x + span > 1350 and x < 2250 then
			x = 2250;
		end
		nextX[row] = x + span + 160;
		local startX = x - left;
		local startY = rowY[row];
		course.startPad = Vector(startX, startY);
		-- (The target's offset is from the start pad's end on the side it lies, so a long run-up pad doesn't reach under it.)
		local padEnd = course.dx >= 0 and startX + (course.startBlocks - self.startWidth) * 24 or startX;
		course.targetPad = Vector(padEnd + course.dx, startY + course.dy);
		course.run = self.only == nil or self.only == i;
		if course.run then
			self:Pad(startX, startY, course.startBlocks);
			self:Pad(course.targetPad.X, course.targetPad.Y, course.width);
			-- A walk's steps: three blocks each, from the start pad's end to the target pad, rising or falling evenly. (A block's top sits
			-- where its Pos is; a step's riser is the difference, filled down to the next step by the block's own 24 px.)
			if course.steps then
				local startEnd = startX + course.startBlocks * 24;
				for k = 1, course.steps do
					local stepY = startY + math.floor(course.dy * k / (course.steps + 1));
					self:Pad(startEnd + (k - 1) * 72, stepY, 3);
				end
			end
		end
	end
end

function FlightGymScript:Spawn()
	local t = self.timer.ElapsedSimTimeMS;
	for i, course in ipairs(self.courses) do
		if course.run then
			local actor = CreateAHuman("Soldier Light", "Coalition.rte");
			actor:AddInventoryItem(CreateHDFirearm("Assault Rifle", "Coalition.rte"));
			local h = actor.Height;
			-- Standing on the middle of the start pad; the goal is over the middle of the target pad, a fifth of a body up.
			-- (On a run-up course, at the far end of the long pad from the gap, so it is running when it leaves.)
			local startX = course.startPad.X + self.startWidth * 12;
			if course.runup then
				startX = course.dx >= 0 and (course.startPad.X + 24) or (course.startPad.X + course.startBlocks * 24 - 24);
			end
			local startPos = Vector(startX, course.startPad.Y - h * 0.5);
			actor.Pos = Vector(startPos.X, startPos.Y);
			actor.Team = 0;
			actor.AIMode = Actor.AIMODE_SENTRY;
			if self.traceCourse == i then
				actor:SetNumberValue("AITrace", 1);
			end
			MovableMan:AddActor(actor);
			local goal = Vector(course.targetPad.X + course.width * 12, course.targetPad.Y - h * 0.2);
			table.insert(self.runners, {
				actor = actor, course = course, name = course.grade .. ": " .. course.name, goal = goal, start = t, startPos = startPos,
				padLeft = course.targetPad.X, padRight = course.targetPad.X + course.width * 24, padTop = course.targetPad.Y,
				dirX = course.dx > 0 and 1 or (course.dx < 0 and -1 or 0), sent = false, done = false, trial = 0,
			});
		end
	end
end

-- Fresh measures for a flight.
function FlightGymScript:ResetMeasures(r)
	r.overX, r.overUp, r.reversals, r.lastSignX, r.fuel, r.fell, r.onPadMS = 0, 0, 0, 0, 0, false, 0;
	r.pulses, r.lastLit = 0, false;
	r.lastFuel = r.actor.Jetpack and r.actor.Jetpack.JetTimeLeft or 0;
end

-- One tick's measures. @return Whether the unit has landed (on the pad, still, half a second) and the stats so far as text.
function FlightGymScript:Measure(r)
	local a = r.actor;
	local dt = TimerMan.DeltaTimeMS;
	if a.Jetpack then
		local lit = a.Jetpack:IsEmitting();
		if lit and not r.lastLit then
			r.pulses = r.pulses + 1;
		end
		r.lastLit = lit;
		local fuel = a.Jetpack.JetTimeLeft;
		if r.lastFuel and fuel < r.lastFuel then
			r.fuel = r.fuel + (r.lastFuel - fuel);
		end
		r.lastFuel = fuel;
	end
	local h = a.Height;
	-- In the air: nothing under the feet within a third of a body.
	local airborne = not SceneMan:CastStrengthRay(a.Pos, Vector(0, h * 0.83), 5, Vector(), 3, rte.grassID, true);
	-- Across from the pad's middle (the map wraps, so measured the short way round).
	local padHalf = (r.padRight - r.padLeft) * 0.5;
	local fromPad = SceneMan:ShortestDistance(Vector(r.padLeft + padHalf, r.padTop), a.Pos, false).X;
	if airborne then
		-- Past the pad sideways: how far beyond its far edge the body went.
		if r.dirX ~= 0 then
			r.overX = math.max(r.overX, fromPad * r.dirX - padHalf);
		end
		-- Above the pad: how far the feet went over its top, more than a body (a hop onto a pad is a body's height at most).
		r.overUp = math.max(r.overUp, (r.padTop - (a.Pos.Y + h * 0.5)) - h);
		-- Sideways reversals: the sideways speed changing sign at more than 1.5 m/s.
		local sign = a.Vel.X > 1.5 and 1 or (a.Vel.X < -1.5 and -1 or 0);
		if sign ~= 0 then
			if r.lastSignX ~= 0 and sign ~= r.lastSignX then
				r.reversals = r.reversals + 1;
			end
			r.lastSignX = sign;
		end
	end
	-- Fallen: well under both pads.
	if a.Pos.Y > math.max(r.course.startPad.Y, r.padTop) + 200 then
		r.fell = true;
	end
	local onPad = math.abs(fromPad) <= padHalf + 4 and math.abs(a.Pos.Y + h * 0.5 - r.padTop) < h * 0.35 and not airborne and a.Vel.Magnitude < 2;
	r.onPadMS = onPad and r.onPadMS + dt or 0;
	local stats = ", fuel " .. math.floor(r.fuel) .. " ms, over " .. math.floor(math.max(0, r.overX)) .. " px, above " .. math.floor(math.max(0, r.overUp)) .. " px, reversals " .. r.reversals .. ", pulses " .. r.pulses .. (r.fell and ", fell" or "");
	return r.onPadMS >= 500, stats;
end

-- The scripted pilot: the keys for one tick, flying towards the pad with one setting. Feet carried to a margin over the pad's top,
-- then across at up to the setting's speed, slowing to stop over the pad with the setting's braking, then down onto it.
function FlightGymScript:Pilot(r, setting)
	local a = r.actor;
	local ctrl = a:GetController();
	local ppm = GetPPM();
	local g = SceneMan.GlobalAcc.Y * ppm;
	local h = a.Height;
	local feetY = a.Pos.Y + h * 0.5;
	local padHalf = (r.padRight - r.padLeft) * 0.5;
	local dx = SceneMan:ShortestDistance(a.Pos, Vector(r.padLeft + padHalf, r.padTop), false).X;
	local overPad = math.abs(dx) < padHalf - 6;
	-- Up and down: the speed wanted to the height wanted (down positive, m/s).
	local wantFeetY = overPad and (r.padTop + 4) or (r.padTop - setting.margin);
	local toGo = feetY - wantFeetY;
	local wantVy;
	if toGo > 0 then
		wantVy = -math.min(9, math.sqrt(2 * g * toGo) / ppm);
	else
		wantVy = math.min(overPad and 2.5 or 6, math.sqrt(2 * g * 0.6 * -toGo) / ppm);
	end
	local jet = a.Vel.Y > wantVy;
	-- Across: not until the feet are over the pad's top (or nearly, rising), so the pad's edge isn't flown into from below.
	local wantVx = 0;
	if feetY < r.padTop + 8 or toGo < 0 then
		local stopRoom = math.max(0, math.abs(dx) - padHalf * 0.3);
		wantVx = (dx > 0 and 1 or -1) * math.min(setting.speed, math.sqrt(2 * setting.brake * stopRoom) / ppm);
	end
	local off = wantVx - a.Vel.X;
	local lat = off > 0.4 and 1 or (off < -0.4 and -1 or 0);
	if lat ~= 0 and math.abs(off) > 1 and a.Vel.Y > wantVy - 2 then
		jet = true;
	end
	ctrl:SetState(Controller.BODY_JUMP, jet);
	ctrl:SetState(Controller.MOVE_RIGHT, lat > 0);
	ctrl:SetState(Controller.MOVE_LEFT, lat < 0);
	a:SetAimAngle(lat ~= 0 and 0 or math.pi * 0.5);
end

function FlightGymScript:UpdateProof(r, t)
	local a = r.actor;
	if not r.trialStart then
		-- A trial: back on the start pad, still, a full tank, a moment to settle.
		r.trial = r.trial + 1;
		if r.trial > #self.proofSettings then
			r.done = true;
			ConsoleMan:PrintString("PROOF " .. r.name .. ": not flown in " .. #self.proofSettings .. " tries");
			return;
		end
		a.Pos = Vector(r.startPos.X, r.startPos.Y);
		a.Vel = Vector();
		if a.Jetpack then
			a.Jetpack.JetTimeLeft = a.Jetpack.JetTimeTotal;
		end
		a:GetController().InputMode = Controller.CIM_DISABLED;
		r.trialStart = t;
		r.flying = false;
		return;
	end
	if not r.flying then
		if t - r.trialStart > 700 then
			r.flying = true;
			r.flyStart = t;
			self:ResetMeasures(r);
		end
		return;
	end
	local setting = self.proofSettings[r.trial];
	self:Pilot(r, setting);
	local landed, stats = self:Measure(r);
	if landed then
		r.done = true;
		local ctrl = a:GetController();
		ctrl:SetState(Controller.BODY_JUMP, false);
		ConsoleMan:PrintString("PROOF " .. r.name .. ": landed on try " .. r.trial .. " in " .. math.floor((t - r.flyStart) / 100) / 10 .. " s (margin " .. setting.margin .. ", speed " .. setting.speed .. ", brake " .. setting.brake .. ")" .. stats);
	elseif r.fell or t - r.flyStart > 12000 then
		r.trialStart = nil;
	end
end

-- CCCP_FLIGHT_STICK=1: which way the analog stick's X pushes the jet. Two soldiers hang in the air, one facing right and one left, the AI
-- off; each jets for a second with the stick at (0.3, -1), and the sideways speed it gained is written up as a STICK line.
function FlightGymScript:StickTest(t)
	if not self.stick then
		self.stick = {};
		for i, flip in ipairs({ false, true }) do
			local a = CreateAHuman("Soldier Light", "Coalition.rte");
			a.Pos = Vector(600 + i * 200, 300);
			a.Team = 0;
			a.HFlipped = flip;
			a.AIMode = Actor.AIMODE_SENTRY;
			MovableMan:AddActor(a);
			table.insert(self.stick, { actor = a, flip = flip });
		end
		self.stickStart = t + 500;
		return;
	end
	for _, entry in ipairs(self.stick) do
		local a = entry.actor;
		if MovableMan:ValidMO(a) then
			local ctrl = a:GetController();
			ctrl.InputMode = Controller.CIM_DISABLED;
			if t < self.stickStart then
				a.Vel = Vector();
				a.Pos = Vector(a.Pos.X, 300);
				a.HFlipped = entry.flip;
			elseif t < self.stickStart + 1000 then
				ctrl:SetState(Controller.BODY_JUMP, true);
				ctrl.AnalogMove = Vector(0.3, -1);
			elseif not entry.done then
				entry.done = true;
				ConsoleMan:PrintString("STICK facing " .. (entry.flip and "left" or "right") .. " (now " .. (a.HFlipped and "left" or "right") .. "): stick X +0.3 gave sideways speed " .. math.floor(a.Vel.X * 100) / 100 .. " m/s");
			end
		end
	end
end

function FlightGymScript:UpdateScript()
	local t = self.timer.ElapsedSimTimeMS;
	if os and os.getenv and os.getenv("CCCP_FLIGHT_STICK") == "1" then
		if t > 3000 then
			self:StickTest(t);
		end
		return;
	end
	if not self.built and t > 2000 then
		self.built = true;
		self:Build();
		SandboxDo("Look around", Vector(700, 400), 0, 0, 1, "");
	end
	if self.built and not self.spawned and t > 3500 then
		self.spawned = true;
		self:Spawn();
	end
	if not self.spawned then
		return;
	end
	local allDone = true;
	for _, r in ipairs(self.runners) do
		if not r.done then
			allDone = false;
			local a = r.actor;
			if not MovableMan:ValidMO(a) then
				r.done = true;
				ConsoleMan:PrintString((self.proof and "PROOF " or "FLIGHT ") .. r.name .. ": died");
			elseif self.proof then
				if t - r.start > 1500 then
					self:UpdateProof(r, t);
				end
			elseif not r.sent then
				if t - r.start > 1500 then
					r.sent = true;
					r.sentAt = t;
					self:ResetMeasures(r);
					a:ClearAIWaypoints();
					a:AddAISceneWaypoint(r.goal);
					a.AIMode = Actor.AIMODE_GOTO;
				end
			else
				local landed, stats = self:Measure(r);
				local seconds = math.floor((t - r.sentAt) / 100) / 10;
				if landed then
					r.done = true;
					ConsoleMan:PrintString("FLIGHT " .. r.name .. ": landed in " .. seconds .. " s" .. stats);
				elseif t - r.sentAt > 30000 then
					r.done = true;
					local toGoal = SceneMan:ShortestDistance(a.Pos, r.goal, false);
					ConsoleMan:PrintString("FLIGHT " .. r.name .. ": GAVE UP after 30 s, at " .. math.floor(a.Pos.X) .. "," .. math.floor(a.Pos.Y) .. ", " .. math.floor(toGoal.Magnitude) .. " px short" .. stats);
				end
			end
		end
	end
	if allDone and not self.finished then
		self.finished = true;
		ConsoleMan:PrintString((self.proof and "PROOF" or "FLIGHT") .. " done");
	end
end
