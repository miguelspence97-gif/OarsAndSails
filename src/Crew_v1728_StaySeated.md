# v1.7.28 - passengers stay seated, close-only prompts, fresh marker, bedroll 0.9

1. Passengers (followers, the dog, the crew rower) stay seated when you leave the oars or the helm, standing in the boat
   or off it, until you are 600 from the boat (flat distance), then they get up and follow (Crew::Linger). Was: up once
   the boat settled (1.2 s under 25) or after 8 s. Sitting down again keeps them seated. Safety: if the boat no longer
   updates them (fast travel, a door) and you are 1500 off, they get up from the player update (Crew::LingerSafety);
   deleting the boat stands them up too.
2. Prompts: the Fish (supplies) and Unroll / Pack Up (bedroll) activators are only placed while you stand within 90 of
   them across the deck (they showed from the game's full activate reach).
3. Fishing marker: a new one each time (the old one is deleted), instead of moving one marker about: moved across cells
   it kept its first cell, and the catch showed empty after moving spots.
4. Bedroll default scale 0.9 (RowRim.ini [Clutter] BedrollScale still overrides).
