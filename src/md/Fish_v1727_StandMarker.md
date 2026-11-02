# v1.7.27 - fishing marker = where you stand

- v1.7.26 log: "the fishing moved you 282 to (-66085,92244,-14132) ... marker (-66085,92244,-14000)": the fishing MoveTo's
  the player onto the linked FishingMarker. It is the fisher's standing spot, not the cast spot (the CC markers are on
  shores and jetties next to the supplies). Put back on the deck, the rod stayed at the marker, hanging in the air,
  casting, and E to reel did nothing.
- Now the marker goes on the floorboards where you stand (boat x clamped to +-35 so never at the gunwale, deck height),
  facing straight out over the side you face; the fishing moves you onto it (a step at most), the rod is in your hands.
  Standing at the edge no longer drops you in the water. The 4 s watch puts you back on the marker if anything still
  moves you off the boat.
- Open water checked at 240 and 450 off that side (land, shallower than 40, or something in the way -> the HUD message).
- Supplies unseen after a load (prompt there, model not until the boat moved or you used it): placed during the load,
  their 3D came in later where the ref was first put. When an item's 3D arrives it is now set on its spot again
  (Gear::Item::had3D).
