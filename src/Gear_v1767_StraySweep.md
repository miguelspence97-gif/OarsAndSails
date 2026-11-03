# v1.7.67: stray supplies swept for good

- Seen: a second Fishing Supplies hovering in the fish tub at the boat spots, not moving with the boat (a copy left from a
  re-place: only the boat's own supplies get their mesh hidden, so a stray one shows).
- Gear::SweepStrays ran only for 30 s after a load. Now it keeps going: every 3 s within 12000 for the first 30 s, then
  every 5 s within 4000. Same rules: a game-made (FF) supplies or bed of our base that is not the boat's own, lying over
  the water or within 600 of the hull, is disabled and deleted ("Gear: stray ... removed" in the log).
