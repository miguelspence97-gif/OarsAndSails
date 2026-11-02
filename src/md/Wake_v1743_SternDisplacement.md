# v1.7.43 - the boat's own ripples (tiller and hull)

(First tried as foam patches at the stern; Miguel meant ripples like the oars make. The foam is gone.)

- TestHull::TendHullRipples, the game's water ripples (Fx::Ripple, as the oar blades):
  - the tiller (rudder, boat y -318): moving, one every SternSpread / speed s (0.07-0.4 s, 40% sooner while turning),
    size 0.03-0.07 (+0.015 turning); at rest one every 1.2-2.6 s, 0.018;
  - the hull: moving, the stem (the stern when backing) then each side pair at y 150 / 40 / -80 / -190 in turn just
    outside the hull (Plankton::HalfWidth + 3), one station every 1.6 x SternSpread / speed s, size 0.03-0.08
    (stem 1.2x); at rest a single small one (0.018) on a random side every 1.5-3.2 s, as she bobs.
  - none while beached (ground lift) or with no water.
- oas "num stern": Num8/2 the tiller aft / forward, Num4/6 ripples closer / further apart, Num9/3 up / down, Num7/1
  tiller ripples smaller / bigger, Num- / Num+ hull ripples smaller / bigger; on the HUD, saved to RowRim.ini [Wake]
  SternY=-318, SternSpread=22, SternZ=0, SternSize=1 (0 = no tiller ripples), SternStrength=1 (0 = no hull ripples).

## v1.7.44 - the hull's ripples, their own settings
- The tiller ripples stay as they were (oas "num stern": Num8/2 aft/forward, 4/6 closer/further apart, 9/3 up/down,
  7/1 smaller/bigger; [Wake] SternY/SternSpread/SternZ/SternSize). SternStrength is no longer used.
- The hull ripples (they looked bad) have their own: oas "num hull":
  Num7/1 smaller / bigger (0 = off), Num4/6 closer to / further from the hull side, Num8/2 fewer / more often,
  Num9/3 up / down, Num- / Num+ fewer / more spots a side (0-8, spread evenly from the bow thwart, y 150, to the aft
  thwart, y -110), Num0 the stem ripple on / off, Num. the at-rest ripples on / off. Shown on the HUD, saved to
  RowRim.ini [Wake] HullSize=0.6, HullOut=3, HullEvery=35, HullZ=0, HullCount=3, HullStem=1, HullRest=0.
- Each ripple lands up to 12 off its spot along the boat, so the pattern never repeats; moving, one spot fires every
  HullEvery / speed / spots seconds (both sides of a spot together, the stem on its own turn).
