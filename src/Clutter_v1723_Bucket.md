# v1.7.23 - bucket remeshed, tipped over under the stern seat

- RowRim_Hull.nif, NiNode RowRim_Clutter_Bucket (block 25) and its BSTriShape (block 22), same blocks and shader
  (boat_floor_d/n, vertex colours on). Was a 690-vertex tub at (24, -148, 40.3), scale 0.7, by the fishing supplies.
- New: a coopered bucket, 16 staves (each its own plank of the floor texture, slightly uneven, gaps between), height 19,
  14 across the bottom, 17 across the mouth, 0.85 thick, a recessed bottom disc above the chime, two staves raised as ears
  with rivets, three iron bands and an iron bail (dark vertex colour, no new texture). 3014 verts, 2108 tris.
- Lying on its side under the stern seat (seat underside 62.7, floor 40-46 there): centre (16, -213), mouth toward the
  middle and forward (125 deg), tilted 13 deg to sit on the rising floor, the bail flopped down past the mouth.
  Bounds x 2..27, y -225..-198, z 39.7..59.4. Baked in boat space (node transform identity).
- Built by script from the scratchpad (bk/bucket.py, place.py, build.py). Old hull kept as
  blender/RowRim_Hull_before_bucket.nif.
