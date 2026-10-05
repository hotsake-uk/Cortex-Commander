TurretBehaviors = {};

function TurretBehaviors.Patrol(AI, Owner, Abort)
	local TurnTimer = Timer();
	local turn = 3000;

	while true do
		if TurnTimer:IsPastSimMS(turn) then
			TurnTimer:Reset();
			turn = RangeRand(2000, 6000);
			Owner:SetAimAngle(RangeRand(-0.7, 0.7));
			Owner.HFlipped = not Owner.HFlipped;
		end

		local _ai, _ownr, _abrt = coroutine.yield(); -- wait until next frame
		if _abrt then return true end
	end

	return true;
end

-- stop the user from inadvertently modifying the storage table
-- Mods written for older versions call the behaviours that are shared between kinds of unit (Patrol, GoToWpt, BrainSearch, GetTeamShootingSkill and so on) through this table.
-- They live in SharedBehaviors now: anything not found here is looked up there, so those mods keep working.
require("AI/SharedBehaviors");
local Storage = TurretBehaviors;
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
		error("The TurretBehaviors table is read-only.", 2);
	end
};
setmetatable(Proxy, Mt);
TurretBehaviors = Proxy;
