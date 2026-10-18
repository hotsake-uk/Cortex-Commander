---@meta
-- Cortex Command's Lua API, written by Tools/ModStudio/tools/make-lua-api.js from the game's bindings.

--- Base class for all Activities, including game modes and editors.
---@class Activity : Entity
---@field ClassName string The C++ class name, like "Activity".
---@field Description string Gets the user-friendly description of this Activity.
---@field InCampaignStage number Shows in which stage of the Campaign this appears.
---@field ActivityState number Gets the current Activity state code. See the ActivityState enumeration for values.
---@field AllowsUserSaving boolean Gets whether or not this Activity allows the player to manually save.
---@field SceneName string Gets the name of the current scene.
---@field PlayerCount number Gets the total number of active players in the current Activity, AI or Human.
---@field HumanCount number Gets the total number of human players in the current Activity.
---@field TeamCount number Gets the total number of teams in the current Activity.
---@field Difficulty number Gets the current difficulty setting.
---@field PLAYER_NONE number One of Activity.Players.
---@field PLAYER_1 number One of Activity.Players.
---@field PLAYER_2 number One of Activity.Players.
---@field PLAYER_3 number One of Activity.Players.
---@field PLAYER_4 number One of Activity.Players.
---@field MAXPLAYERCOUNT number One of Activity.Players.
---@field NOACTIVITY number One of Activity.ActivityState.
---@field NOTSTARTED number One of Activity.ActivityState.
---@field STARTING number One of Activity.ActivityState.
---@field EDITING number One of Activity.ActivityState.
---@field PREGAME number One of Activity.ActivityState.
---@field RUNNING number One of Activity.ActivityState.
---@field INERROR number One of Activity.ActivityState.
---@field OVER number One of Activity.ActivityState.
---@field NOTEAM number One of Activity.Team.
---@field TEAM_1 number One of Activity.Team.
---@field TEAM_2 number One of Activity.Team.
---@field TEAM_3 number One of Activity.Team.
---@field TEAM_4 number One of Activity.Team.
---@field MAXTEAMCOUNT number One of Activity.Team.
---@field NORMAL number One of Activity.ViewState.
---@field OBSERVE number One of Activity.ViewState.
---@field DEATHWATCH number One of Activity.ViewState.
---@field ACTORSELECT number One of Activity.ViewState.
---@field AISENTRYPOINT number One of Activity.ViewState.
---@field AIPATROLPOINTS number One of Activity.ViewState.
---@field AIGOLDDIGPOINT number One of Activity.ViewState.
---@field AIGOTOPOINT number One of Activity.ViewState.
---@field LZSELECT number One of Activity.ViewState.
---@field MINDIFFICULTY number One of Activity.DifficultySetting.
---@field CAKEDIFFICULTY number One of Activity.DifficultySetting.
---@field EASYDIFFICULTY number One of Activity.DifficultySetting.
---@field MEDIUMDIFFICULTY number One of Activity.DifficultySetting.
---@field HARDDIFFICULTY number One of Activity.DifficultySetting.
---@field NUTSDIFFICULTY number One of Activity.DifficultySetting.
---@field MAXDIFFICULTY number One of Activity.DifficultySetting.
---@field MINSKILL number One of Activity.AISkillSetting.
---@field INFERIORSKILL number One of Activity.AISkillSetting.
---@field DEFAULTSKILL number One of Activity.AISkillSetting.
---@field AVERAGESKILL number One of Activity.AISkillSetting.
---@field GOODSKILL number One of Activity.AISkillSetting.
---@field UNFAIRSKILL number One of Activity.AISkillSetting.
Activity = {}

--- Turns off a player if they were active. Should only be done if brain etc are already taken care of and disposed of properly. Will also deactivate the team this player is on, if there's no other active players still on it.
---@param playerToDeactivate number Which player index to deactivate.
---@return boolean
function Activity:DeactivatePlayer(playerToDeactivate) end

--- Indicates whether a specific player is active in the current game.
---@param player number Which player index to check.
---@return boolean
function Activity:PlayerActive(player) end

--- Indicates whether a specific player is human in the current game, ie not an AI player and has a screen etc.
---@param player number Which player index to check.
---@return boolean
function Activity:PlayerHuman(player) end

--- Indicates whether a specific team is active in the current game.
---@param team number Which team index to check.
---@return boolean
function Activity:TeamActive(team) end

--- Gets the current team a specific player belongs to.
---@param player number The player to get the team info on.
---@return number
function Activity:GetTeamOfPlayer(player) end

--- Sets the current team a specific player belongs to.
---@param player number The player to set the team for.
---@param team number The team number to set the player to.
function Activity:SetTeamOfPlayer(player, team) end

--- Gets the current number of players in a specific team.
---@param team number Which team to get the player count for.
---@return number
function Activity:PlayersInTeamCount(team) end

--- Converts a player index into a screen index, and only if that player is human.
---@param player number Which player index to convert.
---@return number
function Activity:ScreenOfPlayer(player) end

--- Gets the current viewing state for a specific player. See the ViewState enumeration for values.
---@param whichPlayer? number Which player to get the view state for.
---@return number
function Activity:GetViewState(whichPlayer) end

--- Sets the current viewing state for a specific player. See the ViewState enumeration for values.
---@param whichViewState number The state to set to.
---@param whichPlayer? number Which player to set the view state for.
function Activity:SetViewState(whichViewState, whichPlayer) end

--- Gets the current Brain actor for a specific player.
---@param player? number Which player to get the brain actor for.
---@return Actor
function Activity:GetPlayerBrain(player) end

--- Sets the current Brain actor for a specific player.
---@param newBrain Actor A pointer to the new brain Actor. Ownership is NOT transferred!
---@param player? number Which team to set the brain actor for.
function Activity:SetPlayerBrain(newBrain, player) end

--- Shows whether a specific player ever had a Brain yet.
---@param player? number Which player to check whether they ever had a Brain.
---@return boolean
function Activity:PlayerHadBrain(player) end

--- Sets to indicate that the player had a Brain at some point. This is to simulate that in automated battle cases.
---@param player number Which player to set whether he had a Brain or not.
---@param hadBrain? boolean Whether he should be flagged as having had a Brain.
function Activity:SetPlayerHadBrain(player, hadBrain) end

--- Sets whether a player's Brain was evacuated during the Activity.
---@param player? number Which player to check whether their Brain was evacuated.
---@param evacuated? boolean Whether it was evacuated yet.
function Activity:SetBrainEvacuated(player, evacuated) end

--- Shows whether a specific player's Brain was evacuated into orbit so far.
---@param player? number Which player to check whether their Brain was evacuated.
---@return boolean
function Activity:BrainWasEvacuated(player) end

--- Shows whether the passed in actor is the Brain of any player.
---@param actor Actor Which Actor to check for player braininess.
---@return boolean
function Activity:IsAssignedBrain(actor) end

--- Shows which player has a specific actor as a Brain, if any.
---@param actor Actor Which Actor to check for player braininess.
---@return number
function Activity:IsBrainOfWhichPlayer(actor) end

--- Shows whether the passed in actor is the Brain of any other player.
---@param actor Actor Which Actor to check for other player braininess.
---@param player number From which player's perspective to check.
---@return boolean
function Activity:IsOtherPlayerBrain(actor, player) end

--- Shows how many human controlled brains are left in this Activity.
---@return number
function Activity:HumanBrainCount() end

--- Shows how many AI controlled brains are left in this Activity.
---@return number
function Activity:AIBrainCount() end

--- Gets the currently controlled actor of a specific player.
---@param player? number Which player to get the controlled actor of.
---@return Actor
function Activity:GetControlledActor(player) end

--- Gets a pointer to the GUI controller of the specified player.
---@param player? number Which player to get the Controller of.
---@return Controller
function Activity:GetPlayerController(player) end

--- Sets the amount of funds a specific team currently has in the Activity.
---@param newFunds number Which team to set the fund count for. 0 = first team.
---@param whichTeam? number
function Activity:SetTeamFunds(newFunds, whichTeam) end

--- Gets the amount of funds a specific team currently has in the Activity.
---@param whichTeam? number Which team to get the fund count from. 0 = first team.
---@return number
function Activity:GetTeamFunds(whichTeam) end

--- Sets AI skill level for specified team.
---@param team number The team to set for.
---@param skill number AI skill level, 1-100.
function Activity:SetTeamAISkill(team, skill) end

--- Returns skill level for specified team. If team is less than 0 or greater than 3 an average of all teams is returned.
---@param team number Team to get skill level for.
---@return number
function Activity:GetTeamAISkill(team) end

--- Changes a team's funds level by a certain amount.
---@param howMuch number The amount with which to change the funds balance.
---@param whichTeam? number Which team to alter the funds of. 0 = first team.
function Activity:ChangeTeamFunds(howMuch, whichTeam) end

--- Checks whether the team funds changed since last time this was called. This also resets the state, so calling this again on the same team will yield false unless it's been changed again.
---@param whichTeam? number Which team's funds to check.
---@return boolean
function Activity:TeamFundsChanged(whichTeam) end

--- Increments the tally of a death of an actor on a specific team.
---@param whichTeam? number Which team to increase the death count of. 0 = first team.
---@param howMany? number The new death count.
---@return number
function Activity:ReportDeath(whichTeam, howMany) end

--- Gets the number of deaths on a specific team so far on the current Activity.
---@param whichTeam? number Which team to get the death tally of. 0 = first team.
---@return number
function Activity:GetTeamDeathCount(whichTeam) end

--- Indicates whether the Activity is currently running or not (not editing, over or paused)
---@return boolean
function Activity:IsRunning() end

--- Indicates whether the Activity is currently paused or not.
---@return boolean
function Activity:IsPaused() end

--- Indicates whether the Activity is over or not.
---@return boolean
function Activity:IsOver() end

--- Forces the ActivityMan to focus player control to a specific Actor for a specific team. Ownership is NOT transferred!
---@param actor Actor Which Actor to switch focus to. The team of this Actor will be set once it is passed in. The actor should have been added to MovableMan already.
---@param player? number Player to force for.
---@param team? number Which team to switch to next actor on.
---@return boolean
function Activity:SwitchToActor(actor, player, team) end

--- Forces the Activity to focus player control to the next Actor of a specific team, other than the current one focused on.
---@param player number Player to force for.
---@param team number Which team to switch to next Actor on.
---@param actorToSkip? Actor An Actor pointer to skip in the sequence.
function Activity:SwitchToNextActor(player, team, actorToSkip) end

--- Forces the Activity to focus player control to the previous Actor of a specific team, other than the current one focused on.
---@param player number Player to force for.
---@param team number Which team to switch to next Actor on.
---@param actorToSkip? Actor An Actor pointer to skip in the sequence.
function Activity:SwitchToPrevActor(player, team, actorToSkip) end

--- Indicates whether a team is player controlled or not.
---@param team number The team number to check.
---@return boolean
function Activity:IsHumanTeam(team) end

--- Resets the message timer for one player.
---@param player? number The player to reset the message timer for.
function Activity:ResetMessageTimer(player) end

--- Saves a string which will be stored in our ini.
---@param key string The key of the saved string.
---@param value string The string to save.
function Activity:SaveString(key, value) end

--- Loads and returns a previously saved string.
---@param key string The key of the string to load.
---@return string
function Activity:LoadString(key) end

--- Saves a number which will be stored in our ini.
---@param key string The key of the saved number.
---@param value number The number to save.
function Activity:SaveNumber(key, value) end

--- Loads and returns a previously saved number.
---@param key string The key of the string to load.
---@return number
function Activity:LoadNumber(key) end

---@param message string
function Activity:SendMessage(message) end

---@param message string
---@param context any
function Activity:SendMessage(message, context) end

--- Base class for all GameActivity:s, including game modes and editors.
---@class GameActivity : Activity
---@field ClassName string The C++ class name, like "GameActivity".
---@field WinnerTeam number Indicates which team is the winner, when the game is over. means the game isn't over yet.
---@field CPUTeam number Gets the current CPU-assisted team, if any (NoTeam) - LEGACY function
---@field DeliveryDelay number Returns current delivery delay
---@field BuyMenuEnabled boolean Returns whether buy menu is enabled in this activity.
---@field CraftsOrbitAtTheEdge boolean Gets whether craft must be considered orbited if they reach the map border on non-wrapped maps.
---@field CursorTimer Timer
---@field GameTimer Timer
---@field GameOverTimer Timer
---@field GameOverPeriod number
---@field ARROWDOWN number One of GameActivity.ObjectiveArrowDir.
---@field ARROWLEFT number One of GameActivity.ObjectiveArrowDir.
---@field ARROWRIGHT number One of GameActivity.ObjectiveArrowDir.
---@field ARROWUP number One of GameActivity.ObjectiveArrowDir.
GameActivity = {}

--- Sets the observation sceneman scroll targets, for when the game is over or a player is in observation mode
---@param newTarget Vector The new absolute position to observe.
---@param player? number Which player to set it for. (default: 0)
function GameActivity:SetObservationTarget(newTarget, player) end

--- Sets the player death sceneman scroll targets, for when a player- controlled actor dies and the view should go to his last position
---@param newTarget Vector The new absolute position to set as death view.
---@param player? number Which player to set it for. (default: 0)
function GameActivity:SetDeathViewTarget(newTarget, player) end

--- Sets the he last selected landing zone.
---@param newZone Vector The new absolute position to set as the last selected landing zone.
---@param player? number Which player to set it for. (default: 0)
function GameActivity:SetLandingZone(newZone, player) end

--- Gets the he last selected landing zone.
---@param player? number Which player to get it for. (default: 0)
---@return Vector
function GameActivity:GetLandingZone(player) end

--- Sets the actor selection cursor position.
---@param newPos Vector The new absolute position to put the cursor at.
---@param player? number Which player to set it for. (default: 0)
function GameActivity:SetActorSelectCursor(newPos, player) end

--- Gets the an in-game GUI Object for a specific player.
---@param which? number Which player to get the GUI for. (default: 0)
---@return BuyMenuGUI
function GameActivity:GetBuyGUI(which) end

--- Gets the an in-game editor GUI Object for a specific player.
---@param which? number Which player to get the GUI for. (default: 0)
---@return SceneEditorGUI
function GameActivity:GetEditorGUI(which) end

--- Locks a player controlled actor to a specific controller mode. Locking the actor will disable player input, including switching actors.
---@param player number Which player to lock the actor for.
---@param lock? boolean Whether to lock or unlock the actor. (Default: true)
---@param lockToMode? number Which controller mode to lock the actor to. (Default: `CIM_AI`)
---@return boolean
function GameActivity:LockControlledActor(player, lock, lockToMode) end

--- Gets the next other team number from the one passed in, if any. If there are more than two teams in this game, then the next one in the series will be returned here.
---@param team number The team not to get.
---@return number
function GameActivity:OtherTeam(team) end

--- Indicates whether there is less than two teams left in this game with a brain in its ranks.
---@return boolean
function GameActivity:OneOrNoneTeamsLeft() end

--- Indicates which single team is left, if any. is returned if there's either more than one team, OR there are no teams at all left with brains in em.
---@return number
function GameActivity:WhichTeamLeft() end

--- Indicates whether there are NO teams left with any brains at all!
---@return boolean
function GameActivity:NoTeamLeft() end

--- Indicates whether there is less than two teams left in this game with a brain in its ranks.
---@return boolean
function GameActivity:OnlyOneTeamLeft() end

--- Gets access to the huge banner of any player that can display messages which can not be missed or ignored.
---@param whichColor? number Which color banner to get - see the GameActivity::BannerColor enum. (default: YELLOW)
---@param player? number Which player's banner to get. (default: Players::PlayerOne)
---@return GUIBanner
function GameActivity:GetBanner(whichColor, player) end

--- Sets the Area within which a team can land things.
---@param team number The number of the team we're setting for.
---@param newArea Area The Area we're setting to limit their landings within.
function GameActivity:SetLZArea(team, newArea) end

--- Gets the Area within which a team can land things. OWNERSHIP IS NOT TRANSFERRED!
---@param team number The number of the team we're setting for.
---@return Area
function GameActivity:GetLZArea(team) end

--- Sets the width of the landing zone box that follows around a player's brain.
---@param player number The number of the in-game player we're setting for.
---@param width number The width of the box, in pixels. 0 means disabled.
function GameActivity:SetBrainLZWidth(player, width) end

--- Gets the width of the landing zone box that follows around a player's brain.
---@param player number The number of the player we're getting for.
---@return number
function GameActivity:GetBrainLZWidth(player) end

--- Returns active CPU team count.
---@return number
function GameActivity:GetActiveCPUTeamCount() end

--- Returns active human team count.
---@return number
function GameActivity:GetActiveHumanTeamCount() end

--- Created an objective point for one of the teams to show until cleared.
---@param description string The team number of the team to give objective. 0 is team #1.
---@param objPos Vector The very short description of what the objective is (three short words max)
---@param whichTeam? number The absolute scene coordiante position of the objective. (default: Teams::TeamOne)
---@param arrowDir? number The desired direction of the arrow when the point is on screen. (default: ARROWDOWN)
function GameActivity:AddObjectivePoint(description, objPos, whichTeam, arrowDir) end

--- Sorts all objective points according to their positions on the Y axis.
function GameActivity:YSortObjectivePoints() end

--- Clears all objective points previously added, for both teams.
function GameActivity:ClearObjectivePoints() end

--- Adds somehting to the purchase list that will override what is set in the buy guy next time CreateDelivery is called.
---@param pPurchase SceneObject The SceneObject preset to add to the override purchase list. OWNERSHIP IS NOT TRANSFERRED!
---@param player number Which player's list to add an override purchase item to.
---@return number
function GameActivity:AddOverridePurchase(pPurchase, player) end

--- First clears and then adds all the stuff in a Loadout to the override purchase list.
---@param pLoadout Loadout The Loadout preset to set the override purchase list to reflect. OWNERSHIP IS NOT TRANSFERRED!
---@param player number The player we're talking about.
---@return number
function GameActivity:SetOverridePurchaseList(pLoadout, player) end

---@param pLoadout string The Loadout preset to set the override purchase list to reflect. OWNERSHIP IS NOT TRANSFERRED!
---@param player number The player we're talking about.
---@return number
function GameActivity:SetOverridePurchaseList(pLoadout, player) end

--- Clears all items from a specific player's override purchase list.
---@param player number
function GameActivity:ClearOverridePurchase(player) end

--- Takes the current order out of a player's buy GUI, creates a Delivery based off it, and stuffs it into that player's delivery queue.
---@param player number Which player to create the delivery for.
---@return boolean
function GameActivity:CreateDelivery(player) end

---@param player number Which player to create the delivery for and Cargo AI mode.
---@param mode number
---@return boolean
function GameActivity:CreateDelivery(player, mode) end

---@param player number Which player to create the delivery for. Cargo AI mode and waypoint.
---@param mode number
---@param waypoint Vector
---@return boolean
function GameActivity:CreateDelivery(player, mode, waypoint) end

---@param player number Which player to create the delivery for. Cargo AI mode and waypoint.
---@param mode number
---@param waypoint Actor
---@return boolean
function GameActivity:CreateDelivery(player, mode, waypoint) end

--- Shows how many deliveries this team has pending.
---@param team number
---@return number
function GameActivity:GetDeliveryCount(team) end

--- Returns the name of the tech module selected for this team during scenario setup
---@param team number Team to return tech module for
---@return string
function GameActivity:GetTeamTech(team) end

--- Sets tech module name for specified team. Module must set must be loaded.
---@param team number Team to set module, module name, for example Dummy.rte
---@param tech string
function GameActivity:SetTeamTech(team, tech) end

--- Returns CrabToHumanSpawnRatio for specified module
---@param moduleid number
---@return number
function GameActivity:GetCrabToHumanSpawnRatio(moduleid) end

--- Indicates whether a specific team is assigned a CPU player in the current game.
---@param team number Which team index to check.
---@return boolean
function GameActivity:TeamIsCPU(team) end

--- Returns how much starting gold was selected in scenario setup dialog. 20000 - infinite amount.
---@return number
function GameActivity:GetStartingGold() end

--- Returns whether fog of war must be enabled for this activity or not. Call it to determine whether you should call MakeAllUnseen or not at the start of activity.
---@return boolean
function GameActivity:GetFogOfWarEnabled() end

--- This is a special update step for when any player is still editing the scene.
function GameActivity:UpdateEditing() end

--- Goes through all Actor:s currently in the MovableMan and disables or enables each one with a Controller set to AI input.
---@param disable? boolean Whether to disable or enable them; (default: true)
---@param whichTeam? number Which team to do this to. If all, then pass Teams::NoTeam (default: Teams::NoTeam)
function GameActivity:DisableAIs(disable, whichTeam) end

--- Goes through all Actor:s currently in the MovableMan and sets each one not controlled by a player to be AI controlled and AIMode setting based on team and CPU team.
function GameActivity:InitAIs() end

--- The base class that specifies certain common creation/destruction patterns and simple reflection support for virtually all RTE classes.
---@class Entity
---@field ClassName string
---@field PresetName string Gets the name of this Entity's data Preset.
---@field Description string Gets the plain text description of this Entity's data Preset.
---@field IsOriginalPreset boolean Indicates whether this Entity was explicitly given a new instance name upon creation, or if it was just copied/inherited implicitly.
---@field ModuleID number Shows the ID of the DataModule this Entity has been defined in.
---@field ModuleName string Gets the name of this Entity's Data Module it was defined in.
---@field RandomWeight number Returns random weight used in PresetMan::GetRandomBuyableOfGroupFromTech.
---@field Groups string[] Gets the set of groups this is member of.
Entity = {}

---@return any
function Entity:Clone(...) end

--- Resets the entire Entity, including its inherited members, to their default settings or values.
function Entity:Reset() end

--- Gets the name of this Entity's data Preset, preceded by the name of the Data Module it was defined in, separated with a '/'.
---@return string
function Entity:GetModuleAndPresetName() end

--- Adds this Entity to a new grouping.
---@param newGroup string A string which describes the group to add this to. Duplicates will be ignored.
function Entity:AddToGroup(newGroup) end

--- Removes this Entity from the specified grouping.
---@param groupToRemoveFrom string A string which describes the group to remove this from.
function Entity:RemoveFromGroup(groupToRemoveFrom) end

--- Gets whether this is part of a specific group or not.
---@param whichGroup string A string which describes the group to check for.
---@return boolean
function Entity:IsInGroup(whichGroup) end

--- A hovering craft, with two engines on each attached on each end which tilt independently of the body to achieve steering.
---@class ACDropShip : ACraft
---@field ClassName string The C++ class name, like "ACDropShip".
---@field RightEngine AEmitter Gets the right side thruster of this ACDropship.
---@field LeftEngine AEmitter Gets the left side thruster of this ACDropship.
---@field RightThruster AEmitter Gets the right side secondary thruster of this ACDropship.
---@field LeftThruster AEmitter Gets the left side secondary thruster of this ACDropship.
---@field RightHatch Attachable Gets the right side hatch of this ACDropship.
---@field LeftHatch Attachable Gets the left side hatch of this ACDropship.
---@field MaxEngineAngle number Get max engine rotation angle in degrees.
---@field LateralControlSpeed number Gets the abstract rate of LateralControl change. Default is 6
---@field LateralControl number Sets lateral control value -1.0 to 1.0 control of sideways movement. 0 means try to stand still in X.
---@field HoverHeightModifier number Gets the modifier for height at which this ACDropship should hover above terrain.
ACDropShip = {}

--- Makes a copy of this ACDropShip, which you own until you add it to the game.
---@return ACDropShip
function ACDropShip:Clone() end

--- Checks for obstacles in the travel direction.
---@param distance number How far ahead of travel direction to check for obstacles.
---@return number
function ACDropShip:DetectObstacle(distance) end

--- Gets the altitide of this' pos (or appropriate low point) over the terrain, in pixels. here means less calculation.
---@param max? number The max altitude you care to check for. 0 Means check the whole scene's height. (default: 0)
---@param accuracy? number The accuracy within which measurement is acceptable. Higher number (default: 0)
---@return number
function ACDropShip:GetAltitude(max, accuracy) end

--- A crab-like actor with four legs.
---@class ACrab : Actor
---@field ClassName string The C++ class name, like "ACrab".
---@field Turret Turret Gets the Turret of this ACrab.
---@field Jetpack AEJetpack Gets the jetpack of this ACrab.
---@field LeftFGLeg Leg Gets the left foreground Leg of this ACrab.
---@field LeftBGLeg Leg Gets the left background Leg of this ACrab.
---@field RightFGLeg Leg Gets the right foreground Leg of this ACrab.
---@field RightBGLeg Leg Gets the right BG Leg of this ACrab.
---@field StrideSound SoundContainer Gets this ACrab's stride sound. Ownership is NOT transferred!
---@field StrideFrame boolean Gets whether this ACrab has just taken a stride this frame.
---@field EquippedItem MovableObject Returns whatever is equipped in the turret, if anything. OWNERSHIP IS NOT TRANSFERRED!
---@field FirearmIsReady boolean Indicates whether the currently held HDFirearm's is ready for use, and has ammo etc.
---@field FirearmIsEmpty boolean Indicates whether the currently held HDFirearm's is out of ammo.
---@field FirearmNeedsReload boolean Indicates whether the currently held HDFirearm's is almost out of ammo.
---@field FirearmIsSemiAuto boolean Indicates whether the currently held HDFirearm's is semi or full auto.
---@field FirearmActivationDelay number Returns the currently held device's delay between pulling the trigger and activating.
---@field LimbPathPushForce number Gets the default force that a limb traveling walking LimbPath can push against stuff in the scene with.
---@field AimRangeUpperLimit number Gets the upper limit of this ACrab's aim range.
---@field AimRangeLowerLimit number Gets the lower limit of this ACrab's aim range.
---@field LEFTSIDE number One of ACrab.Side.
---@field RIGHTSIDE number One of ACrab.Side.
---@field SIDECOUNT number One of ACrab.Side.
---@field FGROUND number One of ACrab.Layer.
---@field BGROUND number One of ACrab.Layer.
---@field STILL number One of ACrab.DeviceHandlingState.
---@field POINTING number One of ACrab.DeviceHandlingState.
---@field SCANNING number One of ACrab.DeviceHandlingState.
---@field AIMING number One of ACrab.DeviceHandlingState.
---@field FIRING number One of ACrab.DeviceHandlingState.
---@field THROWING number One of ACrab.DeviceHandlingState.
---@field DIGGING number One of ACrab.DeviceHandlingState.
---@field NOSWEEP number One of ACrab.SweepState.
---@field SWEEPINGUP number One of ACrab.SweepState.
---@field SWEEPUPPAUSE number One of ACrab.SweepState.
---@field SWEEPINGDOWN number One of ACrab.SweepState.
---@field SWEEPDOWNPAUSE number One of ACrab.SweepState.
---@field NOTDIGGING number One of ACrab.DigState.
---@field PREDIG number One of ACrab.DigState.
---@field STARTDIG number One of ACrab.DigState.
---@field TUNNELING number One of ACrab.DigState.
---@field FINISHINGDIG number One of ACrab.DigState.
---@field PAUSEDIGGER number One of ACrab.DigState.
---@field NOTJUMPING number One of ACrab.JumpState.
---@field FORWARDJUMP number One of ACrab.JumpState.
---@field PREJUMP number One of ACrab.JumpState.
---@field UPJUMP number One of ACrab.JumpState.
---@field APEXJUMP number One of ACrab.JumpState.
---@field LANDJUMP number One of ACrab.JumpState.
ACrab = {}

--- Makes a copy of this ACrab, which you own until you add it to the game.
---@return ACrab
function ACrab:Clone() end

--- Reloads the currently held firearms, if any.
function ACrab:ReloadFirearms() end

--- Tells whether a point on the scene is within close range of the currently used device and aiming status, if applicable.
---@param point Vector A Vector witht he aboslute coordinates of a point to check.
---@return boolean
function ACrab:IsWithinRange(point) end

--- Casts an unseen-revealing ray in the direction of where this is facing. casting. A random ray will be chosen out of this +-range.
---@param FOVSpread number The degree angle to deviate from the current view point in the ray
---@param range number The range, in pixels, beyond the actors sharp aim that the ray will have.
---@return boolean
function ACrab:Look(FOVSpread, range) end

--- Casts an MO detecting ray in the direction of where the head is looking at the time. Factors including head rotation, sharp aim mode, and other variables determine how this ray is cast. casting. A random ray will be chosen out of this +-range.
---@param FOVSpread? number The degree angle to deviate from the current view point in the ray (default: 45)
---@param ignoreMaterial? number A specific material ID to ignore (see through) (default: 0)
---@param ignoreAllTerrain? boolean Whether to ignore all terrain or not (true means 'x-ray vision'). (default: false)
---@return MovableObject
function ACrab:LookForMOs(FOVSpread, ignoreMaterial, ignoreAllTerrain) end

--- Gets the LimbPath corresponding to the passed in Side, Layer and MovementState values.
---@param side number Whether to get the left or right side.
---@param layer number Whether to get foreground or background LimbPath.
---@param movementState number Which movement state to get the LimbPath for.
---@return LimbPath
function ACrab:GetLimbPath(side, layer, movementState) end

--- Get walking limb path speed for the specified preset.
---@param speedPreset number Speed preset to set 0 = LimbPath::SLOW, 1 = Limbpath::NORMAL, 2 = LimbPath::FAST
---@return number
function ACrab:GetLimbPathSpeed(speedPreset) end

--- Set walking limb path speed for the specified preset.
---@param speedPreset number Speed preset to set 0 = LimbPath::SLOW, 1 = Limbpath::NORMAL, 2 = LimbPath::FAST. New speed value in m/s.
---@param speed number
function ACrab:SetLimbPathSpeed(speedPreset, speed) end

--- A flying Actor which carries other things and can drop them.
---@class ACraft : Actor
---@field ClassName string The C++ class name, like "ACraft".
---@field HatchState number Gets the current state of the hatch.
---@field HatchOpenSound SoundContainer Gets this ACraft's hatch opening sound. Ownership is NOT transferred!
---@field HatchCloseSound SoundContainer Gets this ACraft's hatch closing sound. Ownership is NOT transferred!
---@field CrashSound SoundContainer Gets this ACraft's crash sound. Ownership is NOT transferred!
---@field MaxPassengers number The recomended, not absolute, maximum number of actors that fit in the invetory. Used by the activity AI. Default is -1 (unknown).
---@field DeliveryDelayMultiplier number
---@field ScuttleOnDeath boolean Gets whether this ACraft will scuttle automatically on death.
---@field HatchDelay number Gets the hatch opening/closing delay of this ACraft.
---@field CLOSED number One of ACraft.HatchState.
---@field OPENING number One of ACraft.HatchState.
---@field OPEN number One of ACraft.HatchState.
---@field CLOSING number One of ACraft.HatchState.
---@field HatchStateCount number One of ACraft.HatchState.
---@field RIGHT number One of ACraft.Side.
---@field LEFT number One of ACraft.Side.
---@field FALL number One of ACraft.CraftDeliverySequence.
---@field LAND number One of ACraft.CraftDeliverySequence.
---@field STANDBY number One of ACraft.CraftDeliverySequence.
---@field UNLOAD number One of ACraft.CraftDeliverySequence.
---@field LAUNCH number One of ACraft.CraftDeliverySequence.
---@field UNSTICK number One of ACraft.CraftDeliverySequence.
---@field HOVER number One of ACraft.AltitudeMoveState.
---@field DESCEND number One of ACraft.AltitudeMoveState.
---@field ASCEND number One of ACraft.AltitudeMoveState.
ACraft = {}

--- Opens the hatch doors, if they're closed or closing.
function ACraft:OpenHatch() end

--- Closes the hatch doors, if they're open or opening.
function ACraft:CloseHatch() end

--- A rocket craft, with main engine and rotation of the whole body as a means of steering. 12/13/2006 ARocket changed names to ACRocket, parent changed to ACraft
---@class ACRocket : ACraft
---@field ClassName string The C++ class name, like "ACRocket".
---@field RightLeg Leg Gets the right leg of this ACRocket.
---@field LeftLeg Leg Gets the left Leg of this ACRocket.
---@field MainEngine AEmitter Gets the main thruster of this ACRocket.
---@field LeftEngine AEmitter Gets the left side thruster of this ACRocket.
---@field RightEngine AEmitter Gets the right side thruster of this ACRocket.
---@field LeftThruster AEmitter Gets the left side secondary thruster of this ACRocket.
---@field RightThruster AEmitter Gets the right side secondary thruster of this ACRocket.
---@field GearState number Gets the the landing gear state
---@field RAISED number One of ACRocket.LandingGearState.
---@field LOWERED number One of ACRocket.LandingGearState.
---@field LOWERING number One of ACRocket.LandingGearState.
---@field RAISING number One of ACRocket.LandingGearState.
---@field GearStateCount number One of ACRocket.LandingGearState.
ACRocket = {}

--- Makes a copy of this ACRocket, which you own until you add it to the game.
---@return ACRocket
function ACRocket:Clone() end

--- A sprite movable object that is autonomous.
---@class Actor : MOSRotating
---@field ClassName string The C++ class name, like "Actor".
---@field PlayerControllable boolean Gets whether or not this Actor can be controlled by human players. Note that this does not protect the Actor's Controller from having its input mode forced to CIM_PLAYER (e.g. via Lua).
---@field BodyHitSound SoundContainer Gets this Actor's body hit sound. Ownership is NOT transferred!
---@field AlarmSound SoundContainer Gets this Actor's alarm sound. Ownership is NOT transferred!
---@field PainSound SoundContainer Gets this Actor's pain sound. Ownership is NOT transferred!
---@field DeathSound SoundContainer Gets this Actor's death sound. Ownership is NOT transferred!
---@field DeviceSwitchSound SoundContainer Gets this Actor's device switch sound. Ownership is NOT transferred!
---@field ImpulseDamageThreshold number Returns Threshold for taking damage from travel impulses, in kg * m/s
---@field StableRecoveryDelay number Gets the recovery delay from UNSTABLE to STABLE, in MS.
---@field Status number Returns the current Status of this.
---@field Health number Gets this Actor's health value.
---@field PrevHealth number Gets this Actor's previous health value, prior to this frame.
---@field MaxHealth number Gets this Actor's maximum health value.
---@field InventoryMass number Gets the mass of this Actor's inventory. Does not include any equipped item (for actor subtypes that have that).
---@field GoldCarried number Gets how many ounces of gold this Actor is carrying.
---@field AimRange number The limit of this actors aiming angle, in each direction, in radians. Eg if HalfPI, it means full 180 degree range
---@field CPUPos Vector Gets the absoltue position of this' brain, or equivalent.
---@field EyePos Vector Gets the absoltue position of this' eye, or equivalent, where look vector starts from.
---@field HolsterOffset Vector Gets the offset position of the holster where this Actor draws his devices from.
---@field ReloadOffset Vector Gets the offset position of where this Actor reloads his devices from.
---@field ViewPoint Vector Gets the point at which this actor is viewing, or that the scene frame should be centered on if tracking this Actor's view. In absolute scene coordinates.
---@field ItemInReach HeldDevice Gets the item that is within reach of the Actor at this frame, ready to be be picked up. Ownership is NOT transferred!
---@field SharpAimProgress number Gets the normalized amount of sharp aim that has been achieved by this.
---@field Height number Gets the approximate height of this Actor, standing up.
---@field AIMode number Gets this' AI mode.
---@field DeploymentID number
---@field PassengerSlots number Gets this Actor's passenger slots.
---@field Perceptiveness number Gets this' perceptiveness to alarming events going on around him.
---@field PainThreshold number Gets this' PainThreshold value above which it will play PainSound
---@field CanRevealUnseen boolean Gets whether this actor is able to reveal unseen areas by looking.
---@field InventorySize number Tells how many things are in the invetory
---@field MaxInventoryMass number Returns the maximum total mass this Actor can carry in its inventory.
---@field MovePathSize number Gets how many waypoints there are in the MovePath currently
---@field MovePathEnd Vector Gets the last position in this Actor's move path.
---@field IsWaitingOnNewMovePath boolean Returns whether we're waiting on a new pending movepath.
---@field AimDistance number Gets the distance between the actor and the view point when not sharp aiming.
---@field SightDistance number
---@field PieMenu PieMenu Gets a pointer to the PieMenu for this Actor. Ownership is NOT transferred.
---@field AIBaseDigStrength number Gets this Actor's base dig strength, or the strength of terrain they can expect to walk through without tools.
---@field DigStrength number Estimates what material strength this actor can penetrate.
---@field SceneWaypoints Vector[]
---@field LimbPushForcesAndCollisionsDisabled boolean Gets whether or not this Actor's limb push forces have been disabled.
---@field MoveProximityLimit number Gets the distance in which the Actor will have considered itself to have reached it's waypoint.
---@field MOMoveTarget number
---@field MovePath Vector[]
---@field Inventory MovableObject[]
---@field STABLE number One of Actor.Status.
---@field UNSTABLE number One of Actor.Status.
---@field INACTIVE number One of Actor.Status.
---@field DYING number One of Actor.Status.
---@field DEAD number One of Actor.Status.
---@field NOMOVE number One of Actor.MovementState.
---@field STAND number One of Actor.MovementState.
---@field WALK number One of Actor.MovementState.
---@field JUMP number One of Actor.MovementState.
---@field DISLODGE number One of Actor.MovementState.
---@field CROUCH number One of Actor.MovementState.
---@field CRAWL number One of Actor.MovementState.
---@field ARMCRAWL number One of Actor.MovementState.
---@field CLIMB number One of Actor.MovementState.
---@field MOVEMENTSTATECOUNT number One of Actor.MovementState.
---@field AIMODE_NONE number One of Actor.AIMode.
---@field AIMODE_SENTRY number One of Actor.AIMode.
---@field AIMODE_PATROL number One of Actor.AIMode.
---@field AIMODE_GOTO number One of Actor.AIMode.
---@field AIMODE_BRAINHUNT number One of Actor.AIMode.
---@field AIMODE_GOLDDIG number One of Actor.AIMode.
---@field AIMODE_RETURN number One of Actor.AIMode.
---@field AIMODE_STAY number One of Actor.AIMode.
---@field AIMODE_SCUTTLE number One of Actor.AIMode.
---@field AIMODE_DELIVER number One of Actor.AIMode.
---@field AIMODE_BOMB number One of Actor.AIMode.
---@field AIMODE_SQUAD number One of Actor.AIMode.
---@field AIMODE_COUNT number One of Actor.AIMode.
---@field MOVING number One of Actor.ActionState.
---@field MOVING_FAST number One of Actor.ActionState.
---@field FIRING number One of Actor.ActionState.
---@field ActionStateCount number One of Actor.ActionState.
---@field AIMSTILL number One of Actor.AimState.
---@field AIMUP number One of Actor.AimState.
---@field AIMDOWN number One of Actor.AimState.
---@field AimStateCount number One of Actor.AimState.
---@field LAT_STILL number One of Actor.LateralMoveState.
---@field LAT_LEFT number One of Actor.LateralMoveState.
---@field LAT_RIGHT number One of Actor.LateralMoveState.
---@field PROCEEDING number One of Actor.ObstacleState.
---@field BACKSTEPPING number One of Actor.ObstacleState.
---@field DIGPAUSING number One of Actor.ObstacleState.
---@field JUMPING number One of Actor.ObstacleState.
---@field SOFTLANDING number One of Actor.ObstacleState.
---@field NOTBLOCKED number One of Actor.TeamBlockState.
---@field BLOCKED number One of Actor.TeamBlockState.
---@field IGNORINGBLOCK number One of Actor.TeamBlockState.
---@field FOLLOWWAIT number One of Actor.TeamBlockState.
Actor = {}

--- Makes a copy of this Actor, which you own until you add it to the game.
---@return Actor
function Actor:Clone() end

--- Gets this Actor's Controller. Ownership IS NOT transferred!
---@return Controller
function Actor:GetController() end

--- Tells whether a player is currently controlling this.
---@return boolean
function Actor:IsPlayerControlled() end

--- Tells wheter the player can switch control to this at all
---@return boolean
function Actor:IsControllable() end

--- Sets this Actor's new Controller input mode.
---@param newMode number The new input mode.
---@param newPlayer? number The player which will control this if the input mode was set to player. (default: -1)
function Actor:SetControllerMode(newMode, newPlayer) end

--- Sets this Actor's Controller mode and gives back what it used to be.
---@param newMode number The new mode to set to.
---@param newPlayer? number The player which will control this if the input mode was set to player. (default: -1)
---@return number
function Actor:SwapControllerModes(newMode, newPlayer) end

--- Gets the X and Y thresholds for how fast the actor can travel before losing stability.
---@return Vector
function Actor:GetStableVelocityThreshold() end

--- Sets the X and Y thresholds for how fast the actor can travel before losing stability.
---@param newVelX number New value for how fast the actor can travel before losing stability on X axis.
---@param newVelY number New value for how fast the actor can travel before losing stability on Y axis.
function Actor:SetStableVelocityThreshold(newVelX, newVelY) end

---@param newVelVector Vector Vector with new values for how fast the actor can travel before losing stability on both axis.
function Actor:SetStableVelocityThreshold(newVelVector) end

--- Gets this Actor's aim angle.
---@param adjustForFlipped? boolean Whether to adjust the angle for flipping or not. (default: true)
---@return number
function Actor:GetAimAngle(adjustForFlipped) end

--- Sets this Actor's aim angle.
---@param newAngle number A new angle, in radians.
function Actor:SetAimAngle(newAngle) end

--- Shows whether this is or carries a specifically named object in its inventory. Also looks through the inventories of potential passengers, as applicable.
---@param objectName string The Preset name of the object to look for.
---@return boolean
function Actor:HasObject(objectName) end

--- Shows whether this is or carries a specifically grouped object in its inventory. Also looks through the inventories of potential passengers, as applicable.
---@param groupName string The name of the group to look for.
---@return boolean
function Actor:HasObjectInGroup(groupName) end

--- Tells whether a point on the scene is within range of the currently used device and aiming status, if applicable.
---@param point Vector A Vector witht he aboslute coordinates of a point to check.
---@return boolean
function Actor:IsWithinRange(point) end

--- Adds a certain amount of ounces of gold to this' team's total funds.
---@param goldOz number The amount in Oz with which to change this' team's gold tally.
function Actor:AddGold(goldOz) end

--- Adds health points to this Actor's current health value.
---@param addedHealth number
---@return number
function Actor:AddHealth(addedHealth) end

--- Checks if this Actor is in a specific status.
---@param which number Which status to check for.
---@return boolean
function Actor:IsStatus(which) end

--- Checks if this Actor is dead.
---@return boolean
function Actor:IsDead() end

--- Adds an absolute scene point to the list of waypoints this is going to go to, in order are reached.
---@param waypoint Vector
function Actor:AddAISceneWaypoint(waypoint) end

--- Adds an MO in the scene as the next waypoint for this to go to, in order OWNERSHIP IS NOT TRANSFERRED!
---@param pMOWaypoint MovableObject The new MO this should try to get to after all other waypoints are reached.
function Actor:AddAIMOWaypoint(pMOWaypoint) end

--- Removes all AI waypoints and clears the current path to the current waypoint. The AI Actor will stop in its tracks.
function Actor:ClearAIWaypoints() end

--- Gets the last or furthest set AI waypoint of this. If none, this' pos is returned.
---@return Vector
function Actor:GetLastAIWaypoint() end

--- Gets the ID of the last set AI MO waypoint of this. If none, g_NoMOID is returned.
---@return number
function Actor:GetAIMOWaypointID() end

--- Gets how many waypoints this actor have.
---@return number
function Actor:GetWaypointListSize() end

--- Clears the list of coordinates in this' current MovePath, ie the path to the next Waypoint.
function Actor:ClearMovePath() end

--- Adds a coordinate to the beginning of the MovePath, meaning the one closest to this Actor.
---@param newCoordinate Vector The new coordinate to add to the front of the MovePath.
function Actor:AddToMovePathBeginning(newCoordinate) end

--- Adds a coordinate to the end of the MovePath, meaning the one closest to this Actor's next waypoint.
---@param newCoordinate Vector
function Actor:AddToMovePathEnd(newCoordinate) end

--- Removes a coordinate from the beginning of the MovePath, meaning the one closest to this Actor. is empty.
---@return boolean
function Actor:RemoveMovePathBeginning() end

--- Removes a coordinate from the end of the MovePath, meaning the one farthest from this Actor. is empty.
---@return boolean
function Actor:RemoveMovePathEnd() end

--- Adds an inventory item to this Actor.
---@param pItemToAdd MovableObject
function Actor:AddInventoryItem(pItemToAdd) end

--- Removes a specified item from the actor's inventory. Only one item is removed at a time.
---@param presetName string
function Actor:RemoveInventoryItem(presetName) end

---@param moduleName string The module name of the item to remove.
---@param presetName string The preset name of the item to remove.
function Actor:RemoveInventoryItem(moduleName, presetName) end

--- Removes and returns the inventory item at the given index. Ownership IS transferred.
---@param inventoryIndex number The index of the inventory item to remove.
---@return MovableObject
function Actor:RemoveInventoryItemAtIndex(inventoryIndex) end

--- Swaps the next MovableObject carried by this Actor and puts one not currently carried into the into the back of the inventory of this. Whether to mute the sound on this event. Override for the loading screen hack. If there are no MovableObject:s in inventory, 0 will be returned.
---@param pSwapIn? MovableObject A pointer to the external MovableObject to trade in. Ownership IS xferred! (default: nullptr)
---@param muteSound? boolean If 0 is passed in, nothing will be added to the inventory. (default: false)
---@return MovableObject
function Actor:SwapNextInventory(pSwapIn, muteSound) end

--- Swaps the prev MovableObject carried by this Actor and puts one not currently carried into the into the back of the inventory of this. If 0 is passed in, nothing will be added to the inventory. If there are no MovableObject:s in inventory, 0 will be returned.
---@param pSwapIn? MovableObject A pointer to the external MovableObject to trade in. Ownership IS xferred! (default: nullptr)
---@return MovableObject
function Actor:SwapPrevInventory(pSwapIn) end

--- Ejects all inventory items that this is carrying. It may not happen instantaneously, so check for ejection being complete with IsInventoryEmpty().
function Actor:DropAllInventory() end

--- Converts all of the Gold carried by this Actor into MovableObjects and ejects them into the Scene.
function Actor:DropAllGold() end

--- Tells whether inventory is completely empty
---@return boolean
function Actor:IsInventoryEmpty() end

--- Makes this draw its current waypoints and related data on the scene in its HUD drawing stage.
---@param drawWaypoints? boolean Whether to enable or disable the drawing of the waypoints. (default: true)
function Actor:DrawWaypoints(drawWaypoints) end

--- Sets this' AI's move path to be updated. Will update the path to the current waypoint, if any.
function Actor:SetMovePathToUpdate() end

--- Starts updating this Actor's movepath.
function Actor:UpdateMovePath() end

--- Makes this alarmed about a certian point on in the scene, overriding the current AI mode until a certain time has passed. is there.
---@param alarmPoint Vector The new scene point this should look at and see if anything dangerous
function Actor:SetAlarmPoint(alarmPoint) end

--- Gets any point on the scene this actor should be alarmed about this frame. is there or (0,0) if nothing is alarming.
---@return Vector
function Actor:GetAlarmPoint() end

--- Gets whether or not this Actor has the organic flag set and should be considered as organic.
---@return boolean
function Actor:IsOrganic() end

--- Gets whether or not this Actor has the mechanical flag set and should be considered as mechanical.
---@return boolean
function Actor:IsMechanical() end

--- A sliding or swinging door.
---@class ADoor : Actor
---@field ClassName string The C++ class name, like "ADoor".
---@field Door Attachable Gets the moving door Attachable of this ADoor
---@field DoorMoveStartSound SoundContainer Gets this ADoor's door move start sound. Ownership is NOT transferred!
---@field DoorMoveSound SoundContainer Gets this ADoor's door move sound. Ownership is NOT transferred!
---@field DoorDirectionChangeSound SoundContainer Gets this ADoor's door direction change sound. Ownership is NOT transferred!
---@field DoorMoveEndSound SoundContainer Gets this ADoor's door move end sound. Ownership is NOT transferred!
---@field CLOSED number One of ADoor.DoorState.
---@field OPENING number One of ADoor.DoorState.
---@field OPEN number One of ADoor.DoorState.
---@field CLOSING number One of ADoor.DoorState.
---@field STOPPED number One of ADoor.DoorState.
ADoor = {}

--- Makes a copy of this ADoor, which you own until you add it to the game.
---@return ADoor
function ADoor:Clone() end

--- Gets the current state of the door.
---@return number
function ADoor:GetDoorState() end

--- Opens the door if it's closed.
function ADoor:OpenDoor() end

--- Closes the door if it's open.
function ADoor:CloseDoor() end

--- Force the door to stop at the exact position it is.
function ADoor:StopDoor() end

--- Resets the sensor Timer for this ADoor, effectively making it ignore Actors.
function ADoor:ResetSensorTimer() end

--- Sets whether this ADoor closes (or opens) after a while by default.
---@param closedByDefault boolean Whether the door by default goes to a closed position. If not, then it will open after a while.
function ADoor:SetClosedByDefault(closedByDefault) end

--- An attachable MO that creates and emits particle MO's.
---@class AEmitter : Attachable
---@field ClassName string The C++ class name, like "AEmitter".
---@field EmissionSound SoundContainer Gets this AEmitter's emission sound. Ownership is NOT transferred!
---@field BurstSound SoundContainer Gets this AEmitter's burst sound. Ownership is NOT transferred!
---@field EndSound SoundContainer Gets this AEmitter's end sound. Ownership is NOT transferred!
---@field BurstScale number Gets the scale factor that will be applied to the regular spread and emission velocity to get the burst particle parameters.
---@field EmitAngle number Gets the angle of direction that the emitted particles will be shot at.
---@field GetThrottle number Gets the normalized throttle scalar which controls how to affect the emission rate as per the emisison rate range. Depricated for Lua, use the Throttle property instead. 0 means normal, -1.0 means least emission rate.
---@field Throttle number Gets the normalized throttle scalar which controls how to affect the emission rate as per the emisison rate range. Depricated for Lua, use the Throttle property instead. 0 means normal, -1.0 means least emission rate.
---@field ThrottleFactor number Gets the adjusted throttle multiplier that is factored into the emission rate of this AEmitter.
---@field NegativeThrottleMultiplier number Gets the negative throttle multiplier of this AEmitter.
---@field PositiveThrottleMultiplier number Gets the positive throttle multiplier of this AEmitter.
---@field BurstSpacing number Gets the BurstSpacing for this emitter.
---@field BurstDamage number Returns burst damage of this emitter.
---@field EmitterDamageMultiplier number Returns damage multiplier of this emitter.
---@field EmitCount number Gets the number of emissions emitted since emission was last enabled.
---@field EmitCountLimit number Gets the number of emissions left before emitter is disabled.
---@field EmitDamage number Returns emit damage of this emitter.
---@field EmitOffset Vector Gets the offset of the emission point from this' sprite center, which gets rotated with this.
---@field Flash Attachable Gets the flash of this AEmitter.
---@field FlashScale number Gets the display scale factor of the flash effect. This is purely visual.
---@field TotalParticlesPerMinute number Gets the rate at which all of the Emissions of this AEmitter, combined, emit their particles.
---@field TotalBurstSize number Gets the number of particles that will be emitted by all the Emissions of this AEmitter combined, in one shot when a burst is triggered.
---@field Emissions Emission[]
AEmitter = {}

--- Makes a copy of this AEmitter, which you own until you add it to the game.
---@return AEmitter
function AEmitter:Clone() end

--- Indicates whether this AEmitter is currently enabled and emitting.
---@return boolean
function AEmitter:IsEmitting() end

--- Returns whether this emitter was emitting last frame.
---@return boolean
function AEmitter:WasEmitting() end

--- Sets this AEmitter to start emitting at the set rate, or to stop.
---@param enable? boolean Whether to enable or disable emission. (default: true)
function AEmitter:EnableEmission(enable) end

--- A vector in the direction, including the rotation of the emitter, that the emitted particles will be shot at.
---@return Vector
function AEmitter:GetEmitVector() end

--- A vector in the opposite direction, including the rotation of the emitter, that the emitted particles will be shot at.
---@return Vector
function AEmitter:GetRecoilVector() end

--- Calculates the forces this emitter applies on any parent.
---@param burst? boolean Whether to calculate a burst update or not. (default: false)
---@return number
function AEmitter:EstimateImpulse(burst) end

--- Triggers a one-shot burst of emissions in the number that has previously been set. The burst will happen during the next Update of this AEmitter.
function AEmitter:TriggerBurst() end

--- Indicates whether this AEmitter is set to burst next update or not.
---@return boolean
function AEmitter:IsSetToBurst() end

--- Checks if it is possible to trigger a one-shot burst of emissions during the next Update of this AEmitter.
---@return boolean
function AEmitter:CanTriggerBurst() end

--- Returns a scaled throttle value that represents a linear increase of force. Because of (bad) reasons, throttle is in the range -1.0F to 1.0F, where -1.0F is "minimum force" and 1.0F is "maximum force". 0.0F is "whoever the fuck knows?" force. As such, multiplying throttle by 2 does not mean twice the force emitted, instead it means "whoever the fuck knows?" additional force emitted. All work and no play makes Jack a dull boy. All work and no play makes Jack a dull boy. All work and no play makes Jack a dull boy. ...this helper function lets us apply a scale to throttle and get a sensible result.
---@param throttle number The throttle value to be considered.
---@param multiplier number The multiplier to scale by, in terms of absolute force emitted.
---@return number
function AEmitter:GetScaledThrottle(throttle, multiplier) end

--- Returns whether this emitter just started emitting this frame.
---@return boolean
function AEmitter:JustStartedEmitting() end

--- A jetpack MO, which can be used to generate thrust
---@class AEJetpack : AEmitter
---@field ClassName string The C++ class name, like "AEJetpack".
---@field JetpackType number Gets the type of this jetpack.
---@field JetTimeTotal number Gets the amount of time this jetpack can fire when filled, in ms.
---@field JetTimeLeft number Gets the amount of time this jetpack can still fire until out, in ms.
---@field JetReplenishRate number Gets the rate at which this AHuman's jetpack is replenished during downtime.
---@field MinimumFuelRatio number Gets the rate at which this AHuman's jetpack is replenished during downtime.
---@field JetAngleRange number Gets the scalar ratio at which this jetpack's thrust angle follows the aim angle of the user.
---@field CanAdjustAngleWhileFiring boolean Returns whether the angle of this jetpack can adjust while firing, or if it can only be aimed while off.
---@field AdjustsThrottleForWeight boolean Returns whether this jetpack adjusts it's throttle to balance for extra weight.
---@field Standard number One of AEJetpack.JetpackType.
---@field JumpPack number One of AEJetpack.JetpackType.
AEJetpack = {}

--- Makes a copy of this AEJetpack, which you own until you add it to the game.
---@return AEJetpack
function AEJetpack:Clone() end

--- A humanoid actor.
---@class AHuman : Actor
---@field ClassName string The C++ class name, like "AHuman".
---@field Head Attachable Gets the head of this AHuman.
---@field Jetpack AEJetpack Gets the jetpack of this AHuman.
---@field FGArm Arm Gets the foreground Arm of this AHuman.
---@field BGArm Arm Gets the background arm of this AHuman.
---@field FGLeg Leg Gets the foreground Leg of this AHuman.
---@field BGLeg Leg Gets the background Leg of this AHuman.
---@field FGFoot Attachable Gets the foot Attachable of this AHuman's foreground Leg.
---@field BGFoot Attachable Gets the foot Attachable of this AHuman's background Leg.
---@field MaxWalkPathCrouchShift number Gets this AHuman's max walkpath adjustment upwards to crouch below low ceilings.
---@field MaxCrouchRotation number Gets this AHuman's max crouch rotation to duck below low ceilings.
---@field CrouchAmount number Gets this AHuman's current crouch amount. 0.0 == fully standing, 1.0 == fully crouched.
---@field CrouchAmountOverride number Gets this AHuman's current crouch amount override. 0.0 == fully standing, 1.0 == fully crouched, -1 == no override.
---@field StrideSound SoundContainer Gets this AHuman's stride sound. Ownership is NOT transferred!
---@field UpperBodyState number Gets this AHuman's UpperBodyState.
---@field MovementState number Gets this Actor's MovementState.
---@field ProneState number Gets this AHuman's ProneState.
---@field ThrowPrepTime number Gets the duration it takes this AHuman to fully charge a throw.
---@field ThrowProgress number Gets the throw chargeup progress of this AHuman.
---@field EquippedItem HeldDevice Gets the FG Arm's HeldDevice. Ownership is NOT transferred.
---@field EquippedBGItem HeldDevice Gets the BG Arm's HeldDevice. Ownership is NOT transferred.
---@field EquippedMass number Gets the total mass of this AHuman's currently equipped devices.
---@field FirearmIsReady boolean Indicates whether the currently held HDFirearm's is ready for use, and has ammo etc.
---@field ThrowableIsReady boolean Indicates whether the currently held ThrownDevice's is ready to go.
---@field FirearmIsEmpty boolean Indicates whether the currently held HDFirearm's is out of ammo.
---@field FirearmNeedsReload boolean Indicates whether any currently held HDFirearms are almost out of ammo.
---@field FirearmIsSemiAuto boolean Indicates whether the currently held HDFirearm's is semi or full auto.
---@field FirearmActivationDelay number Returns the currently held device's delay between pulling the trigger and activating.
---@field LimbPathPushForce number Gets the default force that a limb traveling walking LimbPath can push against stuff in the scene with.
---@field IsClimbing boolean Gets whether this AHuman is currently attempting to climb something, using arms.
---@field StrideFrame boolean Gets whether this AHuman has just taken a stride this frame.
---@field ArmSwingRate number Gets the rate at which this AHuman's Arms will swing with Leg movement, if they're not holding or supporting a HeldDevice.
---@field DeviceArmSwayRate number Gets the rate at which this AHuman's Arms will sway with Leg movement, if they're holding or supporting a HeldDevice.
---@field WEAPON_READY number One of AHuman.UpperBodyState.
---@field AIMING_SHARP number One of AHuman.UpperBodyState.
---@field HOLSTERING_BACK number One of AHuman.UpperBodyState.
---@field HOLSTERING_BELT number One of AHuman.UpperBodyState.
---@field DEHOLSTERING_BACK number One of AHuman.UpperBodyState.
---@field DEHOLSTERING_BELT number One of AHuman.UpperBodyState.
---@field THROWING_PREP number One of AHuman.UpperBodyState.
---@field THROWING_RELEASE number One of AHuman.UpperBodyState.
---@field NOTPRONE number One of AHuman.ProneState.
---@field GOPRONE number One of AHuman.ProneState.
---@field PRONE number One of AHuman.ProneState.
---@field PRONESTATECOUNT number One of AHuman.ProneState.
---@field FGROUND number One of AHuman.Layer.
---@field BGROUND number One of AHuman.Layer.
---@field STILL number One of AHuman.DeviceHandlingState.
---@field POINTING number One of AHuman.DeviceHandlingState.
---@field SCANNING number One of AHuman.DeviceHandlingState.
---@field AIMING number One of AHuman.DeviceHandlingState.
---@field FIRING number One of AHuman.DeviceHandlingState.
---@field THROWING number One of AHuman.DeviceHandlingState.
---@field DIGGING number One of AHuman.DeviceHandlingState.
---@field NOSWEEP number One of AHuman.SweepState.
---@field SWEEPINGUP number One of AHuman.SweepState.
---@field SWEEPUPPAUSE number One of AHuman.SweepState.
---@field SWEEPINGDOWN number One of AHuman.SweepState.
---@field SWEEPDOWNPAUSE number One of AHuman.SweepState.
---@field NOTDIGGING number One of AHuman.DigState.
---@field PREDIG number One of AHuman.DigState.
---@field STARTDIG number One of AHuman.DigState.
---@field TUNNELING number One of AHuman.DigState.
---@field FINISHINGDIG number One of AHuman.DigState.
---@field PAUSEDIGGER number One of AHuman.DigState.
---@field NOTJUMPING number One of AHuman.JumpState.
---@field FORWARDJUMP number One of AHuman.JumpState.
---@field PREJUMP number One of AHuman.JumpState.
---@field UPJUMP number One of AHuman.JumpState.
---@field APEXJUMP number One of AHuman.JumpState.
---@field LANDJUMP number One of AHuman.JumpState.
AHuman = {}

--- Makes a copy of this AHuman, which you own until you add it to the game.
---@return AHuman
function AHuman:Clone() end

--- Switches the currently held device (if any) to the first found firearm in the inventory. If the held device already is a firearm, or no firearm is in inventory, nothing happens. or just report that it's there or not.
---@param doEquip? boolean Whether to actually equip any matching item found in the inventory, (default: true)
---@return boolean
function AHuman:EquipFirearm(doEquip) end

--- Switches the currently held device (if any) to the first found ThrownDevice in the inventory. If the held device already is a ThrownDevice, or no ThrownDevice  is in inventory, nothing happens. or just report that it's there or not.
---@param doEquip? boolean Whether to actually equip any matching item found in the inventory, (default: true)
---@return boolean
function AHuman:EquipThrowable(doEquip) end

--- Switches the currently held device (if any) to the strongest digging tool in the inventory.
---@param doEquip? boolean Whether to actually equip the strongest digging tool, or just report whether a digging tool was found.
---@return boolean
function AHuman:EquipDiggingTool(doEquip) end

--- Switches the currently held device (if any) to the first found shield in the inventory. If the held device already is a shield, or no shield is in inventory, nothing happens.
---@return boolean
function AHuman:EquipShield() end

--- Tries to equip the first shield in inventory to the background arm; this only works if nothing is held at all, or the FG arm holds a one-handed device, or we're in inventory mode.
---@return boolean
function AHuman:EquipShieldInBGArm() end

--- Switches the currently held device (if any) to the first found device of the specified group in the inventory. If the held device already is of that group, or no device is in inventory, nothing happens. or just report that it's there or not.
---@param group string The group the device must belong to.
---@param doEquip? boolean Whether to actually equip any matching item found in the inventory, (default: true)
---@return boolean
function AHuman:EquipDeviceInGroup(group, doEquip) end

--- Switches the equipped HeldDevice (if any) to the first found device with the specified preset name in the inventory. If the equipped HeldDevice is of that module and preset name, nothing happens.
---@param presetName string The preset name of the HeldDevice to equip.
---@param doEquip boolean Whether to actually equip any matching item found in the inventory, or just report whether or not it's there.
---@return boolean
function AHuman:EquipNamedDevice(presetName, doEquip) end

---@param moduleName string The module name of the HeldDevice to equip.
---@param presetName string The preset name of the HeldDevice to equip.
---@param doEquip boolean Whether to actually equip any matching item found in the inventory, or just report whether or not it's there.
---@return boolean
function AHuman:EquipNamedDevice(moduleName, presetName, doEquip) end

--- Switches the currently held device (if any) to the first loaded HDFirearm of the specified group in the inventory. If no such weapon is in the inventory, nothing happens. or just report that it's there or not.
---@param group string The group the HDFirearm must belong to. "Any" for all groups.
---@param exludeGroup string The group the HDFirearm must *not* belong to. "None" for no group.
---@param doEquip? boolean Whether to actually equip any matching item found in the inventory, (default: true)
---@return boolean
function AHuman:EquipLoadedFirearmInGroup(group, exludeGroup, doEquip) end

--- Unequips whatever is in the FG arm and puts it into the inventory.
---@return boolean
function AHuman:UnequipFGArm() end

--- Unequips whatever is in the BG arm and puts it into the inventory.
---@return boolean
function AHuman:UnequipBGArm() end

--- Unequips whatever is in either of the arms and puts them into the inventory.
function AHuman:UnequipArms() end

--- Reloads the currently held firearm, if any. Will only reload the BG Firearm if the FG one is full already, to support reloading guns one at a time.
function AHuman:ReloadFirearms() end

---@param onlyReloadEmptyFirearms? boolean Whether or not to only reload empty fireams. (default: false)
function AHuman:ReloadFirearms(onlyReloadEmptyFirearms) end

--- Indicates whether currently held HDFirearms are reloading. If the parameter is true, it will only return true if all firearms are reloading, otherwise it will return whether any firearm is reloading.
---@param onlyIfAllFirearmsAreReloading boolean
---@return boolean
function AHuman:FirearmsAreReloading(onlyIfAllFirearmsAreReloading) end

--- Tells whether a point on the scene is within close range of the currently used device and aiming status, if applicable.
---@param point Vector A Vector with the aboslute coordinates of a point to check.
---@return boolean
function AHuman:IsWithinRange(point) end

--- Casts an unseen-revealing ray in the direction of where this is facing. casting. A random ray will be chosen out of this +-range.
---@param FOVSpread number The degree angle to deviate from the current view point in the ray
---@param range number The range, in pixels, beyond the actors sharp aim that the ray will have.
---@return boolean
function AHuman:Look(FOVSpread, range) end

--- Casts a material detecting ray in the direction of where this is facing. casting. A random ray will be chosen out of this +-range. found gold. It will be unaltered if false is returned. has been filled out with the absolute location of the gold.
---@param FOVSpread number The degree angle to deviate from the current view point in the ray
---@param range number The range, in pixels, that the ray will have.
---@param foundLocation Vector A Vector which will be filled with the absolute coordinates of any
---@return boolean
function AHuman:LookForGold(FOVSpread, range, foundLocation) end

--- Casts an MO detecting ray in the direction of where the head is looking at the time. Factors including head rotation, sharp aim mode, and other variables determine how this ray is cast. casting. A random ray will be chosen out of this +-range.
---@param FOVSpread? number The degree angle to deviate from the current view point in the ray (default: 45)
---@param ignoreMaterial? number A specific material ID to ignore (see through) (default: 0)
---@param ignoreAllTerrain? boolean Whether to ignore all terrain or not (true means 'x-ray vision'). (default: false)
---@return MovableObject
function AHuman:LookForMOs(FOVSpread, ignoreMaterial, ignoreAllTerrain) end

--- Gets the LimbPath corresponding to the passed in Layer and MovementState values.
---@param layer number Whether to get foreground or background LimbPath.
---@param movementState number Which movement state to get the LimbPath for.
---@return LimbPath
function AHuman:GetLimbPath(layer, movementState) end

--- Get walking limb path speed for the specified preset.
---@param speedPreset number Speed preset to set 0 = LimbPath::SLOW, 1 = Limbpath::NORMAL, 2 = LimbPath::FAST
---@return number
function AHuman:GetLimbPathSpeed(speedPreset) end

--- Set walking limb path speed for the specified preset.
---@param speedPreset number Speed preset to set 0 = LimbPath::SLOW, 1 = Limbpath::NORMAL, 2 = LimbPath::FAST. New speed value in m/s.
---@param speed number
function AHuman:SetLimbPathSpeed(speedPreset, speed) end

--- Gets the target rot angle for the given MovementState.
---@param movementState number The MovementState to get the rot angle target for.
---@return number
function AHuman:GetRotAngleTarget(movementState) end

--- Sets the target rot angle for the given MovementState.
---@param movementState number The MovementState to get the rot angle target for.
---@param newRotAngleTarget number The new rot angle target to use.
function AHuman:SetRotAngleTarget(movementState, newRotAngleTarget) end

--- Gets the walk path rotation for the specified Layer.
---@param whichLayer number The Layer in question.
---@return number
function AHuman:GetWalkAngle(whichLayer) end

--- Sets the walk path rotation for the specified Layer.
---@param whichLayer number The Layer in question.
---@param angle number The angle to set.
function AHuman:SetWalkAngle(whichLayer, angle) end

--- A detachable arm that can hold HeldDevices.
---@class Arm : Attachable
---@field ClassName string The C++ class name, like "Arm".
---@field MaxLength number Gets the max length of this Arm when fully extended, i.e. the farthest possible length from its joint position to the hand.
---@field MoveSpeed number Gets the move speed of this Arm, where 1.0 is instant and 0.0 is none.
---@field HandIdleOffset Vector Gets the default idle offset of this Arm's hand, i.e. the default offset from the joint position that this Arm will try to move to when not moving towards a position.
---@field HandPos Vector Gets the current position of this Arm's hand in absolute Scene coordinates.
---@field HasAnyHandTargets boolean Gets whether or not this Arm has any HandTargets.
---@field NumberOfHandTargets number Gets the number of HandTargets this Arm has.
---@field NextHandTargetDescription string Gets the name of this Arm's next HandTarget.
---@field NextHandTargetPosition Vector Gets the position, in absolute scene coordinates, of this Arm's next HandTarget.
---@field HandHasReachedCurrentTarget boolean Gets whether or not the hand has reached its current target. This is either the front of the HandTarget queue, or the offset it's currently trying to move to to when it has no HandTargets specified.
---@field GripStrength number Gets the the strength with which this Arm will grip its HeldDevice.
---@field ThrowStrength number Gets the the strength with which this Arm will throw a ThrownDevice.
---@field HeldDevice HeldDevice Gets the HeldDevice currently held by this Arm.
---@field SupportedHeldDevice HeldDevice Gets the HeldDevice this Arm is trying to support.
Arm = {}

--- Makes a copy of this Arm, which you own until you add it to the game.
---@return Arm
function Arm:Clone() end

--- Adds a HandTarget position, in absolute scene coordinates, to the queue for the Arm to move its hand towards. Target positions are removed from the queue when they're reached (or as close to reached as is possible).
---@param description string The description of this HandTarget, for easy identification.
---@param handTargetPositionToAdd Vector The position, in absolute scene coordinates, to add the queue of hand targets.
function Arm:AddHandTarget(description, handTargetPositionToAdd) end

---@param description string The description of this HandTarget, for easy identification.
---@param handTargetPositionToAdd Vector The position, in absolute scene coordinates, to add the queue of hand targets.
---@param delayAtTarget number The amount of time, in MS, that the hand should wait when it reaches the newly added HandTarget.
function Arm:AddHandTarget(description, handTargetPositionToAdd, delayAtTarget) end

--- Removes this Arm's next HandTarget, if there is one.
function Arm:RemoveNextHandTarget() end

--- Empties the queue of HandTargets. With the queue empty, the hand will move to its appropriate idle offset.
function Arm:ClearHandTargets() end

--- An articulated, detachable part of an Actor's body.
---@class Attachable : MOSRotating
---@field ClassName string The C++ class name, like "Attachable".
---@field ParentOffset Vector Gets the stored offset between this Attachable's parent's position and the joint position. This should be maintained by the parent.
---@field JointStrength number Gets the amount of impulse force the joint of this Attachable can handle before breaking.
---@field JointStiffness number Gets the stiffness scalar of the joint of this Attachable, normalized between 0 and 1.0. 1.0 means impulse forces on this attachable will be transferred to the parent with 100% strength, 0 means they will not transfer at all.
---@field JointOffset Vector Gets the offset of the joint (the point around which this Attachable and its parent hinge) from this Attachable's center of mass/origin.
---@field JointPos Vector Gets the absolute position of the joint that the parent of this Attachable sets upon Update().
---@field DeleteWhenRemovedFromParent boolean Gets whether this Attachable will be deleted when removed from its parent. Has no effect until the Attachable has been added to a parent.
---@field GibWhenRemovedFromParent boolean Gets whether this Attachable will gib when removed from its parent. Has no effect until the Attachable has been added to a parent.
---@field ApplyTransferredForcesAtOffset boolean Gets whether forces transferred from this Attachable should be applied at its parent's offset (rotated to match the parent) where they will produce torque, or directly at its parent's position.
---@field BreakWound AEmitter Gets the AEmitter that represents the wound added to this Attachable when it gets detached from its parent. OWNERSHIP IS NOT TRANSFERRED!
---@field ParentBreakWound AEmitter Gets the AEmitter that represents the wound added to this Attachable's parent when this Attachable gets detached from its parent. OWNERSHIP IS NOT TRANSFERRED!
---@field InheritsHFlipped number Gets whether or not this Attachable inherits its parent's HFlipped value, i.e. whether it has its HFlipped value reset to match/reverse its parent's every frame, if attached. 0 means no inheritance, 1 means normal inheritance, anything else means reversed inheritance (i.e. if the parent's HFlipped value is true, this Attachable's HFlipped value will be false).
---@field InheritsRotAngle boolean Gets whether or not this Attachable inherits its RotAngle from its parent, i.e. whether it has its RotAngle reset to match its parent every frame, if attached.
---@field InheritedRotAngleOffset number Gets the offset of this Attachable's rotation angle from its parent. Only actually applied if the Attachable is set to inherit its parent's rotation angle.
---@field AtomSubgroupID number Gets the subgroup ID of this' Atoms.
---@field CollidesWithTerrainWhileAttached boolean Gets whether this Attachable currently has terrain collisions enabled and it's atoms are present in the parent AtomGroup. Attachables with Attachable parents that don't collide with terrain will not collide with terrain. This chains up to the root parent.
---@field IgnoresParticlesWhileAttached boolean Gets whether this Attachable currently ignores collisions with single-atom particles.
---@field CanCollideWithTerrain boolean Gets whether this Attachable is currently able to collide with terrain, taking into account its terrain collision settings and those of its parent and so on.
---@field DrawnAfterParent boolean Gets whether this Attachable is to be drawn after (in front of) or before (behind) its parent.
---@field InheritsFrame boolean Gets whether or not this Attachable inherits its Frame from its parent, if attached.
Attachable = {}

--- Makes a copy of this Attachable, which you own until you add it to the game.
---@return Attachable
function Attachable:Clone() end

--- Indicates whether this Attachable is attached to an MOSRotating parent or not.
---@return boolean
function Attachable:IsAttached() end

--- Indicates whether this Attachable is attached to the specified MOSRotating or not.
---@param parentToCheck MOSRotating A pointer to which MOSRotating you want to check for.
---@return boolean
function Attachable:IsAttachedTo(parentToCheck) end

---@return Attachable
function Attachable:RemoveFromParent() end

---@param addToMovableMan boolean
---@param addBreakWounds boolean
---@return Attachable
function Attachable:RemoveFromParent(addToMovableMan, addBreakWounds) end

--- A special SceneObject that specifies a Loadout of whatever Tech is relevant to be placed in a specific location in a Scene.
---@class Deployment : SceneObject
---@field ClassName string The C++ class name, like "Deployment".
---@field ID number Return this deployment's unique ID
---@field HFlipped boolean Returns whether this MOSprite is being drawn flipped horizontally (along the vertical axis), or not.
---@field SpawnRadius number Gets the radius around this deployment that gets checked if another actor/item of the same type and name already exists and will block re-spawning a new one by this
Deployment = {}

--- Gets the name of the Loadout that this Deployment spawns.
---@return string
function Deployment:GetLoadoutName() end

--- Creates and returns the Actor that this Deployment dictates should spawn here. Ownership IS transferred!! All items of the Loadout of this Deployment will be added to the Actor's inventory as well (and also owned by it) OWNERSHIP IS TRANSFERRED!
---@return Actor
function Deployment:CreateDeployedActor() end

--- Creates and returns the first Device that Deployment dictates should spawn here. Ownership IS transferred!! Only the first Device is created. OWNERSHIP IS TRANSFERRED!
---@return SceneObject
function Deployment:CreateDeployedObject() end

--- Something to bundle the properties of an emission together.
---@class Emission : Entity
---@field ClassName string The C++ class name, like "Emission".
---@field ParticlesPerMinute number Gets the rate at which these emissions are made, in particles per minute.
---@field MinVelocity number Gets the specified minimum velocity an emitted MO can have when emitted.
---@field MaxVelocity number Gets the specified maximum velocity an emitted MO can have when emitted.
---@field PushesEmitter boolean Indicates whether this emission is supposed to push its emitter back because of recoil.
---@field LifeVariation number Gets the specified variation in lifetime of the emitted particles.
---@field BurstSize number Gets the number of extra particles that are bursted at the beginning of emission.
---@field Spread number Gets the angle spread of velocity of the emitted MO's to each side of the m_EmitAngle angle. in radians. PI/2 would mean that MO's fly out to one side only, with the m_Rotation defining the middle of that half circle.
---@field Offset Vector Gets offset of the emission point from Emitter's sprite center, which gets rotated with owner Emitter
Emission = {}

--- Resets the emission timers so they start counting time as to wheter emissions are clearer.
function Emission:ResetEmissionTimers() end

--- Something to bundle the properties of Gib piece together.
---@class Gib
---@field ClassName string The C++ class name, like "Gib".
---@field ParticlePreset MovableObject Gets the reference particle to be used as a Gib. Ownership is NOT transferred!
---@field MinVelocity number Gets the specified minimum velocity a GibParticle object can have when spawned.
---@field MaxVelocity number Gets the specified maximum velocity a GibParticle object can have when spawned.
---@field SpreadMode number Gets this Gib's spread mode, which determines how velocity angles are applied to the GibParticles.
---@field Offset Vector Offset spawn position from owner/parent's position.
---@field Count number The number of copies of the GibParticle that will be spawned.
---@field Spread number The angle spread of the spawned GibParticle objects to each side of the parent's angle in radians.
---@field LifeVariation number The per-Gib variation in Lifetime, in percentage of the existing Lifetime of the gib.
---@field InheritsVel number How much of the exploding parent's velocity this Gib should inherit.
---@field IgnoresTeamHits boolean Whether this Gib should ignore hits with the team of the exploding parent or not.
---@field SpreadRandom number One of Gib.SpreadMode.
---@field SpreadEven number One of Gib.SpreadMode.
---@field SpreadSpiral number One of Gib.SpreadMode.
Gib = {}

--- The supporting object for a lua script that can be turned on and off by the player, and run independent of Activities.
---@class GlobalScript : Entity
---@field ClassName string The C++ class name, like "GlobalScript".
GlobalScript = {}

function GlobalScript:Deactivate() end

--- A firearm device that fires projectile MO's and discharges shell MO's.
---@class HDFirearm : HeldDevice
---@field ClassName string The C++ class name, like "HDFirearm".
---@field ReloadEndOffset number Gets reload end offset, in ms. This is how early the ReloadEnd sound is played compared to actual end of reload.
---@field EjectionPos Vector Gets the absolute position of the Shell ejection point. ejection point of this
---@field EjectionOffset Vector Gets the unrotated relative offset from the position to the Shell ejection point. this from this' position.
---@field RateOfFire number Gets the rate of fire of this. This applies even if semi-auto. it limits how quickly a new round can be fired after the last.
---@field MSPerRound number Gets the minimum time in between shots, in MS.
---@field FullAuto boolean Tells whether the device is fully automatic or not.
---@field Reloadable boolean Gets whether this HDFirearm is set to be reloadable or not.
---@field DualReloadable boolean Gets whether or not this HDFirearm allows dual-reload, i.e. if it's one-handed and dual-wieldable, it can reload at the same time as another weapon that also allows dual-reload.
---@field OneHandedReloadTimeMultiplier number Gets the multiplier to be applied to reload time when this HDFirearm is being reloaded one-handed.
---@field ReloadAngle number Gets the reload angle this HDFirearm will use when support is available.
---@field OneHandedReloadAngle number Gets the reload angle this HDFirearm will use when support is not available.
---@field CurrentReloadAngle number Gets the reload angle this HDFirearm is currently using, based on whether or not support is available.
---@field RoundInMagCount number Gets the number of rounds still in the loaded magazine. Negative value means infinite ammo. HDFirearm. Negative means infinite ammo.
---@field RoundInMagCapacity number Gets the maximum RoundCount a Magazine of this HDFirearm can hold. If there is no Magazine, it gets the RoundCount of the reference Magazine.
---@field Magazine Magazine Gets the Magazine of this HDFirearm.
---@field Flash Attachable Gets the flash of this HDFirearm.
---@field PreFireSound SoundContainer Gets this HDFirearm's pre fire sound. Ownership is NOT transferred!
---@field FireSound SoundContainer Gets this HDFirearm's fire sound. Ownership is NOT transferred!
---@field FireEchoSound SoundContainer Gets this HDFirearm's fire echo sound. Ownership is NOT transferred!
---@field ActiveSound SoundContainer Gets this HDFirearm's active sound. Ownership is NOT transferred!
---@field DeactivationSound SoundContainer Gets this HDFirearm's deactivation sound. Ownership is NOT transferred!
---@field EmptySound SoundContainer Gets this HDFirearm's empty sound. Ownership is NOT transferred!
---@field ReloadStartSound SoundContainer Gets this HDFirearm's reload start sound. Ownership is NOT transferred!
---@field ReloadEndSound SoundContainer Gets this HDFirearm's reload end sound. Ownership is NOT transferred!
---@field ActivationDelay number Gets the delay before firing.
---@field DeactivationDelay number Gets the delay between release of activation and another can be started.
---@field BaseReloadTime number Gets the base time this HDFirearm takes to reload, in milliseconds.
---@field ReloadTime number Gets how long this HDFirearm currently takes to reload, in milliseconds.
---@field ReloadProgress number Gets this HDFirearm's reload progress as a scalar from 0 to 1.
---@field ShakeRange number Gets the range of normal shaking of entire weapon.
---@field SharpShakeRange number Gets the range of shaking of entire weapon during sharp aiming.
---@field NoSupportFactor number Gets the factor for how much more weapon shakes if it isn't supported by a second hand.
---@field ParticleSpreadRange number Gets the range of spread angle of fired particles, in one direction.
---@field ShellVelVariation number Gets the random velocity variation scalar at which this HDFirearm's shell is to be ejected.
---@field FiredOnce boolean Whether at least one round has already been	fired during the current activation.
---@field FiredFrame boolean Whether at least one round has already been fired during the current frame.
---@field CanFire boolean Gets whether this HDFirearm is ready to be fired.
---@field RoundsFired number How many rounds were fired during this frame.
---@field IsAnimatedManually boolean If true then the m_Frame property is not changed bye the Update function
---@field RecoilTransmission number Gets the stiffness scalar of the joint of this Attachable, normalized between 0 and 1.0. 1.0 means impulse forces on this attachable will be transferred to the parent with 100% strength, 0 means they will not transfer at all.
HDFirearm = {}

--- Makes a copy of this HDFirearm, which you own until you add it to the game.
---@return HDFirearm
function HDFirearm:Clone() end

--- Gets the velocity the AI use when aiming this weapon.
---@return number
function HDFirearm:GetAIFireVel() end

--- Gets the bullet life time the AI use when aiming this weapon.
---@return number
function HDFirearm:GetAIBulletLifeTime() end

--- Gets the bullet acceleration scalar the AI use when aiming this weapon.
---@return number
function HDFirearm:GetBulletAccScalar() end

--- Gets the blast radius the AI use when aiming this weapon.
---@return number
function HDFirearm:GetAIBlastRadius() end

--- Gets how much material the projectiles from this weapon can destory.
---@return number
function HDFirearm:GetAIPenetration() end

--- Estimates how close the projectiles from two weapons will land.
---@param pWeapon HDFirearm A HDFirearm pointer to compare with.
---@return number
function HDFirearm:CompareTrajectories(pWeapon) end

--- Gets the preset name of the next Magazine that will be loaded into this gun.
---@return string
function HDFirearm:GetNextMagazineName() end

--- Sets the Preset name of the next Magazine that will be loaded into this gun. This changes all future mags that will be reloaded.
---@param magName string The preset name of the new Magazine to load into this from now on.
---@return boolean
function HDFirearm:SetNextMagazineName(magName) end

--- An articulated device that can be weilded by an Actor. 01/31/2007 Made concrete so Shields can be jsut HeldDevice:s
---@class HeldDevice : Attachable
---@field ClassName string The C++ class name, like "HeldDevice".
---@field SupportPos Vector Gets the absolute position of the support handhold that this HeldDevice offers. position of this HeldDevice.
---@field MagazinePos Vector Gets the absolute position of the magazine or other equivalent point of this. attachment point of this
---@field MuzzlePos Vector Gets the absolute position of the muzzle or other equivalent point of this. of this
---@field MuzzleOffset Vector Gets the unrotated relative offset from the position to the muzzle or other equivalent point of this. this from this' position.
---@field StanceOffset Vector Gets the current position offset of this HeldDevice's joint relative from the parent Actor's position, if attached.
---@field SharpStanceOffset Vector Sets the current position offset of this HeldDevice's joint relative from the parent Actor's position, if attached.
---@field SharpLength number Gets how much farther an Actor which holds this device can see when aiming this HeldDevice sharply.
---@field Supportable boolean Gets whether this HeldDevice can be supported when held.
---@field SupportOffset Vector Returns support offset.
---@field UseSupportOffsetWhileReloading boolean Gets whether this HeldDevice while be held at the support offset with the off-hand when reloading.
---@field HasPickupLimitations boolean Gets whether this HeldDevice has any limitations on what can pick it up.
---@field UnPickupable boolean Gets whether this HeldDevice cannot be picked up at all.
---@field GripStrengthMultiplier number Gets the multiplier for how well this HeldDevice can be gripped by Arms.
---@field Supported boolean Gets whether this HeldDevice is currently supported by a second Arm.
---@field GetsHitByMOsWhenHeld boolean Gets whether this can get hit by MOs when held.
---@field VisualRecoilMultiplier number Gets the visual recoil multiplier.
HeldDevice = {}

--- Makes a copy of this HeldDevice, which you own until you add it to the game.
---@return HeldDevice
function HeldDevice:Clone() end

--- Gets whether this HeldDevice is currently being held or not.
---@return boolean
function HeldDevice:IsBeingHeld() end

--- Indicates whether this is an offensive weapon or not.
---@return boolean
function HeldDevice:IsWeapon() end

--- Indicates whether this is a tool or not.
---@return boolean
function HeldDevice:IsTool() end

--- Indicates whether this is a shield or not.
---@return boolean
function HeldDevice:IsShield() end

--- Indicates whether this is a dual wieldable weapon or not.
---@return boolean
function HeldDevice:IsDualWieldable() end

--- Sets whether this is a dual wieldable weapon or not.
---@param isDualWieldable boolean Dual wieldable or not.
function HeldDevice:SetDualWieldable(isDualWieldable) end

--- Indicates whether this can be held and operated effectively with one hand or not.
---@return boolean
function HeldDevice:IsOneHanded() end

--- Sets whether this can be held and operated effectively with one hand or not.
---@param newValue boolean New value.
function HeldDevice:SetOneHanded(newValue) end

--- Activates one of this HDFirearm's features. Analogous to 'pulling the trigger'.
function HeldDevice:Activate() end

--- Deactivates one of this HDFirearm's features. Analogous to 'releasing the trigger'.
function HeldDevice:Deactivate() end

--- Throws out the currently used Magazine, if any, and puts in a new one after the reload delay is up.
function HeldDevice:Reload() end

--- Tells whether the device is curtrently being activated.
---@return boolean
function HeldDevice:IsActivated() end

--- Tells whether the device is curtrently being reloaded.
---@return boolean
function HeldDevice:IsReloading() end

--- Tells whether the device just finished reloading this frame.
---@return boolean
function HeldDevice:DoneReloading() end

--- Tells whether the device is curtrently in need of being reloaded.
---@return boolean
function HeldDevice:NeedsReloading() end

--- Tells whether the device is curtrently full and reloading won't have any effect.
---@return boolean
function HeldDevice:IsFull() end

--- Tells whether this HeldDevice is currently empty of ammo.
---@return boolean
function HeldDevice:IsEmpty() end

--- Checks whether the given Actor can pick up this HeldDevice.
---@param actor Actor The Actor to check. Ownership is NOT transferred.
---@return boolean
function HeldDevice:IsPickupableBy(actor) end

--- Specify that objects with the given PresetName can pick up this HeldDevice.
---@param presetName string The PresetName of an object that should be able to pick up this HeldDevice.
function HeldDevice:AddPickupableByPresetName(presetName) end

--- Remove allowance for objects with the given PresetName to pick up this HeldDevice. Note that if the last allowance is removed, the HeldDevice will no longer have pickup limitations, rather than setting itself as unpickupable.
---@param actorPresetName string The PresetName of an object that should no longer be able to pick up this HeldDevice.
function HeldDevice:RemovePickupableByPresetName(actorPresetName) end

--- A detachable Leg that will be controlled by LimbPaths.
---@class Leg : Attachable
---@field ClassName string The C++ class name, like "Leg".
---@field Foot Attachable Gets the foot of this Leg.
---@field MoveSpeed number Gets the move speed of this Leg, where 1.0 is instant and 0.0 is no movement.
Leg = {}

--- Makes a copy of this Leg, which you own until you add it to the game.
---@return Leg
function Leg:Clone() end

--- A set of Vector:s making up a motion path for a AtomGroup's limb. The path is continuous.
---@class LimbPath
---@field ClassName string The C++ class name, like "LimbPath".
---@field StartOffset Vector Gets the coordinates where the limb should start at the start of the LimbPath cycle, relative to the owning AtomGroup's local origin.
---@field SegmentCount number Gets the number of Vector:s the internal array of 'waypoints' or segments of this LimbPath.
---@field TravelSpeedMultiplier number Gets the current travel speed multiplier.
LimbPath = {}

--- Gets a pointer to the segment at the given index. Ownership is NOT transferred.
---@param segmentIndex number The index of the segment to get.
---@return Vector
function LimbPath:GetSegment(segmentIndex) end

--- An Attachable ammo magazine that can hold rounds that can be fired by HDFirearm:s.
---@class Magazine : Attachable
---@field ClassName string The C++ class name, like "Magazine".
---@field NextRound Round Gets the next Round preset of ammo in this Magazine, without removing it. Ownership IS NOT transferred!
---@field RoundCount number Returns how many rounds are left in this Magazine.
---@field IsEmpty boolean Returns whether this Magazine is out of rounds.
---@field IsFull boolean Returns whether this Magazine has not used up any rounds yet.
---@field IsOverHalfFull boolean Returns whether this Magazine has not used up half of the rounds yet.
---@field Capacity number Returns teh number of rounds this can hold when it's full.
---@field Discardable boolean Whether this Magazine should be released into the scene when discarded or just deleted.
Magazine = {}

--- Makes a copy of this Magazine, which you own until you add it to the game.
---@return Magazine
function Magazine:Clone() end

--- Represents a material and holds all the relevant data.
---@class Material : Entity
---@field ClassName string The C++ class name, like "Material".
---@field ID number Gets the index of this Material in the material palette.
---@field Restitution number Gets the scalar value that defines the restitution of this Material. 1.0 = no kinetic energy is lost in a collision, 0.0 = all energy is lost (plastic).
---@field Bounce number Gets the scalar value that defines the restitution of this Material. 1.0 = no kinetic energy is lost in a collision, 0.0 = all energy is lost (plastic).
---@field Friction number Gets the scalar value that defines the friction of this Material. 1.0 = will snag onto everything, 0.0 = will glide with no friction.
---@field Stickiness number Gets the scalar value that defines the stickiness of this Material. 1.0 = will stick to everything, 0.0 = will never stick to anything.
---@field Strength number The impulse force that a particle needs to knock loose a terrain pixel of this material. In kg * m/s.
---@field StructuralIntegrity number The impulse force that a particle needs to knock loose a terrain pixel of this material. In kg * m/s.
---@field DensityKGPerVolumeL number Gets the density of this Material in Kg/L.
---@field DensityKGPerPixel number Gets the density of this Material in kg/pixel, usually calculated from the KG per Volume L property.
---@field SettleMaterial number If this material transforms into something else when settling into the terrain, this will return that different material index. If not, it will just return the regular index of this material.
---@field SpawnMaterial number Gets the material index to spawn instead of this one for special effects.
---@field TransformsInto number Gets the material index to spawn instead of this one for special effects.
---@field IsScrap boolean Whether this material is scrap material made from gibs of things that have already been blown apart.
Material = {}

--- Holds data for a Metagame player aka "Tech" or "House".
---@class MetaPlayer
---@field ClassName string The C++ class name, like "MetaPlayer".
---@field NativeTechModule number Gets ID of the DataModule that this MetaPlayer is native to.
---@field ForeignCostMultiplier number Gets the multiplier of costs of any Tech items foreign to this MetaPlayer.
---@field NativeCostMultiplier number Gets the multiplier of costs of any Tech items native to this MetaPlayer.
---@field InGamePlayer number Shows which in-game player controls this MetaPlayer is mapped to.
---@field BrainPoolCount number Gets the number of brains in this MetaPlayer's brain pool.
MetaPlayer = {}

--- Alters the number of brains in this MetaPlayer's brain pool.
---@param change number The number of brains to add or remove from the pool.
---@return number
function MetaPlayer:ChangeBrainPoolCount(change) end

--- A movable object with mass that is graphically represented by a single pixel.
---@class MOPixel : MovableObject
---@field ClassName string The C++ class name, like "MOPixel".
---@field TrailLength number Gets the longest a trail can be drawn, in pixels.
---@field Staininess number Gets this MOPixel's staininess, which defines how likely a pixel is to stain a surface when it collides with it.
MOPixel = {}

--- Makes a copy of this MOPixel, which you own until you add it to the game.
---@return MOPixel
function MOPixel:Clone() end

--- Gets the index of the color of this MOPixel.
---@return number
function MOPixel:GetColorIndex() end

--- Sets the color value of this MOPixel via index.
---@param newColorIndex number
function MOPixel:SetColorIndex(newColorIndex) end

--- A small animated sprite that plays its animation and changes the animation and playback speed when it collides with other things.
---@class MOSParticle : MOSprite
---@field ClassName string The C++ class name, like "MOSParticle".
MOSParticle = {}

--- Makes a copy of this MOSParticle, which you own until you add it to the game.
---@return MOSParticle
function MOSParticle:Clone() end

--- A movable object with mass that is graphically represented by a BITMAP.
---@class MOSprite : MovableObject
---@field ClassName string The C++ class name, like "MOSprite".
---@field Diameter number Gets the largest diameter of this in pixels.
---@field BoundingBox Box Gets the oriented bounding box which is guaranteed to contain this, taking rotation etc into account. It's not guaranteed to be fit perfectly though. TODO MAKE FIT BETTER account, and parts of this box may be out of bounds!
---@field FrameCount number Gets the number of frames in this MOSprite's animation.
---@field SpriteOffset Vector Gets the offset that the BITMAP has from the position of this MOSprite.
---@field HFlipped boolean Returns whether this MOSprite is being drawn flipped horizontally (along the vertical axis), or not.
---@field FlipFactor number Returns a positive or negative number value to multiply with for external calculations.
---@field RotAngle number Gets the current rotational angle of of this, in radians.
---@field PrevRotAngle number Gets the previous rotational angle of this MOSprite, prior to this frame.
---@field AngularVel number Gets the current angular velocity of this MovableObject. Positive is a counter-clockwise rotation.
---@field Frame number Tells which frame is currently set to show.
---@field SpriteAnimMode number Gets the animation mode.
---@field SpriteAnimDuration number Returns animation duration in ms
---@field NOANIM number One of MOSprite.SpriteAnimMode.
---@field ALWAYSLOOP number One of MOSprite.SpriteAnimMode.
---@field ALWAYSRANDOM number One of MOSprite.SpriteAnimMode.
---@field ALWAYSPINGPONG number One of MOSprite.SpriteAnimMode.
---@field LOOPWHENACTIVE number One of MOSprite.SpriteAnimMode.
---@field LOOPWHENOPENCLOSE number One of MOSprite.SpriteAnimMode.
---@field PINGPONGOPENCLOSE number One of MOSprite.SpriteAnimMode.
---@field OVERLIFETIME number One of MOSprite.SpriteAnimMode.
---@field ONCOLLIDE number One of MOSprite.SpriteAnimMode.
MOSprite = {}

--- Hard-sets the frame this sprite is supposed to show, to the consecutive one after the current one. If currently the last fame is this will set it to the be the first, looping the animation.
---@return boolean
function MOSprite:SetNextFrame() end

--- Indicates whether this MO is moving or rotating stupidly fast in a way that will screw up the simulation.
---@return boolean
function MOSprite:IsTooFast() end

--- Indicates whether this' current graphical representation overlaps a point in absolute scene coordinates.
---@param scenePoint Vector The point in absolute scene coordinates.
---@return boolean
function MOSprite:IsOnScenePoint(scenePoint) end

--- Takes a vector which is offset from the center of this when not rotated or flipped, and then rotates and/or flips it to transform it into this' 'local space', if applicable.
---@param offset Vector A vector which is supposed to be offset from this' center when upright.
---@return Vector
function MOSprite:RotateOffset(offset) end

--- Takes a vector which is offset from the center of this when not rotated or flipped, and then rotates and/or flips it to transform it into this' 'local space', but in REVERSE.
---@param offset Vector A vector which is supposed to be offset from this' center when upright.
---@return Vector
function MOSprite:UnRotateOffset(offset) end

--- Adjusts an absolute angle based on wether this MOSprite is flipped.
---@param angle number The input angle in radians.
---@return number
function MOSprite:FacingAngle(angle) end

--- Gets the width of the bitmap of this MOSprite
---@return number
function MOSprite:GetSpriteWidth() end

--- Gets the height of the bitmap of this MOSprite
---@return number
function MOSprite:GetSpriteHeight() end

--- Gets the width of this MOSprite's GUI icon.
---@return number
function MOSprite:GetIconWidth() end

--- Gets the height of this MOSprite's GUI icon.
---@return number
function MOSprite:GetIconHeight() end

--- Sets entry wound emitter for this MOSprite
---@param presetName string Emitter preset name and module name
---@param moduleName string
function MOSprite:SetEntryWound(presetName, moduleName) end

--- Sets exit wound emitter for this MOSprite
---@param presetName string Emitter preset name and module name
---@param moduleName string
function MOSprite:SetExitWound(presetName, moduleName) end

--- Returns entry wound emitter preset name for this MOSprite
---@return string
function MOSprite:GetEntryWoundPresetName() end

--- Returns exit wound emitter preset name for this MOSprite
---@return string
function MOSprite:GetExitWoundPresetName() end

--- A sprite movable object that can rotate.
---@class MOSRotating : MOSprite
---@field ClassName string The C++ class name, like "MOSRotating".
---@field IndividualRadius number Gets the radius of this MOSRotating, not including any Attachables.
---@field IndividualDiameter number Gets the diameter of this MOSRotating, not including any Attachables.
---@field IndividualMass number Gets the mass value of this MOSRotating, not including any Attachables or wounds.
---@field RecoilForce Vector Gets the current recoil impulse force Vector of this MOSprite.
---@field RecoilOffset Vector Gets the current recoil offset Vector of this MOSprite.
---@field TravelImpulse Vector Retrurns the amount of impulse force exerted on this during the last frame.
---@field GibWoundLimit number Gets the gib wound limit for this MOSRotating, i.e. the total number of wounds required to gib this MOSRotating. Does not include any Attachables.
---@field GibSound SoundContainer Gets this MOSRotating's gib sound. Ownership is NOT transferred!
---@field GibImpulseLimit number Gets the gib impulse limit for this MOSRotating, i.e. the amount of impulse force required in a frame to gib this MOSRotating.
---@field WoundCountAffectsImpulseLimitRatio number Gets the rate at which wound count of this MOSRotating will diminish the impulse limit.
---@field GibAtEndOfLifetime boolean Gets whether this MOSRotating should gib at the end of its lifetime instead of just being deleted.
---@field DamageMultiplier number Returns damage multiplier of this attachable.
---@field WoundCount number Gets the number of wounds attached to this MOSRotating. Includes any Attachables (and their Attachables, etc.) that have a positive damage multiplier.
---@field OrientToVel number Gets the velocity orientation scalar of this MOSRotating.
---@field Attachables Attachable[]
---@field Wounds AEmitter[]
---@field Gibs Gib[]
MOSRotating = {}

--- Makes a copy of this MOSRotating, which you own until you add it to the game.
---@return MOSRotating
function MOSRotating:Clone() end

--- Adds graphical recoil offset to this MOSprite according to its angle.
function MOSRotating:AddRecoil() end

--- Adds recoil offset to this MOSprite.
---@param force Vector A vector with the recoil impulse force in kg * m/s.
---@param offset Vector A vector with the recoil offset in pixels.
---@param recoil? boolean Whether recoil should be activated or not for the next Draw(). (default: true)
function MOSRotating:SetRecoil(force, offset, recoil) end

--- Returns whether this MOSprite is currently under the effects of recoil.
---@return boolean
function MOSRotating:IsRecoiled() end

--- Sets whether or not this MOSRotating should check for deep penetrations the terrain or not.
---@param enable? boolean Whether to enable deep penetration checking or not. (default: true)
function MOSRotating:EnableDeepCheck(enable) end

--- Sets to force a deep checking of this' silhouette against the terrain and create an outline hole in the terrain, generating particles of the intersecting pixels in the terrain.
---@param enable? boolean Whether to force a deep penetration check for this sim frame or not.. (default: true)
function MOSRotating:ForceDeepCheck(enable) end

--- Destroys this MOSRotating and creates its specified Gibs in its place with appropriate velocities. Any Attachables are removed and also given appropriate velocities.
---@param impactImpulse? Vector The impulse (kg * m/s) of the impact causing the gibbing to happen.
---@param movableObjectToIgnore? MovableObject A pointer to an MO which the Gibs and Attachables should not be colliding with.
function MOSRotating:GibThis(impactImpulse, movableObjectToIgnore) end

function MOSRotating:GibThis() end

--- Checks whether any of the Atom:s in this MovableObject are on top of terrain pixels, and if so, attempt to move this out so none of this' Atoms are on top of the terrain any more. intersections. even if there wasn't any intersections to begin with.
---@param strongerThan? number Only consider materials stronger than this in the terrain for (default: g_MaterialAir)
---@return boolean
function MOSRotating:MoveOutOfTerrain(strongerThan) end

--- Sets this MOSRotating and all its children to drawn white for a specified amount of time.
---@param durationMS? number Duration of flash in real time MS.
function MOSRotating:FlashWhite(durationMS) end

--- Gets the gib wound limit for this MOSRotating, i.e. the total number of wounds required to gib this MOSRotating. Does not include any Attachables.
---@return number
function MOSRotating:GetGibWoundLimit() end

---@param includePositiveDamageAttachables boolean Whether to count wounds from Attachables that have a positive damage multiplier, i.e. those that damage their parent (this MOSRotating) when wounded.
---@param includeNegativeDamageAttachables boolean Whether to count wounds from Attachables that have a negative damage multiplier, i.e. those that heal their parent (this MOSRotating) when wounded.
---@param includeNoDamageAttachables boolean Whether to count wounds from Attachables that a zero damage multiplier, i.e. those that do not affect their parent (this MOSRotating) when wounded.
---@return number
function MOSRotating:GetGibWoundLimit(includePositiveDamageAttachables, includeNegativeDamageAttachables, includeNoDamageAttachables) end

--- Gets the number of wounds attached to this MOSRotating. Includes any Attachables (and their Attachables, etc.) that have a positive damage multiplier.
---@return number
function MOSRotating:GetWoundCount() end

---@param includePositiveDamageAttachables boolean Whether to count wounds from Attachables that have a positive damage multiplier, i.e. those that damage their parent (this MOSRotating) when wounded.
---@param includeNegativeDamageAttachables boolean Whether to count wounds from Attachables that have a negative damage multiplier, i.e. those that heal their parent (this MOSRotating) when wounded.
---@param includeNoDamageAttachables boolean Whether to count wounds from Attachables that a zero damage multiplier, i.e. those that do not affect their parent (this MOSRotating) when wounded.
---@return number
function MOSRotating:GetWoundCount(includePositiveDamageAttachables, includeNegativeDamageAttachables, includeNoDamageAttachables) end

---@return AEmitter[]
function MOSRotating:GetWounds() end

---@param includePositiveDamageAttachables boolean
---@param includeNegativeDamageAttachables boolean
---@param includeNoDamageAttachables boolean
---@return AEmitter[]
function MOSRotating:GetWounds(includePositiveDamageAttachables, includeNegativeDamageAttachables, includeNoDamageAttachables) end

--- Adds the passed in wound AEmitter to the list of wounds and changes its parent offset to the passed in Vector.
---@param woundToAdd AEmitter The wound AEmitter to add.
---@param parentOffsetToSet Vector The vector to set as the wound AEmitter's parent offset.
---@param checkGibWoundLimit? boolean Whether to gib this MOSRotating if adding this wound raises its wound count past its gib wound limit. Defaults to true.
function MOSRotating:AddWound(woundToAdd, parentOffsetToSet, checkGibWoundLimit) end

--- Removes the specified number of wounds from this MOSRotating, and returns damage caused by these removed wounds. Includes any Attachables (and their Attachables, etc.) that have a positive damage multiplier.
---@param numberOfWoundsToRemove number The number of wounds that should be removed.
---@return number
function MOSRotating:RemoveWounds(numberOfWoundsToRemove) end

---@param numberOfWoundsToRemove number The number of wounds that should be removed.
---@param includePositiveDamageAttachables boolean Whether to count wounds from Attachables that have a positive damage multiplier, i.e. those that damage their parent (this MOSRotating) when wounded.
---@param includeNegativeDamageAttachables boolean Whether to count wounds from Attachables that have a negative damage multiplier, i.e. those that heal their parent (this MOSRotating) when wounded.
---@param includeNoDamageAttachables boolean Whether to count wounds from Attachables that a zero damage multiplier, i.e. those that do not affect their parent (this MOSRotating) when wounded.
---@return number
function MOSRotating:RemoveWounds(numberOfWoundsToRemove, includePositiveDamageAttachables, includeNegativeDamageAttachables, includeNoDamageAttachables) end

--- Indicates whether this MOSRotating's current graphical representation, including its Attachables, overlaps a point in absolute scene coordinates.
---@param scenePoint Vector The point in absolute scene coordinates to check for overlap with.
---@return boolean
function MOSRotating:IsOnScenePoint(scenePoint) end

--- Cuts this' silhouette out from the terrain's material and color layers.
function MOSRotating:EraseFromTerrain() end

--- Adds the passed in Attachable the list of Attachables and sets its parent to this MOSRotating.
---@param attachable Attachable The Attachable to add.
function MOSRotating:AddAttachable(attachable) end

---@param attachable Attachable The Attachable to add.
---@param parentOffsetToSet Vector The Vector to set as the Attachable's parent offset.
function MOSRotating:AddAttachable(attachable, parentOffsetToSet) end

--- Removes the Attachable corresponding to the passed in UniqueID and sets its parent to nullptr. Does not add it to MovableMan or add break wounds.
---@param attachableUniqueID number The UniqueID of the Attachable to remove.
---@return Attachable
function MOSRotating:RemoveAttachable(attachableUniqueID) end

---@param attachableUniqueID number The UniqueID of the Attachable to remove.
---@param addToMovableMan boolean Whether or not to add the Attachable to MovableMan once it has been removed.
---@param addBreakWounds boolean Whether or not to add break wounds to the removed Attachable and this MOSRotating.
---@return Attachable
function MOSRotating:RemoveAttachable(attachableUniqueID, addToMovableMan, addBreakWounds) end

---@param attachableUniqueID Attachable The UniqueID of the Attachable to remove.
---@return Attachable
function MOSRotating:RemoveAttachable(attachableUniqueID) end

---@param attachableUniqueID Attachable The UniqueID of the Attachable to remove.
---@param addToMovableMan boolean Whether or not to add the Attachable to MovableMan once it has been removed.
---@param addBreakWounds boolean Whether or not to add break wounds to the removed Attachable and this MOSRotating.
---@return Attachable
function MOSRotating:RemoveAttachable(attachableUniqueID, addToMovableMan, addBreakWounds) end

--- Adds the passed in Attachable the list of Attachables and sets its parent to this MOSRotating.
---@param attachable Attachable The Attachable to add.
function MOSRotating:AddEmitter(attachable) end

---@param attachable Attachable The Attachable to add.
---@param parentOffsetToSet Vector The Vector to set as the Attachable's parent offset.
function MOSRotating:AddEmitter(attachable, parentOffsetToSet) end

--- Removes the Attachable corresponding to the passed in UniqueID and sets its parent to nullptr. Does not add it to MovableMan or add break wounds.
---@param attachableUniqueID number The UniqueID of the Attachable to remove.
---@return Attachable
function MOSRotating:RemoveEmitter(attachableUniqueID) end

---@param attachableUniqueID number The UniqueID of the Attachable to remove.
---@param addToMovableMan boolean Whether or not to add the Attachable to MovableMan once it has been removed.
---@param addBreakWounds boolean Whether or not to add break wounds to the removed Attachable and this MOSRotating.
---@return Attachable
function MOSRotating:RemoveEmitter(attachableUniqueID, addToMovableMan, addBreakWounds) end

---@param attachableUniqueID Attachable The UniqueID of the Attachable to remove.
---@return Attachable
function MOSRotating:RemoveEmitter(attachableUniqueID) end

---@param attachableUniqueID Attachable The UniqueID of the Attachable to remove.
---@param addToMovableMan boolean Whether or not to add the Attachable to MovableMan once it has been removed.
---@param addBreakWounds boolean Whether or not to add break wounds to the removed Attachable and this MOSRotating.
---@return Attachable
function MOSRotating:RemoveEmitter(attachableUniqueID, addToMovableMan, addBreakWounds) end

--- A movable object with mass.
---@class MovableObject : SceneObject
---@field ClassName string The C++ class name, like "MovableObject".
---@field Material Material Gets the main material of this MovableObject.
---@field Mass number Gets the mass value of this MovableObject.
---@field Pos Vector Gets the absolute position of this SceneObject.
---@field Vel Vector Gets the velocity vector of this MovableObject.
---@field PrevPos Vector Gets the previous position vector of this MovableObject, prior to this frame.
---@field PrevVel Vector Gets the previous velocity vector of this MovableObject, prior to this frame.
---@field DistanceTravelled number Gets the amount of distance this MO has travelled since its creation, in pixels.
---@field AngularVel number Gets the current angular velocity of this MovableObject. Positive is a counter-clockwise rotation.
---@field Radius number Gets the largest radius of this in pixels.
---@field Diameter number Gets the largest diameter of this in pixels.
---@field Scale number Gets the current scale of this MOSRotating. This is mostly for fun.
---@field EffectRotAngle number Gets the current absolute angle of rotation of this MovableObject's effect.
---@field EffectAlwaysShows boolean Gets whether or not this MovableObject's effect can be obscured.
---@field EffectStartStrength number Gets the starting strength of this MovableObject's effect as a float.
---@field EffectStopStrength number Gets the stopping strength of this MovableObject's effect as a float.
---@field GlobalAccScalar number Gets How this is affected by global effects, from +1.0 to -1.0. Something with a negative value will 'float' upward.
---@field AirResistance number How much this is affected by air resistance when traveling over a second, 0 to 1.0, with 0 as default
---@field AirThreshold number At which threshold of velocity, in m/s, the effect of AirResistance kicks in.
---@field Age number Gets real time age of this MovableObject.
---@field Lifetime number Gets the amount of time this MovableObject will exist from creation.
---@field ID number Gets the MOID of this MovableObject for this frame. assigned for the current frame only.
---@field UniqueID number Returns this MO's unique persistent ID
---@field RootID number Gets the MOID of the MovableObject which is the root MO of this MO for this frame. If same as what GetID returns, then this is owned by MovableMan. is owned by for the current frame only.
---@field MOIDFootprint number Gets how many total (subsequent) MOID's this MO and all its children are taking up this frame. ie if this MO has no children, this will likely be 1. Note this is only valid for this frame!
---@field Sharpness number Gets the sharpness factor of this MO. ation to any of the impulses.
---@field HasEverBeenAddedToMovableMan boolean Returns whether or not this MovableObject has ever been added to MovableMan. Does not account for removal from MovableMan.
---@field AboveHUDPos Vector Gets the absoltue position of the top of this' HUD stack.
---@field HitsMOs boolean Gets whether this MovableObject is set to collide with other MovableObject:s during its travel.
---@field GetsHitByMOs boolean Gets whether this MovableObject is set to be able to get hit by other MovableObject:s during their travel.
---@field IgnoresTeamHits boolean Tells whether this will collide with any other MO of the same team.
---@field IgnoresWhichTeam number Tells which team this would be ignoring hits with, if we're ignoring hits at all.
---@field IgnoreTerrain boolean Tells whether this will collide with any Terrain
---@field IgnoresActorHits boolean Gets whether this MO ignores collisions with actors.
---@field ToSettle boolean Indicates whether this MO is marked for settling at the end of the MovableMan update.
---@field ToDelete boolean Indicates whether this MO is marked for deletion at the end of the MovableMan update.
---@field MissionCritical boolean Shows whether this is mission critical and should therefore NEVER be settled or otherwise destroyed during teh course of a mission.
---@field HUDVisible boolean Tells whether this Actor's HUD is drawn or not.
---@field PinStrength number Gets the impulse force threshold which has to be exceeded to 'shake loose' this from a 'pinned' state. Pinned MOs don't get moved by travel algos. If 0, this isn't pinned.
---@field RestThreshold number Returns current rest threshold for this MO
---@field DamageOnCollision number If not zero applyies specified ammount of damage points to actors on collision even without penetration.
---@field DamageOnPenetration number If not zero applies specified ammount of damage points to actors on collision if penetration occured.
---@field WoundDamageMultiplier number Returns damage multiplier transferred to wound inflicted by this object on penetration
---@field HitWhatMOID number Returns the ID of the MO hit at the previously taken Travel This will only potentially return non-g_NoMOID if this object's Atom is set to hit MO's and the MO hit isn't marked to be ignored. intersecting because of the last Travel taken.
---@field HitWhatTerrMaterial number Returns the terrain material the previously taken Tarvel hit, if any.
---@field HitWhatParticleUniqueID number Returns the unique ID of the particle hit at the previously taken Travel
---@field ApplyWoundDamageOnCollision boolean Gets whether or not this MovableObject should apply wound damage when it collides with another MovableObject.
---@field ApplyWoundBurstDamageOnCollision boolean Gets whether or not this MovableObject should apply burst wound damage when it collides with another MovableObject.
---@field SimUpdatesBetweenScriptedUpdates number Gets the number of Sim updates that run between each script update for this MovableObject.
---@field PostEffectEnabled boolean Gets whether or not this MovableObject's effect is drawn every frame.
MovableObject = {}

--- Placeholder method to allow for ease of use with Attachables. Returns nullptr for classes that aren't derived from Attachable.
---@return MOSRotating
function MovableObject:GetParent() end

---@return MOSRotating
function MovableObject:GetParent() end

--- Returns a pointer to this MO, this is to enable Attachables to get their root nodes.
---@return MovableObject
function MovableObject:GetRootParent() end

---@return MovableObject
function MovableObject:GetRootParent() end

--- Reloads the all of the scripts on this object. This will also reload scripts for the original preset in PresetMan so future objects spawned will use the new scripts.
---@return number
function MovableObject:ReloadScripts() end

---@param scriptPath string
---@return boolean
function MovableObject:HasScript(scriptPath) end

---@param scriptPath string
---@return boolean
function MovableObject:AddScript(scriptPath) end

--- Checks if the script at the given path is one of the enabled scripts on this MO.
---@param scriptPath string The path to the script to check.
---@return boolean
function MovableObject:ScriptEnabled(scriptPath) end

---@param scriptPath string
---@return boolean
function MovableObject:EnableScript(scriptPath) end

---@return boolean
function MovableObject:DisableScript() end

---@param scriptPath string
---@return boolean
function MovableObject:DisableScript(scriptPath) end

--- Enables or disables all scripts on this MovableObject.
---@param enableScripts boolean Whether to enable (true) or disable (false) all scripts on this MovableObject.
function MovableObject:EnableOrDisableAllScripts(enableScripts) end

--- Returns the string value associated with the specified key or "" if it does not exist.
---@param key string Key to retrieve value.
---@return string
function MovableObject:GetStringValue(key) end

--- Returns an encoded string value associated with the specified key or "" if it does not exist.
---@param key string Key to retrieve value.
---@return string
function MovableObject:GetEncodedStringValue(key) end

--- Returns the number value associated with the specified key or 0 if it does not exist.
---@param key string Key to retrieve value.
---@return number
function MovableObject:GetNumberValue(key) end

--- Returns the entity value associated with the specified key or nullptr if it does not exist.
---@param key string Key to retrieve value.
---@return Entity
function MovableObject:GetObjectValue(key) end

--- Sets the string value associated with the specified key.
---@param key string Key to retrieve value.
---@param value string The new value to be associated with the key.
function MovableObject:SetStringValue(key, value) end

--- Sets the string value associated with the specified key.
---@param key string Key to retrieve value.
---@param value string The new value to be associated with the key.
function MovableObject:SetEncodedStringValue(key, value) end

--- Sets the number value associated with the specified key.
---@param key string Key to retrieve value.
---@param value number The new value to be associated with the key.
function MovableObject:SetNumberValue(key, value) end

--- Sets the entity value associated with the specified key.
---@param key string Key to retrieve value.
---@param value Entity The new value to be associated with the key.
function MovableObject:SetObjectValue(key, value) end

--- Remove the string value associated with the specified key.
---@param key string The key to remove.
function MovableObject:RemoveStringValue(key) end

--- Remove the number value associated with the specified key.
---@param key string The key to remove.
function MovableObject:RemoveNumberValue(key) end

--- Remove the entity value associated with the specified key.
---@param key string The key to remove.
function MovableObject:RemoveObjectValue(key) end

--- Checks whether the string value associated with the specified key exists.
---@param key string The key to check.
---@return boolean
function MovableObject:StringValueExists(key) end

--- Checks whether the number value associated with the specified key exists.
---@param key string The key to check.
---@return boolean
function MovableObject:NumberValueExists(key) end

--- Checks whether the entity value associated with the specified key exists.
---@param key string The key to check.
---@return boolean
function MovableObject:ObjectValueExists(key) end

--- Gets the altitide of this' pos (or appropriate low point) over the terrain, in pixels. here means less calculation.
---@param max? number The max altitude you care to check for. 0 Means check the whole scene's height. (default: 0)
---@param accuracy? number The accuracy within which measurement is acceptable. Higher number (default: 0)
---@return number
function MovableObject:GetAltitude(max, accuracy) end

--- Gets the MO this MO is set not to hit even when MO hitting is enabled on this MO.
---@return MovableObject
function MovableObject:GetWhichMOToNotHit() end

--- Sets this MO to not hit a specific other MO and all its children even when MO hitting is enabled on this MO.
---@param moToNotHit? MovableObject A pointer to the MO to not be hitting. Null pointer means don't ignore anyhting. Ownership is NOT transferred!
---@param forHowLong? number How long, in seconds, to ignore the specified MO. A negative number means forever.
function MovableObject:SetWhichMOToNotHit(moToNotHit, forHowLong) end

--- Tells if this MovableObject is marked for deletion at the end of the update.
---@return boolean
function MovableObject:IsSetToDelete() end

--- Shows whether this is mission critical and should therefore NEVER be settled or otherwise destroyed during teh course of a mission.
---@return boolean
function MovableObject:IsMissionCritical() end

--- Indicates whether this MO is an Generic or not.
---@return boolean
function MovableObject:IsGeneric() end

--- Indicates whether this MO is an Actor or not.
---@return boolean
function MovableObject:IsActor() end

--- Indicates whether this MO is a Device or not.
---@return boolean
function MovableObject:IsDevice() end

--- Indicates whether this MO is a HeldDevice or not. LEGACY CRAP
---@return boolean
function MovableObject:IsHeldDevice() end

--- Indicates whether this MO is a ThrownDevice or not.
---@return boolean
function MovableObject:IsThrownDevice() end

---@return boolean
function MovableObject:IsThrownDevice() end

--- Indicates whether this MO is made of Gold or not.
---@return boolean
function MovableObject:IsGold() end

--- Shows whether this is or carries a specifically named object in its inventory. Also looks through the inventories of potential passengers, as applicable.
---@param objectName string The Preset name of the object to look for.
---@return boolean
function MovableObject:HasObject(objectName) end

--- Shows whether this is or carries a specifically grouped object in its inventory. Also looks through the inventories of potential passengers, as applicable.
---@param groupName string
---@return boolean
function MovableObject:HasObjectInGroup(groupName) end

--- Adds force to this MovableObject for the next time Update() is called. applied relative to the center of this MovableObject.
---@param force Vector An Vector with the external force vector that will be added to this
---@param offset? Vector ) A Vector with the offset, in METERS, of where the force is being
function MovableObject:AddForce(force, offset) end

--- Adds force to this MovableObject for the next time Update() is called. A Vector with the absolute world coordinates, in PIXELS, of where the force is being applied to the center of this MovableObject.
---@param force Vector An Vector with the external force vector that will be added to this
---@param absPos Vector MovableObject and affect its path next Update(). In N or kg * m/s^2.
function MovableObject:AddAbsForce(force, absPos) end

--- Adds impulse force (or instant momentum) to this MovableObject for the next time Update() is called. to this MovableObject's momentum  next Update(). In kg * m/s. applied relative to the center of this MovableObject.
---@param impulse Vector An Vector with the impulse force vector that will directly be added
---@param offset? Vector A Vector with the offset, in METERS, of where the impulse is being (default: Vector())
function MovableObject:AddImpulseForce(impulse, offset) end

--- Adds impulse force (or instant momentum) to this MovableObject for the next time Update() is called. to this MovableObject's momentum  next Update(). In kg * m/s. force is being applied to the center of this MovableObject.
---@param impulse Vector An Vector with the impulse force vector that will directly be added
---@param absPos Vector A Vector with the absolute world coordinates, in PIXELS, of where the
function MovableObject:AddAbsImpulseForce(impulse, absPos) end

--- Clears out all the forces this MO has accumulated during this frame.
function MovableObject:ClearForces() end

--- Clears out all the impulses this MO has accumulated during this frame.
function MovableObject:ClearImpulseForces() end

--- Returns the number of Forces vectors to apply.
---@return number
function MovableObject:GetForcesCount() end

--- Returns force vector in newtons of the specified Force record.
---@param n number Force record index to get data from.
---@return Vector
function MovableObject:GetForceVector(n) end

--- Returns offset vector in METERS (not pixels) of the specified Force record.
---@param n number Force record index to get data from.
---@return Vector
function MovableObject:GetForceOffset(n) end

--- Sets force vector in newtons of the specified Force record.
---@param n number Force record index to get data from. New Vector force value in newtons.
---@param v Vector
function MovableObject:SetForceVector(n, v) end

--- Sets offset vector in METERS (not pixels) of the specified Force record.
---@param n number Force record index to get data from. New Vector offset value in meters.
---@param v Vector
function MovableObject:SetForceOffset(n, v) end

--- Returns the number of ImpulseForces vectors to apply.
---@return number
function MovableObject:GetImpulsesCount() end

--- Returns Impulse vector in newtons of the specified Impulse record.
---@param n number Impulse record index to get data from.
---@return Vector
function MovableObject:GetImpulseVector(n) end

--- Returns offset vector in METERS (not pixels) of the specified Impulse record.
---@param n number Impulse record index to get data from.
---@return Vector
function MovableObject:GetImpulseOffset(n) end

--- Returns offset vector in METERS (not pixels) of the specified Impulse record.
---@param n number Impulse record index to get data from.
---@param v Vector
function MovableObject:SetImpulseVector(n, v) end

--- Sets offset vector in METERS (not pixels) of the specified Impulse record.
---@param n number Impulse record index to get data from. New Vector offset value in meters.
---@param v Vector
function MovableObject:SetImpulseOffset(n, v) end

--- Does the calculations necessary to detect whether this MovableObject is at rest or not. IsAtRest() retrieves the answer.
function MovableObject:RestDetection() end

--- Forces this MovableObject out of resting conditions.
function MovableObject:NotResting() end

--- Indicates whether this MovableObject has been at rest with no movement for longer than its RestThreshold.
---@return boolean
function MovableObject:IsAtRest() end

--- Checks whether any of the Atom:s in this MovableObject are on top of terrain pixels, and if so, attempt to move this out so none of this' Atoms are on top of the terrain any more. intersections. even if there wasn't any intersections to begin with.
---@param strongerThan? number Only consider materials stronger than this in the terrain for (default: g_MaterialAir)
---@return boolean
function MovableObject:MoveOutOfTerrain(strongerThan) end

--- Rotates a vector offset from this MORotating's position according to the rotate angle and flipping.
---@param offset Vector A const reference the offset Vector to rotate.
---@return Vector
function MovableObject:RotateOffset(offset) end

---@param message string
function MovableObject:SendMessage(message) end

---@param message string
---@param context any
function MovableObject:SendMessage(message, context) end

--- Requests a synced update for the MO this frame.
function MovableObject:RequestSyncedUpdate() end

--- Sets both strengths of this MovableObject's effect.
---@param strength number The new strengths of the effect, 0.0-1.0.
function MovableObject:SetEffectStrength(strength) end

--- Gets the file path of this MovableObject's current screen effect.
---@return string
function MovableObject:GetScreenEffectPath() end

--- Gets the file path of this MovableObject's current screen effect.
---@param pathToFile string A string containing the file path of the new screen effect.
function MovableObject:SetScreenEffectPath(pathToFile) end

--- A particle MO that creates and emits particle MOs.
---@class PEmitter : MOSParticle
---@field ClassName string The C++ class name, like "PEmitter".
---@field BurstScale number Gets the scale factor that will be applied to the regular spread and emission velocity to get the burst particle parameters.
---@field EmitAngle number Gets the angle of direction that the emitted particles will be shot at.
---@field GetThrottle number Gets the normalized throttle scalar which controls how to affect the emission rate as per the emisison rate range. Depricated for Lua, use the Throttle property instead. 0 means normal, -1.0 means least emission rate.
---@field Throttle number Gets the normalized throttle scalar which controls how to affect the emission rate as per the emisison rate range. Depricated for Lua, use the Throttle property instead. 0 means normal, -1.0 means least emission rate.
---@field ThrottleFactor number Gets the adjusted throttle multiplier that is factored into the emission rate of this PEmitter.
---@field BurstSpacing number Gets the BurstSpacing for this emitter.
---@field EmitCountLimit number Gets the number of emissions left before emitter is disabled.
---@field FlashScale number Gets the display scale factor of the flash effect. This is purely visual.
---@field Emissions Emission[]
PEmitter = {}

--- Makes a copy of this PEmitter, which you own until you add it to the game.
---@return PEmitter
function PEmitter:Clone() end

--- Indicates whether this PEmitter is currently enabled and emitting.
---@return boolean
function PEmitter:IsEmitting() end

--- Returns whether this emitter was emitting last frame.
---@return boolean
function PEmitter:WasEmitting() end

--- Sets this PEmitter to start emitting at the set rate, or to stop.
---@param enable? boolean Whether to enable or disable emission. (default: true)
function PEmitter:EnableEmission(enable) end

--- A vector in the direction, including the rotation of the emitter, that the emitted particles will be shot at.
---@return Vector
function PEmitter:GetEmitVector() end

--- A vector in the opposite direction, including the rotation of the emitter, that the emitted particles will be shot at.
---@return Vector
function PEmitter:GetRecoilVector() end

--- Calculates the forces this emitter applies on any parent.
---@param burst? boolean Whether to calculate a burst update or not. (default: false)
---@return number
function PEmitter:EstimateImpulse(burst) end

--- Triggers a one-shot burst of emissions in the number that has previously been set. The burst will happen during the next Update of this PEmitter.
function PEmitter:TriggerBurst() end

--- Indicates whether this PEmitter is set to burst next update or not.
---@return boolean
function PEmitter:IsSetToBurst() end

--- Checks if it is possible to trigger a one-shot burst of emissions during the next Update of this PEmitter.
---@return boolean
function PEmitter:CanTriggerBurst() end

--- Returns whether this emitter just started emitting this frame.
---@return boolean
function PEmitter:JustStartedEmitting() end

--- An individual PieSlice in a PieMenu.
---@class PieSlice : Entity
---@field ClassName string The C++ class name, like "PieSlice".
---@field Type number
---@field Direction Directions Gets the Direction of this PieSlice.
---@field CanBeMiddleSlice boolean Gets whether or not this PieSlice can be the middle PieSlice of a PieQuadrant.
---@field OriginalSource Entity Gets the original Entity source of this PieSlice, if there is one.
---@field Enabled boolean Gets whether or not this PieSlice is enabled.
---@field ScriptPath string Gets the file path of the Lua file this PieSlice should run when activated, if any.
---@field FunctionName string Gets the name of the Lua function to run when this PieSlice is activated.
---@field SubPieMenu PieMenu Gets the sub-PieMenu for this PieSlice if there is one. Ownership is NOT transferred.
---@field DrawFlippedToMatchAbsoluteAngle boolean Gets whether or not this PieSlice should draw itself flipped to match its absolute angle (i.e. its angle accounting for its PieMenu's rotation).
---@field NoType number One of PieSlice.SliceType.
---@field Pickup number One of PieSlice.SliceType.
---@field Drop number One of PieSlice.SliceType.
---@field NextItem number One of PieSlice.SliceType.
---@field PreviousItem number One of PieSlice.SliceType.
---@field Reload number One of PieSlice.SliceType.
---@field BuyMenu number One of PieSlice.SliceType.
---@field Stats number One of PieSlice.SliceType.
---@field Map number One of PieSlice.SliceType.
---@field FormSquad number One of PieSlice.SliceType.
---@field Ceasefire number One of PieSlice.SliceType.
---@field Sentry number One of PieSlice.SliceType.
---@field Patrol number One of PieSlice.SliceType.
---@field BrainHunt number One of PieSlice.SliceType.
---@field GoldDig number One of PieSlice.SliceType.
---@field GoTo number One of PieSlice.SliceType.
---@field Return number One of PieSlice.SliceType.
---@field Stay number One of PieSlice.SliceType.
---@field Deliver number One of PieSlice.SliceType.
---@field Scuttle number One of PieSlice.SliceType.
---@field Done number One of PieSlice.SliceType.
---@field Load number One of PieSlice.SliceType.
---@field Save number One of PieSlice.SliceType.
---@field New number One of PieSlice.SliceType.
---@field Pick number One of PieSlice.SliceType.
---@field Move number One of PieSlice.SliceType.
---@field Remove number One of PieSlice.SliceType.
---@field InFront number One of PieSlice.SliceType.
---@field Behind number One of PieSlice.SliceType.
---@field ZoomIn number One of PieSlice.SliceType.
---@field ZoomOut number One of PieSlice.SliceType.
---@field Team1 number One of PieSlice.SliceType.
---@field Team2 number One of PieSlice.SliceType.
---@field Team3 number One of PieSlice.SliceType.
---@field Team4 number One of PieSlice.SliceType.
PieSlice = {}

--- Makes a copy of this PieSlice, which you own until you add it to the game.
---@return PieSlice
function PieSlice:Clone() end

--- Reloads the the script on this PieSlice.
---@return number
function PieSlice:ReloadScripts() end

--- A PieMenu for managing interactions with objects and Actors.
---@class PieMenu : Entity
---@field ClassName string The C++ class name, like "PieMenu".
---@field Owner Actor Gets the owner Actor of this PieMenu. Ownership is NOT transferred!
---@field Controller Controller Gets the currently in-use Controller for this PieMenu - either the menu Controller if there's one set, or the owning Actor's Controller. Ownership IS NOT transferred!
---@field AffectedObject MovableObject Gets the currently affected MovableObject. Ownership is NOT transferred!
---@field Pos Vector Gets the absolute center position of this PieMenu.
---@field RotAngle number Gets the absolute rotation of this PieMenu in radians.
---@field FullInnerRadius number Gets the full inner radius of this PieMenu.
---@field PieSlices PieSlice[] Gets a const reference to the vector containing pointers to all the PieSlices in this PieMenu.
PieMenu = {}

--- Makes a copy of this PieMenu, which you own until you add it to the game.
---@return PieMenu
function PieMenu:Clone() end

--- Gets whether this PieMenu is a sub-PieMenu, i.e. it's owned by a PieSlice.
---@return boolean
function PieMenu:IsSubPieMenu() end

--- Gets whether or not the PieMenu is enabled or in the process of being enabled, and is not in wobble mode.
---@return boolean
function PieMenu:IsEnabled() end

--- Gets whether or not the PieMenu is in the process of being enabled.
---@return boolean
function PieMenu:IsEnabling() end

--- Gets whether or not the PieMenu is in the process of being disabled.
---@return boolean
function PieMenu:IsDisabling() end

--- Gets whether or not the PieMenu is in the process of enabling or disabling.
---@return boolean
function PieMenu:IsEnablingOrDisabling() end

--- Gets whether or not the PieMenu is at all visible.
---@return boolean
function PieMenu:IsVisible() end

--- Gets whether this PieMenu has an open sub-PieMenu.
---@return boolean
function PieMenu:HasSubPieMenuOpen() end

--- Sets this PieMenu to normal MenuMode.
function PieMenu:SetAnimationModeToNormal() end

--- Plays the disabling animation, regardless of whether the PieMenu was enabled or not.
function PieMenu:DoDisableAnimation() end

--- Plays an animation of the background circle expanding and contracting continuously. The PieMenu is effectively disabled while doing this. This animation will continue until the next call to SetEnabled.
function PieMenu:Wobble() end

--- Makes the background circle freeze at a certain radius until SetEnabled is called. The PieMenu is effectively disabled while doing this.
---@param radius number The radius to make the background circle freeze at.
function PieMenu:FreezeAtRadius(radius) end

---@return number
function PieMenu:GetPieCommand() end

--- Gets the first found PieSlice with the passed in preset name, if there is one. Ownership is NOT transferred!
---@param presetName string The preset name to look for.
---@return PieSlice
function PieMenu:GetFirstPieSliceByPresetName(presetName) end

--- Gets the first found PieSlice with the passed in PieSlice SliceType, if there is one. Ownership is NOT transferred!
---@param pieSliceType number The type of PieSlice to look for.
---@return PieSlice
function PieMenu:GetFirstPieSliceByType(pieSliceType) end

--- Adds a PieSlice to the PieMenu, setting its original source to the specified sliceSource. Ownership IS transferred! The slice will be placed in the appropriate PieQuadrant for its Direction, with Any Direction using the first available PieQuadrant. If allowQuadrantOverflow is true, the PieSlice will be added to the next available PieQuadrant in Direction order. Note that if the slice could not be added, it will be deleted to avoid memory leaks, and the method will return false.
---@param pieSliceToAdd PieSlice The new PieSlice to add. Ownership IS transferred.
---@param pieSliceOriginalSource Entity The source of the added PieSlice. Should be nullptr for slices not added by Entities.
---@param allowQuadrantOverflow? boolean Whether the new PieSlice can be placed in PieQuadrants other than the one specified by its Direction, if that PieQuadrant is full.
---@return boolean
function PieMenu:AddPieSlice(pieSliceToAdd, pieSliceOriginalSource, allowQuadrantOverflow) end

---@param pieSliceToAdd PieSlice
---@param pieSliceOriginalSource Entity
---@return boolean
function PieMenu:AddPieSlice(pieSliceToAdd, pieSliceOriginalSource) end

--- Adds a PieSlice to the PieMenu, with the same conditions as AddPieSlice above, but only if no PieSlice exists in this PieMenu with the same PresetName (optionally with the same original source). Ownership IS transferred!
---@param pieSliceToAdd PieSlice The new PieSlice to add. Ownership IS transferred.
---@param pieSliceOriginalSource Entity The source of the added PieSlice. Should be nullptr for slices not added by Entities.
---@param onlyCheckPieSlicesWithSameOriginalSource? boolean Whether all PieSlices in the PieMenu should be checked to see if there are no duplicates, or only those with the same original source.
---@param allowQuadrantOverflow? boolean Whether the new PieSlice can be placed in PieQuadrants other than the one specified by its Direction, if that PieQuadrant is full.
---@return boolean
function PieMenu:AddPieSliceIfPresetNameIsUnique(pieSliceToAdd, pieSliceOriginalSource, onlyCheckPieSlicesWithSameOriginalSource, allowQuadrantOverflow) end

---@param pieSliceToAdd PieSlice
---@param pieSliceOriginalSource Entity
---@return boolean
function PieMenu:AddPieSliceIfPresetNameIsUnique(pieSliceToAdd, pieSliceOriginalSource) end

---@param pieSliceToAdd PieSlice
---@param pieSliceOriginalSource Entity
---@param onlyCheckPieSlicesWithSameOriginalSource boolean
---@return boolean
function PieMenu:AddPieSliceIfPresetNameIsUnique(pieSliceToAdd, pieSliceOriginalSource, onlyCheckPieSlicesWithSameOriginalSource) end

--- Removes and returns the passed in PieSlice from this PieMenu if it's in the PieMenu. Ownership IS transferred to the caller!
---@param pieSliceToRemove PieSlice The PieSlice to remove from this PieMenu. Ownership IS transferred to the caller!
---@return PieSlice
function PieMenu:RemovePieSlice(pieSliceToRemove) end

--- Removes any PieSlices in this PieMenu whose preset name matches the passed in preset name.
---@param presetNameToRemoveBy string The preset name to check against.
---@return boolean
function PieMenu:RemovePieSlicesByPresetName(presetNameToRemoveBy) end

--- Removes any PieSlices in this PieMenu whose PieSlice SliceType matches the passed in PieSlice SliceType.
---@param pieSliceTypeToRemoveBy number The PieSlice SliceType to check against.
---@return boolean
function PieMenu:RemovePieSlicesByType(pieSliceTypeToRemoveBy) end

--- Removes any PieSlices in this PieMenu whose original source matches the passed in Entity.
---@param originalSource Entity The original source whose PieSlices should be removed.
---@return boolean
function PieMenu:RemovePieSlicesByOriginalSource(originalSource) end

--- Replaces the first PieSlice with the second, ensuring original source, direction, middle slice eligibility, angles and slot count are maintained. The existing PieSlice is returned, and ownership IS transferred both ways!
---@param pieSliceToReplace PieSlice The PieSlice that will be replaced.
---@param replacementPieSlice PieSlice The PieSlice that will replace the existing one. If this is nullptr, the existing one will just be removed.
---@return PieSlice
function PieMenu:ReplacePieSlice(pieSliceToReplace, replacementPieSlice) end

--- A round containing a number of projectile particles and one shell.
---@class Round : Entity
---@field ClassName string The C++ class name, like "Round".
---@field NextParticle MovableObject Gets the next particle contained in this Round. Ownership is NOT transferred!
---@field Shell MovableObject Gets the shell casing preset of this Round. Ownership IS NOT transferred!
---@field FireVel number Gets the velocity at which this round is to be fired.
---@field InheritsFirerVelocity boolean Gets whether or not this Round should inherit velocity from its firer.
---@field ShellVel number Gets the maximum velocity at which this round's shell is to be ejected.
---@field Separation number Gets the separation of particles in this round.
---@field ParticleCount number Returns how many particles are contained within this Round, not counting the shell.
---@field AILifeTime number Returns the lifetime of the projectile used by the AI when executing the shooting scripts.
---@field AIFireVel number Returns the FireVelocity of the projectile used by the AI when executing the shooting scripts.
---@field IsEmpty boolean Returns whether this Round is out of particles or not.
Round = {}

--- Makes a copy of this Round, which you own until you add it to the game.
---@return Round
function Round:Clone() end

--- Contains everything that defines a complete scene.
---@class Scene : Entity
---@field ClassName string The C++ class name, like "Scene".
---@field Location Vector Gets the specified location of this scene on the planet view
---@field Dimensions Vector Gets the total dimensions (width and height) of the scene, in pixels.
---@field Width number Gets the total width of the scene, in pixels.
---@field Height number Gets the total height of the scene, in pixels.
---@field WrapsX boolean Indicates whether the scene wraps its scrolling around the X axis.
---@field WrapsY boolean Indicates whether the scene wraps its scrolling around the Y axis.
---@field TeamOwnership number Shows which team owns this Scene in a Metagame, if any
---@field GlobalAcc Vector Gets the global acceleration (in m/s^2) that is applied to all movable objects' velocities during every frame. Typically models gravity.
---@field ScenePathSize number Gets how many waypoints there are in the ScenePath currently
---@field Deployments Deployment[]
---@field BackgroundLayers SLBackground[]
---@field Areas Area[]
---@field PLACEONLOAD number One of Scene.PlacedObjectSets.
---@field BLUEPRINT number One of Scene.PlacedObjectSets.
---@field AIPLAN number One of Scene.PlacedObjectSets.
---@field PLACEDSETSCOUNT number One of Scene.PlacedObjectSets.
Scene = {}

--- Makes a copy of this Scene, which you own until you add it to the game.
---@return Scene
function Scene:Clone() end

---@return Vector[]
function Scene:GetScenePath() end

--- Shows how much gold this Scene is budgeted to be built for this round.
---@param player number Which player's set budget to show.
---@return number
function Scene:GetBuildBudget(player) end

--- Sets how much gold this Scene is budgeted to be built for this round.
---@param player number The player whom is setting the budget.
---@param budget number The budget in oz that this is allocated to have built for this round.
function Scene:SetBuildBudget(player, budget) end

--- Tells whether a specific team has scheduled an orbital Scan of this.
---@param team number The team to check for.
---@return boolean
function Scene:IsScanScheduled(team) end

--- Sets this to be orbitally scanned by a specific team on next load.
---@param team number The team to schedule the scan for.
---@param scan? boolean Whether to actually schedule the scan or clear it. (default: true)
function Scene:SetScheduledScan(team, scan) end

--- Removes all entries in a specific set of placed Objects.
---@param whichSet number Which set of placed objects to clear. See the PlacedObjectSets enum.
---@param weHaveOwnership? boolean Whether or not we have ownership of these items, and should delete them. (default: true)
---@return number
function Scene:ClearPlacedObjectSet(whichSet, weHaveOwnership) end

--- Places the individual brain of a single player which may be stationed on this Scene, and registers them as such in an Activity.
---@param player number The player's brain to place.
---@param newActivity Activity The Activity to register the placed brains with. OWNERSHIP IS NOT TRANSFERRED!
---@return boolean
function Scene:PlaceResidentBrain(player, newActivity) end

--- Places the individual brains of the various players which may be stationed on this Scene, and registers them as such in an Activity.
---@param newActivity Activity The Activity to register the placed brains with. OWNERSHIP IS NOT TRANSFERRED!
---@return number
function Scene:PlaceResidentBrains(newActivity) end

--- Looks at the Activity and its players' registered brain Actors, and saves them as resident brains for this Scene. Done when a fight is over and the survivors remain!
---@param oldActivity Activity The Activity to check for registered brains. OWNERSHIP IS NOT TRANSFERRED!
---@return number
function Scene:RetrieveResidentBrains(oldActivity) end

--- Gets the resident brain Actor of a specific player from this scene, if there is any. OWNERSHIP IS NOT TRANSFERRED!
---@param player number Which player to get the resident brain of.
---@return SceneObject
function Scene:GetResidentBrain(player) end

--- Sets the resident brain Actor of a specific player from this scene, if there is any. Ownership IS transferred!
---@param player number Which player to set the resident brain of.
---@param pNewBrain SceneObject The Actor to set as the resident brain of the specified player.
function Scene:SetResidentBrain(player, pNewBrain) end

--- Adds one or modifies an existing area of this Scene. NOT transferred!
---@param newArea Area The area to add or modify of the same name in this Scene. Ownership is
---@return boolean
function Scene:SetArea(newArea) end

--- Checks for the existence of a specific Area identified by a name. This won't throw any errors to the console if the Area isn't found.
---@param areaName string The name of the Area to try to find in this Scene.
---@return boolean
function Scene:HasArea(areaName) end

--- Gets a specified Area identified by name. Ownership is NOT transferred!
---@param areaName string The name of the Area to try to get.
---@return Area
function Scene:GetArea(areaName) end

--- Gets a specified Area identified by name, showing a Lua warning if it's not found. Ownership is NOT transferred! Using this function will not add the area to the list of required areas which Scenario GUI uses to show compatible areas.
---@param areaName string The name of the Area to try to get.
---@return Area
function Scene:GetOptionalArea(areaName) end

--- Checks if a point is within a specific named Area of this Scene. If no Area of the name is found, this just returns false without error.
---@param areaName string The name of the Area to try to check against.
---@param point Vector The point to see if it's within the specified Area.
---@return boolean
function Scene:WithinArea(areaName, point) end

---@param areaName string
function Scene:AddNavigatableArea(areaName) end

---@param areaName string
function Scene:ClearNavigatableAreas(areaName) end

--- Recalculates all of the pathfinding data. This is very expensive, so do very rarely!
function Scene:ResetPathFinding() end

--- Recalculates only the areas of the pathfinding data that have been marked as outdated.
function Scene:UpdatePathFinding() end

--- Tells whether the pathfinding data has been updated in the last frame.
---@return boolean
function Scene:PathFindingUpdated() end

---@param start Vector
---@param end Vector
---@param movePathToGround boolean
---@param digStrength number
---@return number
function Scene:CalculatePath(start, end, movePathToGround, digStrength) end

---@param start Vector
---@param end Vector
---@param movePathToGround boolean
---@param digStrength number
---@param team number
---@return number
function Scene:CalculatePath(start, end, movePathToGround, digStrength, team) end

---@param callback any
---@param start Vector
---@param end Vector
---@param movePathToGround boolean
---@param digStrength number
function Scene:CalculatePathAsync(callback, start, end, movePathToGround, digStrength) end

---@param callback any
---@param start Vector
---@param end Vector
---@param movePathToGround boolean
---@param digStrength number
---@param team number
function Scene:CalculatePathAsync(callback, start, end, movePathToGround, digStrength, team) end

--- Something to bundle the properties of scene areas together
---@class Area
---@field ClassName string
---@field Name string Gets the name of the Area
---@field FirstBox Box Gets the first Box in this Area.
---@field Center Vector Gets a center point for this of all the boxes waeighted by their sizes. pulled apart by two separate boxes, for example. 0,0 if this has no Area
---@field RandomPoint Vector Gets a random coordinate contained within any of this' Box:es.
---@field Boxes Box[]
Area = {}

--- Resets the entire Serializable, including its inherited members, to their default settings or values.
function Area:Reset() end

--- Adds a Box to this' area coverage.
---@param newBox Box The Box to add. A copy will be made and added.
---@return boolean
function Area:AddBox(newBox) end

--- Removes the first Box in the Area that has the same Corner, Width and Height of the passed-in Box.
---@param boxToRemove Box A Box whose values are used to determine what Box to remove.
---@return boolean
function Area:RemoveBox(boxToRemove) end

--- Shows whether this really has no Area at all, ie it doesn't have any Box:es with both width and height.
---@return boolean
function Area:HasNoArea() end

--- Shows whether a point is anywhere inside this Area's coverage.
---@param point Vector The point to check if it's inside the Area, in absolute scene coordinates.
---@return boolean
function Area:IsInside(point) end

--- Shows whether a coordinate is anywhere inside this Area's coverage, in the X-axis only.
---@param pointX number The x coord to check if it's inside the Area, in absolute scene units.
---@return boolean
function Area:IsInsideX(pointX) end

--- Shows whether a coordinate is anywhere inside this Area's coverage, in the Y-axis only.
---@param pointY number The x coord to check if it's inside the Area, in absolute scene units.
---@return boolean
function Area:IsInsideY(pointY) end

--- Gets the first Box encountered in this that contains a specific point. none was found. OWNERSHIP IS NOT TRANSFERRED!
---@param point Vector The point to check for Box collision, in absolute scene coordinates.
---@return Box
function Area:GetBoxInside(point) end

--- Removes the first Box encountered in this that contains a specific point.
---@param point Vector The point to check for Box collision, in absolute scene coordinates.
---@return Box
function Area:RemoveBoxInside(point) end

--- Gets a center point for this of all the boxes waeighted by their sizes. pulled apart by two separate boxes, for example. 0,0 if this has no Area
---@return Vector
function Area:GetCenterPoint() end

--- Gets a random coordinate contained within any of this' Box:es.
---@return Vector
function Area:GetRandomPoint() end

---@class SceneLayer : Entity
---@field ClassName string The C++ class name, like "SceneLayer".
SceneLayer = {}

--- The base class shared by Both TerrainObject:s and MovableObject:s, ie anything that can be places in a scene.
---@class SceneObject : Entity
---@field ClassName string The C++ class name, like "SceneObject".
---@field Pos Vector Gets the absolute position of this SceneObject.
---@field HFlipped boolean Returns whether this is being drawn flipped horizontally (around the vertical axis), or not.
---@field RotAngle number Gets the current rotational angle of of this, in radians.
---@field Team number Gets which team this is to be assigned to when placed.
---@field PlacedByPlayer number Gets which player placed this object in the scene, if any.
---@field Buyable boolean Shows whether this should appear in teh buy menus at all.
---@field BuyableMode number
---@field NORESTRICTIONS number One of SceneObject.BuyableMode.
---@field BUYMENUONLY number One of SceneObject.BuyableMode.
---@field OBJECTPICKERONLY number One of SceneObject.BuyableMode.
---@field SCRIPTONLY number One of SceneObject.BuyableMode.
SceneObject = {}

--- Indicates whether this' current graphical representation overlaps a point in absolute scene coordinates.
---@param scenePoint Vector The point in absolute scene coordinates.
---@return boolean
function SceneObject:IsOnScenePoint(scenePoint) end

--- DOES THE SAME THING AS GetGoldValue, USED ONLY TO PRESERVE LUA COMPATIBILITY
---@param nativeModule number
---@param foreignMult number
---@return number
function SceneObject:GetGoldValue(nativeModule, foreignMult) end

---@param nativeModule? number If this is supposed to be adjusted for a specific Tech's subjective (default: 0)
---@param foreignMult? number How much to multiply the value if this happens to be a foreign Tech. (default: 1.0)
---@param nativeMult? number How much to multiply the value if this happens to be a native Tech. (default: 1.0)
---@return number
function SceneObject:GetGoldValue(nativeModule, foreignMult, nativeMult) end

--- Sets the cost to purchase this item, in oz's of gold.
---@param value number The cost, in oz of gold.
function SceneObject:SetGoldValue(value) end

--- Gets a descriptive string describing the cost to purchase this item, in oz's of gold. value, then pass in the native DataModule ID of that tech. 0 means no Tech is specified and the base value is returned.
---@param nativeModule? number If this is supposed to be adjusted for a specific Tech's subjective (default: 0)
---@param foreignMult? number How much to multiply the value if this happens to be a foreign Tech. (default: 1.0)
---@param nativeMult? number
---@return string
function SceneObject:GetGoldValueString(nativeModule, foreignMult, nativeMult) end

--- Gets the total liquidation value of this, including everything inside. value, then pass in the native DataModule ID of that tech. 0 means no Tech is specified and the base value is returned.
---@param nativeModule? number If this is supposed to be adjusted for a specific Tech's subjective (default: 0)
---@param foreignMult? number How much to multiply the value if this happens to be a foreign Tech. (default: 1.0)
---@param nativeMult? number
---@return number
function SceneObject:GetTotalValue(nativeModule, foreignMult, nativeMult) end

---@param nativeModule number
---@param foreignMult number
---@return number
function SceneObject:GetTotalValue(nativeModule, foreignMult) end

--- A scrolling background layer of the Scene, placed behind the terrain.
---@class SLBackground : SceneLayer
---@field ClassName string The C++ class name, like "SLBackground".
---@field Frame number Gets the frame number of this SLBackground that is currently set to be drawn.
---@field SpriteAnimMode number Gets the animation mode of this SLBackground.
---@field SpriteAnimDuration number Gets the time it takes to complete a full animation cycle of this SLBackground.
---@field IsAnimatedManually boolean Gets whether this SLBackground's animation is handled manually/externally.
---@field AutoScrollX boolean Gets whether auto-scrolling is enabled on the X axis.
---@field AutoScrollY boolean Gets whether auto-scrolling is enabled on the Y axis.
---@field AutoScrollInterval number Gets the duration between auto-scroll steps.
---@field AutoScrollStep Vector Gets the auto-scroll step (pixels to advance per interval) values.
---@field AutoScrollStepX number Gets the auto-scroll step (pixels to advance per interval) value on the X axis.
---@field AutoScrollStepY number Gets the auto-scroll step (pixels to advance per interval) value on the Y axis.
SLBackground = {}

--- Gets whether this SLBackground has auto-scrolling enabled and meets the requirements to actually auto-scroll.
---@return boolean
function SLBackground:IsAutoScrolling() end

--- A container for sounds that represent a specific sound effect.
---@class SoundContainer : Entity
---@field ClassName string The C++ class name, like "SoundContainer".
---@field SoundOverlapMode number Gets the SoundOverlapMode of this SoundContainer, which is used to determine how it should behave when it's told to play while already playing.
---@field BusRouting number Gets the bus this sound routes to.
---@field Immobile boolean Gets whether the sounds in this SoundContainer should be considered immobile, i.e. always play at the listener's position.
---@field AttenuationStartDistance number Gets the attenuation start distance of this SoundContainer.
---@field CustomPanValue number Gets the custom pan value of this SoundContainer.
---@field PanningStrengthMultiplier number Gets the panning strength multiplier of this SoundContainer.
---@field Loops number Gets the looping setting of this SoundContainer.
---@field Priority number Gets the current playback priority.
---@field AffectedByGlobalPitch boolean Gets whether the sounds in this SoundContainer are affected by global pitch changes or not.
---@field Pos Vector Gets the position at which this SoundContainer's sound will be played. Note that its individual sounds can be offset from this.
---@field Volume number Gets the volume the sounds in this SoundContainer are played at. Note that this does not factor volume changes due to the SoundContainer's position.
---@field Pitch number Gets the pitch the sounds in this SoundContainer are played at. Note that this does not factor in global pitch.
---@field PitchVariation number Gets the pitch variation the sounds in this SoundContainer are played at.
---@field SFX number One of SoundContainer.BusRouting.
---@field UI number One of SoundContainer.BusRouting.
---@field MUSIC number One of SoundContainer.BusRouting.
---@field OVERLAP number One of SoundContainer.SoundOverlapMode.
---@field RESTART number One of SoundContainer.SoundOverlapMode.
---@field IGNORE_PLAY number One of SoundContainer.SoundOverlapMode.
SoundContainer = {}

--- Makes a copy of this SoundContainer, which you own until you add it to the game.
---@return SoundContainer
function SoundContainer:Clone() end

--- Shows whether this SoundContainer's top level SoundSet has any SoundData or SoundSets.
---@return boolean
function SoundContainer:HasAnySounds() end

--- Gets a reference to the top level SoundSet of this SoundContainer, to which all SoundData and sub SoundSets belong.
---@return SoundSet
function SoundContainer:GetTopLevelSoundSet() end

--- Copies the passed in SoundSet reference into the top level SoundSet of this SoundContainer, effectively making that the new top level SoundSet.
---@param newTopLevelSoundSet SoundSet A reference to the new top level SoundSet for this SoundContainer.
function SoundContainer:SetTopLevelSoundSet(newTopLevelSoundSet) end

--- Indicates whether any sound in this SoundContainer is currently being played.
---@return boolean
function SoundContainer:IsBeingPlayed() end

--- Plays the next sound of this SoundContainer at its current position for all players.
---@return boolean
function SoundContainer:Play() end

---@param player number The player to start playback of this SoundContainer's sounds for.
---@return boolean
function SoundContainer:Play(player) end

---@param player Vector The player to start playback of this SoundContainer's sounds for.
---@return boolean
function SoundContainer:Play(player) end

---@param position Vector The position at which to play the SoundContainer's sounds.
---@param player number The player to start playback of this SoundContainer's sounds for.
---@return boolean
function SoundContainer:Play(position, player) end

--- Stops playback of this SoundContainer for all players.
---@return boolean
function SoundContainer:Stop() end

---@param player number Player to stop playback of this SoundContainer for.
---@return boolean
function SoundContainer:Stop(player) end

--- Restarts playback of this SoundContainer for all players.
---@return boolean
function SoundContainer:Restart() end

---@param player number Player to restart playback of this SoundContainer for.
---@return boolean
function SoundContainer:Restart(player) end

--- Fades out playback of the SoundContainer to 0 volume.
---@param fadeOutTime? number How long the fadeout should take.
function SoundContainer:FadeOut(fadeOutTime) end

--- A set of sounds, and their selection data.
---@class SoundSet
---@field ClassName string The C++ class name, like "SoundSet".
---@field SoundSelectionCycleMode number Gets the current SoundSelectionCycleMode for this SoundSet, which is used to determine what SoundSet to select next time SelectNextSounds is called.
---@field SubSoundSets SoundSet[] The sub SoundSets available for selection in this SoundSet.
---@field RANDOM number One of SoundSet.SoundSelectionCycleMode.
---@field FORWARDS number One of SoundSet.SoundSelectionCycleMode.
---@field ALL number One of SoundSet.SoundSelectionCycleMode.
SoundSet = {}

--- Shows whether this SoundSet has any sounds in it or, optionally its SubSoundSets.
---@param includeSubSoundSets? boolean
---@return boolean
function SoundSet:HasAnySounds(includeSubSoundSets) end

--- Selects the next sounds of this SoundSet to be played, also selecting them for sub SoundSets as appropriate.
---@return boolean
function SoundSet:SelectNextSounds() end

--- Adds a new sound to this SoundSet, spitting out a Lua error if it fails. The sound will have default configuration.
---@param soundFilePath string A path to the new sound to add. This will be handled through PresetMan.
function SoundSet:AddSound(soundFilePath) end

---@param soundFilePath string A path to the new sound to add. This will be handled through PresetMan.
---@param offset Vector The offset position to play this sound at, where (0, 0) is no offset.
---@param minimumAudibleDistance number The minimum distance at which this sound will be audible. 0 means there is none, which is normally the case.
---@param attenuationStartDistance number The attenuation start distance for this sound, -1 sets it to default.
function SoundSet:AddSound(soundFilePath, offset, minimumAudibleDistance, attenuationStartDistance) end

--- Removes all instances of the sound with the given filepath from this SoundSet. Does not remove it from any sub-SoundSets.
---@param soundFilePath string The path to the sound to be removed from this SoundSet.
---@return boolean
function SoundSet:RemoveSound(soundFilePath) end

---@param soundFilePath string The path to the sound to be removed from this SoundSet.
---@param removeFromSubSoundSets boolean Whether or not to remove the sound from any sub-SoundSets as well as this SoundSet.
---@return boolean
function SoundSet:RemoveSound(soundFilePath, removeFromSubSoundSets) end

--- Adds a copy of the passed in SoundSet as a sub SoundSet of this SoundSet. Ownership IS transferred!
---@param soundSetToAdd SoundSet A reference to the SoundSet to be copied in as a sub SoundSet of this SoundSet. Ownership IS transferred!
function SoundSet:AddSoundSet(soundSetToAdd) end

--- A thrown device that explodes after its trigger delay is completed after its activation.
---@class TDExplosive : ThrownDevice
---@field ClassName string The C++ class name, like "TDExplosive".
---@field IsAnimatedManually boolean If true, then the frame will not be changed automatically during update
TDExplosive = {}

--- Makes a copy of this TDExplosive, which you own until you add it to the game.
---@return TDExplosive
function TDExplosive:Clone() end

--- A feature of the terrain, which includes foreground color layer, material layer and optional background layer.
---@class TerrainObject : SceneObject
---@field ClassName string The C++ class name, like "TerrainObject".
TerrainObject = {}

--- Makes a copy of this TerrainObject, which you own until you add it to the game.
---@return TerrainObject
function TerrainObject:Clone() end

--- Gets the offset from the position to the upper left corner of this TerrainObject's BITMAPs.
---@return Vector
function TerrainObject:GetBitmapOffset() end

--- Gets the width of the widest BITMAP of this TerrainObject's layers.
---@return number
function TerrainObject:GetBitmapWidth() end

--- Gets the height of the highest BITMAP of this TerrainObject's layers.
---@return number
function TerrainObject:GetBitmapHeight() end

--- A device that is carried and thrown by Actors.
---@class ThrownDevice : HeldDevice
---@field ClassName string The C++ class name, like "ThrownDevice".
---@field MinThrowVel number Gets the minimum throw velocity of this when thrown.
---@field MaxThrowVel number Gets the maximum throw velocity of this when thrown.
---@field StartThrowOffset Vector Gets the start throw offset of this ThrownDevice's joint relative from the parent Actor's position, if attached.
---@field EndThrowOffset Vector Gets the end throw offset of this ThrownDevice's joint relative from the parent Actor's position, if attached.
ThrownDevice = {}

--- Makes a copy of this ThrownDevice, which you own until you add it to the game.
---@return ThrownDevice
function ThrownDevice:Clone() end

--- Ugly method to deal with lua AI bullshit, by pulling the max throwvel calculation based on arm strength into here. If throw velocity is decided by the Arm and not by the ThrownDevice, then the mass of the ThrownDevice and the angular velocity of the root parent Actor will be taken into account.
---@return number
function ThrownDevice:GetCalculatedMaxThrowVelIncludingArmThrowStrength() end

--- An Attachable Turret pod that can hold HeldDevices.
---@class Turret : Attachable
---@field ClassName string The C++ class name, like "Turret".
---@field MountedDevice HeldDevice Gets the first mounted HeldDevice of this Turret, mostly here for Lua convenience.
---@field MountedDeviceRotationOffset number Gets the current rotational offset of the mounted HeldDevice from the rest of the Turret.
Turret = {}

--- Makes a copy of this Turret, which you own until you add it to the game.
---@return Turret
function Turret:Clone() end

--- Gets the vector of mounted HeldDevices for this Turret.
---@return HeldDevice[]
function Turret:GetMountedDevices() end

--- Adds a HeldDevice to be mounted on this Turret. Ownership IS transferred! Will not remove any other HeldDevices mounted on this Turret.
---@param newMountedDevice HeldDevice The new HeldDevice to be mounted on this Turret.
function Turret:AddMountedDevice(newMountedDevice) end

---@param newMountedDevice HDFirearm
function Turret:AddMountedDevice(newMountedDevice) end

--- A class to handle the drawing of LARGE text banners that fly across the screen, grabbing the player's attention.
---@class GUIBanner
---@field ClassName string The C++ class name, like "GUIBanner".
---@field BannerText string Gets the currently displayed text string.
---@field AnimState number Gets the current state of the overall animation of this banner.
---@field Kerning number Get the character kerning (spacing) between chars, 0 = chars are touching.
---@field BLINKING number One of GUIBanner.AnimMode.
---@field FLYBYLEFTWARD number One of GUIBanner.AnimMode.
---@field FLYBYRIGHTWARD number One of GUIBanner.AnimMode.
---@field ANIMMODECOUNT number One of GUIBanner.AnimMode.
---@field NOTSTARTED number One of GUIBanner.AnimState.
---@field SHOWING number One of GUIBanner.AnimState.
---@field SHOW number One of GUIBanner.AnimState.
---@field HIDING number One of GUIBanner.AnimState.
---@field OVER number One of GUIBanner.AnimState.
---@field ANIMSTATECOUNT number One of GUIBanner.AnimState.
---@field RED number One of GUIBanner.BannerColor.
---@field YELLOW number One of GUIBanner.BannerColor.
GUIBanner = {}

--- Tells whether this banner is currently showing anything on screen.
---@return boolean
function GUIBanner:IsVisible() end

--- Starts the display animation of a text string in this banner's font. This only needs to be called once per display animation. Any currently played animation will be interrupted and the banner restarted. means the text pauses at the display/center until HideText is called. value. 0.5 = banner midline is centered on halfway down the target.
---@param text string The text to display.
---@param mode number The animation mode to display the text in.
---@param duration number The duration of the animation the text is displayed in. Negative value
---@param targetSize Vector The width and height of the bitmap target this will be displayed on.
---@param yOnTarget number The Y position the banner should appear on the target, in normalized
---@param flySpeed? number The speed at which the characters will fly, in pixels per second. (default: 1500)
---@param flySpacing? number The spacing between the flying characters, in pixels. (default: 100)
function GUIBanner:ShowText(text, mode, duration, targetSize, yOnTarget, flySpeed, flySpacing) end

--- Tells the banner to animate away elegantly. Especially useful when a ShowText is waiting with a negative duration.
---@param flySpeed? number The speed at which the characters will fly, in pixels per second. (default: 1500)
---@param flySpacing? number The spacing between the flying characters, in pixels. (default: 100)
function GUIBanner:HideText(flySpeed, flySpacing) end

--- Abruptly clears any text without animating it away. Resets this thing.
function GUIBanner:ClearText() end

--- A full menu system that represents a purchasing GUI for Cortex Command
---@class BuyMenuGUI
---@field ClassName string The C++ class name, like "BuyMenuGUI".
---@field ShowOnlyOwnedItems boolean Returns whether only owned items will be shown in buy menu. Overriden by AlwaysAllowed list.
---@field EnforceMaxPassengersConstraint boolean Tells whether passenger count constraints are enforced by this buy menu.
---@field EnforceMaxMassConstraint boolean Sets whether mass constraints are enforced by this buy menu.
BuyMenuGUI = {}

--- Sets which MetaPlayer uses this menu, if any.
---@param metaPlayer number The index of the MetaPlayer that uses this menu.
function BuyMenuGUI:SetMetaPlayer(metaPlayer) end

--- Sets which DataModule ID should be treated as the native tech of the user of this menu. This will also apply the DataModule's faction BuyMenu theme, if applicable.
---@param whichModule number The module ID to set as the native one. 0 means everything is native.
function BuyMenuGUI:SetNativeTechModule(whichModule) end

--- Sets the multiplier of the cost of any foreign Tech items.
---@param newMultiplier number The scalar multiplier of the costs of foreign Tech items.
function BuyMenuGUI:SetForeignCostMultiplier(newMultiplier) end

--- Sets whether a data module shown in the item menu should be expanded or not.
---@param whichModule number The module ID to set as expanded.
---@param expanded? boolean Whether should be expanded or not. (default: true)
function BuyMenuGUI:SetModuleExpanded(whichModule, expanded) end

--- Loads or re-loads all the loadout presets from the appropriate files on disk. This will first clear out all current loadout presets!
---@return boolean
function BuyMenuGUI:LoadAllLoadoutsFromFile() end

--- Adds an item to the list of allowed items. If the list is not empty then everything not in the list is removed from the buy menu Items will be removed from the buy menu when it's called, category changed or after a ForceRefresh().
---@param presetName string Full preset name to add.
function BuyMenuGUI:AddAllowedItem(presetName) end

--- Removes an item from the list of allowed items.
---@param presetName string
function BuyMenuGUI:RemoveAllowedItem(presetName) end

--- Clears the list of allowed items
function BuyMenuGUI:ClearAllowedItems() end

--- Adds an item to the list of always allowed items. This list overrides all previous constraints.
---@param presetName string Full preset name to add.
function BuyMenuGUI:AddAlwaysAllowedItem(presetName) end

--- Removes an item from the list of always allowed items.
---@param presetName string
function BuyMenuGUI:RemoveAlwaysAllowedItem(presetName) end

--- Clears the list of allowed items
function BuyMenuGUI:ClearAlwaysAllowedItems() end

--- Adds an item prohibited to buy from the buy menu. The item will be removed from the buy menu when it's called, category changed or after a ForceRefresh().
---@param presetName string Full preset name to add.
function BuyMenuGUI:AddProhibitedItem(presetName) end

--- Removes item from the list of prohibited items
---@param presetName string
function BuyMenuGUI:RemoveProhibitedItem(presetName) end

--- Clears the list of prohibited items
function BuyMenuGUI:ClearProhibitedItems() end

--- Forces a refresh update of the list of buy menu items
function BuyMenuGUI:ForceRefresh() end

--- Sets the amount of specified items to be owned in this buy menu
---@param presetName string Full preset name of item to own. Amount of owned items.
---@param amount number
function BuyMenuGUI:SetOwnedItemsAmount(presetName, amount) end

--- Returns the amount of specified items owned in this buy menu
---@param presetName string Full preset name of item.
---@return number
function BuyMenuGUI:GetOwnedItemsAmount(presetName) end

--- Changes the banner image to the one specified. If none is specified, resets it to the default banner image.
---@param imagePath string Path to image to set as banner.
function BuyMenuGUI:SetBannerImage(imagePath) end

--- Changes the logo image to the one specified. If none is specified, resets it to the default logo image.
---@param imagePath string Path to image to set as logo.
function BuyMenuGUI:SetLogoImage(imagePath) end

--- Clear the cart out of items selected for purchase
function BuyMenuGUI:ClearCartList() end

--- Loads the default loadout to the cart
function BuyMenuGUI:LoadDefaultLoadoutToCart() end

---@return SceneObject[]
function BuyMenuGUI:GetOrderList() end

--- Return the total cost of everything listed in the order box, excluding delivery costs.
---@return number
function BuyMenuGUI:GetTotalCartCost() end

--- Return the total cost of everything listed in the order box, including delivery costs.
---@return number
function BuyMenuGUI:GetTotalOrderCost() end

--- Return the total mass of all items listed in the order box.
---@return number
function BuyMenuGUI:GetTotalOrderMass() end

--- Return teh total number of passengers in the order box.
---@return number
function BuyMenuGUI:GetTotalOrderPassengers() end

--- A full menu system that represents the scene editing GUI for Cortex Command
---@class SceneEditorGUI
---@field ClassName string The C++ class name, like "SceneEditorGUI".
---@field EditorMode number Gets the current mode of this editor.
---@field INACTIVE number One of SceneEditorGUI.EditorGUIMode.
---@field PICKINGOBJECT number One of SceneEditorGUI.EditorGUIMode.
---@field ADDINGOBJECT number One of SceneEditorGUI.EditorGUIMode.
---@field INSTALLINGBRAIN number One of SceneEditorGUI.EditorGUIMode.
---@field PLACINGOBJECT number One of SceneEditorGUI.EditorGUIMode.
---@field MOVINGOBJECT number One of SceneEditorGUI.EditorGUIMode.
---@field DELETINGOBJECT number One of SceneEditorGUI.EditorGUIMode.
---@field PLACEINFRONT number One of SceneEditorGUI.EditorGUIMode.
---@field PLACEBEHIND number One of SceneEditorGUI.EditorGUIMode.
---@field DONEEDITING number One of SceneEditorGUI.EditorGUIMode.
---@field EDITORGUIMODECOUNT number One of SceneEditorGUI.EditorGUIMode.
SceneEditorGUI = {}

--- Sets the absolute scene coordinates of the cursor of this Editor.
---@param newCursorPos Vector The new cursor position in absolute scene units.
function SceneEditorGUI:SetCursorPos(newCursorPos) end

--- Gets the currently held Object in the cursor of this Editor. Ownership IS NOT transferred!
---@return SceneObject
function SceneEditorGUI:GetCurrentObject() end

--- Sets the new Object to be held at the cursor of this Editor. Ownership IS transferred!
---@param pNewObject SceneObject The new Object to be held by the cursor. Ownership IS transferred!
---@return boolean
function SceneEditorGUI:SetCurrentObject(pNewObject) end

--- Sets which DataModule space to be picking objects from. If -1, then let the player pick from all loaded modules. modules' objects will alwayws be presented, in addition to the one passed in here.
---@param moduleSpaceID? number The ID of the module to let the player pick objects from. All official (default: -1)
function SceneEditorGUI:SetModuleSpace(moduleSpaceID) end

--- Sets which DataModule ID should be treated as the native tech of the user of this menu.
---@param whichModule number The module ID to set as the native one. 0 means everything is native.
function SceneEditorGUI:SetNativeTechModule(whichModule) end

--- Sets the multiplier of the cost of any foreign Tech items.
---@param newMultiplier number The scalar multiplier of the costs of foreign Tech items.
function SceneEditorGUI:SetForeignCostMultiplier(newMultiplier) end

--- Checks whether the resident brain is currently placed into a valid location in this scene, based on whether there is a clear path to the sky above it. This forces the editor into place brain mode with the current resident brain if the current placement is no bueno. It also removes the faulty brain from residence in the scene! into isntallation mode if no brain was found.
---@param noBrainIsOK? boolean Whether it's OK if we dont' have a brain right now - ie don't force (default: false)
---@return boolean
function SceneEditorGUI:TestBrainResidence(noBrainIsOK) end

--- Updates the state of this Menu each frame
function SceneEditorGUI:Update() end

--- Derived structs for each of the input enums because we can't register enum_wrapper multiple times under a different name. We're doing this so we can access each enum separately by name rather than having all of them accessed from a shared name. If this proves to be a hassle then we can easily revert to the shared name access by registering everything under enum_wrapper.
---@class InputDevice
---@field ClassName string The C++ class name, like "InputDevice".
---@field DEVICE_KEYB_ONLY number One of InputDevice.InputDevice.
---@field DEVICE_MOUSE_KEYB number One of InputDevice.InputDevice.
---@field DEVICE_GAMEPAD_1 number One of InputDevice.InputDevice.
---@field DEVICE_GAMEPAD_2 number One of InputDevice.InputDevice.
---@field DEVICE_GAMEPAD_3 number One of InputDevice.InputDevice.
---@field DEVICE_GAMEPAD_4 number One of InputDevice.InputDevice.
---@field DEVICE_COUNT number One of InputDevice.InputDevice.
InputDevice = {}

---@class InputElements
---@field ClassName string The C++ class name, like "InputElements".
---@field INPUT_L_UP number One of InputElements.InputElements.
---@field INPUT_L_DOWN number One of InputElements.InputElements.
---@field INPUT_L_LEFT number One of InputElements.InputElements.
---@field INPUT_L_RIGHT number One of InputElements.InputElements.
---@field INPUT_R_UP number One of InputElements.InputElements.
---@field INPUT_R_DOWN number One of InputElements.InputElements.
---@field INPUT_R_LEFT number One of InputElements.InputElements.
---@field INPUT_R_RIGHT number One of InputElements.InputElements.
---@field INPUT_FIRE number One of InputElements.InputElements.
---@field INPUT_AIM number One of InputElements.InputElements.
---@field INPUT_AIM_UP number One of InputElements.InputElements.
---@field INPUT_AIM_DOWN number One of InputElements.InputElements.
---@field INPUT_AIM_LEFT number One of InputElements.InputElements.
---@field INPUT_AIM_RIGHT number One of InputElements.InputElements.
---@field INPUT_PIEMENU_ANALOG number One of InputElements.InputElements.
---@field INPUT_PIEMENU_DIGITAL number One of InputElements.InputElements.
---@field INPUT_JUMP number One of InputElements.InputElements.
---@field INPUT_CROUCH number One of InputElements.InputElements.
---@field INPUT_NEXT number One of InputElements.InputElements.
---@field INPUT_PREV number One of InputElements.InputElements.
---@field INPUT_START number One of InputElements.InputElements.
---@field INPUT_BACK number One of InputElements.InputElements.
---@field INPUT_COUNT number One of InputElements.InputElements.
InputElements = {}

---@class MouseButtons
---@field ClassName string The C++ class name, like "MouseButtons".
---@field MOUSE_NONE number One of MouseButtons.MouseButtons.
---@field MOUSE_LEFT number One of MouseButtons.MouseButtons.
---@field MOUSE_RIGHT number One of MouseButtons.MouseButtons.
---@field MOUSE_MIDDLE number One of MouseButtons.MouseButtons.
---@field MAX_MOUSE_BUTTONS number One of MouseButtons.MouseButtons.
MouseButtons = {}

---@class JoyButtons
---@field ClassName string The C++ class name, like "JoyButtons".
---@field JOY_NONE number One of JoyButtons.JoyButtons.
---@field JOY_1 number One of JoyButtons.JoyButtons.
---@field JOY_2 number One of JoyButtons.JoyButtons.
---@field JOY_3 number One of JoyButtons.JoyButtons.
---@field JOY_4 number One of JoyButtons.JoyButtons.
---@field JOY_5 number One of JoyButtons.JoyButtons.
---@field JOY_6 number One of JoyButtons.JoyButtons.
---@field JOY_7 number One of JoyButtons.JoyButtons.
---@field JOY_8 number One of JoyButtons.JoyButtons.
---@field JOY_9 number One of JoyButtons.JoyButtons.
---@field JOY_10 number One of JoyButtons.JoyButtons.
---@field JOY_11 number One of JoyButtons.JoyButtons.
---@field JOY_12 number One of JoyButtons.JoyButtons.
---@field MAX_JOY_BUTTONS number One of JoyButtons.JoyButtons.
JoyButtons = {}

---@class JoyDirections
---@field ClassName string The C++ class name, like "JoyDirections".
---@field JOYDIR_ONE number One of JoyDirections.JoyDirections.
---@field JOYDIR_TWO number One of JoyDirections.JoyDirections.
JoyDirections = {}

---@class Key
---@field ClassName string The C++ class name, like "Key".
---@field UNKNOWN number One of Key.Key.
---@field RETURN number One of Key.Key.
---@field ESCAPE number One of Key.Key.
---@field BACKSPACE number One of Key.Key.
---@field TAB number One of Key.Key.
---@field SPACE number One of Key.Key.
---@field EXCLAIM number One of Key.Key.
---@field QUOTEDBL number One of Key.Key.
---@field HASH number One of Key.Key.
---@field PERCENT number One of Key.Key.
---@field DOLLAR number One of Key.Key.
---@field AMPERSAND number One of Key.Key.
---@field QUOTE number One of Key.Key.
---@field LEFTPAREN number One of Key.Key.
---@field RIGHTPAREN number One of Key.Key.
---@field ASTERISK number One of Key.Key.
---@field PLUS number One of Key.Key.
---@field COMMA number One of Key.Key.
---@field MINUS number One of Key.Key.
---@field PERIOD number One of Key.Key.
---@field SLASH number One of Key.Key.
---@field K_0 number One of Key.Key.
---@field K_1 number One of Key.Key.
---@field K_2 number One of Key.Key.
---@field K_3 number One of Key.Key.
---@field K_4 number One of Key.Key.
---@field K_5 number One of Key.Key.
---@field K_6 number One of Key.Key.
---@field K_7 number One of Key.Key.
---@field K_8 number One of Key.Key.
---@field K_9 number One of Key.Key.
---@field COLON number One of Key.Key.
---@field SEMICOLON number One of Key.Key.
---@field LESS number One of Key.Key.
---@field EQUALS number One of Key.Key.
---@field GREATER number One of Key.Key.
---@field QUESTION number One of Key.Key.
---@field AT number One of Key.Key.
---@field LEFTBRACKET number One of Key.Key.
---@field BACKSLASH number One of Key.Key.
---@field RIGHTBRACKET number One of Key.Key.
---@field CARET number One of Key.Key.
---@field UNDERSCORE number One of Key.Key.
---@field BACKQUOTE number One of Key.Key.
---@field A number One of Key.Key.
---@field B number One of Key.Key.
---@field C number One of Key.Key.
---@field D number One of Key.Key.
---@field E number One of Key.Key.
---@field F number One of Key.Key.
---@field G number One of Key.Key.
---@field H number One of Key.Key.
---@field I number One of Key.Key.
---@field J number One of Key.Key.
---@field K number One of Key.Key.
---@field L number One of Key.Key.
---@field M number One of Key.Key.
---@field N number One of Key.Key.
---@field O number One of Key.Key.
---@field P number One of Key.Key.
---@field Q number One of Key.Key.
---@field R number One of Key.Key.
---@field S number One of Key.Key.
---@field T number One of Key.Key.
---@field U number One of Key.Key.
---@field V number One of Key.Key.
---@field W number One of Key.Key.
---@field X number One of Key.Key.
---@field Y number One of Key.Key.
---@field Z number One of Key.Key.
---@field CAPSLOCK number One of Key.Key.
---@field F1 number One of Key.Key.
---@field F2 number One of Key.Key.
---@field F3 number One of Key.Key.
---@field F4 number One of Key.Key.
---@field F5 number One of Key.Key.
---@field F6 number One of Key.Key.
---@field F7 number One of Key.Key.
---@field F8 number One of Key.Key.
---@field F9 number One of Key.Key.
---@field F10 number One of Key.Key.
---@field F11 number One of Key.Key.
---@field F12 number One of Key.Key.
---@field PRINTSCREEN number One of Key.Key.
---@field SCROLLLOCK number One of Key.Key.
---@field PAUSE number One of Key.Key.
---@field INSERT number One of Key.Key.
---@field HOME number One of Key.Key.
---@field PAGEUP number One of Key.Key.
---@field DELETE number One of Key.Key.
---@field END number One of Key.Key.
---@field PAGEDOWN number One of Key.Key.
---@field RIGHT number One of Key.Key.
---@field LEFT number One of Key.Key.
---@field DOWN number One of Key.Key.
---@field UP number One of Key.Key.
---@field NUMLOCKCLEAR number One of Key.Key.
---@field KP_DIVIDE number One of Key.Key.
---@field KP_MULTIPLY number One of Key.Key.
---@field KP_MINUS number One of Key.Key.
---@field KP_PLUS number One of Key.Key.
---@field KP_ENTER number One of Key.Key.
---@field KP_1 number One of Key.Key.
---@field KP_2 number One of Key.Key.
---@field KP_3 number One of Key.Key.
---@field KP_4 number One of Key.Key.
---@field KP_5 number One of Key.Key.
---@field KP_6 number One of Key.Key.
---@field KP_7 number One of Key.Key.
---@field KP_8 number One of Key.Key.
---@field KP_9 number One of Key.Key.
---@field KP_0 number One of Key.Key.
---@field KP_PERIOD number One of Key.Key.
---@field APPLICATION number One of Key.Key.
---@field POWER number One of Key.Key.
---@field KP_EQUALS number One of Key.Key.
---@field F13 number One of Key.Key.
---@field F14 number One of Key.Key.
---@field F15 number One of Key.Key.
---@field F16 number One of Key.Key.
---@field F17 number One of Key.Key.
---@field F18 number One of Key.Key.
---@field F19 number One of Key.Key.
---@field F20 number One of Key.Key.
---@field F21 number One of Key.Key.
---@field F22 number One of Key.Key.
---@field F23 number One of Key.Key.
---@field F24 number One of Key.Key.
---@field EXECUTE number One of Key.Key.
---@field HELP number One of Key.Key.
---@field MENU number One of Key.Key.
---@field SELECT number One of Key.Key.
---@field STOP number One of Key.Key.
---@field AGAIN number One of Key.Key.
---@field UNDO number One of Key.Key.
---@field CUT number One of Key.Key.
---@field COPY number One of Key.Key.
---@field PASTE number One of Key.Key.
---@field FIND number One of Key.Key.
---@field MUTE number One of Key.Key.
---@field VOLUMEUP number One of Key.Key.
---@field VOLUMEDOWN number One of Key.Key.
---@field KP_COMMA number One of Key.Key.
---@field KP_EQUALSAS400 number One of Key.Key.
---@field ALTERASE number One of Key.Key.
---@field SYSREQ number One of Key.Key.
---@field CANCEL number One of Key.Key.
---@field CLEAR number One of Key.Key.
---@field PRIOR number One of Key.Key.
---@field RETURN2 number One of Key.Key.
---@field SEPARATOR number One of Key.Key.
---@field OUT number One of Key.Key.
---@field OPER number One of Key.Key.
---@field CLEARAGAIN number One of Key.Key.
---@field CRSEL number One of Key.Key.
---@field EXSEL number One of Key.Key.
---@field KP_00 number One of Key.Key.
---@field KP_000 number One of Key.Key.
---@field THOUSANDSSEPARATOR number One of Key.Key.
---@field DECIMALSEPARATOR number One of Key.Key.
---@field CURRENCYUNIT number One of Key.Key.
---@field CURRENCYSUBUNIT number One of Key.Key.
---@field KP_LEFTPAREN number One of Key.Key.
---@field KP_RIGHTPAREN number One of Key.Key.
---@field KP_LEFTBRACE number One of Key.Key.
---@field KP_RIGHTBRACE number One of Key.Key.
---@field KP_TAB number One of Key.Key.
---@field KP_BACKSPACE number One of Key.Key.
---@field KP_A number One of Key.Key.
---@field KP_B number One of Key.Key.
---@field KP_C number One of Key.Key.
---@field KP_D number One of Key.Key.
---@field KP_E number One of Key.Key.
---@field KP_F number One of Key.Key.
---@field KP_XOR number One of Key.Key.
---@field KP_POWER number One of Key.Key.
---@field KP_PERCENT number One of Key.Key.
---@field KP_LESS number One of Key.Key.
---@field KP_GREATER number One of Key.Key.
---@field KP_AMPERSAND number One of Key.Key.
---@field KP_DBLAMPERSAND number One of Key.Key.
---@field KP_VERTICALBAR number One of Key.Key.
---@field KP_DBLVERTICALBAR number One of Key.Key.
---@field KP_COLON number One of Key.Key.
---@field KP_HASH number One of Key.Key.
---@field KP_SPACE number One of Key.Key.
---@field KP_AT number One of Key.Key.
---@field KP_EXCLAM number One of Key.Key.
---@field KP_MEMSTORE number One of Key.Key.
---@field KP_MEMRECALL number One of Key.Key.
---@field KP_MEMCLEAR number One of Key.Key.
---@field KP_MEMADD number One of Key.Key.
---@field KP_MEMSUBTRACT number One of Key.Key.
---@field KP_MEMMULTIPLY number One of Key.Key.
---@field KP_MEMDIVIDE number One of Key.Key.
---@field KP_PLUSMINUS number One of Key.Key.
---@field KP_CLEAR number One of Key.Key.
---@field KP_CLEARENTRY number One of Key.Key.
---@field KP_BINARY number One of Key.Key.
---@field KP_OCTAL number One of Key.Key.
---@field KP_DECIMAL number One of Key.Key.
---@field KP_HEXADECIMAL number One of Key.Key.
---@field LCTRL number One of Key.Key.
---@field LSHIFT number One of Key.Key.
---@field LALT number One of Key.Key.
---@field LGUI number One of Key.Key.
---@field RCTRL number One of Key.Key.
---@field RSHIFT number One of Key.Key.
---@field RALT number One of Key.Key.
---@field RGUI number One of Key.Key.
---@field MODE number One of Key.Key.
---@field AUDIONEXT number One of Key.Key.
---@field AUDIOPREV number One of Key.Key.
---@field AUDIOSTOP number One of Key.Key.
---@field AUDIOPLAY number One of Key.Key.
---@field AUDIOMUTE number One of Key.Key.
---@field MEDIASELECT number One of Key.Key.
---@field WWW number One of Key.Key.
---@field MAIL number One of Key.Key.
---@field CALCULATOR number One of Key.Key.
---@field COMPUTER number One of Key.Key.
---@field AC_SEARCH number One of Key.Key.
---@field AC_HOME number One of Key.Key.
---@field AC_BACK number One of Key.Key.
---@field AC_FORWARD number One of Key.Key.
---@field AC_STOP number One of Key.Key.
---@field AC_REFRESH number One of Key.Key.
---@field AC_BOOKMARKS number One of Key.Key.
---@field BRIGHTNESSDOWN number One of Key.Key.
---@field BRIGHTNESSUP number One of Key.Key.
---@field DISPLAYSWITCH number One of Key.Key.
---@field KBDILLUMTOGGLE number One of Key.Key.
---@field KBDILLUMDOWN number One of Key.Key.
---@field KBDILLUMUP number One of Key.Key.
---@field EJECT number One of Key.Key.
---@field SLEEP number One of Key.Key.
Key = {}

---@class Scancode
---@field ClassName string The C++ class name, like "Scancode".
---@field UNKNOWN number One of Scancode.Scancode.
---@field A number One of Scancode.Scancode.
---@field B number One of Scancode.Scancode.
---@field C number One of Scancode.Scancode.
---@field D number One of Scancode.Scancode.
---@field E number One of Scancode.Scancode.
---@field F number One of Scancode.Scancode.
---@field G number One of Scancode.Scancode.
---@field H number One of Scancode.Scancode.
---@field I number One of Scancode.Scancode.
---@field J number One of Scancode.Scancode.
---@field K number One of Scancode.Scancode.
---@field L number One of Scancode.Scancode.
---@field M number One of Scancode.Scancode.
---@field N number One of Scancode.Scancode.
---@field O number One of Scancode.Scancode.
---@field P number One of Scancode.Scancode.
---@field Q number One of Scancode.Scancode.
---@field R number One of Scancode.Scancode.
---@field S number One of Scancode.Scancode.
---@field T number One of Scancode.Scancode.
---@field U number One of Scancode.Scancode.
---@field V number One of Scancode.Scancode.
---@field W number One of Scancode.Scancode.
---@field X number One of Scancode.Scancode.
---@field Y number One of Scancode.Scancode.
---@field Z number One of Scancode.Scancode.
---@field K_1 number One of Scancode.Scancode.
---@field K_2 number One of Scancode.Scancode.
---@field K_3 number One of Scancode.Scancode.
---@field K_4 number One of Scancode.Scancode.
---@field K_5 number One of Scancode.Scancode.
---@field K_6 number One of Scancode.Scancode.
---@field K_7 number One of Scancode.Scancode.
---@field K_8 number One of Scancode.Scancode.
---@field K_9 number One of Scancode.Scancode.
---@field K_0 number One of Scancode.Scancode.
---@field RETURN number One of Scancode.Scancode.
---@field ESCAPE number One of Scancode.Scancode.
---@field BACKSPACE number One of Scancode.Scancode.
---@field TAB number One of Scancode.Scancode.
---@field SPACE number One of Scancode.Scancode.
---@field MINUS number One of Scancode.Scancode.
---@field EQUALS number One of Scancode.Scancode.
---@field LEFTBRACKET number One of Scancode.Scancode.
---@field RIGHTBRACKET number One of Scancode.Scancode.
---@field BACKSLASH number One of Scancode.Scancode.
---@field NONUSHASH number One of Scancode.Scancode.
---@field SEMICOLON number One of Scancode.Scancode.
---@field APOSTROPHE number One of Scancode.Scancode.
---@field GRAVE number One of Scancode.Scancode.
---@field COMMA number One of Scancode.Scancode.
---@field PERIOD number One of Scancode.Scancode.
---@field SLASH number One of Scancode.Scancode.
---@field CAPSLOCK number One of Scancode.Scancode.
---@field F1 number One of Scancode.Scancode.
---@field F2 number One of Scancode.Scancode.
---@field F3 number One of Scancode.Scancode.
---@field F4 number One of Scancode.Scancode.
---@field F5 number One of Scancode.Scancode.
---@field F6 number One of Scancode.Scancode.
---@field F7 number One of Scancode.Scancode.
---@field F8 number One of Scancode.Scancode.
---@field F9 number One of Scancode.Scancode.
---@field F10 number One of Scancode.Scancode.
---@field F11 number One of Scancode.Scancode.
---@field F12 number One of Scancode.Scancode.
---@field PRINTSCREEN number One of Scancode.Scancode.
---@field SCROLLLOCK number One of Scancode.Scancode.
---@field PAUSE number One of Scancode.Scancode.
---@field INSERT number One of Scancode.Scancode.
---@field HOME number One of Scancode.Scancode.
---@field PAGEUP number One of Scancode.Scancode.
---@field DELETE number One of Scancode.Scancode.
---@field END number One of Scancode.Scancode.
---@field PAGEDOWN number One of Scancode.Scancode.
---@field RIGHT number One of Scancode.Scancode.
---@field LEFT number One of Scancode.Scancode.
---@field DOWN number One of Scancode.Scancode.
---@field UP number One of Scancode.Scancode.
---@field NUMLOCKCLEAR number One of Scancode.Scancode.
---@field KP_DIVIDE number One of Scancode.Scancode.
---@field KP_MULTIPLY number One of Scancode.Scancode.
---@field KP_MINUS number One of Scancode.Scancode.
---@field KP_PLUS number One of Scancode.Scancode.
---@field KP_ENTER number One of Scancode.Scancode.
---@field KP_1 number One of Scancode.Scancode.
---@field KP_2 number One of Scancode.Scancode.
---@field KP_3 number One of Scancode.Scancode.
---@field KP_4 number One of Scancode.Scancode.
---@field KP_5 number One of Scancode.Scancode.
---@field KP_6 number One of Scancode.Scancode.
---@field KP_7 number One of Scancode.Scancode.
---@field KP_8 number One of Scancode.Scancode.
---@field KP_9 number One of Scancode.Scancode.
---@field KP_0 number One of Scancode.Scancode.
---@field KP_PERIOD number One of Scancode.Scancode.
---@field NONUSBACKSLASH number One of Scancode.Scancode.
---@field APPLICATION number One of Scancode.Scancode.
---@field POWER number One of Scancode.Scancode.
---@field KP_EQUALS number One of Scancode.Scancode.
---@field F13 number One of Scancode.Scancode.
---@field F14 number One of Scancode.Scancode.
---@field F15 number One of Scancode.Scancode.
---@field F16 number One of Scancode.Scancode.
---@field F17 number One of Scancode.Scancode.
---@field F18 number One of Scancode.Scancode.
---@field F19 number One of Scancode.Scancode.
---@field F20 number One of Scancode.Scancode.
---@field F21 number One of Scancode.Scancode.
---@field F22 number One of Scancode.Scancode.
---@field F23 number One of Scancode.Scancode.
---@field F24 number One of Scancode.Scancode.
---@field EXECUTE number One of Scancode.Scancode.
---@field HELP number One of Scancode.Scancode.
---@field MENU number One of Scancode.Scancode.
---@field SELECT number One of Scancode.Scancode.
---@field STOP number One of Scancode.Scancode.
---@field AGAIN number One of Scancode.Scancode.
---@field UNDO number One of Scancode.Scancode.
---@field CUT number One of Scancode.Scancode.
---@field COPY number One of Scancode.Scancode.
---@field PASTE number One of Scancode.Scancode.
---@field FIND number One of Scancode.Scancode.
---@field MUTE number One of Scancode.Scancode.
---@field VOLUMEUP number One of Scancode.Scancode.
---@field VOLUMEDOWN number One of Scancode.Scancode.
---@field KP_COMMA number One of Scancode.Scancode.
---@field KP_EQUALSAS400 number One of Scancode.Scancode.
---@field INTERNATIONAL1 number One of Scancode.Scancode.
---@field INTERNATIONAL2 number One of Scancode.Scancode.
---@field INTERNATIONAL3 number One of Scancode.Scancode.
---@field INTERNATIONAL4 number One of Scancode.Scancode.
---@field INTERNATIONAL5 number One of Scancode.Scancode.
---@field INTERNATIONAL6 number One of Scancode.Scancode.
---@field INTERNATIONAL7 number One of Scancode.Scancode.
---@field INTERNATIONAL8 number One of Scancode.Scancode.
---@field INTERNATIONAL9 number One of Scancode.Scancode.
---@field LANG1 number One of Scancode.Scancode.
---@field LANG2 number One of Scancode.Scancode.
---@field LANG3 number One of Scancode.Scancode.
---@field LANG4 number One of Scancode.Scancode.
---@field LANG5 number One of Scancode.Scancode.
---@field LANG6 number One of Scancode.Scancode.
---@field LANG7 number One of Scancode.Scancode.
---@field LANG8 number One of Scancode.Scancode.
---@field LANG9 number One of Scancode.Scancode.
---@field ALTERASE number One of Scancode.Scancode.
---@field SYSREQ number One of Scancode.Scancode.
---@field CANCEL number One of Scancode.Scancode.
---@field CLEAR number One of Scancode.Scancode.
---@field PRIOR number One of Scancode.Scancode.
---@field RETURN2 number One of Scancode.Scancode.
---@field SEPARATOR number One of Scancode.Scancode.
---@field OUT number One of Scancode.Scancode.
---@field OPER number One of Scancode.Scancode.
---@field CLEARAGAIN number One of Scancode.Scancode.
---@field CRSEL number One of Scancode.Scancode.
---@field EXSEL number One of Scancode.Scancode.
---@field KP_00 number One of Scancode.Scancode.
---@field KP_000 number One of Scancode.Scancode.
---@field THOUSANDSSEPARATOR number One of Scancode.Scancode.
---@field DECIMALSEPARATOR number One of Scancode.Scancode.
---@field CURRENCYUNIT number One of Scancode.Scancode.
---@field CURRENCYSUBUNIT number One of Scancode.Scancode.
---@field KP_LEFTPAREN number One of Scancode.Scancode.
---@field KP_RIGHTPAREN number One of Scancode.Scancode.
---@field KP_LEFTBRACE number One of Scancode.Scancode.
---@field KP_RIGHTBRACE number One of Scancode.Scancode.
---@field KP_TAB number One of Scancode.Scancode.
---@field KP_BACKSPACE number One of Scancode.Scancode.
---@field KP_A number One of Scancode.Scancode.
---@field KP_B number One of Scancode.Scancode.
---@field KP_C number One of Scancode.Scancode.
---@field KP_D number One of Scancode.Scancode.
---@field KP_E number One of Scancode.Scancode.
---@field KP_F number One of Scancode.Scancode.
---@field KP_XOR number One of Scancode.Scancode.
---@field KP_POWER number One of Scancode.Scancode.
---@field KP_PERCENT number One of Scancode.Scancode.
---@field KP_LESS number One of Scancode.Scancode.
---@field KP_GREATER number One of Scancode.Scancode.
---@field KP_AMPERSAND number One of Scancode.Scancode.
---@field KP_DBLAMPERSAND number One of Scancode.Scancode.
---@field KP_VERTICALBAR number One of Scancode.Scancode.
---@field KP_DBLVERTICALBAR number One of Scancode.Scancode.
---@field KP_COLON number One of Scancode.Scancode.
---@field KP_HASH number One of Scancode.Scancode.
---@field KP_SPACE number One of Scancode.Scancode.
---@field KP_AT number One of Scancode.Scancode.
---@field KP_EXCLAM number One of Scancode.Scancode.
---@field KP_MEMSTORE number One of Scancode.Scancode.
---@field KP_MEMRECALL number One of Scancode.Scancode.
---@field KP_MEMCLEAR number One of Scancode.Scancode.
---@field KP_MEMADD number One of Scancode.Scancode.
---@field KP_MEMSUBTRACT number One of Scancode.Scancode.
---@field KP_MEMMULTIPLY number One of Scancode.Scancode.
---@field KP_MEMDIVIDE number One of Scancode.Scancode.
---@field KP_PLUSMINUS number One of Scancode.Scancode.
---@field KP_CLEAR number One of Scancode.Scancode.
---@field KP_CLEARENTRY number One of Scancode.Scancode.
---@field KP_BINARY number One of Scancode.Scancode.
---@field KP_OCTAL number One of Scancode.Scancode.
---@field KP_DECIMAL number One of Scancode.Scancode.
---@field KP_HEXADECIMAL number One of Scancode.Scancode.
---@field LCTRL number One of Scancode.Scancode.
---@field LSHIFT number One of Scancode.Scancode.
---@field LALT number One of Scancode.Scancode.
---@field LGUI number One of Scancode.Scancode.
---@field RCTRL number One of Scancode.Scancode.
---@field RSHIFT number One of Scancode.Scancode.
---@field RALT number One of Scancode.Scancode.
---@field RGUI number One of Scancode.Scancode.
---@field MODE number One of Scancode.Scancode.
---@field AUDIONEXT number One of Scancode.Scancode.
---@field AUDIOPREV number One of Scancode.Scancode.
---@field AUDIOSTOP number One of Scancode.Scancode.
---@field AUDIOPLAY number One of Scancode.Scancode.
---@field AUDIOMUTE number One of Scancode.Scancode.
---@field MEDIASELECT number One of Scancode.Scancode.
---@field WWW number One of Scancode.Scancode.
---@field MAIL number One of Scancode.Scancode.
---@field CALCULATOR number One of Scancode.Scancode.
---@field COMPUTER number One of Scancode.Scancode.
---@field AC_SEARCH number One of Scancode.Scancode.
---@field AC_HOME number One of Scancode.Scancode.
---@field AC_BACK number One of Scancode.Scancode.
---@field AC_FORWARD number One of Scancode.Scancode.
---@field AC_STOP number One of Scancode.Scancode.
---@field AC_REFRESH number One of Scancode.Scancode.
---@field AC_BOOKMARKS number One of Scancode.Scancode.
---@field BRIGHTNESSDOWN number One of Scancode.Scancode.
---@field BRIGHTNESSUP number One of Scancode.Scancode.
---@field DISPLAYSWITCH number One of Scancode.Scancode.
---@field KBDILLUMTOGGLE number One of Scancode.Scancode.
---@field KBDILLUMDOWN number One of Scancode.Scancode.
---@field KBDILLUMUP number One of Scancode.Scancode.
---@field EJECT number One of Scancode.Scancode.
---@field SLEEP number One of Scancode.Scancode.
---@field APP1 number One of Scancode.Scancode.
---@field APP2 number One of Scancode.Scancode.
---@field AUDIOREWIND number One of Scancode.Scancode.
---@field AUDIOFASTFORWARD number One of Scancode.Scancode.
---@field NUM_SCANCODES number One of Scancode.Scancode.
Scancode = {}

---@class GamepadButton
---@field ClassName string The C++ class name, like "GamepadButton".
---@field INVALID number One of GamepadButton.GamepadButton.
---@field A number One of GamepadButton.GamepadButton.
---@field B number One of GamepadButton.GamepadButton.
---@field X number One of GamepadButton.GamepadButton.
---@field Y number One of GamepadButton.GamepadButton.
---@field BACK number One of GamepadButton.GamepadButton.
---@field GUIDE number One of GamepadButton.GamepadButton.
---@field START number One of GamepadButton.GamepadButton.
---@field LEFTSTICK number One of GamepadButton.GamepadButton.
---@field RIGHTSTICK number One of GamepadButton.GamepadButton.
---@field LEFTSHOULDER number One of GamepadButton.GamepadButton.
---@field RIGHTSHOULDER number One of GamepadButton.GamepadButton.
---@field DPAD_UP number One of GamepadButton.GamepadButton.
---@field DPAD_DOWN number One of GamepadButton.GamepadButton.
---@field DPAD_LEFT number One of GamepadButton.GamepadButton.
---@field DPAD_RIGHT number One of GamepadButton.GamepadButton.
---@field MAX number One of GamepadButton.GamepadButton.
GamepadButton = {}

---@class GamepadAxis
---@field ClassName string The C++ class name, like "GamepadAxis".
---@field INVALID number One of GamepadAxis.GamepadAxis.
---@field LEFTX number One of GamepadAxis.GamepadAxis.
---@field LEFTY number One of GamepadAxis.GamepadAxis.
---@field RIGHTX number One of GamepadAxis.GamepadAxis.
---@field RIGHTY number One of GamepadAxis.GamepadAxis.
---@field TRIGGERLEFT number One of GamepadAxis.GamepadAxis.
---@field TRIGGERRIGHT number One of GamepadAxis.GamepadAxis.
---@field MAX number One of GamepadAxis.GamepadAxis.
GamepadAxis = {}

--- The singleton manager of the Activities and rules of Cortex Command.
---@class ActivityManager
---@field ClassName string The C++ class name, like "ActivityManager".
---@field DefaultActivityType string Gets the type name of the default Activity to be loaded if nothing else is available.
---@field DefaultActivityName string Gets the name of the default Activity to be loaded if nothing else is available.
ActivityManager = {}

--- Sets a new Activity to copy for next restart. You have to use RestartActivity to get it going. Ownership IS transferred!
---@param newActivity Activity The new Activity to put into effect next time ResetActivity is called.
function ActivityManager:SetStartActivity(newActivity) end

--- Gets the Activity that will be used in the next restart. Ownership is NOT transferred!
---@return Activity
function ActivityManager:GetStartActivity() end

--- Gets the currently active Activity. Won't be what has been set by SetStartActivity unless RestartActivity has been called since.
---@return Activity
function ActivityManager:GetActivity() end

--- Officially starts the Activity passed in. Ownership IS transferred!
---@param activity Activity The new activity to start.
---@return number
function ActivityManager:StartActivity(activity) end

---@param className string The class name of the Activity to start.
---@param presetName string The PresetName of the Activity to start.
---@return number
function ActivityManager:StartActivity(className, presetName) end

--- Completely restarts whatever Activity was last started.
---@return boolean
function ActivityManager:RestartActivity() end

--- Pauses/unpauses the game and saving/resuming in-game music if possible, or queuing default music if not.
---@param pause? boolean Whether to pause the game or not.
---@param skipPauseMenu? boolean Whether the pause menu should be skipped when the game simulation is paused.
function ActivityManager:PauseActivity(pause, skipPauseMenu) end

--- Forces the current game's end.
function ActivityManager:EndActivity() end

--- Indicates whether the game is currently running or not (not editing, over or paused).
---@return boolean
function ActivityManager:ActivityRunning() end

--- Indicates whether the game is currently paused or not.
---@return boolean
function ActivityManager:ActivityPaused() end

--- Saves the currently running Scene and Activity to a savegame file. Note this only works for GAScripted activities.
---@param fileName string Path to the file.
---@return boolean
function ActivityManager:SaveGame(fileName) end

--- Loads a saved game, and launches its Scene and Activity.
---@param fileName string Path to the file.
---@return boolean
function ActivityManager:LoadGame(fileName) end

--- The singleton manager of sound effect and music playback.
---@class AudioManager
---@field ClassName string The C++ class name, like "AudioManager".
---@field MusicVolume number Gets the volume of music. Does not get volume of sounds.
---@field SoundsVolume number Gets the volume of all sounds. Does not get volume of music.
AudioManager = {}

--- Stops all playback and clears the music playlist.
function AudioManager:StopAll() end

--- Gets the global pitch scalar value for all sounds and music.
---@return number
function AudioManager:GetGlobalPitch() end

--- Reports whether any music stream is currently playing.
---@return boolean
function AudioManager:IsMusicPlaying() end

--- Sets the music to a specific volume, but it will only last until a new song is played. Useful for fading etc.
---@param volume? number The desired volume scalar. 0.0-1.0.
function AudioManager:SetTempMusicVolume(volume) end

--- Gets the position of playback of the current music stream, in seconds.
---@return number
function AudioManager:GetMusicPosition() end

--- Sets the music to a specific position in the song.
---@param position number The desired position from the start, in seconds.
function AudioManager:SetMusicPosition(position) end

--- Sets/updates the frequency/pitch for the music channel.
---@param pitch number New pitch, a multiplier of the original normal frequency. Keep it > 0.
---@return boolean
function AudioManager:SetMusicPitch(pitch) end

--- Stops playing a the music channel.
function AudioManager:StopMusic() end

function AudioManager:StopMusic() end

--- Starts playing a certain WAVE, MOD, MIDI, OGG, MP3 file in the music channel.
---@param filePath number The path to the music file to play.
---@param loops? number The number of times to loop the song. 0 means play once. -1 means play infinitely until stopped.
---@param volumeOverrideIfNotMuted? number The volume override for music for this song only, if volume is not muted. < 0 means no override.
function AudioManager:PlayMusic(filePath, loops, volumeOverrideIfNotMuted) end

--- Plays the next music stream in the queue, if any is queued.
function AudioManager:PlayNextStream() end

--- Queues up another path to a stream that will be played after the current one is done. Can be done several times to queue up many tracks. The last track in the queue will be looped infinitely.
---@param filePath number The path to the music file to play after the current one.
function AudioManager:QueueMusicStream(filePath) end

--- Queues up a period of silence in the music stream playlist.
---@param seconds number The number of secs to wait before going to the next stream.
function AudioManager:QueueSilence(seconds) end

--- Clears the music queue.
function AudioManager:ClearMusicQueue() end

--- Starts playing a certain sound file.
---@param filePath string The path to the sound file to play.
---@return SoundContainer
function AudioManager:PlaySound(filePath) end

---@param filePath string The path to the sound file to play.
---@param position Vector
---@return SoundContainer
function AudioManager:PlaySound(filePath, position) end

---@param filePath string The path to the sound file to play.
---@param position Vector The position at which to play the SoundContainer's sounds.
---@param player number Which player to play the SoundContainer's sounds for, -1 means all players.
---@return SoundContainer
function AudioManager:PlaySound(filePath, position, player) end

--- The singleton manager of the lua console.
---@class ConsoleManager
---@field ClassName string The C++ class name, like "ConsoleManager".
ConsoleManager = {}

--- Prints a string into the console.
---@param stringToPrint string The string to print.
function ConsoleManager:PrintString(stringToPrint) end

--- Writes all the input strings to a log in the order they were entered.
---@param filePath string The filename of the file to write to.
function ConsoleManager:SaveInputLog(filePath) end

--- Writes the entire console buffer to a file.
---@param filePath string The filename of the file to write to.
---@return boolean
function ConsoleManager:SaveAllText(filePath) end

--- Clears all previous input.
function ConsoleManager:Clear() end

--- The singleton manager over the composition of frames.
---@class FrameManager
---@field ClassName string The C++ class name, like "FrameManager".
---@field PlayerScreenWidth number Gets the width of the individual player screens. This will only be less than the backbuffer resolution if there are split screens.
---@field PlayerScreenHeight number Gets the height of the individual player screens. This will only be less than the backbuffer resolution if there are split screens.
---@field ScreenCount number Gets the number of currently active screens, counting all splits.
---@field ResolutionMultiplier number Gets the resolution multiplier.
FrameManager = {}

--- Gets whether or not the HUD is disabled for a given screen.
---@param screenId? number The screen to check for.
---@return boolean
function FrameManager:IsHudDisabled(screenId) end

--- Sets whether or not the HUD is disabled for a given screen.
---@param value boolean Whether the HUD should be disabled.
---@param screenId? number The screen to set for.
function FrameManager:SetHudDisabled(value, screenId) end

--- Loads a palette from a bitmap file and sets it as the currently used screen palette.
---@param palettePath string String with the path to the palette bitmap file.
---@return boolean
function FrameManager:LoadPalette(palettePath) end

--- Sets the message to be displayed on top of each player's screen
---@param message string An std::string that specifies what should be displayed.
---@param whichScreen? number Which screen you want to set text to.
---@param blinkInterval? number The interval with which the screen will be blinking, in ms. 0 means no blinking.
---@param displayDuration? number The duration, in MS to force this message to display. No other message can be displayed before this expires. ClearScreenText overrides it though.
---@param centered? boolean Vertically centered on the screen.
function FrameManager:SetScreenText(message, whichScreen, blinkInterval, displayDuration, centered) end

--- Clears the message to be displayed on top of each player's screen.
---@param whichScreen? number Which screen message to clear.
function FrameManager:ClearScreenText(whichScreen) end

--- Fades the palette in from black at a specified speed.
---@param fadeSpeed? number Speed specified from (slowest) 1 - 64 (fastest).
function FrameManager:FadeInPalette(fadeSpeed) end

--- Fades the palette out to black at a specified speed.
---@param fadeSpeed? number Speed specified from (slowest) 1 - 64 (fastest).
function FrameManager:FadeOutPalette(fadeSpeed) end

--- Dumps a bitmap of the screen backbuffer to a 8bpp PNG file.
---@param nameBase number The filename of the file to save to, WITHOUT EXTENSION.
---@return number
function FrameManager:SaveScreenToPNG(nameBase) end

--- Dumps a bitmap to a 8bpp PNG file.
---@param bitmap BITMAP The individual bitmap that will be dumped.
---@param nameBase number The filename of the file to save to, WITHOUT EXTENSION.
---@return number
function FrameManager:SaveBitmapToPNG(bitmap, nameBase) end

--- Flashes any of the players' screen with the specified color for this frame.
---@param screen number Which screen to flash.
---@param color number What color to flash it. -1 means no color or flash.
---@param periodMS? number How long a period to fill the frame with color. If 0, a single-frame flash will happen.
function FrameManager:FlashScreen(screen, color, periodMS) end

--- Calculates the height of a text string using the given font size.
---@param text string Text string.
---@param maxWidth number Maximum width of the text string.
---@param isSmall boolean Whether to use small or large font.
---@return number
function FrameManager:CalculateTextHeight(text, maxWidth, isSmall) end

--- Calculates the width of a text string using the given font size.
---@param text string Text string.
---@param isSmall boolean Whether to use small or large font.
---@return number
function FrameManager:CalculateTextWidth(text, isSmall) end

--- Gets a copy of the passed in string, split into multiple lines as needed to fit within the specified width limit, based on the font to use.
---@param stringToSplit string The string to get a split copy of.
---@param widthLimit number The maximum width each line of the string can be.
---@param useSmallFont boolean
---@return string
function FrameManager:SplitStringToFitWidth(stringToSplit, widthLimit, useSmallFont) end

--- The singleton manager of the Metagame of Cortex Command, ie the games played out in the campaign screen.
---@class MetaManager
---@field ClassName string The C++ class name, like "MetaManager".
---@field GameName string Gets the name of the currently played Metagame. It's what's used when saving to disk.
---@field PlayerTurn number Shows which player's turn is now or coming up. intermediate phase.
---@field PlayerCount number Gets current number of MetaPlayers
---@field Players MetaPlayer[]
MetaManager = {}

--- Gets the designated team of a specific player
---@param metaPlayer number Which player.
---@return number
function MetaManager:GetTeamOfPlayer(metaPlayer) end

--- Gets the specified MetaPlayer
---@param metaPlayer number Which player.
---@return MetaPlayer
function MetaManager:GetPlayer(metaPlayer) end

--- Gets the MetaPlayer playing a specific in-game player, if any. 0 is returned.
---@param inGamePlayer number Which in-game player to translate into a metaplayer.
---@return MetaPlayer
function MetaManager:GetMetaPlayerOfInGamePlayer(inGamePlayer) end

--- The singleton manager of all movable objects in the RTE.
---@class MovableManager
---@field ClassName string The C++ class name, like "MovableManager".
---@field MaxDroppedItems number Gets the max number of dropped items that will be reached before the first dropped with be copied to the terrain.
---@field Actors Actor[]
---@field Items MovableObject[]
---@field Particles MovableObject[]
---@field AddedActors Actor[]
---@field AddedItems MovableObject[]
---@field AddedParticles MovableObject[]
---@field AlarmEvents AlarmEvent[]
---@field AddedAlarmEvents AlarmEvent[]
MovableManager = {}

--- Gets a MO from its MOID. Note that MOID's are only valid during the same frame as they were assigned to the MOs! MOID was found. 0 if 0 was passed in as MOID (no MOID). Ownership is *NOT* transferred!!
---@param whichID number The MOID to get the matching MO from.
---@return MovableObject
function MovableManager:GetMOFromID(whichID) end

--- Uses a global lookup map to find an object by it's unique id.
---@param id number Unique Id to look for.
---@return MovableObject
function MovableManager:FindObjectByUniqueID(id) end

--- Gets the number of MOID's currently in use this frame.
---@return number
function MovableManager:GetMOIDCount() end

--- Returns MO count for specified team
---@param team number Team to count MO's
---@return number
function MovableManager:GetTeamMOIDCount(team) end

--- Clears out all MovableObject:s out of this. Effectively empties the world of anything moving, without resetting all of this' settings.
function MovableManager:PurgeAllMOs() end

--- Get a pointer to the first Actor in the internal Actor list that is of a specifc group, alternatively the first one AFTER a specific actor! Ownership NOT xferred! in the list. 0 if there are no Actors of that team.
---@param group string Which group to try to get an Actor for.
---@param pAfterThis? Actor A pointer to an Actor to use as starting point in the forward search. (default: 0)
---@return Actor
function MovableManager:GetNextActorInGroup(group, pAfterThis) end

--- Get a pointer to the last Actor in the internal Actor list that is of a specifc group, alternatively the last one BEFORE a specific actor! Ownership NOT xferred! in the list. 0 if there are no Actors of that team.
---@param group string Which group to try to get an Actor for.
---@param pBeforeThis? Actor A pointer to an Actor to use as starting point in the backward search. (default: 0)
---@return Actor
function MovableManager:GetPrevActorInGroup(group, pBeforeThis) end

--- Get a pointer to the first Actor in the internal Actor list that is of a specifc team, alternatively the first one AFTER a specific actor! Ownership NOT xferred! in the list. 0 if there are no Actors of that team.
---@param team? number Which team to try to get an Actor for. 0 means first team, 1 means 2nd. (default: 0)
---@param pAfterThis? Actor A pointer to an Actor to use as starting point in the forward search. (default: 0)
---@return Actor
function MovableManager:GetNextTeamActor(team, pAfterThis) end

--- Get a pointer to the last Actor in the internal Actor list that is of a specifc team, alternatively the last one BEFORE a specific actor! Ownership NOT xferred! in the list. 0 if there are no Actors of that team.
---@param team? number Which team to try to get an Actor for. 0 means first team, 1 means 2nd. (default: 0)
---@param pBeforeThis? Actor A pointer to an Actor to use as starting point in the backward search. (default: 0)
---@return Actor
function MovableManager:GetPrevTeamActor(team, pBeforeThis) end

--- Get a pointer to an Actor in the internal Actor list that is of a specifc team and closest to a specific scene point.
---@param team number Which team to try to get an Actor for. 0 means first team, 1 means 2nd.
---@param player number The player to get the Actor for. This affects which brain can be marked.
---@param scenePoint Vector The Scene point to search for the closest to.
---@param maxRadius number The maximum radius around that scene point to search.
---@param getDistance Vector A Vector to be filled out with the distance of the returned closest to the search point. Will be unaltered if no object was found within radius.
---@param excludeThis? Actor An Actor to exclude from the search. OWNERSHIP IS NOT TRANSFERRED!
---@return Actor
function MovableManager:GetClosestTeamActor(team, player, scenePoint, maxRadius, getDistance, excludeThis) end

---@param team number Which team to try to get an Actor for. 0 means first team, 1 means 2nd.
---@param player number The player to get the Actor for. This affects which brain can be marked.
---@param scenePoint Vector The Scene point to search for the closest to.
---@param maxRadius number The maximum radius around that scene point to search.
---@param getDistance Vector A Vector to be filled out with the distance of the returned closest to the search point. Will be unaltered if no object was found within radius.
---@param onlyPlayerControllableActors boolean Whether to only get Actors that are flagged as player controllable.
---@param excludeThis? Actor An Actor to exclude from the search. OWNERSHIP IS NOT TRANSFERRED!
---@return Actor
function MovableManager:GetClosestTeamActor(team, player, scenePoint, maxRadius, getDistance, onlyPlayerControllableActors, excludeThis) end

--- Get a pointer to an Actor in the internal Actor list that is is not of the specified team and closest to a specific scene point. the search point. Will be unaltered if no object was found within radius. point, but not outside the max radius. If no Actor was found within the radius of the point, 0 is returned.
---@param team number Which team to try to get an enemy Actor for. NoTeam means all teams.
---@param scenePoint Vector The Scene point to search for the closest to.
---@param maxRadius number The maximum radius around that scene point to search.
---@param getDistance Vector A Vector to be filled out with the distance of the returned closest to
---@return Actor
function MovableManager:GetClosestEnemyActor(team, scenePoint, maxRadius, getDistance) end

--- Get a pointer to first best Actor in the internal Actor list that is of a specifc team. is in that team, 0 is returned.
---@param team number Which team to try to get an Actor for. 0 means first team, 1 means 2nd.
---@param player number The player to get the Actor for. This affects which brain can be marked.
---@return Actor
function MovableManager:GetFirstTeamActor(team, player) end

--- Get a pointer to an Actor in the internal Actor list that is closest to a specific scene point. the search point. Will be unaltered if no object was found within radius. An Actor to exclude from the search. OWNERSHIP IS NOT TRANSFERRED! point, but not outside the max radius. If no Actor other than the excluded one was found within the radius of the point, 0 is returned.
---@param scenePoint Vector Which team to try to get an Actor for. 0 means first team, 1 means 2nd.
---@param maxRadius number The Scene point to search for the closest to.
---@param getDistance Vector The maximum radius around that scene point to search.
---@param pExcludeThis? Actor A Vector to be filled out with the distance of the returned closest to (default: 0)
---@return Actor
function MovableManager:GetClosestActor(scenePoint, maxRadius, getDistance, pExcludeThis) end

--- Get a pointer to the brain actor of a specific team that is closest to a scene point. OWNERSHIP IS NOT TRANSFERRED! 0 if there are no brains of that team. OWNERSHIP IS NOT TRANSFERRED!
---@param team number Which team to try to get the brain for. 0 means first team, 1 means 2nd.
---@param scenePoint Vector The point in the scene where to look for the closest opposite team brain.
---@return Actor
function MovableManager:GetClosestBrainActor(team, scenePoint) end

--- Get a pointer to the brain actor of a specific team that is closest to a scene point. OWNERSHIP IS NOT TRANSFERRED! 0 if there are no brains of that team. OWNERSHIP IS NOT TRANSFERRED!
---@param team number
---@return Actor
function MovableManager:GetFirstBrainActor(team) end

--- Get a pointer to the brain actor NOT of a specific team that is closest to a scene point. OWNERSHIP IS NOT TRANSFERRED! 0 if there are no brains not on that team. OWNERSHIP IS NOT TRANSFERRED!
---@param notOfTeam number Which team to NOT get the brain for. 0 means first team, 1 means 2nd.
---@param scenePoint Vector The point where to look for the closest brain not of this team.
---@return Actor
function MovableManager:GetClosestOtherBrainActor(notOfTeam, scenePoint) end

--- Get a pointer to the brain actor NOT of a specific team. OWNERSHIP IS NOT TRANSFERRED! 0 if there are no brains not on that team. OWNERSHIP IS NOT TRANSFERRED!
---@param notOfTeam number
---@return Actor
function MovableManager:GetFirstOtherBrainActor(notOfTeam) end

--- Get a pointer to the first brain actor of a specific team which hasn't been assigned to a player yet. in the list that hasn't been assigned to a player. 0 if there are no unassigned brains of that team.
---@param team? number Which team to try to get the brain for. 0 means first team, 1 means 2nd. (default: 0)
---@return Actor
function MovableManager:GetUnassignedBrain(team) end

--- Gets the number of particles (MOPixel:s) currently held.
---@return number
function MovableManager:GetParticleCount() end

--- Gets the global setting for how much splash MOPixels should be created an MO penetrates the terrain deeply.
---@return number
function MovableManager:GetSplashRatio() end

--- Sets this to draw HUD lines for a specific team's roster this frame.
---@param team number Which team to have lines drawn of.
function MovableManager:SortTeamRoster(team) end

--- Changes actor team and updates team rosters.
---@param pActor Actor Pointer to actor, new team value
---@param team number
function MovableManager:ChangeActorTeam(pActor, team) end

--- Removes an Actor from the internal list of MO:s. After the Actor is removed, ownership is effectively released and transferred to whatever client called this method. removed. If the particle entry wasn't found, false is returned.
---@param pActorToRem MovableObject A pointer to the MovableObject to remove.
---@return Actor
function MovableManager:RemoveActor(pActorToRem) end

--- Removes a pickup-able MovableObject item from the internal list of MO:s. After the item is removed, ownership is effectively released and transferred to whatever client called this method. removed. If the particle entry wasn't found, false is returned.
---@param pItemToRem MovableObject A pointer to the MovableObject to remove.
---@return MovableObject
function MovableManager:RemoveItem(pItemToRem) end

--- Removes a MovableObject from the internal list of MO:s. After the MO is removed, ownership is effectively released and transferred to whatever client called this method. removed. If the particle entry wasn't found, false is returned.
---@param pMOToRem MovableObject A pointer to the MovableObject to remove.
---@return MovableObject
function MovableManager:RemoveParticle(pMOToRem) end

--- Indicates whether the passed in MovableObject pointer points to an MO that's currently active in the simulation, and kept by this MovableMan. Internal optimization is made so that the same MO can efficiently be checked many times during the same frame. this MovableMan.
---@param pMOToCheck MovableObject A pointer to the MovableObject to check for being actively kept by
---@return boolean
function MovableManager:ValidMO(pMOToCheck) end

--- Indicates whether the passed in MovableObject is an active Actor kept by this MovableMan or not.
---@param pMOToCheck MovableObject A pointer to the MovableObject to check for Actorness.
---@return boolean
function MovableManager:IsActor(pMOToCheck) end

--- Indicates whether the passed in MovableObject is an active Item kept by this MovableMan or not.
---@param pMOToCheck MovableObject A pointer to the MovableObject to check for Itemness.
---@return boolean
function MovableManager:IsDevice(pMOToCheck) end

--- Indicates whether the passed in MovableObject is an active Item kept by this MovableMan or not.
---@param pMOToCheck MovableObject A pointer to the MovableObject to check for Itemness.
---@return boolean
function MovableManager:IsParticle(pMOToCheck) end

--- Indicates whether the passed in MOID is that of an MO which either is or is parented to an active Actor by this MovableMan, or not.
---@param checkMOID number An MOID to check for Actorness.
---@return boolean
function MovableManager:IsOfActor(checkMOID) end

--- Produces the root MOID of the MOID of a potential child MO to another MO. will be the same as the MOID passed in if the MO is a root itself. It will be equal to g_NoMOID if the MOID isn't allocated to an MO.
---@param checkMOID number An MOID to get the root MOID of.
---@return number
function MovableManager:GetRootMOID(checkMOID) end

--- Removes a MovableObject from the any and all internal lists of MO:s. After the MO is removed, ownership is effectively released and transferred to whatever client called this method. removed. If the MO entry wasn't found, false is returned.
---@param pMOToRem MovableObject A pointer to the MovableObject to remove.
---@return boolean
function MovableManager:RemoveMO(pMOToRem) end

--- Kills and destroys all Actors of a specific Team.
---@param teamToKill number The team to annihilate. If NoTeam is passed in, then NO Actors die.
---@return number
function MovableManager:KillAllTeamActors(teamToKill) end

--- Kills and destroys all enemy Actors of a specific Team.
---@param teamNotToKill? number The team to NOT annihilate. If NoTeam is passed in, then ALL Actors die.
---@return number
function MovableManager:KillAllEnemyActors(teamNotToKill) end

--- Opens all doors and keeps them open until this is called again with false.
---@param open? boolean Whether to open all doors (true), or close all doors (false).
---@param team? number Which team to open doors for. NoTeam means all teams.
function MovableManager:OpenAllDoors(open, team) end

--- Shows whetehr particles are set to get copied to the terrain upon settling
---@return boolean
function MovableManager:IsParticleSettlingEnabled() end

--- Sets whether particles will get copied into the terrain upon them settling down.
---@param enable? boolean Whether to enable or not. (default: true)
function MovableManager:EnableParticleSettling(enable) end

--- Shows whether MO's sihouettes can get subtracted from the terrain at all.
---@return boolean
function MovableManager:IsMOSubtractionEnabled() end

--- Gets pointers to the MOs that are within the given Box.
---@param box Box The Box to get MOs within.
---@return MovableObject[]
function MovableManager:GetMOsInBox(box) end

---@param box Box The Box to get MOs within.
---@param ignoreTeam number The team to ignore.
---@return MovableObject[]
function MovableManager:GetMOsInBox(box, ignoreTeam) end

---@param box Box The Box to get MOs within.
---@param ignoreTeam number The team to ignore.
---@param getsHitByMOsOnly boolean Whether to only include MOs that have GetsHitByMOs enabled, or all MOs.
---@return MovableObject[]
function MovableManager:GetMOsInBox(box, ignoreTeam, getsHitByMOsOnly) end

--- Gets pointers to the MOs that are within the specified radius of the given centre position.
---@param centre Vector The position to check for MOs in.
---@param radius number The radius to check for MOs within.
---@return MovableObject[]
function MovableManager:GetMOsInRadius(centre, radius) end

---@param centre Vector The position to check for MOs in.
---@param radius number The radius to check for MOs within.
---@param ignoreTeam number The team to ignore.
---@return MovableObject[]
function MovableManager:GetMOsInRadius(centre, radius, ignoreTeam) end

---@param centre Vector The position to check for MOs in.
---@param radius number The radius to check for MOs within.
---@param ignoreTeam number The team to ignore.
---@param getsHitByMOsOnly boolean Whether to only include MOs that have GetsHitByMOs enabled, or all MOs.
---@return MovableObject[]
function MovableManager:GetMOsInRadius(centre, radius, ignoreTeam, getsHitByMOsOnly) end

---@param message string
function MovableManager:SendGlobalMessage(message) end

---@param message string
---@param context any
function MovableManager:SendGlobalMessage(message, context) end

--- Adds the given MovableObject to MovableMan if it doesn't already exist in there, or prints an error if it does.
---@param movableObject MovableObject A pointer to the MovableObject to be added.
function MovableManager:AddMO(movableObject) end

--- Adds the given Actor to MovableMan if it doesn't already exist in there, or prints an error if it does.
---@param actor Actor A pointer to the Actor to be added.
function MovableManager:AddActor(actor) end

--- Adds the given item MovableObject (generally a HeldDevice) to MovableMan if it doesn't already exist in there, or prints an error if it does.
---@param item HeldDevice A pointer to the item to be added.
function MovableManager:AddItem(item) end

--- Adds the given particle MovableObject to MovableMan if it doesn't already exist in there, or prints an error if it does.
---@param particle MovableObject A pointer to the particle to be added.
function MovableManager:AddParticle(particle) end

--- Singleton manager responsible for all performance stats counting and drawing.
---@class PerformanceManager
---@field ClassName string The C++ class name, like "PerformanceManager".
---@field ShowPerformanceStats boolean Tells whether to display the performance stats on-screen or not.
PerformanceManager = {}

--- Singleton manager responsible for all 32bpp post-process effect drawing.
---@class PostProcessManager
---@field ClassName string The C++ class name, like "PostProcessManager".
PostProcessManager = {}

--- Registers a post effect to be added at the very last stage of 32bpp rendering by the FrameMan.
---@param effectPos Vector The absolute scene coordinates of the center of the effect.
---@param effect BITMAP A 32bpp BITMAP screen should be drawn centered on the above scene location in the final frame buffer. Ownership is NOT transferred!
---@param hash number Hash value of the effect for transmitting over the network.
---@param strength? number The intensity level this effect should have when blended in post. 0 - 255.
---@param angle? number The angle this effect should be rotated at in radians.
function PostProcessManager:RegisterPostEffect(effectPos, effect, hash, strength, angle) end

--- The singleton manager of all presets of Entity:s in the RTE. The presets serve as a respository of Entity instances with specific and unique and initial runtime data.
---@class PresetManager
---@field ClassName string The C++ class name, like "PresetManager".
---@field Modules DataModule[]
PresetManager = {}

--- Reads an entire DataModule and adds it to this. NOTE that official modules can't be loaded after any non-official ones!
---@param moduleName string The module name to read, e.g. "Base.rte".
---@return boolean
function PresetManager:LoadDataModule(moduleName) end

--- Gets a specific loaded DataModule
---@param whichModule? number The ID of the module to get. (default: 0)
---@return DataModule
function PresetManager:GetDataModule(whichModule) end

--- Gets the ID of a loaded DataModule.
---@param moduleName string The name of the DataModule to get the ID from, including the ".rte"
---@return number
function PresetManager:GetModuleID(moduleName) end

--- Gets the ID of a loaded DataModule from a full data file path.
---@param dataPath string The full path to a data file inside the data module ID you want to get.
---@return number
function PresetManager:GetModuleIDFromPath(dataPath) end

--- Gets the total number of modules loaded so far, official or not.
---@return number
function PresetManager:GetTotalModuleCount() end

--- Gets the total number of OFFICIAL modules loaded so far.
---@return number
function PresetManager:GetOfficialModuleCount() end

--- Adds an Entity instance's pointer and name associations to the internal list of already read in Entity:s. Ownership is NOT transferred! If there already is an instance defined, nothing happens. If there is not, a clone is made of the passed-in Entity and added to the library. from a Reader. Ownership is NOT transferred! was found. If one of the same name but not the exact type, false is returned regardless and nothing will have been added. If "Same" is passed as the file path read from, an overwritten instance will keep the old one's file location entry. into the module. False will be returned if there already was an instance of that class and instance name inserted previously, unless overwritten.
---@param pEntToAdd Entity A pointer to the Entity derived instance to add. It should be created
---@param whichModule? number Which module to add the entity to. (default: 0)
---@param overwriteSame? boolean Whether to overwrite if an instance of the EXACT same TYPE and name (default: false)
---@param readFromFile? string The file this instance was read from, or where it should be written. (default: "Same")
---@return boolean
function PresetManager:AddPreset(pEntToAdd, whichModule, overwriteSame, readFromFile) end

--- Gets a previously read in (defined) Entity, by type and instance name. the official modules will be searched also. -1 means search ALL modules! derived type or instance name was found. Ownership is NOT transferred!
---@param type string The type name of the derived Entity. Ownership is NOT transferred!
---@param preset string The instance name of the derived Entity instance.
---@param whichModule? number Which module to try to get the entity from. If it's not found there, (default: -1)
---@return Entity
function PresetManager:GetPreset(type, preset, whichModule) end

---@param type string The type name of the derived Entity. Ownership is NOT transferred!
---@param preset string The instance name of the derived Entity instance.
---@param whichModule? string Which module to try to get the entity from. If it's not found there, (default: -1)
---@return Entity
function PresetManager:GetPreset(type, preset, whichModule) end

--- Creates and returns actor defined in the specified loadout.
---@param loadoutName string Loadout preset name, module name, whether or not spawn delivery craft defined for that loadout
---@param module string
---@param spawnDropShip boolean
---@return Actor
function PresetManager:GetLoadout(loadoutName, module, spawnDropShip) end

---@param loadoutName string Loadout preset name, module name, whether or not spawn delivery craft defined for that loadout
---@param module number
---@param spawnDropShip boolean
---@return Actor
function PresetManager:GetLoadout(loadoutName, module, spawnDropShip) end

--- Returns a previously read in (defined) Entity which is randomly selected from a specific group. "All" will look at all types.
---@param group string The group to randomly select an Entity from. "All" will look in all.
---@param type? string The name of the least common denominator type of the Entitys you want. (default: "All")
---@param whichModule? number Whether to only get those of one specific DataModule (0-n), or all (-1). (default: -1)
---@return Entity
function PresetManager:GetRandomOfGroup(group, type, whichModule) end

--- Returns a previously read in (defined) Entity which is associated with a specific group, randomly selected and only exist in a specific module space. "All" will look at all types. Which module to get the instances for, in addition to all groups in official modules loaded earlier than the one specified here. -1 means get ALL groups ever reg'd. Ownership is NOT transferred!
---@param group string Ownership of the list or the Entitys placed in it are NOT transferred!
---@param type string The group to randomly select from. "All" will look in all.
---@param whichModuleSpace number The name of the least common denominator type of the Entity:s you want.
---@return Entity
function PresetManager:GetRandomOfGroupInModuleSpace(group, type, whichModuleSpace) end

--- Gets the data file path of a previously read in (defined) Entity. the official modules will be searched also. If no Entity of that description was found, "" is returned.
---@param type string The type name of the derived Entity. Ownership is NOT transferred!
---@param preset string The preset name of the derived Entity preset.
---@param whichModule number Which module to try to get the entity from. If it's not found there,
---@return string
function PresetManager:GetEntityDataLocation(type, preset, whichModule) end

--- Reads a preset of an Entity and tries to add it to the list of read-in instances. Regardless of whether there is a name collision, the read-in preset will be returned, ownership TRANSFERRED! error, or the instance name was 'None'. Ownership IS transferred!
---@param reader Reader The Reader which is about to read in a preset.
---@return Entity
function PresetManager:ReadReflectedPreset(reader) end

--- Reloads the specified Entity preset in PresetMan.
---@param presetName string The preset name of the Entity to reload.
---@param className string The class name of the Entity to reload.
---@param moduleName string The module name of the Entity to reload.
---@return boolean
function PresetManager:ReloadEntityPreset(presetName, className, moduleName) end

---@param presetName string The preset name of the Entity to reload.
---@param className string The class name of the Entity to reload.
---@return boolean
function PresetManager:ReloadEntityPreset(presetName, className) end

--- Gets a list all previously read in (defined) Entities.
---@return Entity[]
function PresetManager:GetAllEntities() end

--- Gets a list all previously read in (defined) Entities which are associated with a specific group.
---@param group string The group to look for. "All" will look in all.
---@param type string The name of the least common denominator type of the Entities you want. "All" will look at all types.
---@param whichModule number Whether to only get those of one specific DataModule (0-n), or all (-1).
---@return Entity[]
function PresetManager:GetAllEntitiesOfGroup(group, type, whichModule) end

---@param group string
---@param type string
---@return Entity[]
function PresetManager:GetAllEntitiesOfGroup(group, type) end

---@param group string
---@return Entity[]
function PresetManager:GetAllEntitiesOfGroup(group) end

--- Reloads all scripted Entity Presets with the latest version of their respective script files.
function PresetManager:ReloadAllScripts() end

--- Returns whether or not the module is vanilla.
---@param moduleName string The name of the module to check, in the form "[moduleName].rte"
---@return boolean
function PresetManager:IsModuleOfficial(moduleName) end

--- Returns whether or not the module is vanilla.
---@param moduleName string The name of the module to check, in the form "[moduleName].rte"
---@return boolean
function PresetManager:IsModuleUserdata(moduleName) end

--- Returns the Full path to the module including Data/, Userdata/ or Mods/.
---@param modulePath string The Path to be completed.
---@return string
function PresetManager:GetFullModulePath(modulePath) end

--- Singleton manager responsible for all primitive drawing.
---@class PrimitiveManager
---@field ClassName string The C++ class name, like "PrimitiveManager".
PrimitiveManager = {}

--- Schedule to draw a line primitive.
---@param startPos Vector Start position of primitive in scene coordinates.
---@param endPos Vector End position of primitive in scene coordinates.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawLinePrimitive(startPos, endPos, color) end

---@param startPos Vector Start position of primitive in scene coordinates.
---@param endPos Vector End position of primitive in scene coordinates.
---@param color number Color to draw primitive with.
---@param thickness number Thickness of the line in pixels.
function PrimitiveManager:DrawLinePrimitive(startPos, endPos, color, thickness) end

---@param startPos number Start position of primitive in scene coordinates.
---@param endPos Vector End position of primitive in scene coordinates.
---@param color Vector Color to draw primitive with.
---@param thickness number Thickness of the line in pixels.
function PrimitiveManager:DrawLinePrimitive(startPos, endPos, color, thickness) end

---@param player number Player screen to draw primitive on.
---@param startPos Vector Start position of primitive in scene coordinates.
---@param endPos Vector End position of primitive in scene coordinates.
---@param color number Color to draw primitive with.
---@param thickness number Thickness of the line in pixels.
function PrimitiveManager:DrawLinePrimitive(player, startPos, endPos, color, thickness) end

--- Schedule to draw an arc primitive.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param startAngle number The angle from which the arc drawing begins.
---@param endAngle number The angle at which the arc drawing ends.
---@param radius number Radius of the arc primitive.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawArcPrimitive(centerPos, startAngle, endAngle, radius, color) end

---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param startAngle number The angle from which the arc drawing begins.
---@param endAngle number The angle at which the arc drawing ends.
---@param radius number Radius of the arc primitive.
---@param color number Color to draw primitive with.
---@param thickness number Thickness of the arc in pixels.
function PrimitiveManager:DrawArcPrimitive(centerPos, startAngle, endAngle, radius, color, thickness) end

---@param centerPos number Position of primitive's center in scene coordinates.
---@param startAngle Vector The angle from which the arc drawing begins.
---@param endAngle number The angle at which the arc drawing ends.
---@param radius number Radius of the arc primitive.
---@param color number Color to draw primitive with.
---@param thickness number Thickness of the arc in pixels.
function PrimitiveManager:DrawArcPrimitive(centerPos, startAngle, endAngle, radius, color, thickness) end

---@param player number Player screen to draw primitive on.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param startAngle number The angle from which the arc drawing begins.
---@param endAngle number The angle at which the arc drawing ends.
---@param radius number Radius of the arc primitive.
---@param color number Color to draw primitive with.
---@param thickness number Thickness of the arc in pixels.
function PrimitiveManager:DrawArcPrimitive(player, centerPos, startAngle, endAngle, radius, color, thickness) end

--- Schedule to draw a Bezier spline primitive.
---@param startPos Vector Start position of primitive in scene coordinates.
---@param guideA Vector The first guide point that controls the curve of the spline. The spline won't necessarily pass through this point, but it will affect it's shape.
---@param guideB Vector The second guide point that controls the curve of the spline. The spline won't necessarily pass through this point, but it will affect it's shape.
---@param endPos Vector End position of primitive in scene coordinates.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawSplinePrimitive(startPos, guideA, guideB, endPos, color) end

---@param player number Player screen to draw primitive on.
---@param startPos Vector Start position of primitive in scene coordinates.
---@param guideA Vector The first guide point that controls the curve of the spline. The spline won't necessarily pass through this point, but it will affect it's shape.
---@param guideB Vector The second guide point that controls the curve of the spline. The spline won't necessarily pass through this point, but it will affect it's shape.
---@param endPos Vector End position of primitive in scene coordinates.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawSplinePrimitive(player, startPos, guideA, guideB, endPos, color) end

--- Schedule to draw a box primitive.
---@param topLeftPos Vector Start position of primitive in scene coordinates. Top left corner.
---@param bottomRightPos Vector End position of primitive in scene coordinates. Bottom right corner.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawBoxPrimitive(topLeftPos, bottomRightPos, color) end

---@param player number Player screen to draw primitive on.
---@param topLeftPos Vector Start position of primitive in scene coordinates. Top left corner.
---@param bottomRightPos Vector End position of primitive in scene coordinates. Bottom right corner.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawBoxPrimitive(player, topLeftPos, bottomRightPos, color) end

--- Schedule to draw a filled box primitive.
---@param topLeftPos Vector Start position of primitive in scene coordinates. Top left corner.
---@param bottomRightPos Vector End position of primitive in scene coordinates. Bottom right corner.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawBoxFillPrimitive(topLeftPos, bottomRightPos, color) end

---@param player number Player screen to draw primitive on.
---@param topLeftPos Vector Start position of primitive in scene coordinates. Top left corner.
---@param bottomRightPos Vector End position of primitive in scene coordinates. Bottom right corner.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawBoxFillPrimitive(player, topLeftPos, bottomRightPos, color) end

--- Schedule to draw a rounded box primitive.
---@param topLeftPos Vector Start position of primitive in scene coordinates. Top left corner.
---@param bottomRightPos Vector End position of primitive in scene coordinates. Bottom right corner.
---@param cornerRadius number The radius of the corners of the box. Smaller radius equals sharper corners.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawRoundedBoxPrimitive(topLeftPos, bottomRightPos, cornerRadius, color) end

---@param player number Player screen to draw primitive on.
---@param topLeftPos Vector Start position of primitive in scene coordinates. Top left corner.
---@param bottomRightPos Vector End position of primitive in scene coordinates. Bottom right corner.
---@param cornerRadius number The radius of the corners of the box. Smaller radius equals sharper corners.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawRoundedBoxPrimitive(player, topLeftPos, bottomRightPos, cornerRadius, color) end

--- Schedule to draw a filled rounded box primitive.
---@param topLeftPos Vector Start position of primitive in scene coordinates. Top left corner.
---@param bottomRightPos Vector End position of primitive in scene coordinates. Bottom right corner.
---@param cornerRadius number The radius of the corners of the box. Smaller radius equals sharper corners.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawRoundedBoxFillPrimitive(topLeftPos, bottomRightPos, cornerRadius, color) end

---@param player number Player screen to draw primitive on.
---@param topLeftPos Vector Start position of primitive in scene coordinates. Top left corner.
---@param bottomRightPos Vector End position of primitive in scene coordinates. Bottom right corner.
---@param cornerRadius number The radius of the corners of the box. Smaller radius equals sharper corners.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawRoundedBoxFillPrimitive(player, topLeftPos, bottomRightPos, cornerRadius, color) end

--- Schedule to draw a circle primitive.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param radius number Radius of circle primitive.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawCirclePrimitive(centerPos, radius, color) end

---@param player number Player screen to draw primitive on.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param radius number Radius of circle primitive.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawCirclePrimitive(player, centerPos, radius, color) end

--- Schedule to draw a filled circle primitive.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param radius number Radius of circle primitive.
---@param color number Color to fill primitive with.
function PrimitiveManager:DrawCircleFillPrimitive(centerPos, radius, color) end

---@param player number Player screen to draw primitive on.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param radius number Radius of circle primitive.
---@param color number Color to fill primitive with.
function PrimitiveManager:DrawCircleFillPrimitive(player, centerPos, radius, color) end

--- Schedule to draw an ellipse primitive.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param horizRadius number Horizontal radius of the ellipse primitive.
---@param vertRadius number Vertical radius of the ellipse primitive.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawEllipsePrimitive(centerPos, horizRadius, vertRadius, color) end

---@param player number Player screen to draw primitive on.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param horizRadius number Horizontal radius of the ellipse primitive.
---@param vertRadius number Vertical radius of the ellipse primitive.
---@param color number Color to draw primitive with.
function PrimitiveManager:DrawEllipsePrimitive(player, centerPos, horizRadius, vertRadius, color) end

--- Schedule to draw a filled ellipse primitive.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param horizRadius number Horizontal radius of the ellipse primitive.
---@param vertRadius number Vertical radius of the ellipse primitive.
---@param color number Color to fill primitive with.
function PrimitiveManager:DrawEllipseFillPrimitive(centerPos, horizRadius, vertRadius, color) end

---@param player number Player screen to draw primitive on.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param horizRadius number Horizontal radius of the ellipse primitive.
---@param vertRadius number Vertical radius of the ellipse primitive.
---@param color number Color to fill primitive with.
function PrimitiveManager:DrawEllipseFillPrimitive(player, centerPos, horizRadius, vertRadius, color) end

--- Schedule to draw a triangle primitive.
---@param pointA Vector Position of the first point of the triangle in scene coordinates.
---@param pointB Vector Position of the second point of the triangle in scene coordinates.
---@param pointC Vector Position of the third point of the triangle in scene coordinates.
---@param color number Color to fill primitive with.
function PrimitiveManager:DrawTrianglePrimitive(pointA, pointB, pointC, color) end

---@param player number Player screen to draw primitive on.
---@param pointA Vector Position of the first point of the triangle in scene coordinates.
---@param pointB Vector Position of the second point of the triangle in scene coordinates.
---@param pointC Vector Position of the third point of the triangle in scene coordinates.
---@param color number Color to fill primitive with.
function PrimitiveManager:DrawTrianglePrimitive(player, pointA, pointB, pointC, color) end

--- Schedule to draw a filled triangle primitive.
---@param pointA Vector Position of the first point of the triangle in scene coordinates.
---@param pointB Vector Position of the second point of the triangle in scene coordinates.
---@param pointC Vector Position of the third point of the triangle in scene coordinates.
---@param color number Color to fill primitive with.
function PrimitiveManager:DrawTriangleFillPrimitive(pointA, pointB, pointC, color) end

---@param player number Player screen to draw primitive on.
---@param pointA Vector Position of the first point of the triangle in scene coordinates.
---@param pointB Vector Position of the second point of the triangle in scene coordinates.
---@param pointC Vector Position of the third point of the triangle in scene coordinates.
---@param color number Color to fill primitive with.
function PrimitiveManager:DrawTriangleFillPrimitive(player, pointA, pointB, pointC, color) end

--- Schedule to draw a text primitive.
---@param start Vector Start position of primitive in scene coordinates.
---@param text string Text string to draw.
---@param isSmall boolean Use small or large font. True for small font.
---@param alignment number Alignment of text.
function PrimitiveManager:DrawTextPrimitive(start, text, isSmall, alignment) end

---@param start Vector Start position of primitive in scene coordinates.
---@param text string Text string to draw.
---@param isSmall boolean Use small or large font. True for small font.
---@param alignment number Alignment of text.
---@param rotAngle number Angle to rotate text in radians.
function PrimitiveManager:DrawTextPrimitive(start, text, isSmall, alignment, rotAngle) end

---@param start number Start position of primitive in scene coordinates.
---@param text Vector Text string to draw.
---@param isSmall string Use small or large font. True for small font.
---@param alignment boolean Alignment of text.
---@param rotAngle number Angle to rotate text in radians.
function PrimitiveManager:DrawTextPrimitive(start, text, isSmall, alignment, rotAngle) end

---@param player number Player screen to draw primitive on.
---@param start Vector Start position of primitive in scene coordinates.
---@param text string Text string to draw.
---@param isSmall boolean Use small or large font. True for small font.
---@param alignment number Alignment of text.
---@param rotAngle number Angle to rotate text in radians.
function PrimitiveManager:DrawTextPrimitive(player, start, text, isSmall, alignment, rotAngle) end

--- Schedule to draw a bitmap primitive.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param moSprite MOSprite A MOSprite to draw BITMAP from.
---@param rotAngle number Rotation angle in radians.
---@param frame number Frame to draw.
function PrimitiveManager:DrawBitmapPrimitive(centerPos, moSprite, rotAngle, frame) end

---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param moSprite MOSprite A MOSprite to draw BITMAP from.
---@param rotAngle number Rotation angle in radians.
---@param frame number Frame to draw.
---@param hFlipped boolean Whether to flip the sprite horizontally.
---@param vFlipped boolean Whether to flip the sprite vertically.
function PrimitiveManager:DrawBitmapPrimitive(centerPos, moSprite, rotAngle, frame, hFlipped, vFlipped) end

---@param player number Player screen to draw primitive on.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param moSprite MOSprite A MOSprite to draw BITMAP from.
---@param rotAngle number Rotation angle in radians.
---@param frame number Frame to draw.
function PrimitiveManager:DrawBitmapPrimitive(player, centerPos, moSprite, rotAngle, frame) end

---@param player number Player screen to draw primitive on.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param moSprite MOSprite A MOSprite to draw BITMAP from.
---@param rotAngle number Rotation angle in radians.
---@param frame number Frame to draw.
---@param hFlipped boolean Whether to flip the sprite horizontally.
---@param vFlipped boolean Whether to flip the sprite vertically.
function PrimitiveManager:DrawBitmapPrimitive(player, centerPos, moSprite, rotAngle, frame, hFlipped, vFlipped) end

---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param filePath string Path to the bitmap to draw.
---@param rotAngle number Rotation angle in radians.
function PrimitiveManager:DrawBitmapPrimitive(centerPos, filePath, rotAngle) end

---@param player Vector Player screen to draw primitive on.
---@param centerPos string Position of primitive's center in scene coordinates.
---@param moSprite number A MOSprite to draw BITMAP from.
---@param rotAngle boolean Rotation angle in radians.
---@param frame boolean Frame to draw.
function PrimitiveManager:DrawBitmapPrimitive(player, centerPos, moSprite, rotAngle, frame) end

---@param centerPos number Position of primitive's center in scene coordinates.
---@param moSprite Vector A MOSprite to draw BITMAP from.
---@param rotAngle string Rotation angle in radians.
---@param frame number Frame to draw.
function PrimitiveManager:DrawBitmapPrimitive(centerPos, moSprite, rotAngle, frame) end

---@param centerPos number Position of primitive's center in scene coordinates.
---@param moSprite Vector A MOSprite to draw BITMAP from.
---@param rotAngle string Rotation angle in radians.
---@param frame number Frame to draw.
---@param hFlipped boolean Whether to flip the sprite horizontally.
---@param vFlipped boolean Whether to flip the sprite vertically.
function PrimitiveManager:DrawBitmapPrimitive(centerPos, moSprite, rotAngle, frame, hFlipped, vFlipped) end

--- Schedule to draw the GUI icon of an object.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param entity Entity An entity to draw sprite from.
function PrimitiveManager:DrawIconPrimitive(centerPos, entity) end

---@param player number Player screen to draw primitive on.
---@param centerPos Vector Position of primitive's center in scene coordinates.
---@param entity Entity An entity to draw sprite from.
function PrimitiveManager:DrawIconPrimitive(player, centerPos, entity) end

--- Schedule to draw a polygon primitive.
---@param centerPos Vector Position of primitive's center in Scene coordinates.
---@param color number Color to draw primitive with.
---@param verticesTable any A Lua table that contains the positions of the primitive's vertices, relative to the center position.
function PrimitiveManager:DrawPolygonPrimitive(centerPos, color, verticesTable) end

---@param player number Player screen to draw primitive on.
---@param centerPos Vector Position of primitive's center in Scene coordinates.
---@param color number Color to draw primitive with.
---@param verticesTable any A Lua table that contains the positions of the primitive's vertices, relative to the center position.
function PrimitiveManager:DrawPolygonPrimitive(player, centerPos, color, verticesTable) end

--- Schedule to draw a filled polygon primitive.
---@param startPos Vector Start position of the primitive in Scene coordinates.
---@param color number Color to draw primitive with.
---@param verticesTable any A Lua table that contains the positions of the primitive's vertices, relative to the center position.
function PrimitiveManager:DrawPolygonFillPrimitive(startPos, color, verticesTable) end

---@param player number Player screen to draw primitive on.
---@param startPos Vector Start position of the primitive in Scene coordinates.
---@param color number Color to draw primitive with.
---@param verticesTable any A Lua table that contains the positions of the primitive's vertices, relative to the center position.
function PrimitiveManager:DrawPolygonFillPrimitive(player, startPos, color, verticesTable) end

--- Schedules to draw multiple primitives of varying type with transparency enabled.
---@param transValue number The transparency value the primitives should be drawn at. From 0 (opaque) to 100 (transparent).
---@param primitivesTable any A Lua table of primitives to schedule drawing for.
function PrimitiveManager:DrawPrimitives(transValue, primitivesTable) end

---@param blendMode number The blending mode the primitives should be drawn with. See DrawBlendMode enumeration.
---@param blendAmount number The blending amount for all the channels. 0-100.
---@param primitivesTable any A Lua table of primitives to schedule drawing for.
function PrimitiveManager:DrawPrimitives(blendMode, blendAmount, primitivesTable) end

---@param blendMode number The blending mode the primitives should be drawn with. See DrawBlendMode enumeration.
---@param blendAmountR number The blending amount for the Red channel. 0-100.
---@param blendAmountG number The blending amount for the Green channel. 0-100.
---@param blendAmountB number The blending amount for the Blue channel. 0-100.
---@param blendAmountA number The blending amount for the Alpha channel. 0-100.
---@param primitivesTable any A Lua table of primitives to schedule drawing for.
function PrimitiveManager:DrawPrimitives(blendMode, blendAmountR, blendAmountG, blendAmountB, blendAmountA, primitivesTable) end

--- The singleton manager of all terrain and backgrounds in the RTE.
---@class SceneManager
---@field ClassName string The C++ class name, like "SceneManager".
---@field Scene Scene Gets the currently loaded scene, if any.
---@field SceneDim Vector Gets the total dimensions (width and height) of the scene, in pixels.
---@field SceneWidth number Gets the total width of the scene, in pixels.
---@field SceneHeight number Gets the total height of the scene, in pixels.
---@field SceneWrapsX boolean Indicates whether the scene wraps its scrolling around the X axis.
---@field SceneWrapsY boolean Indicates whether the scene wraps its scrolling around the Y axis.
---@field SceneOrbitDirection Directions Gets the orbit direction for the current scene.
---@field LayerDrawMode number Gets the current drawing mode of the SceneMan. different possible mode settings.
---@field GlobalAcc Vector Gets the global acceleration (in m/s^2) that is applied to all movable objects' velocities during every frame. Typically models gravity.
---@field OzPerKg number Gets how many Ounces there are in a metric Kilogram
---@field KgPerOz number Gets how many metric Kilograms there are in an Ounce.
---@field ScrapCompactingHeight number Gets the maximum height of a column of scrap terrain to collapse, when the bottom pixel is knocked loose.
SceneManager = {}

--- Actually loads a new Scene into memory. has to be done before using this object. in its definition. Anything below 0 is an error signal.
---@param pNewScene string The instance of the Scene, ownership IS transferred!
---@param placeObjects? boolean Whether the scene should actually apply all its SceneObject:s placed (default: true)
---@param placeUnits? boolean
---@return number
function SceneManager:LoadScene(pNewScene, placeObjects, placeUnits) end

---@param sceneName string The name of the Scene preset instance to load.
---@param placeObjects? boolean Whether the scene should actually apply all its SceneObject:s placed (default: true) { return LoadScene(sceneName)
---@return number
function SceneManager:LoadScene(sceneName, placeObjects) end

--- Gets the SLTerrain, or 0 if no scene is loaded.
---@return SLTerrain
function SceneManager:GetTerrain() end

--- Gets a specific material by name. Ownership is NOT transferred! name was found.
---@param matName string The string name of the Material to get.
---@return Material
function SceneManager:GetMaterial(matName) end

--- Gets a specific material from the material palette. Ownership is NOT transferred!
---@param screen number The unsigned char index specifying screen material to get (0-255).
---@return Material
function SceneManager:GetMaterialFromID(screen) end

--- Gets a specific pixel from the total material representation of this Scene. LockScene() must be called before using this method.
---@param pixelX number The X and Y coordinates of screen material pixel to get.
---@param pixelY number
---@return number
function SceneManager:GetTerrMatter(pixelX, pixelY) end

--- Gets a MOID from pixel coordinates in the Scene. LockScene() must be called before using this method.
---@param pixelX number The X coordinate of the Scene pixel to test.
---@param pixelY number The Y coordinate of the Scene pixel to test.
---@return number
function SceneManager:GetMOIDPixel(pixelX, pixelY) end

---@param pixelX number The X coordinate of the Scene pixel to test.
---@param pixelY number The Y coordinate of the Scene pixel to test.
---@param ignoreTeam number The team to ignore.
---@return number
function SceneManager:GetMOIDPixel(pixelX, pixelY, ignoreTeam) end

--- Sets the drawing mode of the SceneMan, to easily view what's going on in the different internal SceneLayer:s. different possible settings.
---@param mode number The layer mode to draw in, see the LayerDrawMode enumeration for the
function SceneManager:SetLayerDrawMode(mode) end

--- Loads a bitmap from file and use it as the unseen layer for a team.
---@param bitmapPath string The path to the bitmap to use as the unseen layer.
---@param team number Which team we're talking about.
---@return boolean
function SceneManager:LoadUnseenLayer(bitmapPath, team) end

--- Sets one team's view of the scene to be unseen, using a generated map of a specific resolution chunkiness.
---@param pixelSize Vector The dimensions of the pixels that should make up the unseen layer.
---@param team number The team we're talking about.
function SceneManager:MakeAllUnseen(pixelSize, team) end

--- Tells whether a team has anything still unseen on the scene.
---@param team number The team we're talking about.
---@return boolean
function SceneManager:AnythingUnseen(team) end

--- Shows what the resolution factor of the unseen map to the entire Scene is, in both axes.
---@param team number The team we're talking about.
---@return Vector
function SceneManager:GetUnseenResolution(team) end

--- Checks whether a pixel is in an unseen area on of a specific team.
---@param posX number The X and Y coords of the scene pixel that is to be checked.
---@param posY number The team we're talking about.
---@param team number
---@return boolean
function SceneManager:IsUnseen(posX, posY, team) end

--- Reveals a pixel on the unseen map for a specific team, if there is any.
---@param posX number The X and Y coord of the scene pixel that is to be revealed.
---@param posY number The team to reveal for.
---@param team number
---@return boolean
function SceneManager:RevealUnseen(posX, posY, team) end

--- Reveals a box on the unseen map for a specific team, if there is any.
---@param posX number The X and Y coords of the upper left corner of the box to be revealed.
---@param posY number The width and height of the box to be revealed, in scene units (pixels)
---@param width number The team to reveal for.
---@param height number
---@param team number
function SceneManager:RevealUnseenBox(posX, posY, width, height, team) end

--- Hides a pixel on the unseen map for a specific team, if there is any.
---@param posX number The X and Y coord of the scene pixel that is to be revealed.
---@param posY number The team to hide for.
---@param team number
---@return boolean
function SceneManager:RestoreUnseen(posX, posY, team) end

--- Restores a box on the unseen map for a specific team, if there is any.
---@param posX number The X and Y coords of the upper left corner of the box to be revealed.
---@param posY number The width and height of the box to be restored, in scene units (pixels)
---@param width number The team to restore for.
---@param height number
---@param team number
function SceneManager:RestoreUnseenBox(posX, posY, width, height, team) end

--- Traces along a vector and reveals pixels on the unseen layer of a team as long as the accumulated material strengths traced through the terrain don't exceed a specific value. terminated. If it reached the end, it will be set to the end of the ray. for optimization reasons. 0 = every pixel is checked.
---@param team number The team to see for.
---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param endPos Vector A Vector that will be set to the position of where the sight ray was
---@param strengthLimit number The material strength limit where
---@param skip? number For every pixel checked along the line, how many to skip between them (default: 0)
---@return boolean
function SceneManager:CastSeeRay(team, start, ray, endPos, strengthLimit, skip) end

--- Traces along a vector and hides pixels on the unseen layer of a team as long as the accumulated material strengths traced through the terrain don't exceed a specific value. terminated. If it reached the end, it will be set to the end of the ray. for optimization reasons. 0 = every pixel is checked.
---@param team number The team to see for.
---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param endPos Vector A Vector that will be set to the position of where the sight ray was
---@param strengthLimit number The material strength limit where
---@param skip? number For every pixel checked along the line, how many to skip between them (default: 0)
---@return boolean
function SceneManager:CastUnseeRay(team, start, ray, endPos, strengthLimit, skip) end

--- Traces along a vector and reveals or hides pixels on the unseen layer of a team as long as the accumulated material strengths traced through the terrain don't exceed a specific value. terminated. If it reached the end, it will be set to the end of the ray. for optimization reasons. 0 = every pixel is checked.
---@param team number The team to see for.
---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param endPos Vector A Vector that will be set to the position of where the sight ray was
---@param strengthLimit number The material strength limit where
---@param skip number For every pixel checked along the line, how many to skip between them
---@param reveal boolean Whether the ray should reveal or restore unseen layer
---@return boolean
function SceneManager:CastUnseenRay(team, start, ray, endPos, strengthLimit, skip, reveal) end

--- Traces along a vector and gets the location of the first encountered pixel of a specific material in the terrain. location of the found terrain pixel of the above material. for optimization reasons. 0 = every pixel is checked. parameter will not have been altered (and may still not be 0!)
---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param material number The material ID to look for.
---@param result Vector A reference to the vector screen will be filled out with the absolute
---@param skip? number For every pixel checked along the line, how many to skip between them (default: 0)
---@param wrap? boolean Whetehr the ray should wrap around the scene if it crosses a seam. (default: true)
---@return boolean
function SceneManager:CastMaterialRay(start, ray, material, result, skip, wrap) end

---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param material number The material ID to look for.
---@param skip? number For every pixel checked along the line, how many to skip between them (default: 0)
---@return number
function SceneManager:CastMaterialRay(start, ray, material, skip) end

--- Traces along a vector and gets the location of the first encountered pixel that is NOT of a specific material in the scene's terrain. location of the found terrain pixel of the above material. for optimization reasons. 0 = every pixel is checked. If not, the fourth parameter will not have been altered (and may still not be 0!)
---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param material number The material ID to find something OTHER than.
---@param result Vector A reference to the vector screen will be filled out with the absolute
---@param skip? number For every pixel checked along the line, how many to skip between them (default: 0)
---@param checkMOs? boolean Whether to check for MO layer collisions as well, not just terrain. (default: false)
---@return boolean
function SceneManager:CastNotMaterialRay(start, ray, material, result, skip, checkMOs) end

---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param material number The material ID to find something OTHER than.
---@param skip? number For every pixel checked along the line, how many to skip between them (default: 0)
---@param checkMOs? boolean Whether to check for MO layer collisions as well, not just terrain. (default: false)
---@return number
function SceneManager:CastNotMaterialRay(start, ray, material, skip, checkMOs) end

--- Traces along a vector and returns how the sum of all encountered pixels' material strength values. This will take wrapping into account. for optimization reasons. 0 = every pixel is checked. all Air, then 0 is returned (Air's strength value is 0).
---@param start Vector The starting position.
---@param end Vector The ending position.
---@param skip? number For every pixel checked along the line, how many to skip between them (default: 0)
---@param ignoreMaterial? number A material ID to ignore, IN ADDITION to Air. (default: g_MaterialAir)
---@return number
function SceneManager:CastStrengthSumRay(start, end, skip, ignoreMaterial) end

--- Traces along a vector and returns the strongest of all encountered pixels' material strength values. This will take wrapping into account. for optimization reasons. 0 = every pixel is checked. all Air, then 0 is returned (Air's strength value is 0). We use two accessors instead of default parameters, for lua compat
---@param start Vector The starting position.
---@param end Vector The ending position.
---@param skip number For every pixel checked along the line, how many to skip between them
---@param ignoreMaterial number A material ID to ignore, IN ADDITION to Air. This defaults to doors, for legacy script purposes
---@return number
function SceneManager:CastMaxStrengthRay(start, end, skip, ignoreMaterial) end

---@param start Vector
---@param end Vector
---@param skip number
---@return number
function SceneManager:CastMaxStrengthRay(start, end, skip) end

--- Traces along a vector and shows where along that ray there is an encounter with a pixel of a material with strength more than or equal to a specific value. terminate the ray. location of the found terrain pixel of less than or equal to above strength. for optimization reasons. 0 = every pixel is checked. If not, the fourth parameter have been set to last position of the ray.
---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param strength number The strength value of screen any found to be equal or more than will
---@param result Vector A reference to the vector screen will be filled out with the absolute
---@param skip? number For every pixel checked along the line, how many to skip between them (default: 0)
---@param ignoreMaterial? number A material ID to ignore, IN ADDITION to Air. (default: g_MaterialAir)
---@param wrap? boolean Whetehr the ray should wrap around the scene if it crosses a seam. (default: true)
---@return boolean
function SceneManager:CastStrengthRay(start, ray, strength, result, skip, ignoreMaterial, wrap) end

--- Traces along a vector and shows where along that ray there is an encounter with a pixel of a material with strength less than or equal to a specific value. terminate the ray. location of the found terrain pixel of less than or equal to above strength. for optimization reasons. 0 = every pixel is checked. If not, the fourth parameter have been set to last position of the ray.
---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param strength number The strength value of screen any found to be equal or less than will
---@param result Vector A reference to the vector screen will be filled out with the absolute
---@param skip? number For every pixel checked along the line, how many to skip between them (default: 0)
---@param wrap? boolean Whetehr the ray should wrap around the scene if it crosses a seam. (default: true)
---@return boolean
function SceneManager:CastWeaknessRay(start, ray, strength, result, skip, wrap) end

--- Traces along a vector and returns MOID of the first non-ignored non-NoMOID MO encountered. If a non-air terrain pixel is encountered first, g_NoMOID will be returned. team which also has team ignoring enabled itself. for optimization reasons. 0 = every pixel is checked.
---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param ignoreMOID? number An MOID to ignore. Any child MO's of this MOID will also be ignored. (default: g_NoMOID)
---@param ignoreTeam? number To enable ignoring of all MOIDs associated with an object of a specific (default: Activity::NoTeam)
---@param ignoreMaterial? number A specific material ID to ignore hits with. (default: 0)
---@param ignoreAllTerrain? boolean Whether to ignore all terrain hits or not. (default: false)
---@param skip? number For every pixel checked along the line, how many to skip between them (default: 0)
---@return number
function SceneManager:CastMORay(start, ray, ignoreMOID, ignoreTeam, ignoreMaterial, ignoreAllTerrain, skip) end

--- Traces along a vector and shows where a specific MOID has been found. location of the found MO pixel of the above MOID. for optimization reasons. 0 = every pixel is checked.
---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param targetMOID number An MOID to find. Any child MO's of this MOID will also be found. ------------ ???
---@param resultPos Vector A reference to the vector screen will be filled out with the absolute
---@param ignoreMaterial? number A specific material ID to ignore hits with. (default: 0)
---@param ignoreAllTerrain? boolean Whether to ignore all terrain hits or not. (default: false)
---@param skip? number For every pixel checked along the line, how many to skip between them (default: 0)
---@return boolean
function SceneManager:CastFindMORay(start, ray, targetMOID, resultPos, ignoreMaterial, ignoreAllTerrain, skip) end

--- Traces along a vector and returns the length of how far the trace went without hitting any non-ignored terrain material or MOID at all. location of the first obstacle, or the end of the ray if none was hit. location of the last free position before hitting an obstacle, or the end of the ray if none was hit. This is only altered if thre are any free pixels encountered. team which also has team ignoring enabled itself. for optimization reasons. 0 = every pixel is checked. encountered. If no pixel of the right material was found, < 0 is returned. If an obstacle on the starting position was encountered, 0 is returned.
---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param obstaclePos Vector A reference to the vector screen will be filled out with the absolute
---@param freePos Vector A reference to the vector screen will be filled out with the absolute
---@param ignoreMOID? number An MOID to ignore. Any child MO's of this MOID will also be ignored. (default: g_NoMOID)
---@param ignoreTeam? number To enable ignoring of all MOIDs associated with an object of a specific (default: Activity::NoTeam)
---@param ignoreMaterial? number A specific material ID to ignore hits with. (default: 0)
---@param skip? number For every pixel checked along the line, how many to skip between them (default: 0)
---@return number
function SceneManager:CastObstacleRay(start, ray, obstaclePos, freePos, ignoreMOID, ignoreTeam, ignoreMaterial, skip) end

--- Traces along a vector and stops when the accumulated material strengths of the traced-through terrain meets or exceeds a given value. terminated. If it reached the end, it will be set to the end of the ray. for optimization reasons. 0 = every pixel is checked.
---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param endPos Vector A Vector that will be set to the position of where the sight ray was
---@param strengthLimit number The accumulated material strength limit where the ray stops.
---@param skip number For every pixel checked along the line, how many to skip between them
---@return boolean
function SceneManager:CastTerrainPenetrationRay(start, ray, endPos, strengthLimit, skip) end

--- Gets the abosulte pos of where the last cast ray hit somehting.
---@return Vector
function SceneManager:GetLastRayHitPos() end

--- Calculates the altitide of a certain point above the terrain, measured in pixels. here means less calculation.
---@param from Vector
---@param max number
---@param accuracy number
---@return number
function SceneManager:FindAltitude(from, max, accuracy) end

---@param from Vector The max altitude you care to check for. 0 Means check the whole scene's height.
---@param max number The accuracy within screen measurement is acceptable. Higher number
---@param accuracy number
---@param fromSceneOrbitDirection boolean
---@return number
function SceneManager:FindAltitude(from, max, accuracy, fromSceneOrbitDirection) end

--- Takes an arbitrary point in the air and calculates it to be straight down at a certain maximum distance from the ground. be returned (null operation) here means less calculation. the terrain.
---@param from Vector The point to start from. Should be in the air, or the same point will
---@param maxAltitude? number The max altitude in px you want the point to be above the ground. (default: 0)
---@param accuracy? number The accuracy within screen measurement is acceptable. Higher number (default: 0)
---@return Vector
function SceneManager:MovePointToGround(from, maxAltitude, accuracy) end

--- Returns whether the integer coordinates passed in are within the bounds of the current Scene, considering its wrapping.
---@param pixelX number Int coordinates.
---@param pixelY number A margin
---@param margin? number
---@return boolean
function SceneManager:IsWithinBounds(pixelX, pixelY, margin) end

--- Wraps or bounds a position coordinate if it is off bounds of the Scene, depending on the wrap settings of this Scene.
---@param pos Vector The vector coordinates of the position to wrap, if needed.
---@return boolean
function SceneManager:ForceBounds(pos) end

--- Only wraps a position coordinate if it is off bounds of the Scene and wrapping in the corresponding axes are turned on.
---@param pos Vector The vector coordinates of the position to wrap, if needed.
---@return boolean
function SceneManager:WrapPosition(pos) end

--- Returns a position snapped to the current scene grid.
---@param pos Vector The vector coordinates of the position to snap.
---@param snap? boolean Whether to actually snap or not. This is useful for cleaner toggle code. (default: true)
---@return Vector
function SceneManager:SnapPosition(pos, snap) end

--- Calculates the shortest distance between two points in scene coordinates, taking into account all wrapping and out of bounds of the two points. distance between. wrap them if they are. wrapping borders etc. Basically the ideal pos2 - pos1.
---@param pos1 Vector The two Vector coordinates of the two positions to find the shortest
---@param pos2 Vector Whether to check if the passed in points are outside the scene, and to
---@param checkBounds? boolean
---@return Vector
function SceneManager:ShortestDistance(pos1, pos2, checkBounds) end

--- Takes a Box and returns a list of Boxes that describe the Box, wrapped appropriately for the current Scene.
---@param boxToWrap Box The Box to wrap.
---@return Box[]
function SceneManager:WrapBox(boxToWrap) end

--- Tells whether a point on the scene is obscured by MOID or Terrain non-air material. don't check this.
---@param point Vector The point on the scene to check.
---@param team? number Wheter to also check for unseen areas of a specific team. -1 means (default: -1) { return ObscuredPoint(point.GetFloorIntX())
---@return boolean
function SceneManager:ObscuredPoint(point, team) end

---@param x number The point on the scene to check.
---@param y number Wheter to also check for unseen areas of a specific team. -1 means
---@param team? number
---@return boolean
function SceneManager:ObscuredPoint(x, y, team) end

--- Takes any scene object and adds it to the scene in the appropriate way. If it's a TerrainObject, then it gets applied to the terrain, if it's an MO, it gets added to the correct type group in MovableMan. ownership was transferred. If no success, the object was deleted.
---@param pObject SceneObject The SceneObject to add. Ownership IS transferred!
---@return boolean
function SceneManager:AddSceneObject(pObject) end

--- Returns the area of an orphaned region at specified coordinates. Whether to clear internal terrain tracking bitmap or not
---@param posX number Coordinates to check for region, whether the orphaned region should be converted into MOPixels and region removed.
---@param posY number Area of orphaned object calculated during recursve function call to check if we're out of limits
---@param radius number Size of the are to look for orphaned objects
---@param maxArea number Max area of orphaned object to remove
---@param remove? boolean Whether to actually remove orphaned pixels or not (default: false)
---@return number
function SceneManager:CheckAndRemoveOrphans(posX, posY, radius, maxArea, remove) end

--- Removes a pixel from the terrain and adds it to MovableMan.
---@param posX number The Y coordinate of the terrain pixel.
---@param posY number
---@return MOPixel
function SceneManager:DislodgePixel(posX, posY) end

---@param posX number The Y coordinate of the terrain pixel.
---@param posY number
---@param deletePixel boolean Whether or not to immediately mark the pixel for deletion.
---@return MOPixel
function SceneManager:DislodgePixel(posX, posY, deletePixel) end

--- Removes a line of pixels from the terrain and adds them to MovableMan.
---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param skip number For every pixel checked along the line, how many to skip between them.
---@param deletePixels boolean Whether or not to immediately mark all found pixels for deletion.
---@return MOPixel[]
function SceneManager:DislodgePixelLine(start, ray, skip, deletePixels) end

---@param start Vector The starting position.
---@param ray Vector The vector to trace along.
---@param skip number For every pixel checked along the line, how many to skip between them.
---@return MOPixel[]
function SceneManager:DislodgePixelLine(start, ray, skip) end

--- Removes a circle of pixels from the terrain and adds them to MovableMan.
---@param centre Vector The vector position of the centre of the circle.
---@param radius number The radius of the circle of pixels to remove.
---@param deletePixels boolean Whether or not to immediately mark all found pixels for deletion.
---@return MOPixel[]
function SceneManager:DislodgePixelCircle(centre, radius, deletePixels) end

---@param centre Vector The vector position of the centre of the circle.
---@param radius number The radius of the circle of pixels to remove.
---@return MOPixel[]
function SceneManager:DislodgePixelCircle(centre, radius) end

--- Removes a box of pixels from the terrain and adds them to MovableMan.
---@param upperLeftCorner Vector The vector position of the upper left corner of the box.
---@param lowerRightCorner Vector The vector position of the lower right corner of the box.
---@param deletePixels boolean Whether or not to immediately mark all found pixels for deletion.
---@return MOPixel[]
function SceneManager:DislodgePixelBox(upperLeftCorner, lowerRightCorner, deletePixels) end

---@param upperLeftCorner Vector The vector position of the upper left corner of the box.
---@param lowerRightCorner Vector The vector position of the lower right corner of the box.
---@return MOPixel[]
function SceneManager:DislodgePixelBox(upperLeftCorner, lowerRightCorner) end

--- Removes a ring of pixels from the terrain and adds them to MovableMan.
---@param centre Vector The vector position of the centre of the ring.
---@param innerRadius number The inner radius of the ring of pixels to remove.
---@param outerRadius number The outer radius of the ring of pixels to remove.
---@param deletePixels boolean Whether or not to immediately mark all found pixels for deletion.
---@return MOPixel[]
function SceneManager:DislodgePixelRing(centre, innerRadius, outerRadius, deletePixels) end

---@param centre Vector The vector position of the centre of the ring.
---@param innerRadius number The inner radius of the ring of pixels to remove.
---@param outerRadius number The outer radius of the ring of pixels to remove.
---@return MOPixel[]
function SceneManager:DislodgePixelRing(centre, innerRadius, outerRadius) end

--- The singleton manager of the camera for each player.
---@class CameraManager
---@field ClassName string The C++ class name, like "CameraManager".
CameraManager = {}

--- Gets the offset (scroll position) of the terrain.
---@param screenId? number Which screen you want to get the offset of.
---@return Vector
function CameraManager:GetOffset(screenId) end

--- Sets the offset (scroll position) of the terrain.
---@param offset Vector The new offset value.
---@param screenId? number Which screen you want to set the offset of.
function CameraManager:SetOffset(offset, screenId) end

--- Gets the amount that a specific screen is occluded by a GUI panel or something of the sort. This will affect how the scroll target translates into the offset of the screen, in order to keep the target centered on the screen.
---@param screenId? number Which screen you want to get the team of.
---@return Vector
function CameraManager:GetScreenOcclusion(screenId) end

--- Sets the amount that a specific screen is occluded by a GUI panel or something of the sort. This will affect how the scroll target translates into the offset of the screen, in order to keep the target centered on the screen.
---@param occlusion Vector The amount of occlusion of the screen.
---@param screenId? number Which screen you want to set the occlusion of.
function CameraManager:SetScreenOcclusion(occlusion, screenId) end

--- Gets the currently set scroll target, i.e. where the center of the specific screen is trying to line up with.
---@param screenId? number Which screen to get the target for.
---@return Vector
function CameraManager:GetScrollTarget(screenId) end

--- Interpolates a smooth scroll of the view from wherever it is now, towards centering on a new scroll target over time.
---@param targetCenter Vector The new target vector in Scene coordinates.
---@param speed? number The normalized speed at screen the view scrolls. 0 being no movement, and 1.0 being instant movement to the target in one frame.
---@param screenId? number Which screen you want to set the scroll offset of.
function CameraManager:SetScrollTarget(targetCenter, speed, screenId) end

--- Calculates a scalar of how distant a certain point in the world is from the currently closest scroll target of all active screens. A normalized scalar representing the distance between the closest scroll target of all active screens, to the passed in point. 0 means it's the point is within half a screen's width of the target, and 1.0 means it's on the clear opposite side of the scene.
---@param point Vector The world coordinate point to check distance to/from.
---@return number
function CameraManager:TargetDistanceScalar(point) end

--- Makes sure the current offset won't create a view of outside the scene. If that is found to be the case, the offset is corrected so that the view rectangle is as close to the old offset as possible, but still entirely within the scene world.
---@param screenId? number Which screen you want to check the offset of.
function CameraManager:CheckOffset(screenId) end

--- Sets the offset (scroll position) of the terrain to center on specific world coordinates. If the coordinate to center on is close to the terrain border edges, the view will not scroll outside the borders.
---@param center Vector The coordinates to center the terrain scroll on.
---@param screenId? number Which screen you want to set the offset of.
function CameraManager:SetScroll(center, screenId) end

--- Increases the magnitude of screen shake. This is used for spatially located screen-shake, and will automatically determine which screens have shake applied If the screen-shake position is outside our view, it'll gradually weaken and fade away depending on distance.
---@param magnitude number The amount of screen shake.
---@param position number The spatial location of the screen-shake event.
function CameraManager:AddScreenShake(magnitude, position) end

---@param magnitude number The amount of screen shake.
---@param position Vector The spatial location of the screen-shake event.
function CameraManager:AddScreenShake(magnitude, position) end

--- The singleton manager over the application and misc settings.
---@class SettingsManager
---@field ClassName string The C++ class name, like "SettingsManager".
---@field PrintDebugInfo boolean Gets whether debug print mode is enabled or not.
---@field RecommendedMOIDCount number Returns the recommended MOID count. If this amount is exceeded then some units may be removed at the start of the activity.
---@field AIUpdateInterval number Gets the AI update interval.
---@field ShowEnemyHUD boolean Gets whether the HUD of enemy Actors is set to be visible to the player or not.
---@field AutomaticGoldDeposit boolean Gets whether gold gathered by Actors is automatically added into team funds.
SettingsManager = {}

--- The centralized singleton manager of all Timers and overall timekeeping in RTE. Uses QueryPerformanceCounter for sub-ms resolution timers and the model described in http://www.gaffer.org/game-physics/fix-your-timestep.
---@class TimerManager
---@field ClassName string The C++ class name, like "TimerManager".
---@field TimeScale number Gets a time scale factor which will be used to speed up or slow down the progress of the simulation time in relation to the real world time.
---@field RealToSimCap number Gets the cap of the amount of seconds which can be transferred from the real time to the simulated time in one update.
---@field DeltaTimeTicks number Gets the current number of ticks that the simulation should be updating with. Lua can't handle int64 (or long long apparently) so we'll expose this specialized function.
---@field DeltaTimeSecs number Gets the current fixed delta time of the simulation updates, in seconds.
---@field DeltaTimeMS number Gets the current fixed delta time of the simulation updates, in ms.
---@field AIDeltaTimeSecs number Gets the current fixed delta time of AI updates, in seconds.
---@field AIDeltaTimeMS number Gets the current fixed delta time of AI updates, in ms.
---@field TicksPerSecond number Gets the number of ticks per second. Lua can't handle int64 (or long long apparently) so we'll expose this specialized function.
TimerManager = {}

--- Tells whether there is enough sim time accumulated to do at least one physics update.
---@return boolean
function TimerManager:TimeForSimUpdate() end

--- Tells whether the current simulation update will be drawn in a frame. Use this to check if it is necessary to draw purely graphical things during the sim update.
---@return boolean
function TimerManager:DrawnSimUpdate() end

--- The singleton manager responsible for handling user input.
---@class UInputManager
---@field ClassName string The C++ class name, like "UInputManager".
---@field FlagAltState boolean Gets the state of the Alt key.
---@field FlagCtrlState boolean Gets the state of the Ctrl key.
---@field FlagShiftState boolean Gets the state of the Shift key.
UInputManager = {}

--- Gets the currently used input device of the specified player.
---@param whichPlayer number Which player to get input device for.
---@return number
function UInputManager:GetInputDevice(whichPlayer) end

--- Gets whether a specific input element was depressed between the last update and the one previous to it.
---@param whichPlayer number Which player to check for.
---@param whichElement number Which element to check for.
---@return boolean
function UInputManager:ElementPressed(whichPlayer, whichElement) end

--- Gets whether a specific input element was released between the last update and the one previous to it.
---@param whichPlayer number Which player to check for.
---@param whichElement number Which element to check for.
---@return boolean
function UInputManager:ElementReleased(whichPlayer, whichElement) end

--- Gets whether a specific input element was held during the last update.
---@param whichPlayer number Which player to check for.
---@param whichElement number Which element to check for.
---@return boolean
function UInputManager:ElementHeld(whichPlayer, whichElement) end

--- Gets whether a key was pressed between the last update and the one previous to it, by scancode.
---@param scancodeToTest SDL_Keycode A scancode to test. See SDL_Scancode enumeration.
---@return boolean
function UInputManager:KeyPressed(scancodeToTest) end

--- Gets whether a key was released between the last update and the one previous to it, by scancode.
---@param scancodeToTest SDL_Keycode A scancode to test. See SDL_Scancode enumeration.
---@return boolean
function UInputManager:KeyReleased(scancodeToTest) end

--- Gets whether a key is being held right now, by scancode.
---@param scancodeToTest SDL_Keycode A scancode to test. See SDL_Scancode enumeration.
---@return boolean
function UInputManager:KeyHeld(scancodeToTest) end

--- Gets whether a key was pressed between the last update and the one previous to it, by scancode.
---@param scancodeToTest SDL_Scancode A scancode to test. See SDL_Scancode enumeration.
---@return boolean
function UInputManager:ScancodePressed(scancodeToTest) end

--- Gets whether a key was released between the last update and the one previous to it, by scancode.
---@param scancodeToTest SDL_Scancode A scancode to test. See SDL_Scancode enumeration.
---@return boolean
function UInputManager:ScancodeReleased(scancodeToTest) end

--- Gets whether a key is being held right now, by scancode.
---@param scancodeToTest SDL_Scancode A scancode to test. See SDL_Scancode enumeration.
---@return boolean
function UInputManager:ScancodeHeld(scancodeToTest) end

--- Gets whether a mouse button was pressed between the last update and the one previous to it.
---@param whichButton number Which button to check for.
---@param whichPlayer? number Which player to check for.
---@return boolean
function UInputManager:MouseButtonPressed(whichButton, whichPlayer) end

---@param whichButton number Which button to check for.
---@return boolean
function UInputManager:MouseButtonPressed(whichButton) end

--- Gets whether a mouse button was released between the last update and the one previous to it.
---@param whichButton number Which button to check for.
---@param whichPlayer? number Which player to check for.
---@return boolean
function UInputManager:MouseButtonReleased(whichButton, whichPlayer) end

---@param whichButton number Which button to check for.
---@return boolean
function UInputManager:MouseButtonReleased(whichButton) end

--- Gets whether a mouse button is being held down right now.
---@param whichButton number Which button to check for.
---@param whichPlayer? number Which player to check for.
---@return boolean
function UInputManager:MouseButtonHeld(whichButton, whichPlayer) end

---@param whichButton number Which button to check for.
---@return boolean
function UInputManager:MouseButtonHeld(whichButton) end

--- Get the absolute mouse position in window coordinates.
---@return Vector
function UInputManager:GetMousePos() end

--- Gets whether the mouse wheel has been moved past the threshold limit in either direction this frame.
---@return number
function UInputManager:MouseWheelMoved() end

--- Gets whether a joystick button was pressed between the last update and the one previous to it.
---@param whichJoy number Which joystick to check for.
---@param whichButton number Which joystick button to check for.
---@return boolean
function UInputManager:JoyButtonPressed(whichJoy, whichButton) end

--- Gets whether a joystick button was released between the last update and the one previous to it.
---@param whichJoy number Which joystick to check for.
---@param whichButton number Which joystick button to check for.
---@return boolean
function UInputManager:JoyButtonReleased(whichJoy, whichButton) end

--- Gets whether a joystick button is being held down right now.
---@param whichJoy number Which joystick to check for.
---@param whichButton number Which joystick button to check for.
---@return boolean
function UInputManager:JoyButtonHeld(whichJoy, whichButton) end

--- Shows the first joystick button which was pressed down since last frame.
---@param whichJoy number Which joystick to check for.
---@return number
function UInputManager:WhichJoyButtonPressed(whichJoy) end

--- Gets whether a joystick axis direction was pressed between the last update and the one previous to it.
---@param whichJoy number Which joystick to check for.
---@param whichAxis number Which joystick stick axis to check for.
---@param whichDir number Which direction to check for.
---@return boolean
function UInputManager:JoyDirectionPressed(whichJoy, whichAxis, whichDir) end

--- Gets whether a joystick axis direction was released between the last update and the one previous to it.
---@param whichJoy number Which joystick to check for.
---@param whichAxis number Which joystick stick axis to check for.
---@param whichDir number Which direction to check for.
---@return boolean
function UInputManager:JoyDirectionReleased(whichJoy, whichAxis, whichDir) end

--- Gets whether a joystick axis is being held down in a specific direction right now. Two adjacent directions can be held down to produce diagonals.
---@param whichJoy number Which joystick to check for.
---@param whichAxis number Which joystick stick axis to check for.
---@param whichDir number Which direction to check for.
---@return boolean
function UInputManager:JoyDirectionHeld(whichJoy, whichAxis, whichDir) end

--- Gets the analog moving values of a specific player's control scheme.
---@param whichPlayer? number Which player to check for.
---@return Vector
function UInputManager:AnalogMoveValues(whichPlayer) end

--- Gets the analog aiming values of a specific player's control scheme.
---@param whichPlayer? number Which player to check for.
---@return Vector
function UInputManager:AnalogAimValues(whichPlayer) end

--- Set the mouse's analog emulation output to be of a specific normalized magnitude.
---@param magCap number The normalized magnitude, between 0 and 1.0.
---@param whichPlayer? number Which player to set magnitude for. Only relevant when in online multiplayer mode.
function UInputManager:SetMouseValueMagnitude(magCap, whichPlayer) end

--- Gets the normalized value of a certain joystick's stick's axis.
---@param whichJoy? number Which joystick to check for.
---@param whichAxis? number Which joystick stick axis to check for.
---@return number
function UInputManager:AnalogAxisValue(whichJoy, whichAxis) end

--- Reports which player is using the mouse for control at this time if any.
---@return number
function UInputManager:MouseUsedByPlayer() end

--- Return true if there are any mouse button presses at all.
---@return boolean
function UInputManager:AnyMouseButtonPress() end

--- Gets the relative movement of the mouse since last update. Only returns true if the selected player is actually using the mouse.
---@param whichPlayer? number Which player to get movement for. If the player doesn't use the mouse this always returns a zero vector.
---@return Vector
function UInputManager:GetMouseMovement(whichPlayer) end

--- Will temporarily disable positioning of the mouse. This is so that when focus is switched back to the game window, it avoids having the window fly away because the user clicked the title bar of the window.
---@param disable? boolean Whether to disable mouse positioning or not.
function UInputManager:DisableMouseMoving(disable) end

--- Sets the absolute screen position of the mouse cursor.
---@param newPos Vector Where to place the mouse.
---@param whichPlayer? number Which player is trying to control the mouse. Only the player with actual control over the mouse will be affected. -1 means do it regardless of player.
function UInputManager:SetMousePos(newPos, whichPlayer) end

--- Forces the mouse within a box on the screen.
---@param x number X value of the top left corner of the screen box to keep the mouse within, relative to the top left corner of the player's screen.
---@param y number Y value of the top left corner of the screen box to keep the mouse within, relative to the top left corner of the player's screen.
---@param width number The width of the box.
---@param height number The height of the box.
---@param whichPlayer? number Which player is trying to control the mouse. Only the player with actual control over the mouse will be affected. -1 means do it regardless of player.
function UInputManager:ForceMouseWithinBox(x, y, width, height, whichPlayer) end

--- Return true if there are any keyboard button presses at all.
---@return boolean
function UInputManager:AnyKeyPress() end

--- Gets whether there is any joystick input at all, buttons or D-pad.
---@param checkForPresses? boolean Whether to check specifically for presses since last frame.
---@return boolean
function UInputManager:AnyJoyInput(checkForPresses) end

--- Return true if there are any joystick presses at all, buttons or D-pad.
---@return boolean
function UInputManager:AnyJoyPress() end

--- Gets whether there are any joystick button presses at all, but not D-pad input, for a specific joystick.
---@param whichJoy number Which joystick to check for.
---@return boolean
function UInputManager:AnyJoyButtonPress(whichJoy) end

--- Gets whether there is any input at all, keyboard or buttons or D-pad.
---@return boolean
function UInputManager:AnyInput() end

--- Gets whether there are any key, button, or D-pad presses at all. MUST call Update before calling this for it to work properly!
---@return boolean
function UInputManager:AnyPress() end

--- Gets whether there are any start key/button presses at all. MUST call Update before calling this for it to work properly!
---@param includeSpacebar? boolean Whether to check for space bar presses or not.
---@return boolean
function UInputManager:AnyStartPress(includeSpacebar) end

--- Returns whether text input events are available.
---@return boolean
function UInputManager:HasTextInput() end

--- Returns the current text input.
---@return string
function UInputManager:GetTextInput() end

--- A struct to keep all data about a an alarming event for the AI Actors.
---@class AlarmEvent
---@field ClassName string The C++ class name, like "AlarmEvent".
---@field ScenePos Vector
---@field Team number
---@field Range number
AlarmEvent = {}

---@class Directions
---@field ClassName string The C++ class name, like "Directions".
---@field None number One of Directions.Directions.
---@field Up number One of Directions.Directions.
---@field Down number One of Directions.Directions.
---@field Left number One of Directions.Directions.
---@field Right number One of Directions.Directions.
---@field Any number One of Directions.Directions.
Directions = {}

---@class DrawBlendMode
---@field ClassName string The C++ class name, like "DrawBlendMode".
---@field NoBlend number One of DrawBlendMode.DrawBlendMode.
---@field Burn number One of DrawBlendMode.DrawBlendMode.
---@field Color number One of DrawBlendMode.DrawBlendMode.
---@field Difference number One of DrawBlendMode.DrawBlendMode.
---@field Dissolve number One of DrawBlendMode.DrawBlendMode.
---@field Dodge number One of DrawBlendMode.DrawBlendMode.
---@field Invert number One of DrawBlendMode.DrawBlendMode.
---@field Luminance number One of DrawBlendMode.DrawBlendMode.
---@field Multiply number One of DrawBlendMode.DrawBlendMode.
---@field Saturation number One of DrawBlendMode.DrawBlendMode.
---@field Screen number One of DrawBlendMode.DrawBlendMode.
---@field Transparency number One of DrawBlendMode.DrawBlendMode.
---@field BlendModeCount number One of DrawBlendMode.DrawBlendMode.
DrawBlendMode = {}

--- Class used to schedule drawing of graphical primitives created from Lua. All coordinates passed to GraphicalPrimitive objects are Scene coordinates.
---@class GraphicalPrimitive
---@field ClassName string The C++ class name, like "GraphicalPrimitive".
GraphicalPrimitive = {}

--- Class used to schedule drawing of line primitives created from Lua.
---@class LinePrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "LinePrimitive".
LinePrimitive = {}

--- Class used to schedule drawing of arc line primitives created from Lua.
---@class ArcPrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "ArcPrimitive".
ArcPrimitive = {}

--- Class used to schedule drawing of spline primitives created from Lua.
---@class SplinePrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "SplinePrimitive".
SplinePrimitive = {}

--- Class used to schedule drawing of box primitives created from Lua.
---@class BoxPrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "BoxPrimitive".
BoxPrimitive = {}

--- Class used to schedule drawing of filled box primitives created from Lua.
---@class BoxFillPrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "BoxFillPrimitive".
BoxFillPrimitive = {}

--- Class used to schedule drawing of box with round corners primitives created from Lua.
---@class RoundedBoxPrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "RoundedBoxPrimitive".
RoundedBoxPrimitive = {}

--- Class used to schedule drawing of filled box with round corners primitives created from Lua.
---@class RoundedBoxFillPrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "RoundedBoxFillPrimitive".
RoundedBoxFillPrimitive = {}

--- Class used to schedule drawing of circle primitives created from Lua.
---@class CirclePrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "CirclePrimitive".
CirclePrimitive = {}

--- Class used to schedule drawing of filled circle primitives created from Lua
---@class CircleFillPrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "CircleFillPrimitive".
CircleFillPrimitive = {}

--- Class used to schedule drawing of ellipse primitives created from Lua.
---@class EllipsePrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "EllipsePrimitive".
EllipsePrimitive = {}

--- Class used to schedule drawing of filled ellipse primitives created from Lua
---@class EllipseFillPrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "EllipseFillPrimitive".
EllipseFillPrimitive = {}

--- Class used to schedule drawing of triangle primitives created from Lua.
---@class TrianglePrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "TrianglePrimitive".
TrianglePrimitive = {}

--- Class used to schedule drawing of filled triangle primitives created from Lua.
---@class TriangleFillPrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "TriangleFillPrimitive".
TriangleFillPrimitive = {}

--- Class used to schedule drawing of text primitives created from Lua.
---@class TextPrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "TextPrimitive".
TextPrimitive = {}

--- Class used to schedule drawing of bitmap primitives created from Lua.
---@class BitmapPrimitive : GraphicalPrimitive
---@field ClassName string The C++ class name, like "BitmapPrimitive".
BitmapPrimitive = {}

--- A useful 2D axis-aligned rectangle class.
---@class Box
---@field ClassName string
---@field Corner Vector Gets the primary corner of this box.
---@field Width number Gets the width of this box. Note that this can be negative if the box hasn't been righted with Unflip().
---@field Height number Gets the height of this box. Note that this can be negative if the box hasn't been righted with Unflip().
---@field Center Vector Gets the center point of this Box' area, in absolute Scene coordinates.
---@field Area number Gets the width times the height.
Box = {}

--- Gets a random point within this box.
---@return Vector
function Box:GetRandomPoint() end

--- Makes the corner of this box represent the upper left corner, and both width and height will end up positive.
function Box:Unflip() end

--- Tells whether a point is within the Box or not, taking potential flipping into account.
---@param point Vector The Vector describing the point to test for within box bounds.
---@return boolean
function Box:IsWithinBox(point) end

--- Tells whether an X coordinate is within the Box's X-range or not, taking potential flipping into account.
---@param pointX number The coordinate describing the X value to test for within box bounds.
---@return boolean
function Box:IsWithinBoxX(pointX) end

--- Tells whether an Y coordinate is within the Box's Y-range or not, taking potential flipping into account.
---@param pointY number The coordinate describing the Y value to test for within box bounds.
---@return boolean
function Box:IsWithinBoxY(pointY) end

--- Returns an X value constrained inside the Box and returns it.
---@param pointX number The X value to constrain inside the Box.
---@return number
function Box:GetWithinBoxX(pointX) end

--- Returns an Y value constrained inside the Box and returns it.
---@param pointY number The Y value to constrain inside the Box.
---@return number
function Box:GetWithinBoxY(pointY) end

--- Returns a copy of a point constrained inside this box.
---@param point Vector The Vector describing the point to constrain inside the box.
---@return Vector
function Box:GetWithinBox(point) end

--- Tells whether another box intersects this one.
---@param rhs Box The other Box to check for intersection with.
---@return boolean
function Box:IntersectsBox(rhs) end

--- A class controlling MovableObjects through either player input, networking, scripting, AI, etc.
---@class Controller
---@field ClassName string The C++ class name, like "Controller".
---@field InputMode number Gets the current mode of input for this Controller.
---@field ControlledActor Actor Gets which Actor is being controlled by this. 0 if none.
---@field Team number Gets the Team number using this controller.
---@field AnalogMove Vector Gets the analog movement input data.
---@field AnalogAim Vector Gets the analog aiming input data.
---@field AnalogCursor Vector Gets the analog menu input data.
---@field Player number Gets which player's input this is listening to, if in player input mode.
---@field MouseMovement Vector Gets the relative movement of the mouse since last update.
---@field Disabled boolean Shows whether this controller is disabled.
---@field PRIMARY_ACTION number One of Controller.ControlState.
---@field SECONDARY_ACTION number One of Controller.ControlState.
---@field MOVE_IDLE number One of Controller.ControlState.
---@field MOVE_RIGHT number One of Controller.ControlState.
---@field MOVE_LEFT number One of Controller.ControlState.
---@field MOVE_UP number One of Controller.ControlState.
---@field MOVE_DOWN number One of Controller.ControlState.
---@field MOVE_FAST number One of Controller.ControlState.
---@field BODY_JUMPSTART number One of Controller.ControlState.
---@field BODY_JUMP number One of Controller.ControlState.
---@field BODY_CROUCH number One of Controller.ControlState.
---@field AIM_UP number One of Controller.ControlState.
---@field AIM_DOWN number One of Controller.ControlState.
---@field AIM_SHARP number One of Controller.ControlState.
---@field WEAPON_FIRE number One of Controller.ControlState.
---@field WEAPON_RELOAD number One of Controller.ControlState.
---@field PIE_MENU_OPENED number One of Controller.ControlState.
---@field PIE_MENU_ACTIVE number One of Controller.ControlState.
---@field WEAPON_CHANGE_NEXT number One of Controller.ControlState.
---@field WEAPON_CHANGE_PREV number One of Controller.ControlState.
---@field WEAPON_PICKUP number One of Controller.ControlState.
---@field WEAPON_DROP number One of Controller.ControlState.
---@field ACTOR_NEXT number One of Controller.ControlState.
---@field ACTOR_PREV number One of Controller.ControlState.
---@field ACTOR_BRAIN number One of Controller.ControlState.
---@field ACTOR_NEXT_PREP number One of Controller.ControlState.
---@field ACTOR_PREV_PREP number One of Controller.ControlState.
---@field HOLD_RIGHT number One of Controller.ControlState.
---@field HOLD_LEFT number One of Controller.ControlState.
---@field HOLD_UP number One of Controller.ControlState.
---@field HOLD_DOWN number One of Controller.ControlState.
---@field PRESS_PRIMARY number One of Controller.ControlState.
---@field PRESS_SECONDARY number One of Controller.ControlState.
---@field PRESS_RIGHT number One of Controller.ControlState.
---@field PRESS_LEFT number One of Controller.ControlState.
---@field PRESS_UP number One of Controller.ControlState.
---@field PRESS_DOWN number One of Controller.ControlState.
---@field RELEASE_PRIMARY number One of Controller.ControlState.
---@field RELEASE_SECONDARY number One of Controller.ControlState.
---@field PRESS_FACEBUTTON number One of Controller.ControlState.
---@field RELEASE_FACEBUTTON number One of Controller.ControlState.
---@field SCROLL_UP number One of Controller.ControlState.
---@field SCROLL_DOWN number One of Controller.ControlState.
---@field DEBUG_ONE number One of Controller.ControlState.
---@field CONTROLSTATECOUNT number One of Controller.ControlState.
---@field CIM_DISABLED number One of Controller.InputMode.
---@field CIM_PLAYER number One of Controller.InputMode.
---@field CIM_AI number One of Controller.InputMode.
---@field CIM_NETWORK number One of Controller.InputMode.
---@field CIM_INPUTMODECOUNT number One of Controller.InputMode.
Controller = {}

--- Shortcut to indicate if in player input mode.
---@param otherThanPlayer? number If you want to check if it's controlled by a player, AND that player is someone else than a specific one, pass in that player number here.
---@return boolean
function Controller:IsPlayerControlled(otherThanPlayer) end

--- Adds relative movement to a passed-in vector. Uses the appropriate input method currently of this.
---@param cursorPos Vector The vector to alter.
---@param moveScale? number The scale of the input. 1.0 is 'normal'.
---@return boolean
function Controller:RelativeCursorMovement(cursorPos, moveScale) end

--- Indicates whether this is listening to mouse input at all.
---@return boolean
function Controller:IsMouseControlled() end

--- Indicates whether this is only listening to keyboard input.
---@return boolean
function Controller:IsKeyboardOnlyControlled() end

--- Indicates whether this is listening to gamepad at all.
---@return boolean
function Controller:IsGamepadControlled() end

--- Sets one of this controller's states.
---@param controlState number
---@param setting? boolean Value of the state being set.
function Controller:SetState(controlState, setting) end

--- Shows whether the current controller is in a specific state.
---@param controlState number What control state to check for.
---@return boolean
function Controller:IsState(controlState) end

--- Overrides this controller, setting it to match another controller. This is useful for multithreading, where the Lua script can use a copied controller in a multi-threaded context, before overriding the controller in a single-threaded context. This is exposed to Lua API to be clear, whereas ownership relies on operator overloading is rather temperamental :)
---@param otherController Controller The other controller's state to copy. Ownership is not transferred
function Controller:Override(otherController) end

--- A representation of a DataModule containing zero or many Material, Effect, Ammo, Device, Actor, or Scene definitions.
---@class DataModule
---@field ClassName string The C++ class name, like "DataModule".
---@field FileName string Gets the file name of this DataModule, e.g. "MyMod.rte".
---@field FriendlyName string Gets the friendly name of this DataModule, e.g. "My Great Mod".
---@field Author string Gets the author name of this DataModule, e.g. "Data Realms, LLC".
---@field Description string Gets the description of this DataModule's contents.
---@field Version number Gets the version number of this DataModule.
---@field IsFaction boolean Gets whether this DataModule is considered a faction.
---@field IsMerchant boolean Gets whether this DataModule is considered a merchant.
---@field Presets Entity[] A list of loaded entities solely for the purpose of enumeration presets from Lua.
DataModule = {}

--- A precise timer for FPS sync etc.
---@class Timer
---@field ClassName string The C++ class name, like "Timer".
---@field StartRealTimeMS number Gets the start real time value of this Timer.
---@field ElapsedRealTimeS number Gets the elapsed real time in seconds since this Timer was Reset().
---@field ElapsedRealTimeMS number Gets the elapsed real time in ms since this Timer was Reset().
---@field StartSimTimeMS number Gets the start time value of this Timer.
---@field ElapsedSimTimeS number Gets the elapsed time in s since this Timer was Reset().
---@field ElapsedSimTimeMS number Gets the elapsed time in ms since this Timer was Reset().
---@field RealTimeLimitProgress number Returns how much progress has been made toward the set time limit previously set by SetRealTimeLimitMS. 0 means no progress, 1.0 means the timer has reached, or is beyond the limit.
---@field SimTimeLimitProgress number Returns how much progress has been made toward the set time limit previously set by SetSimTimeLimitMS. 0 means no progress, 1.0 means the timer has reached, or is beyond the limit.
Timer = {}

--- Resets the timer so that the elapsed time is 0 ms. TODO: Figure out why calling Clear() here breaks time.
function Timer:Reset() end

--- Sets the real time limit value of this Timer, RELATVE to the start time. This is when the timer is supposed to show that it has 'expired' or reached whatever time limit it is supposed to keep track of.
---@param newTimeLimit number A positive double with the new real time limit relative to the start time.
function Timer:SetRealTimeLimitMS(newTimeLimit) end

--- Sets the real time limit value of this Timer, RELATVE to the start time. This is when the timer is supposed to show that it has 'expired' or reached whatever time limit it is supposed to keep track of.
---@param newTimeLimit number A positive double with the new real time limit relative to the start time.
function Timer:SetRealTimeLimitS(newTimeLimit) end

--- Returns true if the elapsed real time is past a certain amount of time after the start previously set by SetRealTimeLimit.
---@return boolean
function Timer:IsPastRealTimeLimit() end

--- Returns how much time in ms that there is left till this Timer reaches a certain time limit previously set by SetRealTimeLimitMS.
---@return number
function Timer:LeftTillRealTimeLimitMS() end

--- Returns how much time in ms that there is left till this Timer reaches a certain time limit previously set by SetRealTimeLimitS.
---@return number
function Timer:LeftTillRealTimeLimitS() end

--- Returns how much time in ms that there is left till this Timer reaches a certain time limit.
---@param when number A unsigned long specifying till when there is time.
---@return number
function Timer:LeftTillRealMS(when) end

--- Returns true if the elapsed real time is past a certain amount of time relative to this' start.
---@param limit number A long specifying the threshold amount of real time in ms.
---@return boolean
function Timer:IsPastRealMS(limit) end

--- Returns true or false, depending on whether the elapsed time falls in one of two repeating intervals which divide it. This is useful for blink animations etc.
---@param period number An int with the alternating period in ms. The time specified here is how long it will take for the switch to alternate.
---@return boolean
function Timer:AlternateReal(period) end

--- Sets the sim time limit value of this Timer, RELATVE to the start time. This is when the timer is supposed to show that it has 'expired' or reached whatever time limit it is supposed to keep track of.
---@return number
function Timer:GetSimTimeLimitMS() end

--- Sets the sim time limit value of this Timer, RELATVE to the start time. This is when the timer is supposed to show that it has 'expired' or reached whatever time limit it is supposed to keep track of.
---@param newTimeLimit number A positive double with the new sim time limit relative to the start time.
function Timer:SetSimTimeLimitMS(newTimeLimit) end

--- Sets the sim time limit value of this Timer, RELATVE to the start time. This is when the timer is supposed to show that it has 'expired' or reached whatever time limit it is supposed to keep track of.
---@return number
function Timer:GetSimTimeLimitS() end

--- Sets the sim time limit value of this Timer, RELATVE to the start time. This is when the timer is supposed to show that it has 'expired' or reached whatever time limit it is supposed to keep track of.
---@param newTimeLimit number A positive double with the new sim time limit relative to the start time.
function Timer:SetSimTimeLimitS(newTimeLimit) end

--- Returns true if the elapsed sim time is past a certain amount of time after the start previously set by SetSimTimeLimit.
---@return boolean
function Timer:IsPastSimTimeLimit() end

--- Returns how much time in ms that there is left till this Timer reaches a certain time limit previously set by SetSimTimeLimitMS.
---@return number
function Timer:LeftTillSimTimeLimitMS() end

--- Returns how much time in ms that there is left till this Timer reaches a certain time limit previously set by SetSimTimeLimitS.
---@return number
function Timer:LeftTillSimTimeLimitS() end

--- Returns how much time in ms that there is left till this Timer reaches a certain time limit.a certain time limit.
---@param when number A unsigned long specifying till when there is time.
---@return number
function Timer:LeftTillSimMS(when) end

--- Returns true if the elapsed time is past a certain amount of time.
---@param limit number A unsigned long specifying the threshold amount of time in ms.
---@return boolean
function Timer:IsPastSimMS(limit) end

--- Returns true or false, depending on whether the elapsed time falls in one of two repeating intervals which divide it. This is useful for blink animations etc.
---@param period number An int with the alternating period in ms. The time specified here is how long it will take for the switch to alternate.
---@return boolean
function Timer:AlternateSim(period) end

--- A useful 2D float vector.
---@class Vector
---@field ClassName string
---@field RoundedX number Returns the rounded integer X value of this Vector.
---@field RoundedY number Returns the rounded integer Y value of this Vector.
---@field Rounded Vector Returns a rounded copy of this Vector. Does not alter this Vector.
---@field FlooredX number Returns the greatest integer that is not greater than the X value of this Vector.
---@field FlooredY number Returns the greatest integer that is not greater than the Y value of this Vector.
---@field Floored Vector Returns a floored copy of this Vector. Does not alter this Vector.
---@field CeilingedX number Returns the lowest integer that is not less than the X value of this Vector.
---@field CeilingedY number Returns the lowest integer that is not less than the Y value of this Vector.
---@field Ceilinged Vector Returns a ceilinged copy of this Vector. Does not alter this Vector.
---@field Magnitude number Gets the magnitude of this Vector.
---@field SqrMagnitude number Gets the squared magnitude of this Vector.
---@field Largest number Gets the absolute largest of the two elements. Will always be positive.
---@field Smallest number Gets the absolute smallest of the two elements. Will always be positive.
---@field Normalized Vector Returns a Vector that has the same direction as this but with a magnitude of 1.0.
---@field Perpendicular Vector Returns a Vector that is perpendicular to this, rotated PI/2.
---@field AbsRadAngle number Get this Vector's absolute angle in radians. e.g: when x = 1, y = 0, the value returned here will be 0. x = 0, y = 1 yields -pi/2 here.
---@field AbsDegAngle number Get this Vector's absolute angle in degrees. e.g: when x = 1, y = 0, the value returned here will be 0. x = 0, y = 1 yields -90 here.
---@field X number X value of this vector.
---@field Y number Y value of this vector.
Vector = {}

--- Gets whether this Vector's magnitude is greater than the specified value.
---@param magnitude number A float value that this Vector's magnitude will be compared against.
---@return boolean
function Vector:MagnitudeIsGreaterThan(magnitude) end

--- Gets whether this Vector's magnitude is less than the specified value.
---@param magnitude number A float value that this Vector's magnitude will be compared against.
---@return boolean
function Vector:MagnitudeIsLessThan(magnitude) end

--- Sets the magnitude of this Vector. A negative magnitude will invert the Vector's direction.
---@param newMag number A float value that the magnitude will be set to.
---@return Vector
function Vector:SetMagnitude(newMag) end

--- Gets a Vector identical to this except that its X component is flipped.
---@param xFlip? boolean Whether to flip the X axis of the return vector or not.
---@return Vector
function Vector:GetXFlipped(xFlip) end

--- Gets a Vector identical to this except that its Y component is flipped.
---@param yFlip? boolean Whether to flip the Y axis of the return vector or not.
---@return Vector
function Vector:GetYFlipped(yFlip) end

--- Caps the magnitude of this Vector to a max value and keeps its angle intact.
---@param capMag number A float value that the magnitude will be capped by.
---@return Vector
function Vector:CapMagnitude(capMag) end

--- Clamps the magnitude of this Vector between the upper and lower limits, and keeps its angle intact.
---@param lowerMagnitudeLimit number A float value that defines the lower limit for the magnitude of this Vector.
---@param upperMagnitudeLimit number A float value that defines the upper limit for the magnitude of this Vector.
---@return Vector
function Vector:ClampMagnitude(lowerMagnitudeLimit, upperMagnitudeLimit) end

--- Flips the X element of this Vector.
---@param flipX? boolean Whether or not to flip the X element or not.
---@return Vector
function Vector:FlipX(flipX) end

--- Flips the Y element of this Vector.
---@param flipY? boolean Whether or not to flip the Y element or not.
---@return Vector
function Vector:FlipY(flipY) end

--- Indicates whether both X and Y components of this Vector are 0.
---@return boolean
function Vector:IsZero() end

--- Indicates whether the X and Y components of this Vector each have opposite signs to their corresponding components of a passed in Vector.
---@param opp Vector The Vector to compare with.
---@return boolean
function Vector:IsOpposedTo(opp) end

--- Returns the dot product of this Vector and the passed in Vector.
---@param rhs Vector The Vector which will be the right hand side operand of the dot product operation.
---@return number
function Vector:Dot(rhs) end

--- Returns the 2D cross product of this Vector and the passed in Vector. This is really the area of the parallelogram that the two vectors form.
---@param rhs Vector The Vector which will be the right hand side operand of the cross product operation.
---@return number
function Vector:Cross(rhs) end

--- Rounds the X and Y values of this Vector upwards. E.g. 0.49 -> 0.0 and 0.5 -> 1.0.
---@return Vector
function Vector:Round() end

--- Sets the X and Y of this Vector to the nearest half value. E.g. 1.0 -> 1.5 and 0.9 -> 0.5.
---@return Vector
function Vector:ToHalf() end

--- Sets the X and Y of this Vector to the greatest integers that are not greater than their original values. E.g. -1.02 becomes -2.0.
---@return Vector
function Vector:Floor() end

--- Sets the X and Y of this Vector to the lowest integers that are not less than their original values. E.g. -1.02 becomes -1.0.
---@return Vector
function Vector:Ceiling() end

--- Scales this vector to have the same direction but a magnitude of 1.0.
---@return Vector
function Vector:Normalize() end

--- Makes this vector perpendicular to its previous state, rotated PI/2. Much faster than RadRotate by PI/2.
---@return Vector
function Vector:Perpendicularize() end

--- Sets both the X and Y of this Vector to zero.
function Vector:Reset() end

--- Rotate this Vector relatively by an angle in radians.
---@param angle number The angle in radians to rotate by. Positive angles rotate counter-clockwise, and negative angles clockwise.
---@return Vector
function Vector:RadRotate(angle) end

--- Rotate this Vector relatively by an angle in degrees.
---@param angle number The angle in degrees to rotate by. Positive angles rotate counter-clockwise, and negative angles clockwise.
---@return Vector
function Vector:DegRotate(angle) end

--- Returns a copy of this Vector, rotated relatively by an angle in radians.
---@param angle number The angle in radians to rotate by. Positive angles rotate counter-clockwise, and negative angles clockwise.
---@return Vector
function Vector:GetRadRotatedCopy(angle) end

--- Returns a copy of this Vector, rotated relatively by an angle in degrees.
---@param angle number The angle in degrees to rotate by. Positive angles rotate counter-clockwise, and negative angles clockwise.
---@return Vector
function Vector:GetDegRotatedCopy(angle) end

--- Set this Vector to an absolute rotation based on the absolute rotation of another Vector.
---@param refVector Vector The reference Vector whose absolute angle from positive X (0 degrees) this Vector will be rotated to.
---@return Vector
function Vector:AbsRotateTo(refVector) end

--- Sets both the X and Y values of this Vector.
---@param newX number A float value that the X value will be set to.
---@param newY number A float value that the Y value will be set to.
---@return Vector
function Vector:SetXY(newX, newY) end

--- Information required to make an async pathing request.
---@class PathRequest
---@field ClassName string The C++ class name, like "PathRequest".
---@field Path Vector[]
---@field PathLength number
---@field Status number
---@field TotalCost number
---@field Solved number One of PathRequest.Status.
---@field NoSolution number One of PathRequest.Status.
---@field StartEndSame number One of PathRequest.Status.
PathRequest = {}

--- A single lua state. Multiple of these can exist at once for multithreaded scripting.
---@class LuaManager
---@field ClassName string The C++ class name, like "LuaManager".
---@field TempEntity Entity Gets a temporary Entity that can be accessed in the Lua state.
---@field TempEntities Entity[] Gets the temporary vector of Entities that can be accessed in the Lua state.
LuaManager = {}

--- Gets a random integer between minInclusive and maxInclusive.
---@param minInclusive number
---@param maxInclusive number
---@return number
function LuaManager:SelectRand(minInclusive, maxInclusive) end

--- Gets a random real between minInclusive and maxInclusive.
---@param minInclusive number
---@param maxInclusive number
---@return number
function LuaManager:RangeRand(minInclusive, maxInclusive) end

--- Gets a random number between 0 and 1.
---@return number
function LuaManager:PosRand() end

--- Gets a random number between -1 and 1.
---@return number
function LuaManager:NormalRand() end

---@param path string
---@return string[]
function LuaManager:GetDirectoryList(path) end

---@param path string
---@return string[]
function LuaManager:GetFileList(path) end

---@param path string
---@return boolean
function LuaManager:FileExists(path) end

---@param path string
---@return boolean
function LuaManager:DirectoryExists(path) end

---@param path string
---@param accessMode string
---@return number
function LuaManager:FileOpen(path, accessMode) end

---@param fileIndex number
function LuaManager:FileClose(fileIndex) end

---@param path string
---@return boolean
function LuaManager:FileRemove(path) end

---@param path string
---@return boolean
function LuaManager:DirectoryCreate(path) end

---@param path string
---@param recursive boolean
---@return boolean
function LuaManager:DirectoryCreate(path, recursive) end

---@param path string
---@return boolean
function LuaManager:DirectoryRemove(path) end

---@param path string
---@param recursive boolean
---@return boolean
function LuaManager:DirectoryRemove(path, recursive) end

---@param oldPath string
---@param newPath string
---@return boolean
function LuaManager:FileRename(oldPath, newPath) end

---@param oldPath string
---@param newPath string
---@return boolean
function LuaManager:DirectoryRename(oldPath, newPath) end

---@param fileIndex number
---@return string
function LuaManager:FileReadLine(fileIndex) end

---@param fileIndex number
---@param line string
function LuaManager:FileWriteLine(fileIndex, line) end

---@param fileIndex number
---@return boolean
function LuaManager:FileEOF(fileIndex) end

---@type TimerManager
TimerMan = nil

---@type FrameManager
FrameMan = nil

---@type PerformanceManager
PerformanceMan = nil

---@type PostProcessManager
PostProcessMan = nil

---@type PrimitiveManager
PrimitiveMan = nil

---@type PresetManager
PresetMan = nil

---@type AudioManager
AudioMan = nil

---@type UInputManager
UInputMan = nil

---@type SceneManager
SceneMan = nil

---@type ActivityManager
ActivityMan = nil

---@type MetaManager
MetaMan = nil

---@type MovableManager
MovableMan = nil

---@type CameraManager
CameraMan = nil

---@type ConsoleManager
ConsoleMan = nil

---@type LuaManager
LuaMan = nil

---@type SettingsManager
SettingsMan = nil

--- Explicit deletion of any Entity instance that Lua owns. It will probably be handled by the GC, but this makes it instantaneous.
---@param entityToDelete Entity
function DeleteEntity(entityToDelete) end

---@param arg1 number
---@param arg2 number
---@param arg3 number
---@param arg4 number
---@param arg5 number
---@return number
function LERP(arg1, arg2, arg3, arg4, arg5) end

---@param arg1 number
---@param arg2 number
---@param arg3 number
---@param arg4 number
---@param arg5 number
---@return number
function Lerp(arg1, arg2, arg3, arg4, arg5) end

---@param arg1 number
---@param arg2 number
---@param arg3 Vector
---@param arg4 Vector
---@param arg5 number
---@return Vector
function Lerp(arg1, arg2, arg3, arg4, arg5) end

---@param arg1 number
---@param arg2 number
---@param arg3 Matrix
---@param arg4 Matrix
---@param arg5 number
---@return Matrix
function Lerp(arg1, arg2, arg3, arg4, arg5) end

--- Nonlinear ease-in interpolation. Starts slow.
---@param start number
---@param end number
---@param progressScalar number
---@return number
function EaseIn(start, end, progressScalar) end

--- Nonlinear ease-out interpolation. Slows down toward the end.
---@param start number
---@param end number
---@param progressScalar number
---@return number
function EaseOut(start, end, progressScalar) end

--- Nonlinear ease-in-out interpolation. Slows down in the start and end.
---@param start number
---@param end number
---@param progressScalar number
---@return number
function EaseInOut(start, end, progressScalar) end

--- Clamps a value between two limit values.
---@param value number
---@param upperLimit number
---@param lowerLimit number
---@return number
function Clamp(value, upperLimit, lowerLimit) end

--- Returns a copy of the angle normalized so it's between 0 and 2PI.
---@param angle number
---@return number
function NormalizeAngleBetween0And2PI(angle) end

--- Returns a copy of the angle normalized so it's between -PI and PI.
---@param angle number
---@return number
function NormalizeAngleBetweenNegativePIAndPI(angle) end

--- Returns whether or not the angle to check is between the start and end angles. Note that, because of how angles work (when normalized), the start angle may be greater than the end angle.
---@param angleToCheck number
---@param startAngle number
---@param endAngle number
---@return boolean
function AngleWithinRange(angleToCheck, startAngle, endAngle) end

--- Clamps the passed in angle between the specified lower and upper limits, in a CCW direction.
---@param angleToClamp number
---@param startAngle number
---@param endAngle number
---@return number
function ClampAngle(angleToClamp, startAngle, endAngle) end

--- Gets the ratio between on-screen pixels and the physics engine's meters.
---@return number
function GetPPM() end

--- Gets the ratio between the physics engine's meters and on-screen pixels.
---@return number
function GetMPP() end

--- Gets the ratio between the on-screen pixels and the physics engine's Liters.
---@return number
function GetPPL() end

--- Gets the ratio between the physics engine's Liters and on-screen pixels.
---@return number
function GetLPP() end

--- Gets the default pathfinder penetration value that'll allow pathing through corpses, debris, and such stuff.
---@return number
function GetPathFindingDefaultDigStrength() end

--- Rounds a float to a set fixed point precision (digits after decimal point) with option to always ceil or always floor the remainder.
---@param input number
---@param precision number
---@param roundingMode? number
---@return string
function RoundFloatToPrecision(input, precision, roundingMode) end

--- Rounds an integer to the specified nearest multiple. For example, if the arguments are 63 and 5, the returned value will be 65.
---@param num number
---@param multiple number
---@return number
function RoundToNearestMultiple(num, multiple) end

--- Treats an object as a SoundContainer, for its SoundContainer functions and properties. Use IsSoundContainer first when it might be something else.
---@param entity Entity
---@return SoundContainer
function ToSoundContainer(entity) end

--- Whether an object is a SoundContainer (or a kind of one).
---@param entity Entity
---@return boolean
function IsSoundContainer(entity) end

--- Makes a new SoundContainer from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return SoundContainer
function CreateSoundContainer(presetName, moduleName) end

---@param presetName string
---@return SoundContainer
function CreateSoundContainer(presetName) end

--- Makes a random SoundContainer from a group.
---@param groupName string
---@param moduleID number
---@return SoundContainer
function RandomSoundContainer(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return SoundContainer
function RandomSoundContainer(groupName, moduleName) end

---@param groupName string
---@return SoundContainer
function RandomSoundContainer(groupName) end

--- Treats an object as a SceneObject, for its SceneObject functions and properties. Use IsSceneObject first when it might be something else.
---@param entity Entity
---@return SceneObject
function ToSceneObject(entity) end

--- Whether an object is a SceneObject (or a kind of one).
---@param entity Entity
---@return boolean
function IsSceneObject(entity) end

--- Treats an object as a MovableObject, for its MovableObject functions and properties. Use IsMovableObject first when it might be something else.
---@param entity Entity
---@return MovableObject
function ToMovableObject(entity) end

--- Whether an object is a MovableObject (or a kind of one).
---@param entity Entity
---@return boolean
function IsMovableObject(entity) end

--- Treats an object as a MOPixel, for its MOPixel functions and properties. Use IsMOPixel first when it might be something else.
---@param entity Entity
---@return MOPixel
function ToMOPixel(entity) end

--- Whether an object is a MOPixel (or a kind of one).
---@param entity Entity
---@return boolean
function IsMOPixel(entity) end

--- Makes a new MOPixel from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return MOPixel
function CreateMOPixel(presetName, moduleName) end

---@param presetName string
---@return MOPixel
function CreateMOPixel(presetName) end

--- Makes a random MOPixel from a group.
---@param groupName string
---@param moduleID number
---@return MOPixel
function RandomMOPixel(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return MOPixel
function RandomMOPixel(groupName, moduleName) end

---@param groupName string
---@return MOPixel
function RandomMOPixel(groupName) end

--- Treats an object as a TerrainObject, for its TerrainObject functions and properties. Use IsTerrainObject first when it might be something else.
---@param entity Entity
---@return TerrainObject
function ToTerrainObject(entity) end

--- Whether an object is a TerrainObject (or a kind of one).
---@param entity Entity
---@return boolean
function IsTerrainObject(entity) end

--- Makes a new TerrainObject from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return TerrainObject
function CreateTerrainObject(presetName, moduleName) end

---@param presetName string
---@return TerrainObject
function CreateTerrainObject(presetName) end

--- Makes a random TerrainObject from a group.
---@param groupName string
---@param moduleID number
---@return TerrainObject
function RandomTerrainObject(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return TerrainObject
function RandomTerrainObject(groupName, moduleName) end

---@param groupName string
---@return TerrainObject
function RandomTerrainObject(groupName) end

--- Treats an object as a MOSprite, for its MOSprite functions and properties. Use IsMOSprite first when it might be something else.
---@param entity Entity
---@return MOSprite
function ToMOSprite(entity) end

--- Whether an object is a MOSprite (or a kind of one).
---@param entity Entity
---@return boolean
function IsMOSprite(entity) end

--- Treats an object as a MOSParticle, for its MOSParticle functions and properties. Use IsMOSParticle first when it might be something else.
---@param entity Entity
---@return MOSParticle
function ToMOSParticle(entity) end

--- Whether an object is a MOSParticle (or a kind of one).
---@param entity Entity
---@return boolean
function IsMOSParticle(entity) end

--- Makes a new MOSParticle from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return MOSParticle
function CreateMOSParticle(presetName, moduleName) end

---@param presetName string
---@return MOSParticle
function CreateMOSParticle(presetName) end

--- Makes a random MOSParticle from a group.
---@param groupName string
---@param moduleID number
---@return MOSParticle
function RandomMOSParticle(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return MOSParticle
function RandomMOSParticle(groupName, moduleName) end

---@param groupName string
---@return MOSParticle
function RandomMOSParticle(groupName) end

--- Treats an object as a MOSRotating, for its MOSRotating functions and properties. Use IsMOSRotating first when it might be something else.
---@param entity Entity
---@return MOSRotating
function ToMOSRotating(entity) end

--- Whether an object is a MOSRotating (or a kind of one).
---@param entity Entity
---@return boolean
function IsMOSRotating(entity) end

--- Makes a new MOSRotating from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return MOSRotating
function CreateMOSRotating(presetName, moduleName) end

---@param presetName string
---@return MOSRotating
function CreateMOSRotating(presetName) end

--- Makes a random MOSRotating from a group.
---@param groupName string
---@param moduleID number
---@return MOSRotating
function RandomMOSRotating(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return MOSRotating
function RandomMOSRotating(groupName, moduleName) end

---@param groupName string
---@return MOSRotating
function RandomMOSRotating(groupName) end

--- Treats an object as a Attachable, for its Attachable functions and properties. Use IsAttachable first when it might be something else.
---@param entity Entity
---@return Attachable
function ToAttachable(entity) end

--- Whether an object is a Attachable (or a kind of one).
---@param entity Entity
---@return boolean
function IsAttachable(entity) end

--- Makes a new Attachable from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return Attachable
function CreateAttachable(presetName, moduleName) end

---@param presetName string
---@return Attachable
function CreateAttachable(presetName) end

--- Makes a random Attachable from a group.
---@param groupName string
---@param moduleID number
---@return Attachable
function RandomAttachable(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return Attachable
function RandomAttachable(groupName, moduleName) end

---@param groupName string
---@return Attachable
function RandomAttachable(groupName) end

--- Treats an object as a Emission, for its Emission functions and properties. Use IsEmission first when it might be something else.
---@param entity Entity
---@return Emission
function ToEmission(entity) end

--- Whether an object is a Emission (or a kind of one).
---@param entity Entity
---@return boolean
function IsEmission(entity) end

--- Treats an object as a AEmitter, for its AEmitter functions and properties. Use IsAEmitter first when it might be something else.
---@param entity Entity
---@return AEmitter
function ToAEmitter(entity) end

--- Whether an object is a AEmitter (or a kind of one).
---@param entity Entity
---@return boolean
function IsAEmitter(entity) end

--- Makes a new AEmitter from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return AEmitter
function CreateAEmitter(presetName, moduleName) end

---@param presetName string
---@return AEmitter
function CreateAEmitter(presetName) end

--- Makes a random AEmitter from a group.
---@param groupName string
---@param moduleID number
---@return AEmitter
function RandomAEmitter(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return AEmitter
function RandomAEmitter(groupName, moduleName) end

---@param groupName string
---@return AEmitter
function RandomAEmitter(groupName) end

--- Treats an object as a AEJetpack, for its AEJetpack functions and properties. Use IsAEJetpack first when it might be something else.
---@param entity Entity
---@return AEJetpack
function ToAEJetpack(entity) end

--- Whether an object is a AEJetpack (or a kind of one).
---@param entity Entity
---@return boolean
function IsAEJetpack(entity) end

--- Makes a new AEJetpack from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return AEJetpack
function CreateAEJetpack(presetName, moduleName) end

---@param presetName string
---@return AEJetpack
function CreateAEJetpack(presetName) end

--- Makes a random AEJetpack from a group.
---@param groupName string
---@param moduleID number
---@return AEJetpack
function RandomAEJetpack(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return AEJetpack
function RandomAEJetpack(groupName, moduleName) end

---@param groupName string
---@return AEJetpack
function RandomAEJetpack(groupName) end

--- Treats an object as a PEmitter, for its PEmitter functions and properties. Use IsPEmitter first when it might be something else.
---@param entity Entity
---@return PEmitter
function ToPEmitter(entity) end

--- Whether an object is a PEmitter (or a kind of one).
---@param entity Entity
---@return boolean
function IsPEmitter(entity) end

--- Makes a new PEmitter from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return PEmitter
function CreatePEmitter(presetName, moduleName) end

---@param presetName string
---@return PEmitter
function CreatePEmitter(presetName) end

--- Makes a random PEmitter from a group.
---@param groupName string
---@param moduleID number
---@return PEmitter
function RandomPEmitter(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return PEmitter
function RandomPEmitter(groupName, moduleName) end

---@param groupName string
---@return PEmitter
function RandomPEmitter(groupName) end

--- Treats an object as a Actor, for its Actor functions and properties. Use IsActor first when it might be something else.
---@param entity Entity
---@return Actor
function ToActor(entity) end

--- Whether an object is a Actor (or a kind of one).
---@param entity Entity
---@return boolean
function IsActor(entity) end

--- Makes a new Actor from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return Actor
function CreateActor(presetName, moduleName) end

---@param presetName string
---@return Actor
function CreateActor(presetName) end

--- Makes a random Actor from a group.
---@param groupName string
---@param moduleID number
---@return Actor
function RandomActor(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return Actor
function RandomActor(groupName, moduleName) end

---@param groupName string
---@return Actor
function RandomActor(groupName) end

--- Treats an object as a ADoor, for its ADoor functions and properties. Use IsADoor first when it might be something else.
---@param entity Entity
---@return ADoor
function ToADoor(entity) end

--- Whether an object is a ADoor (or a kind of one).
---@param entity Entity
---@return boolean
function IsADoor(entity) end

--- Makes a new ADoor from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return ADoor
function CreateADoor(presetName, moduleName) end

---@param presetName string
---@return ADoor
function CreateADoor(presetName) end

--- Makes a random ADoor from a group.
---@param groupName string
---@param moduleID number
---@return ADoor
function RandomADoor(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return ADoor
function RandomADoor(groupName, moduleName) end

---@param groupName string
---@return ADoor
function RandomADoor(groupName) end

--- Treats an object as a Arm, for its Arm functions and properties. Use IsArm first when it might be something else.
---@param entity Entity
---@return Arm
function ToArm(entity) end

--- Whether an object is a Arm (or a kind of one).
---@param entity Entity
---@return boolean
function IsArm(entity) end

--- Makes a new Arm from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return Arm
function CreateArm(presetName, moduleName) end

---@param presetName string
---@return Arm
function CreateArm(presetName) end

--- Makes a random Arm from a group.
---@param groupName string
---@param moduleID number
---@return Arm
function RandomArm(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return Arm
function RandomArm(groupName, moduleName) end

---@param groupName string
---@return Arm
function RandomArm(groupName) end

--- Treats an object as a Leg, for its Leg functions and properties. Use IsLeg first when it might be something else.
---@param entity Entity
---@return Leg
function ToLeg(entity) end

--- Whether an object is a Leg (or a kind of one).
---@param entity Entity
---@return boolean
function IsLeg(entity) end

--- Makes a new Leg from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return Leg
function CreateLeg(presetName, moduleName) end

---@param presetName string
---@return Leg
function CreateLeg(presetName) end

--- Makes a random Leg from a group.
---@param groupName string
---@param moduleID number
---@return Leg
function RandomLeg(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return Leg
function RandomLeg(groupName, moduleName) end

---@param groupName string
---@return Leg
function RandomLeg(groupName) end

--- Treats an object as a AHuman, for its AHuman functions and properties. Use IsAHuman first when it might be something else.
---@param entity Entity
---@return AHuman
function ToAHuman(entity) end

--- Whether an object is a AHuman (or a kind of one).
---@param entity Entity
---@return boolean
function IsAHuman(entity) end

--- Makes a new AHuman from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return AHuman
function CreateAHuman(presetName, moduleName) end

---@param presetName string
---@return AHuman
function CreateAHuman(presetName) end

--- Makes a random AHuman from a group.
---@param groupName string
---@param moduleID number
---@return AHuman
function RandomAHuman(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return AHuman
function RandomAHuman(groupName, moduleName) end

---@param groupName string
---@return AHuman
function RandomAHuman(groupName) end

--- Treats an object as a ACrab, for its ACrab functions and properties. Use IsACrab first when it might be something else.
---@param entity Entity
---@return ACrab
function ToACrab(entity) end

--- Whether an object is a ACrab (or a kind of one).
---@param entity Entity
---@return boolean
function IsACrab(entity) end

--- Makes a new ACrab from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return ACrab
function CreateACrab(presetName, moduleName) end

---@param presetName string
---@return ACrab
function CreateACrab(presetName) end

--- Makes a random ACrab from a group.
---@param groupName string
---@param moduleID number
---@return ACrab
function RandomACrab(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return ACrab
function RandomACrab(groupName, moduleName) end

---@param groupName string
---@return ACrab
function RandomACrab(groupName) end

--- Treats an object as a Turret, for its Turret functions and properties. Use IsTurret first when it might be something else.
---@param entity Entity
---@return Turret
function ToTurret(entity) end

--- Whether an object is a Turret (or a kind of one).
---@param entity Entity
---@return boolean
function IsTurret(entity) end

--- Makes a new Turret from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return Turret
function CreateTurret(presetName, moduleName) end

---@param presetName string
---@return Turret
function CreateTurret(presetName) end

--- Makes a random Turret from a group.
---@param groupName string
---@param moduleID number
---@return Turret
function RandomTurret(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return Turret
function RandomTurret(groupName, moduleName) end

---@param groupName string
---@return Turret
function RandomTurret(groupName) end

--- Treats an object as a ACraft, for its ACraft functions and properties. Use IsACraft first when it might be something else.
---@param entity Entity
---@return ACraft
function ToACraft(entity) end

--- Whether an object is a ACraft (or a kind of one).
---@param entity Entity
---@return boolean
function IsACraft(entity) end

--- Treats an object as a ACDropShip, for its ACDropShip functions and properties. Use IsACDropShip first when it might be something else.
---@param entity Entity
---@return ACDropShip
function ToACDropShip(entity) end

--- Whether an object is a ACDropShip (or a kind of one).
---@param entity Entity
---@return boolean
function IsACDropShip(entity) end

--- Makes a new ACDropShip from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return ACDropShip
function CreateACDropShip(presetName, moduleName) end

---@param presetName string
---@return ACDropShip
function CreateACDropShip(presetName) end

--- Makes a random ACDropShip from a group.
---@param groupName string
---@param moduleID number
---@return ACDropShip
function RandomACDropShip(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return ACDropShip
function RandomACDropShip(groupName, moduleName) end

---@param groupName string
---@return ACDropShip
function RandomACDropShip(groupName) end

--- Treats an object as a ACRocket, for its ACRocket functions and properties. Use IsACRocket first when it might be something else.
---@param entity Entity
---@return ACRocket
function ToACRocket(entity) end

--- Whether an object is a ACRocket (or a kind of one).
---@param entity Entity
---@return boolean
function IsACRocket(entity) end

--- Makes a new ACRocket from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return ACRocket
function CreateACRocket(presetName, moduleName) end

---@param presetName string
---@return ACRocket
function CreateACRocket(presetName) end

--- Makes a random ACRocket from a group.
---@param groupName string
---@param moduleID number
---@return ACRocket
function RandomACRocket(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return ACRocket
function RandomACRocket(groupName, moduleName) end

---@param groupName string
---@return ACRocket
function RandomACRocket(groupName) end

--- Treats an object as a HeldDevice, for its HeldDevice functions and properties. Use IsHeldDevice first when it might be something else.
---@param entity Entity
---@return HeldDevice
function ToHeldDevice(entity) end

--- Whether an object is a HeldDevice (or a kind of one).
---@param entity Entity
---@return boolean
function IsHeldDevice(entity) end

--- Makes a new HeldDevice from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return HeldDevice
function CreateHeldDevice(presetName, moduleName) end

---@param presetName string
---@return HeldDevice
function CreateHeldDevice(presetName) end

--- Makes a random HeldDevice from a group.
---@param groupName string
---@param moduleID number
---@return HeldDevice
function RandomHeldDevice(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return HeldDevice
function RandomHeldDevice(groupName, moduleName) end

---@param groupName string
---@return HeldDevice
function RandomHeldDevice(groupName) end

--- Treats an object as a Magazine, for its Magazine functions and properties. Use IsMagazine first when it might be something else.
---@param entity Entity
---@return Magazine
function ToMagazine(entity) end

--- Whether an object is a Magazine (or a kind of one).
---@param entity Entity
---@return boolean
function IsMagazine(entity) end

--- Makes a new Magazine from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return Magazine
function CreateMagazine(presetName, moduleName) end

---@param presetName string
---@return Magazine
function CreateMagazine(presetName) end

--- Makes a random Magazine from a group.
---@param groupName string
---@param moduleID number
---@return Magazine
function RandomMagazine(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return Magazine
function RandomMagazine(groupName, moduleName) end

---@param groupName string
---@return Magazine
function RandomMagazine(groupName) end

--- Treats an object as a Round, for its Round functions and properties. Use IsRound first when it might be something else.
---@param entity Entity
---@return Round
function ToRound(entity) end

--- Whether an object is a Round (or a kind of one).
---@param entity Entity
---@return boolean
function IsRound(entity) end

--- Makes a new Round from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return Round
function CreateRound(presetName, moduleName) end

---@param presetName string
---@return Round
function CreateRound(presetName) end

--- Makes a random Round from a group.
---@param groupName string
---@param moduleID number
---@return Round
function RandomRound(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return Round
function RandomRound(groupName, moduleName) end

---@param groupName string
---@return Round
function RandomRound(groupName) end

--- Treats an object as a HDFirearm, for its HDFirearm functions and properties. Use IsHDFirearm first when it might be something else.
---@param entity Entity
---@return HDFirearm
function ToHDFirearm(entity) end

--- Whether an object is a HDFirearm (or a kind of one).
---@param entity Entity
---@return boolean
function IsHDFirearm(entity) end

--- Makes a new HDFirearm from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return HDFirearm
function CreateHDFirearm(presetName, moduleName) end

---@param presetName string
---@return HDFirearm
function CreateHDFirearm(presetName) end

--- Makes a random HDFirearm from a group.
---@param groupName string
---@param moduleID number
---@return HDFirearm
function RandomHDFirearm(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return HDFirearm
function RandomHDFirearm(groupName, moduleName) end

---@param groupName string
---@return HDFirearm
function RandomHDFirearm(groupName) end

--- Treats an object as a ThrownDevice, for its ThrownDevice functions and properties. Use IsThrownDevice first when it might be something else.
---@param entity Entity
---@return ThrownDevice
function ToThrownDevice(entity) end

--- Whether an object is a ThrownDevice (or a kind of one).
---@param entity Entity
---@return boolean
function IsThrownDevice(entity) end

--- Makes a new ThrownDevice from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return ThrownDevice
function CreateThrownDevice(presetName, moduleName) end

---@param presetName string
---@return ThrownDevice
function CreateThrownDevice(presetName) end

--- Makes a random ThrownDevice from a group.
---@param groupName string
---@param moduleID number
---@return ThrownDevice
function RandomThrownDevice(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return ThrownDevice
function RandomThrownDevice(groupName, moduleName) end

---@param groupName string
---@return ThrownDevice
function RandomThrownDevice(groupName) end

--- Treats an object as a TDExplosive, for its TDExplosive functions and properties. Use IsTDExplosive first when it might be something else.
---@param entity Entity
---@return TDExplosive
function ToTDExplosive(entity) end

--- Whether an object is a TDExplosive (or a kind of one).
---@param entity Entity
---@return boolean
function IsTDExplosive(entity) end

--- Makes a new TDExplosive from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return TDExplosive
function CreateTDExplosive(presetName, moduleName) end

---@param presetName string
---@return TDExplosive
function CreateTDExplosive(presetName) end

--- Makes a random TDExplosive from a group.
---@param groupName string
---@param moduleID number
---@return TDExplosive
function RandomTDExplosive(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return TDExplosive
function RandomTDExplosive(groupName, moduleName) end

---@param groupName string
---@return TDExplosive
function RandomTDExplosive(groupName) end

--- Treats an object as a PieSlice, for its PieSlice functions and properties. Use IsPieSlice first when it might be something else.
---@param entity Entity
---@return PieSlice
function ToPieSlice(entity) end

--- Whether an object is a PieSlice (or a kind of one).
---@param entity Entity
---@return boolean
function IsPieSlice(entity) end

--- Makes a new PieSlice from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return PieSlice
function CreatePieSlice(presetName, moduleName) end

---@param presetName string
---@return PieSlice
function CreatePieSlice(presetName) end

--- Makes a random PieSlice from a group.
---@param groupName string
---@param moduleID number
---@return PieSlice
function RandomPieSlice(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return PieSlice
function RandomPieSlice(groupName, moduleName) end

---@param groupName string
---@return PieSlice
function RandomPieSlice(groupName) end

--- Treats an object as a PieMenu, for its PieMenu functions and properties. Use IsPieMenu first when it might be something else.
---@param entity Entity
---@return PieMenu
function ToPieMenu(entity) end

--- Whether an object is a PieMenu (or a kind of one).
---@param entity Entity
---@return boolean
function IsPieMenu(entity) end

--- Makes a new PieMenu from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return PieMenu
function CreatePieMenu(presetName, moduleName) end

---@param presetName string
---@return PieMenu
function CreatePieMenu(presetName) end

--- Makes a random PieMenu from a group.
---@param groupName string
---@param moduleID number
---@return PieMenu
function RandomPieMenu(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return PieMenu
function RandomPieMenu(groupName, moduleName) end

---@param groupName string
---@return PieMenu
function RandomPieMenu(groupName) end

--- Treats an object as a Scene, for its Scene functions and properties. Use IsScene first when it might be something else.
---@param entity Entity
---@return Scene
function ToScene(entity) end

--- Whether an object is a Scene (or a kind of one).
---@param entity Entity
---@return boolean
function IsScene(entity) end

--- Makes a new Scene from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.
---@param presetName string
---@param moduleName string
---@return Scene
function CreateScene(presetName, moduleName) end

---@param presetName string
---@return Scene
function CreateScene(presetName) end

--- Makes a random Scene from a group.
---@param groupName string
---@param moduleID number
---@return Scene
function RandomScene(groupName, moduleID) end

---@param groupName string
---@param moduleName string
---@return Scene
function RandomScene(groupName, moduleName) end

---@param groupName string
---@return Scene
function RandomScene(groupName) end

--- Treats an object as a Deployment, for its Deployment functions and properties. Use IsDeployment first when it might be something else.
---@param entity Entity
---@return Deployment
function ToDeployment(entity) end

--- Whether an object is a Deployment (or a kind of one).
---@param entity Entity
---@return boolean
function IsDeployment(entity) end

--- Treats an object as a GameActivity, for its GameActivity functions and properties. Use IsGameActivity first when it might be something else.
---@param entity Entity
---@return GameActivity
function ToGameActivity(entity) end

--- Whether an object is a GameActivity (or a kind of one).
---@param entity Entity
---@return boolean
function IsGameActivity(entity) end

--- Treats an object as a GlobalScript, for its GlobalScript functions and properties. Use IsGlobalScript first when it might be something else.
---@param entity Entity
---@return GlobalScript
function ToGlobalScript(entity) end

--- Whether an object is a GlobalScript (or a kind of one).
---@param entity Entity
---@return boolean
function IsGlobalScript(entity) end
