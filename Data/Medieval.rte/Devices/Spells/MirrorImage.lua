-- A Mirror Image's keeper (SpellCast.lua makes the image, a clone of the caster, and this controller for it): the image stands with the
-- caster for Seconds (10), then bursts into violet glimmer; it bursts at once when anything hurts it (its health falls or a wound lands), or
-- when the caster it mirrors dies. The image has the Illusion status effect (SpellEffects.lua), so it hurts nothing and is drawn see-through.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local function pop(self, image)
	if self.popped then
		return;
	end
	self.popped = true;
	if image then
		FX.Ring("Sparks", image.Pos, 18, 7, 0xBE96FF, 4);
		EmitVisualParticles("Mist", image.Pos, Vector(0, -1), 0.7, 10, 0xD8C8FF);
		PostProcessMan:AddLight(image.Pos, 110, 190, 150, 255, 1.6);
		-- Gone without a corpse: its weapons and all.
		for item in image.Inventory do
			item.ToDelete = true;
		end
		local held = IsAHuman(image) and ToAHuman(image).EquippedItem or nil;
		if held then
			held.ToDelete = true;
		end
		image.ToSettle = false;
		image.ToDelete = true;
	end
	self.ToDelete = true;
end

function Create(self)
	self.targetUID = self:GetNumberValue("TargetUID");
	self.casterUID = self:GetNumberValue("CasterUID");
	self.untilMS = FX.Now() + (self:NumberValueExists("Seconds") and self:GetNumberValue("Seconds") or 10) * 1000;
	self.frame = 0;
	local image = FX.ActorByUID(self.targetUID);
	if not image then
		self.popped = true;
		self.ToDelete = true;
		return;
	end
	self.health = image.Health;
	self.wounds = image.WoundCount;
end

function Update(self)
	if self.popped then
		return;
	end
	local image = FX.ActorByUID(self.targetUID);
	if not image then
		self.popped = true;
		self.ToDelete = true;
		return;
	end
	local caster = FX.ActorByUID(self.casterUID);
	if FX.Now() > self.untilMS or image.Health < self.health - 0.5 or image.WoundCount > self.wounds or image.Status >= Actor.DYING or not (caster and FX.Alive(caster)) then
		pop(self, image);
		return;
	end
	self.frame = self.frame + 1;
	self.Pos = image.Pos;
	-- The image follows the caster about, as a squad would, when it has nothing of its own to do.
	if caster and self.frame % 30 == 0 and image.AIMode ~= Actor.AIMODE_GOTO then
		local away = SceneMan:ShortestDistance(image.Pos, caster.Pos, true);
		if away.Magnitude > 120 then
			image.AIMode = Actor.AIMODE_GOTO;
			image:AddAISceneWaypoint(caster.Pos + Vector(FX.Rand(-30, 30), 0));
		end
	elseif image.AIMode == Actor.AIMODE_GOTO and self.frame % 30 == 15 then
		local away = caster and SceneMan:ShortestDistance(image.Pos, caster.Pos, true).Magnitude or 0;
		if away < 60 then
			image:ClearAIWaypoints();
			image.AIMode = Actor.AIMODE_SENTRY;
		end
	end
	if self.frame % 7 == 0 then
		EmitVisualParticles("Sparks", image.Pos + Vector(FX.Rand(-5, 5), FX.Rand(-12, 10)), Vector(0, -0.4), 0.8, 1, 0xC8A8FF);
	end
end

function Destroy(self)
	pcall(function()
		local image = FX.ActorByUID(self.targetUID);
		pop(self, image);
	end);
end
