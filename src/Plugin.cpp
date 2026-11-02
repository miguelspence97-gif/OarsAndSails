// RowRim - rowboat SKSE plugin for Skyrim Special Edition / Anniversary Edition
// Copyright (C) 2026 Miguel32
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details. You should have received a copy of the GNU General Public
// License along with this program (LICENSE.txt). If not, see <https://www.gnu.org/licenses/>.
//
// Built with CommonLibSSE-NG (GPL-3.0-or-later WITH Modding Exception AND GPL-3.0 Linking Exception).

// RowRim step 1: keyframed hull test (see src\Step1_KeyframedHull.md).
//
// Question: can a hull's collision move and tilt every frame with people standing on it, without them
// falling through or jittering? OAS keeps the collision level and fakes the tilt at the player's feet.
//
// Test hull: a NEW ref of the vanilla rowboat static (ShipRowBoat01) placed by us. Vanilla refs are never touched.
// Its Havok body is switched to keyframed, then each frame either:
//   drive: the body gets the velocity that carries it to the target pose by the next frame (a hard keyframe), so
//          the character controller sees a moving floor and carries whoever stands on it, or
//   warp:  the body is teleported to the target pose (no velocity), for comparison.
// Console: rr help (rr hijacks an unused dev command, not OAS's TestCode).

#include "PCH.h"
#include "TrueDirectionalMovementAPI.h"  // v1.7.12: OAS 56.43

#include <filesystem>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <format>
#include <fstream>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <xmmintrin.h>

// Defaults baked from the supplied RowRim.ini. Keep this before the .inl includes:
// their existing Win32 INI readers use these fallbacks too. A real INI value always
// wins; writes and in-game tuning are unchanged. Other INI files are unaffected.
namespace RowRimBakedIni
{
	struct Entry { const char* section; const char* key; const char* value; };
	inline constexpr Entry kDefaults[] = {
		{ "Longboat", "Handling", "2" },
		{ "Boat", "BakedTuneReset", "68" },
		{ "Rower", "PlayerRight", "-2" },
		{ "Rower", "PlayerForward", "247" },
		{ "Rower", "PlayerUp", "22" },
		{ "Rower", "PlayerYaw", "180" },
		{ "LakeBoats", "On", "1" },
		{ "LakeBoats", "Count", "3" },
		{ "LakeBoats", "View", "5" },
		{ "LakeBoats", "Defaults", "2" },
		{ "Plankton", "On", "1" },
		{ "Plankton", "Force", "0" },
		{ "Plankton", "Density", "3.00" },
		{ "Plankton", "Size", "0.40" },
		{ "Plankton", "Open", "600" },
		{ "Plankton", "OpenSize", "1.00" },
		{ "Plankton", "OpenCount", "2000" },
		{ "Plankton", "Shore", "400" },
		{ "CrewSeats", "Seat1", "-7.3 -3.8 -3.8 -175.0" },
		{ "CrewSeats", "Seat2", "9.8 -2.0 -4.3 -175.1" },
		{ "CrewSeats", "Seat3", "-8.1 -4.3 -3.3 -180.5" },
		{ "CrewSeats", "Seat4", "4.0 0.0 -2.8 -166.6" },
		{ "CrewSeats", "Seat0", "8.3 -7.1 -4.5 0.0" },
		{ "Controls", "PadZoom", "1" },
		{ "Controls", "PadZoomSpeed", "1.2" },
		{ "Row", "MaxSpeed", "300" },
		{ "Row", "Glide", "0.070" },
		{ "Row", "Turn", "1.00" },
		{ "Clutter", "Bedroll", "0" },
		{ "Clutter", "BedrollX", "1.5" },
		{ "Clutter", "BedrollY", "70.9" },
		{ "Clutter", "BedrollZ", "41.3" },
		{ "Clutter", "BedrollYaw", "-90.0" },
		{ "Clutter", "SuppliesX", "-49.3" },
		{ "Clutter", "SuppliesY", "-148.2" },
		{ "Clutter", "SuppliesZ", "40.3" },
		{ "Clutter", "SuppliesYaw", "-45.4" },
		{ "Camera", "BoatCam", "0" },
		{ "Debug", "CellGrid", "0" },
		{ "Crew", "StandUpAt", "400" },
		{ "Wake", "HullSize", "0.00" },
		{ "Wake", "HullOut", "31.1" },
		{ "Wake", "HullEvery", "107" },
		{ "Wake", "HullZ", "0.0" },
		{ "Wake", "HullCount", "3" },
		{ "Wake", "HullStem", "1" },
		{ "Wake", "HullRest", "0" },
		{ "Wake", "SternY", "-150.00" },
		{ "Wake", "SternSpread", "6.00" },
		{ "Wake", "SternZ", "0.00" },
		{ "Wake", "SternSize", "1.00" },
		{ "Wake", "SternStrength", "1.00" },
	};

	inline bool EqualI(const char* a, const char* b)
	{
		if (!a || !b) return false;
		while (*a && *b) {
			if (std::tolower(static_cast<unsigned char>(*a++)) !=
				std::tolower(static_cast<unsigned char>(*b++))) return false;
		}
		return *a == *b;
	}
	inline const char* Find(const char* section, const char* key, const char* path)
	{
		if (!section || !key || !path) return nullptr; // Preserve Win32 enumeration.
		const char* name = path;
		for (const char* p = path; *p; ++p) if (*p == '/' || *p == '\\') name = p + 1;
		if (!EqualI(name, "RowRim.ini")) return nullptr;
		for (const auto& e : kDefaults)
			if (EqualI(section, e.section) && EqualI(key, e.key)) return e.value;
		return nullptr;
	}
	inline DWORD WINAPI ReadString(LPCSTR section, LPCSTR key, LPCSTR fallback,
		LPSTR buffer, DWORD size, LPCSTR path)
	{
		const char* baked = Find(section, key, path);
		return ::GetPrivateProfileStringA(section, key, baked ? baked : fallback, buffer, size, path);
	}
	inline UINT WINAPI ReadInt(LPCSTR section, LPCSTR key, INT fallback, LPCSTR path)
	{
		const char* baked = Find(section, key, path);
		const INT value = baked ? static_cast<INT>(std::strtol(baked, nullptr, 10)) : fallback;
		return ::GetPrivateProfileIntA(section, key, value, path);
	}
}

// Translation-unit-local redirection, including the existing .inl modules.
#define GetPrivateProfileStringA RowRimBakedIni::ReadString
#define GetPrivateProfileIntA RowRimBakedIni::ReadInt

namespace
{
	constexpr const char* kVersionShort = "v1.7.56";
	constexpr const char* kVersion =
		"v1.7.56 (the bed's [E] at once after rowing: the put-away bed rides along with the boat, so laid out it is already on its spot); v1.7.55 (the first bump: a voiced vanilla \"Ow!\" / \"What was that for?\" (the Hit topic), no HUD text); v1.7.54 (your boat bumps the lake boats: pushed apart with a small recoil; their crew react the first time, row away fast the second, and blame you with a vanilla line the third (a bump counts at most every 3 s); never the same person twice in one boat; no HUD text when old Oars and Sails boats are cleared; at the stern without a rower: 'No rower, move to rower seat or have crew'); v1.7.53 (the tiller ripple dead centre; the hull ripple is the tiller's twin, dead centre under the rower; the oarlocks in their own forged iron texture); v1.7.52 (slaughterfish and mudcrabs leave you, your followers, the dog and the crew alone while you are aboard (not swimming); the hidden horse's breathing muted; the bed lantern further forward; the middle and aft oarlocks turn to cradle the shipped oars; foot IK off while standing aboard (a foot rose onto invisible collision)); v1.7.51 (two copies of RowRim installed at once (RowRim.dll and OarsAndSails.dll): the RowRim.dll copy does nothing, so nothing runs twice); v1.7.50 (the [E] prompts no longer end up elsewhere in the boat after rowing: once the boat is still, a prompt carried more than 30 from where it was placed or off its spot is placed fresh; the bed darker, its sides shaded); v1.7.49 (the bed is a patchwork fur and shearling camping bag (from Miguel's picture), fading in when laid out and out when packed up; the Fish prompt on the tub steady: the prompt proxies move with the boat instead of being deleted and placed again, a wider aim, reach 120); v1.7.48 (a leather and sheepskin sleeping bag in place of the game's bedroll mesh (\"Sleeping bag\" by Tactical_Beard, CC BY 4.0, retextured): drawn on the bed's spot while it is laid out, the bedroll furniture kept for sleeping); v1.7.47 (the bedroll is kept when packed up (disabled, enabled again when laid out) instead of a new bed placed and deleted each time: it no longer goes missing, and E on it again within 1.6 s is ignored (quick repeats could hang the game); the fish in the tub darker); v1.7.46 (no more going over waterfalls and ledges: the boat stops at the lip (forward or backing) with a HUD note, the v1.7.10 fall removed; a ledge needs ground level with the water at the lip (no false stops on open water); a tub of fish baked into the floorboards, port side aft, in place of the Fishing Supplies: their own mesh hidden, the supplies kept on the tub so E on it still uses them (no numpad placing for them)); v1.7.44 (the hull's ripples have their own settings, oas \"num hull\": size, how far out, how often, height, how many spots a side, the stem and the at-rest ripples on or off; oas \"num stern\" is the tiller only); v1.7.43 (the boat makes its own ripples in the water as the oars do: at the tiller and along the hull, often while moving or turning, now and then at rest; oas \"num stern\" to tune them); v1.7.42 (stood up before the game saves and when the map opens, as Oars and Sails did: no saves or fast travel from the seat); v1.7.41 (a save made while rowing no longer loads you frozen in the seat: you are stood up after the load); v1.7.40 (dismissed dogs (Meeko sent home stays your teammate in the game) never board or stay seated: a dog must be in the follower faction; the fishing supplies at 0.5; the tent and its stowed tarp and poles off for this release, the bedroll slung in leather and the lantern by the bed stay); v1.7.39 (only followers the game counts as your teammates board: dismissed Meeko stays off the boat); v1.7.38 (dismissed followers (Meeko too) are never taken aboard again, and one dismissed while aboard gets off the seat; deleting the boat takes the fishing supplies and the bed with it); v1.7.37 (the tent has its own textures (its fade had faded the rod, the stowed poles and the stowed tarp, which share them, to nothing); the tent draws solid once up, kept 4 clear of the wood; the spare oar removed); v1.7.36 (stowed: the tarp rolled and the bedroll (moved to starboard) slung under the bow thwart in leather straps, the tent poles bundled with rope along the port side forward, the spare oar flat against the port side under the thwarts; with the bed out the shipped oars lie aft of the tent (one lay through it) and the dog sits at the stern facing aft); v1.7.35 (the bed lantern under the bow thwart, clear of the bed; the dog lies down at its seat after a while (it was stood back up whenever it lay down); the tarp over the thwart knees and the bow rope too); v1.7.34 (the bed tent redone: a square way in on two posts up the hull sides and a crossbar (nothing over the bed), grey canvas over eave and ridge poles to the stem head, smooth over the gunwales and oarlocks; it fades in and out over 1.5 s with the bed, the bed too; a lit lantern on the floor by the bed"
		"); v1.7.33 (with the bed laid out the oars lie along the gunwales in the middle and aft oarlocks; the tarp goes over the gunwales, rails and oarlocks); v1.7.32 (a canvas tarp tent over the bow while the bedroll is laid out: from the stem head aft over the bed on a ridge pole and two A-frames of skinny poles, no collision); v1.7.31 (standing up from the oars or the helm stops the row animation on the crew rower too (they were left rowing), and sends the stop again over the next second (E right as a stroke started could leave it playing); v1.7.30 (E straight on the fishing supplies goes through the boat's fishing too (marker, open water), never the bare one that turned you to the floor and left you stuck fishing; their prompt only close and standing); v1.7.29 (passengers get up and follow at 400 from the boat (about 6 m; RowRim.ini [Crew] StandUpAt); the fishing supplies placed 3 s after a load, and placed again if their 3D is not in the scene (unseen after a load); v1.7.28 (followers and the dog stay seated when you leave the oars or the helm, also when you step off the boat, until you walk 600 away, then get up and follow; the fishing and bedroll prompts only show up close (90); a fresh fishing marker each time (catches not shown after moving spots); the bedroll at 0.9); v1.7.27 (fishing: the marker is the fisher's standing spot (the fishing moves you onto it): on the floorboards where you stand, facing out over the side, so the rod is in your hands and reeling works; open water checked out to 450; the fishing supplies drawn right after a load); v1.7.26 (fishing from the boat: the fishing's own move of the player (into the water beside the boat) is undone, you stay where you stood on the deck); v1.7.25 (a drawn weapon or spell is put away before you sit at the oars or the helm, the sit waits for it (the camera broke when sitting with it out); the fishing marker on the boat's water, not 14000 down (you were taken underwater), and only on open water: land, shallows or something in the way off that side say so on the HUD); v1.7.24 (fishing from the boat: the supplies are linked to a fishing marker placed on the water off the side you face, as the Creation Club ones are (the cast went nowhere and left you stuck fishing); no fishing prompt or fishing while seated, only standing); v1.7.23 (E standing in the boat no longer sits you down: only the seat prompts do (pick up what you dropped in the boat again); the fishing supplies have their own prompt (Fish), also from the seat (you stand up and fish)); v1.7.22 (seated: your weapon sheathed and your shield taken off (back on when you stand), passengers sheathe theirs (still equipped) and take shields off too; no [E] Talk while seated and no dialogue opened by the E that sits you down or stands you up; E on the fishing supplies from the seat stands you up and starts fishing); v1.7.21 (soft collision: the bow eases to a stop at jetties, docks, longboats, wood and boulders (0.25-0.8 s, begun early enough), small rocks only bump the keel; the fishing supplies no longer fade at cell edges; passengers and the dog stay seated after you stand up until the boat settles, then stand clear of the hull (no dog thrown); fishing supplies found by the plugin's load index; the bedroll no longer respawned every frame at a cell edge; sitting down rolls the bedroll up for good (no more laying itself out when you stand or leave the boat); saved and stray beds removed after a load even with no boat out, strays over the water or by the boat swept for 30 s); v1.7.20 (oas \"grid on|off\": faint see-through walls on the world cell edges round you, your own cell brightest, the cell shown on the HUD as you cross); v1.7.19 (the rower guard never takes an engine late frame as a real move (the jump at row stop); the boat camera tried and removed"
		"); v1.7.18 (the rower steadied on the boat before the game builds the third person camera from him, so the camera no longer shakes when the engine leaves him a frame behind; motion log reads the camera as drawn); v1.7.17 (the third person camera steadied where the game really reads it (GetTranslation): no shake when the engine leaves the rower a frame behind while rowing, no pull-in on cell hops; the rower takes row start/stop at once); v1.7.16 (motion log: the rower's pelvis, head and Camera3rd bone on the drawn boat, kinks flagged BONE); v1.7.15 (the hull's collision model, reloaded on a cell hop, hidden right before the draw too; motion log checks the drawn camera); v1.7.14 (seat guard: the rower and the third person camera held steady on the drawn boat: no 1-unit hiccup every ~1.7 s, no camera pull-in on cell hops; oas \"seatguard on|off\"); v1.7.13 (oas \"motionlog [seconds]\": a frame-by-frame log of the boat and the third person camera in RowRim_Motion.log); v1.7.12 (True Directional Movement paused while seated (its turn-toward-input wound the third person camera up until it could no longer look up or down); the orbit fix-up compares angles, never hundreds of radians; dog followers from any canine race and every current follower board); v1.7.11 (E on the bedroll rolled under the bow thwart (Unroll Bedroll) lays the game's own bedroll in the boat to sleep in, for good (oas \"bedroll roll\" to put it away); oas \"num gear\": the numpad places the bedroll and the fishing supplies (Num5 picks which), saved in [Clutter]); v1.7.10 (waterfalls and ledges as in the old Oars and Sails: row over a lip and the boat flies on with its speed, the bow dipping, and lands in the water below with a slap and a dip, or on the ground; no pull or throw; the hull feelers stay quiet with a fall ahead); v1.7.9 (look-at prompts: [E] Board Rowboat on the hull from the water, under it or beside it (you land standing on the floorboards), [E] Steer Helm on the stern seat, [E] Row Oars on the middle thwart, through invisible activators from OarsAndSails.esl; with followers aboard and you at the stern, one of them (never the dog) is picked at random to row: W/S/A/D steer them, the oars follow their hands; a 13th boat spot at Dawnstar); v1.7.8 (the Oars and Sails update: console command oas again (rr still works); boat spots: your boat waits at the docks in RowRim_Docks.ini, oas \"dock add <name>\"; old Oars and Sails boats in a save are cleared away (their chest's contents given to you) and OarsAndSails.esl stays as an obsolete stub; plankton rides the current in rivers and rapids; 3 NPC boats a lake by default; no ghosts, necromancers or templated NPCs rowing the lake boats, robed ones change into plain, fur or hide clothes; rowing speed, glide and turning saved in RowRim.ini [Row] (taken over from OarsAndSails.ini once), oas \"row turn N\"; controller: the mapped Activate button always works seated, R3 + left stick or L3 + right stick zooms ([Controls]); oas help, status, \"reset settings\"); v1.7.7 (no more \"Ride Horse\" on the hidden carrier: its activate text and activation refused through the horse base's vtable; clutter meshes fixed (each piece its own shader property), placeholder textures, keg out, rod in a holder on the hull side, fish bones sprite in the hull); v1.7.0 (boat clutter: a bucket, a spare oar under the thwarts, an ale keg under the aft thwart, a bedroll tied under the bow thwart, a fishing rod trolling off the stern, fish bones by the rope; usable Fishing Supplies (Creation Club Fishing) left of the stern seat, [Clutter]); v1.6.6 (rowing off from rest the boat and passengers drop back only a sliver (drawn 0.7 of the way to this frame, rr \"lead N\"); the player's foot IK held off at the camera update too; a hang watchdog writes our last step to the log); v1.6.5 (seated feet no longer jump up: foot IK off while seated for the player, passengers and lake boat crew"
		"); v1.6.4 (passengers drawn on their seats on the boat as drawn (no more sliding off it while rowing); lake boat crew from a save removed after the load (their Fisherman name is not saved, so they were kept and sat on the water while new crew rowed off): the co-save keeps each part with its base); v1.6.3 (crew sit only when not seated (OAS: seated state from their animation events), no more flicker and jumps; standing floor raised 2.5 onto the floorboards; crew seats 5 higher, feet on the boards); v1.6.2 (one dog only, in the OAS dog seat in the bow, facing forward, leaning with the boat, OAS sit control); v1.6.1 (seated player and crew moved with the boat as OAS did (glued each frame, the boat drawn one frame behind), no longer pinned onto the hull; rr \"draw pin\" for the old way); v1.6.0 (crew and passengers from OAS: followers and rr \"crew add\" NPCs board with you on the thwarts (dogs on the floor), late arrivals hop in, rr crew remove|clear|list|summon; rr num: numpad seat tuning, Num5 picks who); v1.5.19 (the stern trail stronger and paler, easy to see again); v1.5.18 (wake: the V is off for now; the stern trail as wide as the hull from the start); v1.5.17 (wake: the V a faint grey-green sheen, the stern trail round foam patches each turned and sized its own way so it never repeats); v1.5.16 (the boat lays a real V wake: two arms off the bow at the Kelvin angle past both sides and a churned wash behind the stern, meshes on the water, fading out as far as the plankton wake; [Wake] On); v1.5.15 (underwater the view blues out by about 30 m: our own distance fog over the sky's while the camera is under, plankton fading with it; [Underwater] FogNear / FogFar); v1.5.14 (our own underwater filter for the camera: a clear cool teal with easier bloom so the plankton shows, through the image space override, never the water system; rr underwater on|off); v1.5.13 (oar plankton swirls wider, spreading out and fading sooner; far more glows stuck to the hull and oar blades, bigger, kept glowing with the oars out of the water); v1.5.12 (open-water plankton out to 7000 (was 3600), 1800 of them by default with the wake's fine scatter mixed in; ini key now OpenCount); v1.5.11 (plankton like the concept: finer and far denser sparkles, a wake hugging both sides that opens into a V behind, oar swirls that linger, lake boats light their own wake and oar swirls); v1.5.10 (shoreline plankton scattered in loose patches that hug and curl with the shore, no more single-file rows; open water no longer gets shore spots at sheer drops); v1.5.9 (shoreline plankton is its own pool, apart from the open water: single-file lines along the water's edge and round rocks, jetties and moored boats, washing in and out and creeping along; rr \"plankton shore N\"); v1.5.8 (the v1.5.1 underwater camera fix is off: forcing the water system underwater crashed its culling); v1.5.7 (walking far off and back no longer leaves the boat without collision: the hull is re-homed into your cell once you are back within 3000); v1.5.6 (open-water plankton thick along shorelines and round rocks, jetties and moored boats; anyone wading or swimming stirs it: bursts round them, nearby glows flare and are pushed aside); v1.5.5 (open-water plankton has its own count and size: rr \"plankton open N\", rr \"plankton opensize N\"); v1.5.4 (open-water plankton fixed (dropped the frame it was made), 600 of them at density 3; density 3 and size 0.5 by default, density up to 6, bigger pools); v1.5.3 (plankton never drawn through the hull; the open water all round has its own plankton out to 3600, fading in, drifting and fading out, farther ones drawn bigger); v1.5.2 (plankton: diffuse, slower glows under the surface next to the sparkles on it, oar swirls chaotic: two or three eddies, own spin speeds, some against the swirl, churn, some flung out"
		"); v1.5.1 (third person camera keeps the underwater look however deep it goes; plankton drawn on the surface when seen from above, bigger, scattered over the whole hull waterline, the oar blade ones fixed); v1.5.0 (bioluminescent plankton at dusk and night: oar catches stir spiralling, sinking clusters, the hull lights a wake, a few glows cling to the hull and the oar blades; rr plankton); v1.4.4 (player boat lantern on the stern spiral, swinging, lit at night; thwarts moved 6.5 toward the bow and all oarlocks matched to the middle pair); v1.4.3 (hidden carrier horse can no longer be ridden or show a prompt); v1.4.2 (lake boat lanterns hang from the stern spiral and swing like the OAS player lantern, candle flame off, light kept); v1.4.1 (after-load purge only removes our own lake boat parts, actors deleted 2 s after disable); v1.4.0 (OAS soft physics: beach glide, ground lift and tilt, bank governor, ice, rocks, 12 feelers; lighter standing weight fore and aft; oar seat where the clip sits; no rowing from the stern; lake rowers stay seated between rows)";  // v1.7.51: in pieces (MSVC: one string literal at most 16 KB, C2026)
	constexpr float kPi = 3.14159265f;
	constexpr RE::FormID kRowBoatStatic = 0x00050368;  // ShipRowBoat01 (Skyrim.esm), same base OAS reads
	// v0.5.0/v0.5.1: RowRim_Hull.nif raises the hull's own floor to model z 38 (the lake surface is at 32), so where people
	// stand is 8 higher than on the vanilla collision. The (vanilla) collision is lifted kCollisionLift to match, seats with it.
	// v1.6.3: 10.5: the floorboards (v1.4.x, top 40.3) are 2.3 above the floor the 8 was set for: feet stood inside them
	constexpr float kCollisionLift = 10.5f;
	constexpr float kDeckZ = 31.0f + kCollisionLift;   // standing height in the boat model
	constexpr float kWaterZ = -32.0f;                  // boat origin this far from the water (OAS kParkedBoatZ)

	// ------------------------------------------------------------------
	// Small maths
	// ------------------------------------------------------------------
	RE::NiMatrix3 MakeMat(float a00, float a01, float a02, float a10, float a11, float a12, float a20, float a21, float a22)
	{
		RE::NiMatrix3 m;
		m.entry[0][0] = a00; m.entry[0][1] = a01; m.entry[0][2] = a02;
		m.entry[1][0] = a10; m.entry[1][1] = a11; m.entry[1][2] = a12;
		m.entry[2][0] = a20; m.entry[2][1] = a21; m.entry[2][2] = a22;
		return m;
	}
	// Same conventions as OAS BoatBow (forward = local +Y, yaw clockwise from north).
	RE::NiMatrix3 RzM(float a_h) { const float c = std::cos(a_h), s = std::sin(a_h); return MakeMat(c, s, 0.0f, -s, c, 0.0f, 0.0f, 0.0f, 1.0f); }
	RE::NiMatrix3 RxM(float a_p) { const float c = std::cos(a_p), s = std::sin(a_p); return MakeMat(1.0f, 0.0f, 0.0f, 0.0f, c, -s, 0.0f, s, c); }
	RE::NiMatrix3 RyM(float a_r) { const float c = std::cos(a_r), s = std::sin(a_r); return MakeMat(c, 0.0f, s, 0.0f, 1.0f, 0.0f, -s, 0.0f, c); }

	struct Quat
	{
		float x{ 0.0f }, y{ 0.0f }, z{ 0.0f }, w{ 1.0f };
	};

	Quat Mul(const Quat& a, const Quat& b)
	{
		return Quat{
			a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
			a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
			a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
			a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
		};
	}
	Quat Conj(const Quat& a) { return Quat{ -a.x, -a.y, -a.z, a.w }; }
	float Dot(const Quat& a, const Quat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
	Quat Norm(const Quat& a)
	{
		const float n = std::sqrt(Dot(a, a));
		return n > 1e-6f ? Quat{ a.x / n, a.y / n, a.z / n, a.w / n } : Quat{};
	}

	// v' = M v, M.entry[row][col]
	Quat FromMatrix(const RE::NiMatrix3& m)
	{
		const float m00 = m.entry[0][0], m01 = m.entry[0][1], m02 = m.entry[0][2];
		const float m10 = m.entry[1][0], m11 = m.entry[1][1], m12 = m.entry[1][2];
		const float m20 = m.entry[2][0], m21 = m.entry[2][1], m22 = m.entry[2][2];
		Quat q;
		const float tr = m00 + m11 + m22;
		if (tr > 0.0f) {
			const float s = std::sqrt(tr + 1.0f) * 2.0f;
			q = { (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s };
		} else if (m00 > m11 && m00 > m22) {
			const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
			q = { 0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s };
		} else if (m11 > m22) {
			const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
			q = { (m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s };
		} else {
			const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
			q = { (m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s };
		}
		return Norm(q);
	}

	// v' = M v (inverse of FromMatrix)
	RE::NiMatrix3 ToMatrix(const Quat& q)
	{
		const float x = q.x, y = q.y, z = q.z, w = q.w;
		return MakeMat(1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
			2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
			2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y));
	}

	RE::NiPoint3 Rotate(const Quat& q, const RE::NiPoint3& v)
	{
		const Quat p{ v.x, v.y, v.z, 0.0f };
		const Quat r = Mul(Mul(q, p), Conj(q));
		return { r.x, r.y, r.z };
	}

	void Get4(const RE::hkVector4& a_v, float (&a_out)[4]) { _mm_storeu_ps(a_out, a_v.quad); }
	RE::hkVector4 Hk(float x, float y, float z, float w = 0.0f) { return RE::hkVector4(x, y, z, w); }

	void Print(const std::string& a_text)
	{
		if (auto* con = RE::ConsoleLog::GetSingleton()) con->Print("%s", a_text.c_str());
		logger::info("{}", a_text);
	}

	// ------------------------------------------------------------------
	// Oars (OAS 56.x port): the vanilla ShipOar01 model, its bounds and the hand lock maths
	// ------------------------------------------------------------------
	namespace Oar
	{
		constexpr RE::FormID kShipOar01 = 0x00050E77;  // ShipOar01 (Skyrim.esm), the oar OAS uses
		constexpr const char* kBone[2] = { "AnimObjectA", "AnimObjectB" };  // the row clip moves these with the hands: A left, B right
		constexpr const char* kHand[2] = { "NPC L Hand [LHnd]", "NPC R Hand [RHnd]" };
		constexpr const char* kOarName[2] = { "RowRim Oar R", "RowRim Oar L" };  // one set: stowed in the boat, in the hands while rowing

		struct Bounds
		{
			bool known{ false };
			bool axisX{ false };     // the model's long axis is X (else Y)
			RE::NiPoint3 centre{};
			float halfLen{ 100.0f };
		};
		inline Bounds g_b;

		inline RE::NiPoint3 Axis() { return g_b.axisX ? RE::NiPoint3{ 1.0f, 0.0f, 0.0f } : RE::NiPoint3{ 0.0f, 1.0f, 0.0f }; }

		inline void Learn()
		{
			if (g_b.known) return;
			auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(kShipOar01);
			if (!base) return;
			const auto& bd = base->boundData;
			const float minX = bd.boundMin.x, minY = bd.boundMin.y, minZ = bd.boundMin.z;
			const float maxX = bd.boundMax.x, maxY = bd.boundMax.y, maxZ = bd.boundMax.z;
			const float ex = maxX - minX, ey = maxY - minY, ez = maxZ - minZ;
			g_b.axisX = ex > ey && ex >= ez;
			g_b.centre = RE::NiPoint3{ (minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f };
			g_b.halfLen = 0.5f * (g_b.axisX ? ex : ey);
			g_b.known = true;
			logger::info("Oar model bounds: x {:.0f}..{:.0f} y {:.0f}..{:.0f} z {:.0f}..{:.0f}, long axis {}, half length {:.0f}", minX, maxX, minY, maxY, minZ, maxZ,
				g_b.axisX ? "X" : "Y", g_b.halfLen);
		}

		inline void StripTree(RE::NiAVObject* a_obj, int a_depth = 0)
		{
			if (!a_obj || a_depth > 8) return;
			if (a_obj->collisionObject) a_obj->collisionObject.reset();
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) StripTree(child.get(), a_depth + 1);
			}
		}

		// A wrapper node holding a fresh clone of the oar model (demanded each time, never a node a loaded ref draws).
		inline RE::NiNode* MakeWrapped(const char* a_name)
		{
			Learn();
			auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(kShipOar01);
			auto* model = base ? base->As<RE::TESModel>() : nullptr;
			const char* path = model ? model->GetModel() : nullptr;
			if (!path || !*path) { logger::warn("Oar: ShipOar01 has no model path"); return nullptr; }
			RE::NiPointer<RE::NiNode> src;
			const RE::BSModelDB::DBTraits::ArgsType args{};
			RE::BSModelDB::Demand(path, src, args);
			if (!src) RE::BSModelDB::Demand((std::string("meshes\\") + path).c_str(), src, args);
			if (!src) { logger::warn("Oar: model '{}' not loaded", path); return nullptr; }
			auto* cloneObj = src->Clone();
			auto* clone = cloneObj ? cloneObj->AsNode() : nullptr;
			if (!clone) return nullptr;
			StripTree(clone);
			clone->local = RE::NiTransform{};
			auto* wrap = RE::NiNode::Create(1);
			wrap->name = a_name;
			wrap->AttachChild(clone, true);
			logger::info("Oar: '{}' made from '{}'", a_name, path);
			return wrap;
		}

		// The clone's root is a BSFadeNode: outside a cell's fade list it stays at fade 0 (drawn as nothing) and can carry a
		// ref's world spot in its local. Kept at fade 1 and identity, so the wrapper alone places the oar (OAS 56.6 / 56.7).
		inline void KeepFade(RE::NiAVObject* a_wrap)
		{
			auto* wrap = a_wrap ? a_wrap->AsNode() : nullptr;
			if (!wrap) return;
			for (auto& child : wrap->GetChildren()) {
				if (!child) continue;
				child->local = RE::NiTransform{};
				if (auto* fade = child->AsFadeNode()) fade->GetRuntimeData().currentFade = 1.0f;
			}
		}

		inline void Place(RE::NiAVObject* a_wrap, const RE::NiTransform& a_world)
		{
			a_wrap->local = a_wrap->parent ? a_wrap->parent->world.Invert() * a_world : a_world;
			KeepFade(a_wrap);
			RE::NiUpdateData data{};
			data.flags.set(RE::NiUpdateData::Flag::kDirty);
			a_wrap->Update(data);
		}

		// The oar on its AnimObject bone (OAS BakedTuneLocal with the hand lock on): the model centre on the bone, its long axis
		// on the bone's X, turned end for end (blade out, handle at the hands), rolled a_roll about the shaft. A mirrors B.
		inline RE::NiTransform BoneTune(int a_i, float a_roll)
		{
			const float m = a_i == 0 ? -1.0f : 1.0f;
			const RE::NiMatrix3 axis = g_b.axisX ? MakeMat(1, 0, 0, 0, 1, 0, 0, 0, 1) : RzM(1.5708f);
			const RE::NiMatrix3 rot = RxM(m * a_roll) * RzM(kPi) * axis;
			RE::NiTransform t;
			t.rotate = rot;
			t.translate = rot * g_b.centre * -1.0f;
			t.scale = 1.0f;
			return t;
		}

		// Handle end and the unit direction handle -> blade, in world space, for an oar drawn with a_t.
		inline void HandleDir(const RE::NiTransform& a_t, RE::NiPoint3& a_handle, RE::NiPoint3& a_dir)
		{
			const RE::NiPoint3 axis = Axis();
			a_handle = a_t * (g_b.centre + axis * g_b.halfLen);
			a_dir = a_t * (g_b.centre - axis * g_b.halfLen) - a_handle;
			const float l = a_dir.Length();
			a_dir = l > 1e-4f ? a_dir / l : RE::NiPoint3{ 1.0f, 0.0f, 0.0f };
		}

		// Shortest arc from unit a to unit b (Rodrigues); the roll about the shaft stays as it was.
		inline RE::NiMatrix3 ArcRotation(const RE::NiPoint3& a, const RE::NiPoint3& b)
		{
			const float c = a.x * b.x + a.y * b.y + a.z * b.z;
			RE::NiMatrix3 r;
			if (c < -0.9999f) {
				RE::NiPoint3 k = std::abs(a.x) < 0.9f ? RE::NiPoint3{ 0.0f, -a.z, a.y } : RE::NiPoint3{ -a.z, 0.0f, a.x };
				k = k / k.Length();
				const float kk[3] = { k.x, k.y, k.z };
				for (int i = 0; i < 3; ++i)
					for (int j = 0; j < 3; ++j) r.entry[i][j] = 2.0f * kk[i] * kk[j] - (i == j ? 1.0f : 0.0f);
				return r;
			}
			const RE::NiPoint3 v{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
			const float K[3][3] = { { 0.0f, -v.z, v.y }, { v.z, 0.0f, -v.x }, { -v.y, v.x, 0.0f } };
			const float f = 1.0f / (1.0f + c);
			for (int i = 0; i < 3; ++i)
				for (int j = 0; j < 3; ++j) {
					float kk = 0.0f;
					for (int n = 0; n < 3; ++n) kk += K[i][n] * K[n][j];
					r.entry[i][j] = (i == j ? 1.0f : 0.0f) + K[i][j] + kk * f;
				}
			const RE::NiPoint3 ra = r * a;
			if (ra.x * b.x + ra.y * b.y + ra.z * b.z < 0.999f) r = r.Transpose();
			return r;
		}

		// OAS 56.18 / 56.21: the oar turned about its grip (a_gap in from the handle end) onto the hand -> oarlock line, then
		// moved so the grip is on the hand (30 at the most).
		inline RE::NiTransform HandLock(const RE::NiTransform& a_t, const RE::NiPoint3& a_hand, const RE::NiPoint3& a_lock, float a_gap, bool& a_capped,
			float a_cap = 30.0f, float* a_moved = nullptr)
		{
			RE::NiTransform out = a_t;
			a_capped = false;
			RE::NiPoint3 handle, d;
			HandleDir(a_t, handle, d);
			const RE::NiPoint3 grip = handle + d * (a_gap * a_t.scale);
			RE::NiPoint3 d2 = a_lock - a_hand;
			const float l2 = d2.Length();
			if (l2 < 1.0f) return out;
			d2 = d2 / l2;
			const RE::NiMatrix3 r = ArcRotation(d, d2);
			out.rotate = r * a_t.rotate;
			out.translate = grip + r * (a_t.translate - grip);
			RE::NiPoint3 move = a_hand - grip;
			const float ml = move.Length();
			if (a_moved) *a_moved = ml;
			if (ml > a_cap) {
				move = move * (a_cap / ml);
				a_capped = true;
			}
			out.translate = out.translate + move;
			return out;
		}

		inline RE::NiAVObject* HandPart(RE::NiAVObject* a_root, int a_i, const char* a_part)
		{
			const char lr = a_i == 0 ? 'L' : 'R';
			return a_root->GetObjectByName(std::format("NPC {} Finger{} [{}F{}]", lr, a_part, lr, a_part));
		}

		// OAS 56.21 / 56.23: the palm grip point (hand node + an offset in the palm frame from the finger nodes). a_sign -1 for
		// the left hand's palm side, +1 for the right.
		inline RE::NiPoint3 PalmGrip(RE::NiAVObject* a_root, int a_i, const RE::NiAVObject* a_hand, float a_gx, float a_gy, float a_gz, float a_sign, bool& a_ok)
		{
			const RE::NiPoint3 h = a_hand->world.translate;
			RE::NiAVObject* f[4] = { HandPart(a_root, a_i, "10"), HandPart(a_root, a_i, "20"), HandPart(a_root, a_i, "30"), HandPart(a_root, a_i, "40") };
			a_ok = f[0] && f[1] && f[2] && f[3];
			if (!a_ok) return h;
			auto unit = [](RE::NiPoint3 v) { const float l = v.Length(); return l > 1e-4f ? v / l : v; };
			auto dot = [](const RE::NiPoint3& a, const RE::NiPoint3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
			auto cross = [](const RE::NiPoint3& a, const RE::NiPoint3& b) { return RE::NiPoint3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; };
			const RE::NiPoint3 mean = (f[0]->world.translate + f[1]->world.translate + f[2]->world.translate + f[3]->world.translate) * 0.25f;
			const RE::NiPoint3 y = unit(mean - h);
			RE::NiPoint3 x = f[0]->world.translate - f[3]->world.translate;
			x = unit(x - y * dot(x, y));
			RE::NiPoint3 z = unit(cross(x, y));
			if (a_sign < 0.0f) z = z * -1.0f;
			return h + (x * a_gx + y * a_gy + z * a_gz) * a_hand->world.scale;
		}

		// OAS 50 / 56.35: an oar stowed lengthwise in the boat (Miguel's tuned pose: right 18, forward 309, up -53, turn 7.9 deg
		// in the OAS vehicle frame = model x 18, y 59), lying on the raised thwarts here. Model space, a_side +1 right / -1 left.
		inline RE::NiTransform StowLocal(float a_side, const RE::NiPoint3& a_at, float a_yaw, float a_scale)
		{
			const RE::NiMatrix3 fix = g_b.axisX ? RzM(-1.5708f) : MakeMat(1, 0, 0, 0, 1, 0, 0, 0, 1);  // long axis along the boat
			const RE::NiMatrix3 rot = RzM(a_side * a_yaw) * fix * RzM(kPi);                          // mesh turned end for end (OAS)
			RE::NiTransform t;
			t.rotate = rot;
			t.scale = a_scale;
			t.translate = RE::NiPoint3{ a_side * a_at.x, a_at.y, a_at.z } - rot * g_b.centre * a_scale;
			return t;
		}

		// v1.2.4: resting in the oarlock (the v0.6.0 rest pose): the shaft through the oarlock straight out from the boat, a_in of
		// its length inboard, the blade a_dip down and flat, full size (as in the hands).
		inline RE::NiTransform RestLocal(float a_side, const RE::NiPoint3& a_lock, float a_in, float a_dipDeg)
		{
			const RE::NiPoint3 axis = Axis();
			const float dip = a_dipDeg * kPi / 180.0f;
			const RE::NiPoint3 out{ a_side, 0.0f, 0.0f };
			const RE::NiPoint3 d{ a_side * std::cos(dip), 0.0f, -std::sin(dip) };  // handle -> blade
			const RE::NiMatrix3 flat = ArcRotation(axis * -1.0f, out);             // handle -> blade straight out, blade flat
			const RE::NiMatrix3 rot = ArcRotation(out, d) * flat;                   // then the blade tipped down
			const RE::NiPoint3 lock{ a_side * a_lock.x, a_lock.y, a_lock.z };
			const RE::NiPoint3 handle = lock - d * (a_in * 2.0f * g_b.halfLen);
			RE::NiTransform t;
			t.rotate = rot;
			t.scale = 1.0f;
			t.translate = handle - rot * (g_b.centre + axis * g_b.halfLen);
			return t;
		}

		// v1.7.33: shipped for the night: lying along the gunwale in its side's middle and aft oarlocks (shaft centres), the
		// handle a_fwd forward of the middle one, the blade flat, aft
		inline RE::NiTransform ShipLocal(float a_side, const RE::NiPoint3& a_mid, const RE::NiPoint3& a_aft, float a_fwd, bool a_flip = false)
		{
			const RE::NiPoint3 axis = a_flip ? Axis() * -1.0f : Axis();  // v1.7.36: which end is the handle is checked by the caller
			const RE::NiPoint3 m{ a_side * a_mid.x, a_mid.y, a_mid.z };
			const RE::NiPoint3 a{ a_side * a_aft.x, a_aft.y, a_aft.z };
			RE::NiPoint3 d = a - m;
			const float len = d.Length();
			d = len > 0.001f ? d * (1.0f / len) : RE::NiPoint3{ 0.0f, -1.0f, 0.0f };  // handle -> blade
			const RE::NiPoint3 out{ a_side, 0.0f, 0.0f };
			const RE::NiMatrix3 flat = ArcRotation(axis * -1.0f, out);
			const RE::NiMatrix3 rot = ArcRotation(out, d) * flat;
			const RE::NiPoint3 handle = m - d * a_fwd;
			RE::NiTransform t;
			t.rotate = rot;
			t.scale = 1.0f;
			t.translate = handle - rot * (g_b.centre + axis * g_b.halfLen);
			return t;
		}

		// Between two poses (OAS BlendXf): position and size straight, rotation along the short arc.
		inline RE::NiTransform Blend(const RE::NiTransform& a_a, const RE::NiTransform& a_b, float a_w)
		{
			Quat qa = FromMatrix(a_a.rotate), qb = FromMatrix(a_b.rotate);
			if (Dot(qa, qb) < 0.0f) qb = Quat{ -qb.x, -qb.y, -qb.z, -qb.w };
			const float c = (std::min)(1.0f, Dot(qa, qb));
			Quat q;
			if (c > 0.9995f) {
				q = Norm(Quat{ qa.x + (qb.x - qa.x) * a_w, qa.y + (qb.y - qa.y) * a_w, qa.z + (qb.z - qa.z) * a_w, qa.w + (qb.w - qa.w) * a_w });
			} else {
				const float th = std::acos(c), sn = std::sin(th);
				const float wa = std::sin((1.0f - a_w) * th) / sn, wb = std::sin(a_w * th) / sn;
				q = Norm(Quat{ qa.x * wa + qb.x * wb, qa.y * wa + qb.y * wb, qa.z * wa + qb.z * wb, qa.w * wa + qb.w * wb });
			}
			RE::NiTransform t;
			t.rotate = ToMatrix(q);
			t.translate = a_a.translate + (a_b.translate - a_a.translate) * a_w;
			t.scale = a_a.scale + (a_b.scale - a_a.scale) * a_w;
			return t;
		}
	}

	// ------------------------------------------------------------------
	// Paints and oar wood (OAS Part 30 / 32 / 56.60): our diffuse files swapped onto the boat model's shapes at runtime,
	// only the diffuse slot (normals and the rest stay vanilla). Files in Data\textures\RowRim.
	// ------------------------------------------------------------------
	namespace Paint
	{
		enum : int { kVanilla = 0, kBeige, kBlue, kBlack, kRed, kWhite, kBlueDesign, kCount };
		constexpr const char* kNames[kCount] = { "vanilla", "beige", "blue", "black", "red", "white", "bluedesign" };
		struct Tex
		{
			const char* shape;
			const char* vanillaDiffuse;  // end of the vanilla path, also finds the shape
			const char* ours[kCount];
		};
		constexpr Tex kTex[] = {
			{ "ShipRowBoat01:3", "shipwoodside01.dds",
				{ nullptr, "textures\\RowRim\\boat_side_d.dds", "textures\\RowRim\\boat_side_blue_d.dds", "textures\\RowRim\\boat_side_black_d.dds",
					"textures\\RowRim\\boat_side_red_d.dds", "textures\\RowRim\\boat_side_white_d.dds", "textures\\RowRim\\boat_side_bluedesign_d.dds" } },
			{ "ShipRowBoat01:5 - L1_BoatDetails:5", "shipwood01.dds",
				{ nullptr, "textures\\RowRim\\boat_hull_d.dds", "textures\\RowRim\\boat_hull_blue_d.dds", "textures\\RowRim\\boat_hull_d.dds",
					"textures\\RowRim\\boat_hull_d.dds", "textures\\RowRim\\boat_hull_d.dds", "textures\\RowRim\\boat_hull_d.dds" } },
		};
		constexpr const char* kOarWood = "textures\\RowRim\\boat_oar_kwila_d.dds";

		inline bool IEq(std::string_view a, std::string_view b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
		}
		inline bool EndsI(std::string_view a, std::string_view e) { return a.size() >= e.size() && IEq(a.substr(a.size() - e.size()), e); }
		inline bool Exists(const char* a_rel)
		{
			std::error_code ec;
			return a_rel && std::filesystem::exists(std::filesystem::current_path(ec) / "Data" / a_rel, ec);
		}

		// one shape's diffuse swapped (a copy of its material with only slot 0 changed, as po3's texture swaps do)
		inline void SetDiffuse(RE::BSGeometry* a_geo, RE::BSLightingShaderProperty* a_prop, RE::BSLightingShaderMaterialBase* a_mat, RE::BSTextureSet* a_set, const char* a_path)
		{
			auto* newSet = RE::BSShaderTextureSet::Create();
			for (std::uint32_t s = 0; s < RE::BSTextureSet::Texture::kTotal; ++s) {
				const auto slot = static_cast<RE::BSTextureSet::Texture>(s);
				const char* path = a_set->GetTexturePath(slot);
				newSet->SetTexturePath(slot, path ? path : "");
			}
			newSet->SetTexturePath(RE::BSTextureSet::Texture::kDiffuse, a_path);
			auto* newMat = static_cast<RE::BSLightingShaderMaterialBase*>(a_mat->Create());
			newMat->CopyMembers(a_mat);
			newMat->ClearTextures();
			newMat->OnLoadTextureSet(0, newSet);
			a_prop->SetMaterial(newMat, true);
			a_prop->SetupGeometry(a_geo);
			a_prop->FinishSetupGeometry(a_geo);
			newMat->~BSLightingShaderMaterialBase();
			RE::free(newMat);
		}

		// the paint on a boat model; vanilla puts the shape's own (vanilla) file back
		inline void Apply(RE::NiAVObject* a_root, int a_paint)
		{
			if (!a_root) return;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geo) {
				auto* prop = a_geo->lightingShaderProp_cast();
				auto* mat = prop ? static_cast<RE::BSLightingShaderMaterialBase*>(prop->material) : nullptr;
				auto* set = mat ? mat->textureSet.get() : nullptr;
				if (!set) return RE::BSVisit::BSVisitControl::kContinue;
				const std::string_view name = a_geo->name.c_str() ? a_geo->name.c_str() : "";
				const char* d = set->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
				const std::string_view diffuse = d ? d : "";
				for (const auto& t : kTex) {
					bool ours = false;
					for (const char* o : t.ours) if (o && IEq(diffuse, o)) ours = true;
					if (name != t.shape && !EndsI(diffuse, t.vanillaDiffuse) && !ours) continue;
					static std::string vanilla[2];
					const int ti = static_cast<int>(&t - kTex);
					if (!ours && !diffuse.empty()) vanilla[ti] = std::string(diffuse);
					const char* file = t.ours[a_paint];
					const char* want = file && Exists(file) ? file : (!vanilla[ti].empty() ? vanilla[ti].c_str() : nullptr);
					if (!want || IEq(diffuse, want)) break;
					SetDiffuse(a_geo, prop, mat, set, want);
					logger::info("Paint: '{}' {} -> {} ({})", name, diffuse, want, kNames[a_paint]);
					break;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		inline void OarWood(RE::NiAVObject* a_root)
		{
			static int have = -1;
			if (have < 0) have = Exists(kOarWood) ? 1 : 0;
			if (!a_root || !have) return;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geo) {
				auto* prop = a_geo->lightingShaderProp_cast();
				auto* mat = prop ? static_cast<RE::BSLightingShaderMaterialBase*>(prop->material) : nullptr;
				auto* set = mat ? mat->textureSet.get() : nullptr;
				if (!set) return RE::BSVisit::BSVisitControl::kContinue;
				const char* d = set->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
				if (!d || !IEq(d, kOarWood)) SetDiffuse(a_geo, prop, mat, set, kOarWood);
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
	}

	// OAS row clip events (Nemesis / Pandora, from the Oars and Sails animation files)
	constexpr const char* kRowStartEvent = "OarsRowStart";
	constexpr const char* kRowStopEvent = "IdleForceDefaultState";

	// v1.6.6: a hang watchdog. Each step of our per-frame work names itself (Stage); a thread of ours checks once a second
	// that the player update still beats, and if it stopped for 3 s writes the last step to the log (the 19:42 freeze
	// left nothing: the game hung, no crash log). A menu also stops the beat, so the line says that too.
	inline std::atomic<const char*> g_stage{ "start" };
	inline std::atomic<std::uint64_t> g_beat{ 0 };
	inline void Stage(const char* a_s) { g_stage.store(a_s, std::memory_order_relaxed); }
	inline void StartWatchdog()
	{
		static bool started = false;
		if (started) return;
		started = true;
		std::thread([] {
			std::uint64_t last = 0;
			int still = 0;
			bool told = false;
			for (;;) {
				std::this_thread::sleep_for(std::chrono::seconds(1));
				const std::uint64_t now = g_beat.load(std::memory_order_relaxed);
				if (now != last) {
					if (told) logger::info("Watchdog: the player update runs again");
					last = now;
					still = 0;
					told = false;
					continue;
				}
				if (now == 0 || told) continue;
				if (++still >= 3) {
					told = true;
					logger::info("Watchdog: no player update for 3 s (a hang, or a menu / loading screen). Last step of ours: {}", g_stage.load(std::memory_order_relaxed));
					spdlog::default_logger()->flush();
				}
			}
		}).detach();
	}

	// v1.5.0: bioluminescent plankton round the player's boat at dusk and night, see src\Plankton_v150.md
#include "Plankton.inl"
#include "Wake.inl"  // v1.5.16: the boat's own V wake, see src\Wake_v1516.md
#include "Grid.inl"  // v1.7.20: oas "grid on|off", the world cells shown

	// v1.3.0: the oar ripples and dig sound (Fx) and the OAS lake boats (LakeBoats), see src\Port7_LakeBoats.md
#include "LakeBoats.inl"

	// v1.6.0: crew and passengers (OAS crew add / boarding / late boarding / numpad seat tuning), see src\Crew_v160.md
#include "Crew.inl"
	// v1.7.0: the Fishing Supplies riding in the boat (Creation Club Fishing), see src\Mesh_Clutter.md
#include "Gear.inl"

	// Win32 GDI defines GetObject as GetObjectA/GetObjectW. That macro also
	// rewrites Papyrus Variable::GetObject(), causing the MSVC build to fail.
#ifdef GetObject
#undef GetObject
#endif

	// Audio event callbacks can run outside the player update. Publish only an
	// atomic form ID to them, not the boat's mutable state or scenegraph pointers.
	namespace CarrierAudio
	{
		inline std::atomic<RE::FormID> id{ 0 };
		inline std::atomic<unsigned> footstepsLogged{ 0 }, animationsLogged{ 0 };
		inline bool IsCarrier(const RE::TESObjectREFR* a_ref)
		{
			const auto silent = id.load(std::memory_order_relaxed);
			return silent && a_ref && a_ref->GetFormID() == silent;
		}
		inline bool SoundPlayTag(std::string_view a_tag)
		{
			auto starts = [&](std::string_view a_prefix) {
				if (a_tag.size() < a_prefix.size()) return false;
				for (std::size_t i = 0; i < a_prefix.size(); ++i)
					if (std::tolower(static_cast<unsigned char>(a_tag[i])) != a_prefix[i]) return false;
				return true;
			};
			return starts("soundplay") || starts("playsound");
		}
	}

	// Boat-only fishing gate. Read CC state; never overwrite a running Papyrus
	// stack's state/controls or delete its rod. All changes live in this .cpp.
	namespace BoatFishing
	{
		struct Status
		{
			int state = -1;
			bool handling = false;
			bool ours = false;
			RE::VMHandle reel = 0;
			RE::BSTSmartPointer<RE::BSScript::Object> system;
		};
		inline float startHold = 0.0f;
		inline bool quitAfterMenu = false;
		inline bool watchAttempt = false, sawActiveAttempt = false;
		inline RE::VMHandle Handle(RE::TESForm* a_form)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			return policy && a_form ? policy->GetHandleForObject(a_form->GetFormType(), a_form) : 0;
		}
		inline Status Read()
		{
			Status s;
			auto supplies = Gear::g_items[Gear::kSupplies].ref.get();
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!supplies || !vm) return s;
			const auto h = Handle(supplies.get());
			RE::BSTSmartPointer<RE::BSScript::Object> act;
			if (!h || !vm->FindBoundObject(h, "ccBGSSSE001_FishingActScript", act) || !act) return s;
			auto* p = act->GetProperty("FishingSystem");
			auto sys = p && p->IsObject() ? p->GetObject() : RE::BSTSmartPointer<RE::BSScript::Object>{};
			if (!sys) return s;
			s.system = sys;
			if (auto* v = sys->GetVariable("currentSystemState"); v && v->IsInt()) s.state = v->GetSInt();
			if (auto* v = sys->GetVariable("handlingInputOrUpdate"); v && v->IsBool()) s.handling = v->GetBool();
			if (auto* v = sys->GetVariable("currentFishingSupplies"); v && v->IsObject()) {
				auto owner = v->GetObject();
				s.ours = owner && owner->GetHandle() == h;
			}
			if (auto* v = sys->GetProperty("ReelLineRef"); v && v->IsObject()) {
				auto reel = v->GetObject();
				if (reel) s.reel = reel->GetHandle();
			}
			return s;
		}
		inline bool IsBoatReel(RE::TESObjectREFR* a_ref, const Status& a_s)
		{
			return a_ref && a_s.ours && a_s.reel && Handle(a_ref) == a_s.reel;
		}
		inline bool CanReel(const Status& a_s)
		{
			// CC: fishing=2, nibble=3, hooked=4. Setup/resolve/cleanup
			// must not accept another E or show a stale continuation prompt.
			return !a_s.handling && a_s.state >= 2 && a_s.state <= 4;
		}
		inline bool Idle(const Status& a_s) { return a_s.state == 0 && !a_s.handling; }
		inline void TendFinishedAttempt()
		{
			if (!watchAttempt) return;
			auto* ui = RE::UI::GetSingleton();
			if (ui && ui->GameIsPaused()) return;
			auto s = Read();
			if (!s.ours) return;
			if (s.state > 0) sawActiveAttempt = true;
			if (!Idle(s) || (!sawActiveAttempt && startHold > 0.0f)) return;
			// Normal CC catch cleanup leaves its large continuation triggers
			// on the fisher. Finish that session through CC's idle cleanup:
			// return the volumes, followers and weather, without touching loot.
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (vm && s.system) {
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
				if (vm->DispatchMethodCall(s.system, "DoCleanupTasks", RE::MakeFunctionArguments(), callback)) {
					watchAttempt = false;
					startHold = (std::max)(startHold, 1.0f);
					logger::info("Fish: attempt finished; CC continuation volumes returned, look down to fish again");
				}
			}
		}
		inline void TendMenuExit()
		{
			if (!quitAfterMenu) return;
			auto* ui = RE::UI::GetSingleton();
			if (ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::StatsMenu::MENU_NAME) || ui->IsMenuOpen(RE::LevelUpMenu::MENU_NAME))) return;
			auto s = Read();
			if (!s.ours || Idle(s)) { quitAfterMenu = false; return; }
			if (s.handling || s.state == 5 || s.state == 6) return;
			// Let CC perform its own normal exit after a skills/level-up menu.
			// Do not force controls or set its state while a result is resolving.
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (vm && s.system) {
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
				if (vm->DispatchMethodCall(s.system, "DoCleanupTasks", RE::MakeFunctionArguments(), callback)) {
					quitAfterMenu = false;
					startHold = 2.0f;
					logger::info("Fish: requested CC's normal exit after the skills/level-up menu");
				}
			}
		}
	}
	// v1.7.8: old Oars and Sails boats in saves made with OAS 1.0.x, see src\Release_v178_OAS.md
#include "Legacy.inl"

	// ------------------------------------------------------------------
	// v1.7.8: controller (OAS B2 / B5 / 55.1). Activate also by the gamepad button the game maps to it (seated, the game may
	// send that button without its user event); third person zoom while seated: hold R3 (Toggle POV) and push the left stick
	// up / down (vanilla's way, without the view switch R3 would do, and no rowing meanwhile), or hold L3 (Sneak, off aboard)
	// and push the right stick. RowRim.ini [Controls] PadZoom=1, PadZoomSpeed=1.2.
	// ------------------------------------------------------------------
	namespace Pad
	{
		inline bool g_loaded = false;
		inline bool g_zoomOn = true;
		inline float g_zoomSpeed = 1.2f;
		inline std::uint32_t g_activate = 0;  // the gamepad key mapped to Activate
		inline bool g_l3 = false, g_r3 = false;
		inline float g_leftY = 0.0f, g_rightY = 0.0f;
		inline bool g_zoomedR3 = false, g_r3StartFirst = false;
		inline float g_undo = 0.0f;
		inline bool g_logged = false;

		inline void Load()
		{
			g_loaded = true;
			const auto ini = Plankton::IniPath();
			g_zoomOn = GetPrivateProfileIntA("Controls", "PadZoom", 1, ini.c_str()) != 0;
			char b[32]{};
			GetPrivateProfileStringA("Controls", "PadZoomSpeed", "1.2", b, sizeof(b), ini.c_str());
			try { g_zoomSpeed = std::clamp(std::stof(b), 0.1f, 5.0f); } catch (...) { g_zoomSpeed = 1.2f; }
			if (GetPrivateProfileStringA("Controls", "PadZoom", "", b, sizeof(b), ini.c_str()) == 0) {
				WritePrivateProfileStringA("Controls", "PadZoom", "1", ini.c_str());
				WritePrivateProfileStringA("Controls", "PadZoomSpeed", "1.2", ini.c_str());
			}
			auto* cm = RE::ControlMap::GetSingleton();
			auto* ue = RE::UserEvents::GetSingleton();
			if (cm && ue && ue->activate.c_str()) {
				const std::uint32_t k = cm->GetMappedKey(std::string_view{ ue->activate.c_str() }, RE::INPUT_DEVICE::kGamepad);
				g_activate = (k != 0xFF && k != 0xFFFFFFFF) ? k : 0;
			}
			logger::info("Controls: gamepad Activate {:#x}, zoom while seated {} (speed {:.1f})", g_activate, g_zoomOn ? "on" : "off", g_zoomSpeed);
		}
		inline bool IsActivate(std::uint32_t a_id)
		{
			if (!g_loaded) Load();
			return g_activate != 0 && a_id == g_activate;
		}
		// R3 held and the left stick pushed: zooming, not rowing
		inline bool R3Zooming() { return g_zoomOn && g_r3 && std::abs(g_leftY) > 0.2f; }
		inline void Reset() { g_l3 = g_r3 = false; g_leftY = g_rightY = 0.0f; g_zoomedR3 = false; g_undo = 0.0f; }

		// every frame while seated
		inline void Zoom(float a_dt)
		{
			if (!g_loaded) Load();
			if (!g_zoomOn) return;
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!cam || !cam->currentState) return;
			// R3 used to zoom: if the game switched third to first person on that press / release, back to third at once
			if (g_r3) g_undo = 0.5f;
			else if (g_undo > 0.0f) g_undo -= a_dt;
			if (g_zoomedR3 && !g_r3StartFirst && (g_r3 || g_undo > 0.0f) && cam->IsInFirstPerson()) {
				if (auto target = cam->cameraStates[RE::CameraState::kThirdPerson]) cam->SetState(target.get());
				logger::info("Zoom: R3 view switch undone");
				g_zoomedR3 = false;
			}
			if (cam->currentState->id != RE::CameraState::kThirdPerson) return;
			auto* tps = static_cast<RE::ThirdPersonState*>(cam->currentState.get());
			float input = 0.0f;
			const bool r3 = R3Zooming();
			if (r3) input = g_leftY;
			else if (g_l3 && std::abs(g_rightY) >= 0.2f) input = g_rightY;
			if (input == 0.0f) return;
			tps->targetZoomOffset = std::clamp(tps->targetZoomOffset - input * g_zoomSpeed * a_dt, 0.0f, 1.0f);  // stick up: closer
			tps->savedZoomOffset = tps->targetZoomOffset;
			if (r3) g_zoomedR3 = true;
			if (!g_logged) {
				g_logged = true;
				logger::info("Zoom: {} held + stick, zoom now {:.2f}", r3 ? "R3" : "L3", tps->targetZoomOffset);
			}
		}
	}

	// ------------------------------------------------------------------
	// The test hull
	// ------------------------------------------------------------------
	enum class Mode { kDrive, kWarp, kFrozen };
	const char* ModeName(Mode m) { return m == Mode::kDrive ? "drive" : m == Mode::kWarp ? "warp" : "frozen"; }

	class TestHull
	{
	public:
		static TestHull& Get()
		{
			static TestHull s;
			return s;
		}

		// ---- commands ----
		void Spawn(bool a_underPlayer)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->GetParentCell()) { Print("RowRim: no player cell"); return; }
			const float yaw = player->GetAngleZ();
			const RE::NiPoint3 fwd{ std::sin(yaw), std::cos(yaw), 0.0f };
			const RE::NiPoint3 feet = player->GetPosition();
			RE::NiPoint3 at = a_underPlayer ? feet : RE::NiPoint3{ feet.x + fwd.x * 350.0f, feet.y + fwd.y * 350.0f, feet.z };
			float water = 0.0f;
			const bool wet = WaterAt(at, water);
			if (a_underPlayer) at.z = feet.z - kDeckZ - 3.0f;  // deck just under the feet
			else at.z = wet ? water + kWaterZ + heightOff_ : feet.z - kDeckZ;
			SpawnAt(at, yaw, a_underPlayer ? "under you" : "ahead", wet ? std::format("water {:.0f}", water) : std::string("no water"));
		}

		// v1.7.8: the boat placed at a spot and heading (Spawn ahead of / under the player, and the docks)
		void SpawnAt(const RE::NiPoint3& a_at, float a_yaw, const char* a_where, const std::string& a_water)
		{
			Delete(false);
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* base = RE::TESForm::LookupByID<RE::TESObjectSTAT>(kRowBoatStatic);
			if (!base) base = RE::TESForm::LookupByEditorID<RE::TESObjectSTAT>("ShipRowBoat01");
			if (!player || !base || !player->GetParentCell()) { Print("RowRim: no player cell or ShipRowBoat01 base"); return; }
			const RE::NiPoint3 at = a_at;
			const float yaw = a_yaw;

			// v0.2.3: persistent by default, the way followers are: the engine keeps track of where it is instead of filing it
			// under the cell it was placed in (test: does it stay drawn across cells with no hop at all?)
			auto ref = player->PlaceObjectAtMe(base, persist_);
			if (!ref) { Print("RowRim: PlaceObjectAtMe failed"); return; }
			refId_ = ref->GetFormID();
			ref->data.angle = RE::NiPoint3{ 0.0f, 0.0f, yaw };
			ref->SetPosition(at);
			if (persist_ && mobile_) MakeMobile(ref.get(), at, yaw);
			if (carrierOn_) SpawnCarrier(at);
			pos_ = at;
			yaw_ = yaw;
			t_ = 0.0f;
			roll_ = pitch_ = heave_ = 0.0f;
			needInit_ = true;
			initTries_ = 0;
			havePose_ = false;
			inits_ = 0;
			Print(std::format("RowRim: boat {:08X} placed {} ({}) at {:.0f} {:.0f} {:.0f} yaw {:.0f}; mode {}; {}, cell hop {}",
				refId_, a_where, a_water, at.x, at.y, at.z, yaw * 180.0f / kPi, ModeName(mode_), persist_ ? "persistent" : "temporary", hop_ ? "on" : "off"));
		}

		// ---- v1.7.8: docks (OAS 56.51 / 56.61). Boat spots placed by hand, shipped in RowRim_Docks.ini. There is one boat,
		// so a dock calls it: walk within 7000 of a dock (its cell loaded) while your boat is somewhere else and it is waiting
		// there, facing the way it was placed, in that dock's own paint. A boat you left within 4000 of you is never taken. ----
		struct Dock { RE::FormID world{ 0 }; float x{ 0 }, y{ 0 }, z{ 0 }, yaw{ 0 }; std::string name; };
		std::vector<Dock> docks_;
		bool docksLoaded_{ false };
		bool docksOn_{ true };
		float dockTick_{ 0.0f };
		float dockWait_{ 6.0f };  // after a load: the save's own boat first
		int dockServed_{ -1 };
		static constexpr float kDockCallRange = 7000.0f;
		static constexpr float kDockOwnBoatRange = 4000.0f;
		static constexpr float kDockAtSpot = 300.0f;

		static std::string DockIniPath()
		{
			std::error_code ec;
			return (std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "RowRim_Docks.ini").string();
		}
		void LoadDocks()
		{
			docksLoaded_ = true;
			docks_.clear();
			const std::string path = DockIniPath();
			docksOn_ = GetPrivateProfileIntA("Docks", "Enabled", 1, path.c_str()) != 0;
			const int n = GetPrivateProfileIntA("Docks", "Count", 0, path.c_str());
			for (int i = 1; i <= n; ++i) {
				const std::string sec = std::format("Dock{}", i);
				char w[32]{}, x[32]{}, y[32]{}, z[32]{}, a[32]{}, nm[128]{};
				GetPrivateProfileStringA(sec.c_str(), "World", "", w, sizeof(w), path.c_str());
				GetPrivateProfileStringA(sec.c_str(), "X", "", x, sizeof(x), path.c_str());
				GetPrivateProfileStringA(sec.c_str(), "Y", "", y, sizeof(y), path.c_str());
				GetPrivateProfileStringA(sec.c_str(), "Z", "", z, sizeof(z), path.c_str());
				GetPrivateProfileStringA(sec.c_str(), "Yaw", "", a, sizeof(a), path.c_str());
				GetPrivateProfileStringA(sec.c_str(), "Name", "", nm, sizeof(nm), path.c_str());
				if (!w[0] || !x[0] || !y[0]) continue;
				try {
					Dock d;
					d.world = static_cast<RE::FormID>(std::stoul(w, nullptr, 16));
					d.x = std::stof(x);
					d.y = std::stof(y);
					d.z = z[0] ? std::stof(z) : 0.0f;
					d.yaw = a[0] ? std::stof(a) : 0.0f;
					d.name = nm[0] ? nm : sec;
					docks_.push_back(std::move(d));
				} catch (...) { logger::warn("Docks: {} unreadable, skipped", sec); }
			}
			logger::info("Docks: {} loaded from {} ({})", docks_.size(), path, docksOn_ ? "on" : "off");
		}
		void SaveDocks()
		{
			const std::string path = DockIniPath();
			const int old = GetPrivateProfileIntA("Docks", "Count", 0, path.c_str());
			for (int i = 1; i <= old; ++i) WritePrivateProfileStringA(std::format("Dock{}", i).c_str(), nullptr, nullptr, path.c_str());
			WritePrivateProfileStringA("Docks", "Count", std::to_string(docks_.size()).c_str(), path.c_str());
			WritePrivateProfileStringA("Docks", "Enabled", docksOn_ ? "1" : "0", path.c_str());
			for (std::size_t i = 0; i < docks_.size(); ++i) {
				const auto& d = docks_[i];
				const std::string sec = std::format("Dock{}", i + 1);
				WritePrivateProfileStringA(sec.c_str(), "Name", d.name.c_str(), path.c_str());
				WritePrivateProfileStringA(sec.c_str(), "World", std::format("{:08X}", d.world).c_str(), path.c_str());
				WritePrivateProfileStringA(sec.c_str(), "X", std::format("{:.1f}", d.x).c_str(), path.c_str());
				WritePrivateProfileStringA(sec.c_str(), "Y", std::format("{:.1f}", d.y).c_str(), path.c_str());
				WritePrivateProfileStringA(sec.c_str(), "Z", std::format("{:.1f}", d.z).c_str(), path.c_str());
				WritePrivateProfileStringA(sec.c_str(), "Yaw", std::format("{:.4f}", d.yaw).c_str(), path.c_str());
			}
		}
		static RE::FormID WorldOf(RE::PlayerCharacter* a_player)
		{
			auto* ws = a_player ? a_player->GetWorldspace() : nullptr;
			return ws ? ws->GetFormID() : 0u;
		}
		static int DockPaint(const std::string& a_name)  // each dock its own colour, the same every time (one of our six)
		{
			std::uint32_t h = 2166136261u;
			for (const char c : a_name) h = (h ^ static_cast<std::uint8_t>(c)) * 16777619u;
			return Paint::kBeige + static_cast<int>(h % static_cast<std::uint32_t>(Paint::kCount - Paint::kBeige));
		}
		bool HaveBoat() const { return refId_ && Ref(); }

	public:
		std::string DockCommand(const std::string& a_args)
		{
			if (!docksLoaded_) LoadDocks();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return "no player";
			std::istringstream in(a_args);
			std::string verb;
			in >> verb;
			std::string rest;
			std::getline(in, rest);
			while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
			while (!rest.empty() && rest.back() == ' ') rest.pop_back();
			const RE::NiPoint3 pp = player->GetPosition();
			const RE::FormID world = WorldOf(player);
			auto nearest = [&](float& a_dist) {
				int best = -1;
				a_dist = 1e30f;
				for (int i = 0; i < static_cast<int>(docks_.size()); ++i) {
					const auto& d = docks_[i];
					if (d.world != world) continue;
					const float dd = std::hypot(d.x - pp.x, d.y - pp.y);
					if (dd < a_dist) { a_dist = dd; best = i; }
				}
				return best;
			};
			if (verb == "add") {
				if (seated_) return "get off the boat first (the dock takes where the boat is now)";
				if (!world) return "docks work outdoors only";
				if (!HaveBoat() || std::hypot(pos_.x - pp.x, pos_.y - pp.y) > 1500.0f) {
					Spawn(false);  // no boat beside you: one ahead, as oas boat does, and that spot is saved
					if (!HaveBoat()) return "no boat near you and none could be placed ahead";
				}
				Dock d{ world, pos_.x, pos_.y, pos_.z, yaw_, rest.empty() ? std::format("dock{}", docks_.size() + 1) : rest };
				docks_.push_back(d);
				SaveDocks();
				dockServed_ = static_cast<int>(docks_.size()) - 1;
				logger::info("Docks: added '{}' world {:08X} at ({:.0f}, {:.0f}, {:.0f}) facing {:.0f} deg", d.name, d.world, d.x, d.y, d.z, d.yaw * 57.2958f);
				return std::format("dock {} '{}' saved where the boat is, facing {:.0f} deg ({} docks, {})", docks_.size(), d.name, d.yaw * 57.2958f,
					docks_.size(), DockIniPath());
			}
			if (verb == "del" || verb == "delete" || verb == "remove") {
				float dist = 0;
				const int i = nearest(dist);
				if (i < 0 || dist > 2000.0f) return "no dock within 2000 of you";
				const std::string name = docks_[i].name;
				docks_.erase(docks_.begin() + i);
				SaveDocks();
				dockServed_ = -1;
				logger::info("Docks: removed '{}'", name);
				return std::format("dock '{}' removed ({} left)", name, docks_.size());
			}
			if (verb == "list" || verb.empty()) {
				auto* console = RE::ConsoleLog::GetSingleton();
				for (std::size_t i = 0; i < docks_.size(); ++i) {
					const auto& d = docks_[i];
					const std::string line = d.world == world ? std::format("  {} '{}' {:.0f} away, facing {:.0f} deg, {}", i + 1, d.name, std::hypot(d.x - pp.x, d.y - pp.y),
						d.yaw * 57.2958f, Paint::kNames[DockPaint(d.name)])
					                                          : std::format("  {} '{}' (other worldspace {:08X})", i + 1, d.name, d.world);
					if (console) console->Print("%s", line.c_str());
					logger::info("Docks:{}", line);
				}
				return std::format("{} docks, {} (file {})", docks_.size(), docksOn_ ? "on" : "off", DockIniPath());
			}
			if (verb == "on" || verb == "off") {
				docksOn_ = verb == "on";
				SaveDocks();
				return std::format("docks {}", docksOn_ ? "on" : "off");
			}
			if (verb == "reload") {
				LoadDocks();
				dockServed_ = -1;
				return std::format("{} docks reloaded", docks_.size());
			}
			return "usage: oas \"dock add <name>\" | \"dock del\" | \"dock list\" | \"dock on\" / \"dock off\" | \"dock reload\"";
		}

	private:
		// once a second while not aboard: bring the boat to a dock the player is near, unless their boat is near them already
		void TendDocks(float a_dt)
		{
			if (dockWait_ > 0.0f) { dockWait_ -= a_dt; return; }
			if ((dockTick_ -= a_dt) > 0.0f) return;
			dockTick_ = 1.0f;
			if (!docksLoaded_) LoadDocks();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!docksOn_ || docks_.empty() || !player || seated_ || helm_) return;
			if (HaveBoat() && PlayerAboard()) return;
			const RE::FormID world = WorldOf(player);
			if (!world) return;
			auto* cell = player->GetParentCell();
			if (!cell || !cell->IsAttached()) return;
			const RE::NiPoint3 pp = player->GetPosition();
			int best = -1;
			float bestD = kDockCallRange;
			for (int i = 0; i < static_cast<int>(docks_.size()); ++i) {
				const auto& d = docks_[i];
				if (d.world != world) continue;
				const float dd = std::hypot(d.x - pp.x, d.y - pp.y);
				if (dd < bestD) { bestD = dd; best = i; }
			}
			if (best < 0) {
				if (dockServed_ >= 0 && dockServed_ < static_cast<int>(docks_.size())) {
					const auto& d = docks_[dockServed_];
					if (d.world != world || std::hypot(d.x - pp.x, d.y - pp.y) > kDockCallRange + 1500.0f) dockServed_ = -1;  // left it
				}
				return;
			}
			if (best == dockServed_) return;
			const auto& d = docks_[best];
			auto* tes = RE::TES::GetSingleton();
			auto* dcell = tes ? tes->GetCell(RE::NiPoint3{ d.x, d.y, d.z }) : nullptr;
			if (!dcell || !dcell->IsAttached()) return;  // the dock's own cell loaded first
			if (HaveBoat()) {
				if (std::hypot(pos_.x - d.x, pos_.y - d.y) < kDockAtSpot) { dockServed_ = best; return; }      // already there
				if (std::hypot(pos_.x - pp.x, pos_.y - pp.y) < kDockOwnBoatRange) { dockServed_ = best; return; }  // left nearby: kept
			}
			float water = 0.0f;
			const bool wet = WaterAt(RE::NiPoint3{ d.x, d.y, d.z + 100.0f }, water);
			const RE::NiPoint3 at{ d.x, d.y, wet ? water + kWaterZ + heightOff_ : d.z };
			paint_ = DockPaint(d.name);
			paintedRoot_ = nullptr;
			SpawnAt(at, d.yaw, "at a dock", d.name);
			if (HaveBoat()) {
				dockServed_ = best;
				logger::info("Docks: the boat is waiting at '{}' ({:.0f} from the player, facing {:.0f} deg, {})", d.name, bestD, d.yaw * 57.2958f, Paint::kNames[paint_]);
			}
		}
	public:

		// ---- v1.7.9: look-at prompts (OAS pick proxies). One ref shows one prompt, so each place gets its own invisible
		// activator from OarsAndSails.esl, culled, on a collision layer the crosshair ray hits and the player walks through:
		// the hull ("Board Rowboat": from the water, under it or beside it; you land standing on the floorboards), the
		// stern seat ("Steer Helm") and the middle thwart ("Row Oars"). They are there while you are not seated and within
		// 1500 of the boat; Board only while you are off the boat. Without OarsAndSails.esl, E works as before. ----
	public:
		enum PickKind : int { kPickBoard = 0, kPickSteer, kPickRow, kPickBedroll, kPickSupplies, kPickCount };
		// which of ours this is (any of our three activators near the boat, also one left from before a load), or -1
		int PickKindOf(const RE::TESObjectREFR* a_ref) const
		{
			if (!a_ref || (a_ref->GetFormID() >> 24) != 0xFF || !HaveBoat()) return -1;
			auto* base = a_ref->GetBaseObject();
			if (!base) return -1;
			for (int k = 0; k < kPickCount; ++k) if (picks_[k].id && picks_[k].id == a_ref->GetFormID()) return k;  // v1.7.23: bedroll and supplies share a base
			for (int k = 0; k < kPickCount; ++k) {
				if (picks_[k].base && base == picks_[k].base) {
					const RE::NiPoint3 p = a_ref->GetPosition();
					return std::hypot(p.x - pos_.x, p.y - pos_.y) < 1200.0f ? k : -1;
				}
			}
			return -1;
		}
		bool IsPickRef(RE::FormID a_id) const
		{
			for (const auto& pk : picks_) if (pk.id && pk.id == a_id) return true;
			return false;
		}
		RE::FormID LampLightId() const { auto l = lampLight_.get(); return l ? l->GetFormID() : 0; }
		bool PickedProxy() const
		{
			auto* pick = RE::CrosshairPickData::GetSingleton();
			if (!pick) return false;
			auto r = pick->GetActiveTarget().get();
			return r && PickKindOf(r.get()) >= 0;
		}
		// v1.7.30: the supplies themselves (not their prompt): close enough and standing to fish from here
		bool NearSupplies() const
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			RE::NiPoint3 sup{};
			if (!player || seated_ || !havePose_ || !Gear::SuppliesSpot(sup)) return false;
			const RE::NiPoint3 l = PlayerLocal(player);
			return std::hypot(l.x - sup.x, l.y - sup.y) < 120.0f;  // v1.7.49: 120 (90 lost it from the boat's other side)
		}
		bool LookingAtSupplies() const
		{
			if (!NearSupplies()) return false;
			auto* cam = RE::PlayerCamera::GetSingleton();
			RE::NiPoint3 sup{};
			if (!cam || !cam->cameraRoot || !Gear::SuppliesSpot(sup)) return false;
			const auto& cw = cam->cameraRoot->world;
			const RE::NiPoint3 at = lastAt_ + lastRot_ * RE::NiPoint3{ sup.x, sup.y, sup.z + 12.0f };  // v1.7.49: the tub's mouth
			const RE::NiPoint3 to = at - cw.translate;
			const float distance = to.Length();
			const RE::NiPoint3 forward = cw.rotate * RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
			// A small aiming cone around the supplies, and explicitly looking
			// down: a big invisible CC trigger is not proof of aiming at gear.
			// v1.7.49: a wider cone (25 deg, was 15: the tub is small and the third person camera sits off the head, so the
			// prompt came and went) and a gentler look down
			return distance > 1.0f && forward.z < -0.05f &&
				(to.x * forward.x + to.y * forward.y + to.z * forward.z) / distance > 0.9063f;
		}
		bool CanStartBoatFishing() const
		{
			if (BoatFishing::quitAfterMenu || BoatFishing::startHold > 0.0f || Gear::g_useIn >= 0.0f || !LookingAtSupplies()) return false;
			const auto s = BoatFishing::Read();
			// Fail closed if the CC script is not bound yet. Never start over
			// a cast, a catch result, or its still-running cleanup stack.
			return BoatFishing::Idle(s);
		}
		// E on one of them (the activator's own Activate, hooked)
		void OnPick(int a_kind)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (a_kind == kPickSupplies) {  // v1.7.23: the fishing supplies; v1.7.24: standing only
				if (!CanStartBoatFishing()) return;
				if (seated_ || !player || !havePose_) return;
				// the cast spot: on the water 240 off the side you face, abreast of you (as the CC's own markers, 100-250 out)
				const float hd = player->data.angle.z;
				const RE::NiPoint3 fwd{ std::sin(hd), std::cos(hd), 0.0f };
				const RE::NiPoint3 right = lastRot_ * RE::NiPoint3{ 1.0f, 0.0f, 0.0f };
				const float side = fwd.x * right.x + fwd.y * right.y >= 0.0f ? 1.0f : -1.0f;
				const float ly = std::clamp(PlayerLocal(player).y, -200.0f, 150.0f);
				RE::NiPoint3 spot = lastAt_ + lastRot_ * RE::NiPoint3{ side * 240.0f, ly, 0.0f };
				// v1.7.25: the marker sits on the boat's own water (the cell's water height there can be the world's
				// "no water" -14000: the marker went 14000 down and the fishing took you with it), and only on open water
				const float water = haveWater_ ? waterZ_ : pos_.z - kWaterZ - heightOff_;
				spot.z = water;
				const char* why = nullptr;
				auto* tes = RE::TES::GetSingleton();
				float land = 0.0f;
				if (tes && tes->GetLandHeight(spot, land) && std::isfinite(land) && land > water - 40.0f) why = land > water ? "land" : "too shallow";
				int layer = 0;
				if (!why && BoatSweep(RE::NiPoint3{ spot.x, spot.y, water + 300.0f }, RE::NiPoint3{ spot.x, spot.y, water + 4.0f }, layer)) why = "something in the way";
				// v1.7.27: and further out, where the line lands
				const RE::NiPoint3 farPt = lastAt_ + lastRot_ * RE::NiPoint3{ side * 450.0f, ly, 0.0f };
				if (!why && tes && tes->GetLandHeight(farPt, land) && std::isfinite(land) && land > water - 40.0f) why = land > water ? "land" : "too shallow";
				if (!why && BoatSweep(RE::NiPoint3{ farPt.x, farPt.y, water + 300.0f }, RE::NiPoint3{ farPt.x, farPt.y, water + 4.0f }, layer)) why = "something in the way";
				if (why) {
					logger::info("Pick: no fishing to the {} side: {} at ({:.0f},{:.0f}), water {:.0f}, land {:.0f}, layer {}", side > 0.0f ? "right" : "left", why, spot.x, spot.y, water, land, layer);
					RE::SendHUDMessage::ShowHUDMessage(std::format("No open water to fish off this side ({}). Face the other side, or take the boat out further.", why).c_str());
					return;
				}
				// v1.7.27: the marker is where the fisher STANDS (v1.7.26 log: the fishing moved you exactly onto it, the rod then
				// hung there in the air when you were put back): on the floorboards where you are, kept off the gunwale, facing
				// straight out over the side
				const float sx = std::clamp(PlayerLocal(player).x, -35.0f, 35.0f);
				const RE::NiPoint3 standL{ sx, ly, kDeckZ + 2.0f };
				const RE::NiPoint3 stand = lastAt_ + lastRot_ * standL;
				const RE::NiPoint3 outDir = right * side;
				Gear::SetFishSpot(stand, std::atan2(outDir.x, outDir.y));
				logger::info("Pick: E on {} (cast to the {} side)", picks_[a_kind].tag, side > 0.0f ? "right" : "left");
				if (!Gear::UseSuppliesSoon(0.05f)) return;
				BoatFishing::startHold = 2.0f;  // bridge asynchronous script startup; no double activation
				BoatFishing::watchAttempt = true;
				BoatFishing::sawActiveAttempt = false;
				fishWatch_ = 4.0f;  // v1.7.26
				fishLocal_ = standL;  // v1.7.27: put back on the marker, never away from it
				fishPrev_ = player->GetPosition();
				return;
			}
			if (!player || seated_ || !HaveBoat() || !havePose_) return;
			logger::info("Pick: E on {}", picks_[a_kind].tag);
			if (a_kind == kPickBoard) {
				// onto the floorboards between the thwarts, standing, facing the bow
				const RE::NiPoint3 at = lastAt_ + lastRot_ * RE::NiPoint3{ 0.0f, -40.0f, kDeckZ + 6.0f };
				for (const char* ev : { "swimStop", "SwimStop" }) player->NotifyAnimationGraph(ev);
				player->SetPosition(at, true);
				player->data.angle.z = yaw_ + (bowFlip_ ? kPi : 0.0f);
				if (auto* cc = player->GetCharController()) cc->SetLinearVelocityImpl(RE::hkVector4());
				return;
			}
			if (a_kind == kPickBedroll) {  // v1.7.11: unroll; v1.7.12: or pack it up again
				if (!Gear::BedToggleReady()) return;  // v1.7.47: one toggle at a time (quick repeats hung the game)
				if (Gear::BedrollOut()) {
					Gear::BedrollCommand("roll");
					RE::SendHUDMessage::ShowHUDMessage("Bedroll packed up");
				} else Gear::Unroll();
				return;
			}
			RequestSit(a_kind == kPickSteer ? SeatKind::kHelm : SeatKind::kRower);
		}

	private:
		struct PickProxy
		{
			RE::FormID localId;
			const char* tag;
			RE::TESObjectACTI* base = nullptr;
			RE::ObjectRefHandle h;
			RE::FormID id = 0;
			RE::NiPoint3 at{};
			float yaw = 0.0f;
			bool set = false;
			RE::NiPoint3 placedAt{};  // v1.7.50: where it was placed (moved since then by SetPosition)
			int check = 0;            // v1.7.50: ticks to the next check while the boat is still
		};
		PickProxy picks_[kPickCount] = { { 0x800, "Board (hull)" }, { 0x801, "Steer (stern seat)" }, { 0x802, "Row (middle thwart)" },
			{ 0x803, "Unroll (the bedroll under the bow thwart)" }, { 0x803, "Fish (the fishing supplies)" } };
		bool picksInit_{ false };
		bool picksOk_{ false };
		bool picksSweep_{ true };  // after a load: our prompt activators from the save are removed
		float pickTick_{ 0.0f };
		float benchYawOff_{ 0.0f }, benchScale_[kPickCount]{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
		const RE::NiAVObject* rollCulledOn_{ nullptr };  // v1.7.11: the boat model the rolled bedroll was last shown / hidden on
		bool rollCulled_{ false };
		// v1.7.48: our sleeping bag (hull node RowRim_Clutter_SleepingBag): on the bed's spot while the bed is laid out, hidden
		// otherwise; the game's bedroll mesh is hidden then (Gear::g_ownBag), its furniture kept for sleeping
		RE::NiAVObject* bagNode_{ nullptr };
		const RE::NiAVObject* bagOn_{ nullptr };
		// v1.7.52: the middle and aft oarlock forks (hull nodes RowRim_Oarlock_*, oarlocks.py) turn 90 deg about their pins
		// with the oars shipped along the gunwales (shipW_), so the shafts lie between the prongs
		RE::NiAVObject* locks_[4]{};
		const RE::NiAVObject* locksOn_{ nullptr };
		void TendOarlocks()
		{
			if (!visual_) { locksOn_ = nullptr; return; }
			if (locksOn_ != visual_.get()) {
				locksOn_ = visual_.get();
				int found = 0;
				const char* names[4] = { "RowRim_Oarlock_MidR", "RowRim_Oarlock_MidL", "RowRim_Oarlock_AftR", "RowRim_Oarlock_AftL" };
				for (int i = 0; i < 4; ++i) found += (locks_[i] = FindShape(visual_.get(), names[i])) != nullptr;
				logger::info("Oarlocks: {} of 4 turning oarlocks in the hull", found);
			}
			const float w = shipW_ * shipW_ * (3.0f - 2.0f * shipW_);
			for (int i = 0; i < 4; ++i) {
				auto* n = locks_[i];
				if (!n) continue;
				const RE::NiMatrix3 r = RzM(w * 1.5708f * (i % 2 == 0 ? 1.0f : -1.0f));
				bool same = true;
				for (int a = 0; a < 3 && same; ++a)
					for (int b = 0; b < 3; ++b)
						if (std::abs(n->local.rotate.entry[a][b] - r.entry[a][b]) > 1e-4f) { same = false; break; }
				if (same) continue;
				n->local.rotate = r;
				RE::NiUpdateData data{};
				data.flags.set(RE::NiUpdateData::Flag::kDirty);
				n->Update(data);
			}
		}
		// v1.7.52: standing in the boat the foot IK lifted a foot onto invisible collision (the prompt activators, the
		// hidden bed and supplies): foot IK off while you are aboard, back on when you step off (seated already had it off)
		bool footIkOff_{ false };
		void TendFootIK()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			const bool want = havePose_ && PlayerAboard();
			if (want) player->SetGraphVariableBool("bHumanoidFootIKDisable", true);
			else if (footIkOff_ && !seated_) player->SetGraphVariableBool("bHumanoidFootIKDisable", false);
			footIkOff_ = want;
		}
		// v1.7.52: slaughterfish and mudcrabs leave you, your followers, the dog and the crew alone while you are aboard (not
		// swimming): out of combat and unaggressive, their own aggression back once you are in the water or away
		std::unordered_map<RE::FormID, float> tamed_;
		float tameT_{ 0.0f };
		static bool IsFishOrCrab(RE::Actor* a)
		{
			auto has = [](const char* t, std::string_view w) {
				if (!t) return false;
				std::string s(t);
				std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return s.find(w) != std::string::npos;
			};
			auto* race = a ? a->GetRace() : nullptr;
			const char* rid = race ? race->formEditorID.c_str() : nullptr;
			const char* rn = race ? race->GetFullName() : nullptr;
			const char* bn = a && a->GetActorBase() ? a->GetActorBase()->GetName() : nullptr;
			for (auto w : { std::string_view("slaughterfish"), std::string_view("mudcrab") })
				if (has(rid, w) || has(rn, w) || has(bn, w)) return true;
			return false;
		}
	public:
		void UntameAll(const char* a_why)
		{
			int n = 0;
			for (auto& [id, aggr] : tamed_) {
				if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id); a && !a->IsDeleted()) {
					a->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, aggr);
					++n;
				}
			}
			if (n) logger::info("Calm: {} slaughterfish / mudcrab(s) as before ({})", n, a_why);
			tamed_.clear();
		}
	private:
		void TendTameFish(float a_dt)
		{
			if ((tameT_ -= a_dt) > 0.0f) return;
			tameT_ = 0.5f;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			auto* st = player->AsActorState();
			const bool swimming = st && st->IsSwimming();
			const bool calm = havePose_ && PlayerAboard() && !swimming;
			if (!calm) { if (!tamed_.empty()) UntameAll(swimming ? "you are in the water" : "you left the boat"); return; }
			auto* pl = RE::ProcessLists::GetSingleton();
			if (!pl) return;
			const RE::NiPoint3 pp = player->GetPosition();
			int fresh = 0;
			std::vector<RE::Actor*> fish;
			for (auto& h : pl->highActorHandles) {
				auto a = h.get();
				if (!a || a->IsDead() || !IsFishOrCrab(a.get()) || (a->GetPosition() - pp).Length() > 4000.0f) continue;
				fish.push_back(a.get());
				if (!tamed_.contains(a->GetFormID())) {
					tamed_[a->GetFormID()] = a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAggression);
					a->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, 0.0f);
					++fresh;
				}
				if (a->IsInCombat()) a->StopCombat();
			}
			if (fish.empty()) return;
			// whoever is fighting them on our side stops too (followers, the dog, the crew, the player)
			auto calmOne = [&](RE::Actor* a) {
				if (!a || !a->IsInCombat()) return;
				auto t = a->GetActorRuntimeData().currentCombatTarget.get();
				if (t && IsFishOrCrab(t.get())) a->StopCombat();
			};
			calmOne(player);
			for (auto& h : pl->highActorHandles) {
				auto a = h.get();
				if (a && (Crew::IsFollower(a.get()) || Crew::IsCrew(a.get()))) calmOne(a.get());
			}
			if (fresh) logger::info("Calm: {} slaughterfish / mudcrab(s) leave the boat alone ({} near)", fresh, fish.size());
		}

		float bagA_{ 0.0f };  // v1.7.49: the bed fades in when laid out and out when packed up (1.2 s)
		void TendSleepingBag(float a_dt)
		{
			if (!visual_) { bagNode_ = nullptr; bagOn_ = nullptr; Gear::g_ownBag = false; return; }
			if (bagOn_ != visual_.get()) {
				bagOn_ = visual_.get();
				bagNode_ = FindShape(visual_.get(), "RowRim_Clutter_SleepingBag");
				Gear::g_ownBag = bagNode_ != nullptr;
				logger::info("Bed: {}", bagNode_ ? "our sleeping bag found in the hull, the game's bedroll mesh hidden" : "no sleeping bag in the hull, the game's bedroll drawn");
			}
			if (!bagNode_) return;
			RE::NiPoint3 at{};
			float yawDeg = 0.0f, sc = 1.0f;
			const bool want = Gear::BedrollShown() && Gear::BedPose(at, yawDeg, sc);
			const float before = bagA_;
			bagA_ = std::clamp(bagA_ + (want ? 1.0f : -1.0f) * a_dt / 1.2f, 0.0f, 1.0f);
			if (bagA_ <= 0.0f) {
				if (before > 0.0f) SetAlphaTree(bagNode_, 1.0f);  // put back whole before it is hidden
				if (!bagNode_->GetAppCulled()) bagNode_->SetAppCulled(true);
				return;
			}
			if (bagNode_->GetAppCulled()) bagNode_->SetAppCulled(false);
			if (bagA_ != before || bagA_ < 1.0f) SetAlphaTree(bagNode_, bagA_ >= 1.0f ? 1.0f : bagA_ * bagA_ * (3.0f - 2.0f * bagA_));
			if (!want) return;  // fading out where it lay
			bagNode_->local.translate = at;
			bagNode_->local.rotate = RzM(yawDeg * 0.0174533f);
			bagNode_->local.scale = sc;
			RE::NiUpdateData data{};
			data.flags.set(RE::NiUpdateData::Flag::kDirty);
			bagNode_->Update(data);
		}
		RE::COL_LAYER pickLayer_{ RE::COL_LAYER::kUnidentified };
		bool pickLayerChosen_{ false };
		std::string benchModel_;

		static bool HasCollision(RE::NiAVObject* a_obj, int a_depth = 0)
		{
			if (!a_obj || a_depth > 12) return false;
			if (a_obj->collisionObject) return true;
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) if (HasCollision(child.get(), a_depth + 1)) return true;
			}
			return false;
		}
		// OAS PickLayer: hit by the crosshair ray (kItemPick), not by the character controller, preferably not the camera
		RE::COL_LAYER PickLayer()
		{
			if (pickLayerChosen_) return pickLayer_;
			pickLayerChosen_ = true;
			auto* filter = RE::bhkCollisionFilter::GetSingleton();
			if (!filter) return pickLayer_;
			auto hits = [&](RE::COL_LAYER a_a, RE::COL_LAYER a_b) {
				const auto a = static_cast<int>(a_a), b = static_cast<int>(a_b);
				return ((filter->layerBitfields[a] >> b) & 1) != 0 || ((filter->layerBitfields[b] >> a) & 1) != 0;
			};
			using L = RE::COL_LAYER;
			const L candidates[] = { L::kTransparentSmallAnim, L::kTransparentSmall, L::kDebrisSmall, L::kDebrisLarge, L::kWeapon, L::kClutter, L::kTransparent, L::kTrap };
			for (int pass = 0; pass < 2 && pickLayer_ == L::kUnidentified; ++pass) {
				for (L c : candidates) {
					if (hits(L::kItemPick, c) && !hits(L::kCharController, c) && (pass == 1 || !hits(L::kCameraSphere, c))) { pickLayer_ = c; break; }
				}
			}
			if (pickLayer_ == L::kUnidentified) {
				for (int i = 1; i < 54; ++i) {
					const L c = static_cast<L>(i);
					if (c == L::kItemPick || c == L::kCharController || c == L::kNonCollidable || c == L::kStatic || c == L::kAnimStatic || c == L::kTerrain ||
						c == L::kGround || c == L::kProps || c == L::kTrees)
						continue;
					if (hits(L::kItemPick, c) && !hits(L::kCharController, c) && !hits(L::kCameraSphere, c)) { pickLayer_ = c; break; }
				}
			}
			logger::info("Pick: proxies on collision layer {}", static_cast<int>(pickLayer_));
			return pickLayer_;
		}

		void InitPicks()
		{
			picksInit_ = true;
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) return;
			if (Legacy::g_oasLoaded) {  // the old dll owns these activators (it sets their models and sweeps them): stay off
				logger::info("Pick: no look-at prompts while OarsAndSails.dll is loaded");
				return;
			}
			for (auto& pk : picks_) pk.base = dh->LookupForm<RE::TESObjectACTI>(pk.localId, "OarsAndSails.esl");
			auto* stat = RE::TESForm::LookupByID<RE::TESObjectSTAT>(kRowBoatStatic);
			auto* bench = RE::TESForm::LookupByID<RE::TESBoundObject>(0x00041EC7);  // the OAS bench (has collision)
			auto* benchModel = bench ? bench->As<RE::TESModel>() : nullptr;
			picksOk_ = picks_[0].base && picks_[1].base && picks_[2].base && stat && stat->GetModel() && benchModel && benchModel->GetModel();
			if (!picksOk_) {
				logger::info("Pick: no look-at prompts (OarsAndSails.esl {} loaded, bench {}): E on the boat works as before",
					picks_[0].base ? "is" : "is NOT", benchModel ? "ok" : "missing");
				return;
			}
			picks_[kPickBoard].base->SetModel(stat->GetModel());
			picks_[kPickBoard].base->boundData = stat->boundData;
			benchModel_ = benchModel->GetModel();
			for (int k = kPickSteer; k <= kPickSupplies; ++k) {
				if (!picks_[k].base) continue;
				picks_[k].base->SetModel(benchModel_.c_str());
				picks_[k].base->boundData = bench->boundData;
			}
			// the bench lies across the boat, its long side sized to the seat (stern seat 70, the thwart 130)
			const auto& bd = bench->boundData;
			const float dx = static_cast<float>(bd.boundMax.x - bd.boundMin.x), dy = static_cast<float>(bd.boundMax.y - bd.boundMin.y);
			const float len = (std::max)(1.0f, (std::max)(dx, dy));
			benchYawOff_ = dx >= dy ? 0.0f : kPi * 0.5f;
			benchScale_[kPickBoard] = 1.04f;  // a hair outside the hull, so the crosshair finds it before the hull itself
			benchScale_[kPickSteer] = std::clamp(70.0f / len, 0.2f, 3.0f);
			benchScale_[kPickRow] = std::clamp(130.0f / len, 0.2f, 3.0f);
			benchScale_[kPickBedroll] = std::clamp(45.0f / len, 0.1f, 3.0f);  // v1.7.11: the roll is about 45 across
			benchScale_[kPickSupplies] = std::clamp(40.0f / len, 0.1f, 3.0f);  // v1.7.23: the supplies, about 40 across
			logger::info("Pick: prompts ready (hull '{}', seats '{}' {:.0f} x {:.0f}, scale {:.2f} / {:.2f})", stat->GetModel(), benchModel_, dx, dy,
				benchScale_[kPickSteer], benchScale_[kPickRow]);
		}

		void DropPick(PickProxy& a_p)
		{
			if (auto r = a_p.h.get()) {
				r->Disable();
				r->SetDelete(true);
			}
			a_p.h = {};
			a_p.id = 0;
			a_p.set = false;
		}
		void DropPicks()
		{
			for (auto& pk : picks_) DropPick(pk);
		}

		// v1.7.50: a proxy left behind: its 3D more than 8 from its spot
		static bool PickStale(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_at)
		{
			if (!a_ref) return false;
			auto* root = a_ref->Get3D();
			return root && (root->world.translate - a_at).Length() > 8.0f;
		}
		void TendPick(PickProxy& a_p, int a_kind, bool a_want, const RE::NiPoint3& a_at, float a_yaw)
		{
			auto ref = a_p.h.get();
			if (ref && ref->IsDeleted()) { a_p.h = {}; a_p.id = 0; ref = nullptr; }
			if (!a_want) { if (ref) DropPick(a_p); return; }
			if (ref && (std::hypot(a_at.x - a_p.at.x, a_at.y - a_p.at.y) > 4.0f || std::abs(a_at.z - a_p.at.z) > 6.0f || std::abs(a_yaw - a_p.yaw) > 0.03f)) {
				// v1.7.49: the boat moved: the proxy moved with it (it was deleted and placed again: no prompt until the new
				// one's 3D came in, so the [E] went missing whenever the boat drifted or bobbed)
				if (std::hypot(a_at.x - a_p.at.x, a_at.y - a_p.at.y) > 600.0f) { DropPick(a_p); ref = nullptr; }
				// v1.7.50: once the boat is still, a proxy that was carried more than 30 from where it was placed, or whose 3D
				// is not where it should be, is placed fresh: carried along by
				// SetPosition its collision could stay behind (the [E] found somewhere else in the boat after rowing)
				else if (std::abs(speed_) < 5.0f && ((a_at - a_p.placedAt).Length() > 30.0f || PickStale(ref.get(), a_at))) {
					DropPick(a_p);
					ref = nullptr;
				}
				else {
					ref->SetPosition(a_at);
					ref->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, a_yaw });
					a_p.at = a_at;
					a_p.yaw = a_yaw;
				}
			}
			if (!ref) {
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player || !player->GetParentCell() || !player->GetParentCell()->IsAttached()) return;
				auto placed = player->PlaceObjectAtMe(a_p.base, false);
				if (!placed) return;
				placed->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::clamp(benchScale_[a_kind], 0.01f, 10.0f) * 100.0f + 0.5f);
				placed->SetPosition(a_at);
				placed->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, a_yaw });
				a_p.h = placed->GetHandle();
				a_p.id = placed->GetFormID();
				a_p.at = a_at;
				a_p.placedAt = a_at;
				a_p.yaw = a_yaw;
				a_p.set = false;
				return;
			}
			if (ref && a_p.set && std::abs(speed_) < 5.0f && --a_p.check <= 0) {  // v1.7.50: still: checked once a second
				a_p.check = 10;
				if ((a_at - a_p.placedAt).Length() > 30.0f || PickStale(ref.get(), a_at)) { DropPick(a_p); return; }
			}
			if (a_p.set) return;
			auto* root = ref->Get3D();
			if (!root) return;
			a_p.set = true;
			root->SetAppCulled(true);  // never drawn: only something to look at
			if (!HasCollision(root)) logger::warn("Pick: {} proxy has no collision: the crosshair can't find it", a_p.tag);
			if (const auto layer = PickLayer(); layer != RE::COL_LAYER::kUnidentified) root->SetCollisionLayer(layer);
			logger::info("Pick: {} {:08X} ready at ({:.0f},{:.0f},{:.0f}), layer {}", a_p.tag, ref->GetFormID(), a_at.x, a_at.y, a_at.z, static_cast<int>(root->GetCollisionLayer()));
		}

		// 10 times a second
		void TendPicks(float a_dt)
		{
			if ((pickTick_ -= a_dt) > 0.0f) return;
			pickTick_ = 0.1f;
			if (!picksInit_) InitPicks();
			if (!picksOk_) return;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* hull = Ref();
			const bool nearBoat = player && hull && havePose_ && std::hypot(player->GetPosition().x - pos_.x, player->GetPosition().y - pos_.y) < 1500.0f;
			if (!nearBoat) {
				for (auto& pk : picks_) if (pk.h) DropPick(pk);
				return;
			}
			if (seated_) {  // v1.7.24: no prompts while seated (no fishing from the seat)
				for (auto& pk : picks_) if (pk.h) DropPick(pk);
				return;
			}
			// v1.7.23: the fishing supplies' prompt
			const float by = yaw_ + benchYawOff_;
			RE::NiPoint3 sup{};
			bool supOut = picks_[kPickSupplies].base && Gear::SuppliesSpot(sup);
			auto nearL = [&](const RE::NiPoint3& a_l) {  // v1.7.28: the gear prompts only within reach (90 across the deck)
				const RE::NiPoint3 l = PlayerLocal(player);
				return std::hypot(l.x - a_l.x, l.y - a_l.y) < 120.0f;  // v1.7.49: 120
			};
			supOut = supOut && nearL(sup);
			TendPick(picks_[kPickSupplies], kPickSupplies, supOut, lastAt_ + lastRot_ * RE::NiPoint3{ sup.x, sup.y, sup.z + 6.0f }, by);
			if (picksSweep_) {
				picksSweep_ = false;
				int n = 0;
				if (auto* tes = RE::TES::GetSingleton()) {
					tes->ForEachReferenceInRange(player, 3000.0f, [&](RE::TESObjectREFR* a_ref) {
						if (a_ref && (a_ref->GetFormID() >> 24) == 0xFF && !a_ref->IsDeleted() && !IsPickRef(a_ref->GetFormID())) {
							auto* b = a_ref->GetBaseObject();
							for (const auto& pk : picks_) {
								if (b && b == pk.base) { a_ref->Disable(); a_ref->SetDelete(true); ++n; break; }
							}
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
				}
				if (n) logger::info("Pick: {} prompt activator(s) from the save removed", n);
			}
			const float hy = hull->GetAngle().z;
			TendPick(picks_[kPickBoard], kPickBoard, !PlayerAboard(), hull->GetPosition(), hy);
			TendPick(picks_[kPickSteer], kPickSteer, true, lastAt_ + lastRot_ * RE::NiPoint3{ seat_.x, seat_.y, seat_.z + 22.0f }, by);
			TendPick(picks_[kPickRow], kPickRow, true, lastAt_ + lastRot_ * RE::NiPoint3{ rowSeat_.x, rowSeat_.y, rowSeat_.z + 24.0f }, by);
			if (picks_[kPickBedroll].base)  // v1.7.11: the roll tied under the bow thwart (model 0, 113.7, about 56)
				TendPick(picks_[kPickBedroll], kPickBedroll, nearL(RE::NiPoint3{ 0.0f, 113.7f, 0.0f }), lastAt_ + lastRot_ * RE::NiPoint3{ 0.0f, 113.7f, 50.0f }, by);  // v1.7.12: also to pack it up
		}
	public:

		void Delete(bool a_say)
		{
			if (seated_) StandUp("hull deleted");
			if (Crew::Lingering()) Crew::StandAll(Crew::g.pose);  // v1.7.28: still seated, waiting: up
			DropPicks();  // v1.7.9
			DropHeld();
			DropLamp();
			DropBedLamp();  // v1.7.34
			Gear::RemoveAll("the boat deleted");  // v1.7.38
			DeleteCarrier();
			if (auto* ref = Ref()) {
				RemoveMobile(ref);
				ref->Disable();
				ref->SetDelete(true);
				if (a_say) Print(std::format("RowRim: test hull {:08X} deleted", refId_));
			} else if (a_say) {
				Print("RowRim: no test hull");
			}
			refId_ = 0;
			body_ = nullptr;
			needInit_ = false;
		}

		// ---- co-save (v0.3.4): which hull and carrier are ours, the boat pose and the seat ----
		static constexpr std::uint32_t kRecHull = 'HULL';
		static constexpr std::uint32_t kRecVersion = 3;  // v0.6.0: both seats; v1.1.0: paint and height
		void Save(SKSE::SerializationInterface* a_intfc)
		{
			if (!refId_ || !Ref()) return;
			if (!a_intfc->OpenRecord(kRecHull, kRecVersion)) return;
			const std::uint32_t flags = (bowFlip_ ? 1u : 0u) | (carrierOn_ ? 2u : 0u);
			a_intfc->WriteRecordData(refId_);
			a_intfc->WriteRecordData(carrierId_);
			for (float f : { pos_.x, pos_.y, pos_.z, yaw_, seat_.x, seat_.y, seat_.z, rowSeat_.x, rowSeat_.y, rowSeat_.z }) a_intfc->WriteRecordData(f);
			a_intfc->WriteRecordData(flags);
			a_intfc->WriteRecordData(static_cast<std::int32_t>(paint_));
			a_intfc->WriteRecordData(heightOff_);
			logger::info("Co-save: hull {:08X} carrier {:08X} saved", refId_, carrierId_);
		}
		// v1.3.0: one record (the co-save loop is LoadAll, which also reads the lake boats' records)
		void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t type, std::uint32_t version)
		{
			{
				if (type != kRecHull || version < 1 || version > 3) return;
				RE::FormID hull = 0, carrier = 0;
				float v[10]{};
				std::uint32_t flags = 0;
				a_intfc->ReadRecordData(hull);
				a_intfc->ReadRecordData(carrier);
				const int nf = version >= 2 ? 10 : 7;
				for (int i = 0; i < nf; ++i) a_intfc->ReadRecordData(v[i]);
				a_intfc->ReadRecordData(flags);
				if (version >= 3) {
					std::int32_t pnt = Paint::kBlue;
					a_intfc->ReadRecordData(pnt);
					a_intfc->ReadRecordData(heightOff_);
					paint_ = std::clamp(static_cast<int>(pnt), 0, Paint::kCount - 1);
				}
				paintedRoot_ = nullptr;
				RE::FormID h = 0, c = 0;
				const bool okH = hull && a_intfc->ResolveFormID(hull, h);
				const bool okC = carrier && a_intfc->ResolveFormID(carrier, c);
				refId_ = okH ? h : 0;
				carrierId_ = okC ? c : 0;
				CarrierAudio::id.store(carrierId_, std::memory_order_relaxed);
				pos_ = { v[0], v[1], v[2] };
				yaw_ = v[3];
				if (version >= 2) {
					seat_ = { v[4], v[5], v[6] };
					rowSeat_ = { v[7], v[8], v[9] };
					// v1.4.0: the old default oar seat (0 0 33, rowed at +15) moves to where the clip sits
					if (std::abs(rowSeat_.x) < 0.01f && std::abs(rowSeat_.y) < 0.01f && std::abs(rowSeat_.z - 33.0f) < 0.01f) rowSeat_.y = 15.0f;
				}
				bowFlip_ = (flags & 1u) != 0;
				carrierOn_ = (flags & 2u) != 0;
				needInit_ = refId_ != 0;
				initTries_ = 0;
				havePose_ = false;
				inits_ = 0;
				root_ = nullptr;
				body_ = nullptr;
				visual_.reset();
			wbShown_ = nullptr;
				aiApplied_ = false;
				speed_ = turn_ = 0.0f;
				logger::info("Co-save: hull {:08X} ({}) carrier {:08X} ({}) restored at {:.0f} {:.0f} {:.0f}", refId_, okH ? "ok" : "gone", carrierId_, okC ? "ok" : "gone",
					pos_.x, pos_.y, pos_.z);
			}
		}

		void Forget()
		{
			BoatFishing::startHold = 0.0f;
			BoatFishing::quitAfterMenu = false;
			BoatFishing::watchAttempt = BoatFishing::sawActiveAttempt = false;
			for (auto& pk : picks_) { pk.h = {}; pk.id = 0; pk.set = false; }  // v1.7.9: the saved ones are swept (TendPicks)
			picksSweep_ = true;
			dockWait_ = 6.0f;  // v1.7.8: the save's own boat is restored first
			dockServed_ = -1;
			walkFix_ = 3.0f;  // v1.2.3: a save made after a broken stand-up kept bAllowRotation on: put right after every load
			loadUnstick_ = 1.5f;  // v1.7.41
			loadUnstickPass_ = 0;
			if (seated_) StandUp("game loading");
			heldOld_.reset();  // the scene is torn down by the load
			lamp_.reset();
			lampLight_.reset();  // purged with the lake boat parts after the load, a new one is placed
			bedLight_.reset();   // v1.7.34
			bedLamp_.reset();
			tentOn_ = nullptr;
			tentNode_ = nullptr;
			tentA_ = 0.0f;
			lamp_st_ = LampSwing{};
			visual_.reset();
			wbShown_ = nullptr;
			carrierId_ = 0;
			CarrierAudio::id.store(0, std::memory_order_relaxed);
			helm_ = false;
			refId_ = 0;
			body_ = nullptr;
			needInit_ = false;
		}

		void SetRock(float a_rollDeg)
		{
			rockRoll_ = a_rollDeg * kPi / 180.0f;
			rockPitch_ = rockRoll_ / 3.0f;
			rockHeave_ = a_rollDeg > 0.0f ? 6.0f : 0.0f;
			Print(std::format("RowRim: rock roll {:.1f} deg, pitch {:.1f} deg, heave {:.0f}, period {:.1f} s", a_rollDeg, a_rollDeg / 3.0f, rockHeave_, period_));
		}
		void SetPeriod(float a_s) { period_ = std::clamp(a_s, 0.5f, 30.0f); Print(std::format("RowRim: rock period {:.1f} s", period_)); }
		void SetSpeed(float a_u) { speed_ = a_u; Print(std::format("RowRim: speed {:.0f} u/s", speed_)); }
		void SetTurn(float a_deg) { turn_ = a_deg * kPi / 180.0f; Print(std::format("RowRim: turn {:.0f} deg/s", a_deg)); }
		void Nudge(float a_dz) { pos_.z += a_dz; Print(std::format("RowRim: hull z {:.0f} ({:+.0f})", pos_.z, a_dz)); }
		void SetMode(Mode a_m)
		{
			mode_ = a_m;
			if (body_ && a_m != Mode::kDrive) {
				body_->SetLinearVelocity(Hk(0, 0, 0));
				body_->SetAngularVelocity(Hk(0, 0, 0));
			}
			Print(std::format("RowRim: mode {}", ModeName(mode_)));
		}
		void SetNode(bool a_on) { writeNode_ = a_on; Print(std::format("RowRim: node write {}", a_on ? "on" : "off (the engine draws the hull where the body is, or not at all)")); }
		void SetLayer(RE::COL_LAYER a_layer)
		{
			layer_ = a_layer;
			Print(std::format("RowRim: layer {} for the next spawn", a_layer == RE::COL_LAYER::kAnimStatic ? "AnimStatic" : "Static"));
		}
		void Flip() { flip_ = !flip_; Print(std::format("RowRim: quaternion convention {} (auto picked {})", flip_ ? "conjugate" : "direct", autoFlip_ ? "conjugate" : "direct")); }
		// ---- helm: sit on the seat, steer with WASD ----
		void OnMove(int a_which, bool a_down)
		{
			switch (a_which) {
			case 0: inFwd_ = a_down; break;
			case 1: inBack_ = a_down; break;
			case 2: inLeft_ = a_down; break;
			case 3: inRight_ = a_down; break;
			default: break;
			}
		}
		void OnStick(float a_x, float a_y)
		{
			stickX_ = a_x;
			stickY_ = a_y;
		}
		// v1.7.22: no talking to anyone while seated, nor for a second after E sat you down or stood you up (the same E press
		// also reached the game and opened a passenger's dialogue)
		static double NowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
		void HoldTalk() { noTalkUntil_ = NowS() + 1.0; }
		bool NoTalk() const { return seated_ || NowS() < noTalkUntil_; }
		bool Seated() const { return seated_; }
		// v1.7.42 (OAS did the same): stood up before a save is written and when the map opens (no fast travel or saves
		// made in the seat)
		void StandFor(const char* a_why)
		{
			if (!seated_) return;
			StandUp(a_why);
		}
		// E: stand up when seated; sit when standing on (or right by) the hull. Returns true if it acted.
		bool OnActivate()
		{
			if (seated_) { StandUp("E"); return true; }  // v1.7.24: never fishing from the seat
			if (picksOk_) return false;  // v1.7.23: standing, only the seat prompts sit you down (E on the hull is the game's: picking up what you dropped)
			if (!Ref() || !body_) return false;
			if (!PlayerAboard()) return false;
			if (Gear::Targeted()) return false;  // v1.7.0: E is for the fishing supplies under the crosshair
			if (PickedProxy()) return false;     // v1.7.9: the prompt's own activation does it
			RequestSit(NearestSeat());
			return true;
		}
		double noTalkUntil_{ 0.0 };
		void ToggleSeat()
		{
			if (seated_) { StandUp("oas sit"); return; }
			if (!Ref() || !body_) { Print("RowRim: no test hull (oas spawn)"); return; }
			if (!PlayerAboard()) { Print("RowRim: walk onto the boat first"); return; }
			SitDown(NearestSeat());
		}
		void SeatHere()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !Ref()) { Print("RowRim: no test hull"); return; }
			seat_ = PlayerLocal(player);
			Print(std::format("RowRim: seat set to where you stand: {:.0f} {:.0f} {:.0f} (oas \"seat x y z\" to type it)", seat_.x, seat_.y, seat_.z));
		}
		void SetSeat(float a_x, float a_y, float a_z)
		{
			seat_ = { a_x, a_y, a_z };
			Print(std::format("RowRim: seat {:.0f} {:.0f} {:.0f}", seat_.x, seat_.y, seat_.z));
		}
		void RowSeatHere()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !Ref()) { Print("RowRim: no test hull"); return; }
			rowSeat_ = PlayerLocal(player);
			Print(std::format("RowRim: rower seat set to where you stand: {:.0f} {:.0f} {:.0f}", rowSeat_.x, rowSeat_.y, rowSeat_.z));
		}
		void SetRowSeat(float a_x, float a_y, float a_z)
		{
			rowSeat_ = { a_x, a_y, a_z };
			Print(std::format("RowRim: rower seat {:.0f} {:.0f} {:.0f} (helm seat {:.0f} {:.0f} {:.0f})", rowSeat_.x, rowSeat_.y, rowSeat_.z, seat_.x, seat_.y, seat_.z));
		}
		void SetRowSpeed(float a_max) { rowMax_ = std::clamp(a_max, 50.0f, 700.0f); SaveRowSettings(); Print(std::format("RowRim: row top speed {:.0f} (default 300, saved)", rowMax_)); }
		void SetRowDrag(float a_d) { rowDrag_ = std::clamp(a_d, 0.01f, 0.8f); SaveRowSettings(); Print(std::format("RowRim: row glide drag {:.2f} (default 0.07, lower glides further; saved)", rowDrag_)); }
		// v1.7.8: turning, as OAS (oas "row turn 0.45": rad/s at speed), kept as a scale of the default
		void SetRowTurn(float a_radS)
		{
			turnScale_ = std::clamp(a_radS / 0.45f, 0.3f, 3.0f);
			SaveRowSettings();
			Print(std::format("RowRim: turning {:.2f} (default 0.45, x{:.2f}; saved)", 0.45f * turnScale_, turnScale_));
		}
		// v1.7.8: the rowing feel in RowRim.ini [Row] (MaxSpeed, Glide, Turn: the OAS keys). With no [Row] there yet, an
		// Oars and Sails install's own OarsAndSails.ini [Row] is taken over once.
		void LoadRowSettings()
		{
			rowLoaded_ = true;
			const auto ini = Plankton::IniPath();
			char buf[32]{};
			std::string from = "RowRim.ini";
			std::string path = ini;
			if (GetPrivateProfileStringA("Row", "MaxSpeed", "", buf, sizeof(buf), ini.c_str()) == 0) {
				std::error_code ec;
				const auto oas = (std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OarsAndSails.ini").string();
				if (GetPrivateProfileStringA("Row", "MaxSpeed", "", buf, sizeof(buf), oas.c_str()) > 0) { path = oas; from = "OarsAndSails.ini (taken over)"; }
				else { SaveRowSettings(); return; }  // defaults written so the keys are there to edit
			}
			auto f = [&](const char* a_key, float a_def) {
				char b[32]{};
				GetPrivateProfileStringA("Row", a_key, "", b, sizeof(b), path.c_str());
				try { return b[0] ? std::stof(b) : a_def; } catch (...) { return a_def; }
			};
			rowMax_ = std::clamp(f("MaxSpeed", 300.0f), 50.0f, 700.0f);
			rowDrag_ = std::clamp(f("Glide", 0.07f), 0.01f, 0.8f);
			turnScale_ = std::clamp(f("Turn", 1.0f), 0.3f, 3.0f);
			if (path != ini) SaveRowSettings();
			logger::info("Row: top speed {:.0f}, glide {:.2f}, turning x{:.2f} from {}", rowMax_, rowDrag_, turnScale_, from);
		}
		void SaveRowSettings()
		{
			const auto ini = Plankton::IniPath();
			WritePrivateProfileStringA("Row", "MaxSpeed", std::format("{:.0f}", rowMax_).c_str(), ini.c_str());
			WritePrivateProfileStringA("Row", "Glide", std::format("{:.3f}", rowDrag_).c_str(), ini.c_str());
			WritePrivateProfileStringA("Row", "Turn", std::format("{:.2f}", turnScale_).c_str(), ini.c_str());
		}
		void ResetRowSettings()
		{
			rowMax_ = 300.0f;
			rowDrag_ = 0.07f;
			turnScale_ = 1.0f;
			SaveRowSettings();
		}
		void SetOarRoll(float a_deg) { oarRoll_ = a_deg * kPi / 180.0f; Print(std::format("RowRim: rowing oar roll {:.0f} deg (OAS default 90)", a_deg)); }
		void SetOarLock(float a_x, float a_y, float a_z)
		{
			lockLocal_ = { std::abs(a_x), a_y, a_z };
			Print(std::format("RowRim: oarlock (model space, mirrored) {:.1f} {:.1f} {:.1f}", lockLocal_.x, lockLocal_.y, lockLocal_.z));
		}
		void SetOarStow(float a_x, float a_y, float a_z, float a_yawDeg)
		{
			stowAt_ = { std::abs(a_x), a_y, a_z };
			stowYaw_ = a_yawDeg * kPi / 180.0f;
			Print(std::format("RowRim: stowed oars at {:.0f} {:.0f} {:.1f} (model space, mirrored), turn {:.1f} deg", stowAt_.x, stowAt_.y, stowAt_.z, a_yawDeg));
		}
		void SetIdleRock(float a_amount) { idleBob_ = std::clamp(a_amount, 0.0f, 3.0f); Print(std::format("RowRim: idle rocking x{:.1f} (OAS default 1)", idleBob_)); }
		void SetLead(float a_k) { drawLead_ = std::clamp(a_k, 0.0f, 1.0f); Print(std::format("RowRim: seated boat drawn {:.2f} of the way to this frame's pose (0 = OAS, a frame behind; 1 = this frame)", drawLead_)); }
		void SetDrawMode(const std::string& a_m)
		{
			drawMode_ = a_m == "now" ? 1 : a_m == "player" ? 2 : a_m == "pin" ? 3 : 0;
			Print(std::format("RowRim: seated boat drawn {}", drawMode_ == 0 ? "one frame behind, seated people moved with it (OAS, default)" : drawMode_ == 1 ? "at this frame's seat pose" : drawMode_ == 2 ? "from the drawn player" : "at this frame's pose, rower pinned to the seat (oas \"draw pin\")"));
		}
		void SetRowShift(float a_s) { rowShift_ = std::clamp(a_s, -40.0f, 60.0f); Print(std::format("RowRim: while rowing the rower sits {:.0f} toward the bow (+) / stern (-) of the seat, the oars stay in the oarlocks", rowShift_)); }
		void SetOarRest(float a_in, float a_dip) { restIn_ = std::clamp(a_in, 0.0f, 0.6f); restDip_ = std::clamp(a_dip, -20.0f, 45.0f); Print(std::format("RowRim: resting oars {:.0f}% inboard of the oarlock, blade {:.0f} deg down", restIn_ * 100.0f, restDip_)); }
		void SetHandLock(bool a_on) { handLock_ = a_on; Print(std::format("RowRim: oar hand lock {}", a_on ? "on" : "off (the clip's own oar pose)")); }

		// OAS 28 / 56: the row clip turns AnimObjectA; ThirdPersonState follows that bone into animationRotation and the orbit
		// camera turns over with it. While the player rows (and up to 3 s after) the field is put back to identity.
		void ResetAnimRotation(RE::ThirdPersonState* a_tps)
		{
			if (!a_tps || !(rowAnimOn_ || rideTime_ < animRotUntil_)) return;
			if (std::abs(a_tps->animationRotation.w) > 0.9999f) return;
			a_tps->animationRotation = RE::NiQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
		}

		// After the animation update (camera hook): the oars. One set, on the boat model. Parked or seated they lie stowed in the
		// boat (OAS 50); once the row clip poses the rower's AnimObject bones (third person) they blend from the stowed pose into
		// the clip's, locked to the hands and the oarlocks, over 0.8 s (OAS 56.13 / 56.18), and back the same way after the clip.
		void TendOars(float a_dt)
		{
			if (!oarsRunLogged_) {
				oarsRunLogged_ = true;
				logger::info("Oars: tending (visual {}, oars {} {})", visual_ ? "yes" : "no", oars_[0] ? "R" : "-", oars_[1] ? "L" : "-");
			}
			if (!visual_) return;
			auto* player = RE::PlayerCharacter::GetSingleton();
			// v1.7.9: the oars follow whoever rows: the player, or the crew rower with the player at the stern
			auto* rower = RowingActor();
			auto* root = rower ? rower->Get3D(false) : nullptr;
			const bool playerRows = rower && rower == player;
			const bool want = rowAnimOn_ && seated_ && rower && rowAnimActor_.get().get() == rower && root;
			RE::NiAVObject* bones[2]{};
			bool posed = false;
			float away[2]{};
			if (want) {
				auto* npc = root->GetObjectByName("NPC Root [Root]");
				const RE::NiPoint3 c = npc ? npc->world.translate : root->world.translate;
				posed = rideTime_ - rowAnimOnAt_ >= 0.3f;  // the graph's blend from the sit pose
				for (int i = 0; i < 2; ++i) {
					bones[i] = root->GetObjectByName(Oar::kBone[i]);
					away[i] = bones[i] ? (bones[i]->world.translate - c).Length() : 0.0f;
					if (!bones[i] || away[i] < 20.0f) posed = false;  // the bind pose: the clip is not driving them (yet) (OAS 56.19: 20)
				}
				// v0.8.1: once per row start, a second in: why the oars are (not) following the clip
				if (!posedLogged_ && rideTime_ - rowAnimOnAt_ >= 1.0f) {
					posedLogged_ = true;
					auto* npcLog = root->GetObjectByName("NPC Root [Root]");
					logger::info("Oars: 1 s into the clip: bones A {} ({:.0f} from NPC Root) B {} ({:.0f}), NPC Root {}, hands {} {}, first person {} -> {}",
						bones[0] ? "found" : "MISSING", away[0], bones[1] ? "found" : "MISSING", away[1], npcLog ? "found" : "MISSING",
						root->GetObjectByName(Oar::kHand[0]) ? "L" : "-", root->GetObjectByName(Oar::kHand[1]) ? "R" : "-",
						RE::PlayerCamera::GetSingleton() && RE::PlayerCamera::GetSingleton()->IsInFirstPerson(), posed ? "following the clip" : "NOT posed, oars stay stowed");
				}
			} else {
				posedLogged_ = false;
			}
			auto* cam = RE::PlayerCamera::GetSingleton();
			const bool firstPerson = cam && cam->IsInFirstPerson();
			// v0.8.1: the oars hang on the boat model, so they draw in first person too; the third person skeleton they follow
			// is still animated there (it casts the shadow)
			const bool toClip = want && posed;
			if (toClip != oarsToClip_) {
				oarsToClip_ = toClip;
				logger::info("Oars: {} (bones A {:.0f} B {:.0f} from NPC Root{})", toClip ? "stowed -> the rower's hands (clip)" : "-> stowed", away[0], away[1],
					firstPerson ? ", first person" : "");
				oarLogs_ = 0;
			}
			oarW_ = std::clamp(oarW_ + (toClip ? 1.0f : -1.0f) * a_dt / kOarBlend, 0.0f, 1.0f);
			// v1.2.3: with the rower pinned to the seat before the camera (PinRower), the hands are read in the frame that puts
			// his root on the seat, so the oars stay in his hands however far his 3D was off the boat this frame
			RE::NiTransform boatW = visual_->world;
			if (want && playerRows && drawMode_ == 3 && havePose_) {
				boatW.rotate = lastRot_;
				boatW.translate = root->world.translate - lastRot_ * SeatPos();
			}
			const RE::NiTransform boatInv = boatW.Invert();
			if (toClip) {
				// the clip's two oars, then each paired with the stowed oar on its side (the rower faces aft: A, his left, is the
				// boat's right)
				for (int i = 0; i < 2; ++i) {
					auto* bone = bones[i];
					const RE::NiTransform clip = bone->world * Oar::BoneTune(i, oarRoll_);
					const RE::NiPoint3 mid = boatInv * (clip * Oar::g_b.centre);
					const int k = mid.x >= 0.0f ? 0 : 1;
					const float side = k == 0 ? 1.0f : -1.0f;
					const RE::NiPoint3 lock = boatW * RE::NiPoint3{ side * lockLocal_.x, lockLocal_.y, lockLocal_.z };
					RE::NiTransform drawn = clip;
					bool palmOk = false, capped = false;
					auto* hand = handLock_ ? root->GetObjectByName(Oar::kHand[i]) : nullptr;
					if (hand) {
						const RE::NiPoint3 grip = Oar::PalmGrip(root, i, hand, 0.0f, 7.0f, 4.2f, i == 0 ? -1.0f : 1.0f, palmOk);
						// v1.2.4: the grip all the way onto the hand (was 30 at most: always capped, the hands showed off the
						// handles). The shaft runs hand -> oarlock, so it stays in the oarlock wherever the rower sits.
						float moved = 0.0f;
						drawn = Oar::HandLock(clip, grip, lock, 12.0f, capped, 120.0f, &moved);
						handMoveMax_ = (std::max)(handMoveMax_, moved);
						const RE::NiPoint3 gl = boatInv * grip;
						handLo_[k] = (std::min)(handLo_[k], gl.y);
						handHi_[k] = (std::max)(handHi_[k], gl.y);
					}
					clipLocal_[k] = boatInv * drawn;  // held in the boat frame for the blend back
					haveClip_[k] = true;
					if (oarLogs_ < 4) {
						++oarLogs_;
						RE::NiPoint3 h, d;
						Oar::HandleDir(drawn, h, d);
						const RE::NiPoint3 hl = boatInv * h;
						logger::info("Oars: {} -> the boat's {} oar, handle at boat ({:.0f},{:.0f},{:.0f}), hand lock {}{}{}", i == 0 ? "A" : "B", k == 0 ? "right" : "left",
							hl.x, hl.y, hl.z, hand ? "on" : "off", hand && !palmOk ? " (finger nodes missing: hand node)" : "", capped ? " (move capped at 120)" : "");
					}
				}
				if (++handFrames_ >= 180) {  // every 3 s of rowing: where the hands travel (boat Y) against the oarlocks
					logger::info("Oars: hands over 3 s, boat Y right {:.0f}..{:.0f} left {:.0f}..{:.0f} (oarlocks at {:.0f}), grip moved onto the hand up to {:.0f}, rower shift {:.0f}",
						handLo_[0], handHi_[0], handLo_[1], handHi_[1], lockLocal_.y, handMoveMax_, rowShift_);
					handFrames_ = 0;
					handMoveMax_ = 0.0f;
					handLo_[0] = handLo_[1] = 1e9f;
					handHi_[0] = handHi_[1] = -1e9f;
				}
			}
			const float w = oarW_ * oarW_ * (3.0f - 2.0f * oarW_);
			stowW_ = std::clamp(stowW_ + (oarsStowed_ ? 1.0f : -1.0f) * a_dt / kOarBlend, 0.0f, 1.0f);  // swings out over the blend time
			const float sw = stowW_ * stowW_ * (3.0f - 2.0f * stowW_);
			shipW_ = std::clamp(shipW_ + ((Gear::BedrollShown() && !seated_) ? 1.0f : -1.0f) * a_dt / kOarBlend, 0.0f, 1.0f);  // v1.7.33
			const float shS = shipW_ * shipW_ * (3.0f - 2.0f * shipW_);
			for (int k = 0; k < 2; ++k) {
				auto& oar = oars_[k];
				if (!oar) continue;
				const float sd = k == 0 ? 1.0f : -1.0f;
				const RE::NiTransform stow = Oar::StowLocal(sd, stowAt_, stowYaw_, kStowScale);
				const RE::NiTransform rest = Oar::RestLocal(sd, lockLocal_, restIn_, restDip_);
				const RE::NiTransform base0 = sw >= 1.0f ? stow : sw <= 0.0f ? rest : Oar::Blend(rest, stow, sw);  // v1.2.4
				// v1.7.33: the bed out (you are not seated): the oars lie along the gunwales in the middle and aft oarlocks
				RE::NiTransform ship = Oar::ShipLocal(sd, lockLocal_, kAftLock, 30.0f);
				// v1.7.36: the whole oar must lie aft of the tent (the 11:44 shot: a blade through the tarp, so the oar lay
				// forward): if either end reaches past y 40, the oar is turned end for end
				{
					const RE::NiPoint3 ax = Oar::Axis();
					const RE::NiPoint3 e1 = ship * (Oar::g_b.centre + ax * Oar::g_b.halfLen);
					const RE::NiPoint3 e2 = ship * (Oar::g_b.centre - ax * Oar::g_b.halfLen);
					if ((std::max)(e1.y, e2.y) > 40.0f) {
						ship = Oar::ShipLocal(sd, lockLocal_, kAftLock, 30.0f, true);
						if (!shipFlipLogged_) { shipFlipLogged_ = true; logger::info("Oars: shipped end for end (an end was at y {:.0f})", (std::max)(e1.y, e2.y)); }
					}
				}
				const RE::NiTransform base = shS >= 1.0f ? ship : shS <= 0.0f ? base0 : Oar::Blend(base0, ship, shS);
				oar->local = haveClip_[k] && w > 0.0f ? Oar::Blend(base, clipLocal_[k], w) : base;
				if (w <= 0.0f) haveClip_[k] = false;
				Oar::KeepFade(oar.get());
				RE::NiUpdateData data{};
				data.flags.set(RE::NiUpdateData::Flag::kDirty);
				oar->Update(data);
				if (haveWater_ && !oarsStowed_) Fx::TendBlade(blade_[k], oar.get(), waterZ_, a_dt, oarsToClip_, k);  // v1.3.0: OAS 56.39 / 56.67 (v1.5.0: plankton)
			}
		}
		Fx::Blade blade_[2];
		// v1.7.43: the boat's own ripples in the water, as the oars make them: the tiller (rudder) and the hull (the stem and
		// both sides along the waterline). Moving: one every Wake::g_stern.spread of travel (sooner while turning), bigger
		// with speed; at rest: now and then, small, as she bobs. Sizes and the tiller's place from oas "num stern".
		float tillerT_{ 0.0f }, bodyT_{ 0.0f }, rippleYaw_{ 0.0f };
		int bodyStation_{ 0 };
		void TendHullRipples(float a_dt)
		{
			if (!visual_ || !haveWater_ || a_dt <= 0.0f || groundLift_ > 0.5f) return;
			Wake::LoadStern();
			const auto& r = Wake::g_stern;
			const auto& w = visual_->world;
			const float spd = std::abs(speed_);
			const float s01 = std::clamp(spd / 300.0f, 0.0f, 1.0f);
			float dy = yaw_ - rippleYaw_;
			while (dy > kPi) dy -= 2.0f * kPi;
			while (dy < -kPi) dy += 2.0f * kPi;
			rippleYaw_ = yaw_;
			const bool turning = std::abs(dy / a_dt) > 0.15f;
			const bool moving = spd > 15.0f;
			auto at = [&](float a_x, float a_y) {
				RE::NiPoint3 p = w * RE::NiPoint3{ a_x, a_y, Plankton::kWaterline };
				p.z = waterZ_ + r.z;
				return p;
			};
			// the tiller
			if (r.size > 0.0f && (tillerT_ -= a_dt) <= 0.0f) {
				tillerT_ = moving ? std::clamp(r.spread / spd, 0.07f, 0.4f) * (turning ? 0.6f : 1.0f) : Plankton::Rand(1.2f, 2.6f) * (turning ? 0.5f : 1.0f);
				const float sc = moving || turning ? 0.03f + 0.04f * s01 + (turning ? 0.015f : 0.0f) : 0.018f;
				Fx::Ripple(at(0.0f, r.y), sc * r.size);  // v1.7.53: dead centre (was +-2 at random)
			}
			// v1.7.53: the hull's ripple: the tiller's own, as big and as often, dead centre under the rower's seat
			if (r.size > 0.0f && (bodyT_ -= a_dt) <= 0.0f) {
				bodyT_ = moving ? std::clamp(r.spread / spd, 0.07f, 0.4f) * (turning ? 0.6f : 1.0f) : Plankton::Rand(1.2f, 2.6f) * (turning ? 0.5f : 1.0f);
				const float sc = moving || turning ? 0.03f + 0.04f * s01 + (turning ? 0.015f : 0.0f) : 0.018f;
				Fx::Ripple(at(0.0f, rowSeat_.y), sc * r.size);
			}
			if (true) return;  // v1.7.53: the v1.7.44 side spots are off (kept below for oas "num hull")
			// the hull (v1.7.44: its own settings, oas "num hull"): spots spread evenly from the bow thwart to the aft
			// thwart, each side in turn with the stem, a little off each spot so it never repeats
			Wake::LoadHull();
			const auto& h = Wake::g_hull;
			const int spots = h.count + (h.stem ? 1 : 0);
			if (h.size > 0.0f && spots > 0 && (bodyT_ -= a_dt) <= 0.0f) {
				auto sideAt = [&](int a_i, float a_side) {
					const float y = h.count > 1 ? 150.0f - 260.0f * a_i / (h.count - 1) : 20.0f;
					const float yy = y + Plankton::Rand(-12.0f, 12.0f);
					RE::NiPoint3 p = at(a_side * (Plankton::HalfWidth(yy) + h.out), yy);
					p.z = waterZ_ + h.z;
					return p;
				};
				if (moving) {
					bodyT_ = std::clamp(h.every / spd / static_cast<float>(spots), 0.04f, 1.0f);
					const int k = bodyStation_++ % spots;
					const float sc = (0.03f + 0.05f * s01) * h.size;
					if (h.stem && k == spots - 1) {
						RE::NiPoint3 p = at(0.0f, (speed_ >= 0.0f ? 1.0f : -1.0f) * 240.0f);
						p.z = waterZ_ + h.z;
						Fx::Ripple(p, sc * 1.2f);  // the stem (or the stern backing)
					} else {
						Fx::Ripple(sideAt(k, 1.0f), sc);
						Fx::Ripple(sideAt(k, -1.0f), sc);
					}
				} else if (h.rest && h.count > 0) {
					bodyT_ = Plankton::Rand(1.5f, 3.2f);
					Fx::Ripple(sideAt(static_cast<int>(Plankton::Rand(0.0f, h.count - 0.01f)), Plankton::Rand(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f), 0.018f * h.size);
				} else {
					bodyT_ = 0.5f;
				}
			}
		}
		bool shipFlipLogged_{ false };
		float shipW_{ 0.0f };  // v1.7.33: 0 rest/stow .. 1 shipped along the gunwales (bed out)
		static inline const RE::NiPoint3 kAftLock{ 88.3f, -95.0f, 77.9f };  // the aft oarlock forks (shaft centre), from the hull mesh

		RE::FormID RefId() const { return refId_; }
		void SetPaint(const std::string& a_name)
		{
			for (int i = 0; i < Paint::kCount; ++i) {
				if (a_name == Paint::kNames[i]) {
					paint_ = i;
					Print(std::format("RowRim: paint {}", a_name));
					return;
				}
			}
			std::string all;
			for (const char* n : Paint::kNames) all += std::string(all.empty() ? "" : "|") + n;
			Print(std::format("RowRim: paint is {} (oas paint {})", Paint::kNames[paint_], all));
		}
		bool SpawnBoat()
		{
			Spawn(false);
			return refId_ != 0;
		}
		void SetHeightOffset(float a_off)
		{
			const float d = a_off - heightOff_;
			heightOff_ = a_off;
			if (!haveWater_) pos_.z += d;  // with water known, WorldStep eases it there
			Print(std::format("RowRim: boat height {:+.0f} over the default waterline (oas height N, -30..30)", heightOff_));
		}
		void SetPersist(bool a_on) { persist_ = a_on; Print(std::format("RowRim: next spawn {}", a_on ? "persistent" : "temporary")); }
		void SetCarrier(bool a_on) { carrierOn_ = a_on; Print(std::format("RowRim: next spawn {}", a_on ? "carried by a hidden actor" : "draws itself (no carrier)")); }
		void SetCarrierAI(bool a_on)
		{
			carrierAI_ = a_on;
			if (auto* a = carrierId_ ? RE::TESForm::LookupByID<RE::Actor>(carrierId_) : nullptr) a->EnableAI(a_on);
			Print(std::format("RowRim: carrier AI {}", a_on ? "on" : "off"));
		}
		void SetRegraft(bool a_on) { regraft_ = a_on; Print(std::format("RowRim: node regraft {}", a_on ? "on" : "off")); }
		void SetHop(bool a_on) { hop_ = a_on; Print(std::format("RowRim: cell hop {}", a_on ? "on (MoveTo when the hull is in another cell than you)" : "off")); }

		void BowFlip()
		{
			bowFlip_ = !bowFlip_;
			Print(std::format("RowRim: bow is now the model's {} end (W goes that way, you face it)", bowFlip_ ? "-Y" : "+Y"));
		}
		void StopHelm() { helm_ = false; }

		// v0.3.3: called from the camera update, after every actor has updated and right before drawing. The boat model is
		// placed where the collision body actually is (not where it is heading), so the model and the deck you stand on
		// never part, and nothing that moves the carrier later in the frame can shake it.
		// v1.1.1: called from the camera state's Update (third and first person), after every actor has updated, right before
		// drawing. (The v0.3.3 hook was PlayerCamera vtable slot 2, which is not Update: this never ran, so the model was only
		// placed during the player update and the carrier's own update afterwards could move it again: the boat shifted
		// against the seated rower.)
		// v1.2.3: the seated rower's drawn body put exactly on the seat of this frame's boat pose, before the camera update reads
		// it (both camera states). The View log showed the rower root a frame of travel (5 at full speed) off the drawn boat on
		// some frames and 12 high for a second after each cell hop: his 3D follows the character controller and the havok
		// step, not our glue, on its own timing. Now the camera, the rower and the boat all use the one pose.
		void PinRower()
		{
			if (drawMode_ != 3 || !lateOn_ || !seated_ || !carrierOn_ || !havePose_) return;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			const RE::NiPoint3 want = lastAt_ + lastRot_ * SeatPos();
			for (int fp = 0; fp < 2; ++fp) {
				auto* root = player->Get3D(fp == 1);
				if (!root) continue;
				const RE::NiPoint3 d = want - root->world.translate;
				const float len = d.Length();
				if (len > 60.0f) continue;  // a load or a teleport: not ours to fix
				if (fp == 0) {
					pinMax_ = (std::max)(pinMax_, len);
					if (len > 0.5f) ++pinMoved_;
				}
				if (len < 0.01f) continue;
				RE::NiPoint3 dl = d;
				if (auto* par = root->parent) {
					dl = par->world.rotate.Transpose() * d;
					if (par->world.scale > 0.0f) dl = dl / par->world.scale;
				}
				root->local.translate += dl;
				RE::NiUpdateData data{};
				data.flags.set(RE::NiUpdateData::Flag::kDirty);
				root->Update(data);
			}
		}

		// ---- v1.7.14: seat guard (from the v1.7.13 motion log). Seated in third person, after the boat is drawn:
		// 1) the rower's drawn root against the drawn boat may only move 0.25 a frame: one-frame slips are smoothed away (the
		//    engine moves his 3D about 1 unit too far every ~100 frames, its 60 Hz physics against a 59.5 fps frame; 5 units
		//    on a cell-hop frame), the clip's own slow moves pass.
		// 2) the camera against the drawn boat: a sudden pull-in (250 units on every hull cell hop, its reloading collision in
		//    the camera's line for a frame, then a 0.4 s ease back out) is held at the steady place until the game's own camera
		//    is back within 1 unit (0.7 s at most: a real obstacle gets the camera back). Look input or zoom: the camera is the player's, nothing held.
		// The camera is moved with the rower's fix, so it never shows the slip either. oas "seatguard on|off". ----
	public:
		void SetSeatGuard(bool a_on) { sgOn_ = a_on; sgHave_ = false; Print(std::format("RowRim: seat guard {}", a_on ? "on (rower and camera held steady on the drawn boat)" : "off")); }
		void SeatGuard(RE::ThirdPersonState* a_tps)
		{
			sgFixR_ = 0.0f;
			sgFixC_ = 0.0f;
			if (!sgOn_ || !seated_ || !visual_ || !visual_->parent || !havePose_) { sgHave_ = false; return; }
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* root = player ? player->Get3D(false) : nullptr;
			if (!root) { sgHave_ = false; return; }
			// v1.7.18: run BEFORE the game's camera update (the camera is placed from his 3D there), against the pose the boat is
			// about to be drawn at this frame (LatePlace's own rule), so the camera is built from the steadied rower
			RE::NiPoint3 boat;
			RE::NiMatrix3 R;
			DrawPose(boat, R);
			const RE::NiMatrix3 inv = R.Transpose();
			auto* pc = RE::PlayerControls::GetSingleton();
			const bool look = pc && (std::fabs(pc->data.lookInputVec.x) > 1.0e-4f || std::fabs(pc->data.lookInputVec.y) > 1.0e-4f);
			const bool zoomed = a_tps && sgHave_ && std::fabs(a_tps->currentZoomOffset - sgZoom_) > 1.0e-4f;
			if (a_tps) sgZoom_ = a_tps->currentZoomOffset;
			RE::NiPoint3 rel = inv * (root->world.translate - boat);
			if (!sgHave_) {
				sgRow_ = rel;
				if (a_tps) sgCam_ = inv * (a_tps->translation - boat);
				sgRowDev_ = 0;
				sgCamHold_ = 0;
				sgHave_ = true;
				return;
			}
			// 1) the rower: his place on the drawn boat may only change 0.25 a frame (the clip's own moves are slower; row
			// start / stop blend in over a few more frames); a jump of more than 8 is a new seat or pose and is taken as is
			RE::NiPoint3 want = rel;
			const RE::NiPoint3 ch = rel - sgRow_;
			const float chl = ch.Length();
			// v1.7.17: a jump over 3 that is still there the next frame is real (row start / stop) and is taken at once
			// v1.7.19: ... and only if he stayed where he jumped to (an engine late frame jumps out and back: never taken)
			const bool held = chl > 3.0f && sgRowBig_ && (rel - sgRowRaw_).Length() < 1.5f;
			sgRowBig_ = chl > 3.0f;
			sgRowRaw_ = rel;
			if (chl > 0.25f && chl < 8.0f && !held) want = sgRow_ + ch * (0.25f / chl);
			sgRow_ = want;
			const RE::NiPoint3 dr = want - rel;
			const float drl = dr.Length();
			if (drl > 0.01f) {
				const RE::NiPoint3 w = R * dr;
				RE::NiPoint3 dl = w;
				if (auto* par = root->parent) {
					dl = par->world.rotate.Transpose() * w;
					if (par->world.scale > 0.0f) dl = dl / par->world.scale;
				}
				root->local.translate += dl;
				RE::NiUpdateData data{};
				data.flags.set(RE::NiUpdateData::Flag::kDirty);
				root->Update(data);
				sgFixR_ = drl;
			}
			(void)look;
			(void)zoomed;
		}

		// v1.7.17: the camera, where the game really takes it. The v1.7.16 log showed ThirdPersonState.translation is not what is
		// drawn (our edits to it never reached the screen, DRAWNCAM): the game reads the camera's place through the state's
		// GetTranslation (vtable 5) after its update. Hooked: against the drawn boat the camera may move only 0.6 a frame
		// (its own stroke sway is slower), so a frame where the engine left the rower's 3D a frame behind (eng ~0 in the log,
		// about every couple of seconds while the row clip plays) no longer shakes the view; a pull-in of more than 20 (the hull's
		// collision reloading on a cell hop) is held until the game's camera is back. Look input / zoom: passed straight on.
		void CamOut(RE::ThirdPersonState* a_tps, RE::NiPoint3& a_pos)
		{
			sgFixC_ = 0.0f;
			if (!sgOn_ || !a_tps || !seated_ || !visual_ || !visual_->parent) { sgCamHave_ = false; mlOutHave_ = false; return; }
			if (sgCamFrame_ == camFrame_ && sgCamHave_) { a_pos = sgCamOut_; return; }  // asked again this frame: the same answer
			sgCamFrame_ = camFrame_;
			const RE::NiPoint3 boat = visual_->world.translate;
			const RE::NiMatrix3& R = visual_->world.rotate;
			const RE::NiMatrix3 inv = R.Transpose();
			auto* pc = RE::PlayerControls::GetSingleton();
			const bool look = pc && (std::fabs(pc->data.lookInputVec.x) > 1.0e-4f || std::fabs(pc->data.lookInputVec.y) > 1.0e-4f);
			const bool zoomed = sgCamHave_ && std::fabs(a_tps->currentZoomOffset - sgCamZoom_) > 1.0e-4f;
			sgCamZoom_ = a_tps->currentZoomOffset;
			const RE::NiPoint3 rel = inv * (a_pos - boat);
			RE::NiPoint3 out = rel;
			if (!sgCamHave_ || look || zoomed) {
				sgCamHold_ = 0;
			} else {
				const RE::NiPoint3 ch = rel - sgCam_;
				const float chl = ch.Length();
				if (sgCamHold_ > 0) {
					if (chl < 1.0f || ++sgCamHold_ > 40) sgCamHold_ = 0;
					else out = sgCam_;
				} else if (chl > 20.0f) {
					sgCamHold_ = 1;
					out = sgCam_;
					if (sgLogs_++ < 20) logger::info("Seat guard: camera pulled {:.0f} toward the boat in one frame, held at its place until the game's camera is back", chl);
				}
				if (sgCamHold_ == 0 && chl > 0.6f) out = sgCam_ + ch * (0.6f / chl);
				sgFixC_ = (out - rel).Length();
			}
			sgCam_ = out;
			sgCamHave_ = true;
			a_pos = boat + R * out;
			sgCamOut_ = a_pos;
			// motion log: the camera as drawn, against the boat as drawn
			mlOutRel_ = out;
			mlOutHave_ = true;
		}
		void CamFrame() { ++camFrame_; }
		// the seated pose LatePlace draws the boat at (kept in step with it)
		void DrawPose(RE::NiPoint3& a_at, RE::NiMatrix3& a_rot) const
		{
			a_at = drawMode_ == 0 && havePrev_ ? prevAt_ : lastAt_;
			a_rot = drawMode_ == 0 && havePrev_ ? prevRot_ : lastRot_;
			if (drawMode_ == 0 && havePrev_ && (lastAt_ - prevAt_).Length() < 60.0f) {
				a_at = prevAt_ + (lastAt_ - prevAt_) * drawLead_;
				if (drawLead_ >= 0.5f) a_rot = lastRot_;
			}
		}

	private:
		bool sgOn_{ true };
		int hideLateLogs_{ 0 };
		bool sgHave_{ false };
		RE::NiPoint3 sgRow_{}, sgCam_{};
		int sgRowDev_{ 0 }, sgCamHold_{ 0 }, sgLogs_{ 0 };
		bool sgRowBig_{ false }, sgCamHave_{ false }, mlOutHave_{ false };
		RE::NiPoint3 sgRowRaw_{};
		std::uint32_t camFrame_{ 0 }, sgCamFrame_{ 0 };
		float sgCamZoom_{ 0.0f };
		RE::NiPoint3 sgCamOut_{}, mlOutRel_{}, mlPrevOutRel_{};
		RE::NiMatrix3 mlPrevBoatRot_{};
		bool mlPrevOutHave_{ false };
		float sgZoom_{ 0.0f }, sgFixR_{ 0.0f }, sgFixC_{ 0.0f };

		// ---- v1.7.13: motion log (oas "motionlog [seconds]"): one line a frame for the boat and the third person camera, into
		// RowRim_Motion.log beside RowRim.log. A = our player update step (the sim and the glue), B = the third person camera
		// update (what is drawn this frame). Boat-frame values: x right, y forward, z up, from the boat as drawn. ----
	public:
		void MotionLogStart(float a_secs)
		{
			if (mlOut_.is_open()) mlOut_.close();
			mlLeft_ = 0.0f;
			if (a_secs <= 0.0f) { Print("RowRim: motion log off"); return; }
			auto dir = SKSE::log::log_directory();
			if (!dir) { Print("RowRim: motion log: no log folder"); return; }
			const auto path = *dir / "RowRim_Motion.log";
			mlOut_.open(path, std::ios::out | std::ios::trunc);
			if (!mlOut_) { Print("RowRim: motion log: could not open the file"); return; }
			mlOut_ << "RowRim motion log (" << kVersionShort << "), draw mode " << drawMode_ << ", lead " << drawLead_ << "\n"
				   << "Per frame (B = after the third person camera update, what the game draws this frame):\n"
				   << " f frame | dt game frame time ms | spd boat speed | ph stroke phase | thr thrust\n"
				   << " sim = our sim step this frame (units) | drw = the drawn boat's step | exp = speed x dt\n"
				   << " row = drawn rower root vs its seat on the drawn boat (x y z) | dRow = its change since last frame\n"
				   << " cam = camera (this frame's ThirdPersonState translation) vs the drawn boat (x y z) | dCam = its change\n"
				   << " yaw/pitch = camera heading and pitch (deg), dYaw/dPit = change | look = mouse/stick look input\n"
				   << " zoom = currentZoomOffset | fr0/fr1/fr2 = orbit (freeRotation.x) before the game's camera update, after it, after our fix-up\n"
				   << " eng = how far the engine moved the rower's 3D between our glue and the camera update\n"
				   << "Flags: CAM camera jumped against the boat (>1 unit, no look) | TURN heading jumped (>0.3 deg, no look) |\n"
				   << " BOAT drawn step off the expected by >1.5 | ROWER rower jumped on the boat (>1) | ORBIT the game swung the orbit this frame (we undo it after the draw) | DT frame time spike (>1.6x average)\n\n";
			mlLeft_ = a_secs;
			mlFrame_ = 0;
			mlHavePrev_ = false;
			mlHaveCamSet_ = false;
			for (auto& n : mlBoneN_) n = 0;
			mlSum_ = MlSum{};
			mlDtAvg_ = 0.0f;
			Print(std::format("RowRim: motion log on for {:.0f} s -> {}", a_secs, path.string()));
		}

		// A: our player update (sim + glue). Called at the start and at the end of Update.
		void MotionA0(float a_dt)
		{
			if (mlLeft_ <= 0.0f) return;
			mlA_ = MlA{};
			mlA_.dt = a_dt;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (auto* p3 = player ? player->Get3D(false) : nullptr) mlA_.p3dStart = p3->world.translate;
		}
		void MotionA1()
		{
			if (mlLeft_ <= 0.0f) return;
			mlA_.have = true;
			mlA_.sim = lastAt_;
			mlA_.simPrev = prevAt_;
			mlA_.speed = speed_;
			mlA_.phase = oarPhase_;
			mlA_.thrust = lastThrust_;
			if (auto* player = RE::PlayerCharacter::GetSingleton()) mlA_.glued = player->GetPosition();
		}
		// B0: right before the game's third person update; B1: right after it (before our fix-ups)
		void MotionCamPre(RE::ThirdPersonState* a_tps)
		{
			if (mlLeft_ <= 0.0f || !a_tps) return;
			mlFr0_ = a_tps->freeRotation.x;
			// v1.7.15: what the game really drew last frame: the camera node, against what our camera hook left in translation
			mlRc_ = -1.0f;
			if (auto* pcam = RE::PlayerCamera::GetSingleton(); pcam && pcam->cameraRoot && mlHaveCamSet_) mlRc_ = (pcam->cameraRoot->world.translate - mlCamSet_).Length();
			// v1.7.18: the camera node as the game drew it last frame, against the boat as drawn last frame (the shake you see)
			mlOutHave_ = false;
			if (auto* pcam = RE::PlayerCamera::GetSingleton(); pcam && pcam->cameraRoot && mlHavePrev_) {
				mlOutRel_ = mlPrevBoatRot_.Transpose() * (pcam->cameraRoot->world.translate - mlPrevBoat_);
				mlOutHave_ = true;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (auto* p3 = player ? player->Get3D(false) : nullptr) mlP3dCam_ = p3->world.translate;
		}
		void MotionCamGame(RE::ThirdPersonState* a_tps)
		{
			if (mlLeft_ <= 0.0f || !a_tps) return;
			mlFr1_ = a_tps->freeRotation.x;
		}
		// B: after our fix-ups and the late boat placement: one line
		void MotionB(RE::ThirdPersonState* a_tps)
		{
			if (mlLeft_ <= 0.0f || !mlOut_.is_open() || !a_tps) return;
			const float dt = mlA_.have ? mlA_.dt : 0.0f;
			mlLeft_ -= (dt > 0.0f ? dt : 0.016f);
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* proot = player ? player->Get3D(false) : nullptr;
			if (!visual_ || !visual_->parent || !proot) {
				mlOut_ << std::format("f{} no boat drawn or no rower 3D\n", mlFrame_++);
				if (mlLeft_ <= 0.0f) MotionLogEnd();
				return;
			}
			const RE::NiPoint3 boat = visual_->world.translate;
			const RE::NiMatrix3 inv = visual_->world.rotate.Transpose();
			const RE::NiPoint3 seat = boat + visual_->world.rotate * SeatPos();
			const RE::NiPoint3 row = inv * (proot->world.translate - seat);
			const RE::NiPoint3 cam = a_tps->translation;
			const RE::NiPoint3 camRel = inv * (cam - boat);
			// camera heading / pitch from this frame's ThirdPersonState rotation
			const auto& q = a_tps->rotation;
			const RE::NiPoint3 fwd{ 2.0f * (q.x * q.y - q.w * q.z), 1.0f - 2.0f * (q.x * q.x + q.z * q.z), 2.0f * (q.y * q.z + q.w * q.x) };
			const float yaw = std::atan2(fwd.x, fwd.y) * 57.2958f;
			const float pitch = std::asin(std::clamp(fwd.z, -1.0f, 1.0f)) * 57.2958f;
			auto* pc = RE::PlayerControls::GetSingleton();
			const bool look = pc && (std::fabs(pc->data.lookInputVec.x) > 1.0e-4f || std::fabs(pc->data.lookInputVec.y) > 1.0e-4f);
			const float fr2 = a_tps->freeRotation.x;
			const float simStep = mlA_.have ? (mlA_.sim - mlA_.simPrev).Length() : 0.0f;
			const float drwStep = mlHavePrev_ ? (boat - mlPrevBoat_).Length() : 0.0f;
			const float expStep = std::fabs(mlA_.speed) * dt;
			const float dRow = mlHavePrev_ ? (row - mlPrevRow_).Length() : 0.0f;
			const float dCam = mlHavePrev_ ? (camRel - mlPrevCamRel_).Length() : 0.0f;
			const float dYaw = mlHavePrev_ ? std::remainder(yaw - mlPrevYaw_, 360.0f) : 0.0f;
			const float dPit = mlHavePrev_ ? pitch - mlPrevPitch_ : 0.0f;
			const float eng = mlA_.have ? (mlP3dCam_ - mlA_.glued).Length() : -1.0f;
			if (dt > 0.0f) mlDtAvg_ = mlDtAvg_ <= 0.0f ? dt : mlDtAvg_ * 0.95f + dt * 0.05f;
			std::string flags;
			if (mlHavePrev_ && !look && dCam > 1.0f) { flags += " CAM"; ++mlSum_.cam; }
			if (mlHavePrev_ && !look && std::fabs(dYaw) > 0.3f) { flags += " TURN"; ++mlSum_.turn; }
			if (mlHavePrev_ && std::fabs(drwStep - expStep) > 1.5f) { flags += " BOAT"; ++mlSum_.boat; }
			if (mlHavePrev_ && dRow > 1.0f) { flags += " ROWER"; ++mlSum_.rower; }
			if (std::fabs(std::remainder(fr1Minus(fr2), 6.2831853f)) > 1.0e-5f) { flags += " ORBIT"; ++mlSum_.orbit; }
			if (dt > 0.0f && mlDtAvg_ > 0.0f && dt > 1.6f * mlDtAvg_ && mlFrame_ > 10) { flags += " DT"; ++mlSum_.dtSpike; }
			// v1.7.16: the rower's bones on the drawn boat (pelvis, head, the Camera3rd bone the game camera follows): a kink
			// (second difference over 0.8 in a frame) is the clip itself jumping, e.g. at its loop seam
			std::string bones;
			{
				static constexpr const char* kBones[3] = { "NPC Pelvis [Pelv]", "NPC Head [Head]", "Camera3rd [Cam3]" };
				static constexpr const char* kShort[3] = { "pel", "head", "cam3" };
				for (int i = 0; i < 3; ++i) {
					auto* n = proot->GetObjectByName(kBones[i]);
					if (!n) continue;
					const RE::NiPoint3 b = inv * (n->world.translate - boat);
					float k = 0.0f;
					if (mlBoneN_[i] >= 2) k = (b - mlBone1_[i] * 2.0f + mlBone2_[i]).Length();
					mlBone2_[i] = mlBone1_[i];
					mlBone1_[i] = b;
					mlBoneN_[i] = (std::min)(mlBoneN_[i] + 1, 2);
					bones += std::format(" {} {:.1f} {:.1f} {:.1f} k {:.2f}", kShort[i], b.x, b.y, b.z, k);
					if (k > 0.8f && i < 2) { flags += std::format(" BONE-{} {:.1f}", kShort[i], k); ++mlSum_.bone; }
				}
			}
			// v1.7.17: the camera as the game drew it last frame (our GetTranslation answer), against the boat then: its change is the
			// shake you see. JERK = it moved over 0.7 in a frame without look input.
			std::string outTxt = " | out -";
			if (mlOutHave_) {
				const float dOut = mlPrevOutHave_ ? (mlOutRel_ - mlPrevOutRel_).Length() : 0.0f;
				outTxt = std::format(" | out {:.1f} {:.1f} {:.1f} dOut {:.2f}", mlOutRel_.x, mlOutRel_.y, mlOutRel_.z, dOut);
				if (mlPrevOutHave_ && !look && dOut > 0.7f) { flags += std::format(" JERK {:.1f}", dOut); ++mlSum_.jerk; }
				mlSum_.dOutMax = (std::max)(mlSum_.dOutMax, look ? 0.0f : dOut);
				mlPrevOutRel_ = mlOutRel_;
				mlPrevOutHave_ = true;
			} else {
				mlPrevOutHave_ = false;
			}
			bones += outTxt;
			if (mlRc_ > 1.0f) { flags += std::format(" DRAWNCAM {:.1f}", mlRc_); ++mlSum_.drawnCam; }  // v1.7.15: the drawn camera was not where the hook left it
			if (auto* href = Ref(); href && href->Get3D() && carrierOn_ && !href->Get3D()->flags.any(RE::NiAVObject::Flag::kHidden)) { flags += " HULL3D"; ++mlSum_.hull3d; }  // v1.7.15: the collision hull drawn
			if (sgFixR_ > 0.0f) flags += std::format(" fixR {:.2f}", sgFixR_);  // v1.7.14: seat guard put the rower back
			if (sgFixC_ > 0.0f) flags += std::format(" fixC {:.1f}", sgFixC_);  // v1.7.14: seat guard held the camera
			mlOut_ << std::format("f{} dt {:.1f} spd {:.0f} ph {:.2f} thr {:.0f} | sim {:.2f} drw {:.2f} exp {:.2f} | row {:.1f} {:.1f} {:.1f} dRow {:.2f} | cam {:.1f} {:.1f} {:.1f} dCam {:.2f} | yaw {:.2f} pit {:.2f} dYaw {:.2f} dPit {:.2f} look {} zoom {:.2f} | fr0 {:.4f} fr1 {:.4f} fr2 {:.4f} | eng {:.2f} |{} | seated {} rowing {}{}{}\n",
				mlFrame_, dt * 1000.0f, mlA_.speed, mlA_.phase, mlA_.thrust, simStep, drwStep, expStep, row.x, row.y, row.z, dRow, camRel.x, camRel.y, camRel.z, dCam,
				yaw, pitch, dYaw, dPit, look ? 1 : 0, a_tps->currentZoomOffset, mlFr0_, mlFr1_, fr2, eng, bones, seated_ ? 1 : 0, rowAnimOn_ ? 1 : 0, mlA_.have ? "" : " (no sim step)",
				flags.empty() ? std::string() : " <<" + flags);
			mlSum_.frames++;
			mlSum_.dtMin = (std::min)(mlSum_.dtMin, dt);
			mlSum_.dtMax = (std::max)(mlSum_.dtMax, dt);
			mlSum_.dCamMax = (std::max)(mlSum_.dCamMax, look ? 0.0f : dCam);
			mlSum_.dRowMax = (std::max)(mlSum_.dRowMax, dRow);
			mlSum_.boatErrMax = (std::max)(mlSum_.boatErrMax, mlHavePrev_ ? std::fabs(drwStep - expStep) : 0.0f);
			if (mlSum_.frames >= 120) MotionSummary();
			mlCamSet_ = cam;
			mlHaveCamSet_ = true;
			mlPrevBoatRot_ = visual_->world.rotate;
			mlPrevBoat_ = boat;
			mlPrevRow_ = row;
			mlPrevCamRel_ = camRel;
			mlPrevYaw_ = yaw;
			mlPrevPitch_ = pitch;
			mlHavePrev_ = true;
			mlA_.have = false;
			++mlFrame_;
			if (mlLeft_ <= 0.0f) MotionLogEnd();
		}

	private:
		struct MlA { bool have = false; float dt = 0.0f; RE::NiPoint3 p3dStart{}, glued{}, sim{}, simPrev{}; float speed = 0.0f, phase = 0.0f, thrust = 0.0f; };
		struct MlSum { int frames = 0, cam = 0, turn = 0, boat = 0, rower = 0, orbit = 0, dtSpike = 0, drawnCam = 0, hull3d = 0, bone = 0, jerk = 0; float dOutMax = 0.0f; float dtMin = 1.0f, dtMax = 0.0f, dCamMax = 0.0f, dRowMax = 0.0f, boatErrMax = 0.0f; };
		MlA mlA_{};
		MlSum mlSum_{};
		std::ofstream mlOut_;
		float mlLeft_{ 0.0f };
		std::uint32_t mlFrame_{ 0 };
		bool mlHavePrev_{ false };
		RE::NiPoint3 mlPrevBoat_{}, mlPrevRow_{}, mlPrevCamRel_{}, mlP3dCam_{}, mlCamSet_{};
		bool mlHaveCamSet_{ false };
		RE::NiPoint3 mlBone1_[3]{}, mlBone2_[3]{};
		int mlBoneN_[3]{};
		float mlRc_{ -1.0f };
		float mlPrevYaw_{ 0.0f }, mlPrevPitch_{ 0.0f }, mlFr0_{ 0.0f }, mlFr1_{ 0.0f }, mlDtAvg_{ 0.0f };
		float fr1Minus(float a_fr2) const { return a_fr2 - mlFr1_; }
		void MotionSummary()
		{
			mlOut_ << std::format("== last {} frames: dt {:.1f}..{:.1f} ms (avg {:.1f}) | CAM {} (max {:.2f}) TURN {} BOAT {} (max off {:.2f}) ROWER {} (max {:.2f}) ORBIT {} DT {} DRAWNCAM {} HULL3D {} BONE {} | drawn camera: JERK {} (max move {:.2f} a frame)\n",
				mlSum_.frames, mlSum_.dtMin * 1000.0f, mlSum_.dtMax * 1000.0f, mlDtAvg_ * 1000.0f, mlSum_.cam, mlSum_.dCamMax, mlSum_.turn, mlSum_.boat, mlSum_.boatErrMax,
				mlSum_.rower, mlSum_.dRowMax, mlSum_.orbit, mlSum_.dtSpike, mlSum_.drawnCam, mlSum_.hull3d, mlSum_.bone, mlSum_.jerk, mlSum_.dOutMax);
			mlOut_.flush();
			mlSum_ = MlSum{};
		}
		void MotionLogEnd()
		{
			if (mlSum_.frames > 0) MotionSummary();
			mlOut_ << "end\n";
			mlOut_.close();
			mlLeft_ = 0.0f;
			Print("RowRim: motion log written (RowRim_Motion.log)");
		}

	public:
		void LatePlace(const char* a_where)
		{
			if (!lateLogged_) {
				lateLogged_ = true;
				logger::info("Late: placing the boat model from {}", a_where);
			}
			if (!lateOn_ || !carrierOn_ || !visual_ || !visual_->parent) return;
			// v1.7.15: a hull 3D reloaded by a cell hop can come in after our player update, drawn (the vanilla boat at the ref's
			// spot) for a frame before Update hides it: hidden here too, right before the draw
			if (auto* href = Ref()) {
				if (auto* hroot = href->Get3D(); hroot && !hroot->flags.any(RE::NiAVObject::Flag::kHidden)) {
					hroot->flags.set(RE::NiAVObject::Flag::kHidden);
					if (hideLateLogs_++ < 10) logger::info("Hull: its reloaded 3D was about to be drawn, hidden at the camera update");
				}
			}
			// v1.2.2: the camera update can run between a cell hop (MoveTo reloads the hull 3D) and the next player update,
			// when body_ still points at the old, freed body. Only read the body while it belongs to the hull's current 3D.
			auto* lref = Ref();
			const bool bodyLive = body_ && !needInit_ && root_ && lref && lref->Get3D() == root_;
			if (!bodyLive && body_) {
				body_ = nullptr;
				needInit_ = true;
			}
			if (seated_ || !bodyLive) {
				// seated: v1.1.3, as OAS ("the camera shows last frame's player position, so the boat follows the previous frame's
				// vehicle position to stay glued to the view"): the boat one frame behind the glue. The row stroke's surge changes
				// that one frame of travel every stroke, which is why it only showed while rowing. rr draw now|player to compare.
				if (!havePose_) return;
				RE::NiPoint3 at = drawMode_ == 0 && havePrev_ ? prevAt_ : lastAt_;
				RE::NiMatrix3 rot = drawMode_ == 0 && havePrev_ ? prevRot_ : lastRot_;
				// v1.6.6: drawLead_ of the way from last frame's pose to this frame's: the boat (and the passengers pinned to it)
				// no longer fall a whole frame of travel behind the rower when rowing starts, only a sliver (rr "lead 0..1")
				if (drawMode_ == 0 && havePrev_ && (lastAt_ - prevAt_).Length() < 60.0f) {
					at = prevAt_ + (lastAt_ - prevAt_) * drawLead_;
					if (drawLead_ >= 0.5f) rot = lastRot_;
				}
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (auto* proot = drawMode_ == 2 && seated_ && player ? player->Get3D(false) : nullptr) {
					const RE::NiPoint3 fromPlayer = proot->world.translate - lastRot_ * SeatPos();
					if ((fromPlayer - lastAt_).Length() < 60.0f) at = fromPlayer;
				}
				PlaceNode(visual_.get(), at, rot);
				for (RE::NiAVObject* n = visual_->parent; n; n = n->parent) n->UpdateWorldBound();
				Gear::LatePin(visual_.get());  // v1.7.12: the bedroll and supplies rock with the drawn boat
				if (seated_ && player) player->SetGraphVariableBool("bHumanoidFootIKDisable", true);  // v1.6.6: again here, right before the draw
				if (seated_) Crew::LatePin(CrewPose(at, rot));  // v1.6.4: the passengers onto the boat as it is drawn
				if (rowAnimOn_) ViewDiag(rot);
				return;
			}
			RE::NiPoint3 bp;
			Quat bq;
			ReadBody(bp, bq);
			Quat qt = Norm(Mul(bq, Conj(rotOff_)));  // body = target * offset, so target = body * offset^-1
			const RE::NiPoint3 at = bp - Rotate(qt, posOff_) - Rotate(qt, RE::NiPoint3{ 0.0f, 0.0f, kCollisionLift });
			if (flip_ != autoFlip_) qt = Conj(qt);  // back to the node convention
			PlaceNode(visual_.get(), at, ToMatrix(qt));
			for (RE::NiAVObject* n = visual_->parent; n; n = n->parent) n->UpdateWorldBound();
			Gear::LatePin(visual_.get());  // v1.7.12
			if (Crew::Lingering()) Crew::LatePin(CrewPose(at, ToMatrix(qt)));  // v1.7.21: the waiting passengers on the drawn boat
		}
		void SetWaterBlock(const std::string& a_mode)
		{
			if (a_mode == "a") wbMode_ = "a";
			else if (a_mode == "debug") wbMode_ = "debug";
			else wbMode_.clear();
			if (wbShown_) wbShown_->flags.set(RE::NiAVObject::Flag::kHidden);
			wbShown_ = nullptr;  // TendWaterBlock picks the plane again next frame
			Print(std::format("RowRim: water blocker {} (follows the waterline){}", wbMode_.empty() ? "off" : wbMode_, visual_ ? "" : " (applies when the boat model loads)"));
		}
		void SetWaterBlockZ(float a_z) { wbZ_ = std::clamp(a_z, -10.0f, 16.0f); Print(std::format("RowRim: blocker margin {:+.0f} on top of the automatic height", wbZ_)); }
		void SetFloor(bool a_on) { wbFloor_ = a_on; ApplyWaterBlock(visual_.get()); Print(std::format("RowRim: bilge floor {}", a_on ? "on" : "off")); }
		RE::FormID CarrierId() const { return carrierOn_ ? carrierId_ : 0; }  // v1.7.7
		bool BoatAt(RE::NiPoint3& a_out) const  // v1.6.0
		{
			if (!havePose_) return false;
			a_out = lastAt_;
			return true;
		}
		void SetNudge(bool a_on) { nudgeOn_ = a_on; Print(std::format("RowRim: stand-up nudge {}", a_on ? "on" : "off")); }
		void SetLate(bool a_on) { lateOn_ = a_on; Print(std::format("RowRim: model drawn at the {}", a_on ? "collision body (late, v0.3.3)" : "target pose (v0.3.2)")); }

		void Watch()
		{
			auto sel = RE::Console::GetSelectedRef();
			auto* actor = sel ? sel->As<RE::Actor>() : nullptr;
			watchId_ = actor ? actor->GetFormID() : 0;
			Print(watchId_ ? std::format("RowRim: watching {} ({:08X}) too", actor->GetName(), watchId_) : std::string("RowRim: click an actor in the console first; watching the player only"));
		}

		void Info()
		{
			auto* ref = Ref();
			if (!ref) { Print("RowRim: no test hull (oas spawn)"); return; }
			auto* root = ref->Get3D();
			std::string s = std::format("RowRim: hull {:08X} 3D {} body {} mode {} rock {:.1f} deg speed {:.0f} turn {:.0f}", refId_, root ? "yes" : "no",
				body_ ? "yes" : "no", ModeName(mode_), rockRoll_ * 180.0f / kPi, speed_, turn_ * 180.0f / kPi);
			if (auto* hb = HkBody()) {
				s += std::format(" | motion {} layer {}", static_cast<int>(hb->motion.type.get()), root ? static_cast<int>(root->GetCollisionLayer()) : -1);
			}
			Print(s);
			LogFrame(true);
		}

		// ---- v1.7.12 (OAS 56.43): True Directional Movement turns the player toward the movement input and takes that turn
		// back off ThirdPersonState::freeRotation so its view stays still. Seated, the glue puts the player's heading back
		// on the seat every frame (no matching freeRotation change), so while W is held TDM turns, takes it off the camera,
		// we undo the turn: the orbit winds up and the camera stops pitching. TDM also forces headtracking. Its modder API
		// (V1) pauses both while seated, released on standing up; calls on TDM's thread (the player update). ----
		TDM_API::IVTDM1* tdm_{ nullptr };
		bool compatLooked_{ false };
		bool tdmHeld_{ false };
		void TendModCompat(bool a_seated)
		{
			if (!compatLooked_) {
				compatLooked_ = true;
				if (auto* mod = GetModuleHandleA("TrueDirectionalMovement.dll")) {
					using RequestFn = void* (*)(TDM_API::InterfaceVersion);
					if (auto fn = reinterpret_cast<RequestFn>(GetProcAddress(mod, "RequestPluginAPI"))) tdm_ = static_cast<TDM_API::IVTDM1*>(fn(TDM_API::InterfaceVersion::V1));
					logger::info("Compat: True Directional Movement loaded, API {}", tdm_ ? "found (its directional movement and headtracking pause while seated)" : "NOT found (an old TDM? the camera may wind up while rowing)");
				} else {
					logger::info("Compat: True Directional Movement not loaded");
				}
				logger::info("Compat: SmoothCam {}", GetModuleHandleA("SmoothCam.dll") ? "loaded" : "not loaded");
			}
			if (!tdm_ || a_seated == tdmHeld_) return;
			if (GetCurrentThreadId() != tdm_->GetTDMThreadId()) return;  // not TDM's thread: next frame
			const auto handle = SKSE::GetPluginHandle();
			auto name = [](TDM_API::APIResult a_r) {
				switch (a_r) {
				case TDM_API::APIResult::OK: return "OK";
				case TDM_API::APIResult::NotOwner: return "not owner";
				case TDM_API::APIResult::MustKeep: return "TDM must keep it";
				case TDM_API::APIResult::AlreadyGiven: return "already ours";
				case TDM_API::APIResult::AlreadyTaken: return "held by another mod";
				case TDM_API::APIResult::BadThread: return "wrong thread";
				default: return "?";
				}
			};
			if (a_seated) {
				const auto m = tdm_->RequestDisableDirectionalMovement(handle);
				const auto h = tdm_->RequestDisableHeadtracking(handle);
				logger::info("Compat: TDM paused while seated (directional movement: {}, headtracking: {})", name(m), name(h));
			} else {
				const auto m = tdm_->ReleaseDisableDirectionalMovement(handle);
				const auto h = tdm_->ReleaseDisableHeadtracking(handle);
				logger::info("Compat: TDM given back (directional movement: {}, headtracking: {})", name(m), name(h));
			}
			tdmHeld_ = a_seated;
		}

		// v1.7.8: oas status
		void Status()
		{
			if (!docksLoaded_) LoadDocks();
			auto* player = RE::PlayerCharacter::GetSingleton();
			std::string s = std::format("RowRim {}: boat {}", kVersionShort,
				HaveBoat() ? std::format("{:08X}, {:.0f} away", refId_, player ? std::hypot(pos_.x - player->GetPosition().x, pos_.y - player->GetPosition().y) : 0.0f)
				           : std::string("none (oas boat)"));
			s += std::format(" | seated {} | paint {} | height {:+.0f} | row speed {:.0f} glide {:.2f} turning {:.2f}", seated_ ? "yes" : "no", Paint::kNames[paint_],
				heightOff_, rowMax_, rowDrag_, 0.45f * turnScale_);
			s += std::format(" | docks {} ({}) | controller zoom {}", docks_.size(), docksOn_ ? "on" : "off", Pad::g_zoomOn ? "on" : "off");
			Print(s);
		}
		void ResetSettings()
		{
			ResetRowSettings();
			heightOff_ = 0.0f;
			paint_ = Paint::kBlue;
			paintedRoot_ = nullptr;
			if (!docksLoaded_) LoadDocks();
			docksOn_ = true;
			SaveDocks();
		}

		// ---- per frame (player update) ----
		void Update(float a_dt)
		{
			if (!rowLoaded_) LoadRowSettings();  // v1.7.8
			MotionA0(a_dt);                       // v1.7.13: motion log
			if (seated_) Pad::Zoom(a_dt);         // v1.7.8: controller zoom while seated
			TendModCompat(seated_);               // v1.7.12: True Directional Movement paused while seated (OAS 56.43)
			Gear::TendNum(a_dt);                  // v1.7.11: oas "num gear"
			Wake::TendNum(a_dt);                  // v1.7.43: oas "num stern"
			Gear::TendLoad(refId_ ? Ref() : nullptr, a_dt);  // v1.7.21: saved / stray beds and supplies removed after a load, boat or not
			TendLoadUnstick(a_dt);  // v1.7.41
			TendWalkFix(a_dt);
			{
				// v1.5.0: the plankton (free glows keep fading even with no boat out)
				Plankton::Ctx pc;
				if (refId_ && visual_ && visual_->parent) {
					pc.boat = visual_.get();
					pc.oar[0] = oars_[0] ? oars_[0].get() : nullptr;
					pc.oar[1] = oars_[1] ? oars_[1].get() : nullptr;
					pc.speed = speed_;
					pc.waterZ = waterZ_;
					pc.water = haveWater_;
				}
				Stage("Plankton::Tick");
				Plankton::Tick(a_dt, pc);
				Stage("Wake::Tick");
				Wake::Tick(a_dt, pc);  // v1.5.16
				Stage("TestHull::Update (after plankton and wake)");
			}
			Stage("TestHull::TendDocks");
			TendDocks(a_dt);  // v1.7.8
			Stage("Legacy::Tend");
			Legacy::Tend(a_dt, [this](RE::FormID a_id) { return a_id == refId_ || a_id == carrierId_ || IsPickRef(a_id) || a_id == LampLightId() || LakeBoats::IsOurs(a_id); },
				HaveBoat() ? &pos_ : nullptr);  // v1.7.8; v1.7.9: our prompts, lantern light and anything by our boat are never old pieces
			if (!refId_) return;
			auto* ref = Ref();
			if (!ref) { Forget(); return; }
			TendStow(a_dt);

			auto* root = ref->Get3D();
			const float dt = std::clamp(a_dt, 0.001f, 0.1f);
			rideTime_ += dt;
			if (!root) {  // 3D not loaded yet (or reloading after a cell hop)
				body_ = nullptr;
				root_ = nullptr;
				needInit_ = true;
				if (carrierOn_) CoastFrame(dt);  // v1.0.1: keep going while the collision reloads
				else {
					if (seated_ && havePose_) GlueSeat(lastAt_, lastRot_, 0.0f, dt);  // keep the seated player where the seat was
					if (havePose_) TendHeld(lastAt_, lastRot_, nullptr, dt);
				}
				return;
			}
			KeepOpaque(root);  // a (re)loaded model fades in from 0; held fully drawn instead (OAS 56.28)
			if (carrierOn_ && visual_ && visual_->parent) root->flags.set(RE::NiAVObject::Flag::kHidden);  // collision only; the carrier draws the boat
			else root->flags.reset(RE::NiAVObject::Flag::kHidden);
			if (root != root_) {  // the 3D was (re)loaded, e.g. after a cell hop: the old body pointer is gone
				if (root_) logger::info("Hull: 3D reloaded ({}), setting the body up again", hopsSinceLoad_ ? "after a cell hop" : "by the engine");
				root_ = root;
				body_ = nullptr;
				needInit_ = true;
			}
			if (!needInit_ && body_) {  // same 3D, but the body was put back to fixed (the engine re-adds it on some moves)
				auto* hb = body_->GetRigidBody();
				if (hb && hb->motion.type.get() != RE::hkpMotion::MotionType::kKeyframed) {
					logger::info("Hull: body motion reset to {} (same 3D), keyframed again", static_cast<int>(hb->motion.type.get()));
					needInit_ = true;
				}
			}
			if (needInit_ && !InitBody(ref, root)) {
				if (carrierOn_) CoastFrame(dt);
				else {
					if (seated_ && havePose_) GlueSeat(lastAt_, lastRot_, 0.0f, dt);
					if (havePose_) TendHeld(lastAt_, lastRot_, root, dt);
				}
				return;
			}
			hopTimer_ += a_dt;
			if (hopTimer_ > 0.5f) {
				hopTimer_ = 0.0f;
				// v1.5.7: walking far off left the hull filed in a cell that detached: its collision left the physics world and
				// did not come back when the cell loaded again (its 3D was kept), so you walked through the boat (and saw the
				// carrier's "Ride" prompt under it). Once you are back within 3000 the hull is re-homed into your cell, which
				// reloads its 3D and its collision.
				{
					auto* hc = ref->GetParentCell();
					if (hc && !hc->IsAttached()) cellLost_ = true;
					bool outOfWorld = false;
					if (body_ && !needInit_) {
						if (auto* hb = body_->GetRigidBody(); hb && !hb->world) outOfWorld = true;
					}
					if ((cellLost_ || outOfWorld) && Rehome(ref, outOfWorld ? "its collision was out of the physics world" : "its cell had detached")) {
						if (carrierOn_) CoastFrame(dt);
						return;
					}
				}
				if (hop_ && CellHop(ref)) {
					if (carrierOn_) CoastFrame(dt);
					return;
				}
				if (!hop_ && regraft_) Regraft(root);  // moved into the player's cell; the 3D reloads, next frames set it up again
			}
			if (!body_) return;

			// ---- the target pose ----
			float dYaw = 0.0f;
			RE::NiPoint3 at;
			RE::NiMatrix3 rot;
			StepTarget(dt, at, rot, dYaw);
			Quat qt = FromMatrix(rot);
			if (flip_ != autoFlip_) qt = Conj(qt);

			// body target = ref target * body offset (measured once at init)
			const Quat qb = Norm(Mul(qt, rotOff_));
			const RE::NiPoint3 pb = at + Rotate(qt, posOff_) + Rotate(qt, RE::NiPoint3{ 0.0f, 0.0f, kCollisionLift });
			const float hs = RE::bhkWorld::GetWorldScale();

			if (mode_ == Mode::kWarp) {
				RE::hkVector4 p = Hk(pb.x * hs, pb.y * hs, pb.z * hs);
				RE::hkQuaternion q;
				q.vec = Hk(qb.x, qb.y, qb.z, qb.w);
				body_->SetPositionAndRotation(p, q);
				body_->SetLinearVelocity(Hk(0, 0, 0));
				body_->SetAngularVelocity(Hk(0, 0, 0));
				lastErr_ = 0.0f;
			} else if (mode_ == Mode::kDrive) {
				RE::NiPoint3 cur;
				Quat cq;
				ReadBody(cur, cq);
				const RE::NiPoint3 err = pb - cur;
				if (err.Length() > 300.0f) {  // far behind (first frame, a hitch): put it there
					RE::hkVector4 p = Hk(pb.x * hs, pb.y * hs, pb.z * hs);
					RE::hkQuaternion q;
					q.vec = Hk(qb.x, qb.y, qb.z, qb.w);
					body_->SetPositionAndRotation(p, q);
					body_->SetLinearVelocity(Hk(0, 0, 0));
					body_->SetAngularVelocity(Hk(0, 0, 0));
					if (snaps_++ < 10) logger::info("drive: snapped (error {:.0f})", err.Length());
				} else {
					// v0.3.4: was err / dt (close the gap in one frame). Havok steps on its own fixed clock; when its step
					// is longer than the frame, that overshoots, swings back and buzzes (err 1-5 at rest, the parked jitter).
					// Now: the target's own velocity (feed-forward) plus a gentler correction that cannot overshoot.
					const float kc = (std::min)(1.0f / dt, kCorrect);
					auto axisAngle = [](const Quat& a_from, const Quat& a_to) {
						Quat dq = Norm(Mul(a_to, Conj(a_from)));
						if (dq.w < 0.0f) dq = { -dq.x, -dq.y, -dq.z, -dq.w };
						const float sh = std::sqrt(dq.x * dq.x + dq.y * dq.y + dq.z * dq.z);
						if (sh < 1e-6f) return RE::NiPoint3{};
						const float ang = 2.0f * std::atan2(sh, dq.w);
						return RE::NiPoint3{ dq.x / sh, dq.y / sh, dq.z / sh } * ang;
					};
					RE::NiPoint3 vff{}, wff{};
					if (haveLastTarget_) {
						vff = (pb - lastPb_) / dt;
						wff = axisAngle(lastQb_, qb) / dt;
					}
					const RE::NiPoint3 v = vff + err * kc;
					const RE::NiPoint3 om = wff + axisAngle(cq, qb) * kc;
					body_->SetLinearVelocity(Hk(v.x * hs, v.y * hs, v.z * hs));
					body_->SetAngularVelocity(Hk(om.x, om.y, om.z));
					lastErr_ = err.Length();
				}
			}
			lastPb_ = pb;
			lastQb_ = qb;
			haveLastTarget_ = mode_ == Mode::kDrive;

			// the ref and its node follow the target (no Update3DPosition: that would warp the body)
			ref->data.location = at;
			ref->data.angle = RE::NiPoint3{ 0.0f, 0.0f, yaw_ };
			if (writeNode_) PlaceNode(root, at + rot * RE::NiPoint3{ 0.0f, 0.0f, kCollisionLift }, rot);  // the (hidden) collision model
			if (seated_) GlueSeat(at, rot, dYaw, dt);
			else {
				TendNudge(dt);
				Crew::Linger(CrewPose(at, rot), dt);  // v1.7.21: passengers wait seated until the boat settles
			}
			if (!carrierOn_) TendHeld(at, rot, root, dt);
			else {
				TendCarrier(at, rot, dt);
				TendWaterBlock(dt);
			}
			prevAt_ = lastAt_;
			prevRot_ = lastRot_;
			havePrev_ = havePose_;
			lastAt_ = at;
			lastRot_ = rot;
			havePose_ = true;
			Gear::Tend(ref, at, rot, yaw_ + (bowFlip_ ? kPi : 0.0f), dt, seated_);  // v1.7.0: the fishing supplies ride along
			TendPicks(dt);  // v1.7.9: Board / Steer / Row prompts
			TendSitWait(dt);  // v1.7.25
			TendFishWatch(dt);  // v1.7.26
			TendUnstick(dt);    // v1.7.31
			if (visual_ && (rollCulledOn_ != visual_.get() || rollCulled_ != Gear::BedrollShown())) {  // v1.7.11: the roll hidden once it is out
				rollCulledOn_ = visual_.get();                                                          // v1.7.12: only while the bed is really there
				rollCulled_ = Gear::BedrollShown();
				if (auto* roll = FindShape(visual_.get(), "RowRim_Clutter_Bedroll")) roll->SetAppCulled(rollCulled_);
			}
			TendSleepingBag(dt);  // v1.7.48
			TendOars(dt);  // v0.8.4: here, after the player's update, as OAS TendBakedBoatOars (the camera hook never called it)
			TendHullRipples(dt);  // v1.7.43
			TendOarlocks();       // v1.7.52
			TendFootIK();         // v1.7.52
			TendTameFish(dt);     // v1.7.52
			if (havePose_) {  // v1.7.54: bumping the lake boats
				const float hit = LakeBoats::BumpPlayerBoat(pos_, yaw_ + (bowFlip_ ? kPi : 0.0f), speed_, dt);
				if (hit > 0.0f) dipVel_ -= 45.0f * hit;
			}
			MotionA1();    // v1.7.13

			LogFrame(false, dt);
		}

	private:
		// v1.0.1: one step of the boat's target pose (sim, world, motion). Also run while the hull's collision 3D reloads after a
		// cell hop, so the boat (drawn by the carrier) and the seated player keep moving: those frames used to freeze it (the
		// stutter every cell crossing).
		void StepTarget(float dt, RE::NiPoint3& a_at, RE::NiMatrix3& a_rot, float& a_dYaw)
		{
			a_dYaw = 0.0f;
			lastThrust_ = 0.0f;
			if (helm_) {
				if (seated_ && (seatKind_ == SeatKind::kRower || Crew::Rower())) RowSim(dt);  // v1.7.9: or a crew rower, you at the stern
				else HelmSim(dt);
			}
			TendRowAnim(dt);
			rowShiftW_ = std::clamp(rowShiftW_ + (seated_ && seatKind_ == SeatKind::kRower && rowAnimOn_ ? 1.0f : -1.0f) * dt / 0.5f, 0.0f, 1.0f);
			if (mode_ != Mode::kFrozen) WorldStep(dt);  // v1.0.0: OAS water following and fake collision
			if (mode_ != Mode::kFrozen) {
				t_ += dt;
				a_dYaw = turn_ * dt;
				yaw_ += a_dYaw;
				const float hd = yaw_ + (bowFlip_ ? kPi : 0.0f);
				pos_.x += std::sin(hd) * speed_ * dt;
				pos_.y += std::cos(hd) * speed_ * dt;
			}
			// rock amplitude eased toward the setting (about 1 s), so a toggle never kicks the deck
			const float k = (std::min)(1.0f, dt * 2.0f);
			ampRoll_ += (rockRoll_ - ampRoll_) * k;
			ampPitch_ += (rockPitch_ - ampPitch_) * k;
			ampHeave_ += (rockHeave_ - ampHeave_) * k;
			const float w = 2.0f * kPi / period_;
			float mRoll = 0.0f, mPitch = 0.0f, mHeave = 0.0f;
			BoatMotion(dt, mRoll, mPitch, mHeave);  // v0.8.0: OAS idle rock, stroke squat, turn lean and the player's weight
			roll_ = ampRoll_ * std::sin(w * t_) + mRoll;      // rrt "rock N" (the step 1 test rock) on top
			pitch_ = ampPitch_ * std::sin(w * 0.73f * t_ + 1.1f) + mPitch;
			heave_ = ampHeave_ * std::sin(w * 1.25f * t_ + 0.4f) + mHeave;

			a_rot = RzM(yaw_) * (RxM(pitch_) * RyM(roll_));
			a_at = RE::NiPoint3{ pos_.x, pos_.y, pos_.z + heave_ };
		}
		void CoastFrame(float dt)
		{
			if (!havePose_) return;
			float dYaw = 0.0f;
			RE::NiPoint3 at;
			RE::NiMatrix3 rot;
			StepTarget(dt, at, rot, dYaw);
			if (seated_) GlueSeat(at, rot, dYaw, dt);
			else {
				TendNudge(dt);
				Crew::Linger(CrewPose(at, rot), dt);  // v1.7.21
			}
			if (carrierOn_) TendCarrier(at, rot, dt);
			prevAt_ = lastAt_;
			prevRot_ = lastRot_;
			havePrev_ = havePose_;
			lastAt_ = at;
			lastRot_ = rot;
			haveLastTarget_ = false;  // the body starts over from its new 3D
			TendOars(dt);
			MotionA1();  // v1.7.14: the motion log sees coast frames (cell hops) too
		}

		RE::TESObjectREFR* Ref() const
		{
			if (!refId_) return nullptr;
			auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(refId_);
			return ref && !ref->IsDeleted() ? ref : nullptr;
		}

		RE::hkpRigidBody* HkBody() const { return body_ ? body_->GetRigidBody() : nullptr; }

		static bool WaterAt(const RE::NiPoint3& a_pos, float& a_h)
		{
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(a_pos) : nullptr;
			return cell && cell->GetWaterHeight(a_pos, a_h) && std::isfinite(a_h) && std::abs(a_h) < 1000000.0f;
		}

		static RE::bhkRigidBody* FindBody(RE::NiAVObject* a_obj, int a_depth, std::string& a_where)
		{
			if (!a_obj || a_depth > 6) return nullptr;
			if (auto* co = a_obj->GetCollisionObject()) {
				if (auto* rb = co->GetRigidBody()) { a_where = a_obj->name.c_str(); return rb; }
			}
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) {
					if (auto* rb = FindBody(child.get(), a_depth + 1, a_where)) return rb;
				}
			}
			return nullptr;
		}

		void ReadBody(RE::NiPoint3& a_pos, Quat& a_q) const
		{
			const float inv = RE::bhkWorld::GetWorldScaleInverse();
			RE::hkVector4 p;
			RE::hkQuaternion q;
			body_->GetPosition(p);
			body_->GetRotation(q);
			float pf[4], qf[4];
			Get4(p, pf);
			Get4(q.vec, qf);
			a_pos = { pf[0] * inv, pf[1] * inv, pf[2] * inv };
			a_q = Norm(Quat{ qf[0], qf[1], qf[2], qf[3] });
		}

		bool InitBody(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root)
		{
			std::string where;
			auto* rb = FindBody(a_root, 0, where);
			if (!rb) {
				if (initTries_++ == 120) Print("RowRim: the hull has 3D but no Havok body after 120 frames (collision off?)");
				return false;
			}
			needInit_ = false;
			body_ = rb;
			haveLastTarget_ = false;
			auto* hb = rb->GetRigidBody();
			const int before = hb ? static_cast<int>(hb->motion.type.get()) : -1;
			const int layerBefore = static_cast<int>(a_root->GetCollisionLayer());
			if (layer_ != RE::COL_LAYER::kStatic) a_root->SetCollisionLayer(layer_);
			const bool set = a_root->SetMotionType(RE::hkpMotion::MotionType::kKeyframed, true, true, true);
			const int after = hb ? static_cast<int>(hb->motion.type.get()) : -1;

			// body offset from the ref (a bhkRigidBodyT carries its own translation / rotation)
			RE::NiPoint3 bp;
			Quat bq;
			ReadBody(bp, bq);
			const Quat qn = FromMatrix(a_root->world.rotate);
			const float dDirect = std::abs(Dot(qn, bq));
			const float dConj = std::abs(Dot(Conj(qn), bq));
			autoFlip_ = dConj > dDirect + 0.01f;
			const bool useFlip = flip_ != autoFlip_;
			const Quat qref = useFlip ? Conj(qn) : qn;
			rotOff_ = Norm(Mul(Conj(qref), bq));
			posOff_ = Rotate(Conj(qref), bp - a_root->world.translate);

			const std::string line = std::format("RowRim: body on node '{}', motion {} -> {} (SetMotionType {}), layer {} -> {}, body offset {:.1f} {:.1f} {:.1f}, rot offset w {:.3f}, quat {} (match direct {:.3f} conj {:.3f})",
				where, before, after, set ? 1 : 0, layerBefore, static_cast<int>(a_root->GetCollisionLayer()), posOff_.x, posOff_.y, posOff_.z, rotOff_.w,
				autoFlip_ ? "conjugate" : "direct", dDirect, dConj);
			if (inits_++ == 0) Print(line);  // first setup in the console, re-setups after a cell hop in the log only
			else logger::info("{}", line);
			if (after != static_cast<int>(RE::hkpMotion::MotionType::kKeyframed)) Print("RowRim: WARNING the body did not become keyframed; moving it may do nothing");
			return true;
		}

		static void PlaceNode(RE::NiAVObject* a_node, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot)
		{
			RE::NiMatrix3 parentRotInv = MakeMat(1, 0, 0, 0, 1, 0, 0, 0, 1);
			RE::NiPoint3 parentPos{};
			float parentScale = 1.0f;
			if (auto* parent = a_node->parent) {
				parentRotInv = parent->world.rotate.Transpose();
				parentPos = parent->world.translate;
				parentScale = parent->world.scale > 0.0f ? parent->world.scale : 1.0f;
			}
			a_node->local.rotate = parentRotInv * a_rot;
			a_node->local.translate = (parentRotInv * (a_pos - parentPos)) / parentScale;
			RE::NiUpdateData data{};
			a_node->Update(data);
		}

		// Where an actor's feet are in the boat's frame, and whether the boat body is what they stand on.
		std::string ActorLine(RE::Actor* a_actor)
		{
			if (!a_actor) return {};
			const RE::NiPoint3 feet = a_actor->GetPosition();
			const RE::NiMatrix3 rot = RzM(yaw_) * (RxM(pitch_) * RyM(roll_));
			const RE::NiPoint3 local = rot.Transpose() * (feet - RE::NiPoint3{ pos_.x, pos_.y, pos_.z + heave_ });
			int state = -1;
			bool onUs = false;
			if (auto* cc = a_actor->GetCharController()) {
				state = static_cast<int>(cc->context.currentState);
				onUs = cc->supportBody.get() == HkBody();
			}
			return std::format("{} local {:.0f} {:.0f} {:.1f} (deck {:.0f}) state {} on hull {}", a_actor->GetName(), local.x, local.y, local.z, kDeckZ, state, onUs ? 1 : 0);
		}

		void LogFrame(bool a_now, float a_dt = 0.0f)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			// jitter: spread of the player's height over the deck in each half second window
			const RE::NiPoint3 feet = player->GetPosition();
			const RE::NiMatrix3 rot = RzM(yaw_) * (RxM(pitch_) * RyM(roll_));
			const float lz = (rot.Transpose() * (feet - RE::NiPoint3{ pos_.x, pos_.y, pos_.z + heave_ })).z;
			zMin_ = (std::min)(zMin_, lz);
			zMax_ = (std::max)(zMax_, lz);
			logTimer_ += a_dt;
			if (!a_now && logTimer_ < 0.5f) return;
			logTimer_ = 0.0f;
			RE::NiPoint3 bp;
			Quat bq;
			if (body_) ReadBody(bp, bq);
			std::string s = std::format("t {:.1f} roll {:.1f} pitch {:.1f} heave {:.1f} err {:.1f} | body z {:.1f} node z {:.1f} | player spread {:.1f} | {}",
				t_, roll_ * 180.0f / kPi, pitch_ * 180.0f / kPi, heave_, lastErr_, bp.z,
				Ref() && Ref()->Get3D() ? Ref()->Get3D()->world.translate.z : 0.0f, zMax_ - zMin_, ActorLine(player));
			if (auto* r = Ref()) {
				auto* c = r->GetParentCell();
				auto* pc = player->GetParentCell();
				auto* root = r->Get3D();
				s += std::format(" | hull cell {:08X}{} player cell {:08X} 3D {} culled {}", c ? c->GetFormID() : 0, c && c->IsAttached() ? "" : " (detached)",
					pc ? pc->GetFormID() : 0, root ? "yes" : "NO", root && root->GetAppCulled() ? 1 : 0);
			}
			if (carrierOn_) {
				auto* a = carrierId_ ? RE::TESForm::LookupByID<RE::Actor>(carrierId_) : nullptr;
				auto* ac = a ? a->GetParentCell() : nullptr;
				auto* a3 = a ? a->Get3D() : nullptr;
				s += std::format(" | carrier cell {:08X}{} 3D {} culled {} bound r {:.0f} | boat model {} culled {}", ac ? ac->GetFormID() : 0, ac && ac->IsAttached() ? "" : " (detached)",
					a3 ? "yes" : "NO", a3 && a3->GetAppCulled() ? 1 : 0, a3 ? a3->worldBound.radius : 0.0f,
					visual_ ? (visual_->parent ? "attached" : "not attached") : "none", visual_ && visual_->GetAppCulled() ? 1 : 0);
			}
			if (watchId_) {
				if (auto* a = RE::TESForm::LookupByID<RE::Actor>(watchId_)) s += " | " + ActorLine(a);
			}
			zMin_ = 1e9f;
			zMax_ = -1e9f;
			if (a_now) Print("RowRim: " + s);
			else logger::info("{}", s);
		}

		// The ref stays in the cell it was placed in. Once the boat has moved on, that cell is culled and later detached
		// (the hull went invisible). Like OAS CellHop: move the ref into the player's cell when they differ.
		bool CellHop(RE::TESObjectREFR* a_ref)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* pcell = player ? player->GetParentCell() : nullptr;
			if (!pcell || !pcell->IsExteriorCell() || !pcell->IsAttached()) return false;
			if (a_ref->GetParentCell() == pcell) return false;
			const RE::NiPoint3 at{ pos_.x, pos_.y, pos_.z + heave_ };
			if (player->GetPosition().GetDistance(at) > 3000.0f) return false;  // only a boat near the player follows
			auto* from = a_ref->GetParentCell();
			auto* rootBefore = a_ref->Get3D();
			// v0.2.6: the old model is kept and drawn (under the player's cell node) until the reloaded one is in, so the
			// MoveTo reload no longer blinks. Its target is worked out now, while it still hangs in its group slot.
			RE::NiPointer<RE::NiAVObject> held(rootBefore);
			RE::NiNode* heldParent = rootBefore ? GraftTarget(rootBefore) : nullptr;
			// v0.2.2: first the engine's own SetPosition alone (what actors get: refiled by position, model kept?);
			// MoveTo (v0.2.1, always reloaded the model = the fade) only if that leaves the ref in its old cell.
			a_ref->data.angle = RE::NiPoint3{ 0.0f, 0.0f, yaw_ };
			a_ref->SetPosition(at);
			const char* how = "SetPosition";
			if (a_ref->GetParentCell() == from) {
				a_ref->MoveTo(player);
				a_ref->data.angle = RE::NiPoint3{ 0.0f, 0.0f, yaw_ };
				a_ref->SetPosition(at);
				how = "MoveTo (SetPosition kept the old cell)";
			}
			++hopsSinceLoad_;
			auto* to = a_ref->GetParentCell();
			auto* rootAfter = a_ref->Get3D();
			if (rootAfter != rootBefore) {  // v1.2.2: the old body went with the old 3D; never read it again
				body_ = nullptr;
				needInit_ = true;
				haveLastTarget_ = false;
			}
			if (held && rootAfter != rootBefore && heldParent && !carrierOn_) {
				if (held->parent && held->parent != heldParent) {
					RE::NiPointer<RE::NiAVObject> tmp;
					held->parent->DetachChild(held.get(), tmp);
				}
				if (!held->parent) heldParent->AttachChild(held.get(), true);
				heldOld_ = held;
				heldTime_ = 0.0f;
			}
			logger::info("Hull: cell hop {:08X} -> {:08X} via {}: {}, 3D {}", from ? from->GetFormID() : 0, to ? to->GetFormID() : 0, how,
				to == pcell ? "ok" : "NOT the player's cell", !rootAfter ? "unloaded (reloading)" : rootAfter == rootBefore ? "kept" : "replaced");
			return rootAfter != rootBefore || !rootAfter;  // the frame is skipped only if the model changed
		}

		// v1.5.7: put the hull back into the player's cell (MoveTo reloads its 3D and its collision); only near the player
		bool Rehome(RE::TESObjectREFR* a_ref, const char* a_why)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* pcell = player ? player->GetParentCell() : nullptr;
			if (!pcell || !pcell->IsExteriorCell() || !pcell->IsAttached()) return false;
			const RE::NiPoint3 at{ pos_.x, pos_.y, pos_.z + heave_ };
			if (player->GetPosition().GetDistance(at) > 3000.0f) return false;  // still away: wait
			auto* from = a_ref->GetParentCell();
			auto* rootBefore = a_ref->Get3D();
			a_ref->MoveTo(player);
			a_ref->data.angle = RE::NiPoint3{ 0.0f, 0.0f, yaw_ };
			a_ref->SetPosition(at);
			body_ = nullptr;
			needInit_ = true;
			haveLastTarget_ = false;
			cellLost_ = false;
			++hopsSinceLoad_;
			auto* to = a_ref->GetParentCell();
			logger::info("Hull: re-homed {:08X} -> {:08X} ({}), 3D {}", from ? from->GetFormID() : 0, to ? to->GetFormID() : 0, a_why,
				!a_ref->Get3D() ? "unloaded (reloading)" : a_ref->Get3D() == rootBefore ? "kept (collision set up again)" : "replaced");
			return true;
		}

		void DropHeld()
		{
			if (heldOld_ && heldOld_->parent) {
				RE::NiPointer<RE::NiAVObject> tmp;
				heldOld_->parent->DetachChild(heldOld_.get(), tmp);
			}
			heldOld_.reset();
		}

		// The old model held through a cell hop: moved with the boat until the new one is in the scene, then dropped.
		void TendHeld(const RE::NiPoint3& a_at, const RE::NiMatrix3& a_rot, RE::NiAVObject* a_newRoot, float a_dt)
		{
			if (!heldOld_) return;
			heldTime_ += a_dt;
			const bool newIn = a_newRoot && a_newRoot != heldOld_.get() && a_newRoot->parent;
			if (newIn || heldTime_ > 3.0f) {
				if (heldOld_->parent) {
					RE::NiPointer<RE::NiAVObject> tmp;
					heldOld_->parent->DetachChild(heldOld_.get(), tmp);
				}
				logger::info("Hull: old model dropped after {:.2f} s ({})", heldTime_, newIn ? "new model in" : "timed out");
				heldOld_.reset();
				return;
			}
			if (heldOld_->parent) PlaceNode(heldOld_.get(), a_at, a_rot);
		}

		// v0.2.4: the persistent hull keeps its model and its Havok body when its first cell detaches (v0.2.3 log: 3D yes,
		// err small), it just stops being drawn: its node still hangs under the old cell's scene node. The node alone is
		// moved under the player's cell node (same group slot). No reload, no fade; the ref stays filed where it was.
		RE::NiNode* GraftTarget(RE::NiAVObject* a_root) const
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* pcell = player ? player->GetParentCell() : nullptr;
			if (!pcell || !pcell->IsAttached()) return nullptr;
			auto* ld = pcell->GetRuntimeData().loadedData;
			auto* cell3D = ld ? ld->cell3D.get() : nullptr;
			if (!cell3D) return nullptr;
			int slot = -1;
			if (auto* cur = a_root->parent; cur && cur->parent) {
				auto& kids = cur->parent->GetChildren();
				for (std::uint32_t i = 0; i < kids.size(); ++i) {
					if (kids[i].get() == cur) { slot = static_cast<int>(i); break; }
				}
			}
			auto& groups = cell3D->GetChildren();
			if (slot >= 0 && static_cast<std::uint32_t>(slot) < groups.size() && groups[slot]) {
				if (auto* n = groups[slot]->AsNode()) return n;
			}
			return cell3D;
		}

		void Regraft(RE::NiAVObject* a_root)
		{
			auto* target = GraftTarget(a_root);
			if (!target || a_root->parent == target) return;
			auto* from = a_root->parent;
			RE::NiPointer<RE::NiAVObject> keep(a_root);
			if (from) from->DetachChild(a_root, keep);
			target->AttachChild(a_root, true);
			if (grafts_++ < 40) logger::info("Hull: node moved under the player's cell node '{}' (was under '{}')", target->name.c_str(), from ? from->name.c_str() : "nothing");
		}

		// v0.2.5: v0.2.4 crashed in the renderer drawing the hull ~8 s after its first cell (where it was still filed) was fully
		// unloaded. Followers do not have that problem: they live in the worldspace's persistent cell and are listed as mobile
		// persistent refs, so the engine loads them wherever they are. The hull is put there the same way (one MoveTo at spawn).
		static void MakeMobile(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_at, float a_yaw)
		{
			auto* world = a_ref->GetWorldspace();
			auto* pcell = world ? world->persistentCell : nullptr;
			if (!pcell) { Print("RowRim: no worldspace persistent cell here; the hull stays in its cell"); return; }
			// TESObjectREFR::MoveTo_Impl is private in CommonLib: same engine function, called directly (same IDs as CommonLib)
			using MoveTo_t = void(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*, const RE::NiPoint3&, const RE::NiPoint3&);
			static REL::Relocation<MoveTo_t> moveTo{ RELOCATION_ID(56227, 56626) };
			const RE::ObjectRefHandle none{};
			const RE::NiPoint3 rot{ 0.0f, 0.0f, a_yaw };
			moveTo(a_ref, none, pcell, world, a_at, rot);
			auto& list = world->mobilePersistentRefs;
			const bool before = std::any_of(list.begin(), list.end(), [&](const RE::NiPointer<RE::TESObjectREFR>& r) { return r.get() == a_ref; });
			if (!before) list.push_back(RE::NiPointer<RE::TESObjectREFR>(a_ref));
			auto* now = a_ref->GetParentCell();
			Print(std::format("RowRim: hull moved to the persistent cell {:08X} ({}), mobile list {} ({} refs)", pcell->GetFormID(),
				now == pcell ? "ok" : std::format("FAILED, in {:08X}", now ? now->GetFormID() : 0), before ? "already had it" : "added", list.size()));
		}

		static void RemoveMobile(RE::TESObjectREFR* a_ref)
		{
			auto* world = a_ref->GetWorldspace();
			if (!world) return;
			auto& list = world->mobilePersistentRefs;
			for (auto it = list.begin(); it != list.end(); ++it) {
				if (it->get() == a_ref) {
					list.erase(it);
					logger::info("Hull: removed from the mobile persistent list");
					return;
				}
			}
		}

		// ---- v0.3.0: the carrier actor ----
		// The boat's model rides on a hidden actor, the way a horse is handled: the engine refiles actors into the cell they
		// are in, keeping their model, so whatever hangs on the actor never fades, blinks or is left in an unloaded cell.
		// The collision stays the keyframed hull ref (hidden, cell-hopped: its reload cannot be seen).
		static RE::TESNPC* FindCarrierBase()
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) return nullptr;
			for (auto* npc : dh->GetFormArray<RE::TESNPC>()) {
				if (!npc || npc->IsUnique() || !npc->race) continue;
				const char* edid = npc->race->formEditorID.c_str();
				if (edid && std::string_view(edid) == "HorseRace") return npc;
			}
			return nullptr;
		}

		// v1.4.3: the hidden horse can still be looked at from under the boat ("[E] Ride Horse": riding it broke the game).
		// Its activation and its activate text are blocked (the AE BlockActivation(true, true) flags), for the player too.
		static void ShieldCarrier(RE::Actor* a_actor)
		{
			if (!a_actor) return;
			a_actor->extraList.SetExtraFlags(RE::ExtraFlags::Flag::kBlockActivate, true);
			a_actor->extraList.SetExtraFlags(RE::ExtraFlags::Flag::kBlockPlayerActivate, true);
			a_actor->extraList.SetExtraFlags(RE::ExtraFlags::Flag::kBlockActivateText, true);
		}

		// Clear existing native handles (including sounds started before spawn
		// returned). New animation/footstep sounds are blocked by the hooks below.
		static void MuteCarrier(RE::Actor* a_actor)
		{
			auto* process = a_actor ? a_actor->GetActorRuntimeData().currentProcess : nullptr;
			auto* high = process ? process->high : nullptr;
			// v1.7.52: the horse's breathing is its creature "awake" loop (and its movement loop), kept on the actor as extra
			// data, not in the process's sound handles: held at volume 0 every frame (stopped, the game started it again)
			if (a_actor) {
				if (auto* aw = a_actor->extraList.GetByType<RE::ExtraCreatureAwakeSound>(); aw && aw->handle.IsValid()) aw->handle.SetVolume(0.0f);
				if (auto* mv = a_actor->extraList.GetByType<RE::ExtraCreatureMovementSound>(); mv && mv->handle.IsValid()) mv->handle.SetVolume(0.0f);
				if (auto* xs = a_actor->extraList.GetByType<RE::ExtraSound>(); xs && xs->handle.IsValid()) xs->handle.SetVolume(0.0f);
			}
			if (!high) return;
			for (auto& sound : high->soundHandles) {
				if (sound.IsValid()) {
					sound.SetVolume(0.0f);
					sound.Stop();
					sound = RE::BSSoundHandle{};
				}
			}
		}

		void SpawnCarrier(const RE::NiPoint3& a_at)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* base = FindCarrierBase();
			if (!player || !base) { Print("RowRim: no carrier base (a non-unique HorseRace NPC) found; the hull draws itself"); return; }
			auto ref = player->PlaceObjectAtMe(base, true);
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) { Print("RowRim: carrier spawn failed"); return; }
			carrierId_ = actor->GetFormID();
			CarrierAudio::id.store(carrierId_, std::memory_order_relaxed);
			actor->SetCollision(false);  // no capsule in the boat, nothing to bump or pick
			actor->EnableAI(carrierAI_);
			ShieldCarrier(actor);
			MuteCarrier(actor);
			actor->SetPosition(CarrierSpot(a_at), true);
			visual_.reset();
			wbShown_ = nullptr;
			hiddenKids_ = 0;
			Print(std::format("RowRim: carrier {:08X} ('{}', base {:08X}) placed {} units under the boat, audio event gate on, AI {}", carrierId_, base->GetName(), base->GetFormID(),
				static_cast<int>(kCarrierDepth), carrierAI_ ? "on" : "off"));
		}

		static RE::NiPoint3 CarrierSpot(const RE::NiPoint3& a_at) { return { a_at.x, a_at.y, a_at.z - kCarrierDepth }; }

		void DeleteCarrier()
		{
			if (visual_ && visual_->parent) {
				RE::NiPointer<RE::NiAVObject> tmp;
				visual_->parent->DetachChild(visual_.get(), tmp);
			}
			visual_.reset();
			wbShown_ = nullptr;
			if (auto* a = carrierId_ ? RE::TESForm::LookupByID<RE::Actor>(carrierId_) : nullptr) {
				LakeBoats::DeleteActorLater(a);  // v1.4.1: disabled now, deleted 2 s later (never in the same frame)
			}
			carrierId_ = 0;
			CarrierAudio::id.store(0, std::memory_order_relaxed);
		}

		// The boat model: a clone of the hull's own nif, physics stripped (the hull ref keeps the collision).
		RE::NiPointer<RE::NiNode> MakeVisual()
		{
			RE::NiPointer<RE::NiNode> out;
			auto* ref = Ref();
			auto* base = ref ? ref->GetBaseObject() : nullptr;
			auto* model = base ? base->As<RE::TESModel>() : nullptr;
			const char* path = model ? model->GetModel() : nullptr;
			if (!path || !*path) { logger::warn("Carrier: hull base has no model path"); return out; }
			RE::NiPointer<RE::NiNode> src;
			const RE::BSModelDB::DBTraits::ArgsType args{};
			// v0.4.0: our own hull nif first (vanilla hull + bilge floor + water blocker planes), the hull base's model if missing
			RE::BSModelDB::Demand(kOwnHullModel, src, args);
			if (!src) RE::BSModelDB::Demand((std::string("meshes\\") + kOwnHullModel).c_str(), src, args);
			if (src) path = kOwnHullModel;
			else {
				logger::info("Carrier: {} not found, using the hull base's model", kOwnHullModel);
				RE::BSModelDB::Demand(path, src, args);
			}
			if (!src) RE::BSModelDB::Demand((std::string("meshes\\") + path).c_str(), src, args);
			if (!src) { logger::warn("Carrier: model '{}' not loaded", path); return out; }
			auto* cloneObj = src->Clone();
			auto* clone = cloneObj ? cloneObj->AsNode() : nullptr;
			if (!clone) { logger::warn("Carrier: clone of '{}' failed", path); return out; }
			StripPhysics(clone);
			clone->local = RE::NiTransform{};
			out.reset(clone);
			logger::info("Carrier: boat model '{}' cloned", path);
			ApplyWaterBlock(clone);
			AttachOars(clone);
			return out;
		}

		// ---- v0.4.0: water blocker test ----
		// RowRim_Hull.nif carries flat, hull-shaped planes at model z 46 (about seat height), one per material trick, all
		// meant to write depth without colour so the lake surface is not drawn inside the hull:
		//   WB_A  alpha blend, alpha 0, z write;  WB_B  the same + "no sorter";  WB_C  blend ZERO/ONE + alpha test ALWAYS
		//   WB_Debug  the same plane drawn with the hull texture (to see where it is).  BilgeFloor: the OAS floor (off).
		// One shape is shown at a time (rr "wb a|b|c|debug|off"); rr "wb floor" toggles the floor; rr "wb z N" moves the
		// plane N units from its built height.
		static constexpr const char* kOwnHullModel = "RowRim\\RowRim_Hull.nif";
		static RE::NiAVObject* FindShape(RE::NiAVObject* a_obj, std::string_view a_name, int a_depth = 0)
		{
			if (!a_obj || a_depth > 6) return nullptr;
			if (a_obj->name.c_str() && a_name == a_obj->name.c_str()) return a_obj;
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) {
					if (auto* f = FindShape(child.get(), a_name, a_depth + 1)) return f;
				}
			}
			return nullptr;
		}
		// v0.4.1: planes WB_A_<z> / WB_D_<z> at z 30..48 (every 2), each cut to the hull at its own height. v0.4.0 had one at
		// 46: the camera sitting low in the boat was under it and saw the water beneath. Now the plane just above the current
		// waterline is shown (TendWaterBlock), so the camera is always above it.
		static constexpr int kWbLow = 30, kWbHigh = 48, kWbStep = 2, kWbCount = (kWbHigh - kWbLow) / kWbStep + 1;
		void ApplyWaterBlock(RE::NiAVObject* a_root)
		{
			wbShown_ = nullptr;
			wbShownZ_ = -1;
			if (!a_root) return;
			int found = 0;
			for (int i = 0; i < kWbCount; ++i) {
				const int z = kWbLow + i * kWbStep;
				wbA_[i] = FindShape(a_root, std::format("WB_A_{}", z));
				wbD_[i] = FindShape(a_root, std::format("WB_D_{}", z));
				for (auto* sh : { wbA_[i], wbD_[i] }) {
					if (!sh) continue;
					++found;
					sh->flags.set(RE::NiAVObject::Flag::kHidden);
				}
			}
			if (auto* fl = FindShape(a_root, "BilgeFloor")) {
				if (wbFloor_) fl->flags.reset(RE::NiAVObject::Flag::kHidden);
				else fl->flags.set(RE::NiAVObject::Flag::kHidden);
			}
			RE::NiUpdateData data{};
			a_root->Update(data);
			logger::info("Water block: mode {}, floor {}, {} plane(s) in the model", wbMode_.empty() ? "off" : wbMode_, wbFloor_ ? "on" : "off", found);
		}

		// Shows the plane just above the water inside the hull: the water's height in the boat's frame at the middle, plus
		// what the roll and pitch lift it at the hull's sides and ends, plus a margin; rounded up to the next plane.
		void TendWaterBlock(float a_dt)
		{
			if (!visual_) return;
			wbTimer_ += a_dt;
			if (wbTimer_ >= 0.25f || !wbHaveWater_) {
				wbTimer_ = 0.0f;
				float w = 0.0f;
				wbHaveWater_ = WaterAt(pos_, w);
				wbWater_ = w;
			}
			int want = -1;
			if (!wbMode_.empty() && wbHaveWater_) {
				const float zw = wbWater_ - (pos_.z + heave_);
				const float tilt = std::abs(std::sin(roll_)) * 60.0f + std::abs(std::sin(pitch_)) * 150.0f;
				const float need = zw + tilt + kWbMargin + wbZ_;
				int i = static_cast<int>(std::ceil((need - kWbLow) / kWbStep));
				i = std::clamp(i, 0, kWbCount - 1);
				want = i;
				wbNeed_ = need;
			}
			RE::NiAVObject* sh = want < 0 ? nullptr : (wbMode_ == "debug" ? wbD_[want] : wbA_[want]);
			if (sh == wbShown_) return;
			if (wbShown_) wbShown_->flags.set(RE::NiAVObject::Flag::kHidden);
			if (sh) sh->flags.reset(RE::NiAVObject::Flag::kHidden);
			wbShown_ = sh;
			const int z = want < 0 ? -1 : kWbLow + want * kWbStep;
			if (wbLogs_++ < 60) logger::info("Water block: plane {} at model z {} (water needs {:.1f}, water {})", wbMode_.empty() ? "off" : wbMode_, z, wbNeed_,
				wbHaveWater_ ? std::format("{:.0f}", wbWater_) : std::string("none here"));
			wbShownZ_ = z;
		}

		static void StripPhysics(RE::NiAVObject* a_obj, int a_depth = 0)
		{
			if (!a_obj || a_depth > 8) return;
			if (a_obj->collisionObject) a_obj->collisionObject.reset();
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) StripPhysics(child.get(), a_depth + 1);
			}
		}

		void TendCarrier(const RE::NiPoint3& a_at, const RE::NiMatrix3& a_rot, float a_dt)
		{
			auto* a = carrierId_ ? RE::TESForm::LookupByID<RE::Actor>(carrierId_) : nullptr;
			if (!a || a->IsDeleted()) return;
			CarrierAudio::id.store(carrierId_, std::memory_order_relaxed);
			MuteCarrier(a);  // also covers restored saves and process/3D reloads
			if (!aiApplied_) { a->EnableAI(carrierAI_); a->SetCollision(false); ShieldCarrier(a); aiApplied_ = true; }
			a->SetPosition(CarrierSpot(a_at), true);
			// v0.3.2: with AI off, SetPosition moved the actor (its cell followed) but not its 3D: the horse's model stayed at
			// the spawn spot (bound radius grew with the distance rowed) and the game faded the actor out by that spot's
			// distance, boat and all. Its 3D is now put where the actor is every frame.
			a->Update3DPosition(true);
			auto* aroot = a->Get3D();
			if (!aroot) return;
			if (auto* fade = aroot->AsFadeNode()) fade->GetRuntimeData().currentFade = 1.0f;  // not loaded yet: the hidden-collision rule keeps the hull ref drawn meanwhile
			auto* anode = aroot->AsNode();
			if (!anode) return;
			if (!visual_) visual_ = MakeVisual();
			if (!visual_) return;
			if (visual_->parent != anode) {  // first time, or the carrier's 3D was reloaded
				if (visual_->parent) {
					RE::NiPointer<RE::NiAVObject> tmp;
					visual_->parent->DetachChild(visual_.get(), tmp);
				}
				anode->AttachChild(visual_.get(), true);
				hiddenKids_ = 0;
				logger::info("Carrier: boat model attached to the carrier's 3D");
			}
			// the horse itself stays hidden: everything on its root but our boat (re-done twice a second: equipment can add parts)
			hideTimer_ += a_dt;
			if (hiddenKids_ == 0 || hideTimer_ > 0.5f) {
				hideTimer_ = 0.0f;
				int n = 0;
				for (auto& child : anode->GetChildren()) {
					if (child && child.get() != visual_.get() && !child->flags.any(RE::NiAVObject::Flag::kHidden)) {
						child->flags.set(RE::NiAVObject::Flag::kHidden);
						++n;
					}
				}
				if (n && hideLogs_++ < 10) logger::info("Carrier: hid {} part(s) of the carrier", n);
				if (!a->IsActivationBlocked()) ShieldCarrier(a);  // v1.4.3: older saves' carriers too
				hiddenKids_ = 1;
			}
			KeepOpaque(visual_.get());
			if (paintedRoot_ != visual_.get() || paintedAs_ != paint_) {  // v1.1.0: the OAS paint (rr paint ...), once per model / paint
				Paint::Apply(visual_.get(), paint_);
				paintedRoot_ = visual_.get();
				paintedAs_ = paint_;
			}
			PlaceNode(visual_.get(), a_at, a_rot);
			TendLamp(a_dt);
			TendTent(a_dt);  // v1.7.34
			Crew::g.dogStern = Gear::BedrollShown();  // v1.7.36: the dog sits at the stern while the bed is out
			// the carrier's bound must take in the boat, or the renderer culls them both by the horse alone (a frozen actor
			// does not rebuild it): rebuilt from the boat up to the scene root every frame
			for (RE::NiAVObject* n = anode; n; n = n->parent) n->UpdateWorldBound();
		}

		// ---- v1.7.34: the tent over the bed fades in (and out) over 1.5 s with the bed; a lit lantern on the floor by the bed ----
		float tentA_{ 0.0f };
		float lampA_{ 0.0f };  // v1.7.40: the lantern comes with the bed (half way into its fade)
		RE::NiAVObject* tentNode_{ nullptr };
		const RE::NiAVObject* tentOn_{ nullptr };
		RE::NiPointer<RE::NiNode> bedLamp_;
		RE::ObjectRefHandle bedLight_;
		float bedLightCellTimer_{ 0.0f };
		static inline const RE::NiPoint3 kBedLampAt{ 32.0f, 150.0f, 40.4f };  // v1.7.52: 16 further forward, in a little (it clipped the bow thwart)  // v1.7.35: on the floorboards at the bow thwart's forward edge, starboard (clear of the bed)
		// v1.7.37: blending only while it fades: fully up it draws solid (blended, the wood just under the cloth showed
		// through in spots)
		static void SetAlphaTree(RE::NiAVObject* a_obj, float a_alpha, int a_depth = 0)
		{
			if (!a_obj || a_depth > 6) return;
			if (auto* g = a_obj->AsGeometry()) {
				if (auto* ap = g->GetGeometryRuntimeData().alphaProperty.get()) ap->SetAlphaBlending(a_alpha < 0.999f);
				if (auto* p = g->lightingShaderProp_cast()) {
					if (auto* m = static_cast<RE::BSLightingShaderMaterialBase*>(p->material)) m->materialAlpha = a_alpha;
				}
			}
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) SetAlphaTree(child.get(), a_alpha, a_depth + 1);
			}
		}
		void DropBedLamp()
		{
			if (bedLamp_ && bedLamp_->parent) {
				RE::NiPointer<RE::NiAVObject> tmp;
				bedLamp_->parent->DetachChild(bedLamp_.get(), tmp);
			}
			LakeBoats::DeleteRef(bedLight_);
		}
		void TendTent(float a_dt)
		{
			if (!visual_) return;
			if (tentOn_ != visual_.get()) {
				tentOn_ = visual_.get();
				tentNode_ = FindShape(visual_.get(), "RowRim_Clutter_Tent");
				if (bedLamp_ && bedLamp_->parent) {
					RE::NiPointer<RE::NiAVObject> tmp;
					bedLamp_->parent->DetachChild(bedLamp_.get(), tmp);
				}
			}
			static constexpr bool kTentOn = false;  // v1.7.40: the tent is off for this release (the lantern stays with the bed)
			const bool bedOut = Gear::BedrollShown();
			const bool want = bedOut && kTentOn;
			const float before = tentA_;
			tentA_ = std::clamp(tentA_ + (want ? 1.0f : -1.0f) * a_dt / 1.5f, 0.0f, 1.0f);
			if (tentNode_) {
				if (tentA_ <= 0.0f && before > 0.0f) SetAlphaTree(tentNode_, 1.0f);  // v1.7.37: put back whole before it is hidden
				tentNode_->SetAppCulled(tentA_ <= 0.0f);
				if (tentA_ > 0.0f && (tentA_ != before || tentA_ < 1.0f)) SetAlphaTree(tentNode_, tentA_ >= 1.0f ? 1.0f : tentA_ * tentA_ * (3.0f - 2.0f * tentA_));
			}
			// the lantern: lit while the tent is (more than half) up
			lampA_ = std::clamp(lampA_ + (bedOut ? 1.0f : -1.0f) * a_dt / 1.5f, 0.0f, 1.0f);
			const bool lampWant = lampA_ > 0.5f && bedOut;
			if (!lampWant) {
				if (bedLamp_ && bedLamp_->parent) DropBedLamp();
				else if (bedLight_) LakeBoats::DeleteRef(bedLight_);
				return;
			}
			if (!bedLamp_) {
				bedLamp_ = RE::NiPointer<RE::NiNode>(MakeLampClone(true));
				if (!bedLamp_) return;
				bedLamp_->name = "RowRim Bed Lantern";
			}
			float bottom = 0.0f, mid = 0.0f;
			if (auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(LakeBoats::kLampStatic)) {
				bottom = static_cast<float>(base->boundData.boundMin.z);
				mid = 0.5f * (static_cast<float>(base->boundData.boundMax.z) + bottom);
			}
			const RE::NiTransform& vw = visual_->world;
			const float vs = vw.scale > 0.0f ? vw.scale : 1.0f;
			if (bedLamp_->parent != visual_.get()) {
				if (bedLamp_->parent) {
					RE::NiPointer<RE::NiAVObject> tmp;
					bedLamp_->parent->DetachChild(bedLamp_.get(), tmp);
				}
				visual_->AttachChild(bedLamp_.get(), true);
				logger::info("Lamp: the bed lantern is lit by the bed");
			}
			bedLamp_->local.rotate = RzM(0.6f);  // turned a little, set down by hand
			bedLamp_->local.translate = kBedLampAt - RE::NiPoint3{ 0.0f, 0.0f, bottom / vs };
			bedLamp_->local.scale = 1.0f / vs;
			Oar::KeepFade(bedLamp_.get());
			RE::NiUpdateData data{};
			bedLamp_->Update(data);
			const RE::NiPoint3 centre = vw.translate + vw.rotate * ((kBedLampAt + RE::NiPoint3{ 0.0f, 0.0f, (mid - bottom) / vs }) * vs);
			auto light = bedLight_.get();
			if (!light) {
				if (auto* hull = Ref()) {
					LakeBoats::PlaceAt(hull, RE::TESForm::LookupByID<RE::TESBoundObject>(LakeBoats::kLampLight), "bed light", bedLight_);
					light = bedLight_.get();
				}
			}
			if (light) {
				light->data.location = centre;
				if (light->Get3D()) LakeBoats::PlaceAbs(light->Get3D(), centre, vw.rotate, 1.0f);
				if ((bedLightCellTimer_ -= a_dt) <= 0.0f) {
					bedLightCellTimer_ = 0.5f;
					auto* tes = RE::TES::GetSingleton();
					auto* cellNow = tes ? tes->GetCell(centre) : nullptr;
					if (cellNow && cellNow->IsAttached() && light->GetParentCell() != cellNow) light->SetPosition(centre);
				}
			}
		}

		// ---- v1.4.4: the player's lantern, hung from the stern spiral (LakeBoats::kLampHook) and swinging like the OAS one ----
		void DropLamp()
		{
			if (lamp_ && lamp_->parent) {
				RE::NiPointer<RE::NiAVObject> tmp;
				lamp_->parent->DetachChild(lamp_.get(), tmp);
			}
			lamp_.reset();
			LakeBoats::DeleteRef(lampLight_);
			lamp_st_ = LampSwing{};
		}

	public:
		void SetLamp(bool a_on)
		{
			lampOn_ = a_on;
			if (!a_on) DropLamp();
			Print(std::format("RowRim: boat lantern {}", a_on ? "on (lit at night)" : "off"));
		}

	private:

		static RE::NiNode* MakeLampClone(bool a_flame = false)
		{
			auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(LakeBoats::kLampStatic);
			auto* model = base ? base->As<RE::TESModel>() : nullptr;
			const char* path = model ? model->GetModel() : nullptr;
			if (!path || !*path) { logger::warn("Lamp: the lantern has no model path"); return nullptr; }
			RE::NiPointer<RE::NiNode> src;
			const RE::BSModelDB::DBTraits::ArgsType args{};
			RE::BSModelDB::Demand(path, src, args);
			if (!src) RE::BSModelDB::Demand((std::string("meshes\\") + path).c_str(), src, args);
			if (!src) { logger::warn("Lamp: model '{}' not loaded", path); return nullptr; }
			auto* cloneObj = src->Clone();
			auto* clone = cloneObj ? cloneObj->AsNode() : nullptr;
			if (!clone) return nullptr;
			Oar::StripTree(clone);
			clone->local = RE::NiTransform{};
			const int culled = a_flame ? 0 : LakeBoats::CullFlames(clone);  // v1.7.34: the bed lamp keeps its flame
			auto* wrap = RE::NiNode::Create(1);
			wrap->name = "RowRim Lantern";
			wrap->AttachChild(clone, true);
			logger::info("Lamp: player lantern made from '{}', {} flame part(s) switched off", path, culled);
			return wrap;
		}

		void TendLamp(float a_dt)
		{
			if (!lampOn_ || !visual_) return;
			if (!lamp_) {
				lamp_ = RE::NiPointer<RE::NiNode>(MakeLampClone());
				if (!lamp_) { lampOn_ = false; return; }
				lamp_st_ = LampSwing{};
			}
			if (lamp_->parent != visual_.get()) {
				if (lamp_->parent) {
					RE::NiPointer<RE::NiAVObject> tmp;
					lamp_->parent->DetachChild(lamp_.get(), tmp);
				}
				visual_->AttachChild(lamp_.get(), true);
				lamp_st_ = LampSwing{};
			}
			const RE::NiTransform& vw = visual_->world;
			const RE::NiPoint3 pivot = vw.translate + vw.rotate * (LakeBoats::kLampHook * vw.scale);
			const RE::NiPoint3 fwdW = vw.rotate * RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
			const RE::NiPoint3 rightW = vw.rotate * RE::NiPoint3{ 1.0f, 0.0f, 0.0f };
			auto& st = lamp_st_;
			if (a_dt > 0.0f) {
				if (st.have) {
					const RE::NiPoint3 move = pivot - st.prevPivot;
					RE::NiPoint3 vel = move / a_dt;
					RE::NiPoint3 acc{};
					if (move.Length() < 200.0f) acc = (vel - st.prevVel) / a_dt;
					else vel = RE::NiPoint3{};
					const float am = acc.Length();
					if (am > 2000.0f) acc = acc * (2000.0f / am);
					st.prevVel = vel;
					const float aF = acc.x * fwdW.x + acc.y * fwdW.y + acc.z * fwdW.z;
					const float aR = acc.x * rightW.x + acc.y * rightW.y + acc.z * rightW.z;
					constexpr float w = 6.28318f / 1.6f;
					constexpr float len = 686.0f / (w * w);
					constexpr float zeta = 0.35f;
					constexpr float maxAngle = 0.2094f;
					constexpr float drive = 1.0f / len;
					float left = (std::min)(a_dt, 0.1f);
					while (left > 0.0f) {
						const float d = (std::min)(0.01f, left);
						st.pitchVel += (-w * w * st.pitch - 2.0f * zeta * w * st.pitchVel - drive * aF) * d;
						st.rollVel += (-w * w * st.roll - 2.0f * zeta * w * st.rollVel - drive * aR) * d;
						st.pitch += st.pitchVel * d;
						st.roll += st.rollVel * d;
						left -= d;
					}
					if (std::abs(st.pitch) > maxAngle) { st.pitch = std::clamp(st.pitch, -maxAngle, maxAngle); st.pitchVel = 0.0f; }
					if (std::abs(st.roll) > maxAngle) { st.roll = std::clamp(st.roll, -maxAngle, maxAngle); st.rollVel = 0.0f; }
				}
				st.prevPivot = pivot;
				st.have = true;
			}
			const float hd = std::atan2(fwdW.x, fwdW.y);
			const RE::NiMatrix3 lrot = RzM(hd) * RxM(st.pitch) * RyM(-st.roll);
			float top = 0.0f, mid = 0.0f;
			if (auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(LakeBoats::kLampStatic)) {
				top = static_cast<float>(base->boundData.boundMax.z);
				mid = 0.5f * (static_cast<float>(base->boundData.boundMax.z) + static_cast<float>(base->boundData.boundMin.z));
			}
			const RE::NiPoint3 meshPos = pivot - lrot * RE::NiPoint3{ 0.0f, 0.0f, top };
			// local to the boat model, so the late re-place of the model (LatePlace) carries it
			const RE::NiMatrix3 vinv = vw.rotate.Transpose();
			const float vs = vw.scale > 0.0f ? vw.scale : 1.0f;
			lamp_->local.rotate = vinv * lrot;
			lamp_->local.translate = (vinv * (meshPos - vw.translate)) / vs;
			lamp_->local.scale = 1.0f / vs;
			Oar::KeepFade(lamp_.get());
			RE::NiUpdateData data{};
			lamp_->Update(data);

			// the light: a model-less light ref at the lantern's centre, at night only (the lake boats' light)
			const RE::NiPoint3 centre = meshPos + lrot * RE::NiPoint3{ 0.0f, 0.0f, mid };
			const bool night = LakeBoats::Night();
			auto light = lampLight_.get();
			if (night && !light) {
				if (auto* hull = Ref()) {
					LakeBoats::PlaceAt(hull, RE::TESForm::LookupByID<RE::TESBoundObject>(LakeBoats::kLampLight), "player light", lampLight_);
					light = lampLight_.get();
				}
			} else if (!night && light) {
				LakeBoats::DeleteRef(lampLight_);
				light.reset();
			}
			if (light) {
				light->data.location = centre;
				if (light->Get3D()) LakeBoats::PlaceAbs(light->Get3D(), centre, lrot, 1.0f);
				if ((lampCellTimer_ -= a_dt) <= 0.0f) {
					lampCellTimer_ = 0.5f;
					auto* tes = RE::TES::GetSingleton();
					auto* cellNow = tes ? tes->GetCell(centre) : nullptr;
					if (cellNow && cellNow->IsAttached() && light->GetParentCell() != cellNow) light->SetPosition(centre);
				}
			}
		}

		static void KeepOpaque(RE::NiAVObject* a_obj, int a_depth = 0)
		{
			if (!a_obj || a_depth > 4) return;
			if (auto* fade = a_obj->AsFadeNode()) fade->GetRuntimeData().currentFade = 1.0f;
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) KeepOpaque(child.get(), a_depth + 1);
			}
		}

		// v0.3.5: after standing up the boat model buzzed until the player moved at all (the physics was still: err 0). So
		// the player is moved a tad, as a key tap would: a small forward drift on the character controller for 0.1 s,
		// starting 2 frames after the stand-up (about 1 unit in all). `rr "nudge off"` to compare.
		void TendNudge(float a_dt)
		{
			if (nudgeTime_ < 0.0f) return;
			nudgeTime_ += a_dt;
			++nudgeFrames_;
			if (nudgeFrames_ < 3) return;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cc = player ? player->GetCharController() : nullptr;
			if (!cc || nudgeTime_ > 0.15f) {
				logger::info("Seat: nudge done ({} frames)", nudgeFrames_);
				nudgeTime_ = -1.0f;
				nudgeFrames_ = 0;
				return;
			}
			const float hs = RE::bhkWorld::GetWorldScale();
			const float hd = player->GetAngleZ();
			const RE::hkVector4 v(std::sin(hd) * kNudgeSpeed * hs, std::cos(hd) * kNudgeSpeed * hs, 0.0f, 0.0f);
			cc->SetLinearVelocityImpl(v);
		}

		// v1.2.4: the oars rest in the oarlocks. They are stowed in the boat only once you are well away from it and it is out of
		// view (so the change is never seen), and swing back onto the oarlocks when you are aboard again.
		void TendStow(float a_dt)
		{
			if ((stowTick_ += a_dt) < 0.5f) return;
			stowTick_ = 0.0f;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			if (seated_ || PlayerAboard()) {
				if (oarsStowed_) logger::info("Oars: aboard again, out of the stow onto the oarlocks");
				oarsStowed_ = false;
				return;
			}
			if (oarsStowed_) return;
			const RE::NiPoint3 boat{ pos_.x, pos_.y, pos_.z };
			const float dist = (player->GetPosition() - boat).Length();
			if (dist < kStowDist) return;
			bool inView = false;
			if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
				const auto& cw = cam->cameraRoot->world;
				RE::NiPoint3 to = boat - cw.translate;
				const float l = to.Length();
				const RE::NiPoint3 fwd = cw.rotate * RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
				inView = l > 1.0f && (to.x * fwd.x + to.y * fwd.y + to.z * fwd.z) / l > 0.5f;  // within 60 deg of the view
			}
			if (inView && dist < kStowDist * 3.0f) return;
			oarsStowed_ = true;
			stowW_ = 1.0f;  // unseen: no swing
			logger::info("Oars: stowed in the boat (you are {:.0f} away, boat {})", dist, inView ? "tiny in view" : "out of view");
		}

		// v1.7.41 (Nexus report): a save made while rowing loads with the player still in the seated / rowing pose but not
		// seated as far as we know: frozen, WASD and E dead (oas sit, then E, freed him). 1.5 s and 3 s after a load, if
		// the player is not seated with us and not in a real chair yet his graph says sitting / in furniture / animation
		// driven (or his sit state is not normal), he is stood up the way StandUp does it.
		float loadUnstick_{ 0.0f };
		int loadUnstickPass_{ 0 };
		void TendLoadUnstick(float a_dt)
		{
			if (loadUnstick_ <= 0.0f) return;
			if (seated_) { loadUnstick_ = 0.0f; return; }
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !p->Get3D(false)) return;
			if ((loadUnstick_ -= a_dt) > 0.0f) return;
			loadUnstick_ = ++loadUnstickPass_ < 2 ? 1.5f : 0.0f;
			if (p->GetOccupiedFurniture()) return;  // a real chair or bed: the game's own
			bool sit = false, furn = false, anim = false;
			p->GetGraphVariableBool("bIsSitting", sit);
			p->GetGraphVariableBool("isInFurniture", furn);
			p->GetGraphVariableBool("bAnimationDriven", anim);
			auto* st = p->AsActorState();
			const bool sss = st && st->actorState1.sitSleepState != RE::SIT_SLEEP_STATE::kNormal;
			if (!sit && !furn && !anim && !sss) return;
			logger::info("Load: the player loaded still seated (bIsSitting {}, isInFurniture {}, bAnimationDriven {}, sit state {}): stood up",
				sit, furn, anim, st ? static_cast<int>(st->actorState1.sitSleepState) : -1);
			SetControls(true);
			MaskSitForFreeLook(p, false);
			for (const char* ev : { "IdleChairExitStart", "IdleFurnitureExit", "sneakStop", "IdleForceDefaultState" }) p->NotifyAnimationGraph(ev);
			for (const char* v : { "bIsSitting", "isIdleSitting", "isInFurniture", "bAnimationDriven", "bHumanoidFootIKDisable" }) p->SetGraphVariableBool(v, false);
			if (st) st->actorState1.sitSleepState = RE::SIT_SLEEP_STATE::kNormal;
			p->SetGraphVariableBool("bAllowRotation", false);
			p->SetCollision(true);
			walkFix_ = 2.0f;
		}

		// v1.2.3: OAS PostExitStand: for a while after standing up (and after a load) the walking flags are put back every
		// 0.25 s: bAllowRotation OFF (a walking player has it off; left on, WASD does nothing), foot IK on, collision on.
		void TendWalkFix(float a_dt)
		{
			if (walkFix_ <= 0.0f || seated_) return;
			walkFix_ -= a_dt;
			if ((walkFixTick_ += a_dt) < 0.25f) return;
			walkFixTick_ = 0.0f;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->Get3D(false)) return;
			bool rot = false, sit = false;
			player->GetGraphVariableBool("bAllowRotation", rot);
			player->GetGraphVariableBool("bIsSitting", sit);
			if (rot || walkFixLogs_ < 2) {
				logger::info("Walk: bAllowRotation was {}, bIsSitting {} -> walking flags put back", rot, sit);
				++walkFixLogs_;
			}
			player->SetGraphVariableBool("bAllowRotation", false);
			player->SetGraphVariableBool("bHumanoidFootIKDisable", false);
			player->SetCollision(true);
			if (walkFix_ <= 0.0f) walkFixLogs_ = 0;
		}

		// ---- helm internals ----
		RE::NiPoint3 PlayerLocal(RE::Actor* a_actor) const
		{
			const RE::NiMatrix3 rot = RzM(yaw_) * (RxM(pitch_) * RyM(roll_));
			return rot.Transpose() * (a_actor->GetPosition() - RE::NiPoint3{ pos_.x, pos_.y, pos_.z + heave_ });
		}

		bool PlayerAboard() const
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return false;
			if (auto* cc = player->GetCharController(); cc && cc->supportBody.get() == HkBody()) return true;
			const RE::NiPoint3 l = PlayerLocal(player);
			return std::abs(l.x) < 120.0f && l.y > -450.0f && l.y < 320.0f && l.z > -30.0f && l.z < 150.0f;
		}

		static void SetControls(bool a_enable)
		{
			if (auto* cm = RE::ControlMap::GetSingleton()) {
				for (auto flag : { RE::ControlMap::UEFlag::kMovement, RE::ControlMap::UEFlag::kFighting, RE::ControlMap::UEFlag::kJumping,
						 RE::ControlMap::UEFlag::kSneaking, RE::ControlMap::UEFlag::kActivate }) {
					cm->ToggleControls(flag, a_enable, a_enable);  // back on at once when standing up
				}
			}
		}

		static bool TrySitAnim(RE::PlayerCharacter* a_player)
		{
			for (const char* ev : { "IdleChairEnterInstant", "IdleChairSitting", "IdleChairEnterStart" }) {
				const bool ok = a_player->NotifyAnimationGraph(ev);
				logger::info("Seat: '{}' {}", ev, ok ? "accepted" : "rejected");
				if (ok) return true;
			}
			return false;
		}

		// v0.6.0: two seats. The rower sits on the middle thwart facing aft (W rows); the helm is the stern seat by the tiller
		// (still the step 1 test motor until crew can row). E takes the one nearer to where you stand.
		enum class SeatKind { kHelm, kRower };
		// v1.2.4: while the row clip plays the rower sits rowShift_ further to the bow (eased in and out with the clip); at rest
		// and at the helm the seat is as set
		RE::NiPoint3 SeatPos() const
		{
			if (seatKind_ != SeatKind::kRower) return seat_;
			const float k = rowShiftW_ * rowShiftW_ * (3.0f - 2.0f * rowShiftW_);
			return RE::NiPoint3{ rowSeat_.x, rowSeat_.y + rowShift_ * k, rowSeat_.z };
		}
		float SeatFacing() const { return yaw_ + (bowFlip_ ? kPi : 0.0f) + (seatKind_ == SeatKind::kRower ? kPi : 0.0f); }
		SeatKind NearestSeat() const
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return SeatKind::kRower;
			const RE::NiPoint3 l = PlayerLocal(player);
			return std::abs(l.y - rowSeat_.y) <= std::abs(l.y - seat_.y) ? SeatKind::kRower : SeatKind::kHelm;
		}
		// v1.7.25: weapons (or spells) out: put away first, and only once they are sheathed does the sit start (the sit sent
		// over the sheathe left the third person camera broken). 2 s at most; cancelled if you step off the boat.
		static bool HandsAway(RE::Actor* a_actor)
		{
			auto* st = a_actor ? a_actor->AsActorState() : nullptr;
			return !st || st->GetWeaponState() == RE::WEAPON_STATE::kSheathed;
		}
		void RequestSit(SeatKind a_kind)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || seated_) return;
			if (HandsAway(player)) { SitDown(a_kind); return; }
			player->DrawWeaponMagicHands(false);
			sitWait_ = 0.0f;
			sitWaitKind_ = a_kind;
			sitWaiting_ = true;
			logger::info("Seat: putting the weapon away before sitting down");
		}
		void TendSitWait(float a_dt)
		{
			if (!sitWaiting_) return;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || seated_ || !PlayerAboard()) { sitWaiting_ = false; return; }
			sitWait_ += a_dt;
			if (!HandsAway(player)) {
				if (sitWait_ < 2.0f) return;
				logger::info("Seat: weapon still not away after 2 s, sitting anyway");
			}
			sitWaiting_ = false;
			SitDown(sitWaitKind_);
		}
		// v1.7.26: the fishing moves the player to its own standing spot, worked out from the marker (no shore here: it
		// dropped you in the water by the boat). For 4 s after E on the supplies a jump of the player off the boat is put
		// back where you stood on the deck, keeping the facing the fishing gave you.
		float fishWatch_{ 0.0f };
		RE::NiPoint3 fishLocal_{};
		RE::NiPoint3 fishPrev_{};
		void TendFishWatch(float a_dt)
		{
			if (fishWatch_ <= 0.0f) return;
			fishWatch_ -= a_dt;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !havePose_) { fishWatch_ = 0.0f; return; }
			const RE::NiPoint3 p = player->GetPosition();
			const float jump = (p - fishPrev_).Length();
			fishPrev_ = p;
			if (jump < 40.0f) return;
			const RE::NiPoint3 back = lastAt_ + lastRot_ * fishLocal_;
			const RE::NiPoint3 l = PlayerLocal(player);
			logger::info("Fish: the fishing moved you {:.0f} to ({:.0f},{:.0f},{:.0f}) (boat space {:.0f},{:.0f},{:.0f}; marker ({:.0f},{:.0f},{:.0f})){}",
				jump, p.x, p.y, p.z, l.x, l.y, l.z, Gear::g_fishSpot.x, Gear::g_fishSpot.y, Gear::g_fishSpot.z, PlayerAboard() ? ", still aboard" : ", off the boat: put back on the deck");
			if (PlayerAboard()) return;
			player->SetPosition(back, true);
			if (auto* cc = player->GetCharController()) cc->SetLinearVelocityImpl(RE::hkVector4());
			fishPrev_ = back;
		}
		// v1.7.31: the row clip stopped again a few times after standing up (see StandUp)
		RE::ActorHandle unstickActor_;
		float unstickT_{ 0.0f };
		int unstickStep_{ 3 };
		void TendUnstick(float a_dt)
		{
			if (unstickStep_ >= 3) return;
			unstickT_ += a_dt;
			static constexpr float kAt[3] = { 0.15f, 0.5f, 1.0f };
			if (unstickT_ < kAt[unstickStep_]) return;
			++unstickStep_;
			auto a = unstickActor_.get();
			if (!a || seated_) { unstickStep_ = 3; return; }  // sat down again: the seat logic owns the pose
			const bool ok = a->NotifyAnimationGraph(kRowStopEvent);
			// a passenger rower stays seated while you walk about: straight back into the chair sit
			if (a.get() != RE::PlayerCharacter::GetSingleton() && Crew::IsAboard(a.get())) a->NotifyAnimationGraph("IdleChairEnterInstant");
			if (unstickStep_ == 1) logger::info("Row anim: stop sent again to {:08X} after standing up ({})", a->GetFormID(), ok ? "accepted" : "rejected");
		}
		bool sitWaiting_{ false };
		float sitWait_{ 0.0f };
		SeatKind sitWaitKind_{ SeatKind::kRower };

		// v1.6.0: the boat as the crew sees it
		Crew::Pose CrewPose(const RE::NiPoint3& a_at, const RE::NiMatrix3& a_rot) const
		{
			Crew::Pose p;
			p.at = a_at;
			p.rot = a_rot;
			p.yaw = yaw_ + (bowFlip_ ? kPi : 0.0f);
			p.playerRows = seatKind_ == SeatKind::kRower;
			p.deckZ = kDeckZ;
			p.speed = speed_;
			return p;
		}

		void SitDown(SeatKind a_kind)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			if (player->IsSneaking()) player->NotifyAnimationGraph("sneakStop");
			seated_ = true;
			helm_ = true;
			seatKind_ = a_kind;
			{
				auto* cam = RE::PlayerCamera::GetSingleton();
				camWasFirst_ = cam && cam->IsInFirstPerson();
				facePending_ = a_kind == SeatKind::kRower && !camWasFirst_;  // OAS 56.47: boarding the oars in third person faces the rower
				orbitHave_ = false;
			}
			sitTries_ = 1;
			sitTimer_ = 0.0f;
			sitAt_ = rideTime_;
			rowShiftW_ = 0.0f;
			oarsStowed_ = false;  // v1.2.4: swing out of the stow (if they were) onto the oarlocks
			rowAnimOn_ = rowPending_ = resitPending_ = rowWant_ = false;
			inFwd_ = inBack_ = inLeft_ = inRight_ = false;
			SetControls(false);
			HoldTalk();          // v1.7.22
			Crew::Stow(player);  // v1.7.22: weapon sheathed (still equipped), shield off, rowing or at the helm
			player->SetCollision(false);  // v1.2.3: OAS rides with no player collision ("tcl": no collision, no gravity); the glue holds him
			walkFix_ = 0.0f;
			player->data.angle.z = SeatFacing();
			const bool ok = TrySitAnim(player);
			const auto& s = SeatPos();
			if (havePose_) Crew::Board(CrewPose(lastAt_, lastRot_));  // v1.6.0: followers and crew board with you
			if (a_kind == SeatKind::kRower)
				Print(std::format("RowRim: at the oars ({:.0f} {:.0f} {:.0f}, sit anim {}). W rows, S backs water, A/D turn, E stands up", s.x, s.y, s.z, ok ? "ok" : "refused"));
			else
			{
				auto* r = Crew::Rower();  // v1.7.9: a follower aboard rows for you
				Print(r ? std::format("RowRim: at the stern seat ({:.0f} {:.0f} {:.0f}, sit anim {}). {} rows: W/S/A/D steer them. E stands up", s.x, s.y, s.z, ok ? "ok" : "refused", r->GetName())
				        : std::string("No rower, move to rower seat or have crew"));  // v1.7.54
			}
		}

		void StandUp(const char* a_why)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			Crew::Release(havePose_ ? CrewPose(lastAt_, lastRot_) : Crew::g.pose);  // v1.6.0: they stand up with you
			Pad::Reset();  // v1.7.8
			seated_ = false;
			inFwd_ = inBack_ = inLeft_ = inRight_ = false;
			// v1.7.31: the row clip is stopped on whoever had it (the crew rower kept it for good: only the player got the
			// release events), and stopped again 0.15 / 0.5 / 1 s later: E pressed right as a stroke started (or mid
			// stroke) could land before the clip and leave it playing
			{
				RE::Actor* had = rowAnimOn_ ? rowAnimActor_.get().get() : nullptr;
				if (!had && (rowPending_ || rideTime_ - lastRowStopAt_ < 1.0f || rideTime_ - rowAnimOnAt_ < 1.0f)) had = RowingActor();
				if (rowAnimOn_) StopRowAnim(rowAnimActor_.get().get(), "stood up");
				if (had) {
					unstickActor_ = had->GetHandle();
					unstickT_ = 0.0f;
					unstickStep_ = 0;
				}
				animRotUntil_ = rideTime_ + 3.0f;
			}
			rowAnimOn_ = rowPending_ = resitPending_ = rowWant_ = false;
			Crew::g.rowerRowing = false;
			if (player) {
				MaskSitForFreeLook(player, false);
				// v1.2.3: OFF, as OAS found: "the old exit forced bAllowRotation to TRUE, but a walking player has it FALSE ...
				// stuck for good after the first ride". Forcing it TRUE here (v1.2.0) was the stuck-after-unboarding bug.
				player->SetGraphVariableBool("bAllowRotation", false);
				player->SetCollision(true);
			}
			orbitHave_ = false;
			facePending_ = false;
			SetControls(true);
			HoldTalk();  // v1.7.22
			if (player) Crew::Unstow(player);  // v1.7.22: the shield back on
			if (player) {
				// same release as OAS (TendLoadRelease): the sit state cleared everywhere, or the player stays stuck
				std::string sent;
				for (const char* ev : { "IdleChairExitStart", "IdleFurnitureExit", "sneakStop", "IdleForceDefaultState" }) {
					if (player->NotifyAnimationGraph(ev)) sent += std::format("{}{}", sent.empty() ? "" : ", ", ev);
				}
				for (const char* v : { "bIsSitting", "isIdleSitting", "isInFurniture", "bAnimationDriven", "bHumanoidFootIKDisable" }) player->SetGraphVariableBool(v, false);
				if (auto* st = player->AsActorState()) st->actorState1.sitSleepState = RE::SIT_SLEEP_STATE::kNormal;
				if (Ref()) {
					// feet on the deck at the seat, moving with the boat so it does not slide out from under you
					const RE::NiMatrix3 rot = RzM(yaw_) * (RxM(pitch_) * RyM(roll_));
					const RE::NiPoint3 sp = seatKind_ == SeatKind::kRower ? rowSeat_ : seat_;  // v1.2.4: the seat as set, not the rowing shift
					// v1.2.1: the rower sits on the middle thwart: standing up there put the player inside the plank (stuck, or
					// thrown up onto it). Step off it, 36 aft, onto the floor.
					// v1.2.3: the helm seat is where the hull's collision floor climbs to the stern (48 under the seat, 60 at its
					// back edge, measured from the hull's collision mesh): the feet at 41 were inside it. Step 65 forward, where
					// the floor is 42, feet at 45.
					const bool rower = seatKind_ == SeatKind::kRower;
					const float offY = rower ? -36.0f - sp.y : 65.0f;  // v1.4.0: the rower steps off to 36 aft of the thwart's middle (y 0), wherever the seat is
					const float feetZ = rower ? kDeckZ + 2.0f : kDeckZ + 6.0f;
					const RE::NiPoint3 w = RE::NiPoint3{ pos_.x, pos_.y, pos_.z + heave_ } + rot * RE::NiPoint3{ sp.x, sp.y + offY, feetZ };
					player->SetPosition(w, true);
					if (auto* cc = player->GetCharController()) {
						const float hs = RE::bhkWorld::GetWorldScale();
						const float hd = yaw_ + (bowFlip_ ? kPi : 0.0f);
						const RE::hkVector4 v(std::sin(hd) * speed_ * hs, std::cos(hd) * speed_ * hs, 0.0f, 0.0f);
						cc->SetLinearVelocityImpl(v);
					}
				}
				logger::info("Seat: release events sent: {}", sent);
				nudgeTime_ = nudgeOn_ ? 0.0f : -1.0f;  // v0.3.5: the tiny step after standing (see TendNudge)
				walkFix_ = 2.0f;  // v1.2.3: the walking flags held for 2 s (OAS PostExitStand), the get-up can set them again
			}
			Print(std::format("RowRim: stood up ({}); the boat slows to a stop", a_why));
		}

		void HelmSim(float a_dt)
		{
			constexpr float kMaxFwd = 220.0f, kMaxBack = 80.0f, kAccel = 90.0f, kBrake = 160.0f, kDrag = 0.6f;
			constexpr float kTurnMax = 35.0f * kPi / 180.0f;
			bool f = false, b = false, l = false, r = false;
			if (seated_) {
				f = inFwd_ || stickY_ > 0.4f;
				b = inBack_ || stickY_ < -0.4f;
				l = inLeft_ || stickX_ < -0.4f;
				r = inRight_ || stickX_ > 0.4f;
			}
			// v1.4.0: the stern seat has no oars: sitting there alone does not move the boat (the old test motor is gone)
			noRowerNote_ -= a_dt;
			if (seated_ && seatKind_ == SeatKind::kHelm) {
				if ((f || b || l || r) && noRowerNote_ <= 0.0f) {
					noRowerNote_ = 6.0f;
					RE::SendHUDMessage::ShowHUDMessage("No rower, move to rower seat or have crew");  // v1.7.54
				}
				f = b = l = r = false;
			}
			if (f && !b) speed_ = (std::min)(kMaxFwd, speed_ + (speed_ < 0.0f ? kBrake : kAccel) * a_dt);
			else if (b && !f) speed_ = (std::max)(-kMaxBack, speed_ - (speed_ > 0.0f ? kBrake : kAccel) * a_dt);
			else if (seated_) {
				speed_ *= std::exp(-kDrag * a_dt);
				if (std::abs(speed_) < 2.0f) speed_ = 0.0f;
			} else {
				constexpr float kIdleBrake = 200.0f;  // nobody at the helm: stops in about a second
				speed_ = speed_ > 0.0f ? (std::max)(0.0f, speed_ - kIdleBrake * a_dt) : (std::min)(0.0f, speed_ + kIdleBrake * a_dt);
			}
			const float steer = (r ? 1.0f : 0.0f) - (l ? 1.0f : 0.0f);
			const float grip = 0.35f + 0.65f * (std::min)(1.0f, std::abs(speed_) / 120.0f);  // turns best when moving, a little at rest
			const float target = steer * kTurnMax * grip * (speed_ < -2.0f ? -1.0f : 1.0f);
			turn_ += (target - turn_) * (std::min)(1.0f, a_dt * 3.0f);
			if (!seated_ && speed_ == 0.0f && std::abs(turn_) < 0.001f) { turn_ = 0.0f; helm_ = false; }
		}

		// ---- v0.6.0: rowing (OAS SkimStep row profile, Plugin.cpp 56.49/56.50 tune) ----
		// Speed is gained only while the blades pull (the stroke follows the row clip: 0.3366 strokes/s, its 3 s loop); between
		// strokes the boat glides. The boat moves along its heading only (keel: no sideways slide). A/D turn the boat (oar
		// difference); rowers can spin it nearly as well at rest (turn floor 0.9).
		void RowSim(float a_dt)
		{
			constexpr float kAccelRate = 0.6f, kBrake = 120.0f, kRevMax = 60.0f, kRevAccel = 20.0f;
			constexpr float kTurnLow = 0.5f, kTurnHigh = 0.45f, kTurnAuth = 120.0f, kTurnFloor = 0.9f, kTurnResp = 2.0f, kTurnDrag = 0.25f;
			constexpr float kStrokes = 0.3366f, kTwoPi = 6.28318f;
			const bool f = inFwd_ || stickY_ > 0.3f, b = inBack_ || stickY_ < -0.3f;
			const float throttle = (f ? 1.0f : 0.0f) - (b ? 1.0f : 0.0f);
			const float steer = ((inRight_ || stickX_ > 0.4f) ? 1.0f : 0.0f) - ((inLeft_ || stickX_ < -0.4f) ? 1.0f : 0.0f);
			rowWant_ = throttle > 0.01f;
			const float s01 = std::clamp((std::max)(0.0f, speed_) / rowMax_, 0.0f, 1.0f);
			// the oars pull once the clip is live (after its blend-in), or after 2.5 s with no clip (animation missing), and keep
			// going through the stop debounce so they stop with the clip
			const bool clipLive = rowAnimOn_ && rideTime_ - rowAnimOnAt_ >= kClipLeadIn;
			const bool safety = rowPending_ && rowPendingFor_ > 2.5f;
			const bool drive = (throttle > 0.01f && (clipLive || safety)) || (throttle <= 0.01f && rowAnimOn_ && rowOffTimer_ < kRowStopDebounce);
			oarAmp_ += ((drive ? 1.0f : 0.0f) - oarAmp_) * (1.0f - std::exp(-4.0f * a_dt));
			if (oarAmp_ > 0.0005f) {
				oarPhase_ += a_dt * kTwoPi * kStrokes;
				if (oarPhase_ > kTwoPi) oarPhase_ -= kTwoPi;
			} else {
				oarPhase_ = 0.0f;  // at rest: the next stroke starts at the catch
			}
			const float thrust = drive ? kPi * (std::max)(0.0f, std::sin(oarPhase_)) * (std::min)(1.0f, oarAmp_ * 2.0f) : 0.0f;
			lastThrust_ = thrust;
			if (throttle > 0.01f) {
				if (speed_ < 0.0f) speed_ = (std::min)(0.0f, speed_ + kBrake * 0.6f * a_dt);  // out of reverse first
				else {
					if (speed_ < rowMax_) speed_ = (std::min)(rowMax_, speed_ + rowMax_ * kAccelRate * thrust * a_dt);
					speed_ *= std::exp(-rowDrag_ * a_dt);
				}
			} else if (throttle < -0.01f) {
				if (speed_ > 8.0f) speed_ = (std::max)(0.0f, speed_ - kBrake * (0.4f + 0.6f * s01) * a_dt);
				else speed_ = (std::max)(-kRevMax, (std::min)(speed_, 0.0f) - kRevAccel * a_dt);  // stopped: back water slowly
			} else if (speed_ > 0.0f) {
				speed_ *= std::exp(-rowDrag_ * a_dt);
				if (speed_ < 1.0f) speed_ = 0.0f;
			} else {
				speed_ *= std::exp(-0.9f * a_dt);
				if (speed_ > -1.0f) speed_ = 0.0f;
			}
			const float auth = kTurnFloor + (1.0f - kTurnFloor) * std::clamp(std::abs(speed_) / kTurnAuth, 0.0f, 1.0f);
			const float maxTurn = (kTurnLow + (kTurnHigh - kTurnLow) * s01) * auth * turnScale_;  // v1.7.8: [Row] Turn
			turn_ += (steer * maxTurn - turn_) * (1.0f - std::exp(-kTurnResp * a_dt));
			if (speed_ > 0.0f) {  // hard turns at speed scrub speed
				const float load = std::clamp(std::abs(turn_) / kTurnHigh, 0.0f, 1.0f);
				speed_ *= std::exp(-kTurnDrag * load * load * s01 * a_dt);
			}
			if ((rowLogTimer_ -= a_dt) <= 0.0f && (drive || std::abs(speed_) > 1.0f)) {
				rowLogTimer_ = 2.0f;
				logger::info("Row: speed {:.0f} (top {:.0f}), turn {:.1f} deg/s, stroke {:.2f} amp {:.2f}, clip {}", speed_, rowMax_, turn_ * 180.0f / kPi, oarPhase_ / kTwoPi,
					oarAmp_, rowAnimOn_ ? "on" : (rowPending_ ? "pending" : "off"));
			}
		}

		// OAS UpdateRowAnim / TryRowStart / StopRowAnim: W starts the row clip on the seated player (OarsRowStart, retried every
		// 0.4 s), letting go of W stops it after 0.6 s (IdleForceDefaultState) and the player sits again.
		// v1.7.9: who rows: the player on the middle thwart, or (the player at the stern) the crew rower
		RE::Actor* RowingActor() const
		{
			if (!seated_) return nullptr;
			if (seatKind_ == SeatKind::kRower) return RE::PlayerCharacter::GetSingleton();
			return Crew::Rower();
		}

		void TendRowAnim(float a_dt)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			RE::Actor* rower = RowingActor();
			// the clip is on someone who is no longer the rower (stood up, swapped, gone): stop it there
			if (rowAnimOn_) {
				auto cur = rowAnimActor_.get();
				if (cur.get() != rower) StopRowAnim(cur.get(), "the rower changed");
			}
			if (!rower) {
				rowPending_ = false;
				Crew::g.rowerRowing = false;
				return;
			}
			const bool npc = rower != player;
			if (auto* ui = RE::UI::GetSingleton(); ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME))) return;
			if (resitPending_ && rideTime_ - lastRowStopAt_ > 0.05f) {
				resitPending_ = false;
				if (npc) Crew::ResitRower();
				else {
					TrySitAnim(player);
					sitTries_ = 1;
					sitTimer_ = 0.0f;
					sitAt_ = rideTime_;
				}
			}
			if (rowWant_) {
				rowOffTimer_ = 0.0f;
				if (rowAnimOn_) return;
				if (!rowPending_) {
					rowPending_ = true;
					rowPendingFor_ = 0.0f;
					rowRetryWait_ = 0.0f;
					oarPhase_ = 0.0f;
				}
				rowPendingFor_ += a_dt;
				if ((rowRetryWait_ -= a_dt) > 0.0f) return;
				if (rideTime_ - lastRowStopAt_ < 0.3f || rideTime_ - sitAt_ < 0.3f) return;  // 0.3 s after a stop and after sitting
				if (!rowEverAccepted_ && rowRejects_ >= 10) return;  // given up this session: rowing without the clip
				if (npc) Crew::g.rowerRowing = true;  // before the event: the glue must not send the chair sit over the clip
				if (rower->NotifyAnimationGraph(kRowStartEvent)) {
					rowAnimOn_ = true;
					rowAnimActor_ = rower->GetHandle();
					rowAnimOnAt_ = rideTime_;
					rowPending_ = false;
					rowEverAccepted_ = true;
					oarPhase_ = 0.0f;  // the stroke starts at the catch with the clip
					logger::info("Row anim: {} accepted by {}{}", kRowStartEvent, npc ? std::format("the crew rower {:08X}", rower->GetFormID()) : std::string("the player"),
						rowRejects_ ? std::format(" after {} tries", rowRejects_) : std::string{});
					rowRejects_ = 0;
				} else {
					if (npc) Crew::g.rowerRowing = false;
					rowRetryWait_ = 0.4f;
					if (++rowRejects_ == 1) logger::info("Row anim: {} rejected, retrying every 0.4 s", kRowStartEvent);
					if (rowRejects_ == 10 && !rowEverAccepted_)
						Print("RowRim: the row animation was refused 10 times. Run Nemesis/Pandora with RowRim enabled. Rowing without it for now.");
				}
			} else {
				rowPending_ = false;
				if (rowAnimOn_ && (rowOffTimer_ += a_dt) >= kRowStopDebounce) StopRowAnim(rower, "W released");
			}
		}

		void StopRowAnim(RE::Actor* a_rower, const char* a_why)
		{
			const bool ok = a_rower && a_rower->NotifyAnimationGraph(kRowStopEvent);
			rowAnimOn_ = false;
			rowAnimActor_ = {};
			rowOffTimer_ = 0.0f;
			lastRowStopAt_ = rideTime_;
			animRotUntil_ = rideTime_ + 3.0f;
			Crew::g.rowerRowing = false;
			if (seated_ && a_rower && a_rower == RowingActor()) resitPending_ = true;  // sit again next frame (the clip replaced the sit pose)
			logger::info("Row anim: stopped ({}), {} {}", a_why, kRowStopEvent, ok ? "accepted" : "rejected");
		}

		// The oar set on the boat model itself (it moves with the boat for free).
		void AttachOars(RE::NiNode* a_boat)
		{
			for (int k = 0; k < 2; ++k) {
				oars_[k].reset(Oar::MakeWrapped(Oar::kOarName[k]));
				if (oars_[k]) {
					Paint::OarWood(oars_[k].get());  // the OAS kwila oars
					a_boat->AttachChild(oars_[k].get(), true);
				}
				haveClip_[k] = false;
			}
			oarW_ = 0.0f;
			oarsToClip_ = false;
		}

		// ---- v1.0.0: the world around the boat (OAS SkimWaterAt / SkimSweep, the 38A/39 bow and side probes) ----
		// Water: the hull keeps the waterline it was placed with (kWaterZ + rr boat height) as the water height under it changes
		// (rivers, lakes at other heights), eased. Shore: a bow (or stern, backing) sample with land at or above the water stops
		// the boat gently (it runs up to the shoreline, crawl 40, then stops) and it may back off again. Rocks and banks: three
		// short rays at the waterline ahead of the bow (centre and +-50) against statics, terrain and props stop it dead, with a
		// small knock. Our own hull, its carrier, actors and the water itself never count.
		static bool BoatWaterAt(const RE::NiPoint3& a_pos, float& a_h, int& a_why)
		{
			a_why = 0;
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(a_pos) : nullptr;
			if (!cell || !cell->IsAttached() || !cell->GetbhkWorld()) { a_why = 1; return false; }
			float water = 0.0f, land = 0.0f;
			if (!cell->GetWaterHeight(a_pos, water) || !std::isfinite(water) || std::abs(water) > 1000000.0f) { a_why = 2; return false; }
			if (cell->IsExteriorCell() && tes->GetLandHeight(a_pos, land) && std::isfinite(land) && land >= water + 4.0f) { a_why = 3; return false; }
			a_h = water;
			return true;
		}
		bool BoatSweep(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, int& a_layer)
		{
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(a_from) : nullptr;
			auto* world = cell && cell->IsAttached() ? cell->GetbhkWorld() : nullptr;
			if (!world) return false;
			RE::bhkPickData pick{};
			const float scale = RE::bhkWorld::GetWorldScale();
			pick.rayInput.from = RE::hkVector4(a_from * scale);
			pick.rayInput.to = RE::hkVector4(a_to * scale);
			pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kCameraPick);
			pick.rayOutput.Reset();
			world->PickObject(pick);
			if (!pick.rayOutput.HasHit() || !pick.rayOutput.rootCollidable) return false;
			const auto layer = pick.rayOutput.rootCollidable->GetCollisionLayer();
			const int id = static_cast<int>(layer);
			if (layer != RE::COL_LAYER::kStatic && layer != RE::COL_LAYER::kAnimStatic && layer != RE::COL_LAYER::kTerrain && layer != RE::COL_LAYER::kGround &&
				layer != RE::COL_LAYER::kProps && layer != RE::COL_LAYER::kTrees)
				return false;
			if (auto* hit = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable)) {
				if (hit->GetFormID() == refId_ || hit->GetFormID() == carrierId_ || hit->As<RE::Actor>() || IsPickRef(hit->GetFormID())) return false;
			}
			a_layer = id;
			return true;
		}
		// ---- v1.7.46: waterfalls and ledges block the boat (the v1.7.10 fall is gone: no flying off a lip). A lip is a lower
		// water plane just ahead of the bow, or astern when backing (40 down or more), or the water running out at a ledge
		// about level with it with the ground 80 or more below further on. Rowing toward one stops the boat at the lip; she
		// can still turn and row away. ----
		static constexpr float kFallDrop = 40.0f;         // OAS kSkimDropThreshold: a lower water plane
		static constexpr float kFallCliff = 80.0f;        // OAS kSkimCliffDrop: ground this far under the water = a ledge
		float lipMsgCd_{ 0.0f };

		// a lower water plane right ahead of the bow (OAS SkimDropAhead)
		bool DropAhead(const RE::NiPoint3& a_fwd, float a_tip, float& a_lower) const
		{
			const float runway = (std::max)(96.0f, std::abs(speed_) * 0.25f);
			for (float d : { 48.0f, 96.0f, 160.0f, runway }) {
				const RE::NiPoint3 p{ pos_.x + a_fwd.x * (a_tip + d), pos_.y + a_fwd.y * (a_tip + d), pos_.z };
				float w = 0.0f;
				int why = 0;
				if (BoatWaterAt(p, w, why) && waterZ_ - w >= kFallDrop) { a_lower = w; return true; }
			}
			return false;
		}
		// the water ends at a ledge about level with it and the ground falls away further on (OAS SkimCliffAhead)
		bool CliffAhead(const RE::NiPoint3& a_fwd, float a_tip, float& a_low) const
		{
			auto* tes = RE::TES::GetSingleton();
			if (!tes) return false;
			float nearLand = 0.0f;
			const RE::NiPoint3 nearP{ pos_.x + a_fwd.x * (a_tip + 16.0f), pos_.y + a_fwd.y * (a_tip + 16.0f), pos_.z };
			if (!tes->GetLandHeight(nearP, nearLand) || !std::isfinite(nearLand)) return false;
			if (nearLand > waterZ_ + 24.0f) return false;  // a bank or a wall, not a ledge
			if (nearLand < waterZ_ - 40.0f) return false;  // v1.7.45: the bed far under open water, not a lip level with it
			const float runway = (std::max)(240.0f, std::abs(speed_) * 0.35f);
			for (float d : { 48.0f, 96.0f, 160.0f, 240.0f, runway }) {
				float land = 0.0f;
				const RE::NiPoint3 p{ pos_.x + a_fwd.x * (a_tip + d), pos_.y + a_fwd.y * (a_tip + d), pos_.z };
				if (tes->GetLandHeight(p, land) && std::isfinite(land) && waterZ_ - land >= kFallCliff) { a_low = land; return true; }
			}
			return false;
		}
		// at a lip: stopped where she is, a small knock back, told once in a while
		void BlockAtLip(const char* a_what, const RE::NiPoint3& a_dirv, float a_below)
		{
			const bool wasMoving = std::abs(speed_) > 20.0f;
			speed_ = 0.0f;
			if (wasMoving) {  // only on the first touch (held there while you keep rowing, never shoved back each frame)
				pos_.x -= a_dirv.x * 2.0f;
				pos_.y -= a_dirv.y * 2.0f;
				dipVel_ = -20.0f;
			}
			if (lipMsgCd_ <= 0.0f) {
				lipMsgCd_ = 4.0f;
				RE::SendHUDMessage::ShowHUDMessage("A drop ahead: the boat won't go over");
				logger::info("Lip: {} at ({:.0f},{:.0f}), water {:.0f}, below {:.0f}: blocked", a_what, pos_.x, pos_.y, waterZ_, a_below);
			}
		}

		void WorldStep(float a_dt)
		{
			const float hd = yaw_ + (bowFlip_ ? kPi : 0.0f);
			const RE::NiPoint3 fwd{ std::sin(hd), std::cos(hd), 0.0f };
			const RE::NiPoint3 right{ fwd.y, -fwd.x, 0.0f };
			lipMsgCd_ = (std::max)(0.0f, lipMsgCd_ - a_dt);
			// the water under the boat, 4 times a second; the hull eases to its waterline there
			if ((waterTimer_ -= a_dt) <= 0.0f) {
				waterTimer_ = 0.25f;
				float w = 0.0f;
				int why = 0;
				haveWater_ = BoatWaterAt(RE::NiPoint3{ pos_.x, pos_.y, pos_.z }, w, why) || (why == 3 && WaterAt(pos_, w));
				if (haveWater_) waterZ_ = w;
			}
			if (haveWater_) {
				const float want = waterZ_ + kWaterZ + heightOff_;
				pos_.z += (want - pos_.z) * (1.0f - std::exp(-2.0f * a_dt));
			}
			beached_ = false;
			FeelApplyMotion(a_dt);
			UpdateGround(a_dt, fwd, right);
			UpdateHullFeel(a_dt, fwd, right);
			if (std::abs(speed_) < 0.5f) { onBeach_ = false; return; }
			const float dir = speed_ >= 0.0f ? 1.0f : -1.0f;
			const float tip = dir > 0.0f ? kFeelLen - 10.0f : kFeelLen - 20.0f;
			const float step = std::abs(speed_) * a_dt;
			const RE::NiPoint3 tipAt{ pos_.x + fwd.x * dir * (tip + step), pos_.y + fwd.y * dir * (tip + step), pos_.z };
			float w = 0.0f;
			int why = 0;
			const bool wet = BoatWaterAt(tipAt, w, why);
			// v1.7.46: heading for a lip (forward or backing): a lower water plane at the bow / stern, or a ledge with the
			// ground falling away: blocked there, never over it
			if (haveWater_ && !onBeach_) {
				const RE::NiPoint3 dv{ fwd.x * dir, fwd.y * dir, 0.0f };
				float below = 0.0f;
				if (wet && waterZ_ - w >= kFallDrop) { BlockAtLip("waterfall (lower water at the tip)", dv, w); return; }
				if (!wet && DropAhead(dv, tip, below)) { BlockAtLip("waterfall (lower water past the lip)", dv, below); return; }
				if (!wet && (why == 2 || why == 3) && CliffAhead(dv, tip, below)) { BlockAtLip("ledge (the ground falls away)", dv, below); return; }
			}
			if (!wet && why == 3) {
				// v1.4.0: OAS beach glide: the tip runs up the sand bleeding speed (land crawl 120, slide decel 300/s), heavy drag
				// from half of kBeachMaxSlide on, and no further than that past where it first touched. The ground lift and
				// tilt carry the hull up; a bank (land 40 over the water) is the governor's.
				if (!onBeach_) {
					onBeach_ = true;
					beachStart_ = pos_;
					logger::info("World: beach {} (speed {:.0f})", dir > 0.0f ? "ahead" : "astern", speed_);
				}
				const float bx = pos_.x - beachStart_.x, by = pos_.y - beachStart_.y;
				const float slide = std::sqrt(bx * bx + by * by);
				if (std::abs(speed_) > kLandCrawl) speed_ = dir * (std::max)(kLandCrawl, std::abs(speed_) - kSlideDecel * a_dt);
				const float frac = slide / kBeachMaxSlide;
				if (frac >= 1.0f) speed_ = 0.0f;
				else if (frac > kBeachDragStart) speed_ *= std::exp(-kBeachDrag * (frac - kBeachDragStart) / (1.0f - kBeachDragStart) * a_dt);
				if (std::abs(speed_) < 2.0f) speed_ = 0.0f;
				return;
			}
			onBeach_ = false;
			if (!wet && why == 1) speed_ = dir * (std::min)(std::abs(speed_), 60.0f);  // the cell ahead is not loaded yet: slow, it may load
		}

		// ---- v1.4.0: OAS soft physics (Parts 38-48 and ground v2, 41A/E). Never a real collision: the hull is lifted, tilted,
		// slowed and pushed. Ground: seven samples round the hull (centre, tip, stern, sides, bow quarters); dry ones (land 12
		// over the water) give a least squares plane the hull tilts to (soft clamps 25 deg pitch, 10 deg roll) and a smooth lift
		// so it clears every sample (30 afloat, 60 with the bow dry, 95 on ice), all critically damped springs. A bank (land 40
		// over the water, hysteretic) or a slope steeper than 25 deg at the bow engages the governor: the boat eases to a stop
		// at it. Ice sheets (refs named floe / iceberg / ice sheet ...) are ridden on their tops and are never obstacles. Twelve
		// feelers (bow 3, sides 4, stern corners 2, keel 3): bow STOP over 0.2 s above 150 closing, slower a scrape; one bow
		// quarter turns the bow away; sides and stern a fading push away (40, max 60) and 4% speed; the keel a dip, a jolt and
		// 5% speed. A tall steep terrain face at the waterline is a rock (stop), not a beach.
		static constexpr int kGN = 7;
		static constexpr float kFeelLen = 300.0f, kFeelWidth = 85.0f, kFeelKeel = 20.0f;
		static constexpr float kMaxGroundPitch = 0.4363f, kMaxGroundRoll = 0.1745f, kBeachMaxRise = 40.0f, kMaxBankSlope = 0.4663f;
		static constexpr float kMaxGroundLift = 30.0f, kDryHysteresis = 0.2f, kGroundDryRise = 12.0f, kGroundLiftAllowance = 4.0f;
		static constexpr float kLandCrawl = 120.0f, kSlideDecel = 300.0f, kBeachMaxSlide = 330.0f, kBeachDragStart = 0.5f, kBeachDrag = 40.0f;
		static constexpr float kFeelReach = 25.0f, kFeelStopSpeed = 150.0f, kFeelStopTime = 0.2f, kFeelPushSpeed = 40.0f, kFeelPushMax = 60.0f;
		static constexpr float kFeelTouch = 6.0f, kFeelMinSpeed = 25.0f, kFeelMinClosing = 30.0f, kFeelYawKick = 0.2094f;
		static constexpr float kSoftStopMin = 0.25f, kSoftStopMax = 0.8f;  // v1.7.21
		float feelStopTime_{ 0.2f };
		float groundLift_{ 0.0f }, liftVel_{ 0.0f }, groundPitch_{ 0.0f }, pitchVel_{ 0.0f }, groundRoll_{ 0.0f }, rollVel_{ 0.0f };
		bool touchState_[kGN]{}, dryState_[kGN]{}, bankState_[kGN]{};
		float touchTimer_[kGN]{}, dryTimer_[kGN]{}, bankClearTimer_[kGN]{};
		float governor_{ 1.0f }, govRefSpeed_{ 0.0f };
		bool govEngaged_{ false };
		bool onIce_{ false };
		int iceSamplesPrev_{ 0 };
		std::unordered_map<RE::FormID, bool> iceCache_;
		bool onBeach_{ false };
		RE::NiPoint3 beachStart_{};
		float groundLog_{ 0.0f };
		enum FeelGroup : int { kFeelBow = 0, kFeelSide = 1, kFeelStern = 2, kFeelKeelG = 3 };
		struct Feeler { const char* name; FeelGroup group; float right, fwd, dirRight, dirFwd; int side; };
		static constexpr int kFeelers = 12;
		bool feelTouch_[kFeelers]{};
		float feelReactCd_[kFeelers]{}, feelPrevDist_[kFeelers]{}, feelPrevAt_[kFeelers]{};
		float feelPushX_{ 0.0f }, feelPushY_{ 0.0f }, feelYawKick_{ 0.0f }, feelStopTimer_{ 0.0f }, feelStopFrom_{ 0.0f };
		float dip_{ 0.0f }, dipVel_{ 0.0f };
		float rockAheadUntil_{ -100.0f }, rockOffUntil_{ -100.0f }, rockWindowAt_{ -100.0f }, feelLogCd_{ 0.0f };
		RE::NiPoint3 rockSpot_{};
		int rockFires_{ 0 };
		std::uint32_t feelFrame_{ 0 };

		static const Feeler* Feelers()
		{
			static constexpr float kS = 0.70710678f;
			static constexpr Feeler kFeel[kFeelers] = {
				{ "bow tip", kFeelBow, 0.0f, 1.0f, 0.0f, 1.0f, 0 },
				{ "bow L", kFeelBow, -0.45f, 0.8f, 0.0f, 1.0f, -1 },
				{ "bow R", kFeelBow, 0.45f, 0.8f, 0.0f, 1.0f, 1 },
				{ "side L fore", kFeelSide, -1.0f, 0.35f, -1.0f, 0.0f, -1 },
				{ "side L aft", kFeelSide, -1.0f, -0.35f, -1.0f, 0.0f, -1 },
				{ "side R fore", kFeelSide, 1.0f, 0.35f, 1.0f, 0.0f, 1 },
				{ "side R aft", kFeelSide, 1.0f, -0.35f, 1.0f, 0.0f, 1 },
				{ "stern L", kFeelStern, -0.7f, -0.9f, -kS, -kS, -1 },
				{ "stern R", kFeelStern, 0.7f, -0.9f, kS, -kS, 1 },
				{ "keel aft", kFeelKeelG, 0.0f, -0.5f, 0.0f, 0.0f, 0 },
				{ "keel centre", kFeelKeelG, 0.0f, 0.0f, 0.0f, 0.0f, 0 },
				{ "keel fore", kFeelKeelG, 0.0f, 0.5f, 0.0f, 0.0f, 0 },
			};
			return kFeel;
		}

		// OAS 41E: ice sheets by their model / node name
		static std::string RefHay(RE::TESObjectREFR* a_ref)
		{
			std::string hay;
			auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!base) return hay;
			if (auto* m = base->As<RE::TESModel>(); m && m->GetModel()) hay += m->GetModel();
			if (auto* root = a_ref->Get3D(); root && root->name.c_str()) { hay += ' '; hay += root->name.c_str(); }
			std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return hay;
		}
		bool IsIceRef(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) return false;
			const RE::FormID id = a_ref->GetFormID();
			if (auto it = iceCache_.find(id); it != iceCache_.end()) return it->second;
			const std::string hay = RefHay(a_ref);
			bool ice = false;
			for (const char* h : { "icefloe", "ice_floe", "floe", "iceberg", "icesheet", "ice_sheet", "frozen" }) if (hay.find(h) != std::string::npos) { ice = true; break; }
			if (!ice && hay.find("ice") != std::string::npos) {
				for (const char* s : { "floe", "sheet", "berg", "plate" }) if (hay.find(s) != std::string::npos) { ice = true; break; }
			}
			if (iceCache_.size() > 4096) iceCache_.clear();
			iceCache_[id] = ice;
			return ice;
		}
		bool IceTopAt(const RE::NiPoint3& a_pos, float a_water, float& a_top)
		{
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(a_pos) : nullptr;
			auto* world = cell && cell->IsAttached() ? cell->GetbhkWorld() : nullptr;
			if (!world) return false;
			const RE::NiPoint3 from{ a_pos.x, a_pos.y, a_water + 120.0f }, to{ a_pos.x, a_pos.y, a_water - 60.0f };
			const float scale = RE::bhkWorld::GetWorldScale();
			RE::bhkPickData pick{};
			pick.rayInput.from = RE::hkVector4(from * scale);
			pick.rayInput.to = RE::hkVector4(to * scale);
			pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kCameraPick);
			pick.rayOutput.Reset();
			world->PickObject(pick);
			if (!pick.rayOutput.HasHit() || !pick.rayOutput.rootCollidable) return false;
			auto* ref = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
			if (!IsIceRef(ref)) return false;
			a_top = from.z + (to.z - from.z) * std::clamp(pick.rayOutput.hitFraction, 0.0f, 1.0f);
			return a_top - a_water <= 90.0f;
		}

		// A feeler ray: terrain, ground, statics, props, clutter, trees count; our own refs (spawned, FF), actors and ice never.
		bool FeelSweep(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float& a_frac, RE::COL_LAYER& a_layer, RE::TESObjectREFR** a_ref = nullptr)
		{
			if (a_ref) *a_ref = nullptr;
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(a_from) : nullptr;
			auto* world = cell && cell->IsAttached() ? cell->GetbhkWorld() : nullptr;
			if (!world) return false;
			RE::bhkPickData pick{};
			const float scale = RE::bhkWorld::GetWorldScale();
			pick.rayInput.from = RE::hkVector4(a_from * scale);
			pick.rayInput.to = RE::hkVector4(a_to * scale);
			pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kCameraPick);
			pick.rayOutput.Reset();
			world->PickObject(pick);
			if (!pick.rayOutput.HasHit() || !pick.rayOutput.rootCollidable) return false;
			const auto layer = pick.rayOutput.rootCollidable->GetCollisionLayer();
			if (layer != RE::COL_LAYER::kTerrain && layer != RE::COL_LAYER::kGround && layer != RE::COL_LAYER::kStatic && layer != RE::COL_LAYER::kAnimStatic &&
				layer != RE::COL_LAYER::kProps && layer != RE::COL_LAYER::kClutter && layer != RE::COL_LAYER::kTrees)
				return false;
			if (auto* hit = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable)) {
				if (hit->GetFormID() == refId_ || hit->GetFormID() == carrierId_ || hit->As<RE::Actor>() || (hit->GetFormID() >> 24) == 0xFF || IsIceRef(hit)) return false;
				if (a_ref) *a_ref = hit;
			}
			a_frac = std::clamp(pick.rayOutput.hitFraction, 0.0f, 1.0f);
			a_layer = layer;
			return true;
		}

		void UpdateGround(float a_dt, const RE::NiPoint3& a_fwd, const RE::NiPoint3& a_right)
		{
			const float tipF = kFeelLen - 10.0f, sternF = -(kFeelLen - 10.0f), side = kFeelWidth - 10.0f;
			const float hr[kGN] = { 0.0f, 0.0f, 0.0f, side, -side, 0.5f * kFeelWidth, -0.5f * kFeelWidth };
			const float hf[kGN] = { 0.0f, tipF, sternF, 0.0f, 0.0f, 0.8f * kFeelLen, 0.8f * kFeelLen };
			float req[kGN]{}, surf[kGN]{}, landA[kGN]{}, waterA[kGN]{};
			bool ok[kGN]{}, wetRaw[kGN]{}, touchRaw[kGN]{};
			bool tipSeen = false, govSteep = false, iceCentre = false;
			int iceN = 0, dry = 0;
			RE::NiPoint3 bowPos{};
			auto* tes = RE::TES::GetSingleton();
			const float base = pos_.z - kWaterZ - heightOff_;  // the waterline the boat floats at
			if (haveWater_) {
				for (int i = 0; i < kGN; ++i) {
					const RE::NiPoint3 pt{ pos_.x + a_right.x * hr[i] + a_fwd.x * hf[i], pos_.y + a_right.y * hr[i] + a_fwd.y * hf[i], pos_.z };
					if (i == 1) bowPos = pt;
					float w = 0.0f, land = 0.0f;
					int why = 0;
					const bool wet = BoatWaterAt(pt, w, why);
					const bool landOk = why != 1 && tes && tes->GetLandHeight(pt, land) && std::isfinite(land);
					if (!wet && !landOk) continue;
					const float water = wet ? w : waterZ_;
					landA[i] = landOk ? land : -99999.0f;
					waterA[i] = water;
					if (i == 1) tipSeen = landOk;
					if (wet) {
						float top = 0.0f;
						if (IceTopAt(pt, water, top)) {
							req[i] = (std::max)(water, top);
							surf[i] = top;
							ok[i] = wetRaw[i] = touchRaw[i] = true;
							++iceN;
							if (i == 0) iceCentre = true;
							continue;
						}
					}
					const float rise = landOk ? land - water : -1.0e9f;
					if (bankState_[i]) {
						if (rise < 32.0f) { if ((bankClearTimer_[i] += a_dt) >= 0.3f) { bankState_[i] = false; bankClearTimer_[i] = 0.0f; } }
						else bankClearTimer_[i] = 0.0f;
					} else if (rise > kBeachMaxRise) {
						bankState_[i] = true;
						bankClearTimer_[i] = 0.0f;
					}
					const bool dryFit = landOk && land >= water + kGroundDryRise;
					req[i] = (std::max)(water, dryFit ? land - kGroundLiftAllowance : -1.0e9f);
					surf[i] = dryFit ? land : water;
					ok[i] = true;
					wetRaw[i] = !dryFit;
					touchRaw[i] = dryFit;
				}
				if (ok[0]) {
					for (int i = 1; i < kGN; ++i) {
						if (!ok[i]) continue;
						const float run = std::hypot(hf[i], hr[i]);
						if ((i == 1 || i == 5 || i == 6) && surf[i] > surf[0] && surf[i] - surf[0] > kMaxBankSlope * run) govSteep = true;
					}
				}
			}
			for (int i = 0; i < kGN; ++i) {
				auto settle = [&](bool a_raw, bool& a_state, float& a_timer) {
					if (a_raw == a_state) { a_timer = 0.0f; return; }
					if ((a_timer += a_dt) >= kDryHysteresis) { a_state = a_raw; a_timer = 0.0f; }
				};
				settle(touchRaw[i], touchState_[i], touchTimer_[i]);
				settle(ok[i] && !wetRaw[i], dryState_[i], dryTimer_[i]);
				if (dryState_[i]) ++dry;
			}
			bool touch = false;
			for (int i = 0; i < kGN; ++i) touch = touch || (ok[i] && touchState_[i]);
			float targetPitch = 0.0f, targetRoll = 0.0f, liftTarget = 0.0f;
			bool grounded = false;
			if (ok[0] && touch) {
				double n = 0, Sf = 0, Sr = 0, Sff = 0, Srr = 0, Sfr = 0, Sz = 0, Sfz = 0, Srz = 0;
				for (int i = 0; i < kGN; ++i) {
					if (!ok[i]) continue;
					const double f = hf[i], r = hr[i], z = req[i] - req[0];
					n += 1; Sf += f; Sr += r; Sff += f * f; Srr += r * r; Sfr += f * r; Sz += z; Sfz += f * z; Srz += r * z;
				}
				float b = 0.0f, c = 0.0f;
				const double cff = Sff - Sf * Sf / n, crr = Srr - Sr * Sr / n, cfr = Sfr - Sf * Sr / n, cfz = Sfz - Sf * Sz / n, crz = Srz - Sr * Sz / n;
				const double det = cff * crr - cfr * cfr;
				if (n >= 3 && std::abs(det) > 1.0) {
					b = static_cast<float>((cfz * crr - crz * cfr) / det);
					c = static_cast<float>((crz * cff - cfz * cfr) / det);
				}
				targetPitch = kMaxGroundPitch * std::tanh(std::atan(b) / kMaxGroundPitch);  // + bow up
				targetRoll = kMaxGroundRoll * std::tanh(-std::atan(c) / kMaxGroundRoll);    // + starboard down
				grounded = true;
				if (dry >= 3 && targetPitch < 0.0f && speed_ > 5.0f) {  // basin guard: mostly dry, bow down, moving on
					grounded = false;
					targetPitch = targetRoll = 0.0f;
				}
				if (grounded) {
					const float np = groundPitch_, nr = groundRoll_;
					const float spn = std::sin(np), cpsr = std::cos(np) * std::sin(nr);
					double acc = 1.0;
					for (int i = 0; i < kGN; ++i) {
						if (!ok[i]) continue;
						const float e = std::clamp(req[i] - (spn * hf[i] - cpsr * hr[i]) - base, 0.0f, 200.0f);
						acc += std::exp(0.15 * e) - 1.0;
					}
					liftTarget = static_cast<float>(std::log(acc) / 0.15);
				}
			}
			const bool bowDry = dryState_[1] || dryState_[5] || dryState_[6];
			const float liftCap = iceN > 0 ? 95.0f : (bowDry ? 60.0f : kMaxGroundLift);
			const float rawLift = liftTarget;
			liftTarget = std::clamp(liftTarget, 0.0f, liftCap);
			if (iceN > 0 && iceSamplesPrev_ == 0) logger::info("Ice: on an ice sheet ({} of 7 hull samples)", iceN);
			else if (iceN == 0 && iceSamplesPrev_ > 0) logger::info("Ice: left the sheet");
			iceSamplesPrev_ = iceN;
			onIce_ = iceCentre;
			// critically damped springs: no overshoot; the targets go to 0 off the ground
			const float tp = grounded ? targetPitch : 0.0f, tr = grounded ? targetRoll : 0.0f;
			float tl = (grounded || dry > 0 || iceN > 0) ? liftTarget : 0.0f;
			if (rideTime_ < rockAheadUntil_) tl = 0.0f;  // OAS 48.2: no bow lift up a rock face
			const float wl = iceN > 0 ? 8.0f : 9.0f;
			const int nSub = (std::max)(1, static_cast<int>(std::ceil(a_dt / 0.01f)));
			const float h = a_dt / static_cast<float>(nSub);
			for (int k = 0; k < nSub; ++k) {
				liftVel_ += (wl * wl * (tl - groundLift_) - 2.0f * wl * liftVel_) * h;
				groundLift_ += liftVel_ * h;
				pitchVel_ += (100.0f * (tp - groundPitch_) - 20.0f * pitchVel_) * h;
				groundPitch_ += pitchVel_ * h;
				rollVel_ += (49.0f * (tr - groundRoll_) - 14.0f * rollVel_) * h;
				groundRoll_ += rollVel_ * h;
			}
			groundLift_ = std::clamp(groundLift_, 0.0f, liftCap);
			// the bank governor: the speed allowed shrinks as the lift needed nears the cap, at a steep bow slope, a bank within
			// 60 ahead of the tip (stops 8 short of it) or a rock face ahead; blended in over 0.3 s
			float tipBank = 1.0e9f;
			if (tipSeen && tes) {
				for (float d = 10.0f; d <= 60.0f; d += 10.0f) {
					float l = 0.0f;
					const RE::NiPoint3 q{ bowPos.x + a_fwd.x * d, bowPos.y + a_fwd.y * d, bowPos.z };
					if (tes->GetLandHeight(q, l) && std::isfinite(l) && l - waterA[1] > kBeachMaxRise) { tipBank = d; break; }
				}
			}
			const bool rockAhead = rideTime_ < rockAheadUntil_;
			const bool engage = speed_ > 0.5f && (rawLift > 0.75f * liftCap || govSteep || tipBank < 1.0e8f || rockAhead);
			float govTarget = 1.0f;
			if (engage) govTarget = govSteep ? 0.0f : std::clamp((liftCap - rawLift) / (0.25f * liftCap), 0.0f, 1.0f);
			if (engage && tipBank < 1.0e8f) govTarget = (std::min)(govTarget, std::clamp((tipBank - 8.0f) / 52.0f, 0.0f, 1.0f));
			if (engage && rockAhead) govTarget = 0.0f;
			if (engage && !govEngaged_) {
				govEngaged_ = true;
				govRefSpeed_ = (std::max)(speed_, 30.0f);
				logger::info("Ground: bank governor engaged (tip rise {:.0f}, lift needed {:.0f} of {:.0f}{}{})", tipSeen ? landA[1] - waterA[1] : 0.0f, rawLift, liftCap,
					govSteep ? ", steep" : "", rockAhead ? ", rock" : "");
			}
			governor_ += (govTarget - governor_) * (1.0f - std::exp(-a_dt / 0.3f));
			if (govEngaged_) {
				if (speed_ > 0.0f) speed_ = (std::min)(speed_, governor_ * govRefSpeed_);
				if (!engage && governor_ > 0.98f) { govEngaged_ = false; logger::info("Ground: bank governor released"); }
			}
			if (dry > 0 && (groundLog_ += a_dt) >= 1.0f) {
				groundLog_ = 0.0f;
				logger::info("Ground: lift {:.1f} (target {:.1f}) pitch {:.1f} roll {:.1f} governor {:.2f}, {} dry", groundLift_, liftTarget, groundPitch_ * 57.2958f,
					groundRoll_ * 57.2958f, governor_, dry);
			}
		}

		void UpdateHullFeel(float a_dt, const RE::NiPoint3& a_fwd, const RE::NiPoint3& a_right)
		{
			for (auto& c : feelReactCd_) c = (std::max)(0.0f, c - a_dt);
			feelLogCd_ = (std::max)(0.0f, feelLogCd_ - a_dt);
			if (!haveWater_ || rideTime_ < 3.0f) {
				for (auto& t : feelTouch_) t = false;
				return;
			}
			const Feeler* kFeel = Feelers();
			const int slot = static_cast<int>(feelFrame_++ % 3);
			const float water = waterZ_;
			const float moved = speed_ * a_dt;
			auto toWorld = [&](float a_r, float a_f, float a_z) { return RE::NiPoint3{ pos_.x + a_right.x * a_r + a_fwd.x * a_f, pos_.y + a_right.y * a_r + a_fwd.y * a_f, a_z }; };
			auto* tes = RE::TES::GetSingleton();
			for (int i = 0; i < kFeelers; ++i) {
				const Feeler& fl = kFeel[i];
				if (fl.group != kFeelBow && i % 3 != slot) continue;
				const float r = fl.right * kFeelWidth, f = fl.fwd * kFeelLen;
				bool hit = false, landHit = false;
				RE::COL_LAYER layer = RE::COL_LAYER::kUnidentified;
				float frac = 1.0f, dist = 0.0f;
				RE::TESObjectREFR* hitRef = nullptr;
				// v1.7.21: the bow feelers see far enough ahead for a soft stop (half the stop's travel)
				const float softReach = fl.group == kFeelBow ? std::abs(speed_) * kSoftStopMax * 0.5f + kFeelTouch : 0.0f;
				if (fl.group != kFeelKeelG) {
					const float reach = (std::max)(kFeelReach + (std::max)(0.0f, moved * fl.dirFwd), softReach);
					if (FeelSweep(toWorld(r, f, water + 30.0f), toWorld(r + fl.dirRight * reach, f + fl.dirFwd * reach, water + 30.0f), frac, layer, &hitRef)) {
						hit = true;
						dist = frac * reach;
					}
				} else {
					const float keelLine = water - kFeelKeel;
					if (FeelSweep(toWorld(r, f, water + 5.0f), toWorld(r, f, keelLine + 4.0f), frac, layer)) hit = true;
					float land = 0.0f;
					if (!hit && tes && tes->GetLandHeight(toWorld(r, f, water), land) && std::isfinite(land) && land >= keelLine + kGroundDryRise) {
						hit = true;
						landHit = true;
						layer = RE::COL_LAYER::kTerrain;
					}
				}
				feelTouch_[i] = hit;
				if (!hit) continue;
				const bool terrain = landHit || layer == RE::COL_LAYER::kTerrain || layer == RE::COL_LAYER::kGround;
				const float vRight = turn_ * f, vFwd = speed_ - turn_ * r;
				float closing = fl.group == kFeelKeelG ? std::abs(speed_) : vRight * fl.dirRight + vFwd * fl.dirFwd;
				if (fl.group != kFeelKeelG && feelPrevAt_[i] > 0.0f && rideTime_ - feelPrevAt_[i] < 0.2f && rideTime_ > feelPrevAt_[i])
					closing = (std::max)(closing, (feelPrevDist_[i] - dist) / (rideTime_ - feelPrevAt_[i]));
				feelPrevDist_[i] = dist;
				feelPrevAt_[i] = rideTime_;
				bool contact = fl.group == kFeelKeelG || dist <= kFeelTouch + (std::max)(0.0f, closing) * a_dt;
				// v1.7.21: an object ahead of the bow (jetty, dock, longboat, wood, a boulder) is met with a soft stop, begun early
				// enough to come to rest at it; a small rock (top under 40 over the water) is only a bump under the keel
				bool smallRock = false;
				if (hitRef && !terrain) {
					const RE::NiPoint3 bmax = hitRef->GetBoundMax();
					const float top = hitRef->GetPosition().z + bmax.z * hitRef->GetScale();
					smallRock = top - water < 40.0f && RefHay(hitRef).find("rock") != std::string::npos;
				}
				if (fl.group == kFeelBow && !terrain && !smallRock && closing > kFeelMinClosing && dist <= std::abs(speed_) * kSoftStopMax * 0.5f + kFeelTouch) contact = true;
				// OAS 48.2: a tall, steep terrain face at the waterline (a bank boulder) is a rock, not a shore
				bool rock = false;
				if (terrain && !landHit && fl.group != kFeelKeelG && rideTime_ >= rockOffUntil_ && groundLift_ < 0.5f && !onIce_ && tes) {
					const float hrr = r + fl.dirRight * dist, hff = f + fl.dirFwd * dist;
					const RE::NiPoint3 hitPt = toWorld(hrr, hff, water);
					const RE::NiPoint3 beyond = toWorld(hrr + fl.dirRight * 40.0f, hff + fl.dirFwd * 40.0f, water);
					float l0 = 0.0f, l1 = 0.0f;
					if (tes->GetLandHeight(hitPt, l0) && std::isfinite(l0) && tes->GetLandHeight(beyond, l1) && std::isfinite(l1) && l0 - water > 50.0f && l1 - l0 >= 40.0f) {
						rock = true;
						const float sx = hitPt.x - rockSpot_.x, sy = hitPt.y - rockSpot_.y;
						if (rideTime_ - rockWindowAt_ > 2.0f || sx * sx + sy * sy > 200.0f * 200.0f) { rockWindowAt_ = rideTime_; rockSpot_ = hitPt; rockFires_ = 0; }
						if (contact && ++rockFires_ > 6) {  // chatter guard: off for 3 s
							rockOffUntil_ = rideTime_ + 3.0f;
							rock = false;
						}
						if (rock && fl.group == kFeelBow && dist <= 40.0f) rockAheadUntil_ = rideTime_ + 0.25f;
					}
				}
				if (smallRock && contact && fl.group == kFeelBow) {
					FeelScrape(i, fl, a_fwd, a_right, closing, "small rock");
					if (std::abs(speed_) >= kFeelMinSpeed && feelReactCd_[i] > 0.4f) { dipVel_ += -50.0f; groundPitch_ += 0.015f; }
				} else if ((rock || !terrain) && contact) FeelReact(i, fl, a_fwd, a_right, closing, dist);
				if (feelLogCd_ <= 0.0f) {
					feelLogCd_ = 0.5f;
					logger::info("Feel: {} hit {}{} at {:.0f}, speed {:.0f}, closing {:.0f}", fl.name, rock ? "rock" : terrain ? "terrain (the ground's)" : "an object",
						landHit ? " (land height)" : "", dist, speed_, closing);
				}
			}
		}

		void FeelScrape(int a_i, const Feeler& a_fl, const RE::NiPoint3& a_fwd, const RE::NiPoint3& a_right, float a_closing, const char* a_what)
		{
			if (feelReactCd_[a_i] > 0.0f || std::abs(speed_) < kFeelMinSpeed || a_closing < kFeelMinClosing) return;
			feelReactCd_[a_i] = 0.5f;
			const float wx = a_right.x * a_fl.dirRight + a_fwd.x * a_fl.dirFwd, wy = a_right.y * a_fl.dirRight + a_fwd.y * a_fl.dirFwd;
			bool otherSide = false;
			const Feeler* kFeel = Feelers();
			for (int j = 0; j < kFeelers; ++j) if (j != a_i && feelTouch_[j] && a_fl.side != 0 && kFeel[j].side == -a_fl.side) otherSide = true;
			const float push = kFeelPushSpeed * (otherSide ? 0.5f : 1.0f);
			feelPushX_ -= wx * push;
			feelPushY_ -= wy * push;
			const float len = std::sqrt(feelPushX_ * feelPushX_ + feelPushY_ * feelPushY_);
			if (len > kFeelPushMax) { feelPushX_ *= kFeelPushMax / len; feelPushY_ *= kFeelPushMax / len; }
			const float before = speed_;
			speed_ *= 1.0f - 0.04f * (otherSide ? 0.5f : 1.0f);
			logger::info("Feel: scrape {} ({}), speed {:.0f} -> {:.0f}", a_fl.name, a_what, before, speed_);
		}

		void FeelReact(int a_i, const Feeler& a_fl, const RE::NiPoint3& a_fwd, const RE::NiPoint3& a_right, float a_closing, float a_dist = 0.0f)
		{
			switch (a_fl.group) {
			case kFeelBow:
				// v1.7.21: every bow hit worth the name stops the boat, softly: eased over the time it takes to cover the gap
				// (0.25-0.8 s), a small knock in pitch scaled to the speed; was a 0.2 s stop over 150 closing, a 4% scrape below
				if (a_closing > kFeelMinClosing && std::abs(speed_) >= kFeelMinSpeed && feelReactCd_[a_i] <= 0.0f && feelStopTimer_ <= 0.0f) {
					feelReactCd_[a_i] = 0.5f;
					const float t = std::clamp(2.0f * (std::max)(a_dist - 2.0f, 0.0f) / (std::max)(std::abs(speed_), 1.0f), kSoftStopMin, kSoftStopMax);
					feelStopTime_ = t;
					feelStopTimer_ = t;
					feelStopFrom_ = speed_;
					rockKnock_ = std::clamp(speed_ / 300.0f, -1.0f, 1.0f) * 0.015f;  // a softer knock in pitch
					logger::info("Feel: soft stop {} at speed {:.0f}, {:.0f} ahead, over {:.2f} s", a_fl.name, speed_, a_dist, t);
				} else if (feelStopTimer_ <= 0.0f) {
					FeelScrape(a_i, a_fl, a_fwd, a_right, a_closing, "slow bow touch");
				}
				if (a_fl.side != 0 && !feelTouch_[0] && !feelTouch_[a_fl.side < 0 ? 2 : 1] && std::abs(speed_) >= kFeelMinSpeed && a_closing >= kFeelMinClosing)
					feelYawKick_ = std::clamp(feelYawKick_ - static_cast<float>(a_fl.side) * kFeelYawKick, -kFeelYawKick, kFeelYawKick);
				break;
			case kFeelSide:
			case kFeelStern:
				FeelScrape(a_i, a_fl, a_fwd, a_right, a_closing, a_fl.group == kFeelSide ? "side" : "stern corner");
				break;
			case kFeelKeelG: {
				if (feelReactCd_[a_i] > 0.0f || std::abs(speed_) < kFeelMinSpeed) break;  // RowRim: a still boat on gravel is never bumped
				feelReactCd_[a_i] = 0.4f;
				const float before = speed_;
				speed_ *= 0.95f;
				dipVel_ += -80.0f;
				const float jolt = 0.026f;  // 1.5 deg
				groundPitch_ += a_fl.fwd > 0.0f ? jolt : (a_fl.fwd < 0.0f ? -jolt : 0.0f);
				groundRoll_ += (feelFrame_ & 1u) ? jolt * 0.6f : -jolt * 0.6f;
				logger::info("Feel: bump {} speed {:.0f} -> {:.0f}", a_fl.name, before, speed_);
				break;
			}
			}
		}

		// every frame: the timed bow stop, the yaw kick, the fading push (never onto land), the keel dip spring
		void FeelApplyMotion(float a_dt)
		{
			if (feelStopTimer_ > 0.0f) {
				feelStopTimer_ -= a_dt;
				const float t = std::clamp(1.0f - feelStopTimer_ / (std::max)(feelStopTime_, 0.05f), 0.0f, 1.0f);
				const float cap = std::abs(feelStopFrom_) * (1.0f - t * t * (3.0f - 2.0f * t));  // v1.7.21: eased in and out
				speed_ = std::clamp(speed_, -cap, cap);
				if (feelStopTimer_ <= 0.0f) speed_ = 0.0f;
			}
			if (inLeft_ || inRight_) feelYawKick_ = 0.0f;  // OAS 44.3.3: no feeler turning while you steer
			if (std::abs(feelYawKick_) > 0.001f) {
				yaw_ += feelYawKick_ * a_dt;
				feelYawKick_ *= std::exp(-7.5f * a_dt);
			} else feelYawKick_ = 0.0f;
			if (feelPushX_ != 0.0f || feelPushY_ != 0.0f) {
				const RE::NiPoint3 to{ pos_.x + feelPushX_ * a_dt, pos_.y + feelPushY_ * a_dt, pos_.z };
				float w = 0.0f;
				int why = 0;
				if (BoatWaterAt(to, w, why)) {
					pos_.x = to.x;
					pos_.y = to.y;
				} else {
					feelPushX_ = feelPushY_ = 0.0f;
				}
				const float k = std::exp(-6.0f * a_dt);
				feelPushX_ *= k;
				feelPushY_ *= k;
				if (feelPushX_ * feelPushX_ + feelPushY_ * feelPushY_ < 0.25f) feelPushX_ = feelPushY_ = 0.0f;
			}
			for (float left = (std::min)(a_dt, 0.1f); left > 0.0f;) {
				const float d = (std::min)(0.01f, left);
				dipVel_ += (-90.0f * dip_ - 19.0f * dipVel_) * d;  // OAS buoyant dip: stiffness 90, about critically damped
				dip_ = std::clamp(dip_ + dipVel_ * d, -28.0f, 12.0f);
				left -= d;
			}
		}
		float rockKnock_{ 0.0f };
		int paint_{ Paint::kBlue };              // OAS default
		const RE::NiAVObject* paintedRoot_{ nullptr };
		int paintedAs_{ -1 };

		// ---- v0.8.0: the boat's own motion (OAS BoatBow Update / UpdateParked) ----
		// Idle rock: slow uneven rocking from two sines each (roll ~1.1 deg, pitch ~0.6 deg, ~2.4 units heave), fading to 65%
		// at full row speed. Stroke squat: each oar dig lifts the bow ~0.7 deg (a spring, stiffness 45, damping 7). Turn lean:
		// into the turn, up to 0.25 rad at full speed. Weight (56.64/56.68): the player standing aboard lists it toward their
		// side (0.13 rad at the gunwale), dips their end (0.045 rad at the tips) and sinks it 1.5 (afloat; a third, no sink, on
		// land). Here all of it moves the real collision (no feet pivot trick needed).
		void BoatMotion(float a_dt, float& a_roll, float& a_pitch, float& a_heave)
		{
			const float flip = bowFlip_ ? -1.0f : 1.0f;  // the bow is the model's -Y: pitch and roll from the boat's point of view turn over
			const float s01 = std::clamp(std::abs(speed_) / rowMax_, 0.0f, 1.0f);
			const float t = rideTime_;
			auto smooth = [](float a_lo, float a_hi, float a_x) {
				const float u = std::clamp((a_x - a_lo) / (a_hi - a_lo), 0.0f, 1.0f);
				return u * u * (3.0f - 2.0f * u);
			};
			const float r = idleBob_ * (1.0f - 0.35f * smooth(0.0f, 0.6f, s01)) * (onLand_ ? 0.0f : 1.0f) * (1.0f - smooth(0.0f, 8.0f, groundLift_));  // v1.4.0: grounded, no bob
			const float idleRoll = r * 0.019f * (0.68f * std::sin(t * 1.19f) + 0.32f * std::sin(t * 2.31f + 0.9f));
			const float idlePitch = r * 0.010f * (0.64f * std::sin(t * 1.67f + 1.7f) + 0.36f * std::sin(t * 0.83f + 0.4f));
			const float idleHeave = r * 2.4f * (0.7f * std::sin(t * 1.43f + 0.3f) + 0.3f * std::sin(t * 2.7f + 2.1f));
			// turn lean (the boat's turn rate: + right)
			const float leanTarget = std::clamp(turn_ * 0.35f * s01, -0.25f, 0.25f);
			leanRoll_ += (leanTarget - leanRoll_) * (1.0f - std::exp(-6.0f * a_dt));
			// stroke squat
			const float squatTarget = onLand_ ? 0.0f : 0.012f * (lastThrust_ / kPi);
			for (float left = (std::min)(a_dt, 0.1f); left > 0.0f;) {
				const float d = (std::min)(0.01f, left);
				squatVel_ += (45.0f * (squatTarget - squat_) - 7.0f * squatVel_) * d;
				squat_ += squatVel_ * d;
				left -= d;
			}
			// weight of the player standing aboard (not seated)
			auto* player = RE::PlayerCharacter::GetSingleton();
			bool on = false;
			RE::NiPoint3 d{};
			if (player && !seated_) {
				d = PlayerLocal(player);
				auto* st = player->AsActorState();
				on = std::abs(d.x) < kWeightHalfWide && std::abs(d.y) < kWeightHalfLen && d.z > -60.0f && d.z < 160.0f && !(st && st->IsSwimming());
			}
			if ((landCheck_ -= a_dt) <= 0.0f) {
				landCheck_ = 1.0f;
				float wz = 0.0f;
				onLand_ = !(WaterAt(pos_, wz) && wz > pos_.z - 60.0f);
			}
			const float amt = onLand_ ? 0.33f : 1.0f;
			const float wRoll = on ? amt * 0.10f * std::clamp(d.x / kWeightHalfWide, -1.0f, 1.0f) : 0.0f;
			const float wPitch = on ? -amt * 0.015f * std::clamp(d.y / kWeightHalfLen, -1.0f, 1.0f) : 0.0f;  // v1.4.0: a third (was 0.045)
			const float wSink = on && !onLand_ ? 1.5f : 0.0f;
			const float k = 1.0f - std::exp(-(on ? 3.0f : 1.8f) * a_dt);  // settles in under a second, rises back slower
			weightRoll_ += (wRoll - weightRoll_) * k;
			weightPitch_ += (wPitch - weightPitch_) * k;
			weightSink_ += (wSink - weightSink_) * k;
			if (on != weightOn_) {
				weightOn_ = on;
				logger::info("Weight: player {} the boat (local {:.0f},{:.0f},{:.0f}, {})", on ? "stepped onto" : "stepped off", d.x, d.y, d.z, onLand_ ? "on land" : "afloat");
			}
			a_roll = idleRoll + flip * leanRoll_ + weightRoll_ + flip * groundRoll_;  // the weight is measured in model space already
			rockKnock_ *= std::exp(-4.0f * a_dt);
			a_pitch = idlePitch + flip * squat_ + weightPitch_ - flip * rockKnock_ + flip * groundPitch_;  // v1.4.0: the ground's tilt
			a_heave = idleHeave - weightSink_ + groundLift_ + dip_;                                        // v1.4.0: the ground's lift, the keel dip
		}

		void GlueSeat(const RE::NiPoint3& a_at, const RE::NiMatrix3& a_rot, float a_dYaw, float a_dt)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			const RE::NiPoint3 w = a_at + a_rot * SeatPos();
			player->SetPosition(w, true);
			Crew::Glue(CrewPose(a_at, a_rot), a_dt);  // v1.6.0: the passengers held on their seats
			if (drawMode_ == 1 || drawMode_ == 2) player->Update3DPosition(true);  // v0.8.3 test path; OAS (draw prev) and the pin leave the 3D to the engine
			(void)a_dYaw;
			// v1.2.0: OAS ApplyPlayerBody. The body is carried by position, so its graph thinks it walks while W is held: the
			// movement inputs held at zero. Third person: the body faces along its seat every frame and the graph may not turn
			// it (bAllowRotation off); the camera orbit is the game's, held by CamTend. First person: the look is free (the
			// actor turns with the mouse), the sit state masked so the game does not limit the look, and the view put back to
			// the seat's facing when first person starts.
			for (const char* v : { "Speed", "SpeedSampled", "SpeedDesired", "Direction", "TurnDelta" }) player->SetGraphVariableFloat(v, 0.0f);
			player->SetGraphVariableBool("bHumanoidFootIKDisable", true);  // v1.6.5: foot IK lifted the feet onto the hull's collision (StandUp clears it)
			auto* cam = RE::PlayerCamera::GetSingleton();
			const bool firstPerson = cam && cam->IsInFirstPerson();
			if (!firstPerson && camWasFirst_ && seatKind_ == SeatKind::kRower) facePending_ = true;  // OAS 56.45: into third person at the oars
			if (firstPerson && !camWasFirst_) player->data.angle.z = SeatFacing();  // OAS: first person starts looking the seat's way
			camWasFirst_ = firstPerson;
			MaskSitForFreeLook(player, firstPerson);
			if (firstPerson) {
				player->SetGraphVariableBool("bAllowRotation", true);
			} else {
				player->SetGraphVariableBool("bAllowRotation", false);
				player->data.angle.x = 0.0f;
				player->data.angle.z = SeatFacing();
			}
			if (auto* cc = player->GetCharController()) cc->SetLinearVelocityImpl(RE::hkVector4());
			if (rowAnimOn_ || resitPending_ || fpSitMasked_) return;  // the row clip replaces the sit pose; a masked sit is still a sit
			// the sit can be dropped (or refused at first): try again a few times
			sitTimer_ += a_dt;
			if (sitTimer_ > 0.5f && sitTries_ < 4) {
				sitTimer_ = 0.0f;
				bool sitting = false;
				player->GetGraphVariableBool("bIsSitting", sitting);
				const auto* st = player->AsActorState();
				const bool engineSit = st && st->actorState1.sitSleepState != RE::SIT_SLEEP_STATE::kNormal;
				if (!sitting && !engineSit) {
					++sitTries_;
					TrySitAnim(player);
					if (sitTries_ == 4) logger::info("Seat: the game would not seat the player; held standing at the seat");
				}
			}
		}

		// ---- v1.2.0: the seated camera (OAS 56.45 face-the-rower, 56.46 first person free look, 56.57 HoldOrbitAboard) ----
		bool camWasFirst_{ true };
		bool facePending_{ false };
		bool orbitHave_{ false };
		float orbitX_{ 0.0f };
		float orbitUndone_{ 0.0f };
		int orbitLookFrames_{ 0 }, orbitFrames_{ 0 }, orbitFixLogs_{ 0 };
		float orbitLogTimer_{ 0.0f };
		bool fpSitMasked_{ false };
		RE::SIT_SLEEP_STATE fpSitSaved_{ RE::SIT_SLEEP_STATE::kNormal };

		void MaskSitForFreeLook(RE::PlayerCharacter* a_player, bool a_on)
		{
			auto* st = a_player ? a_player->AsActorState() : nullptr;
			if (!st) return;
			if (a_on) {
				const auto cur = st->actorState1.sitSleepState;
				if (cur == RE::SIT_SLEEP_STATE::kNormal) return;
				if (!fpSitMasked_) {
					fpSitSaved_ = cur;
					logger::info("Camera: first person seated, sit state {} cleared so the view can look around freely", static_cast<int>(cur));
				}
				fpSitMasked_ = true;
				st->actorState1.sitSleepState = RE::SIT_SLEEP_STATE::kNormal;
			} else if (fpSitMasked_) {
				fpSitMasked_ = false;
				if (st->actorState1.sitSleepState == RE::SIT_SLEEP_STATE::kNormal) st->actorState1.sitSleepState = fpSitSaved_;
				a_player->SetGraphVariableBool("bIsSitting", true);
				logger::info("Camera: first person left, sit state put back ({})", static_cast<int>(fpSitSaved_));
			}
		}

	public:
		// From ThirdPersonState::Update, after the game's: the orbit turned to face the rower once (boarding the oars, or
		// first -> third person at the oars), then only moved by real look input (mouse / right stick); a game reset or swing
		// is undone (the game kept swinging the orbit behind the seated actor: the spinning camera OAS fixed in 56.57).
		void CamTend(RE::ThirdPersonState* a_tps)
		{
			if (!a_tps) return;
			if (!seated_) { orbitHave_ = false; facePending_ = false; return; }
			if (facePending_) {
				facePending_ = false;
				a_tps->freeRotationEnabled = true;
				a_tps->freeRotation.x = kPi;
				orbitX_ = kPi;
				orbitHave_ = true;
				logger::info("Camera: third person turned to face the rower (W rows away from the camera)");
				return;
			}
			auto* pc = RE::PlayerControls::GetSingleton();
			const bool look = pc && (std::fabs(pc->data.lookInputVec.x) > 1.0e-4f || std::fabs(pc->data.lookInputVec.y) > 1.0e-4f);
			++orbitFrames_;
			if (look) ++orbitLookFrames_;
			if (orbitHave_ && !look) {
				if (!a_tps->freeRotationEnabled) {
					a_tps->freeRotationEnabled = true;
					a_tps->freeRotation.x = orbitX_;
					if (orbitFixLogs_++ < 6) logger::info("Camera: the game switched the orbit off aboard ({:.3f}), put back", orbitX_);
				} else {
					// v1.7.12: the difference as an angle (-pi..pi): a camera mod that wraps the orbit is not "a swing" of
					// hundreds of radians, and only the game's real swing is taken back off
					const float d = std::remainder(a_tps->freeRotation.x - orbitX_, 2.0f * kPi);
					if (std::fabs(d) > 1.0e-5f) {
						orbitUndone_ += std::fabs(d);
						a_tps->freeRotation.x -= d;
					}
				}
			}
			if (a_tps->freeRotationEnabled) {
				orbitX_ = a_tps->freeRotation.x;
				orbitHave_ = true;
			}
			if (orbitFrames_ >= 120) {
				if (orbitUndone_ > 0.001f || orbitLookFrames_ > 0)
					logger::info("Camera: orbit aboard {:.3f}, game swing undone {:.3f} rad, look input on {} of {} frames", orbitX_, orbitUndone_, orbitLookFrames_, orbitFrames_);
				orbitUndone_ = 0.0f;
				orbitLookFrames_ = orbitFrames_ = 0;
			}
		}

	private:
		bool seated_{ false };
		RE::NiAVObject* root_{ nullptr };
		float hopTimer_{ 0.0f };
		int hopsSinceLoad_{ 0 };
		bool cellLost_{ false };  // v1.5.7: the hull's cell detached while you were away
		bool persist_{ true };
		bool hop_{ true };  // v0.2.6: back on; a ref filed in an unloaded cell crashed the renderer (v0.2.4, v0.2.5)
		bool regraft_{ true };
		bool mobile_{ false };
		bool carrierOn_{ true };
		bool carrierAI_{ false };
		bool lateOn_{ true };
		bool aiApplied_{ true };
		bool nudgeOn_{ true };
		std::string wbMode_{};  // v0.5.0: off (the raised floor does the job); rr "wb a" still works with the v0.4.1 mesh
		RE::NiAVObject* wbA_[10]{};
		RE::NiAVObject* wbD_[10]{};
		RE::NiAVObject* wbShown_{ nullptr };
		int wbShownZ_{ -1 };
		float wbTimer_{ 0.0f };
		bool wbHaveWater_{ false };
		float wbWater_{ 0.0f };
		float wbNeed_{ 0.0f };
		static constexpr float kWbMargin = 1.5f;
		float wbZ_{ 0.0f };
		bool wbFloor_{ false };
		int wbLogs_{ 0 };
		float nudgeTime_{ -1.0f };
		int nudgeFrames_{ 0 };
		float walkFix_{ 0.0f };
		float walkFixTick_{ 0.0f };
		int walkFixLogs_{ 0 };
		float pinMax_{ 0.0f };
		int pinMoved_{ 0 };
		static constexpr float kNudgeSpeed = 10.0f;  // u/s, ~1 unit over the nudge
		RE::NiPoint3 lastPb_{};
		Quat lastQb_{};
		bool haveLastTarget_{ false };
		static constexpr float kCorrect = 12.0f;  // per second: a gap closes in ~1/12 s, never in one step  // v0.3.1: off by default (with AI the hidden horse fled or attacked, dragging the boat)
		RE::FormID carrierId_{ 0 };
		RE::NiPointer<RE::NiNode> visual_;
		// v1.4.4: the player's lantern (the lake boats' hook and swing): a clone hung on the boat model, a light ref at night
		struct LampSwing
		{
			float pitch = 0.0f, roll = 0.0f, pitchVel = 0.0f, rollVel = 0.0f;
			RE::NiPoint3 prevPivot{}, prevVel{};
			bool have = false;
		};
		RE::NiPointer<RE::NiNode> lamp_;
		RE::ObjectRefHandle lampLight_;
		RE::ActorHandle rowAnimActor_;  // v1.7.9: who the row clip was started on (the player or the crew rower)
		LampSwing lamp_st_{};
		bool lampOn_{ true };
		float lampCellTimer_{ 0.0f };
		int hiddenKids_{ 0 };
		float hideTimer_{ 0.0f };
		int hideLogs_{ 0 };
		// Original carrier placement restored; no elevated actor or stretched bounds.
		static constexpr float kCarrierDepth = 220.0f;
		int grafts_{ 0 };
		RE::NiPointer<RE::NiAVObject> heldOld_;
		float heldTime_{ 0.0f };
		RE::NiPoint3 lastAt_{};
		RE::NiMatrix3 lastRot_{};
		bool havePose_{ false };
		int inits_{ 0 };
		bool helm_{ false };
		bool bowFlip_{ false };
		bool inFwd_{ false }, inBack_{ false }, inLeft_{ false }, inRight_{ false };
		float stickX_{ 0.0f }, stickY_{ 0.0f };
		int sitTries_{ 0 };
		float sitTimer_{ 0.0f };
		RE::NiPoint3 seat_{ 0.0f, -215.0f, 35.0f };      // v0.6.0: the stern seat by the tiller (top z 66.3)
		RE::NiPoint3 rowSeat_{ 0.0f, 15.0f, 33.0f };     // v1.4.0: where the row clip sits (was 0 0 33 + the 15 rowing shift): rests there too
		SeatKind seatKind_{ SeatKind::kHelm };
		float rideTime_{ 0.0f };
		float sitAt_{ 0.0f };
		// row clip (OAS 18 / 35)
		static constexpr float kClipLeadIn = 0.12f;      // the oars hold at the catch this long after the clip is accepted
		static constexpr float kRowStopDebounce = 0.6f;  // short W taps do not stop and restart the clip
		bool rowWant_{ false };
		bool rowAnimOn_{ false };
		bool rowPending_{ false };
		bool resitPending_{ false };
		bool rowEverAccepted_{ false };
		int rowRejects_{ 0 };
		float rowAnimOnAt_{ 0.0f };
		float rowOffTimer_{ 0.0f };
		float lastRowStopAt_{ -10.0f };
		float rowRetryWait_{ 0.0f };
		float rowPendingFor_{ 0.0f };
		float animRotUntil_{ 0.0f };
		float rowLogTimer_{ 0.0f };
		// stroke and speed (OAS row profile)
		float oarPhase_{ 0.0f }, oarAmp_{ 0.0f };
		float rowMax_{ 300.0f };
		float rowDrag_{ 0.07f };
		float turnScale_{ 1.0f };  // v1.7.8: [Row] Turn
		bool rowLoaded_{ false };
		// oars
		RE::NiPointer<RE::NiNode> oars_[2];          // 0 right, 1 left
		RE::NiTransform clipLocal_[2]{};             // the clip's pose, boat frame (held for the blend back)
		bool haveClip_[2]{};
		bool oarsToClip_{ false };
		bool posedLogged_{ false };
		bool oarsRunLogged_{ false };
		bool lateLogged_{ false };
		// v1.2.1: while rowing, per second: how far the drawn rower (3D root), the camera and the Cam3rd bone move against the
		// drawn boat (boat frame: x right, y forward, z up), min..max. The one that swings is what shows as the boat shifting.
		struct Range { float lo[3]{ 1e9f, 1e9f, 1e9f }, hi[3]{ -1e9f, -1e9f, -1e9f }; void Add(const RE::NiPoint3& p) { const float v[3] = { p.x, p.y, p.z }; for (int i = 0; i < 3; ++i) { lo[i] = (std::min)(lo[i], v[i]); hi[i] = (std::max)(hi[i], v[i]); } } std::string Text() const { return std::format("x {:.1f} y {:.1f} z {:.1f}", hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]); } };
		Range diagRower_, diagCam_, diagCam3_, diagBoat_;
		int diagFrames_{ 0 };
		RE::NiPoint3 diagLastBoat_{};
		void ViewDiag(const RE::NiMatrix3& a_rot)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* proot = player ? player->Get3D(false) : nullptr;
			auto* cam = RE::PlayerCamera::GetSingleton();
			auto* croot = cam ? cam->cameraRoot.get() : nullptr;
			if (!proot || !croot || !visual_) return;
			const RE::NiPoint3 b = visual_->world.translate;
			const RE::NiMatrix3 inv = a_rot.Transpose();
			diagRower_.Add(inv * (proot->world.translate - b));
			diagCam_.Add(inv * (croot->world.translate - b));
			if (auto* c3 = proot->GetObjectByName("Camera3rd [Cam3]")) diagCam3_.Add(inv * (c3->world.translate - b));
			// the boat's own step per frame along its heading (speed changes show as a spread)
			const RE::NiPoint3 step = inv * (b - diagLastBoat_);
			diagLastBoat_ = b;
			if (diagFrames_ > 0) diagBoat_.Add(step);
			if (++diagFrames_ >= 60) {
				logger::info("View: over {} frames vs the drawn boat: rower root {} | camera {} | Cam3rd bone {} | boat step per frame {} | pin moved the rower on {} frames, max {:.1f} (draw mode {})",
					diagFrames_, diagRower_.Text(), diagCam_.Text(), diagCam3_.Text(), diagBoat_.Text(), pinMoved_, pinMax_, drawMode_);
				pinMoved_ = 0;
				pinMax_ = 0.0f;
				diagRower_ = Range{};
				diagCam_ = Range{};
				diagCam3_ = Range{};
				diagBoat_ = Range{};
				diagFrames_ = 0;
			}
		}
		float drawLead_{ 0.7f };  // v1.6.6
		int drawMode_{ 0 };  // seated: 0 one frame behind (OAS, the default again since v1.6.1), 1 this frame's glue pose, 2 from the drawn player, 3 pinned (v1.2.3)
		RE::NiPoint3 prevAt_{};
		RE::NiMatrix3 prevRot_{};
		bool havePrev_{ false };
		int lateFrames_{ 0 };
		float lateGapMax_{ 0.0f };
		// v1.0.0: water following and fake collision (OAS SkimWaterAt / SkimSweep)
		float heightOff_{ 0.0f };
		float waterTimer_{ 0.0f };
		bool haveWater_{ false };
		float waterZ_{ 0.0f };
		float bumpLog_{ 0.0f };
		bool beached_{ false };
		float oarW_{ 0.0f };                         // 0 resting / stowed .. 1 in the hands
		float stowW_{ 0.0f };                        // v1.2.4: 0 in the oarlocks .. 1 stowed in the boat
		bool oarsStowed_{ false };
		float stowTick_{ 0.0f };
		static constexpr float kStowDist = 3000.0f;  // stowed only this far off (and out of view), or 3x as far
		float restIn_{ 0.30f };                      // resting oar: share of its length inboard of the oarlock (v0.6.0)
		float restDip_{ 12.0f };                     // resting blade down, degrees (v0.6.0)
		float rowShift_{ 0.0f };                     // v1.2.4 rowing shift; v1.4.0: 0, the seat itself is where the clip sits
		float rowShiftW_{ 0.0f };
		float noRowerNote_{ 0.0f };
		float handMoveMax_{ 0.0f };
		float handLo_[2]{ 1e9f, 1e9f }, handHi_[2]{ -1e9f, -1e9f };
		int handFrames_{ 0 };
		static constexpr float kOarBlend = 0.8f;     // OAS OarBlendSeconds
		static constexpr float kStowScale = 0.80f;   // OAS: the stowed size comes from the rowing tune (0.80)
		RE::NiPoint3 stowAt_{ 18.0f, 59.0f, 67.5f }; // OAS stowed pose, raised onto the v0.5.x thwarts (tops 64.1 / 65.6)
		float stowYaw_{ 0.13788f };                  // 7.9 deg
		// boat motion (OAS)
		float idleBob_{ 1.0f };
		float leanRoll_{ 0.0f }, squat_{ 0.0f }, squatVel_{ 0.0f }, lastThrust_{ 0.0f };
		float weightRoll_{ 0.0f }, weightPitch_{ 0.0f }, weightSink_{ 0.0f };
		bool weightOn_{ false }, onLand_{ false };
		float landCheck_{ 0.0f };
		static constexpr float kWeightHalfLen = 230.0f, kWeightHalfWide = 85.0f;  // OAS footprint about the hull centre
		bool handLock_{ true };
		int oarLogs_{ 0 };
		float oarRoll_{ 90.0f * kPi / 180.0f };          // OAS BakedOarRoll default
		RE::NiPoint3 lockLocal_{ 93.3f, 6.7f, 76.5f };  // v1.2.6: the middle oarlock forks where the row clip's oar shafts cross the gunwale (shaft centre; the cradle is 1.5 under it)


		RE::FormID refId_{ 0 };
		RE::FormID watchId_{ 0 };
		RE::bhkRigidBody* body_{ nullptr };
		bool needInit_{ false };
		int initTries_{ 0 };
		Mode mode_{ Mode::kDrive };
		RE::COL_LAYER layer_{ RE::COL_LAYER::kStatic };
		bool writeNode_{ true };
		bool flip_{ false };
		bool autoFlip_{ false };
		RE::NiPoint3 pos_{};
		float yaw_{ 0.0f };
		float t_{ 0.0f };
		float speed_{ 0.0f }, turn_{ 0.0f };
		float rockRoll_{ 0.0f }, rockPitch_{ 0.0f }, rockHeave_{ 0.0f };
		float ampRoll_{ 0.0f }, ampPitch_{ 0.0f }, ampHeave_{ 0.0f };
		float period_{ 4.0f };
		float roll_{ 0.0f }, pitch_{ 0.0f }, heave_{ 0.0f };
		RE::NiPoint3 posOff_{};
		Quat rotOff_{};
		float lastErr_{ 0.0f };
		int snaps_{ 0 };
		float logTimer_{ 0.0f };
		float zMin_{ 1e9f }, zMax_{ -1e9f };
	};

	// ------------------------------------------------------------------
	// Hooks
	// ------------------------------------------------------------------
	// W/S/A/D (or the left stick) and E, read raw so they still arrive while the game's movement is switched off.
	struct InputSink : public RE::BSTEventSink<RE::InputEvent*>
	{
		static InputSink* Get()
		{
			static InputSink s;
			return &s;
		}

		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			if (!a_events) return RE::BSEventNotifyControl::kContinue;
			auto* ui = RE::UI::GetSingleton();
			const bool blocked = ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME));
			auto* ue = RE::UserEvents::GetSingleton();
			auto& hull = TestHull::Get();
			for (auto* e = *a_events; e; e = e->next) {
				if (auto* btn = e->AsButtonEvent()) {
					const bool keyboard = btn->device == RE::INPUT_DEVICE::kKeyboard;
					const bool gamepad = btn->device == RE::INPUT_DEVICE::kGamepad;
					if (!keyboard && !gamepad) continue;
					const std::uint32_t id = btn->GetIDCode();
					const bool down = btn->IsPressed();
					const char* u = btn->QUserEvent().c_str();
					const std::string_view user = u ? std::string_view(u) : std::string_view{};
					auto is = [&](const RE::BSFixedString& a_event, std::uint32_t a_key) {
						if (!user.empty() && a_event.c_str() && user == a_event.c_str()) return true;
						return keyboard && id == a_key;
					};
					if (keyboard && !blocked) {
						Crew::NumKey(id, btn->IsPressed(), btn->IsDown());  // v1.6.0: oas num
						Gear::NumKey(id, btn->IsPressed(), btn->IsDown());  // v1.7.11: oas "num gear"
						Wake::NumKey(id, btn->IsPressed());                 // v1.7.43: oas "num stern"
					}
					if (!ue) continue;
					// v1.7.8: L3 (Sneak) and R3 (Toggle POV) held for the seated zoom
					if (gamepad && !user.empty() && ue->sneak.c_str() && user == ue->sneak.c_str()) Pad::g_l3 = down;
					if (gamepad && ((!user.empty() && ue->togglePOV.c_str() && user == ue->togglePOV.c_str()) ||
									   (user.empty() && id == RE::BSWin32GamepadDevice::Key::kRightThumb))) {
						if (down && !Pad::g_r3) {
							Pad::g_zoomedR3 = false;
							Pad::g_r3StartFirst = RE::PlayerCamera::GetSingleton() && RE::PlayerCamera::GetSingleton()->IsInFirstPerson();
						}
						Pad::g_r3 = down;
					}
					if (is(ue->forward, 0x11)) hull.OnMove(0, down && !blocked);
					if (is(ue->back, 0x1F)) hull.OnMove(1, down && !blocked);
					if (is(ue->strafeLeft, 0x1E)) hull.OnMove(2, down && !blocked);
					if (is(ue->strafeRight, 0x20)) hull.OnMove(3, down && !blocked);
					// v1.7.8: the gamepad button mapped to Activate counts even when it comes without its user event (seated)
					const bool act = is(ue->activate, 0x12) || (gamepad && Pad::IsActivate(id));
					if (act && btn->IsDown() && !blocked) hull.OnActivate();
				} else if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
					auto* t = static_cast<RE::ThumbstickEvent*>(e);
					if (t->IsLeft()) {
						Pad::g_leftY = t->yValue;
						// v1.7.8: R3 held + left stick zooms, the boat is not rowed meanwhile
						const bool zoom = Pad::R3Zooming();
						hull.OnStick(blocked || zoom ? 0.0f : t->xValue, blocked || zoom ? 0.0f : t->yValue);
					} else {
						Pad::g_rightY = t->yValue;
					}
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// Camera update: after the actors, right before rendering (OAS hooks the same slot).
	// v1.1.1: first person camera Update (TESCameraState slot 3): the late boat placement in first person.
	struct FirstPersonStateUpdateHook
	{
		static void Thunk(RE::FirstPersonState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_nextState)
		{
			Stage("first person camera: PinRower");
			TestHull::Get().PinRower();  // v1.2.3
			Stage("first person camera: game's original Update");
			func(a_this, a_nextState);
			Stage("first person camera: LatePlace");
			TestHull::Get().LatePlace("first person camera update");
			Stage("first person camera: lake boats");
			LakeBoats::LatePlace();  // v1.3.1
			Stage("first person camera: fog");
			Underwater::FogTend();   // v1.5.15: the underwater fog again, right before drawing
			Stage("after the first person camera update (the game draws)");
		}
		static inline REL::Relocation<decltype(Thunk)> func;
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_FirstPersonState[0] };
			func = vtbl.write_vfunc(3, Thunk);
		}
	};

	// ThirdPersonState::Update (slot 3 on SE/AE), before the game's own: the camera's animationRotation reset while rowing.
	struct ThirdPersonStateUpdateHook
	{
		static void Thunk(RE::ThirdPersonState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_nextState)
		{
			Stage("third person camera: CamFrame");
			TestHull::Get().CamFrame();  // v1.7.17
			Stage("third person camera: ResetAnimRotation before Update");
			TestHull::Get().ResetAnimRotation(a_this);
			Stage("third person camera: PinRower");
			TestHull::Get().PinRower();  // v1.2.3: before the game places the camera from his bones
			Stage("third person camera: MotionCamPre");
			TestHull::Get().MotionCamPre(a_this);   // v1.7.13
			Stage("third person camera: SeatGuard");
			TestHull::Get().SeatGuard(a_this);      // v1.7.18: before the game's camera update (it follows his 3D)
			Stage("third person camera: game's original Update");
			func(a_this, a_nextState);
			Stage("third person camera: MotionCamGame");
			TestHull::Get().MotionCamGame(a_this);  // v1.7.13
			Stage("third person camera: ResetAnimRotation after Update");
			TestHull::Get().ResetAnimRotation(a_this);  // OAS CamAfterStateUpdate: before and after the game's
			Stage("third person camera: CamTend");
			TestHull::Get().CamTend(a_this);            // v1.2.0
			Stage("third person camera: LatePlace");
			TestHull::Get().LatePlace("third person camera update");  // v1.1.1
			Stage("third person camera: lake boats");
			LakeBoats::LatePlace();  // v1.3.1
			Stage("third person camera: MotionB");
			TestHull::Get().MotionB(a_this);  // v1.7.13: motion log line
			Stage("third person camera: fog");
			Underwater::FogTend();   // v1.5.15: the underwater fog again, right before drawing
			Stage("after the camera update (the game draws)");
		}
		static inline REL::Relocation<decltype(Thunk)> func;
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ThirdPersonState[0] };
			func = vtbl.write_vfunc(3, Thunk);
		}
	};

	// v1.7.17: where the game takes the third person camera's place from (after the state update): CamOut steadies it
	struct ThirdPersonStateTranslationHook
	{
		static void Thunk(RE::ThirdPersonState* a_this, RE::NiPoint3& a_translation)
		{
			func(a_this, a_translation);
			TestHull::Get().CamOut(a_this, a_translation);
		}
		static inline REL::Relocation<decltype(Thunk)> func;
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ThirdPersonState[0] };
			func = vtbl.write_vfunc(REL::Module::IsVR() ? 6 : 5, Thunk);
		}
	};

	// Horse idle/breath sounds may be dispatched as footstep animation tags,
	// notably with USSEP's horse behavior fixes (AFKTrack #29343 / #34655).
	// Skip only the carrier in the native sound consumer, not all horses and not
	// the whole event dispatcher. Normal actors retain the previous hook chain.
	struct CarrierFootstepAudioHook
	{
		static RE::BSEventNotifyControl Thunk(RE::BGSImpactManager* a_this,
			const RE::BGSFootstepEvent* a_event, RE::BSTEventSource<RE::BGSFootstepEvent>* a_source)
		{
			if (a_event && CarrierAudio::id.load(std::memory_order_relaxed)) {
				auto actor = a_event->actor.get();
				if (CarrierAudio::IsCarrier(actor.get())) {
					if (CarrierAudio::footstepsLogged.fetch_add(1, std::memory_order_relaxed) < 24)
						logger::info("Carrier audio: BLOCKED footstep/idle tag '{}' actor {:08X}", a_event->tag.c_str(), actor->GetFormID());
					return RE::BSEventNotifyControl::kContinue;
				}
			}
			return func(a_this, a_event, a_source);
		}
		static inline REL::Relocation<decltype(Thunk)> func;
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_BGSImpactManager[0] };
			func = vtbl.write_vfunc(1, Thunk);
		}
	};

	// Vanilla SoundPlay annotations are separate from mapped footstep tags.
	// Actor/Character's third vtable is TESObjectREFR's animation-event sink; the incoming
	// this pointer is that sink subobject, so preserve it when calling the original.
	template <bool kCharacter>
	struct CarrierAnimationAudioHook
	{
		static RE::BSEventNotifyControl Thunk(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
			const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source)
		{
			if (a_event && CarrierAudio::IsCarrier(a_event->holder)) {
				const char* tag = a_event->tag.c_str();
				const bool block = CarrierAudio::SoundPlayTag(tag ? std::string_view(tag) : std::string_view{});
				if (CarrierAudio::animationsLogged.fetch_add(1, std::memory_order_relaxed) < 24)
					logger::info("Carrier audio: {} animation '{}' payload '{}' actor {:08X}",
						block ? "BLOCKED" : "observed", tag ? tag : "", a_event->payload.c_str(), a_event->holder->GetFormID());
				if (block) return RE::BSEventNotifyControl::kContinue;
			}
			return func(a_this, a_event, a_source);
		}
		static inline REL::Relocation<decltype(Thunk)> func;
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ kCharacter ? RE::VTABLE_Character[2] : RE::VTABLE_Actor[2] };
			func = vtbl.write_vfunc(1, Thunk);
		}
	};

	// v1.7.7: the hidden carrier horse never offers "Ride Horse" and can't be activated. The v1.4.3 block flags did not stop
	// the prompt (seen on land, the carrier under the beached boat). The horse's base (TESNPC) answers both through its
	// vtable: GetActivateText (4C) says nothing and Activate (37) refuses, for our carrier only.
	struct CarrierActivateHooks
	{
		static bool IsCarrier(const RE::TESObjectREFR* a_ref)
		{
			const RE::FormID id = TestHull::Get().CarrierId();
			return a_ref && id && a_ref->GetFormID() == id;
		}
		static bool PickedCarrier()
		{
			auto* pick = RE::CrosshairPickData::GetSingleton();
			if (!pick) return false;
			auto r = pick->GetActiveTarget().get();
			return r && IsCarrier(r.get());
		}
		static bool Text(RE::TESNPC* a_this, RE::TESObjectREFR* a_ref, RE::BSString& a_dst)
		{
			if (IsCarrier(a_ref) || PickedCarrier()) return false;
			if (TestHull::Get().Seated()) return false;  // v1.7.22: no [E] Talk while seated
			return text(a_this, a_ref, a_dst);
		}
		static bool Activate(RE::TESNPC* a_this, RE::TESObjectREFR* a_target, RE::TESObjectREFR* a_activator, std::uint8_t a_arg3, RE::TESBoundObject* a_object, std::int32_t a_count)
		{
			if (IsCarrier(a_target)) return false;
			// v1.7.22: the player talks to no one while seated or for a second after sitting down / standing up
			if (a_activator && a_activator == RE::PlayerCharacter::GetSingleton() && TestHull::Get().NoTalk()) {
				logger::info("Seat: talking to {:08X} held off (seated, or just sat down / stood up)", a_target ? a_target->GetFormID() : 0u);
				return false;
			}
			return act(a_this, a_target, a_activator, a_arg3, a_object, a_count);
		}
		static inline REL::Relocation<decltype(Text)> text;
		static inline REL::Relocation<decltype(Activate)> act;
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_TESNPC[0] };
			text = vtbl.write_vfunc(0x4C, Text);
			act = vtbl.write_vfunc(0x37, Activate);
		}
	};

	// v1.7.9: E on one of our prompt activators (Board / Steer / Row): their Activate (TESObjectACTI vtable 37) does it
	struct PickActivateHook
	{
		// v1.7.12: the bedroll spot reads "Unroll Bedroll" or, with the bedroll laid out, "Pack Up Bedroll" (ACTI vtable 4C)
		static bool Text(RE::TESObjectACTI* a_this, RE::TESObjectREFR* a_ref, RE::BSString& a_dst)
		{
			const auto fishing = BoatFishing::Read();
			if (BoatFishing::IsBoatReel(a_ref, fishing)) {
				if (BoatFishing::CanReel(fishing)) return text(a_this, a_ref, a_dst);
				if (!TestHull::Get().CanStartBoatFishing()) { a_dst = ""; return false; }
				a_dst = "Fish\nFishing Supplies";
				return true;
			}
			const int kind = TestHull::Get().PickKindOf(a_ref);
			if (kind == TestHull::kPickBedroll) {
				a_dst = Gear::BedrollOut() ? "Pack Up\nBedroll" : "Unroll\nBedroll";
				return true;
			}
			if (kind == TestHull::kPickSupplies) {
				if (!TestHull::Get().CanStartBoatFishing()) { a_dst = ""; return false; }
				a_dst = "Fish\nFishing Supplies"; return true;
			}
			if (Gear::IsSupplies(a_ref)) {  // v1.7.30: the supplies themselves: the same prompt, only close and standing
				if (!TestHull::Get().CanStartBoatFishing()) { a_dst = ""; return false; }
				a_dst = "Fish\nFishing Supplies";
				return true;
			}
			return text(a_this, a_ref, a_dst);
		}
		static inline REL::Relocation<decltype(Text)> text;
		static bool Activate(RE::TESObjectACTI* a_this, RE::TESObjectREFR* a_target, RE::TESObjectREFR* a_activator, std::uint8_t a_arg3, RE::TESBoundObject* a_object, std::int32_t a_count)
		{
			if (a_activator && a_activator == RE::PlayerCharacter::GetSingleton()) {
				const auto fishing = BoatFishing::Read();
				if (BoatFishing::IsBoatReel(a_target, fishing)) {
					if (BoatFishing::CanReel(fishing)) return act(a_this, a_target, a_activator, a_arg3, a_object, a_count);
					// CC normally restarts from its oversized invisible ReelLineRef
					// while idle. Route it through our look-down and idle checks.
					if (TestHull::Get().CanStartBoatFishing()) TestHull::Get().OnPick(TestHull::kPickSupplies);
					return true;
				}
			}
			if (a_activator && a_activator == RE::PlayerCharacter::GetSingleton()) Gear::OnUsed(a_target);  // v1.7.22
			// v1.7.30: E straight on the supplies (the crosshair on them, not on their prompt) started the fishing with no
			// marker: turned to the floor, no cast, stuck. It goes our way (marker, open water check) or nowhere.
			if (Gear::IsSupplies(a_target) && !Gear::g_ourUse && a_activator && a_activator == RE::PlayerCharacter::GetSingleton()) {
				if (TestHull::Get().NearSupplies()) TestHull::Get().OnPick(TestHull::kPickSupplies);
				else logger::info("Gear: the supplies used from too far or seated: refused");
				return true;
			}
			const int kind = TestHull::Get().PickKindOf(a_target);
			if (kind >= 0) {
				if (a_activator && a_activator == RE::PlayerCharacter::GetSingleton()) TestHull::Get().OnPick(kind);
				return true;
			}
			return act(a_this, a_target, a_activator, a_arg3, a_object, a_count);
		}
		static inline REL::Relocation<decltype(Activate)> act;
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_TESObjectACTI[0] };
			act = vtbl.write_vfunc(0x37, Activate);
			text = vtbl.write_vfunc(0x4C, Text);  // v1.7.12
		}
	};

	// v1.7.42: the map opened while seated: stood up first (fast travel from the seat left you frozen, as a save did)
	struct MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
		static MenuSink* Get() { static MenuSink s; return &s; }
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_e, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_e && a_e->opening &&
				(a_e->menuName == RE::StatsMenu::MENU_NAME || a_e->menuName == RE::LevelUpMenu::MENU_NAME)) {
				const auto fishing = BoatFishing::Read();
				if (fishing.ours && fishing.state > 0) BoatFishing::quitAfterMenu = true;
			}
			if (a_e && a_e->opening && a_e->menuName == RE::MapMenu::MENU_NAME) {
				SKSE::GetTaskInterface()->AddTask([]() { TestHull::Get().StandFor("the map opened"); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	struct PlayerUpdateHook
	{
		static void Thunk(RE::PlayerCharacter* a_this, float a_delta)
		{
			Stage("game player update");
			func(a_this, a_delta);
			g_beat.fetch_add(1, std::memory_order_relaxed);
			if (std::isfinite(a_delta) && a_delta > 0.0f)
				BoatFishing::startHold = (std::max)(0.0f, BoatFishing::startHold - a_delta);
			Stage("boat fishing: exit after level-up menu");
			BoatFishing::TendMenuExit();
			Stage("boat fishing: finish catch session");
			BoatFishing::TendFinishedAttempt();
			Stage("TestHull::Update");
			TestHull::Get().Update(a_delta);
			Stage("LakeBoats::Update");
			LakeBoats::Update(a_delta);  // v1.3.0
			{
				Stage("Crew::Tick");
				RE::NiPoint3 at{};
				const bool hb = TestHull::Get().BoatAt(at);
				Crew::Tick(a_delta, hb, at);  // v1.6.0: summons, letting go after a load, the numpad
			}
			Stage("Underwater::Tend");
			Underwater::Tend(a_delta);   // v1.5.14: our own underwater look for the camera
			CellGrid::Tick();            // v1.7.20
			Crew::LingerSafety();        // v1.7.28
			Stage("after the player update (the game's own frame)");
		}
		static inline REL::Relocation<decltype(Thunk)> func;
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
			func = vtbl.write_vfunc(REL::Module::IsVR() ? 0xAF : 0xAD, Thunk);
		}
	};

	// ------------------------------------------------------------------
	// Console: rr
	// ------------------------------------------------------------------
	// v1.7.8: the player commands first (oas help), the test / tuning ones after (oas dev)
	constexpr const char* kHelpPlayer =
		"oas boat (a boat ahead of you) | unboat (remove it) | bring (under you) | sit (or E) | paint<blue|beige|black|red|white|bluedesign|vanilla> | "
		"\"height N\" | \"row speed 300\" | \"row drag 0.07\" | \"row turn 0.45\" | \"dock add <name>|del|list|on|off|reload\" | "
		"\"lakeboats on|off|0-10\" | \"crew add|remove|clear|list|summon\" | num (crew seat numpad) | \"plankton on|off|night|auto\" | "
		"\"underwater on|off\" | \"lamp on|off\" | \"num gear\" (place the bedroll / fishing supplies) | \"bedroll roll|unroll\" | status | \"reset settings\" | dev (test commands). Look at the hull / stern seat / middle thwart for [E] Board / Steer / Row. Full sheet: TUNE_SHEET.txt";
	constexpr const char* kHelp =
		"oas spawn | bring | delete | info | sit (or E: the nearer of the oars / helm) | \"rowseat here|x y z\" | \"seat here|x y z\" | \"row speed 300|drag 0.07|shift 15\" | \"oar roll 90|lock x y z|stow x y z turn|rest 30 12|lock on|off\" | \"bob 1\" | \"lamp on|off\" | lakes | \"lakeboats on|off|now|0-10|view 1-10\" | \"lakehere name\" | ripples | \"plankton on|off|night|auto|density 3|size 0.5|open 1800|opensize 1|shore 400\" | \"underwater on|off\" | \"crew add|remove|clear|list|summon\" | \"num [off|reset]\" | bow | \"rock 6\" | calm | \"period 4\" | \"go 150\" | stop | \"turn 20\" | up | down | drive | warp | freeze | \"node on|off\" | \"layer anim|static\" | flip | watch";

	float NumArg(const std::vector<std::string>& a_w, float a_def)
	{
		if (a_w.size() < 2) return a_def;
		try { return std::stof(a_w[1]); } catch (...) { return a_def; }
	}

	void RunRr(const std::string& a_arg)
	{
		std::string lower = a_arg;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		// v1.3.0: the lake boat commands (rr lakes, lakeboats ..., lakehere <name>, lakepoint <name>, lakedel <name>, ripples)
		if (lower.rfind("lake", 0) == 0 || lower == "ripples") {
			const std::string r = LakeBoats::Command(lower);
			if (!r.empty()) { Print("RowRim: " + r); return; }
		}
		// v1.6.0: rr crew add|remove|clear|list|summon, rr num [off|reset]
		if (lower.rfind("crew", 0) == 0) {
			RE::NiPoint3 at{};
			const bool hb = TestHull::Get().BoatAt(at);
			Crew::Command(lower, hb, at);
			return;
		}
		if (lower.rfind("num", 0) == 0) {
			std::string arg = lower.substr(3);
			arg.erase(std::remove(arg.begin(), arg.end(), ' '), arg.end());
			// v1.7.43: oas "num stern" (the tiller's ripples), v1.7.44: oas "num hull" (the hull's); the other modes off meanwhile
			if (arg == "stern" || arg == "tiller" || arg == "hull") {
				const int mode = arg == "hull" ? 2 : 1;
				if (Wake::g_mode == mode) { Print("RowRim: " + Wake::NumCommand(0)); return; }
				Crew::NumCommand("off");
				if (Gear::NumOn()) Gear::NumCommand(false);
				Print("RowRim: " + Wake::NumCommand(mode));
				return;
			}
			if (Wake::NumOn()) Wake::NumCommand(0);
			// v1.7.11: oas "num gear": the numpad places the fishing supplies and the bedroll (the crew tuning off meanwhile)
			if (arg == "gear" || arg == "items" || arg == "bedroll" || arg == "supplies") {
				Crew::NumCommand("off");
				Print("RowRim: " + Gear::NumCommand(true));
				return;
			}
			if (arg == "off" && Gear::NumOn()) {
				Print("RowRim: " + Gear::NumCommand(false));
				Print("RowRim: " + Crew::NumCommand("off"));
				return;
			}
			if (arg.empty() && Gear::NumOn()) { Print("RowRim: " + Gear::NumCommand(false)); return; }
			if (Gear::NumOn()) Gear::NumCommand(false);
			// v1.7.8: the OAS numpad modes: crew / dog are the seat tuning; the others are console values here
			if (arg == "crew" || arg == "dog" || arg == "on") arg.clear();
			for (const char* old : { "speed", "height", "floor", "ripple", "stow", "npc", "rower", "oars", "helm" }) {
				if (arg == old) {
					Print("RowRim: the numpad tunes crew seats only (oas num). Rowing feel: oas \"row speed 300\", \"row drag 0.07\", \"row turn 0.45\"; "
						  "boat height: oas \"height N\"");
					return;
				}
			}
			Print("RowRim: " + Crew::NumCommand(arg));
			return;
		}
		// v1.5.14: rr underwater on|off (our camera underwater filter)
		if (lower.rfind("underwater", 0) == 0) {
			std::string arg = lower.size() > 10 ? lower.substr(10) : "";
			arg.erase(0, arg.find_first_not_of(' '));
			Print("RowRim: " + Underwater::Command(arg));
			return;
		}
		// v1.5.0: rr plankton on|off|night|auto|never, rr "plankton density N", rr "plankton size N"
		if (lower.rfind("plankton", 0) == 0) {
			std::vector<std::string> pw;
			std::istringstream ps(lower);
			for (std::string s; ps >> s;) pw.push_back(s);
			if (pw.size() == 1 && pw[0].size() > 8) pw = { "plankton", pw[0].substr(8) };  // the console glues unquoted words
			Print("RowRim: " + Plankton::Command(pw));
			return;
		}
		std::vector<std::string> w;
		std::istringstream ss(lower);
		for (std::string s; ss >> s;) w.push_back(s);
		// the console glues unquoted words together (rr paint blue arrives as "paintblue"): peel the known command off
		if (w.size() == 1) {
			for (const char* pre : { "boatheight", "height", "paint", "rowseat", "seat", "rock", "period", "go", "turn", "bob", "draw", "lead" }) {
				const std::string_view pv(pre);
				if (w[0].size() > pv.size() && w[0].rfind(pre, 0) == 0) {
					const std::string rest = w[0].substr(pv.size());
					w = { std::string(pre == std::string_view("boatheight") ? "height" : pre), rest };
					break;
				}
			}
		}
		auto& hull = TestHull::Get();
		if (w.empty() || w[0] == "help") { Print(std::string("RowRim (Oars and Sails) ") + kVersionShort + ": " + kHelpPlayer); return; }
		if (w[0] == "dev") { Print(std::string("RowRim test commands: ") + kHelp); return; }
		const std::string& c = w[0];
		if (c == "status") hull.Status();
		else if (c.rfind("bedroll", 0) == 0) Print("RowRim: " + Gear::BedrollCommand(c.size() > 7 ? c.substr(7) : (w.size() >= 2 ? w[1] : std::string{})));  // v1.7.11
		else if (c == "reset" || c == "resetsettings") {
			// v1.7.8 (OAS 56.45): rowing speed, glide and turning, boat height and paint, docks on, 3 NPC boats a lake
			hull.ResetSettings();
			LakeBoats::Command("lakeboats on");
			LakeBoats::Command("lakeboats 3");
			Print("RowRim: settings back to the defaults (row speed 300, glide 0.07, turning 0.45, height 0, blue, docks on, 3 boats a lake)");
		}
		else if (c == "dock" || c.rfind("dock", 0) == 0) {
			// v1.7.8: oas "dock add <name>" (also glued: dockadd, docklist ...)
			std::string args = lower.substr(lower.find("dock") + 4);
			for (const char* v : { "add", "del", "delete", "remove", "list", "on", "off", "reload" }) {
				if (args.rfind(v, 0) == 0 && (args.size() == std::strlen(v) || args[std::strlen(v)] != ' ')) {
					args = std::string(v) + " " + args.substr(std::strlen(v));
					break;
				}
			}
			Print("RowRim: " + hull.DockCommand(args));
		}
		else if (c == "spawn" || (c == "boat" && w.size() < 2)) hull.Spawn(false);
		else if (c == "bring") hull.Spawn(true);
		else if (c == "delete" || c == "unboat") hull.Delete(true);
		else if (c == "paint") hull.SetPaint(w.size() >= 2 ? w[1] : std::string{});
		else if (c == "height" || (c == "boat" && w.size() >= 3 && w[1] == "height")) {
			const std::string& n = c == "height" ? (w.size() >= 2 ? w[1] : std::string{}) : w[2];
			try { hull.SetHeightOffset(std::clamp(std::stof(n), -30.0f, 30.0f)); } catch (...) { Print("RowRim: oas \"height N\" (-30..30)"); }
		}
		else if (c == "info") hull.Info();
		else if (c == "rock") hull.SetRock(NumArg(w, 6.0f));
		else if (c == "calm") hull.SetRock(0.0f);
		else if (c == "period") hull.SetPeriod(NumArg(w, 4.0f));
		else if (c == "go") { hull.StopHelm(); hull.SetSpeed(NumArg(w, 150.0f)); }
		else if (c == "stop") { hull.StopHelm(); hull.SetSpeed(0.0f); hull.SetTurn(0.0f); }
		else if (c == "turn") { hull.StopHelm(); hull.SetTurn(NumArg(w, 20.0f)); }
		else if (c == "up") hull.Nudge(NumArg(w, 10.0f));
		else if (c == "down") hull.Nudge(-NumArg(w, 10.0f));
		else if (c == "drive") hull.SetMode(Mode::kDrive);
		else if (c == "warp") hull.SetMode(Mode::kWarp);
		else if (c == "freeze") hull.SetMode(Mode::kFrozen);
		else if (c == "node") hull.SetNode(w.size() < 2 || w[1] != "off");
		else if (c == "layer") hull.SetLayer(w.size() >= 2 && w[1] == "anim" ? RE::COL_LAYER::kAnimStatic : RE::COL_LAYER::kStatic);
		else if (c == "flip") hull.Flip();
		else if (c == "watch") hull.Watch();
		else if (c == "sit") hull.ToggleSeat();
		else if (c == "bow") hull.BowFlip();
		else if (c == "persist") hull.SetPersist(w.size() < 2 || w[1] != "off");
		else if (c == "hop") hull.SetHop(w.size() < 2 || w[1] != "off");
		else if (c == "graft") hull.SetRegraft(w.size() < 2 || w[1] != "off");
		else if (c == "late") hull.SetLate(w.size() < 2 || w[1] != "off");
		else if (c == "nudge") hull.SetNudge(w.size() < 2 || w[1] != "off");
		else if (c == "wb") {
			if (w.size() >= 3 && w[1] == "z") { try { hull.SetWaterBlockZ(std::stof(w[2])); } catch (...) { Print("RowRim: oas \"wb z 4\""); } }
			else if (w.size() >= 2 && w[1] == "floor") hull.SetFloor(w.size() < 3 || w[2] != "off");
			else hull.SetWaterBlock(w.size() >= 2 ? w[1] : "off");
		}
		else if (c == "carrier") {
			if (w.size() >= 3 && w[1] == "ai") hull.SetCarrierAI(w[2] != "off");
			else hull.SetCarrier(w.size() < 2 || w[1] != "off");
		}
		else if (c == "bob") hull.SetIdleRock(NumArg(w, 1.0f));
		else if (c == "lead") hull.SetLead(NumArg(w, 0.7f));  // v1.6.6
		else if (c == "grid") {  // v1.7.20
			const bool on = w.size() >= 2 ? w[1] != "off" : !CellGrid::On();
			CellGrid::Set(on);
			Print(std::format("RowRim: cell grid {}", on ? "on (walls on the world cell edges round you)" : "off"));
		}
		else if (c == "seatguard") hull.SetSeatGuard(w.size() < 2 || w[1] != "off");  // v1.7.14
		else if (c == "motionlog") hull.MotionLogStart(w.size() >= 2 && w[1] == "off" ? 0.0f : NumArg(w, 30.0f));  // v1.7.13
		else if (c == "draw") hull.SetDrawMode(w.size() >= 2 ? w[1] : std::string("pin"));
		else if (c == "rowseat") {
			if (w.size() >= 2 && w[1] == "here") hull.RowSeatHere();
			else if (w.size() >= 4) {
				try { hull.SetRowSeat(std::stof(w[1]), std::stof(w[2]), std::stof(w[3])); } catch (...) { Print("RowRim: oas \"rowseat x y z\" or oas \"rowseat here\""); }
			} else Print("RowRim: oas \"rowseat here\" or oas \"rowseat x y z\" (model space: x right, y toward the bow, z up)");
		}
		else if (c == "row") {
			try {
				if (w.size() >= 3 && w[1] == "speed") hull.SetRowSpeed(std::stof(w[2]));
				else if (w.size() >= 3 && w[1] == "drag") hull.SetRowDrag(std::stof(w[2]));
				else if (w.size() >= 3 && w[1] == "shift") hull.SetRowShift(std::stof(w[2]));
				else if (w.size() >= 3 && w[1] == "turn") hull.SetRowTurn(std::stof(w[2]));  // v1.7.8
				else Print("RowRim: oas \"row speed 300\" | \"row drag 0.07\" | \"row shift 15\" (rower toward the bow while rowing)");
			} catch (...) { Print("RowRim: oas \"row speed 300\" or oas \"row drag 0.07\""); }
		}
		else if (c == "lamp" || c == "lantern") {
			hull.SetLamp(w.size() < 2 ? true : w[1] != "off");
		}
		else if (c == "oar") {
			try {
				if (w.size() >= 3 && w[1] == "roll") hull.SetOarRoll(std::stof(w[2]));
				else if (w.size() >= 3 && w[1] == "lock" && (w[2] == "on" || w[2] == "off")) hull.SetHandLock(w[2] == "on");
				else if (w.size() >= 5 && w[1] == "lock") hull.SetOarLock(std::stof(w[2]), std::stof(w[3]), std::stof(w[4]));
				else if (w.size() >= 6 && w[1] == "stow") hull.SetOarStow(std::stof(w[2]), std::stof(w[3]), std::stof(w[4]), std::stof(w[5]));
				else if (w.size() >= 4 && w[1] == "rest") hull.SetOarRest(std::stof(w[2]) / 100.0f, std::stof(w[3]));
				else Print("RowRim: oas \"oar roll 90\" | \"oar lock 93.3 6.7 76.5\" | \"oar lock on|off\" | \"oar stow 18 59 67.5 7.9\" (x y z turn) | \"oar rest 30 12\" (% inboard, blade down deg)");
			} catch (...) { Print("RowRim: oas \"oar roll 90\" | \"oar lock x y z\" | \"oar stow x y z turn\""); }
		}
		else if (c == "seat") {
			if (w.size() >= 2 && w[1] == "here") hull.SeatHere();
			else if (w.size() >= 4) {
				try { hull.SetSeat(std::stof(w[1]), std::stof(w[2]), std::stof(w[3])); } catch (...) { Print("RowRim: oas \"seat x y z\" or oas \"seat here\""); }
			} else Print("RowRim: oas \"seat here\" (stand where the seat should be) or oas \"seat x y z\"");
		}
		else Print(std::string("RowRim: ") + kHelp);
	}

	bool ExecuteRr(const RE::SCRIPT_PARAMETER*, RE::SCRIPT_FUNCTION::ScriptData* a_scriptData, RE::TESObjectREFR*, RE::TESObjectREFR*, RE::Script*,
		RE::ScriptLocals*, double&, std::uint32_t&)
	{
		std::string arg;
		if (a_scriptData && a_scriptData->numParams > 0) {
			if (auto* chunk = a_scriptData->GetStringChunk()) arg = chunk->GetString();
		}
		RunRr(arg);
		return true;
	}

	static RE::SCRIPT_PARAMETER kRrParams[] = {
		{ "arg", RE::SCRIPT_PARAM_TYPE::kChar, true }
	};

	void RegisterConsoleCommand()
	{
		// Unused dev commands; the first free ones are taken. v1.7.8: the command is oas again (Oars and Sails players know
		// it), rr kept as a second name. With the old OarsAndSails.dll still loaded it owns oas, so RowRim is rr only.
		std::vector<std::pair<const char*, const char*>> want;
		if (!Legacy::g_oasLoaded) want.emplace_back("OarsAndSails", "oas");
		want.emplace_back("RowRim", "rr");
		std::size_t next = 0;
		for (const char* name : { "TestLocalMap", "TestSeenData", "ShowRenderPasses", "DumpNiUpdates" }) {
			if (next >= want.size()) break;
			auto* cmd = RE::SCRIPT_FUNCTION::LocateConsoleCommand(name);
			if (!cmd) continue;
			cmd->functionName = want[next].first;
			cmd->shortName = want[next].second;
			cmd->helpString = "oas [boat|unboat|sit|paint|dock|lakeboats|plankton|crew|help|...]: RowRim (Oars and Sails)";
			cmd->referenceFunction = false;
			cmd->SetParameters(kRrParams);
			cmd->executeFunction = &ExecuteRr;
			cmd->editorFilter = false;
			logger::info("Registered '{}' / '{}' (replaced '{}')", want[next].first, want[next].second, name);
			++next;
		}
		if (next == 0) logger::error("No free console command to hijack; oas / rr not registered");
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			StartWatchdog();  // v1.6.6
			Legacy::CheckConflict();  // v1.7.8: OarsAndSails.dll still installed?
			PlayerUpdateHook::Install();
			CarrierActivateHooks::Install();  // v1.7.7
			CarrierFootstepAudioHook::Install();
			CarrierAnimationAudioHook<false>::Install();
			CarrierAnimationAudioHook<true>::Install();
			logger::info("Carrier audio: v1 native footstep/idle and SoundPlay event gates installed (carrier only, depth 220)");
			PickActivateHook::Install();      // v1.7.9
			FirstPersonStateUpdateHook::Install();
			ThirdPersonStateUpdateHook::Install();
			RegisterConsoleCommand();
			if (auto* mgr = RE::BSInputDeviceManager::GetSingleton()) mgr->AddEventSink(InputSink::Get());
			if (auto* ui = RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::Get());  // v1.7.42
			logger::info("RowRim ready ({})", kVersion);
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			TestHull::Get().Forget();  // then the co-save load (if any) hands the saved hull and carrier back
			LakeBoats::OnGameLoading();
			Plankton::Flow::Clear();  // v1.7.8
			break;
		case SKSE::MessagingInterface::kNewGame:
			TestHull::Get().Forget();
			LakeBoats::OnGameLoading();
			Plankton::Flow::Clear();
			Legacy::OnGameLoaded();  // v1.7.8
			break;
		case SKSE::MessagingInterface::kSaveGame:
			TestHull::Get().StandFor("saving the game");  // v1.7.42
			TestHull::Get().UntameAll("saving the game");  // v1.7.52: never saved unaggressive (calmed again half a second later)
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			Legacy::OnGameLoaded();  // v1.7.8: the old-boat sweep starts a few seconds in; the OarsAndSails.dll warning
			break;
		default:
			break;
		}
	}

	// ---- co-save: the player's hull (HULL), the lake boats (LAKE: lakes and populations as text; LKSP: their refs, removed
	// after the next load, as OAS did) ----
	constexpr std::uint32_t kRecLake = 'LAKE';
	constexpr std::uint32_t kRecLakeSpawn = 'LKSP';

	void SaveAll(SKSE::SerializationInterface* a_i)
	{
		TestHull::Get().Save(a_i);
		Crew::Save(a_i);  // v1.6.0
		Gear::Save(a_i);  // v1.7.0
		const std::string lakes = LakeBoats::SaveState();
		if (a_i->OpenRecord(kRecLake, 1)) {
			const auto n = static_cast<std::uint32_t>(lakes.size());
			a_i->WriteRecordData(n);
			if (n) a_i->WriteRecordData(lakes.data(), n);
		}
		const auto spawned = LakeBoats::SpawnedNow();
		if (a_i->OpenRecord(kRecLakeSpawn, 2)) {  // v1.6.4: version 2, each ref with its base
			const auto n = static_cast<std::uint32_t>(spawned.size());
			a_i->WriteRecordData(n);
			for (const auto& [id, base] : spawned) {
				a_i->WriteRecordData(id);
				a_i->WriteRecordData(base);
			}
		}
		logger::info("Co-save: {} lake text bytes, {} lake boat ref(s) saved", lakes.size(), spawned.size());
	}

	void LoadAll(SKSE::SerializationInterface* a_i)
	{
		std::uint32_t type = 0, version = 0, length = 0;
		while (a_i->GetNextRecordInfo(type, version, length)) {
			if (type == kRecLake) {
				std::uint32_t n = 0;
				a_i->ReadRecordData(n);
				std::string text(n, '\0');
				if (n) a_i->ReadRecordData(text.data(), n);
				LakeBoats::LoadState(text, [a_i](RE::FormID a_id) {
					RE::FormID out = 0;
					return a_i->ResolveFormID(a_id, out) ? out : RE::FormID{ 0 };
				});
			} else if (type == kRecLakeSpawn) {
				std::uint32_t n = 0;
				a_i->ReadRecordData(n);
				std::vector<std::pair<RE::FormID, RE::FormID>> ids;
				for (std::uint32_t k = 0; k < n; ++k) {
					RE::FormID id = 0, out = 0, base = 0, baseOut = 0;
					a_i->ReadRecordData(id);
					if (version >= 2) {
						a_i->ReadRecordData(base);
						if (!a_i->ResolveFormID(base, baseOut)) baseOut = 0;
					}
					if (a_i->ResolveFormID(id, out)) ids.emplace_back(out, baseOut);
				}
				LakeBoats::PurgeAfterLoad(std::move(ids));
			} else if (Gear::LoadRecord(a_i, type, version)) {
				// v1.7.0
			} else if (Crew::LoadRecord(a_i, type)) {
				// v1.6.0
			} else {
				TestHull::Get().LoadRecord(a_i, type, version);
			}
		}
	}

	void SetupLog()
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) return;
		auto path = *dir / "RowRim.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto log = std::make_shared<spdlog::logger>("RowRim", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] %v");
	}
}

// v1.7.51: two copies of RowRim at once (the test build's RowRim.dll and the release layout's OarsAndSails.dll, both
// enabled): both ran, every hook and effect twice (the log doubled, the underwater view went black). The RowRim.dll copy
// stands down when an OarsAndSails.dll that is RowRim (its SKSE name) is loaded too.
static bool OtherRowRimLoaded()
{
	HMODULE self = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&OtherRowRimLoaded), &self);
	char path[MAX_PATH]{};
	GetModuleFileNameA(self, path, MAX_PATH);
	std::string me = path;
	me = me.substr(me.find_last_of("\\/") + 1);
	std::transform(me.begin(), me.end(), me.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (me != "rowrim.dll") return false;  // the release copy always runs
	const HMODULE oas = GetModuleHandleA("OarsAndSails.dll");
	if (!oas || oas == self) return false;
	const auto* data = reinterpret_cast<const SKSE::PluginVersionData*>(GetProcAddress(oas, "SKSEPlugin_Version"));
	if (!data) return false;
	const char* name = reinterpret_cast<const char*>(data) + 8;  // dataVersion, pluginVersion, then pluginName[256]
	return std::strncmp(name, "RowRim", 7) == 0;
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	if (OtherRowRimLoaded()) {
		SKSE::log::warn("RowRim.dll: OarsAndSails.dll is RowRim too (the release copy): this copy does nothing. Enable only one of them.");
		return true;
	}
	SetupLog();
	logger::info("RowRim {} loaded", kVersion);
	if (auto* ser = SKSE::GetSerializationInterface()) {
		ser->SetUniqueID('RRIM');
		ser->SetSaveCallback([](SKSE::SerializationInterface* a_i) { SaveAll(a_i); });
		ser->SetLoadCallback([](SKSE::SerializationInterface* a_i) { LoadAll(a_i); });
		ser->SetRevertCallback([](SKSE::SerializationInterface*) {
			Crew::Forget();  // v1.6.0: first, so the stand-up below touches no passenger
			Gear::Forget();  // v1.7.0
			TestHull::Get().Forget();
			LakeBoats::OnGameLoading();
		});
	}
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}

#undef GetPrivateProfileStringA
#undef GetPrivateProfileIntA
