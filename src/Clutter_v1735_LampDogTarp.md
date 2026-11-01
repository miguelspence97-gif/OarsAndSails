# v1.7.35 - lantern, dog, last tarp clips

1. Bed lantern moved off the bed: on the floorboards at the bow thwart's forward edge, starboard (40, 134, 40.4).
2. Tarp: the clearance took only hull pieces near the gunwale line; the thwart knees stand 15-20 inside it (the pale
   rectangles through the cloth in the 11:22 shot) and the bow rope runs up the stem (the dark spot near the tip). It now
   takes every hull, rail, knee, oarlock and bow rope surface above the floor (z > 45, |x| > 8). Check after the build:
   no cloth point within 0.8 of any of them. (blender/tent2.py)
3. Dog: the sit control stood the dog back up whenever it lay down (IdleStop on isIdleLay), so it never lay at its seat.
   Now it sits, and after 15-35 s sitting it lies down where it is (tries idleLayDownEnter / IdleLayDownEnter /
   idleLayDownStart / ... standing first if it must) and is left lying; if its graph has no lie-down idle it goes back to
   sitting (logged once). Log "Crew: the dog lies down ('<event>')".
