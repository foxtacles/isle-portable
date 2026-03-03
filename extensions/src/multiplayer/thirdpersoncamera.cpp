#include "extensions/multiplayer/thirdpersoncamera.h"

#include "anim/legoanim.h"
#include "islepathactor.h"
#include "legocameracontroller.h"
#include "legoworld.h"
#include "misc.h"
#include "misc/legotree.h"
#include "mxgeometry/mxgeometry3d.h"
#include "mxgeometry/mxmatrix.h"
#include "realtime/realtime.h"
#include "roi/legoroi.h"

#include <cmath>

using namespace Multiplayer;

ThirdPersonCamera::ThirdPersonCamera()
	: m_enabled(false), m_active(false), m_playerROI(nullptr), m_walkAnimId(0), m_idleAnimId(0),
	  m_walkAnimCache(nullptr), m_idleAnimCache(nullptr), m_animTime(0.0f), m_idleTime(0.0f), m_idleAnimTime(0.0f),
	  m_wasMoving(false), m_emoteAnimCache(nullptr), m_emoteTime(0.0f), m_emoteDuration(0.0f), m_emoteActive(false),
	  m_currentVehicleType(VEHICLE_NONE), m_rideAnim(nullptr), m_rideRoiMap(nullptr), m_rideRoiMapSize(0),
	  m_rideVehicleROI(nullptr)
{
}

void ThirdPersonCamera::Enable()
{
	m_enabled = true;
}

void ThirdPersonCamera::Disable()
{
	m_enabled = false;
	m_active = false;
	m_playerROI = nullptr;
	ClearRideAnimation();
	m_animCacheMap.clear();
	m_walkAnimCache = nullptr;
	m_idleAnimCache = nullptr;
	m_emoteAnimCache = nullptr;
	m_emoteActive = false;
}

void ThirdPersonCamera::OnActorEnter(IslePathActor* p_actor)
{
	if (!m_enabled) {
		return;
	}

	LegoPathActor* userActor = UserActor();
	if (static_cast<LegoPathActor*>(p_actor) != userActor) {
		return;
	}

	m_playerROI = userActor->GetROI();
	if (!m_playerROI) {
		return;
	}

	m_active = true;

	// Detect if we're entering a vehicle
	int8_t vehicleType = DetectVehicleType(userActor);
	m_currentVehicleType = vehicleType;

	if (vehicleType != VEHICLE_NONE) {
		SetupCamera(userActor);
		return;
	}

	// Make the player model visible (Enter() hid it for first-person)
	m_playerROI->SetVisibility(TRUE);

	// Build animation caches
	m_walkAnimCache = GetOrBuildAnimCache(g_walkAnimNames[m_walkAnimId]);
	m_idleAnimCache = GetOrBuildAnimCache(g_idleAnimNames[m_idleAnimId]);

	// Reset animation state
	m_animTime = 0.0f;
	m_idleTime = 0.0f;
	m_idleAnimTime = 0.0f;
	m_wasMoving = false;
	m_emoteActive = false;

	SetupCamera(userActor);
}

void ThirdPersonCamera::OnActorExit(IslePathActor* p_actor)
{
	if (!m_active) {
		return;
	}

	LegoPathActor* userActor = UserActor();
	if (static_cast<LegoPathActor*>(p_actor) != userActor) {
		return;
	}

	ClearRideAnimation();
	m_walkAnimCache = nullptr;
	m_idleAnimCache = nullptr;
	m_emoteAnimCache = nullptr;
	m_emoteActive = false;
	m_currentVehicleType = VEHICLE_NONE;
	m_playerROI = nullptr;
	m_active = false;
}

void ThirdPersonCamera::OnPostApplyTransform(LegoPathActor* p_actor)
{
	// No-op: we don't modify the ROI transform. Animation is driven by Tick().
	(void) p_actor;
}

