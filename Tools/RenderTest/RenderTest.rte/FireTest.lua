function FireTestScript:StartScript()
	self.timer = Timer();
	self.dropped = 0;
end

function FireTestScript:UpdateScript()
	-- Detonates napalm on the ground near the camera a couple of times, to show fire spreading through grass.
	if self.dropped < 2 and self.timer:IsPastSimMS(2000 + self.dropped * 2500) then
		self.dropped = self.dropped + 1;
		local pos = CameraMan:GetOffset(0) + Vector(FrameMan.PlayerScreenWidth * (0.36 + 0.12 * self.dropped), FrameMan.PlayerScreenHeight * 0.45);
		-- Drop it onto the surface below.
		for i = 1, 200 do
			if SceneMan:GetTerrMatter(pos.X, pos.Y + 1) ~= 0 then
				break;
			end
			pos.Y = pos.Y + 1;
		end
		local bomb = CreateTDExplosive("Napalm Bomb", "Base.rte");
		if bomb then
			bomb.Pos = pos + Vector(0, -4);
			MovableMan:AddItem(bomb);
			bomb:GibThis();
		end
	end
end
