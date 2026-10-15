package.loaded.Constants = nil; require("Constants");

-- A campaign battle fought for real (Source/Managers/CampaignScreen.cpp starts it): an attack on a point, on the Maginot Mission map.
-- Every campaign soldier on either side is one actor, made from their squad's Loadout. The defenders hold the bunker; the attackers come
-- down by dropship in waves outside its West entrance, with some walking in through the East tunnel, and hunt the defenders down.
-- The defenders' brain sits in the bunker's command room: the attackers win by killing it, or by bringing down every defender. The
-- defenders win when no attackers are left. The campaign reads back who won and who's still standing (CampaignSurvivors).
--
-- From the campaign, as saved strings and numbers on the activity:
--   CampaignAttackers, CampaignDefenders: "loadout|module|soldier id|health;..." for each soldier.
--   CampaignLocals: how many locals defend the point, CampaignLocalsTech: the module they come from.
--   CampaignPlayerAttacks: 1 when the player's side attacks.
-- Back to the campaign:
--   CampaignSurvivors: "soldier id|health;..." for each campaign soldier still standing (kept up to date, so a retreat counts too).
--   CampaignLocalsLeft: the locals still standing. CampaignStarted: 1 once the battle has begun.

function CampaignBattle:StartActivity(isNewGame)
	self.attackerTeam = Activity.TEAM_1;
	self.defenderTeam = Activity.TEAM_2;
	self.attackLZ = SceneMan.Scene:GetArea("LZ Team 2");
	self.sideLZ = SceneMan.Scene:GetArea("Enemy Sneak Spawn 2");
	self.bunker = SceneMan.Scene:GetArea("Maginot Bunker");

	-- The bunker's rooms and corridors: its boxes below ground (it also has a big one over the sky, for the fog).
	self.rooms = {};
	if self.bunker then
		for box in self.bunker.Boxes do
			if box.Corner.Y > 800 and box.Height < 300 and box.Width > 40 then
				table.insert(self.rooms, Box(box));
			end
		end
	end

	self.attackers = self:ParseSoldiers(self:LoadString("CampaignAttackers"));
	self.defenders = self:ParseSoldiers(self:LoadString("CampaignDefenders"));
	self.localsToPlace = math.floor(self:LoadNumber("CampaignLocals"));
	self.localsTech = self:LoadString("CampaignLocalsTech");
	self.playerAttacks = self:LoadNumber("CampaignPlayerAttacks") > 0.5;
	self.localsPlaced = 0;

	-- What's become of each campaign soldier: "reserve" (not on the field yet), "flying" (in a dropship on its way), "field" or "dead".
	self.state = {};
	self.launched = {};
	self.seen = {};
	self.health = {};
	for _, soldier in ipairs(self.attackers) do
		self.state[soldier.Id] = "reserve";
		self.health[soldier.Id] = soldier.Health;
	end
	for _, soldier in ipairs(self.defenders) do
		self.state[soldier.Id] = "reserve";
		self.health[soldier.Id] = soldier.Health;
	end

	self.maxAttackersOnField = 12;
	self.maxDefendersOnField = 20;
	self.attackTimer = Timer();
	self.attackInterval = 9000;
	self.firstWave = true;
	self.defendTimer = Timer();
	self.checkTimer = Timer();
	self.retargetTimer = Timer();
	self.textTimer = Timer();
	self.started = Timer();

	for team = Activity.TEAM_1, Activity.MAXTEAMCOUNT - 1 do
		if self:TeamActive(team) then
			self:SetTeamFunds(0, team);
		end
	end

	-- The defenders start in the bunker, holding their ground, the locals with them, guarding their brain.
	self:PlaceBrain();
	self:PlaceDefenders();

	-- Players start looking at where the attack comes from (the attackers) or at the bunker (the defenders).
	local look = self.playerAttacks and (self.attackLZ and self.attackLZ:GetCenterPoint() or Vector(200, 800)) or self:RoomSpot();
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if self:PlayerActive(player) and self:PlayerHuman(player) then
			self:SetObservationTarget(look, player);
			CameraMan:SetScrollTarget(look, 1, self:ScreenOfPlayer(player));
		end
	end
	MusicMan:PlayDynamicSong("Generic Battle Music");
	self:WriteResult();
end

function CampaignBattle:OnSave()
	-- A battle is fought in one go, from the campaign: nothing to carry over.
