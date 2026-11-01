# v1.6.6: drop-back sliver, player feet, hang watchdog

## Rowing off from rest, the boat and passengers dropped back a little
- In seated draw mode 0 (OAS) the boat is drawn at last frame's pose, a whole frame of travel behind the rower. The passengers are pinned to the drawn boat (v1.6.4), so they dropped back with it.
- The fix: the boat is drawn `drawLead_` (0.7) of the way from last frame's pose to this frame's, so the drop-back is a sliver. The rotation is this frame's when the lead is 0.5 or more. A step over 60 isn't blended.
- `rr "lead N"` (0..1): 0 is OAS (a frame behind), 1 is this frame.

## Player feet still lifted
- `bHumanoidFootIKDisable` is now also set at the camera update (LatePlace, seated), right before the draw, on top of GlueSeat. The game's own player update may set it back in between.
- If the feet still lift, it's the row clip's legs, not IK. Tell Claude whether it happens while rowing or resting.

## The 19:42 freeze (no crash log, the game hung)
- RowRim.log just stopped mid-row at 19:42:34 with nothing unusual. Underwater lines had already hit their 12-line cap, so it can't show whether the camera had just gone under.
- Now each step of our per-frame work names itself (`Stage`), and a watchdog thread checks once a second that the player update still beats. After 3 s without one it writes "Watchdog: no player update for 3 s ... Last step of ours: X" to RowRim.log and flushes.
- A menu or loading screen also stops the beat (the line says so). "game player update" or "after ... (the game draws)" means the hang was in the game, not in our code.
