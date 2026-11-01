# RowRim

The rowboat rebuilt as its own object: its own mesh, seats and rules (successor to the Oars and Sails rowboat).
Separate mod: own DLL (`RowRim.dll`), later its own ESL.

Current stage: **step 1, keyframed hull test** (`src/Step1_KeyframedHull.md`). Console: `rr` (`rr help`).

Log: `Documents\My Games\Skyrim Special Edition\SKSE\RowRim.log`

## Build
Shares CommonLibSSE-NG + vcpkg with `..\OarsAndSails` (keep both folders side by side), same presets.
`build.bat` builds `release-windows` and copies the DLL + PDB to `E:\Modding2\MO2\mods\RowRim (Test Build)\SKSE\Plugins`.

License: GPL-3.0-or-later (LICENSE.txt), built with CommonLibSSE-NG.
