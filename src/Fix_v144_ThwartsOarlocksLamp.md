# v1.4.4: thwarts, oarlocks, player lantern

Hull nif (RowRim_Hull.nif, same size, no new shapes; script scripts/hull/hull_v144_edit.py):
- The three main thwarts (y -99, 0, +99) moved 6.5 toward the bow (z follows the gunwale sheer, under 0.25). The middle thwart is now centred on the rowing oarlocks (y 6.7).
- The fore and aft oarlock pairs replaced by copies of the middle pair (its taller fork, the same seat on the gunwale), each centred on its moved thwart (y -92.9 and 105.3), turned with the gunwale (yaw 6.5 / 8.0 deg) as the vanilla pairs were; normals turned with them. The middle pair is unchanged (the clip's pivot).
- Backup: blender/RowRim_Hull_before_v144.nif.

Player lantern (code):
- A clone of the lantern (0002D847) hangs on the boat model from the stern spiral hook (LakeBoats::kLampHook 0,-341,140) with the lake boats' pendulum (1.6 s, damping 0.35, 12 deg); flame and particles off.
- Shown day and night; a light ref (0001CBB3) at its centre at night, tracked with the lake boat parts (purged after a load, placed again).
- rr "lamp off" / rr "lamp on".
