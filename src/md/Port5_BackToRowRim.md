# v1.1.0: back to the RowRim boat only

Miguel: RowRim was right as it was (boarding and unboarding, the carrier actor, the real collision, cell crossing). The OAS port was only meant to add the rowing animation, the textures and the boat movement with its fake collision. The full OAS port (v0.7.0) was a wrong turn.

## Taken back out
- Boat.cpp, BoatBow.cpp and LakeBoats.cpp are no longer built (CMakeLists lists only Plugin.cpp). The files are still in src and can be deleted.
- Gone with them: the OAS boat and its boarding, crew, lake boats, docks, lamps, the OAS console (`rr` was OAS), the OAS hooks (camera, save, input), the OAS co-save records, the trampoline.
- RowRim.esl is not used any more (it can be unticked).

## Kept on the RowRim boat (all in Plugin.cpp)
- Row clip (OarsRowStart via RowRim's own Nemesis list) and the camera animationRotation reset.
- Oars: stowed, blending into the clip, hand lock.
- Row physics (OAS row profile).
- Boat motion: idle rock, stroke squat, turn lean, weight tilt.
- Water following and fake collision (shore, rocks and banks, unloaded cells).
- No stall at cell hops (v1.0.1).
- New here: the paints and kwila oars, copied from OAS Part 30 / 32 / 56.60 (`Paint::Apply`, `Paint::OarWood`).

## Console: `rr` again (no more rrt)
- `rr boat` / `rr spawn`: place the boat. `rr unboat` / `rr delete`: remove it.
- `rr paint blue|black|red|white|bluedesign|beige|vanilla`.
- `rr height N` (or `rr boat height N`), -30..30.
- Everything from step 1: sit, seat, rowseat, oar, rock, bow, info...
- Unquoted words glued by the console (paintblue, height10) are split again.

## Co-save
HULL record v3 adds the paint and the height. v1/v2 saves still load. OAS records in saves made with v0.7–v1.0 are ignored.
