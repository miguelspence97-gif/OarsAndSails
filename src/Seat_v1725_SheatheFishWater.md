# v1.7.25 - sheathe before sitting, fishing marker on real water

1. Sheathe first (TestHull::RequestSit / TendSitWait): Row Oars / Steer Helm (and E-to-sit without the prompts) with a
   weapon or spell out now call DrawWeaponMagicHands(false) and wait until the weapon state is kSheathed (2 s at most,
   cancelled if you leave the boat) before the sit is sent. v1.7.22 sheathed in the same frame as the sit animation, which
   cut the sheathe off and broke the third person camera. Log "Seat: putting the weapon away before sitting down".
2. Fishing marker height: the v1.7.24 log put it at z -14000 (the cell's water height there is the world's "no water"
   value; the boat's water comes from its own lookup). It now sits on the boat's water (waterZ_). The underwater teleport
   was the fishing going to that marker.
3. Open water check before fishing: off the side you face, 240 out:
   - land height above the water or less than 40 under it -> "land" / "too shallow";
   - a ray from 300 above down to the water hitting a static, prop, tree or terrain (not our boat) -> "something in the way".
   Then no fishing, and the HUD says: "No open water to fish off this side (...). Face the other side, or take the boat
   out further." Log "Pick: no fishing to the ... side".
