-- The Ogre's trample (phase 3): going at speed, it knocks small units not of its side out of its way: aside and off their feet (Status
-- UNSTABLE), no more than once a second each. The Ogre's number values:
--   TrampleSpeed    how fast it must be going, in m/s (default 2)
--   TrampleMass     the heaviest a unit it shoulders aside is, in kg (default 150)
--   TrampleReach    how far in front of its middle it shoulders them, in pixels (default 30)

local function Number(self, key, default)
	return self:NumberValueExists(key) and self:GetNumberValue(key) or default;
end

function Create(self)
	self.trampleTimer = Timer();
	self.trampled = {};
end

function Update(self)
	if not self.trampleTimer:IsPastSimMS(100) or self.Status ~= Actor.STABLE then
		return;
	end
	self.trampleTimer:Reset();
	local speed = math.abs(self.Vel.X);
	if speed < Number(self, "TrampleSpeed", 2) then
		return;
	end
	local ahead = self.Vel.X > 0 and 1 or -1;
	local reach = Number(self, "TrampleReach", 30);
	local maxMass = Number(self, "TrampleMass", 150);
	local now = TimerMan.SimTimeMS;
	for Act in MovableMan.Actors do
		if Act.Team ~= self.Team and Act.ID ~= self.ID and Act.Status < Actor.DYING and Act.Mass <= maxMass then
			local To = SceneMan:ShortestDistance(self.Pos, Act.Pos, false);
			if To.X * ahead > 0 and math.abs(To.X) < reach + Act.Radius * 0.5 and math.abs(To.Y) < self.Radius
				and (not self.trampled[Act.UniqueID] or now - self.trampled[Act.UniqueID] > 1000) then
				self.trampled[Act.UniqueID] = now;
				Act:AddImpulseForce(Vector(ahead * (speed + 4), -3) * Act.Mass, Vector());
				if Act.Status == Actor.STABLE then
					Act.Status = Actor.UNSTABLE;
				end
			end
		end
	end
end
