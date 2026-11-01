# v1.2.3: stuck after standing up, rower shifting against the boat

## Stuck after unboarding
- Cause: StandUp forced the graph variable bAllowRotation to TRUE (added with the v1.2.0 seated camera). A walking player has it FALSE. This is the bug OAS already found and fixed: "the old exit forced bAllowRotation to TRUE ... stuck for good after the first ride" (OAS Plugin.cpp, savedAllowRot_).
- Fix:
  - StandUp now sets it FALSE.
  - For 2 s after standing up, and 3 s after every load, TendWalkFix puts back bAllowRotation off, foot IK on and collision on every 0.25 s (OAS PostExitStand). Saves broken by the old stand-up get fixed this way.
- Helm stand spot: the hull's collision floor climbs at the stern: 48 under the helm seat and 60 at its back edge, measured from the collision mesh in RowRim_Hull.nif. The feet at 41 were inside it. The player now stands 65 forward of the seat (floor 42), feet at 45.
- Rower stand spot unchanged: 36 aft of the thwart, feet at 41 over a floor of 33.

## Rower vs boat shift while rowing
The View log lines (v1.2.1) showed two things:
- The rower root was one frame of travel (about 5 at full speed) off the drawn boat on some frames. That is the forward/back shift.
- After every cell hop the rower sat about 12 high for a second or two. That is the up/down shift.

Fixes:
- Player collision is off while seated, as OAS ("tcl": no collision, no gravity), and back on at stand-up. The seated capsule no longer touches the hull body, so the rebuilt collision after a hop can't push him up.
- PinRower (draw mode 3, the new default): in both camera Update hooks, before the game places the camera, the player's 3D root is put exactly on the seat of this frame's boat pose. The boat is drawn at the same pose. Camera, rower and boat now share one pose.
- The oars read the hands in the frame that puts the rower's root on the seat, so they stay in his hands after the pin.
- `rr draw prev|now|player|pin` to compare. The View line now also logs how often and how far the pin moved the rower.
