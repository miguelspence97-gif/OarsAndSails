# v1.7.26 - fishing: stay on the boat

- v1.7.25 log: the marker was right (z -14000 is this world's water: the boat sits at -14034 there), yet the player was
  dropped in the water and the Board prompt came back 2 s later (you were off the boat). The fishing moves the player to
  its own standing spot, worked out from the marker; with no shore there that is in the water by the hull.
- TestHull::TendFishWatch: for 4 s after E on the supplies, a jump of the player of 40+ in one frame that leaves the
  boat is put back where you stood on the deck (boat space at the E press), velocity zeroed; the facing the fishing gave
  you is kept. Logged "Fish: the fishing moved you N to (...) (boat space ...; marker (...))" so the offset can be read
  and the marker distance tuned if the cast needs it.
