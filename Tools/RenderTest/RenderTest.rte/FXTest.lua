function FXTestScript:StartScript()
	self.timer = Timer();
	self.index = 0;
end

function FXTestScript:UpdateScript()
	if self.timer:IsPastSimMS(700) then
		self.timer:Reset();
		self.index = self.index + 1;
		local center = CameraMan:GetOffset(0) + Vector(FrameMan.PlayerScreenWidth * 0.35, FrameMan.PlayerScreenHeight * 0.55);
		local pos = center + Vector((self.index % 5) * 60 - 120, (self.index % 3) * 25 - 25);
		-- Every fourth detonation is napalm, which leaves fire burning for a while.
		local presetName = (self.index % 4 == 0) and "Napalm Bomb" or "Frag Grenade";
		local bomb = CreateTDExplosive(presetName, "Base.rte");
		if bomb then
			bomb.Pos = pos;
			MovableMan:AddItem(bomb);
			bomb:GibThis();
		end
	end
end
