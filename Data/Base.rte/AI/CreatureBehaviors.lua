-- Creatures (CR-1): monsters, in the "Actors - Creatures" group. They fight with what they were born with (natural weapons, the
-- "Weapons - Natural" group, mounted on the turret of a crab-bodied one), so instead of shooting from where they stand they run their prey
-- down: along a route to it, leaping at it when it is close, and biting once it is in reach. Creatures of a side hunt as a pack: one that
-- sees no prey of its own joins a packmate's hunt nearby.
--
-- A creature's preset tunes it with number values (AddCustomValue = NumberValue):
--   CreatureBiteReach    how far past the prey's edge its bite reaches, in pixels (default 30)
--   CreaturePounceRange  how close the prey must be to leap at it, in pixels; 0 for a creature that doesn't pounce (default 0)
--   CreatureChaseRange   how far it runs after its prey before giving up, in pixels (default 900)
--   CreaturePackRange    how far off a packmate's hunt is joined, in pixels; 0 for a loner (default 400)
--   CreatureSpitRange    how far it spits at its prey (CR-6, a giant spider's web), in pixels; 0 for one that doesn't spit (default 0)
--   CreatureSpitCooldown how long between spits, in ms (default 6000)
--   CreatureSpitSpeed    how fast the spit flies, in m/s (default 18)
--   CreatureAttackRange  for a creature on two legs (CR-7): how close it gets before it lets its prey have it, in pixels (default 110)
-- and a string value:
--   CreatureSpit         the preset of what it spits (an MOPixel in Base.rte)

require("AI/SharedBehaviors");

CreatureBehaviors = {};

function CreatureBehaviors.IsCreature(Owner)
	return Owner:IsInGroup("Actors - Creatures");
end

local function Value(Owner, key, default)
	return Owner:NumberValueExists(key) and Owner:GetNumberValue(key) or default;
end

-- The typed object an actor is, for a target (the AI reads a target's class-specific members).
local function AsActor(MO)
	if not MO or not IsActor(MO) then
		return nil;
	end
	if MO.ClassName == "AHuman" then
		return ToAHuman(MO);
	elseif MO.ClassName == "ACrab" then
		return ToACrab(MO);
	elseif MO.ClassName == "AVehicle" then
		return ToAVehicle(MO);
	elseif MO.ClassName == "ACShip" then
		return ToACShip(MO);
	elseif MO.ClassName == "ACDropShip" then
		return ToACDropShip(MO);
	elseif MO.ClassName == "ACRocket" then
		return ToACRocket(MO);
	end
	return ToActor(MO);
end

-- Whether it may leave its spot to run its prey down: not told to hold, to stand its ground or only to move, nor running the objective.
function CreatureBehaviors.MayChase(AI, Owner)
	if Owner.OrderHold or SharedBehaviors.Rushing(Owner) or SharedBehaviors.OnObjective(Owner) then
		return false;
	end
	local rule = Owner.MovementRule;
	return rule ~= Actor.MOVE_ONLY and rule ~= Actor.MOVE_HOLD_GROUND;
end

-- How far the prey's edge is from it, and the way there.
function CreatureBehaviors.GapTo(Owner, Target)
	local To = SceneMan:ShortestDistance(Owner.Pos, Target.Pos, false);
	return math.max(To.Magnitude - Target.Radius * 0.5, 0), To;
end

-- Whether a hunt is worth starting at a target now: it may chase, or the target is already in reach of its bite.
function CreatureBehaviors.WillHunt(AI, Owner, Target)
	if CreatureBehaviors.MayChase(AI, Owner) then
		return true;
	end
	return (CreatureBehaviors.GapTo(Owner, Target)) <= Value(Owner, "CreatureBiteReach", 30) * 2;
end

-- The next point to run to on the way to the prey: along a route asked for on the pathing threads with the creature's own body (its leaps,
-- its size, its movement profile), asked again now and then and whenever the prey has moved off from where it was asked to. Straight at
-- the prey until the first route is back.
function CreatureBehaviors.RouteStep(AI, Owner, Route, Target)
	if not Route.pending and (#Route.points == 0 or Route.Timer:IsPastSimMS(1500) or (Route.Goal and SceneMan:ShortestDistance(Route.Goal, Target.Pos, false):MagnitudeIsGreaterThan(60))) then
		Route.pending = true;
		Route.Timer:Reset();
		Route.Goal = Vector(Target.Pos.X, Target.Pos.Y);
		SceneMan.Scene:CalculatePathAsyncForActor(function(pathRequest)
			local points = {};
			if pathRequest.Status ~= PathRequest.NoSolution then
				for Point in pathRequest.Path do
					table.insert(points, Vector(Point.X, Point.Y));
				end
			end
			Route.points = points;
			Route.pending = false;
		end, Owner, Owner.Pos, Target.Pos, Owner.Team);
	end
	-- (The points it has reached, or passed by a little, off the front.)
	local reachX = math.max(Owner.Radius * 0.5, 12);
	while #Route.points > 0 do
		local To = SceneMan:ShortestDistance(Owner.Pos, Route.points[1], false);
		if math.abs(To.X) < reachX and math.abs(To.Y) < Owner.MoveHeight * 0.6 then
			table.remove(Route.points, 1);
		else
			break;
		end
	end
	return Route.points[1] or Target.Pos;
end

-- Whether the prey is already held by what it spits (a web), so it isn't spat at again meanwhile.
local function HeldBySpit(Target)
	return IsActor(Target) and ToActor(Target).IsEnsnared;
end

-- Spits at its prey (CR-6, a giant spider's web) when it is in range, in the open and not held already, and the spit is ready: aimed a
-- little high for the drop on the way. @return Whether it spat.
function CreatureBehaviors.SpitAt(AI, Owner, Target, gap, SpitTimer)
	local range = Value(Owner, "CreatureSpitRange", 0);
	if range <= 0 or gap > range or not Owner:StringValueExists("CreatureSpit") or HeldBySpit(Target)
		or not SpitTimer:IsPastSimMS(Value(Owner, "CreatureSpitCooldown", 6000)) then
		return false;
	end
	local From = Owner.EyePos;
	if SceneMan:CastStrengthSumRay(From, Target.Pos, 4, rte.grassID) >= 50 then
		return false;
	end
	local Spit = CreateMOPixel(Owner:GetStringValue("CreatureSpit"), "Base.rte");
	if not Spit then
		return false;
	end
	SpitTimer:Reset();
	local speed = Value(Owner, "CreatureSpitSpeed", 18);
	local To = SceneMan:ShortestDistance(From, Target.Pos, false);
	-- (Its fall on the way, from its flight time: aimed up by that much.)
	local seconds = To.Magnitude / (speed * GetPPM());
	local drop = 0.5 * SceneMan.GlobalAcc.Y * Spit.GlobalAccScalar * seconds * seconds * GetPPM();
	Spit.Pos = Vector(From.X, From.Y);
	Spit.Vel = (To - Vector(0, drop)):SetMagnitude(speed);
	Spit.Team = Owner.Team;
	Spit.IgnoresTeamHits = true;
	MovableMan:AddParticle(Spit);
	SharedBehaviors.Trace(Owner, "hunt: spit at " .. Target.PresetName);
	return true;
end

-- Running its prey down and biting it (the attack behaviour of a crab-bodied creature). Over when the prey is dead or gone, lost from
-- sight for four seconds, or run further than the chase range; a creature that was on guard then goes back to its post.
function CreatureBehaviors.HuntTarget(AI, Owner, Abort)
	if not AI.Target or not MovableMan:ValidMO(AI.Target) then
		return true;
	end
	AI.TargetLostTimer:Reset();
	AI.TargetLostTimer:SetSimTimeLimitMS(SharedBehaviors.TargetMemoryMS(4000));
	local reach = Value(Owner, "CreatureBiteReach", 30);
	local pounceRange = Value(Owner, "CreaturePounceRange", 0);
	local chaseRange = Value(Owner, "CreatureChaseRange", 900);
	local Keep = Owner.AIMode == Actor.AIMODE_SENTRY and SharedBehaviors.RememberOrder(AI, Owner) or nil;
	local Route = { points = {}, pending = false, Timer = Timer() };
	local LeapTimer = Timer();
	local SpitTimer = Timer();
	SpitTimer.ElapsedSimTimeMS = 60000; -- (Ready at once.)
	local HeldTimer = Timer(); -- How long the prey has been out of reach of one not let go after it.
	local ProgressTimer = Timer();
	local closest = math.huge;
	Owner:SetNumberValue("CreatureHuntID", AI.Target.UniqueID);
	SharedBehaviors.Trace(Owner, "hunt: " .. AI.Target.PresetName);

	local function Finish(aborted)
		Owner:RemoveNumberValue("CreatureHuntID");
		-- (Its prey dead, gone or given up on: forgotten, so the next one seen is taken up at once.)
		if not aborted then
			AI.Target = nil;
		end
		AI.fire = false;
		AI.lateralMoveState = Actor.LAT_STILL;
		AI.deviceState = ACrab.STILL;
		AI.Ctrl:SetState(Controller.BODY_LEAP, false);
		-- (Back to its post, when it was on guard and the hunt took it away; not when another order ended the hunt.)
		if not aborted and Keep and Keep.post and Owner.AIMode == Actor.AIMODE_SENTRY and SceneMan:ShortestDistance(Owner.Pos, Keep.post, false):MagnitudeIsGreaterThan(Owner.MoveHeight) then
			SharedBehaviors.RestoreOrder(AI, Owner, Keep);
		end
		return true;
	end

	while true do
		local Target = AI.Target;
		if not Target or not MovableMan:ValidMO(Target) or Target:IsDead() then
			break;
		end
		local gap, ToTarget = CreatureBehaviors.GapTo(Owner, Target);
		if gap > chaseRange then
			SharedBehaviors.Trace(Owner, "hunt: got away");
			break;
		end
		local dir = ToTarget.X >= 0 and 1 or -1;
		AI.deviceState = ACrab.AIMING;
		AI.Ctrl.AnalogAim = SceneMan:ShortestDistance(Owner.EyePos, Target.Pos, false).Normalized;
		AI.Ctrl:SetState(Controller.BODY_LEAP, false);

		if gap <= reach then
			-- In reach: stand and bite.
			AI.lateralMoveState = Actor.LAT_STILL;
			Owner.HFlipped = dir < 0;
			AI.fire = true;
			HeldTimer:Reset();
		elseif gap > reach * 2 and CreatureBehaviors.SpitAt(AI, Owner, Target, gap, SpitTimer) then
			-- Spat at it from where it stands (a web, to hold it while it closes in).
			AI.fire = false;
			AI.lateralMoveState = Actor.LAT_STILL;
			Owner.HFlipped = dir < 0;
		else
			AI.fire = false;
			if not CreatureBehaviors.MayChase(AI, Owner) then
				-- Not let go after it: facing it, and given up on after two seconds out of reach.
				AI.lateralMoveState = Actor.LAT_STILL;
				Owner.HFlipped = dir < 0;
				if HeldTimer:IsPastSimMS(2000) then
					break;
				end
			elseif pounceRange > 0 and gap < pounceRange and math.abs(ToTarget.Y) < Owner.MoveHeight * 0.5 and LeapTimer:IsPastSimMS(1800)
				and SceneMan:CastStrengthSumRay(Owner.EyePos, Target.Pos, 4, rte.grassID) < 50 then
				-- Close, level and nothing in the way: a leap at it.
				AI.lateralMoveState = dir > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
				AI.Ctrl:SetState(Controller.BODY_LEAP, true);
				LeapTimer:Reset();
				SharedBehaviors.Trace(Owner, "hunt: pounce");
			else
				-- After it, along its route.
				local Next = CreatureBehaviors.RouteStep(AI, Owner, Route, Target);
				local ToNext = SceneMan:ShortestDistance(Owner.Pos, Next, false);
				if math.abs(ToNext.X) > 3 then
					AI.lateralMoveState = ToNext.X > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
				else
					AI.lateralMoveState = Actor.LAT_STILL;
				end
				-- Up onto a ledge it can't step up, or stuck for a second and a half: a leap.
				if gap < closest - 4 then
					closest = gap;
					ProgressTimer:Reset();
				end
				local climb = ToNext.Y < -Owner.MoveHeight * 0.3 and math.abs(ToNext.X) < Owner.MoveHeight;
				if (climb or ProgressTimer:IsPastSimMS(1500)) and LeapTimer:IsPastSimMS(700) then
					AI.Ctrl:SetState(Controller.BODY_LEAP, true);
					LeapTimer:Reset();
					ProgressTimer:Reset();
					closest = gap;
				end
			end
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then
			return Finish(true);
		end
	end

	return Finish(false);
end

-- Going after its prey and letting it have it with what it was born with (CR-7, the attack behaviour of a creature on two legs: a Flame
-- Fiend's fire). It walks at its prey on the usual route, as a soldier with a blade closes in (HumanBehaviors.AttackTarget), and once it is
-- within its attack range and in the open, it aims and fires on the way. Over when the prey is dead or gone, has got further off than the
-- chase range, or it gets no nearer for four seconds; then it goes back to where it was going, or its post.
function CreatureBehaviors.ChargeTarget(AI, Owner, Abort)
	if not AI.Target or not MovableMan:ValidMO(AI.Target) then
		return true;
	end
	AI.TargetLostTimer:SetSimTimeLimitMS(SharedBehaviors.TargetMemoryMS(4000));
	local range = Value(Owner, "CreatureAttackRange", 110);
	local chaseRange = Value(Owner, "CreatureChaseRange", 900);
	local Keep = Owner.AIMode == Actor.AIMODE_SENTRY and SharedBehaviors.RememberOrder(AI, Owner) or nil;
	local ProgressTimer = Timer();
	local closest = math.huge;
	Owner:SetNumberValue("CreatureHuntID", AI.Target.UniqueID);
	SharedBehaviors.Trace(Owner, "charge: " .. AI.Target.PresetName);

	-- (Back to where it was going afterwards, as a soldier closing in with a blade is.)
	local PrevMOMoveTarget, PrevSceneWaypoint;
	if Owner.MOMoveTarget and MovableMan:ValidMO(Owner.MOMoveTarget) then
		PrevMOMoveTarget = Owner.MOMoveTarget;
	elseif Owner.AIMode ~= Actor.AIMODE_SENTRY then
		Owner.MOMoveTarget = nil;
		PrevSceneWaypoint = SceneMan:MovePointToGround(Owner:GetLastAIWaypoint(), Owner.MoveHeight / 5, 4);
	end
	Owner:ClearMovePath();
	Owner:AddAIMOWaypoint(AI.Target);
	if PrevMOMoveTarget then
		Owner:AddAIMOWaypoint(PrevMOMoveTarget);
	elseif PrevSceneWaypoint then
		Owner:AddAISceneWaypoint(PrevSceneWaypoint);
	end
	AI:CreateGoToBehavior(Owner);
	AI.proneState = AHuman.NOTPRONE;

	local function Finish(aborted)
		Owner:RemoveNumberValue("CreatureHuntID");
		AI.fire = false;
		AI.deviceState = AHuman.STILL;
		if not aborted then
			AI.Target = nil;
			if Keep and Keep.post and Owner.AIMode == Actor.AIMODE_SENTRY and SceneMan:ShortestDistance(Owner.Pos, Keep.post, false):MagnitudeIsGreaterThan(Owner.MoveHeight) then
				SharedBehaviors.RestoreOrder(AI, Owner, Keep);
			end
		end
		return true;
	end

	while true do
		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then
			return Finish(true);
		end
		local Target = AI.Target;
		if not Target or not MovableMan:ValidMO(Target) or (IsActor(Target) and ToActor(Target):IsDead()) then
			break;
		end
		if not Owner.EquippedItem then
			Owner:EquipFirearm(true);
		end
		local gap, ToTarget = CreatureBehaviors.GapTo(Owner, Target);
		if gap > chaseRange then
			SharedBehaviors.Trace(Owner, "charge: got away");
			break;
		end
		if gap < closest - 4 then
			closest = gap;
			ProgressTimer:Reset();
		elseif gap > range and ProgressTimer:IsPastSimMS(4000) then
			SharedBehaviors.Trace(Owner, "charge: can't get at it");
			break;
		end
		AI.Ctrl.AnalogAim = SceneMan:ShortestDistance(Owner.EyePos, Target.Pos, false).Normalized;
		if gap <= range and SceneMan:CastStrengthSumRay(Owner.EyePos, Target.Pos, 4, rte.grassID) < 50 then
			AI.deviceState = AHuman.AIMING;
			Owner.HFlipped = ToTarget.X < 0;
			AI.fire = true;
			ProgressTimer:Reset();
		else
			AI.deviceState = AHuman.STILL;
			AI.fire = false;
		end
	end

	return Finish(false);
end

-- Hunting as a pack: a creature with no prey of its own takes up the prey of a packmate (a creature of its side) hunting close by. Called
-- each update; looks twice a second.
function CreatureBehaviors.PackUpdate(AI, Owner)
	if AI.Target or SharedBehaviors.NeverFights(Owner) or Owner:IsPlayerControlled() then
		return;
	end
	local packRange = Value(Owner, "CreaturePackRange", 400);
	if packRange <= 0 then
		return;
	end
	AI.PackTimer = AI.PackTimer or Timer();
	if not AI.PackTimer:IsPastSimMS(500) then
		return;
	end
	AI.PackTimer:Reset();
	for Act in MovableMan.Actors do
		if Act.ID ~= Owner.ID and Act.Team == Owner.Team and Act.Status < Actor.DYING and SharedBehaviors.PeerValueExists(Act, "CreatureHuntID")
			and Act:IsInGroup("Actors - Creatures") and not SceneMan:ShortestDistance(Owner.Pos, Act.Pos, false):MagnitudeIsGreaterThan(packRange) then
			local Prey = AsActor(MovableMan:FindObjectByUniqueID(SharedBehaviors.PeerValue(Act, "CreatureHuntID")));
			if Prey and MovableMan:ValidMO(Prey) and not Prey:IsDead() and Prey.Team ~= Owner.Team and SharedBehaviors.MayTarget(AI, Owner, Prey)
				and CreatureBehaviors.WillHunt(AI, Owner, Prey) then
				SharedBehaviors.Trace(Owner, "pack: joins the hunt for " .. Prey.PresetName);
				AI.Target = Prey;
				AI.TargetOffset = Vector();
				AI.OldTargetPos = nil;
				AI:CreateAttackBehavior(Owner);
				return;
			end
		end
	end
end
