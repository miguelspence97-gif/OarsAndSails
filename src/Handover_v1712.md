# RowRim - shift handover (2026-11-01, v1.7.12)

RowRim = the new Oars and Sails (SKSE, CommonLibSSE-NG). Repo `E:\code\RowRim`, source in `src\` (Plugin.cpp + .inl files
included into it). Miguel builds with `build.bat` and pastes errors; nothing in v1.7.8-v1.7.12 has been compiled by the
assistant, so expect a compile-fix pass. Log: `Documents\My Games\Skyrim Special Edition\SKSE\RowRim.log`.
Per-change briefs are in `src\*.md` (newest: Release_v178_OAS, Prompts_v179_CrewRower, Fall_v1710_OldWaterfall,
Gear_v1711_Bedroll, Fix_v1712_CameraTdmDog).

## Working rules (Miguel)
- Short replies, apply fixes directly, batch changes into one build, write a .md brief into `src\` for each change.
- Keep CRLF line endings in source files. Don't make textures bigger without asking. He opens Blender himself.
- Push to GitHub only when packing a Nexus release.

## Build / deploy / release
- `build.bat`: builds, deploys to `E:\Modding2\MO2\mods\RowRim (Test Build)` (RowRim.dll + Data, xcopy /D) and to
  NEFARAM `E:\Modlists\NEFARAM\mods\OarsAndSails_v` in the RELEASE layout (dll copied as OarsAndSails.dll, Data without
  the RowRimRowing anim folder / RowRim.esl, the row clip in animations\OarsAndSails, release\OarsAndSails.esl).
- `pack_nexus.bat` -> `dist\RowRim_v1.7.12.zip`, made to install OVER Oars and Sails (MO2 merge/replace, Vortex update):
  OarsAndSails.dll (= RowRim.dll), OarsAndSails.esl, row clip under the OAS anim name/path (no Nemesis re-run needed),
  OAS GPMA files kept. OAS-named files come from `release\oas_overlay.zip`. README/TUNE_SHEET in `release\`.
- `release\OarsAndSails.esl` (stub, ESL): ACTI 0x800 Rowboat/Board, 0x801 Helm/Steer, 0x802 Oars/Row,
  0x803 OAS_Bedroll Bedroll/Unroll (was Chest/Open). Used for the look-at prompts. RowRim.esl is unused, not shipped.
- MO2 Default profile: OarsAndSails.esl ticked by hand; old OAS mod disabled. NEFARAM: only OarsAndSails_v enabled.
- Handheld still has old OAS: planned test = install the release zip over it and load an OAS save.

## What was done this shift
### v1.7.8 - the Oars and Sails update
- Console `oas` again (registered OarsAndSails/oas + RowRim/rr). If an OLD OarsAndSails.dll is also loaded (a different
  module than ours), RowRim takes `rr` only, warns with a message box, leaves old boats alone, prompts off.
- Legacy.inl: old OAS boats in saves cleared (FF ShipRowBoat01 hulls not ours, OarsAndSails.esl refs, OAS chest/bench/
  oars/lanterns/light within 800 of such a hull; seen on two 5 s scans); chest contents moved to the player.
- Docks ported (OAS 56.51/61): `RowRim_Docks.ini` (13 spots incl. Dawnstar), boat called to the nearest within 7000,
  kept if within 4000 of you, dock paint by name hash. `oas "dock add <name>|del|list|on|off|reload"`.
  Spawn() split into Spawn + SpawnAt.
- Plankton follows the current (Plankton.inl namespace Flow): water ACTIs with a linear velocity + named rapids/falls
  sources (steepest descent). Logged "Flow: ..." (first 24) - speeds unverified.
- Lake boats: default 3 a lake (old saved count >3 brought down once, [LakeBoats] Defaults=2). Crew pool: no ghosts /
  templated / spellcaster classes; robed crew stripped and re-dressed (fur/hide/farm or fishing clothes).
- RowRim.ini [Row] MaxSpeed/Glide/Turn (OAS keys, taken over from OarsAndSails.ini once), `oas "row turn N"`.
- Controller: mapped Activate button always works seated; R3+left stick / L3+right stick zoom ([Controls]).
- `oas help|status|dev|"reset settings"`. No MCM (needs quest + compiled script; not done).
### v1.7.9
- Look-at prompts via invisible culled activator proxies on a crosshair-only collision layer (OAS PickLayer):
  [E] Board Rowboat (hull, from water/shore -> you stand on the floorboards), [E] Row Oars (middle thwart),
  [E] Steer Helm (stern seat). TESObjectACTI vtable hooks: Activate 0x37, GetActivateText 0x4C.
- Crew rower: you at the stern + people aboard -> a random human (never the dog) moves to the middle thwart and rows
  (RowSim, row clip and oars follow them). `RowingActor()` in TestHull.
### v1.7.10 - old OAS waterfall (no pull / throw)
- Rowing forward over a lip (lower water at/after the bow, or a ledge with ground 80+ below) -> airborne: keeps speed,
  gravity 600, bow dips; lands in lower water (dip, -5..20% speed) or on ground (-60%); 8 s / 6000 safety reset.
  Hull feelers quiet with a fall ahead. Log "Fall: ...".
### v1.7.11 - bedroll + gear placing
- Gear.inl is a list: Fishing Supplies (CC Fishing 0x8D0) and Bedroll (vanilla FURN "Bed Roll", hay piles excluded).
- Roll spot under the bow thwart: [E] Unroll Bedroll / [E] Pack Up Bedroll (text from the 0x4C hook).
  [Clutter] Bedroll=1 keeps it out in every save; `oas "bedroll roll|unroll"`.
- `oas "num gear"`: numpad places items (Num5 next, 8/2 4/6 9/3, 7/1 turn), saved [Clutter] <Item>X/Y/Z/Yaw.
### v1.7.12
- Camera stuck level (NEFARAM): True Directional Movement fought the seat glue -> orbit wound up to 11000 rad. TDM API
  (TrueDirectionalMovementAPI.h) pauses its movement + headtracking while seated; orbit fix-up uses wrapped angles.
- Dog boarding: Meeko always allowed (OAS), CurrentFollowerFaction counts, any canine race, 3D not required; boarding
  logs nearby animals that didn't board and why.
- Gear: rocks with the drawn boat (Gear::LatePin from LatePlace); bedroll stowed while seated and re-laid on standing;
  items re-placed when left in another cell / 3D gone 1.5 s; unroll places at once (roll hidden only once the bed is drawn).
- CC Fishing lookup fixed: the file is ccBGSSSE001-Fish.esm and LookupForm by name is case sensitive -> found by
  case-insensitive scan of dh->files.

## Open / to check next
1. Compile v1.7.12 and fix errors (none of this was compiled here).
2. Test: prompts (wording of "Pack Up\nBedroll" format), follower rowing, waterfall, bedroll seated/stand cycle and cell
   crossings, fishing supplies appear (+ `oas "num gear"`), dog boarding (read "Crew: ... not aboard" lines), camera with
   TDM ("Compat: TDM paused while seated").
3. Handheld jitter while rowing: log shows the rower 6-17 units off the drawn boat (one frame of travel at lower FPS).
   Test `oas "draw player"` / `oas "draw pin"` on the handheld; make the winner default or switch automatically by FPS.
4. Release: install the zip over OAS on the handheld with an OAS save (old boats cleared, chest items, rowing without
   Nemesis, prompts, controller). Changelog: mention new HUD text ("takes the oars", "No rower...", bedroll prompts) for
   the Chinese ESL translator (Wenfan0608); 0x803 was renamed Chest -> Bedroll.
5. Later ideas: real textures for bucket/bedroll clutter, MCM Helper menu, boat buying/sleeping/fishing perks, sails,
   sounds (oarlock creak, lapping), storm rocking, find-my-boat.
