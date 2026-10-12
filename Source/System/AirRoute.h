#pragma once

#include "Vector.h"

#include <deque>

namespace RTE {

	/// The shape a body takes up in the air, for planning a way through it (AirRoute): its half width, how far it reaches over and under its
	/// middle (an underside's figure takes in the gap it keeps over the ground), and how high it may go.
	struct AirBody {
		float HalfWidth = 8.0F; //!< Half its width, in pixels.
		float Top = 8.0F; //!< How far it reaches over its middle, in pixels.
		float Under = 8.0F; //!< How far it reaches under its middle, its gap over the ground taken in, in pixels.
		float SkyTop = 0.0F; //!< The highest its middle may go (above the top of the scene for a ship with room up there).
		float OrbitGrace = 0.0F; //!< How far past the scene's sides it may go in a scene that doesn't wrap.
		int Team = -1; //!< Its side, for keeping off enemies on a cautious order.
		const void* Self = nullptr; //!< Itself, never counted as an enemy.
	};

	/// Planning a way through the air for a body of a given size: the flying ships' route search (ACShip), shared with creatures that fly
	/// (ACrab's flight).
	namespace AirRoute {

		/// Whether a body could fly straight from one place to another: its outline swept along the line (its two ends, top and bottom, and
		/// its middle) meets nothing solid.
		bool LineClear(const AirBody& body, const Vector& from, const Vector& to);

		/// Plans a route through the air from one place to another, over, under or round whatever is in the way, terrain and buildings and
		/// doors alike: the shortest way on a grid of the places the whole body fits, however high that takes it, and, on a cautious order,
		/// kept off enemies. Then cut to as few points as have open air for the body between (LineClear). With no way all the way there, the
		/// route goes as near as it can get.
		/// @param body The body. @param from Where it starts. @param to Where it is going. @param care How much a cautious order keeps it off
		/// enemies, 0 to 1. @param route Filled with the route's points, the first to fly to first. @param reached Set to whether the route
		/// gets all the way there.
		/// @return Whether a route was found at all (false where it has no room to start from).
		bool Plan(const AirBody& body, const Vector& from, const Vector& to, float care, std::deque<Vector>& route, bool& reached);
	} // namespace AirRoute
} // namespace RTE
