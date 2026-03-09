# ROI Direction Conventions & Third-Person Camera Corrections

## Background: The Two Z-Axis Conventions

The game engine represents an actor's facing direction via the z-axis of its ROI
(Real-time Object Instance) local-to-world transform. Two opposite conventions
exist throughout the codebase:

| Convention   | ROI z-axis points...         | Used by                                              |
|--------------|------------------------------|------------------------------------------------------|
| **forward-z**  | Toward visual forward        | `PlaceActor` (when `m_cameraFlag=TRUE`), cam anim end |
| **backward-z** | Away from visual forward     | After `Enter()`'s `TurnAround()`, vehicle ROIs       |

Toggling between conventions is a single operation — `FlipROIDirection` (or
`IslePathActor::TurnAround`): negate the z-axis and recompute the right vector.

The third-person orbit camera (`ComputeOrbitVectors`) uses **backward-z**. It
treats local Z+ as "behind the character" and places the camera there, looking
toward −Z (the character's face). The movement inversion (`ShouldInvertMovement`)
also depends on this convention.

## Why the Complexity Exists

The engine's actor lifecycle does not consistently produce one convention:

```
Enter()
  → ResetWorldTransform(TRUE)   sets m_cameraFlag = TRUE
  → TurnAround()                flips to backward-z  ← what we want
  → TransformPointOfView()      sets 1st-person camera

PlaceActor()                     resets to forward-z  ← overwrites Enter
```

`Enter()` and `PlaceActor()` are called in sequence during `SpawnPlayer()`, and
`PlaceActor` always wins. Similarly, cam anim end handlers call `PlaceActor`,
resetting to forward-z. Every code path that transitions the actor must therefore
correct the ROI direction back to backward-z for the orbit camera.

## Code Paths & Their Corrections

### 1. Normal walking entry

**Path:** `OnActorEnter` (non-vehicle, non-world-transition)

`Enter()` called `TurnAround()` → ROI is backward-z. We call `SetupCamera()`
directly. No correction needed.

### 2. Small vehicle entry

**Path:** `OnActorEnter` (small vehicle)

`Enter()` called `TurnAround()`, but vehicles natively use backward-z (mesh
faces −z). Enter's TurnAround flipped to forward-z, breaking the convention.
Fix: call `p_actor->TurnAround()` to undo it, then `SetupCamera()`.

### 3. Vehicle exit

**Path:** `OnActorExit` → `ReinitForCharacter`

`Exit()` places the walking character ROI via `SetLocation` using the vehicle's
direction. The ROI is already in backward-z. `m_roiUnflipped` is false, so
`ReinitForCharacter` does not flip.

### 4. Disable → Enable cycle

**Path:** `Disable()` → later `Enable()` → `ReinitForCharacter`

`Disable()` flips the ROI to forward-z (for the vanilla 1st-person camera) and
sets `m_roiUnflipped = true`. When `Enable()` → `ReinitForCharacter` runs, it
sees `m_roiUnflipped && !m_needsDirectionFlip`, flips back to backward-z, and
clears the flag.

### 5. World transition (camera enabled)

**Path:** `OnWorldEnabled` → `ReinitForCharacter` → `OnActorEnter` → `PlaceActor` → `Tick`

This is the most complex path:

1. **`OnWorldEnabled`** fires from `LegoWorld::Enable()`, BEFORE `SpawnPlayer`.
   Sets `m_roiUnflipped = true` and `m_needsDirectionFlip = true`. Calls
   `ReinitForCharacter`, which sets up the display ROI and `m_active = true`,
   but skips the flip and `SetupCamera` (PlaceActor hasn't run yet and would
   overwrite them).

2. **`OnActorEnter`** fires from `Enter()` inside `SpawnPlayer`. Detects
   `m_needsDirectionFlip && m_active` → returns immediately. We must NOT call
   `SetupCamera` here because the ROI is at a stale position (from the previous
   world session). The stale orbit camera view would freeze on screen during the
   ~500ms world load, appearing as a wrong-direction flash.

3. **`PlaceActor`** runs inside `SpawnPlayer`, setting the ROI to the correct
   spawn position in forward-z.

4. **First `Tick`** after PlaceActor: detects `m_needsDirectionFlip`, flips the
   ROI to backward-z, calls `SetupCamera` at the correct position. Clears both
   flags.

### 6. World transition (camera disabled, enabled later)

**Path:** `OnWorldEnabled` (early return) → later `Enable()` → `ReinitForCharacter`

`OnWorldEnabled` sets `m_roiUnflipped = true` before the `m_enabled` check, so
the flag is set even when the camera is disabled. When the user later enables the
camera, `ReinitForCharacter` sees `m_roiUnflipped && !m_needsDirectionFlip`
(PlaceActor already ran), flips to backward-z, and clears the flag.

### 7. Cam anim end

**Path:** `OnCamAnimEnd`

The cam anim end handler (`FUN_1004b6d0`) calls `PlaceActor`, resetting the ROI
to forward-z. `OnCamAnimEnd` flips to backward-z and calls `SetupCamera`. It
also clears `m_needsDirectionFlip` and `m_roiUnflipped` to cancel any pending
Tick correction.

Note: `OnCamAnimEnd` only fires for cam anims with `m_unk0x29 = true`.

### 8. Cam anim running (Tick guard)

**Path:** `Tick` (every frame)

While `AnimationManager::m_animRunning` is true, `Tick` skips
`ApplyOrbitCamera()`. The cam anim controls the camera via
`LegoAnimPresenter::TransformPointOfView`. If we called `ApplyOrbitCamera`, it
would fight the cam anim each frame. Critically, if the user interrupts the cam
anim (space bar), the end handler reads the ViewROI position to place the actor.
Our orbit camera position (elevated, behind the player) would cause the actor to
be placed in the air.

## Flags

| Flag                  | Set by                        | Cleared by                              | Meaning                                                  |
|-----------------------|-------------------------------|-----------------------------------------|----------------------------------------------------------|
| `m_roiUnflipped`      | `Disable()`, `OnWorldEnabled` | `ReinitForCharacter`, `Tick`, `OnCamAnimEnd` | ROI is in forward-z and needs flipping to backward-z |
| `m_needsDirectionFlip`| `OnWorldEnabled`              | `Tick`, `OnCamAnimEnd`                  | PlaceActor hasn't run yet; defer flip and camera setup    |
