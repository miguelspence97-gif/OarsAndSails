# Fix v1.4.1: crash after load (deleted Cryomancer)

Crash: a deleted, cell-less Character ("Cryomancer") still had its movement job running on a worker thread, 31 s after launch.

Cause: after a load the lake boats purge every FF form id kept in the co-save (LKSP). A saved id can belong to a different ref after the load (the game reuses free FF ids), so we disabled and deleted a game NPC, and in the same frame, which is the OAS 51.1 crash.

Fix:
- The purge only removes our own parts: hull, lantern and light bases, or an actor named Fisherman/Fisherwoman. Anything else is skipped and logged ("purge skipped ... not one of ours").
- Purged refs are disabled now and deleted 2 s later.
- The player boat carrier horse is also disabled now and deleted 2 s later.
