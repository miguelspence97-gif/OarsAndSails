# v1.5.9: shoreline plankton, a pool of its own

What changed:
- **Open water:** spread evenly again, out of the shallows (deeper than 40). The v1.5.6 shallows and rocks bias is gone from it.
- **New shoreline pool:** 180 sharp + 220 soft glows (`kShoreLo..kFree`), with its own count: `[Plankton] Shore=400`, `rr "plankton shore N"`.
- **Where the shore is:**
  - A 44×44 grid of water depths round the camera, 128 apart (2800 out each way).
  - Filled 3 rows a frame; scanned again every 5 s, or once the camera is 700 off the grid's centre.
  - A wet cell next to dry land is a shore spot. Its normal points to deeper water (the depth gradient).
  - Cells that aren't loaded never count as an edge.
- **Lines:** each spawn is one single-file line.
  - 6–18 glows, 9–16 apart, along the shore tangent.
  - Each glow walks in or out along the normal until it sits at the line's depth (6–22), so the line follows the coast's curve.
  - A quarter of the lines are arcs round anchors instead (rocks, jetty posts, moored boats, as found by the v1.5.6 scan).
- **Movement:**
  - The whole line washes in and out together along the normal (4–11 units at 0.45–0.85 rad/s). The phase shifts 0.12 per glow, so the wash ripples along the line.
  - The line creeps along the shore at 1.5–5 units/s.
  - It fades in from one end, lasts 14–34 s and fades out.
  - Waders still flare and push these glows.
- **Cost:** at most 2 new lines a frame, up to 3 a second.

## v1.5.10: scattered, not single file

The v1.5.9 lines looked too straight and single file, and some rows showed up out in open water. The fix:
- **Patches, not lines:** each spawn is now a loose patch of 6–18 glows spread along about 230 of shore.
- **Depth:** each glow gets its own depth. Most sit just off the edge (2 + an exponential, mean 14), so the band is thick at the edge and thins out to sea.
- **Jitter:** after fitting to its depth, each glow is nudged ±14 along the shore and -4 to +10 out, so nothing reads as a row.
- **Rocks, jetties and moored boats:** the arcs are now scattered patches round them, and only 1 in 5 patches goes there.
- **Shore spots in open water:** a spot only counts when the water there is 3–250 deep, so sheer drops and unknown floors no longer make shore spots out in open water.
- **Movement:** glows in a patch wash about together but out of step. Each creeps at its own ±2.5, and lives 55–125% of the patch life.