void ThirdPersonCamera::Tick(float p_deltaTime)
{
	if (!m_active || !m_playerROI) {
		return;
	}

	// Vehicles: no character animation
	if (m_currentVehicleType != VEHICLE_NONE) {
		return;
	}

	LegoPathActor* userActor = UserActor();
	if (!userActor) {
		return;
	}

	// Determine the active walk/ride animation and its ROI map
	LegoAnim* walkAnim = nullptr;
	LegoROI** walkRoiMap = nullptr;
	MxU32 walkRoiMapSize = 0;

	if (m_currentVehicleType != VEHICLE_NONE && m_rideAnim && m_rideRoiMap) {
		walkAnim = m_rideAnim;
		walkRoiMap = m_rideRoiMap;
		walkRoiMapSize = m_rideRoiMapSize;
	}
	else if (m_walkAnimCache && m_walkAnimCache->anim && m_walkAnimCache->roiMap) {
		walkAnim = m_walkAnimCache->anim;
		walkRoiMap = m_walkAnimCache->roiMap;
		walkRoiMapSize = m_walkAnimCache->roiMapSize;
	}

	// Ensure visibility of all mapped ROIs
	if (walkRoiMap) {
		for (MxU32 i = 1; i < walkRoiMapSize; i++) {
			if (walkRoiMap[i] != nullptr) {
				walkRoiMap[i]->SetVisibility(TRUE);
			}
		}
	}
	if (m_idleAnimCache && m_idleAnimCache->roiMap) {
		for (MxU32 i = 1; i < m_idleAnimCache->roiMapSize; i++) {
			if (m_idleAnimCache->roiMap[i] != nullptr) {
				m_idleAnimCache->roiMap[i]->SetVisibility(TRUE);
			}
		}
	}

	bool inVehicle = (m_currentVehicleType != VEHICLE_NONE);
	float speed = userActor->GetWorldSpeed();
	bool isMoving = inVehicle || fabsf(speed) > 0.01f;

	// Movement interrupts emotes
	if (isMoving && m_emoteActive) {
		m_emoteActive = false;
		m_emoteAnimCache = nullptr;
	}

	if (isMoving) {
		if (!walkAnim || !walkRoiMap) {
			return;
		}

		if (fabsf(speed) > 0.01f) {
			m_animTime += p_deltaTime * 2000.0f;
		}
		float duration = (float) walkAnim->GetDuration();
		if (duration > 0.0f) {
			float timeInCycle = m_animTime - duration * floorf(m_animTime / duration);

			MxMatrix transform(m_playerROI->GetLocal2World());
			LegoTreeNode* root = walkAnim->GetRoot();
			for (LegoU32 i = 0; i < root->GetNumChildren(); i++) {
				LegoROI::ApplyAnimationTransformation(root->GetChild(i), transform, (LegoTime) timeInCycle, walkRoiMap);
			}
		}
		m_wasMoving = true;
		m_idleTime = 0.0f;
		m_idleAnimTime = 0.0f;
	}
	else if (m_emoteActive && m_emoteAnimCache && m_emoteAnimCache->anim && m_emoteAnimCache->roiMap) {
		m_emoteTime += p_deltaTime * 1000.0f;

		if (m_emoteTime >= m_emoteDuration) {
			m_emoteActive = false;
			m_emoteAnimCache = nullptr;
			m_wasMoving = false;
			m_idleTime = 0.0f;
			m_idleAnimTime = 0.0f;
		}
		else {
			MxMatrix transform(m_playerROI->GetLocal2World());
			LegoTreeNode* root = m_emoteAnimCache->anim->GetRoot();
			for (LegoU32 i = 0; i < root->GetNumChildren(); i++) {
				LegoROI::ApplyAnimationTransformation(
					root->GetChild(i),
					transform,
					(LegoTime) m_emoteTime,
					m_emoteAnimCache->roiMap
				);
			}
		}
	}
	else if (m_idleAnimCache && m_idleAnimCache->anim && m_idleAnimCache->roiMap) {
		if (m_wasMoving) {
			m_wasMoving = false;
			m_idleTime = 0.0f;
			m_idleAnimTime = 0.0f;
		}

		m_idleTime += p_deltaTime;

		if (m_idleTime >= 2.5f) {
			m_idleAnimTime += p_deltaTime * 1000.0f;
		}

		float duration = (float) m_idleAnimCache->anim->GetDuration();
		if (duration > 0.0f) {
			float timeInCycle = m_idleAnimTime - duration * floorf(m_idleAnimTime / duration);

			MxMatrix transform(m_playerROI->GetLocal2World());
			LegoTreeNode* root = m_idleAnimCache->anim->GetRoot();
			for (LegoU32 i = 0; i < root->GetNumChildren(); i++) {
				LegoROI::ApplyAnimationTransformation(
					root->GetChild(i),
					transform,
					(LegoTime) timeInCycle,
					m_idleAnimCache->roiMap
				);
			}
		}
	}
}

void ThirdPersonCamera::SetWalkAnimId(uint8_t p_id)
{
	if (p_id >= g_walkAnimCount) {
		return;
	}

	if (p_id != m_walkAnimId) {
		m_walkAnimId = p_id;
		if (m_active) {
			m_walkAnimCache = GetOrBuildAnimCache(g_walkAnimNames[m_walkAnimId]);
		}
	}
}

