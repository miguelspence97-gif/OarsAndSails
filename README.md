# Oars and Sails

Rowboat SKSE plugin for Skyrim Special Edition / Anniversary Edition: board, steer and row a boat with your followers aboard.

This repository is the full source code of `OarsAndSails.dll`. The mod download (plugin, ESL, meshes, textures, animations) is on Nexus Mods.

## License

Copyright (C) 2026 Miguel32.

Oars and Sails is free software licensed under the **GNU General Public License v3.0 or later** ([LICENSE](LICENSE)).
It comes with ABSOLUTELY NO WARRANTY.

Built with [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) (GPL-3.0-or-later WITH Modding Exception AND GPL-3.0 Linking Exception).

## Building

See [BUILDING.txt](BUILDING.txt). CommonLibSSE-NG and vcpkg are git submodules (`.gitmodules`):
CommonLibSSE-NG 12.1.0, commit `4ed481641f65e5b9a35376b90af85847e8870029`.

## Console commands

Numpad tuning is off until you pick a target: `oas "num speed"` (speed, glide, turning), `oas "num height"`,
`oas "num floor"`, `oas "num ripple"`, `oas "num stow"`, `oas "num crew"`, `oas "num dog"`, `oas "num npc"`, `oas "num off"`.

Direct values: `oas "row speed 280"`, `oas "row drag 0.25"`, `oas "row turn 0.45"`, `oas "boat height 5"`, `oas "floor 3"`,
`oas "ripple wake 0.05"`, `oas "ripple oar 0.05"`, `oas "paint blue|beige|vanilla"`, `oas "lakeboats off|on|<0-10>"`,
`oas "reset settings"`, `oas help`. Everything is saved in `Data/SKSE/Plugins/OarsAndSails.ini`.
