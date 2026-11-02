# v1.7.30 - E straight on the supplies

- v1.7.29 log: no "Pick: E on Fish" line before the bugged fishing: the crosshair was on the supplies model itself (the
  prompt activator is only there within 90), the game used them directly with no marker linked: turned to the floor, no
  cast, no way out. Right after a load the same happened (the prompt is not up yet).
- The ACTI Activate hook now catches the player's use of our supplies that is not our own (Gear::g_ourUse): within 90 and
  standing it goes through TestHull::OnPick(kPickSupplies) (open water check, marker, link, then our own use); otherwise
  it is refused. Their own prompt (GetActivateText) reads "Fish Fishing Supplies" only within 90 and standing, none
  otherwise.
