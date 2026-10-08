#include "Actor.h"
#include "ActorWater.h"
#include "ActorFire.h"
#include "FluidSim.h"
#include "ConsoleMan.h"
#include "WeatherEffects.h"
#include "SceneLighting.h"
#include "PostProcessMan.h"

#include "UInputMan.h"
#include "ActivityMan.h"
#include "CameraMan.h"
#include "Singleton.h"
#include "GameActivity.h"
#include "ACrab.h"
#include "ACraft.h"
#include "ADoor.h"
#include "AtomGroup.h"
#include "Controller.h"
#include "RTETools.h"
#include "SceneMan.h"
#include "HeldDevice.h"
#include "PresetMan.h"
#include "AEmitter.h"
#include "Material.h"
#include "MOPixel.h"
#include "Scene.h"
#include "SettingsMan.h"
#include "FrameMan.h"
#include "PerformanceMan.h"
#include "PostProcessMan.h"
#include "PieMenu.h"
#include "SmokeGrid.h"

#include "GUI.h"
#include "AllegroBitmap.h"

#include "tracy/Tracy.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

using namespace RTE;

ConcreteClassInfo(Actor, MOSRotating, 20);

std::vector<BITMAP*> Actor::m_apNoTeamIcon;
BITMAP* Actor::m_apAIIcons[AIMODE_COUNT];
std::vector<BITMAP*> Actor::m_apSelectArrow;
std::vector<BITMAP*> Actor::m_apAlarmExclamation;
bool Actor::m_sIconsLoaded = false;
int Actor::s_ShowAIPaths = 0;

#define ARROWTIME 1000

Actor::Actor() {
	Clear();
}

Actor::~Actor() {
	Destroy(true);
}

void Actor::Clear() {
	m_Controller.Reset();
	m_PlayerControllable = true;
	m_BodyHitSound = nullptr;
	m_AlarmSound = nullptr;
	m_PainSound = nullptr;
	m_DeathSound = nullptr;
	m_DeviceSwitchSound = nullptr;
	m_Status = STABLE;
	m_Health = m_PrevHealth = m_MaxHealth = 100.0F;
	m_pTeamIcon = nullptr;
	m_pControllerIcon = nullptr;
	m_LastSecondTimer.Reset();
	m_LastSecondPos.Reset();
	m_RecentMovement.Reset();
	m_TravelImpulseDamage = 750.0F;
	m_StableVel.SetXY(15.0F, 25.0F);
	m_StableRecoverDelay = 1000;
	m_CanRun = true;
	m_CrouchWalkSpeedMultiplier = 0.7F;
	m_HeartBeat.Reset();
	m_NewControlTmr.Reset();
	m_DeathTmr.Reset();
	m_GoldCarried = 0;
	m_GoldPicked = false;
	m_AimState = AIMSTILL;
	m_AimAngle = 0;
	m_AimRange = c_HalfPI;
	m_AimDistance = 0;
	m_AimTmr.Reset();
	m_SharpAimTimer.Reset();
	m_SharpAimDelay = 250;
	m_SharpAimProgress = 0;
	m_SharpAimMaxedOut = false;
	m_PointingTarget.Reset();
	m_SeenTargetPos.Reset();
	// Set the limit to soemthing reasonable, if the timer is over it, there's no alarm
	m_AlarmTimer.SetSimTimeLimitMS(3000);
	m_AlarmTimer.SetElapsedSimTimeMS(4000);
	m_LastAlarmPos.Reset();
	m_SightDistance = 450.0F;
	m_Perceptiveness = 0.5F;
	m_LookRandomState = 0;
	m_HeadlampBrightness = 1.0F;
	m_HeadlampColor.SetRGB(255, 240, 215);
	m_HeadlampHasColor = false;
	m_PainThreshold = 15.0F;
	m_CanRevealUnseen = true;
	m_CharHeight = 0;
	m_HolsterOffset.Reset();
	m_ReloadOffset.Reset();
	m_ViewPoint.Reset();
	m_Inventory.clear();
	m_MaxInventoryMass = -1.0F;
	m_pItemInReach = nullptr;
	m_HotkeyActivated.fill(false);
	m_HUDStack = 0;
	m_DeploymentID = 0;
	m_PassengerSlots = 1;

	m_AIMode = AIMODE_NONE;
	m_AIOrderSerial = 0;
	m_StandingOrder = StandingOrder();
	m_WeaponRule = WEAPONS_AT_WILL;
	m_Waypoints.clear();
	m_DrawWaypoints = false;
	m_MoveTarget.Reset();
	m_pMOMoveTarget = nullptr;
	m_PrevPathTarget.Reset();
	m_MovePathGoal.Reset();
	m_HasMovePathGoal = false;
	m_MoveVector.Reset();
	m_MovePath.clear();
	m_MovePathKinds.clear();
	m_UpdateMovePath = false;
	m_ImpossiblePaths = 0;
	m_PathImpossible = false;
	m_PathRetryTimer.Reset();
	m_MoveProximityLimit = 20.0F;
	m_AIBaseDigStrength = c_PathFindingDefaultDigStrength;
	m_BaseMass = std::numeric_limits<float>::infinity();

	m_DamageMultiplier = 1.0F;

	m_Organic = false;
	m_Mechanical = false;

	m_LimbPushForcesAndCollisionsDisabled = false;

	m_PieMenu.reset();
}

int Actor::Create() {
	if (MOSRotating::Create() < 0) {
		return -1;
	}

	// Set MO Type.
	m_MOType = MovableObject::TypeActor;

	// A game saved before the standing order was typed keeps its orders as number values: they are taken over.
	if (NumberValueExists("SandboxAttack") || NumberValueExists("SandboxTarget") || NumberValueExists("SandboxAutoTarget") || NumberValueExists("SandboxAttackX") || NumberValueExists("SandboxDefendX") || NumberValueExists("SandboxHold")) {
		m_StandingOrder.Attack = GetNumberValue("SandboxAttack") > 0.0;
		m_StandingOrder.TargetID = static_cast<long>(GetNumberValue("SandboxTarget"));
		m_StandingOrder.AutoTargetID = static_cast<long>(GetNumberValue("SandboxAutoTarget"));
		if (NumberValueExists("SandboxAttackX")) {
			SetOrderAttackPlace(Vector(static_cast<float>(GetNumberValue("SandboxAttackX")), static_cast<float>(GetNumberValue("SandboxAttackY"))));
		}
		if (NumberValueExists("SandboxDefendX")) {
			SetOrderPost(Vector(static_cast<float>(GetNumberValue("SandboxDefendX")), static_cast<float>(GetNumberValue("SandboxDefendY"))));
		}
		m_StandingOrder.Hold = NumberValueExists("SandboxHold");
		for (const char* tag: {"SandboxAttack", "SandboxTarget", "SandboxAutoTarget", "SandboxAttackX", "SandboxAttackY", "SandboxDefendX", "SandboxDefendY", "SandboxHold"}) {
			RemoveNumberValue(tag);
		}
	}

	// Default to an interesting AI controller mode
	m_Controller.SetInputMode(Controller::CIM_AI);
	m_Controller.SetControlledActor(this);

	m_ViewPoint = m_Pos;
	m_HUDStack = -m_CharHeight / 2;

	// Sets up the team icon
	SetTeam(m_Team);

	if (const Actor* presetActor = static_cast<const Actor*>(GetPreset())) {
		m_BaseMass = presetActor->GetMass();
	}

	// All brain actors by default avoid hitting each other on the same team
	if (IsInGroup("Brains")) {
		m_IgnoresTeamHits = true;
	}

	if (!m_PieMenu) {
		SetPieMenu(static_cast<PieMenu*>(g_PresetMan.GetEntityPreset("PieMenu", GetDefaultPieMenuName())->Clone()));
	} else {
		m_PieMenu->SetOwner(this);
	}

	return 0;
}

int Actor::Create(const Actor& reference) {
	MOSRotating::Create(reference);

	// Set MO Type.
	m_MOType = MovableObject::TypeActor;

	m_Controller = reference.m_Controller;
	m_Controller.SetInputMode(Controller::CIM_AI);
	m_Controller.SetControlledActor(this);
	m_PlayerControllable = reference.m_PlayerControllable;

	if (reference.m_BodyHitSound) {
		m_BodyHitSound = dynamic_cast<SoundContainer*>(reference.m_BodyHitSound->Clone());
	}
	if (reference.m_AlarmSound) {
		m_AlarmSound = dynamic_cast<SoundContainer*>(reference.m_AlarmSound->Clone());
	}
	if (reference.m_PainSound) {
		m_PainSound = dynamic_cast<SoundContainer*>(reference.m_PainSound->Clone());
	}
	if (reference.m_DeathSound) {
		m_DeathSound = dynamic_cast<SoundContainer*>(reference.m_DeathSound->Clone());
	}
	if (reference.m_DeviceSwitchSound) {
		m_DeviceSwitchSound = dynamic_cast<SoundContainer*>(reference.m_DeviceSwitchSound->Clone());
	}
	//    m_FacingRight = reference.m_FacingRight;
	m_Status = reference.m_Status;
	m_Health = m_PrevHealth = reference.m_Health;
	m_MaxHealth = reference.m_MaxHealth;
	m_pTeamIcon = reference.m_pTeamIcon;
	//    m_LastSecondTimer.Reset();
	//    m_LastSecondPos.Reset();
	//    m_RecentMovement.Reset();
	m_LastSecondPos = reference.m_LastSecondPos;
	m_TravelImpulseDamage = reference.m_TravelImpulseDamage;
	m_StableVel = reference.m_StableVel;
	m_StableRecoverDelay = reference.m_StableRecoverDelay;
	m_CanRun = reference.m_CanRun;
	m_CrouchWalkSpeedMultiplier = reference.m_CrouchWalkSpeedMultiplier;
	m_GoldCarried = reference.m_GoldCarried;
	m_Suppression = reference.m_Suppression;
	m_Morale = reference.m_Morale;
	m_MoraleLevel = reference.m_MoraleLevel;
	m_AimState = reference.m_AimState;
	m_AimRange = reference.m_AimRange;
	m_AimAngle = reference.m_AimAngle;
	m_AimDistance = reference.m_AimDistance;
	m_SharpAimDelay = reference.m_SharpAimDelay;
	m_SharpAimProgress = reference.m_SharpAimProgress;
	m_PointingTarget = reference.m_PointingTarget;
	m_SeenTargetPos = reference.m_SeenTargetPos;
	m_SightDistance = reference.m_SightDistance;
	m_Perceptiveness = reference.m_Perceptiveness;
	m_HeadlampBrightness = reference.m_HeadlampBrightness;
	m_HeadlampColor = reference.m_HeadlampColor;
	m_HeadlampHasColor = reference.m_HeadlampHasColor;
	m_PainThreshold = reference.m_PainThreshold;
	m_CanRevealUnseen = reference.m_CanRevealUnseen;
	m_CharHeight = reference.m_CharHeight;
	m_HolsterOffset = reference.m_HolsterOffset;
	m_ReloadOffset = reference.m_ReloadOffset;

	for (std::deque<MovableObject*>::const_iterator itr = reference.m_Inventory.begin(); itr != reference.m_Inventory.end(); ++itr) {
		m_Inventory.push_back(dynamic_cast<MovableObject*>((*itr)->Clone()));
	}

	m_MaxInventoryMass = reference.m_MaxInventoryMass;

	// Only load the static AI mode icons once
	if (!m_sIconsLoaded) {
		ContentFile("Base.rte/GUIs/TeamIcons/NoTeam.png").GetAsAnimation(m_apNoTeamIcon, 2);

		ContentFile iconFile("Base.rte/GUIs/PieMenus/PieIcons/Blank000.png");
		m_apAIIcons[AIMODE_NONE] = iconFile.GetAsBitmap();
		m_apAIIcons[AIMODE_BOMB] = iconFile.GetAsBitmap();
		iconFile.SetDataPath("Base.rte/GUIs/PieMenus/PieIcons/Eye000.png");
		m_apAIIcons[AIMODE_SENTRY] = iconFile.GetAsBitmap();
		iconFile.SetDataPath("Base.rte/GUIs/PieMenus/PieIcons/Cycle000.png");
		m_apAIIcons[AIMODE_PATROL] = iconFile.GetAsBitmap();
		iconFile.SetDataPath("Base.rte/GUIs/PieMenus/PieIcons/GoTo000.png");
		m_apAIIcons[AIMODE_GOTO] = iconFile.GetAsBitmap();
		iconFile.SetDataPath("Base.rte/GUIs/PieMenus/PieIcons/Brain000.png");
		m_apAIIcons[AIMODE_BRAINHUNT] = iconFile.GetAsBitmap();
		iconFile.SetDataPath("Base.rte/GUIs/PieMenus/PieIcons/Dig000.png");
		m_apAIIcons[AIMODE_GOLDDIG] = iconFile.GetAsBitmap();
		iconFile.SetDataPath("Base.rte/GUIs/PieMenus/PieIcons/Return000.png");
		m_apAIIcons[AIMODE_RETURN] = iconFile.GetAsBitmap();
		iconFile.SetDataPath("Base.rte/GUIs/PieMenus/PieIcons/Land000.png");
		m_apAIIcons[AIMODE_STAY] = iconFile.GetAsBitmap();
		iconFile.SetDataPath("Base.rte/GUIs/PieMenus/PieIcons/Launch000.png");
		m_apAIIcons[AIMODE_DELIVER] = iconFile.GetAsBitmap();
		iconFile.SetDataPath("Base.rte/GUIs/PieMenus/PieIcons/Death000.png");
		m_apAIIcons[AIMODE_SCUTTLE] = iconFile.GetAsBitmap();
		iconFile.SetDataPath("Base.rte/GUIs/PieMenus/PieIcons/Follow000.png");
		m_apAIIcons[AIMODE_SQUAD] = iconFile.GetAsBitmap();

		ContentFile("Base.rte/GUIs/Indicators/SelectArrow.png").GetAsAnimation(m_apSelectArrow, 4);
		ContentFile("Base.rte/GUIs/Indicators/AlarmExclamation.png").GetAsAnimation(m_apAlarmExclamation, 2);

		m_sIconsLoaded = true;
	}
	m_HotkeyActivated = reference.m_HotkeyActivated;
	m_DeploymentID = reference.m_DeploymentID;
	m_PassengerSlots = reference.m_PassengerSlots;

	m_AIMode = reference.m_AIMode;
	m_StandingOrder = reference.m_StandingOrder;
	m_WeaponRule = reference.m_WeaponRule;
	m_Waypoints = reference.m_Waypoints;
	m_DrawWaypoints = reference.m_DrawWaypoints;
	m_MoveTarget = reference.m_MoveTarget;
	m_pMOMoveTarget = reference.m_pMOMoveTarget;
	m_PrevPathTarget = reference.m_PrevPathTarget;
	m_MovePathGoal = reference.m_MovePathGoal;
	m_HasMovePathGoal = reference.m_HasMovePathGoal;
	m_MoveVector = reference.m_MoveVector;
	m_MovePath.clear();
	m_UpdateMovePath = reference.m_UpdateMovePath;
	m_MoveProximityLimit = reference.m_MoveProximityLimit;
	m_AIBaseDigStrength = reference.m_AIBaseDigStrength;
	m_BaseMass = reference.m_BaseMass;

	m_Organic = reference.m_Organic;
	m_Mechanical = reference.m_Mechanical;

	m_LimbPushForcesAndCollisionsDisabled = reference.m_LimbPushForcesAndCollisionsDisabled;

	RTEAssert(reference.m_PieMenu != nullptr, "Tried to clone actor with no pie menu.");
	SetPieMenu(static_cast<PieMenu*>(reference.m_PieMenu->Clone()));
	m_PieMenu->AddWhilePieMenuOpenListener(this, std::bind(&Actor::WhilePieMenuOpenListener, this, m_PieMenu.get()));

	return 0;
}

