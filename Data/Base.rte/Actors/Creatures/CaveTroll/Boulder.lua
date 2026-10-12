-- A troll's boulder in flight (TrollThrow.lua lobs it): the first unit not of the troll's side it strikes loses BoulderDamage health
-- (default 30) and is knocked back off its feet as a club's blow does (Shared/Club.lua's ClubSpeed and ClubMass), and the rock bursts
-- into stone. Hitting the ground hard it bursts too; dropped gently it lies there as a rock.

local function Number(self, key, default)
	return self:NumberValueExists(key) and self:GetNumberValue(key) or default;
end

function Create(self)
	self.struck = false;
end

function OnCollideWithMO(self, mo, rootMO)
	if self.struck or not rootMO or not IsActor(rootMO) or rootMO.Team == self.Team then
		return;
	end
	local Victim = ToActor(rootMO);
	if Victim.Status >= Actor.DYING then
		return;
	end
	self.struck = true;
	Victim.Health = Victim.Health - Number(self, "BoulderDamage", 30);
	Victim:FlashWhite(120);
	local Away = Vector(self.Vel.X, 0):SetMagnitude(1) + Vector(0, -0.6);
	Victim:AddImpulseForce(Away:SetMagnitude(Number(self, "ClubSpeed", 10) * math.min(Victim.Mass, Number(self, "ClubMass", 220))), Vector());
	if Victim.Status == Actor.STABLE then
		Victim.Status = Actor.UNSTABLE;
	end
	self:GibThis();
end

function OnCollideWithTerrain(self, terrainID)
	if self.Vel.Magnitude > 7 then
		self:GibThis();
	end
end
