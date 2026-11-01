# v1.4.2: lake boat lantern on the stern spiral

Why it was off: OAS lake boats put the lantern's origin on the hook (the player boat hung it by the top of its model) and used the OAS vehicle frame. RowRim used a guessed (0,-300,98).

Hook: (0, -341, 140) in the RowRim hull frame. Measured from RowRim_Hull.nif: the stern spiral centre is y -339, z 141, radius about 10. That is the same spot as the OAS player hook (0,-91,28 from its vehicle point, hull origin at 0,250,-112).

Port of OAS TendLamp:
- The top of the lantern model (bound max z) sits on the hook; it hangs world-down, facing the boat heading.
- Pendulum swing from the hook's acceleration: 1.6 s period, damping 0.35, 12 deg max.
- The candle flame, fire, smoke, ember and particle nodes are switched off (OAS CullFlames); the log says how many.
- The light ref sits at the lantern centre and follows it.
- LatePlace re-applies the swung pose before the frame is drawn.