end

function CampaignBattle:ParseSoldiers(text)
	local soldiers = {};
	if not text then
		return soldiers;
	end
	for entry in string.gmatch(text, "([^;]+)") do
		local loadout, module, id, health = string.match(entry, "^([^|]*)|([^|]*)|([^|]*)|([^|]*)$");
		if id then
			table.insert(soldiers, { Loadout = loadout, Module = module, Id = tonumber(id), Health = tonumber(health) or 1 });
		end
	end
	return soldiers;
end

-- One campaign soldier as an actor: their Loadout, or someone light and armed from their faction when it has none.
function CampaignBattle:MakeSoldier(soldier, team)
	local actor;
	if soldier.Loadout ~= "" then
		actor = PresetMan:GetLoadout(soldier.Loadout, soldier.Module, false);
	end
	if not actor then
		actor = RandomAHuman("Actors - Light", soldier.Module);
		if actor then
			actor:AddInventoryItem(RandomHDFirearm("Weapons - Primary", soldier.Module));
		end
	end
	if not actor then
		actor = CreateAHuman("Soldier Light", "Coalition.rte");
		actor:AddInventoryItem(CreateHDFirearm("Assault Rifle", "Coalition.rte"));
	end
	actor.Team = team;
	actor.Health = math.max(1, actor.MaxHealth * math.max(0.1, math.min(1, soldier.Health)));
	actor:SetNumberValue("CampaignSoldier", soldier.Id);
	return actor;
end

function CampaignBattle:MakeLocal()
	local actor = RandomAHuman("Actors - Light", self.localsTech);
	if actor then
		actor:AddInventoryItem(RandomHDFirearm("Weapons - Primary", self.localsTech));
	else
		actor = CreateAHuman("Soldier Light", "Coalition.rte");
		actor:AddInventoryItem(CreateHDFirearm("Assault Rifle", "Coalition.rte"));
	end
	actor.Team = self.defenderTeam;
	actor:SetNumberValue("CampaignLocal", 1);
	return actor;
end

