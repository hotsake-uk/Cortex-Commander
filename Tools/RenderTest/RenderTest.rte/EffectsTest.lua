function EffectsTestScript:StartScript()
	self.timer = Timer();
	self.done = false;
end

function EffectsTestScript:UpdateScript()
	-- Puts down a row of the sandbox's placeable effects across the view, to see them all at once.
	if not self.done and self.timer:IsPastSimMS(2500) then
		self.done = true;
		local origin = CameraMan:GetOffset(0);
		local w = FrameMan.PlayerScreenWidth;
		local h = FrameMan.PlayerScreenHeight;
		local names = {"Nuclear glow", "Red alarm", "Blue beacon", "Searchlight", "Disco", "Campfire", "Welding arc", "Fireflies", "Portal", "Spark fountain", "Mist vent", "Lava glow"};
		for i, name in ipairs(names) do
			local column = (i - 1) % 6;
			local row = math.floor((i - 1) / 6);
			SandboxDo("Effect", origin + Vector(w * (0.1 + 0.16 * column), h * (0.22 + 0.3 * row)), 0, 0, 1, name);
		end
	end
end
