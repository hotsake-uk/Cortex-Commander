function LightsTestScript:StartScript()
	self.timer = Timer();
end

function LightsTestScript:UpdateScript()
	for actor in MovableMan.Actors do
		if actor.LightRadius == 0 then
			actor.LightRadius = 140;
			actor.LightIntensity = 0.9;
			actor:SetLightColor(255, 230, 190);
			actor.LightOffset = Vector(0, -12);
		end
	end
	local pulse = 0.6 + 0.4 * math.sin(self.timer.ElapsedRealTimeMS * 0.004);
	local center = CameraMan:GetOffset(0) + Vector(FrameMan.PlayerScreenWidth * 0.3, FrameMan.PlayerScreenHeight * 0.5);
	PostProcessMan:AddLight(center, 220, 90, 160, 255, 1.5 * pulse);
end
