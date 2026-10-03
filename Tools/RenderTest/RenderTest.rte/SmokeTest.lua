function SmokeTestScript:StartScript()
	self.timer = Timer();
	self.spawnTimer = Timer();
end

function SmokeTestScript:UpdateScript()
	-- A steady column of smoke rising past a flickering orange light, to show light scattering in smoke.
	local base = CameraMan:GetOffset(0) + Vector(FrameMan.PlayerScreenWidth * 0.3, FrameMan.PlayerScreenHeight * 0.6);
	if self.spawnTimer:IsPastSimMS(25) then
		self.spawnTimer:Reset();
		local smoke = CreateMOSParticle("Smoke Ball 1", "Base.rte");
		if smoke then
			smoke.Pos = base + Vector(math.random(-6, 6), 0);
			smoke.Vel = Vector(math.random(-10, 10) * 0.1, -math.random(10, 25) * 0.1);
			MovableMan:AddParticle(smoke);
		end
	end
	local flicker = 0.8 + 0.2 * math.sin(self.timer.ElapsedRealTimeMS * 0.02) * math.sin(self.timer.ElapsedRealTimeMS * 0.013);
	PostProcessMan:AddLight(base + Vector(0, -30), 160, 255, 140, 60, 1.6 * flicker);
end