int Actor::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return MOSRotating::ReadProperty(propName, reader));

	MatchProperty("PlayerControllable", { reader >> m_PlayerControllable; });
	MatchProperty("BodyHitSound", {
		m_BodyHitSound = new SoundContainer;
		reader >> m_BodyHitSound;
	});
	MatchProperty("AlarmSound", {
		m_AlarmSound = new SoundContainer;
		reader >> m_AlarmSound;
	});
	MatchProperty("PainSound", {
		m_PainSound = new SoundContainer;
		reader >> m_PainSound;
	});
	MatchProperty("DeathSound", {
		m_DeathSound = new SoundContainer;
		reader >> m_DeathSound;
	});
	MatchProperty("DeviceSwitchSound", {
		m_DeviceSwitchSound = new SoundContainer;
		reader >> m_DeviceSwitchSound;
	});
	MatchProperty("Status", { reader >> m_Status; });
	MatchProperty("DeploymentID", { reader >> m_DeploymentID; });
	MatchProperty("PassengerSlots", { reader >> m_PassengerSlots; });
	MatchProperty("Health",
	              {
		              reader >> m_Health;
		              m_PrevHealth = m_Health;
		              if (m_Health > m_MaxHealth)
			              m_MaxHealth = m_Health;
	              });
	MatchProperty("MaxHealth",
	              {
		              reader >> m_MaxHealth;
		              if (m_MaxHealth < m_Health) {
			              m_Health = m_MaxHealth;
			              m_PrevHealth = m_Health;
		              }
	              });
	MatchProperty("ImpulseDamageThreshold", { reader >> m_TravelImpulseDamage; });
	MatchProperty("StableVelocityThreshold", { reader >> m_StableVel; });
	MatchProperty("StableRecoveryDelay", { reader >> m_StableRecoverDelay; });
	MatchProperty("CanRun", { reader >> m_CanRun; });
	MatchProperty("CrouchWalkSpeedMultiplier", { reader >> m_CrouchWalkSpeedMultiplier; });
	MatchProperty("GoldCarried", { reader >> m_GoldCarried; });
	MatchProperty("Suppression", { reader >> m_Suppression; });
	MatchProperty("Morale", { reader >> m_Morale; });
	MatchProperty("AimAngle", { reader >> m_AimAngle; });
	MatchProperty("AimRange", { reader >> m_AimRange; });
	MatchProperty("AimDistance", { reader >> m_AimDistance; });
	MatchProperty("SharpAimDelay", { reader >> m_SharpAimDelay; });
	MatchProperty("SightDistance", { reader >> m_SightDistance; });
	MatchProperty("Perceptiveness", { reader >> m_Perceptiveness; });
	MatchProperty("HeadlampBrightness", { reader >> m_HeadlampBrightness; });
	MatchProperty("HeadlampColor", {
		reader >> m_HeadlampColor;
		m_HeadlampHasColor = true;
	});
	MatchProperty("PainThreshold", { reader >> m_PainThreshold; });
	MatchProperty("CanRevealUnseen", { reader >> m_CanRevealUnseen; });
	MatchProperty("CharHeight", { reader >> m_CharHeight; });
	MatchProperty("HolsterOffset", { reader >> m_HolsterOffset; });
	MatchProperty("ReloadOffset", { reader >> m_ReloadOffset; });
	MatchForwards("AddInventoryDevice") MatchProperty("AddInventory",
	                                                  {
		                                                  MovableObject* pInvMO = dynamic_cast<MovableObject*>(g_PresetMan.ReadReflectedPreset(reader));
		                                                  if (!pInvMO) {
			                                                  reader.ReportError("Object added to inventory is broken.");
		                                                  }
		                                                  AddToInventoryBack(pInvMO);
	                                                  });
	MatchProperty("MaxInventoryMass", { reader >> m_MaxInventoryMass; });
	MatchProperty("AIMode", {
		int mode;
		reader >> mode;
		m_AIMode = static_cast<AIMode>(mode);
	});
	MatchProperty("OrderAttack", { reader >> m_StandingOrder.Attack; });
	MatchProperty("OrderTargetID", { reader >> m_StandingOrder.TargetID; });
	MatchProperty("OrderAutoTargetID", { reader >> m_StandingOrder.AutoTargetID; });
	MatchProperty("OrderAttackPlace", {
		reader >> m_StandingOrder.AttackPlace;
		m_StandingOrder.HasAttackPlace = true;
	});
	MatchProperty("OrderPost", {
		reader >> m_StandingOrder.Post;
		m_StandingOrder.HasPost = true;
	});
	MatchProperty("OrderHold", { reader >> m_StandingOrder.Hold; });
	MatchProperty("OrderMovement", {
		int rule = 0;
		reader >> rule;
		SetMovementRule(rule);
	});
	MatchProperty("WeaponRule", {
		int rule = 0;
		reader >> rule;
		SetWeaponRule(rule);
	});
	MatchProperty("SpecialBehaviour_AddAISceneWaypoint", {
		Vector waypointToAdd;
		reader >> waypointToAdd;
		AddAISceneWaypoint(waypointToAdd);
	});
	MatchProperty("PieMenu", {
		m_PieMenu = std::unique_ptr<PieMenu>(dynamic_cast<PieMenu*>(g_PresetMan.ReadReflectedPreset(reader)));
		if (!m_PieMenu) {
			reader.ReportError("Failed to set Actor's pie menu. Doublecheck your name and everything is correct.");
		}
		m_PieMenu->Create(this);
	});
	MatchProperty("Organic", { reader >> m_Organic; });
	MatchProperty("Mechanical", { reader >> m_Mechanical; });
	MatchProperty("AIBaseDigStrength", { reader >> m_AIBaseDigStrength; });

	EndPropertyList;
}

int Actor::Save(Writer& writer) const {
	MOSRotating::Save(writer);

	writer.NewPropertyWithValue("PlayerControllable", m_PlayerControllable);
	writer.NewProperty("BodyHitSound");
	writer << m_BodyHitSound;
	writer.NewProperty("AlarmSound");
	writer << m_AlarmSound;
	writer.NewProperty("PainSound");
	writer << m_PainSound;
	writer.NewProperty("DeathSound");
	writer << m_DeathSound;
	writer.NewProperty("DeviceSwitchSound");
	writer << m_DeviceSwitchSound;
	writer.NewProperty("Status");
	writer << m_Status;
	writer.NewProperty("Health");
	writer << m_Health;
	writer.NewProperty("MaxHealth");
	writer << m_MaxHealth;
	if (m_DeploymentID) {
		writer.NewProperty("DeploymentID");
		writer << m_DeploymentID;
	}
	writer.NewProperty("ImpulseDamageThreshold");
	writer << m_TravelImpulseDamage;
	writer.NewProperty("StableVelocityThreshold");
	writer << m_StableVel;
	writer.NewProperty("StableRecoveryDelay");
	writer << m_StableRecoverDelay;
	writer.NewProperty("CanRun");
	writer << m_CanRun;
	writer.NewProperty("CrouchWalkSpeedMultiplier");
	writer << m_CrouchWalkSpeedMultiplier;
	writer.NewProperty("GoldCarried");
	writer << m_GoldCarried;
	writer.NewPropertyWithValue("Suppression", m_Suppression);
	writer.NewPropertyWithValue("Morale", m_Morale);
	writer.NewProperty("AimAngle");
	writer << m_AimAngle;
	writer.NewProperty("AimRange");
	writer << m_AimRange;
	writer.NewProperty("AimDistance");
	writer << m_AimDistance;
	writer.NewProperty("SharpAimDelay");
	writer << m_SharpAimDelay;
	writer.NewProperty("SightDistance");
	writer << m_SightDistance;
	writer.NewPropertyWithValue("HeadlampBrightness", m_HeadlampBrightness);
	if (m_HeadlampHasColor) {
		writer.NewPropertyWithValue("HeadlampColor", m_HeadlampColor);
	}
	writer.NewProperty("Perceptiveness");
	writer << m_Perceptiveness;
	writer.NewProperty("PainThreshold");
	writer << m_PainThreshold;
	writer.NewProperty("CanRevealUnseen");
	writer << m_CanRevealUnseen;
	writer.NewProperty("CharHeight");
	writer << m_CharHeight;
	writer.NewProperty("HolsterOffset");
	writer << m_HolsterOffset;
	writer.NewPropertyWithValue("ReloadOffset", m_ReloadOffset);
	for (std::deque<MovableObject*>::const_iterator itr = m_Inventory.begin(); itr != m_Inventory.end(); ++itr) {
		writer.NewProperty("AddInventory");
		writer << **itr;
	}
	writer.NewProperty("MaxInventoryMass");
	writer << m_MaxInventoryMass;
	writer.NewProperty("AIMode");
	writer << m_AIMode;
	// The standing order, the parts it has.
	if (m_StandingOrder.Attack) {
		writer.NewPropertyWithValue("OrderAttack", m_StandingOrder.Attack);
	}
	if (m_StandingOrder.TargetID != 0) {
		writer.NewPropertyWithValue("OrderTargetID", m_StandingOrder.TargetID);
	}
	if (m_StandingOrder.AutoTargetID != 0) {
		writer.NewPropertyWithValue("OrderAutoTargetID", m_StandingOrder.AutoTargetID);
	}
	if (m_StandingOrder.HasAttackPlace) {
		writer.NewPropertyWithValue("OrderAttackPlace", m_StandingOrder.AttackPlace);
	}
	if (m_StandingOrder.HasPost) {
		writer.NewPropertyWithValue("OrderPost", m_StandingOrder.Post);
	}
	if (m_StandingOrder.Hold) {
		writer.NewPropertyWithValue("OrderHold", m_StandingOrder.Hold);
	}
	if (m_StandingOrder.Movement != MOVE_FOLLOW_ORDER) {
		writer.NewPropertyWithValue("OrderMovement", m_StandingOrder.Movement);
	}
	if (m_WeaponRule != WEAPONS_AT_WILL) {
		writer.NewPropertyWithValue("WeaponRule", m_WeaponRule);
	}
	writer.NewProperty("PieMenu");
	writer << m_PieMenu.get();

	writer.NewPropertyWithValue("Organic", m_Organic);
	writer.NewPropertyWithValue("Mechanical", m_Mechanical);
	writer.NewPropertyWithValue("AIBaseDigStrength", m_AIBaseDigStrength);

	return 0;
}

void Actor::DestroyScriptState() {
	for (std::deque<MovableObject*>::const_iterator itr = m_Inventory.begin(); itr != m_Inventory.end(); ++itr) {
		(*itr)->DestroyScriptState();
	}

	MOSRotating::DestroyScriptState();
}

void Actor::Destroy(bool notInherited) {
	delete m_DeviceSwitchSound;
	delete m_BodyHitSound;
	delete m_PainSound;
	delete m_DeathSound;
	delete m_AlarmSound;

	for (std::deque<MovableObject*>::const_iterator itr = m_Inventory.begin(); itr != m_Inventory.end(); ++itr) {
		delete (*itr);
	}

	if (!notInherited) {
		MOSRotating::Destroy();
	}

	Clear();
}

float Actor::GetInventoryMass() const {
	float inventoryMass = 0.0F;
	for (const MovableObject* inventoryItem: m_Inventory) {
		inventoryMass += inventoryItem->GetMass();
	}
	return inventoryMass;
}

float Actor::GetMass() const {
	return MOSRotating::GetMass() + GetInventoryMass() + (m_GoldCarried * g_SceneMan.GetKgPerOz());
}

float Actor::GetBaseMass() {
	if (m_BaseMass == std::numeric_limits<float>::infinity()) {
		if (const Actor* presetActor = static_cast<const Actor*>(GetPreset())) {
			m_BaseMass = presetActor->GetMass();
		} else {
			m_BaseMass = GetMass();
		}
	}

	return m_BaseMass;
}

bool Actor::IsPlayerControlled() const {
	return m_Controller.GetInputMode() == Controller::CIM_PLAYER && m_Controller.GetPlayer() >= 0;
}

float Actor::GetTotalValue(int nativeModule, float foreignMult, float nativeMult) const {
	float totalValue = (GetGoldValue(nativeModule, foreignMult, nativeMult) / 2) + ((GetGoldValue(nativeModule, foreignMult, nativeMult) / 2) * (GetHealth() / GetMaxHealth()));
	totalValue += GetGoldCarried();

	MOSprite* pItem = 0;
	for (std::deque<MovableObject*>::const_iterator itr = m_Inventory.begin(); itr != m_Inventory.end(); ++itr) {
		pItem = dynamic_cast<MOSprite*>(*itr);
		if (pItem)
			totalValue += pItem->GetTotalValue(nativeModule, foreignMult, nativeMult);
	}

	return totalValue;
}

bool Actor::HasObject(std::string objectName) const {
	if (MOSRotating::HasObject(objectName))
		return true;

	for (std::deque<MovableObject*>::const_iterator itr = m_Inventory.begin(); itr != m_Inventory.end(); ++itr) {
		if ((*itr) && (*itr)->HasObject(objectName))
			return true;
	}

	return false;
}

bool Actor::HasObjectInGroup(std::string groupName) const {
	if (MOSRotating::HasObjectInGroup(groupName))
		return true;

	for (std::deque<MovableObject*>::const_iterator itr = m_Inventory.begin(); itr != m_Inventory.end(); ++itr) {
		if ((*itr) && (*itr)->HasObjectInGroup(groupName))
			return true;
	}

	return false;
}

void Actor::SetTeam(int team) {
	MovableObject::SetTeam(team);

	// Change the Team Icon to display
	m_pTeamIcon = 0;
	if (g_ActivityMan.GetActivity())
		m_pTeamIcon = g_ActivityMan.GetActivity()->GetTeamIcon(m_Team);

	// Also set all actors in the inventory
	Actor* pActor = 0;
	for (std::deque<MovableObject*>::const_iterator itr = m_Inventory.begin(); itr != m_Inventory.end(); ++itr) {
		pActor = dynamic_cast<Actor*>(*itr);
		if (pActor)
			pActor->SetTeam(team);
	}
}

void Actor::SetControllerMode(Controller::InputMode newMode, int newPlayer) {

	Controller::InputMode previousControllerMode = m_Controller.GetInputMode();
	int previousControllingPlayer = m_Controller.GetPlayer();

	m_Controller.SetInputMode(newMode);
	m_Controller.SetPlayer(newPlayer);

	// Whoever had it was steering it: the route-follower's timers and any flight in hand no longer describe what the body is doing.
	if (newMode != previousControllerMode || newPlayer != previousControllingPlayer) {
		ResetRouteMovement();
	}

	RunScriptedFunctionInAppropriateScripts("OnControllerInputModeChange", false, false, {}, {std::to_string(previousControllerMode), std::to_string(previousControllingPlayer)});

	m_NewControlTmr.Reset();
}

Controller::InputMode Actor::SwapControllerModes(Controller::InputMode newMode, int newPlayer) {
	Controller::InputMode returnMode = m_Controller.GetInputMode();
	SetControllerMode(newMode, newPlayer);
	return returnMode;
}

float Actor::LookRandomNormalNum() const {
	if (m_LookRandomState == 0) {
		// SplitMix64 of the unique ID, so actors start on unrelated streams.
		uint64_t seed = static_cast<uint64_t>(GetUniqueID()) + 0x9E3779B97F4A7C15ULL;
		seed = (seed ^ (seed >> 30)) * 0xBF58476D1CE4E5B9ULL;
		seed = (seed ^ (seed >> 27)) * 0x94D049BB133111EBULL;
		m_LookRandomState = (seed ^ (seed >> 31)) | 1;
	}
	// Xorshift64*.
	m_LookRandomState ^= m_LookRandomState >> 12;
	m_LookRandomState ^= m_LookRandomState << 25;
	m_LookRandomState ^= m_LookRandomState >> 27;
	uint64_t bits = m_LookRandomState * 0x2545F4914F6CDD1DULL;
	// Top 24 bits give an exact float in [0, 1), mapped to [-1, 1).
	return static_cast<float>(bits >> 40) * (2.0F / 16777216.0F) - 1.0F;
}

bool Actor::Look(float FOVSpread, float range) {
	if (!g_SceneMan.AnythingUnseen(m_Team) || m_CanRevealUnseen == false) {
		return false;
	}

	// Use the 'eyes' on the 'head', if applicable
	Vector aimPos = GetEyePos();
	/*
	    Matrix aimMatrix(m_HFlipped ? -m_AimAngle : m_AimAngle);
	    aimMatrix.SetXFlipped(m_HFlipped);
	    // Get the langth of the look vector
	    Vector aimDistance = m_ViewPoint - aimPos;
	    // Add half the screen width
	    Vector lookVector(fabs(aimDistance.m_X) + range, 0);
	    // Set the rotation to the acutal aiming angle
	    lookVector *= aimMatrix;
	    // Add the spread
	    lookVector.DegRotate(FOVSpread * NormalRand());
	// TEST: Really need so far?
	    lookVector /= 2;
	*/
	Vector lookVector = m_Vel;
	// If there is no vel, just look in all directions
	if (lookVector.GetLargest() < 0.01) {
		lookVector.SetXY(range, 0);
		lookVector.DegRotate(180.0F * LookRandomNormalNum());
	} else {
		// Set the distance in the look direction
		lookVector.SetMagnitude(range);
		// Add the spread from the directed look
		lookVector.DegRotate(FOVSpread * LookRandomNormalNum());
	}

	// The smallest dimension of the fog block, divided by two, but always at least one, as the step for the casts
	int step = (int)g_SceneMan.GetUnseenResolution(m_Team).GetSmallest() / 2;

	// TODO: generate an alarm event if we spot an enemy actor?

	Vector ignored(0, 0);
	return g_SceneMan.CastSeeRay(m_Team, aimPos, lookVector, ignored, 25, step);
}

