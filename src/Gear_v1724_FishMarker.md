# v1.7.24 - fishing from the boat: the cast marker

- Log (v1.7.23 test): "Gear: FF001133 activated by the player (ok)" each time, yet no cast, the view turned to the boat's
  side and the fishing could not be left.
- Read from ccbgssse001-fish.esm: all 70 placed Fishing Supplies carry an XLKR (no keyword) to a REFR of
  ccBGSSSE001_FishingMarker (STAT 0x853, FishingEffectMarker01.nif) out on the water, 100-250 from the supplies. The
  fishing casts at that marker; ours had no linked ref.
- Now: E on the supplies' prompt (standing only) puts a FishingMarker (load index of the file + 0x853) on the water 240
  off the boat's side you face, abreast of you (water height from WaterAt), facing away from the boat, and links it from
  the supplies (extraList.SetLinkedRef(marker, nullptr)) right before they are used. One marker, moved each time; an old
  one from a save is swept as a stray. Log "Gear: fishing marker ... linked from the supplies".
- Seated: no fishing prompt and no fishing at all; E only stands you up (v1.7.22/23 seated fishing removed).
