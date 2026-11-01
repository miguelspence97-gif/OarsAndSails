# v1.7.37 - the missing rod, stowed poles and stowed tarp; the spots on the tent

- Cause: the tent's fade sets the material alpha of its shapes; the game shares one material between shapes with the
  same textures and settings, so the tent poles (boat_oar_kwila_d) and tarp (boat_canvas_d) shared theirs with the rod,
  the stowed pole bundle and the stowed tarp roll. Fading the tent out left those at alpha ~0: invisible.
- The tent now has its own copies: textures/RowRim/boat_tent_d.dds + boat_tent_n.dds (the grey canvas), and
  boat_tentpole_d.dds (the kwila) with boat_tent_n.dds. The stowed roll keeps boat_canvas_*, the rod and bundle kwila.
  The tent's alpha is also put back to 1 just before it is hidden.
- Spots on both sides of the tent (wood showing through): the cloth is kept at least 4 off every hull, rail, knee,
  oarlock and rope surface (was 0.8-2; checked: 4.15 at the closest), and the blending is switched off once the tent
  is fully up (only on while it fades).
- Spare oar removed (its node taken off the root).
