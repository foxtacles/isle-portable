#pragma once

#include "extensions/common/characteranimator.h"
#include "extensions/extensions.h"
#include "extensions/thirdpersoncamera/displayactor.h"
#include "extensions/thirdpersoncamera/inputhandler.h"
#include "extensions/thirdpersoncamera/orbitcamera.h"
#include "mxtypes.h"

#include <SDL3/SDL_events.h>
#include <cstdint>
#include <map>
#include <string>

class IslePathActor;
class LegoEntity;
class LegoEventNotificationParam;
class LegoNavController;
class LegoPathActor;
class LegoROI;
class LegoWorld;
class Vector3;

namespace Extensions
{

class ThirdPersonCameraExt {
public:
	ThirdPersonCameraExt();

	// ─── Extension interface (static) ────────────────────────────────

	static void Initialize();

	// Hooks (called from LEGO1 call sites)
	static void HandleActorEnter(IslePathActor* p_actor);
	static void HandleActorExit(IslePathActor* p_actor);
	static void HandleCamAnimEnd(LegoPathActor* p_actor);
	static void OnSDLEvent(SDL_Event* p_event);
	static MxBool IsThirdPersonCameraActive();
	static MxBool HandleTouchInput(SDL_Event* p_event);
	static MxBool HandleNavOverride(
		LegoNavController* p_nav,
		const Vector3& p_curPos,
		const Vector3& p_curDir,
		Vector3& p_newPos,
		Vector3& p_newDir,
		float p_deltaTime
	);
	static MxBool HandleWorldEnable(LegoWorld* p_world, MxBool p_enable);

	// Self-customization click (standalone mode only; multiplayer overrides)
	static MxBool HandleROIClick(LegoROI* p_rootROI, LegoEventNotificationParam& p_param);

	// Clone check for display actor
	static MxBool IsClonedCharacter(const char* p_name);

	// Called from game initialization
	static void HandleCreate();

	// SDL event routing (LEGO1_EXPORT for WASM)
	LEGO1_EXPORT static void HandleSDLEvent(SDL_Event* p_event);

	static ThirdPersonCameraExt* GetCamera();

	static std::map<std::string, std::string> options;
	static bool enabled;

	// ─── Camera instance interface ───────────────────────────────────

	void Enable();
	void Disable();
	bool IsEnabled() const { return m_enabled; }
	bool IsActive() const { return m_active; }

	// Core hooks
	void OnActorEnter(IslePathActor* p_actor);
	void OnActorExit(IslePathActor* p_actor);
	void OnCamAnimEnd(LegoPathActor* p_actor);

	// Called every frame from the extension's tickle adapter
	void Tick(float p_deltaTime);

	// Animation selection
	void SetWalkAnimId(uint8_t p_walkAnimId);
	void SetIdleAnimId(uint8_t p_idleAnimId);
	void TriggerEmote(uint8_t p_emoteId);
	bool IsInMultiPartEmote() const;
	int8_t GetFrozenEmoteId() const;

	void SetDisplayActorIndex(uint8_t p_displayActorIndex) { m_display.SetDisplayActorIndex(p_displayActorIndex); }
	uint8_t GetDisplayActorIndex() const { return m_display.GetDisplayActorIndex(); }
	LegoROI* GetDisplayROI() const { return m_display.GetDisplayROI(); }
	Common::CustomizeState& GetCustomizeState() { return m_display.GetCustomizeState(); }

	void ApplyCustomizeChange(uint8_t p_changeType, uint8_t p_partIndex) { m_display.ApplyCustomizeChange(p_changeType, p_partIndex); }
	void SetClickAnimObjectId(MxU32 p_clickAnimObjectId) { m_animator.SetClickAnimObjectId(p_clickAnimObjectId); }
	void StopClickAnimation();
	bool IsInVehicle() const { return m_animator.IsInVehicle(); }

	void OnWorldEnabled(LegoWorld* p_world);
	void OnWorldDisabled(LegoWorld* p_world);

	// Camera-relative movement override (called from nav controller hook)
	MxBool HandleCameraRelativeMovement(
		LegoNavController* p_nav,
		const Vector3& p_curPos,
		const Vector3& p_curDir,
		Vector3& p_newPos,
		Vector3& p_newDir,
		float p_deltaTime
	);

	// Free camera input handling (instance method)
	void HandleSDLEventImpl(SDL_Event* p_event);

	// Auto-switch flags (set by HandleSDLEventImpl, consumed by caller)
	bool ConsumeAutoDisable() { return m_input.ConsumeAutoDisable(); }
	bool ConsumeAutoEnable() { return m_input.ConsumeAutoEnable(); }

