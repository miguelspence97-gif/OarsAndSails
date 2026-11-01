# v1.2.4: rower shift while rowing, oars on the hands, oars rest in the oarlocks

## Rower toward the bow while rowing
- While the row clip plays, the rower sits `rowShift_` (default 15) further toward the bow. The shift eases in and out over 0.5 s.
- At rest and at the helm the seats are as set. The stand-up spot uses the seat as set, not the shift.
- `rr "row shift N"` (-40..60) to tune live. Tell me the number to make it the default.

## Oars on the hands, through the oarlocks
- The hand lock moved the oar grip onto the hand by 30 at most. The log showed it capped on every frame, which is why the hands sat off the handles.
- The cap is now 120: the grip always lands on the hand and the shaft runs hand -> oarlock.
- So the oars pivot in the oarlocks wherever the rower sits. The shift moves the rower, not the oarlocks.
- Every 3 s of rowing the log prints the hands' fore/aft travel (boat Y) against the oarlocks and how far the grip had to move.

## Resting oars
- At rest (aboard, seated not rowing, or parked) the oars lie in the oarlocks. The shaft runs straight out through the oarlock, 30% inboard, blade 12 deg down and flat (the v0.6.0 rest pose).
- `rr "oar rest 30 12"` (% inboard, blade down deg) to tune.
- They are stowed in the boat only when you are 3000+ away and the boat is out of view (or 9000+ away). That change is never seen.
- Back aboard (on the deck or seated) they swing out of the stow onto the oarlocks over 0.8 s.
- Rowing still blends rest <-> the clip over 0.8 s.
