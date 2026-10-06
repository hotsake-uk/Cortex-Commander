SharedBehaviors = {};

function SharedBehaviors.GetTeamShootingSkill(team)
	local skill = 80;
	local Activ = ActivityMan:GetActivity();
	if Activ then
		-- i am fancy mathematician, doing fancy mathematics
		-- this weigh actor skill heavily towards 100
		local num = (Activ:GetTeamAISkill(team)/100);
		skill = (1 - math.pow(1 - num, 3)) * 100;
	end

	local aimSpeed, aimSkill;
	if skill >= Activity.UNFAIRSKILL then
		aimSpeed = 0.04;
		aimSkill = 0.04;
	else
		-- the AI shoot sooner and with slightly better precision
		aimSpeed = 1/(0.65/(2.9-math.exp(skill*0.01)));
		aimSkill = 1/(0.75/(3.0-math.exp(skill*0.01)));
	end
	return aimSpeed, aimSkill, skill;
end

function SharedBehaviors.ProcessAlarmEvent(AI, Owner)
	AI.AlarmPos = nil;

	local loudness, AlarmVec;
	local canSupress = not AI.flying and Owner.FirearmIsReady and Owner.EquippedItem:HasObjectInGroup("Weapons - Explosive");
	for Event in MovableMan.AlarmEvents do
		if Event.Team ~= Owner.Team then	-- caused by some other team's activities - alarming!
			loudness = Owner.AimDistance + Owner.Perceptiveness * Event.Range;
			AlarmVec = SceneMan:ShortestDistance(Owner.EyePos, Event.ScenePos, false); -- see how far away the alarm situation is
			if AlarmVec.Largest < loudness then	-- only react if the alarm is within hearing range
				-- if our relative position to the alarm location is the same, don't repeat the signal
				-- check if we have line of sight to the alarm point
				if (not AI.LastAlarmVec or SceneMan:ShortestDistance(AI.LastAlarmVec, AlarmVec, false):MagnitudeIsGreaterThan(25)) then
					AI.LastAlarmVec = AlarmVec;

					if AlarmVec.Largest < 100 then
						-- check more carfully at close range, and allow hearing of partially blocked alarm events
						if SceneMan:CastStrengthSumRay(Owner.EyePos, Event.ScenePos, 4, rte.grassID) < 100 then
							AI.AlarmPos = Vector(Event.ScenePos.X, Event.ScenePos.Y);
						end
					elseif not SceneMan:CastStrengthRay(Owner.EyePos, AlarmVec, 6, Vector(), 8, rte.grassID, true) then
						AI.AlarmPos = Vector(Event.ScenePos.X, Event.ScenePos.Y);
					end

					if AI.AlarmPos then
						Owner:SetAlarmPoint(AI.AlarmPos);
						AI:CreateFaceAlarmBehavior(Owner);
						return true;
					end
				end
			-- sometimes try to shoot back at enemies outside our view range (0.5 is the range of the brain alarm)
			elseif canSupress and Event.Range > 0.5 and PosRand() > (0.3/AI.aimSkill) and
				AlarmVec.Largest < FrameMan.PlayerScreenWidth * 1.8 and
				(not AI.LastAlarmVec or SceneMan:ShortestDistance(AI.LastAlarmVec, AlarmVec, false).Largest > 30)
			then
				-- only do this if we are facing the shortest distance to the alarm event
				local AimOwner = SceneMan:ShortestDistance(Owner.EyePos, Owner.ViewPoint, false).Normalized;
				local AlarmNormal = AlarmVec.Normalized;
				local dot = AlarmNormal.X * AimOwner.X + AlarmNormal.Y * AimOwner.Y;
				if dot > 0.2 then
					-- check LOS
					local ID = SceneMan:CastMORay(Owner.EyePos, AlarmVec, Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, false, 11);
					if ID ~= rte.NoMOID then
						local FoundMO = MovableMan:GetMOFromID(ID);
						if FoundMO then
							FoundMO = FoundMO:GetRootParent();

							if not FoundMO.EquippedItem or not FoundMO.EquippedItem:HasObjectInGroup("Weapons - Explosive") then
								FoundMO = nil; -- don't shoot at without weapons or actors using tools
							end

							if FoundMO and FoundMO:GetController() and FoundMO:GetController():IsState(Controller.WEAPON_FIRE) and FoundMO.Vel.Largest < 20 then
								-- compare the enemy aim angle with the angle of the alarm vector
								local AimEnemy = SceneMan:ShortestDistance(FoundMO.EyePos, FoundMO.ViewPoint, false).Normalized;
								local dot = AlarmNormal.X * AimEnemy.X + AlarmNormal.Y * AimEnemy.Y;
								if dot < -0.5 then
									-- this actor is shooting in our direction
									AI.ReloadTimer:Reset();
									AI.TargetLostTimer:Reset();

									-- try to shoot back
									AI.UnseenTarget = FoundMO;
									AI:CreateSuppressBehavior(Owner);

									AI.AlarmPos = Event.ScenePos;
									return true;
								end
							end
						end
					else
						AI.LastAlarmVec = AlarmVec; -- don't look here again if the raycast failed
						AI.LastAlarmVec = nil;
					end
				end
			end
		end
	end
end

-- look at the alarm event
function SharedBehaviors.FaceAlarm(AI, Owner, Abort)
	if AI.AlarmPos then
		local AlarmDist = SceneMan:ShortestDistance(Owner.EyePos, AI.AlarmPos, false);
		AI.AlarmPos = nil;
		for _ = 1, math.ceil(200/TimerMan.AIDeltaTimeMS) do
			AI.deviceState = AHuman.AIMING;
			if not Owner.aggressive then
				AI.lateralMoveState = Actor.LAT_STILL;
			end
			AI.Ctrl.AnalogAim = AlarmDist.Normalized;
			local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
			if _abrt then return true end
		end
	end
	return true;
end

-- find the closest enemy brain
function SharedBehaviors.BrainSearch(AI, Owner, Abort) 
	if AI.PlayerPreferredHD then
		Owner:EquipNamedDevice(AI.PlayerPreferredHD, true);
	end

	local Brains = {};
	for Act in MovableMan.Actors do
		if Act.Team ~= Owner.Team and Act:HasObjectInGroup("Brains") then
			table.insert(Brains, Act);
		end
	end

	if #Brains < 1 then	-- no brain actors found, check if some other actor is the brain
		local GmActiv = ActivityMan:GetActivity();
		for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
			if GmActiv:PlayerActive(player) and GmActiv:GetTeamOfPlayer(player) ~= Owner.Team then
				local Act = GmActiv:GetPlayerBrain(player);
				if Act and MovableMan:IsActor(Act) then
					table.insert(Brains, Act);
				end
			end
		end
	end

	if #Brains > 0 then
		if #Brains == 1 then
			if MovableMan:IsActor(Brains[1]) then
				Owner:ClearAIWaypoints();
				Owner:AddAIMOWaypoint(Brains[1]);
				AI:CreateGoToBehavior(Owner);
			end
		else	-- lobotomy test
			local ClosestBrain;
			local minDist = math.huge;
			for _, Act in pairs(Brains) do
				-- measure how easy the path to the destination is to traverse
				if MovableMan:IsActor(Act) then
					Owner:ClearAIWaypoints();
					Owner:AddAISceneWaypoint(Act.Pos);
					Owner:UpdateMovePath();

					-- wait until movepath is updated
					while Owner.IsWaitingOnNewMovePath do
						local _ai, _ownr, _abrt = coroutine.yield();
						if _abrt then return true end
					end

					local OldWpt, deltaY;
					local index = 0;
					local height = 0;
					local pathLength = 0;
					local pathObstMaxHeight = 0;

					local PathDump = {}
					-- copy the MovePath to a temporary table so we can yield safely while working on the path
					for WptPos in Owner.MovePath do
						table.insert(PathDump, WptPos);
					end

					for _, Wpt in pairs(PathDump) do
						pathLength = pathLength + 1;
						if OldWpt then
							deltaY = OldWpt.Y - Wpt.Y;
							if deltaY > 20 then	-- Wpt is more than n pixels above OldWpt in the scene
								if deltaY / math.abs(SceneMan:ShortestDistance(OldWpt, Wpt, false).X) > 1 then	-- the slope is more than 45 degrees
									height = height + (OldWpt.Y - Wpt.Y);
									pathObstMaxHeight = math.max(pathObstMaxHeight, height);
								else
									height = 0;
								end
							else
								height = 0;
							end
						end

						OldWpt = Wpt;

						if index > 20 then
							index = 0;
							local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
							if _abrt then return true end
						else
							index = index + 1;
						end
					end

					local score = pathLength * 0.55 + math.floor(pathObstMaxHeight/27) * 8;
					if score < minDist then
						minDist = score;
						ClosestBrain = Act;
					end
				end
			end

			--Owner:ClearAIWaypoints(); -- this part freezes the script when facing the opposing brain

			if MovableMan:IsActor(ClosestBrain) then
				Owner:ClearAIWaypoints(); -- moving the function here fixes it (4zK)
				Owner:AddAIMOWaypoint(ClosestBrain);
				AI:CreateGoToBehavior(Owner);
			else
				return true; -- the brain we found died while we where searching, restart this behavior next frame
			end
		end
	else	-- no enemy brains left
		AI:CreateSentryBehavior(Owner);
	end

	return true;
end

