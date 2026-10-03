#include "GraphicalPrimitive.h"
#include "Matrix.h"
#include "FrameMan.h"
#include "SceneMan.h"
#include "GLStateMan.h"
#include "SLTerrain.h"

#include "GUI.h"
#include "AllegroBitmap.h"

#include "Draw.h"
#include "Shapes.h"

#include <array>
#include <cmath>

using namespace RTE;

const GraphicalPrimitive::PrimitiveType GraphicalPrimitive::c_PrimitiveType = PrimitiveType::None;
const GraphicalPrimitive::PrimitiveType LinePrimitive::c_PrimitiveType = PrimitiveType::Line;
const GraphicalPrimitive::PrimitiveType ArcPrimitive::c_PrimitiveType = PrimitiveType::Arc;
const GraphicalPrimitive::PrimitiveType SplinePrimitive::c_PrimitiveType = PrimitiveType::Spline;
const GraphicalPrimitive::PrimitiveType BoxPrimitive::c_PrimitiveType = PrimitiveType::Box;
const GraphicalPrimitive::PrimitiveType BoxFillPrimitive::c_PrimitiveType = PrimitiveType::BoxFill;
const GraphicalPrimitive::PrimitiveType RoundedBoxPrimitive::c_PrimitiveType = PrimitiveType::RoundedBox;
const GraphicalPrimitive::PrimitiveType RoundedBoxFillPrimitive::c_PrimitiveType = PrimitiveType::RoundedBoxFill;
const GraphicalPrimitive::PrimitiveType CirclePrimitive::c_PrimitiveType = PrimitiveType::Circle;
const GraphicalPrimitive::PrimitiveType CircleFillPrimitive::c_PrimitiveType = PrimitiveType::CircleFill;
const GraphicalPrimitive::PrimitiveType EllipsePrimitive::c_PrimitiveType = PrimitiveType::Ellipse;
const GraphicalPrimitive::PrimitiveType EllipseFillPrimitive::c_PrimitiveType = PrimitiveType::EllipseFill;
const GraphicalPrimitive::PrimitiveType TrianglePrimitive::c_PrimitiveType = PrimitiveType::Triangle;
const GraphicalPrimitive::PrimitiveType TriangleFillPrimitive::c_PrimitiveType = PrimitiveType::TriangleFill;
const GraphicalPrimitive::PrimitiveType PolygonPrimitive::c_PrimitiveType = PrimitiveType::Polygon;
const GraphicalPrimitive::PrimitiveType PolygonFillPrimitive::c_PrimitiveType = PrimitiveType::PolygonFill;
const GraphicalPrimitive::PrimitiveType TextPrimitive::c_PrimitiveType = PrimitiveType::Text;
const GraphicalPrimitive::PrimitiveType BitmapPrimitive::c_PrimitiveType = PrimitiveType::Bitmap;

namespace {
	glm::vec2 ToVec2(const Vector& vector) { return glm::vec2(vector.m_X, vector.m_Y); }
	glm::vec2 ToFloorVec2(const Vector& vector) { return glm::vec2(vector.GetFloorIntX(), vector.GetFloorIntY()); }

	template <typename TrianglePrimitiveType>
	void TriangleCullCircle(const TrianglePrimitiveType& triangle, Vector& center, float& radius) {
		center = (triangle.m_PointAPos + triangle.m_PointBPos + triangle.m_PointCPos) / 3.0f;
		radius = std::sqrt(std::max({(triangle.m_PointAPos - center).GetSqrMagnitude(), (triangle.m_PointBPos - center).GetSqrMagnitude(), (triangle.m_PointCPos - center).GetSqrMagnitude()}));
	}

	float PolygonCullRadius(const std::vector<Vector*>& vertices) {
		float radius = 0.0f;
		for (const Vector* vertex: vertices) {
			radius = std::max(radius, vertex->GetMagnitude());
		}
		return radius;
	}
} // namespace

Color GraphicalPrimitive::GetDrawColor() const {
	Color paletteColor(static_cast<int>(m_Color));
	float red = static_cast<float>(paletteColor.GetR());
	float green = static_cast<float>(paletteColor.GetG());
	float blue = static_cast<float>(paletteColor.GetB());
	float alpha = 255.0f;

	// Text and bitmaps carry their own colors, so only tint them for blending.
	if (GetPrimitiveType() == PrimitiveType::Text || GetPrimitiveType() == PrimitiveType::Bitmap) {
		red = green = blue = 255.0f;
	}

	auto amount = [this](int channel) { return std::clamp(static_cast<float>(m_ColorChannelBlendAmounts[channel]), static_cast<float>(BlendAmountLimits::MinBlend), static_cast<float>(BlendAmountLimits::MaxBlend)) / static_cast<float>(BlendAmountLimits::MaxBlend); };

	switch (m_BlendMode) {
		case DrawBlendMode::NoBlend:
			break;
		case DrawBlendMode::BlendTransparency:
			// Amounts are how transparent each channel is, 0 being opaque.
			alpha = 255.0f * (1.0f - (amount(0) + amount(1) + amount(2)) / 3.0f);
			break;
		case DrawBlendMode::BlendMultiply:
			// Amounts are the strength of the effect, so fade towards white (no change) at 0.
			red = 255.0f + (red - 255.0f) * amount(0);
			green = 255.0f + (green - 255.0f) * amount(1);
			blue = 255.0f + (blue - 255.0f) * amount(2);
			break;
		case DrawBlendMode::BlendInvert:
			red = green = blue = 255.0f * amount(3);
			break;
		case DrawBlendMode::BlendDissolve:
			alpha = 255.0f * (1.0f - amount(3));
			break;
		default:
			// Additive-style modes fade towards black (no change) at 0.
			red *= amount(0);
			green *= amount(1);
			blue *= amount(2);
			break;
	}
	return Color(static_cast<int>(red), static_cast<int>(green), static_cast<int>(blue), static_cast<int>(alpha));
}

