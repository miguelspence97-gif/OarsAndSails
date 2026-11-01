# v1.7.31 - stuck in the rowing animation after E

- Crew rower: standing up only cleared rowAnimOn_; the clip was never stopped on the follower (only the player got
  release events), and Crew::g.rowerRowing stayed true so the glue never sent the chair sit: stuck rowing. StandUp now
  calls StopRowAnim on whoever has the clip and clears rowerRowing.
- E right as a stroke started (or mid stroke): the stop could land before the clip took over. After standing up, the
  stop (IdleForceDefaultState) is sent again 0.15, 0.5 and 1 s later to whoever rowed in the last second (TendUnstick);
  a passenger rower is sat straight back down (they stay seated while you walk about). Skipped if you sit down again.
  Log "Row anim: stop sent again to ... after standing up".
