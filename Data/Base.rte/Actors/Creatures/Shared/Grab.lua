-- A grab (phase 3, the Scorpion's pincers): a unit not of the attacker's side that it strikes is held where it stands (Actor:Ensnare)
-- for the particle's GrabSeconds number value (default 1.2), like a short web: it can still turn, but not move or use its weapons.

function OnCollideWithMO(self, mo, rootMO)
	if self.struck then
		return;
	end
	self.struck = true;
	if rootMO and IsActor(rootMO) and rootMO.Team ~= self.Team then
		ToActor(rootMO):Ensnare(self:NumberValueExists("GrabSeconds") and self:GetNumberValue("GrabSeconds") or 1.2);
	end
end
