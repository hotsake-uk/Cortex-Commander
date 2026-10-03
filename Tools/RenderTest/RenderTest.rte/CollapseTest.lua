function CollapseTestScript:StartScript()
	self.timer = Timer();
	self.stage = 0;
end

function CollapseTestScript:UpdateScript()
	-- Cuts a ring out of the hill near the camera, leaving a loose disc of earth, then sets off a grenade beside it so the collapse check runs.
	local center = CameraMan:GetOffset(0) + Vector(FrameMan.PlayerScreenWidth * 0.35, FrameMan.PlayerScreenHeight * 0.78);
	if self.stage == 0 and self.timer:IsPastSimMS(8000) then
		self.stage = 1;
		SceneMan:DislodgePixelRing(center, 18, 22, true);
	elseif self.stage == 1 and self.timer:IsPastSimMS(11000) then
		self.stage = 2;
		local grenade = CreateTDExplosive("Frag Grenade", "Base.rte");
		if grenade then
			grenade.Pos = center + Vector(60, 0);
			MovableMan:AddItem(grenade);
			grenade:GibThis();
		end
	end
end
