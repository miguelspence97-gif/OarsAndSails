# v1.5.14: our own underwater filter for the camera

The game's underwater look only shows while the camera is just under the surface. v1.5.1 forced the water system underwater, and that crashed the water culling (v1.5.8).

**How it works now:**
- While the camera is more than 2 under the water at its spot, the image space manager's `overrideBaseData` points at our own ImageSpaceBaseData. This is the slot photo-mode mods use.
- It never touches TESWaterSystem.
- It fades in and out over 0.5 s, starting from the game's current look.
- When the camera comes up, the pointer goes back to whatever it was before.
- If something else takes the override while we hold it (a weather, a spell, another mod), we let go.

**The look:** a clear, cool teal.
- Tint 0.38 of (0.06, 0.36, 0.48).
- Saturation ×0.7, brightness ×0.8, contrast ×1.15.
- Bloom: threshold ×0.6, scale ×1.8 (at least 0.6), receive threshold ×0.6, so the plankton glows bloom out of the water.
- No depth-of-field blur.

**Settings:**
- `RowRim.ini [Underwater] Filter=1`
- `rr underwater on|off`

**Log:** "Underwater: camera under/out of the water … our filter on".

**If it shows no change in game:** the engine may not read the override in this state. The next option is a TESImageSpaceModifier triggered from code (an IMAD built at runtime).

## v1.5.15: blue out by about 30 m

**How it works:**
- Sky works its fog out from the weather every frame. While the camera is under, Underwater::FogTend writes ours over it, at the player update and again at both camera updates (right before drawing). It's blended in by the same 0.5 s amount as the colour grading.
- The values: fogNear 150 → fogFar 2100 (about 30 m), power 1, clamp 1.
- Colours: skyColor[1] (fog near) and [12] (fog far) set to a deep teal, darker at night.
- If the sky hasn't worked its own fog out again by the time the camera is out, the sky's values are written back.

**Plankton:** under water, the glows ignore fog (additive effect shader), so they fade by camera distance over the same near/far.

**Ini:** `[Underwater] FogNear=150`, `FogFar=2100`.
