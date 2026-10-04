function LightFixtureTestScript:StartScript()
	self.timer = Timer();
	self.stage = 0;
end

function LightFixtureTestScript:UpdateScript()
	-- For looking at the lights of the scenery, in the tutorial bunker (run it in the sandbox activity on that scene, by night).
	-- The bunker's own pieces bring their lamps. On top of those, one of each kind of placeable fixture is put up along the bottom corridor.
	if self.stage == 0 and self.timer:IsPastSimMS(300) then
		self.stage = 1;
		SandboxPauseAI(true);
		SandboxDo("Look around", Vector(190 + FrameMan.PlayerScreenWidth * 0.5, 300 + FrameMan.PlayerScreenHeight * 0.5), 0, 0, 1, "");
	elseif self.stage == 1 and self.timer:IsPastSimMS(1500) then
		self.stage = 2;
		-- Placed straight into the scene (the sandbox's own tool snaps to the building grid, and this bunker's ceilings aren't on it). The corridor's ceiling is at about 682, its floor at about 737.
		local fixtures = {
			{"Floor Lamp Warm", 816, 725}, {"Ceiling Lamp Warm", 852, 670}, {"Warning Beacon", 900, 670}, {"Strip Light White", 990, 670},
			{"Floodlight Down", 1040, 670}, {"Wall Lamp Green", 1080, 700}, {"Tiny Light Red", 1104, 710}, {"Ceiling Lamp Blue", 1128, 670},
		};
		for _, fixture in ipairs(fixtures) do
			local object = CreateTerrainObject(fixture[1], "Base.rte");
			object.Pos = Vector(fixture[2], fixture[3]);
			SceneMan:AddSceneObject(object);
		end
	end
end
