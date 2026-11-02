# v1.7.41 - frozen after loading a save made while rowing (Nexus report)

- Report (gsvsleeperservice): quicksave while rowing, load: the character is frozen, WASD and E do nothing; oas
  unboat / boat / bring did not help; oas sit (then E) did.
- The save keeps the player's seated / row pose; after a load nothing is seated on our side, so nothing ever sends the
  stand-up. TestHull::TendLoadUnstick, 1.5 s and 3 s after a load: if the player is not seated with us, not in a real
  chair or bed (GetOccupiedFurniture), and his graph has bIsSitting / isInFurniture / bAnimationDriven or his sit state
  is not normal, he is stood up as StandUp does it (controls on, IdleChairExitStart / IdleFurnitureExit / sneakStop /
  IdleForceDefaultState, the sit variables cleared, sit state normal, collision on, the walking flags held 2 s).
  Log "Load: the player loaded still seated ... stood up".
- Workaround for 1.7.40 users: oas sit, then E.

## v1.7.42 (as old OAS did)
- Before a save is written (SKSE kSaveGame) the player is stood up if seated, so saves are never made in the seat.
- Opening the map while seated stands you up first (MenuOpenCloseEvent, MapMenu; done as a task on the main thread), so
  fast travel never starts from the seat. Passengers stay seated as usual and get up once you are far away.
- The v1.7.41 after-load stand-up stays as a safety for older saves.
