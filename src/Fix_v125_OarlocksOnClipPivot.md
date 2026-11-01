# v1.2.5: middle oarlocks moved onto the row clip's oar pivot

- Measured in Blender (rowboat_v20_rowrimfloor.blend, RowRim hull placed as in game: rower root at the oar seat + the 15 rowing shift, facing aft):
  - The clip's oars (AnimObjectA/B) turn about boat model (+/-100..102, -0.3..2.3, 76.9).
  - RowRim's middle oarlock fittings were at Y -30, 31 toward the stern of that.
- RowRim_Hull.nif: the two middle oarlock fittings (base plate 20 verts + fork 64 verts per side, in L1_BoatDetails) moved +31 in Y, to Y 1. X and Z unchanged: the clip pivot sits about 6 higher and 7 further out, above the gunwale, so the forks stay on it.
- Code: lockLocal_ (93.3, 1, 70.9), was (93.3, -30, 70.9). The hand lock and the resting oars use it.
- The old mesh is kept as E:\code\RowRim\blender\RowRim_Hull_before_v125.nif.
- Note: the pivot was measured with `rr row shift 15`. A different shift moves the rower against these oarlocks by the same amount.

## v1.2.6
- Measured where the clip's oar shafts cross the fork line (x +/-93.3): Y 5.8..8.3 (mean 6.7), shaft underside Z 74.6..76.4 (mean 75).
- Forks and plates moved another 5.7 toward the bow (centre Y 6.7). The fork's upper part (above Z 69) is raised 4.6, so the cradle is at Z 75 under the shaft. The fork's foot stays on the plate, so the stem is just longer.
- lockLocal_ (93.3, 6.7, 76.5): the shaft centre, about 1.5 above the cradle.
