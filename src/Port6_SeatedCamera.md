# v1.2.0: the OAS seated camera on the RowRim boat

Ported from OAS v1.0.86. These are the parts that run by default; camfix and ptilt were off in OAS, and its PlayerCamera slot-2 hook (FilterCameraZ / FixCameraUpright) never ran.

- **Face the rower (56.45 / 56.47):** sitting at the oars in third person, or switching first → third person at the oars, turns the orbit once to face the rower (`freeRotationEnabled`, `freeRotation.x = pi`). W rows away from the camera.
- **Orbit held (56.57 HoldOrbitAboard):** seated in third person (either seat), the orbit only moves on frames with real look input (`PlayerControls::lookInputVec`). Any other change the game makes (its swing behind the seated actor, the spin) is undone, and an orbit the game switches off is put back.
  - Log every ~2 s: `Camera: orbit aboard X, game swing undone Y rad, look input on N of M frames`.
- **Body (ApplyPlayerBody, ptilt off):**
  - Speed, SpeedSampled, SpeedDesired, Direction and TurnDelta are held at 0 (no walking graph while W is held).
  - Third person: the heading is locked to the seat's facing every frame and `bAllowRotation` is off.
  - First person: `bAllowRotation` is on and the look is free. Entering first person looks the seat's way.
- **First person free look (56.46):** the sit state is cleared while in first person (the game limits the look while seated) and put back when leaving (with `bIsSitting` true). No sit retries over a masked sit.
- **animationRotation reset (28 / 56):** before and after ThirdPersonState::Update (OAS CamAfterStateUpdate).
- **Standing up:** the mask is restored, `bAllowRotation` goes back on, and the orbit is let go.
