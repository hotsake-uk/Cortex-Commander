-- A flying arrow or bolt: shows the frame drawn at the angle it's flying, and thuds once when it strikes something. It sticks where it
-- strikes: in the ground or a wall it stays, quivering, at the angle it went in; in a unit or a shield a copy stays in them (the arrow
-- itself goes on into the wound), moving with them, until they're gone.

local stuckLife = 15000;

-- The frame drawn for a direction (the sprite has one frame per sixteenth of a turn).
local function frameFor(self, angle)
	return math.floor(angle / (2 * math.pi) * self.FrameCount + 0.5) % self.FrameCount;
end

function Create(self)
	self.lastSpeed = self.Vel.Magnitude;
	self.struck = false;
	if self:NumberValueExists("StuckToID") then
		-- A copy left in someone: it rides on them.
		self.stuckToID = self:GetNumberValue("StuckToID");
		self.stuckLocal = Vector(self:GetNumberValue("StuckLocalX"), self:GetNumberValue("StuckLocalY"));
		self.stuckAngle = self:GetNumberValue("StuckAngle");
		self.HitsMOs = false;
		self.GetsHitByMOs = false;
		self.PinStrength = 1000;
		self.Vel = Vector();
	end
end

function Update(self)
	if self.stuckToID then
		local host = MovableMan:FindObjectByUniqueID(self.stuckToID);
		if not host or host.ToDelete or not MovableMan:ValidMO(host) then
			self.ToDelete = true;
			return;
		end
		local flip = host.HFlipped and -1 or 1;
		self.Pos = host.Pos + Vector(self.stuckLocal.X * flip, self.stuckLocal.Y):RadRotate(host.RotAngle);
		local angle = host.HFlipped and (host.RotAngle + math.pi - self.stuckAngle) or (host.RotAngle + self.stuckAngle);
		self.Frame = frameFor(self, angle);
		self.Vel = Vector();
		return;
	end
	if self.stuckInGround then
		self.Vel = Vector();
		return;
	end

	local speed = self.Vel.Magnitude;
	if speed > 2 then
		self.flightAngle = self.Vel.AbsRadAngle;
		self.Frame = frameFor(self, self.flightAngle);
	end
	if not self.struck and speed < self.lastSpeed * 0.5 and self.lastSpeed > 15 then
		self.struck = true;
		AudioMan:PlaySound("Medieval.rte/Sounds/ArrowHit" .. math.random(1, 2) .. ".flac", self.Pos);
	end
	self.lastSpeed = speed;
end

function OnCollideWithTerrain(self, terrainID)
	terrainID = tonumber(terrainID);
	if self.stuckToID or self.stuckInGround or self.lastSpeed < 12 then
		return;
	end
	-- In it, unless it's metal it glances off.
	local material = SceneMan:GetMaterialFromID(terrainID);
	if material and (string.find(material.PresetName, "Metal") or material.StructuralIntegrity > self.Sharpness * 2.5) then
		return;
	end
	self.stuckInGround = true;
	self.PinStrength = 1000;
	self.Vel = Vector();
	self.AngularVel = 0;
	self.Lifetime = self.Age + stuckLife;
end

function OnCollideWithMO(self, hitMO, hitMORootParent)
	if self.stuckToID or self.leftCopy or self.lastSpeed < 12 or not hitMO or not IsMOSprite(hitMO) then
		return;
	end
	self.leftCopy = true;
	local stuck = CreateMOSParticle(self.PresetName, "Medieval.rte");
	if not stuck then
		return;
	end
	local host = ToMOSprite(hitMO);
	local angle = self.flightAngle or self.Vel.AbsRadAngle;
	-- A little way in from where it struck, in the host's own frame (unflipped, unrotated).
	local pos = self.Pos + Vector(3, 0):RadRotate(angle);
	local offset = SceneMan:ShortestDistance(host.Pos, pos, SceneMan.SceneWrapsX):RadRotate(-host.RotAngle);
	local relativeAngle = angle - host.RotAngle;
	if host.HFlipped then
		offset.X = -offset.X;
		relativeAngle = math.pi - relativeAngle;
	end
	stuck.Pos = pos;
	stuck.Vel = Vector();
	stuck.Lifetime = stuckLife;
	stuck.Frame = frameFor(self, angle);
	stuck:SetNumberValue("StuckToID", host.UniqueID);
	stuck:SetNumberValue("StuckLocalX", offset.X);
	stuck:SetNumberValue("StuckLocalY", offset.Y);
	stuck:SetNumberValue("StuckAngle", relativeAngle);
	MovableMan:AddParticle(stuck);
end
