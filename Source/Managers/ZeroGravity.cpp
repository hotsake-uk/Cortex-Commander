#include "ZeroGravity.h"
#include "Scene.h"
#include "SceneMan.h"
#include "Vector.h"

using namespace RTE;

bool ZeroGravity::s_TerrainFollowsGravity = true;
bool ZeroGravity::s_LiquidsFloat = true;
bool ZeroGravity::s_CountZones = true;
bool ZeroGravity::s_JetpackAnyDirection = true;
bool ZeroGravity::s_AIFlies = true;
bool ZeroGravity::s_PushOff = true;
float ZeroGravity::s_FlySpeed = 6.0F;
float ZeroGravity::s_PushSpeed = 4.0F;

bool ZeroGravity::SceneWeightless() {
	return g_SceneMan.GetScene() && g_SceneMan.GetGlobalAcc().MagnitudeIsLessThan(c_Weightless);
}

bool ZeroGravity::GroundWeightlessAt(const Vector& point) {
	return GroundGravityAt(point).MagnitudeIsLessThan(c_Weightless);
}

Vector ZeroGravity::GroundGravityAt(const Vector& point) {
	Scene* scene = g_SceneMan.GetScene();
	if (!scene) {
		return Vector();
	}
	if (s_CountZones) {
		if (Scene::Area* zone = scene->GetArea("NoGravityArea"); zone && zone->IsInside(point)) {
			return Vector();
		}
	}
	return scene->GetGlobalAcc();
}

bool ZeroGravity::UnitsWeightless() {
	return SceneWeightless();
}

void ZeroGravity::ResetSettings() {
	s_TerrainFollowsGravity = true;
	s_LiquidsFloat = true;
	s_CountZones = true;
	s_JetpackAnyDirection = true;
	s_AIFlies = true;
	s_PushOff = true;
	s_FlySpeed = 6.0F;
	s_PushSpeed = 4.0F;
}
