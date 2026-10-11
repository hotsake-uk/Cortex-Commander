-- A venomous bite's particle (CR-6, the Giant Spider's fangs): a unit not of the biter's side that it strikes is poisoned (Actor:Envenom),
-- losing health for a few seconds and walking slower meanwhile. The particle's number values say how badly:
--   VenomDamage   health a second (default 4)
--   VenomSeconds  how long (default 4)
--   VenomSlow     the share of its pace the victim walks at (default 0.5)

local function Number(self, key, default)
	return self:NumberValueExists(key) and self:GetNumberValue(key) or default;
end

function OnCollideWithMO(self, mo, rootMO)
	if self.struck then
		return;
	end
	self.struck = true;
	if rootMO and IsActor(rootMO) and rootMO.Team ~= self.Team then
		ToActor(rootMO):Envenom(Number(self, "VenomDamage", 4), Number(self, "VenomSeconds", 4), Number(self, "VenomSlow", 0.5));
	end
end
