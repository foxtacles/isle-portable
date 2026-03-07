# Updated Plan: Third-Person Camera with Animation Reuse

## Context

The original plan was written before the multiplayer animation system was extended to support multiple walk/idle animations, emotes, and vehicle ride animations. This updated plan accounts for those changes and replaces the original.

The goal remains the same: reuse the multiplayer animation system for the local player to enable a third-person camera as a feature of the multiplayer extension, with minimal core game changes. The camera works independently of network connectivity.

## What Changed Since the Original Plan

- **AnimCache struct** added to RemotePlayer with move semantics and lazy caching via `GetOrBuildAnimCache()`
- **6 walk animations**, **3 idle animations**, **2 emotes** defined in `protocol.h` with lookup tables
- **NetworkManager** gained `SetWalkAnimation()`, `SetIdleAnimation()`, `SendEmote()` APIs with `m_localWalkAnimId`/`m_localIdleAnimId` tracking
- **Emote one-shot playback** added to RemotePlayer (three-state: moving/emote/idle)
- **Vehicle ride animations** for small vehicles, model swaps for large vehicles
- **WASM exports** and **PlatformCallbacks** interface added

---

## Implementation Summary

### Step 1: Extract AnimUtils

Moved `AnimCache`, `BuildROIMap`, and `AssignROIIndices` from `remoteplayer.cpp` into shared utilities.

- **Created `extensions/include/extensions/multiplayer/animutils.h`** — `AnimUtils::AnimCache` struct, `AnimUtils::BuildROIMap()`, `AnimUtils::GetOrBuildAnimCache()`
- **Created `extensions/src/multiplayer/animutils.cpp`** — Moved `AssignROIIndices()` (file-static) and `BuildROIMap()` bodies
- **Modified `remoteplayer.h`** — Replaced nested `AnimCache` with `using AnimCache = AnimUtils::AnimCache;`, removed `BuildROIMap` declaration
- **Modified `remoteplayer.cpp`** — Removed extracted code, delegated to `AnimUtils`

### Step 2: Move Vehicle Static Data to protocol.h

Moved from `remoteplayer.cpp` to `protocol.h` (alongside existing animation name tables):
- `g_vehicleROINames[VEHICLE_COUNT]`
- `g_rideAnimNames[VEHICLE_COUNT]`
- `g_rideVehicleROINames[VEHICLE_COUNT]`
- `IsLargeVehicle()` inline helper

### Step 3: Create ThirdPersonCamera

- **Created `extensions/include/extensions/multiplayer/thirdpersoncamera.h`**
- **Created `extensions/src/multiplayer/thirdpersoncamera.cpp`**

Key behaviors:
- **OnActorEnter**: If actor is UserActor and enabled: make player ROI visible, build walk/idle anim caches, set camera behind character via `SetWorldTransform(at=(0,2.5,3), dir=(0,-0.3,-1), up=(0,1,0))`. For vehicles, only set camera offset.
- **Tick()**: Per-frame animation driven from `NetworkManager::Tickle()`. Three-state logic: moving → walk anim at 2x speed, emote → one-shot playback, idle → 2.5s delay then loop. Uses `fabsf(GetWorldSpeed())` for movement detection.
- **OnActorExit**: Clear animation/vehicle state.
- **SetWalkAnimId/SetIdleAnimId/TriggerEmote**: Same pattern as RemotePlayer.

### Step 4: Wire ThirdPersonCamera into NetworkManager

- Added `ThirdPersonCamera m_thirdPersonCamera` member
- Added `ThirdPersonCamera& GetThirdPersonCamera()` accessor
- Forward animation selection calls and world enable/disable events
- `Tickle()` calls `m_thirdPersonCamera.Tick(0.016f)` before the transport guard so it runs without network

### Step 5: Add Extension Hooks and Core Game Hooks

- Added `HandleActorEnter`, `HandleActorExit`, `HandlePostApplyTransform`, `ShouldInvertMovement` to `MultiplayerExt`
- Added matching `constexpr auto` pointers in both `#ifdef EXTENSIONS` and `#else` blocks
- Implemented hooks to forward to `ThirdPersonCamera`
- Third-person camera enabled by default (toggled via WASM export, no INI config)
- Modified `extensions.h` `Extension::Call` to handle both void and non-void return types via `if constexpr`

