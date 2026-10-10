-- A flying arrow or bolt: shows the frame drawn at the angle it's flying, and thuds once when it strikes something.
function Create(self)
	self.lastSpeed = self.Vel.Magnitude;
	self.struck = false;
end

function Update(self)
	local speed = self.Vel.Magnitude;
	if speed > 2 then
		self.Frame = math.floor(self.Vel.AbsRadAngle / (2 * math.pi) * self.FrameCount + 0.5) % self.FrameCount;
	end
	if not self.struck and speed < self.lastSpeed * 0.5 and self.lastSpeed > 15 then
		self.struck = true;
		AudioMan:PlaySound("Medieval.rte/Sounds/ArrowHit" .. math.random(1, 2) .. ".flac", self.Pos);
	end
	self.lastSpeed = speed;
end
