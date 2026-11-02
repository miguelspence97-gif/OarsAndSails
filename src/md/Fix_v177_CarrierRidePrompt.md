# v1.7.7: no "Ride Horse" on the hidden carrier

Seen in Miguel's screenshot (21:16) on land by the beached boat: the carrier horse, 220 under the boat, still offered "E Ride Horse". The v1.4.3 ExtraFlags (kBlockActivate / kBlockPlayerActivate / kBlockActivateText) don't stop it.

The fix hooks two functions on the TESNPC vtable (every NPC base, so it filters on our carrier only, `TestHull::CarrierId()`):
- GetActivateText (0x4C) returns false (no prompt) when the ref is our carrier, or the crosshair is on it.
- Activate (0x37) returns false when the target is our carrier, so it can never be mounted.

The block flags stay as they were.

## Fishing Supplies (not a code issue)
`ccbgssse001-fish.esm` is in the Default profile's plugins.txt WITHOUT the `*`: Creation Club Fishing is not enabled. So RowRim logged "no Creation Club Fishing" and placed nothing. Tick it in MO2's plugin list.
