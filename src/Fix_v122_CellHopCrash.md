# v1.2.2: cell hop crash

Crash: access violation in LatePlace -> ReadBody, called from the third person camera update.

Cause: a cell hop moves the hull with MoveTo, which reloads its 3D and frees the old Havok body. body_ was only cleared on the next player update, but LatePlace (camera hook) ran in between and read the freed body.

Fix:
- CellHop clears body_ (needInit_, haveLastTarget_ too) as soon as the 3D changed.
- LatePlace only reads the body when it belongs to the hull's current 3D (Ref()->Get3D() == root_); otherwise it draws at the last pose.
