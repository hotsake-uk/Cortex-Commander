
HumanBehaviors = {};

-- spot targets with the engine's scan (see SharedBehaviors.ScanForTargets): a wide view about the facing, and down the aim when aiming.
-- Brains, snipers, bosses and top-skill (Good or Unfair) units no longer see every enemy on a screen: they look wider and with more rays.
function HumanBehaviors.ScanTargets(AI, Owner, Skill)
	if AI.deviceState == AHuman.AIMING then
		AI.Ctrl:SetState(Controller.AIM_SHARP, true); -- the scan's long, narrow look down the aim
	end
	local keen = Skill >= Activity.GOODSKILL or Owner:HasObjectInGroup("Brains") or Owner:HasObjectInGroup("Actors - Snipers") or Owner:HasObjectInGroup("Actors - Boss");
	local fov = (keen and 180 or 120) * (0.6 + Skill / 250);
	local budget = (keen and 4 or 2) + (AI.Target and 1 or 0);
	return SharedBehaviors.ScanForTargets(AI, Owner, Skill, fov, budget);
end

-- spot targets by casting a ray in a random direction
function HumanBehaviors.LookForTargets(AI, Owner, Skill)
	local viewAngDeg = RangeRand(50, 120) * Owner.Perceptiveness * (0.5 + Skill/200);
	if AI.deviceState == AHuman.AIMING then
		AI.Ctrl:SetState(Controller.AIM_SHARP, true); -- reinforce sharp aim controller state to enable SharpLength in LookForMOs
		viewAngDeg = 15 * Owner.Perceptiveness + (Skill/10);
	end

	local FoundMO = Owner:LookForMOs(viewAngDeg, rte.grassID, false);
	if FoundMO then
		local HitPoint = SceneMan:GetLastRayHitPos();
		-- AI-teams ignore the fog
		if not AI.isPlayerOwned or not SceneMan:IsUnseen(HitPoint.X, HitPoint.Y, Owner.Team) or not SceneMan:IsUnseen(FoundMO.Pos.X, FoundMO.Pos.Y, Owner.Team) then
			return FoundMO, HitPoint;
		end
	end
end

-- brains and snipers spot targets by casting rays at all nearby enemy actors
function HumanBehaviors.CheckEnemyLOS(AI, Owner, Skill)
	if not AI.Enemies then	-- add all enemy actors on our screen to a table and check LOS to them, one per frame
		AI.Enemies = {};
		local box = Box();
		local skillFactor = (0.4 + Skill * 0.005);
		box.Corner = Vector(Owner.ViewPoint.X - (FrameMan.PlayerScreenWidth/2) * skillFactor, Owner.ViewPoint.Y - (FrameMan.PlayerScreenHeight/2) * skillFactor);
		box.Width = FrameMan.PlayerScreenWidth * skillFactor;
		box.Height = FrameMan.PlayerScreenHeight * skillFactor;
		for Act in MovableMan:GetMOsInBox(box, Owner.Team, true) do
			if IsActor(Act) and not AI.isPlayerOwned or not SceneMan:IsUnseen(Act.Pos.X, Act.Pos.Y, Owner.Team) then	-- AI-teams ignore the fog
				table.insert(AI.Enemies, Act);
			end
		end

		return HumanBehaviors.LookForTargets(AI, Owner, Skill); -- cast rays like normal actors occasionally
	else
		local Enemy = table.remove(AI.Enemies);
		if Enemy then
			if MovableMan:ValidMO(Enemy) then
				local Origin;
				if Owner.EquippedItem and AI.deviceState == AHuman.AIMING then
					Origin = Owner.EquippedItem.Pos;
				else
					Origin = Owner.EyePos;
				end

				local LookTarget;
				if Enemy.ClassName == "ADoor" then
					-- TO-DO: use explosive weapons on doors?

					local Door = ToADoor(Enemy).Door;
					if Door and Door:IsAttached() then
						LookTarget = Door.Pos;
					else
						return HumanBehaviors.LookForTargets(AI, Owner, Skill); -- this door is destroyed, cast rays like normal actors
					end
				else
					LookTarget = Enemy.Pos;
				end

				-- cast at body
				if not AI.isPlayerOwned or not SceneMan:IsUnseen(LookTarget.X, LookTarget.Y, Owner.Team) then	-- AI-teams ignore the fog
					local Dist = SceneMan:ShortestDistance(Owner.ViewPoint, LookTarget, false);
					if (math.abs(Dist.X) - Enemy.Radius < FrameMan.PlayerScreenWidth * 0.52) and (math.abs(Dist.Y) - Enemy.Radius < FrameMan.PlayerScreenHeight * 0.52) then
						local Trace = SceneMan:ShortestDistance(Origin, LookTarget, false);
						local ID = SceneMan:CastMORay(Origin, Trace, Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, false, 5);
						if ID ~= rte.NoMOID then
							local MO = MovableMan:GetMOFromID(ID);
							if MO and ID ~= MO.RootID then
								MO = MovableMan:GetMOFromID(MO.RootID);
							end

							return MO, SceneMan:GetLastRayHitPos();
						end
					end
				end

				-- no LOS to the body, cast at head
				if Enemy.EyePos and (not AI.isPlayerOwned or not SceneMan:IsUnseen(Enemy.EyePos.X, Enemy.EyePos.Y, Owner.Team)) then	-- AI-teams ignore the fog
					local Dist = SceneMan:ShortestDistance(Owner.ViewPoint, Enemy.EyePos, false);
					if (math.abs(Dist.X) < FrameMan.PlayerScreenWidth * 0.52) and (math.abs(Dist.Y) < FrameMan.PlayerScreenHeight * 0.52) then
						local Trace = SceneMan:ShortestDistance(Origin, Enemy.EyePos, false);
						local ID = SceneMan:CastMORay(Origin, Trace, Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, false, 5);
						if ID ~= rte.NoMOID then
							local MO = MovableMan:GetMOFromID(ID);
							if MO and ID ~= MO.RootID then
								MO = MovableMan:GetMOFromID(MO.RootID);
							end

							return MO, SceneMan:GetLastRayHitPos();
						end
					end
				end
			end
		else
			AI.Enemies = nil;
			return HumanBehaviors.LookForTargets(AI, Owner, Skill); -- cast rays like normal actors occasionally
		end
	end
end

function HumanBehaviors.GetGrenadeAngle(AimPoint, TargetVel, StartPos, muzVel)
	local Dist = SceneMan:ShortestDistance(StartPos, AimPoint, false);

	-- compensate for gravity if the point we are trying to hit is more than 2m away
	if Dist:MagnitudeIsGreaterThan(40) then
		local timeToTarget = Dist.Magnitude / muzVel;

		-- lead the target if target speed and projectile TTT is above the threshold
		if (timeToTarget * TargetVel.Magnitude) > 0.5 then
			AimPoint = AimPoint + TargetVel * timeToTarget;
			Dist = SceneMan:ShortestDistance(StartPos, AimPoint, false);
		end

		Dist = Dist / GetPPM(); -- convert from pixels to meters
		local velSqr = math.pow(muzVel, 2);
		local gravity = SceneMan.GlobalAcc.Y * 0.67; -- underestimate gravity
		local root = math.sqrt(velSqr*velSqr - gravity*(gravity*Dist.X*Dist.X+2*-Dist.Y*velSqr));

		if root ~= root then
			return nil; -- no solution exists if the root is NaN
		end

		return math.atan2(velSqr-root, gravity*Dist.X);
	end

	return Dist.AbsRadAngle;
end

-- A grenade's arc to a point (AC-5): the angle to throw at and how long it flies, for the throw speed in m/s, the flat arc or (high) the
-- lob, under the same underestimated gravity as GetGrenadeAngle. @return angle (radians, for Vector(1,0):RadRotate) and seconds, or nil.
function HumanBehaviors.GrenadeArc(StartPos, AimPoint, speed, high)
	local Dist = SceneMan:ShortestDistance(StartPos, AimPoint, false) / GetPPM();
	local gravity = SceneMan.GlobalAcc.Y * 0.67;
	local velSqr = speed * speed;
	local disc = velSqr * velSqr - gravity * (gravity * Dist.X * Dist.X + 2 * -Dist.Y * velSqr);
	if disc < 0 or math.abs(Dist.X) < 0.1 or speed <= 0 then
		return nil;
	end
	local root = math.sqrt(disc);
	local angle = math.atan2(high and (velSqr + root) or (velSqr - root), gravity * Dist.X);
	local seconds = Dist.X / (speed * math.cos(angle));
	if seconds ~= seconds or seconds <= 0 or seconds > 6 then
		return nil;
	end
	return angle, seconds;
end

-- Whether a grenade thrown on an arc gets to its end without hitting the ground on the way: the arc in short straight pieces, each checked
-- against the terrain (not units). The last eighth is not checked: the grenade landing there is the point.
function HumanBehaviors.ArcIsClear(StartPos, angle, speed, seconds)
	local gravity = SceneMan.GlobalAcc.Y * 0.67;
	local ppm = GetPPM();
	local vx = speed * math.cos(angle);
	local vy = -speed * math.sin(angle);
	local steps = math.max(6, math.min(40, math.ceil(seconds / 0.05)));
	local From = Vector(StartPos.X, StartPos.Y);
	for i = 1, math.floor(steps * 7 / 8) do
		local t = seconds * i / steps;
		local To = StartPos + Vector(vx * t, vy * t + 0.5 * gravity * t * t) * ppm;
		if SceneMan:CastStrengthSumRay(From, To, 2, rte.grassID) > 30 then
			return false;
		end
		From = To;
	end
	return true;
end

-- How to throw a grenade at a point (AC-5): the flat arc where it is clear, else the lob over what is in the way (overCover: the lob first),
-- at full or at middling strength, led onto a moving target by its flight time. @return angle, the throw speed, seconds of flight and
-- whether it takes full strength; or nil where no arc gets there.
function HumanBehaviors.PlanThrow(Grenade, AimPoint, TargetVel, overCover)
	local maxVel = Grenade:GetCalculatedMaxThrowVelIncludingArmThrowStrength();
	local minVel = Grenade.MinThrowVel;
	if minVel == 0 then
		minVel = maxVel * 0.2;
	end
	local Start = Grenade.MuzzlePos;
	local arcs = overCover and {true, false} or {false, true};
	for _, high in ipairs(arcs) do
		for _, speed in ipairs({(maxVel + minVel) * 0.5, maxVel}) do
			local angle, seconds = HumanBehaviors.GrenadeArc(Start, AimPoint, speed, high);
			-- Led onto a runner: where it will be when the grenade gets there (twice over, as the flight time changes with the point).
			if angle and TargetVel and TargetVel.Magnitude > 1 then
				for _ = 1, 2 do
					local Lead = AimPoint + TargetVel * GetPPM() * seconds * 0.8;
					local leadAngle, leadSeconds = HumanBehaviors.GrenadeArc(Start, Lead, speed, high);
					if not leadAngle then
						break;
					end
					angle, seconds = leadAngle, leadSeconds;
				end
			end
			if angle and HumanBehaviors.ArcIsClear(Start, angle, speed, seconds) then
				return angle, speed, seconds, speed >= maxVel;
			end
		end
	end
	return nil;
end

-- How long to hold a grenade before letting go (AC-5): the arm's wind-up, and for a fused grenade thrown at full strength by a good
-- enough AI, "cooked": held so it goes off about as it gets there, in the air over the target, never closer than 350 ms to going off in hand.
function HumanBehaviors.HoldTime(AI, Owner, Grenade, seconds, fullStrength)
	local prep = Owner.ThrowPrepTime;
	if not fullStrength then
		return prep * RangeRand(0.45, 0.55);
	end
	local hold = prep * RangeRand(0.9, 1.1);
	local fuse = Grenade.TriggerDelay or 0;
	if fuse > 0 and not Grenade.ActivatesWhenReleased and (AI.skill or 50) >= 50 and math.random() * 100 < AI.skill then
		local cooked = fuse - seconds * 1000 - 120;
		if cooked > hold then
			hold = math.min(cooked, fuse - 350);
			SharedBehaviors.Trace(Owner, "grenade: cooked " .. math.floor(hold) .. " ms");
		end
	end
	return hold;
end

-- Lobbing a grenade where an enemy was last seen behind cover (AC-5): up over the wall or ridge onto the spot (AI.LobPos), then back to the
-- gun. Started by the AI's update for a unit that has lost sight of its enemy (see LobUpdate).
function HumanBehaviors.LobAt(AI, Owner, Abort)
	local Spot = AI.LobPos;
	AI.LobPos = nil;
	if not Spot or not Owner:EquipDeviceInGroup("Bombs - Grenades", true) then
		return true;
	end
	local Timer0 = Timer();
	local aim, hold;
	while true do
		if not Owner.ThrowableIsReady or Timer0:IsPastSimMS(3000) then
			break;
		end
		if not aim then
			local Grenade = ToThrownDevice(Owner.EquippedItem);
			local _, seconds, full;
			aim, _, seconds, full = HumanBehaviors.PlanThrow(Grenade, Spot, nil, true);
			if not aim then
				SharedBehaviors.Trace(Owner, "grenade: no lob over to it");
				break;
			end
			aim = aim - Owner.RotAngle;
			hold = HumanBehaviors.HoldTime(AI, Owner, Grenade, seconds, full);
			Timer0:Reset();
			SharedBehaviors.Trace(Owner, "grenade: lob over cover");
		end
		AI.Ctrl.AnalogAim = Vector(1, 0):RadRotate(aim);
		if not Timer0:IsPastSimMS(hold) then
			AI.fire = true;
		else
			AI.fire = false;
			local _ai, _ownr, _abrt = coroutine.yield();
			if _abrt then return true end
			break;
		end
		local _ai, _ownr, _abrt = coroutine.yield();
		if _abrt then return true end
	end
	AI.fire = false;
	Owner:EquipFirearm(true);
	return true;
end

-- Smoke to cross open ground (AC-5): a unit on the move under fire that carries a smoke grenade throws it between itself and the enemy,
-- then goes on. Started by the AI's update (see SmokeUpdate).
function HumanBehaviors.ThrowSmoke(AI, Owner, Abort)
	local Spot = AI.SmokePos;
	AI.SmokePos = nil;
	if not Spot or not Owner:EquipNamedDevice("Smoke Grenade", true) then
		return true;
	end
	local Timer0 = Timer();
	local aim, hold;
	while true do
		if not Owner.ThrowableIsReady or Timer0:IsPastSimMS(3000) then
			break;
		end
		if not aim then
			local Grenade = ToThrownDevice(Owner.EquippedItem);
			aim = HumanBehaviors.PlanThrow(Grenade, Spot, nil, false);
			if not aim then
				break;
			end
			aim = aim - Owner.RotAngle;
			hold = Owner.ThrowPrepTime * RangeRand(0.45, 0.55);
			Timer0:Reset();
			SharedBehaviors.Trace(Owner, "grenade: smoke for the crossing");
		end
		AI.Ctrl.AnalogAim = Vector(1, 0):RadRotate(aim);
		if not Timer0:IsPastSimMS(hold) then
			AI.fire = true;
		else
			AI.fire = false;
			local _ai, _ownr, _abrt = coroutine.yield();
			if _abrt then return true end
			break;
		end
		local _ai, _ownr, _abrt = coroutine.yield();
		if _abrt then return true end
	end
	AI.fire = false;
	Owner:EquipFirearm(true);
	return true;
