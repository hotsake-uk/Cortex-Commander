#pragma once

#include <string>
#include <vector>

struct BITMAP;

namespace RTE {
	class Actor;

	/// Unit speech (US-1): short lines a unit says over its head when its AI does something worth knowing ("Take cover!", "Reloading!",
	/// "Got one!"), so a player can see what the units are up to. The triggers and their lines are data: Base.rte/Speech.ini, then each
	/// other module's Speech.ini in load order, which can add lines, replace them, add triggers of its own, and give a faction or a unit
	/// type a set of lines of its own (an actor's SpeechSet). The AI scripts fire triggers (Actor:Say), as does the engine for what only it
	/// sees (a stuck remedy, a friend falling); each trigger is said on a chance, and can be turned off on its own in the settings.
	/// Cosmetic only: nothing here touches the simulation or its random numbers, so it can be called from the threaded AI.
	class UnitSpeech {

	public:
		/// One kind of thing a unit can say something about, as Speech.ini defines it.
		struct Trigger {
			std::string Key; //!< What scripts and Speech.ini call it ("TakeCover").
			std::string Name; //!< What the settings call it ("Takes cover").
			std::string Description; //!< The settings' tooltip.
			float Chance = 1.0F; //!< Times the chance in the settings (0.5 says it half as often).
			int CooldownMS = 6000; //!< How long before the same unit says this again.
			int TeamCooldownMS = 1500; //!< How long before anyone on the same side says this again, so a squad doesn't shout it in chorus.
			bool Urgent = false; //!< Cuts in over a line still showing (a grenade), where others wait for it.
		};

		/// What a unit is saying, and what it said lately. Kept on the actor (Actor::GetSpeech); only that actor's own updates write it.
		struct State {
			std::string Text; //!< The line showing, empty for none.
			long long StartMS = 0; //!< Sim time it was said.
			int DurationMS = 0; //!< How long it shows.
			int Trigger = -1; //!< The trigger it was said for, -1 for a line a script gave.
			int Line = -1; //!< Which of the trigger's lines, so the next time picks another.
			std::vector<long long> LastSaidMS; //!< When each trigger was last said by this unit, by trigger index.
		};

#pragma region Settings
		/// Gets whether units say anything at all.
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether units say anything at all.
		static void SetEnabled(bool enabled) { s_Enabled = enabled; }

		/// Gets the chance, 0 to 100 percent, that a unit says something when a trigger fires (times the trigger's own Chance).
		static int GetChance() { return s_ChancePercent; }

		/// Sets the chance, 0 to 100 percent, that a unit says something when a trigger fires.
		static void SetChance(int percent);

		/// Gets whether the other sides' units are heard too (where the viewing side can see them).
		static bool ShowsEnemies() { return s_ShowEnemies; }

		/// Sets whether the other sides' units are heard too.
		static void SetShowsEnemies(bool show) { s_ShowEnemies = show; }

		/// Gets whether a trigger is on, by its key. A key no Speech.ini defines reads as on.
		static bool IsTriggerOn(const std::string& key);

		/// Turns a trigger on or off, by its key. Kept by key, so a trigger a mod adds later can be turned off before it's loaded.
		static void SetTriggerOn(const std::string& key, bool on);

		/// Gets the keys of the triggers turned off, for Settings.ini.
		static std::vector<std::string> GetTriggersOff();
#pragma endregion

#pragma region Lines
		/// Gets every trigger the loaded Speech.ini files define, in the order they were first defined.
		static const std::vector<Trigger>& GetTriggers();

		/// Gets a trigger's index by its key, -1 if none has it.
		static int FindTrigger(const std::string& key);

		/// Gets the first line a trigger has in the default set, for the settings' tooltip; empty if it has none.
		static std::string GetExampleLine(int trigger);

		/// Reads the Speech.ini files again (for modders trying lines out). Main thread only, outside the sim update.
		static void Reload();
#pragma endregion

#pragma region Saying
		/// Has an actor say one of a trigger's lines: on the settings' chance, unless the trigger is off, the actor said it lately, a
		/// friend just said it, or it is still saying something else.
		/// @param actor Who says it.
		/// @param triggerKey The trigger, as Speech.ini names it.
		/// @return Whether a line was said.
		static bool Say(Actor& actor, const std::string& triggerKey);

		/// Has an actor say the given words, whatever the chance and the triggers (unit speech must be on). For scripts.
		/// @param actor Who says it.
		/// @param text What it says.
		/// @param durationMS How long it shows; 0 or less for as long as a line that long usually does.
		static void SayText(Actor& actor, const std::string& text, int durationMS);

		/// Draws a speech bubble, in the game's own UI look (dark blue box, small game font) edged in the side's colour, with its tail at a point.
		/// @param targetBitmap The 8-bit HUD bitmap to draw to.
		/// @param x Where the tail points, across.
		/// @param y Where the tail points, down (the top of the unit's HUD).
		/// @param state What the unit is saying.
		/// @param team The unit's side, for the edge colour.
		static void DrawBubble(BITMAP* targetBitmap, int x, int y, const State& state, int team);
#pragma endregion

	private:
		static bool s_Enabled; //!< Whether units say anything at all.
		static int s_ChancePercent; //!< The chance a trigger is said, 0 to 100.
		static bool s_ShowEnemies; //!< Whether the other sides' units are heard too.

		/// Reads the Speech.ini files if they haven't been yet. Safe from any thread.
		static void EnsureLoaded();

		/// Reads Base.rte's Speech.ini and every other module's after it. Called with the load lock held.
		static void LoadAll();
	};
} // namespace RTE
