# Fix v1.4.3: "[E] Ride Horse" under the boat

The hidden carrier horse (220 under the boat) could be looked at from underwater; riding it broke the game.

Fix: the carrier gets the ExtraFlags kBlockActivate, kBlockPlayerActivate and kBlockActivateText (what AE's BlockActivation(true, true) sets). They are set at spawn, at the first tend after a load, and re-checked twice a second (saves made before v1.4.3).

Horse noise: the carrier's AI is off (frozen), so it should not play idle sounds. To check whether the sound comes from it: rr carrier off, then spawn a new boat (it draws itself, no horse). If the sound is gone, it's the carrier.
