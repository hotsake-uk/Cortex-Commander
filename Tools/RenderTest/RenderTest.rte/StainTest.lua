function StainTestScript:StartScript()
	self.timer = Timer();
end

function StainTestScript:UpdateScript()
	-- Sprays blood and oil at the ground near the camera, to show terrain stains.
	if self.timer:IsPastSimMS(30) then
		self.timer:Reset();
		local base = CameraMan:GetOffset(0) + Vector(FrameMan.PlayerScreenWidth * 0.42, FrameMan.PlayerScreenHeight * 0.55);
		for i = 1, 6 do
			local drop = CreateMOPixel("Blood Normal", "Base.rte");
			if drop then
				drop.Pos = base + Vector(math.random(-20, 20), 0);
				drop.Vel = Vector(math.random(-60, 60) * 0.1, math.random(60, 140) * 0.1);
				MovableMan:AddParticle(drop);
			end
		end
	end
end
