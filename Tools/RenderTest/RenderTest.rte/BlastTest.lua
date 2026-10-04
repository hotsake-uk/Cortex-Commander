function BlastTestScript:StartScript()
	self.timer = Timer();
	self.blastTimer = Timer();
	self.index = 0;
end

function BlastTestScript:Spot()
	-- A fixed pattern of points spread over the view, so every run is the same.
	self.index = self.index + 1;
	local x = 0.12 + 0.76 * ((self.index * 0.618) % 1);
	local y = 0.3 + 0.55 * ((self.index * 0.381) % 1);
	return CameraMan:GetOffset(0) + Vector(FrameMan.PlayerScreenWidth * x, FrameMan.PlayerScreenHeight * y);
end

function BlastTestScript:UpdateScript()
	-- For performance runs (see CCCP_PERF_LOG). After five quiet seconds:
	--  5-20 s: a blast on the bunker and the ground around it every 300 ms (two grenades, then a big bomb).
	-- 20-35 s: four big bombs at once every 1.5 seconds.
	-- 35-45 s: a dropship blown up above the bunker every 2 seconds, its wreckage falling on it.
	-- Then quiet again.
	local t = self.timer.ElapsedSimTimeMS;
	if t > 5000 and t < 20000 and self.blastTimer:IsPastSimMS(300) then
		self.blastTimer:Reset();
		SandboxDo((self.index % 3 == 2) and "Big bomb" or "Grenade blast", self:Spot(), 0, 0, 1, "");
	elseif t >= 20000 and t < 35000 and self.blastTimer:IsPastSimMS(1500) then
		self.blastTimer:Reset();
		for i = 1, 4 do
			SandboxDo("Big bomb", self:Spot(), 0, 0, 1, "");
		end
	elseif t >= 35000 and t < 45000 and self.blastTimer:IsPastSimMS(2000) then
		self.blastTimer:Reset();
		local ship = CreateACDropShip("Dropship MK1", "Base.rte");
		if ship then
			local spot = self:Spot();
			ship.Pos = Vector(spot.X, CameraMan:GetOffset(0).Y + FrameMan.PlayerScreenHeight * 0.15);
			ship.Team = 0;
			MovableMan:AddActor(ship);
			ship:GibThis();
		end
	end
end
