RowRim v1.7.40 - the Oars and Sails update
==========================================

RowRim is the new Oars and Sails: a rowboat you sit in and row, crew and passengers, NPC fishing boats on the lakes,
bioluminescent plankton at night, boat spots at the docks. Look at the boat for [E] Board / Row Oars / Steer Helm;
sit at the stern and a follower rows for you. Console command: oas (as before).

New in v1.7.40 (since v1.7.20)
  - Fishing from the boat: stand by the Fishing Supplies and press E ([E] Fish, close up only). You fish over the side
    you face, the rod in your hands; with land, shallows or a jetty off that side the HUD says so. Never from a seat.
  - The bedroll hangs under the bow thwart in leather straps; laid out it fades in, a lit lantern stands by it, the oars
    are laid along the gunwales and the dog moves to the stern. Sitting down rolls it up for the ride.
  - Passengers and the dog stay seated when you get up, until you walk about 6 m from the boat (RowRim.ini [Crew]
    StandUpAt). Dismissed followers (Meeko sent home too) never board again.
  - Weapons are sheathed and shields put away for the ride, yours and your passengers'; no [E] Talk while seated.
  - Soft collision: the boat eases to a stop at jetties, docks, longboats and boulders; small rocks just bump the keel.
  - A coopered bucket tipped over under the stern seat; a leather bedroll.
  - Fixes: the rowing animation no longer sticks after a quick E, the camera and hull no longer jerk while rowing,
    the fishing supplies and bed no longer fade, stray or vanish at cell edges and after loads, deleting the boat takes
    its gear with it, E in the boat no longer sits you down (only the Row / Steer prompts do) so you can pick things up.

Install
  Updating from Oars and Sails: install this over it (MO2: install, choose Replace; Vortex: update the mod), then run
  Nemesis or Pandora once. The plugin, the esl and the rowing animation keep the Oars and Sails file names, so they
  replace the old ones: nothing to tick or untick. Oars and Sails' bundled copy of Offset Movement Animation (the
  Nemesis_Engine\mod\gpma folder and GPMA*.hkx) is no longer included: it overwrote the real mod and hid it from Pandora.
  If you use Offset Movement Animation, install it on its own. Load your save:
    - old Oars and Sails boats near you are cleared away within a few seconds; anything in the old boat chest goes into
      your inventory (a notification says how many items)
    - your rowing speed, glide and turning from OarsAndSails.ini carry over
    - your boat waits at the 13 boat spots; the console command is still oas
  New install: install as a mod (MO2 / Vortex; manual: copy the CONTENTS of Data\ into your game's Data folder), run
  Nemesis or Pandora once, tick OarsAndSails.esl.

Needs
  - Skyrim SE / AE with SKSE64 for your game version
  - Address Library for SKSE Plugins
  - Nemesis Unlimited Behavior Engine or Pandora (run it after installing)
  - Optional: Creation Club Fishing (ccbgssse001-fish.esm) for the usable Fishing Supplies in the boat
  - No MCM needed: every setting is a console command or a line in RowRim.ini (see TUNE_SHEET.txt)

What's inside Data\
  SKSE\Plugins\OarsAndSails.dll        the plugin (RowRim; it keeps the old name so it replaces Oars and Sails)
  SKSE\Plugins\RowRim_Docks.ini        boat spots around Skyrim
  OarsAndSails.esl                     the Board / Row / Steer prompts; keeps old Oars and Sails saves loading
  meshes\RowRim\                       the hull (with its floorboards and clutter), plankton and wake meshes
  meshes\actors\character\...          the row animation (Nemesis / Pandora list, the Oars and Sails name and path)
  textures\RowRim\                     hull, paints, benches, floor, rope, plankton and wake textures

All console commands and settings: TUNE_SHEET.txt next to this file.

Log
  Documents\My Games\Skyrim Special Edition\SKSE\RowRim.log

License
  RowRim (RowRim.dll and its source) Copyright (C) 2026 Miguel32. GNU General Public License v3.0 or later: OarsAndSails LICENSE.txt.
  Built with CommonLibSSE-NG (GPL-3.0-or-later with Modding and Linking Exceptions).