void Actor::AddGold(float goldOz) {
	bool isHumanTeam = g_ActivityMan.GetActivity()->IsHumanTeam(m_Team);
	if (g_SettingsMan.GetAutomaticGoldDeposit() || !isHumanTeam) {
		// TODO: Allow AI to reliably deliver gold via craft
		g_ActivityMan.GetActivity()->ChangeTeamFunds(goldOz, m_Team);
	} else {
		m_GoldCarried += goldOz;
		m_GoldPicked = true;
		if (isHumanTeam) {
			for (int player = Players::PlayerOne; player < Players::MaxPlayerCount; player++) {
				if (g_ActivityMan.GetActivity()->GetTeamOfPlayer(player) == m_Team) {
					g_GUISound.FundsChangedSound()->Play(player);
				}
			}
		}
	}
}

void Actor::RestDetection() {
	MOSRotating::RestDetection();

	if (m_Status != DEAD) {
		m_AngOscillations = 0;
		m_VelOscillations = 0;
		m_RestTimer.Reset();
		m_ToSettle = false;
	}
}

void Actor::AddAIMOWaypoint(const MovableObject* pMOWaypoint) {
	if (g_MovableMan.ValidMO(pMOWaypoint) && (m_Waypoints.empty() || m_Waypoints.back().second != pMOWaypoint)) {
		m_Waypoints.push_back(std::pair<Vector, const MovableObject*>(pMOWaypoint->GetPos(), pMOWaypoint));
		m_WaitingAtDoor = false;
		++m_AIOrderSerial;
	}
}

void Actor::AlarmPoint(const Vector& alarmPoint) {
	if (m_AlarmSound && m_AlarmTimer.IsPastSimTimeLimit()) {
		m_AlarmSound->Play(alarmPoint);
	}

	if (m_AlarmTimer.GetElapsedSimTimeMS() > 50) {
		m_AlarmTimer.Reset();
		m_LastAlarmPos = m_PointingTarget = alarmPoint;
	}
}

MovableObject* Actor::SwapNextInventory(MovableObject* pSwapIn, bool muteSound) {
	MovableObject* pRetDev = 0;
	bool playSound = false;
	if (!m_Inventory.empty()) {
		pRetDev = m_Inventory.front();
		// Reset all the timers of the object being taken out of inventory so it doesn't emit a bunch of particles that have been backed up while dormant in inventory
		pRetDev->ResetAllTimers();
		m_Inventory.pop_front();
		playSound = true;
	}
	if (pSwapIn) {
		pSwapIn->SetAsNoID();
		AddToInventoryBack(pSwapIn);
		playSound = true;
	}

	if (m_DeviceSwitchSound && playSound && !muteSound)
		m_DeviceSwitchSound->Play(m_Pos);

	return pRetDev;
}

void Actor::RemoveInventoryItem(const std::string& moduleName, const std::string& presetName) {
	for (std::deque<MovableObject*>::iterator inventoryIterator = m_Inventory.begin(); inventoryIterator != m_Inventory.end(); ++inventoryIterator) {
		if ((moduleName.empty() || (*inventoryIterator)->GetModuleName() == moduleName) && (*inventoryIterator)->GetPresetName() == presetName) {
			(*inventoryIterator)->DestroyScriptState();
			delete (*inventoryIterator);
			m_Inventory.erase(inventoryIterator);
			break;
		}
	}
}

MovableObject* Actor::RemoveInventoryItemAtIndex(int inventoryIndex) {
	if (inventoryIndex >= 0 && inventoryIndex < m_Inventory.size()) {
		MovableObject* itemAtIndex = m_Inventory[inventoryIndex];
		m_Inventory.erase(m_Inventory.begin() + inventoryIndex);
		return itemAtIndex;
	}
	return nullptr;
}

MovableObject* Actor::SwapPrevInventory(MovableObject* pSwapIn) {
	MovableObject* pRetDev = 0;
	bool playSound = false;
	if (!m_Inventory.empty()) {
		pRetDev = m_Inventory.back();
		m_Inventory.pop_back();
		playSound = true;
	}
	if (pSwapIn) {
		pSwapIn->SetAsNoID();
		AddToInventoryFront(pSwapIn);
		playSound = true;
	}

	if (m_DeviceSwitchSound && playSound)
		m_DeviceSwitchSound->Play(m_Pos);

	return pRetDev;
}

bool Actor::SwapInventoryItemsByIndex(int inventoryIndex1, int inventoryIndex2) {
	if (inventoryIndex1 < 0 || inventoryIndex2 < 0 || inventoryIndex1 >= m_Inventory.size() || inventoryIndex2 >= m_Inventory.size()) {
		return false;
	}

	std::swap(m_Inventory.at(inventoryIndex1), m_Inventory.at(inventoryIndex2));
	return true;
}

MovableObject* Actor::SetInventoryItemAtIndex(MovableObject* newInventoryItem, int inventoryIndex) {
	if (!newInventoryItem) {
		return RemoveInventoryItemAtIndex(inventoryIndex);
	}
	newInventoryItem->SetAsNoID();

	if (inventoryIndex < 0 || inventoryIndex >= m_Inventory.size()) {
		AddToInventoryBack(newInventoryItem);
		return nullptr;
	}
	MovableObject* currentInventoryItemAtIndex = m_Inventory.at(inventoryIndex);
	m_Inventory.at(inventoryIndex) = newInventoryItem;
	return currentInventoryItemAtIndex;
}

void Actor::DropAllInventory() {
	MovableObject* pObject = 0;
	Actor* pPassenger = 0;
	float velMin, velMax, angularVel;
	Vector gibROffset, gibVel;
	for (std::deque<MovableObject*>::iterator gItr = m_Inventory.begin(); gItr != m_Inventory.end(); ++gItr) {
		// Get handy handle to the object we're putting
		pObject = *gItr;
		if (pObject) {
			// Generate the velocities procedurally
			velMin = 3.0F;
			velMax = velMin + std::sqrt(m_SpriteRadius);

			// Randomize the offset from center to be within the original object
			gibROffset.SetXY(m_SpriteRadius * 0.35F * RandomNum(), 0);
			gibROffset.RadRotate(c_PI * RandomNormalNum());
			// Set up its position and velocity according to the parameters of this AEmitter.
			pObject->SetPos(m_Pos + gibROffset);
			pObject->SetRotAngle(m_Rotation.GetRadAngle() + pObject->GetRotMatrix().GetRadAngle());
			// Rotational angle
			pObject->SetAngularVel((pObject->GetAngularVel() * 0.35F) + (pObject->GetAngularVel() * 0.65F / (pObject->GetMass() != 0 ? pObject->GetMass() : 0.0001F)) * RandomNum());
			// Make it rotate away in the appropriate direction depending on which side of the object it is on
			// If the object is far to the relft or right of the center, make it always rotate outwards to some degree
			if (gibROffset.m_X > m_aSprite[0]->w / 3) {
				float offCenterRatio = gibROffset.m_X / (m_aSprite[0]->w / 2);
				angularVel = std::abs(pObject->GetAngularVel() * 0.5F);
				angularVel += std::abs(pObject->GetAngularVel() * 0.5F * offCenterRatio);
				pObject->SetAngularVel(angularVel * (gibROffset.m_X > 0.0F ? -1 : 1));
			}
			// Gib is too close to center to always make it rotate in one direction, so give it a baseline rotation and then randomize
			else {
				pObject->SetAngularVel((pObject->GetAngularVel() * RandomNum(0.5F, 1.5F)) * (RandomNum() < 0.5F ? 1.0F : -1.0F));
			}

			// TODO: Optimize making the random angles!")
			gibVel = gibROffset;
			if (gibVel.IsZero()) {
				gibVel.SetXY(RandomNum(velMin, velMax), 0.0F);
				gibVel.RadRotate(c_PI * RandomNormalNum());
			} else {
				gibVel.SetMagnitude(RandomNum(velMin, velMax));
			}
			// Distribute any impact implse out over all the gibs
			//            gibVel += (impactImpulse / m_Gibs.size()) / pObject->GetMass();
			pObject->SetVel(m_Vel + gibVel);
			// Reset all the timers of the object being shot out so it doesn't emit a bunch of particles that have been backed up while dormant in inventory
			pObject->ResetAllTimers();

			// Detect whether we're dealing with a passenger and add it as Actor instead
			if (pPassenger = dynamic_cast<Actor*>(pObject)) {
				pPassenger->SetRotAngle(c_HalfPI * RandomNormalNum());
				pPassenger->SetAngularVel(pPassenger->GetAngularVel() * 5.0F);
				pPassenger->SetHFlipped(RandomNum() > 0.5F);
				pPassenger->SetStatus(UNSTABLE);
				g_MovableMan.AddActor(pPassenger);
			}
			// Add the gib to the scene, passing ownership from the inventory
			else
				g_MovableMan.AddParticle(pObject);

			pPassenger = 0;
			pObject = 0;
		}
	}

	// We have exhausted all teh inventory into the scene, passing ownership
	m_Inventory.clear();
}

void Actor::DropAllGold() {
	const Material* goldMaterial = g_SceneMan.GetMaterialFromID(g_MaterialGold);
	float velMin = 3.0F;
	float velMax = velMin + std::sqrt(m_SpriteRadius);

	for (int i = 0; i < static_cast<int>(std::floor(m_GoldCarried)); i++) {
		Vector dropOffset(m_SpriteRadius * 0.3F * RandomNum(), 0);
		dropOffset.RadRotate(c_PI * RandomNormalNum());

		Vector dropVelocity(dropOffset);
		dropVelocity.SetMagnitude(RandomNum(velMin, velMax));

		Atom* goldMOPixelAtom = new Atom(Vector(), g_MaterialGold, nullptr, goldMaterial->GetColor(), 2);

		MOPixel* goldMOPixel = new MOPixel(goldMaterial->GetColor(), goldMaterial->GetPixelDensity(), m_Pos + dropOffset, dropVelocity, goldMOPixelAtom);
		goldMOPixel->SetToHitMOs(false);
		g_MovableMan.AddParticle(goldMOPixel);
	}
	m_GoldCarried = 0;
}

bool Actor::AddToInventoryFront(MovableObject* itemToAdd) {
	// This function is called often to add stuff we just removed from our hands, which may be set to delete so we need to guard against that lest we crash.
	if (!itemToAdd || itemToAdd->IsSetToDelete()) {
		return false;
	}

	m_Inventory.push_front(itemToAdd);
	return true;
}

bool Actor::AddToInventoryBack(MovableObject* itemToAdd) {
	// This function is called often to add stuff we just removed from our hands, which may be set to delete so we need to guard against that lest we crash.
	if (!itemToAdd || itemToAdd->IsSetToDelete()) {
		return false;
	}

	m_Inventory.push_back(itemToAdd);
	return true;
}

void Actor::GibThis(const Vector& impactImpulse, MovableObject* movableObjectToIgnore) {
	// Play death sound
	// TODO: Don't attenuate since death is pretty important.. maybe only make this happen for teh brains
	if (m_DeathSound) {
		m_DeathSound->Play(m_Pos);
	}

	// Gib all the regular gibs
	MOSRotating::GibThis(impactImpulse, movableObjectToIgnore);

	// Throw out all the inventory with the appropriate force and directions
	MovableObject* pObject = 0;
	Actor* pPassenger = 0;
	float velMin, velRange, angularVel;
	Vector gibROffset, gibVel;
	for (std::deque<MovableObject*>::iterator gItr = m_Inventory.begin(); gItr != m_Inventory.end(); ++gItr) {
		// Get handy handle to the object we're putting
		pObject = *gItr;

		// Generate the velocities procedurally
		velMin = m_GibBlastStrength / (pObject->GetMass() != 0 ? pObject->GetMass() : 0.0001F);
		velRange = 10.0F;

		// Randomize the offset from center to be within the original object
		gibROffset.SetXY(m_SpriteRadius * 0.35F * RandomNormalNum(), m_SpriteRadius * 0.35F * RandomNormalNum());
		// Set up its position and velocity according to the parameters of this AEmitter.
		pObject->SetPos(m_Pos + gibROffset /*Vector(m_Pos.m_X + 5 * NormalRand(), m_Pos.m_Y + 5 * NormalRand())*/);
		pObject->SetRotAngle(m_Rotation.GetRadAngle() + pObject->GetRotMatrix().GetRadAngle());
		// Rotational angle
		pObject->SetAngularVel((pObject->GetAngularVel() * 0.35F) + (pObject->GetAngularVel() * 0.65F / (pObject->GetMass() != 0 ? pObject->GetMass() : 0.0001F)) * RandomNum());
		// Make it rotate away in the appropriate direction depending on which side of the object it is on
		// If the object is far to the relft or right of the center, make it always rotate outwards to some degree
		if (gibROffset.m_X > m_aSprite[0]->w / 3) {
			float offCenterRatio = gibROffset.m_X / (m_aSprite[0]->w / 2);
			angularVel = fabs(pObject->GetAngularVel() * 0.5F);
			angularVel += fabs(pObject->GetAngularVel() * 0.5F * offCenterRatio);
			pObject->SetAngularVel(angularVel * (gibROffset.m_X > 0 ? -1 : 1));
		}
		// Gib is too close to center to always make it rotate in one direction, so give it a baseline rotation and then randomize
		else {
			pObject->SetAngularVel((pObject->GetAngularVel() * 0.5F + pObject->GetAngularVel() * RandomNum()) * (RandomNormalNum() > 0.0F ? 1.0F : -1.0F));
		}

		// TODO: Optimize making the random angles!")
		gibVel = gibROffset;
		if (gibVel.IsZero())
			gibVel.SetXY(velMin + RandomNum(0.0F, velRange), 0.0F);
		else
			gibVel.SetMagnitude(velMin + RandomNum(0.0F, velRange));
		gibVel.RadRotate(impactImpulse.GetAbsRadAngle());
		// Don't! the offset was already rotated!
		//            gibVel = RotateOffset(gibVel);
		// Distribute any impact implse out over all the gibs
		//            gibVel += (impactImpulse / m_Gibs.size()) / pObject->GetMass();
		pObject->SetVel(m_Vel + gibVel);
		// Reset all the timers of the object being shot out so it doesn't emit a bunch of particles that have been backed up while dormant in inventory
		pObject->ResetAllTimers();

		// Set the gib to not hit a specific MO
		if (movableObjectToIgnore)
			pObject->SetWhichMOToNotHit(movableObjectToIgnore);

		// Detect whether we're dealing with a passenger and add it as Actor instead
		if (pPassenger = dynamic_cast<Actor*>(pObject)) {
			pPassenger->SetRotAngle(c_HalfPI * RandomNormalNum());
			pPassenger->SetAngularVel(pPassenger->GetAngularVel() * 5.0F);
			pPassenger->SetHFlipped(RandomNum() > 0.5F);
			pPassenger->SetStatus(UNSTABLE);
			g_MovableMan.AddActor(pPassenger);
		}
		// Add the gib to the scene, passing ownership from the inventory
		else
			g_MovableMan.AddParticle(pObject);

		pPassenger = 0;
		pObject = 0;
	}

	// We have exhausted all teh inventory into the scene, passing ownership
	m_Inventory.clear();

	// If this is the actual brain of any player, flash that player's screen when he's now dead
	if (g_SettingsMan.FlashOnBrainDamage() && g_ActivityMan.IsInActivity()) {
		int brainOfPlayer = g_ActivityMan.GetActivity()->IsBrainOfWhichPlayer(this);
		// Only flash if player is human (AI players don't have screens!)
		if (brainOfPlayer != Players::NoPlayer && g_ActivityMan.GetActivity()->PlayerHuman(brainOfPlayer)) {
			// Croaked.. flash for a longer period
			if (m_ToDelete || m_Status == DEAD)
				g_FrameMan.FlashScreen(g_ActivityMan.GetActivity()->ScreenOfPlayer(brainOfPlayer), g_WhiteColor, 500);
		}
	}
}

