#pragma once

#include <string>

struct BITMAP;

namespace RTE {

	/// High resolution HUD text. Text the HUD draws into its low resolution bitmap is captured instead of rasterized, and drawn with a TTF font
	/// at the window's resolution after the frame is scaled up, so it stays crisp at any window size. Menus and GUI panels are composited on top
	/// afterwards, so they still cover it. Only text aimed at registered target bitmaps (the HUD layers) is captured; everything else is unchanged.
	class TextOverlay {

	public:
		/// Horizontal alignment of captured text, matching GUIFont's.
		enum Align {
			Left = 0,
			Centre = 1,
			Right = 2
		};

		/// Gets whether HUD text is drawn at high resolution.
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether HUD text is drawn at high resolution.
		static void SetEnabled(bool enabled) { s_Enabled = enabled; }

		/// Registers a bitmap whose text should be captured, and where it ends up on the internal resolution screen.
		/// @param bitmap The target bitmap (a HUD layer).
		/// @param offsetX Position of the bitmap's top left pixel on the screen, in internal pixels.
		/// @param offsetY Position of the bitmap's top left pixel on the screen, in internal pixels.
		/// @param clipWidth Size of the area of the screen text from this bitmap may cover, in internal pixels.
		/// @param clipHeight Size of the area of the screen text from this bitmap may cover, in internal pixels.
		static void SetTarget(const BITMAP* bitmap, int offsetX, int offsetY, int clipWidth, int clipHeight);

		/// Stops capturing text for the target bitmap. Text that something else was drawn over after it was captured is dropped,
		/// since overlay text is drawn above the whole layer but in the bitmap it would have been covered.
		static void ClearTarget();

		/// Captures a line of text aimed at the target bitmap, instead of it being drawn into it.
		/// @param target The bitmap the text was going to be drawn into.
		/// @param x Anchor of the text in the bitmap, in pixels: its left edge, centre or right edge according to align.
		/// @param y Top of the text line in the bitmap, in pixels.
		/// @param text One line of text.
		/// @param align How x anchors the text.
		/// @param lineHeight Height of the original font's lines, in pixels.
		/// @param fillRGB Text color, 0xRRGGBB.
		/// @param outlineRGB Outline color, 0xRRGGBB.
		/// @param shadow Whether the original asked for a drop shadow.
		/// @param bitmapWidth Width the text would have had in the bitmap font, for detecting whether something is drawn over it later.
		/// @return Whether the text was captured. If not, the caller draws it normally.
		static bool Capture(const BITMAP* target, int x, int y, const std::string& text, Align align, int lineHeight, unsigned int fillRGB, unsigned int outlineRGB, bool shadow, int bitmapWidth);

		/// Pauses capturing while GUI controls draw: their text sits among other controls in the same layer and must keep its place in the drawing order.
		static void SuspendCapture() { ++s_Suspended; }

		/// Resumes capturing after SuspendCapture.
		static void ResumeCapture() { --s_Suspended; }

		/// Gets whether any text is waiting to be drawn this frame.
		static bool HasPendingText();

		/// Draws this frame's captured text to the window and keeps it for redrawing the same frame later. Clears the captured text.
		/// @param windowWidth The window's size in pixels.
		/// @param windowHeight The window's size in pixels.
		/// @param viewportX The letterboxed game area in the window, GL convention (from the bottom left), in pixels.
		/// @param viewportY The letterboxed game area in the window, GL convention (from the bottom left), in pixels.
		/// @param viewportWidth The letterboxed game area in the window, in pixels.
		/// @param viewportHeight The letterboxed game area in the window, in pixels.
		/// @param internalWidth The internal resolution the HUD was laid out at.
		/// @param internalHeight The internal resolution the HUD was laid out at.
		/// @param redrawLast Draws the text of the last presented frame again instead (for redraws when the window is exposed).
		static void Render(int windowWidth, int windowHeight, int viewportX, int viewportY, int viewportWidth, int viewportHeight, int internalWidth, int internalHeight, bool redrawLast = false);

		/// Converts a color value of a bitmap color depth to 0xRRGGBB.
		/// @param color The color value; a palette index for 8 bit.
		/// @param colorDepth The color depth the value is in.
		static unsigned int ToRGB(unsigned long color, int colorDepth);

		/// Releases the font and its glyph textures.
		static void Destroy();

	private:
		static bool s_Enabled; //!< Whether HUD text is drawn at high resolution.
		static int s_Suspended; //!< Nesting count of SuspendCapture.
	};
} // namespace RTE
