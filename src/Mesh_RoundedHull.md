# Mesh: rounded hull (after v1.4.4)

RowRim_Hull.nif, shapes 10 (hull skins) and 13 (gunwale, posts, ribs, keel, thwarts) together: one level of Loop subdivision (scripts/hull/hull_round.py + hull_loop.py), the two shapes' shared seams welded so no gaps open.
- Creases over 55 deg stay sharp (box edges, plank edges, the floor's corner lines follow the curve rule); open edges follow the curve rule.
- The flat boards (3 thwarts, stern bench) are held rigid: split only, never moved or reshaded.
- Normals rebuilt per smoothing group, tangent frames re-orthogonalised (texture and UVs unchanged).
- Oarlocks (shape 7) untouched. Checked after: floor z 38, oarlock pads still on the gunwale (0.2), spiral hook moved 0.8 down (lantern hook unchanged), outer bounds the same.
- Triangles: 3424 -> 13696 (skins), 2358 -> 9432 (details). File 242 KB -> 677 KB.
- Backup: blender/RowRim_Hull_before_round.nif. Collision unchanged (vanilla hull).
