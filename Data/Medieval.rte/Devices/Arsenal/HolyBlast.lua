-- The Holy Hand Grenade going off (an invisible gib of it): a huge shockwave, a blinding golden flash that washes the whole picture out, a
-- great shove outwards, heavy harm to everyone close (friend and foe alike: the Book of Armaments is clear on the count), and for a couple of
-- seconds rays of golden light fanning up out of the blast like a painting of a miracle.

local rayCount = 11;
local reach = 140;

function Create(self)
	local pos = Vector(self.Pos.X, self.Pos.Y);
	self.origin = pos;
	self.Vel = Vector();

	PostProcessMan:AddShockwave(pos, 60000);
	PostProcessMan:PulseGrade(5, 1.6, 40, 1800);
	CameraMan:AddScreenShake(40, pos);
	SceneMan:AddAirBlast(pos, 30000);
	SceneMan:MagicPush(pos, Vector(0, -1), 38, 170, 360, nil);

	for actor in MovableMan.Actors do
		if actor.Status < Actor.DYING then
			local dist = SceneMan:ShortestDistance(pos, actor.Pos, SceneMan.SceneWrapsX).Magnitude;
			if dist < reach then
				local f = 1 - dist / reach;
				actor.Health = actor.Health - 150 * f * f;
				if f > 0.25 and actor.Status == Actor.STABLE then
					actor.Status = Actor.UNSTABLE;
				end
				actor:FlashWhite(250);
			end
		end
	end

	-- The rays fan upwards, each its own length and sway.
	self.rays = {};
	for i = 1, rayCount do
		local spread = (i - (rayCount + 1) / 2) / ((rayCount - 1) / 2);
		self.rays[i] = {
			angle = math.pi * 0.5 + spread * 1.3 + RangeRand(-0.05, 0.05),
			length = RangeRand(200, 340) * (1 - 0.35 * math.abs(spread)),
			sway = RangeRand(0, 6.28),
			width = RangeRand(1.5, 3),
		};
	end
end

function Update(self)
	self.Vel = Vector();
	local pos = self.origin;
	local grow = math.min(self.Age / 260, 1);
	local fade = math.max(1 - self.Age / self.Lifetime, 0);
	local fade2 = fade * fade;

	for _, ray in ipairs(self.rays) do
		local angle = ray.angle + 0.04 * math.sin(self.Age * 0.004 + ray.sway);
		local tip = pos + Vector(math.cos(angle), -math.sin(angle)) * (ray.length * grow);
		PostProcessMan:AddEnergyBeam(pos, tip, ray.width * (0.5 + fade), 255, 222, 120, 1.5 * fade2, 36 * fade);
	end
	PostProcessMan:AddLight(pos, 120 + 300 * fade, 255, 226, 140, 3.2 * fade2 + 0.2);
	PostProcessMan:AddShimmer(pos, 50 + 90 * grow, 1.3 * fade);
end
