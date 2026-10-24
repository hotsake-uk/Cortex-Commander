#include "SpriteVolume.h"
#include "Texture.h"
#include "allegro.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace RTE;

std::unordered_map<const BitmapTexture*, SpriteVolume::Entry> SpriteVolume::s_Maps;

namespace {
	/// Frames smaller than this both ways keep the automatic bevel: particles, gibs and the like have no shape to round.
	constexpr int c_MinSize = 6;
} // namespace

GLuint SpriteVolume::Get(const std::shared_ptr<BitmapTexture>& frame, float roundingWidth) {
	if (!frame) {
		return 0;
	}
	auto found = s_Maps.find(frame.get());
	if (found != s_Maps.end() && found->second.Frame.lock() == frame && found->second.RoundingWidth == roundingWidth) {
		return found->second.Map ? found->second.Map->GetTextureId() : 0;
	}
	Entry& entry = s_Maps[frame.get()];
	entry.Frame = frame;
	entry.RoundingWidth = roundingWidth;
	entry.Map = Make(*frame, roundingWidth);
	return entry.Map ? entry.Map->GetTextureId() : 0;
}

std::shared_ptr<BitmapTexture> SpriteVolume::Make(const BitmapTexture& frame, float roundingWidth) {
	const BITMAP* bitmap = frame.GetBitmap();
	if (!bitmap || (bitmap->w < c_MinSize && bitmap->h < c_MinSize)) {
		return nullptr;
	}
	const int width = bitmap->w;
	const int height = bitmap->h;
	const int depth = bitmap_color_depth(const_cast<BITMAP*>(bitmap));
	if (depth != 8 && depth != 32) {
		return nullptr;
	}
	std::vector<unsigned char> inside(width * height, 0);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			if (depth == 8) {
				inside[y * width + x] = bitmap->line[y][x] != 0;
			} else {
				inside[y * width + x] = geta32(reinterpret_cast<const uint32_t*>(bitmap->line[y])[x]) > 127;
			}
		}
	}

	// Distance from each pixel inside to the nearest one outside (past the frame's border counts as outside), in thirds of a pixel: a two pass 3-4 chamfer.
	constexpr int c_Far = 1 << 20;
	std::vector<int> distance(width * height);
	for (int i = 0; i < width * height; ++i) {
		distance[i] = inside[i] ? c_Far : 0;
	}
	auto at = [&](int x, int y) { return (x < 0 || y < 0 || x >= width || y >= height) ? 0 : distance[y * width + x]; };
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			int& here = distance[y * width + x];
			if (here > 0) {
				here = std::min({here, at(x - 1, y) + 3, at(x, y - 1) + 3, at(x - 1, y - 1) + 4, at(x + 1, y - 1) + 4});
			}
		}
	}
	for (int y = height - 1; y >= 0; --y) {
		for (int x = width - 1; x >= 0; --x) {
			int& here = distance[y * width + x];
			if (here > 0) {
				here = std::min({here, at(x + 1, y) + 3, at(x, y + 1) + 3, at(x + 1, y + 1) + 4, at(x - 1, y + 1) + 4});
			}
		}
	}

	// The surface's height: a quarter circle from the outline up to the rounding width, level beyond, so the edge turns right away and the middle faces the viewer.
	// The outline's pixels stand half a pixel up, as if the rise starts at their outer edge.
	const float rounding = std::max(roundingWidth, 1.0F);
	std::vector<float> rise(width * height, 0.0F);
	for (int i = 0; i < width * height; ++i) {
		if (inside[i]) {
			float in = std::min((static_cast<float>(distance[i]) / 3.0F - 0.5F) / rounding, 1.0F);
			float t = 1.0F - std::max(in, 0.0F);
			rise[i] = rounding * std::sqrt(std::max(1.0F - t * t, 0.0F));
		}
	}
	// Smoothed over the pixels next door inside the sprite, so the chamfer's steps and corners don't show as facets.
	std::vector<float> smooth(rise);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			if (!inside[y * width + x]) {
				continue;
			}
			float sum = 0.0F;
			float weight = 0.0F;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					int nx = x + dx;
					int ny = y + dy;
					if (nx >= 0 && ny >= 0 && nx < width && ny < height && inside[ny * width + nx]) {
						float w = (dx == 0 && dy == 0) ? 4.0F : (dx == 0 || dy == 0) ? 2.0F : 1.0F;
						sum += rise[ny * width + nx] * w;
						weight += w;
					}
				}
			}
			smooth[y * width + x] = sum / weight;
		}
	}

	BITMAP* map = create_bitmap_ex(32, width, height);
	clear_to_color(map, makeacol32(128, 128, 255, 0));
	auto heightAt = [&](int x, int y) { return (x < 0 || y < 0 || x >= width || y >= height || !inside[y * width + x]) ? 0.0F : smooth[y * width + x]; };
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			if (!inside[y * width + x]) {
				continue;
			}
			// The slope, downhill being the way the surface faces.
			float slopeX = (heightAt(x + 1, y) - heightAt(x - 1, y)) * 0.5F;
			float slopeY = (heightAt(x, y + 1) - heightAt(x, y - 1)) * 0.5F;
			float nx = -slopeX;
			float ny = -slopeY;
			float nz = 1.0F;
			float length = std::sqrt(nx * nx + ny * ny + nz * nz);
			nx /= length;
			ny /= length;
			nz /= length;
			auto encode = [](float value) { return static_cast<int>(std::clamp(value * 0.5F + 0.5F, 0.0F, 1.0F) * 255.0F + 0.5F); };
			reinterpret_cast<uint32_t*>(map->line[y])[x] = makeacol32(encode(nx), encode(ny), encode(nz), 255);
		}
	}
	return std::make_shared<BitmapTexture>(std::unique_ptr<BITMAP, BitmapDeleter>(map), Filter::Nearest);
}
