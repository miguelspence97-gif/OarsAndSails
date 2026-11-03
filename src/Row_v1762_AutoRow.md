# v1.7.62: auto-row

- The game's Auto-Move (C by default) toggles auto-row while seated: RowSim treats it as forward held.
- Recognised by the button event's user event, or (seated presses can come without it) by ControlMap::GetMappedKey(
  Auto-Move) for that device, read on every press, so a rebind in the game's settings is followed (Pad::IsAutoMove).
- Off when pressed again, when back is pressed (key or stick) or when you stand up. The game's own auto-move flag is
  cleared on each toggle so you don't walk off when you stand.
