function FlareTestScript:StartScript()
	self.timer = Timer();
	self.thrown = false;
end

function FlareTestScript:UpdateScript()
	-- Throws a lit flare out from player 1's soldier.
	if not self.thrown and self.timer:IsPastSimMS(4000) then
		local actor = ActivityMan:GetActivity():GetControlledActor(0);
		if not actor then
			return;
		end
		self.thrown = true;
		local flare = CreateTDExplosive("Flare", "Base.rte");
		if flare then
			flare.Pos = actor.Pos + Vector(-10, -20);
			flare.Vel = Vector(-6, -4);
			flare:Activate();
			MovableMan:AddItem(flare);
		end
	end
end
