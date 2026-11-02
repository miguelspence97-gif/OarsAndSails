# v1.7.52 - calm fish, a silent carrier, lantern, turning oarlocks, foot IK

1. Slaughterfish and mudcrabs (TendTameFish, every 0.5 s): while you are aboard and not swimming, every one within 4000
   (race editor ID, race name or base name has "slaughterfish" / "mudcrab") is put out of combat and its Aggression set
   to 0 (the old value kept in tamed_); whoever fights them on our side (the player, followers, the dog, crew) stops.
   In the water (swimming) or off the boat: their aggression back (UntameAll), they may attack. Also given back before
   every save, so it is never saved at 0. Log "Calm: ...".
2. The hidden horse's breathing: it is the creature awake loop (ExtraCreatureAwakeSound on the actor, and the movement
   loop, ExtraCreatureMovementSound), not in the process's sound handles that MuteCarrier already cleared and not an
   animation / footstep event (the v1 gates logged none). Both, and any ExtraSound, held at volume 0 every frame.
3. Bed lantern: (40, 134) -> (32, 150): clear of the bow thwart.
4. Oarlocks: scripts/clutter/oarlocks.py split the middle and aft forks out of L1_BoatDetails into RowRim_Oarlock_MidR
   / MidL / AftR / AftL, each a node at its pin (mid (+-93.3, 6.7, 68.7), aft (+-86.9, -92.9, 70.0)), own shader copy.
   TendOarlocks turns them 90 deg about the pin with shipW_ (the oars shipped while the bed is out), smoothly.
5. A foot lifting while standing aboard: foot IK followed invisible collision (the prompt activators, the hidden bed and
   supplies). bHumanoidFootIKDisable on while you are aboard, off when you step off (TendFootIK).
