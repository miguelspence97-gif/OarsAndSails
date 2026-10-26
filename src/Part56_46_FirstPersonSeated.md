# Part 56.46 - First person stays seated (bug 3)

Bug: switching first/third person stood the player up and sat him down again; crew rower sometimes did the same.

Cause: HoldPlayerCrouch stood the player up in first person and used a sneak crouch (sneakStart) for free look.
Third person re-seated him. Followers copy the player's sneak, which broke the crew sit until our watcher re-sat them.

Fix:
- First person keeps the seated animation. No stand-up events, no sneakStart while seated.
- `MaskSeatForFreeLook`: while in first person, the engine sit state (`sitSleepState`) is cleared each frame so the game
  does not limit the look; the real value is put back when the view leaves first person and before any exit/get-up.
- Seat watcher now runs in both views (bIsSitting graph var counts as seated while the state is masked).
- Sneak crouch only remains as the fallback when the game refuses to seat the player (sitFailed_).
- ini `[Camera] FirstPersonFreeLook=1` (0 = seated with the game's normal look limit).

Log lines: "First person: seated, sit state N cleared ..." / "First person left: sit state put back (N)".

## 56.48 fix
Log showed bIsSitting mirrors the engine sit state, so while masked it read false and the watcher stood the player up every second
("PlayerSit: the seat was lost"). Now the watcher trusts the seat while first person masks it, and leaving first person puts the
sit state (and bIsSitting) back unconditionally.
