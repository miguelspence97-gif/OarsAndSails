# v1.0.0: the RowRim boat is the boat (OAS added onto it)

Miguel: RowRim exists for its boarding and unboarding (stand on the deck, E to sit, the carrier actor, cell crossing, the real moving collision). That all stays. The OAS port is there to add the rowing animation, the textures, and the boat movement with its fake collision.

## So
- `rr boat` (or `rr spawn`) places the **RowRim boat**: keyframed hull, carrier, its own seats, oars stowed and blending into the row clip. An OAS parked boat, if any, is removed. `rr unboat` removes it.
- `rr oasboat` still places the OAS boat, for comparison only.
- The v0.9.2 auto-removal of the RowRim hull from saves is gone.
- **Textures:** the OAS paint is applied to the RowRim model every frame (cached), so `rr paint blue|black|red|white|bluedesign|beige|vanilla` works on it. The oars get the OAS kwila wood.
- **Boat height:** `rr boat height N` / `rr num height` (OAS BoatHeight, default 5) set the RowRim hull's waterline offset (N - 5).
- **Water following (OAS):** 4×/s the water height under the hull is read. The hull eases to water + kWaterZ (-32) + offset, so it rides rivers and lakes at any height.
- **Fake collision (OAS SkimWaterAt / SkimSweep, bow/stern probes):**
  - **Shore:** a sample at the bow tip (or stern tip, backing) plus this frame's travel finds land at or above the water. The boat crawls the last bit (40, easing off) and stops with the tip at the shoreline; it can back off.
  - **Rocks, banks, docks:** three waterline rays (centre and ±50) from 40 inside the tip to 60 past it. A hit on statics, terrain, props or trees stops it dead, halves the turn and gives a small pitch knock. The boat's own hull, its carrier, actors and water never count.
  - **Unloaded cell ahead:** speed capped at 60 until it loads.
- Logs: `World: shore ahead/astern`, `World: bumped (layer N ...)`, `Hull: height`.

## Still from v0.6–0.8 on this hull
OAS row clip and stroke physics, oars (stowed ↔ hands, hand lock), idle rock, stroke squat, turn lean, weight tilt on the real collision.

## Not yet on the RowRim boat
Followers and crew seats, dogs, lamps, oar ripples, the dig sound, waterfalls and currents, ground tilt when beached.