end

-- When to lob a grenade over cover (AC-5), every second: an AI that lost sight of its enemy in the last four seconds, where it was is
-- 80 to 500 px off and out of sight, and the unit has a grenade; at most every eight seconds. Better AI does it more often.
function HumanBehaviors.LobUpdate(AI, Owner)
	if AI.Target or AI.NextBehavior or not AI.OldTargetPos or AI.flying or Owner.AIMode == Actor.AIMODE_SQUAD or not SharedBehaviors.RuleLetsFire(AI, Owner) then
		return;
	end
	AI.LobCheckTimer = AI.LobCheckTimer or Timer();
	if not AI.LobCheckTimer:IsPastSimMS(1000) then
		return;
	end
	AI.LobCheckTimer:Reset();
	if (AI.LobRestTimer and not AI.LobRestTimer:IsPastSimMS(8000)) or not AI.TargetLostTimer or AI.TargetLostTimer:IsPastSimMS(4000) then
		return;
	end
	if not Owner:HasObjectInGroup("Bombs - Grenades") or math.random() * 100 > (AI.skill or 50) then
		return;
	end
	local Dist = SceneMan:ShortestDistance(Owner.EyePos, AI.OldTargetPos, false);
	if Dist:MagnitudeIsLessThan(80) or Dist:MagnitudeIsGreaterThan(500) or SharedBehaviors.CanSee(Owner.EyePos, AI.OldTargetPos) then
		return;
	end
	AI.LobRestTimer = Timer();
	AI.LobPos = Vector(AI.OldTargetPos.X, AI.OldTargetPos.Y);
	AI.NextBehavior = coroutine.create(HumanBehaviors.LobAt);
	AI.NextCleanup = nil;
	AI.NextBehaviorName = "LobAt";
end

-- When to throw smoke (AC-5), every second: on a move order, pinned (suppression over 0.3) or hit in the last two seconds, with a smoke
-- grenade and an enemy seen in the last few seconds 100 to 700 px off; at most every twenty seconds. The smoke goes a third of the way to it.
function HumanBehaviors.SmokeUpdate(AI, Owner)
	if AI.NextBehavior or AI.flying or SharedBehaviors.OrderKind(Owner) ~= "move" or not SharedBehaviors.RuleLetsFire(AI, Owner) then
		return;
	end
	AI.SmokeCheckTimer = AI.SmokeCheckTimer or Timer();
	if not AI.SmokeCheckTimer:IsPastSimMS(1000) then
		return;
	end
	AI.SmokeCheckTimer:Reset();
	if AI.SmokeRestTimer and not AI.SmokeRestTimer:IsPastSimMS(20000) then
		return;
	end
	local suppression = SharedBehaviors.Suppression(AI, Owner);
	local underFire = suppression > 0.3 or (AI.HitTimer and not AI.HitTimer:IsPastSimMS(2000));
	local Enemy = AI.Target and MovableMan:ValidMO(AI.Target) and AI.Target.Pos or AI.LastEnemyPos;
	if not underFire or not Enemy or not Owner:HasObject("Smoke Grenade") then
		return;
	end
	local Dist = SceneMan:ShortestDistance(Owner.Pos, Enemy, false);
	if Dist:MagnitudeIsLessThan(100) or Dist:MagnitudeIsGreaterThan(700) then
		return;
	end
	AI.SmokeRestTimer = Timer();
	AI.SmokePos = SceneMan:MovePointToGround(Owner.Pos + Dist / 3, 10, 4);
	AI.NextBehavior = coroutine.create(HumanBehaviors.ThrowSmoke);
	AI.NextCleanup = nil;
	AI.NextBehaviorName = "ThrowSmoke";
end

-- deprecated since B30. make sure we equip our preferred device if we have one. return true if we must run this function again to be sure
function HumanBehaviors.EquipPreferredWeapon(AI, Owner)
	if AI.squadShoot == false then
		if AI.PlayerPreferredHD then
			Owner:EquipNamedDevice(AI.PlayerPreferredHD, true);
		elseif not Owner:EquipDeviceInGroup("Weapons - Primary", true) then
			Owner:EquipDeviceInGroup("Weapons - Secondary", true);
		end
		return false;
	end
end

-- deprecated since B30. make sure we equip a primary weapon if we have one. return true if we must run this function again to be sure
function HumanBehaviors.EquipPrimaryWeapon(AI, Owner)
	Owner:EquipDeviceInGroup("Weapons - Primary", true);
	return false;
end

-- deprecated since B30. make sure we equip a secondary weapon if we have one. return true if we must run this function again to be sure
function HumanBehaviors.EquipSecondaryWeapon(AI, Owner)
	Owner:EquipDeviceInGroup("Weapons - Secondary", true);
	return false;
end

