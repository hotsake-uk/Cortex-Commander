function MoveTestScript:StartScript()
	self.timer = Timer();
end

function MoveTestScript:UpdateScript()
	-- A squad on the left of the hill is ordered to a place on its right; the move tool stays in hand so the preview shows at the test pointer.
	local middle = Vector(SceneMan.SceneWidth * 0.5, 0);
	while middle.Y < SceneMan.SceneHeight - 1 and SceneMan:GetTerrMatter(middle.X, middle.Y) == rte.airID do
		middle.Y = middle.Y + 4;
	end
	if not self.done and self.timer.ElapsedRealTimeMS > 4000 then
		self.done = true;
		SandboxDo("Units", middle + Vector(-160, -30), 0, 0, 5, "Soldier Light");
		SandboxDo("Look around", middle + Vector(0, -40), 0, 0, 1, "");
	end
	if self.done and not self.moved and self.timer.ElapsedRealTimeMS > 6000 then
		self.moved = true;
		SandboxDo("Move a side here", middle + Vector(140, -20), 0, 0, 1, "");
	end
	if self.moved and not self.opened and self.timer.ElapsedRealTimeMS > 9000 then
		self.opened = true;
		DebugMan:OpenTools();
	end
end
