function PlayTestScript:StartScript()
	self.timer = Timer();
end

function PlayTestScript:UpdateScript()
	-- Opens the tool windows and puts them away again, as two presses of Tab do: with a tool in hand (a new game starts with Units) that only hides them.
	-- Then the play key steps into the player's character, and a second press much later comes back above.
	local t = self.timer.ElapsedRealTimeMS;
	if not self.hidden and t > 4000 then
		self.hidden = true;
		DebugMan:OpenTools();
		DebugMan:ToggleTools(false);
	end
	if not self.played and t > 6000 then
		self.played = true;
		SandboxTogglePlay(false);
	end
	if not self.back and t > 16000 then
		self.back = true;
		SandboxTogglePlay(false);
	end
end
