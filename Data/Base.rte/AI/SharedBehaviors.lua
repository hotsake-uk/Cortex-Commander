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

-- The jet's numbers from the body, so the flying rules need no tuned constants: its net upward acceleration at full burn, how far a fall
-- at a given speed takes to arrest, and how much height the fuel left buys.
function SharedBehaviors.JetNumbers(AI, Owner)
	local numbers = { accel = 0, stopDistance = function(vDown) return math.huge; end, heightForFuel = 0 };
	if not Owner.Jetpack or not AI.jetImpulseFactor or Owner.Mass <= 0 then
		return numbers;
	end
	local gravity = SceneMan.GlobalAcc.Y * GetPPM(); -- px/s^2, down.
	local thrust = AI.jetImpulseFactor / Owner.Mass; -- px/s^2 straight up at full burn.
	numbers.accel = thrust - gravity;
	if numbers.accel > 1 then
		numbers.stopDistance = function(vDown)
			local v = math.max(0, vDown) * GetPPM();
			return v * v / (2 * numbers.accel);
		end
	end
	numbers.heightForFuel = Owner.JumpHeight * GetPPM() * (Owner.Jetpack.JetTimeLeft / math.max(1, Owner.Jetpack.JetTimeTotal));
	return numbers;
end

-- How fast a climb is flown at the most, in m/s. A human's is high because a climb costs fuel by the second it is lit, not by the pixel:
-- held to 8 m/s a 320 px shaft took more than the tank (1.6 s of burn against 1.5 s), and the unit ran dry at the top, where the jump
-- planner took over with a dry tank and flew it in bursts a hundred pixels past the landing. A crab's climb ends at the height with
-- no hover, so it is kept slower.
function SharedBehaviors.ClimbSpeedCap(Owner)
	return Owner.Head and 12 or 8;
end

-- What the tank has left at the top of a climb of a height, begun with so much fuel, in ms; or -1 when the climb can't be made on it.
-- Flown as the climb flies it, a thirtieth of a second at a time: full burn to the speed cap, held there, let coast from the height
-- gravity alone stops it in, to 32 px under the point (the rate's twelve pixels short of the fifth of a body the hover band begins at,
-- where the hover takes over on the reserve). The thrust falls with the tank: a jetpack's throttle follows the fuel left
-- (AEJetpack::UpdateBurstState), 1.2 of the nominal when full to 0.8 when empty for the base game's packs, and the nominal is what
-- JetNumbers has (EstimateImpulse takes no throttle). Summed from a constant thrust, as it was, the check passed a 329 px climb on
-- 1338 ms that ran dry at the top, and a 167 px one on 1203 that got there with 55.
function SharedBehaviors.ClimbFuelLeft(AI, Owner, height, fuel)
	local jet = SharedBehaviors.JetNumbers(AI, Owner);
	local Pack = Owner.Jetpack;
	if jet.accel <= 1 or not Pack then
		return -1;
	end
	height = math.max(0, height - 32);
	local gravity = SceneMan.GlobalAcc.Y * GetPPM();
	local thrust = jet.accel + gravity;
	local total = math.max(1, Pack.JetTimeTotal);
	local lowMul, highMul = Pack.NegativeThrottleMultiplier, Pack.PositiveThrottleMultiplier;
	local cap = SharedBehaviors.ClimbSpeedCap(Owner) * GetPPM();
	local dt = 1 / 30;
	local y, up, t, lit = 0, 0, 0, false; -- Climbed so far, the speed upwards (px/s), the time, whether the jet is lit.
	fuel = fuel - 200; -- The burst that lights it.
	while y < height do
		if t > 6 or fuel <= 0 then
			return -1;
		end
		local rate = math.max(20, math.min(cap, math.sqrt(2 * gravity * math.max(0, height - y))));
		-- (The jet will not relight under 250 ms of tank, AEJetpack says; a climb that needs a pulse with less has none.)
		if not lit and up < rate - 20 and fuel <= 250 then
			return -1;
		end
		lit = up < (lit and rate + 20 or rate - 20);
		if lit then
			local factor = lowMul + (highMul - lowMul) * math.max(0, math.min(1, fuel / total));
			up = up + (thrust * factor - gravity) * dt;
			fuel = fuel - dt * 1000;
		else
			up = up - gravity * dt;
			fuel = math.min(total, fuel + dt * 1000 * Pack.JetReplenishRate);
		end
		y = y + up * dt;
		t = t + dt;
	end
	return fuel;
end

-- The climb: a jetpack climb up a column to a landing, as the path finder lays it out (a jump point well above the unit, the top of the
-- column, and usually a landing beside the top on the floor the climb is for). It is one piece of work in stages, each with a test for
-- being done and a test for having failed, run by GoToWpt in place of its walking and jetting for as long as the climb lasts:
--   align  on the ground, walk until under the column's middle;
--   fuel   stand there until the tank holds what the climb will burn, with a reserve for the top;
--   rise   jet straight up the column's middle, the rate brought down as the top nears so the coast ends there;
--   step   hold that height and move across onto the landing until there is floor under the feet;
--   done   the climb's points are taken off the route and the legs walk on.
-- A failure (couldn't get under the column, no rise for a while, fell back, ran dry, took too long) ends the climb and asks for a new
-- route from where the unit is. The old climb was a dozen rules spread through GoToWpt, each fixing one place; this is the same physics
-- in one place, in order. Small hops, slopes and walls in the way are still the walking code's.

-- The base game's background ladders ("Background Ladder", Bunker Systems): no material of their own (the hatch they stand in stays
-- open), and a script node at the middle of each 24 px piece that takes hold of an AHuman in front of it. Aiming up and pressing up
-- moves it up, aiming down and pressing down moves it down, left or right moves it along its aim, and with nothing pressed it is held
-- still against gravity; only the jetpack's key lets go. A unit that jetted up one in pulses was caught and held every time the jet
-- went out, and one that dropped slowly down a laddered hatch was stopped half way. So a ladder is climbed the way a player climbs it.
-- The nodes are looked up from the scene's particles every few seconds and kept.
-- (Locals, not fields of SharedBehaviors: the table is made read-only once this file has loaded, and a field set at run time was an
-- error that ended every unit's movement the first time it planned a climb, so nobody used the jetpack at all.)
-- Flights are flown by the engine's pilot (AHuman::PilotFlight) unless CCCP_LUA_PILOT=1 asks for the script's (SharedBehaviors.FlightControl),
-- for comparing the two. (Flight gym, 14 proven courses: engine 28/28 landed, 6.8 s, 3.8 s of fuel, 17 px overshoot, 0.1 reversals; script
-- 23/28, 9.6 s, 7.2 s, 41 px, 0.8; the original AI 14/42.) A build without the pilot (an older exe) falls back to the script's.
-- Whether the build running this has the engine's route-follower (AHuman::MoveAlongRoute), and the scripts are to use it: CCCP_LUA_MOVER=1
-- keeps the script's own (GoToWpt), for comparing the two. (A unit with a digger keeps the script's too, which digs; the engine's doesn't yet.)
local LuaMover = os and os.getenv and os.getenv("CCCP_LUA_MOVER") == "1";
function SharedBehaviors.UsesEngineMover(Owner)
	if LuaMover or not Owner.Head then
		return false;
	end
	local ok, member = pcall(function() return Owner.MoveAlongRoute; end);
	if not (ok and member ~= nil) then
		return false;
	end
	-- (Diggers too, where the engine digs along a route: a build with the motor. An older one's follower doesn't dig.)
	return not Owner:HasObjectInGroup("Tools - Diggers") or SharedBehaviors.EngineMotor(Owner);
end

-- The engine's motor (AHuman.SetAIStance, AHuman.TacticalMoveTo; see AHuman::UpdateAIMotor): the scripts say where to go and how to hold the
-- body, and the engine walks, crawls, climbs and flies. Asked without the error a missing member is, for a build without it (an older exe),
-- where the scripts press the keys themselves as they did.
function SharedBehaviors.EngineMotor(Owner)
	local ok, value = pcall(function() return Owner.TacticalMoveActive; end);
	return ok and value ~= nil;
end

-- Whether the engine's scan (Actor.ScanForEnemies) is there, for a build without it (an older exe), where the scripts look with one
-- random ray a tick as they did.
function SharedBehaviors.CanScan(Owner)
	local ok, value = pcall(function() return Owner.ScanForEnemies; end);
	return ok and value ~= nil;
end

-- Spotting by the engine's scan (AC-1): a person's look, not a random ray. The scan says which enemies are in view and how plainly
-- (Actor.ScanForEnemies); noticing one takes a moment that is shorter the plainer it is and the better the unit, so a poor unit is slow
-- to react, not blind, and a crawling enemy at the edge of the view can get close. Run every update while fighting, every third while
-- not, which keeps the rays near the one a tick the old look cast. @return The enemy noticed (the plainest), and where the look landed on
-- it; or nothing.
-- @param fovDegrees The field of view about the facing. @param budget The rays a scan may cast.
function SharedBehaviors.ScanForTargets(AI, Owner, skill, fovDegrees, budget)
	local fighting = AI.Target ~= nil;
	AI.scanTick = (AI.scanTick or 0) + 1;
	if not fighting and AI.scanTick % 3 ~= 0 then
		return nil;
	end
	AI.ScanClock = AI.ScanClock or Timer();
	AI.Noticing = AI.Noticing or {};
	local now = AI.ScanClock.ElapsedSimTimeMS;
	local elapsed = math.min((now - (AI.lastScanMS or now)) / 1000, 0.5);
	AI.lastScanMS = now;
	-- (As far as the old look reached: the aim distance and half a screen.)
	local range = Owner.AimDistance + FrameMan.PlayerScreenWidth * 0.51;
	-- How fast noticing fills, per second at full visibility: about 0.3 s for the best units, about 1.2 s for the worst; skill 0 to 100.
	local rate = (0.8 + (skill or 50) / 40) * Owner.Perceptiveness;
	local noticed, noticedHit, noticedVisibility, noticedID;
	local current, currentHit, currentID;
	for Sighting in Owner:ScanForEnemies(fovDegrees, range, budget) do
		local Target = Sighting.Target;
		local HitPos = Sighting.HitPos;
		-- AI-teams ignore the fog
		if Target and (not AI.isPlayerOwned or not SceneMan:IsUnseen(HitPos.X, HitPos.Y, Owner.Team) or not SceneMan:IsUnseen(Target.Pos.X, Target.Pos.Y, Owner.Team)) then
			local id = Target.UniqueID;
			local entry = AI.Noticing[id];
			if not entry then
				entry = {progress = 0};
				AI.Noticing[id] = entry;
			end
			-- (A target already being fought is known: no delay to keep it.)
			if AI.Target and MovableMan:ValidMO(AI.Target) and AI.Target.UniqueID == id then
				entry.progress = 1;
				current, currentHit, currentID = Target, Vector(HitPos.X, HitPos.Y), id;
			else
				-- (At least a tick's worth, for the first scan.)
				entry.progress = entry.progress + Sighting.Visibility * rate * math.max(elapsed, TimerMan.DeltaTimeSecs);
			end
			entry.seenMS = now;
			if entry.progress >= 1 and (not noticed or Sighting.Visibility > noticedVisibility) then
				noticed, noticedHit, noticedVisibility, noticedID = Target, Vector(HitPos.X, HitPos.Y), Sighting.Visibility, id;
			end
		end
	end
	-- Out of view for a second and a half: what was half-noticed is forgotten.
	for id, entry in pairs(AI.Noticing) do
		if now - entry.seenMS > 1500 then
			AI.Noticing[id] = nil;
		end
	end
	-- The enemy being fought, while in view, is what is answered (so the fight keeps it, TargetLostTimer and all), but every fourth scan
	-- the plainest other one noticed, so a worse threat can take its place, as the old look's stray rays let it. (By UniqueID: luabind has no == for two Actor
	-- userdata, and threw "No such operator defined" here.)
	if current and (not noticed or noticedID == currentID or AI.scanTick % 4 ~= 0) then
		return current, currentHit;
	end
	return noticed, noticedHit;
end

-- The crouched stance for SharedBehaviors.Stance (the engine's stance 1): ducked behind low cover or made small, on its feet.
SharedBehaviors.CROUCHED = "crouched";

-- How pinned down and how steady a unit is (Actor.Suppression and Actor.Morale, AC-3): 0 and 1 for Unfair AI, which ignores it, and for a
-- build without them. @return suppression, morale.
function SharedBehaviors.Suppression(AI, Owner)
	if AI.ignoresSuppression == nil then
		local ok, value = pcall(function() return Owner.Suppression; end);
		AI.ignoresSuppression = not (ok and value ~= nil) or (AI.skill or Activity.DEFAULTSKILL) >= Activity.UNFAIRSKILL;
	end
	if AI.ignoresSuppression then
		return 0, 1;
	end
	return Owner.Suppression, Owner.Morale;
end

-- Whether a unit's nerve has gone (morale under 0.3): it pulls back whatever its health (see RetreatUpdate).
function SharedBehaviors.Shaken(AI, Owner)
	local _, morale = SharedBehaviors.Suppression(AI, Owner);
	return morale < 0.3;
end

-- A stance for a while: AHuman.PRONE, AHuman.NOTPRONE or SharedBehaviors.CROUCHED. AI.proneState says whether it is prone, for the rules
-- that read it ("not while prone": no strafe, no cover move, no jet, no second GoProne); a crouch is not prone. With the engine's motor,
-- the native AI keeps the engine's stance up while it does (see NativeHumanAI:Update). (Left NOTPRONE, every one of those guards was dead,
-- and a unit strafed sideways lying down.) The crouch is the engine motor's only: without it the stance is just not prone.
function SharedBehaviors.Stance(AI, Owner, stance, milliseconds)
	local crouched = stance == SharedBehaviors.CROUCHED;
	if SharedBehaviors.EngineMotor(Owner) then
		Owner:SetAIStance(stance == AHuman.PRONE and 2 or (crouched and 1 or 0), milliseconds or 1000);
	end
	AI.proneState = crouched and AHuman.NOTPRONE or stance;
end

-- A short move on this floor to a place, for a while; the engine walks (or crawls) it. @return Whether it is still on its way.
function SharedBehaviors.StepTo(AI, Owner, Place, milliseconds)
	if SharedBehaviors.EngineMotor(Owner) then
		Owner:TacticalMoveTo(Place, milliseconds or 3000);
		return true;
	end
	local dx = SceneMan:ShortestDistance(Owner.Pos, Place, false).X;
	if math.abs(dx) > 6 then
		AI.lateralMoveState = dx > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
		return true;
	end
	AI.lateralMoveState = Actor.LAT_STILL;
	return false;
end

