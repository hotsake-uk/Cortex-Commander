
CrabBehaviors = {};
require("AI/HumanBehaviors"); -- (For the ballistic aim, HumanBehaviors.GetAngleToHit, AC-9.)

function CrabBehaviors.LookForTargets(AI, Owner)
	local viewAngDeg = RangeRand(35, 85) * Owner.Perceptiveness;
	if AI.deviceState == AHuman.AIMING then
		AI.Ctrl:SetState(Controller.AIM_SHARP, true);
		viewAngDeg = 20 * Owner.Perceptiveness;
	end

	local HitPoint;
	local FoundMO;
	if SharedBehaviors.CanScan(Owner) then
		-- The engine's scan (see SharedBehaviors.ScanForTargets), as the humans look: crabs and turrets a narrower view, the fog seen to.
		FoundMO, HitPoint = SharedBehaviors.ScanForTargets(AI, Owner, AI.skill or select(3, SharedBehaviors.GetTeamShootingSkill(Owner.Team)), 100 * Owner.Perceptiveness, AI.Target and 3 or 2);
	else
		FoundMO = Owner:LookForMOs(viewAngDeg, rte.grassID, false);
		if FoundMO then
			HitPoint = SceneMan:GetLastRayHitPos();
			if AI.isPlayerOwned and SceneMan:IsUnseen(HitPoint.X, HitPoint.Y, Owner.Team) and SceneMan:IsUnseen(FoundMO.Pos.X, FoundMO.Pos.Y, Owner.Team) then -- AI-teams ignore the fog
				FoundMO = nil; -- target hidden behind the fog
			end
		end
	end

	if FoundMO then
		if AI.Behavior ~= nil and AI.Target and MovableMan:ValidMO(AI.Target) and FoundMO.ID == AI.Target.ID then	-- found the same target
			SharedBehaviors.ReportEnemy(Owner, AI.Target);
			AI.TargetOffset = SceneMan:ShortestDistance(AI.Target.Pos, HitPoint, false);
			AI.TargetLostTimer:Reset();
			AI.ReloadTimer:Reset();
		elseif FoundMO.Team ~= Owner.Team then	-- found an enemy
			if FoundMO.ClassName == "AHuman" then
				FoundMO = ToAHuman(FoundMO);
			elseif FoundMO.ClassName == "ACrab" then
				FoundMO = ToACrab(FoundMO);
			elseif FoundMO.ClassName == "ACRocket" then
				FoundMO = ToACRocket(FoundMO);
			elseif FoundMO.ClassName == "ACDropShip" then
				FoundMO = ToACDropShip(FoundMO);
			elseif FoundMO.ClassName == "ADoor" and FoundMO.Team ~= Activity.NOTEAM and Owner.AIMode ~= Actor.AIMODE_SENTRY and ToADoor(FoundMO).Door and ToADoor(FoundMO).Door:IsAttached() and SharedBehaviors.GetProjectileData(Owner).pen * 0.9 > ToADoor(FoundMO).Door.Material.StructuralIntegrity then
				FoundMO = ToADoor(FoundMO);
			elseif FoundMO.ClassName == "AVehicle" then	-- A cart or other vehicle (VH-1).
				FoundMO = ToAVehicle(FoundMO);
			elseif FoundMO.ClassName == "Actor" then
				FoundMO = ToActor(FoundMO);
			else
				FoundMO = nil;
			end

			if FoundMO then
				-- The team hears of it (AC-2).
				SharedBehaviors.ReportEnemy(Owner, FoundMO);
				if AI.Target then
					-- check if this MO should be targeted instead
					if SharedBehaviors.CalculateThreatLevel(FoundMO, Owner) > SharedBehaviors.CalculateThreatLevel(AI.Target, Owner) + 0.2 then
						AI.OldTargetPos = Vector(AI.Target.Pos.X, AI.Target.Pos.Y);
						AI.Target = FoundMO;
						AI.TargetOffset = SceneMan:ShortestDistance(AI.Target.Pos, HitPoint, false); -- this is the distance vector from the target center to the point we hit with our ray
						AI:CreateAttackBehavior(Owner);
					end
				else
					AI.OldTargetPos = nil;
					AI.Target = FoundMO;
					AI.TargetOffset = SceneMan:ShortestDistance(AI.Target.Pos, HitPoint, false); -- this is the distance vector from the target center to the point we hit with our ray
					AI:CreateAttackBehavior(Owner);
				end
			end
		end
	else	-- no target found this frame
		if AI.Target and AI.TargetLostTimer:IsPastSimTimeLimit() then
			AI.Target = nil; -- the target has been out of sight for too long, ignore it
			AI:CreatePinBehavior(Owner); -- keep aiming in the direction of the target for a short time
		end

		if AI.ReloadTimer:IsPastSimMS(8000) then	-- check if we need to reload
			AI.ReloadTimer:Reset();
			if Owner.FirearmNeedsReload then
				Owner:ReloadFirearms();
			end
		end
	end
