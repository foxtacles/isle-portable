# Plan: Reuse Multiplayer Animation System for Local Player Third-Person Camera

## Context

LEGO Island's local player (`IslePathActor`) is hidden during gameplay — the camera sits at the entity's position (first-person) and the minifig model is invisible. The multiplayer extension already has a complete animation system for remote players: it builds ROI maps, looks up walk/idle animations, and applies bone transforms each frame via `LegoROI::ApplyAnimationTransformation()`. This plan evaluates reusing that system for the local player to enable a third-person camera as a feature of the multiplayer extension, with minimal changes to core game code.

**Activation model**: The third-person camera works independently of network connectivity — it's a local visual feature available whenever the multiplayer extension is enabled.

## Feasibility: YES

1. **The player ROI already exists** — just hidden via `SetVisibility(FALSE)`. No cloning needed.
2. **Walk/idle animations are available** — `CNs001xx` (walk) and `CNs008xx` (idle) are loaded as presenters in the Isle world. The extension already looks these up.
3. **`BuildROIMap` + `AssignROIIndices` are ROI-agnostic** — they take any `LegoROI*` and `LegoAnim*`, so they work on the local player's ROI identically.
4. **Camera offset injection point exists** — `LegoCameraController::m_currentTransform` is multiplied into the camera matrix at `legocameracontroller.cpp:171`. Setting it to an offset matrix gives a third-person camera with zero changes to the camera pipeline. `SetWorldTransform()` is public and sets both `m_currentTransform` and `m_originalTransform`.
5. **The extension already ticks every frame** — `NetworkManager::Tickle()` runs at 10ms intervals via `TickleManager`, regardless of connection state.
6. **The `RotateZ` tilt effect composes correctly** — `LegoPathActor::Animate()` calls `RotateZ(angle)` during turning, which resets `m_currentTransform = m_originalTransform` then rotates. Since `SetWorldTransform` sets both fields, the tilt will compose with the third-person offset.

## Architecture

### Extract shared animation utilities from RemotePlayer

Move `AssignROIIndices()` (file-static, `remoteplayer.cpp:236`) and `BuildROIMap()` (member, `remoteplayer.cpp:278` — only uses parameters, no `this` state) into a shared utility. Both `RemotePlayer` and the new `ThirdPersonCamera` will call these.

### New `ThirdPersonCamera` class in the extension

Manages camera offset, player visibility, and walk/idle animation playback for the local player. Composed into `NetworkManager` and driven from its `Tickle()`.

## Detailed Changes

### Extension-side (bulk of work)

#### New files

**`extensions/include/extensions/multiplayer/animutils.h`** + **`extensions/src/multiplayer/animutils.cpp`**
- `Multiplayer::AnimUtils::BuildROIMap(LegoAnim*, LegoROI* root, LegoROI* extra, LegoROI**& map, MxU32& size)`
- Contains the `AssignROIIndices` helper (moved from `remoteplayer.cpp`)

**`extensions/include/extensions/multiplayer/thirdpersoncamera.h`** + **`extensions/src/multiplayer/thirdpersoncamera.cpp`**

```cpp
namespace Multiplayer {
class ThirdPersonCamera {
public:
    void Enable(LegoWorld* world);  // Build ROI maps, set camera offset, show player
    void Disable();                 // Restore first-person, hide player
    bool IsEnabled() const;
    void Toggle();                  // Runtime toggle (e.g., key binding)

    // Called from extension hooks:
    void OnActorEnter(IslePathActor* actor);   // After Enter() — undo TurnAround, show ROI, set camera
    void OnActorExit(IslePathActor* actor);    // Before Exit() — teardown
    void OnPostApplyTransform(LegoPathActor* actor); // After ApplyTransform — animate skeleton

private:
    bool m_enabled = false;
    LegoROI* m_playerROI = nullptr;

    // Animation state (same pattern as RemotePlayer)
    LegoAnim* m_walkAnim;       LegoROI** m_walkRoiMap;  MxU32 m_walkRoiMapSize;
    LegoAnim* m_idleAnim;       LegoROI** m_idleRoiMap;  MxU32 m_idleRoiMapSize;
    float m_animTime, m_idleTime, m_idleAnimTime;
    bool m_wasMoving;
};
}
```

