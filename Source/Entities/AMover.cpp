#include "AMover.h"
#include "Attachable.h"
#include "FluidSim.h"
#include "Matrix.h"
#include "MovableMan.h"
#include "PresetMan.h"
#include "SceneMan.h"
#include "Scene.h"
#include "SLTerrain.h"
#include "SoundContainer.h"
#include "TimerMan.h"

#include "tracy/Tracy.hpp"

using namespace RTE;

ConcreteClassInfo(AMover, Actor, 20);

AMover::AMover() {
	Clear();
}

AMover::~AMover() {
	Destroy(true);
}

void AMover::Clear() {
	m_Deck = nullptr;
	m_Stops.clear();
	m_Speed = 60.0F;
	m_StopWait = 3000;
	m_OnCall = false;
	m_DeckOffset.Reset();
	m_StopIndex = 0;
	m_TargetStop = 0;
	m_Direction = 1;
	m_Moving = false;
	m_CalledStop = -1;
	m_StopTimer.Reset();
	m_PauseMS = 0;
	m_CallSenseTimer.Reset();
	m_MaterialRedrawTimer.Reset();
	m_DeckMaterialID = g_MaterialAir;
	m_DrawnPixels.clear();
	m_DeckMaterialDrawn = false;
	m_DrawnDeckPos.Reset();
	m_InitialSpriteAnimDuration = 0;
	m_MoveStartSound = nullptr;
	m_MoveSound = nullptr;
	m_MoveEndSound = nullptr;

	// As for doors: a mover isn't squished out of the terrain its own deck is drawn into.
	m_CanBeSquished = false;
}

int AMover::Create() {
	if (Actor::Create() < 0) {
		return -1;
	}
	m_Team = Activity::NoTeam;
	return 0;
}

int AMover::Create(const AMover& reference) {
	if (reference.m_Deck) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_Deck->GetUniqueID());
	}

	Actor::Create(reference);

	if (reference.m_Deck) {
		SetDeck(dynamic_cast<Attachable*>(reference.m_Deck->Clone()));
	}

	m_Stops = reference.m_Stops;
	m_Speed = reference.m_Speed;
	m_StopWait = reference.m_StopWait;
	m_OnCall = reference.m_OnCall;
	m_DeckOffset = reference.m_DeckOffset;
	m_StopIndex = reference.m_StopIndex;
	m_TargetStop = reference.m_StopIndex;
	m_Direction = reference.m_Direction;
	m_InitialSpriteAnimDuration = reference.m_SpriteAnimDuration;
	m_Team = Activity::NoTeam;

	if (reference.m_MoveStartSound) {
		m_MoveStartSound.reset(dynamic_cast<SoundContainer*>(reference.m_MoveStartSound->Clone()));
	}
	if (reference.m_MoveSound) {
		m_MoveSound.reset(dynamic_cast<SoundContainer*>(reference.m_MoveSound->Clone()));
	}
	if (reference.m_MoveEndSound) {
		m_MoveEndSound.reset(dynamic_cast<SoundContainer*>(reference.m_MoveEndSound->Clone()));
	}

	return 0;
}

