# v1.5.16: the boat's own V wake (src\Wake.inl)

The game's ripples are a 2D normal-map trick, and they didn't work for the hull. This is a mesh wake lying on the water.

**Spots:**
- Every 30 units the boat moves (above speed 25), a spot is laid: the bow and stern points of the boat model (y ±236 by whichever end leads), the heading, the speed, the time and the water height.
- 140 spots are kept. Each lasts 9 s, as long as the plankton wake, and is fixed in the world once laid, so the V follows turns.

**Arms:** two pieces per spot, `RowRim_WakeCrest.nif`.
- Placement: the spot's bow ± right × 0.354 × speed × age (the Kelvin angle, 19.5°), out to at most 1100.
- Direction: each piece is turned along its arm (between its neighbouring spots).
- Size: scale 1 + 0.25 × age, up to 3.
- Fade: in over 0.4 s, out as 1 - (age/9)². Strength 0.2 + 0.45 × speed.
- Pieces under the boat's waterline outline aren't drawn, so the arms show from the hull's sides back.

**Wash:** one piece per spot at the stern, `RowRim_WakeWash.nif`.
- Size: scale 0.8 + 0.35 × age, up to 4.
- Fade: (1 - age/6.5)², strength 0.25 + 0.5 × speed.

**Meshes:** flat quads, alpha blended (0x10ED), white textures.
- Crest: half 34 × 45, `textures\RowRim\wake_crest.dds` 128×256 DXT5 (four ripple lines with foam).
- Wash: half 60 × 50, `wake_wash.dds` 256×256 DXT5 (churned foam).
- Built by `scripts\wake\make_wake_nif.py` and `make_wake_tex.py`.

**Colour and fading:**
- Each piece gets its own copy of its effect material (po3's swap pattern), so it can fade on its own through baseColor alpha.
- Colour by time of day: white foam (0.82, 0.88, 0.90) by day, dim blue-green (0.10, 0.24, 0.28) at night.

**Ini:** `[Wake] On=1`. 420 pieces under the player's cell node in world space, like the plankton.

## v1.5.17: a faint V, a stern trail that doesn't repeat

The V read as too strong and too white, and the stern trail showed its repeating texture. The fix:
- **Arms:**
  - The crest texture is now 3 soft ripple bands at 60% alpha.
  - Strength (0.06 + 0.16 × speed) × (1 - age/9)².
  - Colour: a grey-green sheen (0.55, 0.63, 0.66) by day.
- **Stern trail:**
  - Each piece is now one round, soft-edged patch of churned water with foam streaks (the wash nif is half 60 × 60).
  - Every patch is turned to its own random angle, scaled 0.8–1.2 and nudged ±8, so the overlapping patches never line up into a pattern.
  - Strength (0.18 + 0.3 × speed) × (1 - age/6.5)^1.5. Pale foam colour.

## v1.5.18: the stern trail only, as wide as the hull

- The V arms are off (`kArms = false` in Wake.inl).
- The wash patch starts at scale 1.9 (120 across with a soft edge, so about 173 seen, the hull's widest point) and grows only slightly, to x1.25 at most. Each patch is x0.9-1.1 and nudged up to 12 either way.

## v1.5.19: stronger

The trail was barely visible in game. Strength is now (0.4 + 0.45 x speed), was 0.18 + 0.3, and the colour is paler foam: (0.88, 0.92, 0.93) by day, (0.14, 0.28, 0.32) at night.
