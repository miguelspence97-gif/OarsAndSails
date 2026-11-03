# v1.7.64: plankton in still water only

- Plankton::StillWater(p, waterZ): false inside a river / stream water volume (Flow::Vol::stream: its water type or
  mesh named river, stream, creek, brook, rapid, waterfall, cascade, falls; now kept even with no current) or where the
  current is over 25 u/s (water with a current, named rapids / falls meshes).
- Spawn() refuses there (every kind: wake, digs, trails, swooshes, open water, shoreline); Dig() does nothing there (no
  lit blade or hull either); the ribbon's catch is skipped there. Glows carried into running water fade out in 0.6 s.
- Lakes and the sea (still water, no current) keep everything as before.
