function ZoomTestScript:StartScript()
	self.timer = Timer();
end

function ZoomTestScript:UpdateScript()
	-- Cycles the camera zoom: normal, out, far out, in, normal.
	local t = self.timer.ElapsedRealTimeMS;
	local zoom = 1;
	if t > 20000 then
		zoom = 1;
	elseif t > 16000 then
		zoom = 2;
	elseif t > 12000 then
		zoom = 0.4;
	elseif t > 8000 then
		zoom = 0.6;
	end
	if FrameMan.CameraZoom ~= zoom then
		FrameMan.CameraZoom = zoom;
	end
end