function SharedBehaviors.Patrol(AI, Owner, Abort)
	while AI.flying or Owner.Vel:MagnitudeIsGreaterThan(4) do	-- wait until we are stationary
		return true;
	end

	if Owner.ClassName == "AHuman" then
		if AI.PlayerPreferredHD then
			Owner:EquipNamedDevice(AI.PlayerPreferredHD, true);
		elseif not Owner:EquipDeviceInGroup("Weapons - Primary", true) then
			Owner:EquipDeviceInGroup("Weapons - Secondary", true);
		end
	end

	local Free = Vector();
	local WptA, WptB;

	-- look for a path to the right
	SceneMan:CastObstacleRay(Owner.Pos, Vector(512, 0), Vector(), Free, Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 4);
	local Dist = SceneMan:ShortestDistance(Owner.Pos, Free, false);

	if Dist:MagnitudeIsGreaterThan(20) then
		Owner:ClearAIWaypoints();
		Owner:AddAISceneWaypoint(Free);
		Owner:UpdateMovePath();

		-- wait until movepath is updated
		while Owner.IsWaitingOnNewMovePath do
			local _ai, _ownr, _abrt = coroutine.yield();
			if _abrt then return true end
		end

		local PrevPos = Vector(Owner.Pos.X, Owner.Pos.Y);
		for WptPos in Owner.MovePath do
			if math.abs(PrevPos.Y - WptPos.Y) > 14 then
				break;
			end

			WptA = Vector(PrevPos.X, PrevPos.Y);
			PrevPos:SetXY(WptPos.X, WptPos.Y);
		end
	end

	-- look for a path to the left
	SceneMan:CastObstacleRay(Owner.Pos, Vector(-512, 0), Vector(), Free, Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 4);
	Dist = SceneMan:ShortestDistance(Owner.Pos, Free, false);

	if Dist:MagnitudeIsGreaterThan(20) then
		Owner:ClearAIWaypoints();
		Owner:AddAISceneWaypoint(Free);
		Owner:UpdateMovePath();

		-- wait until movepath is updated
		while Owner.IsWaitingOnNewMovePath do
			local _ai, _ownr, _abrt = coroutine.yield();
			if _abrt then return true end
		end

		local PrevPos = Vector(Owner.Pos.X, Owner.Pos.Y);
		for WptPos in Owner.MovePath do
			if math.abs(PrevPos.Y - WptPos.Y) > 14 then
				break;
			end

			WptB = Vector(PrevPos.X, PrevPos.Y);
			PrevPos:SetXY(WptPos.X, WptPos.Y);
		end
	end

	Owner:ClearAIWaypoints();
	local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
	if _abrt then return true end

	if WptA then
		Dist = SceneMan:ShortestDistance(Owner.Pos, WptA, false);
		if Dist:MagnitudeIsGreaterThan(20) then
			Owner:AddAISceneWaypoint(WptA);
		else
			WptA = nil;
		end
	end

	if WptB then
		Dist = SceneMan:ShortestDistance(Owner.Pos, WptB, false);
		if Dist:MagnitudeIsGreaterThan(20) then
			Owner:AddAISceneWaypoint(WptB);
		else
			WptB = nil;
		end
	end

	if WptA or WptB then
		AI:CreateGoToBehavior(Owner);
	else	-- no path was found
		local FlipTimer = Timer();
		FlipTimer:SetSimTimeLimitMS(3000);
		while true do
			local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
			if _abrt then return true end
			if FlipTimer:IsPastSimTimeLimit() then
				FlipTimer:Reset();
				FlipTimer:SetSimTimeLimitMS(RangeRand(2000, 5000));
				Owner.HFlipped = not Owner.HFlipped	-- turn around and try the other direction sometimes
				if PosRand() < 0.3 then
					break; -- end the behavior
				end
			end
		end
	end
	return true;
end

-- sharp aim at an area where we expect the enemy to be
function SharedBehaviors.PinArea(AI, Owner, Abort)
	if AI.OldTargetPos then
		local AlarmDist = SceneMan:ShortestDistance(Owner.EyePos, AI.OldTargetPos, false);
		for _ = 1, math.ceil(math.random(1000, 3000)/TimerMan.AIDeltaTimeMS) do
			AI.deviceState = AHuman.AIMING;
			AI.lateralMoveState = Actor.LAT_STILL;
			AlarmDist:SetXY(AlarmDist.X+RangeRand(-5,5), AlarmDist.Y+RangeRand(-5,5));
			AI.Ctrl.AnalogAim = AlarmDist.Normalized;
			local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
			if _abrt then return true end
		end
	end
	return true;
end

function SharedBehaviors.GetRealVelocity(Owner)
	-- Calculate a velocity based on our actual movement. This is because otherwise gravity falsely reports that we have a downward velocity, even if our net movement is zero.
	-- Note - we use normal delta time, not AI delta time, because PrevPos is updated per-tick (not per-AI-tick)
	return (Owner.Pos - Owner.PrevPos) / TimerMan.DeltaTimeSecs;
end

function SharedBehaviors.UpdateAverageVel(Owner, AverageVel)
	-- Store an exponential moving average of our speed over the past seconds
	local timeInSeconds = 1;

	local ticksPerTime = timeInSeconds / TimerMan.AIDeltaTimeSecs;
	AverageVel = AverageVel - (AverageVel / ticksPerTime);
	AverageVel = AverageVel + (SharedBehaviors.GetRealVelocity(Owner) / ticksPerTime);

	return AverageVel;
end

-- move to the next waypoint
-- Where a waypoint is really meant: on the ground under it, unless it is inside the ground already, in which case it is a place to dig to and
-- is left alone. (MovePointToGround lifts a buried point by the height given, every time it is asked, and a digger never got to it.)
function SharedBehaviors.WaypointOnGround(pos, height)
	if SceneMan:GetTerrMatter(pos.X, pos.Y) ~= rte.airID then
		return Vector(pos.X, pos.Y);
	end
	return SceneMan:MovePointToGround(pos, height * 0.2, 4);
end

