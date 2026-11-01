# v1.7.10 - waterfalls and ledges, the old OAS way (Parts 33-37, before the v1.0.38 pull / throw)

- No current toward the edge, no throw: you only go over if you row over.
- Lip check (WorldStep, rowing forward over 40, not beached), at the bow tip:
  - the tip's water is 40+ below the boat's water, or a lower plane 48-160 (or speed x 0.25) past the tip (DropAhead);
  - or the water runs out (no water / land) at a ledge within 24 of the water level, with the ground 80+ below it within
    48-240 (or speed x 0.35) (CliffAhead).
- Launch: speed at least 150, a hop vz = 80 + 0.12 x speed, feeler stop / push cleared. Log "Fall: ... at (x,y)".
- In the air (FallStep, instead of the rest of WorldStep): gravity 600, the heading and speed carry it on (StepTarget),
  the bow dips with the fall angle (fallPitch_, up to 0.6 rad, eased; back to level after). Unloaded space ahead: speed 0.
  0.35 s grace before any landing.
  - Lower water (20+ under where it went over) reached: afloat at its waterline, buoyant dip (impact x 0.5, 60-450),
    speed -5..-20% by how hard. Log "Fall: water landing N down ...".
  - Terrain reached: down on it, 60% of the speed gone, a small dip; the beach / ground code then stops it.
  - Nothing after 8 s or 6000 down: put back at the lip, stopped.
- A lower water plane 150-550 ahead (checked 4x a second): the hull feelers are off (OAS 35.1.1: the fall's rocks and
  spray were stopping the boat at the lip).
- Seated player and passengers ride it glued as always.
