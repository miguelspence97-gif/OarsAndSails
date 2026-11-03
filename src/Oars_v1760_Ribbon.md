# v1.7.60: oar swoosh ribbon

- New Ribbon namespace (end of Wake.inl): 700 streak pieces, meshes\RowRim\RowRim_Swoosh.nif (flat quad half 2.5 x 7 on
  the water, additive 0x100D, own material each), textures\RowRim\swoosh_streak.dds (64x128 DXT5, white soft band tapered
  at the ends, 11 KB). Built by scripts\wake\make_swoosh_nif.py (maginvabsorb_blue.nif template, as the wake) and
  make_swoosh_tex.py.
- Fx::TendBlade: the catch calls Ribbon::Catch (a 28-piece spiral, two thirds of a turn opening from 2 to 18 out, plus 3
  bright pieces at the heart); every frame in the water Ribbon::Sweep lays a piece every 4 units along the blade's path,
  turned along it. Player's oars only (lake boats keep the sprite swoosh).
- Each piece turns about its stroke's catch point (right oar one way, left the other, 0.5-1.1 rad/s, dying away) and
  drifts outward (6-14 units/s, dying away), so the ribbon curls into an opening spiral; it widens 1.6-2.6x and fades
  over 2-3.4 s. Colour 0.30/0.85/1.0 (the plankton's), x night. Ticked after Wake::Tick (Stage "Ribbon::Tick").
- The v1.7.58 sprite swoosh stays as sparse sparkles over the ribbon (every 6 units).
