# v1.7.31 - leather on the rolled bedroll

- Source: blender/closeup_leather-005-1.jpg (1024, oiled brown leather with creases).
- textures/RowRim/boat_leather_d.dds: 512x512 DXT1 with mips (174 KB), edges blended with a half-offset copy so it tiles.
- textures/RowRim/boat_leather_n.dds: 512x512 DXT5 with mips (341 KB), normals from the creases (soft), alpha a light
  sheen (more on the worn ridges).
- RowRim_Hull.nif: BSShaderTextureSet 33 (the roll body, BSTriShape 35 under RowRim_Clutter_Bedroll) now points at the
  leather pair; the ties (shape 38, set 36) keep the rope textures. No geometry change. Old hull kept as
  blender/RowRim_Hull_before_leather.nif.
