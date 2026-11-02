# RowRim step 1: keyframed hull test (v0.1.0)

RowRim is its own mod (own DLL, later its own ESL), in `E:\code\RowRim`. Like QuickLoadLast it builds against
`..\OarsAndSails\lib` (CommonLibSSE-NG + vcpkg), so keep both folders side by side. Nothing in OAS changed.
Plan and background: `E:\code\OarsAndSails\src\RowRim_Kickoff_Brief.md`.

## What it tests
Can a hull's collision move and tilt every frame with the player and a follower standing on it, without falling
through or jittering?

- `rr spawn` places a NEW ref of the vanilla rowboat static (ShipRowBoat01) 350 ahead (on the water if there is any,
  origin 32 under it). `rr bring` places it under you, deck at your feet. Vanilla refs are never touched.
- Once its 3D loads, its Havok body is made keyframed (`NiAVObject::SetMotionType(kKeyframed, force)`), and the log
  says on which node the body sits, the motion type before/after, the layer and the body offset.
- Every frame (player update hook) the plugin works out a target pose (rock + forward speed + turn) and:
  - **drive** (default): gives the body the linear + angular velocity that lands it on the target next frame (a hard
    keyframe). The character controller should see a moving floor and carry whoever stands on it.
  - **warp**: teleports the body to the target (no velocity). For comparison: expect sliding / jitter here.
  - **freeze**: holds the pose.
  The ref's position and its node follow the target too (`rr "node off"` leaves the node to the engine).

## Console
| command | does |
|---|---|
| `rr spawn` / `rr bring` / `rr delete` | place ahead / under you / remove |
| `rr "rock 6"` / `rr calm` / `rr "period 4"` | roll amplitude in degrees (pitch is a third, heave 6) / none / seconds per roll |
| `rr "go 150"` / `rr "turn 20"` / `rr stop` | forward speed u/s / turn deg/s / both 0 |
| `rr up` / `rr down` (`rr "up 30"`) | raise / lower the hull (default 10) |
| `rr drive` / `rr warp` / `rr freeze` | how the body is moved |
| `rr "layer anim"` / `rr "layer static"` | collision layer for the NEXT spawn (AnimStatic is what moving platforms use) |
| `rr "node off"` / `rr "node on"` | stop / resume writing the hull node ourselves |
| `rr flip` | swap the quaternion convention if the deck turns the wrong way (auto picked at spawn) |
| `rr watch` | click a follower in the console first: they get logged too |
| `rr info` | state + one log line in the console |

`rr` replaces an unused dev command (first free of TestLocalMap, TestSeenData, ShowRenderPasses, DumpNiUpdates;
the log says which). OAS keeps `oas` / TestCode.

## Test
1. On a lake: `rr spawn`, walk on (or `rr bring`), `rr "rock 6"`. Stand still, walk about, jump.
2. `rr "go 100"`, then `rr "turn 15"`: do you ride along, or does it slide away under you?
3. Follower: bring them aboard, click them, `rr watch`, repeat 1-2.
4. Same with `rr warp` to compare. Then `rr delete`, `rr "layer anim"`, `rr spawn` and repeat in drive.
5. `rr delete` before saving (the test ref is not tracked across a load).

## Pass / fail
- Pass: in drive mode the player and follower stand on the rocking, moving hull with no fall-through and no visible
  jitter. Then step 2 (boat-type block + shared rules) builds on keyframed collision.
- Fail: keep the OAS trick (level collision, tilt pivoted at the feet) and design around it.

## Log (`Documents\My Games\Skyrim Special Edition\SKSE\RowRim.log`)
- `RowRim: body on node '...', motion A -> B ...`: B should be 4 (keyframed).
- Every 0.5 s: `t roll pitch heave err | body z node z | player spread S | Name local x y z (deck 31) state N on hull 0/1`.
  - `err`: how far the body was from its target (drive). Should stay small (a few units).
  - `player spread`: how much the player's height over the deck moved in that half second. Under ~2 = smooth.
  - `state`: 0 on ground, 2 in air. `on hull 1` = the character controller's support body is our hull.
  - `body z` vs `node z` apart (beyond the body offset) = the drawn hull and the collision disagree.

Paste the log after the test.

