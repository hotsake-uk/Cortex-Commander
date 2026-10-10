require("Constants")
require("AI/CrabBehaviors");
require("AI/SharedBehaviors");
require("AI/UnitSpeech");

NativeCrabAI = {};

function NativeCrabAI:Create(Owner)
	local Members = {};

	Members.lateralMoveState = Actor.LAT_STILL;
	Members.jumpState = ACrab.NOTJUMPING;
	Members.deviceState = ACrab.STILL;
	Members.lastAIMode = Actor.AIMODE_NONE;
	Members.teamBlockState = Actor.NOTBLOCKED;
	Members.SentryFacing = Owner.HFlipped;
	Members.fire = false;
	Members.groundContact = 5;
	Members.flying = false;
	Members.jetSteady = false; -- The movement script asks for the jet without a burst (a climb's pulses; a burst is a kick and a burst's worth of fuel).

	Members.AirTimer = Timer();
	Members.ReloadTimer = Timer();
	Members.BlockedTimer = Timer();
	Members.TargetLostTimer = Timer();
	Members.SquadShootTimer = Timer();

	Members.AlarmTimer = Timer();
	Members.AlarmTimer:SetSimTimeLimitMS(500);

	-- check if this team is controlled by a human
	if ActivityMan:GetActivity():IsHumanTeam(Owner.Team) then
		Members.isPlayerOwned = true;
		Members.PlayerInterferedTimer = Timer();
		Members.PlayerInterferedTimer:SetSimTimeLimitMS(500);
	end
	
	-- customizable variables, trend started by pawnis on 01/09/2023 :)
	
	-- humble beginnings
	-- pause time between sweeping aim up and down when guarding
	Members.idleAimTime = Owner:NumberValueExists("AIIdleAimTime") and Owner:GetNumberValue("AIIdleAimTime") or 500;

	-- set shooting skill
	Members.aimSpeed, Members.aimSkill, Members.skill = SharedBehaviors.GetTeamShootingSkill(Owner.Team);

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

