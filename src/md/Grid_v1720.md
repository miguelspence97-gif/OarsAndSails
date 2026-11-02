# v1.7.20: cell grid

`oas "grid on|off"` (or `oas grid` to flip it; saved in RowRim.ini [Debug] CellGrid, off by default).

- Exterior world cells are 4096 x 4096. Every cell edge round you (the 5 x 5 cells centred on yours, about 10000 out each way) gets a see-through wall: 60 walls, 4096 long, 800 tall, from 300 under you to 500 over you, following you up and down.
- Look: light blue, faint fill, a bright line at your own height and bright posts at the cell corners. Your own cell's four walls are brightest; the walls fade out from 7000 to 11000 away.
- Crossing into another cell: "Cell X, Y" on the HUD (and in RowRim.log).
- Files: meshes\RowRim\RowRim_CellGrid.nif (one quad, effect shader, alpha blended, both sides; made from the wake crest nif) and textures\RowRim\cell_grid.dds (64 x 64, uncompressed, 16 KB). Code: src\Grid.inl (as the wake: cloned pieces, each its own material, under the player's cell node).
- Interiors: nothing shown.