## v0.2.0: seat + WASD
- **E** while standing on the hull sits you on the seat (or `rr sit`). **W/S** thrust forward/back, **A/D** turn
  (left stick on a controller). **E** again stands you up; the boat glides to a stop.
- Seated, the game's walking, jumping, sneaking, fighting and activation are off; you are held at the seat every frame
  and turn with the boat (looking around stays free). Sit animation: IdleChairEnterInstant (as OAS), retried 3 times.
- Handling: up to 220 u/s forward, 80 back, accel 90, brake 160, drag 0.6/s; turn up to 35 deg/s (35% at rest, full from 120 u/s).
- Seat default local 0 -170 31. `rr "seat here"` sets it to where you stand; `rr "seat x y z"` types it.
- `rr bow` flips which end of the model is the bow (W goes that way, you face it).
- `rr go` / `rr turn` / `rr stop` take back control from the helm.
- Fix: `err` no longer sticks at its last drive value in warp mode.

## v0.2.1 fixes (from the v0.2.0 test)
- Hull went invisible after rowing out of its cell: it stayed in the cell it was placed in. Now, every 0.5 s, if the
  player's cell differs (and the boat is within 3000), the ref moves into it (OAS CellHop: MoveTo player + SetPosition).
  The 3D can reload: a new root is detected and the body set up again; the seated player stays held meanwhile.
  Log: `Hull: cell hop A -> B (ok)`, `Hull: 3D reloaded`.
- Stuck half in the water after standing: the stand-up now does the full OAS release (IdleChairExitStart,
  IdleFurnitureExit, sneakStop, IdleForceDefaultState; sit graph vars off; sitSleepState normal; controls back at once).
- Left behind when standing at speed: feet go onto the deck (seat +2, was +6), the player gets the boat's velocity, and
  with nobody at the helm the boat now stops in about a second (brake 200 u/s^2) instead of gliding on.

## v0.2.2 (the v0.2.1 hop faded)
- v0.2.1 log: every hop re-set the body up from scratch (motion 5 -> 4): `MoveTo` reloads the model = the fade.
- The hop now tries the engine's own `SetPosition` alone first (refiled by position, the way actors are handled?);
  `MoveTo` only if that leaves the ref in its old cell. Log: `Hull: cell hop A -> B via SetPosition|MoveTo: ok, 3D kept|replaced`.
- Every BSFadeNode in the hull held at fade 1 every frame (OAS 56.28), so a reload no longer fades in from nothing.
- Same 3D but the body put back to fixed: detected (`Hull: body motion reset`) and keyframed again.

## v0.2.3 (v0.2.2: SetPosition never refiles the ref; MoveTo always unloads and reloads the model = the blink)
- Test of the follower route: the hull is now placed **persistent** (`PlaceObjectAtMe(base, true)`), and the cell hop is
  **off** by default. Question: does the engine keep a persistent hull drawn across cells by itself, like a follower?
- Every 0.5 s the log adds `hull cell X (detached) player cell Y 3D yes/NO culled 0/1`.
- `rr "persist off"` (next spawn temporary) and `rr hop` / `rr "hop off"` switch back to the old behaviour for comparison.
- A persistent test hull is saved with the game: `rr delete` before saving.

## v0.2.4 (v0.2.3: persistent stayed filed in its first cell; it kept its model and body after that cell detached, but vanished)
- v0.2.3 log: hull cell never changed (00008E79), `3D yes`, body still tracking (err ~3), then the cell detached and the
  hull went invisible: its scene node still hung under the old cell's node.
- Now every 0.5 s only the hull's scene node is moved under the player's cell node (same group slot as before).
  No MoveTo, no reload, no fade; the ref itself stays filed where it was. Log: `Hull: node moved under the player's cell node`.
- `rr "graft off"` turns it off for comparison.

