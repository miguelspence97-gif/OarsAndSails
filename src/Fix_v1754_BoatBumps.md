# v1.7.54 - bumping the lake boats, crew, messages; the v1.7.53 deck collision undone

1. Reverted: the hull's collision switched off while seated (TendDeckCollision) is gone.
2. LakeBoats::BumpPlayerBoat (called each frame from the player's boat): both hulls are capsules (the middle line from
   250 astern to 250 ahead, radius 82). Overlapping: pushed apart half each; a boat closing on the other gets a small
   bounce back (speed x0.35 and at most 40 the other way along the contact, the lake boat slowed x0.3) and the player's
   hull dips (45 x strength). Never a launch.
3. Their reaction (Bump::Memory per boat): a bump counts only 3 s or more after the last contact (a knock touches many
   frames). 1st: HUD "<Fisherman>: Sorry!" (no vanilla voiced apology exists for this). 2nd: nothing said, they row
   away fast (Flee, at least 60% of the flee speed at once). 3rd and on (at most every 20 s): a vanilla voiced line from
   the ActorCollidewithActor topic ("watch it"), Say through Papyrus, and they row away. 90 s without a bump: forgotten.
   The stern hand speaks, else the rower. Log "Bump: ...".
4. No two of the same person in one lake boat: new boats re-pick a duplicate base; PlaceCrew re-picks one already in that
   boat (older saves).
5. Legacy: the "an old Oars and Sails boat was cleared away" HUD message removed (still logged).
6. At the stern with no rower: "No rower, move to rower seat or have crew" (console and the HUD note).
