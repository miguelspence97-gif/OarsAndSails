# v1.7.46 - waterfalls block the boat (the fall is gone)

Asked: strip the waterfall code; just block the boat from going off a waterfall when one is detected.

- Removed: Launch, FallStep, airborne_, vz_, the fall timers and gravity, the bow-dip pitch (fallPitch_), and the
  "fall ahead" quiet time for the hull feelers (fallAhead_). Rocks at a lip stop the boat as anywhere else now.
- Kept: the detectors, DropAhead and CliffAhead (with the v1.7.45 open-water fix: a ledge needs ground level with the
  water at the lip), and the lower-water-at-the-tip check.
- New BlockAtLip: moving toward a lip, forward or backing (the probes look the way she is going), stops the boat
  (speed 0), knocks her back 2 and dips her a little on the first touch only, so she is held at the lip while you keep
  rowing and never shoved back every frame. Turn and row away freely.
- HUD "A drop ahead: the boat won't go over" and a log line "Lip: <which> ... blocked", at most every 4 s.
- The checks now run at any speed (the fall needed 40+ forward), so a slow creep can't slip over either.
