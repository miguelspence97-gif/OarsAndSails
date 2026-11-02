# v1.7.53 - ripples centred, deck collision while seated, oarlock texture

1. Tiller ripple: x 0 exactly (was +-2 at random); its fore/aft place (SternY) unchanged.
2. Hull ripple: the v1.7.44 side spots are off; one ripple the tiller's twin (same size and timing formula, scaled by
   SternSize, spread by SternSpread) dead centre under the rower's seat (rowSeat_.y), x 0.
3. TendDeckCollision: seated, the hull ref's 3D gets collision layer NonCollidable (the old layer kept; set again if a
   cell hop reloads the hull). Standing up: 0.6 s later, if your feet are more than 3 under the boards they are set on
   them first, then the old layer comes back. Log "Deck: ...". Note: anyone standing (not seated) in the boat while you
   sit has no floor then.
4. Oarlocks (scripts/clutter/oarlocks.py, rebuilt from the v1.7.49 hull): all six forks and the six base blocks leave
   L1_BoatDetails. Mid / aft forks: RowRim_Oarlock_MidR/MidL/AftR/AftL (turned by TendOarlocks); bow pair
   RowRim_Oarlock_FwdR/FwdL (still); bases one shape RowRim_OarlockBases. New UVs projected onto Miguel's forged iron
   sheet: fork faces onto the two U plates (prongs up), edges onto the side strips, bases onto the riveted bars.
   textures\RowRim\boat_oarlock_d.dds / _n.dds 512 DXT1 + mips (171 KB each; the sheet's black filled from the nearest
   iron so no black seams); matte.
