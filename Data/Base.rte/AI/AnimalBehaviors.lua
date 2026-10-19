-- Animals (livestock, the "Actors - Livestock" group) going about their lives when nobody is telling them what to do: they get hungry, thirsty
-- and tired, and go and eat the grass and bushes they can reach (taking the plants out of the ground, to grow back a few minutes later:
-- SceneMan:FindFood, EatFood, TerrainGrazing in the engine), drink at the edge of water, lie down for a rest, wander about where they were left
-- and keep with their herd. Chickens hop about, flap up onto ledges and flutter down from them.
--
-- Their orders (number values on the unit, set by the sandbox's animal orders):
--   AnimalOrder 0 "Roam free" (and no value): wander and graze round where they are, moving on to where the food is (up to 20 m away).
--   AnimalOrder 1 "Penned": roam, graze, drink and rest only inside the pen, a polygon of AnimalPenCount points (AnimalPenX1, AnimalPenY1, ...),
--     AnimalPenStamp changing whenever it is set. A penned animal goes back in when it finds itself outside (after running from danger), except
--     a chicken that has been in and flapped out over a low wall: that one is loose, and roams free.
--   AnimalOrder 2 "Stay": stand where they were told, grazing what they can reach and drinking only if the water is right there.
-- A move, a guard (follow) or any other order the player gives is carried out as for any unit, and they take up their own lives again once
-- it's done, round where it left them. Fleeing danger (SharedBehaviors.FleeUpdate) comes first, always.

AnimalBehaviors = {};

AnimalBehaviors.ROAM = 0;
AnimalBehaviors.PENNED = 1;
AnimalBehaviors.STAY = 2;

-- Diet bits (TerrainGrazing::Diet): what counts as food.
local GRASS, BUSHES, TOUGH = 1, 2, 4;

-- Each kind: what it eats (diet), how many plant pixels a bite takes (bite, from a circle of biteRadius px) and a full belly takes (fill), how
-- many seconds it takes to get hungry, thirsty and tired from nothing, and which herd it keeps with (nil: it keeps to itself).
local Kinds = {
	Chicken = { diet = GRASS, bite = 1, biteRadius = 3, fill = 14, hungerSecs = 200, thirstSecs = 300, tiredSecs = 360, herd = "Chicken", chicken = true },
	Pig = { diet = GRASS + BUSHES, bite = 3, biteRadius = 5, fill = 50, hungerSecs = 220, thirstSecs = 300, tiredSecs = 300 },
	Sheep = { diet = GRASS + BUSHES, bite = 3, biteRadius = 5, fill = 50, hungerSecs = 260, thirstSecs = 360, tiredSecs = 420, herd = "Sheep" },
	Goat = { diet = GRASS + BUSHES + TOUGH, bite = 3, biteRadius = 5, fill = 45, hungerSecs = 260, thirstSecs = 360, tiredSecs = 420, herd = "Goat" },
	Cow = { diet = GRASS + BUSHES, bite = 5, biteRadius = 7, fill = 90, hungerSecs = 300, thirstSecs = 360, tiredSecs = 420, herd = "Cattle" },
	Bull = { diet = GRASS + BUSHES, bite = 5, biteRadius = 7, fill = 100, hungerSecs = 300, thirstSecs = 360, tiredSecs = 420, herd = "Cattle" },
};
-- (A mod's animal in the livestock group: a grazer, on its own.)
local DefaultKind = { diet = GRASS + BUSHES, bite = 3, biteRadius = 5, fill = 50, hungerSecs = 260, thirstSecs = 360, tiredSecs = 420 };

local FOOD_REACH = 400; -- How far a roaming animal looks for food: 20 m.
local WATER_REACH = 500; -- And for water: 25 m.
local WANDER_REACH = 90; -- How far it wanders round its home: 4.5 m either way.
local STAY_REACH = 25; -- Told to stay: barely a step.

-- The animal's own state, made the first time it is looked at.
local function NewState(Owner)
	local kind = Kinds[Owner.PresetName] or DefaultKind;
	return {
		kind = kind,
		-- (Needs start part way, at random, so a herd placed together doesn't all eat, drink and sleep at once.)
		hunger = 0.2 + math.random() * 0.5,
		thirst = math.random() * 0.5,
		tired = math.random() * 0.6,
		act = "idle",
		NeedsTimer = Timer(),
		CheckTimer = Timer(),
		ActTimer = Timer(),
		BiteTimer = Timer(),
		WaitMS = math.random(1000, 4000),
		serial = Owner.AIOrderSerial,
		badSpots = {},
		pose = { front = 0, back = 0 },
		FlapTimer = Timer(),
		flapWaitMS = math.random(8000, 20000),
	};
end

local function Order(Owner)
	if Owner:NumberValueExists("AnimalOrder") then
		return math.floor(Owner:GetNumberValue("AnimalOrder"));
	end
	return AnimalBehaviors.ROAM;
end

-- Lets a pen go: the animal roams free from where it is.
local function Unpen(A, Owner)
	Owner:SetNumberValue("AnimalOrder", AnimalBehaviors.ROAM);
	local count = Owner:NumberValueExists("AnimalPenCount") and math.floor(Owner:GetNumberValue("AnimalPenCount")) or 0;
	for i = 1, count do
		Owner:RemoveNumberValue("AnimalPenX" .. i);
		Owner:RemoveNumberValue("AnimalPenY" .. i);
	end
	Owner:RemoveNumberValue("AnimalPenCount");
	Owner:RemoveNumberValue("AnimalPenStamp");
	A.Pen = nil;
	A.Home = Vector(Owner.Pos.X, Owner.Pos.Y);
end

-- The pen's points, read again whenever it is set anew.
local function Pen(A, Owner)
	if not Owner:NumberValueExists("AnimalPenCount") then
		A.Pen = nil;
		return nil;
	end
	local stamp = Owner:NumberValueExists("AnimalPenStamp") and Owner:GetNumberValue("AnimalPenStamp") or 0;
	if A.Pen and A.Pen.stamp == stamp then
		return A.Pen;
	end
	local Pen = { stamp = stamp, points = {} };
	local count = math.floor(Owner:GetNumberValue("AnimalPenCount"));
	for i = 1, count do
		table.insert(Pen.points, Vector(Owner:GetNumberValue("AnimalPenX" .. i), Owner:GetNumberValue("AnimalPenY" .. i)));
	end
	if #Pen.points < 3 then
		A.Pen = nil;
		return nil;
	end
	-- Its middle and box, for picking spots in it.
	local First = Pen.points[1];
	local minX, minY, maxX, maxY, sumX, sumY = 0, 0, 0, 0, 0, 0;
	Pen.rel = {};
	for i, Point in ipairs(Pen.points) do
		local Rel = SceneMan:ShortestDistance(First, Point, false);
		Pen.rel[i] = Rel;
		minX, minY = math.min(minX, Rel.X), math.min(minY, Rel.Y);
		maxX, maxY = math.max(maxX, Rel.X), math.max(maxY, Rel.Y);
		sumX, sumY = sumX + Rel.X, sumY + Rel.Y;
	end
	Pen.First = First;
	Pen.minX, Pen.minY, Pen.maxX, Pen.maxY = minX, minY, maxX, maxY;
	Pen.Middle = First + Vector(sumX / #Pen.points, sumY / #Pen.points);
	A.Pen = Pen;
	A.wasInPen = false;
	return Pen;
end

-- Whether a point is inside the pen (even-odd rule, in the pen's own coordinates so a pen over the map's seam works).
local function InPen(Pen, Point)
	local P = SceneMan:ShortestDistance(Pen.First, Point, false);
	local inside = false;
	local n = #Pen.rel;
	local j = n;
	for i = 1, n do
		local Pi, Pj = Pen.rel[i], Pen.rel[j];
		if (Pi.Y > P.Y) ~= (Pj.Y > P.Y) and P.X < (Pj.X - Pi.X) * (P.Y - Pi.Y) / (Pj.Y - Pi.Y) + Pi.X then
			inside = not inside;
		end
		j = i;
	end
	return inside;
end

-- A spot on the ground near a point: the ground under it, if there's open air there to stand in.
local function GroundAt(Owner, Point)
	local Spot = SceneMan:MovePointToGround(Point + Vector(0, -Owner.MoveHeight * 0.3), math.floor(Owner.MoveHeight * 0.2), 4);
	if SceneMan:GetTerrMatter(Spot.X, Spot.Y) ~= rte.airID then
		return nil;
	end
	return Spot;
end

-- Whether a spot gave no way there lately.
local function IsBad(A, Spot)
	for i = #A.badSpots, 1, -1 do
		local Bad = A.badSpots[i];
		if Bad.Timer:IsPastSimMS(60000) then
			table.remove(A.badSpots, i);
		elseif SceneMan:ShortestDistance(Bad.Spot, Spot, false):MagnitudeIsLessThan(30) then
			return true;
		end
	end
	return false;
end

local function MarkBad(A, Spot)
	if Spot then
		table.insert(A.badSpots, { Spot = Vector(Spot.X, Spot.Y), Timer = Timer() });
	end
end

-- Whether a spot is somewhere this animal may go now, by its order.
local function Allowed(A, Owner, Spot, order)
	if IsBad(A, Spot) then
		return false;
	end
	if order == AnimalBehaviors.PENNED and A.Pen then
		return InPen(A.Pen, Spot);
	end
	if order == AnimalBehaviors.STAY and A.Home then
		return not SceneMan:ShortestDistance(A.Home, Spot, false):MagnitudeIsGreaterThan(STAY_REACH + A.kind.biteRadius + ToMOSprite(Owner):GetSpriteWidth() * 0.5);
	end
	return true;
end

-- How far the body reaches forward from its middle to its mouth.
local function MouthReach(Owner)
	return ToMOSprite(Owner):GetSpriteWidth() * 0.42;
end

-- Lowers the front (the head, to eat or drink) and back of the body by bending the legs, 0 standing to 1 lying down. Done by drawing the stand
-- paths' feet up towards the hips: the body sinks till the feet meet the ground again.
local function SetPose(A, Owner, front, back)
	if Owner.ClassName ~= "ACrab" or (A.pose.front == front and A.pose.back == back) then
		return;
	end
	A.pose.front, A.pose.back = front, back;
	local Crab = ToACrab(Owner);
	if not A.standOffsets then
		A.standOffsets = {};
		for _, side in ipairs({ ACrab.LEFTSIDE, ACrab.RIGHTSIDE }) do
			for _, layer in ipairs({ ACrab.FGROUND, ACrab.BGROUND }) do
				local Path = Crab:GetLimbPath(side, layer, Actor.STAND);
				if Path then
					A.standOffsets[side * 2 + layer] = Vector(Path.StartOffset.X, Path.StartOffset.Y);
				end
			end
		end
	end
	for _, side in ipairs({ ACrab.LEFTSIDE, ACrab.RIGHTSIDE }) do
		-- (The R legs are the front pair, the L legs the hind.)
		local amount = side == ACrab.RIGHTSIDE and front or back;
		for _, layer in ipairs({ ACrab.FGROUND, ACrab.BGROUND }) do
			local Path = Crab:GetLimbPath(side, layer, Actor.STAND);
			local Base = A.standOffsets[side * 2 + layer];
			if Path and Base then
				Path.StartOffset = Vector(Base.X, Base.Y * (1 - 0.65 * amount));
			end
		end
	end
end

-- Turns to face a point.
local function Face(AI, Owner, Point)
	local dx = SceneMan:ShortestDistance(Owner.Pos, Point, false).X;
	if math.abs(dx) > 1 then
		local left = dx < 0;
		AI.SentryFacing = left;
		if Owner.HFlipped ~= left then
			Owner.HFlipped = left;
		end
	end
end

-- Sets off for a spot on its own business.
local function GoTo(AI, A, Owner, Spot, act)
	Owner:ClearAIWaypoints();
	Owner:AddAISceneWaypoint(Spot);
	Owner.AIMode = Actor.AIMODE_GOTO;
	A.serial = Owner.AIOrderSerial;
	A.act = act;
	A.Spot = Vector(Spot.X, Spot.Y);
	A.ActTimer:Reset();
	A.moveLimitMS = 6000 + SceneMan:ShortestDistance(Owner.Pos, Spot, false).Magnitude * 40;
	SetPose(A, Owner, 0, 0);
	SharedBehaviors.Trace(Owner, "animal: " .. act .. " to " .. math.floor(Spot.X) .. "," .. math.floor(Spot.Y), "AI");
end

local function Idle(A, Owner, waitMS)
	A.act = "idle";
	A.Spot = nil;
	A.Target = nil;
	A.ActTimer:Reset();
	A.WaitMS = waitMS or math.random(2000, 6000);
	SetPose(A, Owner, 0, 0);
end

-- The middle of the animal's herd round it (the same herd, the same side, within 20 m), and how many are in it.
local function HerdMiddle(Owner, herd)
	local sumX, sumY, count = 0, 0, 0;
	for Act in SharedBehaviors.ActorsNear(Owner.Pos, 400) do
		if Act.ID ~= Owner.ID and Act.Team == Owner.Team and Act.Status < Actor.DYING then
			local kind = Kinds[Act.PresetName];
			if kind and kind.herd == herd then
				local D = SceneMan:ShortestDistance(Owner.Pos, Act.Pos, false);
				if D:MagnitudeIsLessThan(400) then
					sumX, sumY, count = sumX + D.X, sumY + D.Y, count + 1;
				end
			end
		end
	end
	if count == 0 then
		return nil, 0;
	end
	return Owner.Pos + Vector(sumX / count, sumY / count), count;
end

-- Somewhere to wander to: round home (drawn towards the herd), inside the pen, or barely a step when told to stay. A chicken roaming free
-- sometimes looks up, for a ledge to flap up to.
local function WanderSpot(A, Owner, order)
	for _ = 1, 6 do
		local Try;
		if order == AnimalBehaviors.PENNED and A.Pen then
			local Pen = A.Pen;
			Try = Pen.First + Vector(Pen.minX + math.random() * (Pen.maxX - Pen.minX), Pen.minY + math.random() * (Pen.maxY - Pen.minY));
		else
			local reach = order == AnimalBehaviors.STAY and STAY_REACH or (Owner.OrderHold and 50 or WANDER_REACH);
			Try = A.Home + Vector(math.random(-reach, reach), 0);
			if A.kind.chicken and order == AnimalBehaviors.ROAM and math.random() < 0.35 then
				Try = Try + Vector(0, -math.random(20, 70));
			end
		end
		local Spot = GroundAt(Owner, Try);
		if Spot and Allowed(A, Owner, Spot, order) and not SceneMan:ShortestDistance(Owner.Pos, Spot, false):MagnitudeIsLessThan(20) then
			return Spot;
		end
	end
	return nil;
end

-- Somewhere to stand to eat: the nearest patch of its food it may go to and hasn't failed to reach, with the mouth over it.
local function FoodSpot(A, Owner, order, reach)
	for Patch in SceneMan:FindFood(Owner.Pos, reach, A.kind.diet, 10) do
		if order ~= AnimalBehaviors.PENNED or not A.Pen or InPen(A.Pen, Patch) then
			local dx = SceneMan:ShortestDistance(Owner.Pos, Patch, false).X;
			local side = dx >= 0 and 1 or -1;
			local Spot = GroundAt(Owner, Patch - Vector(side * MouthReach(Owner), 0));
			if Spot and Allowed(A, Owner, Spot, order) then
				return Spot, Vector(Patch.X, Patch.Y);
			end
		end
	end
	return nil;
end

-- Somewhere to stand to drink: the nearest shore it may go to, and the water beside it.
local function WaterSpot(A, Owner, order, reach)
	local Places = {};
	for Place in SceneMan:FindWater(Owner.Pos, reach, 6) do
		table.insert(Places, Vector(Place.X, Place.Y));
	end
	for i = 1, #Places - 1, 2 do
		local Shore, Water = Places[i], Places[i + 1];
		local side = SceneMan:ShortestDistance(Shore, Water, false).X >= 0 and 1 or -1;
		-- (Standing back from the edge by most of its length, so the head is over the water and the feet stay dry.)
		local Spot = GroundAt(Owner, Shore - Vector(side * MouthReach(Owner) * 0.6, 0));
		if Spot and Allowed(A, Owner, Spot, order) then
			return Spot, Water;
		end
	end
	return nil;
end

-- The ground in front of its mouth, where a bite comes from.
local function Mouth(Owner)
	local facing = Owner.HFlipped and -1 or 1;
	return SceneMan:MovePointToGround(Owner.Pos + Vector(facing * MouthReach(Owner), 0), 0, 2);
end

-- Chickens: hops along the way, a flap up to a ledge or over a low wall, and a flutter down from any height (they fall gently).
local function ChickenUpdate(AI, A, Owner, order)
	if Owner.Status ~= Actor.STABLE then
		return;
	end
	-- Fluttering down.
	if AI.flying and Owner.Vel.Y > 2.5 then
		Owner.Vel = Vector(Owner.Vel.X * 0.97, 2.5);
	end
	-- A flap's lift lasts a moment after the jump.
	if A.flapLiftUntil and not A.FlapTimer:IsPastSimMS(A.flapLiftUntil) then
		Owner.Vel = Owner.Vel + Vector(0, -0.12);
	end
	if AI.flying then
		return;
	end
	local penned = order == AnimalBehaviors.PENNED;
	-- Up to a ledge it is heading for, beside it and above: flapped to, not walked round to.
	if A.act ~= "idle" and A.Spot and A.FlapTimer:IsPastSimMS(1500) then
		local D = SceneMan:ShortestDistance(Owner.Pos, A.Spot, false);
		if D.Y < -12 and math.abs(D.X) < 90 then
			local side = D.X >= 0 and 1 or -1;
			-- (Penned, just a short flap: a wall more than a chicken's height holds it.)
			Owner.Vel = Vector(side * 2.5, penned and -4 or -6.5);
			A.FlapTimer:Reset();
			A.flapLiftUntil = penned and 0 or 350;
			SharedBehaviors.Trace(Owner, "animal: flap up", "AI");
			return;
		end
	end
	-- Now and then a flap about for no reason, and a hop along the way.
	if A.FlapTimer:IsPastSimMS(A.flapWaitMS) then
		A.FlapTimer:Reset();
		A.flapWaitMS = penned and math.random(25000, 50000) or math.random(10000, 25000);
		local side = math.random() < 0.5 and -1 or 1;
		Owner.Vel = Vector(side * (penned and 1.5 or 3), penned and -4 or -6);
		A.flapLiftUntil = penned and 0 or 300;
	elseif A.act ~= "idle" and A.act ~= "eat" and A.act ~= "drink" and A.act ~= "rest" and math.random() < 0.01 then
		AI.Ctrl:SetState(Controller.BODY_LEAP, true);
	end
end

-- Draws what the animal is up to, for the unit inspector.
local function DrawDebug(A, Owner, order)
	if not Owner.IsInspected then
		return;
	end
	local names = { [0] = "Roam free", "Penned", "Stay" };
	local text = (names[order] or "?") .. ": " .. A.act .. string.format("  food %d%%  water %d%%  rest %d%%", math.floor(A.hunger * 100), math.floor(A.thirst * 100), math.floor(A.tired * 100));
	PrimitiveMan:DrawTextPrimitive(Owner.Pos + Vector(0, -Owner.MoveHeight - 14), text, true, 1);
	if A.Spot then
		PrimitiveMan:DrawLinePrimitive(Owner.Pos, A.Spot, 147);
		PrimitiveMan:DrawCirclePrimitive(A.Spot, 3, 147);
	end
	if A.Target then
		PrimitiveMan:DrawCirclePrimitive(A.Target, A.kind.biteRadius, 117);
	end
	if A.Home and order ~= AnimalBehaviors.PENNED then
		PrimitiveMan:DrawCirclePrimitive(A.Home, 2, 254);
	end
	if A.Pen then
		local n = #A.Pen.points;
		for i = 1, n do
			PrimitiveMan:DrawLinePrimitive(A.Pen.points[i], A.Pen.points[i % n + 1], 117);
		end
	end
end

-- Once an update, from the animal's AI (NativeCrabAI and NativeHumanAI), after the flee check.
function AnimalBehaviors.Update(AI, Owner)
	if not Owner:IsInGroup("Actors - Livestock") then
		return;
	end
	local A = AI.Animal;
	if not A then
		A = NewState(Owner);
		AI.Animal = A;
	end

	-- Needs go up all the time, slower while resting.
	local secs = A.NeedsTimer.ElapsedSimTimeMS / 1000;
	if secs >= 0.5 then
		A.NeedsTimer:Reset();
		A.hunger = math.min(1, A.hunger + secs / A.kind.hungerSecs);
		A.thirst = math.min(1, A.thirst + secs / A.kind.thirstSecs);
		if A.act == "rest" then
			A.tired = math.max(0, A.tired - secs / 20);
		else
			A.tired = math.min(1, A.tired + secs / A.kind.tiredSecs);
		end
	end

	local order = Order(Owner);
	if order == AnimalBehaviors.PENNED then
		if not Pen(A, Owner) then
			order = AnimalBehaviors.ROAM;
		end
	else
		A.Pen = nil;
	end
	if order ~= A.lastOrder then
		-- A new order of its own: stay is where it stands now; a pen is gone into first.
		A.lastOrder = order;
		A.Home = Vector(Owner.Pos.X, Owner.Pos.Y);
		if A.act ~= "ordered" then
			Idle(A, Owner, 0);
		end
	end
	A.Home = A.Home or Vector(Owner.Pos.X, Owner.Pos.Y);
	DrawDebug(A, Owner, order);

	if Owner:IsPlayerControlled() or AI.Flee or Owner.OrderAttack then
		if A.act ~= "idle" then
			Idle(A, Owner);
		end
		return;
	end

	if A.kind.chicken then
		ChickenUpdate(AI, A, Owner, order);
	end

	-- Someone else's order (a move, a guard, a patrol): carried out as given. A penned animal sent out of its pen, or told to follow, is let out.
	local mode = Owner.AIMode;
	if Owner.AIOrderSerial ~= A.serial then
		A.serial = Owner.AIOrderSerial;
		-- (Its own walk asked for again on the way, by the route-follower, is still its own: only somewhere else is an order.)
		local own = mode == Actor.AIMODE_GOTO and A.Spot ~= nil and A.act ~= "idle" and (Owner:GetWaypointListSize() == 0 or not SceneMan:ShortestDistance(Owner:GetLastAIWaypoint(), A.Spot, false):MagnitudeIsGreaterThan(48));
		if (mode == Actor.AIMODE_GOTO or mode == Actor.AIMODE_SQUAD) and not own then
			if A.act ~= "ordered" then
				SetPose(A, Owner, 0, 0);
			end
			A.act = "ordered";
			A.Spot = nil;
			if order == AnimalBehaviors.PENNED and A.Pen and (mode == Actor.AIMODE_SQUAD or (Owner:GetWaypointListSize() > 0 and not InPen(A.Pen, Owner:GetLastAIWaypoint()))) then
				SharedBehaviors.Trace(Owner, "animal: let out of its pen by an order", "AI");
				Unpen(A, Owner);
				order = AnimalBehaviors.ROAM;
				A.lastOrder = order;
			end
		end
	end
	if A.act == "ordered" then
		if mode == Actor.AIMODE_SENTRY then
			-- Done: its life again, from where it was left.
			A.Home = Vector(Owner.Pos.X, Owner.Pos.Y);
			Idle(A, Owner);
		end
		return;
	end
	if mode ~= Actor.AIMODE_SENTRY and mode ~= Actor.AIMODE_GOTO then
		return;
	end

	-- On its way somewhere of its own.
	if mode == Actor.AIMODE_GOTO then
		if not A.Spot then
			return;
		end
		local far = SceneMan:ShortestDistance(Owner.Pos, A.Spot, false):MagnitudeIsGreaterThan(Owner.MoveHeight + 10);
		local noRoute = A.ActTimer:IsPastSimMS(1500) and not Owner.IsWaitingOnNewMovePath and Owner.MovePathSize == 0 and far;
		if noRoute or A.ActTimer:IsPastSimMS(A.moveLimitMS) then
			SharedBehaviors.Trace(Owner, "animal: no way to " .. A.act, "AI");
			MarkBad(A, A.Spot);
			Owner:ClearAIWaypoints();
			Owner.AIMode = Actor.AIMODE_SENTRY;
			A.serial = Owner.AIOrderSerial;
			Idle(A, Owner, 500);
		end
		return;
	end

	-- Got there (the walk over: the engine makes it a sentry again): what it went for.
	if A.act == "toFood" or A.act == "toWater" or A.act == "toPen" or A.act == "wander" then
		local there = A.Spot and not SceneMan:ShortestDistance(Owner.Pos, A.Spot, false):MagnitudeIsGreaterThan(Owner.MoveHeight + 10);
		if not there then
			MarkBad(A, A.Spot);
			Idle(A, Owner, 500);
		elseif A.act == "toFood" then
			A.act = "eat";
			A.ActTimer:Reset();
			A.BiteTimer:Reset();
			if order == AnimalBehaviors.ROAM then
				-- (Roaming, where the food is is home now.)
				A.Home = Vector(Owner.Pos.X, Owner.Pos.Y);
			end
		elseif A.act == "toWater" then
			A.act = "drink";
			A.ActTimer:Reset();
			A.BiteTimer:Reset();
		else
			Idle(A, Owner);
		end
	end

	-- Eating: a bite now and then from the ground in front of its mouth, head down, till full or the patch is bare.
	if A.act == "eat" then
		if A.Target then
			Face(AI, Owner, A.Target);
		end
		SetPose(A, Owner, 0.6, 0);
		if A.BiteTimer:IsPastSimMS(900) then
			A.BiteTimer:Reset();
			local eaten = SceneMan:EatFood(Mouth(Owner), A.kind.biteRadius, A.kind.diet, A.kind.bite);
			if eaten > 0 then
				A.hunger = math.max(0, A.hunger - eaten / A.kind.fill);
			elseif A.kind.chicken then
				-- (A chicken scratches the bare ground for seeds and grubs: slower, but there's always something.)
				A.hunger = math.max(0, A.hunger - 0.25 / A.kind.fill);
			end
			if A.hunger < 0.05 then
				Idle(A, Owner);
			elseif eaten == 0 and not A.kind.chicken then
				-- The patch is bare: the next one along, if there's one close.
				local Spot, Patch = FoodSpot(A, Owner, order, 60);
				if Spot then
					A.Target = Patch;
					GoTo(AI, A, Owner, Spot, "toFood");
				else
					Idle(A, Owner);
				end
			end
		end
		return;
	end

	-- Drinking: head down over the water till it's had enough.
	if A.act == "drink" then
		if A.Target then
			Face(AI, Owner, A.Target);
		end
		SetPose(A, Owner, 0.6, 0);
		if A.BiteTimer:IsPastSimMS(1000) then
			A.BiteTimer:Reset();
			A.thirst = math.max(0, A.thirst - 0.12);
			if A.thirst < 0.05 then
				Idle(A, Owner);
			end
		end
		return;
	end

	-- Resting: lying down till rested, or till hunger or thirst gets it up.
	if A.act == "rest" then
		SetPose(A, Owner, 1, 1);
		if A.tired <= 0.02 or A.hunger > 0.9 or A.thirst > 0.9 or A.ActTimer:IsPastSimMS(A.restMS) then
			Idle(A, Owner);
		end
		return;
	end

	-- Standing about: every so often it decides what to do next.
	if not A.ActTimer:IsPastSimMS(A.WaitMS) then
		return;
	end
	A.ActTimer:Reset();
	A.WaitMS = math.random(2000, 6000);

	-- Penned and outside the pen: back in, unless a chicken that was in has flapped out over the wall, which is loose now.
	if order == AnimalBehaviors.PENNED and A.Pen then
		local inside = InPen(A.Pen, Owner.Pos);
		if inside then
			A.wasInPen = true;
		elseif A.kind.chicken and A.wasInPen then
			SharedBehaviors.Trace(Owner, "animal: got out of its pen", "AI");
			Unpen(A, Owner);
			order = AnimalBehaviors.ROAM;
			A.lastOrder = order;
		else
			local Spot = GroundAt(Owner, A.Pen.Middle) or WanderSpot(A, Owner, order);
			if Spot then
				GoTo(AI, A, Owner, Spot, "toPen");
			end
			return;
		end
	end

	local reachFood = order == AnimalBehaviors.STAY and (STAY_REACH + MouthReach(Owner)) or FOOD_REACH;
	local reachWater = order == AnimalBehaviors.STAY and 60 or WATER_REACH;
	if A.thirst > 0.6 then
		local Spot, Water = WaterSpot(A, Owner, order, reachWater);
		if Spot then
			A.Target = Water;
			GoTo(AI, A, Owner, Spot, "toWater");
			return;
		end
	end
	if A.hunger > 0.5 then
		local Spot, Patch = FoodSpot(A, Owner, order, reachFood);
		if Spot then
			A.Target = Patch;
			GoTo(AI, A, Owner, Spot, "toFood");
			return;
		elseif A.kind.chicken then
			-- (Nothing green about: it scratches where it stands.)
			A.Target = nil;
			A.act = "eat";
			A.BiteTimer:Reset();
			return;
		end
	end
	if A.tired > 0.8 then
		A.act = "rest";
		A.restMS = math.random(15000, 35000);
		A.ActTimer:Reset();
		SharedBehaviors.Trace(Owner, "animal: lies down", "AI");
		return;
	end
	-- Roaming, a herd animal drifts back to its herd when it has strayed.
	if order == AnimalBehaviors.ROAM and A.kind.herd then
		local Middle, count = HerdMiddle(Owner, A.kind.herd);
		if Middle and SceneMan:ShortestDistance(A.Home, Middle, false):MagnitudeIsGreaterThan(100) then
			local Toward = SceneMan:ShortestDistance(A.Home, Middle, false);
			A.Home = A.Home + Toward * 0.5;
		end
	end
	-- A wander, now and then, or a nibble if there's food right there.
	if A.hunger > 0.25 then
		local Spot, Patch = FoodSpot(A, Owner, order, 80);
		if Spot then
			A.Target = Patch;
			GoTo(AI, A, Owner, Spot, "toFood");
			return;
		end
	end
	if math.random() < 0.6 then
		local Spot = WanderSpot(A, Owner, order);
		if Spot then
			GoTo(AI, A, Owner, Spot, "wander");
		end
	end
end
