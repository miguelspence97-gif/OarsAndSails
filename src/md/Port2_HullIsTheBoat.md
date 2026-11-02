# v0.8.0: the RowRim hull is the boat

## Why
From the v0.7.1 log, OAS's "adopt a nearby rowboat after a load" picked up the RowRim hull (the `rrt` keyframed hull, a ShipRowBoat01 ref). It then hung its props on it: benches, chest, stowed oars and lamps, all following the hidden collision ref. That gave two oar sets, the chest, and props lagging behind the moving boat.

## What changed
- **BoatBow leaves the RowRim hull alone.** `RowRimIsTestHull(id)` is checked in AdoptNearby, AdoptRef, CleanupOldBoats and StrayTick, so it is never adopted, cleaned up or swept as an OAS boat. No OAS chest, benches or lamps go on it. OAS's own `rr boat` boat still works separately.
- **One oar set, on the boat model:**
  - **Stowed** (OAS 50 / 56.35 pose, Miguel's tune): model x ±18, y 59, turn ±7.9°, size 0.8. Raised to z 67.5 so they lie on the raised thwarts.
  - **Rowing:** once the clip poses AnimObjectA/B (third person), each oar blends from stowed into the clip's pose over 0.8 s. The clip pose is locked to the hands and oarlocks (OAS 56.13 / 56.18), paired by boat side.
  - **After the clip** it blends back to stowed from the last clip pose, held in the boat frame.
  - The v0.6.0 side resting oars are gone.
  - Tune: `rrt "oar stow x y z turn"`.
- **Boat motion (OAS BoatBow Update)**, on the real collision:
  - Idle rock: roll ~1.1°, pitch ~0.6°, heave ~2.4. It fades to 65% at full row speed and is off on land. `rrt "bob N"`, 0 = off.
  - Stroke squat: about 0.7° of bow lift per dig (spring 45/7).
  - Turn lean: into the turn, up to 0.25 rad at full speed (OAS 56.41).
- **Parked weight (OAS 56.64 / 56.68):** while the player stands aboard (not seated, not swimming, inside 230 × 85 about the hull origin):
  - It lists toward their side (0.13 rad at the gunwale), dips their end (0.045 rad at the tips) and sinks 1.5.
  - It eases in at 3/s and back out at 1.8/s. On land: a third as much, and no sink.
  - In OAS this only tilted the drawing and pivoted at the feet. Here it tilts the actual deck collision.
- `rrt "rock N"` (the step 1 test rock) still adds on top.
- Bow-flipped boats (`rrt bow`) flip the squat and lean with the bow.

## Not yet
- The OAS movement extras on this hull: beaching and ground tilt, currents and falls, the waterfall code.
- Ripples, the dig sound, followers and crew seats, lamps.
- First person keeps the oars stowed while rowing (the third person skeleton is not drawn there).
