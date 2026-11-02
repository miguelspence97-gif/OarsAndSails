# v1.7.56: bed [E] missing briefly after rowing

- The bed is parked (disabled) while rowing (v1.7.47) but was left where it was parked. Laid out right after rowing it
  was enabled far behind the boat, often in another cell: its 3D (and the "Sleep" prompt) came back only after it was
  moved, or after the lost-check removed and placed it again.
- Now (Gear::Tend): while parked, the bed ref is moved with the boat whenever it is more than 40 off its spot, and on
  lay-out it is set on its spot before Enable.
