-- Mod Studio's test game (Tools/ModStudio/Test In Game.bat): a plain game on a map, no sandbox. The game puts you in the unit Studio sends
-- (Source/Managers/ModStudioPreview.cpp) and swaps it for each new one; this only points the view at the map and keeps the game going.

function ModStudioTestActivity:StartActivity(isNewGame)
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if self:PlayerActive(player) and self:PlayerHuman(player) then
			-- Look at the ground in the middle of the map: the first unit from Studio comes down there.
			local look = Vector(SceneMan.SceneWidth * 0.5, 0);
			while look.Y < SceneMan.SceneHeight - 1 and SceneMan:GetTerrMatter(look.X, look.Y) == rte.airID do
				look.Y = look.Y + 4;
			end
			look.Y = look.Y - 40;
			self:SetViewState(Activity.OBSERVE, player);
			self:SetObservationTarget(look, player);
			CameraMan:SetScrollTarget(look, 1, self:ScreenOfPlayer(player));
		end
	end
	-- The map's doors open for everyone.
	for actor in MovableMan.AddedActors do
		if IsADoor(actor) then
			actor.Team = Activity.NOTEAM;
		end
	end
	for actor in MovableMan.Actors do
		if IsADoor(actor) then
			actor.Team = Activity.NOTEAM;
		end
	end
end

function ModStudioTestActivity:UpdateActivity()
	-- Nothing ends it: losing the unit just waits for the next one from Studio.
end

function ModStudioTestActivity:OnSave()
end
