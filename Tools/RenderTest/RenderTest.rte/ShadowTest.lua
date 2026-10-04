function ShadowTestScript:StartScript()
	self.timer = Timer();
	self.stage = 0;
end

function ShadowTestScript:UpdateScript()
	-- For looking at shadows, in the tutorial bunker's bottom corridor (run it in the sandbox activity on that scene):
	-- a steady warm lamp hangs in the right-hand room with a soldier either side of it, and a third soldier stands in the room to the left.
	-- The AI is paused so nobody moves, and the god camera is put on a fixed spot, so shots with different settings line up.
	if self.stage == 0 and self.timer:IsPastSimMS(300) then
		self.stage = 1;
		SandboxPauseAI(true);
		SandboxDo("Look around", Vector(190 + FrameMan.PlayerScreenWidth * 0.5, 300 + FrameMan.PlayerScreenHeight * 0.5), 0, 0, 1, "");
	elseif self.stage == 1 and self.timer:IsPastSimMS(1500) then
		self.stage = 2;
		for _, x in ipairs({840, 975, 1095}) do
			SandboxDo("Units", Vector(x, 705), 0, 0, 1, "Soldier Light");
		end
	end
	PostProcessMan:AddLight(Vector(1035, 712), 260, 255, 205, 150, 2.2);
end
