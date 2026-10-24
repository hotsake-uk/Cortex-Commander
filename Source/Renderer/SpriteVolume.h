#pragma once

#include "glad/gl.h"

#include <memory>
#include <unordered_map>

namespace RTE {

	class BitmapTexture;

	/// Volume normal maps made from sprites' own shapes, so the lighting shades a sprite as a rounded solid instead of a flat cut-out with a bevelled rim (LightingSettings::SpriteVolume).
	/// Made the first time each frame is drawn and kept for as long as the frame exists. Each map holds, per pixel, the normal of a surface that rises from the sprite's outline
	/// over the rounding width and is level beyond it: a thin arm becomes a tube, a wide hull a slab with rounded edges.
	class SpriteVolume {

	public:
		/// Gets the volume map of a sprite frame, making it if it isn't made yet.
		/// @param frame The sprite frame, 8 bit (index 0 is see-through) or 32 bit with alpha.
		/// @param roundingWidth How far in from the outline the surface rises, in the frame's pixels. Maps made for another width are made again.
		/// @return The map's GL texture (RGB = normal * 0.5 + 0.5 in the frame's own axes, y down; A 1 inside the sprite), or 0 for a frame too small to need one.
		static GLuint Get(const std::shared_ptr<BitmapTexture>& frame, float roundingWidth);

	private:
		/// A frame's map, with the frame it was made from so a new frame made where a freed one was isn't given its map.
		struct Entry {
			std::weak_ptr<BitmapTexture> Frame;
			std::shared_ptr<BitmapTexture> Map; //!< Null for a frame too small to need one.
			float RoundingWidth = 0.0F;
		};

		static std::unordered_map<const BitmapTexture*, Entry> s_Maps; //!< The maps made, by the frame they're of.

		/// Makes a frame's map.
		static std::shared_ptr<BitmapTexture> Make(const BitmapTexture& frame, float roundingWidth);
	};
} // namespace RTE