void GraphicalPrimitive::GetCullCircle(Vector& center, float& radius) const {
	center = m_StartPos;
	radius = std::sqrt(m_DrawRadiusSquared);
	switch (GetPrimitiveType()) {
		case PrimitiveType::Circle:
			radius = static_cast<float>(static_cast<const CirclePrimitive*>(this)->m_Radius);
			break;
		case PrimitiveType::CircleFill:
			radius = static_cast<float>(static_cast<const CircleFillPrimitive*>(this)->m_Radius);
			break;
		case PrimitiveType::Ellipse:
			radius = static_cast<float>(std::max(static_cast<const EllipsePrimitive*>(this)->m_HorizRadius, static_cast<const EllipsePrimitive*>(this)->m_VertRadius));
			break;
		case PrimitiveType::EllipseFill:
			radius = static_cast<float>(std::max(static_cast<const EllipseFillPrimitive*>(this)->m_HorizRadius, static_cast<const EllipseFillPrimitive*>(this)->m_VertRadius));
			break;
		case PrimitiveType::Triangle:
			TriangleCullCircle(*static_cast<const TrianglePrimitive*>(this), center, radius);
			break;
		case PrimitiveType::TriangleFill:
			TriangleCullCircle(*static_cast<const TriangleFillPrimitive*>(this), center, radius);
			break;
		case PrimitiveType::Polygon:
			radius = PolygonCullRadius(static_cast<const PolygonPrimitive*>(this)->m_Vertices);
			break;
		case PrimitiveType::PolygonFill:
			radius = PolygonCullRadius(static_cast<const PolygonFillPrimitive*>(this)->m_Vertices);
			break;
		case PrimitiveType::Text: {
			const TextPrimitive* text = static_cast<const TextPrimitive*>(this);
			radius = text->m_TextBitmap ? static_cast<float>(text->m_TextBitmap->w + text->m_TextBitmap->h) : 0.0f;
			break;
		}
		case PrimitiveType::Bitmap: {
			const BitmapPrimitive* bitmap = static_cast<const BitmapPrimitive*>(this);
			radius = bitmap->m_Bitmap ? static_cast<float>(bitmap->m_Bitmap->w + bitmap->m_Bitmap->h) * std::abs(bitmap->m_Scale) : 0.0f;
			break;
		}
		case PrimitiveType::Box:
		case PrimitiveType::BoxFill:
		case PrimitiveType::RoundedBox:
		case PrimitiveType::RoundedBoxFill:
			center = (m_StartPos + m_EndPos) / 2.0f;
			radius = (m_EndPos - m_StartPos).GetMagnitude() / 2.0f;
			break;
		default:
			break;
	}
	// Pad for line thickness and pixel rounding.
	radius += 4.0f;
}

void LinePrimitive::Draw() {
	RTE::Draw::Line(ToFloorVec2(m_StartPos), ToFloorVec2(m_EndPos), std::max(m_Thickness, 1.0f), GetDrawColor());
}

void ArcPrimitive::Draw() {
	float thickness = static_cast<float>(std::max(m_Thickness, 1));
	RTE::Draw::Ring(ToFloorVec2(m_StartPos), static_cast<float>(m_Radius) - thickness / 2.0f, static_cast<float>(m_Radius) + thickness / 2.0f, m_StartAngle, m_EndAngle, GetDrawColor());
}

void SplinePrimitive::Draw() {
	RTE::Draw::LineSpline({ToFloorVec2(m_StartPos), ToFloorVec2(m_GuidePointAPos), ToFloorVec2(m_GuidePointBPos), ToFloorVec2(m_EndPos)}, GetDrawColor());
}

namespace {
	/// Normalized integer rectangle covering both corners inclusively, like Allegro's rect/rectfill.
	FloatRect InclusiveRect(const Vector& cornerA, const Vector& cornerB) {
		float left = static_cast<float>(std::min(cornerA.GetFloorIntX(), cornerB.GetFloorIntX()));
		float top = static_cast<float>(std::min(cornerA.GetFloorIntY(), cornerB.GetFloorIntY()));
		float right = static_cast<float>(std::max(cornerA.GetFloorIntX(), cornerB.GetFloorIntX()));
		float bottom = static_cast<float>(std::max(cornerA.GetFloorIntY(), cornerB.GetFloorIntY()));
		return FloatRect(left, top, right - left + 1.0f, bottom - top + 1.0f);
	}
} // namespace

