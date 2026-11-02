# v1.5.8: crash in the water culling (11:02)

Crash: `SkyrimSE.exe+052A463 mov ecx,[rax+0xF8]` with rax 0, in the render cull of a TESWaterObject ("Water" under WaterLOD). It happened 91 s into the game, with the camera down at the water.

Cause: v1.5.1's `Underwater::Tend` set the water system's `playerUnderwater` / `partiallyUnderwater` / `underwaterHeight` itself. The water culling then took its underwater path, which reads state the game only sets up when it decides on its own that you're under water (null here).

Fix: Underwater::Tend is no longer called; it's left in Plankton.inl, marked unused. The third person camera goes back to the game's own underwater look (only just under the surface).

The deeper-underwater look needs another way that doesn't touch the water system, e.g. our own image space effect while the camera is under the water.
