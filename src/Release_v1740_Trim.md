# v1.7.40 - trims for the release

- Meeko: the v1.7.39 log shows him boarding after "go home" with "teammate yes, follower rank -99": sent home he stays a
  player teammate but leaves CurrentFollowerFaction. Now a dog follows only while in that faction at rank 0+ (Meeko while
  following: rank 0); a person while a teammate or ranked 0+. The same test frees a dismissed one who is still seated
  (sitting back down no longer keeps him aboard).
- Fishing supplies: default scale 0.5 (was 0.55; RowRim.ini [Clutter] SuppliesScale still overrides).
- Tent off for this release (TendTent kTentOn = false; the tent stays in the nif, hidden). Its stowed tarp roll, pole
  bundle and lashings are out of the nif (blender/stow.py KIT = False). Kept: the bedroll slung under the bow thwart in
  two leather straps (moved to starboard), the lantern by the laid-out bed (lit half way into a 1.5 s fade with the bed),
  the shipped oars and the dog at the stern while the bed is out.