int AMover::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Actor::ReadProperty(propName, reader));

	MatchProperty("Deck", { SetDeck(dynamic_cast<Attachable*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("AddStop", {
		Vector stop;
		reader >> stop;
		m_Stops.push_back(stop);
		// The deck starts at the first stop.
		if (m_Stops.size() == 1) {
			m_DeckOffset = stop;
			m_StopIndex = 0;
			m_TargetStop = 0;
		}
	});
	MatchProperty("ClearStops", {
		bool clear;
		reader >> clear;
		if (clear) {
			m_Stops.clear();
			m_DeckOffset.Reset();
		}
	});
	MatchProperty("Speed", {
		reader >> m_Speed;
		m_Speed = std::max(m_Speed, 1.0F);
	});
	MatchProperty("StopWait", { reader >> m_StopWait; });
	MatchProperty("OnCall", { reader >> m_OnCall; });
	MatchProperty("MoveStartSound", { m_MoveStartSound.reset(dynamic_cast<SoundContainer*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("MoveSound", { m_MoveSound.reset(dynamic_cast<SoundContainer*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("MoveEndSound", { m_MoveEndSound.reset(dynamic_cast<SoundContainer*>(g_PresetMan.ReadReflectedPreset(reader))); });

	EndPropertyList;
}

int AMover::Save(Writer& writer) const {
	Actor::Save(writer);

	writer.NewProperty("Deck");
	writer << m_Deck;
	// Read back over a copy of the preset, which has its own stops.
	writer.NewProperty("ClearStops");
	writer << true;
	for (const Vector& stop: m_Stops) {
		writer.NewProperty("AddStop");
		writer << stop;
	}
	writer.NewProperty("Speed");
	writer << m_Speed;
	writer.NewProperty("StopWait");
	writer << m_StopWait;
	writer.NewProperty("OnCall");
	writer << m_OnCall;
	writer.NewProperty("MoveStartSound");
	writer << m_MoveStartSound.get();
	writer.NewProperty("MoveSound");
	writer << m_MoveSound.get();
	writer.NewProperty("MoveEndSound");
	writer << m_MoveEndSound.get();

	return 0;
}

void AMover::Destroy(bool notInherited) {
	for (const std::unique_ptr<SoundContainer>* sound: {&m_MoveStartSound, &m_MoveSound, &m_MoveEndSound}) {
		if (*sound) {
			(*sound)->Stop();
		}
	}

	if (!notInherited) {
		Actor::Destroy();
	}

	if (m_DeckMaterialDrawn) {
		EraseDeckMaterial(true);
	}

	Clear();
}

void AMover::SetDeck(Attachable* newDeck) {
	if (m_DeckMaterialDrawn) {
		EraseDeckMaterial(true);
	}

	if (m_Deck && m_Deck->IsAttached()) {
		RemoveAndDeleteAttachable(m_Deck);
	}

	m_Deck = newDeck;
	if (m_Deck) {
		AddAttachable(m_Deck);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({m_Deck->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 dynamic_cast<AMover*>(parent)->SetDeck(attachable);
		                                                 }});

		m_Deck->SetInheritsRotAngle(false);
		m_Deck->SetRotAngle(0.0F);
		// The deck's own atoms would sit in the material it draws; the terrain sees the deck through that material instead.
		m_Deck->SetCollidesWithTerrainWhileAttached(false);
		m_Deck->SetParentOffset(m_DeckOffset);
		m_DeckMaterialID = static_cast<unsigned char>(m_Deck->GetMaterial()->GetIndex());
	}
}

void AMover::SetTeam(int team) {
	Actor::SetTeam(Activity::NoTeam);
}

Vector AMover::GetStopStandingPos(int stop) const {
	if (!m_Deck || stop < 0 || stop >= GetStopCount()) {
		return Vector();
	}
	Box box = DeckBoxAt(DeckPosAt(m_Stops[stop]));
	return Vector(box.GetCorner().m_X + box.GetWidth() * 0.5F, box.GetCorner().m_Y);
}

Vector AMover::GetDeckStandingPos() const {
	if (!m_Deck) {
		return Vector();
	}
	Box box = DeckBoxAt(DeckPosAt(m_DeckOffset));
	return Vector(box.GetCorner().m_X + box.GetWidth() * 0.5F, box.GetCorner().m_Y);
}

bool AMover::GetDeckMaterialBox(Box& box) const {
	if (!m_Deck || !m_DeckMaterialDrawn) {
		return false;
	}
	box = DeckBoxAt(m_DrawnDeckPos);
	return true;
}

void AMover::CallTo(int stop) {
	if (stop < 0 || stop >= GetStopCount()) {
		return;
	}
	if (GetCurrentStop() == stop) {
		m_CalledStop = -1;
		return;
	}
	m_CalledStop = stop;
	if (m_Moving && m_TargetStop != stop) {
		// Turned round (or sent on) from where it is: the stop it travels from becomes the one it left.
		m_StopIndex = m_TargetStop;
		m_TargetStop = stop;
	}
}

Vector AMover::DeckPosAt(const Vector& offset) const {
	Vector pos = m_Pos + RotateOffset(offset);
	if (m_Deck) {
		pos -= m_Deck->RotateOffset(m_Deck->GetJointOffset());
	}
	return Vector(std::floor(pos.m_X), std::floor(pos.m_Y));
}

Box AMover::DeckBoxAt(const Vector& deckPos) const {
	if (!m_Deck) {
		return Box(deckPos, 1.0F, 1.0F);
	}
	return Box(deckPos + m_Deck->GetSpriteOffset(), static_cast<float>(m_Deck->GetSpriteWidth()), static_cast<float>(m_Deck->GetSpriteHeight()));
}

int AMover::DrawDeckMaterial(const Vector& deckPos, bool notifyGrid) {
	SLTerrain* terrain = g_SceneMan.GetTerrain();
	BITMAP* sprite = m_Deck ? m_Deck->GetSpriteFrame(0) : nullptr;
	if (!terrain || !terrain->GetMaterialBitmap() || !sprite) {
		return 0;
	}
	if (m_DeckMaterialDrawn) {
		EraseDeckMaterial(false);
	}

	int left = static_cast<int>(deckPos.m_X + m_Deck->GetSpriteOffset().m_X);
	int top = static_cast<int>(deckPos.m_Y + m_Deck->GetSpriteOffset().m_Y);
	int solid = 0;
	m_DrawnPixels.clear();
	for (int y = 0; y < sprite->h; ++y) {
		for (int x = 0; x < sprite->w; ++x) {
			if (getpixel(sprite, x, y) == ColorKeys::g_MaskColor) {
				continue;
			}
			int sceneX = left + x;
			int sceneY = top + y;
			if (terrain->IsAirPixel(sceneX, sceneY)) {
				terrain->SetMaterialPixel(sceneX, sceneY, m_DeckMaterialID);
				m_DrawnPixels.emplace_back(sceneX, sceneY);
			} else if (!FluidSim::IsLiquid(terrain->GetMaterialPixel(sceneX, sceneY))) {
				// Liquid is left where it is, and the deck has a hole there while it's under; solid ground is counted.
				++solid;
			}
		}
	}
	m_DrawnDeckPos = deckPos;
	m_DeckMaterialDrawn = true;
	if (notifyGrid) {
		terrain->AddUpdatedMaterialArea(DeckBoxAt(deckPos));
	}
	return solid;
}

void AMover::EraseDeckMaterial(bool notifyGrid) {
	SLTerrain* terrain = g_SceneMan.GetTerrain();
	if (terrain && terrain->GetMaterialBitmap()) {
		for (const auto& [x, y]: m_DrawnPixels) {
			if (terrain->GetMaterialPixel(x, y) == m_DeckMaterialID) {
				terrain->SetMaterialPixel(x, y, g_MaterialAir);
			}
		}
		if (notifyGrid && m_Deck) {
			terrain->AddUpdatedMaterialArea(DeckBoxAt(m_DrawnDeckPos));
		}
	}
	m_DrawnPixels.clear();
	m_DeckMaterialDrawn = false;
}

int AMover::SolidPixelsAt(const Vector& deckPos) const {
	const SLTerrain* terrain = g_SceneMan.GetTerrain();
	BITMAP* sprite = m_Deck ? m_Deck->GetSpriteFrame(0) : nullptr;
	if (!terrain || !sprite) {
		return 0;
	}
	int left = static_cast<int>(deckPos.m_X + m_Deck->GetSpriteOffset().m_X);
	int top = static_cast<int>(deckPos.m_Y + m_Deck->GetSpriteOffset().m_Y);
	int solid = 0;
	for (int y = 0; y < sprite->h; ++y) {
		for (int x = 0; x < sprite->w; ++x) {
			if (getpixel(sprite, x, y) != ColorKeys::g_MaskColor && !terrain->IsAirPixel(left + x, top + y) && !FluidSim::IsLiquid(terrain->GetMaterialPixel(left + x, top + y))) {
				++solid;
			}
		}
	}
	return solid;
}

bool AMover::StandsOnDeck(const MovableObject* mo, float deckVelY) const {
	if (!mo || mo == this || mo->GetRootParent() != mo || mo->GetPinStrength() > 0.0F || mo->IsSetToDelete() || !m_DeckMaterialDrawn) {
		return false;
	}
	Box deck = DeckBoxAt(m_DrawnDeckPos);
	float deckTop = deck.GetCorner().m_Y;
	float deckBottom = deckTop + deck.GetHeight();
	const Vector& pos = mo->GetPos();
	float radius = std::max(mo->GetRadius(), 2.0F);
	// Over the deck (or its edge), and not leaving it upwards faster than it moves: a unit jumping off is let go.
	if (pos.m_X < deck.GetCorner().m_X - radius * 0.3F || pos.m_X > deck.GetCorner().m_X + deck.GetWidth() + radius * 0.3F || pos.m_Y > deckTop + 1.0F || pos.m_Y < deckTop - radius - 6.0F) {
		return false;
	}
	if (mo->GetVel().m_Y < deckVelY - 4.0F) {
		return false;
	}
	// What's first below it, down the middle or either side of the body, is the deck.
	const SLTerrain* terrain = g_SceneMan.GetTerrain();
	for (float dx: {0.0F, -radius * 0.3F, radius * 0.3F}) {
		int x = static_cast<int>(pos.m_X + dx);
		for (int y = static_cast<int>(pos.m_Y); y <= static_cast<int>(deckBottom); ++y) {
			if (!terrain->IsAirPixel(x, y)) {
				if (terrain->GetMaterialPixel(x, y) == m_DeckMaterialID && y >= static_cast<int>(deckTop) - 1) {
					return true;
				}
				break;
			}
		}
	}
	return false;
}

bool AMover::UnitInSweep(const Vector& toDeckPos, const std::vector<MovableObject*>& riders) const {
	// Where the deck is going to be: a unit beside it (waiting at a stop, or just stepped off) is no more in the way than one riding it.
	Box to = DeckBoxAt(toDeckPos);
	float left = to.GetCorner().m_X;
	float top = to.GetCorner().m_Y;
	float right = left + to.GetWidth() - 1.0F;
	float bottom = top + to.GetHeight() - 1.0F;
	for (int y = static_cast<int>(top); y <= static_cast<int>(bottom); ++y) {
		for (int x = static_cast<int>(left); x <= static_cast<int>(right); x += 2) {
			MOID id = g_SceneMan.GetMOIDPixel(x, y);
			if (id == g_NoMOID) {
				continue;
			}
			const MovableObject* mo = g_MovableMan.GetMOFromID(id);
			const MovableObject* root = mo ? mo->GetRootParent() : nullptr;
			if (!root || root == this || !root->IsActor() || std::find(riders.begin(), riders.end(), root) != riders.end()) {
				continue;
			}
			const Actor* actor = static_cast<const Actor*>(root);
			if (actor->GetStatus() != Actor::DEAD && actor->GetPinStrength() <= 0.0F) {
				return true;
			}
		}
	}
	return false;
}

int AMover::NextShuttleStop() {
	int count = GetStopCount();
	if (count < 2) {
		return m_StopIndex;
	}
	int next = m_StopIndex + m_Direction;
	if (next < 0 || next >= count) {
		m_Direction = -m_Direction;
		next = m_StopIndex + m_Direction;
	}
	return next;
}

void AMover::SetOff(int stop) {
	if (stop < 0 || stop >= GetStopCount() || stop == GetCurrentStop()) {
		return;
	}
	m_TargetStop = stop;
	m_Direction = stop > m_StopIndex ? 1 : -1;
	m_Moving = true;
	if (m_MoveStartSound) {
		m_MoveStartSound->Play(m_Pos);
	}
	// The path grid saw the deck standing here; it isn't any more.
	if (g_SceneMan.GetTerrain() && m_Deck) {
		g_SceneMan.GetTerrain()->AddUpdatedMaterialArea(DeckBoxAt(m_DrawnDeckPos));
	}
}

void AMover::Arrive() {
	m_DeckOffset = m_Stops[m_TargetStop];
	m_StopIndex = m_TargetStop;
	m_Moving = false;
	if (m_CalledStop == m_StopIndex) {
		m_CalledStop = -1;
	}
	m_StopTimer.Reset();
	m_PauseMS = m_StopWait;
	if (m_MoveSound) {
		m_MoveSound->Stop();
	}
	if (m_MoveEndSound) {
		m_MoveEndSound->Play(m_Pos);
	}
}

int AMover::StopWithUnitWaiting() const {
	if (!m_Deck) {
		return -1;
	}
	for (int stop = 0; stop < GetStopCount(); ++stop) {
		if (stop == GetCurrentStop()) {
			continue;
		}
		// Standing where the deck would be at that stop: across its width, within a unit's height above its top.
		Box deck = DeckBoxAt(DeckPosAt(m_Stops[stop]));
		Box zone(Vector(deck.GetCorner().m_X, deck.GetCorner().m_Y - 60.0F), deck.GetWidth(), 60.0F + deck.GetHeight());
		for (const Actor* actor: g_MovableMan.GetActorList()) {
			if (actor != this && actor->IsControllable() && actor->GetStatus() < Actor::DYING && zone.IsWithinBox(actor->GetPos())) {
				return stop;
			}
		}
	}
	return -1;
}

void AMover::UpdateTravel() {
	if (!m_Deck || GetStopCount() < 2) {
		return;
	}

	if (!m_Moving) {
		if (!m_StopTimer.IsPastSimMS(m_PauseMS)) {
			return;
		}
		int stop = -1;
		if (m_CalledStop >= 0) {
			stop = m_CalledStop;
		} else if (!m_OnCall) {
			stop = NextShuttleStop();
		} else {
			// On call: a rider is taken on to the next stop, and a unit waiting at another stop brings the deck to it.
			bool riding = std::any_of(g_MovableMan.GetActorList().begin(), g_MovableMan.GetActorList().end(), [this](const Actor* actor) { return actor->IsControllable() && StandsOnDeck(actor, 0.0F); });
			if (riding) {
				stop = NextShuttleStop();
			} else if (m_CallSenseTimer.IsPastSimMS(250)) {
				m_CallSenseTimer.Reset();
				stop = StopWithUnitWaiting();
			}
		}
		if (stop < 0 || stop == GetCurrentStop()) {
			return;
		}
		SetOff(stop);
	}

	if (m_MoveSound && !m_MoveSound->IsBeingPlayed()) {
		m_MoveSound->Play(m_Pos);
	}

	// Along the straight line to the target, easing in and out over the first and last 16 px so riders aren't jolted.
	const Vector& from = m_Stops[m_StopIndex];
	const Vector& to = m_Stops[m_TargetStop];
	Vector remaining = to - m_DeckOffset;
	float remainingLength = remaining.GetMagnitude();
	float travelled = (m_DeckOffset - from).GetMagnitude();
	float ease = std::clamp(std::min(travelled, remainingLength) / 16.0F, 0.25F, 1.0F);
	float step = m_Speed * ease * g_TimerMan.GetDeltaTimeSecs();
	Vector newOffset = remainingLength <= step ? to : m_DeckOffset + remaining * (step / remainingLength);
	Vector newDeckPos = DeckPosAt(newOffset);
	Vector shift = newDeckPos - m_DrawnDeckPos;

	if (shift.IsZero()) {
		m_DeckOffset = newOffset;
		if (newOffset == to) {
			Arrive();
		}
		return;
	}

	// Whatever stands on the deck, found while its material is still where they stand.
	float deckVelY = shift.m_Y / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F) / c_PPM;
	std::vector<MovableObject*> riders;
	for (Actor* actor: g_MovableMan.GetActorList()) {
		if (StandsOnDeck(actor, deckVelY)) {
			riders.push_back(actor);
		}
	}
	for (MovableObject* item: g_MovableMan.GetItemList()) {
		if (StandsOnDeck(item, deckVelY)) {
			riders.push_back(item);
		}
	}

	// A unit in the way, or one riding up into a ceiling, or ground built across the path: the deck goes back to the stop it left.
	bool blocked = UnitInSweep(newDeckPos, riders);
	if (!blocked && shift.m_Y < 0.0F) {
		const SLTerrain* terrain = g_SceneMan.GetTerrain();
		for (const MovableObject* rider: riders) {
			if (rider->IsActor()) {
				Vector head = rider->GetPos() + Vector(0.0F, -rider->GetRadius() * 0.8F + shift.m_Y);
				if (!terrain->IsAirPixel(head.GetFloorIntX(), head.GetFloorIntY()) && terrain->GetMaterialPixel(head.GetFloorIntX(), head.GetFloorIntY()) != m_DeckMaterialID) {
					blocked = true;
					break;
				}
			}
		}
	}
	Vector oldDeckPos = m_DrawnDeckPos;
	if (!blocked) {
		EraseDeckMaterial(false);
		// Loose bits (settled gibs, a little dirt) are pushed through; a real wall stops it.
		Box deck = DeckBoxAt(newDeckPos);
		int tolerated = std::max(8, static_cast<int>(deck.GetWidth() * deck.GetHeight()) / 8);
		if (SolidPixelsAt(newDeckPos) > tolerated) {
			blocked = true;
		}
	}
	if (blocked) {
		if (!m_DeckMaterialDrawn) {
			DrawDeckMaterial(oldDeckPos, false);
		}
		// Back to the stop it left, after a moment. It stands between stops meanwhile: the stop it was headed for is taken as the one it left,
		// so the leg back eases out from there, and the stop it came from is where it is sent.
		int cameFrom = m_StopIndex;
		m_StopIndex = m_TargetStop;
		m_TargetStop = cameFrom;
		m_CalledStop = cameFrom;
		m_Moving = false;
		m_StopTimer.Reset();
		m_PauseMS = 750;
		if (m_MoveSound) {
			m_MoveSound->Stop();
		}
		return;
	}

	for (MovableObject* rider: riders) {
		rider->SetPos(rider->GetPos() + shift);
	}
	m_DeckOffset = newOffset;
	DrawDeckMaterial(newDeckPos, false);
	if (newOffset == to) {
		Arrive();
		g_SceneMan.GetTerrain()->AddUpdatedMaterialArea(DeckBoxAt(newDeckPos));
	}
}

void AMover::GibThis(const Vector& impactImpulse, MovableObject* movableObjectToIgnore) {
	if (m_DeckMaterialDrawn) {
		EraseDeckMaterial(true);
	}
	if (m_Deck && m_Deck->IsAttached()) {
		m_Deck->SetPinStrength(0);
	}

	Actor::GibThis(impactImpulse, movableObjectToIgnore);
}

void AMover::CorrectAttachableAndWoundPositionsAndRotations() const {
	if (m_Deck) {
		m_Deck->SetParentOffset(m_DeckOffset);
		m_Deck->SetRotAngle(0.0F);
	}

	MOSRotating::CorrectAttachableAndWoundPositionsAndRotations();
}

void AMover::Update() {
	ZoneScoped;

	if (m_Deck && m_Status != Actor::Status::INACTIVE && m_Status != DYING && m_Status != DEAD) {
		if (!m_DeckMaterialDrawn) {
			DrawDeckMaterial(DeckPosAt(m_DeckOffset), true);
			m_MaterialRedrawTimer.Reset();
		}
		UpdateTravel();
		// Standing, the deck is drawn again now and then, so one dug into or blasted is mended (as doors are).
		if (!m_Moving && m_MaterialRedrawTimer.IsPastSimMS(2000)) {
			DrawDeckMaterial(m_DrawnDeckPos, false);
			m_MaterialRedrawTimer.Reset();
		}
		m_Deck->SetParentOffset(m_DeckOffset);
		m_Deck->SetRotAngle(0.0F);
	}

	Actor::Update();

	if (m_SpriteAnimMode == LOOPWHENOPENCLOSE && m_FrameCount > 1 && m_Moving && m_SpriteAnimTimer.IsPastSimMS(m_SpriteAnimDuration)) {
		m_Frame = (m_Frame + 1) % m_FrameCount;
		m_SpriteAnimTimer.Reset();
	}

	// The deck shot off: the motor runs down, as a door's does.
	if (!m_Deck && m_Status != DYING && m_Status != DEAD) {
		if (m_DeckMaterialDrawn) {
			EraseDeckMaterial(true);
		}
		m_Health -= 0.4F;
	}

	if (m_Status == DEAD) {
		GibThis();
	}
}
