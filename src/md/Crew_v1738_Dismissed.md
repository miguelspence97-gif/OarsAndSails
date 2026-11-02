# v1.7.38 - dismissed followers, the stray supplies

- Dismissed followers kept boarding (Meeko taken aboard after "go home", again after deleting and spawning the boat):
  a dismissed follower stays in CurrentFollowerFaction at rank -1, and IsFollower only asked IsInFaction. Now it takes
  rank 0 or more (or IsPlayerTeammate). This covers boarding when you sit, late arrivals hopping in, and the rower pick,
  for dogs and people alike. Anyone added with oas "crew add" is still taken (that list is yours).
- Dismissed while aboard (seated or waiting for you to walk off): on the next glue tick they are put on the floor by their
  seat, let go and dropped from the boat ("Crew: ... was dismissed, off the seat and let go").
- Deleting the boat left the fishing supplies hanging where the boat was: TestHull::Delete now calls Gear::RemoveAll
  (supplies, bed and the fishing marker).

## v1.7.39
- v1.7.38 log: Meeko still "boarded (bow (the dog's seat))" after being sent home. The follower faction check is gone:
  following is now the game's player-teammate flag only (dismissing clears it, for people and animals). The boarding line
  now logs "teammate yes/no, follower rank N, crew yes/no" so a stray boarding shows why.
