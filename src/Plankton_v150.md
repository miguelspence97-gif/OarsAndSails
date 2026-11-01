# v1.5.0: bioluminescent plankton

Sea sparkle round the player's boat. It's faint from late afternoon (16:00, about a third by 17:30), full from 19:30 to 04:30, and gone by 06:30.

## What glows
- **Each oar catch** (where the dig sound plays): about 22 glows burst from the blade. They spiral out of the blade's swirl (the two oars turn opposite ways), get thrown aft against the boat's travel, and sink to about a metre (70 units). Each one flares in 0.1-0.25 s, then fades over 3-7 s with a little twinkle. Deeper ones are dimmer.
- **The blade dragging through the water:** 2-3 more at each trail ripple.
- **The hull moving:** the boat lights the water it disturbs along both sides at the waterline (up to 55 a second at full speed) and closing in behind the stern (18 a second). These stay in the water where they were made, so a glowing trail is left behind. The faster you go, the more there are.
- **Clinging ones:**
  - 24 spots on the outside of the hull round the waterline: a low glow that rises as you row and decays over about 12 s, and a few flare at each catch.
  - 4 on each oar blade: they flare at every dip and fade over about 5 s.

## How
- `meshes\RowRim\RowRim_Plankton.nif`: one small additive camera-facing quad (NiBillboardNode + BSEffectShaderProperty + NiAlphaProperty 0x100D). The blocks are laid out like the vanilla-style spell glow `maginvabsorb_blue.nif`, and the script is `scripts/plankton/make_plankton_nif.py`. Colour (0.30, 0.85, 1.0) x 2.2.
- `textures\RowRim\plankton_glow.dds`: 64x64 DXT5, a white core with a soft halo in alpha (5.6 KB).
- `src\Plankton.inl` (included before LakeBoats.inl):
  - 360 free glows hang under the player's cell node in world space (re-hung when the cell changes).
  - Clinging ones are children of the boat model and the oar wrappers.
  - Brightness is shown as size: a glow below 0.08 is culled.
  - Fx::TendBlade got an `a_glow` side argument: the player's oars pass 0/1, lake boats -1 (no plankton on them yet).
- Hull outline and cling spots were measured from RowRim_Hull.nif (waterline at model z 32).

## Console and ini
- `rr plankton on|off`
- `rr plankton night`: always on, to test by day
- `rr plankton auto`: by the time of day
- `rr plankton never`
- `rr "plankton density N"` (0-3) and `rr "plankton size N"` (0.2-4)
- Saved in RowRim.ini `[Plankton] On / Force / Density / Size`.
- Log: `Plankton: ...` (model load, glows made, the first digs).

## Not yet
- Lake boats' oars and wakes.
- Colour or brightness per glow (all share one material, so brightness is size).

## v1.5.1
- **Underwater look:** while the third person camera itself is under the lake at its spot, `Underwater::Tend` sets the water system's `playerUnderwater` / `partiallyUnderwater` / `underwaterHeight`. It runs from the player update and the third person camera update, after the camera moves. The game keyed this to the player (up in the boat), so it was only on while the camera sat just under the surface. Log: `Underwater: camera under / out of the water`.
- **Seen from above:** the water surface hides anything under it, so with the camera in the air the free glows are drawn on the surface (0.8 above it), the deeper ones smaller and fainter. Under water they're drawn at their depth.
- **Bigger free glows:** dig 2.6-5.2, trail 2-3.6, wake 2-4, stern 2.4-4.4 (were about half that).
- **Hull:** 40 glows scattered anywhere along both sides in the waterline band (model z 22-37) on the skin, instead of a few fixed spots.
- **Oar blades:** they now hang on the oar model inside the wrapper. Oar::KeepFade resets every child of the wrapper to identity each frame, which had pulled them inside the oar.

