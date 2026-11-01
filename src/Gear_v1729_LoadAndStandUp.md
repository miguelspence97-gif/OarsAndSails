# v1.7.29 - supplies after a load, passengers get up sooner

1. Fishing supplies unseen after a load (prompt there, model only once the boat moved or you fished): they were placed
   in the same frame as the load (09:20:52.495) into a cell not fully in. Now the gear is first placed 3 s after a load
   (Gear::Forget sets retry 3), and a piece whose 3D is not in the scene (no parent node) for 1.5 s counts as having no
   3D and is placed again.
2. Passengers get up and follow at 400 from the boat (about 6 m, was 600). RowRim.ini [Crew] StandUpAt (written on first
   use; 150-5000). Log "Crew: you are N from the boat after ... they get up and follow".
