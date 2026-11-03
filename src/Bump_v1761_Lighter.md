# v1.7.61: lighter boat bumps

- Was: apart half each, your speed cut to 35% and bounced back, the lake boat only stopped: felt like a wall.
- Now (LakeBoats::BumpPlayerBoat): overlap undone 80% by the lake boat, 20% yours. On a hit the lake boat gets a
  shove along the hit, 0.9 x the closing speed (max 260), kept in Boat::pushX/Y and glided off in the boat's move step
  (dies away at exp -0.9/s, stops at land), swung by the off-centre hit (turnVel, max 0.7 rad/s), and its own speed
  halved. Yours loses up to 35% of its way head-on (none glancing) and recoils 0.18 x closing (max 45) over about a
  second (exp -2/s). Voice lines and the dip as before.
