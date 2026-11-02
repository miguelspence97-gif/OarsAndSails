# Mesh: seats further to the bow, floorboards

RowRim_Hull.nif (script scripts/hull/hull_floorboards.py, needs the OAS floor nif for its shader block):
- The three main thwarts 8.3 more toward the bow (total 14.8): middle at y 15 (the rower's seat), fore 113.6, aft -84.6. Oarlocks not moved (the middle pair is the clip's pivot).
- New shape RowRim_Floorboards (blocks 16-18, shader copied from the OAS BilgeFloor, textures textures\RowRim\boat_floor_d.dds / _n.dds, already shipped): 14 loose boards 9 wide on the texture's 10 grid (1 gaps), 1.5 thick, top z 40.3 (2.3 above the floor), lying on the frames (z 38.6); each board cut square 4 short of where the hull curves up, so the ends step with the hull; tiny height jitter; four battens under them at y +-52, +-150.
- Not named BilgeFloor on purpose (the water-block code hides that name).
- 677 KB -> 690 KB. Backup: blender/RowRim_Hull_before_floor.nif. Collision unchanged.
- The rower's feet: the clip puts them on the old floor (38), the boards are 2.3 higher; tell Claude if they sink in.

## Hand-cut boards (same day)
- Each board end is cut at an angle toward the hull's curve (its two edges aim at where the hull rises), missing it by 0.5-4.5 per corner, never square; about a third of the corners trimmed again with a rougher second cut.
- Boards 8.3-9.4 wide, shifted up to 0.35 off the grid, turned up to 0.6 deg, height and thickness jitter: uneven gaps.
- Seeded (1447): rerun hull_floorboards.py with another seed (4th argument) for a different set. 690 KB -> 694 KB.
