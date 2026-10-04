function LightBreakTestScript:StartScript()
	self.timer = Timer();
	self.stage = 0;
end

function LightBreakTestScript:UpdateScript()
	-- For checking that lamps go out when they're destroyed, in the tutorial bunker (sandbox activity, by night, best in the lighting-only view).
	-- The ceiling around the bunker's lamp over the door marked 0 is dug away: it should be dark.
	-- Three green wall lamps are put up in the bottom corridor, on the back wall with nothing solid near them. A shot is fired through the first and a grenade goes off by the second.
	-- Only the third should still shine.
	if self.stage == 0 and self.timer:IsPastSimMS(300) then
		self.stage = 1;
		SandboxPauseAI(true);
		SandboxDo("Look around", Vector(190 + FrameMan.PlayerScreenWidth * 0.5, 300 + FrameMan.PlayerScreenHeight * 0.5), 0, 0, 1, "");
	elseif self.stage == 1 and self.timer:IsPastSimMS(1500) then
		self.stage = 2;
		for _, x in ipairs({960, 1030, 1110}) do
			local lamp = CreateTerrainObject("Wall Lamp Green", "Base.rte");
			lamp.Pos = Vector(x, 700);
			SceneMan:AddSceneObject(lamp);
		end
	elseif self.stage == 2 and self.timer:IsPastSimMS(3000) then
		self.stage = 3;
		SandboxDo("Dig", Vector(767, 692), 0, 0, 14, "");
		-- The lamps' lights are six pixels in from their corners.
		local shot = CreateMOPixel("Water Jet Drop", "Base.rte");
		shot.Pos = Vector(930, 706);
		shot.Vel = Vector(60, 0);
		shot.Sharpness = 50;
		MovableMan:AddParticle(shot);
		SandboxDo("Grenade blast", Vector(1036, 725), 0, 0, 1, "");
	end
end
