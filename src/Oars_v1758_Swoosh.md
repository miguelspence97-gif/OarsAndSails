# v1.7.58: oar swooshes

- Plankton::Swoosh(from, to, waterZ, lake): a solid streak of plankton glows along the blade's path, every 2.5 units
  (lake boats 6), sparkles at the surface with a soft glow every 4th, drifting aft, gone in about 2 s.
- Fx::TendBlade calls it every frame the blade is in the water (Blade::prev = last frame's tip). Night only, like
  the rest of the plankton ([Plankton] settings apply). Same colour as the other glows (the plankton model).
