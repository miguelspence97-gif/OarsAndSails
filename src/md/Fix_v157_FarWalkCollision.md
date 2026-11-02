# v1.5.7: walking far off and back left the boat without collision

Log (10:55): `hull cell 00008E1B (detached)` while you were about 5000 away. Back at the boat you stood 40-58 under the deck ("stepped onto the boat local ... -55") and saw the carrier horse's "Ride" prompt under the hull.

**Cause:** the hull stayed filed in a cell that detached. Its collision left the physics world, and when the cell loaded again the hull kept its 3D, so the collision never came back. CellHop only moves the hull into your cell when you are in a different cell and within 3000. The status line kept showing "3D yes", so nothing noticed.

**Fix:**
- Every 0.5 s the hull's cell is checked. Detached means the hull is marked as "cell lost", and the hkpRigidBody is also checked for being out of the world.
- Either one, once you're back within 3000 of the boat with your cell loaded, re-homes the hull into your cell (MoveTo you, then back to its spot). That reloads its 3D and its collision, and the body is set up again.
- Log: `Hull: re-homed X -> Y (its cell had detached / its collision was out of the physics world), 3D ...`

**The "Ride" prompt:** it only showed because you were inside the hull with no collision between you and the carrier 220 below. With the deck solid again the crosshair hits the hull first.
