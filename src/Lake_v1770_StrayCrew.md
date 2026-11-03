# v1.7.70: stray crew swept

- Seen: NPCs standing in the water with no boat (lake boat crew left over: a save whose purge list missed them, or a
  boat that went while its crew stayed).
- LakeBoats::SweepStrayLamps (every 5 s) also checks loaded actors: game-made (FF), named Fisherman / Fisherwoman (the
  display name PlaceCrew gives our crew), in no lake boat's crew, not a player teammate, and swimming or more than 10
  under the water surface -> DeleteActorLater. Your own crew and followers are never touched.
