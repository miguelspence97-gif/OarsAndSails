# Part 56.49 - Snappier rowing

- Same speed with or without crew: solo boost off (kLightBoatFactor / kLightBoatDrag = 1.0 for now).
- Glide drag default 0.05 (was 0.25), tunable down to 0.01.
- Stroke pull no longer tied to drag: accel = top speed x kRowAccelRate (0.6), was top speed x drag (0.25).
  Faster pick-up from a stop; top speed is the cap.
- Old saved [Row] Glide is dropped once (ini [Row] Model=2 marks the new model); MaxSpeed is kept.
- Side effect: with 0.05 drag the boat coasts a long way after you stop rowing (S brakes).
