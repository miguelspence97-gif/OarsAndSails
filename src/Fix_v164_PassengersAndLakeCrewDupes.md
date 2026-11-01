# v1.6.4: passengers stay on the boat while rowing; no duplicate lake crew after a load

## Passengers displaced while rowing
- The glue (SetPosition in GlueSeat) left each rider's 3D where their own actor update put it. That's a frame of travel off the boat as it's drawn, so they slid by the rowing speed, worse on each stroke's surge.
- The fix, `Crew::LatePin`: in TestHull::LatePlace (the camera update, right after the boat model is placed where it is drawn), each rider's 3D root is moved onto their seat on that drawn boat.
- This is only the root's position (the same maths as PinRower). A gap over 60 is left alone.
- The player is unchanged: v1.6.1 glue, boat drawn one frame behind.

## Lake crew duplicated after every load (ScreenShot34)
- RowRim.log showed the cause: `purge skipped FF000DE3 'Courier', not one of ours` / `FF000DE7 'Electromancer'`.
- The crew's "Fisherman" display name isn't kept by the save, so after a load IsOurPart (base or name) said no. The old crew stayed, sitting where their purged boat had been, while the restored boat got new crew and rowed off.
- The fix: LKSP record version 2 saves each spawned ref with its base. After the load, a listed FF ref whose base is still the saved one is ours.
- Older saves (version 1, no bases): a listed FF actor whose base is in the townsfolk crew pool counts as ours, so the hovering crew in existing saves go too. The v1.4.1 guard against reused FF ids stays for everything else.

## v1.6.5: feet jumping up while seated
- Foot IK stayed on for seated actors. Its rays found the hull collision (the floor, raised in v1.6.3, and the thwarts) above or beside the sitting feet and lifted them, most of all for the rower.
- The fix: `bHumanoidFootIKDisable` is set every frame while seated, for the player (GlueSeat), the passengers (Crew::Glue) and the lake boat crew (TendCrew).
- It's cleared on standing up: the player's StandUp already did that, and the passengers are cleared in Crew::LetGo.
