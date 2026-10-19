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
--   CreatureLurks        1 for a creature that hangs from a ceiling in wait while on guard (CR-10, a giant spider; it must climb ceilings)
--   CreatureLurkHeight   how far over its post it looks for a ceiling to lurk on, in pixels (default 300)
--   CreatureLurkWait     how long it waits up there for prey it has seen to come under it before going after it, in ms (default 8000)
--   CreatureDropReach    how far to one side of right under it its prey may be for it to drop on it, in pixels (default 40)
--   CreatureHoverRange   for a flier that hovers (C8): how far off its prey it holds in the air, in pixels (default 40)
--   CreatureDiveRange    for a flier that dives: how close it must be, in the open, to dive at its prey, in pixels (default 160)
--   CreatureScatters     1 for a flier that breaks off and flies away for a moment when it is hurt (a carrion bat)
--   CreatureChargeRange  for one that charges (a boar, a minotaur; four legs or two): how close its prey must be, level and in the open,
--                        to charge it, in pixels; 0 for one that doesn't (default 0)
--   CreatureChargeSpeed  how fast it bursts forward in a charge, in m/s (default 9)
--   CreatureChargeCooldown how long between charges, in ms (default 3000)
--   CreatureFleeBelow    for a swarm creature (a giant rat): it breaks off its hunt and runs when fewer of its kind than this, itself
--                        counted, are within its pack range; 0 for one that never does (default 0)
-- and string values:
--   CreatureSpit         the preset of what it spits (an MOPixel in Base.rte)
--   CreatureFlyAttack    for a flier (C8): Dive to swoop on its prey, bite and pull away (default), or Hover to hold off it and let it have
--                        what it was born with from there

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
	elseif MO.ClassName == "AMech" then
		return ToAMech(MO);
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

-- Whether it crawls with its body (CR-10): up walls (and along ceilings) for a climber, or on its belly for one with no legs.
local function Crawls(Owner)
	return Owner.ClassName == "ACrab" and (Owner.Climbs > 0 or ToACrab(Owner).IsBornLegless);
end

-- Steers a crawling body to a point (CR-10): the stick at it, which the body crawls along the surface it grips (ACrab::UpdateCrawl), and
-- the walking keys towards it while it isn't gripping, to walk to the foot of the wall.
local function CrawlTo(AI, Owner, To)
	if To.Magnitude > 2 then
		AI.Ctrl.AnalogMove = To.Normalized;
	end
	if ToACrab(Owner).IsClinging or math.abs(To.X) <= 3 then
		AI.lateralMoveState = Actor.LAT_STILL;
	else
		AI.lateralMoveState = To.X > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
	end
end

