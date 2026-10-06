function PlayMenuTestScript:StartScript()
	self.timer = Timer();
end

function PlayMenuTestScript:UpdateScript()
	-- Saves the settings as a preset, changes the hour, loads the preset back (the hour should return), then opens every tool window, as Tab does.
	-- In the Sandbox game mode the world then stands still.
	if not self.done and self.timer.ElapsedRealTimeMS > 4000 then
		self.done = true;
		local saved = SettingsMan:SavePreset("Render test");
		local hourBefore = PostProcessMan.TimeOfDay;
		PostProcessMan.TimeOfDay = 23;
		local loaded = SettingsMan:LoadPreset("Render test");
		ConsoleMan:PrintString("PRESET TEST saved '" .. saved .. "' loaded " .. tostring(loaded) .. " hour " .. hourBefore .. " -> " .. PostProcessMan.TimeOfDay);
		SettingsMan:DeletePreset("Render test");
		SandboxSetPins("Units=Soldier Heavy;Units=Dummy;Structure=Brain Vault;Item=Heavy Digger;Water=;Lightning=");
		DebugMan:OpenTools();
	end
end
