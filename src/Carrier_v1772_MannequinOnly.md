# v1.7.72: mannequin only

- FindCarrierBase: Skyrim.esm's ManakinRace NPC only (a non-unique one if any, lowest form ID). The townsperson fallback
  is gone: never a person. A horse only if no mannequin is found at all. Logged "Carrier: mannequin base ...".
- With v1.7.71, a save's carrier that is not the mannequin (horse or person) is swapped for it on load.