bool Actor::ParticlePenetration(HitData& hd) {
	bool penetrated = MOSRotating::ParticlePenetration(hd);

	MovableObject* hitor = hd.Body[HITOR];
	float damageToAdd = hitor->DamageOnCollision();
	damageToAdd += penetrated ? hitor->DamageOnPenetration() : 0;
	if (hitor->GetApplyWoundDamageOnCollision()) {
		damageToAdd += m_pEntryWound->GetEmitDamage() * hitor->WoundDamageMultiplier();
	}
	if (hitor->GetApplyWoundBurstDamageOnCollision()) {
		damageToAdd += m_pEntryWound->GetBurstDamage() * hitor->WoundDamageMultiplier();
	}

	if (damageToAdd != 0) {
		m_Health = std::min(m_Health - (damageToAdd * m_DamageMultiplier), m_MaxHealth);
	}
	if ((penetrated || damageToAdd != 0) && m_Perceptiveness > 0 && m_Health > 0) {
		Vector extruded(hd.HitVel[HITOR]);
		extruded.SetMagnitude(m_CharHeight);
		extruded = m_Pos - extruded;
		g_SceneMan.WrapPosition(extruded);
		AlarmPoint(extruded);
	}

	return penetrated;
}

BITMAP* Actor::GetAIModeIcon() {
	return m_apAIIcons[m_AIMode];
}

MOID Actor::GetAIMOWaypointID() const {
	if (g_MovableMan.ValidMO(m_pMOMoveTarget))
		return m_pMOMoveTarget->GetID();
	else
		return g_NoMOID;
}

void Actor::UpdateMovePath() {
	if (g_SceneMan.GetScene() == nullptr) {
		return;
	}
	// After an impossible answer, a few seconds for the unit to be somewhere else before asking again.
	if (m_ImpossiblePaths > 0 && !m_PathRetryTimer.IsPastSimMS(3000)) {
		return;
	}
	// At a door that's in the way, the route is asked for again now and then, not every frame: when the door opens or is shot out, the
	// grid takes the change and the next answer goes through.
	if (m_WaitingAtDoor && !m_PathRetryTimer.IsPastSimMS(1500)) {
		return;
	}

	// What this actor is to the path grid: what it can jump, dig and breach, and how big it is.
	PathAgent agent = GetPathAgent();

	// A place to go to is taken to be on the ground under it: a point in the air can only be reached by a jump from the node straight below, so a waypoint
	// a little above the ground, or past the edge of what it was over, had no path at all and the unit flew for it blind.
	// (Sampled every 3 px: at 10 a thin floor could be stepped over by the search, which then put the target on the next floor down. And
	// from inside the scene: a point over the top edge was being bounded to somewhere the search didn't see the top floor from.)
	// A target inside the ground stays where it is: it's a place to dig to, and "the ground under it" is itself, so the search was lifting
	// it a fifth of a height every time the path was asked for again, and the digger never got there.
	auto onGround = [this](const Vector& place) {
		Vector inScene(place.m_X, std::max(1.0F, place.m_Y));
		if (g_SceneMan.GetTerrMatter(static_cast<int>(inScene.m_X), static_cast<int>(inScene.m_Y)) != MaterialColorKeys::g_MaterialAir) {
			return inScene;
		}
		return g_SceneMan.MovePointToGround(inScene, m_CharHeight * 0.2F, 3);
	};
	// The start is on the ground too, but not when that is far below: a unit part way up a jetpack climb, or just dropped from a ship, would be given
	// a route that begins at the bottom and heads down for it. The ground has to be near for the start to be moved to it.
	// (And not when the unit is off the ground at all, a quarter of a body over where it would stand: a route asked for in the air, by a
	// re-path or a route check part way through a jump, started at the floor below and behind the unit, so its first point turned the unit
	// back and dropped it there, undoing the jump. From where the unit is, the route goes on from there.)
	Vector start = onGround(m_Pos);
	if (start.m_Y - m_Pos.m_Y > m_CharHeight * 0.25F) {
		start = m_Pos;
	}

	// If we're following someone/thing, then never advance waypoints until that thing disappears
	if (g_MovableMan.ValidMO(m_pMOMoveTarget)) {
		m_HasMovePathGoal = false;
		m_PathRequest = g_SceneMan.GetScene()->CalculatePathAsync(start, m_pMOMoveTarget->GetPos(), agent, static_cast<Activity::Teams>(m_Team));
	} else {
		// The place the route is asked for to is kept (m_MovePathGoal): a route cut short at an obstacle ends short of it, and taken from the
		// route's last point, as it used to be, the next request went to the cut and the unit "arrived" there, at the foot of a hatch its
		// own team's door had been erased from the grid a moment too late to pass.
		// Do we currently have a path to a static target we would like to still pursue?
		if (m_MovePath.empty()) {
			// Ok no path going, so get a new path to the next waypoint, if there is a next waypoint
			if (!m_Waypoints.empty()) {
				// Make sure the path starts from the ground and not somewhere up in the air if/when dropped out of ship
				m_MovePathGoal = onGround(m_Waypoints.front().first);
				m_HasMovePathGoal = true;
				m_PathRequest = g_SceneMan.GetScene()->CalculatePathAsync(start, m_MovePathGoal, agent, static_cast<Activity::Teams>(m_Team));

				// If the waypoint was tied to an MO to pursue, then load it into the current MO target
				if (g_MovableMan.ValidMO(m_Waypoints.front().second)) {
					m_pMOMoveTarget = m_Waypoints.front().second;
					m_HasMovePathGoal = false;
				} else {
					m_pMOMoveTarget = 0;
				}

				// We loaded the waypoint, no need to keep it
				m_Waypoints.pop_front();
			}
			// Just try to get to the place we were going, else the last Move Target
			else {
				if (!m_HasMovePathGoal) {
					m_MovePathGoal = onGround(m_MoveTarget);
					m_HasMovePathGoal = true;
				}
				m_PathRequest = g_SceneMan.GetScene()->CalculatePathAsync(start, m_MovePathGoal, agent, static_cast<Activity::Teams>(m_Team));
			}
		}
		// We had a path before trying to update, so go on to the place it was for (or, with none kept, its last point).
		else {
			if (!m_HasMovePathGoal) {
				m_MovePathGoal = onGround(Vector(m_MovePath.back()));
				m_HasMovePathGoal = true;
			}
			m_PathRequest = g_SceneMan.GetScene()->CalculatePathAsync(start, m_MovePathGoal, agent, static_cast<Activity::Teams>(m_Team));
		}
	}

	m_UpdateMovePath = false;
}

float Actor::EstimateDigStrength() const {
	return m_AIBaseDigStrength;
}

float Actor::GetMaxSafeFallHeight() const {
	float gravity = g_SceneMan.GetGlobalAcc().m_Y;
	if (gravity <= 0.01F || m_TravelImpulseDamage <= 0.0F) {
		return FLT_MAX;
	}
	// The impact is the mass times the speed lost on landing (AtomGroup::Travel's collision impulses), so the speed that reaches the threshold
	// is the threshold over the mass; the height that speed is reached from is v^2 / 2g.
	float speed = m_TravelImpulseDamage / std::max(GetMass(), 1.0F);
	return std::max(speed * speed / (2.0F * gravity) * c_PPM, 96.0F);
}

int Actor::GetLiquidDepth() const {
	return ActorWater::GetDepth(this);
}

float Actor::GetAirLeft() const {
	return ActorWater::GetAir(this);
}

bool Actor::IsFloater() const {
	return ActorWater::IsFloater(this);
}

PathAgent Actor::GetPathAgent() const {
	PathAgent agent;
	agent.JumpHeight = EstimateJumpHeight();
	// With no jet to brake a fall (less than a node's lift), falls higher than the body lands from unhurt are not routed (LM-9).
	if (agent.JumpHeight != FLT_MAX && agent.JumpHeight * c_PPM < 24.0F) {
		agent.MaxSafeFall = GetMaxSafeFallHeight();
	}
	agent.DigStrength = EstimateDigStrength();
	agent.BreachStrength = EstimateBreachStrength();
	agent.Velocity = m_Vel;
	// In liquid (LM-4): whether it floats and swims, how long it holds its breath, and whether lava is any danger to it, as ActorWater and
	// ActorFire have it (with them off, water is only waded and lava harms nothing). What doesn't breathe isn't flesh, and doesn't burn.
	bool waterActs = ActorWater::IsEnabled() && FluidSim::IsEnabled();
	agent.Floats = waterActs && IsFloater();
	agent.BreathSeconds = waterActs ? ActorWater::GetBreathSeconds(this) : FLT_MAX;
	agent.CrossesLava = !ActorFire::IsEnabled() || ActorWater::GetBreathSeconds(this) == FLT_MAX;
	// CharHeight is about twice the sprite's height; the body stands about 0.45 of it tall and lies about a quarter of it.
	agent.StandHeight = std::max(16.0F, m_CharHeight * 0.42F);
	agent.CrawlHeight = agent.StandHeight;
	// (Half the sprite's reach, near enough; at a third of it a soldier was sent down a shaft its own width, and stuck there.)
	agent.HalfWidth = std::clamp(GetRadius() * 0.5F, 8.0F, 16.0F);
	for (const std::pair<Vector, double>& avoid: m_AvoidPoints) {
		if (avoid.second > g_TimerMan.GetSimTimeMS()) {
			agent.Avoid.push_back(avoid.first);
		}
	}
	// And the places and flights the team has failed at lately (see AvoidPathPoint and AvoidPathLink).
	for (const FailedLink& link: m_AvoidLinks) {
		if (link.until > g_TimerMan.GetSimTimeMS()) {
			agent.AvoidLinks.emplace_back(link.from, link.to);
		}
	}
	if (Scene* scene = g_SceneMan.GetScene(); scene && m_Team >= Activity::TeamOne && m_Team < Activity::MaxTeamCount) {
		scene->GetPathFinder(static_cast<Activity::Teams>(m_Team)).GetTeamAvoid(agent.Avoid, g_TimerMan.GetSimTimeMS());
		scene->GetPathFinder(static_cast<Activity::Teams>(m_Team)).GetTeamAvoidLinks(agent.AvoidLinks, g_TimerMan.GetSimTimeMS());
	}
	return agent;
}

float Actor::EstimateJumpHeight() const {
	// Sentinel value that is explicitly checked for within pathfinder code.
	return FLT_MAX;
}

void Actor::VerifyMOIDs() {
	std::vector<MOID> MOIDs;
	GetMOIDs(MOIDs);

	for (std::vector<MOID>::iterator it = MOIDs.begin(); it != MOIDs.end(); it++) {
		RTEAssert(*it == g_NoMOID || *it < g_MovableMan.GetMOIDCount(), "Invalid MOID in actor");
	}
}

void Actor::SetPieMenu(PieMenu* newPieMenu) {
	m_PieMenu = std::unique_ptr<PieMenu>(newPieMenu);
	m_PieMenu->Create(this);
	m_PieMenu->AddWhilePieMenuOpenListener(this, std::bind(&Actor::WhilePieMenuOpenListener, this, m_PieMenu.get()));
}

void Actor::OnNewMovePath() {
	auto popFront = [this]() {
		m_MovePath.pop_front();
		if (!m_MovePathKinds.empty()) {
			m_MovePathKinds.pop_front();
		}
	};
	if (!m_MovePath.empty()) {
		// Remove the first one; it's our position (the path's kinds are one per point after it, so they stay)
		m_PrevPathTarget = m_MovePath.front();
		m_MovePath.pop_front();
		// Also remove the one after that; it may move in opposite direction since it heads to the nearest PathNode center
		// Unless it is the last one, in which case it shouldn't be removed
		if (m_MovePath.size() > 1) {
			m_PrevPathTarget = m_MovePath.front();
			popFront();
		}
	} else if (m_pMOMoveTarget && g_MovableMan.ValidMO(m_pMOMoveTarget)) {
		m_MoveTarget = m_pMOMoveTarget->GetPos();
	} else {
		// The route was computed asynchronously, so the MO we were following may have been deleted since the request was made
		m_pMOMoveTarget = nullptr;
		// Nowhere to gooooo
		m_MoveTarget = m_PrevPathTarget = m_Pos;
	}

	// Smash all non-airborne waypoints down to just above the ground, so they more accurately represent the ground path
	std::list<Vector>::iterator finalItr = m_MovePath.end();
	--finalItr;
	for (std::list<Vector>::iterator lItr = m_MovePath.begin(); lItr != finalItr; ++lItr) {
		(*lItr) = g_SceneMan.MovePointToGround((*lItr), m_CharHeight * 0.2, 0, g_SettingsMan.GetPathFinderGridNodeSize() * 2.5f);
	}
}

void Actor::AvoidPathPoint(const Vector& place, float milliseconds) {
	double now = g_TimerMan.GetSimTimeMS();
	std::erase_if(m_AvoidPoints, [now](const std::pair<Vector, double>& avoid) { return avoid.second <= now; });
	m_AvoidPoints.emplace_back(place, now + static_cast<double>(milliseconds));
	// And for the whole team, for half as long: the next unit to come that way pays for the place too, rather than finding out the same way.
	if (Scene* scene = g_SceneMan.GetScene(); scene && m_Team >= Activity::TeamOne && m_Team < Activity::MaxTeamCount) {
		scene->GetPathFinder(static_cast<Activity::Teams>(m_Team)).AddTeamAvoid(place, now + static_cast<double>(milliseconds) * 0.5, now);
	}
}

bool Actor::IsAITraced(SettingsMan::DebugChannel channel) const {
	return g_SettingsMan.DebugChannelOn(channel) && (g_SettingsMan.TraceAllUnits() || IsDebugInspected());
}

void Actor::AvoidPathLink(const Vector& from, const Vector& to, float milliseconds) {
	double now = g_TimerMan.GetSimTimeMS();
	std::erase_if(m_AvoidLinks, [now](const FailedLink& link) { return link.until <= now; });
	m_AvoidLinks.push_back({from, to, now + static_cast<double>(milliseconds)});
	if (Scene* scene = g_SceneMan.GetScene(); scene && m_Team >= Activity::TeamOne && m_Team < Activity::MaxTeamCount) {
		scene->GetPathFinder(static_cast<Activity::Teams>(m_Team)).AddTeamAvoidLink(from, to, now + static_cast<double>(milliseconds) * 0.5, now);
	}
}

bool Actor::BodyFitsShifted(const Vector& shift, MOSRotating* head) const {
	if (!m_pAtomGroup || !m_pAtomGroup->FitsAt(m_Pos + shift)) {
		return false;
	}
	return !head || !head->GetAtomGroup() || head->GetAtomGroup()->FitsAt(head->GetPos() + shift);
}

namespace {
	/// Ground to hold or stand on: terrain that is neither air nor liquid. (Liquid is no lip: a swimmer pressing towards a bank caught the
	/// water's own surface and was pulled up onto it.)
	bool IsGroundAt(int x, int y) {
		unsigned char id = g_SceneMan.GetTerrMatter(x, y);
		return id != MaterialColorKeys::g_MaterialAir && !FluidSim::IsLiquid(id);
	}
} // namespace

