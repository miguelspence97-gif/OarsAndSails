# v1.7.9 - Board / Row / Steer prompts, a follower rows for you, Dawnstar spot

## Look-at prompts (OAS pick proxies)
- One ref shows one prompt, so each place has its own invisible activator from OarsAndSails.esl (the stub is now used):
  0x800 "Rowboat / Board" with the vanilla ShipRowBoat01 model at the hull ref (scale 1.04, a hair outside the hull, so the
  crosshair finds it first, also from under the water); 0x801 "Helm / Steer" and 0x802 "Oars / Row" with the OAS bench model
  (00041EC7) across the stern seat (70 long) and the middle thwart (130 long).
- Culled (never drawn), put on a collision layer the crosshair ray hits and the character controller does not (OAS PickLayer,
  logged "Pick: proxies on collision layer N"). There while not seated and within 1500 of the boat; Board only while off
  the boat. Placed again when the boat moves 20 / turns 0.1. TendPicks runs 10x a second.
- E on one: TESObjectACTI::Activate (vtable 37) hooked, for our three bases only: Board puts you standing on the floorboards
  (model 0, -40, deck + 6) facing the bow; Steer / Row sit you down (SitDown kHelm / kRower). InputSink's own E skips while
  the crosshair is on one of them.
- After a load the saved activators are removed once near the boat ("Pick: N prompt activator(s) from the save removed").
- Off while OarsAndSails.dll is loaded (it owns those records), and without OarsAndSails.esl (E in the boat as before).
- build.bat copies release\OarsAndSails.esl into both test mods: tick it, untick the old Oars and Sails mod.

## A follower rows for you
- Sitting at the stern with people aboard: one human rider (never the dog) is picked at random and moved to the middle thwart
  (slot 5, swapping with whoever sat there). HUD "<name> takes the oars". A new one is picked if they leave.
- RowSim runs for the crew rower (W/S/A/D), the row clip (OarsRowStart) goes to them, the oars follow their hands
  (TendOars uses RowingActor()). Crew::Glue sends no chair sit / idles to them while the clip plays; on stop they sit again.
- Sitting at the oars yourself: they ride as passengers. No rower: the old "No rower" note.

## Docks
- Dawnstar added (13 spots), cleaned up from the in-game write.
