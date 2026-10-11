package.loaded.Constants = nil; require("Constants");

-- Battle Command, the RTS game mode. It is built on the Sandbox game mode (Source/Managers/Sandbox.cpp, CommanderBar.cpp): you watch from
-- a free camera and command one team of a battle mode's game with the Commander Toolbar; the battle itself is run by the battle modes.

function BattleCommandActivity:StartActivity(isNewGame)
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if self:PlayerActive(player) and self:PlayerHuman(player) then
			self:SetViewState(Activity.OBSERVE, player);
			-- Start looking at the ground in the middle of the map.
			local look = Vector(SceneMan.SceneWidth * 0.5, 0);
			while look.Y < SceneMan.SceneHeight - 1 and SceneMan:GetTerrMatter(look.X, look.Y) == rte.airID do
				look.Y = look.Y + 4;
			end
			look.Y = look.Y - FrameMan.PlayerScreenHeight * 0.15;
			self:SetObservationTarget(look, player);
			CameraMan:SetScrollTarget(look, 1, self:ScreenOfPlayer(player));
		end
	end
	-- Doors already in the scene open for everyone. (In the world already as well as just added: a saved game loaded mid-play has its doors
	-- in the world, and they came back with the scene's teams. Not the doors placed with the Structure tool, which belong to their side.)
	local function Neutral(actor)
		if IsADoor(actor) and not actor:NumberValueExists("SandboxPlaced") then
			actor.Team = Activity.NOTEAM;
		end
	end
	for actor in MovableMan.AddedActors do
		Neutral(actor);
	end
	for actor in MovableMan.Actors do
		Neutral(actor);
	end
end

function BattleCommandActivity:UpdateActivity()
	-- The battle mode decides who wins and says so; the game itself only ends when you leave it.
end

function BattleCommandActivity:OnSave()
	-- Nothing extra to save, but saving needs this.
end