Core game hooks (all use `Extension<MultiplayerExt>::Call(...)` pattern):
- `islepathactor.cpp` end of `Enter()`: `HandleActorEnter`
- `islepathactor.cpp` start of `Exit()`: `HandleActorExit`
- `legopathactor.cpp` end of `ApplyTransform()`: `HandlePostApplyTransform`
- `legopathactor.cpp` in `CalculateTransform()`: `ShouldInvertMovement` — negates direction before `CalculateNewPosDir` and negates `newDir` after, fixing forward/backward movement inversion

### Step 6: Movement Direction Fix (ShouldInvertMovement)

After `Enter()`'s `TurnAround()`, the ROI direction is negated. This makes the mesh face the correct visual direction (mesh faces `-z` in local space), but the NavController drives movement along the ROI direction, so pressing "forward" moves the character backward.

The fix: `ShouldInvertMovement` returns TRUE when the third-person camera is active. In `CalculateTransform()`:
1. **Before** `CalculateNewPosDir`: negate `dir` so movement goes in the visual forward direction
2. **After** `CalculateNewPosDir`: negate `newDir` back so the ROI direction written by `ApplyTransform` remains in the TurnAround-negated form (preserving correct mesh facing)

This avoids oscillation: every frame, the ROI direction stays negated (correct for visuals), while the movement position is computed using the un-negated direction (correct for controls).

### Step 7: Add WASM Toggle Export

- Added `mp_toggle_third_person()` export to `wasm_exports.cpp`
- Added `animutils.cpp` and `thirdpersoncamera.cpp` to CMakeLists.txt

---

## Known Issues

### 1. Initial spawn facing direction — FIXED
**Root cause:** `OnActorEnter()` made the player ROI visible and built animation caches, but never applied an animation frame. `Tick()` wouldn't apply idle animation until `m_idleTime >= 2.5f`, leaving bones in uninitialized state.
**Fix:** Added `ApplyIdleFrame0()` helper that applies idle animation at t=0 to reset all bones. Called at end of `OnActorEnter()` after building caches.

### 2. Vehicle enter/leave — FIXED
**Root cause (enter):** Vehicle path never hid the old walking character ROI. Dashboard overlay remained visible.
**Root cause (exit):** `OnActorExit()` did full teardown. Restored walking character never got `HandleActorEnter` called.
**Fix:**
- On vehicle enter: hide old player ROI, set up camera, build ride animation for small vehicles (Bike/Skateboard/Motorcycle), defer dashboard removal
- On vehicle exit: deferred reinit (`m_needsReinit` flag) — keeps `m_active = true`, `Tick()` calls `ReinitForCharacter()` when UserActor is back to a non-vehicle
- Dashboard removal: deferred `RemoveFromCurrentWorld()` in `Tick()` removes the parent dashboard presenter
- Ride animations: `BuildRideAnimation()` implemented following `RemotePlayer::EnterVehicle()` pattern

### 3. "Hat Tip" emote distorts character mesh — PARTIALLY FIXED / DEFERRED
**Observed:** Distortion in last ~0.5-1s of Hat Tip (`CNs012xx`). Wave (`CNs011xx`) works fine. Only on local player, not remote.
**Partial fix:** `ApplyIdleFrame0()` at emote completion resets bones to neutral pose (fixes post-emote distortion).
**Deferred:** Mid-emote distortion likely caused by dual transform conflict — game engine's `ApplyTransform()` and extension's `ApplyAnimationTransformation()` both modifying the same ROI. Hat Tip likely has root-level keyframes that conflict with engine transforms; Wave only animates child bones. Remote players are unaffected because their ROI is not driven by the game engine.

### 4. Building enter/return loses character ROI — FIXED
**Root cause:** `OnWorldDisabled()` set `m_active = false`. `OnWorldEnabled()` never restored it. `Enter()` may not be called again after world re-enable.
**Fix:** `OnWorldEnabled()` now calls `ReinitForCharacter()` after clearing stale caches — re-acquires ROI, rebuilds caches, sets up camera.

---

## Files Summary

