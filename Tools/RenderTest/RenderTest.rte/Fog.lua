function FogScript:StartScript()
	SceneMan:MakeAllUnseen(Vector(12, 12), Activity.TEAM_1);
end

function FogScript:UpdateScript()
	-- Reveal around the screen center like an actor's view would, with terrain blocking it.
	local center = CameraMan:GetOffset(0) + Vector(FrameMan.PlayerScreenWidth * 0.32, FrameMan.PlayerScreenHeight * 0.5);
	local endPos = Vector();
	for i = 0, 89 do
		local ray = Vector(260, 0):RadRotate(i / 90 * math.pi * 2);
		SceneMan:CastSeeRay(Activity.TEAM_1, center, ray, endPos, 40, 2);
	end
end
