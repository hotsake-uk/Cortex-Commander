#include "Shapes.h"
#include "DrawCall.h"
#include "RenderMan.h"
#include "Constants.h"
#include "RTETools.h"
#include "tracy/Tracy.hpp"

#include <algorithm>
#include <cmath>

using namespace RTE;

namespace {
	/// Number of segments to use for a curve of the given radius covering the given arc (radians), so curves stay smooth at any size without wasting vertices on tiny ones.
	int SegmentsForArc(float radius, float arcRadians) {
		int segments = static_cast<int>(std::ceil(std::abs(arcRadians) * std::max(radius, 1.0f) / 2.0f));
		return std::clamp(segments, 4, 256);
	}

	/// Appends a quad covering a line segment of the given thickness to a shape.
	void AppendSegment(Shape::Shape& shape, glm::vec2 start, glm::vec2 end, float thickness, glm::u8vec4 color) {
		glm::vec2 direction = end - start;
		float length = glm::length(direction);
		if (length < 0.0001f) {
			direction = glm::vec2(1.0f, 0.0f);
			length = 0.0f;
		} else {
			direction /= length;
		}
		// Extend half a pixel along the direction so the end pixels are covered like a rasterized line would.
		glm::vec2 halfExtent = direction * 0.5f;
		glm::vec2 normal = glm::vec2(-direction.y, direction.x) * (std::max(thickness, 1.0f) * 0.5f);
		int base = static_cast<int>(shape.m_Vertices.size());
		shape.m_Vertices.emplace_back(start - halfExtent + normal, color);
		shape.m_Vertices.emplace_back(start - halfExtent - normal, color);
		shape.m_Vertices.emplace_back(end + halfExtent - normal, color);
		shape.m_Vertices.emplace_back(end + halfExtent + normal, color);
		shape.m_Indices.insert(shape.m_Indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
	}

	/// Appends a filled triangle fan around a center point through the given outline points.
	void AppendFan(Shape::Shape& shape, glm::vec2 center, const std::vector<glm::vec2>& outline, glm::u8vec4 color, bool closed) {
		if (outline.size() < 2) {
			return;
		}
		int base = static_cast<int>(shape.m_Vertices.size());
		shape.m_Vertices.emplace_back(center, color);
		for (const glm::vec2& point: outline) {
			shape.m_Vertices.emplace_back(point, color);
		}
		int count = static_cast<int>(outline.size());
		for (int i = 0; i < count - 1; ++i) {
			shape.m_Indices.insert(shape.m_Indices.end(), {base, base + 1 + i, base + 2 + i});
		}
		if (closed) {
			shape.m_Indices.insert(shape.m_Indices.end(), {base, base + count, base + 1});
		}
	}

	/// Points along an elliptical arc. Angles are in degrees, measured like screen space (Y down).
	std::vector<glm::vec2> ArcPoints(glm::vec2 center, float radiusH, float radiusV, float startAngle, float endAngle) {
		float startRadians = DegreesToRadians(startAngle);
		float endRadians = DegreesToRadians(endAngle);
		int segments = SegmentsForArc(std::max(radiusH, radiusV), endRadians - startRadians);
		std::vector<glm::vec2> points;
		points.reserve(segments + 1);
		for (int i = 0; i <= segments; ++i) {
			float angle = startRadians + (endRadians - startRadians) * (static_cast<float>(i) / static_cast<float>(segments));
			points.emplace_back(center + glm::vec2(std::cos(angle) * radiusH, std::sin(angle) * radiusV));
		}
		return points;
	}

	std::shared_ptr<DrawCall> Submit(Shape::Shape&& shape, GLenum drawMode = GL_TRIANGLES) {
		std::shared_ptr<DrawCall> draw = g_RenderMan.BeginDraw();
		draw->m_Vertices = std::move(shape.m_Vertices);
		draw->m_Indices = std::move(shape.m_Indices);
		draw->m_TextureId = g_RenderMan.GetShapeTexture();
		draw->m_Indexed = false;
		draw->m_DrawMode = drawMode;
		return draw;
	}
} // namespace

Shape::Shape Shape::Pixel(glm::vec2 position, Color color) {
	return Rectangle(FloatRect(position.x, position.y, 1.0f, 1.0f), color);
}

Shape::Shape Shape::Line(glm::vec2 start, glm::vec2 end, Color color) {
	return Line(start, end, 1.0f, color);
}

Shape::Shape Shape::Line(glm::vec2 start, glm::vec2 end, float thickness, Color color) {
	ZoneScoped;
	Shape line;
	// Offset to pixel centers so 1px lines land exactly on the pixels a rasterizer would fill.
	AppendSegment(line, start + glm::vec2(0.5f), end + glm::vec2(0.5f), thickness, color);
	return line;
}

Shape::Shape Shape::LineStrip(const std::vector<glm::vec2>& points, Color color) {
	return LineStrip(points, 1.0f, color);
}

Shape::Shape Shape::LineStrip(const std::vector<glm::vec2>& points, float thickness, Color color) {
	ZoneScoped;
	Shape strip;
	for (size_t i = 1; i < points.size(); ++i) {
		AppendSegment(strip, points[i - 1] + glm::vec2(0.5f), points[i] + glm::vec2(0.5f), thickness, color);
	}
	return strip;
}

Shape::Shape Shape::LineBezier(glm::vec2 start, glm::vec2 end, Color color) {
	// Simple S-curve between two points, matching raylib's DrawLineBezier.
	glm::vec2 control = glm::vec2((start.x + end.x) * 0.5f, start.y);
	glm::vec2 control2 = glm::vec2((start.x + end.x) * 0.5f, end.y);
	return LineSpline({start, control, control2, end}, color);
}

Shape::Shape Shape::LineSpline(const std::array<glm::vec2, 4>& controlPoints, Color color) {
	ZoneScoped;
	float approximateLength = glm::length(controlPoints[1] - controlPoints[0]) + glm::length(controlPoints[2] - controlPoints[1]) + glm::length(controlPoints[3] - controlPoints[2]);
	int segments = std::clamp(static_cast<int>(approximateLength / 4.0f), 8, 128);
	std::vector<glm::vec2> points;
	points.reserve(segments + 1);
	for (int i = 0; i <= segments; ++i) {
		float t = static_cast<float>(i) / static_cast<float>(segments);
		float u = 1.0f - t;
		points.emplace_back(u * u * u * controlPoints[0] + 3.0f * u * u * t * controlPoints[1] + 3.0f * u * t * t * controlPoints[2] + t * t * t * controlPoints[3]);
	}
	return LineStrip(points, 1.0f, color);
}

Shape::Shape Shape::Circle(glm::vec2 center, float radius, Color color) {
	return Ellipse(center, radius, radius, color);
}

Shape::Shape Shape::CircleSector(glm::vec2 center, float radius, float startAngle, float endAngle, Color color) {
	ZoneScoped;
	Shape sector;
	AppendFan(sector, center + glm::vec2(0.5f), ArcPoints(center + glm::vec2(0.5f), radius, radius, startAngle, endAngle), color, false);
	return sector;
}

Shape::Shape Shape::CircleLines(glm::vec2 center, float radius, Color color) {
	return EllipseLines(center, radius, radius, color);
}

Shape::Shape Shape::CircleLinesSector(glm::vec2 center, float radius, float startAngle, float endAngle, Color color) {
	return Ring(center, radius - 0.5f, radius + 0.5f, startAngle, endAngle, color);
}

Shape::Shape Shape::Ellipse(glm::vec2 center, float radiusH, float radiusV, Color color) {
	ZoneScoped;
	Shape ellipse;
	glm::vec2 pixelCenter = center + glm::vec2(0.5f);
	// Radius + 0.5 so the filled area covers the same pixels as a rasterized circle of that radius.
	std::vector<glm::vec2> outline = ArcPoints(pixelCenter, radiusH + 0.5f, radiusV + 0.5f, 0.0f, 360.0f);
	outline.pop_back();
	AppendFan(ellipse, pixelCenter, outline, color, true);
	return ellipse;
}

Shape::Shape Shape::EllipseLines(glm::vec2 center, float radiusH, float radiusV, Color color) {
	ZoneScoped;
	Shape ellipse;
	glm::vec2 pixelCenter = center + glm::vec2(0.5f);
	std::vector<glm::vec2> inner = ArcPoints(pixelCenter, std::max(radiusH - 0.5f, 0.0f), std::max(radiusV - 0.5f, 0.0f), 0.0f, 360.0f);
	std::vector<glm::vec2> outer = ArcPoints(pixelCenter, radiusH + 0.5f, radiusV + 0.5f, 0.0f, 360.0f);
	glm::u8vec4 vertexColor = color;
	for (size_t i = 0; i < inner.size(); ++i) {
		ellipse.m_Vertices.emplace_back(inner[i], vertexColor);
		ellipse.m_Vertices.emplace_back(outer[i], vertexColor);
	}
	for (int i = 0; i + 3 < static_cast<int>(ellipse.m_Vertices.size()); i += 2) {
		ellipse.m_Indices.insert(ellipse.m_Indices.end(), {i, i + 1, i + 2, i + 1, i + 3, i + 2});
	}
	return ellipse;
}

Shape::Shape Shape::Ring(glm::vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, Color color) {
	ZoneScoped;
	Shape ring;
	glm::vec2 pixelCenter = center + glm::vec2(0.5f);
	std::vector<glm::vec2> inner = ArcPoints(pixelCenter, std::max(innerRadius, 0.0f), std::max(innerRadius, 0.0f), startAngle, endAngle);
	std::vector<glm::vec2> outer = ArcPoints(pixelCenter, outerRadius, outerRadius, startAngle, endAngle);
	glm::u8vec4 vertexColor = color;
	for (size_t i = 0; i < inner.size(); ++i) {
		ring.m_Vertices.emplace_back(inner[i], vertexColor);
		ring.m_Vertices.emplace_back(outer[i], vertexColor);
	}
	for (int i = 0; i + 3 < static_cast<int>(ring.m_Vertices.size()); i += 2) {
		ring.m_Indices.insert(ring.m_Indices.end(), {i, i + 1, i + 2, i + 1, i + 3, i + 2});
	}
	return ring;
}

Shape::Shape Shape::RingLines(glm::vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, Color color) {
	ZoneScoped;
	Shape ring;
	std::vector<glm::vec2> inner = ArcPoints(center, innerRadius, innerRadius, startAngle, endAngle);
	std::vector<glm::vec2> outer = ArcPoints(center, outerRadius, outerRadius, startAngle, endAngle);
	glm::u8vec4 vertexColor = color;
	for (size_t i = 1; i < inner.size(); ++i) {
		AppendSegment(ring, inner[i - 1] + glm::vec2(0.5f), inner[i] + glm::vec2(0.5f), 1.0f, vertexColor);
		AppendSegment(ring, outer[i - 1] + glm::vec2(0.5f), outer[i] + glm::vec2(0.5f), 1.0f, vertexColor);
	}
	AppendSegment(ring, inner.front() + glm::vec2(0.5f), outer.front() + glm::vec2(0.5f), 1.0f, vertexColor);
	AppendSegment(ring, inner.back() + glm::vec2(0.5f), outer.back() + glm::vec2(0.5f), 1.0f, vertexColor);
	return ring;
}

Shape::Shape Shape::Rectangle(FloatRect rect, Color color) {
	return Rectangle(rect, FloatRect(0.0f, 0.0f, 1.0f, 1.0f), color);
}

Shape::Shape Shape::Rectangle(FloatRect rect, float angle, Color color) {
	ZoneScoped;
	Shape rectangle;
	glm::vec2 center(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f);
	float cosAngle = std::cos(angle);
	float sinAngle = std::sin(angle);
	auto rotate = [&](glm::vec2 point) {
		glm::vec2 local = point - center;
		return center + glm::vec2(local.x * cosAngle - local.y * sinAngle, local.x * sinAngle + local.y * cosAngle);
	};
	glm::u8vec4 vertexColor = color;
	rectangle.m_Vertices = {
	    Vertex(rotate({rect.x, rect.y}), {0.0f, 0.0f}, vertexColor),
	    Vertex(rotate({rect.x + rect.w, rect.y + rect.h}), {1.0f, 1.0f}, vertexColor),
	    Vertex(rotate({rect.x + rect.w, rect.y}), {1.0f, 0.0f}, vertexColor),
	    Vertex(rotate({rect.x, rect.y + rect.h}), {0.0f, 1.0f}, vertexColor)};
	rectangle.m_Indices = {0, 1, 2, 0, 3, 1};
	return rectangle;
}

Shape::Shape Shape::Rectangle(FloatRect rect, FloatRect uv, Color color) {
	ZoneScoped;
	Shape rectangle;
	rectangle.m_Vertices = {
	    Vertex(glm::vec2(rect.x, rect.y), glm::vec2(uv.x, uv.y), color),
	    Vertex(glm::vec2(rect.x, rect.y) + glm::vec2(rect.w, rect.h), {uv.x + uv.w, uv.y + uv.h}, color),
	    Vertex(glm::vec2(rect.x, rect.y) + glm::vec2(rect.w, 0.0f), {uv.x + uv.w, uv.y}, color),
	    Vertex(glm::vec2(rect.x, rect.y) + glm::vec2(0.0f, rect.h), {uv.x, uv.y + uv.h}, color)};

	rectangle.m_Indices = {
	    0, 1, 2,
	    0, 3, 1};

	return rectangle;
}

Shape::Shape Shape::RectangleLines(FloatRect rect, float thickness, Color color) {
	ZoneScoped;
	Shape rectangle;
	rectangle.m_Vertices = {
	    Vertex(glm::vec2(rect.x, rect.y), {0.0f, 0.0f}, color),
	    Vertex(glm::vec2(rect.x, rect.y) + glm::vec2(rect.w, rect.h), {1.0f, 1.0f}, color),
	    Vertex(glm::vec2(rect.x, rect.y) + glm::vec2(rect.w, 0.0f), {1.0f, 0.0f}, color),
	    Vertex(glm::vec2(rect.x, rect.y) + glm::vec2(0.0f, rect.h), {0.0f, 1.0f}, color),
	    Vertex(glm::vec2(rect.x, rect.y) + glm::vec2(thickness, thickness), {0.0f, 0.0f}, color),
	    Vertex(glm::vec2(rect.x, rect.y) + glm::vec2(rect.w - thickness, rect.h - thickness), {1.0f, 1.0f}, color),
	    Vertex(glm::vec2(rect.x, rect.y) + glm::vec2(rect.w - thickness, thickness), {1.0f, 0.0f}, color),
	    Vertex(glm::vec2(rect.x, rect.y) + glm::vec2(thickness, rect.h - thickness), {0.0f, 1.0f}, color)};

	rectangle.m_Indices = {
	    0, 4, 2,
	    4, 6, 2,
	    2, 6, 1,
	    1, 6, 5,
	    5, 3, 1,
	    5, 7, 3,
	    0, 3, 7,
	    0, 7, 4};

	return rectangle;
}

Shape::Shape Shape::RoundedRectangle(FloatRect rect, float cornerRadius, Color color) {
	ZoneScoped;
	float radius = std::clamp(cornerRadius, 0.0f, std::min(rect.w, rect.h) * 0.5f);
	if (radius <= 0.0f) {
		return Rectangle(rect, color);
	}
	std::vector<glm::vec2> outline;
	auto appendCorner = [&](glm::vec2 cornerCenter, float startAngle) {
		std::vector<glm::vec2> corner = ArcPoints(cornerCenter, radius, radius, startAngle, startAngle + 90.0f);
		outline.insert(outline.end(), corner.begin(), corner.end());
	};
	appendCorner({rect.x + rect.w - radius, rect.y + radius}, -90.0f);
	appendCorner({rect.x + rect.w - radius, rect.y + rect.h - radius}, 0.0f);
	appendCorner({rect.x + radius, rect.y + rect.h - radius}, 90.0f);
	appendCorner({rect.x + radius, rect.y + radius}, 180.0f);
	Shape rounded;
	AppendFan(rounded, {rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f}, outline, color, true);
	return rounded;
}

Shape::Shape Shape::RoundedRectangleLines(FloatRect rect, float cornerRadius, Color color) {
	return RoundedRectangleLines(rect, cornerRadius, 1.0f, color);
}

Shape::Shape Shape::RoundedRectangleLines(FloatRect rect, float cornerRadius, float thickness, Color color) {
	ZoneScoped;
	float radius = std::clamp(cornerRadius, 0.0f, std::min(rect.w, rect.h) * 0.5f);
	std::vector<glm::vec2> outline;
	auto appendCorner = [&](glm::vec2 cornerCenter, float startAngle) {
		std::vector<glm::vec2> corner = ArcPoints(cornerCenter, radius, radius, startAngle, startAngle + 90.0f);
		outline.insert(outline.end(), corner.begin(), corner.end());
	};
	// Inset by half a pixel so the outline lies on the edge pixels of the rectangle.
	FloatRect inset(rect.x + 0.5f, rect.y + 0.5f, rect.w - 1.0f, rect.h - 1.0f);
	appendCorner({inset.x + inset.w - radius, inset.y + radius}, -90.0f);
	appendCorner({inset.x + inset.w - radius, inset.y + inset.h - radius}, 0.0f);
	appendCorner({inset.x + radius, inset.y + inset.h - radius}, 90.0f);
	appendCorner({inset.x + radius, inset.y + radius}, 180.0f);
	outline.push_back(outline.front());
	Shape rounded;
	for (size_t i = 1; i < outline.size(); ++i) {
		AppendSegment(rounded, outline[i - 1], outline[i], thickness, color);
	}
	return rounded;
}

Shape::Shape Shape::Triangle(glm::vec2 point1, glm::vec2 point2, glm::vec2 point3, Color color) {
	ZoneScoped;
	Shape triangle;
	glm::u8vec4 vertexColor = color;
	triangle.m_Vertices = {Vertex(point1 + glm::vec2(0.5f), vertexColor), Vertex(point2 + glm::vec2(0.5f), vertexColor), Vertex(point3 + glm::vec2(0.5f), vertexColor)};
	triangle.m_Indices = {0, 1, 2};
	return triangle;
}

Shape::Shape Shape::TriangleLines(glm::vec2 point1, glm::vec2 point2, glm::vec2 point3, Color color) {
	return LineStrip({point1, point2, point3, point1}, 1.0f, color);
}

Shape::Shape Shape::TriangleStrip(std::vector<glm::vec2> points, Color color) {
	ZoneScoped;
	Shape strip;
	glm::u8vec4 vertexColor = color;
	for (const glm::vec2& point: points) {
		strip.m_Vertices.emplace_back(point + glm::vec2(0.5f), vertexColor);
	}
	for (int i = 2; i < static_cast<int>(points.size()); ++i) {
		strip.m_Indices.insert(strip.m_Indices.end(), {i - 2, i - 1, i});
	}
	return strip;
}

Shape::Shape Shape::Polygon(std::vector<glm::vec2> points, Color color) {
	ZoneScoped;
	Shape polygon;
	if (points.size() < 3) {
		return polygon;
	}
	// Ear clipping, so concave polygons (which Allegro's polygon() supported) fill correctly.
	std::vector<glm::vec2> pixelPoints;
	pixelPoints.reserve(points.size());
	for (const glm::vec2& point: points) {
		pixelPoints.emplace_back(point + glm::vec2(0.5f));
	}
	glm::u8vec4 vertexColor = color;
	for (const glm::vec2& point: pixelPoints) {
		polygon.m_Vertices.emplace_back(point, vertexColor);
	}

	float signedArea = 0.0f;
	for (size_t i = 0; i < pixelPoints.size(); ++i) {
		const glm::vec2& a = pixelPoints[i];
		const glm::vec2& b = pixelPoints[(i + 1) % pixelPoints.size()];
		signedArea += a.x * b.y - b.x * a.y;
	}
	float winding = signedArea >= 0.0f ? 1.0f : -1.0f;

	auto cross = [](glm::vec2 a, glm::vec2 b, glm::vec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
	auto pointInTriangle = [&](glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
		float d1 = cross(a, b, p) * winding;
		float d2 = cross(b, c, p) * winding;
		float d3 = cross(c, a, p) * winding;
		return d1 > 0.0f && d2 > 0.0f && d3 > 0.0f;
	};

	std::vector<int> remaining(pixelPoints.size());
	for (int i = 0; i < static_cast<int>(remaining.size()); ++i) {
		remaining[i] = i;
	}
	int guard = static_cast<int>(remaining.size()) * static_cast<int>(remaining.size());
	while (remaining.size() > 3 && guard-- > 0) {
		bool clipped = false;
		for (size_t i = 0; i < remaining.size(); ++i) {
			int prev = remaining[(i + remaining.size() - 1) % remaining.size()];
			int curr = remaining[i];
			int next = remaining[(i + 1) % remaining.size()];
			if (cross(pixelPoints[prev], pixelPoints[curr], pixelPoints[next]) * winding <= 0.0f) {
				continue;
			}
			bool containsOther = false;
			for (int other: remaining) {
				if (other != prev && other != curr && other != next && pointInTriangle(pixelPoints[other], pixelPoints[prev], pixelPoints[curr], pixelPoints[next])) {
					containsOther = true;
					break;
				}
			}
			if (containsOther) {
				continue;
			}
			polygon.m_Indices.insert(polygon.m_Indices.end(), {prev, curr, next});
			remaining.erase(remaining.begin() + i);
			clipped = true;
			break;
		}
		if (!clipped) {
			// Degenerate or self-intersecting polygon, fall back to a fan for whatever is left.
			for (size_t i = 1; i + 1 < remaining.size(); ++i) {
				polygon.m_Indices.insert(polygon.m_Indices.end(), {remaining[0], remaining[i], remaining[i + 1]});
			}
			remaining.clear();
		}
	}
	if (remaining.size() == 3) {
		polygon.m_Indices.insert(polygon.m_Indices.end(), {remaining[0], remaining[1], remaining[2]});
	}
	return polygon;
}

Shape::Shape Shape::PolygonLines(const std::vector<glm::vec2>& points, Color color) {
	if (points.size() < 2) {
		return {};
	}
	std::vector<glm::vec2> closed = points;
	closed.push_back(points.front());
	return LineStrip(closed, 1.0f, color);
}

Shape::Shape Shape::Lines::VectorArrow(glm::vec2 pos, glm::vec2 vector, Color color) {
	ZoneScoped;
	Shape arrow;
	arrow.m_Vertices = {
	    Vertex(pos, color),
	    Vertex(pos + vector, color),
	    Vertex(pos + vector - (std::sqrt(2.0f) / 10.0f * glm::vec2(vector.x - vector.y, vector.x + vector.y)), color),
	    Vertex(pos + vector - (std::sqrt(2.0f) / 10.0f * glm::vec2(vector.x + vector.y, -vector.x + vector.y)), color)};

	arrow.m_Indices = {0, 1, 1, 2, 1, 3};

	return arrow;
}

Shape::Shape Shape::Lines::Rectangle(const FloatRect& rect, Color color) {
	Shape rectangle;
	rectangle.m_Vertices = {
	    Vertex(glm::vec2(rect.x, rect.y), color),
	    Vertex(glm::vec2(rect.x + rect.w, rect.y), color),
	    Vertex(glm::vec2(rect.x + rect.w, rect.y + rect.h), color),
	    Vertex(glm::vec2(rect.x, rect.y + rect.h), color),
	};

	rectangle.m_Indices = {0, 1, 2, 3};
	return rectangle;
}

Shape::Shape Shape::Lines::Line(const glm::vec2& start, const glm::vec2& end, Color color) {
	Shape line;
	line.m_Vertices = {
	    Vertex(start, color),
	    Vertex(end, color)};
	line.m_Indices = {0, 1};
	return line;
}

std::shared_ptr<DrawCall> Draw::Pixel(glm::vec2 position, Color color) {
	return Submit(Shape::Pixel(position, color));
}

std::shared_ptr<DrawCall> Draw::Line(glm::vec2 start, glm::vec2 end, Color color) {
	return Submit(Shape::Line(start, end, color));
}

std::shared_ptr<DrawCall> Draw::Line(glm::vec2 start, glm::vec2 end, float thickness, Color color) {
	return Submit(Shape::Line(start, end, thickness, color));
}

std::shared_ptr<DrawCall> Draw::LineStrip(const std::vector<glm::vec2>& points, Color color) {
	return Submit(Shape::LineStrip(points, color));
}

std::shared_ptr<DrawCall> Draw::LineStrip(const std::vector<glm::vec2>& points, float thickness, Color color) {
	return Submit(Shape::LineStrip(points, thickness, color));
}

std::shared_ptr<DrawCall> Draw::LineBezier(glm::vec2 start, glm::vec2 end, Color color) {
	return Submit(Shape::LineBezier(start, end, color));
}

std::shared_ptr<DrawCall> Draw::LineSpline(const std::array<glm::vec2, 4>& controlPoints, Color color) {
	return Submit(Shape::LineSpline(controlPoints, color));
}

std::shared_ptr<DrawCall> Draw::Circle(glm::vec2 center, float radius, Color color) {
	return Submit(Shape::Circle(center, radius, color));
}

std::shared_ptr<DrawCall> Draw::CircleSector(glm::vec2 center, float radius, float startAngle, float endAngle, Color color) {
	return Submit(Shape::CircleSector(center, radius, startAngle, endAngle, color));
}

std::shared_ptr<DrawCall> Draw::CircleLines(glm::vec2 center, float radius, Color color) {
	return Submit(Shape::CircleLines(center, radius, color));
}

std::shared_ptr<DrawCall> Draw::CircleLinesSector(glm::vec2 center, float radius, float startAngle, float endAngle, Color color) {
	return Submit(Shape::CircleLinesSector(center, radius, startAngle, endAngle, color));
}

std::shared_ptr<DrawCall> Draw::Ellipse(glm::vec2 center, float radiusH, float radiusV, Color color) {
	return Submit(Shape::Ellipse(center, radiusH, radiusV, color));
}

std::shared_ptr<DrawCall> Draw::EllipseLines(glm::vec2 center, float radiusH, float radiusV, Color color) {
	return Submit(Shape::EllipseLines(center, radiusH, radiusV, color));
}

std::shared_ptr<DrawCall> Draw::Ring(glm::vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, Color color) {
	return Submit(Shape::Ring(center, innerRadius, outerRadius, startAngle, endAngle, color));
}

std::shared_ptr<DrawCall> Draw::RingLines(glm::vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, Color color) {
	return Submit(Shape::RingLines(center, innerRadius, outerRadius, startAngle, endAngle, color));
}

std::shared_ptr<DrawCall> Draw::Rectangle(FloatRect rect, Color color) {
	return Submit(Shape::Rectangle(rect, color));
}

std::shared_ptr<DrawCall> Draw::RectangleLines(const FloatRect& rect, float thickness, Color color) {
	return Submit(Shape::RectangleLines(rect, thickness, color));
}

std::shared_ptr<DrawCall> Draw::RoundedRectangle(FloatRect rect, float cornerRadius, Color color) {
	return Submit(Shape::RoundedRectangle(rect, cornerRadius, color));
}

std::shared_ptr<DrawCall> Draw::RoundedRectangleLines(FloatRect rect, float cornerRadius, Color color) {
	return Submit(Shape::RoundedRectangleLines(rect, cornerRadius, color));
}

std::shared_ptr<DrawCall> Draw::RoundedRectangleLines(FloatRect rect, float cornerRadius, float thickness, Color color) {
	return Submit(Shape::RoundedRectangleLines(rect, cornerRadius, thickness, color));
}

std::shared_ptr<DrawCall> Draw::Triangle(glm::vec2 point1, glm::vec2 point2, glm::vec2 point3, Color color) {
	return Submit(Shape::Triangle(point1, point2, point3, color));
}

std::shared_ptr<DrawCall> Draw::TriangleLines(glm::vec2 point1, glm::vec2 point2, glm::vec2 point3, Color color) {
	return Submit(Shape::TriangleLines(point1, point2, point3, color));
}

std::shared_ptr<DrawCall> Draw::TriangleStrip(std::vector<glm::vec2> points, Color color) {
	return Submit(Shape::TriangleStrip(std::move(points), color));
}

std::shared_ptr<DrawCall> Draw::Polygon(std::vector<glm::vec2> points, Color color) {
	return Submit(Shape::Polygon(std::move(points), color));
}

std::shared_ptr<DrawCall> Draw::PolygonLines(const std::vector<glm::vec2>& points, Color color) {
	return Submit(Shape::PolygonLines(points, color));
}

std::shared_ptr<DrawCall> Draw::Lines::VectorArrow(glm::vec2 pos, glm::vec2 vector, Color color) {
	return Submit(Shape::Lines::VectorArrow(pos, vector, color), GL_LINES);
}

std::shared_ptr<DrawCall> Draw::Lines::Rectangle(const FloatRect& rect, Color color) {
	return Submit(Shape::Lines::Rectangle(rect, color), GL_LINE_LOOP);
}

std::shared_ptr<DrawCall> Draw::Lines::Line(const glm::vec2& start, const glm::vec2& end, Color color) {
	return Submit(Shape::Lines::Line(start, end, color), GL_LINES);
}