void ThirdPersonCamera::SetIdleAnimId(uint8_t p_id)
{
	if (p_id >= g_idleAnimCount) {
		return;
	}

	if (p_id != m_idleAnimId) {
		m_idleAnimId = p_id;
		if (m_active) {
			m_idleAnimCache = GetOrBuildAnimCache(g_idleAnimNames[m_idleAnimId]);
		}
	}
}

void ThirdPersonCamera::TriggerEmote(uint8_t p_emoteId)
{
	if (p_emoteId >= g_emoteAnimCount || !m_active) {
		return;
	}

	LegoPathActor* userActor = UserActor();
	if (!userActor || fabsf(userActor->GetWorldSpeed()) > 0.01f) {
		return;
	}

	AnimCache* cache = GetOrBuildAnimCache(g_emoteAnimNames[p_emoteId]);
	if (!cache || !cache->anim) {
		return;
	}

	m_emoteAnimCache = cache;
	m_emoteTime = 0.0f;
	m_emoteDuration = (float) cache->anim->GetDuration();
	m_emoteActive = true;
}

void ThirdPersonCamera::OnWorldEnabled(LegoWorld* p_world)
{
	if (!m_enabled || !p_world) {
		return;
	}

	m_animCacheMap.clear();
	m_walkAnimCache = nullptr;
	m_idleAnimCache = nullptr;
	m_emoteAnimCache = nullptr;
	m_emoteActive = false;
}

void ThirdPersonCamera::OnWorldDisabled(LegoWorld* p_world)
{
	if (!p_world) {
		return;
	}

	m_active = false;
	m_playerROI = nullptr;
	ClearRideAnimation();
	m_animCacheMap.clear();
	m_walkAnimCache = nullptr;
	m_idleAnimCache = nullptr;
	m_emoteAnimCache = nullptr;
	m_emoteActive = false;
}

ThirdPersonCamera::AnimCache* ThirdPersonCamera::GetOrBuildAnimCache(const char* p_animName)
{
	return AnimUtils::GetOrBuildAnimCache(m_animCacheMap, m_playerROI, p_animName);
}

void ThirdPersonCamera::SetupCamera(LegoPathActor* p_actor)
{
	LegoWorld* world = CurrentWorld();
	if (!world || !world->GetCameraController()) {
		return;
	}

	// After Enter()'s TurnAround, the ROI direction is negated.
	// The mesh faces -z (local) = +path_forward (correct visual facing).
	// +z in ROI-local is the negated direction, i.e. behind the visual model.
	// at=(0,2.5,3) places the camera behind the character and
	// dir=(0,-0.3,-1) looks toward it.
	// Movement inversion is handled by ShouldInvertMovement in CalculateTransform.
	Mx3DPointFloat at(0.0f, 2.5f, 3.0f);
	Mx3DPointFloat dir(0.0f, -0.3f, -1.0f);
	Mx3DPointFloat up(0.0f, 1.0f, 0.0f);

	world->GetCameraController()->SetWorldTransform(at, dir, up);
	p_actor->TransformPointOfView();
}

int8_t ThirdPersonCamera::DetectVehicleType(LegoPathActor* p_actor)
{
	static const struct {
		const char* className;
		int8_t vehicleType;
	} vehicleMap[] = {
		{"Helicopter", VEHICLE_HELICOPTER},
		{"Jetski", VEHICLE_JETSKI},
		{"DuneBuggy", VEHICLE_DUNEBUGGY},
		{"Bike", VEHICLE_BIKE},
		{"SkateBoard", VEHICLE_SKATEBOARD},
		{"Motorcycle", VEHICLE_MOTOCYCLE},
		{"TowTrack", VEHICLE_TOWTRACK},
		{"Ambulance", VEHICLE_AMBULANCE},
	};

	if (!p_actor) {
		return VEHICLE_NONE;
	}

	for (const auto& entry : vehicleMap) {
		if (p_actor->IsA(entry.className)) {
			return entry.vehicleType;
		}
	}
	return VEHICLE_NONE;
}

void ThirdPersonCamera::BuildRideAnimation(int8_t p_vehicleType)
{
	(void) p_vehicleType;
}

void ThirdPersonCamera::ClearRideAnimation()
{
	if (m_rideRoiMap) {
		delete[] m_rideRoiMap;
		m_rideRoiMap = nullptr;
		m_rideRoiMapSize = 0;
	}
	m_rideVehicleROI = nullptr;
	m_rideAnim = nullptr;
	m_currentVehicleType = VEHICLE_NONE;
}
