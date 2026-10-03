function LiquidTestScript:StartScript()
	self.timer = Timer();
	self.pours = 0;
end

function LiquidTestScript:UpdateScript()
	-- Pours water, lava and acid onto the hill near the camera for a few seconds.
	if self.pours < 40 and self.timer:IsPastSimMS(4000 + self.pours * 150) then
		self.pours = self.pours + 1;
		local origin = CameraMan:GetOffset(0);
		local w = FrameMan.PlayerScreenWidth;
		local h = FrameMan.PlayerScreenHeight;
		SceneMan:PourLiquid(origin + Vector(w * 0.30, h * 0.25), 3, "Water");
		SceneMan:PourLiquid(origin + Vector(w * 0.50, h * 0.25), 3, "Lava");
		SceneMan:PourLiquid(origin + Vector(w * 0.70, h * 0.25), 3, "Acid");
	end
end
