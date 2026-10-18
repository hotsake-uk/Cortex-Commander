-- Arena: a short versus match. Everyone picks a unit and spawns on their side's part of the map; weapons and medikits wait at spots across
-- the map for whoever runs over them; the dead respawn after a moment, and the first side to the kill target wins. Every random choice goes
-- through math.random (the game's seeded numbers), so a match plays the same on every machine given the same inputs.

local c_UnitLoadouts = {"Infantry Light", "Infantry Heavy", "Infantry CQB", "Infantry Sniper", "Infantry Grenadier", "Infantry Engineer", "Infantry Medic"};
local c_FallbackTech = "Coalition.rte";
local c_PickTimeoutMS = 10000;
local c_GrabRadius = 30;
local c_BotRethinkMS = 3000;
ARENA_DEBUG = false; -- Prints the fighters every 15 s, and the map, to the console.

local function ShortName(loadoutName)
	return (loadoutName:gsub("^Infantry ", ""));
end

function Arena:StartActivity(isNewGame)
	self.BuyMenuEnabled = false;

	self.killTarget = math.floor(self:GetScenarioSettingOr("KillTarget", 10));
	self.respawnMS = self:GetScenarioSettingOr("RespawnDelay", 3) * 1000;
	self.refillMS = self:GetScenarioSettingOr("PickupRefill", 20) * 1000;
	local botSetting = self:GetScenarioSetting("Bots");

	-- The sides: every team with a player on it, and the CPU's.
	self.sides = {};
	self.sideOfTeam = {};
	for team = Activity.TEAM_1, Activity.MAXTEAMCOUNT - 1 do
		if self:PlayersInTeamCount(team) > 0 or team == self.CPUTeam then
			table.insert(self.sides, team);
			self.sideOfTeam[team] = #self.sides;
		end
	end
	if #self.sides < 2 then
		-- One side on its own: give it the CPU to fight.
		local other = self.sides[1] == Activity.TEAM_2 and Activity.TEAM_1 or Activity.TEAM_2;
		self.CPUTeam = other;
		table.insert(self.sides, other);
		self.sideOfTeam[other] = #self.sides;
	end
	table.sort(self.sides);
	for i, team in ipairs(self.sides) do
		self.sideOfTeam[team] = i;
	end

	self.score = {};
	self.techOfTeam = {};
	self.loadoutsOfTeam = {};
	for _, team in ipairs(self.sides) do
		self.score[team] = 0;
		self:SetTeamFunds(0, team);
		local techName = self:GetTeamTech(team);
		local techID = PresetMan:GetModuleID(techName);
		if techID == -1 then
			techID = PresetMan:GetModuleID(c_FallbackTech);
		end
		self.techOfTeam[team] = techID;
		self.loadoutsOfTeam[team] = {};
		for _, loadoutName in ipairs(c_UnitLoadouts) do
			-- (Kept only if the tech has it: made once here to see, and dropped.)
			if PresetMan:GetLoadout(loadoutName, techID, false) then
				table.insert(self.loadoutsOfTeam[team], loadoutName);
			end
		end
		if #self.loadoutsOfTeam[team] == 0 then
			self.techOfTeam[team] = PresetMan:GetModuleID(c_FallbackTech);
			self.loadoutsOfTeam[team] = {unpack(c_UnitLoadouts)};
		end
	end

	-- The fighters: each human player, and bots to make the sides even (or as many as the Bots setting says for the CPU's side).
	self.fighters = {};
	local mostHumans = 1;
	for _, team in ipairs(self.sides) do
		mostHumans = math.max(mostHumans, self:HumanPlayersOnTeam(team));
	end
	for _, team in ipairs(self.sides) do
		for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
			if self:PlayerActive(player) and self:PlayerHuman(player) and self:GetTeamOfPlayer(player) == team then
				table.insert(self.fighters, {Team = team, Player = player, Pick = 2, Timer = Timer(), State = "Picking"});
			end
		end
		local bots = mostHumans - self:HumanPlayersOnTeam(team);
		if team == self.CPUTeam and botSetting >= 0 then
			bots = math.floor(botSetting);
		end
		for _ = 1, bots do
			table.insert(self.fighters, {Team = team, Timer = Timer(), State = "Waiting", RethinkTimer = Timer()});
		end
	end

	-- Clear the map: no doors, no units left from the scene.
	MovableMan:OpenAllDoors(true, -1);
	for actor in MovableMan.AddedActors do
		if actor.ClassName == "ADoor" then
			actor.ToSettle = true;
			actor:GibThis();
		else
			actor.ToDelete = true;
		end
	end
	if self:GetFogOfWarEnabled() then
		for _, team in ipairs(self.sides) do
			SceneMan:RevealUnseenBox(0, 0, SceneMan.SceneWidth, SceneMan.SceneHeight, team);
		end
	end

	self:MakePickupSpots();
	self.matchTimer = Timer();
	if ARENA_DEBUG then
		ConsoleMan:PrintString("Arena debug: scene " .. SceneMan.SceneWidth .. " wide, " .. #self.sides .. " sides, " .. #self.spots .. " pickup spots");
	end
	MusicMan:PlayDynamicSong("Generic Battle Music");
end

function Arena:HumanPlayersOnTeam(team)
	local count = 0;
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if self:PlayerActive(player) and self:PlayerHuman(player) and self:GetTeamOfPlayer(player) == team then
			count = count + 1;
		end
	end
	return count;
end

-- What the spots hand out: heavy and explosive weapons and grenades from the sides' techs, and medikits.
function Arena:MakePickupPool()
	local pool = {};
	local seen = {};
	for _, team in ipairs(self.sides) do
		local techID = self.techOfTeam[team];
		if not seen[techID] then
			seen[techID] = true;
			local module = PresetMan:GetDataModule(techID);
			if module then
				for entity in module.Presets do
					if entity.ClassName == "HDFirearm" and ToMOSRotating(entity).Buyable and (ToMOSRotating(entity):HasObjectInGroup("Weapons - Heavy") or ToMOSRotating(entity):HasObjectInGroup("Weapons - Explosive")) then
						table.insert(pool, {Kind = "HDFirearm", Name = entity:GetModuleAndPresetName(), Label = entity.PresetName});
					elseif entity.ClassName == "TDExplosive" and ToMOSRotating(entity).Buyable and ToMOSRotating(entity):HasObjectInGroup("Bombs - Grenades") then
						table.insert(pool, {Kind = "TDExplosive", Name = entity:GetModuleAndPresetName(), Label = entity.PresetName});
					end
				end
			end
		end
	end
	if #pool == 0 then
		table.insert(pool, {Kind = "HDFirearm", Name = "Coalition.rte/Gatling Gun", Label = "Gatling Gun"});
		table.insert(pool, {Kind = "TDExplosive", Name = "Coalition.rte/Frag Grenade", Label = "Frag Grenade"});
	end
	-- About one spot in four is a medikit.
	local medikits = math.max(1, math.floor(#pool / 3));
	for _ = 1, medikits do
		table.insert(pool, {Kind = "Medikit", Label = "Medikit"});
	end
	return pool;
end

function Arena:MakePickupSpots()
	self.pickupPool = self:MakePickupPool();
	self.spots = {};
	local width = SceneMan.SceneWidth;
	local count = math.max(4, math.min(12, math.floor(width / 350)));
	for i = 1, count do
		local x = width * (i - 0.5) / count + (math.random() - 0.5) * width / count * 0.5;
		local ground = SceneMan:MovePointToGround(Vector(x, 0), 12, 2);
		if ground.Y < SceneMan.SceneHeight - 60 then
			table.insert(self.spots, {Pos = ground, Item = nil, Timer = Timer()});
		end
	end
	for _, spot in ipairs(self.spots) do
		self:RefillSpot(spot);
	end
end

function Arena:RefillSpot(spot)
	spot.Item = self.pickupPool[math.random(#self.pickupPool)];
end

-- Hands a spot's item to the unit that ran over it.
function Arena:GiveItem(unit, item)
	if item.Kind == "Medikit" then
		unit.Health = unit.MaxHealth;
		unit:RemoveWounds(unit.WoundCount);
		unit:FlashWhite(100);
		return;
	end
	local device = item.Kind == "TDExplosive" and CreateTDExplosive(item.Name) or CreateHDFirearm(item.Name);
	if device then
		unit:AddInventoryItem(device);
		if IsAHuman(unit) and item.Kind == "HDFirearm" then
			ToAHuman(unit):EquipNamedDevice(item.Label, true);
		end
	end
end

function Arena:SpawnPoint(team)
	local side = self.sideOfTeam[team];
	local width = SceneMan.SceneWidth;
	local from = width * (side - 1) / #self.sides;
	local to = width * side / #self.sides;
	local fallback;
	for _ = 1, 20 do
		local x = from + (to - from) * (0.15 + 0.7 * math.random());
		local ground = SceneMan:MovePointToGround(Vector(x, 0), 20, 3);
		if ground.Y < SceneMan.SceneHeight - 60 then
			return ground;
		end
		fallback = fallback or ground;
	end
	return fallback;
end

function Arena:Spawn(fighter, loadoutName)
	local team = fighter.Team;
	local unit = PresetMan:GetLoadout(loadoutName, self.techOfTeam[team], false);
	if not unit then
		return;
	end
	unit.Pos = self:SpawnPoint(team);
	unit.Team = team;
	-- Facing the middle of the map.
	unit.HFlipped = unit.Pos.X > SceneMan.SceneWidth * 0.5;
	MovableMan:AddActor(unit);
	fighter.Unit = unit;
	fighter.State = "Fighting";
	if fighter.Player then
		self:SwitchToActor(unit, fighter.Player, team);
		self:SetObservationTarget(unit.Pos, fighter.Player);
	else
		unit.AIMode = Actor.AIMODE_SENTRY;
		fighter.RethinkTimer:Reset();
		fighter.RethinkTimer.ElapsedSimTimeMS = c_BotRethinkMS;
	end
end

-- A bot goes for the nearest enemy, every so often. One that hasn't got anywhere for a while (a ledge it can't find its way up to, say) goes
-- for a pickup spot instead for a bit, and then tries again.
function Arena:SteerBot(fighter)
	if not fighter.RethinkTimer:IsPastSimMS(c_BotRethinkMS) then
		return;
	end
	fighter.RethinkTimer:Reset();
	local unit = fighter.Unit;
	if fighter.LastPos and SceneMan:ShortestDistance(unit.Pos, fighter.LastPos, SceneMan.SceneWrapsX):MagnitudeIsLessThan(30) then
		fighter.StuckCount = (fighter.StuckCount or 0) + 1;
	else
		fighter.StuckCount = 0;
	end
	fighter.LastPos = Vector(unit.Pos.X, unit.Pos.Y);

	if fighter.WanderUntil and fighter.WanderUntil > self.matchTimer.ElapsedSimTimeMS then
		return;
	end
	local nearest, nearestDistance;
	for _, other in ipairs(self.fighters) do
		if other.Team ~= fighter.Team and other.State == "Fighting" and MovableMan:IsActor(other.Unit) then
			local distance = SceneMan:ShortestDistance(unit.Pos, other.Unit.Pos, SceneMan.SceneWrapsX).Magnitude;
			if not nearestDistance or distance < nearestDistance then
				nearest, nearestDistance = other.Unit, distance;
			end
		end
	end
	unit:ClearAIWaypoints();
	if fighter.StuckCount >= 4 and #self.spots > 0 and (not nearestDistance or nearestDistance > 200) then
		fighter.StuckCount = 0;
		fighter.WanderUntil = self.matchTimer.ElapsedSimTimeMS + 10000;
		unit:AddAISceneWaypoint(self.spots[math.random(#self.spots)].Pos);
		unit.AIMode = Actor.AIMODE_GOTO;
	elseif nearest then
		unit:AddAIMOWaypoint(nearest);
		unit.AIMode = Actor.AIMODE_GOTO;
	else
		unit.AIMode = Actor.AIMODE_SENTRY;
	end
end

function Arena:UnitAlive(unit)
	return unit and MovableMan:IsActor(unit) and unit.Status < Actor.DYING;
end

function Arena:OtherSidesScore(team)
	for _, other in ipairs(self.sides) do
		if other ~= team then
			self.score[other] = self.score[other] + 1;
		end
	end
end

function Arena:ScoreLine(team)
	local theirs = 0;
	for _, other in ipairs(self.sides) do
		if other ~= team then
			theirs = math.max(theirs, self.score[other]);
		end
	end
	return "You " .. self.score[team] .. "  -  " .. theirs .. " Them      First to " .. self.killTarget;
end

function Arena:UpdateFighter(fighter)
	local team = fighter.Team;
	if fighter.State == "Fighting" then
		if not self:UnitAlive(fighter.Unit) then
			self:OtherSidesScore(team);
			ConsoleMan:PrintString("Arena: a unit of team " .. (team + 1) .. " died at " .. math.floor(self.matchTimer.ElapsedSimTimeMS / 1000) .. " s; " .. self:ScoreLine(team));
			fighter.Unit = nil;
			fighter.State = "Waiting";
			fighter.Timer:Reset();
		elseif not fighter.Player then
			self:SteerBot(fighter);
		end
	elseif fighter.State == "Waiting" then
		if fighter.Timer:IsPastSimMS(self.respawnMS) then
			if fighter.Player then
				fighter.State = "Picking";
				fighter.Timer:Reset();
			else
				local loadouts = self.loadoutsOfTeam[team];
				self:Spawn(fighter, loadouts[math.random(#loadouts)]);
			end
		end
	elseif fighter.State == "Picking" then
		local loadouts = self.loadoutsOfTeam[team];
		local controller = self:GetPlayerController(fighter.Player);
		if controller:IsState(Controller.PRESS_LEFT) then
			fighter.Pick = (fighter.Pick - 2) % #loadouts + 1;
		elseif controller:IsState(Controller.PRESS_RIGHT) then
			fighter.Pick = fighter.Pick % #loadouts + 1;
		end
		fighter.Pick = math.max(1, math.min(fighter.Pick, #loadouts));
		local secondsLeft = math.max(0, math.ceil((c_PickTimeoutMS - fighter.Timer.ElapsedSimTimeMS) / 1000));
		FrameMan:SetScreenText("Pick your unit:   < " .. ShortName(loadouts[fighter.Pick]) .. " >\nLeft and right to change, fire to deploy (" .. secondsLeft .. ")\n\n" .. self:ScoreLine(team), self:ScreenOfPlayer(fighter.Player), 0, 1, false);
		if controller:IsState(Controller.WEAPON_FIRE) or fighter.Timer:IsPastSimMS(c_PickTimeoutMS) then
			self:Spawn(fighter, loadouts[fighter.Pick]);
		end
	end
end

function Arena:UpdatePickups()
	for _, spot in ipairs(self.spots) do
		if spot.Item then
			for _, fighter in ipairs(self.fighters) do
				if fighter.State == "Fighting" and self:UnitAlive(fighter.Unit) and SceneMan:ShortestDistance(fighter.Unit.Pos, spot.Pos, SceneMan.SceneWrapsX):MagnitudeIsLessThan(c_GrabRadius) then
					self:GiveItem(fighter.Unit, spot.Item);
					spot.Item = nil;
					spot.Timer:Reset();
					break;
				end
			end
		elseif spot.Timer:IsPastSimMS(self.refillMS) then
			self:RefillSpot(spot);
		end
	end
end

function Arena:DrawPickups()
	for _, spot in ipairs(self.spots) do
		if spot.Item then
			PrimitiveMan:DrawCirclePrimitive(spot.Pos, 10, spot.Item.Kind == "Medikit" and 147 or 122);
			PrimitiveMan:DrawCirclePrimitive(spot.Pos, 12, spot.Item.Kind == "Medikit" and 147 or 122);
			PrimitiveMan:DrawTextPrimitive(spot.Pos + Vector(0, -26), spot.Item.Label, true, 1);
		end
	end
end

function Arena:UpdateActivity()
	if self.ActivityState == Activity.OVER then
		return;
	end
	for _, fighter in ipairs(self.fighters) do
		self:UpdateFighter(fighter);
	end
	self:UpdatePickups();
	self:DrawPickups();
	if ARENA_DEBUG and (not self.debugTimer or self.debugTimer:IsPastSimMS(15000)) then
		self.debugTimer = Timer();
		for i, fighter in ipairs(self.fighters) do
			local where = (fighter.Unit and MovableMan:IsActor(fighter.Unit)) and (math.floor(fighter.Unit.Pos.X) .. "," .. math.floor(fighter.Unit.Pos.Y) .. " mode " .. fighter.Unit.AIMode .. " health " .. math.floor(fighter.Unit.Health)) or "no unit";
			ConsoleMan:PrintString("Arena debug " .. math.floor(self.matchTimer.ElapsedSimTimeMS / 1000) .. " s: fighter " .. i .. " team " .. (fighter.Team + 1) .. " " .. fighter.State .. " " .. where);
		end
	end

	for _, fighter in ipairs(self.fighters) do
		if fighter.Player and fighter.State ~= "Picking" then
			FrameMan:SetScreenText(self:ScoreLine(fighter.Team), self:ScreenOfPlayer(fighter.Player), 0, 1, false);
		end
	end

	for _, team in ipairs(self.sides) do
		if self.score[team] >= self.killTarget then
			ConsoleMan:PrintString("Arena: team " .. (team + 1) .. " wins.");
			self.WinnerTeam = team;
			ActivityMan:EndActivity();
			return;
		end
	end
end

function Arena:EndActivity()
	if not self:IsPaused() then
		MusicMan:PlayDynamicSong("Generic Victory Music", "Default", true);
		MusicMan:PlayDynamicSong("Generic Ambient Music");
	end
end
