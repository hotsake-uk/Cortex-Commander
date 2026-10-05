function PlayTestScript:StartScript()
	self.timer = Timer();
end

function PlayTestScript:UpdateScript()
	-- Opens the tool windows and puts them away again, as two presses of Tab do: the second steps into the player's character.
	if not self.done and self.timer.ElapsedRealTimeMS > 4000 then
		self.done = true;
		DebugMan:OpenTools();
		DebugMan:ToggleTools(false);
	end
end
