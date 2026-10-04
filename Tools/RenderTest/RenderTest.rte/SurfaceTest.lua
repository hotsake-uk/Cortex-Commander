function SurfaceTestScript:StartScript()
	self.timer = Timer();
	self.stage = 0;
end

function SurfaceTestScript:UpdateScript()
	-- For looking at what has happened to things, in the tutorial bunker's bottom corridor (run it in the sandbox activity on that scene).
	-- Soldiers and robots stand in a row with a lamp among them. From the left: soldier as he is, wet, sooty, snowed on; then a Combat Robot as it is, with a glowing hot gun and head, and sooty.
	-- The states are held every update, since they wear off by themselves.
	if self.stage == 0 and self.timer:IsPastSimMS(300) then
		self.stage = 1;
		SandboxPauseAI(true);
		SandboxDo("Look around", Vector(190 + FrameMan.PlayerScreenWidth * 0.5, 300 + FrameMan.PlayerScreenHeight * 0.5), 0, 0, 1, "");
	elseif self.stage == 1 and self.timer:IsPastSimMS(1500) then
		self.stage = 2;
		local units = {"Soldier Light", "Soldier Light", "Soldier Light", "Soldier Light", "Combat Robot", "Combat Robot", "Combat Robot"};
		local places = {815, 865, 915, 965, 1030, 1080, 1130};
		for i, name in ipairs(units) do
			SandboxDo("Units", Vector(places[i], 705), 0, 0, 1, name);
		end
	end
	if self.stage == 2 then
		local ok, problem = pcall(function()
			for actor in MovableMan.Actors do
				local x = actor.Pos.X;
				if x > 840 and x < 890 then
					actor.Wetness = 1;
				elseif x > 890 and x < 940 then
					actor.Soot = 1;
				elseif x > 940 and x < 990 then
					actor.SnowCover = 1;
				elseif x > 1055 and x < 1105 then
					-- Its gun and head, as after a lot of firing and a hit or two.
					for part in actor.Attachables do
						if part.ClassName ~= "Leg" and part.ClassName ~= "Arm" then
							part.Heat = 0.8;
						end
					end
					local human = ToAHuman(actor);
					if human and human.EquippedItem then
						human.EquippedItem.Heat = 0.9;
					end
				elseif x > 1105 and x < 1155 then
					actor.Soot = 1;
				end
			end
		end);
		if not ok then
			PrimitiveMan:DrawTextPrimitive(Vector(CameraMan:GetOffset(0).X + 20, CameraMan:GetOffset(0).Y + 60), tostring(problem), false, 0);
		end
	end
	PostProcessMan:AddLight(Vector(995, 700), 240, 255, 215, 170, 2.0);
end