end

-- in sentry behavior the agent only looks for new enemies, it sometimes sharp aims to increase spotting range
function CrabBehaviors.Sentry(AI, Owner, Abort)
	local sweepUp = true;
	local sweepDone = false;
	-- to-do: refer to upper/lower limits!
	local maxAng = Owner.AimRange; --Owner.AimRangeUpperLimit
	local minAng = -maxAng; --Owner.AimRangeLowerLimit
	local aim;

	if AI.OldTargetPos then	-- try to reacquire an old target
		local Dist = SceneMan:ShortestDistance(Owner.EyePos, AI.OldTargetPos, false);
		AI.OldTargetPos = nil;
		if (Dist.X < 0 and Owner.HFlipped) or (Dist.X > 0 and not Owner.HFlipped) then	-- we are facing the target
			AI.deviceState = ACrab.AIMING;
			AI.Ctrl.AnalogAim = Dist.Normalized;

			for _ = 1, 30 do
				local _ai, _ownr, _abrt = coroutine.yield(); -- aim here for ~0.5s
				if _abrt then return true end
			end
		end
	elseif not AI.isPlayerOwned then -- face the most likely enemy approach direction
		for _ = 1, math.random(5) do	-- wait for a while
			local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
			if _abrt then return true end
		end

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
	end

	-- (Posted facing a way, RC-4, or sent somewhere facing a way, RC-5: that is the way to watch. After the look for the likely way an
	-- enemy comes, which would otherwise turn it to face that.)
	if Owner.OrderPostFacing ~= 0 then
		AI.SentryFacing = Owner.OrderPostFacing < 0;
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

	if Owner.HFlipped ~= AI.SentryFacing then
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
	AI.deviceState = ACrab.AIMING;

	while true do	-- scan the area for obstacles
		aim = Owner:GetAimAngle(false);
		if aim < maxAng then
			AI.Ctrl:SetState(Controller.AIM_UP, true);
		else
			break;
		end

		-- save the angle to a table if there is no obstacle
		if not SceneMan:CastStrengthRay(Owner.EyePos, Vector(60, 0):RadRotate(Owner:GetAimAngle(true)), 5, Hit, 2, 0, true) then
			table.insert(NoObstacle, aim); -- TODO: don't use a table for this
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	local SharpTimer = Timer();
	local IdleAimTimer = Timer();
	local sharpAimTime = AI.idleAimTime;
	local angDiff = 1;
	AI.deviceState = ACrab.POINTING;

	if #NoObstacle > 1 then	-- only aim where we know there are no obstacles, e.g. out of a gun port
		minAng = NoObstacle[1] * 0.95;
		maxAng = NoObstacle[#NoObstacle] * 0.95;
		angDiff = 1 / math.max(math.abs(maxAng - minAng), 0.1); -- sharp aim longer from a small aiming window
	end

	while true do
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

			if AI.deviceState == ACrab.AIMING then
				sharpAimTime = RangeRand(AI.idleAimTime * 0.5, AI.idleAimTime * 1.5);
				AI.deviceState = ACrab.POINTING;
			else
				sharpAimTime = RangeRand(AI.idleAimTime * 3, AI.idleAimTime * 6) * angDiff;
				AI.deviceState = ACrab.AIMING;
			end

			if Owner.HFlipped ~= AI.SentryFacing then
				Owner.HFlipped = AI.SentryFacing; -- turn to the direction we have been order to guard
				break; -- restart this behavior
			end
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	return true;
end

-- open fire on the selected target
function CrabBehaviors.ShootTarget(AI, Owner, Abort)
	if not MovableMan:ValidMO(AI.Target) then
		return true;
	end

	AI.ReloadTimer:Reset();
	AI.TargetLostTimer:Reset();
	AI.TargetLostTimer:SetSimTimeLimitMS(1500);

	local LOSTimer = Timer();
	LOSTimer:SetSimTimeLimitMS(450);

	local TargetAvgVel = Vector(AI.Target.Vel.X, AI.Target.Vel.Y);
	local ShootTimer = Timer();
	local aimTime = RangeRand(440, 590) * AI.aimSpeed + 150;
	local aimError = RangeRand(-0.3, 0.3) * AI.aimSkill;
	local AimPoint = Vector(AI.Target.Pos.X, AI.Target.Pos.Y);
	local f1, f2 = 0.5, 0.5; -- aim noise filter
	local openFire = 0;
	-- The weapon's reach and drop (AC-9), as the humans have them: aimed over the drop, and not fired at a target out of reach. A crab
	-- that can't reach or can't see its target for a second and a half works round to somewhere it can (a turret can't move).
	local PrjDat;
	local BlockedTimer;
	local OutOfReachTimer; -- (A turret's: how long its target has been out of its reach.)

	-- spin up asap
	if Owner.FirearmActivationDelay > 0 then
		aimTime = math.max(50*AI.aimSpeed, aimTime-Owner.FirearmActivationDelay);
	end

	while true do
		if Owner.FirearmIsReady then
			AI.deviceState = ACrab.AIMING;
			local Dist = SceneMan:ShortestDistance(Owner.EyePos, AimPoint, false);
			local Weapon = ToHDFirearm(Owner.EquippedItem);
			local magName = Weapon.Magazine and Weapon.Magazine.PresetName or "";
			if not PrjDat or PrjDat.MagazineName ~= magName then
				PrjDat = SharedBehaviors.GetProjectileData(Owner);
			end
			-- Lead a moving target as the humans do, by the smoothed velocity (kept, but never used before).
			local fireVel = Weapon:GetAIFireVel();
			if fireVel > 0 then
				local timeToTarget = Dist.Magnitude / fireVel;
				if timeToTarget * TargetAvgVel.Magnitude > 2 then
					Dist = SceneMan:ShortestDistance(Owner.EyePos, AimPoint + TargetAvgVel * timeToTarget, false);
				end
			end

			if Owner.HFlipped then
				if Dist.X > 0 then
					Owner.HFlipped = false;
				end
			elseif Dist.X < 0 then
				Owner.HFlipped = true;
			end

			-- Over the drop where the weapon has one; nil when the target is out of reach (too far, or no arc gets there).
			local ballistic = Dist:MagnitudeIsLessThan(PrjDat.rng) and HumanBehaviors.GetAngleToHit(PrjDat, Dist) or nil;
			if not ballistic and not AI.isTurret and BlockedTimer == nil then
				BlockedTimer = Timer();
			end
			-- A turret can't go after a target out of its reach: after three seconds it lets it go, to take a nearer one.
			if AI.isTurret and not ballistic then
				OutOfReachTimer = OutOfReachTimer or Timer();
				if OutOfReachTimer:IsPastSimMS(3000) then
					AI.fire = false;
					break;
				end
			else
				OutOfReachTimer = nil;
			end

			-- add some filtered noise to the aim
			local aim = Owner:GetAimAngle(true);
			local aimTarget = (ballistic or Dist.AbsRadAngle) + aimError;
			local noise = RangeRand(-40, 40) * AI.aimSpeed;
			f1, f2 = 0.9*f1+noise*0.1, 0.7*f2+noise*0.3;
			noise = f1 + f2 + noise * 0.1;
			aimTarget = (aimTarget or aim) + math.min(math.max(noise/(Dist.Largest+30), -0.12), 0.12);

			local angDiff = aim - aimTarget;
			if angDiff > math.pi then
				angDiff = angDiff - math.pi * 2;
			elseif angDiff < -math.pi then
				angDiff = angDiff + math.pi * 2;
			end

			local angChange = math.max(math.min(angDiff*(0.12/AI.aimSkill), 0.3), -0.3);
			if (angDiff > 0 and angChange > angDiff) or (angDiff < 0 and angChange < angDiff) then
				angChange = angDiff;
			end
			AI.Ctrl.AnalogAim = Vector(1,0):RadRotate(aim-angChange);

			if not ballistic then
				openFire = 0; -- (Out of reach: not a round wasted on it.)
			elseif ShootTimer:IsPastSimMS(aimTime) then
				aimError = aimError * RangeRand(0.96, 0.99);

				-- open fire if our aim overlap the target
				local overlap = AI.Target.Diameter * math.max(AI.aimSkill, 0.4);
				if ToHDFirearm(Owner.EquippedItem).FullAuto then
					if math.abs(angDiff) < math.tanh((overlap*2.2)/(Dist.Magnitude+10)) then
						openFire = 30; -- don't stop shooting just because we lose the target for a few frames
					end
				elseif math.abs(angDiff) < math.tanh((overlap*1.5)/(Dist.Magnitude+10)) then
					openFire = 1;
				end
			end
		else
			AI.deviceState = ACrab.POINTING;

			ShootTimer:Reset();
			if Owner.FirearmIsEmpty then
				Owner:ReloadFirearms();
				aimError = RangeRand(-0.14, 0.14) * AI.aimSkill;
				aimTime = RangeRand(220, 330) * AI.aimSpeed + 50;
				if Owner.FirearmActivationDelay > 0 then
					aimTime = math.max(50*AI.aimSpeed, aimTime-Owner.FirearmActivationDelay);
				end
			else
				break; -- no firearm available
			end
		end

		if LOSTimer:IsPastSimTimeLimit() then
			LOSTimer:Reset();

			if AimPoint and (not AI.isPlayerOwned or not SceneMan:IsUnseen(AimPoint.X, AimPoint.Y, Owner.Team)) then
				-- periodically check that we can see the target
				local Dist = SceneMan:ShortestDistance(Owner.EyePos, AimPoint, false);
				local viewLen = SceneMan:ShortestDistance(Owner.EyePos, Owner.ViewPoint, false).Magnitude + FrameMan.PlayerScreenWidth * 0.55; -- TODO: get AimDistance and SharpLength from the ini

				if Dist:MagnitudeIsLessThan(viewLen) then
					local ID = SceneMan:CastMORay(Owner.EyePos, Dist, Owner.ID, Owner.IgnoresWhichTeam, rte.grassID, false, 9);
					if ID ~= rte.NoMOID and (ID == AI.Target.ID or (MovableMan:GetMOFromID(ID)).RootID == AI.Target.ID) then
						AI.TargetLostTimer:Reset(); -- we can see the target
						if PrjDat and Dist:MagnitudeIsLessThan(PrjDat.rng) then
							BlockedTimer = nil;
						end
					elseif not AI.isTurret and BlockedTimer == nil then
						BlockedTimer = Timer();
					end
				end
			end
			-- Out of reach or out of sight for a second and a half: somewhere else to shoot it from (AC-9).
			-- (A flank's routes asked for: started once they are back.)
			if AI.FlankSearch then
				if SharedBehaviors.StartFlank(AI, Owner, AI.Target.Pos, PrjDat and PrjDat.rng < 2000 and PrjDat.rng or 500) then
					break;
				end
			elseif BlockedTimer and BlockedTimer:IsPastSimMS(1500) then
				BlockedTimer = nil;
				if SharedBehaviors.StartFlank(AI, Owner, AI.Target.Pos, PrjDat and PrjDat.rng < 2000 and PrjDat.rng or 500) then
					break;
				end
			end
		end

		if openFire > 0 then
			AI.fire = true;
		else
			AI.fire = false;
		end

		openFire = openFire - 1;

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end

		if not AI.Target or AI.Target:IsDead() then
			AI.Target = nil;

			-- the target is gone, try to find another right away
			local ClosestEnemy = MovableMan:GetClosestEnemyActor(Owner.Team, AimPoint, 200, Vector());
			if ClosestEnemy and not ClosestEnemy:IsDead() then
				if ClosestEnemy.ClassName == "AHuman" then
					ClosestEnemy = ToAHuman(ClosestEnemy);
				elseif ClosestEnemy.ClassName == "ACrab" then
					ClosestEnemy = ToACrab(ClosestEnemy);
				elseif ClosestEnemy.ClassName == "AVehicle" then
					ClosestEnemy = ToAVehicle(ClosestEnemy);
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

		-- make target data smoother
		TargetAvgVel = TargetAvgVel * 0.2 + AI.Target.Vel * 0.8;
		AimPoint = AimPoint * 0.6 + (AI.Target.Pos + AI.TargetOffset) * 0.4;
		AI.TargetOffset = AI.TargetOffset * 0.98; -- move the aim point towards the target center
	end

	return true;
end

-- open fire on the area around the selected target
function CrabBehaviors.ShootArea(AI, Owner, Abort)
	if not MovableMan:ValidMO(AI.UnseenTarget) then
		return true;
	end

	local StartPos = Vector(AI.UnseenTarget.Pos.X, AI.UnseenTarget.Pos.Y);

	-- aim at the target in case we can see it when sharp aiming
	Owner:SetAimAngle(SceneMan:ShortestDistance(Owner.EyePos, StartPos, false).AbsRadAngle);
	AI.deviceState = ACrab.AIMING;

	-- aim for ~250ms
	for _ = 1, 15 do
		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	local AimPoint;
	for _ = 1, 5 do	-- try up to five times to find a target area that is resonably close to the target
		AimPoint = StartPos + Vector(RangeRand(-100, 100), RangeRand(-100, 50));
		if AimPoint.X > SceneMan.SceneWidth then
			AimPoint.X = SceneMan.SceneWidth - AimPoint.X;
		elseif AimPoint.X < 0 then
			AimPoint.X = AimPoint.X + SceneMan.SceneWidth;
		end

		-- check if we can fire at the AimPoint
		local Trace = SceneMan:ShortestDistance(Owner.EyePos, AimPoint, false);
		local rayLength = SceneMan:CastObstacleRay(Owner.EyePos, Trace, Vector(), Vector(), rte.NoMOID, Owner.IgnoresWhichTeam, rte.grassID, 11, true); -- (sees through water as far as units do)
		if Trace:MagnitudeIsLessThan(rayLength * 1.5)  then
			break; -- the AimPoint is close enough to the target, start shooting
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	local CheckTargetTimer = Timer();
	local ShootTimer = Timer();
	local aimTime = RangeRand(200, 500) * AI.aimSpeed;
	local aimError = RangeRand(-0.3, 0.3) * AI.aimSkill;
	local aim = SceneMan:ShortestDistance(Owner.EyePos, AimPoint, false).AbsRadAngle;

	while true do
		if Owner.FirearmIsReady then
			AI.deviceState = ACrab.AIMING;
			AI.Ctrl.AnalogAim = Vector(1,0):RadRotate(aim+aimError+RangeRand(-0.01, 0.01)*AI.aimSkill);

			if ShootTimer:IsPastSimMS(aimTime) then
				AI.fire = true;
				aimError = aimError * RangeRand(0.982, 0.995);
			else
				AI.fire = false;
			end
		else
			AI.deviceState = ACrab.POINTING;
			AI.fire = false;

			if Owner.FirearmIsEmpty then
				Owner:ReloadFirearms();
			end

			break; -- stop this behavior when the mag is empty
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end

		if AI.UnseenTarget and CheckTargetTimer:IsPastSimMS(400) then
			if MovableMan:ValidMO(AI.UnseenTarget) and (AI.UnseenTarget.ClassName == "AHuman" or AI.UnseenTarget.ClassName == "ACrab") then
				CheckTargetTimer:Reset();
				if AI.UnseenTarget:GetController() and AI.UnseenTarget:GetController():IsState(Controller.WEAPON_FIRE) then
					-- compare the enemy aim angle with the angle of the alarm vector
					local enemyAim = AI.UnseenTarget:GetAimAngle(true);
					if enemyAim > math.pi*2 then	-- make sure the angle is in the [0..2*pi] range
						enemyAim = enemyAim - math.pi*2;
					elseif enemyAim < 0 then
						enemyAim = enemyAim + math.pi*2;
					end

					local angDiff = SceneMan:ShortestDistance(AI.UnseenTarget.Pos, Owner.Pos, false).AbsRadAngle - enemyAim;
					if angDiff > math.pi then	-- the difference between two angles can never be larger than pi
						angDiff = angDiff - math.pi*2;
					elseif angDiff < -math.pi then
						angDiff = angDiff + math.pi*2;
					end

					if math.abs(angDiff) < 0.5 then
						-- this actor is shooting in our direction
						AimPoint = AI.UnseenTarget.Pos + SceneMan:ShortestDistance(AI.UnseenTarget.Pos, AimPoint, false) / 2 + Vector(RangeRand(-40, 40)*AI.aimSkill, RangeRand(-40, 40)*AI.aimSkill);
						aimError = RangeRand(-0.2, 0.2) * AI.aimSkill;
						aim = SceneMan:ShortestDistance(Owner.EyePos, AimPoint, false).AbsRadAngle;
					end
				end
			else
				AI.UnseenTarget = nil;
			end
		end
	end

	return true;
end

-- stop the user from inadvertently modifying the storage table
-- Mods written for older versions call the behaviours that are shared between kinds of unit (Patrol, GoToWpt, BrainSearch, GetTeamShootingSkill and so on) through this table.
-- They live in SharedBehaviors now: anything not found here is looked up there, so those mods keep working.
require("AI/SharedBehaviors");
local Storage = CrabBehaviors;
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
		error("The CrabBehaviors table is read-only.", 2);
	end
};
setmetatable(Proxy, Mt);
CrabBehaviors = Proxy;