void BoxPrimitive::Draw() {
	RTE::Draw::RectangleLines(InclusiveRect(m_StartPos, m_EndPos), 1.0f, GetDrawColor());
}

void BoxFillPrimitive::Draw() {
	RTE::Draw::Rectangle(InclusiveRect(m_StartPos, m_EndPos), GetDrawColor());
}

void RoundedBoxPrimitive::Draw() {
	RTE::Draw::RoundedRectangleLines(InclusiveRect(m_StartPos, m_EndPos), static_cast<float>(m_CornerRadius), 1.0f, GetDrawColor());
}

void RoundedBoxFillPrimitive::Draw() {
	RTE::Draw::RoundedRectangle(InclusiveRect(m_StartPos, m_EndPos), static_cast<float>(m_CornerRadius), GetDrawColor());
}

void CirclePrimitive::Draw() {
	RTE::Draw::CircleLines(ToFloorVec2(m_StartPos), static_cast<float>(m_Radius), GetDrawColor());
}

void CircleFillPrimitive::Draw() {
	RTE::Draw::Circle(ToFloorVec2(m_StartPos), static_cast<float>(m_Radius), GetDrawColor());
}

void EllipsePrimitive::Draw() {
	RTE::Draw::EllipseLines(ToFloorVec2(m_StartPos), static_cast<float>(m_HorizRadius), static_cast<float>(m_VertRadius), GetDrawColor());
}

void EllipseFillPrimitive::Draw() {
	RTE::Draw::Ellipse(ToFloorVec2(m_StartPos), static_cast<float>(m_HorizRadius), static_cast<float>(m_VertRadius), GetDrawColor());
}

void TrianglePrimitive::Draw() {
	RTE::Draw::TriangleLines(ToFloorVec2(m_PointAPos), ToFloorVec2(m_PointBPos), ToFloorVec2(m_PointCPos), GetDrawColor());
}

void TriangleFillPrimitive::Draw() {
	RTE::Draw::Triangle(ToFloorVec2(m_PointAPos), ToFloorVec2(m_PointBPos), ToFloorVec2(m_PointCPos), GetDrawColor());
}

void PolygonPrimitive::Draw() {
	std::vector<glm::vec2> points;
	points.reserve(m_Vertices.size());
	for (const Vector* vertex: m_Vertices) {
		points.emplace_back(ToFloorVec2(m_StartPos + *vertex));
	}
	RTE::Draw::PolygonLines(points, GetDrawColor());
}

void PolygonFillPrimitive::Draw() {
	std::vector<glm::vec2> points;
	points.reserve(m_Vertices.size());
	for (const Vector* vertex: m_Vertices) {
		points.emplace_back(ToFloorVec2(m_StartPos + *vertex));
	}
	RTE::Draw::Polygon(std::move(points), GetDrawColor());
}

void TextPrimitive::CreateTextBitmap() {
	if(m_Text.empty()) {
		return;
	}
	GUIFont* font = m_IsSmall ? g_FrameMan.GetSmallFont() : g_FrameMan.GetLargeFont();
	Matrix rotation = Matrix(m_RotAngle);
	Vector targetPosAdjustment = Vector();

	int textWidth = font->CalculateWidth(m_Text);
	int textHeight = font->CalculateHeight(m_Text);

	drawing_mode(DRAW_MODE_SOLID, nullptr, 0, 0);
	
	m_TextBitmap = create_bitmap_ex(8, textWidth * 2, textHeight);
	clear_to_color(m_TextBitmap, ColorKeys::g_MaskColor);
	AllegroBitmap tempDrawAllegroBitmap(m_TextBitmap);
	font->DrawAligned(&tempDrawAllegroBitmap, textWidth, 0, m_Text, m_Alignment);

	m_TargetPosAlignment = Vector(static_cast<float>(textWidth), 0);
}

void TextPrimitive::Draw() {
	if (!m_TextBitmap) {
		return;
	}
	// Rotate around the anchor point. CC angles are counter-clockwise, screen space rotation is clockwise.
	RTE::Draw::DrawBitmap(m_TextBitmap, ToFloorVec2(m_StartPos), -ToVec2(m_TargetPosAlignment), -m_RotAngle, glm::vec2(1.0f), GetDrawColor());
}

TextPrimitive::~TextPrimitive() {
	if (m_TextBitmap) {
		g_GLStateMan.DestroyBitmapInfo(m_TextBitmap);
		destroy_bitmap(m_TextBitmap);
	}
}

void BitmapPrimitive::Draw() {
	if (!m_Bitmap) {
		return;
	}
	glm::vec2 scale(m_HFlipped ? -m_Scale : m_Scale, m_VFlipped ? -m_Scale : m_Scale);
	RTE::Draw::DrawBitmap(m_Bitmap, ToFloorVec2(m_StartPos), glm::vec2(-m_Bitmap->w / 2.0f, -m_Bitmap->h / 2.0f), -m_RotAngle, scale, GetDrawColor());
}