-- Spits at its prey (CR-6, a giant spider's web) when it is in range, in the open and not held already, the spit is ready and it isn't
-- webbed itself: aimed a little high for the drop on the way. @return Whether it spat.
function CreatureBehaviors.SpitAt(AI, Owner, Target, gap, SpitTimer)
	local range = Value(Owner, "CreatureSpitRange", 0);
	if range <= 0 or gap > range or Owner.IsEnsnared or not Owner:StringValueExists("CreatureSpit") or HeldBySpit(Target)
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
	-- (Put in the scene by UpdateAI in CrabAI.lua, not here: this runs on an AI thread, and adding an object from one races the other
	-- AI threads' MovableMan:ValidMO checks, a crash with no message in a big fight.)
	AI.PendingSpit = Spit;
	SharedBehaviors.Trace(Owner, "hunt: spit at " .. Target.PresetName);
	return true;
end

-- Running its prey down and biting it (the attack behaviour of a crab-bodied creature). Over when the prey is dead or gone, lost from
-- sight for four seconds, or run further than the chase range; a creature that was on guard then goes back to its post.
function CreatureBehaviors.HuntTarget(AI, Owner, Abort)
	if not AI.Target or not MovableMan:ValidMO(AI.Target) then
		return true;
	end
	-- (A flier hunts from the air, C8.)
	if Owner.ClassName == "ACrab" and Owner.Flies > 0 then
		return CreatureBehaviors.FlyHunt(AI, Owner, Abort);
	end
	AI.TargetLostTimer:Reset();
	AI.TargetLostTimer:SetSimTimeLimitMS(SharedBehaviors.TargetMemoryMS(4000));
	local reach = Value(Owner, "CreatureBiteReach", 30);
	local pounceRange = Value(Owner, "CreaturePounceRange", 0);
	local chaseRange = Value(Owner, "CreatureChaseRange", 900);
	local chargeRange = Value(Owner, "CreatureChargeRange", 0);
	local fleeBelow = Value(Owner, "CreatureFleeBelow", 0);
	local Keep = Owner.AIMode == Actor.AIMODE_SENTRY and SharedBehaviors.RememberOrder(AI, Owner) or nil;
	local Route = { points = {}, pending = false, Timer = Timer() };
	local LeapTimer = Timer();
	local ChargeTimer = Timer();
	ChargeTimer.ElapsedSimTimeMS = 60000; -- (Ready at once.)
	local FleeCheck = Timer();
	local Flee = nil; -- Running, since when (a swarm creature left with too few of its kind).
	local SpitTimer = Timer();
	SpitTimer.ElapsedSimTimeMS = 60000; -- (Ready at once.)
	local HeldTimer = Timer(); -- How long the prey has been out of reach of one not let go after it.
	-- (Seen from a ceiling it lurks on, CR-10: it waits up there a while for its prey to come under it.)
	local LurkTimer = (Crawls(Owner) and ToACrab(Owner).IsOnCeiling) and Timer() or nil;
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

		-- On a ceiling (CR-10): it drops on its prey once the prey is under it, and while it lurks it waits up there a while for that,
		-- spitting at it, before going after it.
		local waiting = false;
		if Crawls(Owner) and ToACrab(Owner).IsOnCeiling then
			local under = ToTarget.Y > 0 and math.abs(ToTarget.X) < Value(Owner, "CreatureDropReach", 40) + ToTarget.Y * 0.2;
			if under and SceneMan:CastStrengthSumRay(Owner.Pos, Target.Pos, 4, rte.grassID) < 50 then
				ToACrab(Owner):LetGo(1.5);
				LurkTimer = nil;
				SharedBehaviors.Trace(Owner, "hunt: drops on " .. Target.PresetName);
			elseif LurkTimer and not LurkTimer:IsPastSimMS(Value(Owner, "CreatureLurkWait", 8000)) then
				waiting = true;
			end
		end

		-- A swarm creature left with too few of its kind about it runs (CreatureFleeBelow): looked at once a second.
		if fleeBelow > 0 and not Flee and FleeCheck:IsPastSimMS(1000) then
			FleeCheck:Reset();
			local packRange = math.max(Value(Owner, "CreaturePackRange", 400), 100);
			local kin = 0;
			for Act in SharedBehaviors.ActorsNear(Owner.Pos, packRange) do
				if Act.Team == Owner.Team and Act.PresetName == Owner.PresetName and Act.Status < Actor.DYING
					and not SceneMan:ShortestDistance(Owner.Pos, Act.Pos, false):MagnitudeIsGreaterThan(packRange) then
					kin = kin + 1;
				end
			end
			if kin < fleeBelow then
				Flee = Timer();
				SharedBehaviors.Trace(Owner, "hunt: too few of its kind left, runs");
			end
		end

		if waiting then
			AI.fire = false;
			AI.lateralMoveState = Actor.LAT_STILL;
			if gap > reach * 2 then
				CreatureBehaviors.SpitAt(AI, Owner, Target, gap, SpitTimer);
			end
		elseif Flee then
			-- Running from it: away for three seconds, with a leap now and then, and the hunt is over.
			if Flee:IsPastSimMS(3000) then
				SharedBehaviors.Trace(Owner, "hunt: ran");
				break;
			end
			AI.fire = false;
			AI.deviceState = ACrab.STILL;
			AI.lateralMoveState = dir > 0 and Actor.LAT_LEFT or Actor.LAT_RIGHT;
			Owner.HFlipped = dir > 0;
			if LeapTimer:IsPastSimMS(900) and math.random() < 0.05 then
				AI.Ctrl:SetState(Controller.BODY_LEAP, true);
				LeapTimer:Reset();
			end
		elseif gap <= reach then
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
			elseif chargeRange > 0 and gap < chargeRange and gap > reach * 1.5 and math.abs(ToTarget.Y) < Owner.MoveHeight * 0.6
				and ChargeTimer:IsPastSimMS(Value(Owner, "CreatureChargeCooldown", 3000))
				and SceneMan:CastStrengthSumRay(Owner.EyePos, Target.Pos, 4, rte.grassID) < 50 then
				-- Close, level and nothing in the way: a charge at it, a burst of speed (what it runs into, its trample sees to).
				AI.lateralMoveState = dir > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
				Owner.HFlipped = dir < 0;
				Owner:AddImpulseForce(Vector(dir * Value(Owner, "CreatureChargeSpeed", 9), -1) * Owner.Mass, Vector());
				ChargeTimer:Reset();
				ProgressTimer:Reset();
				SharedBehaviors.Trace(Owner, "hunt: charges");
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
				if Crawls(Owner) and (ToACrab(Owner).IsClinging or ToNext.Y < -Owner.MoveHeight * 0.3) then
					-- (Up a wall or along a ceiling, CR-10: crawled, not leapt.)
					CrawlTo(AI, Owner, ToNext);
					ProgressTimer:Reset();
				elseif math.abs(ToNext.X) > 3 then
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
	local chargeRange = Value(Owner, "CreatureChargeRange", 0);
	local Keep = Owner.AIMode == Actor.AIMODE_SENTRY and SharedBehaviors.RememberOrder(AI, Owner) or nil;
	local ProgressTimer = Timer();
	local ChargeTimer = Timer();
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
		if chargeRange > 0 and gap < chargeRange and gap > range * 1.5 and math.abs(ToTarget.Y) < Owner.MoveHeight * 0.6
			and ChargeTimer:IsPastSimMS(Value(Owner, "CreatureChargeCooldown", 3000))
			and SceneMan:CastStrengthSumRay(Owner.EyePos, Target.Pos, 4, rte.grassID) < 50 then
			-- Close, level and nothing in the way: a charge at it, a burst of speed (what it runs into, its trample sees to).
			local dir = ToTarget.X < 0 and -1 or 1;
			Owner.HFlipped = dir < 0;
			Owner:AddImpulseForce(Vector(dir * Value(Owner, "CreatureChargeSpeed", 9), -1) * Owner.Mass, Vector());
			ChargeTimer:Reset();
			ProgressTimer:Reset();
			SharedBehaviors.Trace(Owner, "charge: bursts at it");
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
	for Act in SharedBehaviors.ActorsNear(Owner.Pos, packRange) do
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

-- Lurking (CR-10): a creature that climbs ceilings and lurks, left on guard with no prey, climbs to a ceiling over its post and hangs
-- there in wait (its hunt then drops on prey that walks under it, HuntTarget). Called each update; it looks for a spot now and then, and
-- gives a spot up that it can't reach.
function CreatureBehaviors.LurkUpdate(AI, Owner)
	local Lurk = AI.Lurk;
	if AI.Target or Owner.AIMode ~= Actor.AIMODE_SENTRY or Value(Owner, "CreatureLurks", 0) <= 0 or Owner.ClassName ~= "ACrab" or Owner.Climbs < 2
		or Owner:IsPlayerControlled() or not CreatureBehaviors.MayChase(AI, Owner) then
		if Lurk and Lurk.Spot then
			Lurk.Spot = nil;
			Lurk.points = {};
		end
		return;
	end
	if not Lurk then
		Lurk = {points = {}, Timer = Timer(), GiveUp = Timer(), Retry = Timer()};
		Lurk.Retry.ElapsedSimTimeMS = 30000;
		AI.Lurk = Lurk;
	end
	local Crab = ToACrab(Owner);

	-- (Looking for a ceiling over its post: straight up, then a little to each side.)
	if not Lurk.Spot then
		if Crab.IsOnCeiling or not Lurk.Retry:IsPastSimMS(30000) then
			return;
		end
		Lurk.Retry:Reset();
		local height = Value(Owner, "CreatureLurkHeight", 300);
		for _, offsetX in ipairs({0, 80, -80, 160, -160}) do
			local Start = Vector(Owner.Pos.X + offsetX, Owner.Pos.Y - Owner.Radius);
			local Hit = Vector();
			if SceneMan:CastStrengthRay(Start, Vector(0, -height), 30, Hit, 3, rte.grassID, SceneMan.SceneWrapsX) then
				local Spot = Vector(Hit.X, Hit.Y + Owner.Radius * 0.6);
				if not SceneMan:CastStrengthRay(Owner.Pos, SceneMan:ShortestDistance(Owner.Pos, Spot, false), 30, Vector(), 3, rte.grassID, SceneMan.SceneWrapsX)
					or offsetX == 0 then
					Lurk.Spot = Spot;
					break;
				end
			end
		end
		if not Lurk.Spot then
			return;
		end
		Lurk.GiveUp:Reset();
		Lurk.Timer.ElapsedSimTimeMS = 2000;
		Lurk.points = {};
		SharedBehaviors.Trace(Owner, "lurk: climbs up to wait on the ceiling");
	end

	-- (Up there: it holds still and waits.)
	local To = SceneMan:ShortestDistance(Owner.Pos, Lurk.Spot, false);
	if Crab.IsOnCeiling and not To:MagnitudeIsGreaterThan(40) then
		AI.lateralMoveState = Actor.LAT_STILL;
		return;
	end
	if Lurk.GiveUp:IsPastSimMS(20000) then
		SharedBehaviors.Trace(Owner, "lurk: can't get up there, gives up");
		Lurk.Spot = nil;
		Lurk.points = {};
		Lurk.Retry:Reset();
		AI.lateralMoveState = Actor.LAT_STILL;
		return;
	end

	-- (A flier, C8: straight up to it and into the ceiling, asked to perch there.)
	if Owner.Flies > 0 then
		Crab:Perch();
		AI.lateralMoveState = Actor.LAT_STILL;
		AI.Ctrl.AnalogMove = SceneMan:ShortestDistance(Owner.Pos, Lurk.Spot + Vector(0, -Owner.Radius), false).Normalized;
		return;
	end

	-- (On the way: along a route to the spot, asked on the pathing threads, which keeps the goal on the ceiling where it is given.)
	if not Lurk.pending and Lurk.Timer:IsPastSimMS(2000) then
		Lurk.pending = true;
		Lurk.Timer:Reset();
		SceneMan.Scene:CalculatePathAsyncForActor(function(pathRequest)
			local points = {};
			if pathRequest.Status ~= PathRequest.NoSolution then
				for Point in pathRequest.Path do
					table.insert(points, Vector(Point.X, Point.Y));
				end
			end
			Lurk.points = points;
			Lurk.pending = false;
		end, Owner, Owner.Pos, Lurk.Spot, Owner.Team);
	end
	while #Lurk.points > 0 and not SceneMan:ShortestDistance(Owner.Pos, Lurk.points[1], false):MagnitudeIsGreaterThan(14) do
		table.remove(Lurk.points, 1);
	end
	CrawlTo(AI, Owner, SceneMan:ShortestDistance(Owner.Pos, Lurk.points[1] or Lurk.Spot, false));
end

-- Hunting from the air (C8, the attack behaviour of a creature that flies): after its prey along a route through the air (the engine's
-- flier route-follower, ACrab::MoveAlongRoute), and once near it in the open, by its way of attack (CreatureFlyAttack): a diver swoops
-- on it, bites and pulls away up and off to one side to come round again; a hoverer holds in the air off it and lets it have what it was
-- born with from there. Either spits too, when it has something to spit. One that scatters breaks off and flies away for a moment when it
-- is hurt; a tired one makes for a ceiling over it to perch on, if it roosts, and otherwise glides down to rest. Over as HuntTarget is.
function CreatureBehaviors.FlyHunt(AI, Owner, Abort)
	local Crab = ToACrab(Owner);
	AI.TargetLostTimer:Reset();
	AI.TargetLostTimer:SetSimTimeLimitMS(SharedBehaviors.TargetMemoryMS(4000));
	local reach = Value(Owner, "CreatureBiteReach", 30);
	local chaseRange = Value(Owner, "CreatureChaseRange", 900);
	local hoverRange = Value(Owner, "CreatureHoverRange", 40);
	local diveRange = Value(Owner, "CreatureDiveRange", 160);
	local attackRange = Value(Owner, "CreatureAttackRange", reach);
	local style = Owner:StringValueExists("CreatureFlyAttack") and Owner:GetStringValue("CreatureFlyAttack") or "Dive";
	local scatters = Value(Owner, "CreatureScatters", 0) > 0;
	local Keep = Owner.AIMode == Actor.AIMODE_SENTRY and SharedBehaviors.RememberOrder(AI, Owner) or nil;
	local SpitTimer = Timer();
	SpitTimer.ElapsedSimTimeMS = 60000; -- (Ready at once.)
	local PhaseTimer = Timer();
	local HurtTimer = Timer();
	local phase = "approach";
	local Away;
	local lastHealth = Owner.Health;
	local routed = false;
	Owner:SetNumberValue("CreatureHuntID", AI.Target.UniqueID);
	SharedBehaviors.Trace(Owner, "fly hunt: " .. AI.Target.PresetName .. " (" .. style .. ")");

	local function Finish(aborted)
		Owner:RemoveNumberValue("CreatureHuntID");
		if not aborted then
			AI.Target = nil;
		end
		AI.fire = false;
		AI.lateralMoveState = Actor.LAT_STILL;
		AI.deviceState = ACrab.STILL;
		Owner:ClearMovePath();
		if not aborted and Keep and Keep.post and Owner.AIMode == Actor.AIMODE_SENTRY and SceneMan:ShortestDistance(Owner.Pos, Keep.post, false):MagnitudeIsGreaterThan(Owner.MoveHeight) then
			SharedBehaviors.RestoreOrder(AI, Owner, Keep);
		end
		return true;
	end

	-- Along its air route after the prey (the follower plans it again as the prey moves).
	local function Chase(Target)
		if not routed then
			Owner:ClearMovePath();
			Owner:AddAIMOWaypoint(Target);
			routed = true;
		end
		Crab:MoveAlongRoute();
	end

	while true do
		local Target = AI.Target;
		if not Target or not MovableMan:ValidMO(Target) or Target:IsDead() then
			break;
		end
		local gap, ToTarget = CreatureBehaviors.GapTo(Owner, Target);
		if gap > chaseRange then
			SharedBehaviors.Trace(Owner, "fly hunt: got away");
			break;
		end
		local dir = ToTarget.X >= 0 and 1 or -1;
		local clear = SceneMan:CastStrengthSumRay(Owner.EyePos, Target.Pos, 4, rte.grassID) < 50;
		AI.deviceState = ACrab.AIMING;
		AI.Ctrl.AnalogAim = SceneMan:ShortestDistance(Owner.EyePos, Target.Pos, false).Normalized;
		AI.lateralMoveState = Actor.LAT_STILL;
		AI.fire = clear and gap <= attackRange;

		-- (Hurt, one that scatters breaks off: away from its prey and up, for a moment.)
		if scatters and phase ~= "scatter" and lastHealth - Owner.Health >= Owner.MaxHealth * 0.15 then
			phase = "scatter";
			PhaseTimer:Reset();
			Away = Vector(-dir, -1).Normalized;
			SharedBehaviors.Trace(Owner, "fly hunt: scatters");
		end
		if HurtTimer:IsPastSimMS(1000) then
			HurtTimer:Reset();
			lastHealth = Owner.Health;
		end

		if Crab.IsFlightTired then
			-- Tired: up into a ceiling close over it to perch, if it roosts; else it glides down and rests (ACrab::UpdateFlight).
			local Hit = Vector();
			if Owner.Climbs >= 2 and SceneMan:CastStrengthRay(Owner.Pos, Vector(0, -200), 30, Hit, 3, rte.grassID, SceneMan.SceneWrapsX) then
				Crab:Perch();
				AI.Ctrl.AnalogMove = Vector(0, -1);
			end
		elseif phase == "scatter" then
			AI.Ctrl.AnalogMove = Away;
			AI.fire = false;
			if PhaseTimer:IsPastSimMS(1200) then
				phase = "approach";
				routed = false;
			end
		elseif phase == "pull" then
			-- Pulling away from a dive: up and on past its prey, to come round again.
			AI.Ctrl.AnalogMove = SceneMan:ShortestDistance(Owner.Pos, Away, false).Normalized;
			if PhaseTimer:IsPastSimMS(900) or not SceneMan:ShortestDistance(Owner.Pos, Away, false):MagnitudeIsGreaterThan(20) then
				phase = "approach";
				routed = false;
			end
		elseif style == "Hover" then
			-- Held in the air off its prey, above and to the side it came from, and lets it have it from there.
			local Spot = Target.Pos + Vector(-dir * hoverRange, -hoverRange * 0.5);
			local ToSpot = SceneMan:ShortestDistance(Owner.Pos, Spot, false);
			if clear and gap < math.max(hoverRange * 3, 200) and SceneMan:CastStrengthSumRay(Owner.Pos, Spot, 4, rte.grassID) < 50 then
				AI.Ctrl.AnalogMove = ToSpot.Magnitude > 4 and ToSpot:SetMagnitude(math.min(ToSpot.Magnitude / 40, 1)) or Vector();
				Owner.HFlipped = dir < 0;
				routed = false;
			else
				Chase(Target);
			end
			if gap > reach * 2 then
				CreatureBehaviors.SpitAt(AI, Owner, Target, gap, SpitTimer);
			end
		else
			-- A diver: at its prey flat out once close and in the open, biting in reach, then away.
			if clear and gap < diveRange then
				AI.Ctrl.AnalogMove = ToTarget.Normalized;
				Owner.HFlipped = dir < 0;
				routed = false;
				if gap <= reach then
					AI.fire = true;
					phase = "pull";
					PhaseTimer:Reset();
					Away = Target.Pos + Vector(dir * 120, -110);
				end
			else
				if gap > reach * 2 then
					CreatureBehaviors.SpitAt(AI, Owner, Target, gap, SpitTimer);
				end
				Chase(Target);
			end
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then
			return Finish(true);
		end
	end

	return Finish(false);
end

