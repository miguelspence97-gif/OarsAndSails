# Fix v1.7.45 - nose dives on open water

Report: a user gets random nose dives on open water (no waterfall near).

## Cause
The v1.7.10 fall code has three triggers. The ledge one (CliffAhead) fired on open water:
- The bow probe asks the cell for its water height. Where that query fails (cell with no cell water, water from placed
  water planes only, some water mods) the tip counts as "not wet", why 2.
- CliffAhead then only rejected ground ABOVE the water (a bank). The lake/sea bed far below passed, and the bed further
  on is easily 80+ under the water, so it read as "the water ends at a ledge and the ground falls away" -> Launch.
- In the air, FallStep only landed in water at least 20 LOWER than where it left. Same-level water never counted,
  so the boat fell through its own water, bow pitching down, to the bed. That is the nose dive.

## Fix
1. CliffAhead: the ground at the lip must be about level with the water (within 40 below, 24 above). Deep bed = no ledge.
2. FallStep: after the grace, the same water (within 20) under the boat at or above the hull calls the fall off: back on
   the waterline, a small dip, pitch mostly gone, speed kept. Log: `Fall: false alarm, same water under it`.
   Safety net for any false lip (cell water mismatches at borders too).

Real waterfalls and ledges are unchanged (lower water 40+ down still launches and lands as before).

## For the user
RowRim.log lines starting `Fall:` show which trigger fired and where.