| File | Action |
|------|--------|
| `extensions/include/extensions/multiplayer/animutils.h` | CREATE |
| `extensions/src/multiplayer/animutils.cpp` | CREATE |
| `extensions/include/extensions/multiplayer/thirdpersoncamera.h` | CREATE |
| `extensions/src/multiplayer/thirdpersoncamera.cpp` | CREATE |
| `extensions/include/extensions/extensions.h` | MODIFY — void/non-void `if constexpr` in `Extension::Call` |
| `extensions/include/extensions/multiplayer/remoteplayer.h` | MODIFY — use AnimUtils::AnimCache, remove BuildROIMap |
| `extensions/src/multiplayer/remoteplayer.cpp` | MODIFY — remove extracted code, use AnimUtils |
| `extensions/include/extensions/multiplayer/protocol.h` | MODIFY — add vehicle name arrays |
| `extensions/include/extensions/multiplayer/networkmanager.h` | MODIFY — add ThirdPersonCamera member |
| `extensions/src/multiplayer/networkmanager.cpp` | MODIFY — wire camera to animation APIs |
| `extensions/include/extensions/multiplayer.h` | MODIFY — add hook declarations incl. ShouldInvertMovement |
| `extensions/src/multiplayer.cpp` | MODIFY — implement hooks, enable camera by default |
| `extensions/src/multiplayer/platforms/emscripten/wasm_exports.cpp` | MODIFY — add toggle export |
| `CMakeLists.txt` | MODIFY — add new sources |
| `LEGO1/lego/legoomni/src/actors/islepathactor.cpp` | MODIFY — +2 hook lines (Enter/Exit) |
| `LEGO1/lego/legoomni/src/paths/legopathactor.cpp` | MODIFY — +1 hook line (ApplyTransform), +ShouldInvertMovement in CalculateTransform |

## Key Design Decisions

### Movement direction inversion via CalculateTransform hook
Rather than modifying the ROI direction (which gets overwritten by the path system's spline evaluation and edge transitions) or trying to change NavController velocity mapping, the direction is inverted symmetrically around `CalculateNewPosDir`. This ensures: (a) no oscillation between frames, (b) the ROI direction stays consistent for visual facing and camera positioning, (c) the path boundary and spline systems are unaffected.

### Why not modify `IslePathActor`'s inheritance?
Adding `LegoAnimActor` to `IslePathActor`'s class hierarchy would change vtable layout and class size (`DECOMP_SIZE_ASSERT(IslePathActor, 0x160)` would break). The multiplayer extension's approach of directly calling `ApplyAnimationTransformation()` avoids this entirely.

### ROI map index sharing
`AssignROIIndices` modifies shared `LegoAnimNodeData::m_roiIndex` fields on the `LegoAnim` tree nodes. This is safe because all minifig characters share the same skeleton structure, so `AssignROIIndices` produces identical index assignments regardless of which character's ROI is mapped.

### Camera offset via `SetWorldTransform`
`SetWorldTransform(at=(0,2.5,3), dir=(0,-0.3,-1), up=(0,1,0))` sets both `m_currentTransform` and `m_originalTransform`. After TurnAround, +z in ROI-local space points behind the visual model, so at=(0,2.5,3) places the camera behind the character. The `RotateZ` tilt effect during turning composes correctly because it resets from `m_originalTransform`.

## Verification

1. **Build**: `cmake --build build` — no compilation errors
2. **Tests**: `ctest --test-dir build` — no regressions
3. **Remote player animations**: With multiplayer connected, verify walk/idle/emote animations still work after the AnimUtils refactor
4. **Third-person on foot**: Walk around — player visible from behind, selected walk animation plays, idle after 2.5s
5. **Animation switching**: Change walk/idle animation via WASM exports — local player model updates
6. **Emotes**: Trigger emote while stationary — plays on local model, interrupted by movement
7. **Vehicle transitions**: Enter small vehicle — ride animation plays, dashboard hidden. Exit — walk animation resumes with third-person camera
8. **Toggle**: `mp_toggle_third_person` switches between first/third person — first-person restores original behavior
9. **Without network**: Camera works with multiplayer extension enabled but not connected
10. **Building transitions**: Enter/leave buildings — character ROI preserved with third-person camera active on return
