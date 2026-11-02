# v1.7.23 - E on the hull, fishing supplies prompt

1. E standing in the boat no longer sits you down (TestHull::OnActivate). With the look-at prompts working (OarsAndSails.esl
   loaded), only [E] Row Oars / [E] Steer Helm seat you; E anywhere else is the game's own, so something dropped in the
   boat can be picked up or held and carried out again. Without the prompts, E sits you as before.
2. Fishing supplies: the log showed E never reached them (no "used from the seat" line): the crosshair does not find the
   supplies inside the hull. They now get their own prompt activator like the bedroll (pick kind kPickSupplies, base 0x803
   shared with the bedroll, told apart by ref id), a culled bench ~40 across on the pick layer, 6 above the supplies' spot,
   reading "[E] Fish Fishing Supplies". It is kept while seated too (the other prompts are dropped while seated).
   E on it standing: the supplies are activated by the player 0.05 s later (Gear::UseSuppliesSoon / TendUse).
   E on it seated: you stand up and they are activated 0.6 s later; a second sight of the same press is ignored.
   Log: "Pick: E on Fish (the fishing supplies)", "Gear: ... activated by the player (ok|refused)".
