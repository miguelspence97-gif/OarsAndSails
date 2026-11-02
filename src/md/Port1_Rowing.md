# Port 1: rowing (v0.6.0)

First slice of the OAS port: the player rows the RowRim boat with the OAS mechanics and animation.

## Needs (for now)
- The Oars and Sails mod stays **enabled in MO2 with `OarsAndSails.dll` hidden** (right-click the file in MO2's Filetree → Hide). That keeps OAS's row clip (`RowboatRow_Loopv3.hkx`), its FNIS list and the Nemesis/Pandora output, and turns its code off. The Nemesis/Pandora output must have been built with OAS enabled.
- Without the clip the boat still rows. After 10 refusals the console says so, and the oars pull after 2.5 s anyway.
- Later: RowRim ships its own copy of the clip and list.

## Seats
- **Oars**: middle thwart, model `0 0 33` (feet; thwart top 64.1), facing aft. `rr "rowseat here|x y z"`.
- **Helm**: stern seat by the tiller, `0 -215 35` (top 66.3), facing the bow. Still the step 1 test motor. `rr "seat here|x y z"`.
- E (or `rr sit`) takes whichever seat is nearer along the boat. Seat heights are a first guess; tune them in game.
- Co-save HULL record v2 keeps both seats. A v1 save's seat (the old test default) is ignored.

## Row clip (OAS UpdateRowAnim / TryRowStart / StopRowAnim)
- W at the oars: `OarsRowStart` on the player, 0.3 s after sitting or a stop, retried every 0.4 s.
- W released: `IdleForceDefaultState` after 0.6 s (debounce), then the chair sit again on the next frame.
- No sit retries while the clip plays. The rower is held square to the boat (facing aft).
- Camera: ThirdPersonState::Update (slot 3) hook resets `animationRotation` while rowing and for 3 s after (OAS 28/56: the clip turns AnimObjectA and the orbit camera flipped with it).

## Speed (OAS SkimStep row profile, 56.49/56.50 tune)
- Stroke rate 0.3366 per second (the 3 s loop). Amplitude eases in and out at 4/s. Thrust = pi·max(0, sin phase)·min(1, 2·amp), pulling only on the drive.
- Top speed 300, stroke pull 0.6 × top, glide drag 0.07 (`rr "row speed N"`, `rr "row drag N"`).
- S brakes (120); once stopped it backs water (60 max, 20/s²).
- Turning: 0.5 → 0.45 rad/s, full bite at 120 u/s, turn floor 0.9 (it spins at rest), response 2/s. Hard turns at speed scrub speed (0.25).
- The oars wait for the clip (0.12 s lead-in) and keep going through the stop debounce.
- The boat only moves along its heading (keel, no sideways slide). Unattended, it brakes to a stop as before.

## Oars (OAS 56 baked oars + 56.18/56.21 hand lock)
- **Resting**: two ShipOar01 clones on the boat model in the rowing oarlocks (`93.3 -30 70.9`, after the v0.5.2 move), 30% inboard, blade 12° down. `rr "oar rest in dip roll"`, `rr "oar lock x y z"`.
- **Rowing** (clip live, bones posed over 50 from NPC Root, third person): clones hang on AnimObjectA/B (bone tune: blade out, roll 90°, `rr "oar roll N"`). Each is turned about its palm grip onto the hand→oarlock line and moved onto the hand (30 max). The oarlock side is picked from where the oar is. `rr "oar lock on|off"`.
- First person: the resting oars stay (the third person skeleton is not drawn).
- Not yet: the rest↔row blend (they swap), blade ripples, the dig sound, the squat.

## Log lines
`Row anim: ...`, `Row: speed ...` (every 2 s while moving), `Oars: ...`, `Oar model bounds: ...`.
