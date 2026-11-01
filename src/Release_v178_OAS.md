# v1.7.8 - the Oars and Sails update (Nexus build) + docks, flow, lake crew, controller

## Console: oas again
- The command is `oas` (registered as OarsAndSails/oas); `rr` stays as a second name (two hijacked dev commands:
  TestLocalMap, TestSeenData...).
- If OarsAndSails.dll is still loaded, it owns `oas`: RowRim registers `rr` only, and shows a message box at load telling the
  player to uninstall the old version (Legacy::CheckConflict / OnGameLoaded).
- `oas help` = player commands, `oas dev` = the old test list, `oas status`, `oas "reset settings"` (row speed / glide /
  turning, height 0, blue, docks on, 3 lake boats). OAS words still work: boat, unboat, paintblue..., `num crew` / `num dog`
  (= oas num); the other OAS numpad modes print where the setting lives now.
- Every user-facing "rr ..." string now says "oas ...".

## Old OAS boats in saves (Legacy.inl)
- Release ships `OarsAndSails.esl` as an obsolete stub (same records, SNAM says obsolete): saves made with OAS still find
  their plugin. RowRim.esl is not shipped: nothing in the dll uses it.
- Every 5 s (from 8 s after a load), round the player (9000): FF refs from OarsAndSails.esl and FF ShipRowBoat01 hulls that
  are not ours (our hull, carrier, lake boat parts incl. the after-load purge list); the OAS chest (000F3923), benches
  (00041EC7), oars (00050E77), lanterns (0002D847) and light (0001CBB3) only within 800 of such a hull. Seen on two scans in a
  row -> disabled + deleted. A chest's contents go to the player first (each stack with its extra data). Notification +
  log line "Legacy: old Oars and Sails ...".
- Nothing is touched while OarsAndSails.dll is loaded.

## Docks (OAS 56.51 / 56.61 port)
- RowRim_Docks.ini (the 12 OAS spots) is now read: within 7000 of a dock (its cell loaded), not seated / aboard, and your
  boat is not within 300 of the dock or 4000 of you -> the boat is placed there (SpawnAt), facing the saved yaw, at
  water + kWaterZ + height, in the dock's own paint (hash of its name). 6 s wait after a load for the save's own boat.
- `oas "dock add <name>"` / del / list / on / off / reload. add takes the boat within 1500 (else places one ahead).
- Spawn() split: Spawn(bool) works out the spot, SpawnAt(at, yaw, ...) places it (used by the docks).

## Plankton follows the current (Plankton.inl, namespace Flow)
- Every 2 s (or 1500 moved) round the camera (8000): placed water (ACTI with a water type) whose water type has a linear
  velocity -> a volume (its OBND in the water's local frame, velocity turned by its 3D rotation; small values taken as
  m/s x 70); named rapids / waterfall / cascade meshes (not the foam / splash / mist effects) -> a source pointing down the
  steepest drop round it, 170 u/s (falls 260) fading out over 900 (1100).
- Each free glow's centre moves with the current every frame (shoreline ones at 35%). Log: "Flow: water ..." /
  "Flow: rapids ..." (first 24).

## Lake boats
- Default 3 boats a lake ([LakeBoats] Count). A saved count over 3 is brought down once ([LakeBoats] Defaults=2 marks it).
- Crew pool: no ghosts (ghost flag, or an ability with an effect shader), no templated bases (leveled templates were
  coming out as necromancers / spectres), no spellcaster / fighter classes, more names left out (apprentice, adept, priest,
  spectral, spirit, wraith ...).
- Robed townsfolk (robe / hood / cowl in their outfit) still row, but at the 2 s dress check the robes and hood are taken off
  and they get fishing clothes, or fur / hide (60%) / farm clothes and shoes. Fishing outfits now equip at once.

## Settings and controller
- RowRim.ini [Row] MaxSpeed / Glide / Turn (OAS keys). Read at the first update; with no [Row] yet, OarsAndSails.ini [Row]
  is taken over once. `oas "row speed|drag|turn N"` save. Turn is a scale on the turn rate (0.45 rad/s = 1.0).
- Controller (OAS B2 / B5 / 55.1): the gamepad button mapped to Activate works seated even without its user event; while
  seated, hold R3 + left stick (no rowing meanwhile, R3's view switch undone) or L3 + right stick to zoom.
  [Controls] PadZoom=1, PadZoomSpeed=1.2.
- No MCM in this build: everything is a console command or an ini key (TUNE_SHEET.txt). An MCM Helper menu needs a quest
  record in an esp and a compiled script; not done yet.

## Release files
- release\OarsAndSails.esl, release\README.txt, release\TUNE_SHEET.txt; pack_nexus.bat builds dist\RowRim_v1.7.8.zip
  (Data + the stub + docs + LICENSE, no RowRim.esl).
