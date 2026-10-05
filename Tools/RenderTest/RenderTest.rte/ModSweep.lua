function ModSweepScript:StartScript()
	self.timer = Timer();
	self.moduleIndex = PresetMan:GetOfficialModuleCount() - 1;
	self.phase = "wait";
	self.started = false;
end

-- For checking that mods work in play, not just load (run it in the sandbox activity; set CCCP_CONSOLE_LOG to collect what the console says).
-- Takes each mod in turn: puts its units and craft on the ground near the middle of the view, hands its weapons and tools to soldiers, puts a few enemies in front of them,
-- lets them fight for a few seconds, then clears everything away. Lines starting MODSWEEP in the console mark where each mod begins and ends, so errors between them are that mod's.
local unitMakers = {AHuman = CreateAHuman, ACrab = CreateACrab, ACDropShip = CreateACDropShip, ACRocket = CreateACRocket, Actor = CreateActor};
local itemMakers = {HDFirearm = CreateHDFirearm, TDExplosive = CreateTDExplosive, HeldDevice = CreateHeldDevice, ThrownDevice = CreateThrownDevice};

local function ground(x)
	return SceneMan:MovePointToGround(Vector(x, 0), 30, 5);
end

function ModSweepScript:ClearAway()
	for actor in MovableMan.Actors do
		if not actor:IsInGroup("Brains") then
			actor.ToDelete = true;
		end
	end
	for item in MovableMan.Items do
		item.ToDelete = true;
	end
	for particle in MovableMan.Particles do
		particle.ToDelete = true;
	end
end

function ModSweepScript:Populate(module)
	local name = module.FileName;
	local middle = CameraMan:GetOffset(0).X + FrameMan.PlayerScreenWidth * 0.5;
	local units, items = 0, 0;
	local firstWeapon = nil;
	local queue = {};
	for entity in module.Presets do
		if itemMakers[entity.ClassName] and entity.ClassName == "HDFirearm" and not firstWeapon then
			firstWeapon = entity.PresetName;
		end
	end
	for entity in module.Presets do
		local className = entity.ClassName;
		-- Only what a player can actually get: mods are full of helper objects and templates that aren't meant to be put into the world on their own.
		if unitMakers[className] or itemMakers[className] then
			local known, buyable = pcall(function() return ToSceneObject(entity).Buyable; end);
			if known and buyable == false then
				className = "";
			end
		end
		if unitMakers[className] and units < 24 and className ~= "Actor" then
			local presetName, slot = entity.PresetName, units;
			table.insert(queue, function()
			print("MODSWEEP making " .. className .. " " .. presetName);
			local ok, problem = pcall(function()
				local unit = unitMakers[className](presetName, name);
				unit.Team = 0;
				unit.Pos = ground(middle - 40 - slot * 22) + Vector(0, className == "AHuman" and -30 or -60);
				if className == "AHuman" and firstWeapon then
					unit:AddInventoryItem(CreateHDFirearm(firstWeapon, name));
				end
				unit.AIMode = Actor.AIMODE_SENTRY;
				MovableMan:AddActor(unit);
			end);
			if not ok then
				print("MODSWEEP PROBLEM making " .. className .. " '" .. presetName .. "' of " .. name .. ": " .. tostring(problem));
			end
			end);
			units = units + 1;
		elseif itemMakers[className] and items < 24 then
			local presetName, slot = entity.PresetName, items;
			table.insert(queue, function()
			print("MODSWEEP making " .. className .. " " .. presetName);
			local ok, problem = pcall(function()
				local holder = CreateAHuman("Soldier Light", "Coalition.rte");
				holder.Team = 0;
				holder.Pos = ground(middle - 40 - slot * 18) + Vector(0, -90);
				holder:AddInventoryItem(itemMakers[className](presetName, name));
				holder.AIMode = Actor.AIMODE_SENTRY;
				MovableMan:AddActor(holder);
			end);
			if not ok then
				print("MODSWEEP PROBLEM making " .. className .. " '" .. presetName .. "' of " .. name .. ": " .. tostring(problem));
			end
			end);
			items = items + 1;
		end
	end
	-- Something to shoot at.
	for i = 1, 4 do
		local enemy = CreateAHuman("Dummy", "Dummy.rte");
		enemy.Team = 1;
		enemy.Pos = ground(middle + 120 + i * 40) + Vector(0, -30);
		enemy.AIMode = Actor.AIMODE_SENTRY;
		MovableMan:AddActor(enemy);
	end
	print("MODSWEEP " .. name .. ": " .. units .. " units and craft, " .. items .. " items");
	return queue;
end

function ModSweepScript:UpdateScript()
	if not self.started then
		if self.timer:IsPastSimMS(1500) then
			self.started = true;
			self.timer:Reset();
			self.phase = "next";
		end
		return;
	end
	if self.phase == "next" then
		self:ClearAway();
		self.moduleIndex = self.moduleIndex + 1;
		while self.moduleIndex < PresetMan:GetTotalModuleCount() do
			local module = PresetMan:GetDataModule(self.moduleIndex);
			-- The player's own scenes and saves (UserScenes.rte and the like) aren't mods.
			if module and module.FileName ~= "RenderTest.rte" and not string.find(module.FileName, "^User") then
				break;
			end
			self.moduleIndex = self.moduleIndex + 1;
		end
		if self.moduleIndex >= PresetMan:GetTotalModuleCount() then
			print("MODSWEEP DONE");
			self.phase = "done";
			return;
		end
		self.phase = "settle";
		self.timer:Reset();
	elseif self.phase == "settle" and self.timer:IsPastSimMS(700) then
		local module = PresetMan:GetDataModule(self.moduleIndex);
		print("MODSWEEP BEGIN " .. module.FileName);
		self.queue = self:Populate(module);
		self.phase = "populate";
		self.timer:Reset();
	elseif self.phase == "populate" and self.timer:IsPastSimMS(100) then
		self.timer:Reset();
		if #self.queue > 0 then
			table.remove(self.queue, 1)();
		else
			self.phase = "fight";
		end
	elseif self.phase == "fight" and self.timer:IsPastSimMS(6000) then
		print("MODSWEEP END " .. PresetMan:GetDataModule(self.moduleIndex).FileName);
		self.phase = "next";
	end
end
