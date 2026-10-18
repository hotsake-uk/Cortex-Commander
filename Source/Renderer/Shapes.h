#pragma once
#include "Color.h"
#include "glm/fwd.hpp"
#include <array>
#include <vector>
#include <memory>
#include "Vertex.h"
#include "Rectangles.h"

namespace RTE {
	class DrawCall;
	/// Shapes are in scene/screen pixel space: a pixel at (x, y) covers [x, x+1). Angles for arcs/sectors/rings are in degrees.
	namespace Shape {
		struct Shape {
			std::vector<Vertex> m_Vertices{};
			std::vector<int> m_Indices{};
		};
		Shape Pixel(glm::vec2 position, Color color);
		/// Many single pixels in one shape.
		Shape Pixels(const std::vector<std::pair<int, int>>& positions, Color color);
		Shape Line(glm::vec2 start, glm::vec2 end, Color color);
		Shape Line(glm::vec2 start, glm::vec2 end, float thickness, Color color);
		Shape LineStrip(const std::vector<glm::vec2>& points, Color color);
		Shape LineStrip(const std::vector<glm::vec2>& points, float thickness, Color color);
		Shape LineBezier(glm::vec2 start, glm::vec2 end, Color color);
		/// Cubic bezier through 4 control points (start, guide A, guide B, end).
		Shape LineSpline(const std::array<glm::vec2, 4>& controlPoints, Color color);
		Shape Circle(glm::vec2 center, float radius, Color color);
		Shape CircleSector(glm::vec2 center, float radius, float startAngle, float endAngle, Color color);
		Shape CircleLines(glm::vec2 center, float radius, Color color);
		Shape CircleLinesSector(glm::vec2 center, float radius, float startAngle, float endAngle, Color color);
		Shape Ellipse(glm::vec2 center, float radiusH, float radiusV, Color color);
		Shape EllipseLines(glm::vec2 center, float radiusH, float radiusV, Color color);
		Shape Ring(glm::vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, Color color);
		Shape RingLines(glm::vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, Color color);
		Shape Rectangle(FloatRect rect, Color color);
		Shape Rectangle(FloatRect rect, float angle, Color color);
		Shape Rectangle(FloatRect rect, FloatRect uv, Color color);
		Shape RectangleLines(FloatRect rect, float thickness, Color color);
		Shape RoundedRectangle(FloatRect rect, float cornerRadius, Color color);
		Shape RoundedRectangleLines(FloatRect rect, float cornerRadius, Color color);
		Shape RoundedRectangleLines(FloatRect rect, float cornerRadius, float thickness, Color color);
		Shape Triangle(glm::vec2 point1, glm::vec2 point2, glm::vec2 point3, Color color);
		Shape TriangleLines(glm::vec2 point1, glm::vec2 point2, glm::vec2 point3, Color color);
		Shape TriangleStrip(std::vector<glm::vec2> points, Color color);
		Shape Polygon(std::vector<glm::vec2> points, Color color);
		Shape PolygonLines(const std::vector<glm::vec2>& points, Color color);
		/// Lines mode shapes. Don't use for primitives.
		namespace Lines {
			Shape VectorArrow(glm::vec2 pos, glm::vec2 vector, Color color);
			Shape Rectangle(const FloatRect& rect, Color color);
			Shape Line(const glm::vec2& start, const glm::vec2& end, Color color);
		}
	} // namespace Shape
	namespace Draw {
		std::shared_ptr<DrawCall> Pixel(glm::vec2 position, Color color);
		/// Draws a single pixel like Pixel(), but pixels drawn one after another with nothing else in between share a draw call. For particles: thousands of them cost a few draw calls instead of thousands.
		/// Each pixel still gets its own place in the draw order. Nothing is returned, because the draw call may be shared.
		void PixelBatched(glm::vec2 position, Color color);
		/// Draws several single pixels in one color, joining the same shared draw call as PixelBatched. For particle trails.
		void PixelsBatched(const std::vector<std::pair<int, int>>& positions, Color color);
		std::shared_ptr<DrawCall> Pixels(const std::vector<std::pair<int, int>>& positions, Color color);
		std::shared_ptr<DrawCall> Line(glm::vec2 start, glm::vec2 end, Color color);
		std::shared_ptr<DrawCall> Line(glm::vec2 start, glm::vec2 end, float thickness, Color color);
		std::shared_ptr<DrawCall> LineStrip(const std::vector<glm::vec2>& points, Color color);
		std::shared_ptr<DrawCall> LineStrip(const std::vector<glm::vec2>& points, float thickness, Color color);
		std::shared_ptr<DrawCall> LineBezier(glm::vec2 start, glm::vec2 end, Color color);
		std::shared_ptr<DrawCall> LineSpline(const std::array<glm::vec2, 4>& controlPoints, Color color);
		std::shared_ptr<DrawCall> Circle(glm::vec2 center, float radius, Color color);
		std::shared_ptr<DrawCall> CircleSector(glm::vec2 center, float radius, float startAngle, float endAngle, Color color);
		std::shared_ptr<DrawCall> CircleLines(glm::vec2 center, float radius, Color color);
		std::shared_ptr<DrawCall> CircleLinesSector(glm::vec2 center, float radius, float startAngle, float endAngle, Color color);
		std::shared_ptr<DrawCall> Ellipse(glm::vec2 center, float radiusH, float radiusV, Color color);
		std::shared_ptr<DrawCall> EllipseLines(glm::vec2 center, float radiusH, float radiusV, Color color);
		std::shared_ptr<DrawCall> Ring(glm::vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, Color color);
		std::shared_ptr<DrawCall> RingLines(glm::vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, Color color);
		std::shared_ptr<DrawCall> Rectangle(FloatRect rect, Color color);
		std::shared_ptr<DrawCall> RectangleLines(const FloatRect& rect, float thickness, Color color);
		std::shared_ptr<DrawCall> RoundedRectangle(FloatRect rect, float cornerRadius, Color color);
		std::shared_ptr<DrawCall> RoundedRectangleLines(FloatRect rect, float cornerRadius, Color color);
		std::shared_ptr<DrawCall> RoundedRectangleLines(FloatRect rect, float cornerRadius, float thickness, Color color);
		std::shared_ptr<DrawCall> Triangle(glm::vec2 point1, glm::vec2 point2, glm::vec2 point3, Color color);
		std::shared_ptr<DrawCall> TriangleLines(glm::vec2 point1, glm::vec2 point2, glm::vec2 point3, Color color);
		std::shared_ptr<DrawCall> TriangleStrip(std::vector<glm::vec2> points, Color color);
		std::shared_ptr<DrawCall> Polygon(std::vector<glm::vec2> points, Color color);
		std::shared_ptr<DrawCall> PolygonLines(const std::vector<glm::vec2>& points, Color color);
		namespace Lines {
			std::shared_ptr<DrawCall> VectorArrow(glm::vec2 pos, glm::vec2 vector, Color color);
			std::shared_ptr<DrawCall> Rectangle(const FloatRect& rect, Color color);
			std::shared_ptr<DrawCall> Line(const glm::vec2& start, const glm::vec2& end, Color color);
		}
	} // namespace Draw
} // namespace RTE
