# v1.4.0: OAS soft physics, seats, weight, lake rower sit

## 1. Lake rowers stand up between rows
- The row stop (IdleForceDefaultState) stood them up until the next sit 0.3 s later. The sit is now sent in the same frame, and once more the next frame.
- Short pauses (giving way, a sharp turn, yielding to another boat) no longer stop the clip: the stop waits until the boat has not wanted a stroke for 1 s.

## 2. Standing weight
- Pitch (standing toward the bow or stern): 0.015 rad at the tips (was 0.045), a third.
- Roll (standing toward a side): 0.10 rad at the gunwale (was 0.13).
- The sink (1.5) is unchanged.

## 4. OAS soft physics (Parts 38-48, ground v2 41A, ice 41E) in WorldStep
Never a real collision: the hull is lifted, tilted, slowed and pushed.
- **Ground:** 7 samples (centre, tip, stern, both sides, two bow quarters, hull 300 x 85).
  - Dry samples (land 12 over the water) give a least squares plane: tilt soft-clamped to 25 deg pitch and 10 deg roll.
  - A smooth lift clears every sample: up to 30 afloat, 60 with the bow dry, 95 on ice.
  - Lift, pitch and roll are critically damped springs (no overshoot). Basin guard; 0.2 s dry/touch hysteresis.
- **Banks:** land 40 over the water (hysteretic to 32), a bow slope over 25 deg, a bank within 60 of the tip, or a rock face ahead engage the governor. The boat eases to a stop (0.3 s blend, 8 short of a bank).
- **Beach glide:** the tip runs up the sand at land crawl (120, decel 300/s). Heavy drag from 165 of slide, stop at 330 past where it touched. Backing off is free.
- **Ice:** refs whose model or node name says floe, iceberg, ice sheet or frozen are ridden on their tops (water-like) and are never obstacles.
- **Feelers (12):**
  - Bow (3, every frame): above 150 closing, STOP eased over 0.2 s with the pitch knock; slower, a scrape. One bow quarter alone turns the bow away (12 deg/s kick, gone in 0.4 s; not while you steer).
  - Sides (4) and stern corners (2): a push away (40 u/s, max 60, fading 6/s, never onto land) and 4% speed (halved when both sides touch).
  - Keel (3): a dip (spring 90), a 1.5 deg jolt and 5% speed; RowRim: not when slower than 25.
  - Contact needs at least 25 speed and 30 closing (except the bow stop).
- **Rocks:** a tall steep terrain face at the waterline (land 50 over the water, 40 more within 40 beyond) is a rock: bow stop and no lift up it. Chatter guard: 6 in 2 s turns it off for 3 s.
- Replaces the v1.0.0 three rock rays and the shore crawl. Our refs (FF), actors and ice are never obstacles.
- In BoatMotion the ground's pitch, roll and lift and the keel dip go on top. The idle bob fades out as the ground lifts the hull.

## 5. Oar seat where the clip sits
- rowSeat_ (0, 15, 33), rowing shift 0. You now rest where you row.
- A save with the old default (0, 0, 33) moves to (0, 15, 33) on load.
- Standing up from it still puts you 36 aft of the thwart's middle.
- Lake rowers likewise.

## 6. The stern seat does not row
- The step 1 test motor is gone. At the stern seat alone, W/S/A/D do nothing but show "No rower. Sit on the middle seat to row (E there)." (at most every 6 s).
- A boat already moving glides on and slows.
