# v1.7.48 - a leather and sheepskin sleeping bag for the bed

Asked: use the Sketchfab "Sleeping bag" with Miguel's leather / sheepskin picture (blender\sleeping bag.jpg).

## License
"Sleeping bag" (https://skfb.ly/6xZyn) by Tactical_Beard, CC BY 4.0 (Sketchfab credit line, from Miguel). Credit added
to release\README.txt; put the same lines on the Nexus page. The zip (blender\sleeping-bag.zip) has no license file.

## Mesh (scripts/clutter/sleepingbag.py <hull in> <hull out> <model.dae>)
- 226 verts, 378 tris. Collada Y up, length on Z -> Skyrim x = X, y = -Z, z = Y, scaled 88.5 to the vanilla
  Bedroll01 footprint (88.6 x 177, 12.4 high, base at 0), so the same spot / turn / scale (0.9) as the bed furniture fits.
- One BSTriShape under NiNode RowRim_Clutter_SleepingBag, own shader (the floorboards' settings), node app culled in the
  nif (flags 0xF): nothing shows until the plugin puts it on the bed. Hull 1,925,430 -> about 1,935,000 bytes.

## Textures (scripts/clutter/sleepingbag_tex.py: sbag folder with ref.jpg and the unzipped model)
- boat_sleepingbag_d.dds / _n.dds, 1024 DXT1 + mips, 699 KB each (Miguel gave a 1024 picture: 1024 it is).
- Outer shell (UV u < 0.5): the leather from his picture, one piece, no repeats; lining and folded flap (u > 0.5): the
  sheepskin, twice down (second flipped). Shaded by the original quilting / folds; the plaid and its brand tag are gone.
- Normal: the model's own map (quilting, folds) plus fine detail from the new colour map.

## Code
- Plugin.cpp TendSleepingBag (each frame after the roll cull): finds the node once per boat model; while
  Gear::BedrollShown() it is shown and set to the bed's spot (Gear::BedPose: [Clutter] BedrollX/Y/Z, Yaw, scale), hidden
  otherwise. Gear::g_ownBag = node found: the game's bedroll mesh is hidden (HideMesh, in Tend and LatePin) and the
  furniture stays for sleeping. A hull without the node keeps the game's bedroll drawn. Log: "Bed: ...".
- The bag rides the drawn boat (it is in the hull model), so it rocks with it; num gear moves it with the bed.
