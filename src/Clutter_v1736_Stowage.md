# v1.7.36 - stowage, shipped oars, the dog at the stern

From the 11:44 shot: an oar blade and shaft through the tent (the shipped oars lay forward, into it).

1. Shipped oars (bed out): after building the pose, both oar ends are checked; if either is forward of y 40 the oar is
   turned end for end (Oar::ShipLocal a_flip), so the oars lie aft of the tent whichever way the oar model runs.
   Logged once: "Oars: shipped end for end".
2. Stowed (bed packed up), all under RowRim_Clutter_Bedroll so they hide together when the bed is laid out:
   - the bedroll roll moved 24 to starboard (x -12..60) under the bow thwart;
   - the tarp rolled (canvas, r 6.6, 44 long, spiral ends) beside it to port (x -59..-15);
   - both slung from the bow thwart (y 101-125, z 62.6-66.2) by flat leather straps, two each, round the roll and round
     the thwart (shape 38 rebuilt, its texture set 36 now boat_leather_d/n; the old rope ties are gone);
   - the tent poles: 7 sticks (r 1.1, ~110 long) bundled, three rope lashings, lying on the port side forward of the
     bow thwart (from (-40, 138) to (-24, 250)) on the hull bottom.
   New shapes RowRim_StowTarpRoll (canvas), RowRim_StowPoles (kwila), RowRim_StowLashings (rope). (blender/stow.py)
3. Spare oar: off the floor, flat against the port side under the thwarts, same fore-and-aft place: node 32 rotated so
   the blade stands up the hull side (tilted 22 deg with it), at x -73 (closest 1.3 to the hull along its length), z 50.5.
4. Dog: while the bed is out its seat is on the floorboards forward of the stern seat (0, -168, 43), facing aft
   (Crew::g.dogStern, set every frame from Gear::BedrollShown). It jumps there when the bed is laid out and back to the
   bow when it is packed up.
