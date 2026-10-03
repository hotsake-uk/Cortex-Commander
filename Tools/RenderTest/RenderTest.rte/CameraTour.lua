function CameraTourScript:StartScript()
	self.timer = Timer();
	-- Points as fractions of the scene size.
	self.points = {Vector(0.18, 0.62), Vector(0.38, 0.70), Vector(0.55, 0.55), Vector(0.74, 0.66), Vector(0.50, 0.30), Vector(0.90, 0.60)};
	self.holdMS = 3000;
	-- CCCP_CAMERA_POI pins the camera to one point, for comparing builds.
	self.fixedPoint = nil;
	if os and os.getenv then
		self.fixedPoint = tonumber(os.getenv("CCCP_CAMERA_POI") or "");
	end
end

function CameraTourScript:UpdateScript()
	local index = self.fixedPoint or (math.floor(self.timer.ElapsedRealTimeMS / self.holdMS) % #self.points + 1);
	local point = self.points[index];
	CameraMan:SetScrollTarget(Vector(SceneMan.SceneWidth * point.X, SceneMan.SceneHeight * point.Y), 1.0, 0);
end
