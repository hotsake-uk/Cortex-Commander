-- The movement gym (LM-12): short courses on concrete pads in the open sky over the sandbox map, one per movement item, each written up
-- as a MOVEGYM line: arrived (and in how long, and how long it stood still) or gave up (and how far short). "MOVEGYM done" at the end.
-- A course is a pad of blocks with something on it between the start and the goal: a low beam to duck under (LM-1), a lump to step,
-- leap or pull over (LM-5), a friend lying or standing in the way, or two units walking at each other (LM-2), and the crab's leap over a
-- lump and a gap (LM-8). Some soldiers have their jetpack taken off, so the legs have to do it.
-- Uses only the same calls as the recovery gym, so it runs on any build that has the courses' features.

local PadY = 380; -- The pads' top edge, well above the hills (which top out at y 559). (A Concrete Block's Pos is its top-left corner: its
-- BitmapOffset is 0,0, which skips TerrainObject's centring.)
local Block = 24;
local PadTop = PadY;

local Cases = {
	{ name = "low beam (duck under)", ceiling = 34 },
	{ name = "knee lump 10 px, no jet", lump = 10, noJet = true },
	{ name = "knee lump 24 px", lump = 24 },
	{ name = "friend lying in the way, no jet", friend = "prone", noJet = true },
	{ name = "two walking at each other", headOn = true },
	{ name = "friend standing in the way", friend = "standing" },
	{ name = "crab over a lump", lump = 10, crab = true },
	{ name = "crab across a gap", gap = 48, crab = true },
};

function MoveGymScript:StartScript()
	self.timer = Timer();
	self.runners = {};
end

function MoveGymScript:PlaceBlock(x, y)
	local block = CreateTerrainObject("Concrete Block", "Base.rte");
	if block then
		block.Pos = Vector(x, y);
		SceneMan:AddSceneObject(block);
	end
end

-- A pad of so many blocks from x along, leaving out the blocks in [skipFrom, skipTo) for a gap.
function MoveGymScript:Pad(x, blocks, skipFrom, skipTo)
	for i = 0, blocks - 1 do
		local bx = x + i * Block;
		if not (skipFrom and bx >= skipFrom and bx < skipTo) then
			self:PlaceBlock(bx, PadY);
		end
	end
end

function MoveGymScript:Unit(case, x, crab)
	local actor = crab and CreateACrab("Dreadnought", "Dummy.rte") or CreateAHuman("Soldier Light", "Coalition.rte");
	actor.Pos = Vector(x, PadTop - actor.Height * 0.5 - 4);
	actor.Team = 0;
	actor.AIMode = Actor.AIMODE_SENTRY;
	MovableMan:AddActor(actor);
	if case.noJet and actor.Jetpack then
		pcall(function() ToAHuman(actor):RemoveAttachable(actor.Jetpack, false, false); end);
	end
	return actor;
end

function MoveGymScript:UpdateScript()
	local t = self.timer.ElapsedSimTimeMS;
	if not self.built and t > 2000 then
		self.built = true;
		-- A pad of 14 blocks (336 px) per course, 440 px apart. (A gap is whole blocks left out: 48 px is two.)
		for i, case in ipairs(Cases) do
			local x = 100 + (i - 1) * 440;
			case.padX = x;
			if case.gap then
				-- The gap halfway along.
				self:Pad(x, 14, x + 168, x + 168 + case.gap);
			else
				self:Pad(x, 14);
			end
			if case.ceiling then
				-- A low beam over the middle with so much head room under it, and a wall up from it, so the way is under, not over.
				local bottom = PadTop - case.ceiling;
				for bx = x + 120, x + 216, Block do
					self:PlaceBlock(bx, bottom - Block);
				end
				for k = 1, 4 do
					self:PlaceBlock(x + 168, bottom - Block - k * Block);
				end
			end
			if case.lump then
				-- One block sunk into the pad so only so much of it stands up.
				self:PlaceBlock(x + 168, PadTop - case.lump);
			end
		end
		SandboxDo("Look around", Vector(900, PadY), 0, 0, 1, "");
	end
	if self.built and not self.spawned and t > 3500 then
		self.spawned = true;
		for _, case in ipairs(Cases) do
			local x = case.padX;
			local actor = self:Unit(case, x + 40, case.crab);
			table.insert(self.runners, { actor = actor, case = case, name = case.name, goal = Vector(x + 300, PadTop), start = t, sent = false, done = false, still = 0, lastPos = Vector(actor.Pos.X, actor.Pos.Y) });
			if case.headOn then
				local other = self:Unit(case, x + 300, false);
				table.insert(self.runners, { actor = other, case = case, name = case.name .. " (the other)", goal = Vector(x + 40, PadTop), start = t, sent = false, done = false, still = 0, lastPos = Vector(other.Pos.X, other.Pos.Y) });
			end
			if case.friend then
				-- A friend in the middle of the pad, told to stay, lying down or standing.
				local friend = self:Unit({}, x + 170, false);
				friend:SetNumberValue("MoveGymObstacle", 1);
				case.friendActor = friend;
			end
		end
	end
	if not self.spawned then
		return;
	end
	-- The friend in the way: held lying down (or standing) where it is.
	for _, case in ipairs(Cases) do
		local friend = case.friendActor;
		if friend and MovableMan:ValidMO(friend) and IsAHuman(friend) then
			local human = ToAHuman(friend);
			if case.friend == "prone" then
				human:SetAIStance(2, 500); -- (The engine's motor holds the stance; see AHuman::SetAIStance.)
			end
		end
	end
	local allDone = true;
	for _, r in ipairs(self.runners) do
		if not r.done then
			allDone = false;
			local a = r.actor;
			if not MovableMan:ValidMO(a) then
				r.done = true;
				ConsoleMan:PrintString("MOVEGYM " .. r.name .. ": died");
			elseif not r.sent then
				-- A moment to land and, for the friend, to lie down first.
				if t - r.start > 1500 then
					r.sent = true;
					r.start = t;
					a:ClearAIWaypoints();
					a:AddAISceneWaypoint(r.goal + Vector(0, -a.Height * 0.5));
					a.AIMode = Actor.AIMODE_GOTO;
				end
			else
				local offset = SceneMan:ShortestDistance(a.Pos, r.goal, false);
				if math.abs(offset.X) < 30 and math.abs(offset.Y) < a.Height * 0.9 then
					r.done = true;
					ConsoleMan:PrintString("MOVEGYM " .. r.name .. ": arrived in " .. math.floor((t - r.start) / 100) / 10 .. " s, stood still " .. r.still .. " s");
				elseif t - r.start > 30000 then
					r.done = true;
					ConsoleMan:PrintString("MOVEGYM " .. r.name .. ": GAVE UP after 30 s, " .. math.floor(math.abs(offset.X)) .. " px short, stood still " .. r.still .. " s, at " .. math.floor(a.Pos.X) .. "," .. math.floor(a.Pos.Y));
				elseif not r.lastTick or t - r.lastTick > 1000 then
					r.lastTick = t;
					if SceneMan:ShortestDistance(a.Pos, r.lastPos, false).Magnitude < 4 then
						r.still = r.still + 1;
					end
					r.lastPos = Vector(a.Pos.X, a.Pos.Y);
				end
			end
		end
	end
	if allDone and not self.finished then
		self.finished = true;
		ConsoleMan:PrintString("MOVEGYM done");
	end
end
