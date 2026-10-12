-- The Ooze's split (phase 3, D10): killed, an ooze whose OozeSplits number value is set splits into two halves ("Ooze Spawn", of its side,
-- which don't split again) that slither off either way, and bursts into slime. Another creature splits the same way with the script and
-- the number value, into what its OozeSplitInto string value names (Bestiary II, the Myconid's sprouts).

function Update(self)
	if self.split or (self.Health > 0 and self.Status < Actor.DYING) then
		return;
	end
	self.split = true;
	if self:NumberValueExists("OozeSplits") and self:GetNumberValue("OozeSplits") > 0 then
		for _, side in ipairs({-1, 1}) do
			local Half = CreateACrab(self:StringValueExists("OozeSplitInto") and self:GetStringValue("OozeSplitInto") or "Ooze Spawn", "Base.rte");
			if Half then
				Half.Pos = self.Pos + Vector(side * self.Radius * 0.5, -4);
				Half.Vel = self.Vel + Vector(side * 3, -3);
				Half.Team = self.Team;
				Half.AIMode = self.AIMode == Actor.AIMODE_NONE and Actor.AIMODE_SENTRY or self.AIMode;
				Half.HFlipped = side < 0;
				MovableMan:AddActor(Half);
			end
		end
	end
	self:GibThis();
end
