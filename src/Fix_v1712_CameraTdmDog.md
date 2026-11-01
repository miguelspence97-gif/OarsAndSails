# v1.7.12 - camera stuck looking level (TDM), dog not boarding, NEFARAM release-layout deploy

## Camera could only turn side to side (NEFARAM, after a while, helm and oars)
- Log: "Camera: orbit aboard" grew from 0.1 to 11000 rad in a few minutes, "game swing undone" 300-700 rad per 2 s.
- Cause (OAS 56.43, never ported): True Directional Movement turns the player toward the movement input and takes the turn
  back off ThirdPersonState::freeRotation; seated, our glue puts the heading back every frame, so TDM and we fight and the
  orbit winds up.
- Fix: TDM's modder API (TrueDirectionalMovementAPI.h, from OAS): directional movement and headtracking paused while
  seated, given back on standing up (TendModCompat from Update, on TDM's thread). Log "Compat: ...".
- The orbit fix-up now takes the difference as an angle (remainder 2 pi) and only removes that, keeping whatever winding
  the camera mod uses.

## Dog would not board
- No dog line in the log at all: it was never a candidate. Menagerie / pet mods use their own races and some follower
  frameworks keep animals out of the teammate flag.
- IsDog: any non-person race with dog / hound / husky / mastiff / canine / wolf in its editor ID or name.
- Followers: player teammate OR CurrentFollowerFaction (0005C84E).
- Boarding now logs every nearby animal follower or canine that did not board, and why ("Crew: ... not aboard: ...").

## build.bat
- NEFARAM now gets the release layout in mods\OarsAndSails_v: SKSE\Plugins\OarsAndSails.dll (= RowRim.dll), Data
  without the RowRimRowing folder / RowRim.esl, the row clip in animations\OarsAndSails, the OarsAndSails.esl stub.
