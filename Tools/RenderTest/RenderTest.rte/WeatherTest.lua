function WeatherTestScript:StartScript()
	self.timer = Timer();
	self.logTimer = Timer();
	self.stage = 0;
	self.log = io.open("weathertest.log", "w");
end

function WeatherTestScript:Log(text)
	if self.log then
		self.log:write(string.format("%6.1f %s\n", self.timer.ElapsedSimTimeMS / 1000, text));
		self.log:flush();
	end
end

local function GroundBelow(pos)
	for i = 1, 300 do
		if SceneMan:GetTerrMatter(pos.X, pos.Y + 1) ~= 0 then
			break;
		end
		pos.Y = pos.Y + 1;
	end
	return pos;
end

function WeatherTestScript:UpdateScript()
	-- Starts a grass fire left of player 1's soldier and sends a walker off to the right, logging how big the fire is and how far the walker got.
	local player = ActivityMan:GetActivity():GetControlledActor(0);
	if not player then
		return;
	end
	local t = self.timer.ElapsedSimTimeMS;
	if self.stage == 0 and t > 12000 then
		self.stage = 1;
		self.origin = Vector(player.Pos.X, player.Pos.Y);
		local bomb = CreateTDExplosive("Napalm Bomb", "Base.rte");
		bomb.Pos = GroundBelow(self.origin + Vector(-170, -60)) + Vector(0, -4);
		MovableMan:AddItem(bomb);
		bomb:GibThis();
		local walker = CreateAHuman("Soldier Light", "Coalition.rte");
		walker.Pos = self.origin + Vector(20, -10);
		walker.Team = player.Team;
		walker:AddAISceneWaypoint(GroundBelow(self.origin + Vector(400, -60)));
		walker.AIMode = Actor.AIMODE_GOTO;
		MovableMan:AddActor(walker);
		self.walker = walker;
		self:Log("start");
	end
	if self.stage >= 1 and self.logTimer:IsPastSimMS(250) then
		self.logTimer:Reset();
		local walked = (self.walker and MovableMan:ValidMO(self.walker)) and string.format("%.0f", self.walker.Pos.X - self.origin.X) or "-";
		self:Log(string.format("burning %d  walker %s  player x %.1f", SceneMan:GetBurningPixelCount(), walked, player.Pos.X));
	end
end

function WeatherTestScript:EndScript()
	if self.log then
		self.log:close();
		self.log = nil;
	end
end