## v0.2.5 (v0.2.4: no vanishing over ~90 s and 7 node moves, then a crash)
- Crash (18:31:40): access violation in the renderer while drawing the hull's `L1_BoatDetails:5` shape (execution jumped
  into freed memory), ~8 s after the last node move, with the hull still filed in its first cell, long detached (most
  likely now fully unloaded, taking something the hull's drawing still pointed at with it). Solitude lighthouse fire
  meshes were on the stack.
- Fix attempt, the follower way: at spawn the hull is moved (one MoveTo, before its model loads) into the worldspace's
  persistent cell and added to the worldspace's mobile persistent refs list, where the engine keeps moving actors.
  Log: `RowRim: hull moved to the persistent cell X (ok), mobile list added`. Removed from the list on `rr delete`.
- Node regraft stays on. Test on a throwaway save.

## v0.2.6 (v0.2.5: same crash, same shape, ~70 s after the hull's cell detached)
- v0.2.5 log: the MoveTo into the persistent cell did not take (the engine filed it in exterior 00008E5A again), so the
  mobile list did nothing (removed; it is the engine's actor list). Same crash drawing `L1_BoatDetails:5`.
- Conclusion: the hull must never stay filed in a detached cell. The cell hop is back on (MoveTo into the player's cell
  at each crossing), and the reload's blink is hidden: the old model is kept, moved under the player's cell node and
  moved with the boat until the reloaded model is in the scene (or 3 s), then dropped. The new model is held at fade 1.
  Log: `Hull: cell hop ... via MoveTo`, `Hull: old model dropped after X s (new model in)`.
- Node regraft only runs with the hop off.

## v0.3.0: the boat rides on a hidden actor, like a horse
- Why: actors are refiled by the engine into whatever cell they are in, keeping their model (no fade, no reload, never left
  in an unloaded cell). A static is never refiled except by MoveTo (reload). So the boat's model now hangs on an actor.
- Carrier: the first non-unique NPC of `HorseRace`, placed persistent, collision off, 2000 under the boat (no capsule in the
  boat, nothing to pick or ride), moved with the boat every frame. Its own parts are hidden (re-hidden twice a second);
  no horse animation shows. AI on by default (`rr "carrier ai off"` to freeze it).
- Boat model: a clone of ShipRowBoat01's nif (physics stripped) attached to the carrier's 3D and placed at the boat pose
  every frame, fade held at 1. Re-attached if the carrier's 3D reloads.
- Collision: still the keyframed hull ref (step 1's proven moving deck), now hidden once the model is on the carrier, and
  cell-hopped with MoveTo (its reload can't be seen; the collision is gone for a frame or two at each crossing).
- Log every 0.5 s adds `carrier cell X 3D yes boat model drawn`. `rr delete` removes both. `rr "carrier off"`: next
  spawn draws itself (v0.2.6 behaviour).

## v0.3.1 (v0.3.0 test: AI on = the hidden horse fled/attacked enemies, dragging the boat; AI off = the boat disappeared)
- v0.3.0 log: the carrier was refiled into the player's cell every crossing (`carrier cell` = `player cell` throughout) and
  the boat model stayed attached: the actor route works for cells.
- Disappearing with AI off: the horse sat 2000 under the boat (under the lake bed); a frozen actor's bound is not rebuilt,
  so the horse and the boat model hanging on it were culled. Now: horse 220 under the boat (just under the keel, behind the
  hull's collision from the deck), and its bound rebuilt every frame from the boat model up.
- Carrier AI off by default. Log adds `carrier culled X bound r R | boat model attached culled X`.

## v0.3.2 (v0.3.1: still disappeared later on)
- v0.3.1 log: `carrier bound r` grew from 352 to ~14000 with the distance rowed: with AI off the horse's 3D never moved
  (SetPosition moved the actor and its cell, not its model), so the boat model hung further and further from it and the
  game faded the actor out by the distance of its own (spawn) spot.
- Now `Update3DPosition(true)` on the carrier every frame (its model follows it) and the carrier root's fade held at 1.
  Expect `bound r` to stay around 350-450.

## v0.3.3 (v0.3.2 test: CLEAN across cells, seated rowing fine; parked boat model has high-frequency jitter)
- v0.3.2 log (parked, rock 2): physics fine (on hull, spread 0.3-2.7, err ~1). The jitter is the drawn model: it was put at
  the target pose, while the deck is one physics step behind; rocking flips that gap back and forth every frame.
- Now the model is also placed late (camera update hook, after all actors, right before drawing) at the collision body's
  actual pose. `rr "late off"` goes back to the v0.3.2 placement for comparison.

## v0.3.4 (v0.3.3 test: model still buzzes when parked; a loaded save leaves the old boat unusable)
- v0.3.3 log: parked and calm, the body stayed 1-5 units off its target, never settling. Cause: velocity = gap / frame
  time; Havok steps on its own fixed clock, so with a step longer than the frame it overshoots and swings back (buzz).
  Now: velocity = the target's own velocity (feed-forward) + gap x 12/s (angular the same). Expect err near 0 when parked.
- Co-save (`RRIM`, record `HULL` v1): hull ref, carrier, boat pose, seat, bow flip, carrier on. On load both refs are
  re-resolved and taken over again (body and boat model set up anew; carrier AI/collision re-applied). Log: `Co-save: ...`.
  `rr delete` before saving is no longer needed.

## v0.3.5 (v0.3.4 test: drive jitter and load fixed; after standing up the boat model buzzes until the player moves)
- v0.3.4 log after stand-up: err 0.0, player on the hull, height steady: the physics is still, yet the model buzzed until
  any movement. Miguel's idea: move the character a tad after standing. Now 2 frames after the stand-up the character
  controller gets a 10 u/s forward drift for 0.1 s (about 1 unit), as a key tap would. Log: `Seat: nudge done`.
  `rr "nudge off"` to compare.

## v0.4.0: water blocker test (before the port)
- Goal: no lake surface drawn inside the hull, up to about seat height, while the real hull bottom still shows.
- New mesh `Data\meshes\RowRim\RowRim_Hull.nif` (build.bat now copies Data\meshes to the MO2 test mod): the OAS bilge
  floor hull (vanilla ShipRowBoat01 + BilgeFloor) plus flat planes shaped to the hull's inside at model z 46 (parked water
  is at 32; heave/roll take it to ~26-46). Outline from the hull shell's cross-section, edges tucked 2.5 into the planks,
  y -235..233, 314 verts. Built by `scripts\water_blocker\build_blocker.py <src nif> <out> 46`.
- Variants (one shown at a time), each meant to write depth and no colour, so water drawn after it fails the depth test:
  - `WB_A` alpha blend (src alpha / inv src alpha), material + vertex alpha 0, z write on (default)
  - `WB_B` as A + the alpha property's no-sorter bit (may draw in another pass)
  - `WB_C` blend ZERO / ONE (colour untouched) + alpha test ALWAYS, alpha 1
  - `WB_Debug` the plane with the hull texture, to see its outline and height
- Which one works depends on whether the engine draws it before the water. If none does, the next idea is a refraction /
  effect shader variant, or the floor.
- Console: `rr "wb a|b|c|debug|off"`, `rr "wb z N"` (N units from 46, -30..30), `rr "wb floor"` / `rr "wb floor off"`
  (OAS floor, off by default). The boat model reloads the shapes on spawn/load.

## v0.4.1 (v0.4.0 test, with screenshots: the plane at 46 is right from above, but a camera low in the boat is under it)
- Screenshots: camera above the boat, debug sheet in place and no water inside. Camera down in the hull (sitting /
  zoomed in): it is below the z 46 plane and sees the water sheet over the floor boards from underneath, so no plane
  above the camera can hide it.
- Now planes at model z 30..48 every 2 (`WB_A_<z>` alpha-0 blocker, `WB_D_<z>` debug), each cut to the hull's inside at its
  own height; the material shaders are shared. Each frame the plane just above the water inside the hull is shown:
  water height in the boat frame (lake height at the boat - boat z - heave) + roll x 60 + pitch x 150 + 1.5, rounded up
  to the next plane (30..48). Built by `scripts\water_blocker\build_blocker.py <src nif> <out>`; nif 601 KB.
- Console: `rr "wb a"` (default) / `rr "wb debug"` (same automatic height, drawn with the hull texture) / `rr "wb off"`,
  `rr "wb z N"` extra margin (-10..16), `rr "wb floor"` OAS floor on/off. B and C variants dropped.
- Log: `Water block: plane a at model z 34 (water needs 33.2, water -14000)` each time it changes plane.

## v0.5.0: own hull mesh (Miguel's plan: edit the vanilla boat instead of adding shapes)
- v0.4.x outcome: the invisible blocker sat right (just above the water) but the water still drew over it: with Community
  Shaders the water is drawn before any see-through surface, so a depth-only blocker cannot work from a mesh. Dropped.
- `Data\meshes\RowRim\RowRim_Hull.nif` (181 KB) is now the vanilla ShipRowBoat01 meshes edited in place, no added shapes
  (`scripts\hull\build_hull.py <src> <out> 34 -10`, source = the OAS bilge hull with its BilgeFloor dropped):
  - floor: the hull's own inner skin, its 66 vertices below model z 34 lifted onto 34 (normals up), so the boards are 2
    above the lake surface at rest (32); the ribs still stand above it
  - the three thwarts (seat planks) raised flush with the gunwale top (+15.9 at the middle one, +16.5 fore and aft) and
    widened into the gunwale (x1.03-1.05)
  - all six oarlock parts moved 10 toward the stern (-Y, the tall curled post; 2 clicks of 5), following the gunwale's
    curve (dx up to 1.4) and sheer (dz up to 0.4)
- Code: the hidden collision (vanilla) is lifted 4 (`kCollisionLift`) so you stand on the raised floor; default seat and
  spawn heights follow (deck 35). Water blocker off by default.
- A seat saved in an older co-save keeps its old height (4 low now): `rr "seat here"` or `rr "seat 0 -170 35"`.

## v0.5.1 (Miguel: floor higher, ribs to follow, oarlocks 3x further aft, seat planks without the cut corners)
- `build_hull.py <src> <out> 38 -30`: floor at model z 38 (6 above the resting water, 84 inner-skin vertices); the five
  ribs' parts under it laid onto it at 38.6, so they run on across the floor; oarlocks 30 toward the stern (6 clicks of
  5, dx up to 4.2, dz up to 1.1); the three seat planks squared off (every vertex in a plank's end region on the full
  end line, the worn / cut corners filled).
- `kCollisionLift` 8 (was 4), deck 39. A saved seat keeps its old height: `rr "seat here"` or `rr "seat 0 -170 39"`.

## v0.5.2 (Miguel: more polygons, less 2011)
- The hull skins (inner and outer, `ShipRowBoat01:3`) get one level of curved subdivision before the floor is lifted:
  856 -> 3424 triangles, 530 -> 1910 vertices (`scripts\hull\subdiv.py`). Original points stay put (no cracks with the
  rim, keel or ribs); each edge's midpoint is pushed out along the vertex normals (averaged per position, Phong
  tessellation, 0.75); open edges (gunwale line, ends) stay straight; UVs, normals, tangents, colours interpolated.
- The floor now lifts 310 inner-skin vertices. Mesh 241 KB. Rim, posts, ribs and planks are unchanged (their hard plank
  edges would get rounded by the same treatment).

## v0.5.3: smooth hull
- The sharp edges came from the vanilla hull skins having split normals at every plank seam (hard shading creases).
- `build_hull.py` now welds the skins and gives every vertex the area-weighted average normal (`subdiv.smooth_normals`), before and after the curved subdivision. Inner and outer skins are kept apart. Tangents are rebuilt to match.
- Still one subdivision level: two levels doubles the nif (472 KB vs 241 KB), so not without asking. Run `build_hull.py bilge.nif RowRim_Hull.nif 38 -30 2` for that.
- Rim, posts, ribs and seat planks are unchanged (they keep crisp plank edges).

## v0.5.4: stern seat
- A fourth seat plank: a copy of the aft thwart moved to y -165 (past the stern oarlocks at -127.5), top flush with the gunwale there (71.3).
- Its ends follow the hull inner face as it narrows (+-77 forward edge, +-69 aft edge), so no gaps.
- `build_hull.py` default subdivision level is now 1 (as built).

## v0.5.5: stern seat by the tiller
- Stern seat moved aft to y -215 (just behind the y -202 frame) and lowered to top z 66.3 (5 below where it sat).
- Past the gunwale table the width comes from slicing the inner hull skin at z 62, so the ends still meet the hull (+-59 forward edge, +-50 aft edge).

## v0.5.6: thwarts lowered
- The three thwarts sit 2.5 below the gunwale top (`THWART_DROP`), half the stern seat's drop. Mesh step done; next is the OAS port.