bool Actor::TryStartMantle(MOSRotating* head, bool rising, float bodyWidth) {
	if (m_Mantling || !g_SettingsMan.MantlingEnabled() || !m_pAtomGroup || m_Status == INACTIVE || m_Status == DYING || m_Status == DEAD || m_PinStrength > 0.0F) {
		return false;
	}
	bool left = m_Controller.IsState(MOVE_LEFT);
	bool right = m_Controller.IsState(MOVE_RIGHT);
	if (left == right) {
		return false;
	}
	float dir = right ? 1.0F : -1.0F;
	// Only against something: free to move on, there's nothing to mantle.
	if (BodyFitsShifted(Vector(dir * 3.0F, 0.0F), head)) {
		return false;
	}
	float height = std::max(m_CharHeight, 20.0F);
	// Nor against a slope or a bump the walk goes up: free to move on with the body a little higher (a step's worth over 3 px), the legs
	// take it. (Anything at all ahead blocked the shifted body, the uphill ground of any incline included, so units pulled themselves up
	// every hill in a string of mantles instead of walking it.)
	const int walkStep = static_cast<int>(std::max(6.0F, height * 0.08F));
	for (int lift = 2; lift <= walkStep; lift += 2) {
		if (BodyFitsShifted(Vector(dir * 3.0F, static_cast<float>(-lift)), head)) {
			return false;
		}
	}
	// Not for an AI whose route goes down from here: pressing towards the wall of a hatch it was dropping through, a unit was pulled back
	// up onto the ledge beside it, walked back to the hole, and did it again for twenty seconds.
	if (!m_Controller.IsPlayerControlled() && !m_MovePath.empty() && g_SceneMan.ShortestDistance(m_Pos, m_MovePath.front()).m_Y > height * 0.25F) {
		return false;
	}
	int maxLift = static_cast<int>(height * (rising ? 0.55F : 0.3F));
	// Far enough over the edge for the body's middle to be over the top.
	float over = std::max(8.0F, bodyWidth * 0.6F) + 4.0F;
	for (int lift = 4; lift <= maxLift; lift += 2) {
		Vector up(0.0F, static_cast<float>(-lift));
		Vector end(dir * over, static_cast<float>(-lift));
		// Room all the way: straight up, then across.
		if (!BodyFitsShifted(up, head) || !BodyFitsShifted(Vector(0.0F, static_cast<float>(-lift / 2)), head) || !BodyFitsShifted(end, head) || !BodyFitsShifted(Vector(dir * over * 0.5F, static_cast<float>(-lift)), head)) {
			continue;
		}
		// Something to stand on there: ground within the legs' reach under the body's middle.
		Vector target = m_Pos + end;
		bool supported = false;
		for (int down = 0; down <= static_cast<int>(height * 0.45F) && !supported; down += 2) {
			supported = IsGroundAt(static_cast<int>(target.m_X), static_cast<int>(target.m_Y) + down);
		}
		if (!supported) {
			continue;
		}
		m_Mantling = true;
		m_MantleDir = dir;
		m_MantleStart = m_Pos;
		m_MantleUp = m_Pos + up;
		m_MantleEnd = target;
		// The lip the hands go for: between the start and the end, at the ledge's floor.
		m_MantleLip = Vector((m_Pos.m_X + target.m_X) * 0.5F, target.m_Y + height * 0.2F);
		m_MantleProgress = 0.0F;
		// Quicker for a small step than a full pull-up: about 0.4 s for the highest, so the pull reads as one.
		m_MantleDurationMS = 160.0F + static_cast<float>(lift) * 5.0F;
		m_MantleTimer.Reset();
		return true;
	}
	return false;
}

bool Actor::TryCatchLedge(MOSRotating* head, float bodyWidth, float wantDir, float lipNearY) {
	if (m_Mantling || !g_SettingsMan.MantlingEnabled() || !m_pAtomGroup || m_Status == INACTIVE || m_Status == DYING || m_Status == DEAD || m_PinStrength > 0.0F) {
		return false;
	}
	float dir = wantDir;
	if (dir == 0.0F) {
		bool left = m_Controller.IsState(MOVE_LEFT);
		bool right = m_Controller.IsState(MOVE_RIGHT);
		if (left == right) {
			return false;
		}
		dir = right ? 1.0F : -1.0F;
	}
	// In the air (free to drop a little), and slow enough for the hands to hold: falling past a lip from up to a storey or so, or at the
	// top of a rise.
	if (std::abs(m_Vel.m_Y) > 7.0F || !BodyFitsShifted(Vector(0.0F, 3.0F), head)) {
		return false;
	}
	float height = std::max(m_CharHeight, 20.0F);
	// (Not for an AI whose route goes down from here, as for the mantle: a unit dropping down a shaft caught every lip on the way.)
	if (!m_Controller.IsPlayerControlled() && !m_MovePath.empty() && g_SceneMan.ShortestDistance(m_Pos, m_MovePath.front()).m_Y > height * 0.25F) {
		return false;
	}
	// The lip: the top of the ground just beside the body, within the hands' reach, from a little over the head down to the waist, with air
	// over it up to there (ground all the way up is a wall, not a lip).
	int handX = static_cast<int>(m_Pos.m_X + dir * (bodyWidth * 0.5F + 4.0F));
	int fromY = static_cast<int>(m_Pos.m_Y - height * 0.45F);
	int toY = static_cast<int>(m_Pos.m_Y + height * 0.1F);
	int lipY = -1;
	for (int y = fromY; y <= toY; ++y) {
		if (IsGroundAt(handX, y)) {
			lipY = y;
			break;
		}
	}
	if (lipY <= fromY || (lipNearY >= 0.0F && std::abs(static_cast<float>(lipY) - lipNearY) > height * 0.33F)) {
		return false;
	}
	// Where the body ends: over the lip and a body's width onto it, standing on it, with room all the way (up to the lip's height, then
	// across) and ground under it there.
	float over = bodyWidth * 0.5F + 4.0F + std::max(8.0F, bodyWidth * 0.6F);
	Vector end(dir * over, static_cast<float>(lipY) - height * 0.4F - m_Pos.m_Y);
	Vector up(0.0F, std::min(end.m_Y, 0.0F));
	if (!BodyFitsShifted(up, head) || !BodyFitsShifted(Vector(end.m_X * 0.5F, up.m_Y), head) || !BodyFitsShifted(end, head)) {
		return false;
	}
	Vector target = m_Pos + end;
	bool supported = false;
	for (int down = 0; down <= static_cast<int>(height * 0.45F) && !supported; down += 2) {
		supported = IsGroundAt(static_cast<int>(target.m_X), static_cast<int>(target.m_Y) + down);
	}
	if (!supported) {
		return false;
	}
	// Caught: from the hang, the mantle's pull up and over (UpdateMantle puts the body where it should be each frame, so the fall stops at
	// once), a little slower than a mantle from the ground, the moment of the hang in it.
	m_Mantling = true;
	m_MantleDir = dir;
	m_MantleStart = m_Pos;
	m_MantleUp = m_Pos + up;
	m_MantleEnd = target;
	m_MantleLip = Vector(static_cast<float>(handX), static_cast<float>(lipY));
	m_MantleProgress = 0.0F;
	m_MantleDurationMS = 300.0F + std::abs(up.m_Y) * 5.0F;
	m_MantleTimer.Reset();
	m_Vel.Reset();
	if (IsAITraced()) {
		g_ConsoleMan.PrintString("AITRACE caught the ledge at " + std::to_string(handX) + "," + std::to_string(lipY));
	}
	return true;
}

Vector Actor::GetMantleLip() const {
	// Kept unwrapped, like the other key points; given on the body's side of the seam, where its arms are.
	return m_Pos + g_SceneMan.ShortestDistance(m_Pos, m_MantleLip);
}

void Actor::UpdateMantle() {
	if (!m_Mantling) {
		return;
	}
	if (m_Status == DYING || m_Status == DEAD || m_Status == INACTIVE) {
		m_Mantling = false;
		return;
	}
	// Up for the first half, across for the second; the body is put where it should be each frame (gravity and the jet are nothing
	// while the arms pull), and its speed is what that movement is, so the limbs animate with it.
	float progress = std::clamp(static_cast<float>(m_MantleTimer.GetElapsedSimTimeMS()) / std::max(m_MantleDurationMS, 1.0F), 0.0F, 1.0F);
	m_MantleProgress = progress;
	// One curve from the start up and over onto the ledge (a quadratic through the corner), eased in and out: up a line and across a
	// line at a constant rate, as it was, the body cornered like a lift.
	float eased = progress * progress * (3.0F - 2.0F * progress);
	float a = (1.0F - eased) * (1.0F - eased);
	float b = 2.0F * (1.0F - eased) * eased;
	float c = eased * eased;
	Vector target = m_MantleStart * a + m_MantleUp * b + m_MantleEnd * c;
	float deltaTime = std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F);
	// The key points are unwrapped (a ledge just past the X seam has its end past the scene's edge) and Travel wraps m_Pos every update, so the
	// step is taken the short way and the position wrapped again: from the wrapped position to the unwrapped point the step was a scene width,
	// a speed of thousands of m/s that fired the body through the terrain for a frame, with the impact damage that goes with it.
	Vector step = g_SceneMan.ShortestDistance(m_Pos, target);
	m_Vel = step * (c_MPP / deltaTime);
	m_Pos += step;
	g_SceneMan.WrapPosition(m_Pos);
	m_AngularVel = 0.0F;
	if (progress >= 1.0F) {
		m_Mantling = false;
		// On it walking, the way it was going: a vault over a low obstacle is this and the walk off the far side.
		m_Vel.SetXY(m_MantleDir * 1.5F, 0.0F);
	}
}

void Actor::RequestRouteCheck() {
	if (m_PathRequest || m_UpdateMovePath || m_MovePath.empty()) {
		return;
	}
	m_RouteCheck = true;
	UpdateMovePath();
	if (!m_PathRequest) {
		m_RouteCheck = false; // (Nothing was asked: a retry wait is on.)
	}
}

void Actor::PreControllerUpdate() {
	// A route check's answer is taken only when the goal is reachable from here; otherwise the route being followed is kept, as it was.
	if (m_PathRequest && m_PathRequest->complete && m_RouteCheck) {
		m_RouteCheck = false;
		bool reachable = m_PathRequest->status == micropather::MicroPather::SOLVED && m_PathRequest->totalCost <= 100000.0F && !const_cast<std::list<Vector>&>(m_PathRequest->path).empty();
		if (!reachable && !m_MovePath.empty()) {
			if (IsAITraced()) {
				g_ConsoleMan.PrintString("AITRACE route check: not reachable from " + std::to_string(static_cast<int>(m_Pos.m_X)) + "," + std::to_string(static_cast<int>(m_Pos.m_Y)) + ", keeping the route");
			}
			m_PathRequest.reset();
		} else if (reachable && !m_MovePath.empty() && m_Vel.MagnitudeIsGreaterThan(2.0F)) {
			// Nor when it turns the unit back against the way it is going at speed, unless it is clearly cheaper than what is left of the route
			// it has: a check from mid-air, which knows nothing of momentum, sent units round in the air for a route no better than their own.
			const std::list<Vector>& answer = const_cast<std::list<Vector>&>(m_PathRequest->path);
			auto second = std::next(answer.begin());
			Vector heading = g_SceneMan.ShortestDistance(m_Pos, second != answer.end() ? *second : answer.front());
			// (Only while the route it has still goes the way it is moving: when its next point is behind too, the unit has overshot, and the
			// answer, which turns it back, is the one to take. Held to the old route there, a unit that had overshot flew on away from both.)
			Vector oldHeading = g_SceneMan.ShortestDistance(m_Pos, m_MovePath.front());
			bool oldGoesOn = oldHeading.Dot(m_Vel) > 0.0F;
			if (oldGoesOn && heading.MagnitudeIsGreaterThan(1.0F) && heading.Dot(m_Vel) < 0.0F && m_PathCostAtAdoption > 0.0F && m_PathSizeAtAdoption > 0) {
				float left = m_PathCostAtAdoption * static_cast<float>(m_MovePath.size()) / static_cast<float>(m_PathSizeAtAdoption);
				if (m_PathRequest->totalCost > left * 0.8F) {
					if (IsAITraced()) {
						g_ConsoleMan.PrintString("AITRACE route check: turns back at speed for " + std::to_string(m_PathRequest->totalCost) + " against " + std::to_string(left) + " left, keeping the route");
					}
					m_PathRequest.reset();
				}
			}
		}
	}
	if (m_PathRequest && m_PathRequest->complete) {
		m_MovePath = const_cast<std::list<Vector>&>(m_PathRequest->path);
		m_PathCostAtAdoption = m_PathRequest->totalCost;
		m_PathSizeAtAdoption = static_cast<int>(m_MovePath.size());
		m_MovePathKinds = const_cast<std::list<PathStepKind>&>(m_PathRequest->kinds);
		if (IsAITraced()) {
			g_ConsoleMan.PrintString("AITRACE path for " + GetPresetName() + ": status " + std::to_string(m_PathRequest->status) + ", " + std::to_string(m_MovePath.size()) + " nodes, cost " + std::to_string(m_PathRequest->totalCost) + ", from " +
			                         std::to_string(static_cast<int>(m_PathRequest->startPos.m_X)) + "," + std::to_string(static_cast<int>(m_PathRequest->startPos.m_Y)) + " to " + std::to_string(static_cast<int>(m_PathRequest->targetPos.m_X)) + "," + std::to_string(static_cast<int>(m_PathRequest->targetPos.m_Y)));
			// (Each point after the first with the kind of the step that reaches it.)
			std::string nodes;
			auto kind = m_MovePathKinds.begin();
			int count = 0;
			for (const Vector& point: m_MovePath) {
				if (++count > 60) {
					break;
				}
				nodes += " " + std::to_string(static_cast<int>(point.m_X)) + "," + std::to_string(static_cast<int>(point.m_Y));
				if (count > 1 && kind != m_MovePathKinds.end()) {
					nodes += "(" + std::to_string(static_cast<int>(*kind)) + ")";
					++kind;
				}
			}
			g_ConsoleMan.PrintString("AITRACE nodes:" + nodes);
		}
		// A route that only exists through ground the unit can't dig is no route. One such answer is usually down to where the unit is standing
		// (wedged under a ledge, pressed into a bunker wall), so the route isn't followed, the unit is left to the stuck handling for a few seconds,
		// and then it asks again from wherever that has got it. Only when the answer keeps coming back the same is it left with nothing to follow,
		// and the AI stands down rather than pushing at the wall for ever.
		// (The pathfinder cuts such a route short at the obstacle; while there is still a way to go along it, it's followed, and the counting
		// starts once the unit is there.)
		// (Not when it's a door that's in the way: a door opens for its own side and is shot open by the other, so the unit goes to it and
		// waits, keeping its goal. Counted as a dead end, the six answers it got while the door was being shot at stood it down, and it never
		// went through the doorway once the door was gone.)
		bool cutAtDoor = m_PathRequest->cutAtDoor;
		m_WaitingAtDoor = cutAtDoor && m_MovePath.size() <= 3;
		if (m_WaitingAtDoor) {
			m_PathRetryTimer.Reset();
		}
		bool impossible = !cutAtDoor && m_PathRequest->status == micropather::MicroPather::SOLVED && m_PathRequest->totalCost > 100000.0F && EstimateDigStrength() <= c_PathFindingDefaultDigStrength + 1.0F && m_MovePath.size() <= 3;
		m_ImpossiblePaths = impossible ? m_ImpossiblePaths + 1 : 0;
		// For the path display: a route with no way there, or one that only gets there through ground this unit can't dig, is shown in red.
		m_PathImpossible = m_PathRequest->status != micropather::MicroPather::SOLVED || (!cutAtDoor && m_PathRequest->totalCost > 100000.0F && EstimateDigStrength() <= c_PathFindingDefaultDigStrength + 1.0F);
		if (impossible) {
			m_PathRetryTimer.Reset();
			m_MovePath.clear();
			m_MovePathKinds.clear();
			if (m_ImpossiblePaths >= 6) {
				m_ImpossiblePaths = 0;
				m_Waypoints.clear();
				// The stand-down: with the goal kept, GetLastAIWaypoint never says "here", and the unit asks for the same route for ever.
				m_HasMovePathGoal = false;
				m_MoveTarget = m_Pos;
			}
			m_PathRequest.reset();
			return;
		}
		m_PathRequest.reset();
		OnNewMovePath();
	}

	// We update this after, because pathing requests are forced to take at least 1 frame for the sake of determinism for now.
	// In future maybe we can move this back, but it doesn't make much difference
	if (m_UpdateMovePath) {
		UpdateMovePath();
	}
}

float Actor::GetNightAmount() {
	glm::vec3 daylight = SceneLighting::GetDaylightTint(g_PostProcessMan.GetLightingSettings().TimeOfDay);
	float dayFactor = glm::dot(daylight, glm::vec3(0.2126F, 0.7152F, 0.0722F));
	return std::clamp((0.45F - dayFactor) / 0.35F, 0.0F, 1.0F);
}

float Actor::GetNightSightScale() const {
	const LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
	// Blown dust hides things day or night.
	float weather = WeatherEffects::GetSightMultiplier();
	if (!settings.NightAffectsAI || !settings.Enabled) {
		return weather;
	}
	float night = GetNightAmount();
	// A headlamp keeps most of the view; without one, eyes only reach about half as far in the dark.
	float floor = (settings.Headlamps && !IsDead()) ? 0.8F : 0.5F;
	return (1.0F - night * (1.0F - floor)) * weather;
}

