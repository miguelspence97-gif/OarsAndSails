# Part 56.54 - Rower camera spin

Log: on the rowing seat freeRotation.x drifted ~1.5 rad/s while rowing with no mouse input (heading constant): the game
turns the actor toward the camera and we reset the heading each frame, so the orbit spun.
HoldRowerOrbit: on the rowing seat in third person the orbit only changes with mouse / right-stick input (150 ms window);
any other change is undone and a disabled orbit is re-enabled at the held value. Replaces the 45-frame face hold.

## 56.55
The 56.54 lock let the spin through whenever mouse events arrived (log 06:34:44: orbit 3.2 -> 6.2 in 2 s). Real fix: the turn the
game gives the actor (heading +k, orbit -k) is measured against our last SetAngle and given back to the orbit before the
heading is reset (CancelGameTurn). HoldRowerOrbit now only re-enables an orbit the game switched off.

## 56.56 pulled
Still spun after 56.55. Face-the-rower is off for this release: [Camera] FaceRowerExperimental=1 brings it back (old FaceRower key
ignored). With it off nothing touches the third person orbit (CancelGameTurn / HoldRowerOrbit gated too). Redo later via the
camera hook instead of the free orbit.
