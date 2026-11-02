# v1.7.45 - a tub of fish in place of the Fishing Supplies

Asked: bake the tub-of-fish (Miguel's reference picture) into the hull floor, hide the game's Fishing Supplies mesh but
keep its Use / script, out of the way of the player's feet and the passengers' legs.

## Mesh (RowRim_Hull.nif, NiNode RowRim_Clutter_FishBucket, baked in boat space)
- Built by `scripts/clutter/fishbucket.py <hull in> <hull out> [fish_d.dds out]`. Finds the floorboards by their
  texture (boat_floor_d), so it works on the current hull; refuses a hull that already has the tub.
- Spot: (-44.5, -143), port side aft, on the plank tops (z 40.4). As far out as the floorboards go there (their edge is
  about x -53 at y -143); clear of the helmsman's feet (centre line aft) and aft of the aft thwart crew's feet.
  Bounds x -54.9..-34.5, y -153.8..-130.6, z 40.4..60.8. No hull geometry inside it (checked on the Oct 31 hull).
- Tub: 14 tapered staves (radius 8 at the bottom, 10 at the mouth, 15 high), floor wood, two staves raised as ears
  fore and aft, three iron bands with rivets, ear pins and a bail laid down over the port side (dark vertex colour on the
  floor wood: no new wood/iron texture).
- Fish: a big one across the top with its tail over the rim, 9 heaped under it, 4 heads up against the staves, a dark
  fill under them. ~5800 verts total, 4 shapes, each its own shader property (v1.7.6 rule).
- New texture: `textures\RowRim\fish_d.dds` 256x128 DXT1 + mips (22 KB): back to belly, scales, gill line, orange eye,
  red tail and pectoral. Normal map: the existing flat fishbone_n.dds.

## Code (Gear.inl)
- The supplies ref is still placed and moved with the boat (its Use and script, the fishing marker link as before),
  but always on the tub (kTubX/Y/Z, ini SuppliesX.. ignored) and its 3D app-culled in LatePin: not drawn, still picked by
  the crosshair (as the pick proxies).
- oas "num gear" skips the supplies (only the bedroll is placed with the numpad).

## Not done / later
- The supplies' collision box (the CC cluster at 0.5) decides where the prompt shows; if it is off the tub or awkward,
  that is the next fix (Miguel: "may also need fixing later").

## v1.7.46: the game's supplies mesh removed from view for good
- Every node of the supplies' 3D is app-culled and its fade set to 0, in Tend (before it is ever drawn) and every frame
  in LatePin (a cell hop or reload brings its 3D back). Only its collision (the crosshair pick on the tub) and its
  script (Use / fishing) are left. A ref has to exist for the CC script to run: it cannot sit on a mesh in our hull.
