# v1.7.32 - tarp tent over the bed

- Source: blender/Gemini_Generated_Image_hzwjq3hzwjq3hzwj.jpg (white canvas; the grommet corner cropped away).
  textures/RowRim/boat_canvas_d.dds 512 DXT1 + mips (175 KB), evened out and toned to a weathered cream, tiling;
  boat_canvas_n.dds 512 DXT5 + mips (341 KB), soft weave, alpha 25 (almost no sheen; the tarp shader has specular off).
- RowRim_Hull.nif: new NiNode RowRim_Clutter_Tent (child of the root, flag hidden) with
  - RowRim_TentTarp: an A-frame cloth from the ridge down to just over the gunwales on both sides, following the hull's
    gunwale line (so it closes at the stem), sagging 4 between ridge and gunwale and between poles, a short aft flap
    drooping past the poles, both faces (inside slightly darker). 2912 verts.
  - RowRim_TentPoles: skinny round poles (r 1.0-1.15, kwila oar wood): a ridge pole from the stem head (y 316, z 117)
    aft to an A-frame at y -62 (ridge z 125, feet on the floor at x +-40), and a second A-frame at y 150 (ridge 121,
    feet +-32). The A-frame poles cross 7 over the ridge. 192 verts.
  - No collision objects: you walk and lie through/under it freely.
- Plugin (v1.7.32): the tent is shown on the player's boat only while the bedroll is laid out (same switch that hides the
  rolled bedroll); hidden in the nif, so lake boats and the stowed state never show it.
- Built by blender/../scratchpad tarp/tent.py (copy in this folder of the release notes). Old hull:
  blender/RowRim_Hull_before_tent.nif.