-- in sentry behavior the agent only looks for new enemies, it sometimes sharp aims to increase spotting range
function HumanBehaviors.Sentry(AI, Owner, Abort)
	local sweepUp = true;
	local sweepDone = false;
	local maxAng = math.min(1.4, Owner.AimRange);
	local minAng = -maxAng;
	local aim;

	if AI.PlayerPreferredHD then
		Owner:EquipNamedDevice(AI.PlayerPreferredHD, true);
	elseif not Owner:EquipDeviceInGroup("Weapons - Primary", true) then
		Owner:EquipDeviceInGroup("Weapons - Secondary", true);
	end

	if AI.OldTargetPos then	-- try to reacquire an old target
		local Dist = SceneMan:ShortestDistance(Owner.EyePos, AI.OldTargetPos, false);
		AI.OldTargetPos = nil;
		if (Dist.X < 0 and Owner.HFlipped) or (Dist.X > 0 and not Owner.HFlipped) then	-- we are facing the target
			AI.deviceState = AHuman.AIMING;
			AI.Ctrl.AnalogAim = Dist.Normalized;

			for _ = 1, math.random(20, 30) do
				local _ai, _ownr, _abrt = coroutine.yield(); -- aim here for ~0.25s
				if _abrt then return true end
			end
		end
	elseif not AI.isPlayerOwned and Owner.AIMode ~= Actor.AIMODE_GOTO then -- face the most likely enemy approach direction
		for _ = 1, math.random(5) do	-- wait for a while
			local _ai, _ownr, _abrt = coroutine.yield(); -- aim here for ~0.25s
			if _abrt then return true end
		end

		Owner:ClearMovePath();
		Owner:AddAISceneWaypoint(Vector(Owner.Pos.X, 0));
		Owner:UpdateMovePath();

		-- wait until movepath is updated
		while Owner.IsWaitingOnNewMovePath do
			local _ai, _ownr, _abrt = coroutine.yield();
			if _abrt then return true end
		end

		-- face the direction of the first waypoint
		for WptPos in Owner.MovePath do
			local Dist = SceneMan:ShortestDistance(Owner.Pos, WptPos, false);
			if Dist.X > 5 then
				AI.SentryFacing = false;
				AI.Ctrl.AnalogAim = Dist.Normalized;
			elseif Dist.X < -5 then
				AI.SentryFacing = true;
				AI.Ctrl.AnalogAim = Dist.Normalized;
			end

			break;
		end

		Owner:ClearMovePath();
	end

	-- (Posted facing a way, RC-4, or sent somewhere facing a way, RC-5: that is the way to watch. After the look for the likely way an
	-- enemy comes, which would otherwise turn it to face that.)
	if Owner.OrderPostFacing ~= 0 then
		AI.SentryFacing = Owner.OrderPostFacing < 0;
	end
	if not AI.SentryPos then
		AI.SentryPos = Vector(Owner.Pos.X, Owner.Pos.Y);
	end

	while true do	-- start by looking forward
		aim = Owner:GetAimAngle(false);

		if sweepUp then
			if aim < maxAng/3 then
				AI.Ctrl:SetState(Controller.AIM_UP, false);
				local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
				if _abrt then return true end
				AI.Ctrl:SetState(Controller.AIM_UP, true);
			else
				sweepUp = false;
			end
		else
			if aim > minAng/3 then
				AI.Ctrl:SetState(Controller.AIM_DOWN, false);
				local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
				if _abrt then return true end
				AI.Ctrl:SetState(Controller.AIM_DOWN, true);
			else
				sweepUp = true;
				if sweepDone then
					break;
				else
					sweepDone = true;
				end
			end
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	if AI.SentryFacing ~= nil and Owner.HFlipped ~= AI.SentryFacing then
		Owner.HFlipped = AI.SentryFacing; -- turn to the direction we have been order to guard
		return true; -- restart this behavior
	end

	while true do	-- look down
		aim = Owner:GetAimAngle(false);
		if aim > minAng then
			AI.Ctrl:SetState(Controller.AIM_DOWN, true);
		else
			break;
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	local Hit = Vector();
	local NoObstacle = {};
	local StartPos;
	AI.deviceState = AHuman.AIMING;

	while true do	-- scan the area for obstacles
		aim = Owner:GetAimAngle(false);
		if aim < maxAng then
			AI.Ctrl:SetState(Controller.AIM_UP, true);
		else
			break;
		end

		if Owner:EquipFirearm(false) and Owner.EquippedItem then
			StartPos = ToHeldDevice(Owner.EquippedItem).MuzzlePos;
		else
			StartPos = Owner.EyePos;
		end

		-- save the angle to a table if there is no obstacle
		if not SceneMan:CastStrengthRay(StartPos, Vector(60, 0):RadRotate(Owner:GetAimAngle(true)), 5, Hit, 2, 0, true) then
			table.insert(NoObstacle, aim); -- TODO: don't use a table for this
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	local SharpTimer = Timer();
	local IdleAimTimer = Timer();
	local sharpAimTime = AI.idleAimTime;
	local angDiff = 1;
	AI.deviceState = AHuman.POINTING;

	if #NoObstacle > 1 then	-- only aim where we know there are no obstacles, e.g. out of a gun port
		minAng = NoObstacle[1] * 0.95;
		maxAng = NoObstacle[#NoObstacle] * 0.95;
		angDiff = 1 / math.max(math.abs(maxAng - minAng), 0.1); -- sharp aim longer from a small aiming window
	end

	while true do
		if not Owner:EquipFirearm(false) and not Owner:EquipThrowable(false) then
			break;
		end

		aim = Owner:GetAimAngle(false);

		if IdleAimTimer:IsPastSimMS(AI.idleAimTime) then
			if sweepUp then
				if aim < maxAng then
					if aim < maxAng/5 and aim > minAng/5 and PosRand() > 0.3 then
						AI.Ctrl:SetState(Controller.AIM_UP, false);
					else
						AI.Ctrl:SetState(Controller.AIM_UP, true);
					end
				else
					sweepUp = false;
					IdleAimTimer:Reset();
				end
			else
				if aim > minAng then
					if aim < maxAng/5 and aim > minAng/5 and PosRand() > 0.3 then
						AI.Ctrl:SetState(Controller.AIM_DOWN, false);
					else
						AI.Ctrl:SetState(Controller.AIM_DOWN, true);
					end
				else
					sweepUp = true;
					IdleAimTimer:Reset();
				end
			end
		end

		if SharpTimer:IsPastSimMS(AI.idleAimTime) then
			SharpTimer:Reset();

			-- make sure that we have any preferred weapon equipped
			if AI.PlayerPreferredHD then
				Owner:EquipNamedDevice(AI.PlayerPreferredHD, true);
			elseif not Owner:EquipDeviceInGroup("Weapons - Primary", true) then
				Owner:EquipDeviceInGroup("Weapons - Secondary", true);
			end

			if AI.deviceState == AHuman.AIMING then
				sharpAimTime = RangeRand(AI.idleAimTime * 0.5, AI.idleAimTime * 1.5);
				AI.deviceState = AHuman.POINTING;
			else
				sharpAimTime = RangeRand(AI.idleAimTime * 3, AI.idleAimTime * 6) * angDiff;
				AI.deviceState = AHuman.AIMING;
			end
			if Owner.AIMode ~= Actor.AIMODE_SQUAD then
				if SceneMan:ShortestDistance(Owner.Pos, AI.SentryPos, false):MagnitudeIsGreaterThan(Owner.Height*0.7) then
					AI.SentryPos = SceneMan:MovePointToGround(AI.SentryPos, Owner.Height*0.25, 3);
					Owner:ClearAIWaypoints();
					Owner:AddAISceneWaypoint(AI.SentryPos);
					AI:CreateGoToBehavior(Owner); -- try to return to the sentry pos
					break;
				elseif AI.SentryFacing and Owner.HFlipped ~= AI.SentryFacing then
					Owner.HFlipped = AI.SentryFacing; -- turn to the direction we have been order to guard
					break; -- restart this behavior
				elseif AI.TargetLostTimer:IsPastSimTimeLimit() and math.random() < Owner.Perceptiveness then
					-- turn around occasionally if there is open space behind our back
					local backAreaRay = Vector(-math.random(FrameMan.PlayerScreenWidth/4, FrameMan.PlayerScreenWidth/2) * Owner.FlipFactor, 0):DegRotate(math.random(-25, 25) * Owner.Perceptiveness);
					if not SceneMan:CastStrengthRay(Owner.EyePos, backAreaRay, 10, Vector(), 10, rte.grassID, SceneMan.SceneWrapsX) then
						Owner.HFlipped = Owner.FlipFactor == 1 and true or false;
					end
				end
			end
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	return true;
end

function HumanBehaviors.GoldDig(AI, Owner, Abort)
	-- make sure our weapon have ammo before we start to dig, just in case we encounter an enemy while digging
	if Owner.EquippedItem and (Owner.FirearmNeedsReload or Owner.FirearmIsEmpty) and Owner.EquippedItem:HasObjectInGroup("Weapons") then
		Owner:ReloadFirearms();

		repeat
			local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
			if _abrt then return true end
		until not Owner.FirearmIsEmpty
	end

	-- select a digger
	if not Owner:EquipDiggingTool(true) then
		return true; -- our digger is gone, abort this behavior
	end

	local aimAngle = 0.45;
	local BestGoldLocation = {X = 0, Y = 0};
	local smallestPenalty = math.huge;

	for aimAngle = 0.4, -3.54, -0.033 do
		local Digger;
		if Owner.EquippedItem then
			Digger = ToHeldDevice(Owner.EquippedItem);
			if not Digger then
				break;
			end
		else
			break;
		end

		local LookVec;
		if aimAngle < -0.8 and aimAngle > -2.4 then
			LookVec = Vector(60,0):RadRotate(aimAngle);
		else	-- search further away horizontally
			LookVec = Vector(180,0):RadRotate(aimAngle);
		end

		AI.Ctrl.AnalogAim = LookVec.Normalized;
		local GoldPos = Vector();
		if SceneMan:CastMaterialRay(Digger.MuzzlePos, LookVec, rte.goldID, GoldPos, 1, true) then
			-- avoid gold close to the edges of the scene
			if GoldPos.Y < SceneMan.SceneHeight - 25 and (SceneMan.SceneWrapsX or (GoldPos.X > 50 and GoldPos.X < SceneMan.SceneWidth - 50)) then
				local Dist = SceneMan:ShortestDistance(Owner.Pos, GoldPos, false); -- prioritize gold close to us
				local str = SceneMan:CastStrengthSumRay(Owner.EyePos, GoldPos, 3, rte.goldID) / 30; -- prioritize gold in soft ground
				local penalty = str + Dist.Magnitude + math.abs(Dist.Y*5);
				local DigArea = SceneMan:ShortestDistance(GoldPos, Owner.EyePos+LookVec, false);
				local digLength = math.min(DigArea.Magnitude, 180); -- sanity check to circumvent infinite loops

				-- prioritize gold located horizontally or below us
				if Dist.Y > -20 then
					penalty = penalty - 5;
				end

				local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
				if _abrt then return true end

				-- prioritize areas with more gold
				DigArea:Normalize();
				for i = 5, digLength, 5 do
					local Step = GoldPos + DigArea * i;
					if Step.X >= SceneMan.SceneWidth then
						if SceneMan.SceneWrapsX then
							Step.X = Step.X - SceneMan.SceneWidth;
						else
							break;
						end
					elseif Step.X < 0 then
						if SceneMan.SceneWrapsX then
							Step.X = SceneMan.SceneWidth - Step.X;
						else
							break;
						end
					end

					if Step.Y > SceneMan.SceneHeight - 50 then
						break;
					end

					if SceneMan:GetTerrMatter(Step.X, Step.Y) == rte.goldID then
						penalty = penalty - 4;
					end
				end

				-- prioritize gold located horizontally relative to us
				if math.abs(Dist.X) > math.abs(Dist.Y) then
					if math.abs(Dist.X) * 0.5 > math.abs(Dist.Y) then
						penalty = penalty - 80;
					else
						penalty = penalty - 40;
					end
				end

				if penalty < smallestPenalty then
					if Dist:MagnitudeIsLessThan(50) then	-- dig to a point behind the gold
						GoldPos = Owner.Pos + Dist:SetMagnitude(55);
					end

					-- make sure there is no metal in our path
					if not SceneMan:CastStrengthRay(Owner.Pos, Dist:SetMagnitude(60), 95, Vector(), 2, rte.grassID, SceneMan.SceneWrapsX) then
						smallestPenalty = penalty + RangeRand(-7, 7);
						BestGoldLocation.X, BestGoldLocation.Y = GoldPos.X, GoldPos.Y;
					end
				end
			end
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	local BestGoldPos = Vector(BestGoldLocation.X, BestGoldLocation.Y);
	if BestGoldPos.Largest == 0 then
		if Owner.Pos.Y < SceneMan.SceneHeight - 50 then	-- don't dig beyond the scene limit
			-- no gold found, so dig down and try again
			local rayLenghtY = math.min(80, SceneMan.SceneHeight-100);
			local rayLenghtX = rayLenghtY * 0.5;
			local Target = Owner.Pos + Vector(rayLenghtX, rayLenghtY);
			local str_r = SceneMan:CastStrengthSumRay(Owner.Pos, Target, 6, rte.goldID);
			local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
			if _abrt then return true end

			Target = Owner.Pos + Vector(-rayLenghtX, rayLenghtY);
			local str_l = SceneMan:CastStrengthSumRay(Owner.Pos, Target, 6, rte.goldID);
			local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
			if _abrt then return true end

			if str_r < str_l then
				BestGoldPos = Owner.Pos + Vector(rayLenghtX, rayLenghtY);
			else
				BestGoldPos = Owner.Pos + Vector(-rayLenghtX, rayLenghtY);
			end
		else
			-- no gold here, and we cannot dig deeper, calculate average horizontal strength
			local rayLenght = 80;
			local Target = Owner.Pos + Vector(rayLenght, -5);
			local Trace = SceneMan:ShortestDistance(Owner.Pos, Target, false);
			local str_r = SceneMan:CastStrengthSumRay(Owner.Pos, Target, 5, rte.goldID);
			local obst_r = SceneMan:CastStrengthRay(Owner.Pos, Trace, 95, Vector(), 2, rte.grassID, SceneMan.SceneWrapsX);
			local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
			if _abrt then return true end

			Target = Owner.Pos + Vector(-rayLenght, -5);
			Trace = SceneMan:ShortestDistance(Owner.Pos, Target, false);
			local str_l = SceneMan:CastStrengthSumRay(Owner.Pos, Target, 5, rte.goldID);
			local obst_l = SceneMan:CastStrengthRay(Owner.Pos, Trace, 95, Vector(), 2, rte.grassID, SceneMan.SceneWrapsX);
			local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
			if _abrt then return true end

			local goLeft;
			if obst_l then
				goLeft = false;
			elseif obst_r then
				goLeft = true;
			else
				goLeft = math.random() > 0.5;

				-- go towards the larger obstacle, unless metal
				if math.abs(str_l - str_r) > 200 then
					if str_r > str_l and not obst_r then
						goLeft = false;
					elseif str_r < str_l and not obst_l then
						goLeft = true;
					end
				end
			end

			if goLeft then
				BestGoldPos = Owner.Pos + Vector(-rayLenght, -5);
			else
				BestGoldPos = Owner.Pos + Vector(rayLenght, -5);
			end
		end
	end

	BestGoldPos.Y = math.min(BestGoldPos.Y, SceneMan.SceneHeight-30);
	Owner:ClearAIWaypoints();
	Owner:AddAISceneWaypoint(BestGoldPos);
	AI:CreateGoToBehavior(Owner);

	return true;
end

-- find a weapon to pick up
function HumanBehaviors.WeaponSearch(AI, Owner, Abort)
	local pickupDiggers = not Owner:HasObjectInGroup("Tools - Diggers");

	local maxSearchDistance;
	if AI.isPlayerOwned then
		maxSearchDistance = 160; -- don't move player actors too far
	else
		maxSearchDistance = FrameMan.PlayerScreenWidth * 0.45;
	end

	if Owner.AIMode == Actor.AIMODE_SENTRY then
		maxSearchDistance = maxSearchDistance * 0.6;
	end
	
	local devices = {};
	local mosSearched = 0;
	for movableObject in MovableMan:GetMOsInRadius(Owner.Pos, maxSearchDistance, -1, true) do
		mosSearched = mosSearched + 1;
		if mosSearched % 30 == 0 then
			local _ai, _ownr, _abrt = coroutine.yield();
			if _abrt then return true end
		end
		
		if MovableMan:ValidMO(movableObject) and IsHeldDevice(movableObject) then
			local device = ToHeldDevice(movableObject);
			if device:IsPickupableBy(Owner) and not device:IsActivated() and device.Vel.Largest < 3 and not SceneMan:IsUnseen(device.Pos.X, device.Pos.Y, Owner.Team) then
				local distanceToDevice = SceneMan:ShortestDistance(Owner.Pos, device.Pos, SceneMan.SceneWrapsX or SceneMan.SceneWrapsY);
				if distanceToDevice:MagnitudeIsGreaterThan(Owner.Radius * 0.75) then
					table.insert(devices, { device = device, distance = distanceToDevice });
				else
					devices = {{ device = device, distance = distanceToDevice }};
					break;
				end
			end
		end
	end
	table.sort(devices, function(device, otherDevice) return device.distance.SqrMagnitude < otherDevice.distance.SqrMagnitude end);
	
	if #devices > 0 then
		local maxPathLength = 36; --TODO when this gets turned into a part of pathing calc, use it that way instead
		if AI.isPlayerOwned then
			maxPathLength = 10;
		end
		
		local searchesRemaining = #devices;
		local devicesToPickUp = {};
		for _, deviceEntry in pairs(devices) do
			local device = deviceEntry.device;
			if MovableMan:ValidMO(device) then
				local pathMultiplier = 1;
				if device:HasObjectInGroup("Weapons - Primary") or device:HasObjectInGroup("Weapons - Heavy") then
					pathMultiplier = 0.4; -- prioritize primary or heavy weapons
				elseif device.ClassName == "TDExplosive" then
					pathMultiplier = 1.4; -- avoid grenades if there are other weapons
				elseif device:IsTool() then
					if pickupDiggers and device:HasObjectInGroup("Tools - Diggers") then
						pathMultiplier = 1.8; -- avoid diggers if there are other weapons
					else
						pathMultiplier = -1; -- disregard non-digger tools
					end
				end

				if pathMultiplier ~= -1 then
					local deviceID = device.UniqueID;
					SceneMan.Scene:CalculatePathAsync(
						function(pathRequest)
							local pathLength = pathRequest.PathLength;
							if pathRequest.Status ~= PathRequest.NoSolution and pathLength < maxPathLength then
								local score = pathLength * pathMultiplier;
								table.insert(devicesToPickUp, {deviceId = deviceID, score = score});
							end
							searchesRemaining = searchesRemaining - 1;
						end, 
						Owner.Pos, device.Pos, Owner.JumpHeight, Owner.DigStrength, Owner.Team
					);
				end
			end
		end
		
		while searchesRemaining > 0 do
			local _ai, _ownr, _abrt = coroutine.yield();
			if _abrt then return true end
		end
		
		AI.PickupHD = nil;
		table.sort(devicesToPickUp, function(A,B) return A.score < B.score end);
		for _, deviceToPickupEntry in ipairs(devicesToPickUp) do
			local device = MovableMan:FindObjectByUniqueID(deviceToPickupEntry.deviceId);
			if MovableMan:ValidMO(device) and device:IsDevice() then
				AI.PickupHD = device;
				break;
			end
		end

		if AI.PickupHD then
			-- where do we move after pick up?
			local prevMoveTarget, prevSceneWaypoint;
			if Owner.MOMoveTarget and MovableMan:ValidMO(Owner.MOMoveTarget) then
				prevMoveTarget = Owner.MOMoveTarget;
				Owner.MOMoveTarget = nil;
			else
				prevSceneWaypoint = SceneMan:MovePointToGround(Owner:GetLastAIWaypoint(), Owner.Height/5, 4); -- last wpt or current pos
			end

			Owner:ClearAIWaypoints();
			Owner:AddAIMOWaypoint(AI.PickupHD);

			if prevMoveTarget then
				Owner:AddAIMOWaypoint(prevMoveTarget);
			elseif prevSceneWaypoint then
				Owner:AddAISceneWaypoint(prevSceneWaypoint);
			end

			if Owner.AIMode == Actor.AIMODE_SENTRY then
				AI.SentryFacing = Owner.HFlipped;
			end

			Owner:UpdateMovePath();

			-- wait until movepath is updated
			while Owner.IsWaitingOnNewMovePath do
				local _ai, _ownr, _abrt = coroutine.yield();
				if _abrt then return true end
			end

			AI:CreateGoToBehavior(Owner);
		end
	end

	return true;
end


-- find a tool to pick up
function HumanBehaviors.ToolSearch(AI, Owner, Abort)
	local maxSearchDistance;
	if Owner.AIMode == Actor.AIMODE_GOLDDIG then
		maxSearchDistance = FrameMan.PlayerScreenWidth * 0.5; -- move up to half a screen when digging
	elseif AI.isPlayerOwned then
		maxSearchDistance = 160; -- don't move player actors too far
	else
		maxSearchDistance = FrameMan.PlayerScreenWidth * 0.3;
	end

	if Owner.AIMode == Actor.AIMODE_SENTRY then
		maxSearchDistance = maxSearchDistance * 0.6;
	end
		
	local devices = {};
	local mosSearched = 0;
	for movableObject in MovableMan:GetMOsInRadius(Owner.Pos, maxSearchDistance, -1, true) do
		mosSearched = mosSearched + 1;
		
		if MovableMan:ValidMO(movableObject) and IsHeldDevice(movableObject) then
			local device = ToHeldDevice(movableObject);
			if device:IsPickupableBy(Owner) and not device:IsActivated() and device.Vel.Largest < 3 and not SceneMan:IsUnseen(device.Pos.X, device.Pos.Y, Owner.Team) and device:HasObjectInGroup("Tools - Diggers") then
				local distanceToDevice = SceneMan:ShortestDistance(Owner.Pos, device.Pos, SceneMan.SceneWrapsX or SceneMan.SceneWrapsY);
				if distanceToDevice:MagnitudeIsGreaterThan(20) then
					table.insert(devices, { device = device, distance = distanceToDevice });
				else
					devices = {{ device = device, distance = distanceToDevice }};
					break;
				end
			end
		end
	end
	table.sort(devices, function(device, otherDevice) return device.distance.SqrMagnitude < otherDevice.distance.SqrMagnitude end);
	
	if #devices > 0 then
		local maxPathLength = 16;
		if Owner.AIMode == Actor.AIMODE_GOLDDIG then
			maxPathLength = 30;
		elseif AI.isPlayerOwned then
			maxPathLength = 5;
		end
		
		local searchesRemaining = #devices;
		local devicesToPickUp = {};
		for _, deviceEntry in pairs(devices) do
			local device = deviceEntry.device;
			if MovableMan:ValidMO(device) then
				local deviceId = device.UniqueID;
				SceneMan.Scene:CalculatePathAsync(
					function(pathRequest)
						local pathLength = pathRequest.PathLength;
						if pathRequest.Status ~= PathRequest.NoSolution and pathLength < maxPathLength then
							table.insert(devicesToPickUp, {deviceId = deviceId, score = pathLength});
						end
						searchesRemaining = searchesRemaining - 1;
					end, 
					Owner.Pos, device.Pos, Owner.JumpHeight, Owner.DigStrength, Owner.Team
				);
			end
		end
		
		while searchesRemaining > 0 do
			local _ai, _ownr, _abrt = coroutine.yield();
			if _abrt then return true end
		end

		AI.PickupHD = nil;
		table.sort(devicesToPickUp, function(A,B) return A.score < B.score end); -- sort the items in order of discounted distance
		for _, deviceToPickupEntry in ipairs(devicesToPickUp) do
			local device = MovableMan:FindObjectByUniqueID(deviceToPickupEntry.deviceId);
			if MovableMan:ValidMO(device) and device:IsDevice() then
				AI.PickupHD = device;
				break;
			end
		end

		if AI.PickupHD then
			-- where do we move after pick up?
			local prevMoveTarget, prevSceneWaypoint;
			if Owner.MOMoveTarget and MovableMan:ValidMO(Owner.MOMoveTarget) then
				prevMoveTarget = Owner.MOMoveTarget;
				Owner.MOMoveTarget = nil;
			else
				prevSceneWaypoint = SceneMan:MovePointToGround(Owner:GetLastAIWaypoint(), Owner.Height/5, 4); -- last wpt or current pos
			end

			Owner:ClearAIWaypoints();
			Owner:AddAIMOWaypoint(AI.PickupHD);

			if Owner.AIMode ~= Actor.AIMODE_GOLDDIG then
				if prevMoveTarget then
					Owner:AddAIMOWaypoint(prevMoveTarget);
				elseif prevSceneWaypoint then
					Owner:AddAISceneWaypoint(prevSceneWaypoint);
				end

				if Owner.AIMode == Actor.AIMODE_SENTRY then
					AI.SentryFacing = Owner.HFlipped;
				end
			end

			Owner:UpdateMovePath();

			-- wait until movepath is updated
			while Owner.IsWaitingOnNewMovePath do
				local _ai, _ownr, _abrt = coroutine.yield();
				if _abrt then return true end
			end

			AI:CreateGoToBehavior(Owner);
		end
	end

	return true;
end

-- go prone if we can shoot from the prone position and return the result
function HumanBehaviors.GoProne(AI, Owner, TargetPos, targetID)
	if (not Owner.Head or AI.proneState == AHuman.PRONE) or (Owner:NumberValueExists("AIDisableProne")) then
		return false;
	end

	-- only go prone if we can see the ground near the target
	local AimPoint = SceneMan:MovePointToGround(TargetPos, 10, 3);
	local ground = Owner.Pos.Y + Owner.Height * 0.25;
	local Dist = SceneMan:ShortestDistance(Owner.Pos, AimPoint, false);
	local PronePos;

	-- check if there is room to go prone here
	if Dist.X > Owner.Height then
		-- to the right
		PronePos = Owner.EyePos + Vector(Owner.Height*0.3, 0);

		local x_pos = Owner.Pos.X + 10;
		for _ = 1, math.ceil(Owner.Height/16) do
			x_pos = x_pos + 7;
			if SceneMan.SceneWrapsX and x_pos >= SceneMan.SceneWidth then
				x_pos = SceneMan.SceneWidth - x_pos;
			end

			if 0 == SceneMan:GetTerrMatter(x_pos, ground) then
				return false;
			end
		end
	elseif Dist.X < -Owner.Height then
		-- to the left
		PronePos = Owner.EyePos + Vector(-Owner.Height*0.3, 0);

		local x_pos = Owner.Pos.X - 10;
		for _ = 1, math.ceil(Owner.Height/16) do
			x_pos = x_pos - 7;
			if SceneMan.SceneWrapsX and x_pos < 0 then
				x_pos = x_pos + SceneMan.SceneWidth;
			end

			if 0 == SceneMan:GetTerrMatter(x_pos, ground) then
				return false;
			end
		end
	else
		return false; -- target is too close
	end

	PronePos = SceneMan:MovePointToGround(PronePos, Owner.Head.Radius+3, 2);
	Dist = SceneMan:ShortestDistance(PronePos, AimPoint, false);

	-- check LOS from the prone position
	--if not SceneMan:CastFindMORay(PronePos, Dist, targetID, Hit, rte.grassID, false, 8) then
	if SceneMan:CastObstacleRay(PronePos, Dist, Vector(), Vector(), targetID, Owner.IgnoresWhichTeam, rte.grassID, 9, true) > -1 then
		return false;
	else
		-- check for obstacles more more carefully
		Dist:CapMagnitude(60);
		if SceneMan:CastObstacleRay(PronePos, Dist, Vector(), Vector(), 0, Owner.IgnoresWhichTeam, rte.grassID, 1, true) > -1 then
			return false;
		end
	end

	-- Down, and a crawl forward towards the target (the engine's motor does both; see SharedBehaviors.Stance and StepTo).
	SharedBehaviors.Stance(AI, Owner, AHuman.PRONE, 2500);
	if not Owner.EquippedBGItem then
		SharedBehaviors.StepTo(AI, Owner, Owner.Pos + Vector(Dist.X > 0 and Owner.Height * 0.6 or -Owner.Height * 0.6, 0), 1500);
	end

	return true;
end

-- open fire on the selected target
function HumanBehaviors.ShootTarget(AI, Owner, Abort)
	if not MovableMan:ValidMO(AI.Target) then
		return true;
	end

	AI.canHitTarget = false;
	AI.TargetLostTimer:SetSimTimeLimitMS(1000);

	local LOSTimer = Timer();
	LOSTimer:SetSimTimeLimitMS(170);

	local ImproveAimTimer = Timer();
	local ShootTimer = Timer();
	local shootDelay = RangeRand(440, 590) * AI.aimSpeed + 150;
	-- Pinned down (AC-3): slower to fire, a worse first aim that settles slower, short bursts over half way, and down behind something
	-- (or crouched, or flat) by how hard it is being hammered. Unfair AI and machines feel none of it (see SharedBehaviors.Suppression).
	local suppression = SharedBehaviors.Suppression(AI, Owner);
	shootDelay = shootDelay + 400 * suppression;
	local BurstTimer = Timer();
	local SuppressedStanceTimer = Timer();
	local AimPoint = AI.Target.Pos + AI.TargetOffset;
	if not AI.flying and AI.Target.Vel.Largest < 4 and HumanBehaviors.GoProne(AI, Owner, AimPoint, AI.Target.ID) then
		shootDelay = shootDelay + 250;
	end

	-- spin up asap
	if Owner.FirearmActivationDelay > 0 then
		shootDelay = math.max(50*AI.aimSpeed, shootDelay-Owner.FirearmActivationDelay);
	end

	local PrjDat;
	local openFire = 0;
	local checkAim = true;
	local TargetAvgVel = Vector(AI.Target.Vel.X, AI.Target.Vel.Y);
	local Dist = SceneMan:ShortestDistance(Owner.Pos, AimPoint, false);

	-- make sure we are facing the right direction
	if Owner.HFlipped then
		if Dist.X > 0 then
			Owner.HFlipped = false;
		end
	elseif Dist.X < 0 then
		Owner.HFlipped = true;
	end

	local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
	if _abrt then return true end

	local distMultiplier = AI.aimSkill * math.max(math.min(0.0035*Dist.Largest, 1.0), 0.01);
	local ErrorOffset = Vector(RangeRand(40, 80)*distMultiplier*(1 + 2*suppression), 0):RadRotate(RangeRand(1, 6));
	local aimTarget = SceneMan:ShortestDistance(Owner.Pos, AimPoint+ErrorOffset, false).AbsRadAngle;
	local f1, f2 = 0.5, 0.5; -- aim noise filter

	while true do
		suppression = SharedBehaviors.Suppression(AI, Owner);
		-- Over 0.8: behind cover if there is any near, else flat on the ground; over 0.5: behind cover, else crouched. Looked at twice a
		-- second, each stance held a little longer than that.
		if suppression > 0.5 and SuppressedStanceTimer:IsPastSimMS(500) and not AI.flying then
			SuppressedStanceTimer:Reset();
			if not HumanBehaviors.TakeCover(AI, Owner, AI.Target and AI.Target.Pos or AimPoint, "suppressed") and AI.lateralMoveState == Actor.LAT_STILL then
				SharedBehaviors.Stance(AI, Owner, suppression > 0.8 and AHuman.PRONE or SharedBehaviors.CROUCHED, 800);
			end
		end
		if not AI.Target or AI.Target:IsDead() then
			AI.Target = nil;

			-- the target is gone, try to find another right away
			local ClosestEnemy = MovableMan:GetClosestEnemyActor(Owner.Team, AimPoint, 200, Vector());
			if ClosestEnemy and not ClosestEnemy:IsDead() then
				if ClosestEnemy.ClassName == "AHuman" then
					ClosestEnemy = ToAHuman(ClosestEnemy);
				elseif ClosestEnemy.ClassName == "ACrab" then
					ClosestEnemy = ToACrab(ClosestEnemy);
				else
					ClosestEnemy = nil;
				end

				if ClosestEnemy then
					-- check if the target is inside our "screen"
					local ViewDist = SceneMan:ShortestDistance(Owner.ViewPoint, ClosestEnemy.Pos, false);
					if (math.abs(ViewDist.X) - ClosestEnemy.Radius < FrameMan.PlayerScreenWidth * 0.5) and (math.abs(ViewDist.Y) - ClosestEnemy.Radius < FrameMan.PlayerScreenHeight * 0.5) then
						if not AI.isPlayerOwned or not SceneMan:IsUnseen(ClosestEnemy.Pos.X, ClosestEnemy.Pos.Y, Owner.Team) then	-- AI-teams ignore the fog
							if SceneMan:CastStrengthSumRay(Owner.EyePos, ClosestEnemy.Pos, 6, rte.grassID) < 120 then
								AI.Target = ClosestEnemy;
								AI.TargetOffset = Vector();
								AimPoint = AimPoint * 0.3 + AI.Target.Pos * 0.7;
							end
						end
					end
				end
			end

			-- no new target found
			if not AI.Target then
				break;
			end
		end

		if Owner.FirearmIsReady then
			-- it is now safe to get the ammo stats since FirearmIsReady
			local Weapon = ToHDFirearm(Owner.EquippedItem);
			if not PrjDat or PrjDat.MagazineName ~= Weapon.Magazine.PresetName then
				PrjDat = SharedBehaviors.GetProjectileData(Owner);

				-- uncomment these to get the range of the weapon
				--ConsoleMan:PrintString(Weapon.PresetName .. " range = " .. PrjDat.rng .. " px");
				--ConsoleMan:PrintString(AI.Target.PresetName .. " range = " .. SceneMan:ShortestDistance(Owner.Pos, AI.Target.Pos, false).Magnitude .. " px");

				-- Aim longer with low capacity weapons
				if ((Weapon.Magazine.Capacity > -1 and Weapon.Magazine.Capacity < 10) or Weapon:HasObjectInGroup("Weapons - Sniper")) and Dist.Largest > 100 then
					-- reduce ErrorOffset and increase shootDelay when the target is further away
					local mag = Dist.Magnitude;
					ErrorOffset = ErrorOffset * Clamp(-0.001*mag+1, 1.0, 0.8);
					shootDelay = shootDelay + Clamp(2*mag-200, 600, 50);
				end
			else
				AimPoint = AI.Target.Pos + AI.TargetOffset + ErrorOffset;

				-- TODO: make low skill AI lead worse
				TargetAvgVel = TargetAvgVel * 0.8 + AI.Target.Vel * 0.2; -- smooth the target's velocity
				-- A fuel barrel by the target is the better shot (AC-12): at it, with no lead.
				local Barrel = SharedBehaviors.BarrelNear(AI, Owner, AI.Target);
				if Barrel then
					AimPoint = Barrel.Pos + ErrorOffset * 0.5;
					TargetAvgVel:Reset();
				end
				Dist = SceneMan:ShortestDistance(Weapon.Pos, AimPoint, false);
				local range = Dist.Magnitude;
				if range < 100 then
					-- move the aim-point towards the center of the target at close ranges
					if range < 50 then
						AI.TargetOffset = AI.TargetOffset * 0.95;
						TargetAvgVel:Reset(); -- high velocity at close range confuse the AI tracking
					else
						TargetAvgVel = TargetAvgVel * 0.5;
					end
				end

				if checkAim then
					checkAim = false; -- only check every second frame

					if range < PrjDat.blast then
						-- it is not safe to fire an explosive projectile at this distance
						aimTarget = Dist.AbsRadAngle;
						AI.canHitTarget = true;
						if Owner.InventorySize > 0 then	-- we have more things in the inventory
							if range < 60 and (Owner:HasObjectInGroup("Tools - Diggers") or Owner:HasObjectInGroup("Weapons - Melee")) then
								AI:CreateHtHBehavior(Owner);
								break;
							elseif Owner:EquipLoadedFirearmInGroup("Any", "Weapons - Explosive", true) then
								PrjDat = nil
								local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
								if _abrt then return true end
								if not Owner.FirearmIsReady then
									break;
								end
							else
								PrjDat.blast = 0; -- no other loaded weapon, ignore the blast radius
							end
						else
							PrjDat.blast = 0; -- no other weapon, ignore the blast radius
						end
					elseif range < PrjDat.rng then
						-- lead the target if target speed and projectile TTT is above the threshold
						local timeToTarget = range / PrjDat.vel;
						if (timeToTarget * TargetAvgVel.Magnitude) > 2 then
							-- ~double this value since we only do this every second update
							if PosRand() > 0.5 then
								timeToTarget = timeToTarget * (2 - RangeRand(0, 0.4) * AI.aimSkill);
							else
								timeToTarget = timeToTarget * (2 + RangeRand(0, 0.4) * AI.aimSkill);
							end

							Dist = SceneMan:ShortestDistance(Weapon.Pos, AimPoint+(Owner.Vel*0.5+TargetAvgVel)*timeToTarget, false);
						end

						aimTarget = HumanBehaviors.GetAngleToHit(PrjDat, Dist);
						if aimTarget then
							AI.canHitTarget = true;
						else
							AI.canHitTarget = false;

							-- the target is too far away: go after it, if the order allows (an attack, or the guard modes; never a move or a defence)
							if SharedBehaviors.MayClose(AI, Owner) then
								if not Owner.MOMoveTarget or not MovableMan:ValidMO(Owner.MOMoveTarget) or Owner.MOMoveTarget.RootID ~= AI.Target.RootID then	-- move towards the target
									local OldWaypoint = SceneMan:MovePointToGround(Owner:GetLastAIWaypoint(), Owner.Height/5, 4); -- move back here later
									Owner:ClearAIWaypoints();
									Owner:AddAIMOWaypoint(AI.Target);
									Owner:AddAISceneWaypoint(OldWaypoint);
									AI:CreateGoToBehavior(Owner);
									AI.proneState = AHuman.NOTPRONE;
								end
								AI.closingIn = true;
							else
								-- TODO: switch weapon properly
								if Weapon:HasObjectInGroup("Weapons - Primary") then
									if Owner:EquipDeviceInGroup("Weapons - Secondary", true) then
										local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
										if _abrt then return true end
										PrjDat = nil;
										if not Owner.FirearmIsReady then
											break;
										end
									end
								elseif Weapon:HasObjectInGroup("Weapons - Secondary") then
									if Owner:EquipDeviceInGroup("Weapons - Primary", true) then
										local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
										if _abrt then return true end
										PrjDat = nil;
										if not Owner.FirearmIsReady then
											break;
										end
									end
								end
							end
						end
					elseif Owner.AIMode ~= Actor.AIMODE_SQUAD and SharedBehaviors.MayClose(AI, Owner) then -- target out of reach; move towards it
						-- check if we are already moving towards an actor
						if not Owner.MOMoveTarget or not MovableMan:ValidMO(Owner.MOMoveTarget) or Owner.MOMoveTarget.RootID ~= AI.Target.RootID then	-- move towards the target
							local OldWaypoint = SceneMan:MovePointToGround(Owner:GetLastAIWaypoint(), Owner.Height/5, 4); -- move back here later
							Owner:ClearAIWaypoints();
							Owner:AddAIMOWaypoint(AI.Target);
							Owner:AddAISceneWaypoint(OldWaypoint);
							AI:CreateGoToBehavior(Owner);
							AI.proneState = AHuman.NOTPRONE;
							AI.canHitTarget = false;
						end
						AI.closingIn = true;
					end
				else
					checkAim = true;

					-- periodically check that we have LOS to the target
					if LOSTimer:IsPastSimTimeLimit() then
						LOSTimer:Reset();
						AI.TargetLostTimer:SetSimTimeLimitMS(700);
						local TargetPoint = AI.Target.Pos + AI.TargetOffset;

						local shotClear = false;
						if (range < Owner.AimDistance + Weapon.SharpLength + FrameMan.PlayerScreenWidth*0.5) and
							(not AI.isPlayerOwned or not SceneMan:IsUnseen(TargetPoint.X, TargetPoint.Y, Owner.Team)) and
							not SceneMan:SmokeBlocksSight(Owner.EyePos, TargetPoint) -- lose track of targets hidden by smoke
						then
							if PrjDat.pen > 0 then
								if SceneMan:CastStrengthSumRay(Weapon.Pos, TargetPoint, 6, rte.grassID) * 5 < PrjDat.pen then
									AI.TargetLostTimer:Reset(); -- we can shoot at the target
									AI.OldTargetPos = Vector(AI.Target.Pos.X, AI.Target.Pos.Y);
									shotClear = true;
								end
							else
								if SceneMan:CastStrengthSumRay(Weapon.Pos, TargetPoint, 6, rte.grassID) < 120 then
									AI.TargetLostTimer:Reset(); -- we can shoot at the target
									AI.OldTargetPos = Vector(AI.Target.Pos.X, AI.Target.Pos.Y);
									shotClear = true;
								end
							end
						end
						-- A target in sight that the shots can't reach (dug in behind a ridge, say) for long enough: somewhere else to shoot it from.
						-- (A shot at the head counts as a shot: a target flat on the ground had its body behind the ground's own bumps.)
						if not shotClear and AI.Target.EyePos and SharedBehaviors.CanSee(Weapon.Pos, AI.Target.EyePos) then
							shotClear = true;
						end
						if AI.FlankSearch then
							-- (A flank's routes asked for: started once they are back.)
							if SharedBehaviors.StartFlank(AI, Owner, TargetPoint, PrjDat and PrjDat.rng or 500) then
								break;
							end
						elseif shotClear then
							AI.ShotBlockedTimer = nil;
						elseif not AI.ShotBlockedTimer then
							AI.ShotBlockedTimer = Timer();
						elseif AI.ShotBlockedTimer:IsPastSimMS(1500) then
							AI.ShotBlockedTimer = nil;
							if SharedBehaviors.StartFlank(AI, Owner, TargetPoint, PrjDat and PrjDat.rng or 500) then
								break;
							end
						end
					end
				end

				if AI.canHitTarget then
					HumanBehaviors.HoldRange(AI, Owner, Weapon, PrjDat, range, Dist);
					if not AI.flying then
						AI.deviceState = AHuman.AIMING;
					end
				end
				-- Just hit and hurt: a moment behind something, then out again.
				if AI.HitTimer and not AI.HitTimer:IsPastSimMS(400) and Owner.Health < Owner.MaxHealth * 0.5 and not AI.Cover then
					HumanBehaviors.TakeCover(AI, Owner, AI.Target.Pos, "hurt");
				elseif AI.Cover and not AI.Cover.Leaving then
					HumanBehaviors.TakeCover(AI, Owner, AI.Target.Pos, AI.Cover.Why);
				end

				-- add some filtered noise to the aim
				local aim = Owner:GetAimAngle(true);
				local sharpLen = SceneMan:ShortestDistance(Owner.Pos, Owner.ViewPoint, false).Magnitude;
				local noise = RangeRand(-30, 30) * AI.aimSpeed * (1 + (150 / sharpLen)) * 0.5;
				f1, f2 = 0.9*f1+noise*0.1, 0.7*f2+noise*0.3;
				noise = f1 + f2 + noise * 0.1;
				aimTarget = (aimTarget or aim) + math.min(math.max(noise/(range+30), -0.12), 0.12);

				if AI.flying then
					aimTarget = aimTarget + RangeRand(-0.05, 0.05);
				end

				local angDiff = aim - aimTarget;
				if angDiff > math.pi then
					angDiff = angDiff - math.pi*2;
				elseif angDiff < -math.pi then
					angDiff = angDiff + math.pi*2;
				end

				local angChange = math.max(math.min(angDiff*(0.1/AI.aimSkill), 0.17/AI.aimSkill), -0.17/AI.aimSkill);
				if (angDiff > 0 and angChange > angDiff) or (angDiff < 0 and angChange < angDiff) then
					angChange = angDiff;
				end
				AI.Ctrl.AnalogAim = Vector(1,0):RadRotate(aim-angChange);

				if PrjDat and ShootTimer:IsPastSimMS(shootDelay) then
					-- reduce the aim point error
					if ImproveAimTimer:IsPastSimMS(50) then
						ImproveAimTimer:Reset();
						ErrorOffset = ErrorOffset * (0.93 + 0.05 * suppression);
					end

					if AI.canHitTarget and angDiff < 0.7 then
						local overlap = AI.Target.Diameter * math.max(AI.aimSkill, 0.4);
						if Weapon.FullAuto then	-- open fire if our aim overlap the target
							if math.abs(angDiff) < math.tanh((overlap*2)/(range+10)) then
								openFire = 5; -- don't stop shooting just because we lose the target for a few frames
							else
								openFire = openFire - 1;
							end
						elseif not AI.fire then	-- open fire if our aim overlap the target
							if math.abs(angDiff) < math.tanh((overlap*1.25)/(range+10)) then
								openFire = 1;
							else
								openFire = 0;
							end
						else
							openFire = openFire - 1; -- release the trigger if semi auto
						end

						-- check for obstacles if the ammo have a blast radius
						if openFire > 0 and PrjDat.blast > 0 then
							if SceneMan:CastObstacleRay(Weapon.MuzzlePos, Weapon:RotateOffset(Vector(50, 0)), Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 2) > -1 then
								-- equip another primary if possible
								if Owner:EquipLoadedFirearmInGroup("Any", "Weapons - Explosive", true) then
									PrjDat = nil;
									openFire = 0;
									local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
									if _abrt then return true end
									if not Owner.FirearmIsReady then
										break;
									end
								else
									PrjDat.blast = 0;
								end
							end
						end
					else
						openFire = openFire - 1;
					end
				else
					-- reduce the aim point error
					if ImproveAimTimer:IsPastSimMS(50) then
						ImproveAimTimer:Reset();
						ErrorOffset = ErrorOffset * 0.97;
					end

					openFire = 0;
				end

				-- Bursts that fit the range (AC-8, see SharedBehaviors.BurstPattern): held down close in, shorter further out, single
				-- taps where the weapon's spread is far wider than the target, and short bursts when pinned down.
				local burstOn, burstOff;
				if Weapon and Weapon.FullAuto then
					burstOn, burstOff = SharedBehaviors.BurstPattern(Weapon, range, AI.Target.Radius, suppression);
				end
				if openFire > 0 and burstOn and BurstTimer:IsPastSimMS(burstOn) then
					if BurstTimer:IsPastSimMS(burstOn + burstOff) then
						BurstTimer:Reset();
					else
						openFire = 0;
					end
				elseif openFire <= 0 then
					BurstTimer:Reset();
				end

				if openFire > 0 then
					AI.fire = true;
				else
					AI.fire = false;
				end
			end
		else
			if Owner.EquippedItem and ToHeldDevice(Owner.EquippedItem):IsReloading() then
				ShootTimer:Reset();
				HumanBehaviors.StopRangeStep(AI, Owner);
				AI.Ctrl.AnalogAim = SceneMan:ShortestDistance(Owner.Pos, AI.Target.Pos, false).Normalized;
				-- Behind something for the reload, if there's something to be behind a few steps away; else flat on the ground.
				if not HumanBehaviors.TakeCover(AI, Owner, AI.Target.Pos, "reload") and AI.lateralMoveState == Actor.LAT_STILL then
					SharedBehaviors.Stance(AI, Owner, AHuman.PRONE, 1500);
				end
			elseif Owner:EquipFirearm(true) then
				local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame, just in case the magazine is replenished by another script
				if _abrt then return true end
				AI.TargetLostTimer:SetSimTimeLimitMS(1400);

				if Owner.FirearmIsEmpty then
					AI.deviceState = AHuman.POINTING;
					AI.fire = false;
					openFire = 0;

					if AI.Target then
						-- equip another primary if possible
						if Owner:EquipLoadedFirearmInGroup("Weapons - Primary", "None", true) then
							PrjDat = nil;
						else
							-- select a secondary instead of reloading if the target is within half a screen; being shot at (AC-8), within
							-- most of a screen: a pistol now beats a rifle after a reload in the open
							local reach = SharedBehaviors.UnderFire(AI, Owner) and 0.8 or 0.5;
							if Dist.Largest < (FrameMan.PlayerScreenWidth * reach + AI.Target.Radius + Owner.AimDistance) then
								-- select a primary if we have an empty secondary equipped
								if Owner:EquipLoadedFirearmInGroup("Weapons - Secondary", "None", true) then
									PrjDat = nil;
								elseif Owner:EquipDeviceInGroup("Weapons - Primary", true) then
									PrjDat = nil;
								end
							end
						end

						local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
						if _abrt then return true end
					end

					-- we might have a different weapon equipped now check if FirearmIsEmpty again
					if Owner.FirearmIsEmpty then
						-- TODO: check if ducking is appropriate while reloading (when we can make the actor stand up reliably)
						Owner:ReloadFirearms();

						-- increase the TargetLostTimer limit so we don't end this behavior before the reload is finished
						if Owner.EquippedItem and IsHDFirearm(Owner.EquippedItem) then
							AI.TargetLostTimer:SetSimTimeLimitMS(ToHDFirearm(Owner.EquippedItem).ReloadTime+500);
						end
					end

					distMultiplier = AI.aimSkill * math.max(math.min(0.0035*Dist.Largest, 1.0), 0.01);
					ErrorOffset = Vector(RangeRand(25, 40)*distMultiplier, 0):RadRotate(RangeRand(1, 6));
					shootDelay = RangeRand(220, 330) * AI.aimSpeed + 50;
					if Owner.FirearmActivationDelay > 0 then	-- Spin up asap
						shootDelay = math.max(50*AI.aimSpeed, shootDelay-Owner.FirearmActivationDelay);
					end
				end
			else
				AI:CreateGetWeaponBehavior(Owner);
				break; -- no firearm available
			end
		end

		-- make sure we are facing the right direction
		if Owner.HFlipped then
			if Dist.X > 0 then
				Owner.HFlipped = false;
			end
		elseif Dist.X < 0 then
			Owner.HFlipped = true;
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	-- reload the secondary before switching to the primary weapon
	if Owner:HasObjectInGroup("Weapons - Primary") and (Owner.EquippedItem and Owner.EquippedItem:HasObjectInGroup("Weapons - Secondary")) then
		while Owner.EquippedItem and ToHeldDevice(Owner.EquippedItem):IsReloading() do
			local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
			if _abrt then return true end
		end
	end

	if AI.PlayerPreferredHD then
		Owner:EquipNamedDevice(AI.PlayerPreferredHD, true);
	elseif not Owner:EquipDeviceInGroup("Weapons - Primary", true) then
		Owner:EquipDeviceInGroup("Weapons - Secondary", true);
	end

	if Owner.FirearmIsEmpty then
		Owner:ReloadFirearms();
	end

	return true;
end

-- In range of a target: the distance kept, and the odd step sideways. A move order's unit walks on (the move behaviour has the legs); a
-- defender stands; anyone else holds about half the weapon's reach (snipers most of it, explosives well clear of their own blast),
-- closing in by the path when further off than that and backing off a step when much nearer, and otherwise shifts a step one way or
-- the other now and then, so it isn't the same mark twice. The better the AI, the more it shifts.
-- The step sideways HoldRange set, stopped (it is only reset when HoldRange is called again, which wants a weapon ready to hit the target:
-- a step under way when the magazine ran dry was walked the whole reload, and on after the target died). Left alone for the units whose
-- legs HoldRange doesn't have (a move order's, an aggressive one's).
function HumanBehaviors.StopRangeStep(AI, Owner)
	if not Owner.aggressive and SharedBehaviors.OrderKind(Owner) ~= "move" then
		AI.lateralMoveState = Actor.LAT_STILL;
	end
end

function HumanBehaviors.HoldRange(AI, Owner, Weapon, PrjDat, range, Dist)
	local kind = SharedBehaviors.OrderKind(Owner);
	if Owner.aggressive or kind == "move" then
		return;
	end
	AI.lateralMoveState = Actor.LAT_STILL;
	-- (Nor with no projectile data: ShootTarget drops it on a reload or a new magazine, and gets it again once the weapon is ready.)
	if kind == "defend" or AI.flying or AI.proneState == AHuman.PRONE or AI.Cover or not PrjDat then
		AI.closingIn = false;
		return;
	end
	local holdRange = PrjDat.rng * 0.55;
	if Weapon:HasObjectInGroup("Weapons - Sniper") then
		holdRange = PrjDat.rng * 0.9;
	elseif PrjDat.blast > 0 then
		holdRange = math.max(PrjDat.blast * 2.5, PrjDat.rng * 0.5);
	end
	holdRange = math.max(120, math.min(holdRange, FrameMan.PlayerScreenWidth * 0.6));
	AI.holdRangeTick = holdRange; -- For the combat overlay (see SharedBehaviors.ExportDebugState).
	local towards = Dist.X > 0 and 1 or -1;
	if range > holdRange * 1.3 and SharedBehaviors.MayClose(AI, Owner) then
		-- Too far for a good shot: closing in by the path, firing on the way (the move behaviour keeps the legs going while closingIn is set).
		if not AI.closingIn then
			if not Owner.MOMoveTarget or not MovableMan:ValidMO(Owner.MOMoveTarget) or Owner.MOMoveTarget.RootID ~= AI.Target.RootID then
				local OldWaypoint = SceneMan:MovePointToGround(Owner:GetLastAIWaypoint(), Owner.Height/5, 4);
				Owner:ClearAIWaypoints();
				Owner:AddAIMOWaypoint(AI.Target);
				Owner:AddAISceneWaypoint(OldWaypoint);
				AI:CreateGoToBehavior(Owner);
			end
			AI.closingIn = true;
			SharedBehaviors.Trace(Owner, "range: closing from " .. math.floor(range) .. " to " .. math.floor(holdRange));
		end
		return;
	end
	if AI.closingIn then
		SharedBehaviors.Trace(Owner, "range: holding at " .. math.floor(range));
	end
	AI.closingIn = false;
	if range < holdRange * 0.4 and not Weapon:HasObjectInGroup("Weapons - Melee") and SharedBehaviors.StepIsSafe(Owner, -towards) then
		AI.lateralMoveState = towards > 0 and Actor.LAT_LEFT or Actor.LAT_RIGHT; -- Too close: a step back.
		return;
	end
	if not AI.StrafeTimer then
		AI.StrafeTimer = Timer();
		AI.StrafeWait = 2000;
	end
	if AI.StrafeUntil and not AI.StrafeUntil:IsPastSimMS(AI.StrafeFor) then
		AI.lateralMoveState = AI.StrafeDir > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
	elseif AI.StrafeTimer:IsPastSimMS(AI.StrafeWait) then
		AI.StrafeTimer:Reset();
		AI.StrafeUntil = nil;
		AI.StrafeWait = math.random(1500, 3500) * (2 - AI.skill / 100);
		if Owner.FirearmIsReady and math.random() * 100 < AI.skill * 0.8 then
			local dir = math.random() < 0.5 and 1 or -1;
			if not SharedBehaviors.StepIsSafe(Owner, dir) then
				dir = -dir;
			end
			if SharedBehaviors.StepIsSafe(Owner, dir) then
				AI.StrafeDir = dir;
				AI.StrafeUntil = Timer();
				AI.StrafeFor = math.random(250, 450);
				AI.lateralMoveState = dir > 0 and Actor.LAT_RIGHT or Actor.LAT_LEFT;
			end
		end
	end
end

-- A few steps behind something the target can't see through, to reload or to recover from a hit, and then out again (LeaveCover).
-- A defender only goes as far as half a body, so it's still at its post. @return Whether the legs are taken for it this tick.
function HumanBehaviors.TakeCover(AI, Owner, FromPos, why)
	if AI.flying or Owner.aggressive then
		return false;
	end
	local kind = SharedBehaviors.OrderKind(Owner);
	-- Pinned down already behind low cover (PeekUpdate's): it holds there, ducking and peeking; the Aim loop's own crouch and prone would
	-- fight the peeking's stances.
	if not AI.Cover and why == "suppressed" and AI.Peek then
		return true;
	end
	if not AI.Cover then
		if AI.CoverRestTimer and not AI.CoverRestTimer:IsPastSimMS(why == "hurt" and 6000 or 1500) then
			return false;
		end
		AI.CoverRestTimer = Timer();
		-- Pinned down, a unit wants cover it can duck behind and peek out of to shoot back (AC-4); to reload or recover, cover it is hidden behind standing.
		local Spot, coverKind = SharedBehaviors.FindCover(Owner, FromPos, kind == "defend" and Owner.Height * 0.5 or Owner.Height * 1.5, why == "suppressed");
		if not Spot then
			return false;
		end
		AI.Cover = { Spot = Spot, Return = Vector(Owner.Pos.X, Owner.Pos.Y), Timer = Timer(), Why = why, There = false, Leaving = false, Low = coverKind == "low" };
		SharedBehaviors.Trace(Owner, "cover: " .. why .. ", " .. (coverKind or "?") .. ", " .. math.floor(SceneMan:ShortestDistance(Owner.Pos, Spot, false).X) .. " px over");
	end
	if AI.Cover.Leaving then
		return false;
	end
	local dx = SceneMan:ShortestDistance(Owner.Pos, AI.Cover.Spot, false).X;
	if not AI.Cover.There and math.abs(dx) > 6 and not AI.Cover.Timer:IsPastSimMS(3000) then
		-- (Walked by the engine's motor: see SharedBehaviors.StepTo.)
		SharedBehaviors.StepTo(AI, Owner, AI.Cover.Spot, 3000 - AI.Cover.Timer.ElapsedSimTimeMS);
		-- (Shot at from out of sight, it goes to low cover bent double.)
		SharedBehaviors.Stance(AI, Owner, (AI.Cover.Why == "shot" and AI.Cover.Low) and SharedBehaviors.CROUCHED or AHuman.NOTPRONE, 200);
	else
		if not AI.Cover.There then
			AI.Cover.There = true;
			AI.Cover.Timer:Reset();
		end
		AI.lateralMoveState = Actor.LAT_STILL;
	end
	return true;
end

-- Using the world (AC-12): out of burning ground it stands in, and with a water cannon, putting out a burning friend when there is no enemy
-- to shoot. (Shooting fuel barrels by an enemy is ShootTarget's, SharedBehaviors.BarrelNear; cover is not taken in fire, FindCover.)
-- Called every tick by the AI's update.
function HumanBehaviors.UseTheWorld(AI, Owner)
	AI.douse = false;
	if Owner:NumberValueExists("OnFire") then
		return; -- Burning itself: ActorFire's panic has it.
	end
	-- Out of the fire, to whichever side is clear: a spot not burning, on much the same floor, with nothing solid between; the one tried last
	-- (a wall the step ran into) left out the next time. Looked for four times a second.
	local Feet = Owner.Pos + Vector(0, Owner.Height * 0.4);
	if AI.FireStep then
		if AI.FireStep.Timer:IsPastSimMS(1500) or not SharedBehaviors.StepTo(AI, Owner, AI.FireStep.Spot, 1500 - AI.FireStep.Timer.ElapsedSimTimeMS) then
			AI.FireStepLast = AI.FireStep.dx;
			AI.FireStep = nil;
		end
	elseif not AI.flying and (not AI.FireCheckTimer or AI.FireCheckTimer:IsPastSimMS(250)) then
		AI.FireCheckTimer = AI.FireCheckTimer or Timer();
		AI.FireCheckTimer:Reset();
		if SceneMan:IsBurningNear(Feet, 14) then
			for _, dx in ipairs({48, -48, 96, -96}) do
				local Spot = SceneMan:MovePointToGround(Owner.Pos + Vector(dx, -Owner.Height * 0.2), math.floor(Owner.Height * 0.2), 4);
				local Way = SceneMan:ShortestDistance(Owner.Pos, Spot, false);
				if dx ~= AI.FireStepLast and not SceneMan:IsBurningNear(Spot, 20) and math.abs(Way.Y) < Owner.Height * 0.5
					and SceneMan:CastObstacleRay(Owner.Pos, Way, Vector(), Vector(), Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, 3) < 0 then
					AI.FireStep = { Spot = Spot, Timer = Timer(), dx = dx };
					SharedBehaviors.Trace(Owner, "fire underfoot: stepping out");
					SharedBehaviors.StepTo(AI, Owner, Spot, 1500);
					break;
				end
			end
		else
			AI.FireStepLast = nil;
		end
	end
	-- A burning friend in reach of a water cannon.
	-- (Not on a move order: "get there; don't stop for it".)
	if AI.Target or SharedBehaviors.OrderKind(Owner) == "move" or not Owner:HasObject("Water Cannon") then
		if AI.Dousing then
			AI.Dousing = nil;
			Owner:EquipFirearm(true);
		end
		return;
	end
	AI.DouseTimer = AI.DouseTimer or Timer();
	if not AI.Dousing or AI.DouseTimer:IsPastSimMS(500) then
		AI.DouseTimer:Reset();
		local Friend;
		for Act in MovableMan.Actors do
			if Act.Team == Owner.Team and Act.ID ~= Owner.ID and SharedBehaviors.PeerValueExists(Act, "OnFire") and SceneMan:ShortestDistance(Owner.Pos, Act.Pos, false):MagnitudeIsLessThan(220) and SharedBehaviors.CanSee(Owner.EyePos, Act.Pos) then
				Friend = Act;
				break;
			end
		end
		if Friend then
			AI.Dousing = Friend;
		elseif AI.Dousing then
			AI.Dousing = nil;
			Owner:EquipFirearm(true);
		end
	end
	local Friend = AI.Dousing;
	if Friend and MovableMan:ValidMO(Friend) then
		if not (Owner.EquippedItem and Owner.EquippedItem.PresetName == "Water Cannon") then
			Owner:EquipNamedDevice("Water Cannon", true);
			return;
		end
		AI.deviceState = AHuman.AIMING;
		AI.lateralMoveState = Actor.LAT_STILL;
		AI.Ctrl.AnalogAim = SceneMan:ShortestDistance(Owner.EyePos, Friend.Pos, false).Normalized;
		AI.douse = true;
	elseif Friend then
		AI.Dousing = nil;
		Owner:EquipFirearm(true);
	end
end

-- Shot from somewhere it can't see (AC-10): the hit's alarm point, a body's height back along the shot (Actor::ParticlePenetration), says
-- which way the shooter is. The unit gets out of the line into cover on that side (full cover if there is some, else low, crouched on the way), looking
-- that way as it goes, and once there aims along it for a while (PinArea); NativeHumanAI's hit-flank waits until it has got there. A unit
-- with a target is the shooting rules' business, and one that can't find cover just faces the shot (FaceAlarm) as before. @param AlarmPoint
-- The alarm point when the unit was hit this tick, else nil.
function HumanBehaviors.ShotFromUnseen(AI, Owner, AlarmPoint)
	-- (Not a unit on a move order or an aggressive one: "get there; don't stop for it". It faces the shot, FaceAlarm, as before.)
	if Owner.aggressive or SharedBehaviors.OrderKind(Owner) == "move" then
		AI.ShotFrom = nil;
		return;
	end
	if AlarmPoint and AlarmPoint.Largest > 0 and not AI.Target then
		local Dir = SceneMan:ShortestDistance(Owner.Pos, AlarmPoint, false);
		if Dir.Largest > 0 then
			Dir:SetMagnitude(400);
			local From = Owner.Pos + Dir;
			SceneMan:WrapPosition(From);
			if AI.ShotFrom then
				AI.ShotFrom.Pos = From;
				AI.ShotFrom.Timer:Reset();
			else
				AI.ShotFrom = { Pos = From, Timer = Timer(), Pinned = false };
			end
		end
	end
	local Shot = AI.ShotFrom;
	if not Shot then
		return;
	end
	if AI.Target or Shot.Timer:IsPastSimMS(4000) then
		AI.ShotFrom = nil;
		return;
	end
	-- Into cover from it, if not already behind some.
	if (AI.Cover and AI.Cover.Why ~= "shot") or not HumanBehaviors.TakeCover(AI, Owner, Shot.Pos, "shot") then
		return;
	end
	local Aim = SceneMan:ShortestDistance(Owner.EyePos, Shot.Pos, false);
	if not AI.Cover.There then
		AI.deviceState = AHuman.AIMING;
		AI.Ctrl.AnalogAim = Aim.Normalized;
	elseif not Shot.Pinned then
		Shot.Pinned = true;
		SharedBehaviors.Trace(Owner, "shot from out of sight: in cover, watching that way");
		AI.OldTargetPos = AI.OldTargetPos or Vector(Shot.Pos.X, Shot.Pos.Y);
		AI:CreatePinBehavior(Owner);
	end
	if AI.Cover.There and AI.Cover.Low then
		SharedBehaviors.Stance(AI, Owner, SharedBehaviors.CROUCHED, 300);
	end
end

-- Out of cover again, back to where the unit was, once the reload is done or the moment's rest is over. Called every tick by the AI's
-- update, so it happens whether or not the shooting rules are still running.
function HumanBehaviors.LeaveCover(AI, Owner)
	if not AI.Cover then
		return;
	end
	if not AI.Cover.Leaving then
		if not AI.Cover.There then
			return;
		end
		local reloading = Owner.EquippedItem and ToHeldDevice(Owner.EquippedItem):IsReloading();
		local rested = AI.Cover.Timer:IsPastSimMS(AI.Cover.Why == "hurt" and 1500 or 300);
		if reloading or not rested then
			return;
		end
		-- Behind low cover with the enemy still about, the unit stays and fights from there, ducking and peeking (PeekUpdate), for up to
		-- twelve seconds; then back out as from any cover.
		if AI.Cover.Low and AI.Target and MovableMan:ValidMO(AI.Target) and not AI.Cover.Timer:IsPastSimMS(12000) then
			return;
		end
		-- Shot at from out of sight, it stays put while the shots keep coming (ShotFromUnseen forgets them 4 s after the last).
		if AI.Cover.Why == "shot" and AI.ShotFrom and not AI.Cover.Timer:IsPastSimMS(12000) then
			return;
		end
		AI.Cover.Leaving = true;
		AI.Cover.Timer:Reset();
	end
	-- Not while off on a walk, or lying down.
	if (AI.GoToBehavior and not AI.Target) or AI.proneState == AHuman.PRONE then
		AI.Cover = nil;
		AI.CoverRestTimer = Timer();
		return;
	end
	local dx = SceneMan:ShortestDistance(Owner.Pos, AI.Cover.Return, false).X;
	if math.abs(dx) > 6 and not AI.Cover.Timer:IsPastSimMS(3000) then
		SharedBehaviors.StepTo(AI, Owner, AI.Cover.Return, 3000 - AI.Cover.Timer.ElapsedSimTimeMS);
	else
		SharedBehaviors.Trace(Owner, "cover: out again");
		AI.Cover = nil;
		AI.CoverRestTimer = Timer();
	end
end

-- Fighting from behind low cover (AC-4): with an enemy in sight, standing still and a sandbag, low wall or ridge between, so crouched the
-- unit is hidden and standing it can shoot over, it ducks down and comes up to fire in turn: up a second or so to shoot (longer the better
-- the AI), down a moment, and down for good while reloading. Holds its fire while down (MayFire), the gun being behind the cover then.
-- Not on the move, flying, lying down, aggressive, or without the engine's motor (the crouch is the motor's). Called every tick by the AI's update.
function HumanBehaviors.PeekUpdate(AI, Owner)
	AI.ducked = false;
	local Target = AI.Target;
	local moving = AI.lateralMoveState ~= Actor.LAT_STILL and not (AI.Cover and AI.Cover.There);
	if not Target or not MovableMan:ValidMO(Target) or moving or AI.flying or Owner.aggressive or AI.closingIn or AI.proneState == AHuman.PRONE or not SharedBehaviors.EngineMotor(Owner) then
		AI.Peek = nil;
		AI.peekCover = nil;
		return;
	end
	AI.PeekCheckTimer = AI.PeekCheckTimer or Timer();
	if AI.peekCover == nil or AI.PeekCheckTimer:IsPastSimMS(500) then
		AI.PeekCheckTimer:Reset();
		AI.peekCover = SharedBehaviors.CoverAt(Owner, SceneMan:MovePointToGround(Owner.Pos, 0, 4), Target.Pos) == "low";
		if SharedBehaviors.CanSee(Owner.EyePos, Target.Pos) then
			AI.PeekSeenTimer = AI.PeekSeenTimer or Timer();
			AI.PeekSeenTimer:Reset();
		end
	end
	if not AI.peekCover then
		AI.Peek = nil;
		return;
	end
	if not AI.Peek then
		AI.Peek = { Up = true, Timer = Timer(), For = 800 };
		SharedBehaviors.Trace(Owner, "cover: low, peeking");
	end
	if AI.Peek.Timer:IsPastSimMS(AI.Peek.For) then
		AI.Peek.Up = not AI.Peek.Up;
		AI.Peek.Timer:Reset();
		local skill = (AI.skill or 50) / 100;
		AI.Peek.For = AI.Peek.Up and math.random(800, 1400) * (0.7 + skill * 0.6) or math.random(500, 1100) * (1.3 - skill * 0.6);
	end
	local reloading = Owner.EquippedItem and IsHeldDevice(Owner.EquippedItem) and ToHeldDevice(Owner.EquippedItem):IsReloading();
	if AI.Peek.Up and not reloading then
		SharedBehaviors.Stance(AI, Owner, AHuman.NOTPRONE, 300);
	else
		SharedBehaviors.Stance(AI, Owner, SharedBehaviors.CROUCHED, 300);
		AI.ducked = true;
		-- (Out of its sight while down is the point, not losing it: the target is kept, as it is where it was. But only while it was seen
		-- from up in the last four seconds: one that has gone is let go, low cover or no.)
		if AI.TargetLostTimer and AI.PeekSeenTimer and not AI.PeekSeenTimer:IsPastSimMS(4000) then
			AI.TargetLostTimer:Reset();
		end
	end
end

-- throw a grenade at the selected target
--TODO: This behavior should effectively have the actor close in on the target if out of range!
function HumanBehaviors.ThrowTarget(AI, Owner, Abort)
	-- (Not against the weapons rule, RC-1: a grenade is a weapon, and a trigger the rule let go of mid-throw threw it short.)
	if not SharedBehaviors.RuleLetsFire(AI, Owner) then
		return true;
	end
	local ThrowTimer = Timer();
	local aimTime = Owner.ThrowPrepTime;
	local scan = 0;
	local miss = 0; -- stop scanning after a few missed attempts
	local AimPoint, Dist, MO, ID, rootID, LOS, aim;

	AI.TargetLostTimer:SetSimTimeLimitMS(1500);

	while true do
		if not MovableMan:ValidMO(AI.Target) then
			break;
		end

		if LOS then	-- don't sharp aim until LOS has been confirmed
			if Owner.ThrowableIsReady then
				if not ThrowTimer:IsPastSimMS(aimTime) then
					AI.Ctrl.AnalogAim = Vector(1,0):RadRotate(aim+RangeRand(-0.04, 0.04));
					AI.fire = true;
				else
					AI.fire = false;
				end
			else	-- no grenades left, continue attack
				if not (Owner.AIMode == Actor.AIMODE_SENTRY or Owner.AIMode == Actor.AIMODE_SQUAD) then
					AI:CreateAttackBehavior(Owner);
				end
				break;
			end
		else
			if scan < 1 then
				if AI.Target.Door then
					AimPoint = AI.Target.Door.Pos;
				else
					AimPoint = AI.Target.Pos; -- look for the center
					if AI.Target.EyePos then
						AimPoint = (AimPoint + AI.Target.EyePos) / 2;
					end
				end

				ID = rte.NoMOID;
				--if Owner:IsWithinRange(Vector(AimPoint.X, AimPoint.Y)) then	-- TODO: use grenade properties to decide this
				if true then
					Dist = SceneMan:ShortestDistance(Owner.EyePos, AimPoint, false);
					ID = SceneMan:CastMORay(Owner.EyePos, Dist, Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, false, 3);
					if ID < 1 or ID == rte.NoMOID then	-- not found, look for any head or legs
						AimPoint = AI.Target.EyePos; -- the head
						if AimPoint then
							local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
							if _abrt then return true end
							if not MovableMan:ValidMO(AI.Target) then	-- must verify that the target exist after a yield
								break;
							end

							Dist = SceneMan:ShortestDistance(Owner.EyePos, AimPoint, false);
							ID = SceneMan:CastMORay(Owner.EyePos, Dist, Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, false, 3);
						end

						if ID < 1 or ID == rte.NoMOID then
							local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
							if _abrt then return true end
							if not MovableMan:ValidMO(AI.Target) then	-- must verify that the target exist after a yield
								break;
							end

							local Legs = AI.Target.FGLeg or AI.Target.BGLeg; -- the legs
							if Legs then
								AimPoint = Legs.Pos;
								Dist = SceneMan:ShortestDistance(Owner.EyePos, AimPoint, false);
								ID = SceneMan:CastMORay(Owner.EyePos, Dist, Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, false, 3);
							end
						end
					end
				else
					break; -- out of range
				end

				if ID > 0 and ID ~= rte.NoMOID then	-- MO found
					-- check what target we will hit
					rootID = MovableMan:GetRootMOID(ID);
					if rootID ~= AI.Target.ID then
						MO = MovableMan:GetMOFromID(rootID);
						if MovableMan:ValidMO(MO) then
							if MO.Team ~= Owner.Team then
								if MO.ClassName == "AHuman" then
									AI.Target = ToAHuman(MO);
									local Legs = AI.Target.FGLeg or AI.Target.BGLeg; -- the legs
									if Legs then
										AimPoint = Legs.Pos;
									end
								elseif MO.ClassName == "ACrab" then
									AI.Target = ToACrab(MO);
									local Legs = AI.Target.LeftFGLeg or AI.Target.RightFGLeg or AI.Target.LeftBGLeg or AI.Target.RightFGLeg; -- the legs
									if Legs then
										AimPoint = Legs.Pos;
									end
								elseif MO.ClassName == "ACRocket" then
									AI.Target = ToACRocket(MO);
								elseif MO.ClassName == "ACDropShip" then
									AI.Target = ToACDropShip(MO);
								elseif MO.ClassName == "ADoor" then
									AI.Target = ToADoor(MO);
								elseif MO.ClassName == "Actor" then
									AI.Target = ToActor(MO);
								else
									break;
								end
							else
								break; -- don't shoot friendlies
							end
						end
					end

					scan = 6; -- skip the LOS check the next n frames
					miss = 0;
					LOS = true; -- we have line of sight to the target

					-- The arc (AC-5): flat where clear, else lobbed over what is in the way, led onto a runner by its flight time, and
					-- cooked by a good AI so it bursts over the target.
					if Owner.ThrowableIsReady then
						local Grenade = ToThrownDevice(Owner.EquippedItem);
						if Grenade then
							local _, seconds, full;
							aim, _, seconds, full = HumanBehaviors.PlanThrow(Grenade, AimPoint, AI.Target.Vel, false);
							if aim then
								aim = aim - Owner.RotAngle;
								ThrowTimer:Reset();
								aimTime = HumanBehaviors.HoldTime(AI, Owner, Grenade, seconds, full);
								-- (The runner moves while the grenade is held too: led on by the hold as well, once the hold is known.)
								local Vel = AI.Target.Vel;
								if Vel.Magnitude > 1 and aimTime > 200 then
									local heldAim = HumanBehaviors.PlanThrow(Grenade, AimPoint + Vel * GetPPM() * aimTime * 0.001, Vel, false);
									if heldAim then
										aim = heldAim - Owner.RotAngle;
									end
								end
							else
								break; -- target out of range, or no arc gets there
							end
						else
							break;
						end
					else
						break;
					end
				else
					miss = miss + 1;
					if miss > 4 then	-- stop looking if we cannot find anything after n attempts
						break;
					else
						scan = 3; -- check LOS a little bit more often if no MO was found
					end
				end
			else
				scan = scan - 1;
			end
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	return true;
end

-- attack the target in hand-to-hand
function HumanBehaviors.AttackTarget(AI, Owner, Abort)
	if not AI.Target or not MovableMan:ValidMO(AI.Target) then
		return true;
	end

	AI.TargetLostTimer:SetSimTimeLimitMS(5000);

	-- If we've been trying for a while and we're not getting any closer to our target, auto-fail the behaviour so we do other stuff
	local MovementFailTimer = Timer();
	MovementFailTimer:SetSimTimeLimitMS(3000);
	local closestDistance = nil;

	-- If we're attacking the target but it's not dying (likely that we're too far away), fail
	-- It may be that we are successfully damaging it, just very slowly. That's not a big issue
	-- If it's still a good target, we'll likely retarget it next frame, and move a little closer (by re-adding the GoToBehavior)
	local DamageFailTimer = Timer();
	DamageFailTimer:SetSimTimeLimitMS(6000);

	-- move back here later
	local PrevMOMoveTarget, PrevSceneWaypoint;
	if Owner.MOMoveTarget and MovableMan:ValidMO(Owner.MOMoveTarget) then
		PrevMOMoveTarget = Owner.MOMoveTarget;
	else
		Owner.MOMoveTarget = nil;
		PrevSceneWaypoint = SceneMan:MovePointToGround(Owner:GetLastAIWaypoint(), Owner.Height/5, 4);
	end

	-- move towards the target
	Owner:ClearMovePath();
	Owner:AddAIMOWaypoint(AI.Target);

	if PrevMOMoveTarget then
		Owner:AddAIMOWaypoint(PrevMOMoveTarget);
	end

	if PrevSceneWaypoint then
		Owner:AddAISceneWaypoint(PrevSceneWaypoint);
	end

	AI:CreateGoToBehavior(Owner);
	AI.proneState = AHuman.NOTPRONE;

	while true do
		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end

		if not AI.Target or not MovableMan:ValidMO(AI.Target) then
			break;
		end

		-- use following sequence to attack either with a suited melee weapon or arms
		local suitableWeapon = false;

		if AI.Target.ClassName == "ADoor" then
			-- Prefer breaching tools for attacking doors
			suitableWeapon = Owner:EquipDeviceInGroup("Tools - Breaching", true) or Owner:EquipDeviceInGroup("Tools - Diggers", true) or Owner:EquipDeviceInGroup("Weapons - Melee", true);
		else
			-- Prefer melee weapons for attacking actors
			suitableWeapon = Owner:EquipDeviceInGroup("Weapons - Melee", true) or Owner:EquipDeviceInGroup("Tools - Diggers", true) or Owner:EquipDeviceInGroup("Tools - Breaching", true);
		end

		if not suitableWeapon then
			-- We have no suitable weapon to use
			AI:CreateGetWeaponBehavior(Owner);
			break;
		end

		local startPos = Vector(Owner.EquippedItem.Pos.X, Owner.EquippedItem.Pos.Y);
		local attackPos = (AI.Target.ClassName == "ADoor" and ToADoor(AI.Target).Door and ToADoor(AI.Target).Door:IsAttached()) and ToADoor(AI.Target).Door.Pos or AI.Target.Pos;
		local distance = SceneMan:ShortestDistance(startPos, attackPos, SceneMan.SceneWrapsX);
		local meleeDist = Owner.IndividualRadius + (IsThrownDevice(Owner.EquippedItem) and 50 or 25);
		if distance:MagnitudeIsLessThan(meleeDist) then
			if DamageFailTimer:IsPastSimTimeLimit() then
				break;
			end
			AI.lateralMoveState = Actor.LAT_STILL;
			AI.Ctrl.AnalogAim = SceneMan:ShortestDistance(Owner.EyePos, attackPos, SceneMan.SceneWrapsX).Normalized;
			AI.fire = not (AI.fire and IsThrownDevice(Owner.EquippedItem) and Owner.ThrowProgress == 1);
		else
			DamageFailTimer:Reset();

			-- Ensure we're getting closer
			if not closestDistance or distance.SqrMagnitude < closestDistance.SqrMagnitude then
				closestDistance = distance;
				MovementFailTimer:Reset();
			elseif MovementFailTimer:IsPastSimTimeLimit() then
				break;
			end

			AI.fire = false;
		end
	end

	return true;
end


function HumanBehaviors.GetAngleToHit(PrjDat, Dist)
	if PrjDat.g == 0 then	-- this projectile is not affected by gravity
		return Dist.AbsRadAngle;
	else	-- compensate for gravity
		local rootSq, muzVelSq;
		local D = Dist / GetPPM(); -- convert from pixels to meters
		if PrjDat.drg < 1 then	-- compensate for air resistance
			local rng = D.Magnitude;
			local timeToTarget = math.floor((rng / math.max(PrjDat.vel*PrjDat.drg^math.floor(rng/(PrjDat.vel+1)+0.5), PrjDat.thr)) / TimerMan.DeltaTimeSecs); -- estimate time of flight in frames

			if timeToTarget > 1 then
				local muzVel = 0.9*math.max(PrjDat.vel * PrjDat.drg^timeToTarget, PrjDat.thr) + 0.1*PrjDat.vel; -- compensate for velocity reduction during flight
				muzVelSq = muzVel * muzVel;
				rootSq = muzVelSq*muzVelSq - PrjDat.g * (PrjDat.g*D.X*D.X + 2*-D.Y*muzVelSq);
			else
				muzVelSq = PrjDat.vsq;
				rootSq = PrjDat.vqu - PrjDat.g * (PrjDat.g*D.X*D.X + 2*-D.Y*muzVelSq);
			end
		else
			muzVelSq = PrjDat.vsq;
			rootSq = PrjDat.vqu - PrjDat.g * (PrjDat.g*D.X*D.X + 2*-D.Y*muzVelSq);
		end

		if rootSq >= 0 then	-- no solution exists if rootSq is below zero
			local ang1 = math.atan2(muzVelSq - math.sqrt(rootSq), PrjDat.g*D.X);
			local ang2 = math.atan2(muzVelSq + math.sqrt(rootSq), PrjDat.g*D.X);
			if ang1 + ang2 > math.pi then	-- both angles in the second or third quadrant
				if ang1 > math.pi or ang2 > math.pi then	-- one or more angle in the third quadrant
					return math.min(ang1, ang2);
				else
					return math.max(ang1, ang2);
				end
			else	-- both angles in the firs quadrant
				return math.min(ang1, ang2);
			end
		end
	end
end

-- open fire on the area around the selected target
function HumanBehaviors.ShootArea(AI, Owner, Abort)
	if not MovableMan:ValidMO(AI.UnseenTarget) or not Owner.FirearmIsReady then
		return true;
	end

	-- see if we can shoot from the prone position
	local ShootTimer = Timer();
	local aimTime = 50 + RangeRand(100, 300) * AI.aimSpeed;
	if not AI.flying and AI.UnseenTarget.Vel.Largest < 12 and HumanBehaviors.GoProne(AI, Owner, AI.UnseenTarget.Pos, AI.UnseenTarget.ID) then
		aimTime = aimTime + 500;
	end

	local StartPos = Vector(AI.UnseenTarget.Pos.X, AI.UnseenTarget.Pos.Y);

	-- aim at the target in case we can see it when sharp aiming
	Owner:SetAimAngle(SceneMan:ShortestDistance(Owner.EyePos, StartPos, false).AbsRadAngle);
	AI.deviceState = AHuman.AIMING;

	-- aim for ~160ms
	for _ = 1, 10 do
		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	if not Owner.FirearmIsReady then
		return true;
	end

	local AimPoint;
	for _ = 1, 5 do	-- try up to five times to find a target area that is reasonably close to the target
		AimPoint = StartPos + Vector(RangeRand(-100, 100), RangeRand(-100, 50));
		if AimPoint.X >= SceneMan.SceneWidth then
			AimPoint.X = SceneMan.SceneWidth - AimPoint.X;
		elseif AimPoint.X < 0 then
			AimPoint.X = AimPoint.X + SceneMan.SceneWidth;
		end

		-- check if we can fire at the AimPoint
		local Trace = SceneMan:ShortestDistance(Owner.EyePos, AimPoint, false);
		local rayLength = SceneMan:CastObstacleRay(Owner.EyePos, Trace, Vector(), Vector(), rte.NoMOID, Owner.IgnoresWhichTeam, rte.grassID, 11, true); -- (sees through water as far as units do)
		if Trace:MagnitudeIsLessThan(rayLength * 1.5) then
			break; -- the AimPoint is close enough to the target, start shooting
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	if not Owner.FirearmIsReady then
		return true;
	end

	local aim;
	local PrjDat = SharedBehaviors.GetProjectileData(Owner);
	local Dist = SceneMan:ShortestDistance(Owner.EquippedItem.Pos, AimPoint, false);
	local Weapon = ToHDFirearm(Owner.EquippedItem);

	-- uncomment these to get the range of the weapon
	--ConsoleMan:PrintString(Owner.EquippedItem.PresetName .. " range = " .. PrjDat.rng .. " px");
	--ConsoleMan:PrintString("AimPoint range = " .. SceneMan:ShortestDistance(Owner.Pos, AimPoint, false).Magnitude .. " px");

	if Dist:MagnitudeIsLessThan(PrjDat.rng) then
		aim = HumanBehaviors.GetAngleToHit(PrjDat, Dist);
	else
		return true; -- target out of range
	end

	local CheckTargetTimer = Timer();
	local aimError = RangeRand(-0.25, 0.25) * AI.aimSkill;

	AI.fire = false;
	while aim do
		if Owner.FirearmIsReady then
			AI.deviceState = AHuman.AIMING;
			AI.Ctrl.AnalogAim = Vector(1,0):RadRotate(aim+aimError+RangeRand(-0.02, 0.02)*AI.aimSkill);
			if ShootTimer:IsPastRealMS(aimTime) then
				-- Fire at a place, not a target (AC-8): not the whole magazine sprayed at it. A third is kept for whoever comes out, and
				-- an automatic fires 300 ms bursts with 400 ms between.
				if Weapon.RoundInMagCapacity > 0 and Weapon.RoundInMagCount <= Weapon.RoundInMagCapacity * 0.34 then
					AI.fire = false;
					Owner:ReloadFirearms(); -- (Topped up while the enemy is out of sight, not when it shows itself.)
					break;
				end
				if Weapon.FullAuto then
					AI.BurstClock = AI.BurstClock or Timer();
					local phase = AI.BurstClock.ElapsedSimTimeMS % 700;
					AI.fire = phase < 300;
				else
					ShootTimer:Reset();
					aimTime = 120 * AI.aimSkill;
					AI.fire = not AI.fire;
				end

				aimError = aimError * 0.985;
			end
		else
			AI.deviceState = AHuman.POINTING;
			AI.fire = false;

			ShootTimer:Reset();
			if Owner.FirearmIsEmpty then
				Owner:ReloadFirearms();
			end

			break; -- stop this behavior when the mag is empty
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end

		if AI.UnseenTarget and CheckTargetTimer:IsPastRealMS(400*AI.aimSkill) then
			if MovableMan:ValidMO(AI.UnseenTarget) and (AI.UnseenTarget.ClassName == "AHuman" or AI.UnseenTarget.ClassName == "ACrab") then
				CheckTargetTimer:Reset();
				if AI.UnseenTarget:GetController() and AI.UnseenTarget:GetController():IsState(Controller.WEAPON_FIRE) then
					-- compare the enemy aim angle with the angle to us
					local AimEnemy = SceneMan:ShortestDistance(AI.UnseenTarget.EyePos, AI.UnseenTarget.ViewPoint, false).Normalized;
					local DistNormal = SceneMan:ShortestDistance(AI.UnseenTarget.EyePos, Owner.Pos, false).Normalized;
					local dot = DistNormal.X * AimEnemy.X + DistNormal.Y * AimEnemy.Y;
					if dot > 0.4 then
						-- this actor is shooting in our direction
						AimPoint = AI.UnseenTarget.Pos + SceneMan:ShortestDistance(AI.UnseenTarget.Pos, AimPoint, false) / 2 + Vector(RangeRand(-40, 40)*AI.aimSkill, RangeRand(-40, 40)*AI.aimSkill);
						aimError = RangeRand(-0.15, 0.15) * AI.aimSkill;

						Dist = SceneMan:ShortestDistance(Owner.EquippedItem.Pos, AimPoint, false);
						if Dist:MagnitudeIsLessThan(PrjDat.rng) then
							aim = HumanBehaviors.GetAngleToHit(PrjDat, Dist);
						end
					end
				end
			else
				AI.UnseenTarget = nil;
			end
		end
	end
	return true
end

-- Medics (AC-7): a unit carrying a medikit, with no enemy to deal with, goes to a badly hurt friend near by, puts the kit to it and patches
-- it up, then takes its own order up again. A friend falling back to a medic (SharedBehaviors.RetreatUpdate) is seen to from further off.
-- Not a unit told to hold its position or defend a spot, which stays where it was put. Called every tick by the AI's update.
-- @return Whether the unit is seeing to a friend.
function HumanBehaviors.MedicUpdate(AI, Owner)
	-- The tag taken off by someone else (a sandbox order): that order stands, and the one this would have put back is gone.
	if AI.Medic and not Owner:NumberValueExists("AIMedic") then
		SharedBehaviors.Trace(Owner, "medic: called off by a new order");
		HumanBehaviors.MedicEnd(AI, Owner, nil);
		return false;
	end
	if AI.Medic then
		local M = AI.Medic;
		local Patient = M.Patient;
		local over;
		if AI.Target or AI.UnseenTarget then
			over = "an enemy about";
		elseif not MovableMan:ValidMO(Patient) or Patient.Status >= Actor.DYING or Patient.Health <= 0 then
			over = "the friend is gone";
		elseif (SharedBehaviors.MedicSeeingTo(Patient, Owner.UniqueID) or math.huge) < Owner.UniqueID then
			over = "another medic has the friend"; -- (Two went for the same friend on the same update: the lower unique ID keeps it.)
		elseif Patient.Health >= Patient.MaxHealth * 0.9 and Patient.WoundCount == 0 then
			over = "patched up";
		elseif not Owner:HasObject("Medikit") then
			over = "the kit is used up";
		elseif M.Timer:IsPastSimMS(20000) then
			over = "couldn't get to the friend";
		elseif SharedBehaviors.OrderChangedSince(Owner, M.Spot) then
			-- Another order given meanwhile (the walk's own end, as a sentry at the spot, isn't one): that order stands.
			SharedBehaviors.Trace(Owner, "medic: called off by another order");
			HumanBehaviors.MedicEnd(AI, Owner, nil);
			return false;
		end
		if over then
			SharedBehaviors.Trace(Owner, "medic: done, " .. over);
			HumanBehaviors.MedicEnd(AI, Owner, M.Keep);
			return false;
		end
		local ToPatient = SceneMan:ShortestDistance(Owner.Pos, Patient.Pos, false);
		if math.abs(ToPatient.X) < 24 + Patient.Radius * 0.3 and math.abs(ToPatient.Y) < Owner.Height * 0.6 then
			-- Close enough: the kit out, pointed at the friend, and pressed once a second (it is a single-shot device). The aim sweeps up
			-- and down the friend a little so a press that missed (and found nobody hurt, which costs nothing) catches it the next time.
			if not M.Close then
				M.Close = true;
				M.ShotTimer = Timer();
				SharedBehaviors.Trace(Owner, "medic: seeing to " .. Patient.PresetName);
			end
			-- (The kit in hand every tick, not only on arriving: the AI's own self-heal puts a gun back in hand once the medic is over half
			-- health again, and the trigger then fired the gun at the friend.)
			local kitInHand = Owner.EquippedItem ~= nil and Owner.EquippedItem.PresetName == "Medikit";
			if not kitInHand then
				Owner:EquipNamedDevice("Medikit", true);
				kitInHand = Owner.EquippedItem ~= nil and Owner.EquippedItem.PresetName == "Medikit";
			end
			local sweep = math.sin(M.Timer.ElapsedSimTimeMS * 0.004) * Patient.Height * 0.2;
			local Aim = SceneMan:ShortestDistance(Owner.EyePos, Patient.Pos + Vector(0, sweep), false);
			if Aim:MagnitudeIsGreaterThan(1) then
				AI.Ctrl.AnalogAim = Aim.Normalized;
			end
			AI.lateralMoveState = Actor.LAT_STILL;
			AI.medicHeal = false;
			if kitInHand and M.ShotTimer:IsPastSimMS(1100) then
				M.ShotTimer:Reset();
				AI.medicHeal = true;
			end
		else
			AI.medicHeal = false;
			if M.Close then
				M.Close = false;
			end
			-- The friend moved on: the walk follows it.
			if SceneMan:ShortestDistance(M.Spot, Patient.Pos, false):MagnitudeIsGreaterThan(60) then
				M.Spot = SceneMan:MovePointToGround(Patient.Pos, math.floor(Owner.Height * 0.2), 4);
				Owner:ClearAIWaypoints();
				Owner:AddAISceneWaypoint(M.Spot);
				Owner.AIMode = Actor.AIMODE_GOTO;
			end
		end
		return true;
	end

	-- Looking for someone to see to, once a second.
	if AI.Target or AI.UnseenTarget or AI.Retreat or AI.Flank or AI.useMedikit or Owner:IsPlayerControlled() then
		return false;
	end
	if AI.MedicLookTimer and not AI.MedicLookTimer:IsPastSimMS(1000) then
		return false;
	end
	AI.MedicLookTimer = AI.MedicLookTimer or Timer();
	AI.MedicLookTimer:Reset();
	if not Owner:HasObject("Medikit") or Owner.OrderHold or Owner.OrderHasPost or SharedBehaviors.OrderKind(Owner) == "defend"
		or Owner:NumberValueExists("AIRetreat") or Owner:NumberValueExists("AIFlank") then
		return false;
	end
	local Patient = SharedBehaviors.FindPatient(Owner);
	if not Patient then
		return false;
	end
	local Spot = SceneMan:MovePointToGround(Patient.Pos, math.floor(Owner.Height * 0.2), 4);
	AI.Medic = { Keep = SharedBehaviors.RememberOrder(AI, Owner), Patient = Patient, Spot = Spot, Timer = Timer() };
	Owner:SetNumberValue("AIMedic", 1);
	Owner:SetNumberValue("AIMedicFor", Patient.UniqueID); -- (On itself: no unit's AI writes another's values, see SharedBehaviors.PeerValue.)
	Owner.OrderAttack = false;
	Owner:ClearAIWaypoints();
	Owner:AddAISceneWaypoint(Spot);
	Owner.AIMode = Actor.AIMODE_GOTO;
	SharedBehaviors.Trace(Owner, "medic: going to " .. Patient.PresetName .. ", health " .. math.floor(Patient.Health));
	return true;
end

-- Ends a medic's errand: the kit put away for a gun, the friend free for another medic, and the order kept from before put back (nil when
-- a new order has taken its place).
function HumanBehaviors.MedicEnd(AI, Owner, keep)
	local M = AI.Medic;
	AI.Medic = nil;
	AI.medicHeal = false;
	Owner:RemoveNumberValue("AIMedic");
	Owner:RemoveNumberValue("AIMedicFor");
	if Owner.EquippedItem and Owner.EquippedItem.PresetName == "Medikit" then
		Owner:EquipFirearm(true);
	end
	if keep then
		SharedBehaviors.RestoreOrder(AI, Owner, keep);
	end
	AI.MedicLookTimer = Timer(); -- (A second's pause before the next friend.)
end

-- Saving the last magazine (AC-8): with no enemy in sight or heard for a second and a half, a weapon under half full is reloaded, so the
-- next fight doesn't start with a few rounds and a reload in the open. Weapons only (a medikit is a firearm too, and isn't to be refilled).
-- Called every tick by the AI's update.
function HumanBehaviors.ReloadInLull(AI, Owner)
	AI.LullTimer = AI.LullTimer or Timer();
	if AI.Target or AI.UnseenTarget or Owner:IsPlayerControlled() then
		AI.LullTimer:Reset();
		return;
	end
	if not AI.LullTimer:IsPastSimMS(1500) then
		return;
	end
	AI.LullTimer:Reset();
	local Item = Owner.EquippedItem;
	if Item and IsHDFirearm(Item) and Item:HasObjectInGroup("Weapons") then
		local Gun = ToHDFirearm(Item);
		if not Gun:IsReloading() and Gun.RoundInMagCapacity > 0 and Gun.RoundInMagCount >= 0 and Gun.RoundInMagCount < Gun.RoundInMagCapacity * 0.5 then
			SharedBehaviors.Trace(Owner, "reload: in a lull, " .. Gun.RoundInMagCount .. " of " .. Gun.RoundInMagCapacity .. " left");
			Owner:ReloadFirearms();
		end
	end
end

-- stop the user from inadvertently modifying the storage table
-- Mods written for older versions call the behaviours that are shared between kinds of unit (Patrol, GoToWpt, BrainSearch, GetTeamShootingSkill and so on) through this table.
-- They live in SharedBehaviors now: anything not found here is looked up there, so those mods keep working.
require("AI/SharedBehaviors");
local Storage = HumanBehaviors;
local Shared = SharedBehaviors;
local Proxy = {};
local Mt = {
	__index = function(_, name)
		local found = Storage[name];
		if found == nil then
			found = Shared[name];
		end
		return found;
	end,
	__newindex = function(Table, k, v)
		error("The HumanBehaviors table is read-only.", 2);
	end
}
setmetatable(Proxy, Mt);
HumanBehaviors = Proxy;
