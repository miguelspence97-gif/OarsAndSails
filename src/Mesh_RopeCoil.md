# Mesh: bow rope (knot, run down, coil)

RowRim_Hull.nif (script `scripts/hull/hull_ropecoil.py <in> <out> [buoy rope nif] [seed]`, run on `blender/RowRim_Hull_before_ropecoil.nif`):
- New shape `RowRim_BowRope` (blocks 19-21), a child of the root. Looks only, no collision. It replaces the first coil on the floorboards.
- **Knot:** three round turns round the bow post (z 103-109.5; the post's lean and width were measured from the mesh), a half hitch back across them, and a short loose end sticking up.
- **The run:** the rope leaves the aft side of the post and lies down along the inside of the bow onto the bottom, a little slack.
- **The coil:** a messy 6-loop pile on the hull bottom just forward of the floorboards, in the hollow at (1, 199), z 38-41 (below the board tops at 40.3). The loops are uneven (radius 6.9-10.6, squashed, turned, one slid off to the side, out of level), with the end running out of the bottom of the pile.
- Every rope point is kept on or above the hull, details and boards surface under it.
- Rope radius 1.1, 6 sides, a ring every 1.6 units. 2613 verts, 4476 tris. File 694 KB -> 805 KB.
- **Texture (v2):** our own `textures\RowRim\boat_rope_d.dds` / `boat_rope_n.dds` (512x256 DXT1, 87 KB each), made by `scripts/hull/make_rope_tex.py`. It is a dark, weathered three-strand laid hemp rope with dark lay grooves, fibres, and very dark green moss in patches, thickest in the grooves, and a normal map from the same height. v runs once round the rope; u covers two lay lengths (a lay is 3.5 rope diameters), so the strands run at about 40 deg. This replaces RiftenRope01, whose layout didn't suit a round rope. The shader is still from `nifs\byohfisherybuoyrope00.nif`.
- The paint code ignores it.
- Previews in blender/: bowrope_bow.jpg, bowrope_knot.jpg, bowrope_coil.jpg, bowrope_wide.jpg (rendered with the real rope texture; bowrope_texture.png is the knot and coil close up).
- Backup: blender/RowRim_Hull_before_ropecoil.nif (no rope at all).

## Texture v3: Miguel's Gemini rope
- `blender/gemini rope.jpg` (1456x720) kept Miguel's strand layout (45 deg, period about 2.84x ours), so `scripts/hull/gemini_to_rope.py <jpg> <d.png> <n.png>` scales it to 512x256 and makes it tile. The seams are blended with copies shifted by whole strands (x 256, then x+y 128), so the grooves stay lined up across the joins.
- The normal map is rebuilt from the image's own shading (dark grooves low, plus a little fibre).
- Same sizes as before (512x256 DXT1, 87 KB each). The procedural one can be remade with make_rope_tex.py.
