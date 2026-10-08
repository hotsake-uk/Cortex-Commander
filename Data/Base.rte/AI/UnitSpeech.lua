-- Unit speech (US-1): what the AI's state changes say over a unit's head. Called once at the end of every AI update by the human and crab
-- AIs; it compares what the AI is doing now with what it was doing at the last update, and fires a trigger where something started.
-- Whether a line is actually said (the chance, the trigger being on, the unit having said it lately, a friend having just said it) and
-- which line, is the engine's business (Actor:Say), from Base.rte/Speech.ini. Other scripts can call Owner:Say("Trigger") themselves, or
-- Owner:SayText("words", ms) for words of their own.

UnitSpeech = {};

-- The behaviour names (NativeHumanAI/NativeCrabAI NextBehaviorName) whose start is worth a line, and the trigger each fires.
UnitSpeech.BehaviorTriggers = {
	ShootArea = "Suppressing", -- Firing at an enemy shooting at us that can't be seen.
	ThrowTarget = "Grenade",
	WeaponSearch = "OutOfAmmo", -- No usable gun: off to find one.
	PinArea = "LostTarget", -- The enemy went out of sight; aiming where it was.
	FaceAlarm = "Alarm", -- Heard something and turned to it.
};

-- Whether an enemy is down (gone, dying or dead).
local function isDown(Enemy)
	return not MovableMan:ValidMO(Enemy) or Enemy.Status >= Actor.DYING or Enemy.Health <= 0;
end

-- @param AI The unit's AI.
-- @param Owner The unit.
-- @param ordered Whether an order was given since the AI's last update (the AI's own "ordered").
function UnitSpeech.Update(AI, Owner, ordered)
	if Owner:IsPlayerControlled() or Owner.Status >= Actor.DYING then
		AI.Speech = nil;
		return;
	end
	local Last = AI.Speech;
	local Now = UnitSpeech.Snapshot(AI, Owner);
	AI.Speech = Now;
	if not Last then
		-- (Nothing to compare with: a new unit, or one a player just let go of, says nothing about how it started.)
		Now.FireTimer = Timer();
		Now.TargetTimer = Timer();
		Now.TargetTimer.ElapsedSimTimeMS = 60000;
		return;
	end
	Now.FireTimer = Last.FireTimer;
	Now.TargetTimer = Last.TargetTimer;
	Now.KilledID = Last.KilledID;
	if AI.fire then
		Now.FireTimer:Reset();
	end

	-- In the order of what matters most to a player watching, one line at most a tick (the engine also keeps one line on show at a time).
	local said = false;
	local function say(trigger)
		if not said then
			said = Owner:Say(trigger);
		end
	end

	-- The enemy it had is down, and it was shooting at it.
	local Old = Last.Target;
	local oldDown = Old and isDown(Old);
	if oldDown and Now.KilledID ~= Last.TargetID and not Last.FireTimer:IsPastSimMS(2000) then
		Now.KilledID = Last.TargetID;
		say("Kill");
	end
	if Now.Retreat and not Last.Retreat then
		say(SharedBehaviors.Shaken(AI, Owner) and "Shaken" or "Retreat");
	end
	if Now.Health < Owner.MaxHealth * 0.35 and Last.Health >= Owner.MaxHealth * 0.35 then
		say("LowHealth");
	elseif Now.Health < Last.Health - Owner.MaxHealth * 0.05 then
		say("Hit");
	end
	if Now.Behavior ~= Last.Behavior and Now.Behavior then
		local trigger = UnitSpeech.BehaviorTriggers[Now.Behavior];
		-- (An enemy that went down isn't one that got away.)
		if trigger and not (trigger == "LostTarget" and oldDown) then
			say(trigger);
		end
	end
	if Now.Cover and not Last.Cover then
		say("TakeCover");
	end
	-- (Not the same fight picked up again: an enemy seen again within a few seconds of the last one isn't news.)
	if Now.Seen and not Last.Seen and Last.TargetTimer:IsPastSimMS(5000) then
		say(Owner.WeaponRule == Actor.WEAPONS_HOLD and "SpottedHoldingFire" or "EnemySpotted");
	end
	-- (On return fire only, opening up means it was shot at: RC-1.)
	if Now.Firing and not Last.Firing and Owner.WeaponRule == Actor.WEAPONS_RETURN_FIRE then
		say("ReturningFire");
	end
	if Now.Target or Now.UnseenTarget then
		Now.TargetTimer:Reset();
	end
	-- (Under fire until the suppression has mostly worn off again, so it isn't said every time it bobs over the line.)
	if Now.Suppression > 0.5 and not Last.UnderFire then
		say("UnderFire");
	end
	Now.UnderFire = Now.Suppression > 0.5 or (Last.UnderFire and Now.Suppression > 0.2);
	if Now.Reloading and not Last.Reloading then
		say("Reloading");
	end
	if Now.Flank and not Last.Flank then
		say("Flank");
	end
	if Now.Medikit and not Last.Medikit then
		say("Healing");
	end

	-- Orders, and getting where it was sent: only for a player's units, and not the AI's own fall-backs and flanks.
	if AI.isPlayerOwned and not Now.Retreat and not Now.Flank then
		if ordered then
			if Owner.OrderAttack or Owner.AIMode == Actor.AIMODE_BRAINHUNT then
				say("OrderAttack");
			elseif Owner.OrderHold or (Owner.OrderHasPost and Owner.AIMode ~= Actor.AIMODE_GOTO) or Owner.AIMode == Actor.AIMODE_SENTRY then
				say("OrderHold");
			elseif Owner.AIMode == Actor.AIMODE_GOTO or Owner.AIMode == Actor.AIMODE_SQUAD then
				say("OrderMove");
			end
		elseif Last.Mode == Actor.AIMODE_GOTO and Now.Mode == Actor.AIMODE_SENTRY and not Last.Retreat and not Last.Flank then
			say("Arrived");
		end
	end
end

-- What the AI is doing this tick, for the next tick to compare with.
function UnitSpeech.Snapshot(AI, Owner)
	-- (An enemy unit: a door shot through isn't "one down".)
	local Target = AI.Target and MovableMan:ValidMO(AI.Target) and AI.Target.ClassName ~= "ADoor" and AI.Target or nil;
	if not Target and AI.UnseenTarget and MovableMan:ValidMO(AI.UnseenTarget) and AI.UnseenTarget.ClassName ~= "ADoor" then
		Target = AI.UnseenTarget;
	end
	local reloading = false;
	if Owner.ClassName == "AHuman" and Owner.EquippedItem then
		reloading = ToHeldDevice(Owner.EquippedItem):IsReloading();
	end
	return {
		Target = Target,
		TargetID = Target and Target.UniqueID or nil,
		Seen = AI.Target ~= nil and MovableMan:ValidMO(AI.Target),
		UnseenTarget = AI.UnseenTarget ~= nil,
		Behavior = AI.BehaviorName,
		Cover = AI.Cover ~= nil,
		Retreat = AI.Retreat ~= nil,
		Flank = AI.Flank ~= nil,
		Medikit = AI.useMedikit == true,
		Reloading = reloading,
		Health = Owner.Health,
		Suppression = SharedBehaviors.Suppression(AI, Owner) or 0,
		Mode = Owner.AIMode,
		Firing = AI.fire == true,
	};
end
