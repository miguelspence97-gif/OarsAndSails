# v1.6.0: crew and passengers (ported from OAS), src\Crew.inl

## What came over from OAS
- `crew add / remove / clear / list / summon`, the crew list in the co-save.
- BoardPassengers: when the player sits down, followers and crew within 3000 board.
- UpdateLateBoarding: one late arrival at a time hops in. They must be within 400 of the boat for 1 s with the boat below 60 u/s; the hop takes 0.7 s with an arc of 40.
- The per-seat numpad tuning.

OAS's per-actor seat logic, rowing followers, peace/aggression handling, dog sit watcher and camera bits were left out.

## Seats
Model space, the same frame as the player's `seat_` / `rowSeat_`. z is the sitting feet height; the thwart tops are 64–66.

| slot | seat | where | facing |
|---|---|---|---|
| 0 | stern seat | 0, -215, 35 | bow; only free while the player rows |
| 1 / 2 | aft thwart left / right | ∓34, -84.7, 34 | bow |
| 3 / 4 | bow thwart left / right | ∓30, 113.7, 35 | stern |
| 5 | middle thwart | 0, 15, 33 | stern; only free while the player is at the stern |

- They're filled in the order 1, 2, 3, 4, then 0 / 5, so 5 people at most.
- Dogs go on the floor: bow 0, 165 facing the stern, and stern 0, -150, at deckZ + 2.
- The thwart centres were measured from RowRim_Hull.nif: the mid plank y 2.8–27.2, the bow plank 101.8–125.5, the aft plank -96.5 to -72.8, the stern seat -226.8 to -203.2.

## Riding
- When a rider boards: collision off, SetDontMove (through papyrus), swim stop, placed at the seat, then the chair sit events (`IdleChairEnterInstant` / `IdleChairSitting`). Dogs get `idleSitStart`.
- Every frame while the player is seated, from TestHull::GlueSeat with the same boat pose as the player: SetPosition at the seat, angle = boat facing + seat yaw + tune.
- The sit is sent again every 3 s (5 s for dogs), with a chair idle every 25–55 s.
- On StandUp (and before `seated_` is cleared): collision on, DontMove off, the chair exit events and graph sit flags cleared. Each rider is put on the floor beside their seat: the thwart sitters step 34 off the plank, the stern 65 forward, the middle 51 aft. Then they're free to follow you off.

## Numpad
- `rr num` turns tuning on and shows who is selected; `rr num` again or `rr "num off"` turns it off; `rr "num reset"` sets every tune back to zero.
- Num5 picks the next one aboard. Only "Seat tuning: <name> (<seat>)" is shown on the HUD; positions are never shown.
- Held keys move the selected seat at 15 units/s and turn it at 45°/s: 8/2 forward/back, 4/6 left/right, 9/3 up/down, 7/1 turn.
- Tunes are per seat slot. They're saved to `RowRim.ini [CrewSeats] Seat0..5 / Dog0..1` ("x y z yawdeg") when a key is let go.

## Co-save
- `CREW` holds the crew list.
- `RIDE` holds who was aboard at the save. After a load they're let go once their 3D is in (collision on, DontMove off, the sit cleared), so nobody stays frozen.
- On revert, Crew::Forget runs first, so TestHull's stand-up touches no passenger.

## v1.6.1: moved with the boat, not pinned

The seated draw mode default is now 0, as OAS did it: the player (and the crew, always) are glued by SetPosition each frame, the 3D is left to the engine, and the boat model is drawn one frame behind so it stays under them. v1.2.3's mode 3 (PinRower moving the rower's 3D root onto the seat before the camera) looked locked into the hull. `rr "draw pin"` brings mode 3 back; `rr "draw prev"`, or any other word, sets the default.

## v1.6.2: one dog, in the OAS dog seat

- Where: OAS bowSlot_ (0, 440, -73, facing the bow), moved into the model frame the same way the seats map (OAS rower slot (0, 259, -84) is RowRim's (0, 15, 33)), so the dog sits at model (0, 196, 44), facing the bow. Num5 tunes it as Dog0.
- Pose: it leans with the boat's pitch (people stay upright), and gets OAS's sit control every 0.25 s: if it isn't sitting, `idleSitStart`, or `IdleStop` first if it is lying down.
- Only one dog boards, and rr "crew add" refuses a second dog.

## v1.6.3: no flicker, feet on the floorboards

- Flicker and jumps: the v1.6.0 sit was re-sent blindly every 3 s, which restarted the sit on people already seated. Now it works like OAS (v1.0.11-1.0.13): an animation event sink on each person aboard tracks a seated flag from their own events. tailChairIdle / idleChairSitting set it; tailMTIdle / IdleStop / IdleChairExit* clear it. The sink only sets flags under a mutex; the main thread reads them. The sit is checked 4 times a second and only sent when they are not seated, and not within 4 s of a chair idle we sent. Chair idles go only to someone seated. The sink is removed when they are let go.
- Standing floor: kCollisionLift is now 10.5 (was 8). The 8 matched the old floor at 38, but the floorboards (top 40.3) are 2.3 higher, so feet stood inside them. kDeckZ follows (stand-up spots, the crew stand spots).
- Crew seats are 5 higher: thwarts 39, bow 40, stern 40, middle 38. The chair sit puts the feet at the root, and at 33-35 the feet were inside the boards. The player's own seats (seat_ 35, rowSeat_ 33) are unchanged: they are kept per boat in the co-save, and the row clip's legs were fitted to them.
