require("Constants")
require("AI/HumanBehaviors");
require("AI/SharedBehaviors");
require("AI/UnitSpeech");

NativeHumanAI = {};

function NativeHumanAI:Create(Owner)
	local Members = {};

	Members.lateralMoveState = Actor.LAT_STILL;
	Members.proneState = AHuman.NOTPRONE;
	Members.jumpState = AHuman.NOTJUMPING;
	Members.deviceState = AHuman.STILL;
	Members.lastAIMode = Actor.AIMODE_NONE;
	Members.JumpHoldTimer = Timer();
	Members.JumpHoldTimer:SetSimTimeLimitMS(150);
	Members.teamBlockState = Actor.NOTBLOCKED;
	Members.SentryFacing = Owner.HFlipped;
	Members.fire = false;
	Members.groundContact = 5;
	Members.flying = false;
	Members.jetClimb = false; -- Climbing on the jetpack to a waypoint above; held until up at its height.
	Members.jetSteady = false; -- The movement script asks for the jet without a burst (a climb's pulses; a burst is a kick and a burst's worth of fuel).
	Members.running = false;

	Members.squadShoot = false;
	Members.useMedikit = false;
	Members.medicHeal = false;

	-- timers
	Members.AirTimer = Timer();
	Members.PickUpTimer = Timer();
	Members.ReloadTimer = Timer();
	Members.BlockedTimer = Timer();
	Members.SquadShootTimer = Timer();
	Members.SquadShootDelay = math.random(50,100);

	Members.RunStateTimer = Timer();
	Members.RunStateTimer:SetSimTimeLimitMS(math.random(2000,5000));

	Members.AlarmTimer = Timer();
	Members.AlarmTimer:SetSimTimeLimitMS(400);

	Members.TargetLostTimer = Timer();
	Members.TargetLostTimer:SetSimTimeLimitMS(1000);
	
	-- customizable variables, trend started by pawnis on 01/09/2023 :)
	
	-- humble beginnings
	-- pause time between sweeping aim up and down when guarding
	Members.idleAimTime = Owner:NumberValueExists("AIIdleAimTime") and Owner:GetNumberValue("AIIdleAimTime") or 500;

	-- set shooting skill
	Members.aimSpeed, Members.aimSkill, Members.skill = SharedBehaviors.GetTeamShootingSkill(Owner.Team);
	
	Members.aimSpeed = Owner:NumberValueExists("AIAimSpeed") and Owner:GetNumberValue("AIAimSpeed") or Members.aimSpeed;
	Members.aimSkill = Owner:NumberValueExists("AIAimSkill") and Owner:GetNumberValue("AIAimSkill") or Members.aimSkill;
	Members.skill = Owner:NumberValueExists("AISkill") and Owner:GetNumberValue("AISkill") or Members.skill;
	
	-- default to enhanced AI if AI skill has been set high enough (an AI skill, 1 to 100: Good or Unfair, not a difficulty constant)
	-- (The engine's scan where the build has it: everyone looks the same way, keener units wider; see HumanBehaviors.ScanTargets.)
	if SharedBehaviors.CanScan(Owner) then
		Members.SpotTargets = HumanBehaviors.ScanTargets;
	elseif Members.skill >= Activity.GOODSKILL or Owner:HasObjectInGroup("Brains") or Owner:HasObjectInGroup("Actors - Snipers") or Owner:HasObjectInGroup("Actors - Boss") then
		Members.SpotTargets = HumanBehaviors.CheckEnemyLOS;
	else
		Members.SpotTargets = HumanBehaviors.LookForTargets;
	end

	-- check if this team is controlled by a human
	if ActivityMan:GetActivity():IsHumanTeam(Owner.Team) then
		Members.isPlayerOwned = true;
		Members.PlayerInterferedTimer = Timer();
		Members.PlayerInterferedTimer:SetSimTimeLimitMS(500);
	end

	-- the native AI assume the jetpack cannot be destroyed
	if Owner.Jetpack then
		if not Members.isPlayerOwned then
			Owner.Jetpack.JetTimeTotal = Owner.Jetpack.JetTimeTotal * 1.2;	-- increase jetpack fuel to compensate for extra fuel spend
		end

		Members.minBurstTime = math.min(Owner.Jetpack.BurstSpacing*2, Owner.Jetpack.JetTimeTotal*0.99); -- in milliseconds
	end

	setmetatable(Members, self);
	self.__index = self;
	return Members;
end

function NativeHumanAI:Update(Owner)
	self.Ctrl = Owner:GetController();

	-- Our jetpack might have thrust balancing enabled, so update for our current mass
	if Owner.Jetpack then		
		self.jetImpulseFactor = Owner.Jetpack:EstimateImpulse(false) * GetPPM() / TimerMan.DeltaTimeSecs;
		self.jetBurstFactor = (Owner.Jetpack:EstimateImpulse(true) * GetPPM() / TimerMan.DeltaTimeSecs - self.jetImpulseFactor) * math.pow(TimerMan.DeltaTimeSecs, 2) * 0.5;
	end

	if self.isPlayerOwned then
		if self.PlayerInterferedTimer:IsPastSimTimeLimit() then
			-- Tell the coroutines to abort to avoid memory leaks
			if self.Behavior then
				local msg, done = coroutine.resume(self.Behavior, self, Owner, true);
			end

			self.Behavior = nil; -- remove the current behavior
			self.BehaviorName = nil;
			if self.BehaviorCleanup then
				self.BehaviorCleanup(self); -- clean up after the current behavior
				self.BehaviorCleanup = nil;
			end

			-- Tell the coroutines to abort to avoid memory leaks
			if self.GoToBehavior then
				local msg, done = coroutine.resume(self.GoToBehavior, self, Owner, true);
			end

			self.GoToBehavior = nil;
			self.GoToName = nil;
			if self.GoToCleanup then
				self.GoToCleanup(self);
				self.GoToCleanup = nil;
			end

			self.Target = nil;
			self.UnseenTarget = nil;
			self.OldTargetPos = nil;
			self.PickupHD = nil;

			self.fire = false;
			self.canHitTarget = false;
			self.jump = false;

			self.squadShoot = false;
			self.useMedikit = false;

			-- The fighting rules' state goes too: a unit a player took over mid-retreat kept its retreating tag, and was a "move" order to
			-- the rules (and left alone by the sandbox) ever after.
			self.closingIn = false;
			self.Cover = nil;
			self.Flank = nil;
			self.Retreat = nil;
			self.Investigate = nil;
			Owner:RemoveNumberValue("AIRetreat");
			Owner:RemoveNumberValue("AIFlank");
			Owner:RemoveNumberValue("AIInvestigate");
			Owner:RemoveNumberValue("AITargetID");
			self.overwatch = false;
			-- (And a medic's errand, AC-7: the friend it was going to is free for another medic.)
			self.Medic = nil;
			self.medicHeal = false;
			Owner:RemoveNumberValue("AIMedic");
			Owner:RemoveNumberValue("AIMedicFor");

			self.proneState = AHuman.NOTPRONE;
			self.SentryFacing = Owner.HFlipped;
			self.deviceState = AHuman.STILL;
			self.lastAIMode = Actor.AIMODE_NONE;
			self.teamBlockState = Actor.NOTBLOCKED;

			if Owner.EquippedItem then
				self.PlayerPreferredHD = Owner.EquippedItem:GetModuleAndPresetName();
			else
				self.PlayerPreferredHD = nil;
			end
		end

		self.PlayerInterferedTimer:Reset();
	end

	if self.Target and not MovableMan:ValidMO(self.Target) then
		self.Target = nil;
	end

	if self.UnseenTarget and not MovableMan:ValidMO(self.UnseenTarget) then
		self.UnseenTarget = nil;
	end

	-- The AI's state for the unit inspector, when a debug overlay wants it (see SharedBehaviors.ExportDebugState).
	SharedBehaviors.ExportDebugState(self, Owner);

	-- switch to the next behavior, if available
	if self.NextBehavior then
		if self.BehaviorCleanup then
			self.BehaviorCleanup(self);
		end

		-- Tell the coroutines to abort to avoid memory leaks
		if self.Behavior then
			local msg, done = coroutine.resume(self.Behavior, self, Owner, true);
		end

		self.Behavior = self.NextBehavior;
		self.BehaviorCleanup = self.NextCleanup;
		self.BehaviorName = self.NextBehaviorName;

		self.NextBehavior = nil;
		self.NextCleanup = nil;
		self.NextBehaviorName = nil;
	end

	-- switch to the next GoTo behavior, if available
	if self.NextGoTo then
		if self.GoToCleanup then
			self.GoToCleanup(self);
		end

		-- Tell the coroutines to abort to avoid memory leaks
		if self.GoToBehavior then
			local msg, done = coroutine.resume(self.GoToBehavior, self, Owner, true);
		end

		self.GoToBehavior = self.NextGoTo;
		self.GoToCleanup = self.NextGoToCleanup;
		self.GoToName = self.NextGoToName;

		self.NextGoTo = nil;
		self.NextGoToCleanup = nil;
		self.NextGoToName = nil;
	end

	-- An attack order picks and re-picks its own enemy here (see SharedBehaviors.AttackOrderUpdate), before the new-order check below takes up a redirect.
	SharedBehaviors.AttackOrderUpdate(self, Owner);

	-- Running the objective (carrying a flag home): nothing else it was doing is kept (see SharedBehaviors.OnObjective).
	local objective = SharedBehaviors.OnObjective(Owner);
	if objective then
		SharedBehaviors.FocusOnObjective(self, Owner);
	elseif SharedBehaviors.Rushing(Owner) then
		SharedBehaviors.FocusOnRush(self, Owner);
	end

	-- check if the AI mode has changed or if we need a new behavior
	-- (Or if we're told to go somewhere and aren't: after arriving the mode stays GOTO while the behaviour is Sentry, and a new order with new
	-- waypoints then looked like no change at all, so the unit never set off until the mode was knocked out of GOTO and back.)
	local newOrder = (Owner.AIMode == Actor.AIMODE_GOTO or Owner.AIMode == Actor.AIMODE_SQUAD) and not self.GoToBehavior and not self.NextGoTo and (Owner:GetWaypointListSize() > 0 or Owner.MOMoveTarget);
	-- (And any order given since this AI's own last update, even one to the mode it is in: the order serial counts them, and the count is
	-- taken again at the end of each update, so the AI's own waypoint and mode writes are not taken for orders.)
	local ordered = self.orderSerial ~= nil and Owner.AIOrderSerial ~= self.orderSerial;
	if Owner.AIMode ~= self.lastAIMode or not(self.Behavior or self.GoToBehavior) or newOrder or ordered then
		-- Tell the coroutines to abort to avoid memory leaks
		if self.Behavior then
			local msg, done = coroutine.resume(self.Behavior, self, Owner, true);
		end

		self.Behavior = nil;
		if self.BehaviorCleanup then
			self.BehaviorCleanup(self); -- stop the current behavior
			self.BehaviorCleanup = nil;
		end

		-- Tell the coroutines to abort to avoid memory leaks
		if self.GoToBehavior then
			local msg, done = coroutine.resume(self.GoToBehavior, self, Owner, true);
		end

		self.GoToBehavior = nil;
		if self.GoToCleanup then
			self.GoToCleanup(self);
			self.GoToCleanup = nil;
		end

		-- select a new behavior based on AI mode
		if Owner.AIMode == Actor.AIMODE_GOTO or Owner.AIMode == Actor.AIMODE_SQUAD then
			self:CreateGoToBehavior(Owner);
		elseif Owner.AIMode == Actor.AIMODE_BRAINHUNT then
			self:CreateBrainSearchBehavior(Owner);
		elseif Owner.AIMode == Actor.AIMODE_GOLDDIG then
			self:CreateGoldDigBehavior(Owner);
		elseif Owner.AIMode == Actor.AIMODE_PATROL then
			self:CreatePatrolBehavior(Owner);
		else
			if (Owner.AIMode ~= self.lastAIMode or ordered) and Owner.AIMode == Actor.AIMODE_SENTRY then
				self.SentryFacing = Owner.HFlipped; -- store the direction in which we should be looking
				self.SentryPos = Vector(Owner.Pos.X, Owner.Pos.Y); -- store the pos on which we should be standing
				-- (Back at its post after a fall-back (SharedBehaviors.RestoreOrder): the post's own place and facing, not where it stopped.)
				if self.ReturnPost and not SceneMan:ShortestDistance(Owner.Pos, self.ReturnPost.Pos, false):MagnitudeIsGreaterThan(Owner.Height) then
					self.SentryPos = Vector(self.ReturnPost.Pos.X, self.ReturnPost.Pos.Y);
					if self.ReturnPost.facing ~= nil then
						self.SentryFacing = self.ReturnPost.facing;
					end
				end
				self.ReturnPost = nil;
			end

			self:CreateSentryBehavior(Owner);
		end

		self.lastAIMode = Owner.AIMode;
	end


	-- check if the feet reach the ground
	if self.AirTimer:IsPastSimMS(250) then
		self.AirTimer:Reset();

		local Origin = {};
		if Owner.FGFoot then
			table.insert(Origin, Vector(Owner.FGFoot.Pos.X, Owner.FGFoot.Pos.Y) + Vector(0, 4));
		end
		if Owner.BGFoot then
			table.insert(Origin, Vector(Owner.BGFoot.Pos.X, Owner.BGFoot.Pos.Y) + Vector(0, 4));
		end
		if #Origin == 0 then
			table.insert(Origin, Vector(Owner.Pos.X, Owner.Pos.Y) + Vector(0, 4 + ToMOSprite(Owner):GetSpriteHeight() + Owner.SpriteOffset.Y));
		end
		for i = 1, #Origin do
			if SceneMan:GetTerrMatter(Origin[i].X, Origin[i].Y) ~= rte.airID then
				self.groundContact = 3;
				break;
			else
				self.groundContact = self.groundContact - 1;
			end
		end

		local newFlying = self.groundContact < 0;

		if self.flying ~= newFlying then
			Owner:SendMessage("AI_IsFlying", newFlying);
			self.flying = newFlying;
		end

		Owner:EquipShieldInBGArm(); -- try to equip a shield
	end

	-- look for targets
	local FoundMO, HitPoint = self.SpotTargets(self, Owner, self.skill);
	if FoundMO then
		--TODO: decide whether to attack based on the material strength of found MO
		if self.Behavior ~= nil and self.Target and MovableMan:ValidMO(self.Target) and FoundMO.ID == self.Target.ID then	-- found the same target
			SharedBehaviors.ReportEnemy(Owner, self.Target);
			self.OldTargetPos = Vector(self.Target.Pos.X, self.Target.Pos.Y);
			self.TargetOffset = SceneMan:ShortestDistance(self.Target.Pos, HitPoint, false);
			self.TargetLostTimer:Reset();
			self.ReloadTimer:Reset();
		elseif FoundMO.Team ~= Owner.Team then	-- found an enemy
			if FoundMO.ClassName == "AHuman" then
				FoundMO = ToAHuman(FoundMO);
			elseif FoundMO.ClassName == "ACrab" then
				FoundMO = ToACrab(FoundMO);
			elseif FoundMO.ClassName == "ACRocket" then
				FoundMO = ToACRocket(FoundMO);
			elseif FoundMO.ClassName == "ACDropShip" then
				FoundMO = ToACDropShip(FoundMO);
			elseif FoundMO.ClassName == "ADoor" and FoundMO.Team ~= Activity.NOTEAM and Owner.AIMode ~= Actor.AIMODE_SENTRY and ToADoor(FoundMO).Door and ToADoor(FoundMO).Door:IsAttached() then
				FoundMO = ToADoor(FoundMO);
			elseif FoundMO.ClassName == "AVehicle" then	-- A cart or other vehicle (VH-1): shot to pieces like any enemy, driven or not.
				FoundMO = ToAVehicle(FoundMO);
			elseif FoundMO.ClassName == "Actor" then
				FoundMO = ToActor(FoundMO);
			else
				FoundMO = nil;
			end

			if FoundMO and FoundMO.Status < Actor.INACTIVE then
				-- The team hears of it (AC-2).
				SharedBehaviors.ReportEnemy(Owner, FoundMO);
				if self.Target and MovableMan:ValidMO(self.Target) and FoundMO.ID == self.Target.ID then
					-- The same target, with no fight under way: a new order (a sandbox re-send hops SENTRY to GOTO) aborted the attack, and in
					-- GOTO nothing made a new one while the target lived, so the unit held its fire for up to 5 s, until it lost sight of it.
					self.TargetOffset = SceneMan:ShortestDistance(self.Target.Pos, HitPoint, false);
					self.TargetLostTimer:Reset();
					if not self.NextBehavior then
						self:CreateAttackBehavior(Owner);
					end
				elseif self.Target then
					-- check if this MO should be targeted instead
					if SharedBehaviors.CalculateThreatLevel(FoundMO, Owner) > SharedBehaviors.CalculateThreatLevel(self.Target, Owner) + 0.5 then
						self.OldTargetPos = Vector(self.Target.Pos.X, self.Target.Pos.Y);
						self.Target = FoundMO;
						self.TargetOffset = SceneMan:ShortestDistance(self.Target.Pos, HitPoint, false); -- this is the distance vector from the target center to the point we hit with our ray
						if self.NextBehaviorName ~= "ShootTarget" then
							self:CreateAttackBehavior(Owner);
						end
					end
				else
					self.OldTargetPos = nil;
					self.Target = FoundMO;
					self.TargetOffset = SceneMan:ShortestDistance(self.Target.Pos, HitPoint, false); -- this is the distance vector from the target center to the point we hit with our ray
					self:CreateAttackBehavior(Owner);
				end
			end
		end
	else -- no target found this frame
		if self.Target and self.TargetLostTimer:IsPastSimTimeLimit() then
			self.Target = nil; -- the target has been out of sight for too long, ignore it
			self:CreatePinBehavior(Owner); -- keep aiming in the direction of the target for a short time
		end

		if self.ReloadTimer:IsPastSimMS(8000) then	-- check if we need to reload
			if Owner.FirearmNeedsReload then
				Owner:ReloadFirearms();
				if not Owner.FirearmNeedsReload then -- account for BG weapon needing to reload separately
					self.ReloadTimer:Reset();
				end
			elseif not HumanBehaviors.EquipPreferredWeapon(self, Owner) then	-- make sure we equip a preferred or a primary weapon if we have one
				self.ReloadTimer:Reset();
			end
		end
	end

	local AlarmPoint = Owner:GetAlarmPoint();

	-- If we currently have a target or are alerted, we walk. Otherwise we run
	-- We also have a small random chance to walk for a lil bit
	local wasAlarmed = AlarmPoint.Largest > 0;
	if wasAlarmed or self.RunStateTimer:IsPastSimTimeLimit() then
		self.running = self.Target == nil and not wasAlarmed and math.random() < 0.6;
		self.RunStateTimer:Reset();
	end

	-- (Not over the engine's route-follower, which runs where the way is long, level and open; see AHuman::MoveAlongRoute.)
	if not self.engineMover and not SharedBehaviors.EngineMotor(Owner) then
		self.Ctrl:SetState(Controller.MOVE_FAST, self.running);
	end

	self.squadShoot = false;
	if Owner.MOMoveTarget then
		-- make the last waypoint marker stick to the MO we are following
		if MovableMan:ValidMO(Owner.MOMoveTarget) then
			local Leader = nil;
			if Owner.AIMode == Actor.AIMODE_SQUAD then
				Leader = MovableMan:GetMOFromID(Owner:GetAIMOWaypointID());
				if Leader then
					if IsAHuman(Leader) then
						Leader = ToAHuman(Leader);
					elseif IsACrab(Leader) then
						Leader = ToACrab(Leader);
					else
						Leader = nil;
					end
				end
			end
			if Leader then
				-- A place in line behind the leader, so far back along the way it came (SharedBehaviors.SquadPoint): the route ends there,
				-- not at the leader, which every follower used to steer straight at and shove for.
				local Point, LeaderGround = SharedBehaviors.SquadPoint(self, Owner, Leader);
				self.squadPoint = Point;
				SharedBehaviors.SquadTrimPath(Owner, Point, LeaderGround);
				SharedBehaviors.DrawSquadDebug(self, Owner, Leader, Point);
			else
				-- make the last waypoint marker stick to the MO we are following
				self.squadPoint = nil;
				Owner:RemoveMovePathEnd();
				Owner:AddToMovePathEnd(Owner.MOMoveTarget.Pos);
			end

			if Owner.AIMode == Actor.AIMODE_SQUAD then
				-- look where the SL looks, if not moving
				-- (Not while the engine's route-follower drives: the copied keys went on top of its own, and a follower near its leader
				-- jumped and walked as the leader did instead of as its route said.)
				if not self.jump and self.lateralMoveState == Actor.LAT_STILL and not self.engineMover then
					if Leader then
						local dist = SceneMan:ShortestDistance(Owner.Pos, Leader.Pos, false).Largest;
						local radius = (Leader.Height + Owner.Height) * 0.5;
						if dist < radius then
							-- (The keys only for the script's own mover. On the engine's, this is a follower held on its place (FollowStep), which
							-- is when engineMover is off: copied, the leader's jet lit the held follower's, AI.flying broke the hold, and the
							-- route-follower took over mid-air; and the leader's prone or step moved it off its place.)
							if not SharedBehaviors.UsesEngineMover(Owner) then
								local copyControls = {Controller.MOVE_LEFT, Controller.MOVE_RIGHT, Controller.BODY_JUMPSTART, Controller.BODY_JUMP, Controller.BODY_PRONE};
								for _, control in pairs(copyControls) do
									local state = Leader:GetController():IsState(control);
									self.Ctrl:SetState(control, state);
								end
							end
							if Leader.EquippedItem then
								local aimDelta = SceneMan:ShortestDistance(Leader.Pos, Leader.ViewPoint, false);
								
								if IsHDFirearm(Leader.EquippedItem) then
									local aimCorrectionRatio = Leader.SharpAimProgress * (dist/radius);
									self.Ctrl.AnalogAim = (aimDelta * (1 - aimCorrectionRatio) + SceneMan:ShortestDistance(Owner.Pos, Leader.ViewPoint + aimDelta, false) * aimCorrectionRatio).Normalized;
									
									local LeaderWeapon = ToHDFirearm(Leader.EquippedItem);
									if LeaderWeapon:IsWeapon() then
										self.deviceState = AHuman.POINTING;

										-- check if the SL is shooting and if we have a similar weapon
										if Owner.FirearmIsReady then
											self.deviceState = AHuman.AIMING;

											if IsHDFirearm(Owner.EquippedItem) and Leader:GetController():IsState(Controller.WEAPON_FIRE) then
												local OwnerWeapon = ToHDFirearm(Owner.EquippedItem);
												if OwnerWeapon:IsTool() then
													-- try equipping a weapon
													if Owner.InventorySize > 0 and not Owner:EquipDeviceInGroup("Weapons - Primary", true) then
														Owner:EquipFirearm(true);
													end
												elseif LeaderWeapon:GetAIBlastRadius() >= OwnerWeapon:GetAIBlastRadius() * 0.5 and OwnerWeapon:CompareTrajectories(LeaderWeapon) < math.max(100, OwnerWeapon:GetAIBlastRadius()) then
													-- slightly displace full-auto shots to diminish stacking sounds and create a more dense fire rate
													if OwnerWeapon.FullAuto then
														if math.random() < 0.3 then
															self.Target = nil;
															self.squadShoot = true;
														end
													else
														self.Target = nil;
														self.squadShoot = true;
													end
												end
											else
												self.squadShoot = false;
											end
										else
											if Owner.FirearmIsEmpty then
												Owner:ReloadFirearms();
											elseif Owner.InventorySize > 0 and not Owner:EquipDeviceInGroup("Weapons - Primary", true) then
												Owner:EquipFirearm(true);
											end
										end
									end
								elseif IsThrownDevice(Leader.EquippedItem) and Leader:IsPlayerControlled() and ToThrownDevice(Leader.EquippedItem):HasObjectInGroup("Bombs - Grenades") and Owner:HasObjectInGroup("Bombs - Grenades") then
									-- throw grenades in unison with squad
									self.Ctrl.AnalogAim = aimDelta.Normalized;
									self.deviceState = AHuman.POINTING;

									if Leader:GetController():IsState(Controller.WEAPON_FIRE) then

										Owner:EquipDeviceInGroup("Bombs - Grenades", true);

										self.Target = nil;
										self.squadShoot = true;
									else
										self.squadShoot = false;
									end
								end
							end
						end
						if Leader.AIMode == Actor.AIMODE_GOTO then
							Owner.leaderWaypoint = Leader:GetLastAIWaypoint();
						end
					end
				end
			end
		else
			if self.GoToName == "GoToWpt" then
				self:CreateGoToBehavior(Owner);
			end

			-- if we are in AIMODE_SQUAD the leader just got killed
			if Owner.AIMode == Actor.AIMODE_SQUAD then
				Owner:ClearMovePath();
				if Owner.leaderWaypoint then
					Owner.AIMode = Actor.AIMODE_GOTO;
					Owner:AddAISceneWaypoint(Owner.leaderWaypoint);
					Owner.leaderWaypoint = nil;
				else
					Owner.AIMode = Actor.AIMODE_SENTRY;
				end
			end
		end
	elseif Owner.AIMode == Actor.AIMODE_SQUAD then	-- if we are in AIMODE_SQUAD the leader just got killed
		Owner.AIMode = Actor.AIMODE_SENTRY;
		if self.GoToName == "GoToWpt" then
			self:CreateGoToBehavior(Owner);
		end
	end

	if self.squadShoot then
		-- cycle semi-auto weapons on and off so the AI will shoot even if the player only press and hold the trigger
		if Owner.FirearmIsSemiAuto and self.SquadShootTimer:IsPastSimMS(Owner.FirearmActivationDelay+self.SquadShootDelay) then
			self.SquadShootTimer:Reset();
			self.squadShoot = false;
			self.squadShootDelay = math.random(50,100);
		end
	else
		-- run the move behavior and delete it if it returns true
		if self.GoToBehavior then
			local msg, done = coroutine.resume(self.GoToBehavior, self, Owner, false);
			if not msg then
				ConsoleMan:PrintString(Owner.PresetName .. " " .. self.GoToName .. " error:\n" .. done  .. debug.traceback(self.GoToBehavior)); -- print the error message
				done = true;
			end

			if done then
				self.GoToBehavior = nil;
				self.GoToName = nil;
				if self.GoToCleanup then
					self.GoToCleanup(self);
					self.GoToCleanup = nil;
				end
			end
		elseif self.flying and not SharedBehaviors.EngineMotor(Owner) then	-- avoid falling damage (the engine's motor does, where it has one)
			local jumpThreshold = 9;
			if Owner.Jetpack and Owner.Jetpack.JetpackType == AEJetpack.JumpPack then
				jumpThreshold = jumpThreshold * 3;
			end

			if (not self.jump and Owner.Vel.Y > jumpThreshold) or (self.jump and Owner.Vel.Y > jumpThreshold * 0.66) then
				self.jump = true;

				-- try falling straight down
				if not self.Target then
					if Owner.Vel.X > 2 then
						self.lateralMoveState = Actor.LAT_LEFT;
					elseif Owner.Vel.X < -2 then
						self.lateralMoveState = Actor.LAT_RIGHT;
					else
						self.lateralMoveState = Actor.LAT_STILL;
					end
				end
			else
				self.jump = false;
				self.lateralMoveState = Actor.LAT_STILL;
			end
		else
			self.jump = false;
		end

		-- run the selected behavior and delete it if it returns true
		if self.Behavior then
			local msg, done = coroutine.resume(self.Behavior, self, Owner, false);
			if not msg then
				ConsoleMan:PrintString(Owner.PresetName .. " behavior " .. self.BehaviorName .. " error:\n" .. done); -- print the error message
				done = true;
			end

			if done then
				self.Behavior = nil;
				self.BehaviorName = nil;
				if self.BehaviorCleanup then
					self.BehaviorCleanup(self);
					self.BehaviorCleanup = nil;
				end

				if not self.NextBehavior and not self.PickupHD and not objective and self.PickUpTimer:IsPastSimMS(10000) then
					self.PickUpTimer:Reset();

					if not Owner:EquipFirearm(false) then
						self:CreateGetWeaponBehavior(Owner);
					elseif Owner.AIMode ~= Actor.AIMODE_SENTRY and not Owner:EquipDiggingTool(false) then
						self:CreateGetToolBehavior(Owner);
					end
				end
			end
		end

		-- there is a HeldDevice we want to pick up
		if self.PickupHD then
			if not MovableMan:IsDevice(self.PickupHD) or self.PickupHD.ID ~= self.PickupHD.RootID then
				self.PickupHD = nil; -- the HeldDevice has been destroyed or picked up
			elseif SceneMan:ShortestDistance(Owner.Pos, self.PickupHD.Pos, false):MagnitudeIsLessThan(Owner.Height) then
				self.Ctrl:SetState(Controller.WEAPON_PICKUP, true);
			end
		end

		-- listen and react to AlarmEvents and AlarmPoints
		-- (Not on the objective: turning to an alarm, a medikit or an alarm event stopped a flag carrier on its way.)
		if objective then
			self.useMedikit = false;
		elseif AlarmPoint.Largest > 0 then
			if not self.Target and not self.UnseenTarget then
				self.AlarmPos = Vector(AlarmPoint.X, AlarmPoint.Y);
				self:CreateFaceAlarmBehavior(Owner);
			else
				-- is the alarm generated from behind us?
				local AlarmVector = SceneMan:ShortestDistance(Owner.Pos, AlarmPoint, false);
				if (Owner.HFlipped and AlarmVector.X > 0) or (not Owner.HFlipped and AlarmVector.X < 0) then
					self.AlarmPos = Vector(AlarmPoint.X, AlarmPoint.Y);
					self:CreateFaceAlarmBehavior(Owner);
				end
			end
		elseif not (self.Target or self.UnseenTarget) then
			-- use medikit if not engaging enemy
			if Owner.Health < (Owner.MaxHealth * 0.5) then
				if Owner:HasObject("Medikit") then

					self.useMedikit = Owner:EquipNamedDevice("Medikit", true);
				else
					self.useMedikit = false;
					-- (Not a unit told to hold its position: a sandbox "Hold position" is a sentry that stays put.)
					if not self.isPlayerOwned and Owner.AIMode == Actor.AIMODE_SENTRY and not Owner.OrderHold then
						Owner.AIMode = Actor.AIMODE_PATROL;
					end
				end
			else
				if self.useMedikit == true then
					self.useMedikit = false;
					Owner:EquipFirearm(true);
				end
				if self.AlarmTimer:IsPastSimTimeLimit() and SharedBehaviors.ProcessAlarmEvent(self, Owner) then
					self.AlarmTimer:Reset();
				end
			end
		end
	end

	-- The fighting rules that outlast any one behaviour: hits taken (for the cover rules), coming out of cover, a flank seen through, and
	-- falling back when badly hurt. An enemy that can't be seen any more but was shooting at us from somewhere known is flanked too.
	local hit = self.LastHealth and Owner.Health < self.LastHealth;
	if hit then
		self.HitTimer = self.HitTimer or Timer();
		self.HitTimer:Reset();
	end
	self.LastHealth = Owner.Health;
	if self.Target and MovableMan:ValidMO(self.Target) then
		self.LastEnemyPos = Vector(self.Target.Pos.X, self.Target.Pos.Y);
	end
	-- (On the objective, only what doesn't stop it or turn it off its way: reloading, and what it remembers.)
	if not objective then
		HumanBehaviors.LeaveCover(self, Owner);
	end
	HumanBehaviors.PeekUpdate(self, Owner);
	if not objective then
		HumanBehaviors.LobUpdate(self, Owner);
	end
	if not objective then
		HumanBehaviors.SmokeUpdate(self, Owner);
		SharedBehaviors.SquadTactics(self, Owner);
		SharedBehaviors.FlankUpdate(self, Owner);
		HumanBehaviors.ShotFromUnseen(self, Owner, hit and AlarmPoint);
		HumanBehaviors.UseTheWorld(self, Owner);
		-- (A unit shot from out of sight flanks only once it has reached the cover it went for, if any.)
		local reachingCover = self.Cover and self.Cover.Why == "shot" and not self.Cover.There;
		if not self.Flank and not self.Target and self.OldTargetPos and self.HitTimer and not self.HitTimer:IsPastSimMS(3000) and not reachingCover then
			SharedBehaviors.StartFlank(self, Owner, self.OldTargetPos, 500);
		end
		SharedBehaviors.RetreatUpdate(self, Owner);
	end
	SharedBehaviors.RememberUpdate(self, Owner);
	SharedBehaviors.AdvertiseMedikit(self, Owner);
	if not objective then
		HumanBehaviors.MedicUpdate(self, Owner);
	end
	HumanBehaviors.ReloadInLull(self, Owner);

	if self.teamBlockState == Actor.IGNORINGBLOCK then
		if self.BlockedTimer:IsPastSimMS(10000) then
			self.teamBlockState = Actor.NOTBLOCKED;
		end
	elseif self.teamBlockState == Actor.BLOCKED and not objective then	-- we are blocked by a team-mate, stop
		self.lateralMoveState = Actor.LAT_STILL;
		self.jump = false;
		if self.BlockedTimer:IsPastSimMS(20000) then
			self.BlockedTimer:Reset();
			self.teamBlockState = Actor.IGNORINGBLOCK;
		end
	else
		self.BlockedTimer:Reset();
	end

	-- controller states (the trigger only as the weapons rule allows, RC-1; a medikit is always used, and so is a digger cutting the route)
	local mayFire = SharedBehaviors.MayFire(self, Owner);
	-- (The engine's route-follower holds a digger's trigger itself on a dig step, AHuman::MoveAlongRoute; let go of here every update, the
	-- digger never fired, and a unit routed through ground stood at its face with the digger out.)
	local routeDig = self.engineMover == true and Owner.DiggingRoute == true;
	if self.squadShoot then
		self.Ctrl:SetState(Controller.WEAPON_FIRE, (mayFire and (self.fire or self.squadShoot)) or routeDig);
	else
		self.Ctrl:SetState(Controller.WEAPON_FIRE, (mayFire and self.fire) or self.useMedikit or self.medicHeal or self.douse or routeDig);
	end

	if self.deviceState == AHuman.AIMING then
		self.Ctrl:SetState(Controller.AIM_SHARP, true);
	end
	-- Sharp aim is the engine's sign of a fight: the aim holds the facing (walking or flying backwards) for a second and a half after it.
	-- Only with an enemy to fight, then, while on the move: an alarm's glance, a squad's look where its leader looks or a watch on where an
	-- enemy was all aim sharp, and units walked and flew backwards to where they were going, missed the steps and ledges ahead of them,
	-- and couldn't climb up onto them.
	if not self.Target and not self.UnseenTarget and (self.engineMover or self.flying or self.lateralMoveState ~= Actor.LAT_STILL) then
		self.Ctrl:SetState(Controller.AIM_SHARP, false);
	end
	-- force jetpack at detrimental downwards velocity
	-- (Not while the engine's route-follower and pilot fly the unit: the pilot brakes a fall for its landing, and lit over its head the
	-- jet threw flights off.)
	if (not self.jump and Owner.Vel.Y > 18) and not self.engineMover and not SharedBehaviors.EngineMotor(Owner) then
		self.jump = true;
	end
	-- A jet once lit stays lit for a moment: the planner's wish flickers from tick to tick, and every relighting cost a burst's worth of fuel for nothing.
	-- (Not in a climb, which pulses the jet to a rate of its own and relights without a burst: held 150 ms past its cut, 36 px at 12 m/s, the
	-- top of every shaft was overshot by that.)
	if self.jump then
		self.JumpHoldTimer:Reset();
	elseif self.jumpState ~= AHuman.NOTJUMPING and not self.jetClimb and not self.pilotFlight and not self.JumpHoldTimer:IsPastSimTimeLimit() and Owner.Vel.Y < 0 then
		self.jump = true;
	end
	if self.jump and Owner.Jetpack and Owner.Jetpack.JetTimeLeft > TimerMan.AIDeltaTimeMS then
		if self.jumpState == AHuman.PREJUMP then
			self.jumpState = AHuman.UPJUMP;
		elseif self.jumpState ~= AHuman.UPJUMP then	-- the jetpack is off
			-- A burst only when one can be had and is wanted; otherwise straight to the steady jet, which doesn't charge for a burst that never came.
			self.jumpState = (Owner.Jetpack:CanTriggerBurst() and not self.jetSteady) and AHuman.PREJUMP or AHuman.UPJUMP;
		end
	else
		self.jumpState = AHuman.NOTJUMPING;
	end

	-- (The script's own jet keys stand down while the engine's route-follower drives: it holds the jet as the flight wants.)
	if Owner.Jetpack and not self.engineMover then
		if self.jumpState == AHuman.PREJUMP then
			self.Ctrl:SetState(Controller.BODY_JUMPSTART, true); -- try to trigger a burst
		elseif self.jumpState == AHuman.UPJUMP then
			self.Ctrl:SetState(Controller.BODY_JUMP, true); -- trigger normal jetpack emission
		end
	end

	-- The engine pilot's lean of the nozzle (see AHuman::PilotFlight): by the stick, which tilts it without turning the body round.
	if self.jetStick then
		self.Ctrl.AnalogMove = Vector(self.jetStick, -1);
		self.jetStick = nil;
	end

	if self.proneState == AHuman.GOPRONE then
		self.proneState = AHuman.PRONE;
	elseif self.proneState == AHuman.PRONE then
		if SharedBehaviors.EngineMotor(Owner) then
			-- The engine's motor holds the stance (and lets it go for a take-off). Kept up for the whole fight, as the script's prone was,
			-- till a cleanup or a stand puts proneState back; but a unit on a move order gets up when the stance's while is out, to walk on.
			if SharedBehaviors.OrderKind(Owner) ~= "move" then
				Owner:SetAIStance(2, 500);
			elseif Owner.AIStance ~= 2 then
				self.proneState = AHuman.NOTPRONE;
			end
		else
			self.Ctrl:SetState(Controller.BODY_PRONE, true);
		end
	end

	-- Up or down a ladder (see SharedBehaviors.LadderAt): the base game's background ladders move a unit that aims up and presses up,
	-- or aims down and presses down.
	if self.ladderUp then
		self.Ctrl:SetState(Controller.MOVE_UP, true);
	elseif self.ladderDown then
		self.Ctrl:SetState(Controller.MOVE_DOWN, true);
	end
	-- (Asked for afresh every tick by the movement script, so a climb that ends or is taken over by another behaviour lets go.)
	self.ladderUp = false;
	self.ladderDown = false;

	-- (While the engine's route-follower drives, a behaviour's step (a crawl in, a step to cover) only when the follower pressed no side
	-- key itself this tick: with both keys at once, the unit stood or went the wrong way.)
	-- (Nor while it presses up or down: at a ladder, a side key from a behaviour on the same tick refused the grab or let go of the rungs.)
	local engineSide = self.engineMover and (self.Ctrl:IsState(Controller.MOVE_LEFT) or self.Ctrl:IsState(Controller.MOVE_RIGHT) or self.Ctrl:IsState(Controller.MOVE_UP) or self.Ctrl:IsState(Controller.MOVE_DOWN));
	if engineSide then
		-- (The follower's key stands.)
	elseif self.lateralMoveState == Actor.LAT_LEFT then
		self.Ctrl:SetState(Controller.MOVE_LEFT, true);
	elseif self.lateralMoveState == Actor.LAT_RIGHT then
		self.Ctrl:SetState(Controller.MOVE_RIGHT, true);
	end

	-- What this update changed, said over the unit's head where it's worth a line (unit speech).
	UnitSpeech.Update(self, Owner, ordered);
	self.orderSerial = Owner.AIOrderSerial;
end

function NativeHumanAI:Destroy(Owner)
	-- Tell the coroutines to abort to avoid memory leaks
	if self.GoToBehavior then
		local msg, done = coroutine.resume(self.GoToBehavior, self, Owner, true);
	end

	if self.Behavior then
		local msg, done = coroutine.resume(self.Behavior, self, Owner, true);
	end
end

-- functions that create behaviors. the default behaviors are stored in the HumanBehaviors table. store your custom behaviors in a table to avoid name conflicts between mods.
function NativeHumanAI:CreateQuickthrowBehavior(Owner)
	if self.Target and MovableMan:ValidMO(self.Target) then
		if Owner:EquipThrowable(true) and Owner.ThrowableIsReady then
			self.NextBehavior = coroutine.create(HumanBehaviors.ThrowTarget);
			self.NextBehaviorName = "ThrowTarget";
			return true;
		end
	end
	return false;
end

function NativeHumanAI:CreateSentryBehavior(Owner)
	if self.Target then
		self:CreateAttackBehavior(Owner);
	else
		if not Owner:EquipFirearm(true) then
			if self.PickUpTimer:IsPastSimMS(2000) then
				self.PickUpTimer:Reset();
				self:CreateGetWeaponBehavior(Owner);
			end

			return;
		end

		self.NextBehavior = coroutine.create(HumanBehaviors.Sentry); -- replace "HumanBehaviors.Sentry" with the function name of your own sentry behavior
		self.NextCleanup = nil;
		self.NextBehaviorName = "Sentry";
	end
end

function NativeHumanAI:CreatePatrolBehavior(Owner)
	self.NextBehavior = coroutine.create(SharedBehaviors.Patrol);
	self.NextCleanup = nil;
	self.NextBehaviorName = "Patrol";
end

function NativeHumanAI:CreateGoldDigBehavior(Owner)
	if not Owner:EquipDiggingTool(false) then
		if self.PickUpTimer:IsPastSimMS(1000) then
			self.PickUpTimer:Reset();
			self:CreateGetToolBehavior(Owner);
		end

		return;
	end

	self.NextBehavior = coroutine.create(HumanBehaviors.GoldDig);
	self.NextCleanup = nil;
	self.NextBehaviorName = "GoldDig";
end

function NativeHumanAI:CreateBrainSearchBehavior(Owner)
  	if self.PickupHD then
		-- We're currently trying to pickup a weapon, do that instead
		self.Target = nil;
		return;
	end

	self.NextBehavior = coroutine.create(SharedBehaviors.BrainSearch);
	self.NextCleanup = nil;
	self.NextBehaviorName = "BrainSearch";
end

function NativeHumanAI:CreateGetToolBehavior(Owner)
	if Owner.AIMode ~= Actor.AIMODE_SQUAD then
		self.NextBehavior = coroutine.create(HumanBehaviors.ToolSearch);
		self.NextCleanup = nil;
		self.NextBehaviorName = "ToolSearch";
	end
end

function NativeHumanAI:CreateGetWeaponBehavior(Owner)
	if Owner.AIMode ~= Actor.AIMODE_SQUAD then
		self.NextBehavior = coroutine.create(HumanBehaviors.WeaponSearch);
		self.NextCleanup = nil;
		self.NextBehaviorName = "WeaponSearch";
	end
end

function NativeHumanAI:CreateGoToBehavior(Owner)
	-- The engine's route-follower when the build has it (SharedBehaviors.GoToRoute, AHuman::MoveAlongRoute); else the script's own.
	self.NextGoTo = coroutine.create(SharedBehaviors.UsesEngineMover(Owner) and SharedBehaviors.GoToRoute or SharedBehaviors.GoToWpt);
	self.NextGoToCleanup = function(AI)
		AI.lateralMoveState = Actor.LAT_STILL;
		AI.deviceState = AHuman.STILL;
		AI.proneState = AHuman.NOTPRONE;
		AI.jump = false;
		AI.fire = false;
		-- (Set by GoToRoute while the engine moves the unit, and cleared by it on its own way out; a GoTo that ended in an error left it
		-- set for good, and the unit's run key, its own jump keys and the squad key copy stayed off from then on.)
		AI.engineMover = false;
	end
	self.NextGoToName = "GoToWpt";
end

function NativeHumanAI:CreateAttackBehavior(Owner)
	self.ReloadTimer:Reset();
	self.TargetLostTimer:Reset();

	-- Running the objective: no fight at all, not even a stop to shoot; it just goes.
	if SharedBehaviors.OnObjective(Owner) then
		self.Target = nil;
		return;
	end

	if self.PickupHD then
		-- We're currently trying to pickup a weapon, do that instead
		self.Target = nil;
		return;
	end
		
	local dist = SceneMan:ShortestDistance(Owner.Pos, self.Target.Pos, false);
	if IsADoor(self.Target) and Owner.AIMode ~= Actor.AIMODE_SQUAD then
		--TODO: Include other explosive weapons with varying effective ranges!
		if Owner:EquipDeviceInGroup("Tools - Breaching", true) then
			self.NextBehavior = coroutine.create(HumanBehaviors.AttackTarget);
			self.NextBehaviorName = "AttackTarget";
		elseif Owner.FirearmIsReady and SharedBehaviors.GetProjectileData(Owner).pen * 0.9 > (self.Target.Door or self.Target).Material.StructuralIntegrity then
			self.NextBehavior = coroutine.create(HumanBehaviors.ShootTarget);
			self.NextBehaviorName = "ShootTarget";
		else	--Cannot harm this door!
			self.Target = nil;
			return;
		end
	-- favor grenades as the initiator to a sneak attack
	elseif Owner.AIMode ~= Actor.AIMODE_SQUAD and Owner.AIMode ~= Actor.AIMODE_SENTRY and self.Target.HFlipped == Owner.HFlipped and Owner:EquipDeviceInGroup("Bombs - Grenades", true)
	and dist:MagnitudeIsGreaterThan(100) and dist:MagnitudeIsLessThan(ToThrownDevice(Owner.EquippedItem):GetCalculatedMaxThrowVelIncludingArmThrowStrength() * GetPPM()) and (self.Target.Pos.Y + 20) > Owner.Pos.Y then
		self.NextBehavior = coroutine.create(HumanBehaviors.ThrowTarget);
		self.NextBehaviorName = "ThrowTarget";
	elseif Owner:EquipFirearm(true) then
		if Owner.EquippedItem:HasObjectInGroup("Weapons - Melee") then
			self.NextBehavior = coroutine.create(HumanBehaviors.AttackTarget);
			self.NextBehaviorName = "AttackTarget";
		else
			self.NextBehavior = coroutine.create(HumanBehaviors.ShootTarget);
			self.NextBehaviorName = "ShootTarget";
		end
	elseif Owner.AIMode ~= Actor.AIMODE_SQUAD and Owner:EquipThrowable(true) and dist:MagnitudeIsLessThan(ToThrownDevice(Owner.EquippedItem):GetCalculatedMaxThrowVelIncludingArmThrowStrength() * GetPPM()) then
		self.NextBehavior = coroutine.create(HumanBehaviors.ThrowTarget);
		self.NextBehaviorName = "ThrowTarget";
	elseif Owner.AIMode ~= Actor.AIMODE_SQUAD and Owner:EquipDiggingTool(true) and dist:MagnitudeIsLessThan(250) then
		self.NextBehavior = coroutine.create(HumanBehaviors.AttackTarget);
		self.NextBehaviorName = "AttackTarget";
	else	-- unarmed or far away
		if self.PickUpTimer:IsPastSimMS(2500) then
			self.PickUpTimer:Reset();
			self.NextBehavior = coroutine.create(HumanBehaviors.WeaponSearch);
			self.NextBehaviorName = "WeaponSearch";
			self.NextCleanup = nil;

			return;
		else -- there are probably no weapons around here (in the vicinity of an area adjacent to a location)
			if not (self.isPlayerOwned and Owner.AIMode == Actor.AIMODE_SENTRY) and (self.Target.ClassName == "AHuman" or self.Target.ClassName == "ACrab") then
				self.NextBehavior = coroutine.create(HumanBehaviors.AttackTarget);
				self.NextBehaviorName = "AttackTarget";
			else
				self.Target = nil;
				return;
			end
		end
	end

	self.NextCleanup = function(AI)
		AI.fire = false;
		AI.canHitTarget = false;
		AI.closingIn = false;
		AI.ShotBlockedTimer = nil;
		AI.deviceState = AHuman.STILL;
		AI.proneState = AHuman.NOTPRONE;
		HumanBehaviors.StopRangeStep(AI, Owner);
		AI.TargetLostTimer:SetSimTimeLimitMS(2000);
	end
end

-- force the use of a digger when attacking
function NativeHumanAI:CreateHtHBehavior(Owner)					-- has to be digger, not just "tool"
	if Owner.AIMode ~= Actor.AIMODE_SQUAD and self.Target and (Owner:HasObjectInGroup("Tools - Diggers") or Owner:HasObjectInGroup("Weapons - Melee")) then
		self.NextBehavior = coroutine.create(HumanBehaviors.AttackTarget);
		self.NextBehaviorName = "AttackTarget";
		self.NextCleanup = function(AI)
			AI.fire = false;
			AI.Target = nil;
			AI.deviceState = AHuman.STILL;
			AI.proneState = AHuman.NOTPRONE;
		end
	end
end

function NativeHumanAI:CreateSuppressBehavior(Owner)
	if Owner:EquipFirearm(true) then
		self.NextBehavior = coroutine.create(HumanBehaviors.ShootArea);
		self.NextBehaviorName = "ShootArea";
	else
		if Owner.FirearmIsEmpty then
			Owner:ReloadFirearms();
		end
		return;
	end

	self.NextCleanup = function(AI)
		AI.fire = false;
		AI.UnseenTarget = nil;
		AI.deviceState = AHuman.STILL;
		AI.proneState = AHuman.NOTPRONE;
	end
end

function NativeHumanAI:CreateFaceAlarmBehavior(Owner)
	self.NextBehavior = coroutine.create(SharedBehaviors.FaceAlarm);
	self.NextBehaviorName = "FaceAlarm";
	self.NextCleanup = nil;
end

function NativeHumanAI:CreatePinBehavior(Owner)
	if self.OldTargetPos and Owner:EquipFirearm(true) then
		self.NextBehavior = coroutine.create(SharedBehaviors.PinArea);
		self.NextBehaviorName = "PinArea";
	else
		return;
	end

	self.NextCleanup = function(AI)
		self.OldTargetPos = nil;
	end
end