std::vector<ActorSighting>& Actor::ScanForEnemies(float fovDegrees, float range, int budget) {
	m_Sightings.clear();
	if (budget <= 0 || range <= 0.0F) {
		return m_Sightings;
	}
	const Vector eyes = GetEyePos();
	// The aim and the facing, as directions on screen (angles are counter-clockwise with Y up).
	const float aimAngle = GetAimAngle(true);
	const Vector aimDirection(std::cos(aimAngle), -std::sin(aimAngle));
	const Vector facing(m_HFlipped ? -1.0F : 1.0F, 0.0F);
	const float halfField = std::clamp(fovDegrees, 10.0F, 360.0F) * 0.5F;
	// Sharp aim: a narrow look down the aim (a fifth of the field, 4 degrees at least) that reaches half as far again, as down a scope.
	const bool sharp = m_Controller.IsState(AIM_SHARP);
	const float halfNarrow = std::max(4.0F, halfField * 0.2F);
	const float sightScale = GetNightSightScale();
	const float reach = range * sightScale;
	const float narrowReach = sharp ? reach * 1.5F : reach;
	auto degreesBetween = [](const Vector& a, const Vector& b) {
		float cosine = std::clamp((a.m_X * b.m_X + a.m_Y * b.m_Y) / std::max(a.GetMagnitude() * b.GetMagnitude(), 0.0001F), -1.0F, 1.0F);
		return std::acos(cosine) * 180.0F / c_PI;
	};

	struct Candidate {
		Actor* actor;
		Vector toTarget;
		float distance;
		float offAim; // Degrees off the aim.
		float off; // Degrees off the field it is in (the aim's when sharp and inside it, else the facing's), as a fraction of that field's half.
	};
	std::vector<Candidate> candidates;
	Box box(eyes - Vector(narrowReach, narrowReach), narrowReach * 2.0F, narrowReach * 2.0F);
	for (MovableObject* found: g_SceneMan.GetMOIDGrid().GetMOsInBox(box, m_Team, true)) {
		Actor* actor = dynamic_cast<Actor*>(found ? found->GetRootParent() : nullptr);
		if (!actor || actor == this || actor->GetTeam() == m_Team || actor->GetTeam() == Activity::NoTeam || actor->IsIgnoredByAI() || actor->GetStatus() == DEAD || actor->GetStatus() == DYING) {
			continue;
		}
		if (std::any_of(candidates.begin(), candidates.end(), [actor](const Candidate& candidate) { return candidate.actor == actor; })) {
			continue;
		}
		Vector toTarget = g_SceneMan.ShortestDistance(eyes, actor->GetPos(), g_SceneMan.SceneWrapsX() || g_SceneMan.SceneWrapsY());
		float distance = toTarget.GetMagnitude();
		float offAim = degreesBetween(toTarget, aimDirection);
		float offFacing = degreesBetween(toTarget, facing);
		float off;
		if (sharp && offAim <= halfNarrow && distance <= narrowReach) {
			off = offAim / halfNarrow;
		} else if (offFacing <= halfField && distance <= reach) {
			off = offFacing / halfField;
		} else {
			continue;
		}
		candidates.push_back({actor, toTarget, distance, offAim, off});
	}
	// The likeliest first: nearest the aim, then nearest, so the budget goes where a person would look.
	std::sort(candidates.begin(), candidates.end(), [narrowReach](const Candidate& a, const Candidate& b) { return a.offAim / 90.0F + a.distance / narrowReach < b.offAim / 90.0F + b.distance / narrowReach; });

	const LightingSettings& lighting = g_PostProcessMan.GetLightingSettings();
	const float night = (lighting.Enabled && lighting.NightAffectsAI) ? GetNightAmount() : 0.0F;
	for (const Candidate& candidate: candidates) {
		if (budget <= 0) {
			break;
		}
		// The body, then the head when the body is hidden (over a wall, behind a crate).
		bool seen = false;
		bool head = false;
		Vector hitPos;
		for (int look = 0; look < 2 && budget > 0 && !seen; ++look) {
			Vector target = look == 0 ? candidate.actor->GetPos() : candidate.actor->GetEyePos();
			if (look == 1 && target == candidate.actor->GetPos()) {
				break;
			}
			--budget;
			Vector ray = g_SceneMan.ShortestDistance(eyes, target, g_SceneMan.SceneWrapsX() || g_SceneMan.SceneWrapsY());
			// (A little past the point, so a ray to a thin body's middle still lands on it.)
			ray.SetMagnitude(ray.GetMagnitude() + 4.0F);
			MOID hit = g_SceneMan.CastMORay(eyes, ray, m_MOID, IgnoresWhichTeam(), g_MaterialGrass, false, 5);
			const MovableObject* hitMO = g_MovableMan.GetMOFromID(hit);
			if (hitMO && hitMO->GetRootParent() == candidate.actor && !SmokeGrid::BlocksSight(eyes, target)) {
				seen = true;
				head = look == 1;
				hitPos = g_SceneMan.GetLastRayHitPos();
			}
		}
		if (!seen) {
			continue;
		}
		// How plainly: off the middle of the field, far, in the dark (unless lit, by a lamp, its own headlamp or its gun going off), still,
		// and small (lying down, crouched, only a head showing) each take some away. The script delays its notice by it.
		float angle = 1.0F - 0.6F * std::clamp(candidate.off, 0.0F, 1.0F);
		float far = 1.0F - 0.7F * std::clamp(candidate.distance / narrowReach, 0.0F, 1.0F);
		float light = 1.0F;
		if (night > 0.05F) {
			float lit = g_PostProcessMan.GetDynamicLightAt(candidate.actor->GetPos());
			if (candidate.actor->GetController()->IsState(WEAPON_FIRE)) {
				lit = 1.0F;
			}
			light = std::max(1.0F - night * 0.7F, std::min(1.0F, lit * 1.5F));
		}
		float moving = candidate.actor->GetVel().MagnitudeIsGreaterThan(1.0F) ? 1.0F : 0.75F;
		float profile = std::clamp(candidate.actor->GetSightProfile(), 0.1F, 1.0F) * (head ? 0.7F : 1.0F);
		float visibility = std::clamp(angle * far * light * moving * profile, 0.05F, 1.0F);
		m_Sightings.push_back({candidate.actor, hitPos, visibility, candidate.distance, head});
	}
	std::sort(m_Sightings.begin(), m_Sightings.end(), [](const ActorSighting& a, const ActorSighting& b) { return a.Visibility > b.Visibility; });
	return m_Sightings;
}

bool Actor::FeelsFire() const {
	if ((m_Mechanical && !m_Organic) || GetMetalness() >= 0.2F) {
		return false;
	}
	return !dynamic_cast<const ADoor*>(this) && !dynamic_cast<const ACraft*>(this);
}

void Actor::AddSuppression(float amount) {
	if (amount <= 0.0F || m_Status == DYING || m_Status == DEAD || !FeelsFire()) {
		return;
	}
	m_Suppression = std::clamp(m_Suppression + amount * g_SettingsMan.AISuppression(), 0.0F, 1.0F);
}

void Actor::ChangeMorale(float change) {
	if (m_Status == DYING || m_Status == DEAD || !FeelsFire()) {
		return;
	}
	m_Morale = std::clamp(m_Morale + (change < 0.0F ? change * g_SettingsMan.AISuppression() : change), 0.0F, 1.0F);
}

void Actor::ShotPassing(const MovableObject& shot) {
	if (!shot.HitsMOs() || shot.GetSharpness() <= 0.0F || !shot.GetVel().MagnitudeIsGreaterThan(25.0F) || g_SettingsMan.AISuppression() <= 0.0F) {
		return;
	}
	// Only so many shots looked at a sim update: a minigun's stream pins a unit down as well with a few as with all of them. Which ones turns over
	// from update to update: the particle loop calls this in list order, oldest first, and a first-come budget went every update to the
	// long-lived shrapnel and ricochets at the front, so in a big firefight the shots just fired, at the back, were never looked at. The window
	// of shots looked at starts c_Budget further along the last update's count each update, so every shot gets its turn.
	constexpr int c_Budget = 96;
	static long long s_Update = -1;
	static int s_Seen = 0; // Shots that got this far this update, in call order.
	static int s_LastSeen = 0; // And last update.
	static int s_Start = 0; // Where this update's window starts in that order.
	static int s_Checks = 0; // Shots looked at this update.
	long long update = g_TimerMan.GetSimUpdateCount();
	if (update != s_Update) {
		s_Update = update;
		s_LastSeen = s_Seen;
		s_Start = s_LastSeen > c_Budget ? (s_Start + c_Budget) % s_LastSeen : 0;
		s_Seen = 0;
		s_Checks = 0;
	}
	int index = s_Seen++;
	if (s_LastSeen > c_Budget && ((index - s_Start) % s_LastSeen + s_LastSeen) % s_LastSeen >= c_Budget) {
		return;
	}
	if (++s_Checks > c_Budget) {
		return;
	}
	// Within two body widths of its last step, about 30 px: close enough to hear the crack.
	constexpr float c_Reach = 30.0F;
	const Vector from = shot.GetPrevPos();
	const Vector step = g_SceneMan.ShortestDistance(from, shot.GetPos(), g_SceneMan.SceneWrapsX() || g_SceneMan.SceneWrapsY());
	Box box(Vector(std::min(from.m_X, from.m_X + step.m_X) - c_Reach, std::min(from.m_Y, from.m_Y + step.m_Y) - c_Reach), std::abs(step.m_X) + c_Reach * 2.0F, std::abs(step.m_Y) + c_Reach * 2.0F);
	const float stepLengthSq = std::max(step.GetSqrMagnitude(), 0.0001F);
	// The box finds every part of a body (head, torso, limbs, held gun); each unit counts once for this shot.
	static thread_local std::vector<const Actor*> s_Counted;
	s_Counted.clear();
	for (MovableObject* found: g_SceneMan.GetMOIDGrid().GetMOsInBox(box, shot.GetTeam(), true)) {
		Actor* actor = dynamic_cast<Actor*>(found ? found->GetRootParent() : nullptr);
		if (!actor || (shot.GetTeam() != Activity::NoTeam && actor->GetTeam() == shot.GetTeam())) {
			continue;
		}
		if (std::find(s_Counted.begin(), s_Counted.end(), actor) != s_Counted.end()) {
			continue;
		}
		s_Counted.push_back(actor);
		// The nearest point of the step to the body: a shot that is passing, not one that has hit (that is the hit's own business).
		Vector toActor = g_SceneMan.ShortestDistance(from, actor->GetPos(), g_SceneMan.SceneWrapsX() || g_SceneMan.SceneWrapsY());
		float along = std::clamp((toActor.m_X * step.m_X + toActor.m_Y * step.m_Y) / stepLengthSq, 0.0F, 1.0F);
		float distance = (toActor - step * along).GetMagnitude();
		float bodyRadius = std::max(actor->GetRadius() * 0.5F, 6.0F);
		if (distance <= bodyRadius || distance > bodyRadius + c_Reach) {
			continue;
		}
		if (actor->m_NearMissUpdate != update) {
			actor->m_NearMissUpdate = update;
			actor->m_NearMissThisUpdate = 0.0F;
		}
		// (At most 0.15 an update from near misses, however many.)
		float amount = std::min(0.02F + 0.06F * (1.0F - (distance - bodyRadius) / c_Reach), 0.15F - actor->m_NearMissThisUpdate);
		if (amount > 0.0F) {
			actor->m_NearMissThisUpdate += amount;
			actor->AddSuppression(amount);
		}
	}
}

void Actor::UpdateSuppressionAndMorale() {
	if (m_Status == DYING || m_Status == DEAD) {
		// A friend dying in sight shakes the friends who saw it, the closer the more.
		if (!m_DeathReported) {
			m_DeathReported = true;
			if (FeelsFire()) {
				constexpr float c_SightOfDeath = 300.0F;
				for (Actor* friendActor: g_MovableMan.GetActorList()) {
					if (friendActor == this || friendActor->GetTeam() != m_Team || friendActor->GetStatus() == DYING || friendActor->GetStatus() == DEAD) {
						continue;
					}
					Vector toFriend = g_SceneMan.ShortestDistance(m_Pos, friendActor->GetPos(), g_SceneMan.SceneWrapsX() || g_SceneMan.SceneWrapsY());
					Vector notUsed;
					if (toFriend.MagnitudeIsLessThan(c_SightOfDeath) && !g_SceneMan.CastStrengthRay(m_Pos, toFriend, 10.0F, notUsed, 4, g_MaterialGrass)) {
						friendActor->ChangeMorale(-(0.08F + 0.12F * (1.0F - toFriend.GetMagnitude() / c_SightOfDeath)));
					}
				}
			}
		}
		return;
	}
	if (!FeelsFire()) {
		m_Suppression = 0.0F;
		m_Morale = 1.0F;
		return;
	}
	const float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	// A better team gets over it quicker: 0.6 to 1.4 times as fast from the worst skill to the best.
	const float skill = static_cast<float>(g_ActivityMan.GetActivity() ? g_ActivityMan.GetActivity()->GetTeamAISkill(m_Team) : Activity::DefaultSkill);
	const float recovery = 0.6F + std::clamp(skill, 0.0F, 100.0F) / 125.0F;
	// A hit pins it down and shakes it, by how much of its health it took.
	if (float damage = m_PrevHealth - m_Health; damage > 0.0F) {
		float share = damage / std::max(m_MaxHealth, 1.0F);
		AddSuppression(0.1F + share * 2.0F);
		ChangeMorale(-share * 0.8F);
	}
	m_Suppression = std::max(0.0F, m_Suppression - deltaTime * 0.25F * recovery);
	// Being pinned down wears the nerve.
	if (m_Suppression > 0.0F) {
		ChangeMorale(-deltaTime * m_Suppression * 0.08F);
	}
	// What it comes back towards: with friends about (up to three within 200 px) and its brain near, steadier; hurt, less so. Worked out
	// now and then, each actor on its own update so they don't all look at once.
	if ((static_cast<long long>(GetUniqueID()) + g_TimerMan.GetSimUpdateCount()) % 30 == 0) {
		int friends = 0;
		bool brainNear = false;
		for (const Actor* other: g_MovableMan.GetActorList()) {
			if (other == this || other->GetTeam() != m_Team || other->GetStatus() == DYING || other->GetStatus() == DEAD) {
				continue;
			}
			Vector toOther = g_SceneMan.ShortestDistance(m_Pos, other->GetPos(), g_SceneMan.SceneWrapsX() || g_SceneMan.SceneWrapsY());
			if (other->IsInGroup("Brains")) {
				brainNear = brainNear || toOther.MagnitudeIsLessThan(300.0F);
			} else if (friends < 3 && toOther.MagnitudeIsLessThan(200.0F)) {
				++friends;
			}
		}
		m_MoraleLevel = std::clamp(0.55F + 0.1F * static_cast<float>(friends) + (brainNear ? 0.15F : 0.0F) - 0.3F * (1.0F - std::clamp(m_Health / std::max(m_MaxHealth, 1.0F), 0.0F, 1.0F)), 0.2F, 1.0F);
	}
	if (m_Morale < m_MoraleLevel) {
		m_Morale = std::min(m_MoraleLevel, m_Morale + deltaTime * 0.04F * recovery);
	} else {
		m_Morale = std::max(m_MoraleLevel, m_Morale - deltaTime * 0.02F);
	}
}

void Actor::PostUpdate() {
	// The item in reach is kept from one update to the next. If it was flagged this update after this actor's own Update (picked up by someone
	// else, settled, gibbed, the sandbox's erase tool) it is deleted at the end of this update, and the HUD and next update's reach test would
	// read freed memory. Every flag is set by now and nothing is deleted yet.
	if (m_pItemInReach && (!g_MovableMan.IsDevice(m_pItemInReach) || m_pItemInReach->ToDelete())) {
		m_pItemInReach = nullptr;
	}
	MOSRotating::PostUpdate();
}

