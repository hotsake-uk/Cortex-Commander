#pragma once

namespace RTE {

	class Actor;
	struct ActorConcealment;

	/// Shows what AI units can see and are aware of, drawn at window resolution over player 1's view: a mark over each AI unit (a "?" that
	/// fills while it notices someone, an orange "?" while it searches, a red "!" when it spots someone), its sight cone (its field of view,
	/// as far as it sees, cut short by walls, night and dust, coloured by what it is aware of), and a meter by the unit you play saying how
	/// visible you are and why (the dark, a roof, crouching, keeping still; a headlamp, firing or loud steps giving you away).
	class AIAwareness {

	public:
		/// Whose marks and cones are shown.
		enum Units : int {
			NoUnits = 0, //!< Off.
			EnemyUnits, //!< Other sides' AI units (every side's in the Sandbox game mode, where you look from no side).
			AllUnits //!< Your own side's AI units too.
		};

		/// When the visibility meter is shown.
		enum MeterShown : int {
			MeterOff = 0, //!< Never.
			MeterAlways, //!< Always, by the unit you play.
			MeterWhenHidden //!< Only while it is dark, you are hidden or cloaked, or an enemy is noticing or fighting you.
		};

		static int Marks; //!< Whose awareness marks are drawn (Units).
		static int Cones; //!< Whose sight cones are drawn (Units).
		static int Meter; //!< When the visibility meter is shown (MeterShown).
		static bool OnlyWithStealth; //!< All three only while "Night, light and noise affect AI" (stealth) is on.
		static float ConeOpacity; //!< How strongly the sight cones are drawn, 0 to 1.
		static float MarkScale; //!< How big the marks and the meter are drawn, about 1.

		/// Whether the visibility meter wants the controlled unit's concealment looked at this sim update.
		static bool MeterWanted();

		/// Keeps how well hidden the unit player 1 plays is (Actor::GetConcealment), and how loud its steps are, for the meter. Called in the sim.
		static void StoreConcealment(const Actor& actor, const ActorConcealment& concealment, float footstepNoise);

		/// Works out an AI unit's sight cone afresh (rays cut short by walls), now and then, when its scan reports (Actor::ReportAwareness): in
		/// the sim, so the drawing reads only what is kept.
		static void UpdateCone(Actor& actor);

		/// Whether the awareness marks stand in for the old alert mark (Actor::DrawHUD) over this unit: an AI unit whose mark is drawn.
		static bool ReplacesAlertMark(const Actor& actor);

		/// Draws the marks, cones and meter for player 1. Call once per frame while building the ImGui frame.
		static void Draw();
	};
} // namespace RTE
