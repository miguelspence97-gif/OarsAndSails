# v1.7.47 - bedroll kept when packed up, darker fish, build reaches the game

Reports: unrolling sometimes showed no bed; packing up and unrolling again quickly could freeze the screen; the fish in
the tub too bright; builds never showed in game.

## Bedroll (Gear.inl, Plugin.cpp OnPick)
- Before: every unroll placed a new Bed Roll furniture ref (PlaceObjectAtMe) and every pack-up disabled and deleted it.
  Quick toggles piled up place/delete work (and the bed lantern with it); a fresh ref's 3D sometimes never came in.
  The 23:18 log ends 6 s after the second unroll in 9 s.
- Now the bed ref is kept: pack-up / stowed for the ride only Disable()s it (Item::parked), laying it out Enable()s the
  same ref, puts it on its spot and fades it in from that moment. Removed for real only with no boat, the boat deleted,
  its cell unloaded, or its 3D missing 1.5 s after being laid out (then placed fresh as before).
- E on the bedroll prompt within 1.6 s of the last toggle is ignored (Gear::BedToggleReady), so a toggle finishes its
  fade before the next. The console oas "bedroll roll|unroll" is not limited.
- BedrollShown / LatePin skip a put-away bed. Log: "Gear: Bedroll put away (...)", "Gear: Bedroll <id> laid out again".

## Fish texture
- fish_d.dds rebuilt darker (x0.58, slightly cooler): mean about 105 of 255, nothing near white. Same size (256x128 DXT1).
  scripts/clutter/fishbucket.py has the change.

## build.bat
- The Default MO2 profile only has the "Oars and Sails" mod (RowRim (Test Build) is not in its list), so builds never
  reached the game. build.bat now also deploys the release layout there (OarsAndSails.dll, Data, OarsAndSails.esl),
  as it does for NEFARAM.
