# v1.7.50 - [E] prompts found elsewhere in the boat after rowing

Report: after rowing and stopping, the Fish, Row Oars and bedroll prompts sometimes sat somewhere else in the boat.

- v1.7.49 stopped deleting and re-placing the invisible prompt activators on every boat move and carried them along
  with SetPosition / SetAngle instead (steadier prompt while drifting). Carried a long way while rowing, a proxy's
  collision (what the crosshair hits) could be left behind.
- Now (TendPick): while the boat moves they are still carried; once it is still (|speed| < 5), a proxy carried more than
  30 from where it was placed, or whose 3D is more than 8 off its spot (PickStale), is deleted and placed fresh on its
  spot. Also checked once a second while still. PickProxy::placedAt / check.
- Also in this build: the camp bed darker (x0.52) with its sides shaded toward the outline (texture only, campbed.py).