Key behaviors:

- **`OnActorEnter()`**: Fires after core `Enter()` completes. Undoes `TurnAround()` (calls it again — it's a self-inverse direction negate). Sets `m_roi->SetVisibility(TRUE)`. Overrides camera via `world->GetCameraController()->SetWorldTransform(offsetAt, offsetDir, offsetUp)` with behind+above offset. Rebuilds animation ROI maps. Re-calls `TransformPointOfView()`.

- **`OnPostApplyTransform()`**: Fires after each frame's `ApplyTransform()`. Gets speed from `UserActor()->GetWorldSpeed()`. Applies walk or idle animation via `LegoROI::ApplyAnimationTransformation()` — identical logic to `RemotePlayer::UpdateAnimation()`.

- **`OnActorExit()`**: Tears down animator state. Core `Exit()` handles visibility/TurnAround restore.

#### Modified extension files

**`extensions/src/multiplayer/remoteplayer.cpp`** — Remove `AssignROIIndices()` and `BuildROIMap()` bodies, call `AnimUtils::BuildROIMap()` instead.

**`extensions/include/extensions/multiplayer/remoteplayer.h`** — Remove `BuildROIMap` declaration (now in AnimUtils).

**`extensions/src/multiplayer/networkmanager.cpp`** — Add `ThirdPersonCamera m_thirdPersonCamera` member. In `Tickle()`, check for toggle key via SDL. In `OnWorldEnabled()`/`OnWorldDisabled()`, enable/disable camera.

**`extensions/include/extensions/multiplayer.h`** — Add 3 new hook declarations:
```cpp
static void HandlePostApplyTransform(LegoPathActor* p_actor);
static void HandleActorEnter(IslePathActor* p_actor);
static void HandleActorExit(IslePathActor* p_actor);
```
Plus corresponding `constexpr auto` pointers following the existing pattern.

**`extensions/src/multiplayer.cpp`** — Read `multiplayer:third person camera` INI option. Wire hooks to `ThirdPersonCamera`.

**`CMakeLists.txt`** — Add `animutils.cpp` and `thirdpersoncamera.cpp` to the extension sources.

### Core game changes (3 single-line hooks)

All follow the existing `Extension<MultiplayerExt>::Call(...)` pattern.

**`LEGO1/lego/legoomni/src/actors/islepathactor.cpp`** — 2 hooks:

```cpp
// At end of Enter() (after line 97):
Extension<MultiplayerExt>::Call(HandleActorEnter, this);

// At start of Exit() (before line 104):
Extension<MultiplayerExt>::Call(HandleActorExit, this);
```

**`LEGO1/lego/legoomni/src/paths/legopathactor.cpp`** — 1 hook:

```cpp
// At end of ApplyTransform() (after line 412):
Extension<MultiplayerExt>::Call(HandlePostApplyTransform, this);
```

**Total core game diff: 3 lines added**, each a single `Extension::Call` invocation. No existing lines modified.

## Key Design Decisions

### Why post-hooks instead of wrapping individual lines?
The extension's `OnActorEnter()` undoes `TurnAround()` and restores visibility after `Enter()` completes. This is cleaner than wrapping `SetVisibility(FALSE)` and `direction *= -1.0f` with conditionals because: (a) fewer core code changes, (b) each core hook is a pure addition, (c) no rendering happens between Enter()'s hide and the extension's show (single function call).

### Why not modify `IslePathActor`'s inheritance?
Adding `LegoAnimActor` to `IslePathActor`'s class hierarchy would change vtable layout and class size (`DECOMP_SIZE_ASSERT(IslePathActor, 0x160)` would break). The multiplayer extension's approach of directly calling `ApplyAnimationTransformation()` avoids this entirely.

### ROI map index sharing
`AssignROIIndices` modifies shared `LegoAnimNodeData::m_roiIndex` fields on the `LegoAnim` tree nodes. If both the local player and remote players call `BuildROIMap` on the same `LegoAnim*` (e.g., `CNs001xx`), the last caller's indices win. However, this is safe because all minifig characters share the same skeleton structure (body, head, larm, rarm, lleg, rleg), so `AssignROIIndices` produces identical index assignments regardless of which character's ROI is mapped. Each caller maintains its own `roiMap` array pointing to different ROI instances, but the indices into those arrays are consistent.

### Camera offset via `SetWorldTransform`
Calling `SetWorldTransform(at=(0, 2.5, -3.0), dir=(0, -0.3, 1.0), up=(0, 1, 0))` sets both `m_currentTransform` and `m_originalTransform`. This persists across frames — the core's `TransformPointOfView()` already uses `m_currentTransform` in its multiplication, so no per-frame camera override is needed. The `RotateZ` tilt effect during turning composes correctly because it resets from `m_originalTransform`.

### Vehicle transitions
When entering a vehicle, its `Enter()` hook fires. The extension detects the actor type (e.g., `IsA("Helicopter")`). For vehicles, the walking animation is disabled but the third-person camera offset can optionally remain. For large vehicles that replace the player model entirely, the extension defers to the vehicle's own visibility management.

## Files Summary

| File | Action | Scope |
|------|--------|-------|
| `extensions/include/extensions/multiplayer/animutils.h` | CREATE | Shared ROI map building |
| `extensions/src/multiplayer/animutils.cpp` | CREATE | Moved from remoteplayer.cpp |
| `extensions/include/extensions/multiplayer/thirdpersoncamera.h` | CREATE | Camera + animation manager |
| `extensions/src/multiplayer/thirdpersoncamera.cpp` | CREATE | Core implementation |
| `extensions/src/multiplayer/remoteplayer.cpp` | MODIFY | Use AnimUtils |
| `extensions/include/extensions/multiplayer/remoteplayer.h` | MODIFY | Remove BuildROIMap decl |
| `extensions/src/multiplayer/networkmanager.cpp` | MODIFY | Tick camera, toggle key |
| `extensions/include/extensions/multiplayer/networkmanager.h` | MODIFY | Add member |
| `extensions/include/extensions/multiplayer.h` | MODIFY | Add 3 hook declarations |
| `extensions/src/multiplayer.cpp` | MODIFY | Read config, wire hooks |
| `CMakeLists.txt` | MODIFY | Add new sources |
| `LEGO1/lego/legoomni/src/actors/islepathactor.cpp` | MODIFY | +2 lines (Enter/Exit hooks) |
| `LEGO1/lego/legoomni/src/paths/legopathactor.cpp` | MODIFY | +1 line (ApplyTransform hook) |

## Verification

1. **Build**: `cmake --build build` — no compilation errors
2. **Tests**: `ctest --test-dir build` — no regressions
3. **Multiplayer remote players**: With multiplayer connected, verify remote player walk/idle animations still work after the AnimUtils refactor
4. **Third-person on foot**: Enable via INI config. Walk around the island — player character visible from behind, walk animation plays when moving, idle animation plays after ~2.5s standing
5. **Direction**: Character faces movement direction (not backward)
6. **Camera**: Follows from behind and above, tilt during turns works
7. **Vehicle enter/exit**: Enter a vehicle (e.g., bike), camera transitions. Exit vehicle, walk animation resumes
8. **Toggle**: Switch between first/third person at runtime — first-person restores original behavior (hidden model, reversed direction)
9. **Without network**: Verify camera works with multiplayer extension enabled but not connected to any room
