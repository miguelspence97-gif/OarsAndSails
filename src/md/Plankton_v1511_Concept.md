# v1.5.11: plankton like the concept image

The image shows fine, dense sparkles, a wake hugging the hull that opens into a V, oar swirls lingering behind, and the lake boats lit too.

- **Pools:** kSharp 260 → 1500, kSoft 360 → 600.
- **Finer sparkles:** the sharp glows from the boat (wake, dig, trail) are about half the old size (1.2–2.8 vs 2–4.6).
- **Wake():** one function for the player and lake boats.
  - Sides: 110 × density × speed per second, spawned right on the hull's waterline outline (HalfWidth + 0.5–5).
  - The side glows are pushed out 30–90 × speed, so the trail left in the water opens into a V. They live 5–10 s and are mostly sharp and shallow.
  - Stern: 45/s, a fan spreading sideways from behind whichever end leads.
- **Dig:** 60 × density (was 34), 60% sharp.
  - Sharp: sink 0.3–2, life 6–12 s. Soft: sink 1.5–5, life 7–14 s.
  - The swirls hang in the water behind the boat.
- **Trail:** twice as many, mostly sharp, life 5–10 s.
- **Lake boats:**
  - TendOars passes 10 + k to TendBlade, so their catches call `Dig(..., lake=true)`. That's half the count, with no boat throw and no stuck glows.
  - Their trails are lit too.
  - `Plankton::LakeWake(visual, yaw, speed, water)` runs per boat within kFxRange, at 0.6× the player's rate. Boat fields: `pkWake`, `pkStern`.

## v1.5.12: open water farther, with the fine scatter

- **Reach:** open-water glows are kept out to 7000 (kAmbRadius, was 3600). Far ones are drawn up to 7× bigger (was 4×). Things standing in the water are still only looked for out to 3600 (kAnchorRadius).
- **Pools and count:** both open-water pools are 1000 each. 1800 glows by default (0–2000), filling at 400/s.
- **Mix:** half diffuse, a quarter regular sparkles, and a quarter the wake's fine scatter (size 1.2–2.4, 0–3 deep).
- **Ini:** the key is now `OpenCount`, because the old `Open=600` a command had saved would have capped it.

## v1.5.13: wider oar swirls, stuck glows you can see

- **Dig:**
  - Swirl start radius 3–22 (was 0.5–12) and growth 10–36 (was 2–14), so the spiral is about 3× wider.
  - 35% are flung out (was 20%) at 40–110.
  - Life: sharp 2–4.5 s, soft 2.5–5 s (were 6–12 and 7–14).
- **Stuck glows:**
  - Hull 40 → 110, oar 4 → 14 per blade.
  - They were a fifth of a free glow's size; now 1.8–3.2.
  - Hull ones sit at model z 30–40, at and above the waterline. Below it, the water drawn over them hid them.
  - Oar ones are spread over the whole blade (1–28 from the tip).
  - Resting glow: hull 0.4 + 0.5 × speed, oar 0.45 (was 0.1). They stay lit with the oars raised.
  - A dig lights 10 hull glows (was 3).