## v1.5.2: diffuse under-surface glows, chaotic oar swirls
- **Two kinds of free glow:**
  - 200 sharp sparkles (`RowRim_Plankton.nif`) near the surface.
  - 260 diffuse ones (`RowRim_PlanktonSoft.nif` + `plankton_soft.dds`): a wide soft blob, deeper blue-green (0.16, 0.62, 0.85) x 0.9, soft falloff 12 so the edge melts into what it touches. They sit 6-70 deep and move at 0.35-0.8 of the pace.
  - Seen from above, the diffuse ones lie just on the surface, get wider and fainter the deeper they are, and the slower pace sells the depth. Under water both are drawn at their depth.
- **Oar catch:** 34 glows (was 22), about 60% diffuse.
  - They come from two or three eddies off the blade, not one point.
  - Each glow has its own spin speed (0.6-4.8, 18% against the swirl) and radius.
  - A churn wobble grows as the swirl breaks up.
  - 20% get flung out of the swirl.
- **Trail, wake and stern** mix both kinds (the deeper ones are diffuse).
- `scripts/plankton/make_plankton_nif.py <template> <out> [soft]` builds either model.

## v1.5.3: no clipping through the hull, plankton over the whole water
- **Through the hull:** each frame, a free glow that falls inside the boat's waterline outline (model |y| < 250, |x| < half width + 4) isn't drawn. Open-water ones never start there.
- **Open water:** 340 more glows (110 sparkles, 230 diffuse) are kept round the camera out to 3600 units (none within 260). They spread evenly over the area, on open water only (water at least 60 deep), not touched by the boat.
  - Each fades in over 1.5-4 s, drifts with a slow current (1.5-6 u/s), glows for 8-30 s, then fades out over 3-8 s.
  - New ones top the field back up, up to 80 a second. Ones left more than 4500 away are dropped.
  - The field fills to 40% at dusk and 100% at night, scaled by density (capped at 1).
- **Far glows:** drawn up to 4x bigger with distance (from 700 out), so they stay points of light, not lost pixels.

## v1.5.4
- **Open-water plankton fixed:** v1.5.3 measured "too far, drop it" from a centre at height 0 against a camera 14000 down (Sea of Ghosts water is at -14000), so each one died the frame it was made. The distance is measured flat now, and the centre sits on the water.
- **More of it:** 600 open-water glows (200 sparkles, 400 diffuse) at density 3, scaled down for lower density. Topped up at up to 150 a second, with a slow twinkle. Log every 10 s: `Plankton: open water N of M glowing`.
- **Defaults:** density 3 (range 0-6), size 0.5. Boat pools 260 sparkles and 360 diffuse.

## v1.5.5: open water has its own settings
- `rr "plankton open N"`: how many open-water glows, 0-600 (default 600). It no longer follows density.
- `rr "plankton opensize N"`: open-water glow size, 0.2-4 (default 1). It multiplies `size`.
- Saved as RowRim.ini `[Plankton] Open / OpenSize`.

## v1.5.6: shorelines, things in the water, waders
- **Things in the water:** every 3 s, statics, activators and movable statics within 3600 whose bounds cross the water surface (rocks, jetty posts and decks, moored boats) are listed, up to 240. Ours (FF) and actors are skipped. Log: `Plankton: N things standing in the water round you`.
- **Where open-water glows start:**
  - 42% round one of those things: half on a ring at its edge, half under or inside its footprint.
  - The rest anywhere. Water under 140 deep (the shoreline band) is always kept; deeper water only 22% of the time, so the shores are thick with it.
  - Dry land (under 3 deep) is never used.
- **Waders:** you, followers and NPCs with their feet 4-130 under the surface (the boat's hidden carrier, 220 down, isn't counted).
  - Moving faster than 12 u/s, they throw up to about 38 glows a second round them, scaled with speed and density.
  - Open-water glows within 150 flare (up to about 4.6x) and are pushed aside, easing back over about 1.4 s.
