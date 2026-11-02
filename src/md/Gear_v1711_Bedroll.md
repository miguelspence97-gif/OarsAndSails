# v1.7.11 - the bedroll and gear placing

- E on the bedroll rolled under the bow thwart: an invisible prompt there (OarsAndSails.esl 0x803, renamed in the stub:
  FULL "Bedroll", RNAM "Unroll", EDID OAS_Bedroll; the bench model sized 45 across, at model 0, 113.7, 50). It shows while
  the bedroll is rolled up and you are not seated.
- Unrolled: [Clutter] Bedroll=1 (for good, every save), the game's own bedroll furniture (a Skyrim.esm FURN named Bedroll
  with "bedroll" in its model path; logged "Gear: bedroll furniture ...") is placed and moved with the boat every frame like
  the fishing supplies (a separate ref, not part of the hull). The roll in the hull mesh (node RowRim_Clutter_Bedroll) is
  hidden. oas "bedroll roll" puts it away (and shows the roll again), oas "bedroll unroll" lays it out.
- Gear.inl is now a list of items (Supplies, Bedroll), each with its own spot / turn / scale in [Clutter] <Item>X/Y/Z/Yaw.
  Co-save GEAR v2 keeps both refs (v1 saves: the one supplies ref), removed after a load and placed again.
- oas "num gear": numpad places them (Num5 next item that is in the boat, 8/2 y, 4/6 x, 9/3 z at 15/s, 7/1 turn 45 deg/s),
  saved when a key is let go. The crew seat tuning is switched off meanwhile; oas num / "num off" ends it.
- Bedroll default spot: model 0, 30, 41 (the floorboards), turn 0: place it with the numpad.
