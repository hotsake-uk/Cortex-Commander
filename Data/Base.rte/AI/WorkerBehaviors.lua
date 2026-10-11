-- Colony workers (a woodcutter's staff): swinging their tool at what they are cutting. Where they go and what they cut is the engine's
-- (Colony.cpp), which walks them there and cuts the wood away as they swing; this only swings the tool.
--
-- The number values the engine sets on a worker:
--   ColonyWork 1 while it is to cut, 0 otherwise; ColonyWorkX, ColonyWorkY what it cuts.
--   ColonyTool (a string value) the tool it cuts with: the axe swung as a melee weapon, or the laser saw run like a gun.
-- And the one this sets for the engine: ColonyWorkSwing 1 while it is swinging at it, 0 otherwise.

WorkerBehaviors = {};

local REACH = 54; -- How far from its middle what it cuts can be (the engine cuts within 46 px, and looks for 54).

-- Once an update, from the human AI, after the flee check.
function WorkerBehaviors.Update(AI, Owner)
	AI.workSwing = false;
	if not Owner:NumberValueExists("ColonyWork") then
		return;
	end
	local swing = false;
	-- (Not while someone else has it, or it is fighting, running, or on its way somewhere.)
	if Owner:GetNumberValue("ColonyWork") > 0 and not Owner:IsPlayerControlled() and not AI.Flee and not AI.Target and not AI.UnseenTarget
		and Owner.AIMode == Actor.AIMODE_SENTRY then
		local Aim = Vector(Owner:GetNumberValue("ColonyWorkX"), Owner:GetNumberValue("ColonyWorkY"));
		local Way = SceneMan:ShortestDistance(Owner.EyePos, Aim, false);
		local Held = Owner.EquippedItem;
		if not SceneMan:ShortestDistance(Owner.Pos, Aim, false):MagnitudeIsGreaterThan(REACH) and Held and Held.PresetName == Owner:GetStringValue("ColonyTool") then
			AI.deviceState = AHuman.AIMING;
			AI.lateralMoveState = Actor.LAT_STILL;
			AI.Ctrl.AnalogAim = Way.Normalized; -- (Aimed behind it, it turns round to face it.)
			swing = true;
		end
	end
	AI.workSwing = swing;
	Owner:SetNumberValue("ColonyWorkSwing", swing and 1 or 0);
end
