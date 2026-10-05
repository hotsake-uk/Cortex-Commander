function ColonyTestScript:StartScript()
	self.timer = Timer();
end

function ColonyTestScript:UpdateScript()
	-- A barracks for each of two sides and an extractor between them, on the ground in the middle of the map.
	if not self.done and self.timer.ElapsedRealTimeMS > 3000 then
		self.done = true;
		local middle = Vector(SceneMan.SceneWidth * 0.5, 0);
		while middle.Y < SceneMan.SceneHeight - 1 and SceneMan:GetTerrMatter(middle.X, middle.Y) == rte.airID do
			middle.Y = middle.Y + 4;
		end
		middle.Y = middle.Y - 60;
		SandboxDo("Barracks", middle + Vector(-150, 0), 0, 0, 2, "Soldier Light");
		SandboxDo("Barracks", middle + Vector(150, 0), 1, 0, 2, "Soldier Light");
		SandboxDo("Extractor", middle, 0, 0, 1, "");
		SandboxDo("Look around", middle + Vector(0, -20), 0, 0, 1, "");
	end
end
