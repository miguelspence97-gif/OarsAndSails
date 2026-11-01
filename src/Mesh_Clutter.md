# v1.7.0: clutter in the boat

## In the hull mesh
RowRim_Hull.nif grows 804,662 → 995,806 bytes. Backup: `blender/RowRim_Hull_before_clutter.nif`. Script: `scripts/clutter/clutter.py <hull.nif> <out> <nifs folder>`, with nifmerge.py / meshes.py.

Each item is a NiNode wrapper under the root named `RowRim_Clutter_*`. The vanilla meshes are imported as they are: shapes plus shader / texture set / alpha, with collision, controllers and extra data dropped. Their own textures are kept, and the game supplies them.

| item | from | where (model space) |
|---|---|---|
| Bucket | bucket01.nif ×0.7, turned 25° | (24, -148) on the boards, right of the stern |
| SpareOar | shipoar01.nif, diffuse swapped to our kwila | lengthways on the floor at x -52 under all three thwarts (y -116..128), z 40.2–46.9 |
| Keg | meadbarrel01.nif ×0.47, across the boat | under the aft thwart (y -88), feet on the boards, top 61.1 (plank underside 61.7) |
| Bedroll | built: a 62-long roll, radius 8.5, texture `furniture\bedroll\Bedroll01` | hung under the bow thwart (y 113.7) by two rope straps round the plank (x ±19) |
| Rod | built: pole 150 (kwila), corded grip, reel, guides, holder and bracket | on the right gunwale by the stern (66, -184, 82), out over the side and back, about 36° up |
| Rod line | built | from the tip (123, -291, 170), sagging back to the water 640 astern |
| FishBones | built: spine, 11 rib pairs, skull, tail rays; own textures `textures\RowRim\fishbone_d/_n.dds` (64², DXT1, 2.8 KB each) | on the boards by the rope coil, (-17, 166) |

## Fishing Supplies (code, src\Gear.inl)
- The game's own usable activator from Creation Club Fishing: `ccbgssse001-fish.esm` 0x8D0, the "Fishing Supplies" in the screenshot. It's a ref, not a mesh, so "Use" and its script work.
- It's placed from the hull ref at ×0.55 (papyrus SetScale) and moved with the boat every frame (SetPosition / SetAngle with the boat's heading).
- It sits at model (-24, -150, 40.3), left of the stern seat.
- E aimed at it uses it (the boat's E sit is skipped then).
- Nothing is placed without the CC plugin.
- The co-save (GEAR) has its id. After a load the old one is removed and a new one placed.
- Settings in `RowRim.ini [Clutter]`: `Supplies=1`, `SuppliesX/Y/Z`, `SuppliesYaw`, `SuppliesScale`.

## Previews
`scripts/clutter/render2.py`. Stand-in textures are used for the bedroll and the mead barrel: the vanilla ones are in the game BSAs.

## v1.7.1
- The keg is out (it clipped the aft thwart), and so are the 3D fish bones (a 2D sprite from Gemini comes later).
- The rod has no line now. Its butt stands in a wooden holder tube (two rope bands) at model (47, -173, 49), fixed by a cross piece to a mount block on the hull's inner side (62.6, -176, 57). It rises 55 deg, leaning 22 deg back, and clears the gunwale (tip at 127, -205, 172).
- The bedroll is the game's own bedroll01 mesh, with its real textures, UVs and alpha, rolled up:
  - edges longer than 6 along its length split first (2,732 verts),
  - scaled 0.85, then wound into a spiral (1.2 core, 1.8 per turn, 4.5 turns, outer radius 10.6, 72 long),
  - hung under the bow thwart by the two straps.
- RowRim_Hull.nif is 1,028,608 bytes. Rebuild with `python clutter.py blender/RowRim_Hull_before_clutter.nif Data/meshes/RowRim/RowRim_Hull.nif nifs`.

## v1.7.2: fish bones sprite
- Made from Miguel's Gemini picture: alpha from the black background, cropped, 512x128 DXT5 (`textures\RowRim\fishbones_d.dds`, 87 KB), normal `fishbone_n.dds` (flat).
- It's a flat 26 x 7.3 quad, alpha tested (0x12EC, threshold 110), lying on the boards by the rope coil at (-17, 166, 40.45), turned 200 deg.
- The old 64x64 `fishbone_d.dds` is no longer used.

## v1.7.3: fish bones down in the hull
- They're off the boards now, lying in the hull's bottom forward of them, left of the rope coil, between two frames (y 214-240, x -25..-17).
- The sprite is laid on a plane fitted to the planking there, 0.7 above it, tilted with the bottom as it rises toward the bow.
- The frame at y 205-211 is avoided: across it the sprite would have been clipped.

## v1.7.4: freeze after the clutter (20:44)
- The watchdog showed the hang in the game's own frame, not in our per-frame code. The mesh checks out: counts, indices, data sizes, UVs and positions are all fine.
- Miguel remembers the same freeze in OAS when the bilge floor was first added, fixed by its textures / shaders. The OAS floor ended up SHARING the hull's own shader property.
- So the built pieces now share shaders already in the hull instead of their own new shader + texture set blocks:
  - rope straps, rod grip and holder bands use the hull's rope shader (block 20),
  - the holder, mount block and cross piece use the floorboards' shader (block 17),
  - the rod pole uses the spare oar's kwila shader.
- Still with their own shaders: the bucket and the oar (imported, vanilla), the bedroll (its own, with its alpha), and the fish bones sprite (own texture, alpha test).
- If it still freezes: test with blender/RowRim_Hull_before_clutter.nif to be sure it's the clutter, then take items out one by one (bedroll first, then the fish bones).

## v1.7.5: placeholder textures, nothing vanilla in the clutter
- Still froze. In game the hull, the floors and the spare oar went grey (textures not loading) before the freeze; the bedroll looked fine.
- Now no vanilla shader or texture comes in with the clutter (`import_shapes(..., shaders=False)`, so no orphan blocks either). Every clutter shape draws with our own textures:
  - the bucket uses the floorboards' shader (block 17),
  - the spare oar wood and the rod pole use one new shader on the floorboards' settings with `boat_oar_kwila_d` + `boat_floor_n`,
  - the oar's rope wrap, the straps, grip and bands use the hull's rope shader (block 20),
  - the bedroll uses the rope shader too (placeholder: no vanilla Bedroll01, no alpha),
  - the fish bones keep their own sprite shader (our texture).
- The only texture sets in the file now are the hull's own three vanilla ones (swapped at runtime by the paint as before) plus ours.
- Miguel will bring copies of the game's textures (bucket, bedroll) later, or we make our own, and then they go in under `textures\RowRim`.

## v1.7.6: the real cause, shared shader properties
- The freeze, the grey hull, floors and oar, and now the black screen on load, all came from shapes SHARING one BSLightingShaderProperty:
  - since v1.7.0 the rope shader was shared by the straps, the rod grip and the line,
  - v1.7.4 / v1.7.5 even shared the hull's own floorboards (17) and rope (20) shaders with the clutter, which greyed the hull and floor themselves.
- Vanilla nifs never share a shader property between shapes (texture sets at most). The property is set up per geometry.
- Now every shape has its own shader property and texture set (`custom` / `use_shader` make a new one per shape), and clutter.py refuses to write a nif where any shader or alpha property is used twice.
- Hull blocks 1-21 are byte-identical to before the clutter. Textures are still the placeholders from v1.7.5 (our own only).
- Note for OAS: its bilge floor (Part 56.25) shares the hull's shader property, the same pattern.
