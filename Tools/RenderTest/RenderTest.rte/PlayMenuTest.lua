function PlayMenuTestScript:StartScript()
	self.timer = Timer();
end

function PlayMenuTestScript:UpdateScript()
	-- Opens every tool window, as Tab does. In the Sandbox game mode the world then stands still.
	if not self.done and self.timer.ElapsedRealTimeMS > 4000 then
		self.done = true;
		DebugMan:OpenTools();
	end
end