function SharedBehaviors.GoToWpt(AI, Owner, Abort)
	-- check if we have arrived
	if not (Owner.AIMode == Actor.AIMODE_SQUAD or Owner:GetWaypointListSize() > 0) then
		if not Owner.MOMoveTarget then
			if SceneMan:ShortestDistance(Owner:GetLastAIWaypoint(), Owner.Pos, false).Largest < Owner.Height * 0.15 then
				Owner:ClearAIWaypoints();
				Owner:ClearMovePath();
				Owner:DrawWaypoints(false);
				AI:CreateSentryBehavior(Owner);

				if Owner.AIMode == Actor.AIMODE_GOTO then
					AI.SentryFacing = Owner.HFlipped; -- guard this direction
					AI.SentryPos = Vector(Owner.Pos.X, Owner.Pos.Y); -- guard this point
				end

				return true;
			end
		end
	end

	-- is Y1 lower down in the scene, compared to Y2?
	local Lower = function(Y1, Y2, margin)
		return Y1.Pos and Y2.Pos and (Y1.Pos.Y - margin > Y2.Pos.Y);
	end

	local ArrivedTimer = Timer();
	local UpdatePathTimer = Timer();

	if Owner.MOMoveTarget then
		UpdatePathTimer:SetSimTimeLimitMS(RangeRand(7000, 8000));
	else
		UpdatePathTimer:SetSimTimeLimitMS(RangeRand(12000, 14000));
	end

	local NoLOSTimer = Timer();
	NoLOSTimer:SetSimTimeLimitMS(1000);

	local StuckTimer = Timer();
	local StuckDirectionTimer = Timer(); -- A direction tried when stuck is kept for a moment before another is tried.
	StuckDirectionTimer:SetSimTimeLimitMS(500);
	local StuckJumped = false; -- One jetpack try per time stuck.
	local StuckJumpTimer = Timer(); -- How long that try is let burn.
	StuckJumpTimer:SetSimTimeLimitMS(350);
	StuckTimer:SetSimTimeLimitMS(1000);
	local AverageVel = Owner.Vel;

	local nextLatMove = AI.lateralMoveState;
	local nextAimAngle = Owner:GetAimAngle(false) * 0.95;
	local scanAng = 0; -- for obstacle detection
	local Obstacles = {};
	local PrevWptPos = Vector(Owner.Pos.X, Owner.Pos.Y);
	local sweepCW = true;
	local sweepRange = 0;
	local digState = AHuman.NOTDIGGING;
	local obstacleState = Actor.PROCEEDING;
	local Obst = {R_LOW = 1, R_FRONT = 2, R_HIGH = 3, R_UP = 5, L_UP = 6, L_HIGH = 8, L_FRONT = 9, L_LOW = 10};
	local Facings = {{aim=0, facing=0}, {aim=1.4, facing=1.4}, {aim=1.4, facing=math.pi-1.4}, {aim=0, facing=math.pi}};

	local NeedsNewPath, Waypoint, HasMovePath, Dist, CurrDist, NextWptPos;
	local ClimbStepX = 0; -- The sideways step off the top of a jetpack climb, kept up a moment after the jet goes out.
	local LastTracedKind = -2;
	local ClimbStepTimer = Timer();
	ClimbStepTimer:SetSimTimeLimitMS(700);
	local ProneHoldTimer = Timer(); -- How long a crawl is kept up after the way ahead looks clear.
	ProneHoldTimer:SetSimTimeLimitMS(1200);
	NeedsNewPath = true;
	AI.jetClimb = false; -- A climb from a previous order or path is over.

	Owner:RemoveNumberValue("AI_StuckForTime");

	while true do
		Waypoint = nil;
		HasMovePath = false;
		NextWptPos = nil; -- The waypoint after this one, so a climb knows which way it steps off at the top.

		-- ugh
		local wptIndex = 0;
		for pos in Owner.MovePath do
			wptIndex = wptIndex + 1;
			if wptIndex == 1 then
				HasMovePath = true;
				Waypoint = {};
				Waypoint.Pos = pos;
				Waypoint.Type = nil;
				-- What the pathfinder meant by this step (0 walk, 1 crawl, 2 jump, 3 fall, 4 dig, 5 door), so it needn't be guessed from the
				-- ground: a step off an edge is walked off, not hopped; a jump is jetted whatever the slope looks like; a crawl is gone prone for.
				Waypoint.Kind = Owner.MovePathStepKind;
				if Owner:NumberValueExists("AITrace") and Waypoint.Kind ~= LastTracedKind then
					LastTracedKind = Waypoint.Kind;
					ConsoleMan:PrintString("AITRACE step kind " .. tostring(Waypoint.Kind) .. " to " .. math.floor(pos.X) .. "," .. math.floor(pos.Y) .. " from " .. math.floor(Owner.Pos.X) .. "," .. math.floor(Owner.Pos.Y) .. " prone " .. tostring(AI.proneState == AHuman.PRONE));
				end
				if Owner.MovePathSize == 1 then
					Waypoint.Type = "last";
				end
				if Waypoint.Kind == 3 then
					Waypoint.Type = "drop";
				end
			else
				NextWptPos = pos;
				break;
			end
		end

		if Waypoint ~= nil and Waypoint.Type ~= "air" then
			local Free = Vector();

			-- only if we have a digging tool
			if Waypoint.Type ~= "drop" and Owner:HasObjectInGroup("Tools - Diggers") then
				local PathSegRay = SceneMan:ShortestDistance(PrevWptPos, Waypoint.Pos, false); -- detect material blocking the path and start digging through it
				if AI.teamBlockState ~= Actor.BLOCKED and SceneMan:CastStrengthRay(PrevWptPos, PathSegRay, 4, Free, 2, rte.doorID, true) then
					if SceneMan:ShortestDistance(Owner.Pos, Free, false):MagnitudeIsLessThan(Owner.Height*0.4) then	-- check that we're close enough to start digging
						digState = AHuman.STARTDIG;
						AI.deviceState = AHuman.DIGGING;
						obstacleState = Actor.DIGPAUSING;
						nextLatMove = Actor.LAT_STILL;
						sweepRange = math.min(math.pi*0.25, Owner.AimRange);
						StuckTimer:SetSimTimeLimitMS(6000);
					else
						digState = AHuman.NOTDIGGING;
						obstacleState = Actor.PROCEEDING;
					end
				else
					digState = AHuman.NOTDIGGING;
					obstacleState = Actor.PROCEEDING;
					StuckTimer:SetSimTimeLimitMS(1000);
				end
			end

			if digState == AHuman.NOTDIGGING and AI.deviceState ~= AHuman.DIGGING then
				-- if our path isn't blocked enough to dig, but the headroom is too little, start crawling to get through
				local heading = SceneMan:ShortestDistance(Owner.Pos, Waypoint.Pos, false):SetMagnitude(Owner.Height*0.5);

				-- This gets the angle of the heading vector relative to flat (i.e, straight along the X axis)
				-- This gives a range of [0, 90]
				-- 0 is pointing straight left/right, and 90 is pointing straight up/down.
				local angleRadians = math.abs(math.atan2(-(heading.X * heading.Y), heading.X * heading.X));
				local angleDegrees = angleRadians * (180 / math.pi);

				-- We only crawl it it's quite flat, otherwise climb
				local crawlThresholdDegrees = 30;
				if angleDegrees <= crawlThresholdDegrees and Owner.Head and Owner.Head:IsAttached() then
					-- Where the top of the head is when standing (a few pixels over it: a doorway that would just scrape it was walked into standing,
					-- and that is a wall). Not where the head is now: prone, it's lower, the way looked clear, the unit stood up into the ceiling, and
					-- so on every tick.
					local topHeadPos = Owner.Pos - Vector(0, Owner.Height * 0.3 + 5);

					-- first check up to the top of the head, and then from there forward
					if Waypoint.Kind == 1 or SceneMan:CastStrengthRay(Owner.Pos, topHeadPos - Owner.Pos, 5, Free, 4, rte.doorID, true) or SceneMan:CastStrengthRay(topHeadPos, heading, 5, Free, 4, rte.doorID, true) then
						if Owner:NumberValueExists("AITrace") and AI.proneState ~= AHuman.PRONE then ConsoleMan:PrintString("AITRACE crawl: going prone, wpt dx " .. math.floor(heading.X) .. " dy " .. math.floor(heading.Y)); end
						AI.proneState = AHuman.PRONE;
						ProneHoldTimer:Reset();
					elseif AI.proneState ~= AHuman.PRONE or ProneHoldTimer:IsPastSimTimeLimit() then
						-- (Kept down a moment after the way looks clear: a crawl through a slot was stood up in the middle of.)
						if Owner:NumberValueExists("AITrace") and AI.proneState == AHuman.PRONE then ConsoleMan:PrintString("AITRACE crawl: standing up"); end
						AI.proneState = AHuman.NOTPRONE;
					end
				else
					AI.proneState = AHuman.NOTPRONE;
				end
			end
		end

		if Waypoint == nil or not Waypoint.Type then
			ArrivedTimer:SetSimTimeLimitMS(100);
		elseif Waypoint.Type == "last" then
			ArrivedTimer:SetSimTimeLimitMS(300);
		else	-- air or corner wpt
			ArrivedTimer:SetSimTimeLimitMS(0);
		end

		AverageVel = SharedBehaviors.UpdateAverageVel(Owner, AverageVel);

		local stuckThreshold = 2.5; -- pixels per second of movement we need to be considered not stuck

		-- Cap AverageVel, so if we have a spike in velocity it doesn't take too long to come back down
		AverageVel:CapMagnitude(stuckThreshold * 5)

		-- Reset our stuck timer if we're moving
		if AverageVel:MagnitudeIsGreaterThan(stuckThreshold) then
			if StuckTimer:IsPastSimTimeLimit() then
				Owner:RemoveNumberValue("AI_StuckForTime");
			end
			StuckTimer:Reset();
			StuckJumped = false;
		end

		if AI.refuel and Owner.Jetpack then
			-- if jetpack is full or we are falling we can stop refuelling
			if Owner.Jetpack.JetTimeLeft > Owner.Jetpack.JetTimeTotal * 0.98 or (AI.flying and Owner.Vel.Y < -3 and Owner.Jetpack.JetTimeLeft > AI.minBurstTime*2) then
				AI.refuel = false;
			elseif not AI.flying then
				-- No jetting until there's fuel, but walking goes on: standing still until the tank was nearly full was the long pauses at the foot of every ledge.
				-- (Towards the waypoint: the direction left over from a brake or a climb could be away from it, or into the wall.)
				AI.jump = false;
				if CurrDist then
					nextLatMove = CurrDist.X < -3 and Actor.LAT_LEFT or (CurrDist.X > 3 and Actor.LAT_RIGHT or Actor.LAT_STILL);
				end
			end
		elseif UpdatePathTimer:IsPastSimTimeLimit() then
			UpdatePathTimer:Reset();

			AI.deviceState = AHuman.STILL;
			AI.proneState = AHuman.NOTPRONE;
			AI.jump = false;
			nextLatMove = Actor.LAT_STILL;
			digState = AHuman.NOTDIGGING;
			Waypoint = nil;
			NeedsNewPath = true; -- update the path
			AI.jetClimb = false;
		elseif StuckTimer:IsPastSimTimeLimit() then	-- dislodge
			Owner:SetNumberValue("AI_StuckForTime", StuckTimer.ElapsedSimTimeMS);
			if AI.jump then
				if Owner.Jetpack and Owner.Jetpack.JetTimeLeft < AI.minBurstTime then	-- out of fuel
					AI.jump = false;
					AI.refuel = true;
					nextLatMove = Actor.LAT_STILL;
				else
					local chance = PosRand();
					if chance < 0.1 then
						nextLatMove = Actor.LAT_LEFT;
					elseif chance > 0.9 then
						nextLatMove = Actor.LAT_RIGHT;
					else
						nextLatMove = Actor.LAT_STILL;
					end
				end
			else
				local updateInterval = SettingsMan.AIUpdateInterval;
				-- The chances are per second of game time, spread over the AI's ticks: the old sums came out at over half a chance per tick, so a stuck unit
				-- flipped direction and dropped prone and stood up at thirty times a second, which is the twitching players saw.
				local ticksPerSecond = 60 / math.max(updateInterval, 1);
				local flipChance = 1 - (1 - 0.6) ^ (1 / ticksPerSecond); -- about once every two seconds
				local proneChance = 1 - (1 - 0.15) ^ (1 / ticksPerSecond); -- about once every seven seconds
				if StuckDirectionTimer:IsPastSimTimeLimit() and PosRand() < flipChance then
					nextLatMove = AI.lateralMoveState == Actor.LAT_LEFT and Actor.LAT_RIGHT or Actor.LAT_LEFT;
					StuckDirectionTimer:Reset();
				end
				if PosRand() < proneChance then
					AI.proneState = AI.proneState == AHuman.PRONE and AHuman.NOTPRONE or AHuman.PRONE;
				end
				-- A jetpack is the usual way off whatever we're stuck on: one burst, when there's fuel and head room on the side we're heading.
				-- Not when the goal is below us, though: a digger stuck in its own shaft was blown out of it.
				local goalBelow = SceneMan:ShortestDistance(Owner.Pos, Owner:GetLastAIWaypoint(), false).Y > Owner.Height * 0.3;
				if Owner.Jetpack and Owner.Jetpack.JetpackType == AEJetpack.Standard and Owner.Jetpack.JetTimeLeft >= AI.minBurstTime and not StuckJumped and not goalBelow then
					local headRoom = (nextLatMove == Actor.LAT_LEFT and not Obstacles[Obst.L_UP]) or (nextLatMove ~= Actor.LAT_LEFT and not Obstacles[Obst.R_UP]);
					if headRoom then
						AI.jump = true;
						StuckJumped = true;
						StuckJumpTimer:Reset();
						if Owner:NumberValueExists("AITrace") then ConsoleMan:PrintString("AITRACE jet: stuck"); end
					end
				elseif StuckJumped and StuckJumpTimer:IsPastSimTimeLimit() then
					AI.jump = false;
				end
				-- refuelling done
				if AI.refuel and Owner.Jetpack and Owner.Jetpack.JetpackType == AEJetpack.Standard and Owner.Jetpack.JetTimeLeft >= Owner.Jetpack.JetTimeTotal * 0.99 then
					AI.jump = true;
				end
			end
		elseif not NeedsNewPath then	-- we have a list of waypoints, follow it
			if Owner.MovePathSize == 0 then	-- arrived
				if Owner.MOMoveTarget then -- following actor
					if Owner.MOMoveTarget:IsActor() then
						local Trace = SceneMan:ShortestDistance(Owner.Pos, Owner.MOMoveTarget.Pos, false);
						if Trace.Largest < Owner.Height * 0.5 + (Owner.MOMoveTarget.Height or 100) * 0.5 and
							SceneMan:CastStrengthRay(Owner.Pos, Trace, 5, Vector(), 4, rte.grassID, true)
						then -- add a waypoint if the MOMoveTarget is close and in LOS
							Waypoint = {Pos=SceneMan:MovePointToGround(Owner.MOMoveTarget.Pos, Owner.Height*0.2, 4)};
						else
							NeedsNewPath = true; -- update the path
							AI.jetClimb = false;
						end
					end
				else	-- moving towards a scene point
					local GroundPos = SharedBehaviors.WaypointOnGround(Owner:GetLastAIWaypoint(), Owner.Height);
					if SceneMan:ShortestDistance(GroundPos, Owner.Pos, false).Largest < Owner.Height * 0.4 then
						if Owner.AIMode == Actor.AIMODE_GOTO then
							AI.SentryFacing = Owner.HFlipped; -- guard this direction
							AI.SentryPos = Vector(Owner.Pos.X, Owner.Pos.Y); -- guard this point
							AI:CreateSentryBehavior(Owner);
						end

						Owner:ClearAIWaypoints();
						Owner:ClearMovePath();
						Owner:DrawWaypoints(false);
						return true;
					else
						-- The path ran out short of the goal (a buried goal that the dig hasn't reached, say): another path, not a wait for the
						-- next scheduled one with the stuck handling going off in the meantime.
						NeedsNewPath = true;
					end
				end
			else
				if Waypoint then
					if Owner.MOMoveTarget and MovableMan:ValidMO(Owner.MOMoveTarget) then
						local Trace = SceneMan:ShortestDistance(Owner.Pos, Owner.MOMoveTarget.Pos, false);

						-- WTF is the following code for? It causes us to idle and do nothing forever??
						if Owner.MOMoveTarget.Team == Owner.Team then
							if Trace.Largest > Owner.Height * 0.3 + (Owner.MOMoveTarget.Height or 100) * 0.3 then
								Waypoint.Pos = Owner.MOMoveTarget.Pos;
							else	-- arrived
								if not AI.flying then
									while true do
										StuckTimer:Reset();
										UpdatePathTimer:Reset();
										AI.lateralMoveState = Actor.LAT_STILL;
										AI.jump = false;

										local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
										if _abrt then return true end

										if Owner.MOMoveTarget and MovableMan:ValidMO(Owner.MOMoveTarget) then
											Trace = SceneMan:ShortestDistance(Owner.Pos, Owner.MOMoveTarget.Pos, false);
											if Trace.Largest > Owner.Height * 0.4 + (Owner.MOMoveTarget.Height or 100) * 0.4 or
												SceneMan:CastStrengthRay(Owner.Pos, Trace, 5, Vector(), 4, rte.doorID, true)
											then
												Waypoint = nil;
												NeedsNewPath = true; -- update the path
												AI.jetClimb = false;
												break;
											end
										else -- MOMoveTarget gone
											return true;
										end
									end
								end
							end
						elseif Trace.Largest < Owner.Height * 0.33 + (Owner.MOMoveTarget.Height or 100) * 0.33 then -- enemy MO
							Waypoint.Pos = Owner.MOMoveTarget.Pos;
						end
					end

					if Waypoint then
						CurrDist = SceneMan:ShortestDistance(Owner.Pos, Waypoint.Pos, false);

						-- digging
						if digState ~= AHuman.NOTDIGGING then
							if not AI.Target and Owner:EquipDiggingTool(true) then	-- switch to the digger if we have one
								if Owner.FirearmIsEmpty then	-- reload if it's empty
									AI.fire = false;
									AI.Ctrl:SetState(Controller.WEAPON_RELOAD, true);
								else
									if AI.teamBlockState == Actor.BLOCKED then
										AI.fire = false;
										nextLatMove = Actor.LAT_STILL;
									else
										if obstacleState == Actor.PROCEEDING then
											if CurrDist.X < -1 then
												nextLatMove = Actor.LAT_LEFT;
											elseif CurrDist.X > 1 then
												nextLatMove = Actor.LAT_RIGHT;
											end
										else
											nextLatMove = Actor.LAT_STILL;
										end

										-- check if we are close enough to dig
										if SceneMan:ShortestDistance(PrevWptPos, Owner.Pos, false):MagnitudeIsGreaterThan(Owner.Height*0.5) and
											 SceneMan:ShortestDistance(Owner.Pos, Waypoint.Pos, false):MagnitudeIsGreaterThan(Owner.Height*0.5)
										then
											digState = AHuman.NOTDIGGING;
											obstacleState = Actor.PROCEEDING;
											AI.deviceState = AHuman.STILL;
											AI.fire = false;
											Owner:EquipFirearm(true);
										else
											-- see if we have dug out all that we can in the sweep area without moving closer
											local centerAngle = CurrDist.AbsRadAngle;
											local Ray = Vector(Owner.Height*0.3, 0):RadRotate(centerAngle); -- center
											if SceneMan:CastNotMaterialRay(Owner.Pos, Ray, 0, 3, false) < 0 then
												-- now check the tunnel's thickness
												Ray = Vector(Owner.Height*0.3, 0):RadRotate(centerAngle + sweepRange); -- up
												if SceneMan:CastNotMaterialRay(Owner.Pos, Ray, rte.airID, 3, false) < 0 then
													Ray = Vector(Owner.Height*0.3, 0):RadRotate(centerAngle - sweepRange); -- down
													if SceneMan:CastNotMaterialRay(Owner.Pos, Ray, rte.airID, 3, false) < 0 then
														obstacleState = Actor.PROCEEDING; -- ok the tunnel section is clear, so start walking forward while still digging
													else
														obstacleState = Actor.DIGPAUSING; -- tunnel cavity not clear yet, so stay put and dig some more
													end
												end
											else
												obstacleState = Actor.DIGPAUSING; -- tunnel cavity not clear yet, so stay put and dig some more
											end

											local aimAngle = Owner:GetAimAngle(true);
											local AimVec = Vector(1, 0):RadRotate(aimAngle);

											local angDiff = math.asin(AimVec:Cross(CurrDist.Normalized)); -- the angle between CurrDist and AimVec
											if math.abs(angDiff) < sweepRange then
												AI.fire = true; -- only fire the digger at the obstacle
											else
												AI.fire = false;
											end

											-- sweep the digger between the two endpoints of the obstacle
											local DigTarget;
											if sweepCW then
												DigTarget = Vector(Owner.Height*0.4, 0):RadRotate(centerAngle + sweepRange);
											else
												DigTarget = Vector(Owner.Height*0.4, 0):RadRotate(centerAngle - sweepRange);
											end

											angDiff = math.asin(AimVec:Cross(DigTarget.Normalized)); -- The angle between DigTarget and AimVec
											if math.abs(angDiff) < 0.1 then
												AI.Ctrl.AnalogAim = DigTarget.Normalized; -- aim in the direction of the next waypoint
												sweepCW = not sweepCW;
											else
												local sweepSpeed = 2.5;
												local sweepDir = sweepCW and 1 or -1;
												AI.Ctrl.AnalogAim = (Vector(AimVec.X, AimVec.Y):RadRotate(sweepDir*TimerMan.AIDeltaTimeSecs*sweepSpeed)).Normalized;
											end

											-- check if we are done when we get close enough to the waypoint
											if Owner.AIMode == Actor.AIMODE_GOLDDIG then
												Waypoint.Pos = SceneMan:MovePointToGround(Waypoint.Pos, Owner.Height*0.2, 4);
											end
										end
									end
								end
							else
								digState = AHuman.NOTDIGGING;
								obstacleState = Actor.PROCEEDING;
								AI.deviceState = AHuman.STILL;
								AI.fire = false;
								Owner:EquipFirearm(true);
							end
						else	-- not digging
							if not AI.Target then
								AI.fire = false;
							end

							-- Scan for obstacles
							local Trace = Vector(Owner.Radius*0.75, 0):RadRotate(scanAng);
							local Free = Vector();
							local index = math.floor(scanAng*2.5+2.01);
							if SceneMan:CastObstacleRay(Owner.Pos, Trace, Vector(), Free, Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) > -1 then
								Obstacles[index] = true;
							else
								Obstacles[index] = false;
							end

							if scanAng < 1.57 then	-- pi/2
								if scanAng > 1.2 then
									scanAng = 1.89;
								else
									scanAng = scanAng + 0.55;
								end
							else
								if scanAng > 3.5 then
									scanAng = -0.4;
								else
									scanAng = scanAng + 0.55;
								end
							end

							local tolerance = Owner.MoveProximityLimit;
							if AI.jump then
								tolerance = tolerance * 2;
							end

							-- A waypoint behind us with the next one in plain sight is done with: the path's nodes are only 24 px apart, and waiting to stand on
							-- each one pulled a running or flying unit back to every node it had passed.
							if Waypoint.Type ~= "last" and Owner.MovePathSize > 1 and CurrDist:MagnitudeIsGreaterThan(tolerance) then
								local NextPos = nil;
								local index = 0;
								for pos in Owner.MovePath do
									index = index + 1;
									if index == 2 then
										NextPos = pos;
										break;
									end
								end
								if NextPos then
									local ToNext = SceneMan:ShortestDistance(Owner.Pos, NextPos, false);
									local passed = ToNext:MagnitudeIsLessThan(CurrDist.Magnitude) or (CurrDist.X * ToNext.X < 0 and math.abs(CurrDist.X) < Owner.Height * 0.5 and math.abs(CurrDist.Y) < Owner.Height * 0.5);
									if passed and SceneMan:CastObstacleRay(Owner.Pos, ToNext, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 9) < 0 then
										PrevWptPos = Waypoint.Pos;
										Owner:RemoveMovePathBeginning();
										Waypoint.Pos = NextPos;
										Waypoint.Kind = Owner.MovePathStepKind;
										if Waypoint.Kind == 3 then
											Waypoint.Type = "drop";
										end
										if Owner.MovePathSize == 1 then
											Waypoint.Type = "last";
										end
										CurrDist = ToNext;
									end
								end
							end

							if CurrDist:MagnitudeIsGreaterThan(tolerance) then	-- not close enough to the waypoint
								ArrivedTimer:Reset();

								-- check if we have LOS to the waypoint
								-- (A climb has no line of sight to its top from under the lip it's climbing round, and a new path every second, each
								-- one starting the climb over, kept the unit bobbing at the foot of the shaft.)
								if AI.jetClimb or SceneMan:CastObstacleRay(Owner.Pos, CurrDist, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 9) < 0 then
									NoLOSTimer:Reset();
								elseif NoLOSTimer:IsPastSimTimeLimit() then	-- calculate new path
									Waypoint = nil;
									NeedsNewPath = true; -- update the path
									AI.jetClimb = false;
									nextLatMove = Actor.LAT_STILL;

									if Owner.AIMode == Actor.AIMODE_GOLDDIG and digState == AHuman.NOTDIGGING and math.random() < 0.5 then
										return true; -- end this behavior and look for gold again
									end
								end
							elseif ArrivedTimer:IsPastSimTimeLimit() then	-- only remove a waypoint if we have been close to it for a while
								if Waypoint.Type == "last" then
									if not AI.flying and Owner.Vel.Largest < 5 then
										if not Owner.MOMoveTarget then
											local ProxyWpt = SharedBehaviors.WaypointOnGround(Owner:GetLastAIWaypoint(), Owner.Height);
											if SceneMan:ShortestDistance(Owner.Pos, ProxyWpt, false).Largest < Owner.Height*0.4 then
												Owner:ClearAIWaypoints();
												Owner:ClearMovePath();
												Owner:DrawWaypoints(false);
												break;
											end
										end

										PrevWptPos = Waypoint.Pos;
										Owner:RemoveMovePathBeginning();
										Waypoint = nil;
										NeedsNewPath = true; -- update the path
										AI.jetClimb = false;
									end
								else
									PrevWptPos = Waypoint.Pos;
									Owner:RemoveMovePathBeginning();
									Waypoint = nil;
								end
							end

							if Waypoint then	-- move towards the waypoint
								local WallAhead = false; -- Something chest high in the way on the side we're walking.
								-- control horizontal movement
								if ClimbStepX ~= 0 and not ClimbStepTimer:IsPastSimTimeLimit() and not AI.jetClimb then
									-- Stepping off the top of a climb: carried on until we're over the ledge or down on it.
									nextLatMove = ClimbStepX > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
								elseif ClimbStepX ~= 0 then
									ClimbStepX = 0;
								end
								if not AI.flying and ClimbStepX == 0 then
									if CurrDist.X < -3 then
										nextLatMove = Actor.LAT_LEFT;
									elseif CurrDist.X > 3 then
										nextLatMove = Actor.LAT_RIGHT;
									else
										nextLatMove = Actor.LAT_STILL;
									end
									-- What is in the way on that side, chest high and knee high, a body length ahead: a knee-high step is walked over, a wall
									-- is hopped with the jetpack when there is head room, and crawled under when the gap is low.
									-- (Not when crawling: the wall is the top of a doorway we're going under, and the way through is the path's.)
									if nextLatMove ~= Actor.LAT_STILL and Waypoint.Pos.Y < Owner.Pos.Y + Owner.Height * 0.5 and AI.proneState ~= AHuman.PRONE then
										local side = nextLatMove == Actor.LAT_LEFT and -1 or 1;
										local ahead = Vector(side * Owner.Height * 0.45, 0);
										local chest = Owner.Pos + Vector(0, -Owner.Height * 0.2);
										local head = Owner.Pos + Vector(0, -Owner.Height * 0.3); -- The top of the head: Height is about twice the sprite.
										local chestHit = SceneMan:CastStrengthRay(chest, ahead, 5, Vector(), 2, rte.grassID, true);
										local headHit = SceneMan:CastStrengthRay(head, ahead, 5, Vector(), 2, rte.grassID, true);
										-- Chest and head both blocked is a wall; chest alone is a steep slope or a step, which the legs and climbing arms deal with,
										-- and the stuck handling jets if they can't.
										if chestHit and headHit and Waypoint.Kind ~= 3 and Waypoint.Kind ~= 4 then
											WallAhead = true;
											local up = (side < 0) and (Obstacles[Obst.L_UP] == true) or (side > 0 and Obstacles[Obst.R_UP] == true);
											if Owner.Jetpack and Owner.Jetpack.JetpackType == AEJetpack.Standard and Owner.Jetpack.JetTimeLeft >= AI.minBurstTime and not up then
												AI.jump = true;
												if Owner:NumberValueExists("AITrace") then ConsoleMan:PrintString("AITRACE jet: wall ahead"); end
											end
										end
									end
								end

								if Waypoint.Type == "right" then
									if CurrDist.X > -3 then
										nextLatMove = Actor.LAT_RIGHT;
									end
								elseif Waypoint.Type == "left" then
									if CurrDist.X < 3 then
										nextLatMove = Actor.LAT_LEFT;
									end
								end

								if Owner.Jetpack then
									if Owner.Jetpack.JetTimeLeft < AI.minBurstTime then
										if not AI.flying or Owner.Vel.Y > 4 then
											AI.jump = false; -- not enough fuel left, no point in jumping yet
											AI.refuel = true;
										end
									else
										-- do we have a target we want to shoot at?
										if Owner.Head and AI.Target and AI.canHitTarget and AI.BehaviorName ~= "AttackTarget" then
											-- are we also flying
											if AI.flying and Owner.Jetpack.JetpackType == AEJetpack.Standard then
												-- predict jetpack movement when jumping and there is a target (check one direction)
												local jetStrength = AI.jetImpulseFactor / Owner.Mass;
												local t = math.min(0.4, Owner.Jetpack.JetTimeLeft*0.001);
												local PixelVel = Owner.Vel * (GetPPM() * t);
												local Accel = SceneMan.GlobalAcc * GetPPM();

												-- a burst use 10x more fuel
												if Owner.Jetpack:CanTriggerBurst() then
													t = math.max(math.min(0.4, Owner.Jetpack.JetTimeLeft*0.001-TimerMan.AIDeltaTimeSecs*10), TimerMan.AIDeltaTimeSecs);
												end

												-- test jumping
												local JetAccel = Accel + Vector(-jetStrength, 0):RadRotate(Owner.RotAngle+1.375*math.pi+Owner:GetAimAngle(false)*0.25);
												local JumpPos = (Owner.Head and Owner.Head.Pos or Owner.Pos) + PixelVel + JetAccel * (t*t*0.5);

												-- a burst add a one time boost to acceleration
												if Owner.Jetpack:CanTriggerBurst() then
													JumpPos = JumpPos + Vector(-AI.jetBurstFactor, 0):AbsRotateTo(JetAccel);
												end

												-- check for obstacles from the head
												Trace = SceneMan:ShortestDistance((Owner.Head and Owner.Head.Pos or Owner.Pos), JumpPos, false);
												local jumpScore = SceneMan:CastObstacleRay((Owner.Head and Owner.Head.Pos or Owner.Pos), Trace, JumpPos, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3);
												if jumpScore < 0 then	-- no obstacles: calculate the distance from the future pos to the wpt
													jumpScore = SceneMan:ShortestDistance(Waypoint.Pos, JumpPos, false).Magnitude;
												else -- the ray hit terrain or start inside terrain: avoid
													jumpScore = SceneMan:ShortestDistance(Waypoint.Pos, JumpPos, false).Largest * 2;
												end

												-- test falling
												local FallPos = (Owner.Head and Owner.Head.Pos or Owner.Pos) + PixelVel + Accel * (t*t*0.5);

												-- check for obstacles when falling/walking
												local Trace = SceneMan:ShortestDistance((Owner.Head and Owner.Head.Pos or Owner.Pos), FallPos, false);
												SceneMan:CastObstacleRay((Owner.Head and Owner.Head.Pos or Owner.Pos), Trace, FallPos, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3);

												if SceneMan:ShortestDistance(Waypoint.Pos, FallPos, false):MagnitudeIsLessThan(jumpScore) then
													AI.jump = false;
												else
													AI.jump = true;
												end
											else
												AI.jump = false;
											end
										else
											local hopping = false; -- A hop over something low: the planner below must not cancel it as "walkable".
											if Waypoint.Type ~= "drop" and not Lower(Waypoint, Owner, 20) and Owner.Jetpack.JetpackType == AEJetpack.Standard and AI.proneState ~= AHuman.PRONE then
												-- jump over low obstacles unless we want to jump off a ledge (and not while crawling under something)
												if nextLatMove == Actor.LAT_RIGHT and Obstacles[Obst.R_FRONT] and not Obstacles[Obst.R_UP] then
														hopping = true;
													if Owner:NumberValueExists("AITrace") and not AI.jump then ConsoleMan:PrintString("AITRACE jet: hop right"); end
													AI.jump = true;
													-- Something high in front as well: straight up, not backwards. Backing off with the jet lit (the nozzle leans the way
													-- we move) sent units flying back down the slope they had just climbed.
													if Obstacles[Obst.R_HIGH] then
														nextLatMove = Actor.LAT_STILL;
													end
												elseif nextLatMove == Actor.LAT_LEFT and Obstacles[Obst.L_FRONT] and not Obstacles[Obst.L_UP] then
														hopping = true;
													if Owner:NumberValueExists("AITrace") and not AI.jump then ConsoleMan:PrintString("AITRACE jet: hop left"); end
													AI.jump = true;
													if Obstacles[Obst.L_HIGH] then
														nextLatMove = Actor.LAT_STILL;
													end
												end
											end

											-- A climb: the waypoint is higher than the legs can manage, so the jetpack takes us up, with the move keys pushing us along towards it
											-- (the nozzle leans with the direction moved). It is held until we are up at the waypoint's height, and it isn't lit under a ceiling,
											-- when we're already going fast sideways, or when the tank is low.
											local above = Waypoint.Pos.Y - Owner.Pos.Y; -- Negative when the waypoint is higher than us.
											-- (Not "and AI.flying": that only comes on after a second clear of the ground, and a climb up a slope brushes it all the way.)
											local climbing = AI.jetClimb;
											-- A rise steeper than the legs can walk (about 40 degrees: a slope gentler than that is walked, however far up the waypoint
												-- is, where units were jetting over every hill crest), or a wall in the way at any height.
												local steep = -above > math.abs(CurrDist.X) * 0.85;
												-- The path says this step is a jump: that settles it, whatever the slope looks like from here.
												-- (And a dig is dug, never jetted: the shaft's walls looked like a wall ahead, and the digger was jetted out of its own hole.)
												local wantsClimb = AI.proneState ~= AHuman.PRONE and Waypoint.Kind ~= 4 and ((Waypoint.Kind == 2 and above < -Owner.Height * 0.15) or (above < -Owner.Height * 0.25 and steep) or (WallAhead and above < Owner.Height * 0.3));
											local climbRefused = false;
											if wantsClimb and not climbing then -- (In the air too: a unit passing a ledge on the way up from one jump couldn't start the next.)
												local towardsX = CurrDist.X;
												local Hit = Vector();
												-- Room over the head for the climb itself, no more: a ceiling well above where we're going is no ceiling.
												local Up = Vector(0, math.min(-Owner.Height * 0.5, above - Owner.Height * 0.3));
												local ceiling = SceneMan:CastObstacleRay(Owner.Pos + Vector(0, -Owner.Height * 0.3), Up, Hit, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) >= 0;
												-- A tall climb wants a near full tank: started on half of one it ends part way up the face, with the fall and the wait to
												-- refill to show for it. So the unit waits at the foot until the tank is in.
												local tall = above < -Owner.Height;
												local tankIn = Owner.Jetpack.JetTimeLeft >= (tall and Owner.Jetpack.JetTimeTotal * 0.85 or AI.minBurstTime);
												if not tankIn and tall and not AI.flying then
													AI.refuel = true;
												end
												if not ceiling and tankIn then
													climbing = true;
													AI.jetClimb = true;
													AI.climbClearY = nil;
													if Owner:NumberValueExists("AITrace") then ConsoleMan:PrintString("AITRACE climb: wpt dx " .. math.floor(towardsX) .. " dy " .. math.floor(above)); end
												else
													climbRefused = true;
												end
											end
											if climbRefused then
												-- A climb is what's wanted and it can't be had here: no jet, the legs and the stuck handling take it from here.
												AI.jump = false;
												AI.jetClimb = false;
											elseif climbing or (AI.jetClimb and wantsClimb) then
												-- Up at the waypoint's height the climb is over, but only once the feet would clear whatever we step onto next: the waypoint
												-- sits up to a node above the ledge's top, and the step off it is sideways, so the way at foot level has to be open
												-- that way. It is also over well past the waypoint's height, and when the tank runs dry.
												local stepX = CurrDist.X;
												if math.abs(stepX) < 10 and NextWptPos then
													stepX = SceneMan:ShortestDistance(Owner.Pos, NextWptPos, false).X;
												end
												local feetClear = true;
												local chestClear = true;
												local touchingWall = false;
												if math.abs(stepX) >= 10 then
													local reach = math.abs(stepX) + Owner.Height * 0.15; -- All the way to it: a ledge short of the ray's end was stepped off towards, and fallen short of.
													local Step = Vector(stepX > 0 and reach or -reach, 0);
													-- (Height is about twice the sprite: the feet are a fifth of it under Pos.)
													feetClear = SceneMan:CastObstacleRay(Owner.Pos + Vector(0, Owner.Height * 0.2), Step, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) < 0 and SceneMan:CastObstacleRay(Owner.Pos + Vector(0, Owner.Height * 0.1), Step, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) < 0;
													chestClear = SceneMan:CastObstacleRay(Owner.Pos + Vector(0, -Owner.Height * 0.1), Step, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) < 0;
													-- Right up against it and not rising for all the jet: a unit pressed to a wall just burns, so that's a push away from it.
													local Touch = Vector(stepX > 0 and Owner.Height * 0.15 or -Owner.Height * 0.15, 0);
													touchingWall = SceneMan:CastObstacleRay(Owner.Pos + Vector(0, -Owner.Height * 0.1), Touch, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 2) >= 0 or SceneMan:CastObstacleRay(Owner.Pos + Vector(0, Owner.Height * 0.1), Touch, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 2) >= 0;
												end
												-- Up at the height with the way clear, but not yet near the landing sideways, it isn't over: the jet went out a hundred pixels
													-- short of a ledge and the unit dropped below it on the way; now it hovers across (see below).
													local done = (above > -Owner.Height * 0.2 and feetClear and math.abs(stepX) < Owner.Height * 0.4) or above > Owner.Height * 0.6 or Owner.Jetpack.JetTimeLeft < TimerMan.AIDeltaTimeMS * 4;
												if done then
													AI.jetClimb = false;
													AI.jump = false;
													-- Up: step off onto the ledge. Nothing else moves us sideways while in the air, so this is kept up for a moment.
													if feetClear and math.abs(stepX) >= 10 then
														nextLatMove = stepX > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
														ClimbStepX = stepX;
														ClimbStepTimer:Reset();
													end
												else
													-- Lit in pulses: already going up at a fair rate, it coasts, or the climb shoots far past the top. Hill tops were cleared by a hundred pixels.
													-- Up at the height but not yet over the landing, it hovers: lit whenever we start to drop.
													-- The rate of climb comes down as the top nears: what it reaches the top at, it coasts on past it (eighty pixels, from five
													-- metres a second), and that was fuel and height to come down again.
													local toGo = -above - Owner.Height * 0.2;
													local climbRate = math.max(1, math.min(5, toGo / 15));
													-- With some play in it: every relight can be a burst, at a burst's worth of fuel, so the fewer the better.
													if above > -Owner.Height * 0.2 then
														AI.jump = Owner.Vel.Y > (AI.jump and -1.5 or 0.5);
													else
														AI.jump = Owner.Vel.Y > (AI.jump and -climbRate - 2 or -climbRate + 1);
													end
													-- Under something (the underside of the ledge we're climbing to, usually): no way up here. The spot just out from under its lip is
													-- remembered, and held until we're above the lip's height; only backing out, and then drifting for the waypoint again, went back
													-- under it every other tick. (The look up goes only as far as the climb does: a roof above where we're heading is no roof.)
													local Lip = Vector();
													local Over = Vector(0, math.max(-Owner.Height * 0.6, math.min(-Owner.Height * 0.2, above)));
													if above < -Owner.Height * 0.1 and SceneMan:CastObstacleRay(Owner.Pos + Vector(0, -Owner.Height * 0.25), Over, Lip, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) >= 0 then
														AI.climbClearX = Owner.Pos.X + (CurrDist.X > 0 and -1 or 1) * Owner.Height * 0.3;
														AI.climbClearY = Lip.Y;
														if Owner:NumberValueExists("AITrace") and math.random() < 0.1 then ConsoleMan:PrintString("AITRACE climb: under a ceiling at " .. math.floor(Lip.Y) .. ", keeping out from under it"); end
													end
													if AI.climbClearY and Owner.Pos.Y - Owner.Height * 0.3 < AI.climbClearY then
														AI.climbClearY = nil; -- Above it now.
													end
													-- Sideways it is flown by speed: a little drift towards the waypoint, more the further off it is, and none at all into a wall or
													-- slope (that only pins us to it) or when the waypoint is straight above. Too fast either way and the nozzle is leant against it:
													-- the speed walked up with was carrying units under the ledges they were climbing to.
													local wantVelX = math.max(-4, math.min(4, CurrDist.X / 20));
													if AI.climbClearY then
														wantVelX = math.max(-3, math.min(3, (AI.climbClearX - Owner.Pos.X) / 10));
													elseif touchingWall and AI.jump and Owner.Vel.Y > -1 then
														wantVelX = stepX > 0 and -1.5 or 1.5;
													elseif not chestClear or math.abs(CurrDist.X) < Owner.Height * 0.15 then
														wantVelX = 0;
													end
													local offVelX = wantVelX - Owner.Vel.X;
													if offVelX > 1.5 then
														nextLatMove = Actor.LAT_RIGHT;
													elseif offVelX < -1.5 then
														nextLatMove = Actor.LAT_LEFT;
													else
														nextLatMove = Actor.LAT_STILL;
													end
													-- The nozzle leans a few degrees forward whenever the aim is level, so a climb aimed level drifts, and keeps gathering speed, the
													-- way it faces. Aimed straight up it lifts and nothing else: that's the climb, with the lean kept for the drift towards the waypoint.
													nextAimAngle = nextLatMove == Actor.LAT_STILL and math.pi * 0.5 or 0;
												end
											else
												AI.jetClimb = false;
												-- predict jetpack movement...
												local jetStrength = (AI.jetImpulseFactor / Owner.Mass);
												local t = math.min(0.4, Owner.Jetpack.JetTimeLeft*0.001);
												local PixelVel = Owner.Vel * (GetPPM() * t);
												local Accel = SceneMan.GlobalAcc * GetPPM();

												-- a burst use 10x more fuel
												if Owner.Jetpack:CanTriggerBurst() then
													t = math.max(math.min(0.4, Owner.Jetpack.JetTimeLeft*0.001-TimerMan.AIDeltaTimeSecs*10), TimerMan.AIDeltaTimeSecs);
												end

												-- when jumping (check four directions)
												for k, Face in pairs(Facings) do
													local JetAccel = Vector(-jetStrength, 0):RadRotate(Owner.RotAngle+1.375*math.pi+Face.facing*0.25);
													local JumpPos = Owner.Pos + PixelVel + (Accel + JetAccel) * (t*t*0.5);

													-- a burst add a one time boost to acceleration
													if Owner.Jetpack:CanTriggerBurst() then
														JumpPos = JumpPos + Vector(-AI.jetBurstFactor, 0):AbsRotateTo(JetAccel);
													end

													-- A flight that would end in a ceiling or a wall is as bad as it gets: the hit point is where we'd really end up, and the score is doubled.
													local Trace = SceneMan:ShortestDistance(Owner.Pos, JumpPos, false);
													local Hit = Vector();
													if SceneMan:CastObstacleRay(Owner.Pos + Vector(0, -Owner.Height * 0.4), Trace, Hit, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) >= 0 then
														JumpPos = Hit + Vector(0, Owner.Height * 0.4);
														Facings[k].range = SceneMan:ShortestDistance(Waypoint.Pos, JumpPos, false).Magnitude * 2 + 50;
														Facings[k].blocked = true;
													else
														Facings[k].range = SceneMan:ShortestDistance(Waypoint.Pos, JumpPos, false).Magnitude;
														Facings[k].blocked = false;
													end
												end

												-- when falling or walking
												local FallPos = Owner.Pos + PixelVel;
												if AI.flying then
													FallPos = FallPos + Accel * (t*t*0.5);
												elseif nextLatMove ~= Actor.LAT_STILL then
													-- On our feet we'd be walking, not standing: where a walk of the same time gets us, along the ground. Scored from where we are
													-- standing, every jet looked better than a walk that was taken to go nowhere.
													local walkSpeed = 2.5 * GetPPM(); -- pixels a second, about a soldier's walk
													local Ahead = Vector((nextLatMove == Actor.LAT_LEFT and -1 or 1) * walkSpeed * t, 0);
													FallPos = SceneMan:MovePointToGround(Owner.Pos + Ahead, Owner.Height * 0.4, 6);
												end

												-- check for obstacles when falling/walking
												local Trace = SceneMan:ShortestDistance(Owner.Pos, FallPos, false);
												SceneMan:CastObstacleRay(Owner.Pos, Trace, FallPos, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3);

												local deltaToJump = 5;
												if Owner.Jetpack.JetpackType == AEJetpack.JumpPack then
													deltaToJump = deltaToJump * 1.4;
												end

												table.sort(Facings, function(A, B) return A.range < B.range end);
												local delta = SceneMan:ShortestDistance(Waypoint.Pos, FallPos, false).Magnitude - Facings[1].range;
												-- Going up, once the waypoint is no longer above the chest the climb is done: thrusting on from there is the hovering past the ledge.
												local aboveWaypoint = Owner.Vel.Y < 0 and (Waypoint.Pos.Y - Owner.Pos.Y) > -Owner.Height * 0.3;
												-- On the ground, the jetpack is for what can't be walked: a wall in the way, or a waypoint well above us. A slope is walked.
												local walkable = not AI.flying and not WallAhead and not hopping and (Waypoint.Pos.Y - Owner.Pos.Y) > -Owner.Height * 0.25;
												-- Well above the waypoint already: whatever the scores say, more height is wasted fuel.
												local tooHigh = (Waypoint.Pos.Y - Owner.Pos.Y) > Owner.Height;
												-- Already going fast enough sideways: more thrust only builds a speed that ends in a wall. Momentum is let carry.
												local fastEnough = math.abs(Owner.Vel.X) > 5 and Owner.Vel.Y < 8;
												-- Under an overhang with the waypoint above: every flight hits the ceiling. Step out from under it first, towards a side with ground
												-- under it and open sky over it; with no such side, the usual choice is made and the stuck handling takes it from there.
												-- (Not while crawling: the "overhang" is the top of the slot being crawled into, and stepping out of it is stepping back.)
	local underOverhang = not AI.flying and AI.proneState ~= AHuman.PRONE and Facings[1].blocked and (Waypoint.Pos.Y - Owner.Pos.Y) < -Owner.Height * 0.25;
												local stepTo = Actor.LAT_STILL;
												if underOverhang then
													local function sideOpen(dir)
														local foot = Owner.Pos + Vector(dir * Owner.Height * 0.35, 0);
														local ground = SceneMan:CastStrengthRay(foot, Vector(0, Owner.Height * 0.7), 5, Vector(), 2, rte.grassID, true);
														local sky = not SceneMan:CastStrengthRay(foot + Vector(0, -Owner.Height * 0.3), Vector(0, -Owner.Height * 1.4), 5, Vector(), 2, rte.grassID, true);
														return ground and sky;
													end
													local towards = CurrDist.X < 0 and -1 or 1;
													if sideOpen(towards) then
														stepTo = towards < 0 and Actor.LAT_LEFT or Actor.LAT_RIGHT;
													elseif sideOpen(-towards) then
														stepTo = towards < 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
													else
														underOverhang = false;
													end
												end
												if underOverhang then
													AI.jump = false;
													nextLatMove = stepTo;
													if Owner:NumberValueExists("AITrace") then ConsoleMan:PrintString("AITRACE overhang: stepping " .. (nextLatMove == Actor.LAT_LEFT and "left" or "right")); end
												elseif delta < 1 or aboveWaypoint or walkable or tooHigh or fastEnough then
													if Owner:NumberValueExists("AITrace") and (Waypoint.Pos.Y - Owner.Pos.Y) < -Owner.Height * 0.25 and not AI.flying and math.random() < 0.1 then
														ConsoleMan:PrintString("AITRACE no jet: delta " .. math.floor(delta) .. " above " .. tostring(aboveWaypoint) .. " walkable " .. tostring(walkable) .. " tooHigh " .. tostring(tooHigh) .. " fast " .. tostring(fastEnough) .. " blocked " .. tostring(Facings[1].blocked) .. " range " .. math.floor(Facings[1].range) .. " wpt dx " .. math.floor(CurrDist.X) .. " dy " .. math.floor(CurrDist.Y) .. " lat " .. tostring(nextLatMove));
													end
													AI.jump = false;
												elseif delta > deltaToJump then
													if Owner:NumberValueExists("AITrace") and not AI.jump then ConsoleMan:PrintString("AITRACE jet: planner delta " .. math.floor(delta) .. " flying " .. tostring(AI.flying) .. " wpt dy " .. math.floor(Waypoint.Pos.Y - Owner.Pos.Y)); end
													AI.jump = true;
													nextAimAngle = Owner:GetAimAngle(false) * 0.5 + Facings[1].aim * 0.5; -- adjust jetpack nozzle direction
													nextLatMove = Actor.LAT_STILL;

													if Facings[1].facing > 1.4 then
														if not Owner.HFlipped then
															nextLatMove = Actor.LAT_LEFT;
														end
													elseif Owner.HFlipped then
														nextLatMove = Actor.LAT_RIGHT;
													end
													-- Still stepping off the top of a climb: that keeps the sideways input.
													if ClimbStepX ~= 0 and not ClimbStepTimer:IsPastSimTimeLimit() then
														nextLatMove = ClimbStepX > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
													end
												end
											end
										end
									end
								end
								
							end
						end
					end
				end
			end
		else	-- no waypoint list
			NeedsNewPath = false;
			if Owner.Vel.Y < 15 then
				AI.jump = false;
			end
			nextLatMove = Actor.LAT_STILL;
			AI.lateralMoveState = Actor.LAT_STILL;

			local Trace = SceneMan:ShortestDistance(Owner.Pos, Owner:GetLastAIWaypoint(), false);
			Owner:UpdateMovePath();

			-- wait until movepath is updated
			while Owner.IsWaitingOnNewMovePath do
				local _ai, _ownr, _abrt = coroutine.yield();
				if _abrt then return true end
			end
			-- A fresh path is a fresh start: standing still while it was worked out, or before the order came, is not being stuck.
			StuckTimer:Reset();
			StuckJumped = false;

			-- have we arrived?
			if not Owner.MOMoveTarget then
				local ProxyWpt = SceneMan:MovePointToGround(Owner:GetLastAIWaypoint(), Owner.Height*0.2, 5);
				local Trace = SceneMan:ShortestDistance(Owner.Pos, ProxyWpt, false);
				if Trace.Largest < Owner.Height*0.25 and not SceneMan:CastStrengthRay(Owner.Pos, Trace, 6, Vector(), 3, rte.grassID, true) then
					if Owner.AIMode == Actor.AIMODE_GOTO then
						AI.SentryPos = Vector(Owner.Pos.X, Owner.Pos.Y);
						AI:CreateSentryBehavior(Owner);
					end

					Owner:ClearAIWaypoints();
					Owner:ClearMovePath();
					Owner:DrawWaypoints(false);

					break;
				end
			end

			Owner:DrawWaypoints(true);
			NoLOSTimer:Reset();
		end

		-- A governor on flight, whatever else was decided, and even while the tank is left to refill (which skips everything above). Falling
		-- fast with the ground coming up inside the next half second, the jet is lit straight up to take the speed off; going fast sideways, the
		-- nozzle is leant against it (the move keys away from the way we face flip the lean). Units flying over a hill used to come down far
		-- past it, hard enough to die.
		if Owner.Jetpack then
			local fuel = Owner.Jetpack.JetTimeLeft >= AI.minBurstTime;
			local fastSideways = math.abs(Owner.Vel.X) > 8;
			local fallingOnGround = false;
			if Owner.Vel.Y > 6 then
				local drop = Vector(0, Owner.Vel.Y * GetPPM() * 0.5 + Owner.Height * 0.2);
				fallingOnGround = SceneMan:CastObstacleRay(Owner.Pos + Vector(0, Owner.Height * 0.2), drop, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 4) >= 0;
			end
			if fastSideways and (AI.flying or Owner.Vel.Y > 6 or AI.jump) then
				-- The lean against the speed is set whether or not there's fuel for more: a jet already lit on the last of the tank was
				-- still leaning forward, and took a unit to thirty metres a second.
				if Owner:NumberValueExists("AITrace") and not AI.jump then ConsoleMan:PrintString("AITRACE jet: sideways brake at " .. math.floor(Owner.Vel.X * 10) / 10); end
				nextAimAngle = 0;
				nextLatMove = Owner.Vel.X > 0 and Actor.LAT_LEFT or Actor.LAT_RIGHT;
				if fuel then
					AI.jump = true;
				end
			elseif fallingOnGround and fuel then
				if Owner:NumberValueExists("AITrace") and not AI.jump then ConsoleMan:PrintString("AITRACE jet: landing brake at " .. math.floor(Owner.Vel.Y * 10) / 10); end
				AI.jump = true;
				nextAimAngle = math.pi * 0.5;
			end
		end

		-- No jet while lying down: the nozzle turns with the body, and a prone unit's jet drove it along the ground, faster the more the
		-- governor "braked" it.
		if AI.proneState == AHuman.PRONE then
			AI.jump = false;
		end

		-- movement commands
		if (AI.Target and AI.BehaviorName ~= "AttackTarget" and not AI.PickupHD) or (Owner.AIMode ~= Actor.AIMODE_SQUAD and (AI.BehaviorName == "ShootArea" or AI.BehaviorName == "FaceAlarm")) then
			if Owner.aggressive then	-- the aggressive behavior setting makes the AI pursue waypoint at all times
				AI.lateralMoveState = nextLatMove;
			else
				AI.lateralMoveState = Actor.LAT_STILL;
			end
			if not AI.flying then
				AI.jump = false;
			end
		else
			AI.lateralMoveState = nextLatMove;
			if digState == AHuman.NOTDIGGING then
				Owner:SetAimAngle(nextAimAngle);
				nextAimAngle = Owner:GetAimAngle(false) * 0.95; -- look straight ahead
			end
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	Owner:RemoveNumberValue("AI_StuckForTime");
	return true;
end

function SharedBehaviors.CalculateThreatLevel(MO, Owner)
	-- prioritize closer targets
	local priority = -SceneMan:ShortestDistance(Owner.Pos, MO.Pos, false).Largest / FrameMan.PlayerScreenWidth;

	-- prioritize the weaker humans over crabs
	if MO.ClassName == "AHuman" then
		if MO.FirearmIsReady then	-- prioritize armed targets
			priority = priority + 1.0;
		else
			priority = priority + 0.5;
		end
	elseif MO.ClassName == "ACrab" then
		if MO.FirearmIsReady then	-- prioritize armed targets
			priority = priority + 0.7;
		else
			priority = priority + 0.3;
		end
	elseif MO.ClassName == "ADoor" then
		priority = priority * 0.3;
	end

	return priority - MO.Health / 500; -- prioritize damaged targets
end

-- get the projectile properties from the magazine
function SharedBehaviors.GetProjectileData(Owner)
	local PrjDat = {MagazineName = ""};
	local Weapon, Round, Projectile;
	if Owner.EquippedItem and IsHDFirearm(Owner.EquippedItem) then
		Weapon = ToHDFirearm(Owner.EquippedItem);
		if Weapon.Magazine then
			Round = Weapon.Magazine.NextRound;
			Projectile = Round.NextParticle;
			PrjDat.MagazineName = Weapon.Magazine.PresetName;
		end
	end
	if Round == nil or Round.IsEmpty then	-- set default values if there is no particle
		PrjDat.g = 0;
		PrjDat.vel = 100;
		PrjDat.rng = math.huge;
		PrjDat.pen = math.huge;
		PrjDat.blast = 0;
	else
		-- find muzzle velocity
		PrjDat.vel = Weapon:GetAIFireVel();
		-- half of the theoretical upper limit for the total amount of material strength this weapon can destroy in 250ms

		PrjDat.g = SceneMan.GlobalAcc.Y * 0.67 * Weapon:GetBulletAccScalar(); -- underestimate gravity
		PrjDat.vsq = PrjDat.vel^2; -- muzzle velocity squared
		PrjDat.vqu = PrjDat.vsq^2; -- muzzle velocity quad
		PrjDat.drg = 1 - Projectile.AirResistance * TimerMan.DeltaTimeSecs; -- AirResistance is stored as the ini-value times 60
		PrjDat.thr = math.min(Projectile.AirThreshold, PrjDat.vel);
		PrjDat.pen = Weapon:GetAIPenetration() * PrjDat.drg;

		PrjDat.blast = Weapon:GetAIBlastRadius();
		if PrjDat.blast > 0 or Weapon:IsInGroup("Weapons - Explosive") then
			PrjDat.exp = true; -- set this for legacy reasons
			PrjDat.pen = PrjDat.pen + 100;
		end

		-- estimate theoretical max range with ...
		local lifeTime = Weapon:GetAIBulletLifeTime();
		if lifeTime < 1 then	-- infinite life time
			PrjDat.rng = math.huge;
		elseif PrjDat.drg < 1 then	-- AirResistance
			PrjDat.rng = 0;
			local threshold = PrjDat.thr * rte.PxTravelledPerFrame; -- AirThreshold in pixels/frame
			local vel = PrjDat.vel * rte.PxTravelledPerFrame; -- muzzle velocity in pixels/frame

			for _ = 0, math.ceil(lifeTime/TimerMan.DeltaTimeMS) do
				PrjDat.rng = PrjDat.rng + vel;
				if vel > threshold then
					vel = vel * PrjDat.drg;
				end
			end
		else	-- no AirResistance
			PrjDat.rng = PrjDat.vel * rte.PxTravelledPerFrame * (lifeTime / TimerMan.DeltaTimeMS);
		end

		-- Artificially decrease reported range to make sure AI
		-- is close enough to reach target with current weapon
		-- even if it the range is calculated incorrectly
		PrjDat.rng = PrjDat.rng * 0.9;
	end

	return PrjDat;
end

-- stop the user from inadvertently modifying the storage table
local Proxy = {};
local Mt = {
	__index = SharedBehaviors,
	__newindex = function(Table, k, v)
		error("The SharedBehaviors table is read-only.", 2);
	end
}
setmetatable(Proxy, Mt);
SharedBehaviors = Proxy;