	float GetOrbitDistance() const { return m_orbit.GetOrbitDistance(); }
	void SetOrbitDistance(float p_distance) { m_orbit.SetOrbitDistance(p_distance); }
	void ResetTouchState() { m_input.ResetTouchState(); }

	// Finger-claiming API for split-screen touch zones (left=movement, right=camera)
	bool TryClaimFinger(const SDL_TouchFingerEvent& event) { return m_input.TryClaimFinger(event, m_active); }
	bool TryReleaseFinger(SDL_FingerID id) { return m_input.TryReleaseFinger(id); }
	bool IsFingerTracked(SDL_FingerID id) const { return m_input.IsFingerTracked(id); }

	// Display actor frozen state (set by multiplayer when explicitly overriding)
	void FreezeDisplayActor() { m_display.FreezeDisplayActor(); }
	void UnfreezeDisplayActor() { m_display.UnfreezeDisplayActor(); }
	bool IsDisplayActorFrozen() const { return m_display.IsDisplayActorFrozen(); }

	LegoROI* GetPlayerROI() const { return m_playerROI; }

	static constexpr float CAMERA_ZONE_X = ThirdPersonCamera::InputHandler::CAMERA_ZONE_X;
	static constexpr float MIN_DISTANCE = ThirdPersonCamera::OrbitCamera::MIN_DISTANCE;

private:
	void ReinitForCharacter();

	// Extension static state
	static ThirdPersonCameraExt* s_camera;
	static bool s_registered;
	static bool s_inIsleWorld;

	// Sub-components
	ThirdPersonCamera::OrbitCamera m_orbit;
	ThirdPersonCamera::InputHandler m_input;
	ThirdPersonCamera::DisplayActor m_display;
	Common::CharacterAnimator m_animator;

	// Camera instance state
	bool m_enabled;
	bool m_active;
	bool m_pendingWorldTransition;
	LegoROI* m_playerROI;
};

namespace TP {
#ifdef EXTENSIONS
constexpr auto HandleCreate = &ThirdPersonCameraExt::HandleCreate;
constexpr auto HandleWorldEnable = &ThirdPersonCameraExt::HandleWorldEnable;
constexpr auto HandleActorEnter = &ThirdPersonCameraExt::HandleActorEnter;
constexpr auto HandleActorExit = &ThirdPersonCameraExt::HandleActorExit;
constexpr auto HandleCamAnimEnd = &ThirdPersonCameraExt::HandleCamAnimEnd;
constexpr auto HandleSDLEvent = &ThirdPersonCameraExt::OnSDLEvent;
constexpr auto IsThirdPersonCameraActive = &ThirdPersonCameraExt::IsThirdPersonCameraActive;
constexpr auto HandleTouchInput = &ThirdPersonCameraExt::HandleTouchInput;
constexpr auto HandleNavOverride = &ThirdPersonCameraExt::HandleNavOverride;
constexpr auto HandleROIClick = &ThirdPersonCameraExt::HandleROIClick;
constexpr auto IsClonedCharacter = &ThirdPersonCameraExt::IsClonedCharacter;
#else
constexpr decltype(&ThirdPersonCameraExt::HandleCreate) HandleCreate = nullptr;
constexpr decltype(&ThirdPersonCameraExt::HandleWorldEnable) HandleWorldEnable = nullptr;
constexpr decltype(&ThirdPersonCameraExt::HandleActorEnter) HandleActorEnter = nullptr;
constexpr decltype(&ThirdPersonCameraExt::HandleActorExit) HandleActorExit = nullptr;
constexpr decltype(&ThirdPersonCameraExt::HandleCamAnimEnd) HandleCamAnimEnd = nullptr;
constexpr decltype(&ThirdPersonCameraExt::OnSDLEvent) HandleSDLEvent = nullptr;
constexpr decltype(&ThirdPersonCameraExt::IsThirdPersonCameraActive) IsThirdPersonCameraActive = nullptr;
constexpr decltype(&ThirdPersonCameraExt::HandleTouchInput) HandleTouchInput = nullptr;
constexpr decltype(&ThirdPersonCameraExt::HandleNavOverride) HandleNavOverride = nullptr;
constexpr decltype(&ThirdPersonCameraExt::HandleROIClick) HandleROIClick = nullptr;
constexpr decltype(&ThirdPersonCameraExt::IsClonedCharacter) IsClonedCharacter = nullptr;
#endif
} // namespace TP

}; // namespace Extensions
