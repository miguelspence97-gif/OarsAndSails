# v1.7.51 - two copies of RowRim loaded at once

Report (01:03): Surface Tides installed, went underwater, screen black, still black after reloading.

## What the logs show (01:00 run)
- skse64.log: both OarsAndSails.dll (name RowRim) and RowRim.dll (name RowRim) loaded: the Default profile has
  "Oars and Sails" and "RowRim (Test Build)" both enabled. Every RowRim line in RowRim.log is written twice; RowRim.dll
  took the other for the old Oars and Sails ("Legacy: OarsAndSails.dll is loaded as well", "Pick: no look-at prompts
  while OarsAndSails.dll is loaded"). Two copies run every hook twice, our underwater view (image space override, the
  underwater fog) included: the likely black screen, and an override set twice stays set across a reload.
- Community Shaders is disabled in the profile (Surface Tides blocks it); "Ever-Soul Weathers - Community Shaders" is
  still enabled. Surface Tides ran standalone, no errors; its simulation saw the player go under at 01:01:13.

## Fix
- SKSEPluginLoad: a copy loaded as RowRim.dll while an OarsAndSails.dll whose SKSE name is RowRim is loaded too does
  nothing (no log, no hooks, no serialization). The release copy always runs.
- Right away (no build): untick "RowRim (Test Build)" in the Default profile; build.bat deploys to "Oars and Sails".

## If it is still black with one copy
Untick Surface Tides and test the same spot. The Ever-Soul CS weathers expect Community Shaders.
