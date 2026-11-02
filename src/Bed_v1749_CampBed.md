# v1.7.49 - the camping bed, its fade, a steady Fish prompt

## The bed (scripts/clutter/campbed.py <hull in> <hull out> <camping bed.jpg> <texture prefix>)
- Replaces the v1.7.48 Sketchfab bag (not in the hull any more; its CC BY credit taken out of release\README.txt).
  Same node, RowRim_Clutter_SleepingBag, same plugin code places it on the bed's spot.
- From Miguel's "camping bed.jpg" (a top-down shot of a patchwork fur bag with a shearling hood): the bag's outline
  (smoothed, drawstrings dropped) is the mesh's outline; a closed pillow: top 8 high, the hood's rim 11, its inside sunk
  in; the side rolls round (quarter circle, 4.5) to the outline at 2.6 high and under again to the floor.
- Built in columns along the bag (140) and rows across it (44), cosine spaced: a smooth outline. 11,956 verts, 23,908
  tris. 170 long, 68 wide, 13 high at the cuff, base at 0; head (hood) toward +Y as the old bag.
- The cuff (shearling turned over the hide at 0.221-0.292 of the length): a round roll; where it folds into the hood
  (the edge Miguel marked) it rolls into shadow, a deep crease, its shadow falling into the hollow.
- Texture: the picture itself, darker (x0.74), contrast 0.85, background filled from the nearest bag pixel;
  boat_campbed_d.dds 1024x512 DXT1 + mips (350 KB); _n.dds fine fur detail from the picture. Matte (no specular).
- An NiAlphaProperty (0x00EC, blending off) on the shape for the fade.

## Fade (Plugin.cpp TendSleepingBag)
- bagA_ goes 0..1 over 1.2 s toward Gear::BedrollShown(); the node is shown and its alpha set (SetAlphaTree, as the
  tent) while fading; put back whole before it is hidden. Packing up fades it out where it lay.

## The Fish prompt on the tub
- Pick proxies (Board / Steer / Row / Unroll / Fish) were deleted and placed again whenever the boat moved 20 or turned
  0.1: no prompt until the new one's 3D loaded, so the [E] dropped out as the boat drifted. Now moved with SetPosition /
  SetAngle (re-placed only after a 600 jump).
- The aim on the tub: the point at its mouth (+12), a 25 deg cone (was 15), looking down past -0.05 (was -0.10);
  reach 120 from the tub (was 90), also for the bedroll prompt.
