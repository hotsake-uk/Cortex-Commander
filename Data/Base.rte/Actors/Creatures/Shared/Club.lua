-- A club's blow (phase 3, the Ogre's club): a unit not of the attacker's side that it strikes is knocked back, up off its feet and onto the
-- ground (Status UNSTABLE; it gets up again once it lies still). Heavier units are moved less: the push is as for one of at most
-- ClubMass kg. The particle's number values:
--   ClubSpeed  how hard it is knocked, in m/s (default 9)
--   ClubMass   the heaviest a unit is knocked as hard (default 200)

local function Number(self, key, default)
	return self:NumberValueExists(key) and self:GetNumberValue(key) or default;
end

function OnCollideWithMO(self, mo, rootMO)
	if self.struck then
		return;
	end
	self.struck = true;
	if not rootMO or not IsActor(rootMO) or rootMO.Team == self.Team then
		return;
	end
	local Victim = ToActor(rootMO);
	-- (Once a swing, though a swing is a few particles: not again within a third of a second.)
	local now = TimerMan.SimTimeMS;
	if Victim.Status >= Actor.DYING or (Victim:NumberValueExists("ClubbedAt") and now - Victim:GetNumberValue("ClubbedAt") < 300) then
		return;
	end
	Victim:SetNumberValue("ClubbedAt", now);
	local Away = Vector(self.Vel.X, 0):SetMagnitude(1) + Vector(0, -0.6);
	Victim:AddImpulseForce(Away:SetMagnitude(Number(self, "ClubSpeed", 9) * math.min(Victim.Mass, Number(self, "ClubMass", 200))), Vector());
	if Victim.Status == Actor.STABLE then
		Victim.Status = Actor.UNSTABLE;
	end
end
