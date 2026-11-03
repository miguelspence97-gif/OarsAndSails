# v1.7.63: mannequin carrier

- A user's horse mod (a horse follower mod) conflicted with the hidden horse carrying the boat model.
- FindCarrierBase picks Skyrim.esm's mannequin NPC first (race ManakinRace, a non-unique one if any, lowest form ID):
  bald, no outfit, a race body / hair / outfit / physics / horse mods skip. Else a plain Skyrim.esm person (not unique,
  Nord/Imperial/Breton/Redguard, no template, no factions, else any faction); a horse only if neither is found.
  Cached and logged ("Carrier: person base").
- CalmCarrier: WaterBreathing 1, Aggression 0 (it sits 220 under the water, AI off, collision off, hidden, activation
  blocked as before).
- TendCarrier swaps a save's horse carrier (or a dead carrier) for a new one.
- Untested: check nothing of it shows and it is silent.
