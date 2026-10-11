-- Telekinetic Push (MG-1): each cast shoves everything in a cone in front of the hand away from it, and sets the air blowing that way.
-- The cone's numbers can be changed per preset with NumberValues: PushPower (metres a second at the hand), PushRange (pixels) and PushSpread (degrees).

function Create(self)
	self.pushPower = self:NumberValueExists("PushPower") and self:GetNumberValue("PushPower") or 30;
	self.pushRange = self:NumberValueExists("PushRange") and self:GetNumberValue("PushRange") or 150;
	self.pushSpread = self:NumberValueExists("PushSpread") and self:GetNumberValue("PushSpread") or 50;
end

function OnFire(self)
	local direction = Vector(self.FlipFactor * math.cos(self.RotAngle), -self.FlipFactor * math.sin(self.RotAngle));
	SceneMan:MagicPush(self.MuzzlePos, direction, self.pushPower, self.pushRange, self.pushSpread, self:GetRootParent());
end
