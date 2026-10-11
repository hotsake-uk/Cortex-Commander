-- The moment a firepot breaks (an invisible gib of it): the greek fire takes, setting alight whoever and whatever is close, with a flash,
-- a small shockwave, a scorch on the ground and a burst of heat-haze that fades as the flames settle into burning.

function Create(self)
	SceneMan:IgniteArea(self.Pos, 42);
	SceneMan:AddAirBlast(self.Pos, 2500);
	SceneMan:AddGas(self.Pos + Vector(0, -6), 0, 2);
	PostProcessMan:AddShockwave(self.Pos, 2600);
	PostProcessMan:AddScorchMark(self.Pos, 3000);
	CameraMan:AddScreenShake(4, self.Pos);
	self.Vel = Vector();
	self.secondCatch = false;
end

function Update(self)
	self.Vel = Vector();
	local fade = math.max(1 - self.Age / self.Lifetime, 0);
	local flicker = 0.8 + 0.2 * math.random();
	PostProcessMan:AddLight(self.Pos + Vector(0, -6), 50 + 150 * fade, 255, 135, 40, (0.6 + 2.4 * fade * fade) * flicker);
	PostProcessMan:AddShimmer(self.Pos + Vector(0, -12), 26 + 20 * (1 - fade), 1.1 * fade);
	-- The splashed fuel catches a moment later a little further out.
	if not self.secondCatch and self.Age > 350 then
		self.secondCatch = true;
		SceneMan:IgniteArea(self.Pos, 60);
	end
end
