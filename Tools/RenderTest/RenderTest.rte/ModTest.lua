function ModTestScript:StartScript()
	self.timer = Timer();
	self.burstTimer = Timer();
	self.log = io.open("modtest.log", "w");
end

function ModTestScript:UpdateScript()
	-- A beam light and visual particles on a flare-less item, set from Lua the way a mod would.
	local actor = ActivityMan:GetActivity():GetControlledActor(0);
	if not actor then
		return;
	end
	if not self.done and self.timer:IsPastSimMS(3000) then
		self.done = true;
		actor.LightRadius = 260;
		actor.LightIntensity = 1.4;
		actor:SetLightColor(170, 220, 255);
		actor.LightConeAngle = 18;
		actor.LightConeDirection = -20;
		actor:SetVisualEmission("Embers", 25, 0.8);
		self.log:write("cone " .. actor.LightConeAngle .. "\n");
		self.log:flush();
	end
	if self.done and self.burstTimer:IsPastSimMS(700) then
		self.burstTimer:Reset();
		local ok = EmitVisualParticles("Sparks", actor.Pos + Vector(60, -30), Vector(0, -6), 1, 60, 0);
		local bad = EmitVisualParticles("Nonsense", actor.Pos, Vector(), 0, 1, 0);
		self.log:write("emit " .. tostring(ok) .. " " .. tostring(bad) .. "\n");
		self.log:flush();
	end
end
