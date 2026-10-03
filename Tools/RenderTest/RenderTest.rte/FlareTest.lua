function FlareTestScript:StartScript()
	self.timer = Timer();
	self.thrown = false;
end

function FlareTestScript:UpdateScript()
	-- Throws a lit flare out from player 1's soldier.
	if self.flare and MovableMan:ValidMO(self.flare) and (self.lastReport == nil or self.timer.ElapsedSimTimeMS - self.lastReport > 1000) then
		self.lastReport = self.timer.ElapsedSimTimeMS;
		print("FLARETEST valid=" .. tostring(MovableMan:ValidMO(self.flare)) .. " pos=" .. tostring(self.flare.Pos) .. " light=" .. tostring(self.flare.LightRadius) .. " act=" .. tostring(self.flare:IsActivated()) .. " cam=" .. tostring(CameraMan:GetOffset(0)) .. " actor=" .. tostring(ActivityMan:GetActivity():GetControlledActor(0).Pos));
		ConsoleMan:SaveAllText("flaredebug.txt");
	end
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
			self.flare = flare;
		end
	end
end
