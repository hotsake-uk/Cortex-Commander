function PrimitiveGalleryScript:StartScript()
	self.icon = CreateHDFirearm("SMG", "Base.rte");
	self.angle = 0;
end

function PrimitiveGalleryScript:UpdateScript()
	local o = CameraMan:GetOffset(0) + Vector(30, 60);
	local red, yellow, white, blue, green = 13, 117, 254, 166, 147;
	self.angle = self.angle + 0.02;

	PrimitiveMan:DrawLinePrimitive(o, o + Vector(40, 20), red);
	PrimitiveMan:DrawLinePrimitive(o + Vector(0, 30), o + Vector(40, 30), yellow, 3);
	PrimitiveMan:DrawArcPrimitive(o + Vector(70, 20), 0, 270, 15, white);
	PrimitiveMan:DrawArcPrimitive(o + Vector(110, 20), 45, 315, 15, blue, 3);
	PrimitiveMan:DrawSplinePrimitive(o + Vector(140, 35), o + Vector(150, 0), o + Vector(170, 40), o + Vector(180, 5), green);

	PrimitiveMan:DrawBoxPrimitive(o + Vector(0, 50), o + Vector(30, 70), white);
	PrimitiveMan:DrawBoxFillPrimitive(o + Vector(40, 50), o + Vector(70, 70), red);
	PrimitiveMan:DrawRoundedBoxPrimitive(o + Vector(80, 50), o + Vector(120, 70), 6, yellow);
	PrimitiveMan:DrawRoundedBoxFillPrimitive(o + Vector(130, 50), o + Vector(170, 70), 6, blue);

	PrimitiveMan:DrawCirclePrimitive(o + Vector(15, 95), 12, green);
	PrimitiveMan:DrawCircleFillPrimitive(o + Vector(50, 95), 12, white);
	PrimitiveMan:DrawEllipsePrimitive(o + Vector(90, 95), 18, 9, red);
	PrimitiveMan:DrawEllipseFillPrimitive(o + Vector(135, 95), 18, 9, yellow);

	PrimitiveMan:DrawTrianglePrimitive(o + Vector(0, 135), o + Vector(15, 112), o + Vector(30, 135), blue);
	PrimitiveMan:DrawTriangleFillPrimitive(o + Vector(40, 135), o + Vector(55, 112), o + Vector(70, 135), green);
	PrimitiveMan:DrawPolygonPrimitive(o + Vector(95, 125), white, {Vector(-15, -10), Vector(0, -15), Vector(15, -10), Vector(5, 0), Vector(15, 10), Vector(-15, 10)});
	PrimitiveMan:DrawPolygonFillPrimitive(o + Vector(140, 125), red, {Vector(-15, -10), Vector(0, -15), Vector(15, -10), Vector(5, 0), Vector(15, 10), Vector(-15, 10)});

	PrimitiveMan:DrawTextPrimitive(o + Vector(0, 150), "Small text, left", true, 0);
	PrimitiveMan:DrawTextPrimitive(o + Vector(180, 150), "Large text, right", false, 2);
	PrimitiveMan:DrawTextPrimitive(o + Vector(90, 175), "Rotating", false, 1, self.angle);
	if self.icon then
		PrimitiveMan:DrawIconPrimitive(o + Vector(210, 20), self.icon);
		PrimitiveMan:DrawBitmapPrimitive(o + Vector(210, 60), self.icon, self.angle, 0);
	end
end
