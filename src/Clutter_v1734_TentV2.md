# v1.7.34 - the bed tent, second design

From the 10:55-10:57 screenshots: rough edges, the tuck under the gunwale clipping, an A-frame pole standing in the
bed, the canvas too white, the tent popping in.

- Frame (blender/tent2.py): the way in at y 50 (just forward of the rower's thwart) is square: two posts up the hull
  sides (x +-W0 = gunwale - 9, from the floor to 112), a crossbar across their tops (z 109.5), a short king post to the
  ridge (116). Eave poles from the post tops and a ridge pole run forward to the stem head (316, 117). Nothing stands in
  the bed; the middle A-frame is gone.
- Cloth: roof (ridge -> eave, sagging 2 between) then wall (eave -> over the gunwale, bellied out 2) then the hang down
  the outside (port ~14, starboard ~10, varying), a short valance drooping past the crossbar. 110 x 30 grid, both faces.
  Fitting: 8 rounds of "lift over what is under it as a cone (2 over, falling 0.55 per unit: rounded bumps over the
  oarlocks, not plateaus), push the hang out past the hull side" then Laplacian smoothing, then a face pass (9 points per
  quad against the hull surfaces and the poles' tops sampled every unit). 13200 verts.
- Grey canvas: boat_canvas_d.dds re-toned to a mid grey (mean ~110), weave a touch stronger (same size).
- Each tent shape now has a blended NiAlphaProperty (0x00ED, z-write kept by the shader); the plugin fades the material
  alpha 0 -> 1 over 1.5 s when the bed is laid out, and back out when it is packed up (TestHull::TendTent). The laid-out
  bed (and any gear piece) fades in over 1.5 s after it is placed (Gear::LatePin, currentFade ramp).
- A lantern (CandleLanternwithCandle01, flame kept) stands on the floorboards inside the way in, port side
  (-52, 66, 40.4), turned a little; it is lit (the warm lamp light, radius 300, day and night) once the tent is half up,
  gone when it fades out.
