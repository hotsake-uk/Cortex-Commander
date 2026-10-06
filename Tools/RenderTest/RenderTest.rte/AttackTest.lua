function AttackTestScript:StartScript()
	self.timer = Timer();
end

function AttackTestScript:UpdateScript()
	-- Three red soldiers on the left are told to attack one green unit far to the right, with a nearer green unit in between. They must go for the chosen one.
	local middle = Vector(SceneMan.SceneWidth * 0.5, 0);
	while middle.Y < SceneMan.SceneHeight - 1 and SceneMan:GetTerrMatter(middle.X, middle.Y) == rte.airID do
		middle.Y = middle.Y + 4;
	end
	local t = self.timer.ElapsedRealTimeMS;
	if not self.done and t > 4000 then
		self.done = true;
		SandboxDo("Units", middle + Vector(-170, -30), 0, 0, 3, "Soldier Light");
		SandboxDo("Units", middle + Vector(40, -30), 1, 5, 1, "Dummy");
		SandboxDo("Units", middle + Vector(200, -30), 1, 5, 1, "Dummy");
		SandboxDo("Look around", middle + Vector(0, -40), 0, 0, 1, "");
	end
	if self.done and not self.ordered and t > 6000 then
		self.ordered = true;
		SandboxDo("Select", middle + Vector(-170, 20), 0, 0, 150, "");
	end
	if self.ordered and not self.attacked and t > 6500 then
		self.attacked = true;
		local far = nil;
		for actor in MovableMan.Actors do
			if actor.Team == 1 and (far == nil or actor.Pos.X > far.Pos.X) then
				far = actor;
			end
		end
		if far then
			SandboxDo("Command", far.Pos, 0, 0, 0, "");
		end
	end
end