-- Following a unit (a squad's leader, or one we were told to follow), as 8.0's GoToWpt did it, for the route-follower's wrapper: near the
-- place and in plain sight (a place in line behind a leader, AI.squadPoint, else the unit itself), walked straight to; in the place,
-- held still until it moves off (the leader walks on, or a door's sweep); an enemy unit close by, walked at. @return Whether this tick's
-- movement is handled here (AI.lateralMoveState set), or left to the route-follower.
function SharedBehaviors.FollowStep(AI, Owner)
	local Target = Owner.MOMoveTarget;
	if not Target or not MovableMan:ValidMO(Target) or not IsActor(Target) or AI.flying then
		AI.followHold = false;
		return false;
	end
	local TargetActor = ToActor(Target);
	local H = Owner.Height;
	local targetH = TargetActor.Height or 100;
	if Target.Team ~= Owner.Team then
		local ToTarget = SceneMan:ShortestDistance(Owner.Pos, Target.Pos, false);
		if ToTarget.Largest < H * 0.33 + targetH * 0.33 then
			SharedBehaviors.StepTo(AI, Owner, Target.Pos, 400);
			return true;
		end
		return false;
	end
	local Goal = AI.squadPoint or Target.Pos;
	local ToGoal = SceneMan:ShortestDistance(Owner.Pos, Goal, false);
	local Lift = Vector(0, -H * 0.2);
	local function InSight()
		return SceneMan:CastObstacleRay(Owner.Pos + Lift, ToGoal, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 6) < 0;
	end
	-- (A place in line stands off the leader already, so its radius is under half the gap between places, or two followers hold one spot.)
	local nearIn = AI.squadPoint and 0.15 or 0.3;
	local nearOut = AI.squadPoint and 0.25 or 0.4;
	local moving = Target.Vel.Largest > 1;
	-- Near the place and in a door's sweep: out of it. (Only near the place: a follower on its way through a doorway walks on through.)
	local Exit = not moving and ToGoal.Largest < H * 1.5 and SharedBehaviors.DoorSweepExit(Owner, Goal);
	if Exit then
		AI.followHold = false;
		SharedBehaviors.StepTo(AI, Owner, Exit, 600);
		return true;
	end
	-- (Out of it, with the place still in the open door's sweep: waited for here, near the place, till the door shuts.)
	if not moving and ToGoal.Largest < H * 1.5 and SharedBehaviors.DoorSweptBy(Owner, Goal) then
		if SharedBehaviors.EngineMotor(Owner) and Owner.TacticalMoveActive then
			Owner:CancelTacticalMove();
		end
		return true;
	end
	if AI.followHold then
		local off = ToGoal.Largest > H * nearOut + targetH * nearOut or moving;
		if not off and InSight() then
			-- (Held: no move of the engine's either.)
			if SharedBehaviors.EngineMotor(Owner) and Owner.TacticalMoveActive then
				Owner:CancelTacticalMove();
			end
			return true;
		end
		AI.followHold = false;
	elseif not moving and ToGoal.Largest <= H * nearIn + targetH * nearIn and InSight() then
		AI.followHold = true;
		return true;
	end
	if ToGoal:MagnitudeIsLessThan(H * 1.5) and math.abs(ToGoal.Y) < H * 0.3 and InSight() then
		-- (Walked by the engine's motor, a short move to the place, renewed each tick while it is near and in sight.)
		if math.abs(ToGoal.X) > 3 then
			SharedBehaviors.StepTo(AI, Owner, Goal, 400);
		end
		return true;
	end
	return false;
end

-- The move behaviour on the engine's route-follower: a coroutine like GoToWpt, so the AI drives it the same way, but each tick is one
-- call of AHuman::MoveAlongRoute, which sets the controls itself. The script keeps what is its own: fighting on the move, following a
-- unit near at hand (SharedBehaviors.FollowStep), and what to do on arrival. (Gold digging keeps 8.0's GoToWpt: a digger's unit uses it.)
function SharedBehaviors.GoToRoute(AI, Owner, Abort)
	Owner:ResetRouteMovement();
	AI.routeHeld = false;
	AI.jetClimb = false;
	Owner:RemoveNumberValue("AI_StuckForTime");
	while true do
		local holding = false;
		if AI.Target and AI.BehaviorName ~= "AttackTarget" and not AI.PickupHD and not SharedBehaviors.FightsOnTheMove(AI, Owner) then
			holding = true;
		elseif Owner.AIMode ~= Actor.AIMODE_SQUAD and (AI.BehaviorName == "ShootArea" or AI.BehaviorName == "FaceAlarm") and not SharedBehaviors.FightsOnTheMove(AI, Owner) then
			holding = true;
		end
		AI.lateralMoveState = Actor.LAT_STILL;
		AI.jump = false;
		AI.pilotFlight = true; -- (The engine holds the jet as it means to: no hold timer of the native AI's over it.)
		-- Following a unit near at hand: walked or held here, not routed (the route-follower never "arrives" at a unit, and shoved for its spot).
		-- A flight under way is flown on to its landing whatever turns up: held mid-air, the jet went out under the unit, and when the
		-- route-follower was called again it judged the flight "down again under the landing" and marked a good take-off failed for 20 s
		-- (for its team 10). (Humans only: the crab follower plans no flights.)
		if holding and Owner.ClassName == "AHuman" and Owner.FlyingRoute then
			holding = false;
		end
		local following = not holding and SharedBehaviors.FollowStep(AI, Owner);
		if following then
			holding = true;
			AI.wasFollowing = true;
		elseif AI.wasFollowing then
			-- (Back to the route: its timers start again, or the time spent following read as being stuck.)
			AI.wasFollowing = false;
			Owner:ResetRouteMovement();
		end
		AI.engineMover = not holding; -- (And the run key: the follower runs where the way is open.)
		if holding then
			AI.routeHeld = true;
		elseif AI.routeHeld then
			-- Back from a hold (a fight, a look at an alarm): the route-follower's timers and flight start again. It isn't called while
			-- held, so its stuck timers ran on through the hold, and the first tick after a fight of more than 6 s marked the step it was on
			-- avoided, for the whole team when a flight was ahead, and asked for a new route; a flight cut short by the hold was judged
			-- failed and its take-off avoided too.
			AI.routeHeld = false;
			Owner:ResetRouteMovement();
		end
		if not holding then
			local result = Owner:MoveAlongRoute();
			if result == 1 then
				-- Arrived.
				AI.engineMover = false;
				if Owner.AIMode == Actor.AIMODE_GOTO then
					AI.SentryFacing = Owner.HFlipped;
					AI.SentryPos = Vector(Owner.Pos.X, Owner.Pos.Y);
					AI:CreateSentryBehavior(Owner);
				end
				Owner:ClearAIWaypoints();
				Owner:ClearMovePath();
				Owner:DrawWaypoints(false);
				return true;
			elseif result == 2 then
				-- No route to it: the order is dropped, as GoToWpt drops it.
				AI.engineMover = false;
				if Owner:IsAITracedOn("AI") then ConsoleMan:PrintString("AITRACE GoToRoute: no route; standing down"); end
				Owner:ClearAIWaypoints();
				Owner:ClearMovePath();
				Owner:DrawWaypoints(false);
				return true;
			end
		end
		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then
			AI.engineMover = false;
			return true;
		end
	end
end

-- The navigation debug overlay's level (SettingsMan.NavDebugOverlay), or 0 on a build that hasn't got it: the scripts are read by whatever
-- build runs them, and reading a member the build lacks is an error that would end the unit's movement script.
-- Whether the build running this has the engine's pilot (AHuman::PilotFlight), asked without the error a missing member is.
function SharedBehaviors.HasEnginePilot(Owner)
	local ok, member = pcall(function() return Owner.PilotFlight; end);
	return ok and member ~= nil;
end

function SharedBehaviors.NavDebugLevel()
	local ok, level = pcall(function() return SettingsMan.NavDebugOverlay; end);
	return (ok and type(level) == "number") and level or 0;
end

-- CCCP_NO_FLIGHT_THROTTLE=1: the flight plan looked for every tick, for measuring what the limit saves.
local FlightThrottleOff = os and os.getenv and os.getenv("CCCP_NO_FLIGHT_THROTTLE") == "1";

local EnginePilot = not (os and os.getenv and os.getenv("CCCP_LUA_PILOT") == "1");

local LadderCache = nil;
local LadderCacheTimer = nil;
function SharedBehaviors.LadderNodes()
	if not LadderCacheTimer then
		LadderCacheTimer = Timer();
	end
	if not LadderCache or LadderCacheTimer:IsPastSimMS(4000) then
		LadderCacheTimer:Reset();
		local nodes = {};
		for mo in MovableMan.Particles do
			if mo.PresetName == "Background Ladder Node" and mo.PinStrength > 0 then
				table.insert(nodes, Vector(mo.Pos.X, mo.Pos.Y));
			end
		end
		LadderCache = nodes;
	end
	return LadderCache;
end

-- The ladder piece nearest a point, within so far sideways and so far up or down. @return Its middle, or nil.
function SharedBehaviors.LadderAt(Point, reachX, reachY)
	local best, bestDistance = nil, math.huge;
	for _, Node in ipairs(SharedBehaviors.LadderNodes()) do
		local Off = SceneMan:ShortestDistance(Point, Node, false);
		if math.abs(Off.X) <= reachX and math.abs(Off.Y) <= reachY and Off.Magnitude < bestDistance then
			best, bestDistance = Node, Off.Magnitude;
		end
	end
	return best;
end

-- Where a unit in open flight steers for: the furthest of the route's next few points its whole body has a clear way to (rays from
-- its head and its feet height), not always the first. A route found in the air starts at the node the unit is in, so its first point
-- was often behind or under a unit moving the other way, and steering for it turned the unit round mid-air for a point it had no need
-- of; a person cuts the corner to the point they can see. (The pops in GoToWpt drop the points as they are passed.)
-- @return The point to steer for.
function SharedBehaviors.FlightTarget(Owner, First)
	local target = First;
	local rise = Vector(0, -Owner.Height * 0.3);
	local drop = Vector(0, Owner.Height * 0.3);
	local index = 0;
	for pos in Owner.MovePath do
		index = index + 1;
		if index > 6 then
			break;
		end
		if index > 1 then
			local To = SceneMan:ShortestDistance(Owner.Pos, pos, false);
			if To:MagnitudeIsGreaterThan(Owner.Height * 4) then
				break;
			end
			if SceneMan:CastObstacleRay(Owner.Pos + rise, To, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) >= 0
			or SceneMan:CastObstacleRay(Owner.Pos + drop, To, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) >= 0 then
				break;
			end
			target = pos;
		end
	end
	return target;
end

-- A flight from take-off to touchdown, flown as one (see SharedBehaviors.FlightControl): where the route leaves the ground, its landing is
-- the first point after the airborne part with a floor under it, and the unit flies straight for that, rising first, then across, then
-- down, rather than for each of the route's points 24 px apart with the climbs flown by other code. That handover was where landings were
-- overshot by a hundred pixels and turned back for.
-- Only in open air: the rise at the unit's own column and the crossing at the landing's height must both be clear, or the shaft and
-- ledge climbers keep the climb.
-- @return The landing (the point, and its floor's y), or nil when the route has no flight ahead or it isn't in open air.
function SharedBehaviors.FindLanding(Owner)
	-- (The furthest landing along the route that can be flown to straight, through open air and on one tank, not just the first: a route
	-- across a gap too wide for the grid's jumps dropped into the valley and climbed out, and the unit dived after it instead of flying
	-- across as a person would.)
	local h = Owner.Height;
	local airborne = false;
	local index = 0;
	local first = nil;
	local candidates = {};
	-- (The air is looked for along each leg of the route too, every 16 px: a hop across a gap is one leg from floor to floor, both of its
	-- points on the ground, and looked at by its points alone it was no flight at all, so the old burst code flew it.)
	local Last = Vector(Owner.Pos.X, Owner.Pos.Y); -- (The route's points start after the unit's own place: its first leg is from here.)
	for pos in Owner.MovePath do
		index = index + 1;
		if index > 30 then
			break;
		end
		if Last then
			local Leg = SceneMan:ShortestDistance(Last, pos, false);
			local samples = math.floor(Leg.Magnitude / 16);
			for k = 1, samples - 1 do
				local Sample = Last + Leg * (k / samples);
				if not SceneMan:CastStrengthRay(Sample, Vector(0, h * 0.8), 5, Vector(), 2, rte.grassID, true) then
					airborne = true;
					break;
				end
			end
		end
		local Floor = Vector();
		local grounded = SceneMan:CastStrengthRay(pos, Vector(0, h * 0.8), 5, Floor, 2, rte.grassID, true);
		if not grounded then
			airborne = true;
		elseif airborne then
			local landing = { pos = Vector(pos.X, pos.Y), floorY = Floor.Y };
			first = first or landing;
			table.insert(candidates, landing);
			airborne = false;
		end
		Last = pos;
	end
	if not first then
		return nil;
	end
	local Pack = Owner.Jetpack;
	local ppm = GetPPM();
	for k = #candidates, math.max(2, #candidates - 3), -1 do
		local landing = candidates[k];
		local Distance = SharedBehaviors.FlightDistance(Owner, landing);
		local needed = (math.max(0, Distance.rise) / (4 * ppm) + math.abs(Distance.across) / (5 * ppm) * 0.5) * 1000 * 1.3 + 200;
		if Pack and needed <= Pack.JetTimeTotal * 0.95 and SharedBehaviors.FlightWayClear(Owner, landing) then
			return landing;
		end
	end
	return first;
end

-- How far a landing is from the unit: the rise to just over its floor (px, up positive) and the distance across.
function SharedBehaviors.FlightDistance(Owner, landing)
	local To = SceneMan:ShortestDistance(Owner.Pos, landing.pos, false);
	local feetY = Owner.Pos.Y + Owner.Height * 0.2;
	return { rise = feetY - landing.floorY + 12, across = To.X };
end

-- Whether the way to a landing is open air for the flight: up at the unit's column to over the landing's floor, then across.
function SharedBehaviors.FlightWayClear(Owner, landing)
	local h = Owner.Height;
	local cruiseY = math.min(Owner.Pos.Y, landing.floorY - 12 - h * 0.5);
	local Up = Vector(0, cruiseY - Owner.Pos.Y - h * 0.3);
	local Over = SceneMan:ShortestDistance(Vector(Owner.Pos.X, cruiseY), Vector(landing.pos.X, cruiseY), false);
	for _, dx in ipairs({ -h * 0.15, h * 0.15 }) do
		if Up.Y < -2 and SceneMan:CastObstacleRay(Owner.Pos + Vector(dx, -h * 0.3), Up, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) >= 0 then
			return false;
		end
	end
	for _, dy in ipairs({ -h * 0.35, h * 0.35 }) do
		if SceneMan:CastObstacleRay(Vector(Owner.Pos.X, cruiseY + dy), Over, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) >= 0 then
			return false;
		end
	end
	return true;
end

-- Whether a landing can be walked to instead of flown: the floor followed from under the unit to under the landing, every 6 px, never
-- climbing more than a step a soldier takes or mantles (a quarter of its height) nor dropping more than a safe step down (0.6 of it), with
-- room to crawl over it all the way. A person asks this before lighting a jetpack; the route's points can say "jump" for a few 20 px steps.
function SharedBehaviors.CanWalkTo(Owner, landing)
	local h = Owner.Height;
	local stepUp = h * 0.24;
	local stepDown = h * 0.6;
	local room = h * 0.24;
	local Floor = Vector();
	if not SceneMan:CastStrengthRay(Owner.Pos, Vector(0, h * 0.8), 5, Floor, 2, rte.grassID, true) then
		return false;
	end
	local floorY = Floor.Y;
	local To = SceneMan:ShortestDistance(Owner.Pos, landing.pos, false);
	-- (Every 12 px, and no further than 360 px: every 6 px over any distance, every tick a jet was wanted, was a share of the hitches seen
	-- while units flew.)
	if math.abs(To.X) > 360 then
		return false;
	end
	local steps = math.floor(math.abs(To.X) / 12);
	local dir = To.X > 0 and 1 or -1;
	for k = 1, steps do
		local x = Owner.Pos.X + dir * k * 12;
		-- A wall higher than a step: solid where the step's top would be.
		if SceneMan:GetTerrMatter(x, floorY - stepUp) ~= rte.airID then
			return false;
		end
		local Next = Vector();
		if not SceneMan:CastStrengthRay(Vector(x, floorY - stepUp), Vector(0, stepUp + stepDown), 5, Next, 1, rte.grassID, true) then
			return false; -- (A gap, or a drop too far to step down.)
		end
		floorY = Next.Y;
		if SceneMan:CastStrengthRay(Vector(x, floorY - 2), Vector(0, -room), 5, Vector(), 1, rte.grassID, true) then
			return false; -- (No room to get through.)
		end
	end
	return math.abs(floorY - landing.floorY) <= 6;
end

-- The flight plan for a tick: begun when the planner wants the jet (or the unit is in the air) and the route has a landing ahead in open
-- air, ended on the landing, or when the unit is back on its feet anywhere else.
-- @param plan Last tick's plan, or nil. @param wantsJet Whether the walking and climbing code below asked for the jet this tick.
-- @return This tick's plan, or nil.
function SharedBehaviors.UpdateFlightPlan(AI, Owner, plan, wantsJet)
	local h = Owner.Height;
	local Pack = Owner.Jetpack;
	if not Owner.Head or not Pack or Pack.JetpackType ~= AEJetpack.Standard or AI.proneState == AHuman.PRONE then
		return nil;
	end
	if plan then
		local To = SceneMan:ShortestDistance(Owner.Pos, plan.landing.pos, false);
		local feetY = Owner.Pos.Y + h * 0.5;
		local onLanding = math.abs(To.X) < h * 0.25 and math.abs(feetY - plan.landing.floorY) < h * 0.3;
		if not AI.flying and plan.timer:IsPastSimMS(400) and (onLanding or plan.timer:IsPastSimMS(1200)) then
			return nil;
		end
		if plan.timer:IsPastSimMS(15000) then
			return nil;
		end
		-- Out of fuel in the air, short of the landing: the plan is over. Flown on, it steered for a landing it could no longer reach while
		-- the unit fell; from here the route is asked again from where it is coming down.
		if AI.flying and Pack.JetTimeLeft < 60 and not onLanding then
			if Owner:IsAITracedOn("Pilot") then ConsoleMan:PrintString("AITRACE flight: out of fuel short of the landing, plan dropped"); end
			Owner:RequestRouteCheck();
			return nil;
		end
		return plan;
	end
	if not (wantsJet or AI.flying) then
		return nil;
	end
	-- (Not begun in the air on an empty tank: there is nothing to fly it with.)
	if AI.flying and Pack.JetTimeLeft < 200 then
		return nil;
	end
	local landing = SharedBehaviors.FindLanding(Owner);
	if not landing then
		-- A route on the ground all the way (steps, a slope): walked, no jet, when the floor can be followed to a point a few along it. The
		-- hop code jetted every 20 px step of a stair for want of this question.
		if not AI.flying then
			local ahead = nil;
			local index = 0;
			for pos in Owner.MovePath do
				index = index + 1;
				ahead = pos;
				if index >= 4 then
					break;
				end
			end
			if ahead then
				local Floor = Vector();
				if SceneMan:CastStrengthRay(ahead, Vector(0, Owner.Height * 0.8), 5, Floor, 2, rte.grassID, true) and SharedBehaviors.CanWalkTo(Owner, { pos = ahead, floorY = Floor.Y }) then
					return nil, true;
				end
			end
		end
		return nil;
	end
	-- On the ground and it can be walked: no flight, and no jet (see CanWalkTo).
	if not AI.flying and SharedBehaviors.CanWalkTo(Owner, landing) then
		return nil, true;
	end
	-- Falling with no jet asked for, towards a landing below: a drop, left to gravity (the native AI lights the jet itself at a dangerous
	-- speed). A plan begun here jetted every step down a stair.
	if AI.flying and not wantsJet and landing.floorY > Owner.Pos.Y and math.abs(SceneMan:ShortestDistance(Owner.Pos, landing.pos, false).X) < h * 0.5 then
		return nil;
	end
	-- Off the ground only with the fuel the flight takes, and a third over: the climb at about 4 m/s, the crossing lit about half the time,
	-- never more than a full tank. Short of it the unit waits on its feet with the jet out, whatever else asked for it; it used to set off
	-- on what was left from the last flight, and fell short.
	if not AI.flying then
		local ToLanding = SharedBehaviors.FlightDistance(Owner, landing);
		local ppm = GetPPM();
		local needed = (math.max(0, ToLanding.rise) / (4 * ppm) + math.abs(ToLanding.across) / (5 * ppm) * 0.5) * 1000 * 1.3 + 200;
		needed = math.min(needed, Pack.JetTimeTotal * 0.98);
		if Pack.JetTimeLeft < needed then
			if Owner:IsAITracedOn("Pilot") and math.random() < 0.05 then ConsoleMan:PrintString("AITRACE flight: waiting for fuel, " .. math.floor(Pack.JetTimeLeft) .. " of " .. math.floor(needed)); end
			return nil, true;
		end
	end
	local To = SceneMan:ShortestDistance(Owner.Pos, landing.pos, false);
	if math.abs(To.X) < h * 0.3 and math.abs(To.Y) < h * 0.6 then
		return nil; -- (A hop: the walking code's.)
	end
	if not SharedBehaviors.FlightWayClear(Owner, landing) then
		return nil;
	end
	if Owner:IsAITracedOn("Pilot") then ConsoleMan:PrintString("AITRACE flight: plan from " .. math.floor(Owner.Pos.X) .. "," .. math.floor(Owner.Pos.Y) .. " to land at " .. math.floor(landing.pos.X) .. "," .. math.floor(landing.floorY)); end
	return { landing = landing, state = {}, timer = Timer() };
end

-- Steering in open flight towards a point, for a humanoid in the air or taking off (the planner in GoToWpt decides the take-off). Flown
-- the way a person flies a jetpack, and the way the flight gym's scripted pilot proved every one of its courses (Tools/RenderTest/
-- RenderTest.rte/FlightGym.lua): up to a little over the height of where it is going, across at a speed that suits the distance,
-- braking so as to stop over it, and only then down onto it. The old way steered for the point itself at every moment, sideways and
-- up at once: it came at landings from below, caught their edges, ran past them, and turned round in the air for them.
-- The point's floor: the ground under it within most of a body; with none (a point in the air on the way somewhere), the point is passed
-- through at its own height, feet a little under it.
-- @param state A table kept between ticks (the key and the jet held, the point's floor). @return The move key, whether to jet, and the aim.
function SharedBehaviors.FlightControl(AI, Owner, Target, state)
	local ppm = GetPPM();
	local g = SceneMan.GlobalAcc.Y * ppm;
	local h = Owner.Height;
	local feetY = Owner.Pos.Y + h * 0.5;
	local To = SceneMan:ShortestDistance(Owner.Pos, Target, false);
	-- The floor under the point, looked for again when the point moves.
	if not state.target or SceneMan:ShortestDistance(state.target, Target, false):MagnitudeIsGreaterThan(4) then
		state.target = Vector(Target.X, Target.Y);
		local Floor = Vector();
		if SceneMan:CastStrengthRay(Target, Vector(0, h * 0.8), 5, Floor, 2, rte.grassID, true) then
			state.floorY = Floor.Y;
		else
			state.floorY = nil;
		end
	end
	local landing = state.floorY ~= nil;
	local floorY = state.floorY or (Target.Y + h * 0.4);
	-- Over it: near enough across to come straight down.
	local overIt = math.abs(To.X) < math.max(6, h * 0.12);
	-- Up and down: the speed wanted to the height wanted (m/s, down positive). Feet carried a little over the floor while crossing; over
	-- it, down onto it gently.
	local margin = landing and 12 or 0;
	local wantFeetY = (overIt and landing) and (floorY + 4) or (floorY - margin);
	local toGo = feetY - wantFeetY;
	local wantVy;
	if toGo > 0 then
		wantVy = -math.min(9, math.sqrt(2 * g * toGo) / ppm);
	else
		wantVy = math.min((overIt and landing) and 2.5 or 6, math.sqrt(2 * g * 0.6 * -toGo) / ppm);
	end
	local jump = Owner.Vel.Y > wantVy + (state.jump and -0.3 or 0.3);
	-- Across: once the feet are up near the height (or the way across is downhill), at a speed that suits the distance, brought down to
	-- what the braking can stop from in what is left. Not before: crossing low, the unit met the landing's edge from under it.
	local wantVx = 0;
	if feetY < floorY + 8 or toGo < 0 then
		local speed = math.max(3, math.min(7, math.abs(To.X) / 100));
		local brake = speed > 5 and 160 or 120;
		local stopRoom = math.max(0, math.abs(To.X) - 4);
		wantVx = (To.X > 0 and 1 or -1) * math.min(speed, math.sqrt(2 * brake * stopRoom) / ppm);
	end
	local off = wantVx - Owner.Vel.X;
	local lat = state.lat or Actor.LAT_STILL;
	if off > 0.4 then
		lat = Actor.LAT_RIGHT;
	elseif off < -0.4 then
		lat = Actor.LAT_LEFT;
	elseif math.abs(off) < 0.2 then
		lat = Actor.LAT_STILL;
	end
	state.lat = lat;
	-- The sideways push is the jet's lean: lit for it when well off the speed wanted, unless that would be rising faster than the height wants.
	if lat ~= Actor.LAT_STILL and math.abs(off) > 1 and Owner.Vel.Y > wantVy - 2 then
		jump = true;
	end
	-- Just over a floor level with the point's: down onto it and walk, no jet. (Hovering along a corridor a little over its floor, the head
	-- met the ceiling, and a unit at the foot of a shaft was held against the lip of the corridor it was to walk into until the tank was dry.)
	if landing and not overIt then
		local Under = Vector();
		local Mid = Vector();
		-- (And floor half way there too: over the start of a gap, the floor under the feet is level with the far side's, but it ends.)
		if SceneMan:CastStrengthRay(Owner.Pos, Vector(0, h * 0.9), 5, Under, 2, rte.grassID, true) and math.abs(Under.Y - floorY) < 10
		and SceneMan:CastStrengthRay(Owner.Pos + Vector(To.X * 0.5, 0), Vector(0, h * 0.9), 5, Mid, 2, rte.grassID, true) and math.abs(Mid.Y - floorY) < 10 then
			jump = false;
		end
	end
	state.jump = jump;
	local aim = lat ~= Actor.LAT_STILL and 0 or math.pi * 0.5;
	return lat, jump, aim;
end

-- The floor under a point, looking down so far. @return Its y, or nil.
function SharedBehaviors.FloorUnder(Owner, Point, reach)
	local Hit = Vector();
	if SceneMan:CastObstacleRay(Point, Vector(0, reach), Hit, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 2) >= 0 then
		return Hit.Y;
	end
	return nil;
end

-- Whether a vertical line is open from one height up to another at an x (a door of ours across it counts as open: it opens as we come).
function SharedBehaviors.ColumnOpen(Owner, x, fromY, toY)
	if toY >= fromY then
		return true;
	end
	local Hit = Vector();
	return SceneMan:CastObstacleRay(Vector(x, fromY), Vector(0, toY - fromY), Hit, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 2) < 0 or SharedBehaviors.OurDoorAt(Owner, Hit) ~= nil;
end

-- A climb's plan from the route, or nil when this is no climb for the climber. @param Top The jump point well above (the route's first
-- point). @param NextPos The route's point after it, if any.
function SharedBehaviors.ClimbPlan(AI, Owner, Top, NextPos)
	local H = Owner.Height;
	local feetBelowPos = H * 0.2;
	local headAbovePos = SharedBehaviors.HeadAbovePos(Owner);
	local plan = { stage = "align", top = Vector(Top.X, Top.Y), pops = 1, timer = Timer(), stageTimer = Timer() };
	-- The landing: the point after the top, when it is level with it and close beside it (the pather's "up, then over"). Without one the
	-- top is a point on a floor of its own and the climb ends standing there.
	plan.landing = Vector(Top.X, Top.Y);
	if NextPos and math.abs(NextPos.Y - Top.Y) <= H * 0.5 and math.abs(SceneMan:ShortestDistance(Top, NextPos, false).X) <= H * 1.2 then
		plan.landing = Vector(NextPos.X, NextPos.Y);
		plan.pops = 2;
	end
	-- The column: the middle of the free channel the body goes up, where it is narrowest. Looked at every 6 px from the head's start to the
	-- top, out to 0.6 of a body either side of the pather's column node: the walls' nearest faces (a ladder's rungs and its pole are the
	-- left wall's, wherever they reach furthest) bound the channel, and the climb goes up its middle. Up the node's own x, a soldier 28 px
	-- wide climbed with its side in a ladder's rungs and burned a tank getting two thirds of the way.
	local colX = Top.X;
	local leftFace, rightFace = nil, nil;
	local reach = H * 0.6;
	for y = Owner.Pos.Y - headAbovePos, Top.Y - headAbovePos, -6 do
		local Probe = Vector(Top.X, y);
		if SceneMan:GetTerrMatter(Probe.X, Probe.Y) == rte.airID then
			local Hit = Vector();
			if SceneMan:CastObstacleRay(Probe, Vector(-reach, 0), Hit, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 1) >= 0 then
				leftFace = math.max(leftFace or -math.huge, Hit.X);
			end
			if SceneMan:CastObstacleRay(Probe, Vector(reach, 0), Hit, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 1) >= 0 then
				rightFace = math.min(rightFace or math.huge, Hit.X);
			end
		end
	end
	if leftFace and rightFace and rightFace > leftFace then
		colX = (leftFace + rightFace) * 0.5;
	elseif leftFace then
		-- A wall on one side only (a cliff, a ledge): keep the body clear of it.
		colX = math.max(Top.X, leftFace + Owner.Height * 0.16);
	elseif rightFace then
		colX = math.min(Top.X, rightFace - Owner.Height * 0.16);
	end
	plan.channelLeft, plan.channelRight = leftFace, rightFace;
	-- A shaft or a hatch: walls both sides of the column, near enough that the body has to keep to its middle. That is what this climber is
	-- for; a climb up an open cliff or a ledge on a hill is left to the walking code, which flies those well.
	plan.shaft = leftFace ~= nil and rightFace ~= nil and (rightFace - leftFace) <= H * 1.2;
	-- Open from where the head starts to where it will be at the top; else the nearest open line a little either side; else no climb here.
	local headStart = Owner.Pos.Y - headAbovePos + 2;
	local headTop = Top.Y - headAbovePos + 2;
	local found = nil;
	for _, dx in ipairs({ 0, -H * 0.08, H * 0.08, -H * 0.16, H * 0.16, -H * 0.24, H * 0.24 }) do
		if SharedBehaviors.ColumnOpen(Owner, colX + dx, headStart, headTop) then
			found = colX + dx;
			break;
		end
	end
	plan.colX = found or colX;
	plan.blocked = found == nil;
	-- How high to rise: to the top; and when the landing is off to one side, until the feet clear the landing's floor, by as much as the
	-- landing's ceiling allows (a 48 px corridor leaves a 44 px body four pixels).
	plan.sideways = SceneMan:ShortestDistance(Vector(plan.colX, Top.Y), plan.landing, false).X;
	plan.riseToY = Top.Y;
	if math.abs(plan.sideways) >= 10 then
		local floorY = SharedBehaviors.FloorUnder(Owner, plan.landing + Vector(0, -4), H * 0.6) or (plan.landing.Y + feetBelowPos);
		local ceilY = nil;
		local Hit = Vector();
		if SceneMan:CastObstacleRay(Vector(plan.landing.X, floorY - 2), Vector(0, -H), Hit, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 2) >= 0 then
			ceilY = Hit.Y;
		end
		local room = ceilY and (floorY - ceilY - SharedBehaviors.StandingHeight(Owner) - 1) or 6;
		local clearance = math.max(1, math.min(6, room));
		plan.floorY = floorY;
		-- (The feet just over the landing's floor: the top point can sit higher than that, and held there the feet hung over the ledge too
		-- high for the look down to find its floor, and the step across never ended.)
		plan.riseToY = floorY - feetBelowPos - clearance;
	end
	-- A background ladder up this column (a piece of it beside the top and one near the foot): the climb is up the ladder, centred on it
	-- (the ladder takes hold of a body that covers the middle of its pieces). Only for an AHuman, the ladders' scripts take no other.
	if Owner.Head and Owner.ClassName == "AHuman" then
		local TopLadder = SharedBehaviors.LadderAt(Vector(Top.X, plan.riseToY), H * 0.4, H * 0.35);
		local FootLadder = SharedBehaviors.LadderAt(Owner.Pos, H * 0.4, H * 0.5);
		if TopLadder and FootLadder and math.abs(TopLadder.X - FootLadder.X) < 6 then
			plan.ladder = true;
			plan.colX = TopLadder.X;
			plan.blocked = false;
			plan.sideways = SceneMan:ShortestDistance(Vector(plan.colX, Top.Y), plan.landing, false).X;
		end
	end
	plan.startY = Owner.Pos.Y;
	plan.bestY = Owner.Pos.Y;
	return plan;
end

-- What the AI is doing, written into the unit's values for the engine's unit inspector and combat overlay (see Actor::GetDebugState and
-- the "Unit inspector" and "Combat AI overlay" options under AI debug): the behaviour, the climb stage, the target and whether it is in sight, the squad leader and slot, and the cover,
-- flank and retreat spots. Written only while an overlay wants this unit's (Owner.DebugExport) and only when a value changes, and taken
-- off again when the overlay stops wanting it, so normal play writes nothing.
function SharedBehaviors.ExportDebugState(AI, Owner)
	local Out = AI.debugExported;
	if not Owner.DebugExport then
		if Out then
			for key, value in pairs(Out) do
				if type(value) == "string" then
					Owner:RemoveStringValue(key);
				else
					Owner:RemoveNumberValue(key);
				end
			end
			AI.debugExported = nil;
		end
		AI.climbTick = nil;
		AI.holdRangeTick = nil;
		return;
	end
	if not Out then
		Out = {};
		AI.debugExported = Out;
	end
	local function Put(key, value)
		local old = Out[key];
		if old == value then
			return;
		end
		if old ~= nil and (value == nil or type(old) ~= type(value)) then
			if type(old) == "string" then
				Owner:RemoveStringValue(key);
			else
				Owner:RemoveNumberValue(key);
			end
		end
		Out[key] = value;
		if type(value) == "string" then
			Owner:SetStringValue(key, value);
		elseif value ~= nil then
			Owner:SetNumberValue(key, value);
		end
	end
	local function PutSpot(name, Spot)
		Put(name .. "X", Spot and math.floor(Spot.X) or nil);
		Put(name .. "Y", Spot and math.floor(Spot.Y) or nil);
	end
	Put("AI_Behavior", AI.BehaviorName);
	-- (ClimbUpdate leaves its plan in AI.climbTick each tick it runs; taken here, so a climb dropped anywhere stops showing.)
	Put("AI_ClimbStage", AI.climbTick and AI.climbTick.stage or nil);
	AI.climbTick = nil;
	local Target = AI.Target or AI.UnseenTarget;
	if Target and not MovableMan:ValidMO(Target) then
		Target = nil;
	end
	Put("AI_TargetID", Target and Target.UniqueID or nil);
	Put("AI_TargetSeen", Target and (AI.Target and 1 or 0) or nil);
	local inSquad = Owner.AIMode == Actor.AIMODE_SQUAD and AI.squadLeaderID ~= nil;
	Put("AI_SquadLeaderID", inSquad and AI.squadLeaderID or nil);
	Put("AI_SquadSlot", inSquad and AI.squadSlot or nil);
	PutSpot("AI_Cover", AI.Cover and AI.Cover.Spot);
	PutSpot("AI_Flank", AI.Flank and AI.Flank.Spot);
	PutSpot("AI_Retreat", AI.Retreat and AI.Retreat.Spot);
	-- For the combat overlay: why the unit took cover, how long each spot has been held (to a tenth of a second, so the value changes
	-- six times a second rather than every tick), and the range HoldRange keeps to (left in AI.holdRangeTick on each tick it runs).
	local function Tenths(Timer)
		return Timer and math.floor(Timer.ElapsedSimTimeMS / 100) * 100 or nil;
	end
	Put("AI_CoverWhy", AI.Cover and AI.Cover.Why or nil);
	Put("AI_CoverMs", AI.Cover and Tenths(AI.Cover.Timer));
	Put("AI_FlankMs", AI.Flank and Tenths(AI.Flank.Timer));
	Put("AI_RetreatMs", AI.Retreat and Tenths(AI.Retreat.WaitTimer));
	Put("AI_HoldRange", AI.holdRangeTick and math.floor(AI.holdRangeTick) or nil);
	AI.holdRangeTick = nil;
end

-- One tick of a climb. Sets the jet on AI. @return status ("run", "done" or "fail"), the lateral move, the aim angle, and a reason
-- when it failed.
function SharedBehaviors.ClimbUpdate(AI, Owner, plan)
	local H = Owner.Height;
	local ppm = GetPPM();
	local gravity = SceneMan.GlobalAcc.Y * ppm;
	local feetBelowPos = H * 0.2;
	local Pack = Owner.Jetpack;
	local isCrab = not Owner.Head;
	local lat = Actor.LAT_STILL;
	local aim = math.pi * 0.5;
	AI.jetLeanX = 0;
	AI.jetSteady = false;
	AI.climbTick = plan; -- For the unit inspector (see ExportDebugState).

	local function Trace(text)
		if Owner:IsAITracedOn("Climb") then
			ConsoleMan:PrintString("AITRACE climb: " .. text);
		end
	end
	local function Stage(name)
		plan.stage = name;
		plan.stageTimer:Reset();
		Trace(name .. " at " .. math.floor(Owner.Pos.X) .. "," .. math.floor(Owner.Pos.Y) .. " col " .. math.floor(plan.colX) .. " to y " .. math.floor(plan.riseToY) .. " fuel " .. math.floor(Pack.JetTimeLeft));
	end
	local function Fail(reason)
		AI.jump = false;
		AI.jetClimb = false;
		Trace("failed (" .. reason .. ") at " .. math.floor(Owner.Pos.X) .. "," .. math.floor(Owner.Pos.Y) .. " in " .. plan.stage);
		return "fail", Actor.LAT_STILL, 0, reason;
	end
	local function Toward(dx, deadband)
		if dx < -deadband then
			return Actor.LAT_LEFT;
		elseif dx > deadband then
			return Actor.LAT_RIGHT;
		end
		return Actor.LAT_STILL;
	end
	-- Sideways by speed: keys pressed when the speed is off what's wanted by more than a band (each press leans the nozzle).
	local function HoldSpeedX(wantVelX, band)
		local off = wantVelX - Owner.Vel.X;
		if off > band then
			return Actor.LAT_RIGHT;
		elseif off < -band then
			return Actor.LAT_LEFT;
		end
		return Actor.LAT_STILL;
	end
	-- Walking to a spot and stopping on it: full pace far off, a slow walk over the last 20 px, and the other key (a brake) when going
	-- towards it faster than that, or away from it.
	local function SettleX(dx)
		local wantVelX = 0;
		if math.abs(dx) > 20 then
			wantVelX = dx > 0 and 3 or -3;
		elseif math.abs(dx) > math.max(3, H * 0.05) then
			wantVelX = dx > 0 and 0.8 or -0.8;
		end
		return HoldSpeedX(wantVelX, 0.3);
	end
	local function GroundUnderFeet()
		return SceneMan:CastObstacleRay(Owner.Pos + Vector(0, feetBelowPos), Vector(0, H * 0.3), Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 2) >= 0;
	end

	if not Pack or Pack.JetpackType ~= AEJetpack.Standard then
		return Fail("no jetpack");
	end
	if plan.blocked then
		return Fail("no open column");
	end
	if plan.timer:IsPastSimMS(12000) then
		return Fail("took too long");
	end

	if plan.stage == "align" then
		AI.jump = false;
		AI.jetClimb = false;
		if AI.flying then
			-- Already in the air (passing a ledge on the way up from the last climb): straight into the rise, if the tank holds the climb from
			-- here. With no fuel it waits to land (and then refuels under the column as usual): straight into the rise on an empty tank, a unit
			-- that had just fallen out of a climb failed again at once, every frame.
			if SharedBehaviors.ClimbFuelLeft(AI, Owner, Owner.Pos.Y - plan.riseToY, Pack.JetTimeLeft) >= 450 then
				plan.startY = Owner.Pos.Y;
				plan.bestY = Owner.Pos.Y;
				plan.progressTimer = Timer();
				Stage("rise");
			end
		else
			-- Under the column and still before the jet is lit: walked there, slowing over the last 20 px, and braked to a stop. Lit while
			-- still walking, the walk speed carried the unit past the gap above, and it swung back and forth across it until a swing
			-- happened to line up.
			local dx = plan.colX - Owner.Pos.X;
			if math.abs(dx) <= math.max(3, H * 0.05) and math.abs(Owner.Vel.X) < 0.4 then
				Stage("fuel");
			elseif plan.stageTimer:IsPastSimMS(4000) then
				return Fail("couldn't get under the column");
			else
				lat = SettleX(dx);
				aim = 0;
			end
		end
	end

	if plan.stage == "fuel" and plan.ladder then
		plan.startY = Owner.Pos.Y;
		plan.bestY = Owner.Pos.Y;
		plan.progressTimer = Timer();
		Stage("ladder");
	end

	if plan.stage == "ladder" then
		-- Up the ladder: aim up and press up, no jet; the ladder's script does the moving. No rise for a second and a half (the ladder
		-- hasn't taken hold) and it is jetted instead.
		AI.jetClimb = true;
		AI.jump = false;
		AI.ladderUp = true;
		lat = Actor.LAT_STILL;
		aim = math.pi * 0.5;
		if Owner.Pos.Y < plan.bestY - 3 then
			plan.bestY = Owner.Pos.Y;
			plan.progressTimer:Reset();
		end
		if Owner.Pos.Y <= plan.riseToY then
			if math.abs(plan.sideways) < 10 then
				AI.ladderUp = false;
				AI.jetClimb = false;
				Stage("done");
			else
				plan.holdY = plan.riseToY;
				Stage("ladderStep");
			end
		elseif plan.progressTimer:IsPastSimMS(1500) then
			Trace("the ladder didn't take hold; jetting");
			AI.ladderUp = false;
			plan.ladder = false;
			Stage("fuel");
		end
	end

	if plan.stage == "ladderStep" then
		-- Off the top of the ladder onto the landing: aimed level and walked towards it (the ladder moves a unit along its aim while
		-- left or right is pressed), until there is floor under the feet.
		AI.jetClimb = true;
		AI.jump = false;
		local side = plan.sideways > 0 and 1 or -1;
		lat = side > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
		aim = 0;
		local across = (Owner.Pos.X - plan.colX) * side;
		if GroundUnderFeet() and across >= math.min(math.abs(plan.sideways), H * 0.25) then
			AI.jetClimb = false;
			Stage("done");
		elseif plan.stageTimer:IsPastSimMS(2500) then
			return Fail("couldn't step off the ladder");
		elseif Owner.Pos.Y < plan.riseToY - 2 then
			-- (Still on the ladder: up a little more while stepping, so the feet clear the landing's edge.)
			AI.ladderUp = false;
		else
			AI.ladderUp = true;
			aim = math.pi * 0.5;
			lat = Actor.LAT_STILL;
		end
	end

	if plan.stage == "fuel" then
		AI.jump = false;
		AI.jetClimb = false;
		local reserve = (math.abs(plan.sideways) >= 10 and not isCrab) and 450 or 300;
		local height = Owner.Pos.Y - plan.riseToY;
		local ok = SharedBehaviors.ClimbFuelLeft(AI, Owner, height, Pack.JetTimeLeft) >= reserve;
		if not ok and SharedBehaviors.ClimbFuelLeft(AI, Owner, height, Pack.JetTimeTotal) < reserve then
			-- No tank makes it by the reckoning: go on a full one.
			ok = Pack.JetTimeLeft >= Pack.JetTimeTotal * 0.95;
		end
		-- Still under the column while waiting, and still: no lift-off with any sideways speed left.
		local dx = plan.colX - Owner.Pos.X;
		lat = SettleX(dx);
		if lat ~= Actor.LAT_STILL then
			aim = 0;
		end
		if ok and math.abs(Owner.Vel.X) < 0.4 and math.abs(dx) <= math.max(3, H * 0.05) then
			plan.startY = Owner.Pos.Y;
			plan.bestY = Owner.Pos.Y;
			plan.progressTimer = Timer();
			Stage("rise");
		elseif plan.stageTimer:IsPastSimMS(250) and not plan.waitTraced then
			plan.waitTraced = true;
			Trace("waiting for fuel: " .. math.floor(Pack.JetTimeLeft) .. " of " .. math.floor(Pack.JetTimeTotal) .. " for " .. math.floor(height) .. " px");
		end
	end

	if plan.stage == "rise" then
		AI.jetClimb = true;
		plan.progressTimer = plan.progressTimer or Timer();
		-- Progress: a new best height resets the clock; none for 800 ms is a climb that isn't going anywhere.
		if Owner.Pos.Y < plan.bestY - 3 then
			plan.bestY = Owner.Pos.Y;
			plan.progressTimer:Reset();
		end
		if Pack.JetTimeLeft < TimerMan.AIDeltaTimeMS * 4 then
			return Fail("ran dry");
		end
		if Owner.Pos.Y > plan.startY + H * 0.3 then
			return Fail("fell back");
		end
		if plan.progressTimer:IsPastSimMS(800) then
			return Fail("no rise");
		end
		local toGo = Owner.Pos.Y - plan.riseToY; -- Positive while below the height.
		-- A background ladder alongside (see SharedBehaviors.LadderAt): it takes hold of a unit whenever the jet's key isn't held, and
		-- holds it still, so a climb that pulsed the jet was caught on every pulse. Beside one, the jet is held lit the whole way up and
		-- goes out only at the top or with the tank empty; at the top the ladder's hold is the hover, and the step off is along it.
		local besideLadder = not isCrab and SharedBehaviors.LadderAt(Owner.Pos, H * 0.3, H * 0.4) ~= nil;
		if toGo <= 0 and besideLadder and math.abs(plan.sideways) >= 10 then
			AI.jump = false;
			plan.holdY = plan.riseToY;
			Stage("ladderStep");
		elseif toGo <= 0 then
			if isCrab or math.abs(plan.sideways) < 10 then
				-- Straight up onto what's at the top (or a crab, which lands on legs either side): done at the height.
				AI.jump = false;
				AI.jetClimb = false;
				if math.abs(plan.sideways) >= 10 then
					lat = plan.sideways > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
					AI.jetLeanX = plan.sideways > 0 and 1 or -1;
				end
				Stage("done");
			else
				plan.holdY = plan.riseToY;
				Stage("step");
			end
		else
			-- The rate allowed is what gravity alone stops a few pixels short of the height, up to the cap: the tall part flown fast, the
			-- jet out where the coast just reaches the top.
			local rate = math.max(1, math.min(SharedBehaviors.ClimbSpeedCap(Owner), math.sqrt(2 * gravity * math.max(0, toGo - 8)) / ppm));
			AI.jump = Owner.Vel.Y > (AI.jump and -rate - 1 or -rate + 1);
			-- (Every relight a burst for the first 400 ms cost 130 ms of tank a time, which the fuel reckoning counts once: one burst to
			-- leave the ground, then a steady jet.)
			AI.jetSteady = AI.flying or plan.stageTimer:IsPastSimMS(150);
			if besideLadder then
				AI.jump = true; -- (Held lit: see above.)
			end
			-- Kept to the column's middle, tightly: the walk speed carried in takes a unit off a hatch's column into the slab beside it.
			local dx = plan.colX - Owner.Pos.X;
			if isCrab then
				if math.abs(dx) > 6 then
					AI.jetLeanX = dx > 0 and 1 or -1;
				end
				lat = Toward(dx, 3);
			else
				-- (Gently, so a correction eases in rather than swinging past the middle.)
				lat = HoldSpeedX(math.max(-1.5, math.min(1.5, dx / 8)), 0.3);
				aim = lat == Actor.LAT_STILL and math.pi * 0.5 or 0;
			end
		end
	end

	if plan.stage == "step" and not isCrab and SharedBehaviors.LadderAt(Owner.Pos, H * 0.3, H * 0.4) then
		Stage("ladderStep");
	end

	if plan.stage == "step" then
		AI.jetClimb = true;
		if Pack.JetTimeLeft < TimerMan.AIDeltaTimeMS * 4 then
			return Fail("ran dry at the top");
		end
		if plan.stageTimer:IsPastSimMS(2500) then
			return Fail("couldn't step across");
		end
		if Owner.Pos.Y > plan.holdY + H * 0.4 then
			return Fail("dropped from the top");
		end
		local side = plan.sideways > 0 and 1 or -1;
		-- The way across at foot and shin height: blocked means the feet don't clear yet, so hold a little higher.
		local reach = Vector(side * (math.abs(plan.sideways) + H * 0.15), 0);
		local feetClear = SceneMan:CastObstacleRay(Owner.Pos + Vector(0, feetBelowPos - 2), reach, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) < 0 and SceneMan:CastObstacleRay(Owner.Pos + Vector(0, H * 0.1), reach, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) < 0;
		if not feetClear then
			plan.holdY = math.max(plan.top.Y - H * 0.3, plan.holdY - 1);
		end
		local across = (Owner.Pos.X - plan.colX) * side; -- How far across towards the landing.
		if GroundUnderFeet() and across >= math.min(math.abs(plan.sideways), H * 0.25) then
			AI.jump = false;
			AI.jetClimb = false;
			lat = side > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
			Stage("done");
		else
			-- Hold the height: lit when below it or sinking.
			local err = Owner.Pos.Y - plan.holdY; -- Positive when below.
			AI.jump = (err > 0 and Owner.Vel.Y > -1.5) or (err > -6 and Owner.Vel.Y > 1);
			AI.jetSteady = true;
			if feetClear then
				lat = HoldSpeedX(side * 2.5, 0.5);
				aim = lat == Actor.LAT_STILL and math.pi * 0.5 or 0;
			else
				lat = HoldSpeedX(math.max(-1, math.min(1, (plan.colX - Owner.Pos.X) / 6)), 0.4);
			end
		end
	end

	if plan.stage == "done" then
		AI.jetClimb = false;
		return "done", lat, aim;
	end
	return "run", lat, aim;
end

-- Fighting on the move, cover, flanking and falling back: the rules shared by the human and crab AIs. They read the unit's standing order
-- from its AI mode and the tags the sandbox's command tool leaves on it, so a unit sent somewhere by the game's own waypoint order and
-- one sent by the sandbox fight the same way.

-- What the unit has been told to do, as the fighting rules read it: "move" (get there; shoot back on the way but don't stop for it),
-- "attack" (fight whatever is met, closing in), "defend" (stand this ground, move as little as can be) or "guard" (the sentry, patrol
-- and gold-digging modes: stop and fight what turns up, and chase it as the game's AI always has).
function SharedBehaviors.OrderKind(Owner)
	-- (A defender on its way back to its post is moving, not standing its ground: told "defend" while the mode said "go there", the
	-- fighting rules held it still wherever it had been shoved to.)
	if Owner:NumberValueExists("SandboxDefendX") then
		return Owner.AIMode == Actor.AIMODE_GOTO and "move" or "defend";
	end
	if Owner:NumberValueExists("AIRetreat") then
		return "move";
	end
	if Owner:GetNumberValue("SandboxAttack") > 0 or Owner.AIMode == Actor.AIMODE_BRAINHUNT then
		return "attack";
	end
	if Owner.AIMode == Actor.AIMODE_GOTO or Owner.AIMode == Actor.AIMODE_SQUAD then
		return "move";
	end
	return "guard";
end

-- Whether a unit with a target in sight keeps going for its waypoint: on a move order, or when its script marks it aggressive (the Ronin
-- do when hurt), or when it's closing in on a target it can't hit from here.
function SharedBehaviors.FightsOnTheMove(AI, Owner)
	if Owner.aggressive then
		return true;
	end
	local kind = SharedBehaviors.OrderKind(Owner);
	if kind == "move" then
		return true;
	end
	return AI.closingIn == true and kind ~= "defend";
end

-- Whether a unit may leave its spot to go after a target it can't hit from where it is.
function SharedBehaviors.MayClose(AI, Owner)
	local kind = SharedBehaviors.OrderKind(Owner);
	if kind == "attack" then
		return true;
	elseif kind == "guard" then
		return not AI.isPlayerOwned or Owner.AIMode ~= Actor.AIMODE_SENTRY;
	end
	return false;
end

-- The standing body, in pixels, from the height: the feet are a fifth of the height under Pos and the top of the head about a quarter
-- over it, so a Soldier Light (height 100, its head 24 px over Pos by its sprites) stands 44 px tall and walks a 48 px tunnel with room
-- to spare. The path grid takes its standing room from the same fraction (AHuman::GetPathAgent), so the two never disagree about a
-- corridor: when they did, the grid routed a walk through the 48 px corridors of every bunker and this script, probing 55 px over the
-- floor for the head, went prone in them and crawled at five pixels a second.
function SharedBehaviors.StandingHeight(Owner)
	return Owner.Height * 0.44;
end

-- How far the top of the head is over Pos when standing.
function SharedBehaviors.HeadAbovePos(Owner)
	return Owner.Height * 0.24;
end

-- The middle of a shaft a point is in: walls within reach both ways at that height. @return The middle's x, or nil in the open.
function SharedBehaviors.ShaftMiddle(Owner, x, y, reach)
	local Left = Vector();
	local Right = Vector();
	local leftHit = SceneMan:CastObstacleRay(Vector(x, y), Vector(-reach, 0), Left, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 2) >= 0;
	local rightHit = SceneMan:CastObstacleRay(Vector(x, y), Vector(reach, 0), Right, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 2) >= 0;
	if leftHit and rightHit then
		return (Left.X + Right.X) * 0.5, Right.X - Left.X;
	end
	return nil;
end

-- An open column near the unit for a climb of a given height: the nearest sideways offset (within half a body either way) from which
-- the way up is clear. @param Up The climb, as a vector. @return The offset, or nil.
function SharedBehaviors.OpenColumnNear(Owner, Up)
	for _, dx in ipairs({ 0, -Owner.Height * 0.25, Owner.Height * 0.25, -Owner.Height * 0.5, Owner.Height * 0.5 }) do
		if SceneMan:CastObstacleRay(Owner.Pos + Vector(dx, -SharedBehaviors.HeadAbovePos(Owner) + 2), Up, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) < 0 then
			return dx;
		end
	end
	return nil;
end

-- A traced unit's line in the console. channel is the debug channel it belongs to ("Combat" unless said, or "Squad", "AI", ...): ticked in the settings, or switched on by CCCP_AI_LOG.
function SharedBehaviors.Trace(Owner, text, channel)
	if Owner:IsAITracedOn(channel or "Combat") then
		ConsoleMan:PrintString("AITRACE [" .. Owner.PresetName .. " " .. Owner.Team .. " at " .. math.floor(Owner.Pos.X) .. "," .. math.floor(Owner.Pos.Y) .. "] " .. text);
	end
end

-- A door of ours (or no one's) whose moving part is at a point, within a body of it: the door a ray that stopped on door material
-- ran into. Such a door opens for us as we come, so to the move it is no wall and no ceiling, whatever its leaf is doing now.
-- @return The door (an ADoor), or nil.
function SharedBehaviors.OurDoorAt(Owner, Point)
	if SceneMan:GetTerrMatter(Point.X, Point.Y) ~= rte.doorID then
		return nil;
	end
	for mo in MovableMan:GetMOsInRadius(Point, Owner.Height * 0.6) do
		if mo.ClassName == "ADoor" and (mo.Team == Owner.Team or mo.Team == Activity.NOTEAM) then
			return ToADoor(mo);
		end
	end
	return nil;
end

-- A door of ours (or no one's) whose moving part lies on the way to a point, within a body and a half of us. The pather sees a team's
-- own doors as open, since they open for its units, so the route says nothing about them; the door itself does, through its state.
-- @return The door (an ADoor), or nil.
function SharedBehaviors.DoorAhead(Owner, ToPos)
	local best = nil;
	local bestDist = math.huge;
	local Along = SceneMan:ShortestDistance(Owner.Pos, ToPos, false);
	local length = Along.Magnitude;
	for mo in MovableMan:GetMOsInRadius(Owner.Pos, Owner.Height * 1.5) do
		if mo.ClassName == "ADoor" and (mo.Team == Owner.Team or mo.Team == Activity.NOTEAM) then
			local door = ToADoor(mo);
			local Leaf = door.Door;
			if Leaf and Leaf:IsAttached() then
				-- How far the leaf is from the line from us to the point.
				local ToLeaf = SceneMan:ShortestDistance(Owner.Pos, Leaf.Pos, false);
				local t = length > 1 and math.max(0, math.min(1, ToLeaf:Dot(Along) / (length * length))) or 0;
				local d = (ToLeaf - Along * t).Magnitude;
				if d < Owner.Height * 0.5 and d < bestDist then
					best = door;
					bestDist = d;
				end
			end
		end
	end
	return best;
end

-- Squads: a follower keeps a place in line behind its leader, so far back along the way the leader came. The leader's route isn't
-- readable (a player's leader has none, and a unit's is popped as it is walked), so each follower keeps its own record of where the
-- leader has walked and measures back along that. The place is what the follower's route ends at and what it holds at; the leader
-- itself is only where the route is asked for to (there is sure to be a way to where the leader stands).

-- Where the leader has walked: ground points, newest first, one for every 16 px it moves on the ground, up to 64 of them. A leader in
-- the air leaves no point (the trail is where feet have been), and a new leader starts a new trail.
-- @return The leader's present ground point, or nil while it is in the air.
function SharedBehaviors.SquadTrailUpdate(AI, Owner, Leader)
	if AI.squadLeaderID ~= Leader.UniqueID or not AI.squadTrail then
		AI.squadTrail = {};
		AI.squadLeaderID = Leader.UniqueID;
	end
	local trail = AI.squadTrail;
	-- On the ground: something under the feet within a third of its height.
	if not SceneMan:CastStrengthRay(Leader.Pos, Vector(0, Leader.Height * 0.35), 5, Vector(), 2, rte.grassID, true) then
		return nil;
	end
	local Ground = SceneMan:MovePointToGround(Leader.Pos, Leader.Height * 0.2, 4);
	if #trail == 0 or SceneMan:ShortestDistance(trail[1], Ground, false):MagnitudeIsGreaterThan(16) then
		table.insert(trail, 1, Ground);
		if #trail > 64 then
			table.remove(trail);
		end
	end
	return Ground;
end

-- The follower's place in line, 1 for the nearest: its rank by UniqueID among the leader's followers, which is the same on every tick
-- and every machine. Looked up once a second.
function SharedBehaviors.SquadSlot(AI, Owner, Leader)
	if AI.squadSlot and AI.squadSlotTimer and not AI.squadSlotTimer:IsPastSimTimeLimit() then
		return AI.squadSlot;
	end
	if not AI.squadSlotTimer then
		AI.squadSlotTimer = Timer();
		AI.squadSlotTimer:SetSimTimeLimitMS(1000);
	end
	AI.squadSlotTimer:Reset();
	local ids = {};
	for actor in MovableMan.Actors do
		-- (GetAIMOWaypointID checks its pointer; another actor's MOMoveTarget may still point at an MO deleted last frame. A dying
		-- unit gives up its place.)
		if actor.AIMode == Actor.AIMODE_SQUAD and actor.Team == Owner.Team and actor.Status < Actor.DYING and actor:GetAIMOWaypointID() == Leader.ID then
			table.insert(ids, actor.UniqueID);
		end
	end
	table.sort(ids);
	AI.squadSlot = 1;
	for i, id in ipairs(ids) do
		if id == Owner.UniqueID then
			AI.squadSlot = i;
			break;
		end
	end
	return AI.squadSlot;
end

-- The place a follower keeps to: its slot's distance back along the leader's trail (a slot is about a third of the two heights, 70 px
-- for two soldiers), on the ground. A leap or a flight between two trail points is not walked along; the place sits at its near end
-- until the leader walks on. When the trail runs out short of the place (the leader has only just set off, or stands where it was
-- put), the place is on past the trail's oldest point the way the leader came from, or, with no direction yet, beside the leader on
-- the follower's side; where that ground is open and near enough level; else the trail's end. (The trail's end alone was the leader's
-- own spot for every follower of a standing leader, and they all converged on it.)
-- @return The place, and the leader's ground point.
function SharedBehaviors.SquadPoint(AI, Owner, Leader)
	local Ground = SharedBehaviors.SquadTrailUpdate(AI, Owner, Leader);
	local trail = AI.squadTrail;
	local gap = (Leader.Height + Owner.Height) * 0.35;
	local back = gap * SharedBehaviors.SquadSlot(AI, Owner, Leader);
	local LeaderGround = Ground or trail[1] or Leader.Pos;
	local From = LeaderGround;
	local LastSeg = nil;
	for i = 1, #trail do
		local To = trail[i];
		local Seg = SceneMan:ShortestDistance(From, To, false);
		local length = Seg.Magnitude;
		if length > Leader.Height + Owner.Height then
			return From, LeaderGround;
		end
		if length >= back then
			return SceneMan:MovePointToGround(From + Seg * (back / math.max(1, length)), Owner.Height * 0.2, 4), LeaderGround;
		end
		back = back - length;
		From = To;
		if length > 1 then
			LastSeg = Seg;
		end
	end
	local Dir = LastSeg and LastSeg.Normalized or Vector(SceneMan:ShortestDistance(Leader.Pos, Owner.Pos, false).X < 0 and -1 or 1, 0);
	local Beyond = SceneMan:MovePointToGround(From + Dir * back + Vector(0, -Owner.Height * 0.2), Owner.Height * 0.2, 4);
	if SceneMan:GetTerrMatter(Beyond.X, Beyond.Y) == rte.airID and math.abs(Beyond.Y - From.Y) < Owner.Height * 0.5 then
		return Beyond, LeaderGround;
	end
	return From, LeaderGround;
end

-- The "Squad links and trails" overlay (AI debug in the settings panel): for a follower or leader being inspected, a line from the leader
-- to the follower, the trail of ground points the follower measures back along, and its place in line as a ring with the slot number.
function SharedBehaviors.DrawSquadDebug(AI, Owner, Leader, Point)
	if not SettingsMan.ShowSquadLinks or not (Owner.IsInspected or Leader.IsInspected) then
		return;
	end
	PrimitiveMan:DrawLinePrimitive(Leader.Pos, Owner.Pos, 147);
	local trail = AI.squadTrail;
	if trail then
		for i = 1, #trail do
			PrimitiveMan:DrawCirclePrimitive(trail[i], 1, 117);
			if i > 1 then
				PrimitiveMan:DrawLinePrimitive(trail[i - 1], trail[i], 117);
			end
		end
	end
	if Point then
		PrimitiveMan:DrawCirclePrimitive(Point, 5, 254);
		PrimitiveMan:DrawLinePrimitive(Owner.Pos, Point, 254);
		PrimitiveMan:DrawTextPrimitive(Point + Vector(0, -14), tostring(AI.squadSlot or "?"), true, 1);
	end
end

-- The follower's route ends at its place, not at the leader: the route is asked for to the leader, and here the nodes beyond the place
-- (nearer the leader than the place is) come off its end and the place goes on. Every tick, so the end follows the place as it moves.
function SharedBehaviors.SquadTrimPath(Owner, Point, LeaderGround)
	local placeToLeader = SceneMan:ShortestDistance(Point, LeaderGround, false).Magnitude;
	while Owner.MovePathSize >= 2 do
		-- The last real node: the one before the point put on last tick.
		local count = 0;
		local Node = nil;
		for pos in Owner.MovePath do
			count = count + 1;
			if count == Owner.MovePathSize - 1 then
				Node = pos;
			end
		end
		if not Node or SceneMan:ShortestDistance(Node, LeaderGround, false).Magnitude >= placeToLeader - 12 then
			break;
		end
		Owner:RemoveMovePathEnd();
		Owner:RemoveMovePathEnd();
		Owner:AddToMovePathEnd(Point);
	end
	Owner:RemoveMovePathEnd();
	Owner:AddToMovePathEnd(Point);
end

-- Whether the unit stands in the way of a door of ours (or no one's) that isn't shut: where its moving piece will be when it closes,
-- which it does a second and a half after its sensors last saw a body, on whatever is there.
function SharedBehaviors.InDoorSweep(Owner)
	return SharedBehaviors.DoorSweptBy(Owner, Owner.Pos) ~= nil;
end

-- The open door of the unit's side whose piece sweeps a place (the unit's own, or one it means to stand on), or nil.
function SharedBehaviors.DoorSweptBy(Owner, Pos)
	for mo in MovableMan:GetMOsInRadius(Pos, Owner.Height * 1.2) do
		if mo.ClassName == "ADoor" and (mo.Team == Owner.Team or mo.Team == Activity.NOTEAM) then
			local door = ToADoor(mo);
			if door.Door and door:GetDoorState() ~= ADoor.CLOSED and door:SweepContains(Pos, Owner.Height * 0.3) then
				return door;
			end
		end
	end
	return nil;
end

-- Standing in a door's sweep: the nearest spot on this floor clear of it, the goal's side first, onto ground. nil when not in a sweep,
-- or no such spot is near. (A squad's place in the sweep was let go of, walked to again, and held again: the follower stood in the door
-- for good.)
function SharedBehaviors.DoorSweepExit(Owner, Goal)
	local door = SharedBehaviors.DoorSweptBy(Owner, Owner.Pos);
	if not door then
		return nil;
	end
	local H = Owner.Height;
	local first = SceneMan:ShortestDistance(Owner.Pos, Goal, false).X < 0 and -1 or 1;
	for _, reach in ipairs({0.5, 0.8, 1.2}) do
		for _, side in ipairs({first, -first}) do
			local Spot = Owner.Pos + Vector(side * H * reach, 0);
			if not door:SweepContains(Spot, H * 0.3) and SharedBehaviors.StepIsSafe(Owner, side) then
				return Spot;
			end
		end
	end
	return nil;
end

-- Whether a step sideways from here is onto ground, not off a drop or into a wall. @param dir -1 or 1.
function SharedBehaviors.StepIsSafe(Owner, dir)
	local reach = Owner.Height * 0.4;
	local Step = Vector(dir * reach, 0);
	if SceneMan:CastObstacleRay(Owner.Pos, Step, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) >= 0 then
		return false; -- A wall.
	end
	local Foot = Owner.Pos + Step;
	return SceneMan:CastObstacleRay(Foot, Vector(0, Owner.Height * 0.8), Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 4) >= 0; -- Ground within reach of the feet.
end

-- Whether a point can be seen from an eye: nothing much in the way for the ray. (The same measure the shooting rules use for a shot.)
function SharedBehaviors.CanSee(EyePos, Point)
	return SceneMan:CastStrengthSumRay(EyePos, Point, 6, rte.grassID) < 120;
end

-- A spot near the unit, on the ground, that can't be seen from a point: cover to reload or recover behind. Looked for a step at a time
-- out to reach either side, nearest first, along ground a walk away (no climb or drop of more than half a body, no wall between).
-- @return The spot, or nil.
function SharedBehaviors.FindCover(Owner, FromPos, reach)
	local eyeUp = Owner.Height * 0.3;
	if not SharedBehaviors.CanSee(Owner.Pos + Vector(0, -eyeUp), FromPos) then
		return nil; -- Already out of its sight: nowhere better to be.
	end
	for step = 1, math.floor(reach / 8) do
		for _, dir in ipairs({1, -1}) do
			local Spot = Owner.Pos + Vector(dir * step * 8, -Owner.Height * 0.2);
			Spot = SceneMan:MovePointToGround(Spot, math.floor(Owner.Height * 0.2), 4);
			local Way = SceneMan:ShortestDistance(Owner.Pos, Spot, false);
			if math.abs(Way.Y) < Owner.Height * 0.5 and SceneMan:CastObstacleRay(Owner.Pos, Way, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) < 0 then
				if not SharedBehaviors.CanSee(Spot + Vector(0, -eyeUp), FromPos) then
					return Spot;
				end
			end
		end
	end
	return nil;
end

-- A place from which a dug-in target can be shot: above it or to one side, with a line of sight to it, that the pather can reach in
-- not too many nodes. @param range How far this unit's weapon reaches. @return The spot, or nil.
function SharedBehaviors.FindFlank(AI, Owner, TargetPos, range)
	local stand = math.max(150, math.min(400, range * 0.6));
	local candidates = {};
	for _, angle in ipairs({60, 90, 120, 40, 140}) do -- Degrees up from the target's right, over the top.
		local rad = math.rad(angle);
		table.insert(candidates, TargetPos + Vector(math.cos(rad) * stand, -math.sin(rad) * stand));
	end
	table.insert(candidates, TargetPos + Vector(stand, -Owner.Height));
	table.insert(candidates, TargetPos + Vector(-stand, -Owner.Height));
	local best, bestCost = nil, 50; -- The cap: a flank worth fifty nodes is a walk across the map.
	for _, Spot in ipairs(candidates) do
		Spot = SceneMan:MovePointToGround(Spot, math.floor(Owner.Height * 0.2), 6);
		-- Somewhere else (a flank of ten pixels was the same spot with the same problem), seen from about where the gun would be held.
		if SceneMan:GetTerrMatter(Spot.X, Spot.Y) == rte.airID and SceneMan:ShortestDistance(Owner.Pos, Spot, false):MagnitudeIsGreaterThan(Owner.Height * 1.5) and SharedBehaviors.CanSee(Spot + Vector(0, -Owner.Height * 0.1), TargetPos) then
			local Dist = SceneMan:ShortestDistance(Spot, TargetPos, false);
			if Dist:MagnitudeIsGreaterThan(stand * 0.5) and Dist:MagnitudeIsLessThan(range) then
				local cost = SceneMan.Scene:CalculatePath(Owner.Pos, Spot, Owner.JumpHeight, 35, Owner.Team);
				if cost > 1 and cost < bestCost then
					best, bestCost = Spot, cost;
				end
			end
		end
	end
	return best, bestCost;
end

-- Keeps a unit's standing order so it can be put back after a flank or a retreat.
function SharedBehaviors.RememberOrder(AI, Owner)
	local keep = { mode = Owner.AIMode, attack = Owner:GetNumberValue("SandboxAttack") };
	-- (A squad follower's leader too: cleared with the waypoints, a follower came back from a fall-back with no one to follow.)
	if Owner.AIMode == Actor.AIMODE_GOTO or Owner.AIMode == Actor.AIMODE_SQUAD then
		if Owner.MOMoveTarget and MovableMan:ValidMO(Owner.MOMoveTarget) then
			keep.target = Owner.MOMoveTarget;
		elseif Owner:GetWaypointListSize() > 0 then
			keep.waypoint = Owner:GetLastAIWaypoint();
		end
	elseif Owner.AIMode == Actor.AIMODE_SENTRY then
		-- (A sentry's post and the way it faced: kept as the mode only, a sentry came back from a fall-back and stood guard wherever the
		-- fall-back had ended. A crab keeps no post of its own; where it stood is its post.)
		local Post = AI.SentryPos or Owner.Pos;
		keep.post = Vector(Post.X, Post.Y);
		keep.facing = AI.SentryFacing;
	end
	return keep;
end

function SharedBehaviors.RestoreOrder(AI, Owner, keep)
	Owner:ClearAIWaypoints();
	if keep.mode == Actor.AIMODE_SQUAD then
		if keep.target and MovableMan:ValidMO(keep.target) then
			Owner:AddAIMOWaypoint(keep.target);
		else
			keep.mode = Actor.AIMODE_SENTRY;
		end
	elseif keep.mode == Actor.AIMODE_GOTO then
		if keep.target and MovableMan:ValidMO(keep.target) then
			Owner:AddAIMOWaypoint(keep.target);
		elseif keep.waypoint then
			Owner:AddAISceneWaypoint(keep.waypoint);
		else
			keep.mode = Actor.AIMODE_SENTRY;
		end
	elseif keep.mode == Actor.AIMODE_SENTRY and keep.post then
		-- Back on guard at the post: walked back to it when away (the engine makes the unit a sentry again on arrival, and the AI's update
		-- takes the post and facing from AI.ReturnPost then), else simply on guard there again.
		AI.SentryPos = Vector(keep.post.X, keep.post.Y);
		AI.SentryFacing = keep.facing;
		AI.ReturnPost = { Pos = keep.post, facing = keep.facing };
		if SceneMan:ShortestDistance(Owner.Pos, keep.post, false):MagnitudeIsGreaterThan(Owner.Height * 0.7) then
			Owner:AddAISceneWaypoint(keep.post);
			keep.mode = Actor.AIMODE_GOTO;
		end
	end
	Owner.AIMode = keep.mode;
	if keep.attack > 0 then
		Owner:SetNumberValue("SandboxAttack", keep.attack);
	end
end

-- Whether the walk of a fall-back is over (got there, or stood down with no route): nothing left of it, the waypoints, the path or
-- one being worked out. (Not "no GoTo behaviour": that is swapped in a tick or two after the order, and crabs never set it, so a fall-back
-- was "arrived" the tick after it began and its wait ran down wherever the unit was.) A second's grace first, for the order to be taken up.
function SharedBehaviors.RetreatWalkOver(AI, Owner)
	return AI.Retreat.WaitTimer:IsPastSimMS(1000) and Owner:GetWaypointListSize() == 0 and Owner.MovePathSize == 0 and not Owner.IsWaitingOnNewMovePath;
end

-- Whether the unit has been given another order since a fall-back or a flank sent it to a spot: another mode, or a waypoint queued last
-- that isn't the spot. (Put back unconditionally, a pie-menu order given meanwhile was wiped up to 25 s later.) Not something to follow:
-- the AI's own detours (to a weapon to pick up, closing on a target) set that and queue the spot again after it.
function SharedBehaviors.OrderChangedSince(Owner, Spot)
	if Owner.AIMode ~= Actor.AIMODE_GOTO then
		-- (Sentry at the spot with nothing more to go to is the walk's own end: the engine puts a unit whose GOTO is used up into
		-- SENTRY once within 20 px of its last point. Any other mode, or sentry elsewhere, is someone's order.)
		local arrived = Owner.AIMode == Actor.AIMODE_SENTRY and Owner:GetWaypointListSize() == 0 and not SceneMan:ShortestDistance(Owner.Pos, Spot, false):MagnitudeIsGreaterThan(Owner.Height);
		return not arrived;
	end
	return Owner:GetWaypointListSize() > 0 and SceneMan:ShortestDistance(Owner:GetLastAIWaypoint(), Spot, false):MagnitudeIsGreaterThan(48);
end

-- Falling back: a badly hurt unit with no enemy in sight goes to the nearest friend (the brain for choice) and waits a while to be
-- patched up, then takes its order up again whether or not it was. Not a brain, not a defender, not a sentry a player posted.
-- Called every tick by the AI's update. @return Whether the unit is falling back.
function SharedBehaviors.RetreatUpdate(AI, Owner)
	-- The tag taken off by someone else (a sandbox order): the fall-back is over and the order it would have put back is gone too.
	if AI.Retreat and not Owner:NumberValueExists("AIRetreat") then
		SharedBehaviors.Trace(Owner, "retreat: called off by a new order");
		AI.Retreat = nil;
		return false;
	end
	-- Another order given meanwhile: the fall-back is over, and that order stands.
	if AI.Retreat and SharedBehaviors.OrderChangedSince(Owner, AI.Retreat.Spot) then
		SharedBehaviors.Trace(Owner, "retreat: called off by another order");
		Owner:RemoveNumberValue("AIRetreat");
		AI.Retreat = nil;
		return false;
	end
	if AI.Retreat then
		local done = false;
		local _, morale = SharedBehaviors.Suppression(AI, Owner);
		if Owner.Health >= Owner.MaxHealth * 0.6 and morale >= 0.5 then
			done = true; -- Patched up, and steady again.
		elseif AI.Retreat.Arrived and AI.Retreat.WaitTimer:IsPastSimMS(25000) then
			done = true; -- Nobody came; back to it.
		elseif not AI.Retreat.Arrived and AI.Retreat.WaitTimer:IsPastSimMS(40000) then
			done = true; -- Never got there.
		elseif not AI.Retreat.Arrived and (SceneMan:ShortestDistance(Owner.Pos, AI.Retreat.Spot, false):MagnitudeIsLessThan(100) or SharedBehaviors.RetreatWalkOver(AI, Owner)) then
			AI.Retreat.Arrived = true; -- The walk is over.
			AI.Retreat.WaitTimer:Reset();
		end
		if done then
			SharedBehaviors.Trace(Owner, "retreat: over, health " .. math.floor(Owner.Health));
			Owner:RemoveNumberValue("AIRetreat");
			SharedBehaviors.RestoreOrder(AI, Owner, AI.Retreat.Keep);
			AI.Retreat = nil;
			return false;
		end
		return true;
	end
	-- (Hurt, with no enemy about; or shaken (morale under 0.3), which pulls a unit back whatever its health and in the middle of a fight.)
	local shaken = SharedBehaviors.Shaken(AI, Owner);
	if (not shaken and (Owner.Health >= Owner.MaxHealth * 0.3 or AI.Target or AI.UnseenTarget)) or Owner:IsPlayerControlled() or Owner:HasObjectInGroup("Brains") then
		return false;
	end
	local kind = SharedBehaviors.OrderKind(Owner);
	-- (A defender on its way back to its post too, which OrderKind calls a move: the sandbox sends it back to its post whatever it does.)
	if kind == "defend" or Owner:NumberValueExists("SandboxDefendX") or (AI.isPlayerOwned and Owner.AIMode == Actor.AIMODE_SENTRY) or Owner:NumberValueExists("SandboxHold") or Owner:NumberValueExists("AIFlank") then
		return false;
	end
	if shaken then
		AI.RetreatCheckTimer = nil; -- (No waiting to be clear of the enemy: it is the enemy it is getting away from.)
	elseif not AI.RetreatCheckTimer then
		AI.RetreatCheckTimer = Timer();
	elseif not AI.RetreatCheckTimer:IsPastSimMS(2000) then
		return false; -- Two seconds clear of enemies first.
	end
	-- Somewhere to go: the brain, or the nearest friend that isn't right here; but never through the enemy last seen. With no friend
	-- the right side of it, it's a way back from the enemy along the ground.
	local Friend = MovableMan:GetClosestBrainActor(Owner.Team, Owner.Pos);
	if not Friend or Friend.ID == Owner.ID then
		Friend = MovableMan:GetClosestTeamActor(Owner.Team, Activity.PLAYER_NONE, Owner.Pos, 3000, Vector(), Owner);
	end
	if Friend and (Friend.ID == Owner.ID or SceneMan:ShortestDistance(Owner.Pos, Friend.Pos, false):MagnitudeIsLessThan(150)) then
		Friend = nil;
	end
	local Spot;
	local enemyDx = AI.LastEnemyPos and SceneMan:ShortestDistance(Owner.Pos, AI.LastEnemyPos, false).X or 0;
	if Friend then
		local friendDx = SceneMan:ShortestDistance(Owner.Pos, Friend.Pos, false).X;
		if enemyDx * friendDx > 0 and math.abs(friendDx) > math.abs(enemyDx) - 100 then
			Friend = nil; -- The friend is past the enemy.
		else
			Spot = SceneMan:MovePointToGround(Friend.Pos + Vector(math.random(-60, 60), 0), math.floor(Owner.Height * 0.2), 4);
		end
	end
	if not Spot then
		if enemyDx == 0 then
			return false;
		end
		Spot = SceneMan:MovePointToGround(Owner.Pos + Vector(enemyDx > 0 and -350 or 350, -Owner.Height * 0.3), math.floor(Owner.Height * 0.2), 4);
		if SceneMan:GetTerrMatter(Spot.X, Spot.Y) ~= rte.airID then
			return false;
		end
	end
	AI.Retreat = { Keep = SharedBehaviors.RememberOrder(AI, Owner), WaitTimer = Timer(), Arrived = false, Spot = Spot };
	Owner:SetNumberValue("AIRetreat", 1);
	Owner:RemoveNumberValue("SandboxAttack");
	Owner:ClearAIWaypoints();
	Owner:AddAISceneWaypoint(Spot);
	Owner.AIMode = Actor.AIMODE_GOTO;
	SharedBehaviors.Trace(Owner, "retreat: health " .. math.floor(Owner.Health) .. ", falling back to " .. (Friend and Friend.PresetName or "away from the enemy") .. " at " .. math.floor(Spot.X) .. "," .. math.floor(Spot.Y));
	return true;
end

-- A flank once started is seen through: when the unit gets there (or gives up), its order is put back and it looks for the target again.
-- Called every tick by the AI's update.
function SharedBehaviors.FlankUpdate(AI, Owner)
	if not AI.Flank then
		return;
	end
	-- (Likewise a flank called off by a new order: not put back.)
	if not Owner:NumberValueExists("AIFlank") then
		SharedBehaviors.Trace(Owner, "flank: called off by a new order");
		AI.Flank = nil;
		AI.FlankRestTimer = Timer();
		return;
	end
	if SharedBehaviors.OrderChangedSince(Owner, AI.Flank.Spot) then
		SharedBehaviors.Trace(Owner, "flank: called off by another order");
		Owner:RemoveNumberValue("AIFlank");
		AI.Flank = nil;
		AI.FlankRestTimer = Timer();
		return;
	end
	local arrived = SceneMan:ShortestDistance(Owner.Pos, AI.Flank.Spot, false):MagnitudeIsLessThan(Owner.Height * 0.5);
	if arrived or AI.Flank.Timer:IsPastSimMS(15000) then
		SharedBehaviors.Trace(Owner, "flank: " .. (arrived and "there" or "gave up"));
		Owner:RemoveNumberValue("AIFlank");
		SharedBehaviors.RestoreOrder(AI, Owner, AI.Flank.Keep);
		AI.Flank = nil;
		AI.FlankRestTimer = Timer();
	end
end

-- Starts a flank towards a spot with a line of sight to a target that can't be shot from here. @return Whether one was started.
function SharedBehaviors.StartFlank(AI, Owner, TargetPos, range)
	if AI.Flank or AI.Retreat or not SharedBehaviors.MayClose(AI, Owner) or AI.skill < 40 then
		return false;
	end
	if AI.FlankRestTimer and not AI.FlankRestTimer:IsPastSimMS(8000) then
		return false;
	end
	AI.FlankRestTimer = Timer();
	if math.random() * 100 > AI.skill then
		return false; -- The better the AI, the more often it thinks of it.
	end
	local Spot, cost = SharedBehaviors.FindFlank(AI, Owner, TargetPos, range);
	if not Spot then
		SharedBehaviors.Trace(Owner, "flank: nowhere to go");
		return false;
	end
	AI.Flank = { Keep = SharedBehaviors.RememberOrder(AI, Owner), Spot = Spot, Timer = Timer() };
	Owner:SetNumberValue("AIFlank", 1);
	Owner:ClearAIWaypoints();
	Owner:AddAISceneWaypoint(Spot);
	Owner.AIMode = Actor.AIMODE_GOTO;
	SharedBehaviors.Trace(Owner, "flank: to " .. math.floor(Spot.X) .. "," .. math.floor(Spot.Y) .. " (" .. cost .. " nodes)");
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

-- The step kinds the script's own mover knows (0 to 7): a leap (8) or a mantle (9), which only the engine's route-follower takes as such, is
-- a jump to it. (Taken for a walk, a leap's gap was walked off the edge of, and the comparison runs with CCCP_LUA_MOVER=1 meant nothing.)
-- A crouch (10) is a walk to it: the body ducks under the low part by itself (AHuman's auto-crouch). A scramble (11) is a jump to it.
-- A swim (12) or a wade (13) is a walk to it: ActorWater strokes the body along with the move keys.
function SharedBehaviors.ScriptStepKind(kind)
	if kind == 8 or kind == 9 or kind == 11 then
		return 2;
	elseif kind == 10 or kind == 12 or kind == 13 then
		return 0;
	end
	return kind;
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
	local ClimbTimer = Timer(); -- How long the climb in progress has been going.
	local Climb = nil; -- The column climb in progress (see SharedBehaviors.ClimbPlan), or nil.
	local DoorWaitTimer = Timer(); -- How long the wait at the door in DoorWaitID has gone on without it opening.
	local DoorWaitID = nil; -- The door waited at (its UniqueID).
	local DoorIgnoreID = nil; -- A door whose wait gave up: walked into as before, for a while (see DoorIgnoreTimer).
	local DoorIgnoreTimer = Timer();
	local FlightState = {}; -- Kept between ticks by SharedBehaviors.FlightControl.
	local FlightPlan = nil; -- A flight flown as one, take-off to touchdown (see SharedBehaviors.UpdateFlightPlan).
	local FlightSearchTimer = Timer(); -- When the landing was last looked for, with no flight under way.
	local FlightLastWalk = nil; -- Whether the last look found the way walkable.
	local WalkBestGap = nil; -- While walking instead of flying: the nearest the unit has been to the route's next point.
	local WalkProgressTimer = Timer(); -- Since the walk last got 6 px nearer.
	local WalkJetTimer = Timer(); -- Since the walk was given up on for a moment and the jet allowed.
	WalkJetTimer:SetSimTimeLimitMS(0);
	WalkJetTimer.ElapsedSimTimeMS = 100000;
	local RouteCheckTimer = Timer(); -- How long since the route was last checked in flight (see Actor::RequestRouteCheck).
	local NotAShaft = nil; -- The last jump point looked at and found not to be a shaft's (left to the walking code).
	local ProneHoldTimer = Timer(); -- How long a crawl is kept up after the way ahead looks clear.
	ProneHoldTimer:SetSimTimeLimitMS(1200);
	-- The body: where the feet and the top of the head are (see SharedBehaviors.StandingHeight). Rays that must start in the open under a
	-- ceiling the body just fits start a couple of pixels under the head's top.
	local feetBelowPos = Owner.Height * 0.2;
	local standingHeight = SharedBehaviors.StandingHeight(Owner);
	local headAbovePos = SharedBehaviors.HeadAbovePos(Owner);
	local underHeadTop = headAbovePos - 2;
	NeedsNewPath = true;
	AI.jetClimb = false; -- A climb from a previous order or path is over.

	Owner:RemoveNumberValue("AI_StuckForTime");

	while true do
		Waypoint = nil;
		HasMovePath = false;
		NextWptPos = nil; -- The waypoint after this one, so a climb knows which way it steps off at the top.
		-- The lean a crab's jet is to have this tick, -1, 0 or 1 (the crab AI sets its move stick by it; see NativeCrabAI.lua): straight up
		-- unless a climb or a brake below asks otherwise. A crab's turret can't aim the nozzle, and its legs flail while the jet is lit, so
		-- this is the only sideways force a flying crab has.
		AI.jetLeanX = 0;
		AI.jetSteady = false; -- Set by the climb: relight the jet without a burst (see the native AI's jump state).
		AI.ladderUp = false; -- Set by a ladder climb: up is pressed (see SharedBehaviors.LadderAt).
		AI.ladderDown = false; -- Set going down a ladder: down is pressed.
		local doorHold = false; -- Standing in the doorway of a door of ours, on its sensor, for it to open (see below).
		local doorGoal = nil; -- The doorway to go and stand in.
		local doorPass = false; -- In the way of an open door's piece: no standing here.

		-- ugh
		local wptIndex = 0;
		for pos in Owner.MovePath do
			wptIndex = wptIndex + 1;
			if wptIndex == 1 then
				HasMovePath = true;
				Waypoint = {};
				Waypoint.Pos = pos;
				Waypoint.Type = nil;
				-- What the pathfinder meant by this step (0 walk, 1 crawl, 2 jump, 3 fall, 4 dig, 5 door, 6 stairs), so it needn't be guessed from
				-- the ground: a step off an edge is walked off, not hopped; a jump is jetted whatever the slope looks like; a crawl is gone prone
				-- for; stairs are walked, steep as they look.
				Waypoint.Kind = SharedBehaviors.ScriptStepKind(Owner.MovePathStepKind);
				-- (A dig step for a unit with nothing to dig with is whatever the ground makes it: it mustn't keep the unit from a hop or a climb.)
				if Waypoint.Kind == 4 and not Owner:HasObjectInGroup("Tools - Diggers") then
					Waypoint.Kind = 0;
				end
				if Owner:IsAITracedOn("AI") and Waypoint.Kind ~= LastTracedKind then
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
				-- Not while climbing, stepping off the top of a climb, or in the air: a unit that had just come up through a hatch lay down with
				-- half its body still over the hole, and a prone unit may not jet, so it slid back down the hatch, and did so for the whole minute.
				local climbingNow = Climb ~= nil or AI.jetClimb or AI.flying or (ClimbStepX ~= 0 and not ClimbStepTimer:IsPastSimTimeLimit());
				if climbingNow then
					-- And up off the ground if it is lying down: a prone body may not jet, so a unit that stepped off a corridor's edge prone fell
					-- the whole outside wall of a bunker with no brake and no steering, past the landing it was meant for, while the planner asked
					-- for the jet every tick; and one lying under a small climb waited four seconds for the stuck handler to stand it up.
					AI.proneState = AHuman.NOTPRONE;
				elseif Owner.Head and Owner.Head:IsAttached() then
					-- Where the top of the head is when standing: the standing height up from the floor under the unit, so the pose doesn't move it.
					-- (Not where the head is now: prone, it's lower, the way looked clear, the unit stood up into the ceiling, and so on every tick.
					-- And not a fixed distance over Pos either: that put the probe 55 px over the floor, in the ceiling of every 48 px corridor,
					-- which the grid rightly calls walkable, and units crawled the length of them.)
					local Floor = Vector();
					local floorY = Owner.Pos.Y + feetBelowPos;
					if SceneMan:CastStrengthRay(Owner.Pos, Vector(0, Owner.Height * 0.5), 5, Floor, 2, rte.grassID, true) then
						-- (Never deeper than the standing feet: over the edge of a drop the floor is further down, and a probe taken from there sat
						-- at chest height and read a knee-high step ahead as no head room.)
						floorY = math.min(Floor.Y, Owner.Pos.Y + feetBelowPos);
					end
					local topHeadPos = Vector(Owner.Pos.X, math.min(Owner.Pos.Y - 4, floorY - standingHeight));
					-- No room to stand right here (the mouth of a low tunnel): kept down whatever the way on looks like. Stood up for a point
					-- steeply above, a unit at a tunnel's mouth put its head into the slab over it and could not walk the last steps out.
					local noRoomHere = AI.proneState == AHuman.PRONE and SceneMan:CastStrengthRay(Owner.Pos, topHeadPos - Owner.Pos, 5, Free, 4, rte.doorID, true);
					if angleDegrees > crawlThresholdDegrees and not noRoomHere then
						AI.proneState = AHuman.NOTPRONE;
					else

					-- first check up to the top of the head, and then from there forward
					-- (A crawl step lays the unit down only once it is near: the step's point is where the low part is, and a route's points can be a
					-- long way apart. Prone from 100 px out, a unit at the top of the tower lay at its start and never crawled to the hatch.)
						local crawlStepNear = Waypoint.Kind == 1 and SceneMan:ShortestDistance(Owner.Pos, Waypoint.Pos, false):MagnitudeIsLessThan(Owner.Height * 0.65);
						if crawlStepNear or SceneMan:CastStrengthRay(Owner.Pos, topHeadPos - Owner.Pos, 5, Free, 4, rte.doorID, true) or SceneMan:CastStrengthRay(topHeadPos, heading, 5, Free, 4, rte.doorID, true) then
							if Owner:IsAITracedOn("Climb") and AI.proneState ~= AHuman.PRONE then ConsoleMan:PrintString("AITRACE crawl: going prone, wpt dx " .. math.floor(heading.X) .. " dy " .. math.floor(heading.Y)); end
							AI.proneState = AHuman.PRONE;
							ProneHoldTimer:Reset();
						elseif AI.proneState ~= AHuman.PRONE or ProneHoldTimer:IsPastSimTimeLimit() then
							-- (Kept down a moment after the way looks clear: a crawl through a slot was stood up in the middle of.)
							if Owner:IsAITracedOn("Climb") and AI.proneState == AHuman.PRONE then ConsoleMan:PrintString("AITRACE crawl: standing up"); end
							AI.proneState = AHuman.NOTPRONE;
						end
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

		if Climb then
			StuckTimer:Reset(); -- (The climb has failure tests of its own.)
		end
		-- In the air or on the jet, the route is checked again twice a second: the same check an order makes, from where the unit is now. A
		-- jet's overshoot left units following a route that had fallen behind them, turning back for points already passed until the
		-- scheduled re-path came round. The answer replaces the route only if the goal is reachable from here (Actor::RequestRouteCheck).
		-- (Not in a shaft climb, which follows its column.)
		if not Climb and not NeedsNewPath and (AI.flying or AI.jump) and Owner.MovePathSize > 0 and RouteCheckTimer:IsPastSimMS(500) then
			RouteCheckTimer:Reset();
			Owner:RequestRouteCheck();
		end

		if AI.refuel and Owner.Jetpack and not Climb then
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
		elseif UpdatePathTimer:IsPastSimTimeLimit() and not Climb and not (AI.jetClimb and AI.flying) then
			-- (Not in the middle of a climb that is in the air: a new path half way up a shaft put the jet out, and the unit fell back to the
			-- foot and started over on a tank sized for one climb. The tank bounds how long a climb can hold this off; the timer stays past,
			-- so it fires the tick the climb ends. On the ground it fires regardless: a unit that had fallen out of its climb and stood under
			-- a ceiling with the climb still "on" was never given a new route.)
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
				elseif StuckDirectionTimer:IsPastSimTimeLimit() then
					-- The drift under the jet, picked afresh only when the last has had its moment (StuckDirectionTimer), as on the ground: rolled
					-- every tick, it went left, still and right at random many times a second, the flicker the ground rules below were rid of.
					local chance = PosRand();
					if chance < 0.1 then
						nextLatMove = Actor.LAT_LEFT;
					elseif chance > 0.9 then
						nextLatMove = Actor.LAT_RIGHT;
					else
						nextLatMove = Actor.LAT_STILL;
					end
					StuckDirectionTimer:Reset();
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
						if Owner:IsAITracedOn("Pilot") then ConsoleMan:PrintString("AITRACE jet: stuck"); end
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
						-- (A squad's follower goes to its place in line, not to the leader: see the native AI and SquadPoint.)
						local Goal = AI.squadPoint or Owner.MOMoveTarget.Pos;
						local Trace = SceneMan:ShortestDistance(Owner.Pos, Goal, false);
						-- (In plain sight: the ray says whether something is in the way; read the other way round, it added the waypoint
						-- only when something was.)
						if Trace.Largest < Owner.Height * 0.5 + (Owner.MOMoveTarget.Height or 100) * 0.5 and
							not SceneMan:CastStrengthRay(Owner.Pos, Trace, 5, Vector(), 4, rte.grassID, true)
						then -- add a waypoint if the place is close and in LOS
							Waypoint = {Pos=SceneMan:MovePointToGround(Goal, Owner.Height*0.2, 4)};
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

						if Owner.MOMoveTarget.Team == Owner.Team then
							-- Following one of ours: a squad's follower keeps to its place in line behind the leader (AI.squadPoint, from the
							-- native AI each tick: so far back along the way the leader came), anyone else to the one followed. The route's
							-- last point is that place; near it and in plain sight it is walked straight to. (It used to be steered straight
							-- at from any distance, through whatever was in the way, and all of a squad at the leader itself, where they
							-- shoved for the one spot.)
							local Goal = AI.squadPoint or Owner.MOMoveTarget.Pos;
							local ToGoal = SceneMan:ShortestDistance(Owner.Pos, Goal, false);
							-- Straight at the place: near, level (a climb is the pather's) and nothing between, measured a fifth of a body up so
							-- the line over the brow of a hill doesn't clip it.
							local Lift = Vector(0, -Owner.Height * 0.2);
							local function StraightTo()
								return ToGoal:MagnitudeIsLessThan(Owner.Height * 1.5) and math.abs(ToGoal.Y) < Owner.Height * 0.3 and SceneMan:CastObstacleRay(Owner.Pos + Lift, ToGoal, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 6) < 0;
							end
							-- A place in line already stands off the leader, so its radius must be under half the gap between places, or two
							-- places' "in place" overlap and two followers hold on the one spot; following a unit itself, the old radius.
							local nearIn = AI.squadPoint and 0.15 or 0.3;
							local nearOut = AI.squadPoint and 0.25 or 0.4;
							-- (And no holding while the one followed is on the move: the place moves with it, and a follower that held until
							-- it was 50 px off, then asked for a new route and walked back, lurched along in stops and starts.)
							local moving = Owner.MOMoveTarget.Vel.Largest > 1;
							if moving or ToGoal.Largest > Owner.Height * nearIn + (Owner.MOMoveTarget.Height or 100) * nearIn then
								if StraightTo() then
									Waypoint.Pos = Goal;
								end
							else	-- in place
								if not AI.flying then
									-- Held here, still, until the place moves off (the leader walks on) or something comes between: the stuck
									-- handling and the scheduled re-path don't count the wait. Out of the hold, the place is walked straight to
									-- when it is near and in plain sight, else a new route is asked for.
									while true do
										StuckTimer:Reset();
										UpdatePathTimer:Reset();
										AI.lateralMoveState = Actor.LAT_STILL;
										AI.jump = false;

										local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
										if _abrt then return true end

										if Owner.MOMoveTarget and MovableMan:ValidMO(Owner.MOMoveTarget) then
											Goal = AI.squadPoint or Owner.MOMoveTarget.Pos;
											ToGoal = SceneMan:ShortestDistance(Owner.Pos, Goal, false);
											-- (And not held in the way of a door's piece: see InDoorSweep.)
											local off = ToGoal.Largest > Owner.Height * nearOut + (Owner.MOMoveTarget.Height or 100) * nearOut or Owner.MOMoveTarget.Vel.Largest > 1 or SharedBehaviors.InDoorSweep(Owner);
											local Exit = SharedBehaviors.DoorSweepExit(Owner, Goal);
											if Exit then
												Waypoint.Pos = Exit; -- (Out of the sweep, not back to the place in it.)
												break;
											end
											if off and StraightTo() then
												Waypoint.Pos = Goal;
												break;
											end
											if off or SceneMan:CastObstacleRay(Owner.Pos + Lift, ToGoal, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 6) >= 0
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
							-- Doubled in flight, but not for a jump's landing beside us once we are up at its height: that point is on the floor past the
							-- hole's edge, and "near enough" from thirty pixels off, over the hole or on the other lip, dropped it, and the unit walked for
							-- the point after it, across the hole, and fell in.
							if AI.jump and not (Waypoint.Kind == 2 and CurrDist.Y > -Owner.Height * 0.2 and math.abs(CurrDist.X) > Owner.Height * 0.15) then
								tolerance = tolerance * 2;
							end

							-- The top of a jet column (a jump point with nothing under it for half a body: the pather's "up, then over" point) is reached
							-- from its height, not from anywhere within the tolerance: the top of a hatch's column sits just over the floor it lands on,
							-- and "reached" from 28 px below it, while the feet were still a body's length down the hatch, turned the unit for the landing
							-- beside it and drove it under the floor slab. A jump point on the floor (a 24 px step, a landing) is for the legs and pops as
							-- any other: held to its height, a prone unit at a step under a doorway, which may not jet, crept against it for five seconds.
							local function IsApexPoint()
								return Waypoint.Kind == 2 and not SceneMan:CastStrengthRay(Waypoint.Pos, Vector(0, Owner.Height * 0.6), 5, Vector(), 3, rte.grassID, true);
							end
							local apexPoint = IsApexPoint();
							-- Under a low ceiling (a tunnel, or the slab over its mouth), a point above us is not reached, nor passed, however near: from
							-- inside a tunnel the jump point at its mouth was "reached" 33 px below and 19 across it, the climb took over, stood the unit
							-- up into the slab, and it never got out.
							local function LowCeilingUnder(Point)
								-- (Lying down only: a unit standing in a 48 px corridor touches its ceiling with this probe, and then no step up in the
								-- corridor was ever reached.)
								return AI.proneState == AHuman.PRONE and Point.Y < Owner.Pos.Y - 6 and SceneMan:CastStrengthRay(Owner.Pos, Vector(0, -Owner.Height * 0.3), 5, Vector(), 3, rte.grassID, true);
							end

							-- A waypoint behind us with the next one in plain sight is done with: the path's nodes are only 24 px apart, and waiting to stand on
							-- each one pulled a running or flying unit back to every node it had passed.
							if not Climb and Waypoint.Type ~= "last" and Owner.MovePathSize > 1 and CurrDist:MagnitudeIsGreaterThan(tolerance) then
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
									-- (The sign test needs the point to be off to one side by more than the wobble of a climb: an apex straight overhead, with
									-- the landing beside it, was "passed" from a pixel to one side of its column, and the climb went on against the landing,
									-- into the slab under it. And a climb's top is never passed from below it while the climb is on: the climb reaches it in
									-- its own terms, then steps off; "passed" for the landing from 90 px down the shaft, because the landing was the nearer
									-- of the two, carried the climb state onto the landing and from there onto the next shaft's jump point, which was then
									-- climbed with no look at the tank.)
									local passed = ToNext:MagnitudeIsLessThan(CurrDist.Magnitude) or (CurrDist.X * ToNext.X < 0 and math.abs(CurrDist.X) > 6 and math.abs(CurrDist.X) < Owner.Height * 0.5 and math.abs(CurrDist.Y) < Owner.Height * 0.5);
									if apexPoint and AI.jetClimb and CurrDist.Y < -6 then
										passed = false;
									end
									if passed and (LowCeilingUnder(Waypoint.Pos) or LowCeilingUnder(NextPos)) then
										passed = false;
									end
									-- (Through a door of ours is in plain sight: it opens as we come.)
									local Blocked = Vector();
									local inSight = passed and (SceneMan:CastObstacleRay(Owner.Pos, ToNext, Blocked, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 9) < 0 or SharedBehaviors.OurDoorAt(Owner, Blocked) ~= nil);
									if inSight then
										if Owner:IsAITracedOn("AI") then ConsoleMan:PrintString("AITRACE pop: passed " .. math.floor(Waypoint.Pos.X) .. "," .. math.floor(Waypoint.Pos.Y) .. " for " .. math.floor(NextPos.X) .. "," .. math.floor(NextPos.Y) .. " from " .. math.floor(Owner.Pos.X) .. "," .. math.floor(Owner.Pos.Y)); end
										PrevWptPos = Waypoint.Pos;
										Owner:RemoveMovePathBeginning();
										Waypoint.Pos = NextPos;
										Waypoint.Kind = SharedBehaviors.ScriptStepKind(Owner.MovePathStepKind);
										if Waypoint.Kind == 4 and not Owner:HasObjectInGroup("Tools - Diggers") then
											Waypoint.Kind = 0;
										end
										if Waypoint.Kind == 3 then
											Waypoint.Type = "drop";
										end
										if Owner.MovePathSize == 1 then
											Waypoint.Type = "last";
										end
										CurrDist = ToNext;
										apexPoint = IsApexPoint();
										-- A new jump point well above us is a new climb, with its own look at the way up and the tank (see the climb): with
										-- the climb state carried over from the last one, a 320 px shaft was flown on the fuel the last one left. (A jump
										-- point below, the landing beside a column's top, is the same climb's step off, and keeps the state.)
										if AI.jetClimb and Waypoint.Kind == 2 and NextPos.Y < Owner.Pos.Y - Owner.Height * 0.3 then
											AI.jetClimb = false;
										end
									end
								end
							end

							-- (The top of a column is held to its height only in the air: standing on the landing under a top that sits 20 px over Pos,
							-- nothing else would ever pop it.)
							local notThereYet = Climb ~= nil or CurrDist:MagnitudeIsGreaterThan(tolerance) or (apexPoint and CurrDist.Y < -6 and (AI.jetClimb or AI.flying)) or LowCeilingUnder(Waypoint.Pos);
							if notThereYet then	-- not close enough to the waypoint
								ArrivedTimer:Reset();

								-- check if we have LOS to the waypoint
								-- (A climb has no line of sight to its top from under the lip it's climbing round, and a new path every second, each
								-- one starting the climb over, kept the unit bobbing at the foot of the shaft.)
								-- (A door of ours in the way is no loss of sight: it opens as we come. With a closed hatch door of its own team over it, a
								-- unit under a shaft asked for a new route every second, and never climbed.)
								local Blocked = Vector();
								if Climb or AI.jetClimb or SceneMan:CastObstacleRay(Owner.Pos, CurrDist, Blocked, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 9) < 0 or SharedBehaviors.OurDoorAt(Owner, Blocked) then
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
									if Owner:IsAITracedOn("AI") then ConsoleMan:PrintString("AITRACE pop: reached " .. math.floor(Waypoint.Pos.X) .. "," .. math.floor(Waypoint.Pos.Y) .. " from " .. math.floor(Owner.Pos.X) .. "," .. math.floor(Owner.Pos.Y) .. ", " .. Owner.MovePathSize - 1 .. " left"); end
									PrevWptPos = Waypoint.Pos;
									Owner:RemoveMovePathBeginning();
									Waypoint = nil;
									-- A new jump point well above us is a new climb, looked at afresh (the way up, the tank); see the "passed" pop.
									if AI.jetClimb and Owner.MovePathStepKind == 2 then
										for pos in Owner.MovePath do
											if pos.Y < Owner.Pos.Y - Owner.Height * 0.3 then
												AI.jetClimb = false;
											end
											break;
										end
									end
								end
							end

							if Waypoint then	-- move towards the waypoint
								-- A door of ours on the way that isn't open yet is waited for a little short of it: its sensors see us from there and open
								-- it, and a leaf in motion kills whatever stands in its sweep (a unit that jetted up into a floor hatch as its leaves swung
								-- was gibbed at full health). Enemy doors are the fighting rules' business, and the pather's.
								local DoorAhead = SharedBehaviors.DoorAhead(Owner, Waypoint.Pos);
								-- (A door whose wait came to nothing is walked into, as doors were before these manners, for five seconds: a unit that
								-- thought itself on a sensor it wasn't quite on, outdoors at a slide door stood on end, waited the whole minute.)
								if DoorAhead and DoorIgnoreID == DoorAhead.UniqueID and not DoorIgnoreTimer:IsPastSimMS(5000) then
									DoorAhead = nil;
								end
								if DoorAhead and DoorAhead.Door then
									local state = DoorAhead:GetDoorState();
									if state ~= ADoor.OPEN then
										-- Not open yet. It opens for what its sensors see, and a sensor is a ray across the doorway: a unit waiting 80 px short of
										-- a Door Slide Long's one ray was never seen, and stood 27 s at a door that never opened. So the way to have it open is
										-- to stand in the doorway, on a sensor's ray, and that is where the wait is.
										-- (On the ray, not near it: the door sees a body crossing the ray, and a unit 25 px off it wasn't seen.)
										if DoorAhead:SensesPoint(Owner.Pos, 6) then
											doorHold = true;
										else
											local Sense = DoorAhead:NearestSensorPoint(Owner.Pos);
											if Sense.Largest > 0 then
												doorGoal = SceneMan:MovePointToGround(Sense, Owner.Height * 0.2, 4);
											else
												-- (A door with no sensors opens by other means; a wait short of its piece is the most that can be done.)
												doorHold = SceneMan:ShortestDistance(Owner.Pos, DoorAhead.Door.Pos, false).Magnitude < Owner.Height * 0.8;
											end
										end
										if doorHold or doorGoal then
											if DoorWaitID ~= DoorAhead.UniqueID then
												DoorWaitID = DoorAhead.UniqueID;
												DoorWaitTimer:Reset();
											elseif state == ADoor.CLOSED and DoorWaitTimer:IsPastSimMS(2000) then
												if Owner:IsAITracedOn("AI") then ConsoleMan:PrintString("AITRACE door: " .. DoorAhead.PresetName .. " didn't open in 2 s, walking into it"); end
												DoorIgnoreID = DoorAhead.UniqueID;
												DoorIgnoreTimer:Reset();
												DoorWaitID = nil;
												doorHold = false;
												doorGoal = nil;
											end
										end
										if (doorHold or doorGoal) and Owner:IsAITracedOn("AI") and math.random() < 0.1 then ConsoleMan:PrintString("AITRACE door: " .. (doorHold and "in the doorway of " or "going to the doorway of ") .. DoorAhead.PresetName .. " (state " .. state .. ")"); end
									end
								end
								-- An open door's piece is no place to stand: it closes a second and a half after its sensors last saw a body, on whatever
								-- is in its way (three units died at full health under doors of their own team), so in its way the legs keep moving.
								if SharedBehaviors.InDoorSweep(Owner) then
									doorPass = true;
								end
								-- A climb up a column to a landing (a jump point well above us) is the climber's: see SharedBehaviors.ClimbPlan. The walking
								-- and jetting below wait while it runs; a door of ours in the way is waited for first (the manners above).
								local climbHandled = false;
								-- (In the air, only from near the column: the climb's start in the air goes straight to the rise, and planned in a hop 90 px
								-- short of the column it flew there at 11 m/s, past it, and ran dry. Further out the flight control takes the unit there.)
								local farInAir = AI.flying and math.abs(CurrDist.X) > Owner.Height * 0.3;
								if not Climb and not farInAir and not doorHold and not doorGoal and Waypoint.Kind == 2 and Owner.Jetpack and Owner.Jetpack.JetpackType == AEJetpack.Standard and CurrDist.Y < -Owner.Height * 0.3 and not (NotAShaft and SceneMan:ShortestDistance(NotAShaft, Waypoint.Pos, false):MagnitudeIsLessThan(4)) then
									Climb = SharedBehaviors.ClimbPlan(AI, Owner, Waypoint.Pos, NextWptPos);
									if not Climb.shaft and not Climb.ladder then
										NotAShaft = Vector(Waypoint.Pos.X, Waypoint.Pos.Y);
										Climb = nil;
									end
								end
								if Climb and Climb.stage == "align" and Climb.stageTimer.ElapsedSimTimeMS < 1 then
									if Owner:IsAITracedOn("Climb") then ConsoleMan:PrintString("AITRACE climb: plan from " .. math.floor(Owner.Pos.X) .. "," .. math.floor(Owner.Pos.Y) .. " up column " .. math.floor(Climb.colX) .. " to y " .. math.floor(Climb.riseToY) .. ", landing " .. math.floor(Climb.landing.X) .. "," .. math.floor(Climb.landing.Y) .. (Climb.blocked and ", no open column" or "")); end
								end
								if Climb then
									climbHandled = true;
									AI.proneState = AHuman.NOTPRONE;
									if doorHold or doorGoal then
										-- Paused for a door: the climb's stage and rise clocks wait with it. (Left running, a second's wait at a hatch
										-- was a second of "no rise", and the climb failed the moment it went on.)
										Climb.stageTimer:Reset();
										if Climb.progressTimer then
											Climb.progressTimer:Reset();
										end
									end
									if not doorHold and not doorGoal then
										local status, lat, aim = SharedBehaviors.ClimbUpdate(AI, Owner, Climb);
										nextLatMove = lat;
										nextAimAngle = aim;
										if status == "done" then
											-- Its points off the route, if they are still the route's first, and the legs walk on.
											local pops = 0;
											for pos in Owner.MovePath do
												if SceneMan:ShortestDistance(pos, Climb.top, false):MagnitudeIsLessThan(4) then
													pops = Climb.pops;
												end
												break;
											end
											for _ = 1, pops do
												if Owner.MovePathSize > 0 then
													Owner:RemoveMovePathBeginning();
												end
											end
											PrevWptPos = Vector(Climb.landing.X, Climb.landing.Y);
											Climb = nil;
											StuckTimer:Reset();
										elseif status == "fail" then
											-- A new route from here.
											Climb = nil;
											Waypoint = nil;
											NeedsNewPath = true;
										end
									end
								end
								if not climbHandled then
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
										-- The chest is a tenth of the height over Pos (thirty pixels over the feet) and the head ray just under the head's top: a step
										-- of up to thirty pixels passes under both, one up to the head's height blocks the chest alone, and only something higher
										-- than the body blocks both. (With the rays 40 and 50 px over the feet, the head's was in the ceiling of every 48 px
										-- corridor, so a knee-high step in a corridor was a wall, and jetted at.)
										local chest = Owner.Pos + Vector(0, -Owner.Height * 0.1);
										local head = Owner.Pos + Vector(0, -underHeadTop);
										local ChestHit = Vector();
										local HeadHit = Vector();
										local chestHit = SceneMan:CastStrengthRay(chest, ahead, 5, ChestHit, 2, rte.grassID, true);
										local headHit = SceneMan:CastStrengthRay(head, ahead, 5, HeadHit, 2, rte.grassID, true);
										-- Chest and head both blocked, at much the same distance, is a wall (a face); chest alone, or the head's hit well beyond the
										-- chest's, is a steep slope or a step, which the legs and climbing arms deal with, and the stuck handling jets if they can't.
										-- (Both hitting was enough before: with the head ray at 42 px over the feet a 45 degree slope met both within a body length,
										-- and slopes that were walked were jetted at.)
										local face = chestHit and headHit and math.abs(HeadHit.X - ChestHit.X) < 8;
										-- (Nor on stairs, whose risers meet both rays within a body length: the legs walk them.)
										if face and Waypoint.Kind ~= 3 and Waypoint.Kind ~= 4 and Waypoint.Kind ~= 6 then
											WallAhead = true;
											local up = (side < 0) and (Obstacles[Obst.L_UP] == true) or (side > 0 and Obstacles[Obst.R_UP] == true);
											if Owner.Jetpack and Owner.Jetpack.JetpackType == AEJetpack.Standard and Owner.Jetpack.JetTimeLeft >= AI.minBurstTime and not up then
												AI.jump = true;
												if Owner:IsAITracedOn("Pilot") then ConsoleMan:PrintString("AITRACE jet: wall ahead"); end
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
											if Waypoint.Type ~= "drop" and Waypoint.Kind ~= 6 and not Lower(Waypoint, Owner, 20) and Owner.Jetpack.JetpackType == AEJetpack.Standard and AI.proneState ~= AHuman.PRONE then
												-- jump over low obstacles unless we want to jump off a ledge (and not while crawling under something, nor on stairs: each
												-- step of them is a low obstacle, and a hop at each one is a flight of them jetted)
												if nextLatMove == Actor.LAT_RIGHT and Obstacles[Obst.R_FRONT] and not Obstacles[Obst.R_UP] then
														hopping = true;
													if Owner:IsAITracedOn("Pilot") and not AI.jump then ConsoleMan:PrintString("AITRACE jet: hop right"); end
													AI.jump = true;
													-- Something high in front as well: straight up, not backwards. Backing off with the jet lit (the nozzle leans the way
													-- we move) sent units flying back down the slope they had just climbed.
													if Obstacles[Obst.R_HIGH] then
														nextLatMove = Actor.LAT_STILL;
													end
												elseif nextLatMove == Actor.LAT_LEFT and Obstacles[Obst.L_FRONT] and not Obstacles[Obst.L_UP] then
														hopping = true;
													if Owner:IsAITracedOn("Pilot") and not AI.jump then ConsoleMan:PrintString("AITRACE jet: hop left"); end
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
												-- (Nor stairs: steeper than the legs' slope limit as the rise reads from their foot, but the pather only sends a unit up
												-- stairs it can walk, and a soldier walks the base game's steepest.)
												local wantsClimb = not doorHold and not doorGoal and AI.proneState ~= AHuman.PRONE and Waypoint.Kind ~= 4 and Waypoint.Kind ~= 6 and ((Waypoint.Kind == 2 and above < -Owner.Height * 0.3) or (above < -Owner.Height * 0.25 and steep) or (WallAhead and above < Owner.Height * 0.3));
											local climbRefused = false;
											local stepToColumnDx = nil; -- A step to take first, to under the column the climb goes up.
											if wantsClimb and not climbing then -- (In the air too: a unit passing a ledge on the way up from one jump couldn't start the next.)
												local towardsX = CurrDist.X;
												local Hit = Vector();
												-- Room over the head for the climb itself, no more: a ceiling well above where we're going is no ceiling.
												-- (As far as the head travels and a few pixels short of it, not over: the pather fits the head two pixels under the
												-- ceiling at the top of a column, and a probe that went four pixels past where the head would be found that ceiling
												-- at the top of every ceiling-limited shaft, and the climb was refused without a word for as long as the unit stood there.)
												local Up = Vector(0, math.min(-Owner.Height * 0.2, above + 4));
												-- In a shaft the climb is up its middle, so that's where the way up is looked at from: a unit a few pixels off the middle
												-- of a shaft two bodies wide had its ray up hit the wall's edge, and stood at the bottom for ever.
												-- (Out to a body's height either way: a hub's opening is 96 px across, and from a column near one wall of it the far wall
												-- was out of a shorter reach, so the opening wasn't a shaft and the climb drifted into its side.)
												local shaftX, shaftWidth = SharedBehaviors.ShaftMiddle(Owner, Owner.Pos.X, Owner.Pos.Y - Owner.Height * 0.1, Owner.Height);
												-- (A shaft, not a room: with both walls of a room within reach its middle was taken for a shaft's, and the way up looked at
												-- from there, beside the hatch, found the ceiling every time.)
												if shaftWidth and shaftWidth > Owner.Height * 1.2 then
													shaftX = nil;
												end
												local probeX = shaftX or Owner.Pos.X;
												-- The way up is looked at from our own column first, then from the shaft's middle. (A door of ours across the column is no
												-- ceiling: it opens as we come up to it, and the manners above hold us short of it until it has. The pather already routes
												-- through it.)
												local function OpenAbove(x)
													return SceneMan:CastObstacleRay(Vector(x, Owner.Pos.Y - underHeadTop), Up, Hit, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) < 0 or SharedBehaviors.OurDoorAt(Owner, Hit) ~= nil;
												end
												local columnX = Owner.Pos.X; -- The column the climb goes up.
												local ceiling = not OpenAbove(Owner.Pos.X);
												if ceiling and probeX ~= Owner.Pos.X and OpenAbove(probeX) then
													ceiling = false;
													columnX = probeX;
												end
												-- Still under a ceiling: the route's own column, when it is open, before any other. (The nearest opening could be the other
												-- way: a unit 48 px from the ladder its route went up walked 50 px away from it, to a gap that led nowhere, all minute.)
												if ceiling and math.abs(towardsX) >= Owner.Height * 0.12 and OpenAbove(Waypoint.Pos.X) then
													ceiling = false;
													columnX = Waypoint.Pos.X;
												end
												AI.climbShaftX = shaftX;
												-- A tall climb wants the fuel it will take: started on half a tank it ends part way up the face, with the fall and the wait
												-- to refill to show for it. So the unit waits at the foot until the tank is in. How much is wanted is what the climb burns
												-- flown the way it is flown (see ClimbFuelLeft), with a reserve for the hover and the step off at the top; never more than
												-- 95% of a tank, or it would never go.
												-- (It used to be reckoned from the height a whole tank buys, 440 px for a soldier, but that is the height of a ballistic
												-- burn, and a climb flown under control at 5 m/s got about 250 px from a tank: a 192 px shaft left it with nothing at the top.
												-- Then from the climb's seconds at 8 m/s, nine tenths lit, which let a 320 px climb go that the tank could not make.)
												-- (Every climb the route asks for, not only the tall ones: the route is planned on a full tank, and a unit that set off on
												-- whatever was left from the last one missed what a player, waiting a moment at the foot, makes easily.)
												local tall = above < -Owner.Height * 0.3;
												-- What the climb would leave at the top, flown from the tank as it is (see ClimbFuelLeft), must cover the hover and the step
												-- off: 450 ms, since the jet is weak by then. A climb that no full tank makes by that reckoning is tried on a full one.
												local tankIn;
												if tall then
													-- (Less for a short climb, whose hover is short: a 54 px step wanted 450 ms over its own burn, and waited on half a tank;
													-- never under 300, since below 250 the hover at the top could not relight at all.)
													local reserve = math.min(450, math.max(300, 150 - above));
													if SharedBehaviors.ClimbFuelLeft(AI, Owner, -above, Owner.Jetpack.JetTimeLeft) >= reserve then
														tankIn = true;
													elseif SharedBehaviors.ClimbFuelLeft(AI, Owner, -above, Owner.Jetpack.JetTimeTotal) < reserve then
														tankIn = Owner.Jetpack.JetTimeLeft >= Owner.Jetpack.JetTimeTotal * 0.95;
													else
														tankIn = false;
													end
												else
													tankIn = true;
												end
												tankIn = tankIn and Owner.Jetpack.JetTimeLeft >= AI.minBurstTime;
												if not tankIn and tall and not AI.flying then
													AI.refuel = true;
												end
												-- A climb begins from under the open column, not beside it: lit under the slab beside a hatch, with the drift to carry it
												-- across, a unit burned half a tank pinned to the slab's underside before it was under the opening. On the ground the legs
												-- go there first (see the refusal below); in the air, passing a ledge on the way up, the drift is all there is.
												-- (And only on floor: over a hole, a landing inside a shaft, the climb starts here and drifts, as before.)
												if not ceiling and tankIn and not AI.flying and math.abs(columnX - Owner.Pos.X) >= Owner.Height * 0.12 and SceneMan:CastObstacleRay(Vector(columnX, Owner.Pos.Y), Vector(0, Owner.Height * 0.5), Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 4) >= 0 then
													climbRefused = true;
													stepToColumnDx = columnX - Owner.Pos.X;
												elseif not ceiling and tankIn then
													climbing = true;
													AI.jetClimb = true;
													AI.climbClearY = nil;
													AI.climbStartX = Owner.Pos.X;
													ClimbTimer:Reset();
													if Owner:IsAITracedOn("Climb") then ConsoleMan:PrintString("AITRACE climb: wpt dx " .. math.floor(towardsX) .. " dy " .. math.floor(above)); end
												else
													climbRefused = true;
												end
											end
											if climbRefused then
												-- A climb is what's wanted and it can't be had here: no jet. Under a ceiling, a step towards the nearest column that is
												-- open above (the edge of the ceiling, the middle of the shaft); else the legs and the stuck handling take it from here.
												AI.jump = false;
												AI.jetClimb = false;
												if not AI.flying then
													local dx = stepToColumnDx;
													if not dx then
														local Up = Vector(0, math.min(-Owner.Height * 0.2, above + 4));
														dx = SharedBehaviors.OpenColumnNear(Owner, Up);
													end
													if dx and math.abs(dx) > 2 then
														nextLatMove = dx > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
														if Owner:IsAITracedOn("Climb") and math.random() < 0.1 then ConsoleMan:PrintString("AITRACE climb: " .. (stepToColumnDx and ("stepping " .. math.floor(dx) .. " under the open column first") or ("under a ceiling, stepping " .. math.floor(dx) .. " to an open column"))); end
													end
												end
											elseif (climbing or (AI.jetClimb and wantsClimb)) and ClimbTimer.ElapsedSimTimeMS > 1500 and not AI.flying and Owner.Vel.Y > -0.5 and above < -Owner.Height * 0.2 then
												-- On the ground again, a second and a half into a climb, with the waypoint still well above: the climb has failed (fallen
												-- back down the hatch, or come down on the wrong lip), so it ends, and a new route is asked for from here rather than the
												-- rest of the old one followed from where it was never meant to start. (With the climb left "on", the unit stood under
												-- the floor it had fallen through for the rest of the minute, with a route of one point straight above it.)
												if Owner:IsAITracedOn("Climb") then ConsoleMan:PrintString("AITRACE climb: down again, asking for a new route from " .. math.floor(Owner.Pos.X) .. "," .. math.floor(Owner.Pos.Y)); end
												AI.jetClimb = false;
												AI.jump = false;
												AI.climbClearY = nil;
												Waypoint = nil;
												NeedsNewPath = true;
											elseif climbing or (AI.jetClimb and wantsClimb) then
												-- Up at the waypoint's height the climb is over, but only once the feet would clear whatever we step onto next: the waypoint
												-- sits up to a node above the ledge's top, and the step off it is sideways, so the way at foot level has to be open
												-- that way. It is also over well past the waypoint's height, and when the tank runs dry.
												local stepX = CurrDist.X;
												-- The top of a column is a place to turn at, not to land on: the drift and the step off aim at the point after it, the landing,
												-- from wherever the column was climbed. (Measured to the top itself when the unit was more than 10 px off its column, the
												-- step off went towards the column, over the hole.)
												if NextWptPos and (apexPoint or math.abs(stepX) < 10) then
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
													-- And floor under the feet, or the landing straight below: a hatch's landing is a node past the hole's edge, and the jet
													-- cut with the feet merely level with the floor, a node short of it, dropped the unit back down the hole it had climbed
													-- (in the air, with the jet out, nothing moves a unit sideways).
													local groundUnderFeet = SceneMan:CastObstacleRay(Owner.Pos + Vector(0, Owner.Height * 0.2), Vector(0, Owner.Height * 0.25), Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 2) >= 0;
													-- (Only a landing itself counts as reached by being under us; a top-of-column is over the hole by definition.)
													local overLanding = groundUnderFeet or (not apexPoint and math.abs(stepX) < Owner.Height * 0.15);
													-- (With a floor under the feet at the height, the climb is over wherever the point is sideways: the legs walk the rest. Held
													-- in the air until within 40 px of a point 109 px along the corridor, a hover across the merged run of walk nodes after a
													-- hatch's top burned the rest of the tank.)
													local done = (above > -Owner.Height * 0.2 and feetClear and (groundUnderFeet or (overLanding and math.abs(stepX) < Owner.Height * 0.4))) or above > Owner.Height * 0.6 or Owner.Jetpack.JetTimeLeft < TimerMan.AIDeltaTimeMS * 4;
													-- A crab's climb is over at the height: it lands on legs either side, so there is no step off a lip to wait for, and a
													-- slope ahead at foot level is not a floor that is fouled. Held to the human rules against the gym's hill it rose a
													-- hundred pixels past a 23 px climb, burned the tank and fell for half its health.
													if not Owner.Head then
														done = above > -Owner.Height * 0.2 or Owner.Jetpack.JetTimeLeft < TimerMan.AIDeltaTimeMS * 4;
													end
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
													-- The rate allowed with a height still to go is the speed gravity alone brings to a stop a few pixels short of it, up to
													-- the cap: so the tall part is flown at the cap, the jet goes out where the coast will just reach the top, and nothing is
													-- spent fighting gravity on the way down from the cap. (A climb costs fuel by the second lit, not by the pixel, so the
													-- faster the tall part is flown the more of the tank is left for the top: a 192 px shaft flown at 5 m/s took nine tenths
													-- of the tank. Brought down by a fifteenth of the height to go, the rate had the jet lit through the whole top half of a
													-- shaft, holding a speed gravity would have given for nothing, and a 320 px shaft ran the tank dry at its top.)
													local gravity = SceneMan.GlobalAcc.Y * GetPPM();
													local climbRate = math.max(1, math.min(SharedBehaviors.ClimbSpeedCap(Owner), math.sqrt(2 * gravity * math.max(0, toGo - 12)) / GetPPM()));
													-- Relit without a burst once the climb is under way: a burst is a kick and 130 ms of fuel, right for leaving the ground,
													-- and wrong for every pulse of the hover at the top, where the kicks threw the unit up past its point and the fuel went
													-- with them.
													AI.jetSteady = ClimbTimer.ElapsedSimTimeMS > 400;
													-- With some play in it: every relight can be a burst, at a burst's worth of fuel, so the fewer the better.
													if Owner.Head and above > -Owner.Height * 0.2 and not feetClear and math.abs(stepX) >= 10 and not AI.climbClearY and above < Owner.Height * 0.5 then
														-- At the point's height but the feet still foul the floor we step onto (the point sits as low as the landing's
														-- ceiling allows, and the feet hang a fifth of a body under it): a slow rise, a metre a second, until they clear.
														-- Hovering here just burned the tank in the hatch.
														AI.jump = Owner.Vel.Y > (AI.jump and -1.5 or -0.5);
													elseif above > -Owner.Height * 0.2 then
														AI.jump = Owner.Vel.Y > (AI.jump and -1.5 or 0.5);
													else
														-- (A metre a second either side of the rate: the jet goes out at the rate the coast is reckoned from, or the top is
														-- overshot by what the extra speed buys, 26 px from 2 m/s over at the cap.)
														AI.jump = Owner.Vel.Y > (AI.jump and -climbRate - 1 or -climbRate + 1);
													end
													-- Under something (the underside of the ledge we're climbing to, usually): no way up here. The spot just out from under its lip is
													-- remembered, and held until we're above the lip's height; only backing out, and then drifting for the waypoint again, went back
													-- under it every other tick. (The look up goes only as far as the climb does: a roof above where we're heading is no roof.)
													local Lip = Vector();
													-- No higher than where the head will be at the waypoint: the pather lands only where a body stands, so a ceiling at or
													-- over the head's height there is the corridor's own, not a lip. (Probed a fifth of a body up from wherever we were, the
													-- 48 px corridor's ceiling was a lip to every unit coming up a hatch into it, the push out from under it went away from
													-- the landing, and the unit came down on the far lip or back in the hole.)
													-- (A few pixels under the head's top there, not over it: over it was the ceiling the pather had just fitted the head
													-- under, and the sky bunker's hatch still read it as a lip.)
													local headAtWaypoint = Waypoint.Pos.Y - headAbovePos + 4;
													local overTo = math.max(headAtWaypoint - (Owner.Pos.Y - underHeadTop), -Owner.Height * 0.6);
													local Over = Vector(0, math.min(-2, overTo));
													local overOpen = overTo < -4;
													if overOpen and above < -Owner.Height * 0.1 and SceneMan:CastObstacleRay(Owner.Pos + Vector(0, -underHeadTop), Over, Lip, Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) >= 0 then
														-- Out from under it towards the nearest column that is open above (the mouth of a shaft in a corridor's ceiling is on
														-- the waypoint's side; a cliff's lip is on the far side), and no further than a node or so from where the climb began:
														-- indoors there is always a ceiling, and a push away from the waypoint every tick walked units the length of a corridor.
														local startX = AI.climbStartX or Owner.Pos.X;
														local openDx = SharedBehaviors.OpenColumnNear(Owner, Over);
														local pushX = openDx and (Owner.Pos.X + openDx) or (Owner.Pos.X + (CurrDist.X > 0 and -1 or 1) * Owner.Height * 0.3);
														AI.climbClearX = math.max(startX - Owner.Height * 0.6, math.min(startX + Owner.Height * 0.6, pushX));
														AI.climbClearY = Lip.Y;
														if Owner:IsAITracedOn("Climb") and math.random() < 0.1 then ConsoleMan:PrintString("AITRACE climb: under a ceiling at " .. math.floor(Lip.Y) .. ", keeping out from under it"); end
													end
													-- (Above it once the feet are: cleared at head height, the drift back for the waypoint took the unit straight back under
													-- the lip, and the push out again cost the rest of the tank.)
													if AI.climbClearY and Owner.Pos.Y + Owner.Height * 0.2 < AI.climbClearY - 4 then
														AI.climbClearY = nil; -- Above it now.
													end
													-- Sideways it is flown by speed: a little drift towards the waypoint, more the further off it is, and none at all into a wall or
													-- slope (that only pins us to it) or when the waypoint is straight above. Too fast either way and the nozzle is leant against it:
													-- the speed walked up with was carrying units under the ledges they were climbing to.
													-- Paced to the climb: the sideways distance is covered in the time the height takes at the climb rate, so a 24 px lean
													-- over a 300 px climb is a slow drift that arrives at the top beside the landing, not a 1.2 m/s slide that had the unit
													-- against the side of the opening 250 px below it, and stuck under the floor there. Near the top it is the old rule.
													local secondsToTop = math.max(1, -above / (climbRate * GetPPM()));
													local wantVelX = math.max(-4, math.min(4, CurrDist.X / (secondsToTop * GetPPM())));
													-- In a shaft, the middle of it is what's kept to, whatever side the waypoint is on; the walls come first.
													local shaftNow, shaftNowWidth = SharedBehaviors.ShaftMiddle(Owner, Owner.Pos.X, Owner.Pos.Y - Owner.Height * 0.1, Owner.Height);
													if shaftNowWidth and shaftNowWidth > Owner.Height * 1.2 then
														shaftNow = nil; -- A room, not a shaft (see the climb's start).
													end
													if shaftNow then
														wantVelX = math.max(-2, math.min(2, (shaftNow - Owner.Pos.X) / 6));
													end
													-- (A crab's jet is for lift only: leant, it threw the crab backwards off the slope. Its legs do the sideways work.)
													if not Owner.Head then
														wantVelX = 0;
													elseif AI.climbClearY then
														wantVelX = math.max(-3, math.min(3, (AI.climbClearX - Owner.Pos.X) / 10));
													elseif touchingWall and AI.jump and Owner.Vel.Y > -1 then
														wantVelX = stepX > 0 and -1.5 or 1.5;
													elseif not chestClear or math.abs(CurrDist.X) < Owner.Height * 0.15 then
														wantVelX = 0;
													elseif overOpen and SceneMan:CastObstacleRay(Owner.Pos + Vector(wantVelX > 0 and Owner.Height * 0.25 or -Owner.Height * 0.25, -underHeadTop), Over, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) >= 0 then
														-- No drifting in under something: the column a little way over towards the waypoint has to be open above us as far as the
														-- climb goes, or we climb straight here and drift once we're past it. (A cliff with a hollow under its lip drew units
														-- in under the lip, where they burned the tank pinned.)
														wantVelX = 0;
													end
													-- Up at the height and clear to step across: at a walking pace at the least. The paced drift for a landing one node over
													-- is 1.2 m/s, under the move keys' dead band below, so no key was ever pressed and the unit hovered in the hatch's mouth
													-- until the tank was dry. (Not while a shaft's middle is being kept: that is the hole itself, and the walls come first.)
													if Owner.Head and above > -Owner.Height * 0.2 and feetClear and chestClear and not AI.climbClearY and not shaftNow and math.abs(stepX) >= 10 then
														wantVelX = stepX > 0 and math.max(2, wantVelX) or math.min(-2, wantVelX);
													end
													-- The keys are pressed when the speed is off what's wanted by more than a band: a wide one in the open, so the nozzle
													-- isn't flicked about, and a narrow one while climbing a column, where the walk speed carried in at the foot (1.5 m/s is
													-- 30 px a second) took the unit forty pixels off a hatch's column in the time the climb took, into the slab beside it.
													local band = (above < -Owner.Height * 0.2 and math.abs(CurrDist.X) < Owner.Height * 0.15) and 0.5 or 1.5;
													local offVelX = wantVelX - Owner.Vel.X;
													if offVelX > band then
														nextLatMove = Actor.LAT_RIGHT;
													elseif offVelX < -band then
														nextLatMove = Actor.LAT_LEFT;
													else
														nextLatMove = Actor.LAT_STILL;
													end
													-- A crab's jet is lift only (its nozzle is held straight up), so the keys don't lean it: they work the legs, which walk it
													-- towards the waypoint as it rises and wherever it touches the slope.
													if not Owner.Head then
														nextLatMove = CurrDist.X < -3 and Actor.LAT_LEFT or (CurrDist.X > 3 and Actor.LAT_RIGHT or Actor.LAT_STILL);
														-- And the nozzle leans a little towards the waypoint when it is off to one side and the way there is clear at chest
														-- height, in screen terms, whichever way the crab faces: with lift alone the crab came down where it took off.
														-- (Whether or not the chest ray is clear that way: on the slope it is climbing it never is, and gated on it the crab
														-- rose straight up and came down where it took off.)
														if math.abs(CurrDist.X) > Owner.Height * 0.15 then
															AI.jetLeanX = CurrDist.X > 0 and 1 or -1;
														end
													end
													-- The nozzle leans a few degrees forward whenever the aim is level, so a climb aimed level drifts, and keeps gathering speed, the
													-- way it faces. Aimed straight up it lifts and nothing else: that's the climb, with the lean kept for the drift towards the waypoint.
													nextAimAngle = nextLatMove == Actor.LAT_STILL and math.pi * 0.5 or 0;
												end
											else
												AI.jetClimb = false;
												if Owner.Head and AI.flying and not WallAhead and not hopping and Owner.Jetpack.JetpackType == AEJetpack.Standard then
													-- In the air: steered by SharedBehaviors.FlightControl, not re-chosen from four directions every tick.
													nextLatMove, AI.jump, nextAimAngle = SharedBehaviors.FlightControl(AI, Owner, SharedBehaviors.FlightTarget(Owner, Waypoint.Pos), FlightState);
												else
												FlightState = {};
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
													-- (A crab's nozzle is held straight up, or leant on purpose by the lean above; the planner predicts it vertical.)
													local JetAccel = Vector(-jetStrength, 0):RadRotate(Owner.Head and (Owner.RotAngle+1.375*math.pi+Face.facing*0.25) or (Owner.RotAngle + 1.5*math.pi));
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
													if Owner:IsAITracedOn("Pilot") then ConsoleMan:PrintString("AITRACE overhang: stepping " .. (nextLatMove == Actor.LAT_LEFT and "left" or "right")); end
												elseif delta < 1 or aboveWaypoint or walkable or tooHigh or fastEnough then
													if Owner:IsAITracedOn("Pilot") and (Waypoint.Pos.Y - Owner.Pos.Y) < -Owner.Height * 0.25 and not AI.flying and math.random() < 0.1 then
														ConsoleMan:PrintString("AITRACE no jet: delta " .. math.floor(delta) .. " above " .. tostring(aboveWaypoint) .. " walkable " .. tostring(walkable) .. " tooHigh " .. tostring(tooHigh) .. " fast " .. tostring(fastEnough) .. " blocked " .. tostring(Facings[1].blocked) .. " range " .. math.floor(Facings[1].range) .. " wpt dx " .. math.floor(CurrDist.X) .. " dy " .. math.floor(CurrDist.Y) .. " lat " .. tostring(nextLatMove));
													end
													AI.jump = false;
												elseif delta > deltaToJump then
													if Owner:IsAITracedOn("Pilot") and not AI.jump then ConsoleMan:PrintString("AITRACE jet: planner delta " .. math.floor(delta) .. " flying " .. tostring(AI.flying) .. " wpt dy " .. math.floor(Waypoint.Pos.Y - Owner.Pos.Y)); end
													AI.jump = true;
													nextAimAngle = Owner:GetAimAngle(false) * 0.5 + Facings[1].aim * 0.5; -- adjust jetpack nozzle direction
													nextLatMove = Actor.LAT_STILL;

													if not Owner.Head then
														-- A crab's facing doesn't lean its jet: it leans towards the waypoint when that is off to one side.
														if math.abs(CurrDist.X) > Owner.Height * 0.15 then
															AI.jetLeanX = CurrDist.X > 0 and 1 or -1;
														end
													else
														-- The take-off is the planner's; its direction is the flight controller's, towards the point. (Facing for whichever
														-- of the four fixed jet directions scored best, the unit turned and jetted away from where it was going whenever the
														-- backward lean scored a little better.)
														nextLatMove, AI.jump, nextAimAngle = SharedBehaviors.FlightControl(AI, Owner, Waypoint.Pos, FlightState);
														AI.jump = true;
													end
													-- Still stepping off the top of a climb: that keeps the sideways input.
													if ClimbStepX ~= 0 and not ClimbStepTimer:IsPastSimTimeLimit() then
														nextLatMove = ClimbStepX > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
													end
												end
												end -- (not FlightControl)
											end
										end
									end
								end
								end -- (not climbHandled)
							end
						end
					end
				end
			end
		else	-- no waypoint list
			NeedsNewPath = false;
			Climb = nil;
			AI.jetClimb = false;
			if Owner.Vel.Y < 15 then
				AI.jump = false;
			end
			nextLatMove = Actor.LAT_STILL;
			AI.lateralMoveState = Actor.LAT_STILL;

			local Trace = SceneMan:ShortestDistance(Owner.Pos, Owner:GetLastAIWaypoint(), false);
			if Owner:IsAITracedOn("AI") then ConsoleMan:PrintString("AITRACE path: none to follow, asking again from " .. math.floor(Owner.Pos.X) .. "," .. math.floor(Owner.Pos.Y)); end
			-- A fresh path gets its time: the no-line-of-sight timer that asked for it was left run out, so the very next tick asked again, and
			-- a unit whose first waypoint was out of sight (through a hatch under it) asked for the same path every frame and went nowhere.
			NoLOSTimer:Reset();
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

		-- Waiting for a door, in its doorway: still, and no jet from the ground; in the air under it (a hatch), a hover, so the leaves aren't
		-- met on the way up. Standing here is not being stuck. Short of the doorway, the legs take us there (no jet: the shut door reads as a
		-- wall ahead, which the walk code would hop at).
		if doorHold then
			nextLatMove = Actor.LAT_STILL;
			if AI.flying then
				AI.jump = Owner.Vel.Y > 0.5;
			else
				AI.jump = false;
			end
			StuckTimer:Reset();
		elseif doorGoal and not AI.flying then
			local dx = SceneMan:ShortestDistance(Owner.Pos, doorGoal, false).X;
			nextLatMove = dx < -3 and Actor.LAT_LEFT or (dx > 3 and Actor.LAT_RIGHT or Actor.LAT_STILL);
			AI.jump = false;
			StuckTimer:Reset();
		end
		-- Down a ladder: a waypoint well below, nearly straight down, and a ladder here. The ladder catches a slow fall and holds it, so a
		-- unit that dropped down a laddered hatch was stopped half way; it is climbed down instead, aimed down with down pressed.
		if not Climb and Waypoint and CurrDist and Owner.Head and CurrDist.Y > Owner.Height * 0.3 and math.abs(CurrDist.X) < Owner.Height * 0.4 and SharedBehaviors.LadderAt(Owner.Pos, Owner.Height * 0.2, Owner.Height * 0.3) then
			AI.ladderDown = true;
			AI.jump = false;
			nextLatMove = Actor.LAT_STILL;
			nextAimAngle = -math.pi * 0.5;
			StuckTimer:Reset();
		end
		-- A drop straight down from where we stand (a drop step, or a walk or crawl whose point is well below), and floor still under the feet: the route's point is a node's middle, which can sit on
		-- the very edge of the slab beside the hole, and a unit counted that as reached and stood on the lip all minute. A step towards the
		-- side where the floor falls away.
		if not Climb and not AI.ladderDown and not AI.flying and Waypoint and CurrDist and (Waypoint.Kind == 3 or Waypoint.Kind == 0 or Waypoint.Kind == 1) and CurrDist.Y > Owner.Height * 0.3 and math.abs(CurrDist.X) < Owner.Height * 0.3 and math.abs(Owner.Vel.Y) < 1 then
			local function FloorAt(dx)
				return SceneMan:CastStrengthRay(Vector(Owner.Pos.X + dx, Owner.Pos.Y), Vector(0, Owner.Height * 0.75), 5, Vector(), 2, rte.grassID, true);
			end
			-- (Floor under either side of the body counts: a unit wedged on the hole's corner had its middle over the hole already, and stood
			-- there with nothing under Pos.)
			local side = Owner.Height * 0.12;
			if FloorAt(0) or FloorAt(-side) or FloorAt(side) then
				local reach = Owner.Height * 0.25;
				local towards = CurrDist.X < 0 and -1 or 1;
				local holeSide = nil;
				if not FloorAt(towards * reach) then
					holeSide = towards;
				elseif not FloorAt(-towards * reach) then
					holeSide = -towards;
				end
				if holeSide then
					nextLatMove = holeSide < 0 and Actor.LAT_LEFT or Actor.LAT_RIGHT;
					if Owner:IsAITracedOn("Pilot") and math.random() < 0.1 then ConsoleMan:PrintString("AITRACE drop: floor under the feet, stepping " .. (holeSide < 0 and "left" or "right") .. " to the hole"); end
				end
			end
		end
		-- In the way of an open door's piece: on, whatever else stopped the legs.
		if doorPass and not AI.flying and nextLatMove == Actor.LAT_STILL then
			nextLatMove = (CurrDist and CurrDist.X < 0) and Actor.LAT_LEFT or Actor.LAT_RIGHT;
			StuckTimer:Reset();
		end

		-- A governor on flight, whatever else was decided, and even while the tank is left to refill (which skips everything above). Falling
		-- fast with the ground coming up inside the next half second, the jet is lit straight up to take the speed off; going fast sideways, the
		-- nozzle is leant against it (the move keys away from the way we face flip the lean). Units flying over a hill used to come down far
		-- past it, hard enough to die.
		if Owner.Jetpack then
			local fuel = Owner.Jetpack.JetTimeLeft >= AI.minBurstTime;
			local fastSideways = math.abs(Owner.Vel.X) > 8;
			-- Falling onto ground that the jet couldn't stop us short of: how far the stop takes comes from the body's thrust, not a guess at a
			-- half second's fall.
			local fallingOnGround = false;
			-- (Not for a fall the body can take anyway, which a stop a few pixels long says it is; and not while digging down a shaft.)
			local digging = Waypoint ~= nil and Waypoint.Kind == 4;
			if Owner.Vel.Y > 3 and not digging then
				local jet = SharedBehaviors.JetNumbers(AI, Owner);
				local stop = jet.stopDistance(Owner.Vel.Y);
				if stop > Owner.Height * 0.3 and stop < 4000 then
					local drop = Vector(0, stop * 1.3 + Owner.Height * 0.3);
					fallingOnGround = SceneMan:CastObstacleRay(Owner.Pos + Vector(0, Owner.Height * 0.2), drop, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 4) >= 0;
				end
			end
			if fastSideways and (AI.flying or Owner.Vel.Y > 6 or AI.jump) then
				-- The lean against the speed is set whether or not there's fuel for more: a jet already lit on the last of the tank was
				-- still leaning forward, and took a unit to thirty metres a second.
				if Owner:IsAITracedOn("Pilot") and not AI.jump then ConsoleMan:PrintString("AITRACE jet: sideways brake at " .. math.floor(Owner.Vel.X * 10) / 10); end
				nextAimAngle = 0;
				nextLatMove = Owner.Vel.X > 0 and Actor.LAT_LEFT or Actor.LAT_RIGHT;
				AI.jetLeanX = Owner.Vel.X > 0 and -1 or 1; -- A crab's nozzle leans against the speed the same way.
				if fuel then
					AI.jump = true;
				end
			elseif fallingOnGround and fuel then
				if Owner:IsAITracedOn("Pilot") and not AI.jump then ConsoleMan:PrintString("AITRACE jet: landing brake at " .. math.floor(Owner.Vel.Y * 10) / 10); end
				AI.jump = true;
				nextAimAngle = math.pi * 0.5;
				-- Leant a little towards the landing when that is off to one side: the brake is the last of the jet before the ground, and a
				-- unit that came down a long drop with the walk speed it stepped off with landed sixty pixels past its point, outside the bunker.
				if Waypoint and CurrDist and math.abs(CurrDist.X) > Owner.Height * 0.15 then
					nextLatMove = CurrDist.X > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
					nextAimAngle = 0;
					AI.jetLeanX = CurrDist.X > 0 and 1 or -1;
				end
			end
		end

		-- No jet while lying down: the nozzle turns with the body, and a prone unit's jet drove it along the ground, faster the more the
		-- governor "braked" it.
		if AI.proneState == AHuman.PRONE then
			AI.jump = false;
		end

		-- A flight from take-off to touchdown is flown as one, whatever the code above made of this tick (see SharedBehaviors.UpdateFlightPlan).
		-- (A plan in flight lives through the ticks with no waypoint, while the route is asked for again: dropped on those, the unit was
		-- handed back mid-climb to the old jump code, eight pixels under the landing, and flown 270 px the other way.)
		if (Waypoint or FlightPlan) and not doorHold and not doorGoal then
			local walk;
			-- (Looked for at most four times a second while there is no flight: the landing search and the walk check ray-test along the
			-- route, and every tick for every unit wanting the jet was hitches in a battle. The last answer about walking holds between.)
			if FlightPlan or FlightThrottleOff or FlightSearchTimer:IsPastSimMS(250) then
				if not FlightPlan then
					FlightSearchTimer:Reset();
				end
				FlightPlan, walk = SharedBehaviors.UpdateFlightPlan(AI, Owner, FlightPlan, AI.jump);
				FlightLastWalk = walk;
			else
				walk = FlightLastWalk;
			end
			-- (Unless the walk is getting nowhere: no 6 px nearer the route's next point in a second and a half, and the jet may be used for
			-- the next two. A step the legs couldn't take at that spot was stood at for the rest of the minute; the stuck timer never ran
			-- out, the legs' jiggling against the step reset it.)
			if walk and Waypoint then
				local gap = SceneMan:ShortestDistance(Owner.Pos, Waypoint.Pos, false).Magnitude;
				if not WalkBestGap or gap < WalkBestGap - 6 then
					WalkBestGap = gap;
					WalkProgressTimer:Reset();
				elseif WalkProgressTimer:IsPastSimMS(1500) then
					WalkJetTimer:Reset();
					WalkProgressTimer:Reset();
					WalkBestGap = gap;
				end
				if not WalkJetTimer:IsPastSimMS(2000) then
					walk = false;
				end
			elseif not walk then
				WalkBestGap = nil;
			end
			if walk then
				AI.jump = false;
				if Owner:IsAITracedOn("Pilot") and math.random() < 0.05 then ConsoleMan:PrintString("AITRACE flight: walkable, no jet"); end
			end
		else
			FlightPlan = nil;
		end
		AI.pilotFlight = false;
		if FlightPlan and SharedBehaviors.NavDebugLevel() >= 2 then
			-- The navigation debug overlay: the flight's landing, and a line to it.
			PrimitiveMan:DrawCirclePrimitive(FlightPlan.landing.pos, 6, 254);
			PrimitiveMan:DrawLinePrimitive(FlightPlan.landing.pos + Vector(-10, 0), FlightPlan.landing.pos + Vector(10, 0), 254);
			PrimitiveMan:DrawLinePrimitive(Owner.Pos, FlightPlan.landing.pos, 254);
		end
		if FlightPlan then
			if EnginePilot and SharedBehaviors.HasEnginePilot(Owner) then
				-- Flown by the engine (AHuman::PilotFlight): predicted a second ahead on the jet's learned push, the nozzle leant by the stick.
				local Command = Owner:PilotFlight(FlightPlan.landing.pos, FlightPlan.landing.floorY);
				AI.jump = Command.Y > 0.5;
				AI.jetStick = Command.X;
				AI.pilotFlight = true;
				nextLatMove = Actor.LAT_STILL;
			else
				nextLatMove, AI.jump, nextAimAngle = SharedBehaviors.FlightControl(AI, Owner, FlightPlan.landing.pos, FlightPlan.state);
			end
			AI.jetClimb = false;
			AI.jetSteady = true;
			Climb = nil;
			StuckTimer:Reset();
		end

		-- movement commands
		if (AI.Target and AI.BehaviorName ~= "AttackTarget" and not AI.PickupHD) or (Owner.AIMode ~= Actor.AIMODE_SQUAD and (AI.BehaviorName == "ShootArea" or AI.BehaviorName == "FaceAlarm")) then
			-- An enemy in sight. On a move order, with an aggressive script, or closing in on one out of reach, the waypoint is still
			-- pursued and the shooting rules aim and fire on the way; otherwise the legs stop and the shooting rules have them.
			if SharedBehaviors.FightsOnTheMove(AI, Owner) then
				AI.lateralMoveState = nextLatMove;
			else
				AI.lateralMoveState = Actor.LAT_STILL;
				if not AI.flying then
					AI.jump = false;
				end
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