# Port 7 (v1.3.0): OAS lake boats on the RowRim boat, oar ripples and the dig sound

New file: src\LakeBoats.inl. Plugin.cpp includes it inside its anonymous namespace, so it is one translation unit and CMakeLists is unchanged.

## Lake boats (OAS Part 51-56, LakeBoats.cpp: same code and behaviour)
- Lakes are found by sampling the water as you explore; you can also save one with `rr "lakehere name"` or `rr "lakepoint name"`, and remove one with `rr "lakedel name"`.
- Townsfolk crews of every race, most in a fishing outfit. Row 2500..9000, rest and fish, give way to you, steer round each other, flee when attacked. A lantern at night.
- Population: one boat per 50 route points, up to `rr "lakeboats N"` (default 6). Up to 5 drawn at once (`rr "lakeboats view N"`). The sea gets 3-4 near Solitude, Dawnstar and Windhelm, 0-1 elsewhere.
- Kept in the co-save (record LAKE). Their refs are listed (LKSP) and removed after the next load, as OAS did.
- Settings: Data\SKSE\Plugins\RowRim.ini [LakeBoats] On/Count/View. Saved lakes: Data\SKSE\Plugins\RowRim_Lakes.txt.
- `rr lakes` lists them.

## What is RowRim in them
- **Boat:** a ShipRowBoat01 hull ref (collision off) carrying a clone of RowRim_Hull.nif (physics stripped). The ref's own shapes are hidden. Moved each frame like OAS moved its hull node. No OAS bench props: our model has its thwarts.
- **Frame:** the player boat's. Origin mid-boat, 32 under the water, the idle rock.
- **Paint:** random per boat from vanilla, beige, blue, black, red, white and bluedesign. Kept in the co-save.
- **Seats:** the rower on the oar thwart (0, 0, 33) facing the stern, shifted 15 to the bow while rowing (as the player). The second crewman on the stern plank (0, -215, 35) facing the bow.
- **Oars:** two ShipOar01 clones (kwila) on the boat. They rest in the middle oarlocks (93.3, 6.7, 76.5). While the rower's clip poses AnimObjectA/B, they follow the bones through the oarlock with the grip on the hand (the player's hand lock). Blended over 0.8 s.

## Oar ripples and the dig sound (OAS BoatBow 56.39 / 56.67)
- Each oar's blade tip against the water: a ripple (scale 0.05) where it goes in, with the feet-in-water footstep sound, and where it comes out. While it moves in the water, a smaller ripple every 12 units, and every 0.2 s while rowing.
- **Player's boat:** when the water under it is known and the oars are not stowed.
- **Lake boats:** within 4000 of you.
- `rr ripples` toggles. RowRim.ini [Sound] OarDigSound=<hex form> picks another sound (0 off); OarDigVolume (default 0.8).

## v1.3.1: smooth lake boats
- The idle rock's phase came from the boat's position, so it jumped a few units every couple of units the boat moved (heave and roll snapping while rowing). Each boat now has its own fixed phase.
- Turns ease in and out (the player's 2/s turn response) instead of a hard clamp, and the boat leans into them like the player's (0.35 x turn rate x speed share, eased 6/s).
- The hull, lantern and light refs no longer get SetPosition twice a second (that put their 3D back to an unrocked pose at the old heading for a frame). Their data carries the spot every frame. SetPosition only when the boat enters another cell.
- LakeBoats::LatePlace from both camera Update hooks (as the player's boat): every drawn lake boat is placed at this frame's pose again right before the frame, and its crew's bodies are pinned onto their seats (60 max), as PinRower does for you.
