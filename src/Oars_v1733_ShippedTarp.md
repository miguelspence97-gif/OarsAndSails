# v1.7.33 - oars shipped for the night, tarp over the wood

1. Oars: while the bedroll is laid out (and you are not seated) the oars blend (0.8 s, as the stow) into a shipped pose:
   each lies along its gunwale in the middle oarlock (lockLocal_, 93.3 / 6.7 / 76.5) and the aft oarlock (88.3 / -95 /
   77.9, the forks' shaft centre read from the hull mesh), handle 30 forward of the middle one, blade flat, aft (clear of
   the tent, which starts at y 50). Oar::ShipLocal; TestHull::shipW_. Sitting down (the bed is rolled up) brings them back.
2. Tarp: it no longer cuts through the gunwale cap, the outer rail or the oarlocks. The cloth is checked against points
   sampled over the hull's own triangles (the rail's vertices alone are 30 apart) and lifted 1.2-1.5 over anything
   within 6 under it, the part hanging outside pushed 2 out past the hull side at its height; it rolls over the outer
   rail at the gunwale's height before dropping; rows closer together toward the gunwale; the front oarlocks show as
   bumps under the cloth. (blender/tent.py)
