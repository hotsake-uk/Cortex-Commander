function MaterialTestScript:StartScript()
	self.timer = Timer();
	self.stage = 0;
end

function MaterialTestScript:UpdateScript()
	-- For looking at what things look like they're made of, in the tutorial bunker's bottom corridor (run it in the sandbox activity on that scene):
	-- units of different makes stand in a row with a warm lamp among them. From the left: plastic Dummy, soldier, Silver Man, Whitebot, Combat Robot, Browncoat.
	-- The AI is paused so nobody moves, and the god camera is put on a fixed spot, so shots with different settings line up.
	if self.stage == 0 and self.timer:IsPastSimMS(300) then
		self.stage = 1;
		SandboxPauseAI(true);
		SandboxDo("Look around", Vector(190 + FrameMan.PlayerScreenWidth * 0.5, 300 + FrameMan.PlayerScreenHeight * 0.5), 0, 0, 1, "");
	elseif self.stage == 1 and self.timer:IsPastSimMS(1500) then
		self.stage = 2;
		local units = {"Dummy", "Soldier Light", "Silver Man", "Whitebot", "Combat Robot", "Browncoat"};
		local places = {815, 870, 970, 1020, 1075, 1125};
		for i, name in ipairs(units) do
			SandboxDo("Units", Vector(places[i], 705), 0, 0, 1, name);
		end
	end
	PostProcessMan:AddLight(Vector(995, 700), 240, 255, 215, 170, 2.0);
end
