function BackgroundTestScript:StartScript()
	self.timer = Timer();
	-- The camera pans steadily across the whole scene (and over its seam) while the zoom steps through these; a background layer that
	-- jumps, tears or mirrors shows up in the burst of captures. CCCP_BACKGROUND_ZOOM pins the zoom.
	self.zooms = { 1.0, 0.5, 0.75, 1.5, 2.0 };
	self.zoomHoldMS = 4000;
	self.panSpeed = 420; -- px/s
	self.fixedZoom = nil;
	if os and os.getenv then
		self.fixedZoom = tonumber(os.getenv("CCCP_BACKGROUND_ZOOM") or "");
	end
end

function BackgroundTestScript:UpdateScript()
	local t = self.timer.ElapsedRealTimeMS;
	local zoom = self.fixedZoom or self.zooms[math.floor(t / self.zoomHoldMS) % #self.zooms + 1];
	if FrameMan.CameraZoom ~= zoom then
		FrameMan.CameraZoom = zoom;
	end
	-- Start near the seam and keep going right; the scene wraps, so this crosses the seam every SceneWidth / panSpeed seconds.
	local x = (SceneMan.SceneWidth - 600 + t * 0.001 * self.panSpeed) % SceneMan.SceneWidth;
	local y = SceneMan.SceneHeight * 0.45 + 120 * math.sin(t * 0.0006);
	CameraMan:SetScrollTarget(Vector(x, y), 1.0, 0);
	if not self.reportTimer then
		self.reportTimer = Timer();
	end
	if self.reportTimer:IsPastRealMS(1000) then
		self.reportTimer:Reset();
		ConsoleMan:PrintString("BGTEST " .. math.floor(t / 1000) .. "s zoom " .. zoom .. " camera " .. math.floor(x) .. "," .. math.floor(y));
	end
end