void Actor::Update() {
	// Night: a headlamp lighting where the actor looks, plus a little glow around it. Render only.
	if (const LightingSettings& lighting = g_PostProcessMan.GetLightingSettings(); lighting.Headlamps && lighting.Enabled && m_HeadlampBrightness > 0.0F && m_Status != DEAD && m_Status != DYING) {
		float night = lighting.HeadlampsByDay ? 1.0F : GetNightAmount();
		if (night > 0.05F) {
			Vector eyePos = GetEyePos();
			float aimAngle = GetAimAngle(true);
			// CC angles are counter-clockwise with Y up; screen space is Y down.
			Vector direction(std::cos(aimAngle), -std::sin(aimAngle));
			// The lamp's color: this unit's own if its INI or a script gave it one, else the player's setting, with as much of the side's color as the player asked for.
			glm::vec3 color = glm::pow(glm::clamp(lighting.HeadlampColor, glm::vec3(0.0F), glm::vec3(1.0F)), glm::vec3(1.0F / 2.2F)) * 255.0F;
			if (m_HeadlampHasColor) {
				color = glm::vec3(m_HeadlampColor.GetR(), m_HeadlampColor.GetG(), m_HeadlampColor.GetB());
			} else if (lighting.HeadlampTeamTint > 0.0F && m_Team >= 0 && m_Team < 4) {
				static const glm::vec3 teamColors[4] = {{255.0F, 105.0F, 85.0F}, {105.0F, 255.0F, 120.0F}, {110.0F, 165.0F, 255.0F}, {255.0F, 225.0F, 95.0F}};
				color = glm::mix(color, teamColors[m_Team], std::clamp(lighting.HeadlampTeamTint, 0.0F, 1.0F));
			}
			g_PostProcessMan.RegisterConeLight(eyePos, direction, std::clamp(lighting.HeadlampWidth, 2.0F, 89.0F), color, lighting.HeadlampReach, lighting.HeadlampBrightness * m_HeadlampBrightness * night, LightSource::Headlamps);
			g_PostProcessMan.RegisterLight(eyePos, color, 36.0F, lighting.HeadlampGlow * m_HeadlampBrightness * night, LightSource::Headlamps);
		}
	}

	ZoneScoped;

	/////////////////////////////////
	// Hit Body update and handling
	MOSRotating::Update();

	m_PieMenu->Update();

	// Update the viewpoint to be at least what the position is
	m_ViewPoint = m_Pos;

	// Check if the MO we're following still exists, and if not, then clear the destination
	if (m_pMOMoveTarget && !g_MovableMan.ValidMO(m_pMOMoveTarget)) {
		m_pMOMoveTarget = nullptr;
	}

	///////////////////////////////////////////////////////////////////////////////
	// Check for manual player-made progress made toward the set AI goal

	if ((m_AIMode == AIMODE_GOTO || m_AIMode == AIMODE_SQUAD) && (!m_PathRequest || m_PathRequest->complete) && m_Controller.IsPlayerControlled() && !m_Controller.IsDisabled()) {
		Vector notUsed;
		// See if we are close enough to the next move target that we should grab the next in the path that is out of proximity range
		Vector pathPointVec;
		for (std::list<Vector>::iterator lItr = m_MovePath.begin(); lItr != m_MovePath.end();) {
			pathPointVec = g_SceneMan.ShortestDistance(m_Pos, *lItr);
			// Make sure we are within range AND have a clear sight to the path point we're about to eliminate, or it might be around a corner
			if (pathPointVec.MagnitudeIsLessThan(m_MoveProximityLimit) && !g_SceneMan.CastStrengthRay(m_Pos, pathPointVec, 5, notUsed, 0)) {
				lItr++;
				// Save the last one before being popped off so we can use it to check if we need to dig (if there's any material between last and current)
				m_PrevPathTarget = m_MovePath.front();
				m_MovePath.pop_front();
				if (!m_MovePathKinds.empty()) {
					m_MovePathKinds.pop_front();
				}
			} else {
				break;
			}
		}

		if (!m_MovePath.empty()) {
			Vector notUsed;

			// See if we are close enough to the last point in the current path, in which case we can toss teh whole current path and start ont he next
			pathPointVec = g_SceneMan.ShortestDistance(m_Pos, m_MovePath.back());
			// Clear out the current path, the player apparently took a shortcut
			if (pathPointVec.MagnitudeIsLessThan(m_MoveProximityLimit) && !g_SceneMan.CastStrengthRay(m_Pos, pathPointVec, 5, notUsed, 0, g_MaterialDoor)) {
				m_MovePath.clear();
				m_MovePathKinds.clear();
			}
		}

		// If still stuff in the path, get the next point on it
		if (!m_MovePath.empty())
			m_MoveTarget = m_MovePath.front();
		// No more path, so check if any more waypoints to make a new path to? This doesn't apply if we're following something
		else if (m_MovePath.empty() && !m_Waypoints.empty() && !m_pMOMoveTarget)
			UpdateMovePath();
		// Nope, so just conclude that we must have reached the ultimate AI target set and exit the goto mode
		else if (!m_pMOMoveTarget)
			m_AIMode = AIMODE_SENTRY;
	}
	// Save health state so we can compare next update
	m_PrevHealth = m_Health;
	/////////////////////////////////////
	// Take damage/heal from wounds and wounds on Attachables
	for (AEmitter* wound: m_Wounds) {
		m_Health -= wound->CollectDamage() * m_DamageMultiplier;
	}
	for (Attachable* attachable: m_Attachables) {
		m_Health -= attachable->CollectDamage();
	}
	m_Health = std::min(m_Health, m_MaxHealth);

	/////////////////////////////
	// Stability logic

	if (m_Status == STABLE) {
		// If moving really fast, we're not able to be stable
		if (std::abs(m_Vel.m_X) > std::abs(m_StableVel.m_X) || std::abs(m_Vel.m_Y) > std::abs(m_StableVel.m_Y)) {
			m_Status = UNSTABLE;
		}

		m_StableRecoverTimer.Reset();
	} else if (m_Status == UNSTABLE) {
		// Only regain stability if we're not moving too fast and it's been a while since we lost it
		if (m_StableRecoverTimer.IsPastSimMS(m_StableRecoverDelay) && !(std::abs(m_Vel.m_X) > std::abs(m_StableVel.m_X) || std::abs(m_Vel.m_Y) > std::abs(m_StableVel.m_Y))) {
			m_Status = STABLE;
		}
	}

	/////////////////////////////////////////////
	// Take damage from large hits during travel

	const float travelImpulseMagnitudeSqr = m_TravelImpulse.GetSqrMagnitude();

	// If we're travelling at least half the speed to hurt ourselves, play the body hit noise
	float halfTravelImpulseDamage = m_TravelImpulseDamage * 0.5F;
	if (m_BodyHitSound && travelImpulseMagnitudeSqr > (halfTravelImpulseDamage * halfTravelImpulseDamage)) {
		m_BodyHitSound->Play(m_Pos);
	}

	// But only actually damage ourselves if we're unstable
	if (m_Status == Actor::UNSTABLE && travelImpulseMagnitudeSqr > (m_TravelImpulseDamage * m_TravelImpulseDamage)) {
		const float impulse = std::sqrt(travelImpulseMagnitudeSqr) - m_TravelImpulseDamage;
		const float damage = std::max(impulse / (m_GibImpulseLimit - m_TravelImpulseDamage) * m_MaxHealth, 0.0F);
		m_Health -= damage;
		m_ForceDeepCheck = true;
	}

	// Spread the carried items and gold around before death.
	if (m_Status == DYING || m_Status == DEAD) {
		// Actor may die for a long time, no need to call this more than once
		if (m_Inventory.size() > 0) {
			DropAllInventory();
		}
		if (m_GoldCarried > 0) {
			DropAllGold();
		}
	}

	////////////////////////////////
	// Death logic

	if (m_Status != DYING && m_Status != DEAD && m_Health <= 0) {
		if (m_DeathSound) {
			m_DeathSound->Play(m_Pos);
		}
		DropAllInventory();
		m_Status = DYING;
		m_DeathTmr.Reset();
	}

	// Prevent dead actors from rotating like mad
	if (m_Status == DYING || m_Status == DEAD) {
		m_AngularVel = m_AngularVel * 0.98F;
	}

	if (m_Status == DYING && m_DeathTmr.GetElapsedSimTimeMS() > 1000) {
		m_Status = DEAD;
	}

	UpdateSuppressionAndMorale();

	//////////////////////////////////////////////////////
	// Save previous second's position so we can detect larger movement

	if (m_LastSecondTimer.IsPastSimMS(1000)) {
		m_RecentMovement = m_Pos - m_LastSecondPos;
		m_LastSecondPos = m_Pos;
		m_LastSecondTimer.Reset();
	}

	////////////////////////////////////////
	// Animate the sprite, if applicable

	if (m_FrameCount > 1) {
		if (m_SpriteAnimMode == LOOPWHENACTIVE) {
			if (m_Controller.IsState(MOVE_LEFT) || m_Controller.IsState(MOVE_RIGHT) || m_Controller.GetAnalogMove().GetLargest() > 0.1) {
				// TODO: improve; make this
				float cycleTime = ((long)m_SpriteAnimTimer.GetElapsedSimTimeMS()) % m_SpriteAnimDuration;
				m_Frame = std::floor((cycleTime / (float)m_SpriteAnimDuration) * (float)m_FrameCount);
			}
		}
	}

	/////////////////////////////////
	// Misc

	// If in AI setting mode prior to actor switch, made the team rosters get sorted so the lines are drawn correctly
	if (m_Controller.IsState(PIE_MENU_ACTIVE)) {
		g_MovableMan.SortTeamRoster(m_Team);
	}

	// Play PainSound if damage this frame exceeded PainThreshold
	if (m_PainThreshold > 0 && m_PrevHealth - m_Health > m_PainThreshold && m_Health > 1 && m_PainSound) {
		m_PainSound->Play(m_Pos);
	}

	int brainOfPlayer = g_ActivityMan.GetActivity()->IsBrainOfWhichPlayer(this);
	if (brainOfPlayer != Players::NoPlayer && g_ActivityMan.GetActivity()->PlayerHuman(brainOfPlayer)) {
		if (m_PrevHealth - m_Health > 1.5F) {
			// If this is a brain that's under attack, broadcast an alarm event so that the enemy AI won't dawdle in trying to kill it.
			g_MovableMan.RegisterAlarmEvent(AlarmEvent(m_Pos, m_Team, 0.5F));
			if (g_SettingsMan.FlashOnBrainDamage()) {
				g_FrameMan.FlashScreen(g_ActivityMan.GetActivity()->ScreenOfPlayer(brainOfPlayer), g_RedColor, 10);
			}
		}
		if ((m_ToDelete || m_Status == DEAD) && g_SettingsMan.FlashOnBrainDamage()) {
			g_FrameMan.FlashScreen(g_ActivityMan.GetActivity()->ScreenOfPlayer(brainOfPlayer), g_WhiteColor, 500);
		}
	}

	if (m_Controller.IsState(ACTOR_PRIMARY_HOTKEY)) {
		ActivateHotkeyAction(PRIMARYHOTKEY);
	} else {
		DeactivateHotkeyAction(PRIMARYHOTKEY);
	}

	if (m_Controller.IsState(ACTOR_AUXILIARY_HOTKEY)) {
		ActivateHotkeyAction(AUXILIARYHOTKEY);
	} else {
		DeactivateHotkeyAction(AUXILIARYHOTKEY);
	}
}

void RTE::Actor::CastSeeRays() {
	// "See" the location and surroundings of this actor on the unseen map
	if (m_Status != Actor::INACTIVE) {
		const int lookIterations = 6; // How many see rays to cast per frame
		for (int i = 0; i < lookIterations; ++i) {
			Look(45 * m_Perceptiveness, g_FrameMan.GetPlayerScreenWidth() * 0.51 * m_Perceptiveness);
		}
	}
}

void Actor::FullUpdate() {
	PreControllerUpdate();
	m_Controller.Update();
	Update();
}