-- Somewhere open in the bunker to stand.
function CampaignBattle:RoomSpot()
	for _ = 1, 30 do
		local room = self.rooms[math.random(#self.rooms)];
		if not room then
			break;
		end
		local spot = Vector(room.Corner.X + math.random() * room.Width, room.Corner.Y + math.random() * room.Height);
		if SceneMan:GetTerrMatter(spot.X, spot.Y) == rte.airID and SceneMan:GetTerrMatter(spot.X, spot.Y - 30) == rte.airID then
			return spot;
		end
	end
	return Vector(2200, 1180);
end

-- The defenders' brain, in the bunker's command room (where Maginot Defence puts it).
function CampaignBattle:PlaceBrain()
	local brain = CreateActor("Brain Case", "Base.rte");
	brain.Team = self.defenderTeam;
	brain.Pos = Vector(2328, 1170);
	MovableMan:AddActor(brain);
	self.brain = brain;
	self.brainPos = Vector(brain.Pos.X, brain.Pos.Y);
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if self:PlayerActive(player) and self:GetTeamOfPlayer(player) == self.defenderTeam then
			self:SetPlayerBrain(brain, player);
		end
	end
end

function CampaignBattle:BrainAlive()
	return self.brain ~= nil and MovableMan:IsActor(self.brain) and not self.brain:IsDead();
end

function CampaignBattle:Count(team)
	local count = 0;
	for actor in MovableMan.Actors do
		if actor.Team == team and (actor:NumberValueExists("CampaignSoldier") or actor:NumberValueExists("CampaignLocal")) and not actor:IsDead() then
			count = count + 1;
		end
	end
	return count;
end

function CampaignBattle:PlaceDefenders()
	local onField = self:Count(self.defenderTeam);
	for _, soldier in ipairs(self.defenders) do
		if onField >= self.maxDefendersOnField then
			break;
		end
		if self.state[soldier.Id] == "reserve" then
			local actor = self:MakeSoldier(soldier, self.defenderTeam);
			actor.Pos = self:RoomSpot();
			actor.AIMode = Actor.AIMODE_SENTRY;
			MovableMan:AddActor(actor);
			self.state[soldier.Id] = "field";
			self.launched[soldier.Id] = Timer();
			onField = onField + 1;
		end
	end
	while self.localsPlaced < self.localsToPlace and onField < self.maxDefendersOnField do
		local actor = self:MakeLocal();
		actor.Pos = self:RoomSpot();
		actor.AIMode = Actor.AIMODE_SENTRY;
		MovableMan:AddActor(actor);
		self.localsPlaced = self.localsPlaced + 1;
		onField = onField + 1;
	end
end

-- The next wave of attackers: a dropship over the West entrance, and now and then one on foot through the East tunnel.
function CampaignBattle:SendAttackers()
	local onField = self:Count(self.attackerTeam);
	for _, soldier in ipairs(self.attackers) do
		if self.state[soldier.Id] == "flying" then
			onField = onField + 1;
		end
	end
	if onField >= self.maxAttackersOnField then
		return;
	end
	local waiting = {};
	for _, soldier in ipairs(self.attackers) do
		if self.state[soldier.Id] == "reserve" then
			table.insert(waiting, soldier);
		end
	end
	if #waiting == 0 then
		return;
	end
	local tech = waiting[1].Module;
	local craft = RandomACDropShip("Craft", tech);
	if not craft then
		craft = CreateACDropShip("Dropship MK1", "Base.rte");
	end
	local room = math.min(craft.MaxPassengers > 0 and craft.MaxPassengers or 4, self.maxAttackersOnField - onField, self.firstWave and 6 or 4);
	local aboard = 0;
	for _, soldier in ipairs(waiting) do
		if aboard >= room then
			break;
		end
		-- Now and then one goes the long way round, on foot through the East tunnel.
		if aboard > 0 and self.sideLZ and math.random() < 0.15 then
			local walker = self:MakeSoldier(soldier, self.attackerTeam);
			for box in self.sideLZ.Boxes do
				walker.Pos = Vector(box.Corner.X + box.Width, box.Center.Y);
				break;
			end
			self:SendToFight(walker);
			MovableMan:AddActor(walker);
			self.state[soldier.Id] = "field";
		else
			craft:AddInventoryItem(self:MakeSoldier(soldier, self.attackerTeam));
			self.state[soldier.Id] = "flying";
			aboard = aboard + 1;
		end
		self.launched[soldier.Id] = Timer();
	end
	if aboard == 0 then
		DeleteEntity(craft);
		return;
	end
	craft.Pos = Vector(self.attackLZ and self.attackLZ:GetRandomPoint().X or 200, -50);
	craft.Team = self.attackerTeam;
	craft.PlayerControllable = false;
	craft:SetGoldValue(0);
	craft:SetControllerMode(Controller.CIM_AI, -1);
	MovableMan:AddActor(craft);
	self.firstWave = false;
end

-- An attacker sent at the defenders: about half go for the brain, the rest for the nearest defender, or into the bunker.
function CampaignBattle:SendToFight(actor)
	if self:BrainAlive() and math.random() < 0.5 then
		actor:ClearAIWaypoints();
		actor:AddAISceneWaypoint(self.brainPos);
		actor.AIMode = Actor.AIMODE_GOTO;
		return;
	end
	local target;
	local best = math.huge;
	for other in MovableMan.Actors do
		if other.Team == self.defenderTeam and (other:NumberValueExists("CampaignSoldier") or other:NumberValueExists("CampaignLocal")) and not other:IsDead() then
			local distance = SceneMan:ShortestDistance(actor.Pos, other.Pos, false).Magnitude;
			if distance < best then
				best = distance;
				target = other.Pos;
			end
		end
	end
	if not target then
		target = self:RoomSpot();
	end
	actor:ClearAIWaypoints();
	actor:AddAISceneWaypoint(target);
	actor.AIMode = Actor.AIMODE_GOTO;
end

-- Who's still standing, for the campaign: written as it goes, so leaving early counts what's left.
function CampaignBattle:WriteResult()
	local parts = {};
	for id, state in pairs(self.state) do
		if state ~= "dead" then
			table.insert(parts, string.format("%d|%.3f", id, self.health[id] or 1));
		end
	end
	self:SaveString("CampaignSurvivors", table.concat(parts, ";"));
	local locals = self.localsToPlace - self.localsPlaced;
	for actor in MovableMan.Actors do
		if actor:NumberValueExists("CampaignLocal") and not actor:IsDead() then
			locals = locals + 1;
		end
	end
	self:SaveNumber("CampaignLocalsLeft", locals);
	self:SaveNumber("CampaignStarted", 1);
	return locals;
end

-- Brings each soldier's state up to date from the actors in the world.
function CampaignBattle:UpdateSoldiers()
	local present = {};
	for actor in MovableMan.Actors do
		if actor:NumberValueExists("CampaignSoldier") and not actor:IsDead() then
			local id = math.floor(actor:GetNumberValue("CampaignSoldier") + 0.5);
			present[id] = true;
			self.seen[id] = true;
			self.state[id] = "field";
			self.health[id] = math.max(0.05, math.min(1, actor.Health / math.max(1, actor.MaxHealth)));
		end
	end
	for id, state in pairs(self.state) do
		if not present[id] then
			if state == "field" then
				-- Seen on the field and gone: fallen.
				self.state[id] = "dead";
			elseif state == "flying" and self.launched[id] and self.launched[id]:IsPastSimMS(45000) then
				-- Never came down: lost with their dropship.
				self.state[id] = "dead";
			end
		end
	end
end

function CampaignBattle:SideLeft(soldiers)
	local count = 0;
	for _, soldier in ipairs(soldiers) do
		if self.state[soldier.Id] ~= "dead" then
			count = count + 1;
		end
	end
	return count;
end

function CampaignBattle:UpdateActivity()
	if self.ActivityState == Activity.OVER then
		return;
	end

	if self.attackTimer:IsPastSimMS(self.firstWave and 2500 or self.attackInterval) then
		self.attackTimer:Reset();
		self:SendAttackers();
	end
	if self.defendTimer:IsPastSimMS(12000) then
		self.defendTimer:Reset();
		self:PlaceDefenders();
	end

	-- Attackers on the ground with nothing to do go after the nearest defender.
	if self.retargetTimer:IsPastSimMS(4000) then
		self.retargetTimer:Reset();
		for actor in MovableMan.Actors do
			if actor.Team == self.attackerTeam and actor:NumberValueExists("CampaignSoldier") and not actor:IsPlayerControlled() and actor.AIMode ~= Actor.AIMODE_GOTO then
				self:SendToFight(actor);
			end
		end
	end

	-- The player always has someone of theirs to play once they're on the field.
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if self:PlayerActive(player) and self:PlayerHuman(player) then
			local team = self:GetTeamOfPlayer(player);
			local controlled = self:GetControlledActor(player);
			if not controlled or not MovableMan:IsActor(controlled) or controlled.Team ~= team or IsACraft(controlled) then
				for actor in MovableMan.Actors do
					if actor.Team == team and actor:NumberValueExists("CampaignSoldier") and not actor:IsDead() then
						self:SwitchToActor(actor, player, team);
						break;
					end
				end
			end
		end
	end

	if self.checkTimer:IsPastSimMS(1000) then
		self.checkTimer:Reset();
		self:UpdateSoldiers();
		local locals = self:WriteResult();
		local attackersLeft = self:SideLeft(self.attackers);
		local defendersLeft = self:SideLeft(self.defenders) + locals;

		for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
			if self:PlayerActive(player) and self:PlayerHuman(player) then
				local yours = self.playerAttacks and attackersLeft or defendersLeft;
				local theirs = self.playerAttacks and defendersLeft or attackersLeft;
				local waiting = 0;
				for _, soldier in ipairs(self.playerAttacks and self.attackers or self.defenders) do
					if self.state[soldier.Id] == "reserve" or self.state[soldier.Id] == "flying" then
						waiting = waiting + 1;
					end
				end
				local lead = self.playerAttacks and "Take the bunker: kill the brain in its command room, or bring down every defender." or "Hold the bunker: keep your brain alive and bring down every attacker.";
				local reserve = waiting > 0 and string.format(" (%d still to arrive)", waiting) or "";
				FrameMan:SetScreenText(string.format("%s\nYours: %d%s. Enemy: %d.", lead, yours, reserve, theirs), self:ScreenOfPlayer(player), 0, 1100, false);
			end
		end

		if self.started:IsPastSimMS(4000) then
			if not self:BrainAlive() or defendersLeft == 0 then
				self.WinnerTeam = self.attackerTeam;
				ActivityMan:EndActivity();
			elseif attackersLeft == 0 then
				self.WinnerTeam = self.defenderTeam;
				ActivityMan:EndActivity();
			end
		end
	end
end
