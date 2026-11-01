# v1.7.13: motion log (boat + third person camera)

Console: `oas motionlog` (30 s), `oas "motionlog 60"`, `oas "motionlog off"`. Writes `RowRim_Motion.log` next to RowRim.log (Documents\My Games\Skyrim Special Edition\SKSE), one line a frame, a summary every 120 frames. Nothing changes in game.

Sampled at three points each frame:
- A (our player update): frame time, the sim step, speed, stroke phase, thrust, where the glue put the player.
- B0/B1 (around the game's ThirdPersonState::Update): the orbit (freeRotation.x) before and after the game's update, the rower's 3D.
- B (after our orbit fix-up and the late boat placement, i.e. what is drawn): the drawn boat, the rower vs his seat on it, the camera (this frame's ThirdPersonState translation/rotation) vs the boat, heading/pitch, zoom, look input.

Flags per line: CAM (camera jumped >1 vs the boat without look input), TURN (heading jumped >0.3 deg without look), BOAT (drawn step off speed x dt by >1.5), ROWER (rower jumped >1 on the boat), ORBIT (the game swung the orbit in its update; our fix-up comes after the camera was computed, so that frame shows the swing), DT (frame time >1.6x average).

Suspects going in (the log decides):
1. drawLead 0.7 (v1.6.6): the boat is drawn 0.7 of a frame ahead of the rower and camera (who show last frame's glue). That gap is 0.7 x speed x dt, so uneven frame times or the stroke surge make the boat shift against the camera. Compare with `oas "lead 0"` (OAS: exactly one frame behind).
2. Orbit fix-up after the draw: CamTend undoes a game swing of freeRotation after ThirdPersonState::Update has already built this frame's camera, so the swing shows for one frame (the brief jerk). ORBIT flags will show it.
3. Engine moves of the rower's 3D between our glue and the camera (eng column).

Test: row in a straight line for ~20 s with `oas motionlog` (don't touch the mouse for a stretch), then once more after `oas "lead 0"`. Send RowRim_Motion.log (both runs).

## v1.7.14: seat guard (fix from the first motion log, 2026-11-01)
Log: 60 fps flat (16.7-17.0 ms), boat sim and drawn steps exact (BOAT never off by more than 0.06 outside hops). Two real problems:
1. Every ~100 frames (1.7 s) the engine moves the rower's 3D ~1 unit too far for one frame (eng 3.85 instead of 4.85), then back: the rower and the camera hop 1 unit against the boat. Not tied to the stroke; the period matches 60 Hz physics against a 59.5 fps frame. That is the small displacement stutter.
2. Every hull cell hop (MoveTo reloads the hull's collision) the camera is pulled 250 units toward the boat for a frame (the reloading collision in its line), then eases back over ~20 frames (0.35 s). That is the camera jerk. The rower also slips 5 that frame.

Fix (SeatGuard, third person camera hook, after the boat is drawn, before the game renders):
- The rower's root on the drawn boat may only move 0.25 a frame; slips are put back (bones and all) and the camera is moved with him. Jumps over 8 (new seat/pose) pass.
- A camera pull-in of more than 20 in one frame (no look input, no zoom) is held at the steady place until the game's camera is back within 1 (40 frames max, so a real obstacle still gets the camera).
- `oas "seatguard off"` to compare. The motion log shows fixR / fixC on lines where it acted. Log line "Seat guard: camera pulled ..." on each hold (first 20).

Test: `oas motionlog` while rowing across a few cells: the CAM/ROWER flags should be gone (fixR ~1 every ~100 frames, fixC on hop frames).

## v1.7.15 (second motion log)
- The seat guard did its job in the log: CAM/ROWER flags gone (one-frame rower slips put back, ~0.25 max; the 190-unit camera pull on each of 5 cell hops held). Miguel still saw the jerk, so the log now checks what is really drawn:
  - DRAWNCAM: at the next camera update the camera node is compared with where our hook left ThirdPersonState.translation; if the game does not draw from it, this shows the gap.
  - HULL3D: the hull's own (collision) model drawn while the carrier draws the boat.
- Likely visible cause: CellHop MoveTo reloads the hull's 3D (the vanilla ShipRowBoat01 model). It comes in during the frame, after our player update hid the old one, so it can be drawn for a frame at the ref's spot: a second, plain hull flashing next to ours = the hull jerk at each cell hop. Now also hidden in LatePlace, right before the draw (log line "Hull: its reloaded 3D was about to be drawn").

## v1.7.16
- Miguel: with `oas "go 300"` (no rowing clip, sit pose) the boat and camera are smooth; the jerk only comes with the row clip. Boat speed over a stroke is smooth (270-300, no steps), no row restarts in RowRim.log, so the clip itself is the suspect (its loop seam: the v20 export with the leg edits).
- Motion log now also has the rower's pelvis, head and Camera3rd bone on the drawn boat each frame, with k = the frame's kink (second difference). BONE-pel / BONE-head over 0.8 = the clip jumping. Regular kinks ~2.9 s apart (the stroke loop) would point at the loop seam.
- A/B for later: the pre-leg-edit clip is kept at blender\RowboatRow_Loopv3_before_legs.hkx.

## v1.7.17 (third motion log)
- BONE 0: the clip is smooth (pelvis/head/Camera3rd kinks never over 0.53). Not the loop seam.
- Found: now and then the engine leaves the rower's 3D a whole frame behind (eng 0.26 instead of ~4.8), then catches up. The camera follows his 3D, so the view (and the hull in it) shakes ~5 units. That is the rowing-only jerk (with oas go there is no row clip).
- Found: DRAWNCAM = fixR: our edits to ThirdPersonState.translation never reached the screen; the game takes the camera's place through the state's GetTranslation.
- Fix: GetTranslation hooked (ThirdPersonState vtable 5): against the drawn boat the camera may move 0.6 a frame at most; pulls over 20 held (cell hops). Look input / zoom pass straight on. The rower no longer moves the camera (SeatGuard only fixes his bones).
- Rower: a jump over 3 that is still there the next frame (row start / stop) is taken at once (v1.7.16 slid him 4 units over 15 frames at row start).
- Log: `out` = the camera as drawn (previous frame) against the boat, `dOut` its move; JERK if over 0.7 without look. Summary line: drawn camera JERK count and max move.

## v1.7.18 (fourth motion log)
- The GetTranslation hook was never called (out - on every line): the game (or a camera mod) does not take the camera's place through it. Its install is off.
- From the v1.7.16 log: the drawn camera followed the rower's lagging 3D, not ThirdPersonState.translation, and our rower fix came after the game had read him.
- Fix: SeatGuard (the rower's slew on the boat, 0.25 a frame, real jumps over 3 taken at once) now runs BEFORE the game's third person update, against the pose the boat is about to be drawn at (DrawPose, LatePlace's own rule). The camera is then built from the steadied rower.
- Log: `out` is now read from the camera node as drawn last frame (against the boat as drawn last frame); JERK = it moved over 0.7 in a frame without look input. NB the 06:05 log was seated but not rowing (the rowing run was overwritten by a second `oas motionlog`): log while rowing.
