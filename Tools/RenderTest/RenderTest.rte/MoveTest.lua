function MoveTestScript:StartScript()
	self.timer = Timer();
end

function MoveTestScript:UpdateScript()
	-- A squad on the left of the hill is ordered to the right, then, while still on its way, back to the left: the second order must take.
	-- Then the command tool is put in hand with the ring open at the test pointer.
	local middle = Vector(SceneMan.SceneWidth * 0.5, 0);
	while middle.Y < SceneMan.SceneHeight - 1 and SceneMan:GetTerrMatter(middle.X, middle.Y) == rte.airID do
		middle.Y = middle.Y + 4;
	end
	local t = self.timer.ElapsedRealTimeMS;
	if not self.done and t > 4000 then
		self.done = true;
		SandboxDo("Units", middle + Vector(-160, -30), 0, 5, 5, "Soldier Light");
		SandboxDo("Look around", middle + Vector(0, -40), 0, 0, 1, "");
	end
	if self.done and not self.moved and t > 6000 then
		self.moved = true;
		SandboxDo("Move a side here", middle + Vector(140, -20), 0, 0, 1, "");
	end
	if self.moved and not self.back and t > 9000 then
		self.back = true;
		SandboxDo("Move a side here", middle + Vector(-220, -20), 0, 0, 1, "");
	end
	if self.back and not self.opened and t > 15000 then
		self.opened = true;
		SandboxDo("Command", middle, 0, 0, 1, "");
		DebugMan:OpenTools();
	end
end
