# Part 56.51 - Docks (hand-placed boat spots)

Miguel places boat spots around Skyrim so players don't need `oas boat`. Buying a boat (with sleeping/fishing perks) comes later.

## How it works
- The mod has one boat (the parked boat). A dock "calls" it: when the player is within 2500 of a dock (cell loaded, not boating)
  and their boat is not already within 300 of the dock or within 3000 of the player, the boat is placed at the dock, facing
  the saved direction. A boat you left near you is never taken. Chest items move with it (existing chest carry-over).
- The same dock is not re-served until the player has gone 4000 away from it.
- Waits for the save's own parked boat to be resolved first (savedPark_).
- Height: water level at the spot + parked offset + boat height (stored Z only as a fallback), so height tuning still applies.

## Console
- `oas "dock add <name>"`  saves the parked boat's exact spot and facing (within 1500 of you). No boat near: places one ahead
  like `oas boat` and saves that. Workflow: `oas boat` (or row in and get off), line it up, then `dock add`.
- `oas "dock del"`  removes the nearest dock (within 2000).
- `oas "dock list"` / `oas "dock on"` / `oas "dock off"` / `oas "dock reload"`.

## File
`Data\SKSE\Plugins\OarsAndSails_Docks.ini`  ([Docks] Count/Enabled, [DockN] Name/World/X/Y/Z/Yaw). With MO2 new writes land in
the overwrite folder: move it into the mod (and the release zip).
Names are lowercase (the console line is lowercased).
