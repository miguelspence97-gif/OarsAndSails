# v1.7.69: stray lamps swept

- Seen: lamp lights (and lanterns) glowing over lakes with no boat: refs whose boat or handle was lost (a save, a cell
  unload, a lake boat hidden while its lamp stayed).
- LakeBoats::SweepStrayLamps, every 5 s within 12000 of the player (called from TestHull::Update after Legacy::Tend):
  a game-made (FF) ref of the lamp light (0001CBB3) or lantern (0002D847) base, within 400 of the water surface, that no
  boat holds (any lake boat's lantern / light, the player boat's light) is disabled and deleted. Boats place theirs again
  when needed. Lamps on land (another mod's) are left alone.