function NativeCrabAI:Update(Owner)
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

			self.Target = nil;
			self.UnseenTarget = nil;
			self.OldTargetPos = nil;
			self.fire = false;
			-- The fighting rules' state goes too, as the human's does: a crab a player took over mid-retreat kept its retreating tag, and was a
			-- "move" order to the rules (and left alone by the sandbox) ever after.
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
			self.SentryFacing = Owner.HFlipped;
			self.lastAIMode = Actor.AIMODE_NONE;
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

	-- switch to the next behavior, if avaliable
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
	end

	-- An attack order picks and re-picks its own enemy here (see SharedBehaviors.AttackOrderUpdate), before the new-order check below takes up a redirect.
	SharedBehaviors.AttackOrderUpdate(self, Owner);

	-- check if the AI mode has changed or if we need a new behavior
	-- (Or told to go somewhere while the behaviour left over from arriving is still running: see NativeHumanAI.)
	local newOrder = (Owner.AIMode == Actor.AIMODE_GOTO or Owner.AIMode == Actor.AIMODE_SQUAD) and self.BehaviorName ~= "GoToWpt" and self.NextBehaviorName ~= "GoToWpt" and self.BehaviorName ~= "GoToRoute" and self.NextBehaviorName ~= "GoToRoute" and (Owner:GetWaypointListSize() > 0 or Owner.MOMoveTarget);
	-- (And any order given since this AI's own last update: see NativeHumanAI.)
	local ordered = self.orderSerial ~= nil and Owner.AIOrderSerial ~= self.orderSerial;
	if Owner.AIMode ~= self.lastAIMode or not self.Behavior or newOrder or ordered then
		-- Tell the coroutines to abort to avoid memory leaks
		if self.Behavior then
			local msg, done = coroutine.resume(self.Behavior, self, Owner, true);
		end

		self.Behavior = nil;
		self.BehaviorName = nil;
		if self.BehaviorCleanup then
			self.BehaviorCleanup(self); -- stop the current behavior
			self.BehaviorCleanup = nil;
		end

		-- select a new behavior based on AI mode
		if Owner.AIMode == Actor.AIMODE_GOTO or Owner.AIMode == Actor.AIMODE_SQUAD then
			self:CreateGoToBehavior(Owner);
		elseif Owner.AIMode == Actor.AIMODE_PATROL then
			self:CreatePatrolBehavior(Owner);
		elseif Owner.AIMode == Actor.AIMODE_BRAINHUNT then
			self:CreateBrainSearchBehavior(Owner);
		else
			if (Owner.AIMode ~= self.lastAIMode or ordered) and Owner.AIMode == Actor.AIMODE_SENTRY then
				self.SentryFacing = Owner.HFlipped; -- store the direction in which we should be looking
				-- (Back at its post after a fall-back (SharedBehaviors.RestoreOrder): the post's facing, not the way it walked in.)
				if self.ReturnPost and self.ReturnPost.facing ~= nil and not SceneMan:ShortestDistance(Owner.Pos, self.ReturnPost.Pos, false):MagnitudeIsGreaterThan(Owner.Height) then
					self.SentryFacing = self.ReturnPost.facing;
				end
				self.ReturnPost = nil;
			end

			self:CreateSentryBehavior(Owner);
		end

		self.lastAIMode = Owner.AIMode;
	end

	-- cast a ray to find targets (only those its temperament lets it fight, NC-1)
	SharedBehaviors.DropForbiddenTarget(self, Owner);
	if not SharedBehaviors.NeverFights(Owner) then
		CrabBehaviors.LookForTargets(self, Owner);
		SharedBehaviors.DropForbiddenTarget(self, Owner);
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
				if not self.jump and self.lateralMoveState == Actor.LAT_STILL then
					if Leader and Leader.EquippedItem and IsHDFirearm(Leader.EquippedItem) and SceneMan:ShortestDistance(Owner.Pos, Leader.Pos, false).Largest < (Leader.Height + Owner.Height) * 0.5 then
						local LeaderWeapon = ToHDFirearm(Leader.EquippedItem);
						if LeaderWeapon:IsWeapon() then
							local AimDelta = SceneMan:ShortestDistance(Leader.Pos, Leader.ViewPoint, false);
							self.Ctrl.AnalogAim = SceneMan:ShortestDistance(Owner.Pos, Leader.ViewPoint+AimDelta, false).Normalized;
							self.deviceState = AHuman.POINTING;

							-- check if the SL is shooting and if we have a similar weapon
							if Owner.FirearmIsReady then
								self.deviceState = AHuman.AIMING;

								if IsHDFirearm(Owner.EquippedItem) and Leader:GetController():IsState(Controller.WEAPON_FIRE) then
									local OwnerWeapon = ToHDFirearm(Owner.EquippedItem);
									if not OwnerWeapon:IsTool() and LeaderWeapon:GetAIBlastRadius() >= OwnerWeapon:GetAIBlastRadius() * 0.5 and OwnerWeapon:CompareTrajectories(LeaderWeapon) < math.max(100, OwnerWeapon:GetAIBlastRadius()) then
										self.Target = nil;
										self.squadShoot = true;
									end
								end
							else
								if Owner.FirearmIsEmpty then
									Owner:ReloadFirearms();
								end
							end
						end
					end
				end
			end
		else
			if self.GoToName == "GoToWpt" or self.GoToName == "GoToRoute" then
				self:CreateGoToBehavior(Owner);
			end

			-- if we are in AIMODE_SQUAD the leader just got killed
			if Owner.AIMode == Actor.AIMODE_SQUAD then
				Owner.AIMode = Actor.AIMODE_SENTRY;
				Owner:ClearMovePath();
			end
		end
	elseif Owner.AIMode == Actor.AIMODE_SQUAD then	-- if we are in AIMODE_SQUAD the leader just got killed
		Owner.AIMode = Actor.AIMODE_SENTRY;
		if self.GoToName == "GoToWpt" or self.GoToName == "GoToRoute" then
			self:CreateGoToBehavior(Owner);
		end
	end
	--[[if Owner.MOMoveTarget then
		-- make the last waypoint marker stick to the MO we are following
		if MovableMan:ValidMO(Owner.MOMoveTarget) then
			Owner:RemoveMovePathEnd();
			Owner:AddToMovePathEnd(Owner.MOMoveTarget.Pos);

			if Owner.AIMode == Actor.AIMODE_SQUAD then
				-- look where the SL looks, if not moving
				if not self.Target and self.lateralMoveState == Actor.LAT_STILL then
					local Leader = ToActor(Owner.MOMoveTarget);
					if Leader and SceneMan:ShortestDistance(Owner.Pos, Leader.Pos, false).Largest < Owner.Height then
						self.deviceState = AHuman.AIMING;
						self.Ctrl.AnalogAim = SceneMan:ShortestDistance(Owner.EyePos, Leader.ViewPoint, false).Normalized;
					end
				end
			end
		else
			Owner.MOMoveTarget = nil;

			-- if we are in AIMODE_SQUAD the leader just got killed
			if Owner.AIMode == Actor.AIMODE_SQUAD then
				Owner.AIMode = Actor.AIMODE_SENTRY;
				Owner:ClearMovePath();
			end
		end
	elseif Owner.AIMode == Actor.AIMODE_SQUAD then	-- if we are in AIMODE_SQUAD the leader just got killed
		Owner.AIMode = Actor.AIMODE_SENTRY;
		Owner:ClearMovePath();
	end]]

	if self.squadShoot then
		-- cycle semi-auto weapons on and off so the AI will shoot even if the player only press and hold the trigger
		if Owner.FirearmIsSemiAuto and self.SquadShootTimer:IsPastSimMS(Owner.FirearmActivationDelay+50) then
			self.SquadShootTimer:Reset();
			self.squadShoot = false;
		end
	else
		-- run the selected behavior and delete it if it returns true
		if self.Behavior then
			local msg, done = coroutine.resume(self.Behavior, self, Owner, false);
			if not msg then
				ConsoleMan:PrintString(Owner.PresetName .. " behavior " .. self.BehaviorName .. " error:\n" .. done .. debug.traceback(self.Behavior)); -- print the error message
				done = true;
			end

			if done then
				self.Behavior = nil;
				self.BehaviorName = nil;
				if self.BehaviorCleanup then
					self.BehaviorCleanup(self);
					self.BehaviorCleanup = nil;
				end
			end
		end

		-- listen and react to relevant AlarmEvents
		if not self.Target and not self.UnseenTarget then
			if self.AlarmTimer:IsPastSimTimeLimit() and SharedBehaviors.ProcessAlarmEvent(self, Owner) then
				self.AlarmTimer:Reset();
			end
		end

		-- The fighting rules that outlast any one behaviour: a flank seen through, and falling back when badly hurt.
		if self.Target and MovableMan:ValidMO(self.Target) then
			self.LastEnemyPos = Vector(self.Target.Pos.X, self.Target.Pos.Y);
		end
		-- (And as the humans do, AC-9: shot by an enemy it has just lost sight of, it works round to where it can shoot back.)
		if self.LastHealth and Owner.Health < self.LastHealth then
			self.HitTimer = self.HitTimer or Timer();
			self.HitTimer:Reset();
		end
		self.LastHealth = Owner.Health;
		SharedBehaviors.FlankUpdate(self, Owner);
		if not self.Flank and not self.Target and self.OldTargetPos and self.HitTimer and not self.HitTimer:IsPastSimMS(3000) then
			SharedBehaviors.StartFlank(self, Owner, self.OldTargetPos, 500);
		end
		SharedBehaviors.RetreatUpdate(self, Owner);
		-- (Its nature, NC-1: a skittish animal runs from danger, and livestock left standing graze about.)
		SharedBehaviors.FleeUpdate(self, Owner);
		SharedBehaviors.GrazeUpdate(self, Owner);
		SharedBehaviors.RememberUpdate(self, Owner);

		if self.teamBlockState == Actor.IGNORINGBLOCK then
			if self.BlockedTimer:IsPastSimMS(20000) then
				self.teamBlockState = Actor.NOTBLOCKED;
			end
		elseif self.teamBlockState == Actor.BLOCKED then	-- we are blocked by a teammate, stop
			self.lateralMoveState = Actor.LAT_STILL;
			if self.BlockedTimer:IsPastSimMS(10000) then
				self.BlockedTimer:Reset();
				self.teamBlockState = Actor.IGNORINGBLOCK;
			end
		else
			self.BlockedTimer:Reset();
		end
	end

	-- check if the feet reach the ground
	if self.AirTimer:IsPastSimMS(250) then
		self.AirTimer:Reset();

		local Origin = {};
		if Owner.LeftFGLeg then
			table.insert(Origin, Vector(Owner.LeftFGLeg.Pos.X, Owner.LeftFGLeg.Pos.Y) + Vector(0, 4));
		end
		if Owner.RightFGLeg then
			table.insert(Origin, Vector(Owner.RightFGLeg.Pos.X, Owner.RightFGLeg.Pos.Y) + Vector(0, 4));
		end
		if Owner.LeftBGLeg then
			table.insert(Origin, Vector(Owner.LeftBGLeg.Pos.X, Owner.LeftBGLeg.Pos.Y) + Vector(0, 4));
		end
		if Owner.RightBGLeg then
			table.insert(Origin, Vector(Owner.RightBGLeg.Pos.X, Owner.RightBGLeg.Pos.Y) + Vector(0, 4));
		end
		if #Origin == 0 then
			table.insert(Origin, Vector(Owner.Pos.X, Owner.Pos.Y) + Vector(0, 4 + ToMOSprite(Owner):GetSpriteHeight() + Owner.SpriteOffset.Y));
		end
		for i = 1, #Origin do
			if SceneMan:GetTerrMatter(Origin[i].X, Origin[i].Y) ~= rte.airID then
				self.groundContact = 5;
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
	end

	-- controller states (the trigger only as the weapons rule allows, RC-1)
	self.Ctrl:SetState(Controller.WEAPON_FIRE, SharedBehaviors.MayFire(self, Owner) and (self.fire or self.squadShoot));

	if self.deviceState == ACrab.AIMING then
		self.Ctrl:SetState(Controller.AIM_SHARP, true);
	end

	-- (Not over a side key the engine's route-follower pressed this tick, as for humans: both keys at once and the crab stood or turned.)
	if self.Ctrl:IsState(Controller.MOVE_LEFT) or self.Ctrl:IsState(Controller.MOVE_RIGHT) then
		-- (The follower's key stands.)
	elseif self.lateralMoveState == Actor.LAT_LEFT then
		self.Ctrl:SetState(Controller.MOVE_LEFT, true);
	elseif self.lateralMoveState == Actor.LAT_RIGHT then
		self.Ctrl:SetState(Controller.MOVE_RIGHT, true);
	end

	if self.jump and Owner.Jetpack and Owner.Jetpack.JetTimeLeft > TimerMan.AIDeltaTimeMS then
		if self.jumpState == ACrab.PREJUMP then
			self.jumpState = ACrab.UPJUMP;
		elseif self.jumpState ~= ACrab.UPJUMP then	-- the jetpack is off
			-- A burst only when one can be had and is wanted; otherwise straight to the steady jet, which doesn't charge for a burst that never came.
			self.jumpState = (Owner.Jetpack:CanTriggerBurst() and not self.jetSteady) and ACrab.PREJUMP or ACrab.UPJUMP;
		end
	else
		self.jumpState = ACrab.NOTJUMPING;
	end

	if Owner.Jetpack then
		if self.jumpState == ACrab.PREJUMP then
			self.Ctrl:SetState(Controller.BODY_JUMPSTART, true); -- try to trigger a burst
		elseif self.jumpState == ACrab.UPJUMP then
			self.Ctrl:SetState(Controller.BODY_JUMP, true); -- trigger normal jetpack emission
		end
		-- A crab's jet is lift, leant only on purpose. The nozzle follows the aim and leans forward by up to fourteen degrees when the aim is
		-- level, and a crab's turret can't aim far enough up to take the lean out (the Dreadnought's range is half a radian), so every climb
		-- was also a push the way it faced: up the gym's steep hill, with the facing flipped by a brake or a step back, that threw it backwards
		-- off the slope (it got over the hill one run in three). The move stick, when it is pushed, sets the nozzle directly, in screen terms
		-- whichever way the crab faces, and the crab's walking doesn't read it. So whenever the jet is lit the stick is pushed up, and over to
		-- the side the movement script asks for (jetLeanX, -1 to 1: towards its waypoint in a climb, against its speed when braking); the
		-- legs flail while the jet is lit, so this is the only sideways force a flying crab has. 0.27 across is the full fourteen degrees.
		if self.jumpState ~= ACrab.NOTJUMPING then
			self.Ctrl.AnalogMove = Vector((self.jetLeanX or 0) * 0.27, -1);
		end
	end

	-- What this update changed, said over the unit's head where it's worth a line (unit speech).
	UnitSpeech.Update(self, Owner, ordered);
	self.orderSerial = Owner.AIOrderSerial;
end

function NativeCrabAI:Destroy(Owner)
	-- Tell the coroutines to abort to avoid memory leaks
	if self.Behavior then
		local msg, done = coroutine.resume(self.Behavior, self, Owner, true);
	end
end

function NativeCrabAI:CreatePatrolBehavior(Owner)
	self.NextBehavior = coroutine.create(SharedBehaviors.Patrol);
	self.NextCleanup = nil;
	self.NextBehaviorName = "Patrol";
end

function NativeCrabAI:CreateSentryBehavior(Owner)
	if not Owner.FirearmIsReady then
		return;
	end

	self.NextBehavior = coroutine.create(CrabBehaviors.Sentry);
	self.NextCleanup = nil;
	self.NextBehaviorName = "Sentry";
end

function NativeCrabAI:CreateAttackBehavior(Owner)
	if Owner.FirearmIsReady then
		self.NextBehavior = coroutine.create(CrabBehaviors.ShootTarget);
		self.NextBehaviorName = "ShootTarget";
	else
		if Owner.FirearmIsEmpty then
			Owner:ReloadFirearms();
		end
		return;
	end

	self.NextCleanup = function(AI)
		AI.fire = false;
		AI.Target = nil;
		AI.deviceState = ACrab.STILL;
	end
end

function NativeCrabAI:CreateSuppressBehavior(Owner)
	if Owner.FirearmIsReady then
		self.NextBehavior = coroutine.create(CrabBehaviors.ShootArea);
		self.NextBehaviorName = "ShootArea";
	else
		-- (The crab's, not the AI table's: read off the table, it was never set, and the reload never happened.)
		if Owner.FirearmIsEmpty then
			Owner:ReloadFirearms();
		end
		return;
	end

	self.NextCleanup = function(AI)
		AI.fire = false;
		AI.UnseenTarget = nil;
		AI.deviceState = ACrab.STILL;
	end
end

function NativeCrabAI:CreateGoToBehavior(Owner)
	-- The engine's route-follower for crabs (ACrab::MoveAlongRoute) where the build has it; else the script's own. (Asked without the error a
	-- missing member is, for an older exe.)
	local ok, member = pcall(function() return Owner.MoveAlongRoute; end);
	local engine = ok and member ~= nil and not (os and os.getenv and os.getenv("CCCP_LUA_MOVER") == "1");
	self.NextBehavior = coroutine.create(engine and SharedBehaviors.GoToRoute or SharedBehaviors.GoToWpt);
	self.NextBehaviorName = engine and "GoToRoute" or "GoToWpt";

	self.NextCleanup = function(AI)
		AI.lateralMoveState = Actor.LAT_STILL;
	end
end

function NativeCrabAI:CreateBrainSearchBehavior(Owner)
	self.NextBehavior = coroutine.create(SharedBehaviors.BrainSearch);
	self.NextCleanup = nil;
	self.NextBehaviorName = "BrainSearch";
end

function NativeCrabAI:CreateFaceAlarmBehavior(Owner)
	self.NextBehavior = coroutine.create(SharedBehaviors.FaceAlarm);
	self.NextBehaviorName = "FaceAlarm";
	self.NextCleanup = nil;
end

function NativeCrabAI:CreatePinBehavior(Owner)
	if self.OldTargetPos and Owner.FirearmIsReady then
		self.NextBehavior = coroutine.create(SharedBehaviors.PinArea);
		self.NextBehaviorName = "PinArea";
	else
		return;
	end

	self.NextCleanup = function(AI)
		self.OldTargetPos = nil;
	end
end
