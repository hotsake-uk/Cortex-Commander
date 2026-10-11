#pragma once

#include <string>
#include <vector>

struct BITMAP;

namespace RTE {
	class Actor;
	class Vector;

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
			std::string Group; //!< Which heading the settings list it under ("Combat", "Weather"); empty for "Other".
			float Chance = 1.0F; //!< Times the chance in the settings (0.5 says it half as often).
			int CooldownMS = 6000; //!< How long before the same unit says this again.
			int TeamCooldownMS = 1500; //!< How long before anyone on the same side says this again, so a squad doesn't shout it in chorus.
			bool Urgent = false; //!< Cuts in over a line still showing (a grenade), where others wait for it.
			bool Order = false; //!< An answer to an order. Said by the commands themselves (SayOrder); the AI's own guess at an order (Say) is left out while one has just been answered.
		};

		/// What a unit last noticed about its surroundings and itself (UpdateWorld), to say something when it changes. Kept on the actor, in its State.
		struct Senses {
			bool Primed = false; //!< Looked once already: the first look only notes how things are, so nobody remarks on how a game started.
			long long NextLookUpdate = 0; //!< The sim update it looks again.
			int Liquid = 0; //!< The kind of liquid it is in (UnitSpeechWorld's), 0 for none.
			long long LiquidSinceMS = 0; //!< Since when it's been in that liquid.
			int Depth = 0; //!< How deep, as Actor::GetLiquidDepth.
			float Air = 1.0F; //!< Air left, as Actor::GetAirLeft.
			int Ground = 0; //!< The kind of ground it last stood on, 0 for none seen yet.
			unsigned long long Flags = 0; //!< Conditions that held at the last look, by UnitSpeechWorld's bits.
			int Arms = -1; //!< Arms and legs it had, -1 for not a body that has them.
			int Legs = -1;
			float FallStartY = 0.0F; //!< Where it began falling, while it falls.
			float JetLeft = 1.0F; //!< Its jetpack's fuel left, as a share.
			float Health = 0.0F; //!< Its health at the last look (vehicles).
			long long CoveredSinceMS = 0; //!< Since when it has been under a roof thick enough to be underground, 0 for not.
			long BodyID = 0; //!< The last body it remarked on, so it isn't remarked on again.
			long CraftID = 0; //!< The last craft of its side it saw come in.
			int Items = -1; //!< How many items it carries and holds, -1 for not counted yet.
		};

		/// What a unit is saying, and what it said lately. Kept on the actor (Actor::GetSpeech); only that actor's own updates write it.
		struct State {
			std::string Text; //!< The line showing, empty for none.
			long long StartMS = 0; //!< Sim time it was said.
			int DurationMS = 0; //!< How long it shows.
			int Trigger = -1; //!< The trigger it was said for, -1 for a line a script gave.
			int Line = -1; //!< Which of the trigger's lines, so the next time picks another.
			std::vector<long long> LastSaidMS; //!< When each trigger was last said by this unit, by trigger index.
			long long OrderAnsweredMS = 0; //!< Sim time a command last asked this unit to answer an order (SayOrder), said or not.
			Senses World; //!< What it last noticed around it (UpdateWorld).
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

		/// Gets whether lines are drawn in a bubble, or as bare text over the unit.
		static bool ShowsBubbles() { return s_ShowBubbles; }

		/// Sets whether lines are drawn in a bubble, or as bare text over the unit.
		static void SetShowsBubbles(bool show) { s_ShowBubbles = show; }

		/// Gets whether a trigger is on, by its key. A key no Speech.ini defines reads as on.
		static bool IsTriggerOn(const std::string& key);

		/// Turns a trigger on or off, by its key. Kept by key, so a trigger a mod adds later can be turned off before it's loaded.
		static void SetTriggerOn(const std::string& key, bool on);

		/// Gets the keys of the triggers turned off, for Settings.ini.
		static std::vector<std::string> GetTriggersOff();

		/// Gets the tones lines come in ("Serious", "Casual", "Funny"; Speech.ini's "Tone = ..."), in the order first used.
		static std::vector<std::string> GetTones();

		/// Gets the tones a side's units speak in, as Settings.ini keeps them: "Any", or the tones' names separated by commas.
		/// @param team The side, 0 to 3.
		static std::string GetTeamTonesText(int team);

		/// Sets the tones a side's units speak in from Settings.ini's text ("Any", or names separated by commas).
		/// @param team The side, 0 to 3.
		static void SetTeamTonesText(int team, const std::string& text);

		/// Gets whether a side's units speak lines of a tone: true for every tone when the side takes any.
		static bool TeamUsesTone(int team, const std::string& tone);

		/// Gets whether a side's units take lines of any tone.
		static bool TeamUsesAnyTone(int team);

		/// Adds a tone to, or takes it from, those a side's units speak in. With none left, the side takes any.
		static void SetTeamTone(int team, const std::string& tone, bool on);

		/// Gets how often a tone is picked against the others a side speaks in, 0 to 100 (Speech.ini's ToneWeight, else 100, unless set).
		static int GetToneWeight(const std::string& tone);

		/// Gets what a tone is like, for the settings (Speech.ini's ToneDescription); empty if none says.
		static std::string GetToneDescription(const std::string& tone);

		/// Sets how often a tone is picked against the others a side speaks in, 0 to 100.
		static void SetToneWeight(const std::string& tone, int weight);

		/// Gets the tones' weights as Settings.ini keeps them: "Serious:10, Funny:90" (tones not listed are 100).
		static std::string GetToneWeightsText();

		/// Sets the tones' weights from Settings.ini's text.
		static void SetToneWeightsText(const std::string& text);

		/// Has a side take lines of any tone.
		static void SetTeamAnyTone(int team);
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
		static bool Say(Actor& actor, const std::string& triggerKey) { return Say(actor, triggerKey, false, nullptr); }

		/// Has an actor say one of a trigger's lines about another, as Say. "{name}" in a line is the other's name (GetName); lines with it are
		/// only picked when there is someone to name. "{self}", in any line, is the speaker's own.
		/// @param actor Who says it.
		/// @param triggerKey The trigger, as Speech.ini names it.
		/// @param subject Who it's about; none for a line that names nobody.
		/// @return Whether a line was said.
		static bool SayAbout(Actor& actor, const std::string& triggerKey, const Actor* subject) { return Say(actor, triggerKey, false, subject); }

		/// Gets the name a unit's friends call it: one of Speech.ini's UnitName list (its speech set's own, if that has one), picked by the unit's ID
		/// so it stays the same all game.
		static std::string GetName(const Actor& actor);

		/// Has an actor answer an order the player just gave it ("Moving!", "Holding fire."), as Say. Called by the commands that give the order,
		/// which know what it was; for a moment after, the AI's own answer to an order (UnitSpeech.lua, which only sees that some order came) is
		/// left out, so the unit doesn't answer twice.
		/// @param actor Who answers.
		/// @param triggerKey The trigger, as Speech.ini names it.
		/// @param force Says it whatever the chance, the cooldown and a line still showing (still not with speech off, or no lines for it).
		/// @return Whether a line was said.
		static bool SayOrder(Actor& actor, const std::string& triggerKey, bool force = false);

		/// Has an actor say the given words, whatever the chance and the triggers (unit speech must be on). For scripts.
		/// @param actor Who says it.
		/// @param text What it says.
		/// @param durationMS How long it shows; 0 or less for as long as a line that long usually does.
		static void SayText(Actor& actor, const std::string& text, int durationMS);

		/// Has units remark on what happens to them and around them that their AI doesn't decide: wading into acid, catching fire, gas,
		/// the weather, nightfall, falling rock, losing a limb, a cart, flags. Each unit looks a few times a second; a line comes when what it
		/// sees changes (or, for the weather and the like, now and then while it lasts). Main thread, once per sim update, after the liquids
		/// and gas have updated (UnitSpeechWorld.cpp). Reads the world, never changes it.
		static void UpdateWorld();

		/// Notes a lightning strike, for the units near it to remark on. Thread safe.
		/// @param position Where it landed.
		static void NoteLightning(const Vector& position);

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
		static bool s_ShowBubbles; //!< Whether lines are drawn in a bubble (off: bare text).

		/// Say, from a command answering an order or not, about someone or not.
		static bool Say(Actor& actor, const std::string& triggerKey, bool answeringOrder, const Actor* subject, bool force = false);

		/// Reads the Speech.ini files if they haven't been yet. Safe from any thread.
		static void EnsureLoaded();

		/// Reads Base.rte's Speech.ini and every other module's after it. Called with the load lock held.
		static void LoadAll();
	};
} // namespace RTE
