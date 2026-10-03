function SaveTestScript:StartScript()
	self.timer = Timer();
	self.stage = 0;
	-- Only save and load once: after loading, this script starts again.
	if io and io.open then
		local flag = io.open("savetest.flag", "r");
		if flag then
			flag:close();
			self.stage = 3;
			self.reportTimer = Timer();
		end
	end
end

function SaveTestScript:UpdateScript()
	-- After the fire test's napalm has set the hill burning, quicksave (the test harness then presses F9 to load it), to check fire survives a save.
	if self.stage == 0 and self.timer:IsPastSimMS(6500) then
		self.stage = 1;
		ActivityMan:SaveGame("QuickSave");
		local flag = io and io.open and io.open("savetest.flag", "w");
		if flag then
			flag:write("1");
			flag:close();
		end
	elseif self.stage == 3 and self.reportTimer:IsPastSimMS(500) then
		self.stage = 4;
		ConsoleMan:SaveAllText("savetest.txt");
	end
end
