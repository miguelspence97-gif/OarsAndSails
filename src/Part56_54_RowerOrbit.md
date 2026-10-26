# Part 56.54 - Rower camera spin

Log: on the rowing seat freeRotation.x drifted ~1.5 rad/s while rowing with no mouse input (heading constant): the game
turns the actor toward the camera and we reset the heading each frame, so the orbit spun.
HoldRowerOrbit: on the rowing seat in third person the orbit only changes with mouse / right-stick input (150 ms window);
any other change is undone and a disabled orbit is re-enabled at the held value. Replaces the 45-frame face hold.
