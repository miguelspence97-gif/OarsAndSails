# v1.7.59: swooshes seen

- v1.7.58's swoosh glows came from the shared sparkle pool (1500), always full with the wake and the digs: Take() handed
  the newest streak glows straight back out, so the streak never showed.
- Own pool now: kSwooshSharp 360 + kSwooshSoft 140 at the end of g.free (Take kind 3, Spawn(..., a_swoosh)). Sparkles
  size 3-4.5, soft 6-9 (about twice before), life 1.8-3 s. Swoosh uses EnsureRoot.