void Actor::DrawHUD(BITMAP* pTargetBitmap, const Vector& targetPos, int whichScreen, bool playerControlled) {
	// This should indeed be a local var and not alter a member one in a draw func! Can cause nasty jittering etc if multiple sim updates are done without a drawing in between etc
	m_HUDStack = -m_CharHeight / 2;

	// Only do HUD if on a team
	if (m_Team < 0) {
		return;
	}

	// Only draw if the team viewing this is on the same team OR has seen the space where this is located.
	int viewingTeam = g_ActivityMan.GetActivity()->GetTeamOfPlayer(g_ActivityMan.GetActivity()->PlayerOfScreen(whichScreen));
	if (viewingTeam != m_Team && viewingTeam != Activity::NoTeam && (!g_SettingsMan.ShowEnemyHUD() || g_SceneMan.IsUnseen(m_Pos.GetFloorIntX(), m_Pos.GetFloorIntY(), viewingTeam))) {
		return;
	}

	// Draw stat info HUD
	char str[64];

	GUIFont* pSymbolFont = g_FrameMan.GetLargeFont();
	GUIFont* pSmallFont = g_FrameMan.GetSmallFont();
	Vector drawPos = m_Pos - targetPos;
	Vector cpuPos = GetCPUPos() - targetPos;

	// If we have something to draw, adjust the draw position to work if drawn to a target screen bitmap that is straddling a scene seam
	if ((m_HUDVisible || m_PieMenu->IsVisible()) && !targetPos.IsZero()) {
		// Spans vertical scene seam
		int sceneWidth = g_SceneMan.GetSceneWidth();
		if (g_SceneMan.SceneWrapsX() && pTargetBitmap->w < sceneWidth) {
			if ((targetPos.m_X < 0) && (m_Pos.m_X > (sceneWidth - pTargetBitmap->w))) {
				drawPos.m_X -= sceneWidth;
				cpuPos.m_X -= sceneWidth;
			} else if (((targetPos.m_X + pTargetBitmap->w) > sceneWidth) && (m_Pos.m_X < pTargetBitmap->w)) {
				drawPos.m_X += sceneWidth;
				cpuPos.m_X += sceneWidth;
			}
		}

		// Spans horizontal scene seam
		int sceneHeight = g_SceneMan.GetSceneHeight();
		if (g_SceneMan.SceneWrapsY() && pTargetBitmap->h < sceneHeight) {
			if ((targetPos.m_Y < 0) && (m_Pos.m_Y > (sceneHeight - pTargetBitmap->h))) {
				drawPos.m_Y -= sceneHeight;
				cpuPos.m_Y -= sceneHeight;
			} else if (((targetPos.m_Y + pTargetBitmap->h) > sceneHeight) && (m_Pos.m_Y < pTargetBitmap->h)) {
				drawPos.m_Y += sceneHeight;
				cpuPos.m_Y += sceneHeight;
			}
		}
	}

	int actorScreen = g_ActivityMan.GetActivity() ? g_ActivityMan.GetActivity()->ScreenOfPlayer(m_Controller.GetPlayer()) : -1;
	bool screenTeamIsSameAsActorTeam = g_ActivityMan.GetActivity() ? g_ActivityMan.GetActivity()->GetTeamOfPlayer(g_ActivityMan.GetActivity()->PlayerOfScreen(whichScreen)) == m_Team : true;
	if (m_PieMenu->IsVisible() && screenTeamIsSameAsActorTeam && (!m_PieMenu->IsInNormalAnimationMode() || (actorScreen == whichScreen))) {
		m_PieMenu->Draw(pTargetBitmap, targetPos);
	}

	if (!m_HUDVisible) {
		return;
	}

	// Draw the selection arrow, if controlled and under the arrow's time limit
	if (m_Controller.IsPlayerControlled() && m_NewControlTmr.GetElapsedSimTimeMS() < ARROWTIME) {
		draw_sprite(pTargetBitmap, m_apSelectArrow[m_Team], cpuPos.m_X, EaseOut(drawPos.m_Y + m_HUDStack - 60, drawPos.m_Y + m_HUDStack - 20, m_NewControlTmr.GetElapsedSimTimeMS() / (float)ARROWTIME));
	} else if (m_SandboxSelected) {
		// Selected in the sandbox: the same arrow, bobbing over the head for as long as it's selected, with a glow so it shows against anything.
		float bob = std::sin(static_cast<float>(g_TimerMan.GetSimTimeMS()) * 0.006F) * 3.0F;
		int arrowY = drawPos.GetFloorIntY() + m_HUDStack - 20 + static_cast<int>(bob);
		int team = std::clamp(m_Team, 0, std::max(0, static_cast<int>(m_apSelectArrow.size()) - 1));
		if (team >= 0 && !m_apSelectArrow.empty()) {
			draw_sprite(pTargetBitmap, m_apSelectArrow[team], cpuPos.m_X, arrowY);
			g_PostProcessMan.RegisterGlowArea(Vector(m_Pos.m_X, m_Pos.m_Y + static_cast<float>(m_HUDStack - 20 + 7) + bob), 8);
		}
	}

	// Draw the alarm exclamation mark if we are alarmed!
	if (m_AlarmTimer.GetSimTimeLimitProgress() < 0.25) {
		draw_sprite(pTargetBitmap, m_apAlarmExclamation[m_AgeTimer.AlternateSim(100)], cpuPos.m_X - 3, EaseOut(drawPos.m_Y + m_HUDStack - 10, drawPos.m_Y + m_HUDStack - 25, m_AlarmTimer.GetSimTimeLimitProgress() / 0.25f));
	}

	if (pSmallFont && pSymbolFont) {
		AllegroBitmap bitmapInt(pTargetBitmap);

		if (!m_Controller.IsState(PIE_MENU_ACTIVE) || actorScreen != whichScreen) {
			// If we're still alive, show the team colors
			if (m_Health > 0) {

				// Get the Icon bitmaps of this Actor's team, if any
				std::vector<BITMAP*> apIconBitmaps;
				if (m_pTeamIcon) {
					apIconBitmaps = m_pTeamIcon->GetBitmaps8();
				}

				// Team Icon could not be found, or of no team, so use the static noteam Icon instead
				if (apIconBitmaps.empty()) {
					apIconBitmaps = m_apNoTeamIcon;
				}

				// Now draw the Icon if we can
				if (!apIconBitmaps.empty() && m_pTeamIcon && m_pTeamIcon->GetFrameCount() > 0) {
					// Make team icon blink faster as the health goes down
					int f = m_HeartBeat.AlternateReal(200 + 800 * (MAX(m_Health, 0) / 100)) ? 0 : 1;
					f = MIN(f, m_pTeamIcon ? m_pTeamIcon->GetFrameCount() - 1 : 1);
					masked_blit(apIconBitmaps.at(f), pTargetBitmap, 0, 0, drawPos.m_X - apIconBitmaps.at(f)->w - 2, drawPos.m_Y + m_HUDStack - (apIconBitmaps.at(f)->h / 2) + 8, apIconBitmaps.at(f)->w, apIconBitmaps.at(f)->h);
				}
			} else {
				// Draw death icon
				str[0] = -39;
				str[1] = 0;
				pSymbolFont->DrawAligned(&bitmapInt, drawPos.m_X - 10, drawPos.m_Y + m_HUDStack, str, GUIFont::Left);
			}

			std::snprintf(str, sizeof(str), "%.0f", std::ceil(m_Health));
			pSymbolFont->DrawAligned(&bitmapInt, drawPos.m_X - 0, drawPos.m_Y + m_HUDStack, str, GUIFont::Left);

			m_HUDStack += -12;

			if (IsPlayerControlled()) {
				if (GetGoldCarried() > 0) {
					str[0] = m_GoldPicked ? -57 : -58;
					str[1] = 0;
					pSymbolFont->DrawAligned(&bitmapInt, drawPos.GetFloorIntX() - 11, drawPos.GetFloorIntY() + m_HUDStack, str, GUIFont::Left);
					std::snprintf(str, sizeof(str), "%.0f oz", GetGoldCarried());
					pSmallFont->DrawAligned(&bitmapInt, drawPos.GetFloorIntX() - 0, drawPos.GetFloorIntY() + m_HUDStack + 2, str, GUIFont::Left);

					m_HUDStack -= 11;
				}
			}
		}
	}

	// Don't proceed to draw all the secret stuff below if this screen is for a player on the other team! (Unless the AI paths are being
	// shown on purpose: that is a look at every side's units, and in the sandbox the viewer is on no team at all.)
	if (s_ShowAIPaths == 0 && g_ActivityMan.GetActivity() && g_ActivityMan.GetActivity()->GetTeamOfPlayer(whichScreen) != m_Team) {
		return;
	}

	// AI waypoints or points of interest
	bool pathShown = s_ShowAIPaths == 1 || (s_ShowAIPaths == 2 && m_SandboxSelected);
	// (Red when the route couldn't be found, or only goes through ground this unit can't dig: the destination is out of its reach.)
	int pathColor = m_PathImpossible ? g_RedColor : g_YellowGlowColor;
	if ((pathShown || (m_DrawWaypoints && m_PlayerControllable && m_Controller.IsPlayerControlled())) && (m_AIMode == AIMODE_GOTO || m_AIMode == AIMODE_SQUAD)) {
		// Draw the AI paths, from the ultimate destination back up to the actor's position.
		// We do this backwards so the lines won't crawl and the dots can be evenly spaced throughout
		Vector waypoint;
		std::list<std::pair<Vector, const MovableObject*>>::reverse_iterator vLast, vItr;
		std::list<Vector>::reverse_iterator lLast, lItr;
		int skipPhase = 0;

		// Draw the line between the end of the movepath and the first waypoint after that, if any
		if (!m_Waypoints.empty()) {
			// Draw the first destination/waypoint point
			//            waypoint = m_MoveTarget - targetPos;
			//            circlefill(pTargetBitmap, waypoint.m_X, waypoint.m_Y, 2, pathColor);

			// Draw the additional waypoint points beyond the first one
			vLast = m_Waypoints.rbegin();
			vItr = m_Waypoints.rbegin();
			for (; vItr != m_Waypoints.rend(); ++vItr) {
				// Draw the line
				g_FrameMan.DrawLine(pTargetBitmap, (*vLast).first - targetPos, (*vItr).first - targetPos, pathColor, 0, AILINEDOTSPACING, 0, true);
				vLast = vItr;

				// Draw the points
				waypoint = (*vItr).first - targetPos;
				circlefill(pTargetBitmap, waypoint.m_X, waypoint.m_Y, 2, pathColor);

				// Add pixel glow area around it, in scene coordinates
				g_PostProcessMan.RegisterGlowArea((*vItr).first, 5);
			}

			// Draw line from the last movetarget on the current path to the first waypoint in queue after that
			if (!m_MovePath.empty()) {
				g_FrameMan.DrawLine(pTargetBitmap, m_MovePath.back() - targetPos, m_Waypoints.front().first - targetPos, pathColor, 0, AILINEDOTSPACING, 0, true);
			} else {
				g_FrameMan.DrawLine(pTargetBitmap, m_MoveTarget - targetPos, m_Waypoints.front().first - targetPos, pathColor, 0, AILINEDOTSPACING, 0, true);
			}
		}

		// Draw the current movepath, but backwards so the dot spacing can be even and they don't crawl as the guy approaches
		if (!m_MovePath.empty()) {
			lLast = m_MovePath.rbegin();
			lItr = m_MovePath.rbegin();
			for (; lItr != m_MovePath.rend(); ++lItr) {
				// Draw these backwards so the skip phase works
				skipPhase = g_FrameMan.DrawLine(pTargetBitmap, (*lLast) - targetPos, (*lItr) - targetPos, pathColor, 0, AILINEDOTSPACING, skipPhase, true);
				lLast = lItr;
				// Each node of the path marked too, when the paths are being shown on purpose: the dotted line alone is a pixel every sixteen, and
				// hard to see.
				if (pathShown) {
					Vector node = (*lItr) - targetPos;
					circlefill(pTargetBitmap, node.GetFloorIntX(), node.GetFloorIntY(), 1, pathColor);
				}
			}

			// Draw the line between the current position and to the start of the movepath, backwards so the dotted lines doesn't crawl
			skipPhase = g_FrameMan.DrawLine(pTargetBitmap, m_MovePath.front() - targetPos, m_Pos - targetPos, pathColor, 0, AILINEDOTSPACING, skipPhase, true);

			// Draw the first destination/waypoint point
			waypoint = m_MovePath.back() - targetPos;
			circlefill(pTargetBitmap, waypoint.m_X, waypoint.m_Y, 2, pathColor);

			// Add pixel glow area around it, in scene coordinates
			g_PostProcessMan.RegisterGlowArea(m_MovePath.back(), 5);
		} else {
			// No points left on movepath, so draw straight line to the movetarget

			// Draw it backwards so the dotted lines doesn't crawl
			skipPhase = g_FrameMan.DrawLine(pTargetBitmap, m_MoveTarget - targetPos, m_Pos - targetPos, pathColor, 0, AILINEDOTSPACING, skipPhase, true);

			// Draw the first destination/waypoint point
			waypoint = m_MoveTarget - targetPos;
			circlefill(pTargetBitmap, waypoint.m_X, waypoint.m_Y, 2, pathColor);

			// Add pixel glow area around it, in scene coordinates
			g_PostProcessMan.RegisterGlowArea(m_MoveTarget, 5);
		}
	}

	// AI Mode team roster HUD lines
	if (m_PlayerControllable && g_ActivityMan.GetActivity()->GetViewState(g_ActivityMan.GetActivity()->PlayerOfScreen(whichScreen)) == Activity::ViewState::ActorSelect && g_SceneMan.ShortestDistance(m_Pos, g_CameraMan.GetScrollTarget(whichScreen), g_SceneMan.SceneWrapsX()).GetMagnitude() < 100) {
		draw_sprite(pTargetBitmap, GetAIModeIcon(), cpuPos.m_X - 6, cpuPos.m_Y - 6);
	} else if (m_Controller.IsState(ACTOR_NEXT_PREP) || m_Controller.IsState(ACTOR_PREV_PREP)) {
		int prevColor = m_Controller.IsState(ACTOR_PREV_PREP) ? 122 : (m_Team == Activity::TeamOne ? 13 : 147);
		int nextColor = m_Controller.IsState(ACTOR_NEXT_PREP) ? 122 : (m_Team == Activity::TeamOne ? 13 : 147);
		int prevSpacing = m_Controller.IsState(ACTOR_PREV_PREP) ? 3 : 9;
		int nextSpacing = m_Controller.IsState(ACTOR_NEXT_PREP) ? 3 : 9;
		int altColor = m_Team == Activity::TeamOne ? 11 : 160;

		Actor* pPrevAdj = 0;
		Actor* pNextAdj = 0;
		std::list<Actor*>* pRoster = g_MovableMan.GetTeamRoster(m_Team);

		if (pRoster->size() > 1) {
			// Find this in the list, both ways
			std::list<Actor*>::reverse_iterator selfRItr = find(pRoster->rbegin(), pRoster->rend(), this);
			RTEAssert(selfRItr != pRoster->rend(), "Actor couldn't find self in Team roster!");
			std::list<Actor*>::iterator selfItr = find(pRoster->begin(), pRoster->end(), this);
			RTEAssert(selfItr != pRoster->end(), "Actor couldn't find self in Team roster!");

			// Find the adjacent actors
			if (selfItr != pRoster->end()) {
				// Get the previous available actor in the list (not controlled by another player)
				std::list<Actor*>::reverse_iterator prevItr = selfRItr;
				do {
					if (++prevItr == pRoster->rend())
						prevItr = pRoster->rbegin();
					if ((*prevItr) == (*selfItr))
						break;
				} while (!(*prevItr)->IsPlayerControllable() || (*prevItr)->GetController()->IsPlayerControlled() ||
				         g_ActivityMan.GetActivity()->IsOtherPlayerBrain((*prevItr), m_Controller.GetPlayer()));

				// Get the next actor in the list (not controlled by another player)
				std::list<Actor*>::iterator nextItr = selfItr;
				do {
					if (++nextItr == pRoster->end()) {
						nextItr = pRoster->begin();
					}

					if ((*nextItr) == (*selfItr)) {
						break;
					}
				} while (!(*nextItr)->IsPlayerControllable() || (*nextItr)->GetController()->IsPlayerControlled() || g_ActivityMan.GetActivity()->IsOtherPlayerBrain((*prevItr), m_Controller.GetPlayer()));

				Vector iconPos = cpuPos;

				// Only continue if there are available adjacent Actors
				if ((*prevItr) != (*selfItr) && (*nextItr) != (*selfItr)) {
					pPrevAdj = *prevItr;
					pNextAdj = *nextItr;
					if (pPrevAdj != pNextAdj) {
						// Only draw both lines if they're not pointing to the same thing
						g_FrameMan.DrawLine(pTargetBitmap, cpuPos, pPrevAdj->GetCPUPos() - targetPos, prevColor, prevColor, prevSpacing, 0, true);
						g_FrameMan.DrawLine(pTargetBitmap, cpuPos, pNextAdj->GetCPUPos() - targetPos, nextColor, nextColor, nextSpacing, 0, true);
					} else {
						// If only one other available Actor, only draw one yellow line to it
						g_FrameMan.DrawLine(pTargetBitmap, cpuPos, pNextAdj->GetCPUPos() - targetPos, 122, 122, 3, 0, true);
					}

					// Prev selected icon
					iconPos = pPrevAdj->GetCPUPos() - targetPos;
					draw_sprite(pTargetBitmap, pPrevAdj->GetAIModeIcon(), iconPos.m_X - 6, iconPos.m_Y - 6);

					// Next selected icon
					iconPos = pNextAdj->GetCPUPos() - targetPos;
					draw_sprite(pTargetBitmap, pNextAdj->GetAIModeIcon(), iconPos.m_X - 6, iconPos.m_Y - 6);
				}

				// Self selected icon
				iconPos = cpuPos;
				draw_sprite(pTargetBitmap, GetAIModeIcon(), iconPos.m_X - 6, iconPos.m_Y - 6);
			}
		}
	}
}

void Actor::DrawHUD(const Camera& camera) {}

void Actor::GetDebugState(std::vector<DebugStateField>& fields) const {
	static const char* const modeNames[] = {"none", "sentry", "patrol", "goto", "brainhunt", "gold dig", "return", "stay", "scuttle", "deliver", "bomb", "squad", "count"};
	auto number = [&fields](const std::string& name, double value) {
		std::ostringstream text;
		text << value;
		fields.push_back({name, text.str(), false});
	};
	auto flag = [&fields](const std::string& name, bool value) { fields.push_back({name, value ? "true" : "false", false}); };
	fields.push_back({"preset", GetPresetName(), true});
	number("id", static_cast<double>(GetUniqueID()));
	number("team", m_Team);
	number("x", std::floor(m_Pos.m_X));
	number("y", std::floor(m_Pos.m_Y));
	number("vx", std::round(m_Vel.m_X * 10.0F) / 10.0);
	number("vy", std::round(m_Vel.m_Y * 10.0F) / 10.0);
	number("health", std::round(m_Health));
	number("status", m_Status);
	fields.push_back({"aiMode", m_AIMode >= 0 && m_AIMode < static_cast<int>(std::size(modeNames)) ? modeNames[m_AIMode] : std::to_string(m_AIMode), true});
	flag("playerControlled", IsPlayerControlled());
	number("routePoints", static_cast<double>(m_MovePath.size()));
	number("waypoints", static_cast<double>(m_Waypoints.size()));
	flag("routeAsked", IsWaitingOnNewMovePath());
	static const char* const stepNames[] = {"walk", "crawl", "jump", "fall", "dig", "door", "stairs", "ladder", "leap", "mantle", "crouch", "scramble", "swim", "wade"};
	auto stepName = [](int kind) { return kind >= 0 && kind < static_cast<int>(std::size(stepNames)) ? std::string(stepNames[kind]) : std::string("none"); };
	fields.push_back({"step", stepName(GetMovePathStepKind()), true});
	fields.push_back({"nextStep", stepName(GetMovePathNextStepKind()), true});
	if (m_PathSizeAtAdoption > 0) {
		number("routeCost", std::round(m_PathCostAtAdoption * 10.0F) / 10.0);
		number("routePointsTaken", m_PathSizeAtAdoption);
	}
	if (!m_MovePath.empty()) {
		number("nextX", std::floor(m_MovePath.front().m_X));
		number("nextY", std::floor(m_MovePath.front().m_Y));
	}
	// What the scripts have told the engine (AIRetreat, AIFlank, AI_StuckForTime, SandboxAttack and the like), in name order so the line reads the same each time.
	std::vector<std::pair<std::string, std::string>> scriptValues;
	for (const auto& [key, value]: GetNumberValueMap()) {
		if (key.rfind("AI", 0) == 0 || key.rfind("Sandbox", 0) == 0) {
			std::ostringstream text;
			text << value;
			scriptValues.emplace_back(key, text.str());
		}
	}
	size_t numbers = scriptValues.size();
	for (const auto& [key, value]: GetStringValueMap()) {
		if (key.rfind("AI", 0) == 0 || key.rfind("Sandbox", 0) == 0) {
			scriptValues.emplace_back(key, value);
		}
	}
	std::sort(scriptValues.begin(), scriptValues.begin() + numbers);
	std::sort(scriptValues.begin() + numbers, scriptValues.end());
	for (size_t i = 0; i < scriptValues.size(); ++i) {
		fields.push_back({scriptValues[i].first, scriptValues[i].second, i >= numbers});
	}
}

std::string Actor::DescribeDebugState(bool json) const {
	std::vector<DebugStateField> fields;
	GetDebugState(fields);
	std::string out = json ? "{" : "";
	for (size_t i = 0; i < fields.size(); ++i) {
		const DebugStateField& field = fields[i];
		if (json) {
			std::string value = field.Value;
			if (field.Text) {
				std::string escaped = "\"";
				for (char c: value) {
					if (c == '"' || c == '\\') {
						escaped += '\\';
					}
					escaped += (c == '\n' || c == '\r') ? ' ' : c;
				}
				value = escaped + "\"";
			}
			out += (i > 0 ? ", \"" : "\"") + field.Name + "\": " + value;
		} else {
			out += (i > 0 ? ", " : "") + field.Name + " " + field.Value;
		}
	}
	return json ? out + "}" : out;
}
