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
#include <cstdio>

#include <xmmintrin.h>
#include <atomic>
#include "SKSEMenuFramework.h"  // v1.7.86: the settings page (MenuUi.inl); LGPL API header, see SKSEMenuFramework_LICENSE.txt

// Defaults baked from the supplied RowRim.ini. Keep this before the .inl includes:
// their existing Win32 INI readers use these fallbacks too. A real INI value always
// wins; writes and in-game tuning are unchanged. Other INI files are unaffected.
namespace RowRimBakedIni
{
	struct Entry { const char* section; const char* key; const char* value; };
	// v1.8.32c: Miguel's settings from the game (RowRim.ini, 8 Nov) are the defaults. Not baked: the boat in use, its sail
	// state, the debug grid, and [Sail] Enabled (sailing stays off in a release until said otherwise). A boat's own section
	// ([SailBoat.Sea], [Longship.Wake] ...) is looked up as itself first, then as the shared one.
	inline constexpr Entry kDefaults[] = {
		{ "Longboat", "Handling", "2" },
		{ "Boat", "BakedTuneReset", "68" },
		{ "Boat", "Carry", "1" },
		{ "Boat", "CarryJump", "0" },
		{ "Boat", "SailModel", "sailskin" },
		{ "Rower", "PlayerRight", "-2" },
		{ "Rower", "PlayerForward", "247" },
		{ "Rower", "PlayerUp", "22" },
		{ "Rower", "PlayerYaw", "180" },
		{ "LakeBoats", "On", "1" },
		{ "LakeBoats", "Count", "3" },
		{ "LakeBoats", "View", "5" },
		{ "LakeBoats", "Defaults", "2" },
		{ "Plankton", "On", "1" },  // v1.8.90: on by default, at night only (Force 0)
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
		{ "Row", "MaxSpeed", "265" },
		{ "Row", "Glide", "0.151" },
		{ "Row", "Turn", "0.67" },
		{ "Row", "TurnLag", "2.50" },
		{ "Clutter", "Bedroll", "1" },  // v1.8.53
		{ "Clutter", "BedrollX", "1.5" },
		{ "Clutter", "BedrollY", "71.1" },
		{ "Clutter", "BedrollZ", "41.3" },
		{ "Clutter", "BedrollYaw", "-90.0" },
		{ "Camera", "BoatCam", "0" },
		{ "Crew", "StandUpAt", "388" },
		{ "Wake", "HullSize", "0.00" },
		{ "Wake", "HullOut", "34.6" },
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
		{ "Sail", "Speed", "0.80" },
		{ "Sail", "Flap", "1.62" },
		{ "Sail", "Rock", "1.56" },
		{ "Sail", "Heel", "0.96" },
		{ "Sail", "HeadWind", "0.71" },  // v1.8.53: Miguel's ini, 8 Nov evening
		{ "Sail", "Enabled", "1" },      // v1.8.53: the sailing boat ships
		{ "SailBoat", "MaxSpeed", "214" },
		{ "SailBoat", "Glide", "0.151" },
		{ "SailBoat", "Turn", "0.64" },
		{ "SailBoat", "TurnLag", "3.50" },
		{ "Longship", "MaxSpeed", "185" },
		{ "Longship", "Glide", "0.151" },
		{ "Longship", "Turn", "0.40" },
		{ "Longship", "TurnLag", "3.60" },
		{ "LongshipSail", "Speed", "1.00" },
		{ "LongshipSail", "Flap", "1.92" },
		{ "Sea", "Tilt", "1.010" },
		{ "Sea", "Follow", "0.880" },
		{ "Sea", "Softness", "0.200" },
		{ "Sea", "MaxTilt", "13.000" },
		{ "Sea", "Trim", "1.800" },
		{ "Longship.Sea", "Follow", "1.000" },
		{ "Wind", "Strength", "1.13" },
		{ "Wind", "Wander", "0.49" },
		{ "Wind", "Shifts", "1.02" },
		{ "Wind", "HeadWind", "0.70" },
		{ "Wind", "Streaks", "1.00" },  // v1.8.53
		{ "SailBoat.Wake", "HullOut", "31.1" },
		{ "Longship.Wake", "HullOut", "31.1" },
		{ "SailBoat.Sea", "Follow", "1.360" },
		{ "SailBoat.Sea", "Tilt", "1.500" },
		{ "SailBoat.Sea", "Softness", "0.200" },
		{ "SailBoat.Sea", "MaxTilt", "14.000" },
		{ "SailBoat.Sea", "Trim", "2.500" },
		{ "Longship.Sea", "Tilt", "1.500" },
		{ "Longship.Sea", "Softness", "0.200" },
		{ "Longship.Sea", "MaxTilt", "14.000" },
		{ "Longship.Sea", "Trim", "2.500" },
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
	// v1.8.26: each boat its own settings: the rowboat [Wake] / [CrewSeats] / [Sea], the sailing boat [SailBoat.Wake] ...,
	// the longship [Longship.Wake] ... (the baked defaults are the same for all three until a boat's own are tuned)
	inline int g_iniKind = 0;  // the boat in use: 0 rowboat, 1 sailing boat, 2 longship
	inline const char* BoatSec(int a_kind, const char* a_base)
	{
		if (a_kind <= 0 || a_kind > 2) return a_base;
		static std::mutex m;
		static std::unordered_map<std::string, std::string> names;  // node based: the strings never move
		std::lock_guard lk(m);
		const std::string key = std::to_string(a_kind) + a_base;
		auto it = names.find(key);
		if (it == names.end()) it = names.emplace(key, std::string(a_kind == 1 ? "SailBoat." : "Longship.") + a_base).first;
		return it->second.c_str();
	}
	inline const char* BoatSec(const char* a_base) { return BoatSec(g_iniKind, a_base); }
	inline const char* Find(const char* section, const char* key, const char* path)
	{
		if (!section || !key || !path) return nullptr; // Preserve Win32 enumeration.
		const char* name = path;
		for (const char* p = path; *p; ++p) if (*p == '/' || *p == '\\') name = p + 1;
		if (!EqualI(name, "RowRim.ini")) return nullptr;
		for (const auto& e : kDefaults)  // v1.8.32c: the boat's own section first
			if (EqualI(section, e.section) && EqualI(key, e.key)) return e.value;
		if (_strnicmp(section, "SailBoat.", 9) == 0 || _strnicmp(section, "Longship.", 9) == 0) {  // v1.8.26: then the shared one
			section += 9;
			for (const auto& e : kDefaults)
				if (EqualI(section, e.section) && EqualI(key, e.key)) return e.value;
		}
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
	constexpr const char* kVersionShort = "v1.8.90";
	constexpr const char* kVersion =
		"v1.8.90 (plankton: on by default, night only; the plankton of the sailing boat follow its own hull: wider than the rowboat's)\n"
		"v1.8.89 (menu: three buttons per boat: Spawn boat (puts it ahead of you and makes it the one in use), Use this boat (makes it the one in use, and it stays so until you sit in or board another boat), Dock here for good; oas spawn <boat>, oas use <boat>)\n"
		"v1.8.88 (menu: \"Dock here for good\" on each boat's page and the Boat tab: places the boat ahead of you and saves the spot as a shipped dock in RowRim_Docks.ini (Boat=row|sail), oas home perm <boat>)\n"
		"v1.8.87 (shipped docks for the sailing boat: a Dock entry with Boat=sail in RowRim_Docks.ini calls the sailing boat, as the others call the rowboat; oas boat sail, then oas dock add <name> where it should wait)\n"
		"v1.8.86 (rowing: the glide between strokes is back as it was before v1.8.81)\n"
		"v1.8.85 (standing up from the boat: the game's own camera pitch is kept level while ours runs and put level when it takes over, so it no longer sits under the water looking up at you; uncertain)\n"
		"v1.8.84 (the Queen Anne's Revenge test ship is taken out of the build: her boat, her settings and her page)\n"
		"v1.8.83 (menu: an Advanced page with the seated draw mode per boat, saved in [Advanced] DrawRow / DrawSail; automatic = the rowboat pinned, the others as before)\n"
		"v1.8.82 (the rowboat is drawn pinned: the boat at this frame's pose and the rower pinned to the seat, so the row clip cannot move him off the drawn boat; oas \"draw old\" is no longer the rowboat's default; uncertain, a soft rule)\n"
		"v1.8.81 (rowing: no glide between strokes, the boat's speed is steady instead of surging and dropping with every stroke; top speed and acceleration unchanged)\n"
		"v1.8.80 (ALL fishing removed from the rowboat: no Fishing Supplies, no Fish prompt or button, no fishing code; the Creation Club Fishing is no longer used at all; Fishing Supplies and markers older versions left in a save are swept away after a load)\n"
		"v1.8.79 (plankton also away from the boat by default, [Plankton] Everywhere=0 or the Effects page to keep it aboard only; cleanup: the free camera, the camera offsets and the old camera fixes taken out; the helm camera is back, with its own wheel and pad zoom (closest .. 60000, [HelmCam] MaxDist) and the look from the raw mouse / right stick, never the player controls' look input; log lines every 10 s instead of every half second; version list cut: older ones in the src briefs); "
		"v1.8.78 (the camera spin: the row and the tiller clips turn a bone the game's third person camera follows (animationRotation, toggleAnimCam): cut off every frame before and after the game's camera update on every boat seat, logged as \"AnimCam\"); "
		"v1.8.77 (the camera started again from the game's own third person camera: every camera fix and our helm camera and the free camera taken out; Camera page: X / Y / Z offset sliders (right / along the view / up) on a boat, [Camera] OffsetX / Y / Z); "
		"v1.8.76 (the helm camera is the free camera (the TFC state) from 2.5 s after you sit: ours every frame, mouse orbit and wheel zoom our own, boat input read raw; stands back to third person when you leave; [HelmCam] FreeCam=1 (off by default: it stopped the player update, the boat froze); zoom out to 60000 (ship 400..60000)); "
		"v1.8.74 (the game camera's target is never our hidden carrier: put back on the player (logged), for the spin-outs that came with the carriers' own races); "
		"v1.8.73 (the helm camera also on the rowboat (drawn the old way it had no boat model, so the game's camera stayed: under you, looking up as you sat)); "
		"v1.8.72 (the compass stays on the HUD while seated: movement is no longer switched off (that hid it), the walk is held instead); "
		"v1.8.71 (sitting and standing glide: the body slides onto the seat and turns to face its way over half a second, then sits; standing up rises and steps off over a third of a second, instead of one-frame snaps); "
		"v1.8.70 (the helm camera spin that lasted until the game was restarted: the game's orbit angle is kept to one turn (it was wound up to thousands of radians, and a number that large made the camera spin and jump)); "
		"v1.8.69d (the sailing boat comes to its dock spots in any save, also one where it was never out); "
		"v1.8.69c (the boat's Fishing Supplies and bedroll are persistent (a temporary ref was lost when you travelled and a new one placed, the old one left at the dock); the Fishing Supplies kept disabled except while you fish); "
		"v1.8.69 (never vanilla: the lake boats' hulls, lanterns and lights, your boat's lantern lights, the lake crew (our own commoners, templated on the game's) and the fishing marker are our own records in OarsAndSails.esl); "
		"v1.8.68 and earlier: see the briefs in src (one .md per change)";
	constexpr float kPi = 3.14159265f;
	constexpr RE::FormID kRowBoatStatic = 0x00050368;  // ShipRowBoat01 (Skyrim.esm), same base OAS reads
	// v1.8.8: the sailing boat's and the longship's own hulls (their own collision models), activators in OarsAndSails.esl
	constexpr RE::FormID kSailHullLocal = 0x804;  // OAS_SailBoatHull: RowRim\RowRim_SailBoatHull.nif
	constexpr RE::FormID kLongHullLocal = 0x805;  // OAS_LongshipHull: RowRim\RowRim_LongshipHull.nif
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
	// v1.8.31: a turn of a_a about the unit axis (a_x, a_y, a_z), anticlockwise looking down the axis (RzM(-a) about z)
	RE::NiMatrix3 AxisM(float a_x, float a_y, float a_z, float a_a);
	// v1.8.32: the turn taking a_from onto a_to (both any length), the shortest way
	RE::NiMatrix3 TurnOnto(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
	{
		const float lf = a_from.Length(), lt = a_to.Length();
		if (lf < 1e-6f || lt < 1e-6f) return MakeMat(1, 0, 0, 0, 1, 0, 0, 0, 1);
		const RE::NiPoint3 f = a_from / lf, t = a_to / lt;
		RE::NiPoint3 ax{ f.y * t.z - f.z * t.y, f.z * t.x - f.x * t.z, f.x * t.y - f.y * t.x };
		const float s = ax.Length(), c = f.x * t.x + f.y * t.y + f.z * t.z;
		if (s < 1e-6f) {
			if (c > 0.0f) return MakeMat(1, 0, 0, 0, 1, 0, 0, 0, 1);
			ax = std::abs(f.x) < 0.9f ? RE::NiPoint3{ 0.0f, -f.z, f.y } : RE::NiPoint3{ -f.z, 0.0f, f.x };  // half a turn about any square axis
			const float l = ax.Length(); return AxisM(ax.x / l, ax.y / l, ax.z / l, 3.14159265f);
		}
		return AxisM(ax.x / s, ax.y / s, ax.z / s, std::atan2(s, c));
	}
	RE::NiMatrix3 AxisM(float a_x, float a_y, float a_z, float a_a)
	{
		const float c = std::cos(a_a), s = std::sin(a_a), k = 1.0f - c;
		return MakeMat(c + k * a_x * a_x, k * a_x * a_y - s * a_z, k * a_x * a_z + s * a_y,
			k * a_y * a_x + s * a_z, c + k * a_y * a_y, k * a_y * a_z - s * a_x,
			k * a_z * a_x - s * a_y, k * a_z * a_y + s * a_x, c + k * a_z * a_z);
	}

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

	// v1.8.39: a turn part way from a_a to a_b (a_t 0..1), the short way round
	RE::NiMatrix3 SlerpM(const RE::NiMatrix3& a_a, const RE::NiMatrix3& a_b, float a_t)
	{
		const Quat qa = FromMatrix(a_a);
		Quat qb = FromMatrix(a_b);
		float d = Dot(qa, qb);
		if (d < 0.0f) { qb = Quat{ -qb.x, -qb.y, -qb.z, -qb.w }; d = -d; }
		const float t = std::clamp(a_t, 0.0f, 1.0f);
		float wa = 1.0f - t, wb = t;
		if (d < 0.9995f) {
			const float th = std::acos(std::clamp(d, -1.0f, 1.0f)), sn = std::sin(th);
			wa = std::sin((1.0f - t) * th) / sn; wb = std::sin(t * th) / sn;
		}
		return ToMatrix(Norm(Quat{ qa.x * wa + qb.x * wb, qa.y * wa + qb.y * wb, qa.z * wa + qb.z * wb, qa.w * wa + qb.w * wb }));
	}

	// v1.8.39b: as SlerpM, but turning at most a_max radians (a heavy thing's slow, steady turn)
	RE::NiMatrix3 SlerpCapM(const RE::NiMatrix3& a_a, const RE::NiMatrix3& a_b, float a_t, float a_max)
	{
		const float d = std::abs(Dot(FromMatrix(a_a), FromMatrix(a_b)));
		const float ang = 2.0f * std::acos(std::clamp(d, 0.0f, 1.0f));
		float t = std::clamp(a_t, 0.0f, 1.0f);
		if (ang * t > a_max && ang > 1e-5f) t = a_max / ang;
		return SlerpM(a_a, a_b, t);
	}
	float TurnBetween(const RE::NiMatrix3& a_a, const RE::NiMatrix3& a_b)
	{
		return 2.0f * std::acos(std::clamp(std::abs(Dot(FromMatrix(a_a), FromMatrix(a_b))), 0.0f, 1.0f));
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
	constexpr const char* kHelmStartEvent = "OarsHelmStart";  // v1.8.40: the sailing boat's helm clip (HelmTiller_Loop.hkx), ended as the row clip
	constexpr const char* kHelmLeftEvent = "OarsHelmLeft";    // v1.8.41: the tiller over to the left (steering right), HelmTiller_Left.hkx
	constexpr const char* kHelmRightEvent = "OarsHelmRight";  // v1.8.41: the tiller over to the right (steering left), HelmTiller_Right.hkx

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
#include "SeaLink.inl"  // v1.8.10: boats ride the OceanRim sea, see src\Sea_v1810_Waves.md
#include "Plankton.inl"
#include "Wake.inl"  // v1.5.16: the boat's own V wake, see src\Wake_v1516.md
#include "Grid.inl"  // v1.7.20: oas "grid on|off", the world cells shown

	// v1.3.0: the oar ripples and dig sound (Fx) and the OAS lake boats (LakeBoats), see src\Port7_LakeBoats.md
#include "LakeBoats.inl"

	// v1.6.0: crew and passengers (OAS crew add / boarding / late boarding / numpad seat tuning), see src\Crew_v160.md
#include "Crew.inl"
	// v1.7.0: the bedroll riding in the boat (v1.8.80: the fishing supplies are gone), see src\Mesh_Clutter.md
#include "Gear.inl"

	// Win32 GDI defines GetObject as GetObjectA/GetObjectW. That macro also
	// rewrites Papyrus Variable::GetObject(), causing the MSVC build to fail.
#ifdef GetObject
#undef GetObject
#endif

	// Audio event callbacks can run outside the player update. Publish only an
	// atomic form ID to them, not the boat's mutable state or scenegraph pointers.
	// v1.8.28: our own wind for the sails (src\Sail_v1828_OwnWind.md). The game's wind direction is no longer used, and
	// nothing of the game's wind is ever written: the direction is ours, always wandering (a slow drift, big shifts every
	// few minutes, gusts that veer it a little); the strength follows whatever weather the game or a weather mod has on
	// (the sky's wind speed, rain, snow), with gusts and lulls on top. One wind for all the boats.
	namespace OwnWind
	{
		constexpr float kTau = 6.2831853f;
		struct State
		{
			bool init = false;
			float dir = 0.0f;                       // radians, world: the way it blows toward
			float drift = 0.0f;                     // rad/s: the slow wander
			float shiftRate = 0.0f, shiftLeft = 0.0f, nextShift = 45.0f;  // a big shift under way, and the next one
			float base = 0.4f;                      // from the weather, eased (about 20 s)
			float gust = 0.0f, gustWant = 0.0f, gustHold = 0.0f, nextGust = 4.0f;
			float veer = 0.0f, veerWant = 0.0f;     // a gust's own veer
			float str = 0.4f;                       // what the sails get (0..1)
			float logT = 0.0f;
		};
		inline State g;
		// v1.8.28b: RowRim.ini [Wind], the Wind page: Strength (x the weather's), Shifts (how far and how often it swings
		// round; 0 holds its direction), Wander (the slow drift), Gusts (0: a steady wind), HeadWind (the least drive a set
		// sail gives, even head to wind)
		struct Settings
		{
			bool loaded = false;
			float strength = 1.0f, shifts = 1.0f, wander = 1.0f, gusts = 1.0f, headWind = 0.35f;
		};
		inline Settings g_set;
		inline float ReadF(const char* a_key, float a_def)
		{
			char b[32]{};
			GetPrivateProfileStringA("Wind", a_key, "", b, sizeof(b), Plankton::IniPath().c_str());
			try { return b[0] ? std::stof(b) : a_def; } catch (...) { return a_def; }
		}
		inline void Load()
		{
			if (g_set.loaded) return;
			g_set.loaded = true;
			g_set.strength = std::clamp(ReadF("Strength", 1.0f), 0.2f, 2.0f);
			g_set.shifts = std::clamp(ReadF("Shifts", 1.0f), 0.0f, 3.0f);
			g_set.wander = std::clamp(ReadF("Wander", 1.0f), 0.0f, 3.0f);
			g_set.gusts = std::clamp(ReadF("Gusts", 1.0f), 0.0f, 3.0f);
			g_set.headWind = std::clamp(ReadF("HeadWind", 0.35f), 0.0f, 0.8f);
			logger::info("Wind: strength {:.2f}, shifts {:.2f}, wander {:.2f}, gusts {:.2f}, head wind drive {:.2f} (RowRim.ini [Wind])", g_set.strength, g_set.shifts, g_set.wander, g_set.gusts, g_set.headWind);
		}
		inline void Save(const char* a_key, float a_v)
		{
			WritePrivateProfileStringA("Wind", a_key, std::format("{:.2f}", a_v).c_str(), Plankton::IniPath().c_str());
		}
		inline void ShiftNow()  // the Wind page's button: a big shift starts now
		{
			g.nextShift = 0.0f;
			g.shiftLeft = 0.0f;
		}
		inline std::mt19937& Rng()
		{
			static std::mt19937 r{ std::random_device{}() };
			return r;
		}
		inline float Dir() { return g.dir + g.veer; }
		inline float Str() { return g.str; }
		inline float U(float a_lo, float a_hi) { return std::uniform_real_distribution<float>(a_lo, a_hi)(Rng()); }
		inline float WeatherBase()
		{
			auto* sky = RE::Sky::GetSingleton();
			if (!sky) return 0.4f;
			float b = 0.18f + 0.62f * std::clamp(sky->windSpeed, 0.0f, 1.0f);  // a still day still sails, a gale near full
			if (auto* wt = sky->currentWeather) {
				const auto f = wt->data.flags;
				if (f.any(RE::TESWeather::WeatherDataFlag::kRainy)) b += 0.12f;
				if (f.any(RE::TESWeather::WeatherDataFlag::kSnow)) b += 0.08f;
				if (f.any(RE::TESWeather::WeatherDataFlag::kPleasant)) b -= 0.04f;
			}
			return std::clamp(b, 0.12f, 1.0f);
		}
		inline void Tick(float a_dt)
		{
			Load();
			a_dt = std::clamp(a_dt, 0.0f, 0.25f);
			if (!g.init) {
				g.init = true;
				g.dir = U(0.0f, kTau);
				g.base = WeatherBase();
				g.nextShift = U(40.0f, 120.0f);
			}
			// the strength: the weather's, eased, so a change of weather freshens or dies away over half a minute
			g.base += (WeatherBase() - g.base) * (1.0f - std::exp(-a_dt / 20.0f));
			const float wild = 0.6f + g.base;  // rougher weather: more and stronger gusts, shifts sooner
			// the slow wander: up to about 2 deg/s, mostly well under
			g.drift += -0.12f * g.drift * a_dt + U(-1.0f, 1.0f) * 0.02f * g_set.wander * std::sqrt(a_dt);
			g.drift = std::clamp(g.drift, -0.035f * g_set.wander, 0.035f * g_set.wander);
			g.dir += g.drift * a_dt;
			// a big shift every 1.5 to 4 minutes: 30 to 120 deg either way, over 20 to 45 s
			if (g.shiftLeft > 0.0f) {
				const float st = (std::min)(a_dt, g.shiftLeft);
				g.dir += g.shiftRate * st;
				g.shiftLeft -= st;
			} else if (g_set.shifts > 0.0f && (g.nextShift -= a_dt * wild * g_set.shifts) <= 0.0f) {
				const float by = (U(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f) * (std::min)(U(0.52f, 2.1f) * (std::min)(g_set.shifts, 1.5f), kTau * 0.5f);
				g.shiftLeft = U(20.0f, 45.0f);
				g.shiftRate = by / g.shiftLeft;
				g.nextShift = U(90.0f, 240.0f);
				logger::info("Wind: shifting {:.0f} deg over {:.0f} s", by * 57.2958f, g.shiftLeft);
			}
			g.dir = std::fmod(g.dir, kTau);
			if (g.dir < 0.0f) g.dir += kTau;
			// gusts and lulls: a gust every 4 to 14 s (sooner in rough weather), held 1.5 to 5 s, veering up to 12 deg
			if ((g.gustHold -= a_dt) <= 0.0f) { g.gustWant = 0.0f; g.veerWant = 0.0f; }
			if (g_set.gusts > 0.0f && (g.nextGust -= a_dt * g_set.gusts) <= 0.0f) {
				const bool lull = U(0.0f, 1.0f) < 0.25f;
				g.gustWant = (lull ? -U(0.05f, 0.15f) : U(0.08f, 0.3f) * wild) * (std::min)(g_set.gusts, 2.0f);
				g.veerWant = U(-0.21f, 0.21f) * (std::min)(g_set.gusts, 2.0f);
				g.gustHold = U(1.5f, 5.0f);
				g.nextGust = U(4.0f, 14.0f) / wild;
			}
			const float up = g.gustWant > g.gust ? 1.2f : 0.4f;  // gusts come quickly, die away slowly
			g.gust += (g.gustWant - g.gust) * (1.0f - std::exp(-up * a_dt));
			g.veer += (g.veerWant - g.veer) * (1.0f - std::exp(-0.6f * a_dt));
			if (g_set.gusts <= 0.0f) { g.gustWant = g.veerWant = 0.0f; }
			g.str = std::clamp((g.base + g.gust) * g_set.strength, 0.05f, 1.0f);
			if ((g.logT -= a_dt) <= 0.0f) {
				g.logT = 15.0f;
				logger::info("Wind: toward {:.0f} deg, strength {:.2f} (weather {:.2f}, gust {:+.2f}), drift {:+.1f} deg/s{}", Dir() * 57.2958f, g.str, g.base, g.gust,
					g.drift * 57.2958f, g.shiftLeft > 0.0f ? ", shifting" : "");
			}
		}
	}
#include "WindStreaks.inl"  // v1.8.39: wind streaks in the air while aboard, see src\Wind_v1839_Streaks.md

	namespace CarrierAudio
	{
		inline std::atomic<RE::FormID> ids[3]{};  // v1.8.26: each boat's own carrier
		inline std::atomic<unsigned> footstepsLogged{ 0 }, animationsLogged{ 0 };
		inline bool Any()
		{
			for (auto& i : ids) if (i.load(std::memory_order_relaxed)) return true;
			return false;
		}
		inline bool IsCarrier(const RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) return false;
			const auto f = a_ref->GetFormID();
			for (auto& i : ids) { const auto silent = i.load(std::memory_order_relaxed); if (silent && f == silent) return true; }
			return false;
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
		// v1.7.62: the key or button bound to the game's Auto-Move (C by default) on that device, read live so a rebind in
		// the game's settings is followed (seated, the press can come without its user event)
		inline bool IsAutoMove(RE::INPUT_DEVICE a_dev, std::uint32_t a_id)
		{
			auto* cm = RE::ControlMap::GetSingleton();
			auto* ue = RE::UserEvents::GetSingleton();
			if (!cm || !ue || !ue->autoMove.c_str()) return false;
			const std::uint32_t k = cm->GetMappedKey(std::string_view{ ue->autoMove.c_str() }, a_dev);
			return k != 0xFF && k != 0xFFFFFFFF && a_id == k;
		}
		// v1.7.74: any game control's key or button on that device, read live (rebinds followed)
		inline bool IsMappedTo(const RE::BSFixedString& a_event, RE::INPUT_DEVICE a_dev, std::uint32_t a_id)
		{
			auto* cm = RE::ControlMap::GetSingleton();
			if (!cm || !a_event.c_str()) return false;
			const std::uint32_t k = cm->GetMappedKey(std::string_view{ a_event.c_str() }, a_dev);
			return k != 0xFF && k != 0xFFFFFFFF && a_id == k;
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

	// v1.8.61: SmoothCam's API (its SmoothCamAPI.h, the parts used): while our helm camera is on, SmoothCam is asked to
	// hand the camera over (it otherwise places the camera itself after the game, over ours); given back as you stand.
	namespace SmoothCamLink
	{
		enum class Ver : std::uint8_t { V1, V2, V3 };
		enum class Res : std::uint8_t { OK, NotOwner, MustKeep, AlreadyGiven, AlreadyTaken, BadThread };
		class IV1
		{
		public:
			[[nodiscard]] virtual unsigned long GetSmoothCamThreadId() const noexcept = 0;
			[[nodiscard]] virtual Res RequestCameraControl(SKSE::PluginHandle a_h) noexcept = 0;
			[[nodiscard]] virtual Res RequestCrosshairControl(SKSE::PluginHandle a_h, bool a_restore = true) noexcept = 0;
			[[nodiscard]] virtual Res RequestStealthMeterControl(SKSE::PluginHandle a_h, bool a_restore = true) noexcept = 0;
			virtual SKSE::PluginHandle GetCameraOwner() const noexcept = 0;
			virtual SKSE::PluginHandle GetCrosshairOwner() const noexcept = 0;
			virtual SKSE::PluginHandle GetStealthMeterOwner() const noexcept = 0;
			virtual Res ReleaseCameraControl(SKSE::PluginHandle a_h) noexcept = 0;
			virtual Res ReleaseCrosshairControl(SKSE::PluginHandle a_h) noexcept = 0;
			virtual Res ReleaseStealthMeterControl(SKSE::PluginHandle a_h) noexcept = 0;
		};
		class IV2 : public IV1
		{
		public:
			virtual RE::NiPoint3 GetLastCameraPosition() const noexcept = 0;
			virtual Res RequestInterpolatorUpdates(SKSE::PluginHandle a_h, bool a_allow) noexcept = 0;
			virtual Res SendToGoalPosition(SKSE::PluginHandle a_h, bool a_should, bool a_now = false, const RE::Actor* a_ref = nullptr) noexcept = 0;
		};
		struct Command { std::uint32_t header = 0x9007CA50; std::uint8_t type = 0; void* data = nullptr; };
		struct Request { Ver version; };
		struct Response { std::uint8_t type; void* data = nullptr; };
		struct Container { void* instance = nullptr; Ver version; };
		inline IV1* g_api = nullptr;
		inline Ver g_ver = Ver::V1;
		inline bool g_held = false;
		inline void OnMsg(SKSE::MessagingInterface::Message* a_msg)
		{
			if (!a_msg || a_msg->type != 0 || a_msg->dataLen != sizeof(Response) || !a_msg->data) return;
			const auto* r = static_cast<Response*>(a_msg->data);
			if (r->type == 1 && r->data) {
				const auto* c = static_cast<Container*>(r->data);
				g_api = static_cast<IV1*>(c->instance);
				g_ver = c->version;
				logger::info("Compat: SmoothCam API v{} (the helm camera asks it for the camera while seated)", static_cast<int>(c->version) + 1);
			} else {
				logger::info("Compat: SmoothCam API error");
			}
		}
		inline void Ask()
		{
			if (!GetModuleHandleA("SmoothCam.dll")) return;
			static Request req{ Ver::V3 };
			static Command cmd{};
			cmd.data = &req;
			const bool ok = SKSE::GetMessagingInterface()->Dispatch(0, &cmd, sizeof(Command), "SmoothCam");
			logger::info("Compat: SmoothCam API asked for ({})", ok ? "sent" : "nobody listening");
		}
		inline void Take()
		{
			if (!g_api || g_held) return;
			const auto r = g_api->RequestCameraControl(SKSE::GetPluginHandle());
			g_held = r == Res::OK || r == Res::AlreadyGiven;
			logger::info("Helm camera: SmoothCam {} ({})", g_held ? "handed us the camera" : "kept the camera", static_cast<int>(r));
		}
		inline void Give()
		{
			if (!g_api || !g_held) return;
			if (g_ver >= Ver::V2) static_cast<IV2*>(g_api)->SendToGoalPosition(SKSE::GetPluginHandle(), true, true);
			g_api->ReleaseCameraControl(SKSE::GetPluginHandle());
			g_held = false;
			logger::info("Helm camera: camera given back to SmoothCam");
		}
	}

	// v1.8.59: the helm camera's settings (RowRim.ini [HelmCam]), edited live on the menu's Helm camera page
	namespace HelmCamSet
	{
		struct Cfg
		{
			bool on{ true }, invertY{ false }, cine{ false }, recenter{ true };
			bool helmPoses{ true };     // v1.8.65: [Helm] Poses: the tiller poses (left / right clips) at the helm
			bool calmGraph{ true };     // v1.8.65: [Helm] CalmGraph: seated, the body's movement values held at rest (CBE and other behaviour overhauls)
			int rideMode{ 1 };          // v1.8.64: [Carrier] RideMode: 0 the boat model on the carrier only, 1 on the carrier and on you only while the carrier blinks out (default), 2 always on you (v1.8.57)
			float sens{ 0.35f };        // mouse
			float smooth{ 0.18f };      // s: the orbit eases to where the mouse put it (PlayStation feel); 0 = instant
			float follow{ 0.10f };      // s: the camera trails the boat this much; 0 = locked on
			float minDist{ 250.0f }, maxDist{ 12000.0f };  // zoomed in .. out (scroll)
			float height{ 140.0f };     // the point above the boat it looks at
			float pitchMin{ -20.0f }, pitchMax{ 70.0f };  // deg
			float recenterDelay{ 3.0f }, recenterSpeed{ 0.6f };  // s idle, then swings back behind her
			float shotLen{ 8.0f };      // s per cinematic shot
			float deadzone{ 0.004f };   // v1.8.62: look input smaller than this is ignored
			int cineKey{ 0x2F };        // V
			float mouseScale{ 0.02f };  // v1.8.79: [HelmCam] MouseScale: raw mouse counts -> orbit
		};
		inline Cfg g;
		inline bool loaded = false;
		inline void Load()
		{
			loaded = true;
			const auto ini = Plankton::IniPath();
			auto f = [&](const char* k, float d, float lo, float hi) {
				char b[32]{};
				GetPrivateProfileStringA("HelmCam", k, "", b, sizeof(b), ini.c_str());
				float v = d;
				try { if (b[0]) v = std::stof(b); } catch (...) {}
				return std::clamp(v, lo, hi);
			};
			auto i = [&](const char* k, int d) { return GetPrivateProfileIntA("HelmCam", k, d, ini.c_str()); };
			g.on = i("On", 1) != 0;
			g.helmPoses = GetPrivateProfileIntA("Helm", "Poses", 1, ini.c_str()) != 0;
			g.calmGraph = GetPrivateProfileIntA("Helm", "CalmGraph", 1, ini.c_str()) != 0;
			g.rideMode = std::clamp(static_cast<int>(GetPrivateProfileIntA("Carrier", "RideMode", 1, ini.c_str())), 0, 2);
			g.invertY = i("InvertY", 0) != 0;
			g.recenter = i("Recenter", 1) != 0;
			g.cine = i("Cinematic", 0) != 0;
			g.cineKey = i("CinematicKey", 0x2F);
			g.sens = f("Sens", 0.35f, 0.02f, 3.0f);
			g.smooth = f("Smooth", 0.18f, 0.0f, 1.5f);
			g.follow = f("Follow", 0.10f, 0.0f, 1.0f);
			g.minDist = f("MinDist", 250.0f, 50.0f, 2000.0f);
			g.maxDist = f("MaxDist", 12000.0f, 300.0f, 60000.0f);
			g.mouseScale = f("MouseScale", 0.02f, 0.001f, 0.5f);
			g.height = f("Height", 140.0f, -200.0f, 1000.0f);
			g.pitchMin = f("PitchMin", -20.0f, -80.0f, 0.0f);
			g.pitchMax = f("PitchMax", 70.0f, 0.0f, 89.0f);
			g.recenterDelay = f("RecenterDelay", 3.0f, 0.5f, 30.0f);
			g.recenterSpeed = f("RecenterSpeed", 0.6f, 0.05f, 5.0f);
			g.shotLen = f("ShotLength", 8.0f, 2.0f, 60.0f);
			g.deadzone = f("DeadZone", 0.004f, 0.0f, 0.2f);
			logger::info("Helm camera: {} | mouse x{:.2f}, smooth {:.2f} s, follow {:.2f} s, distance {:.0f}..{:.0f}, cinematic key {:X} (RowRim.ini [HelmCam])",
				g.on ? "ours" : "off (the game's)", g.sens, g.smooth, g.follow, g.minDist, g.maxDist, g.cineKey);
		}
		inline const Cfg& Get()
		{
			if (!loaded) Load();
			return g;
		}
		inline void Save()
		{
			const auto ini = Plankton::IniPath();
			auto w = [&](const char* k, const std::string& v) { WritePrivateProfileStringA("HelmCam", k, v.c_str(), ini.c_str()); };
			auto wf = [&](const char* k, float v) { w(k, std::format("{:.2f}", v)); };
			w("On", g.on ? "1" : "0");
			w("MouseScale", std::format("{:.3f}", g.mouseScale));
			WritePrivateProfileStringA("Helm", "Poses", g.helmPoses ? "1" : "0", ini.c_str());
			WritePrivateProfileStringA("Helm", "CalmGraph", g.calmGraph ? "1" : "0", ini.c_str());
			WritePrivateProfileStringA("Carrier", "RideMode", std::to_string(g.rideMode).c_str(), ini.c_str());
			w("InvertY", g.invertY ? "1" : "0");
			w("Recenter", g.recenter ? "1" : "0");
			w("Cinematic", g.cine ? "1" : "0");
			w("CinematicKey", std::to_string(g.cineKey));
			wf("Sens", g.sens); wf("Smooth", g.smooth); wf("Follow", g.follow);
			wf("MinDist", g.minDist); wf("MaxDist", g.maxDist); wf("Height", g.height);
			wf("PitchMin", g.pitchMin); wf("PitchMax", g.pitchMax);
			wf("RecenterDelay", g.recenterDelay); wf("RecenterSpeed", g.recenterSpeed); wf("ShotLength", g.shotLen);
			w("DeadZone", std::format("{:.3f}", g.deadzone));
		}
		inline void ToggleCine()
		{
			Get();
			g.cine = !g.cine;
			RE::SendHUDMessage::ShowHUDMessage(g.cine ? "Cinematic camera" : "Helm camera");
			logger::info("Helm camera: cinematic {}", g.cine ? "on" : "off");
		}
	}


	class TestHull
	{
	public:
		// v1.8.26: the three boats are each their own, all three can be out at once: Of(kind) is that boat, Get() the one
		// in use (seated in, aboard, or the nearest to you, see TickAll). src\Boats_v1826_ThreeBoats.md
		static TestHull& Of(int a_k)
		{
			static TestHull s[kKindCount];
			static bool named = false;
			if (!named) {
				named = true;
				for (int i = 0; i < kKindCount; ++i) s[i].kind_ = i;
				s[kKindSail].heightOff_ = kSailHeight;  // v1.8.27c
			}
			return s[std::clamp(a_k, 0, kKindCount - 1)];
		}
		static inline int s_active = -1;
		static inline float s_holdT = 0.0f;  // v1.8.89: after Spawn boat / Use this boat the auto-switch ("you are by it") leaves the chosen boat alone for a while
		static void HoldActive() { s_holdT = 60.0f; }
		static TestHull& Get()
		{
			if (s_active < 0) {
				s_active = SailEnabled() ? IniKind() : 0;
				RowRimBakedIni::g_iniKind = s_active;
			}
			return Of(s_active);
		}
		static int Active() { Get(); return s_active; }
		bool IsActive() const { return this == &Get(); }
		// the boat you use: the one you sit in, else the one you stand aboard, else the nearest one out within 1500 of you
		// (from its hull's ends), else the last one. Every boat out runs its own frame (waves, drift, its sail, its model);
		// only the one in use does what is yours (seats, prompts, crew, the camera, wake ripples, the Sea page readout).
		// v1.8.66: the whole save, once a few seconds after each load, silently (log only): every game-made (FF) carrier
		// (our own or an old version's mannequin) and every game-made boat hull (vanilla ShipRowBoat01, our rowboat,
		// sailing boat and longship hulls) that is none of the boats out now and none of the lake boats' is disabled and
		// deleted, wherever it is (a docked stray far away, a mannequin left in a house). Plugin-placed mannequins and
		// rowboats (player homes, docks) are never FF refs and are never touched.
		static inline float s_saveSweepIn = -1.0f;
		static void SweepSaveStrays()
		{
			std::unordered_set<RE::FormID> keep;
			for (int k = 0; k < kKindCount; ++k) {
				if (Of(k).refId_) keep.insert(Of(k).refId_);
				if (Of(k).carrierId_) keep.insert(Of(k).carrierId_);
			}
			auto* dh = RE::TESDataHandler::GetSingleton();
			const RE::TESForm* hullBases[4] = {
				RE::TESForm::LookupByID(kRowBoatStatic),
				dh ? dh->LookupForm(kOwnRowHullLocal, "OarsAndSails.esl") : nullptr,
				dh ? dh->LookupForm(kSailHullLocal, "OarsAndSails.esl") : nullptr,
				dh ? dh->LookupForm(kLongHullLocal, "OarsAndSails.esl") : nullptr,
			};
			std::vector<RE::TESObjectREFR*> gone;
			int carriers = 0, hulls = 0, gear = 0;
			{
				auto [map, lock] = RE::TESForm::GetAllForms();
				if (!map) return;
				RE::BSReadLockGuard guard{ lock.get() };
				for (auto& [id, form] : *map) {
					if (!form || (id >> 24) != 0xFF || keep.contains(id)) continue;
					auto* ref = form->AsReference();
					if (!ref || ref->IsDeleted()) continue;
					auto* base = ref->GetBaseObject();
					if (!base) continue;
					if (auto* npc = base->As<RE::TESNPC>(); npc && IsCarrierBase(npc)) {
						gone.push_back(ref);
						++carriers;
						continue;
					}
					bool hull = false;
					for (auto* hb : hullBases) if (hb && base == hb) hull = true;
					if (hull && !LakeBoats::IsOurs(id)) {
						gone.push_back(ref);
						++hulls;
						continue;
					}
					// v1.8.68: Fishing Supplies, bedrolls and fishing markers of ours left at old boat spots
					if (Gear::IsGearBase(base) && !Gear::IsHeldGear(id) && !LakeBoats::IsOurs(id)) {
						gone.push_back(ref);
						++gear;
					}
				}
			}
			for (auto* r : gone) {
				auto* c = r->GetParentCell();
				logger::info("Strays: {:08X} ({}) removed{}", r->GetFormID(), r->As<RE::Actor>() ? "a carrier" : Gear::IsGearBase(r->GetBaseObject()) ? "boat gear" : "a boat hull",
					c ? std::format(" from {} {:08X}", c->IsExteriorCell() ? "exterior" : "interior", c->GetFormID()) : std::string{});
				if (auto* a = r->As<RE::Actor>()) LakeBoats::DeleteActorLater(a);
				else {
					r->Disable();
					r->SetDelete(true);
				}
			}
			logger::info("Strays: save swept: {} stray carrier(s), {} stray hull(s), {} stray piece(s) of gear removed ({} kept as the boats out)", carriers, hulls, gear, keep.size());
		}
		static void TickAll(float a_dt)
		{
			if (s_saveSweepIn > 0.0f && (s_saveSweepIn -= a_dt) <= 0.0f) SweepSaveStrays();  // v1.8.66
			OwnWind::Tick(a_dt);  // v1.8.28: our own wind, once a frame
			if (!SailEnabled()) {
				if (Active() != kKindRow) SwitchActive(kKindRow, "sailing is off");
				Of(kKindRow).Update(a_dt);
				return;
			}
			if (!kLongshipOn) {  // v1.8.53: no longship in this build
				if (Active() == kKindLong) SwitchActive(kKindRow, "the longship is not in this build");
				static RE::FormID s_gone = 0;
				auto& ls = Of(kKindLong);
				if (ls.refId_ && ls.refId_ != s_gone) {
					s_gone = ls.refId_;
					logger::info("Boat: a longship {:08X} from the save removed (not in this build)", ls.refId_);
					ls.Delete(false);
				}
			}
			int want = -1;
			for (int k = 0; k < kKindCount && want < 0; ++k) if (Of(k).seated_) want = k;
			if (s_holdT > 0.0f) { s_holdT -= a_dt; if (want >= 0) s_holdT = 0.0f; }  // v1.8.89: sat in a boat: the hold is over
			// the boat in use first: you stay on it while you are aboard it (v1.8.29b)
			if (want < 0 && Get().havePose_ && Get().HaveBoat() && Get().PlayerAboard()) want = Active();
			for (int k = 0; k < kKindCount && want < 0; ++k) if (Of(k).havePose_ && Of(k).HaveBoat() && Of(k).PlayerAboard()) want = k;
			if (want < 0) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					const auto p = player->GetPosition();
					float best = 1500.0f;
					for (int k = 0; k < kKindCount; ++k) {
						auto& h = Of(k);
						if (!h.havePose_ || !h.HaveBoat()) continue;
						const float d = std::hypot(p.x - h.pos_.x, p.y - h.pos_.y) - h.Dims().feelLen;
						if (d < best) { best = d; want = k; }
					}
				}
			}
			if (s_holdT <= 0.0f && want >= 0 && want != Active() && !Get().seated_) SwitchActive(want, Of(want).seated_ ? "seated" : "you are by it");
			const int act = Active();
			Of(act).Update(a_dt);
			for (int k = 0; k < kKindCount; ++k) {
				if (k == act || (!kLongshipOn && k == kKindLong)) continue;
				auto& h = Of(k);
				if (h.refId_ || k == kKindRow || (k == kKindSail && SailEnabled())) h.Update(a_dt);  // the rowboat (v1.8.87: and the sailing boat) every frame for its docks
				else h.TendHome(a_dt);  // v1.8.69d: a boat not out in this save (a new save) still comes to its own dock spots
			}
			{
				auto& b = Get();  // v1.8.39: the wind streaks round you while you are aboard the boat in use
				WindStreaks::Tick(a_dt, b.haveWater_ && b.OnBoat(), b.waterZ_);
			}
			for (int k = 0; k < kKindCount; ++k) Of(k).GuardRide(k == Active());  // v1.8.57
		}
		static void SwitchActive(int a_k, const char* a_why)
		{
			a_k = std::clamp(a_k, 0, kKindCount - 1);
			if (a_k == Active()) return;
			auto& from = Get();
			if (from.seated_) from.StandUp("changing boats");
			if (Crew::Lingering()) Crew::StandAll(Crew::g.pose);
			from.DropPicks();
			from.UntameAll("changing boats");
			if (from.footIkOff_) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) player->SetGraphVariableBool("bHumanoidFootIKDisable", false);
				from.footIkOff_ = false;
			}
			s_active = a_k;
			RowRimBakedIni::g_iniKind = a_k;  // its own [Wake] and [CrewSeats] (RowRim.ini [SailBoat.Wake] ...)
			Wake::g_stern = Wake::Stern{};
			Wake::g_hull = Wake::Hull{};
			Crew::ReloadTunes();
			s_boardPickKind = -1;
			logger::info("Boat: using the {} now ({})", DimsOf(a_k).label, a_why);
		}
		static bool IsAnyCarrier(RE::FormID a_id)
		{
			if (!a_id) return false;
			for (int k = 0; k < kKindCount; ++k) if (Of(k).CarrierId() == a_id) return true;
			return false;
		}
		static bool IsOursAny(RE::FormID a_id)
		{
			if (!a_id) return false;
			for (int k = 0; k < kKindCount; ++k) {
				const auto& h = Of(k);
				if (a_id == h.refId_ || a_id == h.carrierId_ || h.IsPickRef(a_id) || a_id == h.LampLightId()) return true;
			}
			return false;
		}

		// ---- v1.8.8: the boats. Each its own: hull (and collision), model, seats, size and settings ----
		enum BoatKind : int { kKindRow = 0, kKindSail = 1, kKindLong = 2, kKindCount = 3 };
		// v1.8.53: the longship is out of this release (its code stays in the source, off: no boat, page, dock or command;
		// its meshes and textures are not packed). A longship left out in a save is removed on load.
		static constexpr bool kLongshipOn = false;
		// v1.8.27: the sailing boat drawn a quarter bigger (its collision nif is built at this size: hull_collision.py sail 1.25).
		// Everything placed on it in code (seats, prompts, crew, its size and waterline) is in world units, scaled to match.
		static constexpr float kSailScale = 1.25f;
		static constexpr float kSailHeight = -10.0f;  // v1.8.27c: Miguel's height for the sailing boat (its floorboards 8 higher in the model to match)
		float ModelScale() const { return Kind() == kKindSail ? kSailScale : 1.0f; }
		float WaterZ() const { return kWaterZ * ModelScale(); }  // the origin under the water: a bigger boat draws deeper
		struct BoatDims
		{
			const char* name;       // oas boat <name>, RowRim.ini [Boat] Kind
			const char* label;
			const char* rowSec;     // RowRim.ini: speed, glide, turning
			const char* sailSec;    // sail speed and flapping
			float feelLen, feelWidth;            // the shore / rock feelers: half length and half beam
			float aboardX, aboardYlo, aboardYhi, aboardZhi;  // "on the boat" (model space)
			float deckZ;                         // standing height on the deck (model z)
			float weightHalfLen, weightHalfWide; // where standing people tip the boat
			float rock;                          // v1.8.8b: how much it rolls and pitches (a longship hardly answers a step)
			float aboardZlo;                     // v1.8.8b: "on the boat" only this high (swimming beside it is not aboard)
		};
		static const BoatDims& DimsOf(int a_k)
		{
			static const BoatDims k[kKindCount] = {
				{ "row", "Rowboat", "Row", "RowSail", 300.0f, 85.0f, 120.0f, -450.0f, 320.0f, 150.0f, kDeckZ, 230.0f, 85.0f, 1.0f, -30.0f },
				// v1.8.27: a quarter bigger (kSailScale), its standing height on the new floorboards (z 41 in the model; v1.8.27c: 49)
				{ "sail", "Sailing boat", "SailBoat", "Sail", 300.0f * kSailScale, 100.0f * kSailScale, 105.0f * kSailScale, -300.0f * kSailScale, 300.0f * kSailScale,
					170.0f * kSailScale, 49.0f * kSailScale, 250.0f * kSailScale, 95.0f * kSailScale, 0.7f, 10.0f * kSailScale },
				{ "longship", "Longship", "Longship", "LongshipSail", 1080.0f, 262.0f, 255.0f, -1080.0f, 1080.0f, 230.0f, 48.0f, 900.0f, 250.0f, 0.15f, 25.0f },
			};
			return k[std::clamp(a_k, 0, kKindCount - 1)];
		}
		static int KindFromName(std::string_view a_n)
		{
			std::string n(a_n);
			for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (n == "row" || n == "rowboat") return kKindRow;
			if (n.rfind("sail", 0) == 0) return kKindSail;  // sail, sailboat, sailing, sailskin, sailcloth
			if (n == "longship" || n == "long" || n == "longboat" || n == "viking") return kLongshipOn ? kKindLong : -1;
			return -1;
		}
		// the boat out (or the next one out): RowRim.ini [Boat] Kind (older inis: [Boat] Model); the rowboat with sailing off
		int Kind() const
		{
			if (!SailEnabled()) return kKindRow;
			if (kind_ < 0) kind_ = IniKind();
			return kind_;  // v1.8.26: each of the three is always its own kind
		}
		const BoatDims& Dims() const { return DimsOf(Kind()); }
		bool BoatOut() const { return HaveBoat(); }
		static bool SailingOn() { return SailEnabled(); }
		static int IniKind()
		{
			char b[32]{};
			const auto ini = Plankton::IniPath();
			GetPrivateProfileStringA("Boat", "Kind", "", b, sizeof(b), ini.c_str());
			if (!b[0]) GetPrivateProfileStringA("Boat", "Model", "row", b, sizeof(b), ini.c_str());
			const int k = KindFromName(b);
			return k < 0 ? kKindRow : k;
		}
	private:
		mutable int kind_{ -1 };
	public:

		// ---- commands ----
		void Spawn(bool a_underPlayer)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->GetParentCell()) { Print("RowRim: no player cell"); return; }
			const float yaw = player->GetAngleZ();
			const RE::NiPoint3 fwd{ std::sin(yaw), std::cos(yaw), 0.0f };
			const RE::NiPoint3 feet = player->GetPosition();
			const float ahead = 350.0f;
			RE::NiPoint3 at = a_underPlayer ? feet : RE::NiPoint3{ feet.x + fwd.x * ahead, feet.y + fwd.y * ahead, feet.z };
			float water = 0.0f;
			const bool wet = WaterAt(at, water);
			if (a_underPlayer) at.z = feet.z - Dims().deckZ - 3.0f;  // deck just under the feet
			else at.z = wet ? water + WaterZ() + heightOff_ : feet.z - Dims().deckZ;
			SpawnAt(at, yaw, a_underPlayer ? "under you" : "ahead", wet ? std::format("water {:.0f}", water) : std::string("no water"));
		}

		// v1.7.8: the boat placed at a spot and heading (Spawn ahead of / under the player, and the docks)
		void SpawnAt(const RE::NiPoint3& a_at, float a_yaw, const char* a_where, const std::string& a_water)
		{
			Delete(false);
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* base = HullBase();  // v1.8.8: each boat its own hull (the rowboat ShipRowBoat01, the others from OarsAndSails.esl)
			if (!player || !base || !player->GetParentCell()) {
				Print(std::format("RowRim: no player cell or no hull for the {} ({})", Dims().label, Kind() == kKindRow ? "ShipRowBoat01" : "OarsAndSails.esl from this build: tick it"));
				return;
			}
			const RE::NiPoint3 at = a_at;
			const float yaw = a_yaw;

			// v0.2.3: persistent by default, the way followers are: the engine keeps track of where it is instead of filing it
			// under the cell it was placed in (test: does it stay drawn across cells with no hop at all?)
			auto ref = player->PlaceObjectAtMe(base, persist_);
			if (!ref) { Print("RowRim: PlaceObjectAtMe failed"); return; }
			refId_ = ref->GetFormID();
			kind_ = Kind();  // v1.8.8: this boat is this kind (saved with it)
			if (kind_ != kKindRow) {  // its hull is an activator: no prompt, no activation (the prompts are the pick proxies)
				ref->extraList.SetExtraFlags(RE::ExtraFlags::Flag::kBlockActivate, true);
				ref->extraList.SetExtraFlags(RE::ExtraFlags::Flag::kBlockPlayerActivate, true);
				ref->extraList.SetExtraFlags(RE::ExtraFlags::Flag::kBlockActivateText, true);
			}
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
			Print(std::format("RowRim: {} {:08X} placed {} ({}) at {:.0f} {:.0f} {:.0f} yaw {:.0f}; mode {}; {}, cell hop {}",
				Dims().label, refId_, a_where, a_water, at.x, at.y, at.z, yaw * 180.0f / kPi, ModeName(mode_), persist_ ? "persistent" : "temporary", hop_ ? "on" : "off"));
		}

		// ---- v1.7.8: docks (OAS 56.51 / 56.61). Boat spots placed by hand, shipped in RowRim_Docks.ini. There is one boat,
		// so a dock calls it: walk within 7000 of a dock (its cell loaded) while your boat is somewhere else and it is waiting
		// there, facing the way it was placed, in that dock's own paint. A boat you left within 4000 of you is never taken. ----
		struct Dock { RE::FormID world{ 0 }; float x{ 0 }, y{ 0 }, z{ 0 }, yaw{ 0 }; std::string name; int boat{ 0 }; };  // v1.8.87: boat 0 the rowboat, 1 the sailing boat (RowRim_Docks.ini Boat=row|sail)
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
				char bt[16]{};
				GetPrivateProfileStringA(sec.c_str(), "Boat", "row", bt, sizeof(bt), path.c_str());
				if (!w[0] || !x[0] || !y[0]) continue;
				try {
					Dock d;
					d.world = static_cast<RE::FormID>(std::stoul(w, nullptr, 16));
					d.x = std::stof(x);
					d.y = std::stof(y);
					d.z = z[0] ? std::stof(z) : 0.0f;
					d.yaw = a[0] ? std::stof(a) : 0.0f;
					d.name = nm[0] ? nm : sec;
					d.boat = _stricmp(bt, "sail") == 0 ? static_cast<int>(kKindSail) : static_cast<int>(kKindRow);
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
				WritePrivateProfileStringA(sec.c_str(), "Boat", d.boat == static_cast<int>(kKindSail) ? "sail" : "row", path.c_str());
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
				Dock d{ world, pos_.x, pos_.y, pos_.z, yaw_, rest.empty() ? std::format("dock{}", docks_.size() + 1) : rest, kind_ == kKindSail ? static_cast<int>(kKindSail) : static_cast<int>(kKindRow) };
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
						d.yaw * 57.2958f, d.boat == static_cast<int>(kKindSail) ? "sailing boat" : "rowboat")
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
			return "usage: oas \"dock add <name>\" (the boat in use: oas boat sail first for a sailing boat dock) | \"dock del\" | \"dock list\" | \"dock on\" / \"dock off\" | \"dock reload\"";
		}

		// ---- v1.8.47: each boat's own dock. v1.8.50: each boat keeps every spot you docked it at (up to 12), not only the
		// last: "Dock here" on its page (oas "home here <boat>") brings that boat to the water ahead of you, wherever it is
		// (even close by), and adds the spot (one within 2000 of an old spot replaces it). Come back within 7000 of any of its
		// spots with the boat somewhere else and it is waiting there, facing as it was put (the rowboat no longer answers the
		// shipped docks). A boat you left within 4000 of you is never taken. "Reset dock" (oas "home reset <boat>") ends the
		// spot you are at (none near: all of them), "Reset all" (oas "home resetall <boat>") every one. Kept in
		// RowRim_Docks.ini, [Home.row] / [Home.sail] / [Home.longship]: Count, Spot1..N = world x y z yaw. ----
		struct Home { RE::FormID world{ 0 }; float x{ 0 }, y{ 0 }, z{ 0 }, yaw{ 0 }; };
		static constexpr int kHomeMax = 12;
		static constexpr float kHomeSame = 2000.0f;
		std::vector<Home> homes_;
		bool homeLoaded_{ false };
		int homeServed_{ -1 };
		float homeTick_{ 0.0f }, homeWait_{ 6.0f };  // after a load: the save's own boat first
		std::string HomeSec() const { return std::format("Home.{}", DimsOf(kind_).name); }
		void LoadHome()
		{
			homeLoaded_ = true;
			homes_.clear();
			homeServed_ = -1;
			const std::string path = DockIniPath(), sec = HomeSec();
			const int n = static_cast<int>(GetPrivateProfileIntA(sec.c_str(), "Count", -1, path.c_str()));
			if (n < 0) {  // v1.8.47's one spot: World, X, Y, Z, Yaw
				char w[32]{}, x[32]{}, y[32]{}, z[32]{}, a[32]{};
				GetPrivateProfileStringA(sec.c_str(), "World", "", w, sizeof(w), path.c_str());
				GetPrivateProfileStringA(sec.c_str(), "X", "", x, sizeof(x), path.c_str());
				GetPrivateProfileStringA(sec.c_str(), "Y", "", y, sizeof(y), path.c_str());
				GetPrivateProfileStringA(sec.c_str(), "Z", "", z, sizeof(z), path.c_str());
				GetPrivateProfileStringA(sec.c_str(), "Yaw", "", a, sizeof(a), path.c_str());
				if (w[0] && x[0] && y[0]) {
					try {
						Home h{ static_cast<RE::FormID>(std::stoul(w, nullptr, 16)), std::stof(x), std::stof(y), z[0] ? std::stof(z) : 0.0f, a[0] ? std::stof(a) : 0.0f };
						if (h.world) homes_.push_back(h);
					} catch (...) { logger::warn("Docks: [{}] unreadable, ignored", sec); }
				}
			} else {
				for (int i = 1; i <= (std::min)(n, kHomeMax); ++i) {
					char s[128]{};
					GetPrivateProfileStringA(sec.c_str(), std::format("Spot{}", i).c_str(), "", s, sizeof(s), path.c_str());
					unsigned w = 0;
					Home h{};
					if (std::sscanf(s, "%x %f %f %f %f", &w, &h.x, &h.y, &h.z, &h.yaw) >= 3 && w) { h.world = w; homes_.push_back(h); }
					else if (s[0]) logger::warn("Docks: [{}] Spot{} unreadable, ignored", sec, i);
				}
			}
			for (const auto& h : homes_)
				logger::info("Docks: the {}'s own dock: world {:08X} at ({:.0f}, {:.0f}, {:.0f}) facing {:.0f} deg", DimsOf(kind_).label, h.world, h.x, h.y, h.z, h.yaw * 57.2958f);
		}
		void SaveHome()
		{
			const std::string path = DockIniPath(), sec = HomeSec();
			WritePrivateProfileStringA(sec.c_str(), nullptr, nullptr, path.c_str());  // the section gone
			if (homes_.empty()) return;
			WritePrivateProfileStringA(sec.c_str(), "Count", std::to_string(homes_.size()).c_str(), path.c_str());
			for (size_t i = 0; i < homes_.size(); ++i) {
				const auto& h = homes_[i];
				WritePrivateProfileStringA(sec.c_str(), std::format("Spot{}", i + 1).c_str(), std::format("{:08X} {:.1f} {:.1f} {:.1f} {:.4f}", h.world, h.x, h.y, h.z, h.yaw).c_str(), path.c_str());
			}
		}
		// the spot of this boat's in this world nearest the point, within a_range (or -1)
		int NearestHome(RE::FormID a_world, float a_x, float a_y, float a_range, float* a_d = nullptr) const
		{
			int best = -1;
			float bestD = a_range;
			for (int i = 0; i < static_cast<int>(homes_.size()); ++i) {
				if (homes_[i].world != a_world) continue;
				const float d = std::hypot(homes_[i].x - a_x, homes_[i].y - a_y);
				if (d < bestD) { bestD = d; best = i; }
			}
			if (a_d) *a_d = bestD;
			return best;
		}
	public:
		bool HasHome()
		{
			if (!homeLoaded_) LoadHome();
			return !homes_.empty();
		}
		int HomeCount()
		{
			if (!homeLoaded_) LoadHome();
			return static_cast<int>(homes_.size());
		}
		// a_perm (v1.8.88): the spot goes into RowRim_Docks.ini as a shipped dock for this boat ([DockN], Boat=row|sail) instead
		// of the player's own spot ([Home.*])
		std::string HomeHere(bool a_perm = false)
		{
			if (!homeLoaded_) LoadHome();
			if (a_perm && !docksLoaded_) LoadDocks();
			if (kind_ != kKindRow && !SailEnabled()) return std::format("the {}: sailing is off (Oars and Sails / Boat), only the rowboat is out", DimsOf(kind_).label);
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->GetParentCell()) return "no player";
			if (seated_) return "get up first";
			if (havePose_ && HaveBoat() && PlayerAboard()) return std::format("step off the {} first", Dims().label);
			const RE::FormID world = WorldOf(player);
			if (!world) return "docks are outdoors only";
			const float yaw = player->GetAngleZ();
			const RE::NiPoint3 feet = player->GetPosition();
			const float ahead = Dims().feelLen + 120.0f;  // its stern clear of you
			RE::NiPoint3 at{ feet.x + std::sin(yaw) * ahead, feet.y + std::cos(yaw) * ahead, feet.z };
			float water = 0.0f;
			if (!WaterAt(RE::NiPoint3{ at.x, at.y, at.z + 100.0f }, water)) return std::format("no water {:.0f} ahead of you: face the water and try again", ahead);
			at.z = water + WaterZ() + heightOff_;
			SpawnAt(at, yaw, "at its new dock", std::format("water {:.0f}", water));
			if (!HaveBoat()) return std::format("the {} could not be placed", Dims().label);
			if (a_perm) {
				const int boatKind = kind_ == kKindSail ? static_cast<int>(kKindSail) : static_cast<int>(kKindRow);
				int nearI = -1;
				float nearD = kHomeSame;
				int sameKind = 0;
				for (int i = 0; i < static_cast<int>(docks_.size()); ++i) {
					if (docks_[i].boat != boatKind) continue;
					++sameKind;
					if (docks_[i].world != world) continue;
					const float dd = std::hypot(docks_[i].x - pos_.x, docks_[i].y - pos_.y);
					if (dd < nearD) { nearD = dd; nearI = i; }
				}
				const bool replaced = nearI >= 0;
				Dock d{ world, pos_.x, pos_.y, pos_.z, yaw_, replaced ? docks_[nearI].name : std::format("{}{}", Dims().name, sameKind + 1), boatKind };
				if (replaced) docks_[nearI] = d; else docks_.push_back(d);
				SaveDocks();
				dockServed_ = replaced ? nearI : static_cast<int>(docks_.size()) - 1;
				logger::info("Docks: the {} docked here for good ('{}'{}): world {:08X} at ({:.0f}, {:.0f}, {:.0f}) facing {:.0f} deg", Dims().label, d.name, replaced ? ", in place of the dock near it" : "", world, d.x, d.y, d.z, d.yaw * 57.2958f);
				return std::format("the {} is docked here for good: it is a shipped dock '{}' in RowRim_Docks.ini ({} docks). A boat with a player dock spot (Dock here) ignores shipped docks: Reset all clears them", Dims().label, d.name, docks_.size());
			}
			const Home h{ world, pos_.x, pos_.y, pos_.z, yaw_ };
			int i = NearestHome(world, h.x, h.y, kHomeSame);
			const bool moved = i >= 0;
			if (moved) homes_[i] = h;
			else {
				if (static_cast<int>(homes_.size()) >= kHomeMax) homes_.erase(homes_.begin());  // the oldest one goes
				homes_.push_back(h);
				i = static_cast<int>(homes_.size()) - 1;
			}
			SaveHome();
			homeServed_ = i;
			dockServed_ = -1;
			logger::info("Docks: the {} docked here ({} of {}{}): world {:08X} at ({:.0f}, {:.0f}, {:.0f}) facing {:.0f} deg", Dims().label, i + 1, homes_.size(),
				moved ? ", in place of the spot near it" : "", world, h.x, h.y, h.z, h.yaw * 57.2958f);
			return std::format("the {} is docked here: it waits here whenever you come back ({} dock spot{})", Dims().label, homes_.size(), homes_.size() == 1 ? "" : "s");
		}
		// a_all: every spot; else the spot you are at (none within reach: every spot)
		std::string HomeReset(bool a_all = false)
		{
			if (!homeLoaded_) LoadHome();
			if (homes_.empty()) return std::format("the {} has no dock of its own", DimsOf(kind_).label);
			auto* player = RE::PlayerCharacter::GetSingleton();
			const int i = (!a_all && player) ? NearestHome(WorldOf(player), player->GetPosition().x, player->GetPosition().y, kDockCallRange) : -1;
			if (i >= 0) homes_.erase(homes_.begin() + i);
			else homes_.clear();
			SaveHome();
			homeServed_ = -1;
			dockServed_ = -1;
			logger::info("Docks: the {}'s {} reset, {} left", DimsOf(kind_).label, i >= 0 ? "dock here" : "docks", homes_.size());
			if (i >= 0) return std::format("the {}'s dock here reset: it stays where it is ({} other dock spot{})", DimsOf(kind_).label, homes_.size(), homes_.size() == 1 ? "" : "s");
			return std::format("the {}'s docks reset: it stays where it is{}", DimsOf(kind_).label, kind_ == kKindRow ? ", the shipped docks call it again" : "");
		}
		std::string HomeStatus()
		{
			if (!homeLoaded_) LoadHome();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (homes_.empty()) return std::format("{}: no dock of its own", DimsOf(kind_).label);
			float d = 0.0f;
			const int i = player ? NearestHome(WorldOf(player), player->GetPosition().x, player->GetPosition().y, 1.0e9f, &d) : -1;
			if (i < 0) return std::format("{}: {} dock spot(s), none in this worldspace", DimsOf(kind_).label, homes_.size());
			return std::format("{}: {} dock spot(s), the nearest {:.0f} away", DimsOf(kind_).label, homes_.size(), d);
		}
	private:
		// once a second while not aboard: the boat to its own dock spot you come near, unless it is near you already
		void TendHome(float a_dt)
		{
			if (homeWait_ > 0.0f) { homeWait_ -= a_dt; return; }
			if ((homeTick_ -= a_dt) > 0.0f) return;
			homeTick_ = 1.0f;
			if (!homeLoaded_) LoadHome();
			if (homes_.empty() || (kind_ != kKindRow && !SailEnabled()) || (!kLongshipOn && kind_ == kKindLong)) return;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || seated_ || helm_) return;
			if (havePose_ && HaveBoat() && PlayerAboard()) return;
			const RE::FormID world = WorldOf(player);
			const RE::NiPoint3 pp = player->GetPosition();
			if (homeServed_ >= static_cast<int>(homes_.size())) homeServed_ = -1;
			if (homeServed_ >= 0) {  // left it (another worldspace, indoors or 8500 away): it calls the boat again next time
				const auto& s = homes_[homeServed_];
				if (s.world != world || std::hypot(s.x - pp.x, s.y - pp.y) > kDockCallRange + 1500.0f) homeServed_ = -1;
			}
			if (!world) return;
			auto* cell = player->GetParentCell();
			if (!cell || !cell->IsAttached()) return;
			float d = 0.0f;
			const int i = NearestHome(world, pp.x, pp.y, kDockCallRange, &d);
			if (i < 0 || i == homeServed_) return;
			const Home& h = homes_[i];
			auto* tes = RE::TES::GetSingleton();
			auto* hcell = tes ? tes->GetCell(RE::NiPoint3{ h.x, h.y, h.z }) : nullptr;
			if (!hcell || !hcell->IsAttached()) return;  // its cell loaded first
			if (HaveBoat()) {
				if (std::hypot(pos_.x - h.x, pos_.y - h.y) < kDockAtSpot) { homeServed_ = i; return; }  // already there
				if (std::hypot(pos_.x - pp.x, pos_.y - pp.y) < kDockOwnBoatRange) { homeServed_ = i; return; }  // left nearby: kept
			}
			float water = 0.0f;
			const bool wet = WaterAt(RE::NiPoint3{ h.x, h.y, h.z + 100.0f }, water);
			const RE::NiPoint3 at{ h.x, h.y, wet ? water + WaterZ() + heightOff_ : h.z };
			SpawnAt(at, h.yaw, "at its dock", std::format("home {}", i + 1));
			if (HaveBoat()) {
				homeServed_ = i;
				logger::info("Docks: the {} is waiting at its own dock {} ({:.0f} from the player)", Dims().label, i + 1, d);
			}
		}

	private:
		// once a second while not aboard: bring the boat to a dock the player is near, unless their boat is near them already
		void TendDocks(float a_dt)
		{
			if (dockWait_ > 0.0f) { dockWait_ -= a_dt; return; }
			if ((dockTick_ -= a_dt) > 0.0f) return;
			dockTick_ = 1.0f;
			if (!docksLoaded_) LoadDocks();
			if (HasHome()) return;  // v1.8.47: docked at its own spot: the shipped docks leave it be
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!docksOn_ || docks_.empty() || !player || seated_ || helm_) return;
			// v1.8.26: not while you are on the rowboat itself. v1.8.37: on the sailing boat or the longship it is waiting at the
			// dock as you come in (it only came once you had stepped off)
			if (havePose_ && HaveBoat() && PlayerAboard()) return;
			const RE::FormID world = WorldOf(player);
			if (!world) return;
			auto* cell = player->GetParentCell();
			if (!cell || !cell->IsAttached()) return;
			const RE::NiPoint3 pp = player->GetPosition();
			int best = -1;
			float bestD = kDockCallRange;
			for (int i = 0; i < static_cast<int>(docks_.size()); ++i) {
				const auto& d = docks_[i];
				if (d.world != world || d.boat != static_cast<int>(kind_)) continue;  // v1.8.87: each boat its own shipped docks
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
			const RE::NiPoint3 at{ d.x, d.y, wet ? water + WaterZ() + heightOff_ : d.z };
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
		enum PickKind : int { kPickBoard = 0, kPickSteer, kPickRow, kPickBedroll, kPickSail, kPickCount };  // v1.7.73: the mast
		// which of ours this is (any of our three activators near the boat, also one left from before a load), or -1
		int PickKindOf(const RE::TESObjectREFR* a_ref) const
		{
			if (!a_ref || (a_ref->GetFormID() >> 24) != 0xFF || !HaveBoat()) return -1;
			auto* base = a_ref->GetBaseObject();
			if (!base) return -1;
			for (int k = 0; k < kPickCount; ++k) if (picks_[k].id && picks_[k].id == a_ref->GetFormID()) return k;
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
		// E on one of them (the activator's own Activate, hooked)
		void OnPick(int a_kind)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || seated_ || !HaveBoat() || !havePose_) return;
			if ((a_kind == kPickSteer || a_kind == kPickRow) && SeatFlipTooSoon(picks_[a_kind].tag)) return;  // v1.8.57
			logger::info("Pick: E on {}", picks_[a_kind].tag);
			if (a_kind == kPickBoard) {
				// onto the floorboards between the thwarts, standing, facing the bow; v1.8.39b: the sailing boat by the helm (just
				// forward of the stern seat, clear of the tiller to port of it), not amidships
				const RE::NiPoint3 at = lastAt_ + lastRot_ * (Kind() == kKindSail ? RE::NiPoint3{ seat_.x - 6.0f, seat_.y + 55.0f, Dims().deckZ + 6.0f } : RE::NiPoint3{ 0.0f, -40.0f, Dims().deckZ + 6.0f });
				for (const char* ev : { "swimStop", "SwimStop" }) player->NotifyAnimationGraph(ev);
				player->SetPosition(at, true);
				player->data.angle.z = yaw_ + (bowFlip_ ? kPi : 0.0f);
				if (auto* cc = player->GetCharController()) cc->SetLinearVelocityImpl(RE::hkVector4());
				return;
			}
			if (a_kind == kPickSail) { SailNext(); return; }  // v1.7.73
			if (a_kind == kPickBedroll) {  // v1.7.11: unroll; v1.7.12: or pack it up again
				if (!Gear::BedToggleReady()) return;  // v1.7.47: one toggle at a time (quick repeats hung the game)
				if (Gear::BedrollOut()) {
					Gear::BedrollCommand("roll");
					logger::info("Gear: bedroll packed up");  // v1.7.68: no HUD text (the bed fades away)
				} else Gear::Unroll();
				return;
			}
			if (a_kind == kPickBedroll && Kind() != kKindRow) return;  // v1.8.26
			RequestSit(a_kind == kPickSteer || Kind() != kKindRow ? SeatKind::kHelm : SeatKind::kRower);  // v1.8.26: the helm only on the sailing boats
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
			{ 0x803, "Unroll (the bedroll under the bow thwart)" }, { 0x803, "Sail (the mast)" } };
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

		// ---- v1.7.73: the mast and lug sail (hull node RowRim_Sail, built by scripts\clutter\sail.py) ----
		// sail_: 0 no mast, 1 furled (mast up, sail rolled on the boom), 2 full, 3 reefed. E on the mast steps through them
		// (Step Mast, Raise Sail, Reef Sail, Lower Mast); kept in RowRim.ini [Clutter] Sail. The rig (yard, boom, sail) turns
		// about the mast to the wind the boat feels (the weather's wind less the boat's own way): the boom swings out to
		// half the wind's angle off the stern, lee side, eased like a pendulum, centred when furled; luffing (head to wind)
		// it shivers. Everything on the rig keeps its distance from the mast at any angle (sail.py), and nothing has
		// collision. Phase 1: the sail's shape is fixed per state (cloth and wind drive come next).
	public:
		static constexpr float kSailY = 132.0f;
		static constexpr float kSailClothD = 7.5f;  // sail.py kD: the cloth's plane off the mast axis
		std::string SailPrompt() const
		{
			switch (sail_) {
			case 0: return "Step\nMast";
			case 1: return "Raise\nSail";
			case 2: return "Reef\nSail";
			default: return "Lower\nMast";
			}
		}
		// v1.7.91: how fast the sail drives (1 = the old pace, about twice too fast) and how much the cloth flaps; RowRim.ini
		// [Sail] Speed / Flap, oas "sail speed 0.5" / "sail flap 0.6", the settings page
		void SetSailTuning(float a_speed, float a_flap)
		{
			LoadSailTuning();
			if (a_speed >= 0.0f) sailSpeed_ = std::clamp(a_speed, 0.1f, 2.0f);
			if (a_flap >= 0.0f) sailFlap_ = std::clamp(a_flap, 0.0f, 2.0f);
			const auto ini = Plankton::IniPath();
			WritePrivateProfileStringA(SailSection(), "Speed", std::format("{:.2f}", sailSpeed_).c_str(), ini.c_str());
			WritePrivateProfileStringA(SailSection(), "Flap", std::format("{:.2f}", sailFlap_).c_str(), ini.c_str());
			Print(std::format("RowRim: sail speed {:.2f} (default 0.50), flapping {:.2f} (default 0.60)", sailSpeed_, sailFlap_));
		}
		void SetSailHeadWind(float a_v)  // v1.8.34
		{
			LoadSailTuning();
			sailHead_ = std::clamp(a_v, 0.0f, 1.0f);
			WritePrivateProfileStringA(SailSection(), "HeadWind", std::format("{:.2f}", sailHead_).c_str(), Plankton::IniPath().c_str());
			Print(std::format("RowRim: {} goes {:.0f}% of its speed straight into the wind", Dims().label, sailHead_ * 100.0f));
		}
		void SetSailMotion(float a_rock, float a_heel)  // v1.8.27d
		{
			LoadSailTuning();
			if (a_rock >= 0.0f) sailRock_ = std::clamp(a_rock, 0.0f, 6.0f);
			if (a_heel >= 0.0f) sailHeelK_ = std::clamp(a_heel, 0.0f, 3.0f);
			const auto ini = Plankton::IniPath();
			WritePrivateProfileStringA(SailSection(), "Rock", std::format("{:.2f}", sailRock_).c_str(), ini.c_str());
			WritePrivateProfileStringA(SailSection(), "Heel", std::format("{:.2f}", sailHeelK_).c_str(), ini.c_str());
			Print(std::format("RowRim: {} rocks {:.2f} (default {:.2f}), heels {:.2f} (default {:.2f})", Dims().label, sailRock_, DefRock(), sailHeelK_, DefHeel()));
		}
		std::string SailCommand(const std::string& a_arg)
		{
			if (!SailEnabled()) return "sailing is off (RowRim.ini [Sail] Enabled=1, then restart)";
			static const char* names[] = { "off", "furl", "full", "reef" };
			for (int i = 0; i < 4; ++i)
				if (a_arg == names[i] || (i == 0 && a_arg == "down") || (i == 1 && a_arg == "on")) { SetSail(i); return std::format("sail {}", names[i]); }
			return std::format("sail is {} (oas sail off|furl|full|reef)", names[std::clamp(sail_, 0, 3)]);
		}
	private:
		int sail_{ -1 };       // -1: not read from the ini yet
		float sailLee_{ 0.0f }, sailLeeWant_{ 0.0f }, sailHeel_{ 0.0f }, sailHeelWant_{ 0.0f };  // v1.8.9j: leeway (units/s, + starboard), heel (radians, + starboard down)
		float sailSide_{ 1.0f };  // v1.7.77: which side the boom is on (held through wobbles downwind)
		float mastA_{ 0.0f }, sailStA_[3]{ 0.0f, 0.0f, 0.0f }, sailYaw_{ 0.0f }, sailYawVel_{ 0.0f }, sailShiver_{ 0.0f }, sailToggleAt_{ -10.0f };
		RE::NiAVObject* sailOn_{ nullptr };
		RE::NiAVObject* sailNode_{ nullptr };
		RE::NiAVObject* sailRig_{ nullptr };
		RE::NiAVObject* sailSt_[5]{};  // furled, full, reefed (starboard set), v1.7.78: full, reefed (port set)
		float sailStShown_[5]{ -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };  // the alpha last applied to each
		float sailSideW_{ 1.0f };  // v1.7.78: 1 the starboard set shown .. 0 the port set (the cloth bellies downwind)
		bool sailWasSet_{ false };
		static constexpr int kSheetN = 16;
		RE::NiAVObject* sailSheet_[kSheetN]{};  // v1.7.75: the sheet's rope pieces
		// v1.7.83: the cloth moves. Each set sail's cloth is a BSDynamicTriShape (as the game's faces): its positions are
		// rewritten every frame from the shape it was built with. Grid 23 x 18 (luff to leech, foot to head), row-major.
		static constexpr int kClothU = 23, kClothV = 18;
		struct SailCloth
		{
			RE::BSDynamicTriShape* shape{ nullptr };
			std::vector<RE::NiPoint3> base;
			float side{ 1.0f };  // +1 the starboard set (bellies +x), -1 the port set
		};
		SailCloth cloth_[4]{};  // full, reefed (starboard), full, reefed (port): sailSt_[1..4]
		float clothT_{ 0.0f }, clothFill_{ 1.0f }, clothLag_{ 0.0f };
		void FindCloth()
		{
			int found = 0;
			for (int c = 0; c < 4; ++c) {
				cloth_[c] = SailCloth{};
				auto* n = sailSt_[c + 1] ? sailSt_[c + 1]->AsNode() : nullptr;
				if (!n) continue;
				for (auto& ch : n->GetChildren()) {
					auto* d = ch ? ch->AsDynamicTriShape() : nullptr;
					if (!d) continue;
					auto& rd = d->GetDynamicTrishapeRuntimeData();
					const auto nv = static_cast<int>(rd.dataSize / 16);
					if (!rd.dynamicData || nv != kClothU * kClothV) { logger::info("Sail: cloth shape with {} points, not moved", nv); break; }
					RE::BSSpinLockGuard g(rd.lock);
					const auto* f = static_cast<const float*>(rd.dynamicData);
					cloth_[c].base.resize(nv);
					float sx = 0.0f;
					for (int i = 0; i < nv; ++i) { cloth_[c].base[i] = { f[i * 4], f[i * 4 + 1], f[i * 4 + 2] }; sx += f[i * 4]; }
					cloth_[c].side = sx >= 0.0f ? 1.0f : -1.0f;
					cloth_[c].shape = d;
					++found;
					break;
				}
			}
			logger::info("Sail: {} of 4 cloths can move{}", found, found ? "" : " (an older hull: the cloth stays still)");
		}
		// the cloth's motion, sideways off the plane of the sail (rig x): it fills with the wind (and empties head to wind),
		// breathes with the gusts, ripples aft and flutters at the leech (much more when luffing), and lags as the boom
		// swings. The luff (on the mast), the foot (the boom) and the head (the yard) stay put; nearest the mast it never
		// comes closer than kD (7.5, mast radius 3): it cannot clip the mast.
		void MoveCloth(float a_dt, float a_phi, float a_wsp, bool a_set)
		{
			const float a = std::abs(a_phi);
			const float draw = !a_set ? 0.0f : a < 0.5f ? 0.0f : a < 1.2f ? (a - 0.5f) / 0.7f : 1.0f;  // 0 luffing .. 1 full
			const float want = a_set ? 0.25f + 0.75f * draw * (0.6f + 0.4f * std::clamp(a_wsp * 2.5f, 0.0f, 1.0f)) : 0.3f;
			clothFill_ += (want - clothFill_) * (1.0f - std::exp(-1.5f * a_dt));
			clothLag_ += (std::clamp(-sailYawVel_ * 9.0f, -10.0f, 10.0f) - clothLag_) * (1.0f - std::exp(-4.0f * a_dt));
			const float luffK = a_set ? 1.0f - draw : 0.3f;
			clothT_ += a_dt * (1.0f + 2.0f * a_wsp + 2.5f * luffK);
			if (clothT_ > 6283.0f) clothT_ -= 6283.0f;
			const float t = clothT_;
			const float gust = 0.07f * std::sin(t * 0.55f) + 0.05f * std::sin(t * 1.37f + 1.1f);
			LoadSailTuning();  // v1.7.91: the flapping times [Sail] Flap
			const float ripA = (0.6f + 1.6f * a_wsp + 3.0f * luffK) * sailFlap_;
			const float flA = (1.0f + 1.5f * a_wsp + 3.5f * luffK) * sailFlap_;
			for (auto& c : cloth_) {
				if (!c.shape || c.shape->GetAppCulled()) continue;
				if (auto* parent = c.shape->parent; parent && parent->GetAppCulled()) continue;
				auto& rd = c.shape->GetDynamicTrishapeRuntimeData();
				if (!rd.dynamicData || rd.dataSize / 16 != c.base.size()) continue;
				RE::BSSpinLockGuard g(rd.lock);
				auto* f = static_cast<float*>(rd.dynamicData);
				for (int j = 0; j < kClothV; ++j) {
					const float sv = static_cast<float>(j) / (kClothV - 1);
					const float ss = std::sin(kPi * sv);
					for (int i = 0; i < kClothU; ++i) {
						const float r = static_cast<float>(i) / (kClothU - 1);
						const int q = j * kClothU + i;
						const auto& b = c.base[q];
						const float off = std::abs(b.x) - kSailClothD;  // the belly it was built with
						const float sr = std::sin(kPi * r);
						float x = kSailClothD + off * (clothFill_ + gust * ss);
						x += ripA * std::sin(7.0f * (r + 0.35f * sv) * kPi - t * 3.1f) * sr * ss;
						x += flA * r * r * r * std::sqrt(ss) * std::sin(16.0f * sv - t * 7.3f + 1.7f * r);
						x += clothLag_ * c.side * std::pow(r, 1.3f) * ss;
						x = (std::max)(x, std::abs(b.y) < 14.0f ? kSailClothD : kSailClothD - 3.0f);
						f[q * 4] = c.side * x;
						f[q * 4 + 1] = b.y;
						f[q * 4 + 2] = b.z;
					}
				}
			}
		}
		// v1.7.81: sailing is a work in progress: off unless RowRim.ini [Sail] Enabled=1 (no mast, no prompt, no Jump key)
		float sailSpeed_{ -1.0f }, sailFlap_{ -1.0f };
		// v1.8.27d: how much it rocks on the water (the idle swell, more in a wind under sail) and heels under sail;
		// RowRim.ini [Sail] / [LongshipSail] Rock / Heel, oas "sail rock N" / "sail heel N", each boat's page
		float sailRock_{ 1.0f }, sailHeelK_{ 1.0f };
		// v1.8.34: how fast she still goes straight into the wind (the least share of full speed a set sail gives), each
		// boat its own: RowRim.ini [Sail] / [LongshipSail] HeadWind (first read from [Wind] HeadWind), oas "sail headwind N"
		float sailHead_{ 0.35f };
		float DefRock() const { return Kind() == kKindSail ? 2.5f : 1.0f; }
		float DefHeel() const { return Kind() == kKindSail ? 1.5f : 1.0f; }
		void LoadSailTuning()
		{
			if (sailSpeed_ >= 0.0f) return;
			const auto ini = Plankton::IniPath();
			char b[32]{};
			// v1.7.98: each boat its own: the sailing boat [Sail], the rowboat's lug sail [RowSail] (first read from [Sail])
			char d[32]{};
			GetPrivateProfileStringA("Sail", "Speed", "0.50", d, sizeof(d), ini.c_str());
			GetPrivateProfileStringA(SailSection(), "Speed", d, b, sizeof(b), ini.c_str());
			try { sailSpeed_ = std::clamp(std::stof(b), 0.1f, 2.0f); } catch (...) { sailSpeed_ = 0.5f; }
			GetPrivateProfileStringA("Sail", "Flap", "0.60", d, sizeof(d), ini.c_str());
			GetPrivateProfileStringA(SailSection(), "Flap", d, b, sizeof(b), ini.c_str());
			try { sailFlap_ = std::clamp(std::stof(b), 0.0f, 2.0f); } catch (...) { sailFlap_ = 0.6f; }
			GetPrivateProfileStringA("Wind", "HeadWind", "0.35", d, sizeof(d), ini.c_str());  // v1.8.34
			GetPrivateProfileStringA(SailSection(), "HeadWind", d, b, sizeof(b), ini.c_str());
			try { sailHead_ = std::clamp(std::stof(b), 0.0f, 1.0f); } catch (...) { sailHead_ = 0.35f; }
			GetPrivateProfileStringA(SailSection(), "Rock", std::format("{:.2f}", DefRock()).c_str(), b, sizeof(b), ini.c_str());  // v1.8.27d
			try { sailRock_ = std::clamp(std::stof(b), 0.0f, 6.0f); } catch (...) { sailRock_ = DefRock(); }
			GetPrivateProfileStringA(SailSection(), "Heel", std::format("{:.2f}", DefHeel()).c_str(), b, sizeof(b), ini.c_str());
			try { sailHeelK_ = std::clamp(std::stof(b), 0.0f, 3.0f); } catch (...) { sailHeelK_ = DefHeel(); }
			logger::info("Sail: speed {:.2f}, flapping {:.2f}, rock {:.2f}, heel {:.2f} (RowRim.ini [{}] Speed / Flap / Rock / Heel)", sailSpeed_, sailFlap_, sailRock_, sailHeelK_, SailSection());
		}
		const char* SailSection() const { return Dims().sailSec; }  // v1.8.8: each boat's own
		const char* RowSection() const { return Dims().rowSec; }
		static bool SailEnabled()
		{
			static int on = -1;
			if (on < 0) {
				on = GetPrivateProfileIntA("Sail", "Enabled", 1, Plankton::IniPath().c_str()) != 0 ? 1 : 0;  // v1.8.53: on by default (the sailing boat ships)
				logger::info("Sail: {} (RowRim.ini [Sail] Enabled)", on ? "enabled" : "off");
			}
			return on == 1;
		}
		void SailLoad()
		{
			if (sail_ >= 0) return;
			// v1.8.21: the rowboat never has a sail (its lug sail and mast are gone for good); the sailing boat and longship keep theirs
			if (!SailEnabled() || Kind() == kKindRow) { sail_ = 0; mastA_ = 0.0f; for (auto& a : sailStA_) a = 0.0f; return; }
			sail_ = std::clamp(static_cast<int>(GetPrivateProfileIntA(RowRimBakedIni::BoatSec(Kind(), "Clutter"), "Sail", 0, Plankton::IniPath().c_str())), 0, 3);  // v1.8.26: each boat's own
			mastA_ = sail_ > 0 ? 1.0f : 0.0f;
			for (int i = 0; i < 3; ++i) sailStA_[i] = sail_ == i + 1 ? 1.0f : 0.0f;
		}
		void SetSail(int a_state)
		{
			SailLoad();
			if (!SailEnabled() || Kind() == kKindRow) return;  // v1.8.21
			sail_ = std::clamp(a_state, 0, 3);
			WritePrivateProfileStringA(RowRimBakedIni::BoatSec(Kind(), "Clutter"), "Sail", std::to_string(sail_).c_str(), Plankton::IniPath().c_str());
			logger::info("Sail: {}", sail_ == 0 ? "mast down" : sail_ == 1 ? "furled" : sail_ == 2 ? "full" : "reefed");
		}
		void SailNext()
		{
			const double now = NowS();
			if (now - sailToggleAt_ < 0.8) return;  // one step a press
			sailToggleAt_ = now;
			SailLoad();
			SetSail(sail_ == 0 ? 1 : sail_ == 1 ? 2 : sail_ == 2 ? 3 : 0);
		}
		// v1.7.74: the wind the boat feels, in its own frame: a_phi where it blows, off the stern (+ starboard: 0 head to
		// wind, pi dead astern); a_str its strength 0..1. RowRim.ini [Sail] WindFlip=1 if the game's wind turns out reversed.
		void SailWind(float& a_phi, float& a_str)
		{
			// v1.8.28: our own wind (OwnWind: its own wandering direction, its strength from the weather), not the sky's
			const float wsp = OwnWind::Str(), wang = OwnWind::Dir();
			const float hd = yaw_ + (bowFlip_ ? kPi : 0.0f);
			const float wx = std::sin(wang) * (0.25f + wsp), wy = std::cos(wang) * (0.25f + wsp);
			// v1.7.79: the boat's own way barely turns the wind (it was 0.6: at sailing speed it outran a 0.15 wind, the wind
			// seemed to come from ahead, the boom sat at 30 deg and gybed back and forth running downwind)
			const float own = std::clamp(speed_ / 300.0f, -1.0f, 1.0f) * 0.06f;
			const float ax = wx * std::cos(hd) - wy * std::sin(hd);   // toward starboard
			const float ay = wx * std::sin(hd) + wy * std::cos(hd) - own;  // toward the bow
			a_phi = std::atan2(ax, -ay);
			a_str = wsp;  // v1.7.77: the weather's own strength (the boat's way only turns the wind, it no longer weakens it)
		}
		// v1.8.9l: Sea of Thieves style trim (sailing boat, longship): W / S at the helm swing the sail while it is set; it
		// starts at the best angle when set and then stays where you put it. How well it draws goes with how near it is to
		// the best angle for the wind. RowRim.ini [Sail] ManualTrim=0: the sail trims itself as before.
		float sailTrim_{ 0.0f };
		bool SailManual()
		{
			return false;  // v1.8.27: removed: the sail always swings to the wind by itself
		}
		float SailIdeal(float a_phi) const
		{
			const float a = std::abs(a_phi), side = a_phi >= 0.0f ? 1.0f : -1.0f;
			return lsRig_ ? -side * (std::min)((kPi - a) * 0.6f, 0.87f) : side * (std::min)(a * 0.5f, 1.5708f);
		}
		float SailMatch(float a_phi)
		{
			if (!SailManual()) return 1.0f;
			const float ideal = SailIdeal(a_phi), a = std::abs(a_phi);
			float err = std::abs(sailYaw_ - ideal);
			if (a < 0.5f || a > kPi - 0.44f) err = (std::min)(err, std::abs(sailYaw_ + ideal));  // nearly head on or dead astern: either side serves
			const float k = std::clamp(1.0f - err / (lsRig_ ? 0.87f : 1.0f), 0.0f, 1.0f);
			return k * k * (3.0f - 2.0f * k);
		}
		void TendTrim(float a_dt, float a_phi, bool a_set, float a_limit, float& a_target)
		{
			if (!SailManual() || !a_set) return;
			if (!sailWasSet_) sailTrim_ = SailIdeal(a_phi);
			if (seated_ && seatKind_ == SeatKind::kHelm) {
				const float in = ((inFwd_ || stickY_ > 0.4f) ? 1.0f : 0.0f) - ((inBack_ || stickY_ < -0.4f) ? 1.0f : 0.0f);
				sailTrim_ = std::clamp(sailTrim_ + in * 0.5f * a_dt, -a_limit, a_limit);
			}
			a_target = sailTrim_;
		}
		// v1.8.9b: the sail draws with you seated or anywhere aboard (stand up from the helm and it sails on, holding its course)
		bool SailDrawing() const { return (sailNode_ || sbMain_ || lsRig_) && (seated_ || PlayerAboard()) && (sail_ == 2 || sail_ == 3) && (sbMain_ || lsRig_ || mastA_ > 0.5f); }
		// v1.7.74: the sail drives the boat, only while you are seated (helm or rower seat). How well it draws by where the
		// wind blows: nothing head to wind (inside 40 deg), best across (90), a little less running dead downwind. Reefed
		// carries 55 percent. Its top speed grows with the wind: about rowing pace in a light breeze, faster in a gale.
		void SailDrive(float a_dt)
		{
			if (!SailDrawing()) return;
			if (AnchorHolding()) return;  // v1.8.29: at anchor the sail drives nothing
			float phi = 0.0f, str = 0.0f;
			SailWind(phi, str);
			const float a = std::abs(phi);  // 0 head to wind .. pi from astern
			// v1.8.8: the longship's square sail points less high: nothing inside 60 deg of the wind, full from 115 deg aft
			const float kNoGo = lsRig_ ? 1.05f : 0.70f, kBeam = lsRig_ ? 2.0f : 1.5708f;
			float eff = 0.0f;
			// v1.7.79: full from a beam reach all the way round to dead downwind (the boom squared out at 90 deg)
			if (a > kNoGo) eff = a < kBeam ? std::sin((a - kNoGo) / (kBeam - kNoGo) * 1.5708f) : 1.0f;
			// v1.8.9j: points of sail. Fore-and-aft rigs point well (60 deg off the wind about 70 percent); from 120 deg aft each
			// rig falls off to its dead-downwind share: sailing boat 0.65, lug sail 0.8, the square sail 0.9
			if (!lsRig_ && eff > 0.0f && a < kBeam) eff = std::pow(eff, 0.7f);
			if (a > 2.1f) {
				const float u = std::clamp((a - 2.1f) / (kPi - 2.1f), 0.0f, 1.0f);
				const float run = lsRig_ ? 0.9f : sbMain_ ? 0.65f : 0.8f;
				eff *= 1.0f + (run - 1.0f) * u * u * (3.0f - 2.0f * u);
			}
			// v1.8.9l: Sea of Thieves style (sailing boat, longship): the sail's angle counts, and it never stalls
			const float match = SailMatch(phi);
			if (lsRig_ || sbMain_) eff = (std::max)(sailHead_, eff * match);  // v1.8.28: always under way; v1.8.34: each boat's own [Sail] HeadWind
			// v1.7.90: the sailing boat carries a main and a jib: more cloth than the lug sail (reefed: part of the main only)
			const float area = lsRig_ ? (sail_ == 2 ? 1.6f : 0.8f) : sbMain_ ? (sail_ == 2 ? 1.3f : 0.6f) : (sail_ == 2 ? 1.0f : 0.55f);  // v1.8.8: the longship's big square sail
			// v1.7.77: faster: a fresh breeze (0.3) about 1.4x rowing pace, a gale about 2.7x
			// v1.7.91: times [Sail] Speed (0.5 by default: a breeze about rowing pace, a gale about 1.4x; was twice that)
			LoadSailTuning();
			const float anK = Kind() == kKindSail ? anDrive_ : 1.0f;  // v1.8.29d: the anchor holds her back, then she gathers way slowly
			const float top = rowMax_ * (0.9f + 1.8f * str) * area * eff * sailSpeed_ * anK;
			const float before = speed_;
			// v1.8.9j: eff > 0 here: in the no-go zone nothing drives, so a boat going astern used to keep going astern for ever
			if (eff > 0.0f && top > speed_) speed_ = (std::min)(top, speed_ + (55.0f + 90.0f * str) * area * eff * (0.4f + 0.6f * sailSpeed_) * anK * a_dt);
			else {
				// v1.7.78: more than the wind gives: it slows to it. v1.8.9j: a fore-and-aft boat carries its way through a tack
				const float slow = (eff <= 0.0f && !lsRig_) ? 0.3f : 0.5f;
				speed_ += (top - speed_) * (1.0f - std::exp(-slow * a_dt));
			}
			// v1.8.9j: leeway and heel, most close-hauled, none running. The side force is the part of the wind across the boat
			const float across = std::sin(a) * (a < 1.5708f ? 1.0f + (1.5708f - a) : 1.0f);
			const float leeSign = phi > 0.0f ? 1.0f : -1.0f;  // to leeward. v1.8.9l: phi + = the wind blows toward starboard (from port)
			sailLeeWant_ = onLand_ ? 0.0f : leeSign * (std::max)(0.0f, speed_) * across * (lsRig_ ? 0.16f : 0.08f);
			sailHeelWant_ = onLand_ ? 0.0f : leeSign * std::clamp(std::sin(a) * eff * (0.04f + 0.14f * str) * area, 0.0f, 0.12f);
			// v1.7.78: every half second while the sail draws: everything that decides the boom and the speed
			static float s_logT = 0.0f;
			if ((s_logT -= a_dt) <= 0.0f) {
				s_logT = 10.0f;  // v1.8.79: was every half second (a third of the whole log)
				logger::info("Sail: wind {:.0f} off the stern (ours {:.2f} rad, {:.2f}), heading {:.0f}, side {}, boom {:.0f} (vel {:.0f}/s), cloth {}, trim {:.2f}, draws {:.2f}, top {:.0f}, speed {:.0f} -> {:.0f}, lee {:.1f}, heel {:.1f}, {} seat",
					phi * 57.2958f, OwnWind::Dir(), OwnWind::Str(), (yaw_ + (bowFlip_ ? kPi : 0.0f)) * 57.2958f, sailSide_ > 0.0f ? "stbd" : "port",
					sailYaw_ * 57.2958f, sailYawVel_ * 57.2958f, sailSideW_ > 0.5f ? "stbd" : "port", match, eff, top, before, speed_, sailLee_, sailHeel_ * 57.2958f, seatKind_ == SeatKind::kHelm ? "helm" : "rower");
			}
		}
	public:
		// v1.7.74: Jump while seated sets the sail: furled -> full -> reefed -> furled (the mast itself: [E] on it, standing)
		void OnSailKey()
		{
			if (!seated_ || !(sailNode_ || sbMain_ || lsRig_) || !SailEnabled() || Kind() == kKindRow) return;  // v1.8.21: no sail on the rowboat
			SailLoad();
			if (sail_ == 0 && (sbMain_ || lsRig_)) sail_ = 1;
			if (sail_ == 0) { RE::SendHUDMessage::ShowHUDMessage("Step the mast first ([E] on the mast)"); return; }
			const double now = NowS();
			if (now - sailToggleAt_ < 0.8) return;
			sailToggleAt_ = now;
			if (sbMain_) { SetSail(sail_ == 1 ? 2 : sail_ == 2 ? 3 : 1); return; }  // v1.8.29: the sailing boat: stowed, full, reefed (Sprint)
			if (lsRig_) { SetSail(sail_ == 2 ? 1 : 2); return; }  // v1.7.94: stowed <-> full (v1.8.8: the longship)
			SetSail(sail_ == 1 ? 2 : sail_ == 2 ? 3 : 1);
		}
	private:
		// ---- v1.7.90: the sailing boat's own rig (RowRim_SailBoat.nif, oas model sail) ----
		// SB_MainRig turns about the mast like the lug sail's rig (the same wind, boom pendulum, gybes and drive); SB_Gaff in
		// it drops to reef or stow the main; SB_JibRig swings about the forestay (its own z), following the boom. Both sails
		// are BSDynamicTriShapes: rebuilt every frame from their built shape: hoisted (gathered toward the boom / the tack),
		// bellied to the lee side (crossfaded through flat as the boom crosses), breathing, rippling and fluttering as the
		// lug sail's cloth does. Stowed (furled): both sails hidden, the gaff down on the boom. Reefed: the main part way,
		// no jib. The mast is always up on this boat.
		struct SbCloth
		{
			RE::BSDynamicTriShape* shape{ nullptr };
			std::vector<RE::NiPoint3> base;
			std::vector<float> t, u;     // t: 0 at the foot .. 1 at the head; u: 0 at the luff .. 1 at the leech
			float built{ 1.0f };         // the side the belly was built on (+1 +x)
		};
		RE::NiAVObject* sbOn_{ nullptr };
		RE::NiAVObject* sbMain_{ nullptr };
		RE::NiAVObject* sbMainT0On_{ nullptr };  // v1.8.39: the rig's own place in the model (read once a model)
		RE::NiPoint3 sbMainT0_{};
		RE::NiAVObject* sbGaff_{ nullptr };
		RE::NiAVObject* sbJib_{ nullptr };
		RE::NiAVObject* sbHal_{ nullptr };
		RE::NiAVObject* sbSails_[4]{};  // v1.7.95: main starboard, main port, jib starboard, jib port (thin solids, one shown a sail)
		// v1.7.96: the sails skinned to a grid of bones (rows up the sail, columns luff to leech): moving the bones bends the
		// mesh on the GPU, as armour on a skeleton (the reshaped-every-frame cloth flickered)
		struct SbBones
		{
			RE::NiAVObject* shape{ nullptr };
			std::vector<RE::NiAVObject*> bone;
			std::vector<RE::NiPoint3> bind;
			int rows{ 0 }, cols{ 0 };
		};
		SbBones sbMainB_{}, sbJibB_{};
		SbBones sbSideB_[4]{};
		RE::NiAVObject* sbClew_{ nullptr };      // v1.8.7: the jib's clew marker (under SB_JibRig)
		RE::NiAVObject* sbSheet_{ nullptr };     // the jib sheet (skinned rope)
		RE::NiAVObject* sbRing_{ nullptr };      // v1.8.27e: the iron ring in the jib's clew (shown with the jib)
		RE::NiAVObject* sbJawOn_{ nullptr };
		// v1.8.31: the rudder (turning about the sternpost) and the tiller (about its post), as the helmsman steers
		RE::NiAVObject* sbRudderOn_{ nullptr };
		RE::NiAVObject* sbRudder_{ nullptr };
		RE::NiAVObject* sbTiller_{ nullptr };
		float rudderA_{ 0.0f };
		// v1.8.40: the helm clip, and the arm on the tiller
		bool helmAnimOn_{ false }, helmEverOk_{ false }, helmPoseWarned_{ false };
		float helmAnimAt_{ 0.0f }, helmRetry_{ 0.0f };
		int helmRejects_{ 0 };
		int helmPose_{ 0 };  // v1.8.41: the clip on: 0 at rest, 1 the tiller over to the left (steering right), -1 to the right
		float helmPoseAt_{ 0.0f }, helmPoseFromA_{ 0.0f }, helmTillerA_{ 0.0f };
		float helmPhi0_{ 0.0f }, helmHandA_{ 0.0f };  // v1.8.42: his wrist's angle round the tiller's post at rest; the tiller from it
		bool helmPhiHave_{ false };
		RE::NiAVObject* sbJaw_[4]{};             // v1.8.29: SB_JawRopeS / P, SB_JawIronS / P
		RE::NiAVObject* sbFurlOn_{ nullptr };
		RE::NiAVObject* sbFurl_[6]{};            // v1.8.30: the main's furl and ties, its reef roll and points, the jib's roll and wrap
		RE::NiAVObject* sbSheetB_[2]{};          // its two end bones: the fairlead, the clew
		RE::NiNode* sbSheetBox_{ nullptr };  // v1.8.4: the skinned sails per side: main starboard, main port, jib starboard, jib port
		static void SbFindBones(RE::NiAVObject* a_root, const char* a_shape, const char* a_box, int a_rows, int a_cols, SbBones& a_out)
		{
			a_out = SbBones{};
			auto* shape = FindShape(a_root, a_shape);
			auto* box = FindShape(a_root, a_box);
			auto* node = box ? box->AsNode() : nullptr;
			if (!shape || !node || static_cast<int>(node->GetChildren().size()) != a_rows * a_cols) return;
			for (auto& ch : node->GetChildren()) {
				if (!ch) { a_out = SbBones{}; return; }
				a_out.bone.push_back(ch.get());
				a_out.bind.push_back(ch->local.translate);
			}
			a_out.shape = shape; a_out.rows = a_rows; a_out.cols = a_cols;
		}
		// the bones moved across the sail (its x): the belly to the lee side (a_side -1 port .. +1 starboard, through flat as
		// the boom crosses), filled by the wind, rippling and fluttering at the leech, lagging as the boom swings
		// v1.8.4: the per-side skinned sails. The cloth is the original mesh as made (its bind pose), the port one that mesh
		// mirrored vertex for vertex; the bones only add small offsets around it, faded to nothing at every edge (the
		// edges never flap), mirrored with the side (a_mir +1 starboard, -1 port). Placed fresh from the bind each frame.
		void SbFlap(SbBones& a_b, float a_mir, float a_wsp, float a_luffK, bool a_jib, float a_fill, float a_hoist = 1.0f)
		{
			if (!a_b.shape || a_b.rows < 3 || a_b.cols < 3 || a_b.shape->GetAppCulled()) return;
			const float t0 = sbT_;
			const float ripA = (0.5f + 1.3f * a_wsp + 2.5f * a_luffK) * sailFlap_;
			const float flA = (0.4f + 0.8f * a_wsp + 1.8f * a_luffK) * sailFlap_;
			const float gust = (0.06f * std::sin(t0 * 0.55f) + 0.04f * std::sin(t0 * 1.37f + 1.1f)) * (std::min)(sailFlap_, 1.0f);
			const float lag = std::clamp(sbLag_, -6.0f, 6.0f) * (a_jib ? 0.6f : 1.0f);
			for (int r = 0; r < a_b.rows; ++r) {
				const float t = static_cast<float>(r) / static_cast<float>(a_b.rows - 1);
				for (int c = 0; c < a_b.cols; ++c) {
					const float u = static_cast<float>(c) / static_cast<float>(a_b.cols - 1);
					const int i = r * a_b.cols + c;
					const auto& b = a_b.bind[i];
					const float st = (std::max)(std::sin(kPi * t), 0.0f), su = (std::max)(std::sin(kPi * u), 0.0f);
					const float m = std::pow(st * su, 1.5f);
					float d = b.x * gust * m;                                                         // breathing (mirrors with b.x)
					d += a_mir * ripA * m * std::sin(2.2f * (u + 0.35f * t) * kPi - t0 * 3.1f);       // a slow wave across
					d += a_mir * flA * m * std::sin(4.5f * t - t0 * 7.3f + 1.7f * u);                 // a quicker shiver up
					d += lag * m;                                                                     // lagging the boom
					// a_fill 1 the mesh as made, 0 emptied to flat (mid change of side): the bones carry the belly away
					// v1.8.29: hoisted part way (reefed), gathered down toward the boom: the head as far down as the gaff
					float z = b.z;
					float y = b.y;
					if (a_hoist < 0.999f && !a_jib) {
						const float zf = a_b.bind[c].z, zh = a_b.bind[(a_b.rows - 1) * a_b.cols + c].z;
						if (zh - zf > 1.0f) z = zf + (b.z - zf) * (std::max)(0.05f, 1.0f - (1.0f - a_hoist) * kSbGaffDrop / (zh - zf));
					}
					// v1.8.30: coming down it folds (flaked side to side, the folds deeper the more it is gathered)
					// v1.8.30h: not the jib (it scrunched): it empties and narrows smoothly into its bundle (SbJibStow)
					const float fold = (r > 0 && a_hoist < 0.999f && !a_jib) ? ((r & 1) ? 1.0f : -1.0f) * 4.5f * (1.0f - a_hoist) * su : 0.0f;
					// v1.8.33: head to wind the sail empties flat (its belly by how well it draws, sbFill_), and as it changes side it
					// goes flat through the middle (a_fill); either way the wind running through it lifts a gentle ripple that runs
					// from the luff aft to the leech (the edges still, as ever)
					const float drawK = std::clamp((sbFill_ - 0.25f) / 0.75f, 0.0f, 1.0f);
					const float belly = a_fill * (0.08f + 0.92f * drawK);
					const float slack = (std::max)(1.0f - drawK, 1.0f - a_fill) * a_hoist * a_hoist;
					const float luffA = slack * (a_jib ? 1.6f : 2.2f) * std::clamp(sailFlap_, 0.3f, 1.5f) * (0.6f + 0.6f * std::clamp(a_wsp, 0.0f, 1.0f));
					const float luff = luffA * m * (std::sin(2.6f * kPi * u - t0 * 4.2f + 1.1f * t) + 0.35f * std::sin(5.1f * kPi * u - t0 * 6.3f + 2.0f * t));
					a_b.bone[i]->local.translate = RE::NiPoint3{ b.x * belly + d * a_fill + (fold + luff) * a_mir, y, z };
				}
			}
		}
		// v1.8.30d: where the jib is furled: lashed along the bowsprit (model space; SB_JibFurl), its sheet then lying slack
		// from the bundle's aft end to the foot of the mast
		// v1.8.30e: no, beside the mast (Miguel's picture): it hangs from a lashing on the mast's forward side (SB_JibFurlHang,
		// at -2 97 362), 205 long, swaying as the boat rocks
		static constexpr float kJibHangLen = 205.0f;
		RE::NiAVObject* sbHang_{ nullptr };
		RE::NiAVObject* sbStay_{ nullptr };      // v1.8.30f: the jib's tack line (hook to the bowsprit's end)
		RE::NiAVObject* sbTackIron_{ nullptr };  // and its iron hook
		RE::NiAVObject* sbCoil_{ nullptr };
		float hangAx_{ 0.0f }, hangAy_{ 0.0f }, hangVx_{ 0.0f }, hangVy_{ 0.0f };
		// v1.8.32b: the jib fades out as it comes in, gone halfway (hoisted 0.75 .. 0.5), its hanging bundle fading in as it goes
		static float SbJibFadeA(float a_hoist)
		{
			const float e = std::clamp((a_hoist - 0.5f) / 0.25f, 0.0f, 1.0f);
			return e * e * (3.0f - 2.0f * e);
		}
		float sbJibAShown_{ 1.0f };
		static float SbJibStowK(float a_hoist)
		{
			const float e = std::clamp((1.0f - a_hoist) / 0.85f, 0.0f, 1.0f);
			return e * e * (3.0f - 2.0f * e);
		}
		// the jib coming in: each bone carried from the sail toward its place on the bundle (the tack to the bowsprit's end,
		// the clew aft along it), all of it there by the time the bundle shows
		void SbJibStow(SbBones& a_b, float a_hoist)
		{
			if (!a_b.shape || !sbJib_ || a_b.rows < 2 || a_b.cols < 2 || a_hoist >= 0.999f || a_b.shape->GetAppCulled()) return;
			if (!sbHang_) return;
			const float e = SbJibStowK(a_hoist);
			const RE::NiMatrix3 Rt = sbJib_->local.rotate.Transpose();
			const RE::NiPoint3 T = sbJib_->local.translate;
			const auto& H = sbHang_->local;  // v1.8.30e: onto the bundle hanging by the mast (the head up, the tack down)
			for (int r = 0; r < a_b.rows; ++r)
				for (int c = 0; c < a_b.cols; ++c) {
					// v1.8.30h: each row of the sail to its height on the bundle, the row kept straight (no shear, no twist)
					const float sAlong = 1.0f - static_cast<float>(r) / static_cast<float>(a_b.rows - 1);
					(void)c;
					const RE::NiPoint3 tm = H.translate + H.rotate * RE::NiPoint3{ 0.0f, 0.0f, -kJibHangLen * (0.04f + 0.92f * sAlong) };
					const RE::NiPoint3 tl = Rt * (tm - T);
					auto& p = a_b.bone[r * a_b.cols + c]->local.translate;
					p = p + (tl - p) * e;
				}
		}
		// v1.8.3: a clean reset of the bones' movement. Each frame every bone is placed fresh from its bind: the shape is
		// worked out once as if the wind were on the right (starboard) and then mirrored as a whole for the left, so the
		// left is the exact flip of the right. The edges never flap: the ripples, gusts, flutter and swing lag are faded
		// to nothing at the luff, foot, head and leech (the outer ring of bones holds the plain belly), the middle of the
		// sail does the moving.
		void SbBend(SbBones& a_b, float a_side, float a_wsp, float a_luffK, bool a_jib)
		{
			if (!a_b.shape || a_b.rows < 3 || a_b.cols < 3) return;
			const float t0 = sbT_;
			const float ripA = (0.6f + 1.6f * a_wsp + 3.0f * a_luffK) * sailFlap_;
			const float flA = (0.8f + 1.2f * a_wsp + 2.5f * a_luffK) * sailFlap_;
			const float gust = (0.07f * std::sin(t0 * 0.55f) + 0.05f * std::sin(t0 * 1.37f + 1.1f)) * (std::min)(sailFlap_, 1.0f);
			const float lag = std::clamp(sbLag_ * a_side, -8.0f, 8.0f) * (a_jib ? 0.6f : 1.0f);  // as seen from the right
			for (int r = 0; r < a_b.rows; ++r) {
				const float t = static_cast<float>(r) / static_cast<float>(a_b.rows - 1);
				for (int c = 0; c < a_b.cols; ++c) {
					const float u = static_cast<float>(c) / static_cast<float>(a_b.cols - 1);
					const int i = r * a_b.cols + c;
					const auto& b = a_b.bind[i];
					// 0 on the edges, 1 in the middle (sin^1.5 both ways)
					const float st = (std::max)(std::sin(kPi * t), 0.0f), su = (std::max)(std::sin(kPi * u), 0.0f);
					const float m = std::pow(st * su, 1.5f);
					float x = b.x * sbFill_;                                                  // the belly (right side)
					x += b.x * gust * m;                                                     // breathing in the gusts
					x += ripA * m * std::sin(2.2f * (u + 0.35f * t) * kPi - t0 * 3.1f);      // a slow wave across the cloth
					x += flA * m * std::sin(4.5f * t - t0 * 7.3f + 1.7f * u);                // a quicker shiver up it
					x += lag * m;                                                            // the cloth lagging the boom
					a_b.bone[i]->local.translate = RE::NiPoint3{ a_side * x, b.y, b.z };     // the left: the right mirrored
				}
			}
		}
		RE::NiMatrix3 sbJibR0_{};
		SbCloth sbMainC_{}, sbJibC_{};
		float sbHoistMain_{ -1.0f }, sbHoistJib_{ -1.0f }, sbT_{ 0.0f }, sbFill_{ 1.0f }, sbLag_{ 0.0f };
		static constexpr float kSbGaffDrop = 200.0f;  // the gaff's fall to the boom when stowed (model units)
		static bool SbCacheCloth(RE::NiAVObject* a_obj, SbCloth& a_c, bool a_jib)
		{
			a_c = SbCloth{};
			auto* d = a_obj ? a_obj->AsDynamicTriShape() : nullptr;
			if (!d) return false;
			auto& rd = d->GetDynamicTrishapeRuntimeData();
			const auto nv = static_cast<int>(rd.dataSize / 16);
			if (!rd.dynamicData || nv <= 0) return false;
			{
				RE::BSSpinLockGuard g(rd.lock);
				const auto* f = static_cast<const float*>(rd.dynamicData);
				a_c.base.resize(nv);
				for (int i = 0; i < nv; ++i) a_c.base[i] = { f[i * 4], f[i * 4 + 1], f[i * 4 + 2] };
			}
			// the foot and head lines (lowest / highest point in each of 16 bands along the sail), the luff and leech by band too
			float sx = 0.0f, lo = 1e9f, hi = -1e9f;
			for (auto& p : a_c.base) { sx += p.x; const float a = a_jib ? p.z : -p.y; lo = (std::min)(lo, a); hi = (std::max)(hi, a); }
			a_c.built = sx >= 0.0f ? 1.0f : -1.0f;
			constexpr int kB = 16;
			float bMin[kB], bMax[kB], cMin[kB], cMax[kB];
			for (int b = 0; b < kB; ++b) { bMin[b] = cMin[b] = 1e9f; bMax[b] = cMax[b] = -1e9f; }
			const float span = (std::max)(hi - lo, 1.0f);
			auto band = [&](float a) { return std::clamp(static_cast<int>((a - lo) / span * kB), 0, kB - 1); };
			// main (rig frame): along the boom is -y, up is z: bands along -y hold z's range; jib (its frame): along the luff is z, out to the clew is -y
			for (auto& p : a_c.base) {
				const int b = band(a_jib ? p.z : -p.y);
				const float h = a_jib ? -p.y : p.z;
				bMin[b] = (std::min)(bMin[b], h); bMax[b] = (std::max)(bMax[b], h);
			}
			for (int b = 0; b < kB; ++b) if (bMin[b] > bMax[b]) { bMin[b] = b ? bMin[b - 1] : 0.0f; bMax[b] = b ? bMax[b - 1] : 1.0f; }
			float hLo = 1e9f, hHi = -1e9f;
			for (auto& p : a_c.base) { const float h = a_jib ? -p.y : p.z; hLo = (std::min)(hLo, h); hHi = (std::max)(hHi, h); }
			const float hs = (std::max)(hHi - hLo, 1.0f);
			auto hband = [&](float h) { return std::clamp(static_cast<int>((h - hLo) / hs * kB), 0, kB - 1); };
			for (auto& p : a_c.base) {
				const int b = hband(a_jib ? -p.y : p.z);
				const float a = a_jib ? p.z : -p.y;
				cMin[b] = (std::min)(cMin[b], a); cMax[b] = (std::max)(cMax[b], a);
			}
			for (int b = 0; b < kB; ++b) if (cMin[b] > cMax[b]) { cMin[b] = b ? cMin[b - 1] : 0.0f; cMax[b] = b ? cMax[b - 1] : 1.0f; }
			a_c.t.resize(nv); a_c.u.resize(nv);
			for (int i = 0; i < nv; ++i) {
				const auto& p = a_c.base[i];
				const float a = a_jib ? p.z : -p.y, h = a_jib ? -p.y : p.z;
				const int b = band(a), c = hband(h);
				// main: t up from the boom to the gaff, u aft from the mast to the leech; jib: t up the luff, u out to the leech
				if (a_jib) {
					a_c.t[i] = std::clamp((p.z - lo) / span, 0.0f, 1.0f);
					a_c.u[i] = std::clamp((h - bMin[b]) / (std::max)(bMax[b] - bMin[b], 1.0f), 0.0f, 1.0f);
				} else {
					a_c.t[i] = std::clamp((h - bMin[b]) / (std::max)(bMax[b] - bMin[b], 1.0f), 0.0f, 1.0f);
					a_c.u[i] = std::clamp((a - cMin[c]) / (std::max)(cMax[c] - cMin[c], 1.0f), 0.0f, 1.0f);
				}
			}
			a_c.shape = d;
			return true;
		}
		void SbMoveCloth(SbCloth& a_c, bool a_jib, float a_hoist, float a_side, float a_wsp, float a_luffK)
		{
			if (!a_c.shape) return;
			const bool show = a_hoist > 0.04f;
			if (a_c.shape->GetAppCulled() == show) a_c.shape->SetAppCulled(!show);
			if (!show) return;
			auto& rd = a_c.shape->GetDynamicTrishapeRuntimeData();
			if (!rd.dynamicData || rd.dataSize / 16 != a_c.base.size()) return;
			const float t0 = sbT_;
			const float gust = (0.07f * std::sin(t0 * 0.55f) + 0.05f * std::sin(t0 * 1.37f + 1.1f)) * (std::min)(sailFlap_, 1.0f);
			// v1.7.98: the flapping back (as the skinned sails), times [Sail] Flap
			const float ripA = (0.6f + 1.6f * a_wsp + 3.0f * a_luffK) * sailFlap_, flA = (1.0f + 1.5f * a_wsp + 3.5f * a_luffK) * sailFlap_;
			const float bellyK = a_side * a_c.built * (sbFill_ + 0.0f) * (0.35f + 0.65f * a_hoist);
			RE::BSSpinLockGuard g(rd.lock);
			auto* f = static_cast<float*>(rd.dynamicData);
			const auto n = a_c.base.size();
			for (std::size_t i = 0; i < n; ++i) {
				const auto& b = a_c.base[i];
				const float t = a_c.t[i], u = a_c.u[i];
				const float ss = std::sin(kPi * t), su = std::sin(kPi * u);
				float x = b.x * (bellyK + gust * ss * a_side * a_c.built);
				const float sx = a_side;  // v1.7.93: the ripples fade through the crossing too (they jumped sign at the middle)
				x += sx * ripA * std::sin(7.0f * (u + 0.35f * t) * kPi - t0 * 3.1f) * su * ss;
				x += sx * flA * u * u * u * std::sqrt((std::max)(ss, 0.0f)) * std::sin(16.0f * t - t0 * 7.3f + 1.7f * u);
				x += sbLag_ * std::pow(u, 1.3f) * ss * (a_jib ? 0.6f : 1.0f);
				float y = b.y, z = b.z;
				if (a_jib) { y *= a_hoist; z *= a_hoist; x *= a_hoist; }   // gathered down the stay to the tack
				else z -= t * (1.0f - a_hoist) * kSbGaffDrop;              // the cloth folds down with the gaff
				f[i * 4] = x; f[i * 4 + 1] = y; f[i * 4 + 2] = z;
			}
		}
		// ---- v1.8.8: the longship's square sail (RowRim_Longship.nif) ----
		// LS_SailRig (at the mast foot) carries the yard, its robands and the sail. It is braced about the mast to the wind with
		// the same wind and the same pendulum as the sailing boat's boom (slower: a heavy yard): square across running before
		// the wind, braced round as the wind comes onto the beam (at most 50 deg), the windward yardarm aft. The sail is a
		// thin solid, almost flat; it never flaps (RULES: no edge ever flaps). Jump while seated: stowed <-> full: the sail
		// fades in or out over 1.5 s and the 24 oars are run in while it is set.
		RE::NiAVObject* lsOn_{ nullptr };
		RE::NiAVObject* lsRig_{ nullptr };
		RE::NiAVObject* lsSail_{ nullptr };
		RE::NiAVObject* lsOars_{ nullptr };
		float lsHoist_{ -1.0f }, lsHoistShown_{ -1.0f };
		// v1.8.9: the square sail skinned to an 8 x 8 bone grid (LS_SailBones, rows foot -> head, columns port -> starboard).
		// Each frame every bone is placed fresh from its bind, moved fore and aft only (its rig y): the wind filling it out
		// (and breathing in the gusts), a slow wave across, a quicker shiver up it (more when it luffs), all times
		// [LongshipSail] Flap and faded to nothing at every edge: the yard, the foot and both leeches never flap.
		SbBones lsB_{};
		float lsT_{ 0.0f }, lsFill_{ 0.0f }, lsFillVel_{ 0.0f }, lsSideE_{ 0.0f }, lsLogT_{ 0.0f };
		// v1.8.9k: the sail answers each wind. lsFill_ is signed: -1 taken aback (blown back against the mast) .. 0 luffing ..
		// 1 running, blown full; on a spring, so the big cloth is slow to fill and swings a little past. Big and slow: flap
		// about 1 Hz at most (was up to 6), and less the tauter the cloth. Every shape below is 0 at all four edges, and
		// everything to one side is the exact mirror of the other (each pattern and its mirror crossfaded by the wind's side).
		void LsFlap(float a_dt, float a_phi, float a_wsp, bool a_set)
		{
			if (!lsB_.shape || lsB_.rows < 3 || lsB_.cols < 3 || lsB_.shape->GetAppCulled()) { lsFill_ = lsFillVel_ = 0.0f; return; }
			LoadSailTuning();
			const float a = std::abs(a_phi);
			auto ss = [](float e0, float e1, float x) { const float k = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return k * k * (3.0f - 2.0f * k); };
			float n = 0.0f;
			if (a_set) n = a < 1.05f ? -0.75f * (1.0f - ss(0.52f, 1.05f, a)) : (0.8f * ss(1.05f, 2.0f, a) + 0.2f * ss(2.0f, kPi, a)) * (0.25f + 0.75f * SailMatch(a_phi));  // v1.8.9l: badly trimmed it spills the wind
			lsFillVel_ += ((n - lsFill_) * 2.2f - lsFillVel_ * 2.0f) * a_dt;
			lsFill_ += lsFillVel_ * a_dt;
			const float depth = 70.0f + 110.0f * std::clamp(a_wsp * 2.0f, 0.0f, 1.0f);  // the deepest belly in this wind (units; the sail is 990 wide)
			const float taut = (std::min)(std::abs(lsFill_), 1.0f);
			const float luffK = a_set ? 1.0f - (std::min)(std::abs(n) / 0.5f, 1.0f) : 0.2f;
			lsSideE_ += (std::clamp(std::sin(a_phi) * 3.0f, -1.0f, 1.0f) - lsSideE_) * (1.0f - std::exp(-1.5f * a_dt));  // + the wind blows toward starboard (from port)
			lsT_ += a_dt * (0.6f + 0.8f * a_wsp + 0.7f * luffK);
			if (lsT_ > 6283.0f) lsT_ -= 6283.0f;
			const float t0 = lsT_;
			const float slack = 1.0f - 0.55f * taut;
			const float flapK = (std::min)(sailFlap_ / 0.6f, 1.5f);
			const float ripA = (3.0f + 4.0f * a_wsp + 10.0f * luffK) * slack * sailFlap_;
			const float flA = (1.0f + 2.0f * a_wsp + 5.0f * luffK) * slack * sailFlap_;
			const float billowA = 14.0f * luffK * sailFlap_;
			const float peak = 1.0f + 0.35f * std::abs(std::sin(a_phi));  // on a reach the belly sits to leeward
			const float windP = (1.0f + lsSideE_) * 0.5f, windS = 1.0f - windP;  // v1.8.9l: blowing toward port uses the mirrored pattern
			auto belly = [&](float u) { return std::pow((std::max)(std::sin(kPi * std::pow(u, peak)), 0.0f), 0.75f); };
			auto wave = [&](float u, float t) {
				float w = 0.08f * depth * taut * flapK * std::sin(t0 * 0.7f + 2.0f * u + 0.7f * t);  // the gust, running across
				w += ripA * std::sin(2.2f * (u + 0.35f * t) * kPi - t0 * 2.2f);  // a slow wave across
				w += flA * std::sin(4.5f * t - t0 * 4.2f + 1.7f * u);            // a quicker shiver up
				w += billowA * std::sin(t0 * 1.1f + 1.5f * u + 1.2f * t);        // luffing: the whole cloth lifts and falls
				return w;
			};
			for (int r = 0; r < lsB_.rows; ++r) {
				const float t = static_cast<float>(r) / static_cast<float>(lsB_.rows - 1);
				const float st = (std::max)(std::sin(kPi * t), 0.0f);
				for (int c = 0; c < lsB_.cols; ++c) {
					const float u = static_cast<float>(c) / static_cast<float>(lsB_.cols - 1);
					const int i = r * lsB_.cols + c;
					const float su = (std::max)(std::sin(kPi * u), 0.0f);
					const float m = std::pow(st * su, 1.5f);
					// blowing toward starboard: leeward is starboard (u 1), so its belly peaks there and its waves run that way
					const float bm = (windP * belly(u) + windS * belly(1.0f - u)) * std::pow(st, 0.75f);
					float d = lsFill_ * depth * bm;
					d += m * (windP * wave(u, t) + windS * wave(1.0f - u, t));
					const auto& b = lsB_.bind[i];
					float y = b.y + d;
					// blown back: the cloth drapes round the mast (rig y 0), never through it
					if (d < 0.0f) y = (std::max)(y, (std::min)(b.y, 26.0f - 0.5f * (std::max)(std::abs(b.x) - 71.0f, 0.0f)));
					lsB_.bone[i]->local.translate = RE::NiPoint3{ b.x, y, b.z };
				}
			}
			if ((lsLogT_ -= a_dt) <= 0.0f) {
				lsLogT_ = 3.0f;
				logger::info("Sail: longship cloth: wind {:.0f} off the stern, fill {:.2f} (wants {:.2f}), belly {:.0f}, luff {:.2f}", a_phi * 57.2958f, lsFill_, n, lsFill_ * depth, luffK);
			}
		}
		void TendLongSail(float a_dt)
		{
			if (lsOn_ != visual_.get()) {
				lsOn_ = visual_.get();
				lsRig_ = FindShape(visual_.get(), "LS_SailRig");
				lsSail_ = FindShape(visual_.get(), "LS_Sail");
				lsOars_ = FindShape(visual_.get(), "LS_Oars");
				SbFindBones(visual_.get(), "LS_Sail", "LS_SailBones", 8, 8, lsB_);  // v1.8.9
				logger::info("Sail: longship sail {}", lsB_.shape ? "skinned (flaps)" : "fixed (an older model)");
				lsHoist_ = lsHoistShown_ = -1.0f;
				logger::info("Sail: longship rig {} (sail {}, oars {})", lsRig_ ? "found" : "not in this model", lsSail_ ? "ok" : "none", lsOars_ ? "ok" : "none");
			}
			if (!lsRig_) return;
			if (sail_ == 0) sail_ = 1;  // the mast is always up
			if (sail_ == 3) sail_ = 2;  // no reefing: stowed or full
			mastA_ = 1.0f;
			const float dt = std::clamp(a_dt, 0.0f, 0.1f);
			const float want = sail_ == 2 ? 1.0f : 0.0f;
			if (lsHoist_ < 0.0f) lsHoist_ = want;
			lsHoist_ = want > lsHoist_ ? (std::min)(want, lsHoist_ + dt / 1.5f) : (std::max)(want, lsHoist_ - dt / 1.5f);
			if (lsSail_ && lsHoist_ != lsHoistShown_) {
				lsHoistShown_ = lsHoist_;
				const float e = lsHoist_ * lsHoist_ * (3.0f - 2.0f * lsHoist_);
				if (lsSail_->GetAppCulled() != (e <= 0.0f)) lsSail_->SetAppCulled(e <= 0.0f);
				if (e > 0.0f) SetAlphaTree(lsSail_, e);
			}
			if (lsOars_) {
				const bool hide = true;  // v1.8.26: the oars removed (were run in under sail)
				if (lsOars_->GetAppCulled() != hide) lsOars_->SetAppCulled(hide);
			}
			float phi = 0.0f, wsp = 0.0f;
			SailWind(phi, wsp);
			const bool set = sail_ >= 2;
			float target = 0.0f;
			if (set) {
				// phi: where the wind blows, off the stern (+ from starboard). Dead astern: square (0). Toward the beam: braced
				// round, the windward yardarm aft (wind from starboard: the starboard end aft = a turn to port, negative here)
				const float side = phi >= 0.0f ? 1.0f : -1.0f;
				target = -side * (std::min)((kPi - std::abs(phi)) * 0.6f, 0.87f);
				sailSide_ = side;
			}
			TendTrim(dt, phi, set, 0.87f, target);  // v1.8.9l
			sailWasSet_ = set;
			sailYawVel_ += ((target - sailYaw_) * 3.0f - sailYawVel_ * 2.4f) * dt;
			sailYawVel_ = std::clamp(sailYawVel_, -0.6f, 0.6f);  // about 35 deg/s at most
			sailYaw_ = std::clamp(sailYaw_ + sailYawVel_ * dt, -0.87f, 0.87f);
			lsRig_->local.rotate = RzM(-sailYaw_);  // as the boom: + turns the starboard end forward
			LsFlap(dt, phi, wsp, set);  // v1.8.9
			RE::NiUpdateData data{};
			data.flags.set(RE::NiUpdateData::Flag::kDirty);
			lsRig_->Update(data);
		}
		void TendSailBoat(float a_dt)
		{
			if (Kind() == kKindLong) { TendLongSail(a_dt); return; }  // v1.8.8
			if (sbOn_ != visual_.get()) {
				sbOn_ = visual_.get();
				sbMain_ = FindShape(visual_.get(), "SB_MainRig");
				sbGaff_ = FindShape(visual_.get(), "SB_Gaff");
				sbJib_ = FindShape(visual_.get(), "SB_JibRig");
				sbHal_ = FindShape(visual_.get(), "SB_Halyards");
				{
					const char* nm[4] = { "SB_SailMainS", "SB_SailMainP", "SB_SailJibS", "SB_SailJibP" };
					for (int i = 0; i < 4; ++i) sbSails_[i] = FindShape(visual_.get(), nm[i]);
				}
				SbFindBones(visual_.get(), "SB_SailMain", "SB_SailMainBones", 8, 8, sbMainB_);  // v1.7.99: 8x8 (was 5x5)
				SbFindBones(visual_.get(), "SB_SailJib", "SB_SailJibBones", 8, 6, sbJibB_);  // v1.7.99: 8x6 (was 5x4)
				{  // v1.8.4: one skinned sail per side (the original mesh and its mirror), shown as the static ones were
					const char* sh[4] = { "SB_SailMainS", "SB_SailMainP", "SB_SailJibS", "SB_SailJibP" };
					const char* bx[4] = { "SB_SailMainSBones", "SB_SailMainPBones", "SB_SailJibSBones", "SB_SailJibPBones" };
					for (int k = 0; k < 4; ++k) SbFindBones(visual_.get(), sh[k], bx[k], 8, k < 2 ? 8 : 6, sbSideB_[k]);
					sbClew_ = FindShape(visual_.get(), "SB_JibClew");  // v1.8.7
					sbSheet_ = FindShape(visual_.get(), "SB_JibSheet");
					sbRing_ = FindShape(visual_.get(), "SB_JibClewRing");
					sbSheetB_[0] = FindShape(visual_.get(), "SB_JibSheetBone0");
					sbSheetB_[1] = FindShape(visual_.get(), "SB_JibSheetBone1");
					auto* bxo = FindShape(visual_.get(), "SB_JibSheetBones");
					sbSheetBox_ = bxo ? bxo->AsNode() : nullptr;
					if (!sbClew_ || !sbSheet_ || !sbSheetB_[0] || !sbSheetB_[1] || !sbSheetBox_) { sbSheet_ = nullptr; }
					logger::info("Sail: jib sheet {}", sbSheet_ ? "found" : "not in this model");
					logger::info("Sail: per-side skinned sails {} ({} {} {} {} bones)", sbSideB_[0].shape ? "found" : "none", sbSideB_[0].bone.size(),
						sbSideB_[1].bone.size(), sbSideB_[2].bone.size(), sbSideB_[3].bone.size());
				}
				logger::info("Sail: sailing boat sails {} (main {} bones, jib {} bones)", sbMainB_.shape && sbJibB_.shape ? "skinned" : "fixed", sbMainB_.bone.size(), sbJibB_.bone.size());
				if (sbJib_) sbJibR0_ = sbJib_->local.rotate;
				const bool m = SbCacheCloth(FindShape(visual_.get(), "SB_SailMain"), sbMainC_, false);
				const bool j = SbCacheCloth(FindShape(visual_.get(), "SB_SailJib"), sbJibC_, true);
				sbHoistMain_ = sbHoistJib_ = -1.0f;
				logger::info("Sail: sailing boat rig {} (main cloth {}, jib cloth {}, gaff {}, jib rig {})", sbMain_ ? "found" : "not in this model", m ? "ok" : "none",
					j ? "ok" : "none", sbGaff_ ? "ok" : "none", sbJib_ ? "ok" : "none");
				if (!sbMain_) { sbGaff_ = sbJib_ = sbHal_ = nullptr; for (auto& q : sbSails_) q = nullptr; sbMainB_ = sbJibB_ = SbBones{}; for (auto& q : sbSideB_) q = SbBones{}; sbClew_ = sbSheet_ = sbRing_ = nullptr; }
			}
			if (!sbMain_) return;
			if (sail_ == 0) sail_ = 1;  // this boat's mast is always up
			// v1.8.29: reefing back (Sprint at the helm: stowed, full, reefed): the main part way up, no jib
			mastA_ = 1.0f;
			const float dt = std::clamp(a_dt, 0.0f, 0.1f);
			// hoisting: stowed 0, full 1, reefed: the main part way and no jib
			const float wantM = sail_ == 2 ? 1.0f : sail_ == 3 ? 0.55f : 0.0f;
			const float wantJ = (sail_ == 2 || sail_ == 3) ? 1.0f : 0.0f;  // v1.8.30: the jib stays set when the main is reefed
			if (sbHoistMain_ < 0.0f) { sbHoistMain_ = wantM; sbHoistJib_ = wantJ; }
			auto toward = [&](float& v, float w) { const float k = dt * 0.3f; v = v < w ? (std::min)(w, v + k) : (std::max)(w, v - k); };  // v1.8.30: 3 s from full to stowed
			toward(sbHoistMain_, wantM);
			toward(sbHoistJib_, wantJ);
			// the lee side, crossfaded through flat as the boom crosses (as the lug sail's starboard / port sets)
			// v1.7.93: only once the boom is well over (9 deg), and more slowly: with the boom wobbling round the centre (head to
			// wind, running straight downwind) the belly flipped side to side every few frames: the flicker
			{
				const float want = sailYaw_ > 0.15f ? 1.0f : sailYaw_ < -0.15f ? 0.0f : sailSideW_;
				// v1.8.5: 1.4 s across (the per-side sails empty to flat and fill out again on the other side over it)
				sailSideW_ = std::clamp(sailSideW_ + (want > sailSideW_ ? 1.0f : want < sailSideW_ ? -1.0f : 0.0f) * dt / 1.4f, 0.0f, 1.0f);
			}
			float phi = 0.0f, wsp = 0.0f;
			SailWind(phi, wsp);
			const bool set = sail_ >= 2;
			float target = 0.0f;
			if (set) {
				const float side = phi >= 0.0f ? 1.0f : -1.0f;
				if (!sailWasSet_) sailSide_ = side;
				if (side != sailSide_ && std::abs(phi) > kPi - 0.44f) target = sailSide_ * 1.5708f;
				else {
					if (side != sailSide_) logger::info("Sail: {} (wind {:.0f} deg off the stern), boom to {}", std::abs(phi) > 1.5708f ? "gybe" : "tack", phi * 57.2958f, side > 0.0f ? "starboard" : "port");
					sailSide_ = side;
					target = side * (std::min)(std::abs(phi) * 0.5f, 1.5708f);
				}
			}
			TendTrim(dt, phi, set, 1.5708f, target);  // v1.8.9l
			sailWasSet_ = set;
			const bool luff = set && std::abs(phi) < 0.5f;
			sailShiver_ += dt * (luff ? 11.0f : 0.0f);
			LoadSailTuning();
			LoadSailTuning();
			const float shiver = luff ? 0.05f * sailFlap_ * (0.4f + wsp) * std::sin(sailShiver_) * (1.0f - std::abs(phi) / 0.5f) : 0.0f;  // v1.7.96: back
			sailYawVel_ += ((target - sailYaw_) * 5.0f - sailYawVel_ * 2.2f) * dt;
			sailYawVel_ = std::clamp(sailYawVel_, -1.1f, 1.1f);
			sailYaw_ = std::clamp(sailYaw_ + sailYawVel_ * dt, -1.5708f, 1.5708f);
			// v1.8.39: the boom's jaw sits on the lower mast (axis -5.14 74.05), the gaff's on the topmast (-1.98 83): the rig turns
			// about the line through both jaws, so each stays on its own mast either side (about the topmast's axis alone the
			// boom's jaw swung 15 clear of the lower mast to starboard). + still swings the boom to starboard.
			{
				constexpr float kAx = -1.98f - -5.14f, kAy = 83.0f - 74.05f, kAz = 377.0f - 150.0f;
				const float al = std::sqrt(kAx * kAx + kAy * kAy + kAz * kAz);
				const RE::NiPoint3 A{ -5.14f, 74.05f, 150.0f };
				if (sbMainT0On_ != sbMain_) { sbMainT0On_ = sbMain_; sbMainT0_ = sbMain_->local.translate; }
				const RE::NiMatrix3 R = AxisM(kAx / al, kAy / al, kAz / al, sailYaw_ + shiver);
				sbMain_->local.rotate = R;
				sbMain_->local.translate = A + R * (sbMainT0_ - A);
			}
			// v1.8.30h: the jib held in by its sheet, as a real one is: it follows the boom closely in close, but never goes
			// out past about 32 degrees, however far the main is let out
			if (sbJib_) {
				constexpr float kJibOut = 0.56f;
				const float jy = kJibOut * std::tanh(sailYaw_ * 0.8f / kJibOut);
				sbJib_->local.rotate = sbJibR0_ * RzM(-(jy + shiver));
			}
			// v1.8.31: the rudder on the sternpost (SB_RudderRig, at the hinge's foot) swings its blade to the side you steer
			// (26 degrees at most), the tiller (SB_TillerRig, its post through the stern bench) the other way, eased
			{
				if (sbRudderOn_ != visual_.get()) {
					sbRudderOn_ = visual_.get();
					sbRudder_ = FindShape(visual_.get(), "SB_RudderRig");
					sbTiller_ = FindShape(visual_.get(), "SB_TillerRig");
				}
				const float want = (seated_ && seatKind_ == SeatKind::kHelm) ? helmSteer_ * 0.45f : 0.0f;
				// v1.8.41/42: with the helm clip, the tiller goes where his hand is (v1.8.43: measured in TillerHelm, after the boat
				// is placed; first person or before it is known, the timed turn)
				if (helmAnimOn_ && !helmPoseWarned_) rudderA_ = helmHandLive_ ? helmHandA_ : helmTillerA_;
				else rudderA_ += (want - rudderA_) * (1.0f - std::exp(-3.0f * dt));
				if (sbRudder_) sbRudder_->local.rotate = AxisM(0.0f, -0.65636f, 0.75445f, std::clamp(rudderA_, -0.45f, 0.45f));  // the sternpost's line (v1.8.44: never past its 26 deg)
				if (sbTiller_) sbTiller_->local.rotate = AxisM(0.0f, 0.0f, 1.0f, rudderA_ * 0.8f);
				RE::NiUpdateData ud{};
				ud.flags.set(RE::NiUpdateData::Flag::kDirty);
				if (sbRudder_) sbRudder_->Update(ud);
				if (sbTiller_) sbTiller_->Update(ud);
			}
			if (sbGaff_) sbGaff_->local.translate = RE::NiPoint3{ 0.0f, 0.0f, -(1.0f - sbHoistMain_) * kSbGaffDrop };
			// v1.8.30: the rolled and tied bundles: the main's on the boom (reefed: the foot rolled thinner, its reef points),
			// the jib's round its stay; each rises out of the boom / comes up round the stay as the cloth comes in
			{
				if (sbFurlOn_ != visual_.get()) {
					sbFurlOn_ = visual_.get();
					const char* fn[6] = { "SB_MainFurl", "SB_MainFurlTies", "SB_MainReef", "SB_MainReefTies", "SB_JibFurl", "SB_JibFurlWrap" };
					for (int i = 0; i < 6; ++i) sbFurl_[i] = FindShape(visual_.get(), fn[i]);
					sbHang_ = FindShape(visual_.get(), "SB_JibFurlHang");
					sbStay_ = FindShape(visual_.get(), "SB_JibTack");
					sbTackIron_ = FindShape(visual_.get(), "SB_JibTackIron");
					sbCoil_ = FindShape(visual_.get(), "SB_JibCoil");
				}
				// v1.8.30c: furled as Miguel's reference (the cloth hanging under its spar in swags between rope ties); the bundles
				// stay as they are, the sails warp down onto them and are gone once they are in
				const float h = sbHoistMain_;
				const bool furl = h < 0.18f;
				const bool reef = false;  // v1.8.30d: reefed, no bundle (the reefed sail as it is)
				const bool jib = sbHoistJib_ < 0.15f;
				const float jibA = SbJibFadeA(sbHoistJib_);
				const bool on[3] = { furl, reef, jibA < 0.999f };  // v1.8.32b: the jib's bundle fades in as the sail fades out
				for (int g = 0; g < 3; ++g)
					for (int j = 0; j < 2; ++j) {
						if (g == 2 && j == 1) continue;  // v1.8.32d: the jib's wraps: shown by the fade below, once the sail is half gone
						if (auto* o = sbFurl_[g * 2 + j]; o && o->GetAppCulled() == on[g]) o->SetAppCulled(!on[g]);
					}
				// v1.8.30e: furled, the jib's tack line is taken off and coiled on the bowsprit (v1.8.30f: that line and its hook;
				// the forestay stays up)
				if (sbStay_ && sbStay_->GetAppCulled() != jib) sbStay_->SetAppCulled(jib);
				if (sbTackIron_ && sbTackIron_->GetAppCulled() != jib) sbTackIron_->SetAppCulled(jib);
				if (sbCoil_ && sbCoil_->GetAppCulled() == jib) sbCoil_->SetAppCulled(!jib);
				// the hanging jib sways as the boat rocks: it keeps hanging plumb, a little late, and swings past
				if (sbHang_) {
					const float tx = -pitch_, ty = -roll_;
					hangVx_ += (14.0f * (tx - hangAx_) - 1.6f * hangVx_) * dt;
					hangVy_ += (14.0f * (ty - hangAy_) - 1.6f * hangVy_) * dt;
					hangAx_ = std::clamp(hangAx_ + hangVx_ * dt, -0.35f, 0.35f);
					hangAy_ = std::clamp(hangAy_ + hangVy_ * dt, -0.35f, 0.35f);
					sbHang_->local.rotate = RxM(hangAx_) * RyM(hangAy_);
					RE::NiUpdateData hu{};
					sbHang_->Update(hu);
				}
				// v1.8.30e: stowed, the gaff comes to rest upright against the mast (turned about its jaw)
				if (sbGaff_) {
					const float g0 = std::clamp((0.2f - h) / 0.2f, 0.0f, 1.0f), g = g0 * g0 * (3.0f - 2.0f * g0);
					const RE::NiMatrix3 R = RxM(-0.766f * g);
					const RE::NiPoint3 jaw{ 0.9f, -10.0f, 385.0f };
					sbGaff_->local.rotate = R;
					sbGaff_->local.translate = jaw - R * jaw + RE::NiPoint3{ 0.0f, 0.0f, -(1.0f - sbHoistMain_) * kSbGaffDrop };
				}
			}
			if (sbHal_) { const bool h = sbHoistMain_ < 0.97f; if (sbHal_->GetAppCulled() != h) sbHal_->SetAppCulled(h); }
			// the cloth: as the lug sail's (MoveCloth): fill with the wind, empty head to wind, gusts, ripples, flutter, lag
			const float a = std::abs(phi);
			const float draw = (!set ? 0.0f : a < 0.5f ? 0.0f : a < 1.2f ? (a - 0.5f) / 0.7f : 1.0f) * (0.25f + 0.75f * SailMatch(phi));  // v1.8.9l
			const float want = set ? 0.25f + 0.75f * draw * (0.6f + 0.4f * std::clamp(wsp * 2.5f, 0.0f, 1.0f)) : 0.3f;
			sbFill_ += (want - sbFill_) * (1.0f - std::exp(-1.5f * dt));
			sbLag_ += (std::clamp(-sailYawVel_ * 9.0f, -10.0f, 10.0f) - sbLag_) * (1.0f - std::exp(-4.0f * dt));
			const float luffK = set ? 1.0f - draw : 0.3f;
			sbT_ += dt * (1.0f + 2.0f * wsp + 2.5f * luffK);
			if (sbT_ > 6283.0f) sbT_ -= 6283.0f;
			const float side = sailSideW_ * 2.0f - 1.0f;
			SbMoveCloth(sbMainC_, false, sbHoistMain_, side, wsp, luffK);  // (an older model with cloth shapes)
			SbMoveCloth(sbJibC_, true, sbHoistJib_, side, wsp, luffK);
			// v1.7.95: the sails are thin solids, built bellied to starboard and to port: the lee one shown once hoisted (the
			// moving cloth flickered in the renderer)
			if (!sbSideB_[0].shape) {  // (v1.8.5: the per-side skinned sails show themselves, below)
				const bool stbd = sailSideW_ >= 0.5f;
				const bool show[4] = { sbHoistMain_ >= 0.98f && stbd, sbHoistMain_ >= 0.98f && !stbd, sbHoistJib_ >= 0.98f && stbd, sbHoistJib_ >= 0.98f && !stbd };
				for (int i = 0; i < 4; ++i)
					if (sbSails_[i] && sbSails_[i]->GetAppCulled() == show[i]) sbSails_[i]->SetAppCulled(!show[i]);
			}
			// v1.7.96: the skinned sails: shown once hoisted, bent by their bones (the belly crossfades through flat as the
			// boom crosses: side runs -1..1 with sailSideW_)
			{
				const SbBones* bs[2] = { &sbMainB_, &sbJibB_ };
				const float hoist[2] = { sbHoistMain_, sbHoistJib_ };
				for (int k = 0; k < 2; ++k) {
					auto* sh = bs[k]->shape;
					if (!sh) continue;
					const bool show = hoist[k] >= 0.98f;
					if (sh->GetAppCulled() == show) sh->SetAppCulled(!show);
				}
				SbBend(sbMainB_, side, wsp, luffK, false);
				SbBend(sbJibB_, side, wsp, luffK, true);
				// v1.8.4: the per-side sails keep their mesh as made and only flap around it (the lee one is shown above)
				// v1.8.5: the change of side is a transition, not a swap: the lee sail empties (its belly eases to flat), then
				// the other side's sail takes over at flat and fills out (side -1 port .. +1 starboard, eased)
				const float e = std::abs(side), fillK = e * e * (3.0f - 2.0f * e);
				const bool stbdNow = side >= 0.0f;
				for (int k = 0; k < 4; ++k) {
					auto* sh = sbSideB_[k].shape;
					if (!sh) continue;
					const bool mine = (k & 1) ? !stbdNow : stbdNow;
					// v1.8.29: the main reefed too; v1.8.30: both shown while they come in (the main gathered down onto the boom, the
					// jib rolled round its stay) until the rolled bundles take over
					const bool show = mine && (k < 2 ? sbHoistMain_ > 0.06f : SbJibFadeA(sbHoistJib_) > 0.001f);  // v1.8.32b: the jib gone halfway in
					if (sh->GetAppCulled() == show) sh->SetAppCulled(!show);
					SbFlap(sbSideB_[k], (k & 1) ? -1.0f : 1.0f, wsp, luffK, k >= 2, k >= 2 ? fillK * (1.0f - SbJibStowK(sbHoistJib_)) : fillK, k < 2 ? sbHoistMain_ : sbHoistJib_);  // v1.8.30h: the jib's belly empties as it comes in
					if (k >= 2) SbJibStow(sbSideB_[k], sbHoistJib_);  // v1.8.30d: the jib brought down onto the bowsprit
				}
			}
			// v1.8.32b: the jib fades out as it comes in (gone halfway), its hanging bundle fading in over the same stretch.
			// v1.8.32d: only shapes with textures of their own are faded (the game shares one material between shapes with the
			// same textures: fading the rope wraps faded every rope): the jib (BoatFabric02_jib_d) and its bundle
			// (BoatFabric02_jibfurl_d). The wraps and the clew ring are only shown or hidden.
			{
				const float a = SbJibFadeA(sbHoistJib_);
				if (a != sbJibAShown_ || (a > 0.0f && a < 1.0f)) {
					for (int k = 2; k < 4; ++k)
						if (sbSideB_[k].shape) SetAlphaTree(sbSideB_[k].shape, a);
					if (sbFurl_[4]) SetAlphaTree(sbFurl_[4], 1.0f - a);
					sbJibAShown_ = a;
				}
				if (auto* wrap = sbFurl_[5]) {
					const bool on = a < 0.5f;
					if (wrap->GetAppCulled() == on) wrap->SetAppCulled(!on);
				}
			}
			// v1.8.7: the jib sheet: from the jib's clew (the corner by the mast, swinging with the jib) to the mast (v1.8.27e;
			// was a fairlead on the gunwale); shown with the jib, as its clew ring
			// v1.8.29: the boom's and gaff's jaw knots and iron as made with the boom to port, mirrored with it to starboard
			{
				if (sbJawOn_ != visual_.get()) {
					sbJawOn_ = visual_.get();
					const char* jn[4] = { "SB_JawRopeS", "SB_JawRopeP", "SB_JawIronS", "SB_JawIronP" };
					for (int i = 0; i < 4; ++i) sbJaw_[i] = FindShape(visual_.get(), jn[i]);
				}
				const bool stbd = sailSideW_ >= 0.5f;
				for (int i = 0; i < 4; ++i) {
					if (!sbJaw_[i]) continue;
					const bool show = (i & 1) ? stbd : !stbd;
					if (sbJaw_[i]->GetAppCulled() == show) sbJaw_[i]->SetAppCulled(!show);
				}
			}
			if (sbRing_) {
				const bool show = SbJibFadeA(sbHoistJib_) > 0.001f;  // v1.8.32b: fading with the jib
				if (sbRing_->GetAppCulled() == show) sbRing_->SetAppCulled(!show);
			}
			if (sbSheet_ && sbJib_) {
				const bool show = sbHoistJib_ > 0.15f;  // v1.8.30e: in with the jib, then tied up in its bundle
				if (sbSheet_->GetAppCulled() == show) sbSheet_->SetAppCulled(!show);
				if (show) {
					const auto& jl = sbJib_->local;
					const RE::NiPoint3 cl{ 0.0f, sbClew_->local.translate.y, sbClew_->local.translate.z };
					const RE::NiPoint3 clew = jl.rotate * cl + jl.translate;
					sbSheetB_[1]->local.translate = clew;
					// v1.8.27e: tied to the mast (was a fairlead on the gunwale); the clew end through the iron ring (SB_JibClewRing)
					// v1.8.30h: tied to the iron eye on the mast's forward side above the boom (Miguel's picture; was the
					// lashing higher up at z 276)
					sbSheetB_[0]->local.translate = RE::NiPoint3{ -2.6f, 97.0f, 187.0f };
					{
						const float e = SbJibStowK(sbHoistJib_);  // v1.8.30d: coming in with the jib to rest
						const RE::NiPoint3 foot = sbHang_ ? sbHang_->local.translate + sbHang_->local.rotate * RE::NiPoint3{ 0.0f, 0.0f, -kJibHangLen } : clew;
						sbSheetB_[1]->local.translate = clew + (foot - clew) * e;
					}
					// v1.8.32: the sheet is a round rope bound along +z (0..100): both its bones turned onto its line, so its
					// section is always square to it (moved only, it went flat)
					{
						const RE::NiMatrix3 R = TurnOnto(RE::NiPoint3{ 0.0f, 0.0f, 1.0f }, sbSheetB_[1]->local.translate - sbSheetB_[0]->local.translate);
						sbSheetB_[0]->local.rotate = R;
						sbSheetB_[1]->local.rotate = R;
					}
					RE::NiUpdateData sd{};
					sbSheetBox_->Update(sd);
				}
			}
			// v1.8.32: the jib's tack line and its hook follow the sail's tack (its corner on the forestay) a little: the hook
			// carried with the corner and turned with the jib, the line swung about its far end on the bowsprit (-1.8 406 116)
			if ((sbStay_ || sbTackIron_) && sbJib_) {
				const SbBones& jb = sbSideB_[sailSideW_ >= 0.5f ? 2 : 3];
				if (jb.shape && !jb.bone.empty() && !jb.bind.empty() && jb.bone[0]) {
					const auto& jl = sbJib_->local;
					const RE::NiPoint3 rest = sbJibR0_ * jb.bind[0] + jl.translate;
					const RE::NiPoint3 now = jl.rotate * jb.bone[0]->local.translate + jl.translate;
					RE::NiUpdateData ud{};
					ud.flags.set(RE::NiUpdateData::Flag::kDirty);
					if (sbTackIron_) {
						const RE::NiMatrix3 Rd = jl.rotate * sbJibR0_.Transpose();
						sbTackIron_->local.rotate = Rd;
						sbTackIron_->local.translate = now - Rd * rest;
						sbTackIron_->Update(ud);
					}
					if (sbStay_) {
						const RE::NiPoint3 A{ -1.8f, 406.0f, 116.0f };
						const RE::NiMatrix3 Rs = TurnOnto(rest - A, now - A);
						sbStay_->local.rotate = Rs;
						sbStay_->local.translate = A - Rs * A;
						sbStay_->Update(ud);
					}
				}
			}
			RE::NiUpdateData data{};
			data.flags.set(RE::NiUpdateData::Flag::kDirty);
			sbMain_->Update(data);
			if (sbJib_) sbJib_->Update(data);
		}
		// ---- v1.8.29: the sailing boat's anchor (src\Anchor_v1829.md) ----
		// SB_Anchor (the iron anchor), its line: SB_AnchorRope + SB_AnchorChain skinned to 31 bones (SB_AnchorBones):
		// 0..24 the rope from the cathead, 24..30 a short chain, 30 at the ring. The line is simulated in the world (verlet,
		// heavy and slow in the water, never through the sea bed); the bones are placed from it each frame.
		// Stowed: catted and fished on the starboard bow (the ring at the cathead, the shank along the hull). At the helm
		// Jump drops it (too deep: a note, nothing happens); it falls, the rope paying out, and once it lies on the bottom
		// the boat is held: after a moment it stops (no sail drive), and it never drifts further than the line. Jump again
		// hauls it up and stows it.
		static constexpr int kAnN = 31, kAnC0 = 24;
		static constexpr float kAnChain = 40.0f;      // the chain (model units)
		static constexpr float kAnMaxRope = 560.0f;   // v1.8.33: the boat is held once this much is out (model units: 700 in game at 1.25, 10 m)
		static constexpr float kAnLineMax = 4200.0f;  // v1.8.33: the whole line: it goes on down to the bottom (75 m)
		static inline const RE::NiPoint3 kAnCat{ 39.0f, 232.0f, 98.0f };      // the cathead: starboard bow, over the gunwale
		static inline const RE::NiPoint3 kAnStowRing{ 41.0f, 230.0f, 93.0f };  // stowed: the ring just under it
		static inline const RE::NiPoint3 kAnStowCrown{ 63.0f, 176.0f, 76.0f }; // and the crown aft along the hull's side
		RE::NiAVObject* anOn_{ nullptr };
		RE::NiAVObject* anNode_{ nullptr };
		RE::NiAVObject* anRopeS_{ nullptr };
		RE::NiAVObject* anChainS_{ nullptr };
		RE::NiAVObject* anStowRope_{ nullptr };  // v1.8.39: SB_BowRopeStow, the cathead to the stowed ring (the line in)
		RE::NiAVObject* anBone_[kAnN]{};
		RE::NiAVObject* anBox_{ nullptr };
		int anState_{ 0 };  // 0 stowed, 2 falling, 3 on the bottom (holding), 4 hauling up
		bool anInit_{ false };
		RE::NiPoint3 anP_[kAnN]{}, anQ_[kAnN]{};  // world, this step and the last
		float anRope_{ 0.0f };   // rope paid out (world units)
		float anBed_{ 0.0f };    // the sea bed under the drop (world z)
		float anHold_{ 0.0f };   // seconds on the bottom
		float anDownT_{ 0.0f };  // v1.8.35: seconds it has been going down (hauled up in as long)
		float anHaulRate_{ 0.0f };  // v1.8.35: rope a second, hauling
		float anStow_{ 1.0f };   // 0..1 into the stowed pose (eased as it is brought in)
		RE::NiPoint3 anHoldRing_{};
		RE::NiPoint3 anLieDir_{ 0.0f, 1.0f, 0.0f };
		RE::NiPoint3 anZ_{};       // v1.8.29d: the hanging anchor's shank, eased
		RE::NiMatrix3 anLastR_{};  // v1.8.39: its turn last frame, in the boat's frame
		bool anHaveR_{ false };
		RE::NiPoint3 anRingL_{};    // v1.8.39b: the last of the haul: the ring carried with the boat (no more dangling)
		bool anRingLOn_{ false };
		bool anOnBed_{ false };    // v1.8.29e: held on the bottom (else hanging at the end of its line)
		float anDrive_{ 1.0f };    // v1.8.29d: how much the sail may drive (0 at anchor, slowly back once it is up)
		double anKeyAt_{ 0.0 };
		static RE::NiMatrix3 AnFrame(const RE::NiPoint3& a_z, const RE::NiPoint3& a_xHint)
		{
			// a rotation whose local +z is a_z and local +x as near a_hint as it can (columns: the local axes in the parent)
			RE::NiPoint3 z = a_z; const float zl = z.Length(); z = zl > 1e-5f ? z / zl : RE::NiPoint3{ 0.0f, 0.0f, 1.0f };
			RE::NiPoint3 x = a_xHint - z * a_xHint.Dot(z);
			if (x.Length() < 1e-3f) { x = RE::NiPoint3{ 1.0f, 0.0f, 0.0f } - z * z.x; if (x.Length() < 1e-3f) x = RE::NiPoint3{ 0.0f, 1.0f, 0.0f } - z * z.y; }
			x = x / x.Length();
			const RE::NiPoint3 y = z.Cross(x);
			return MakeMat(x.x, y.x, z.x, x.y, y.y, z.y, x.z, y.z, z.z);
		}
	public:
		bool AnchorKeys() const { return seated_ && Kind() == kKindSail && anNode_ != nullptr; }  // Jump: the anchor, Sprint: the sail
		bool AnchorHolding() const { return anState_ == 3 && anHold_ > 1.2f; }
		void OnAnchorKey()
		{
			if (!AnchorKeys() || !visual_) return;
			const double now = NowS();
			if (now - anKeyAt_ < 0.6) return;
			anKeyAt_ = now;
			if (anState_ == 0) {
				if (!haveWater_) return;
				const auto& W = visual_->world;
				const RE::NiPoint3 cat = W * kAnCat;
				const float sc = W.scale;
				// v1.8.29e: it holds at the end of its 10 m of line whether it reaches the bottom or not; a shallower bottom stops it there
				// v1.8.33: it goes on down to the bottom (75 m of line), the boat held once 10 m are out
				const RE::NiPoint3 from{ cat.x + 10.0f, cat.y, waterZ_ - 5.0f }, to{ cat.x + 10.0f, cat.y, waterZ_ - (kAnLineMax + kAnChain) * sc * 1.2f };
				float frac = 1.0f; RE::COL_LAYER layer{};
				anBed_ = FeelSweep(from, to, frac, layer) ? from.z + (to.z - from.z) * frac : -1.0e9f;
				anState_ = 2; anHold_ = 0.0f; anDownT_ = 0.0f;
				if (anBed_ > -1.0e8f) logger::info("Anchor: let go, the bottom {:.0f} under the water", waterZ_ - anBed_);
				else logger::info("Anchor: let go, no bottom within its line: it holds hanging at the end of it");
			} else if (anState_ == 2 || anState_ == 3) {
				// v1.8.35: hauled up in as long as it took to go down (let go by mistake, it is back as quickly)
				anState_ = 4;
				anHaulRate_ = anRope_ / std::clamp(anDownT_, 0.6f, 30.0f);
				logger::info("Anchor: hauling up ({:.0f} of rope out, {:.1f} s down: {:.0f} a second)", anRope_, anDownT_, anHaulRate_);
			} else if (anState_ == 4) {
				anState_ = 2; anHold_ = 0.0f;
				anDownT_ = anHaulRate_ > 1.0f ? anRope_ / anHaulRate_ : 0.0f;  // as far down as it still is
				logger::info("Anchor: let go again");
			}
		}
	private:
		void TendAnchor(float a_dt)
		{
			if (Kind() != kKindSail || !visual_) { anOn_ = anNode_ = nullptr; anInit_ = false; anState_ = 0; anRope_ = 0.0f; return; }
			if (anOn_ != visual_.get()) {
				anOn_ = visual_.get(); anInit_ = false; anHaveR_ = false;
				anNode_ = FindShape(visual_.get(), "SB_Anchor");
				anRopeS_ = FindShape(visual_.get(), "SB_AnchorRope");
				anChainS_ = FindShape(visual_.get(), "SB_AnchorChain");
				anBox_ = FindShape(visual_.get(), "SB_AnchorBones");
				anStowRope_ = FindShape(visual_.get(), "SB_BowRopeStow");
				bool ok = anNode_ && anRopeS_ && anChainS_ && anBox_;
				for (int i = 0; i < kAnN && ok; ++i) ok = (anBone_[i] = FindShape(visual_.get(), std::format("SB_AnchorBone{}", i))) != nullptr;
				logger::info("Anchor: {}", ok ? "found (anchor, rope, chain, 31 bones)" : "not in this model");
				if (!ok) anNode_ = nullptr;
			}
			if (!anNode_) return;
			const float dt = std::clamp(a_dt, 0.0f, 0.05f);
			const auto& W = visual_->world;
			const RE::NiTransform inv = W.Invert();
			const float sc = W.scale > 0.01f ? W.scale : 1.0f;
			const RE::NiPoint3 cat = W * kAnCat;
			const RE::NiPoint3 stowRing = W * kAnStowRing;
			const float chainW = kAnChain * sc, holdW = kAnMaxRope * sc, maxW = kAnLineMax * sc;  // v1.8.33: held at 10 m, the line runs on
			const float water = haveWater_ ? waterZ_ : -1.0e9f;
			if (!anInit_) {
				anInit_ = true;
				const RE::NiPoint3 end = anState_ == 3 ? anHoldRing_ : stowRing;
				for (int i = 0; i < kAnN; ++i) {
					const float f = static_cast<float>(i) / static_cast<float>(kAnN - 1);
					anP_[i] = anQ_[i] = cat + (end - cat) * f;
				}
				if (anState_ == 0) anRope_ = 0.0f;
			}
			RE::NiPoint3& ring = anP_[kAnN - 1];
			// the rope: paid out as the anchor falls, kept a little slack on the bottom, hauled in
			const float span = (ring - cat).Length();
			if (anState_ == 2) {
				anDownT_ += dt;
				// v1.8.29d/e: the first 10 m paid out freely as it falls (the rope uncoiling in the water); v1.8.35: past that only as
				// much as it needs (it kept paying out 450 a second, far more line than the depth)
				if (anRope_ < holdW) anRope_ = (std::min)((std::max)(anRope_ + 450.0f * sc * dt, span * 1.08f - chainW), holdW);
				else anRope_ = (std::max)(anRope_, span * 1.02f - chainW);
				anRope_ = std::clamp(anRope_, 0.0f, maxW);
			}
			else if (anState_ == 3) anRope_ = std::clamp((std::max)(anRope_, span * 1.06f - chainW), 0.0f, maxW);
			else if (anState_ == 4) {
				// v1.8.29e: hauled in steadily (10 m in about 5 s); v1.8.33: the last few feet slowly, easing it up into its place
				// v1.8.39b: the last 240 (about 4 m) far slower still: down to a tenth, creeping the last bit in
				const float e = std::clamp(anRope_ / (240.0f * sc), 0.0f, 1.0f), nearK = e * e * (3.0f - 2.0f * e);
				const float rate = anHaulRate_ > 1.0f ? (std::max)(anHaulRate_, 60.0f * sc) : 150.0f * sc;  // v1.8.35: as fast as it went down
				anRope_ = (std::max)(0.0f, anRope_ - (std::max)(rate * (0.1f + 0.9f * nearK), 10.0f * sc) * dt);
			}
			else anRope_ = 0.0f;
			if (anState_ == 4 && anRope_ <= 0.0f && span < chainW + 6.0f * sc) { anState_ = 0; anStow_ = 0.0f; logger::info("Anchor: stowed"); }
			anStow_ = anState_ == 0 ? (std::min)(1.0f, anStow_ + dt / 1.8f) : 0.0f;  // v1.8.33: settling into its place slowly (was 0.6 s)
			const float ropeSeg = anRope_ / static_cast<float>(kAnC0), chainSeg = chainW / static_cast<float>(kAnN - 1 - kAnC0);
			// v1.8.39b: the last of the haul (under 240 of rope) the ring no longer dangles on the light line: it is carried with
			// the boat, eased slowly from where it hung up to its stowed place under the cathead
			const float kin0 = anState_ == 4 ? 1.0f - std::clamp(anRope_ / (240.0f * sc), 0.0f, 1.0f) : 0.0f;
			const bool ringKin = kin0 > 0.0f;
			RE::NiPoint3 ringKinW = stowRing;
			if (ringKin) {
				if (!anRingLOn_) { anRingL_ = inv * ring; anRingLOn_ = true; }
				const float k = kin0 * kin0 * (3.0f - 2.0f * kin0);
				const RE::NiPoint3 hangL = inv * (cat + RE::NiPoint3{ 0.0f, 0.0f, -(anRope_ + chainW) * 0.95f });
				const RE::NiPoint3 tgt = hangL + (kAnStowRing - hangL) * k;
				anRingL_ = anRingL_ + (tgt - anRingL_) * (1.0f - std::exp(-1.2f * dt));
				ringKinW = W * anRingL_;
			} else anRingLOn_ = false;
			const bool ringPinned = anState_ == 3 || (anState_ == 0 && anStow_ >= 1.0f) || ringKin;
			// the line: verlet, two steps
			for (int sub = 0; sub < 2; ++sub) {
				const float h = dt * 0.5f;
				for (int i = 1; i < kAnN; ++i) {
					auto& p = anP_[i]; auto& q = anQ_[i];
					RE::NiPoint3 v = p - q;
					const bool wet = p.z < water;
					// v1.8.29d: heavier, less dangly; v1.8.29e: the rope free to bend and drift (the chain and anchor still damped)
					const bool iron = i >= kAnC0;
					v = v * std::exp(-(iron ? (wet ? 6.0f : 2.5f) : (wet ? 2.0f : 0.6f)) * h);
					float g = 690.0f * sc;  // 9.8 m/s2
					if (wet) g = i == kAnN - 1 ? 380.0f * sc : (i >= kAnC0 ? 220.0f * sc : 110.0f * sc);  // the iron sinks, the rope slowly (v1.8.29e: it sags)
					if (i == kAnN - 1 && wet) { const float vl = v.Length(), vmax = 300.0f * sc * h; if (vl > vmax) v = v * (vmax / vl); }  // its sinking pace
					q = p;
					p = p + v + RE::NiPoint3{ 0.0f, 0.0f, -g * h * h };
				}
				anP_[0] = cat;
				if (ringPinned) { ring = anState_ == 3 ? anHoldRing_ : ringKin ? ringKinW : stowRing; anQ_[kAnN - 1] = ring; }
				else if (anState_ == 0) ring = ring + (stowRing - ring) * (1.0f - std::exp(-(1.2f + 4.0f * anStow_ * anStow_) * h));  // brought in to the stowed spot, gently (v1.8.33)
				for (int it = 0; it < 8; ++it) {  // v1.8.29e: fewer passes: a softer, bendier rope (was 20)
					for (int i = 0; i < kAnN - 1; ++i) {
						auto& a = anP_[i]; auto& b = anP_[i + 1];
						const float rest = i < kAnC0 ? ropeSeg : chainSeg;
						RE::NiPoint3 dd = b - a; const float l = dd.Length();
						if (l < 1e-4f) continue;
						const float wa = i == 0 ? 0.0f : 1.0f;
						const float wb = (i + 1 == kAnN - 1) ? (ringPinned ? 0.0f : 0.25f) : 1.0f;
						if (wa + wb <= 0.0f) continue;
						const RE::NiPoint3 corr = dd * ((l - rest) / l / (wa + wb));
						a = a + corr * wa; b = b - corr * wb;
					}
					if (anState_ >= 2) for (int i = 1; i < kAnN; ++i) if (anP_[i].z < anBed_ + 1.0f) anP_[i].z = anBed_ + 1.0f;  // never through the bottom
				}
			}
			// on the bottom, or at the end of its line (v1.8.29e): it holds there
			const bool onBed = ring.z <= anBed_ + 15.0f * sc;
			if (anState_ == 2 && (onBed || (anRope_ >= maxW - 0.5f && span >= (maxW + chainW) * 0.9f))) {
				anState_ = 3; anOnBed_ = onBed;  // v1.8.33: the hold already running from 10 m on keeps its time
				anHoldRing_ = onBed ? RE::NiPoint3{ ring.x, ring.y, anBed_ + 15.0f * sc } : ring;
				RE::NiPoint3 lie = cat - ring; lie.z = 0.0f;
				anLieDir_ = lie.Length() > 1.0f ? lie / lie.Length() : RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
				logger::info("Anchor: {}, {:.0f} of rope out", onBed ? "on the bottom" : "at the end of its line", anRope_);
			}
			// v1.8.33: the boat is held once 10 m of line are out, the anchor going on down to the bottom
			const bool held = anState_ == 3 || (anState_ == 2 && anRope_ >= holdW - 0.5f);
			if (held) {
				anHold_ += dt;
				if (anState_ == 3) {
					// on the bottom: never further than the line, and once it bites (a moment after it lands) she comes to a stop
					RE::NiPoint3 d = cat - anHoldRing_; d.z = 0.0f;
					const float L = anRope_ + chainW;
					const float reach = std::sqrt((std::max)(0.0f, L * L - (cat.z - anHoldRing_.z) * (cat.z - anHoldRing_.z)));
					const float dl = d.Length();
					if (dl > reach && dl > 1.0f) { pos_.x -= d.x / dl * (dl - reach); pos_.y -= d.y / dl * (dl - reach); speed_ *= 0.9f; }
				}
				if (anHold_ > 1.2f) {
					speed_ *= std::exp(-1.8f * dt);
					if (std::abs(speed_) < 3.0f) speed_ = 0.0f;
					sailLee_ *= std::exp(-2.0f * dt);
					turn_ *= std::exp(-1.2f * dt);
				}
			}
			// the anchor itself, in the world
			RE::NiMatrix3 R;
			RE::NiPoint3 crown;
			const RE::NiPoint3 up{ 0.0f, 0.0f, 1.0f };
			if (anState_ == 3 && anOnBed_) {
				// lying on the bottom, the shank toward the boat and a little up, the lower fluke dug in
				const RE::NiPoint3 z = anLieDir_ * 0.978f + up * 0.208f;
				R = AnFrame(z, up);
				crown = anHoldRing_ - z * (62.0f * sc);
				crown.z = (std::max)(crown.z, anBed_ + 2.0f * sc);
			} else {
				RE::NiPoint3 z = anP_[kAnN - 2] - ring;  // hanging: the shank up the line
				if (z.Length() < 0.5f) z = up;
				z = z / z.Length();
				// v1.8.29d: a heavy anchor turns slowly with its line (it swung about with every twitch of the chain)
				// v1.8.39: the last of the haul (under about 2 m of rope) it settles: its shank follows the line more and more
				// slowly and it turns, the short way and smoothly, toward its stowed hang (it spun round as it came in)
				const float s0 = kin0;  // v1.8.39b: over the last 240
				const float settle = s0 * s0 * (3.0f - 2.0f * s0);
				if (anZ_.Length() < 0.5f) anZ_ = z;
				anZ_ = anZ_ + (z - anZ_) * (1.0f - std::exp(-3.0f * (1.0f - 0.8f * settle) * dt));
				z = anZ_.Length() > 1e-3f ? anZ_ / anZ_.Length() : up;
				const RE::NiMatrix3 hang = AnFrame(z, RE::NiPoint3{ W.rotate.entry[0][0], W.rotate.entry[1][0], W.rotate.entry[2][0] });
				// stowed: catted and fished along the starboard bow (the ring at the cathead, the arms up and down)
				const RE::NiPoint3 sr = W * kAnStowRing, sc0 = W * kAnStowCrown;
				const RE::NiMatrix3 stow = AnFrame(sr - sc0, RE::NiPoint3{ W.rotate.entry[0][2], W.rotate.entry[1][2], W.rotate.entry[2][2] });
				const RE::NiMatrix3 last = W.rotate * anLastR_;
				if (anState_ == 0) {
					// from wherever it was, eased into its place over the 1.8 s (slowly at first): never a lerp of two far-apart turns
					// v1.8.39b: at most 20 deg a second, and on until it is there (no snap at the end of the 1.8 s)
					R = (!anHaveR_ || TurnBetween(last, stow) < 0.004f) ? stow : SlerpCapM(last, stow, 1.0f - std::exp(-(0.8f + 2.0f * anStow_) * dt), 0.35f * dt);
				} else if (anState_ == 4) {
					const RE::NiMatrix3 want = settle > 0.0f ? SlerpM(hang, stow, 0.9f * settle) : hang;
					// v1.8.39b: never a quick turn: 70 deg a second at most while it comes up, 20 over the last of it
					R = anHaveR_ ? SlerpCapM(last, want, 1.0f - std::exp(-(4.0f - 3.0f * settle) * dt), (1.2f - 0.85f * settle) * dt) : want;
				} else R = hang;
				crown = ring - RE::NiPoint3{ R.entry[0][2], R.entry[1][2], R.entry[2][2] } * (62.0f * sc);
			}
			anNode_->local.rotate = inv.rotate * R;
			anLastR_ = inv.rotate * R;
			anHaveR_ = true;
			anNode_->local.translate = inv * crown;
			anNode_->local.scale = 1.0f;
			// the line's bones, in the model
			RE::NiPoint3 m[kAnN];
			for (int i = 0; i < kAnN; ++i) m[i] = inv * anP_[i];
			for (int i = 0; i < kAnN; ++i) {
				const RE::NiPoint3 dir = i < kAnN - 1 ? m[i + 1] - m[i] : m[i] - m[i - 1];
				anBone_[i]->local.translate = m[i];
				anBone_[i]->local.rotate = AnFrame(RE::NiPoint3{ -dir.x, -dir.y, -dir.z }, RE::NiPoint3{ 1.0f, 0.0f, 0.0f });  // the bind runs down -z
			}
			const bool ropeOut = anRope_ > 2.0f * sc;
			if (anRopeS_->GetAppCulled() == ropeOut) anRopeS_->SetAppCulled(!ropeOut);
			// v1.8.39: the line from the bow bitt is always there; stowed, its short drop from the cathead to the ring
			if (anStowRope_ && anStowRope_->GetAppCulled() != ropeOut) anStowRope_->SetAppCulled(ropeOut);
			// v1.8.29d: stowed, the chain is in (hidden): no loop of it swinging under the cathead
			const bool chainOut = !(anState_ == 0 && anStow_ >= 1.0f);
			if (anChainS_->GetAppCulled() == chainOut) anChainS_->SetAppCulled(!chainOut);
			// the sail: nothing at anchor, a little while it is hauled (it drags), then back slowly once it is up (12 s)
			{
				const float want = held ? 0.0f : anState_ == 4 ? 0.1f : anState_ == 2 ? 0.5f : 1.0f;  // v1.8.33: none once held
				anDrive_ = want < anDrive_ ? (std::max)(want, anDrive_ - dt * 2.0f) : (std::min)(want, anDrive_ + dt / 12.0f);
			}
			RE::NiUpdateData ud{};
			ud.flags.set(RE::NiUpdateData::Flag::kDirty);
			anNode_->Update(ud);
			anBox_->Update(ud);
		}
		void TendSail(float a_dt)
		{
			if (!visual_) { lsB_ = SbBones{}; lsOn_ = lsRig_ = lsSail_ = lsOars_ = nullptr; sailOn_ = sailNode_ = sailRig_ = nullptr; for (auto& c : cloth_) c = SailCloth{}; sbOn_ = sbMain_ = sbGaff_ = sbJib_ = sbHal_ = nullptr; for (auto& q : sbSails_) q = nullptr; sbMainB_ = sbJibB_ = SbBones{}; for (auto& q : sbSideB_) q = SbBones{}; sbClew_ = sbSheet_ = sbRing_ = nullptr; sbMainC_ = sbJibC_ = SbCloth{}; return; }
			SailLoad();
			if (Kind() == kKindRow) sail_ = 0;  // v1.8.21: a swap back to the rowboat brings no sail with it
			if (sailOn_ != visual_.get()) {
				sailOn_ = visual_.get();
				sailNode_ = FindShape(visual_.get(), "RowRim_Sail");
				sailRig_ = FindShape(visual_.get(), "RowRim_SailRig");
				const char* st[5] = { "RowRim_SailFurled", "RowRim_SailFull", "RowRim_SailReefed", "RowRim_SailFullP", "RowRim_SailReefedP" };
				for (int i = 0; i < 5; ++i) { sailSt_[i] = FindShape(visual_.get(), st[i]); sailStShown_[i] = -1.0f; }
				for (int i = 0; i < kSheetN; ++i) sailSheet_[i] = FindShape(visual_.get(), std::format("RowRim_SailSheet{}", i));
				FindCloth();
				logger::info("Sail: {} in the hull", sailNode_ && sailRig_ && sailSt_[0] && sailSt_[1] && sailSt_[2] ? "mast and sail found" : "no mast (an older hull)");
				if (!sailRig_ || !sailSt_[0] || !sailSt_[1] || !sailSt_[2] || !sailSt_[3] || !sailSt_[4]) sailNode_ = nullptr;
				mastA_ = -1.0f;  // re-applied below
			}
			if (!sailNode_) { TendSailBoat(a_dt); return; }  // v1.7.90: no lug sail: the sailing boat's own rig, if this is it
			const float dt = std::clamp(a_dt, 0.0f, 0.1f);
			const float k = dt / 1.2f;
			const float mastBefore = mastA_;
			mastA_ = std::clamp((mastA_ < 0.0f ? (sail_ > 0 ? 1.0f : 0.0f) : mastA_) + (sail_ > 0 ? k : -k), 0.0f, 1.0f);
			bool fading = mastA_ != mastBefore || (mastA_ > 0.0f && mastA_ < 1.0f);
			for (int i = 0; i < 3; ++i) {
				const float b = sailStA_[i];
				sailStA_[i] = std::clamp(sailStA_[i] + (sail_ == i + 1 ? k : -k), 0.0f, 1.0f);
				fading = fading || b != sailStA_[i] || (sailStA_[i] > 0.0f && sailStA_[i] < 1.0f);
			}
			if (mastA_ <= 0.0f) {
				if (mastBefore > 0.0f || mastBefore < 0.0f) SetAlphaTree(sailNode_, 1.0f);
				if (!sailNode_->GetAppCulled()) sailNode_->SetAppCulled(true);
				return;
			}
			if (sailNode_->GetAppCulled()) sailNode_->SetAppCulled(false);
			auto ease = [](float a) { return a >= 1.0f ? 1.0f : a * a * (3.0f - 2.0f * a); };
			// v1.7.78: the starboard or the port set by where the boom is (crossfaded as it crosses the centre)
			{
				const float want = sailYaw_ > 0.03f ? 1.0f : sailYaw_ < -0.03f ? 0.0f : sailSideW_;
				const float b = sailSideW_;
				sailSideW_ = std::clamp(sailSideW_ + (want > sailSideW_ ? 1.0f : want < sailSideW_ ? -1.0f : 0.0f) * dt / 0.35f, 0.0f, 1.0f);
				if (b != sailSideW_ && (sailSideW_ == 0.0f || sailSideW_ == 1.0f)) logger::info("Sail: the cloth now on the {} side", sailSideW_ > 0.5f ? "starboard" : "port");
			}
			if (fading) SetAlphaTree(sailNode_, ease(mastA_));
			{
				const float m = ease(mastA_);
				const float a5[5] = { m * ease(sailStA_[0]), m * ease(sailStA_[1]) * sailSideW_, m * ease(sailStA_[2]) * sailSideW_,
					m * ease(sailStA_[1]) * (1.0f - sailSideW_), m * ease(sailStA_[2]) * (1.0f - sailSideW_) };
				for (int i = 0; i < 5; ++i) {
					if (!fading && a5[i] == sailStShown_[i]) continue;
					sailStShown_[i] = a5[i];
					sailSt_[i]->SetAppCulled(a5[i] <= 0.0f);
					if (a5[i] > 0.0f) SetAlphaTree(sailSt_[i], a5[i]);
				}
			}
			// the wind the boat feels (SailWind)
			float phi = 0.0f, wsp = 0.0f;
			SailWind(phi, wsp);
			float target = 0.0f;
			const bool set = sail_ >= 2;
			// v1.7.77: the boom keeps its side through small wobbles round dead downwind: it gybes only once the wind is 25 deg
			// past dead astern on the other quarter (the flip back and forth as you steered), and then swings across slowly
			if (set) {
				const float side = phi >= 0.0f ? 1.0f : -1.0f;
				if (!sailWasSet_) sailSide_ = side;  // v1.7.78: raised: straight to the lee side (it started on starboard)
				if (side != sailSide_ && std::abs(phi) > kPi - 0.44f) target = sailSide_ * 1.5708f;  // by the lee: held
				else {
					if (side != sailSide_) logger::info("Sail: {} (wind {:.0f} deg off the stern), boom to {}", std::abs(phi) > 1.5708f ? "gybe" : "tack", phi * 57.2958f, side > 0.0f ? "starboard" : "port");
					sailSide_ = side;
					target = side * (std::min)(std::abs(phi) * 0.5f, 1.5708f);  // v1.7.79: out to 90 deg dead downwind
				}
			}
			sailWasSet_ = set;
			// luffing: head to wind the cloth shivers (a little to and fro about the centre)
			const bool luff = set && std::abs(phi) < 0.5f;
			sailShiver_ += dt * (luff ? 11.0f : 0.0f);
			LoadSailTuning();
			const float shiver = luff ? 0.05f * sailFlap_ * (0.4f + wsp) * std::sin(sailShiver_) * (1.0f - std::abs(phi) / 0.5f) : 0.0f;
			sailYawVel_ += ((target - sailYaw_) * 5.0f - sailYawVel_ * 2.2f) * dt;  // a pendulum: lags, overshoots a little
			sailYawVel_ = std::clamp(sailYawVel_, -1.1f, 1.1f);  // v1.7.77: no faster than about 60 deg/s
			sailYaw_ = std::clamp(sailYaw_ + sailYawVel_ * dt, -1.5708f, 1.5708f);
			sailRig_->local.rotate = RzM(-(sailYaw_ + shiver));  // + swings the boom to starboard
			MoveCloth(dt, phi, wsp, set);  // v1.7.83
			// v1.7.75: the sheet from the boom's end to the cleat by the helm, a gentle sag, laid as pieces end to end (each
			// overlaps the next: it never stretches or gaps). Sail node space (origin the mast step).
			{
				const RE::NiPoint3 clew = sailRig_->local.rotate * RE::NiPoint3{ 7.5f, -150.0f, 189.5f };
				const RE::NiPoint3 cleat{ 60.0f, -190.0f - kSailY, 75.0f };
				const RE::NiPoint3 span = cleat - clew;
				const float sag = 4.0f + 0.015f * span.Length();
				auto at = [&](float t) { return clew + span * t - RE::NiPoint3{ 0.0f, 0.0f, sag * 4.0f * t * (1.0f - t) }; };
				for (int i = 0; i < kSheetN; ++i) {
					auto* piece = sailSheet_[i];
					if (!piece) continue;
					const RE::NiPoint3 p0 = at(static_cast<float>(i) / kSheetN), p1 = at(static_cast<float>(i + 1) / kSheetN);
					RE::NiPoint3 y = p1 - p0;
					const float l = y.Length();
					y = l > 0.001f ? y / l : RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
					RE::NiPoint3 x{ y.y, -y.x, 0.0f };  // y x up
					const float lx = x.Length();
					x = lx > 0.001f ? x / lx : RE::NiPoint3{ 1.0f, 0.0f, 0.0f };
					const RE::NiPoint3 z{ x.y * y.z - x.z * y.y, x.z * y.x - x.x * y.z, x.x * y.y - x.y * y.x };
					auto& m = piece->local.rotate;
					m.entry[0][0] = x.x; m.entry[0][1] = y.x; m.entry[0][2] = z.x;
					m.entry[1][0] = x.y; m.entry[1][1] = y.y; m.entry[1][2] = z.y;
					m.entry[2][0] = x.z; m.entry[2][1] = y.z; m.entry[2][2] = z.z;
					piece->local.translate = (p0 + p1) * 0.5f;
				}
			}
			RE::NiUpdateData data{};
			data.flags.set(RE::NiUpdateData::Flag::kDirty);
			sailNode_->Update(data);
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
			s_boardPickKind = -1;  // v1.8.26: set to the boat's own again
			benchModel_ = benchModel->GetModel();
			for (int k = kPickSteer; k <= kPickSail; ++k) {
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
			benchScale_[kPickSail] = std::clamp(28.0f / len, 0.1f, 3.0f);      // v1.7.73: round the mast, chest high
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
		// v1.8.8b: the Board prompt's proxy takes the boat's own hull shape (its collision model, a hair bigger) and name: the
		// rowboat-shaped one sat inside the longship and the crosshair hit the longship's own hull first
		static inline int s_boardPickKind = -1;  // v1.8.26: the prompt's base is shared by the three boats
		std::string boardName_;
		void BoardPickFor(RE::TESObjectREFR* a_hull)
		{
			auto& pk = picks_[kPickBoard];
			if (!pk.base || s_boardPickKind == Kind()) return;
			auto* hb = a_hull ? a_hull->GetBaseObject() : nullptr;
			auto* hm = hb ? hb->As<RE::TESModel>() : nullptr;
			if (!hm || !hm->GetModel() || !*hm->GetModel()) return;
			s_boardPickKind = Kind();
			pk.base->SetModel(hm->GetModel());
			pk.base->boundData = hb->boundData;
			boardName_ = Dims().label;
			pk.base->fullName = boardName_.c_str();
			if (pk.h) DropPick(pk);
			logger::info("Pick: Board prompt is the {} ('{}')", Dims().label, hm->GetModel());
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
				else if (std::abs(speed_) < 5.0f && ((a_kind == kPickBoard ? std::hypot(a_at.x - a_p.placedAt.x, a_at.y - a_p.placedAt.y) > 8.0f : (a_at - a_p.placedAt).Length() > 30.0f) || PickStale(ref.get(), a_at))) {
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
				// v1.8.35: the Board prompt (the whole hull) re-placed once carried more than 8: its collision could stay where it
				// was placed, so looking at the hull found nothing
				// (across the water only: it bobs up and down on the waves all the time)
				const float moved = a_kind == kPickBoard ? std::hypot(a_at.x - a_p.placedAt.x, a_at.y - a_p.placedAt.y) : (a_at - a_p.placedAt).Length();
				if (moved > (a_kind == kPickBoard ? 8.0f : 30.0f) || PickStale(ref.get(), a_at)) { DropPick(a_p); return; }
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
			if (!IsActive()) {  // v1.8.26: only the boat in use shows prompts
				for (auto& pk : picks_) if (pk.h) DropPick(pk);
				return;
			}
			if (!picksInit_) InitPicks();
			if (!picksOk_) return;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* hull = Ref();
			const bool nearBoat = player && hull && havePose_ && std::hypot(player->GetPosition().x - pos_.x, player->GetPosition().y - pos_.y) < 1500.0f;
			if (!nearBoat) {
				for (auto& pk : picks_) if (pk.h) DropPick(pk);
				return;
			}
			if (seated_) {  // v1.7.24: no prompts while seated
				for (auto& pk : picks_) if (pk.h) DropPick(pk);
				return;
			}
			const float by = yaw_ + benchYawOff_;
			auto nearL = [&](const RE::NiPoint3& a_l) {  // v1.7.28: the gear prompts only within reach (90 across the deck)
				const RE::NiPoint3 l = PlayerLocal(player);
				return std::hypot(l.x - a_l.x, l.y - a_l.y) < 120.0f;  // v1.7.49: 120
			};
			if (picksSweep_) {
				picksSweep_ = false;
				int n = 0;
				if (auto* tes = RE::TES::GetSingleton()) {
					tes->ForEachReferenceInRange(player, 3000.0f, [&](RE::TESObjectREFR* a_ref) {
						if (a_ref && (a_ref->GetFormID() >> 24) == 0xFF && !a_ref->IsDeleted() && !IsOursAny(a_ref->GetFormID())) {
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
			BoardPickFor(hull);  // v1.8.8b
			TendPick(picks_[kPickBoard], kPickBoard, !PlayerAboard(), hull->GetPosition(), hy);
			TendPick(picks_[kPickSteer], kPickSteer, true, lastAt_ + lastRot_ * RE::NiPoint3{ seat_.x, seat_.y, seat_.z + 22.0f }, by);
			TendPick(picks_[kPickRow], kPickRow, Kind() == kKindRow, lastAt_ + lastRot_ * RE::NiPoint3{ rowSeat_.x, rowSeat_.y, rowSeat_.z + 24.0f }, by);  // v1.8.8 (v1.8.26: no oars on the sailing boats)
			if (picks_[kPickBedroll].base)  // v1.7.11: the roll tied under the bow thwart (model 0, 113.7, about 56)
				TendPick(picks_[kPickBedroll], kPickBedroll, Kind() == kKindRow && nearL(RE::NiPoint3{ 0.0f, 113.7f, 0.0f }), lastAt_ + lastRot_ * RE::NiPoint3{ 0.0f, 113.7f, 50.0f }, by);  // v1.7.12: also to pack it up (v1.8.26: the rowboat's only)
			if (picks_[kPickSail].base && sailNode_ && SailEnabled() && Kind() != kKindRow)  // v1.8.21: no mast prompt on the rowboat. v1.7.73 (v1.7.81: only with sailing enabled): the mast, chest high (the bedroll's prompt is down by the floor)
				TendPick(picks_[kPickSail], kPickSail, nearL(RE::NiPoint3{ 0.0f, kSailY, 0.0f }), lastAt_ + lastRot_ * RE::NiPoint3{ 0.0f, kSailY, 112.0f }, by);
		}
	public:

		void Delete(bool a_say)
		{
			if (seated_) StandUp("hull deleted");
			if (IsActive() && Crew::Lingering()) Crew::StandAll(Crew::g.pose);  // v1.7.28: still seated, waiting: up
			DropPicks();  // v1.7.9
			DropHeld();
			DropLamp();
			DropBedLamp();  // v1.7.34
			if (kind_ == kKindRow) Gear::RemoveAll("the boat deleted");  // v1.7.38 (v1.8.26: the rowboat's bed)
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
			body_.reset();
			needInit_ = false;
		}

		// ---- co-save (v0.3.4): which hull and carrier are ours, the boat pose and the seat ----
		static constexpr std::uint32_t kRecHull = 'HULL';
		static constexpr std::uint32_t kRecVersion = 4;  // v0.6.0: both seats; v1.1.0: paint and height; v1.8.8: which boat
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
			a_intfc->WriteRecordData(static_cast<std::int32_t>(Kind()));  // v1.8.8
			logger::info("Co-save: {} hull {:08X} carrier {:08X} saved", Dims().label, refId_, carrierId_);
		}
		// v1.3.0: one record (the co-save loop is LoadAll, which also reads the lake boats' records)
		// v1.8.26: one HULL record per boat out; each goes back to its own boat
		static void LoadRecordAll(SKSE::SerializationInterface* a_intfc, std::uint32_t type, std::uint32_t version)
		{
			if (type != kRecHull || version < 1 || version > 4) return;
			RE::FormID hull = 0, carrier = 0;
			float v[10]{};
			std::uint32_t flags = 0;
			a_intfc->ReadRecordData(hull);
			a_intfc->ReadRecordData(carrier);
			const int nf = version >= 2 ? 10 : 7;
			for (int i = 0; i < nf; ++i) a_intfc->ReadRecordData(v[i]);
			a_intfc->ReadRecordData(flags);
			std::int32_t pnt = Paint::kBlue;
			float height = 0.0f;
			if (version >= 3) {
				a_intfc->ReadRecordData(pnt);
				a_intfc->ReadRecordData(height);
			}
			int kind = kKindRow;
			if (version >= 4) {  // v1.8.8: which boat this is (before: the ini's model)
				std::int32_t k = kKindRow;
				a_intfc->ReadRecordData(k);
				kind = std::clamp(static_cast<int>(k), 0, kKindCount - 1);
			} else kind = IniKind();
			auto& b = Of(kind);
			if (version >= 3) {
				b.heightOff_ = height;
				b.paint_ = std::clamp(static_cast<int>(pnt), 0, Paint::kCount - 1);
			}
			b.LoadRecord(a_intfc, hull, carrier, v, flags, version);
		}
		void LoadRecord(SKSE::SerializationInterface* a_intfc, RE::FormID hull, RE::FormID carrier, const float* v, std::uint32_t flags, std::uint32_t version)
		{
			{
				rowLoaded_ = false;  // this boat's own settings
				sailSpeed_ = -1.0f;
				paintedRoot_ = nullptr;
				RE::FormID h = 0, c = 0;
				const bool okH = hull && a_intfc->ResolveFormID(hull, h);
				const bool okC = carrier && a_intfc->ResolveFormID(carrier, c);
				refId_ = okH ? h : 0;
				carrierId_ = okC ? c : 0;
				rebased_ = false;  // v1.8.66
				CarrierAudio::ids[kind_].store(carrierId_, std::memory_order_relaxed);
				pos_ = { v[0], v[1], v[2] };
				yaw_ = v[3];
				if (version >= 2) {
					seat_ = { v[4], v[5], v[6] };
					rowSeat_ = { v[7], v[8], v[9] };
					// v1.8.40: the sailing boat's old default helm seat (0 -200 47.5 in the model) moves over to the tiller
					if (kind_ == kKindSail && (seat_ - kSailHelmSeatOld * kSailScale).Length() < 0.5f) seat_ = kSailHelmSeat * kSailScale;
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
				body_.reset();
				UnrideVisual("load");  // v1.8.57
				visual_.reset();
			wbShown_ = nullptr;
				aiApplied_ = false;
				carrierChecked_ = carrierSwapped_ = false;  // v1.7.71
				speed_ = turn_ = 0.0f;
				logger::info("Co-save: {} hull {:08X} ({}) carrier {:08X} ({}) restored at {:.0f} {:.0f} {:.0f}", Dims().label, refId_, okH ? "ok" : "gone", carrierId_, okC ? "ok" : "gone",
					pos_.x, pos_.y, pos_.z);
			}
		}

		void Forget()
		{
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
			UnrideVisual("reset");  // v1.8.57
			visual_.reset();
			wbShown_ = nullptr;
			carrierId_ = 0;
			CarrierAudio::ids[kind_].store(0, std::memory_order_relaxed);
			helm_ = false;
			refId_ = 0;
			body_.reset();
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
		// v1.7.62: auto-row, the game's Auto-Move while seated: rows on until pressed again, back is pressed or you stand up
		void OnAutoRow()
		{
			if (!seated_) return;
			autoRow_ = !autoRow_;
			if (auto* pc = RE::PlayerControls::GetSingleton()) pc->data.autoMove = false;  // the game's own walk stays off
			logger::info("Row: auto-row {}", autoRow_ ? "on" : "off");
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
		bool OnBoat() const { return HaveBoat() && (seated_ || PlayerAboard()); }  // v1.8.0: the underwater look only from the boat
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
			if (seated_) {  // v1.7.24
				if (SeatFlipTooSoon("stand up")) return true;  // v1.8.57
				StandUp("E");
				return true;
			}
			if (picksOk_) return false;  // v1.7.23: standing, only the seat prompts sit you down (E on the hull is the game's: picking up what you dropped)
			if (!Ref() || !body_) return false;
			if (!PlayerAboard()) return false;
			if (Gear::Targeted()) return false;  // v1.7.0: E is for the bedroll under the crosshair
			if (PickedProxy()) return false;     // v1.7.9: the prompt's own activation does it
			if (SeatFlipTooSoon("sit")) return true;  // v1.8.57
			RequestSit(NearestSeat());
			return true;
		}
		double noTalkUntil_{ 0.0 };
		// v1.8.57: one E press = one step. On Elysium the press that stood you up reached the seat prompt (back under you
		// 0.15 s later) and sat you straight down again: you could never get up. A sit or stand within 0.6 s of the last is ignored.
		double seatFlipAt_{ -10.0 };
		bool SeatFlipTooSoon(const char* a_what) const
		{
			const double since = NowS() - seatFlipAt_;
			if (since >= 0.6) return false;
			logger::info("Seat: E to {} ignored ({:.2f} s after the last sit / stand)", a_what, since);
			return true;
		}
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
			const char* sec = RowSection();  // v1.7.98: [Row] the rowboat, [SailBoat] the sailing boat
			const char* rd = sec;
			if (GetPrivateProfileStringA(sec, "MaxSpeed", "", buf, sizeof(buf), ini.c_str()) == 0 && sec != std::string_view("Row")) {
				rd = "Row";  // another boat's first settings: a copy of the rowboat's, then its own
				from = std::format("RowRim.ini [Row] (copied to [{}])", sec);
				path = "";
			} else if (GetPrivateProfileStringA(sec, "MaxSpeed", "", buf, sizeof(buf), ini.c_str()) == 0) {
				std::error_code ec;
				const auto oas = (std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OarsAndSails.ini").string();
				if (GetPrivateProfileStringA("Row", "MaxSpeed", "", buf, sizeof(buf), oas.c_str()) > 0) { path = oas; from = "OarsAndSails.ini (taken over)"; }
				else { SaveRowSettings(); return; }  // defaults written so the keys are there to edit
			}
			auto f = [&](const char* a_key, float a_def) {
				char b[32]{};
				GetPrivateProfileStringA(rd, a_key, "", b, sizeof(b), path.empty() ? ini.c_str() : path.c_str());
				try { return b[0] ? std::stof(b) : a_def; } catch (...) { return a_def; }
			};
			rowMax_ = std::clamp(f("MaxSpeed", 300.0f), 50.0f, 700.0f);
			rowDrag_ = std::clamp(f("Glide", 0.07f), 0.01f, 0.8f);
			turnScale_ = std::clamp(f("Turn", 1.0f), 0.3f, 3.0f);
			turnLag_ = std::clamp(f("TurnLag", 2.5f), 1.0f, 6.0f);  // v1.7.97
			if (path != ini) SaveRowSettings();
			logger::info("Row: [{}] top speed {:.0f}, glide {:.2f}, turning x{:.2f}, sluggishness {:.1f} from {}", sec, rowMax_, rowDrag_, turnScale_, turnLag_, from);
		}
		void SaveRowSettings()
		{
			const auto ini = Plankton::IniPath();
			WritePrivateProfileStringA(RowSection(), "MaxSpeed", std::format("{:.0f}", rowMax_).c_str(), ini.c_str());
			WritePrivateProfileStringA(RowSection(), "Glide", std::format("{:.3f}", rowDrag_).c_str(), ini.c_str());
			WritePrivateProfileStringA(RowSection(), "Turn", std::format("{:.2f}", turnScale_).c_str(), ini.c_str());
			WritePrivateProfileStringA(RowSection(), "TurnLag", std::format("{:.2f}", turnLag_).c_str(), ini.c_str());
		}
		// v1.7.97: how sluggishly the boat answers the helm or the oars: 1 the old quick turn, 2.5 by default (a heavy boat
		// swinging slowly into a turn and slowly out of it); RowRim.ini [Row] TurnLag, oas "row lag N", the settings page
		void SetTurnLag(float a_lag)
		{
			if (!rowLoaded_) LoadRowSettings();
			turnLag_ = std::clamp(a_lag, 1.0f, 6.0f);
			SaveRowSettings();
			Print(std::format("RowRim: turn sluggishness {:.1f} (default 2.5, 1 = the old quick turn; saved)", turnLag_));
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
		// v1.8.82: how this boat is drawn while seated (oas "draw auto|old|now|player|pin", or the menu's Advanced page; saved in
		// RowRim.ini [Advanced] DrawRow / DrawSail). auto = the rowboat pinned, the others one frame behind (old).
		static constexpr const char* kDrawNames[5] = { "auto", "old", "now", "player", "pin" };
		const char* DrawKey() const { return kind_ == kKindRow ? "DrawRow" : kind_ == kKindSail ? "DrawSail" : "DrawLong"; }
		void LoadDrawSel() const
		{
			if (drawLoaded_) return;
			drawLoaded_ = true;
			char b[16]{};
			GetPrivateProfileStringA("Advanced", DrawKey(), "auto", b, sizeof(b), Plankton::IniPath().c_str());
			drawSel_ = 0;
			for (int k = 0; k < 5; ++k) if (_stricmp(b, kDrawNames[k]) == 0) drawSel_ = k;
		}
		int DrawSel() const { LoadDrawSel(); return drawSel_; }
		void SetDrawSel(int a_sel)
		{
			LoadDrawSel();
			drawSel_ = std::clamp(a_sel, 0, 4);
			WritePrivateProfileStringA("Advanced", DrawKey(), kDrawNames[drawSel_], Plankton::IniPath().c_str());
		}
		void SetDrawMode(const std::string& a_m)
		{
			int sel = 1;
			for (int k = 0; k < 5; ++k) if (a_m == kDrawNames[k]) sel = k;
			SetDrawSel(sel);
			const int m = DrawModeNow();
			Print(std::format("RowRim: seated boat drawn {} (saved)", sel == 0 ? "automatically (the rowboat pinned, the others a frame behind)" : m == 0 ? "one frame behind, seated people moved with it (OAS)" : m == 1 ? "at this frame's seat pose" : m == 2 ? "from the drawn player" : "at this frame's pose, rower pinned to the seat"));
		}
		void SetRowShift(float a_s) { rowShift_ = std::clamp(a_s, -40.0f, 60.0f); Print(std::format("RowRim: while rowing the rower sits {:.0f} toward the bow (+) / stern (-) of the seat, the oars stay in the oarlocks", rowShift_)); }
		void SetOarRest(float a_in, float a_dip) { restIn_ = std::clamp(a_in, 0.0f, 0.6f); restDip_ = std::clamp(a_dip, -20.0f, 45.0f); Print(std::format("RowRim: resting oars {:.0f}% inboard of the oarlock, blade {:.0f} deg down", restIn_ * 100.0f, restDip_)); }
		void SetHandLock(bool a_on) { handLock_ = a_on; Print(std::format("RowRim: oar hand lock {}", a_on ? "on" : "off (the clip's own oar pose)")); }


		// After the animation update (camera hook): the oars. One set, on the boat model. Parked or seated they lie stowed in the
		// boat (OAS 50); once the row clip poses the rower's AnimObject bones (third person) they blend from the stowed pose into
		// the clip's, locked to the hands and the oarlocks, over 0.8 s (OAS 56.13 / 56.18), and back the same way after the clip.
		void TendOars(float a_dt)
		{
			if (!oarsRunLogged_) {
				oarsRunLogged_ = true;
				logger::info("Oars: tending (visual {}, oars {} {})", visual_ ? "yes" : "no", oars_[0] ? "R" : "-", oars_[1] ? "L" : "-");
			}
			if (!visual_ || Kind() != kKindRow) return;  // v1.8.26: oars on the rowboat only
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
			if (want && playerRows && DrawModeNow() == 3 && havePose_) {
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
			// v1.7.75: also under sail (full or reefed) while nobody rows: the oars lie shipped as in camp
			const bool sailShip = (sailNode_ || sbMain_) && (sail_ == 2 || sail_ == 3) && !rowWant_ && !rowAnimOn_;  // v1.7.92: the sailing boat too
			shipW_ = std::clamp(shipW_ + (((kind_ == kKindRow && Gear::BedrollShown() && !seated_) || sailShip) ? 1.0f : -1.0f) * a_dt / kOarBlend, 0.0f, 1.0f);  // v1.7.33
			const float shS = shipW_ * shipW_ * (3.0f - 2.0f * shipW_);
			for (int k = 0; k < 2; ++k) {
				auto& oar = oars_[k];
				if (!oar) continue;
				const float sd = k == 0 ? 1.0f : -1.0f;
				const RE::NiTransform stow = Oar::StowLocal(sd, stowAt_, stowYaw_, kStowScale);
				const RE::NiTransform rest = Oar::RestLocal(sd, lockLocal_, restIn_, restDip_);
				const RE::NiTransform base0 = sw >= 1.0f ? stow : sw <= 0.0f ? rest : Oar::Blend(rest, stow, sw);  // v1.2.4
				// v1.7.33: the bed out (you are not seated): the oars lie along the gunwales in the middle and aft oarlocks
				RE::NiTransform ship = Oar::ShipLocal(sd, lockLocal_, aftLock_, 30.0f);
				// v1.7.36: the whole oar must lie aft of the tent (the 11:44 shot: a blade through the tarp, so the oar lay
				// forward): if either end reaches past y 40, the oar is turned end for end
				{
					const RE::NiPoint3 ax = Oar::Axis();
					const RE::NiPoint3 e1 = ship * (Oar::g_b.centre + ax * Oar::g_b.halfLen);
					const RE::NiPoint3 e2 = ship * (Oar::g_b.centre - ax * Oar::g_b.halfLen);
					if ((std::max)(e1.y, e2.y) > 40.0f) {
						ship = Oar::ShipLocal(sd, lockLocal_, aftLock_, 30.0f, true);
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
			// v1.7.53: the hull's ripple: the tiller's own, as big and as often, dead centre (v1.7.57: under the stowed bedroll, bow thwart y 113.7)
			if (r.size > 0.0f && (bodyT_ -= a_dt) <= 0.0f) {
				bodyT_ = moving ? std::clamp(r.spread / spd, 0.07f, 0.4f) * (turning ? 0.6f : 1.0f) : Plankton::Rand(1.2f, 2.6f) * (turning ? 0.5f : 1.0f);
				const float sc = moving || turning ? 0.03f + 0.04f * s01 + (turning ? 0.015f : 0.0f) : 0.018f;
				Fx::Ripple(at(0.0f, 113.7f), sc * r.size);
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
		// v1.7.84: what the Oars and Sails window shows
		struct UiValues { float rowMax, rowDrag, turn, height; int paint, sail; bool lamp, docks, sailOn; float sailSpeed, sailFlap, turnLag; int model; bool out; int sailModel; int carry; int carryJump; };
		UiValues GetUiValues()
		{
			if (!rowLoaded_) LoadRowSettings();
			SailLoad();
			LoadSailTuning();
			return { rowMax_, rowDrag_, 0.45f * turnScale_, heightOff_, paint_, sail_ < 0 ? 0 : sail_, lampOn_, Of(kKindRow).docksOn_, SailEnabled(), sailSpeed_, sailFlap_, turnLag_, Kind(), HaveBoat(), SailModelIndex(), CarryMode(), JumpMode() };
		}
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
			if (DrawModeNow() != 3 || !lateOn_ || !seated_ || !carrierOn_ || !havePose_) return;
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

		void CamFrame() { ++camFrame_; }
		// v1.8.58: the helm camera, ours. Seated at the helm (third person) the camera's place and turn are computed here
		// once a frame, after the game's camera update and our boat placement. GetTranslation / GetRotation of the third
		// person state hand ours to the game, so the furniture / tiller animation swaps and other camera mods' state changes
		// never move it.
		// v1.8.59: PlayStation feel: the mouse sets where the orbit goes and it eases there (Smooth), the camera trails
		// the boat a touch (Follow), idle it swings back behind her (Recenter), scroll zooms MinDist..MaxDist. V: cinematic
		// shots, cut every ShotLength s (chase, low side, bow looking back, high wide, fly-by from ahead, slow orbit).
		bool HelmCamActive() const { return hcActive_; }
		// v1.8.79: raw mouse / wheel / right stick from the InputSink (the look and the zoom of the helm camera)
		static inline std::atomic<float> g_wheel{ 0.0f }, g_mx{ 0.0f }, g_my{ 0.0f }, g_rx{ 0.0f }, g_ry{ 0.0f };
		static inline std::atomic<bool> g_mouseSeen{ false };
		float hcZoomT_{ 0.0f };
		void HelmCamTick(RE::ThirdPersonState* a_tps)
		{
			const auto& c = HelmCamSet::Get();
			// v1.8.63: the boat model blinking out for a moment (the carrier lost) no longer hands the camera back to the
			// game for those frames (it swung round to look at you side on): its last place is held for up to 2 s
			// v1.8.73: the boat drawn the old way (the hull ref itself, no model on the carrier: the rowboat by default) had
			// no visual_, so the helm camera never took over there and the game's camera (going under and looking up as
			// you sat) stayed: the hull's own 3D is used then
			RE::NiAVObject* boatNode = visual_ && visual_->parent ? static_cast<RE::NiAVObject*>(visual_.get()) : nullptr;
			float boatLift = 0.0f;
			if (!boatNode && root_ && root_->parent) { boatNode = root_; boatLift = kCollisionLift; }
			const bool haveBoat = boatNode != nullptr;
			const double tnow = NowS();
			if (haveBoat) hcBoatSeen_ = tnow;
			const bool grace = !haveBoat && hcActive_ && hcHaveBoat_ && tnow - hcBoatSeen_ < 2.0;
			const bool want = seated_ && (haveBoat || grace) && c.on;  // v1.8.76b: the oars seat too (the game's own camera wound up there: 43 rad swings)
			if (grace && hcGraceLogs_ < 20 && !hcInGrace_) { ++hcGraceLogs_; logger::info("Helm camera: the boat model is gone for a moment, held at its last place"); }
			hcInGrace_ = grace;
			if (!want) {
				if (hcActive_) logger::info("Helm camera: handed back to the game");
				// v1.8.85: the game's own camera ran under ours the whole time with the mouse look wound into its pitch (freeRotation.y):
				// handed back it sat under the water looking up at you. Its pitch goes back to level as it takes over. Uncertain, a soft rule
				if (hcActive_ && a_tps) { a_tps->freeRotation.y = 0.0f; logger::info("Helm camera: the game's own pitch put back to level"); }
				if (hcActive_) SmoothCamLink::Give();  // v1.8.61
				hcActive_ = false;
				hcHave_ = false;
				hcHaveBoat_ = false;
				return;
			}
			if (hcActive_ && hcFrame_ == camFrame_) return;
			hcFrame_ = camFrame_;
			const double now = NowS();
			const float dt = hcHave_ ? std::clamp(static_cast<float>(now - hcLast_), 0.0f, 0.1f) : 0.0f;
			hcLast_ = now;
			if (haveBoat) {
				const RE::NiMatrix3& R = boatNode->world.rotate;
				hcBoatYaw_ = std::atan2(R.entry[0][1], R.entry[1][1]);  // her forward (local Y) in the world
				hcBoat_ = boatNode->world.translate - R * RE::NiPoint3{ 0.0f, 0.0f, boatLift };
				hcHaveBoat_ = true;
			}
			const float boatYaw = hcBoatYaw_;
			const RE::NiPoint3 boat = hcBoat_;
			const RE::NiPoint3 pivotNow = boat + RE::NiPoint3{ 0.0f, 0.0f, c.height };
			if (!hcHave_) {
				hcYaw_ = hcYawT_ = 0.0f;   // behind the helmsman, looking along her
				hcPitch_ = hcPitchT_ = 0.22f;
				hcPivot_ = pivotNow;
				hcIdle_ = 0.0f;
				hcDist_ = 0.0f;
				hcZoomT_ = 0.0f;
				g_wheel = 0.0f; g_mx = 0.0f; g_my = 0.0f;
				hcSinceSit_ = 0.0f;  // v1.8.70c
				hcPosTick_ = 0.0f;
				hcShot_ = -1;
				hcHave_ = true;
				logger::info("Helm camera: ours (boat heading {:.0f} deg)", boatYaw * 57.2958f);
			}
			hcActive_ = true;
			if (a_tps) a_tps->freeRotation.y = 0.0f;  // v1.8.85: the game's own pitch never winds up under ours (see the hand-back below)
			SmoothCamLink::Take();  // v1.8.61: SmoothCam steps aside (once)
			auto ease = [&](float a_tau) { return a_tau <= 0.001f ? 1.0f : 1.0f - std::exp(-dt / a_tau); };
			// the point it looks at trails the boat (snaps on a jump)
			if ((pivotNow - hcPivot_).Length() > 800.0f) hcPivot_ = pivotNow;
			else hcPivot_ = hcPivot_ + (pivotNow - hcPivot_) * ease(c.follow);
			// v1.8.79: our own zoom: the raw wheel and the pad, from a default, closest .. 60000 (never the game's own zoom range)
			const float maxEff = (std::min)((std::max)(c.maxDist, 12000.0f), 60000.0f);
			const float zLo = c.minDist;
			if (hcZoomT_ <= 0.0f) hcZoomT_ = 1100.0f;
			{
				const float notches = g_wheel.exchange(0.0f);
				if (notches != 0.0f) hcZoomT_ *= std::pow(0.85f, notches);
				float pz = 0.0f;
				if (Pad::R3Zooming()) pz = Pad::g_leftY; else if (Pad::g_l3) pz = Pad::g_rightY;
				if (pz != 0.0f) hcZoomT_ *= std::exp(-pz * dt * 1.6f);
				hcZoomT_ = std::clamp(hcZoomT_, zLo, maxEff);
			}
			const float distT = hcZoomT_;
			RE::NiPoint3 pos, look;
			if (c.cine) {
				CineTick(dt, boat, boatYaw, pos, look);
			} else {
				hcShot_ = -1;
				float lx = 0.0f, ly = 0.0f;
				// v1.8.79: the look from the raw mouse and the right stick only, never from the player controls' look input (the row
				// and tiller clips fed that one: the spin-outs). If no raw mouse event has ever come, the game's look input as before.
				auto* pcs = RE::PlayerControls::GetSingleton();
				if (g_mouseSeen.load()) {
					const float mx = g_mx.exchange(0.0f), my = g_my.exchange(0.0f);
					// which way the raw counts run against the game's own look (learnt from the frames the mouse really moves)
					static int s_votX = 0, s_votY = 0;
					if (pcs && mx != 0.0f && std::fabs(pcs->data.lookInputVec.x) > 1.0e-4f) s_votX = std::clamp(s_votX + (mx * pcs->data.lookInputVec.x > 0.0f ? 1 : -1), -50, 50);
					if (pcs && my != 0.0f && std::fabs(pcs->data.lookInputVec.y) > 1.0e-4f) s_votY = std::clamp(s_votY + (my * pcs->data.lookInputVec.y > 0.0f ? 1 : -1), -50, 50);
					lx = (s_votX >= 0 ? 1.0f : -1.0f) * mx * c.mouseScale;
					ly = (s_votY > 0 ? 1.0f : -1.0f) * my * c.mouseScale;  // until learnt: the raw Y runs down, the look's up
					lx += g_rx.load() * dt * 7.0f;
					ly += g_ry.load() * dt * 7.0f;
				} else if (pcs) { lx = pcs->data.lookInputVec.x; ly = pcs->data.lookInputVec.y; }
				if (c.invertY) ly = -ly;
				// v1.8.62: a dead zone: a gamepad stick resting a hair off centre (or a mouse jitter) sent tiny look input every
				// frame and the orbit crept round on its own
				const bool tiny = (lx != 0.0f || ly != 0.0f) && std::fabs(lx) < c.deadzone && std::fabs(ly) < c.deadzone;
				if (tiny) {
					++hcTiny_;
					lx = ly = 0.0f;
				}
				hcDiagT_ += dt;
				if (hcDiagT_ > 5.0f) {
					if (hcTiny_ > 0 || hcRecentering_) logger::info("Helm camera: {} tiny look inputs ignored in 5 s (dead zone {:.3f}){}", hcTiny_, c.deadzone, hcRecentering_ ? ", swinging back behind her" : "");
					hcTiny_ = 0;
					hcDiagT_ = 0.0f;
				}
				hcRecentering_ = false;
				if (lx != 0.0f || ly != 0.0f) {
					hcYawT_ = hcYaw_ + std::remainder(hcYawT_ - hcYaw_ + lx * c.sens, 2.0f * kPi);
					hcPitchT_ -= ly * c.sens;
					hcIdle_ = 0.0f;
				} else {
					hcIdle_ += dt;
					if (c.recenter && hcIdle_ > c.recenterDelay) {  // idle: back behind her, gently
						const float back = std::remainder(0.0f - hcYawT_, 2.0f * kPi);
						hcRecentering_ = std::fabs(back) > 0.01f;
						hcYawT_ += back * (1.0f - std::exp(-dt * c.recenterSpeed));
					}
				}
				hcPitchT_ = std::clamp(hcPitchT_, c.pitchMin / 57.2958f, c.pitchMax / 57.2958f);
				const float k = ease(c.smooth);
				hcYaw_ = std::remainder(hcYaw_ + std::remainder(hcYawT_ - hcYaw_, 2.0f * kPi) * k, 2.0f * kPi);
				hcYawT_ = hcYaw_ + std::remainder(hcYawT_ - hcYaw_, 2.0f * kPi);
				hcPitch_ += (hcPitchT_ - hcPitch_) * k;
				hcDist_ = hcDist_ <= 0.0f ? distT : hcDist_ + (distT - hcDist_) * ease(0.10f);
				// v1.8.70c: never below the water: zoomed far out a low angle put the camera hundreds under the surface, and
				// the old fix only lifted it to the surface, looking up at the sky from below the boat. Now the angle itself
				// is raised so the camera stays over the water at this distance (it slides up as you zoom out)
				if (auto* wc = RE::PlayerCharacter::GetSingleton() ? RE::PlayerCharacter::GetSingleton()->GetParentCell() : nullptr; wc && wc->IsExteriorCell()) {
					const float water = wc->GetExteriorWaterHeight();
					if (water > -1.0e6f && hcDist_ > 1.0f) {
						const float sMin = std::clamp((water + 25.0f - hcPivot_.z) / hcDist_, -0.99f, 0.99f);
						const float pMin = std::asin(sMin);
						if (hcPitch_ < pMin) hcPitch_ = pMin;
						if (hcPitchT_ < pMin) hcPitchT_ = pMin;
					}
				}
				const float yaw = boatYaw + hcYaw_, cp = std::cos(hcPitch_), sp = std::sin(hcPitch_);
				const RE::NiPoint3 fwd{ std::sin(yaw) * cp, std::cos(yaw) * cp, -sp };
				pos = hcPivot_ - fwd * hcDist_;
				look = hcPivot_;
			}
			// keep it out of the water
			if (auto* cell = RE::PlayerCharacter::GetSingleton() ? RE::PlayerCharacter::GetSingleton()->GetParentCell() : nullptr; cell && cell->IsExteriorCell()) {
				const float water = cell->GetExteriorWaterHeight();
				if (water > -1.0e6f && pos.z < water + 15.0f) pos.z = water + 15.0f;
			}
			hcPos_ = pos;
			// v1.8.70c: below the boat watch (Miguel: a split second after sitting the camera turns up and looks at the sky from
			// below, another run from under the water): logged with everything that places it, the first 2 s after sitting
			// every 0.1 s, then whenever it is under the boat's deck
			{
				hcSinceSit_ += dt;
				const bool under = pos.z < boat.z - 20.0f;
				if ((hcSinceSit_ < 2.0f || under) && hcPosLogs_ < 80 && (hcPosTick_ -= dt) <= 0.0f) {
					hcPosTick_ = 0.1f;
					++hcPosLogs_;
					auto* pcs = RE::PlayerControls::GetSingleton();
					logger::info("Helm camera: at {:.0f} {:.0f} {:.0f}{} | boat {:.0f} {:.0f} {:.0f} heading {:.0f} | pivot {:.0f} {:.0f} {:.0f} | pitch {:.2f} (target {:.2f}) orbit {:.2f} dist {:.0f} | look {:.3f} {:.3f} | visual on '{}' | since sitting {:.2f} s",
						pos.x, pos.y, pos.z, under ? " UNDER THE BOAT" : "", boat.x, boat.y, boat.z, boatYaw * 57.2958f, hcPivot_.x, hcPivot_.y, hcPivot_.z,
						hcPitch_, hcPitchT_, hcYaw_, hcDist_, pcs ? pcs->data.lookInputVec.x : 0.0f, pcs ? pcs->data.lookInputVec.y : 0.0f,
						visual_ && visual_->parent ? visual_->parent->name.c_str() : "nothing", hcSinceSit_);
				}
			}
			// v1.8.70b: spin watch: the drawn camera's turn per frame; a fast turn (over 1.5 rad/s for half a second) without the
			// mouse is logged with what fed it (the boat's heading, our orbit, the cinematic), so a spin in the log says why
			{
				const RE::NiPoint3 dv = look - pos;
				const float camYaw = std::atan2(dv.x, dv.y);
				const float dy = std::remainder(camYaw - hcLastCamYaw_, 2.0f * kPi);
				const float dby = std::remainder(boatYaw - hcLastBoatYaw_, 2.0f * kPi);
				hcLastCamYaw_ = camYaw;
				hcLastBoatYaw_ = boatYaw;
				auto* pcs = RE::PlayerControls::GetSingleton();
				const bool lk = pcs && (std::fabs(pcs->data.lookInputVec.x) > 1.0e-4f || std::fabs(pcs->data.lookInputVec.y) > 1.0e-4f);
				const float rate = dt > 0.0001f ? std::fabs(dy) / dt : 0.0f;
				hcFast_ = (!lk && rate > 1.5f) ? hcFast_ + dt : 0.0f;
				if (hcFast_ > 0.5f && hcSpinLogs_ < 30) {
					++hcSpinLogs_;
					hcFast_ = 0.0f;
					logger::info("Helm camera: SPIN {:.1f} rad/s without the mouse | boat heading {:.0f} deg (turned {:.2f} rad this frame) | orbit {:.2f} (target {:.2f}) pitch {:.2f} | cinematic {} shot {} | visual on {} | dt {:.3f}",
						rate, boatYaw * 57.2958f, dby, hcYaw_, hcYawT_, hcPitch_, c.cine, hcShot_, visual_ && visual_->parent ? visual_->parent->name.c_str() : "nothing", dt);
				}
			}
			RE::NiPoint3 fwd = look - pos;
			const float fl = fwd.Length();
			fwd = fl > 0.01f ? fwd * (1.0f / fl) : RE::NiPoint3{ std::sin(boatYaw), std::cos(boatYaw), 0.0f };
			const float h = std::atan2(fwd.x, fwd.y);
			const RE::NiPoint3 right{ std::cos(h), -std::sin(h), 0.0f };
			const RE::NiPoint3 up{ right.y * fwd.z - right.z * fwd.y, right.z * fwd.x - right.x * fwd.z, right.x * fwd.y - right.y * fwd.x };
			RE::NiMatrix3 m;  // columns: right, forward, up (local X, Y, Z in the world)
			m.entry[0][0] = right.x; m.entry[0][1] = fwd.x; m.entry[0][2] = up.x;
			m.entry[1][0] = right.y; m.entry[1][1] = fwd.y; m.entry[1][2] = up.y;
			m.entry[2][0] = right.z; m.entry[2][1] = fwd.z; m.entry[2][2] = up.z;
			hcRot_ = RE::NiQuaternion(m);
		}
		// v1.8.59: the cinematic shots, GTA style: cut to a new one every ShotLength s (or when a fly-by has passed)
		void CineTick(float a_dt, const RE::NiPoint3& a_boat, float a_yaw, RE::NiPoint3& a_pos, RE::NiPoint3& a_look)
		{
			const auto& c = HelmCamSet::Get();
			struct Shot { const char* name; float yaw, pitch, dist, height, drift; bool flyby; };
			static constexpr Shot kShots[] = {
				{ "chase", 0.0f, 0.12f, 1100.0f, 150.0f, 0.0f, false },
				{ "low side", 1.57f, -0.02f, 750.0f, 40.0f, 0.04f, false },
				{ "bow looking back", 3.14159f, 0.10f, 520.0f, 160.0f, 0.0f, false },
				{ "high wide", 0.6f, 0.75f, 2600.0f, 120.0f, 0.03f, false },
				{ "fly-by", 0.0f, 0.0f, 0.0f, 60.0f, 0.0f, true },
				{ "slow orbit", -1.2f, 0.25f, 1300.0f, 140.0f, 0.12f, false },
				{ "low stern quarter", -0.6f, 0.02f, 650.0f, 50.0f, 0.0f, false },
			};
			constexpr int kN = static_cast<int>(sizeof(kShots) / sizeof(kShots[0]));
			hcShotT_ += a_dt;
			bool cut = hcShot_ < 0 || hcShotT_ > c.shotLen;
			if (hcShot_ >= 0 && kShots[hcShot_].flyby && (a_boat - hcFly_).Length() > 2600.0f && hcShotT_ > 2.0f) cut = true;
			if (cut) {
				int next = hcShot_;
				std::uniform_int_distribution<int> d(0, kN - 1);
				static std::mt19937 rng{ std::random_device{}() };
				while (next == hcShot_) next = d(rng);
				hcShot_ = next;
				hcShotT_ = 0.0f;
				hcShotYaw_ = kShots[next].yaw;
				if (kShots[next].flyby) {  // a fixed spot ahead of her and off to a side: she sails past it
					const float side = (rng() & 1) ? 1.0f : -1.0f;
					const RE::NiPoint3 f{ std::sin(a_yaw), std::cos(a_yaw), 0.0f }, r{ std::cos(a_yaw), -std::sin(a_yaw), 0.0f };
					hcFly_ = a_boat + f * 1400.0f + r * (side * 450.0f) + RE::NiPoint3{ 0.0f, 0.0f, kShots[next].height };
				}
				logger::info("Helm camera: cinematic shot '{}'", kShots[next].name);
			}
			const Shot& s = kShots[hcShot_];
			const RE::NiPoint3 look = a_boat + RE::NiPoint3{ 0.0f, 0.0f, 110.0f };
			if (s.flyby) {
				a_pos = hcFly_;
			} else {
				hcShotYaw_ += s.drift * a_dt;
				const float yaw = a_yaw + hcShotYaw_, cp = std::cos(s.pitch), sp = std::sin(s.pitch);
				const RE::NiPoint3 fwd{ std::sin(yaw) * cp, std::cos(yaw) * cp, -sp };
				a_pos = a_boat + RE::NiPoint3{ 0.0f, 0.0f, s.height } - fwd * s.dist;
			}
			a_look = look;
		}
		void HelmCamPos(RE::NiPoint3& a_pos) const { if (hcActive_) a_pos = hcPos_; }
		void HelmCamRot(RE::NiQuaternion& a_rot) const { if (hcActive_) a_rot = hcRot_; }
		bool hcActive_{ false }, hcHave_{ false };
		std::uint32_t hcFrame_{ ~0u };
		double hcLast_{ 0.0 };
		float hcYaw_{ 0.0f }, hcPitch_{ 0.0f }, hcYawT_{ 0.0f }, hcPitchT_{ 0.0f }, hcDist_{ 0.0f }, hcIdle_{ 0.0f };
		int hcShot_{ -1 };
		float hcShotT_{ 0.0f }, hcShotYaw_{ 0.0f };
		int hcTiny_{ 0 }, hcGraceLogs_{ 0 }, hcSpinLogs_{ 0 };
		float hcLastCamYaw_{ 0.0f }, hcLastBoatYaw_{ 0.0f }, hcFast_{ 0.0f };
		float hcSinceSit_{ 0.0f }, hcPosTick_{ 0.0f };
		int hcPosLogs_{ 0 };
		double hcBoatSeen_{ 0.0 };
		float hcBoatYaw_{ 0.0f };
		RE::NiPoint3 hcBoat_{};
		bool hcHaveBoat_{ false }, hcInGrace_{ false };
		float hcDiagT_{ 0.0f };
		bool hcRecentering_{ false };
		RE::NiPoint3 hcPos_{}, hcPivot_{}, hcFly_{};
		RE::NiQuaternion hcRot_{ 1.0f, 0.0f, 0.0f, 0.0f };
		// the seated pose LatePlace draws the boat at (kept in step with it)
		void DrawPose(RE::NiPoint3& a_at, RE::NiMatrix3& a_rot) const
		{
			a_at = DrawModeNow() == 0 && havePrev_ ? prevAt_ : lastAt_;
			a_rot = DrawModeNow() == 0 && havePrev_ ? prevRot_ : lastRot_;
			if (DrawModeNow() == 0 && havePrev_ && (lastAt_ - prevAt_).Length() < 60.0f) {
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
			mlOut_ << "RowRim motion log (" << kVersionShort << "), draw mode " << DrawModeNow() << ", lead " << drawLead_ << "\n"
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
				body_.reset();
				needInit_ = true;
			}
			if (seated_ || !bodyLive) {
				// seated: v1.1.3, as OAS ("the camera shows last frame's player position, so the boat follows the previous frame's
				// vehicle position to stay glued to the view"): the boat one frame behind the glue. The row stroke's surge changes
				// that one frame of travel every stroke, which is why it only showed while rowing. rr draw now|player to compare.
				if (!havePose_) return;
				RE::NiPoint3 at = DrawModeNow() == 0 && havePrev_ ? prevAt_ : lastAt_;
				RE::NiMatrix3 rot = DrawModeNow() == 0 && havePrev_ ? prevRot_ : lastRot_;
				// v1.6.6: drawLead_ of the way from last frame's pose to this frame's: the boat (and the passengers pinned to it)
				// no longer fall a whole frame of travel behind the rower when rowing starts, only a sliver (rr "lead 0..1")
				if (DrawModeNow() == 0 && havePrev_ && (lastAt_ - prevAt_).Length() < 60.0f) {
					at = prevAt_ + (lastAt_ - prevAt_) * drawLead_;
					if (drawLead_ >= 0.5f) rot = lastRot_;
				}
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (auto* proot = DrawModeNow() == 2 && seated_ && player ? player->Get3D(false) : nullptr) {
					const RE::NiPoint3 fromPlayer = proot->world.translate - lastRot_ * SeatPos();
					if ((fromPlayer - lastAt_).Length() < 60.0f) at = fromPlayer;
				}
				PlaceNode(visual_.get(), at, rot);
				drawAt_ = at; drawRot_ = rot; haveDraw_ = true;  // v1.8.57d: ReplaceRiding keeps this pose
				for (RE::NiAVObject* n = visual_->parent; n; n = n->parent) n->UpdateWorldBound();
				if (kind_ == kKindRow) Gear::LatePin(visual_.get());  // v1.7.12: the bedroll rocks with the drawn boat (v1.8.26: the rowboat's)
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
			drawAt_ = at; drawRot_ = ToMatrix(qt); haveDraw_ = true;  // v1.8.57d
			for (RE::NiAVObject* n = visual_->parent; n; n = n->parent) n->UpdateWorldBound();
			if (kind_ == kKindRow) Gear::LatePin(visual_.get());  // v1.7.12
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
		void SetGlide(bool a_on) { glideOn_ = a_on; Print(std::format("RowRim: seat glide {}", a_on ? "on" : "off")); }
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
			heightOff_ = kind_ == kKindSail ? kSailHeight : 0.0f;  // v1.8.27c
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
			const bool act = IsActive();          // v1.8.26: the boat in use (the others: their own frame only)
			if (act) {
			MotionA0(a_dt);                       // v1.7.13: motion log
			if (seated_) Pad::Zoom(a_dt);         // v1.7.8: controller zoom while seated
			TendModCompat(seated_);               // v1.7.12: True Directional Movement paused while seated (OAS 56.43)
			Gear::TendNum(a_dt);                  // v1.7.11: oas "num gear"
			Wake::TendNum(a_dt);                  // v1.7.43: oas "num stern"
			{
				auto& row = Of(kKindRow);  // v1.8.26: the bed is the rowboat's only
				Gear::TendLoad(row.refId_ ? row.Ref() : nullptr, a_dt);  // v1.7.21: saved / stray beds (and old fishing leftovers) removed after a load, boat or not
			}
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
					pc.onBoat = OnBoat();  // v1.8.0
				}
				pc.onBoat = pc.onBoat || (Plankton::LoadSettings(), Plankton::g_set.everywhere);  // v1.8.79: the plankton also away from the boat, unless switched off
				Plankton::g_hullX = Kind() == kKindSail ? (DimsOf(kKindSail).feelWidth / ModelScale()) / DimsOf(kKindRow).feelWidth : 1.0f;  // v1.8.90: this boat's hull in the plankton's eyes (the outline is the rowboat's, widened for the sailing boat)
				Stage("Plankton::Tick");
				Plankton::Tick(a_dt, pc);
				Stage("Wake::Tick");
				Wake::Tick(a_dt, pc);  // v1.5.16
				Stage("Ribbon::Tick");
				Ribbon::Tick(a_dt);  // v1.7.60: the oars' glowing swoosh
				Stage("TestHull::Update (after plankton and wake)");
			}
			}  // act
			if (kind_ == kKindRow || (kind_ == kKindSail && SailEnabled())) {  // v1.8.26: the docks keep the rowboat (v1.8.87: and the sailing boat, at its own docks)
				Stage("TestHull::TendDocks");
				TendDocks(a_dt);  // v1.7.8
			}
			Stage("TestHull::TendHome");
			TendHome(a_dt);  // v1.8.47: each boat's own dock
			if (act) {
				Stage("Legacy::Tend");
				Legacy::Tend(a_dt, [](RE::FormID a_id) { return IsOursAny(a_id) || LakeBoats::IsOurs(a_id); },
					HaveBoat() ? &pos_ : nullptr);  // v1.7.8; v1.7.9: our prompts, lantern light and anything by our boat are never old pieces (v1.8.26: all three boats')
				Stage("LakeBoats::SweepStrayLamps");
				LakeBoats::SweepStrayLamps(a_dt, Of(kKindRow).LampLightId());  // v1.7.69: lights and lanterns left over the lakes (v1.7.70: and crew in the water)
				Stage("TestHull::SweepStrayCarriers");
				SweepStrayCarriers(a_dt);  // v1.8.31e
			}
			if (!refId_) return;
			if (!rebased_) RebaseHull();  // v1.8.66
			auto* ref = Ref();
			if (!ref) { Forget(); return; }
			TendStow(a_dt);

			auto* root = ref->Get3D();
			const float dt = std::clamp(a_dt, 0.001f, 0.1f);
			rideTime_ += dt;
			if (standGlide_ && rideTime_ - standAt_ > 1.0f) {  // v1.8.71: the glide was not ticked (3D gone, not active): let go
				standGlide_ = false;
				if (auto* pl = RE::PlayerCharacter::GetSingleton()) pl->SetCollision(true);
				logger::info("Seat: glide out cut short");
			}
			if (!root) {  // 3D not loaded yet (or reloading after a cell hop)
				body_.reset();
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
				body_.reset();
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
			else if (act && TendStandGlide(at, rot)) {}  // v1.8.71
			else if (act) {
				TendNudge(dt);
				TendCarry(at, rot, dt);  // v1.8.9: standing / jumping aboard while it moves
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
			const bool row = kind_ == kKindRow;  // v1.8.26: the bed is the rowboat's only
			if (row && HullCellLoaded()) Gear::Tend(ref, at, rot, yaw_ + (bowFlip_ ? kPi : 0.0f), dt, seated_);  // v1.7.0: the bedroll rides along
			TendPicks(dt);  // v1.7.9: Board / Steer / Row prompts (v1.8.26: the boat in use only)
			TendSitWait(dt);  // v1.7.25
			TendSeatSwitch(dt);  // v1.8.29c
			TendUnstick(dt);    // v1.7.31
			if (row && visual_ && (rollCulledOn_ != visual_.get() || rollCulled_ != Gear::BedrollShown())) {  // v1.7.11: the roll hidden once it is out
				rollCulledOn_ = visual_.get();                                                          // v1.7.12: only while the bed is really there
				rollCulled_ = Gear::BedrollShown();
				if (auto* roll = FindShape(visual_.get(), "RowRim_Clutter_Bedroll")) roll->SetAppCulled(rollCulled_);
			}
			if (row) TendSleepingBag(dt);  // v1.7.48
			TendSail(dt);         // v1.7.73
			TendAnchor(dt);       // v1.8.29: the sailing boat's anchor
			TendOars(dt);  // v0.8.4: here, after the player's update, as OAS TendBakedBoatOars (the camera hook never called it)
			if (act) TendHullRipples(dt);  // v1.7.43
			TendOarlocks();       // v1.7.52
			if (!act) return;     // v1.8.26: the rest is the player's, on the boat in use
			TendFootIK();         // v1.7.52
			TendTameFish(dt);     // v1.7.52
			if (havePose_) {  // v1.7.54: bumping the lake boats
				const float hit = LakeBoats::BumpPlayerBoat(pos_, yaw_ + (bowFlip_ ? kPi : 0.0f), speed_, dt);
				if (hit > 0.0f) dipVel_ -= 45.0f * hit;
				BumpOwnBoats(dt);  // v1.8.37
			}
			MotionA1();    // v1.7.13

			LogFrame(false, dt);
		}

	private:
		// v1.8.37: our own boats bump each other (the rowboat waiting at a dock, a boat left out): each a capsule along its
		// keel (half length less half beam, the beam), pushed apart, the smaller boat giving way the more; the boat in use
		// loses the way it had into the other
		void BumpOwnBoats(float a_dt)
		{
			const auto& da = Dims();
			const float hdA = yaw_ + (bowFlip_ ? kPi : 0.0f);
			const float fx = std::sin(hdA), fy = std::cos(hdA);
			const float rA = da.feelWidth, hA = (std::max)(1.0f, da.feelLen - da.feelWidth);
			for (int k = 0; k < kKindCount; ++k) {
				if (k == kind_) continue;
				auto& o = Of(k);
				if (!o.havePose_ || !o.HaveBoat() || !o.refId_) continue;
				const auto& db = o.Dims();
				const float rB = db.feelWidth, hB = (std::max)(1.0f, db.feelLen - db.feelWidth);
				const float ddx = o.pos_.x - pos_.x, ddy = o.pos_.y - pos_.y;
				const float reach = hA + hB + rA + rB;
				if (ddx * ddx + ddy * ddy > reach * reach) continue;
				const float hdB = o.yaw_ + (o.bowFlip_ ? kPi : 0.0f), gx = std::sin(hdB), gy = std::cos(hdB);
				float px, py, qx, qy;
				LakeBoats::Bump::SegClosest(pos_.x - fx * hA, pos_.y - fy * hA, pos_.x + fx * hA, pos_.y + fy * hA,
					o.pos_.x - gx * hB, o.pos_.y - gy * hB, o.pos_.x + gx * hB, o.pos_.y + gy * hB, px, py, qx, qy);
				float nx = px - qx, ny = py - qy;  // from the other boat to this one
				const float dist = std::sqrt(nx * nx + ny * ny);
				if (dist >= rA + rB) continue;
				if (dist > 0.01f) { nx /= dist; ny /= dist; } else { nx = -gy; ny = gx; }
				const float depth = rA + rB - dist;
				const float share = std::clamp(rA / (rA + rB), 0.2f, 0.8f);  // the bigger boat moves the less
				pos_.x += nx * depth * (1.0f - share); pos_.y += ny * depth * (1.0f - share);
				o.pos_.x -= nx * depth * share; o.pos_.y -= ny * depth * share;
				const float into = (fx * nx + fy * ny) * speed_;  // < 0: going into it
				if (into < -2.0f) {
					speed_ -= (fx * nx + fy * ny) * into * 0.7f;
					if (bumpLog_ <= 0.0f) { logger::info("Bump: {} into the {} ({:.0f})", Dims().label, db.label, -into); bumpLog_ = 2.0f; }
					dipVel_ -= (std::min)(-into, 200.0f) * 0.08f;
				}
			}
			bumpLog_ -= a_dt;
		}
		// v1.0.1: one step of the boat's target pose (sim, world, motion). Also run while the hull's collision 3D reloads after a
		// cell hop, so the boat (drawn by the carrier) and the seated player keep moving: those frames used to freeze it (the
		// stutter every cell crossing).
		void StepTarget(float dt, RE::NiPoint3& a_at, RE::NiMatrix3& a_rot, float& a_dYaw)
		{
			a_dYaw = 0.0f;
			lastThrust_ = 0.0f;
			sailLeeWant_ = sailHeelWant_ = 0.0f;  // v1.8.9j: SailDrive sets them while the sail draws
			if (helm_) {
				// v1.8.9l: at the helm under a trimmed sail W / S work the sail, so a crew rower does not row
				if (seated_ && (seatKind_ == SeatKind::kRower || (Crew::Rower() && !(SailManual() && SailDrawing())))) RowSim(dt);  // v1.7.9: or a crew rower, you at the stern
				else { rowWant_ = false; HelmSim(dt); }
				SailDrive(dt);  // v1.7.74: the wind in the sail, seated only
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
				// v1.8.9j: leeway: the boat slides to leeward under sail (starboard = (cos, -sin)); the side feelers still stop it
				sailLee_ += (sailLeeWant_ - sailLee_) * (1.0f - std::exp(-1.0f * dt));
				sailHeel_ += (sailHeelWant_ - sailHeel_) * (1.0f - std::exp(-0.8f * dt));
				pos_.x += std::cos(hd) * sailLee_ * dt;
				pos_.y -= std::sin(hd) * sailLee_ * dt;
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
			// v1.8.10: the OceanRim sea under the hull, eased: a big hull answers more slowly than a small one
			{
				const auto& dims = Dims();
				float sh = 0.0f, sp = 0.0f, sr = 0.0f;
				const float water = pos_.z - WaterZ() - heightOff_;
				const int sk = Kind();  // v1.8.26: each boat its own [Sea] settings
				const auto& tune = SeaLink::T(sk);
				const bool seaOk = SeaLink::Motion(pos_.x, pos_.y, water, yaw_, dims.feelLen * 0.8f, dims.feelWidth, sh, sp, sr, sk);
				if (!seaOk) sh = sp = sr = 0.0f;
				const bool act = IsActive();
				if (act) {
					SeaLink::g_playerOk = seaOk;
					SeaLink::g_rawH = sh;  // v1.8.13: shown on the Sea page
				}
				SeaLink::Step(seaState_, sh, sp, sr, dt, dims.feelLen > 600.0f ? 1.5f : 1.0f, sk);  // v1.8.11: a soft spring, was a plain ease
				seaHeave_ = seaState_.h;
				seaPitch_ = seaState_.p;
				seaRoll_ = seaState_.r;
				if (act) {
					SeaLink::g_outH = seaHeave_;
					SeaLink::g_outP = seaPitch_;
					SeaLink::g_outR = seaRoll_;
				}
				heave_ += seaHeave_;
				pitch_ += seaPitch_;
				roll_ += seaRoll_;
				if (seaOk) {
					// v1.8.15: under way the bow rides up its own bow wave and the stern squats
					const float sp01 = std::clamp(std::abs(speed_) / 300.0f, 0.0f, 1.0f);
					const float ahead = (speed_ >= 0.0f ? 1.0f : -1.0f) * (bowFlip_ ? -1.0f : 1.0f);
					pitch_ += tune.trim * 0.0174533f * sp01 * sp01 * ahead;
					heave_ -= tune.squat * sp01;
					// the bow digging into a wave throws up a splash
					const float hl = dims.feelLen * 0.9f;
					const float bx = pos_.x + std::sin(yaw_) * hl, by = pos_.y + std::cos(yaw_) * hl;
					float bz = 0.0f;
					if (bowCool_ > 0.0f) bowCool_ -= dt;
					if (SeaLink::g_surface && SeaLink::g_surface(bx, by, water, &bz)) {
						const float imm = bz - (water + heave_ + std::sin(pitch_) * hl);
						const float rate = dt > 0.0f ? (imm - bowImm_) / dt : 0.0f;
						if (imm > 2.0f && rate > 25.0f && bowCool_ <= 0.0f) {
							SeaLink::Splash(bx, by, water, dims.feelWidth * 1.3f, std::clamp(rate * 0.12f + sp01 * 6.0f, 3.0f, 22.0f), 1);
							bowCool_ = 0.5f;
						}
						bowImm_ = imm;
					}
				}
				SeaLink::Hull(1u + 4u * static_cast<std::uint32_t>(sk), pos_.x, pos_.y, yaw_, dims.feelLen, dims.feelWidth, pos_.z + heave_ - WaterZ() - heightOff_);  // v1.8.26: ids 1, 5, 9 (lake boats' have bit 2)
			}

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
			else if (IsActive() && TendStandGlide(at, rot)) {}  // v1.8.71
			else if (IsActive()) {
				TendNudge(dt);
				TendCarry(at, rot, dt);  // v1.8.9: the hull's collision is reloading (a cell hop): nothing holds you, we carry you
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
			if (IsActive()) MotionA1();  // v1.7.14: the motion log sees coast frames (cell hops) too
		}

		RE::TESObjectREFR* Ref() const
		{
			if (!refId_) return nullptr;
			auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(refId_);
			return ref && !ref->IsDeleted() ? ref : nullptr;
		}

		// v1.8.49: only while the body is the hull's own now (its 3D the one it was found in). After a coc / a door the hull's
		// 3D is unloaded and its body freed before our next look; every boat asks "is the player aboard me" each frame before
		// its own update re-checks, and the cast on the freed body crashed (crash log 17:19, coc to Dawnstar). The body is also
		// held (body_ is a NiPointer now), so it is never freed under us.
		RE::hkpRigidBody* HkBody() const
		{
			if (!body_ || needInit_ || !root_) return nullptr;
			auto* r = Ref();
			if (!r || r->Get3D() != root_) return nullptr;
			return body_->GetRigidBody();
		}

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
			body_ = RE::NiPointer<RE::bhkRigidBody>(rb);  // v1.8.49: held
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
			return std::format("{} local {:.0f} {:.0f} {:.1f} (deck {:.0f}) state {} on hull {}", a_actor->GetName(), local.x, local.y, local.z, Dims().deckZ, state, onUs ? 1 : 0);
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
			if (!a_now && logTimer_ < 10.0f) return;  // v1.8.79: was every half second
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
				body_.reset();
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
			body_.reset();
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
		// v1.7.63: not a horse (horse mods (follower horses, horse AI) took the hidden horse for one of theirs). The player
		// home mannequin first: Skyrim.esm's own ManakinRace NPC, bald, bare and lifeless, a race body, hair, outfit and
		// physics mods leave alone. Else a plain Skyrim.esm townsperson base (not unique, Nord / Imperial / Breton /
		// Redguard, no template, no factions); a horse only if neither is found. Lowest form ID: the same base every time.
		static bool IsHorse(const RE::TESRace* a_race)
		{
			const char* edid = a_race ? a_race->formEditorID.c_str() : nullptr;
			return edid && std::string_view(edid) == "HorseRace";
		}
		// v1.8.66: each boat's carrier is its own actor of its own race in OarsAndSails.esl (RowRimRowboatCarrier 0x808 /
		// RowRimSailboatCarrier 0x809, races 0x806 / 0x807, copies of the mannequin's without its script): no vanilla
		// mannequins placed in saves any more, and with the esl gone every carrier goes with it. The mannequin is only a
		// fallback for an old esl.
		static constexpr RE::FormID kOwnCarrierLocal[2] = { 0x808, 0x809 };
		static constexpr RE::FormID kOwnRowHullLocal = 0x80A;
		static RE::TESNPC* OwnCarrier(int a_kind)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh ? dh->LookupForm<RE::TESNPC>(kOwnCarrierLocal[a_kind == kKindRow ? 0 : 1], "OarsAndSails.esl") : nullptr;
		}
		// any carrier base, ours or the mannequin an older version placed
		static bool IsCarrierBase(const RE::TESNPC* a_npc)
		{
			if (!a_npc) return false;
			if (a_npc == OwnCarrier(kKindRow) || a_npc == OwnCarrier(kKindSail)) return true;
			const char* re = a_npc->race ? a_npc->race->formEditorID.c_str() : nullptr;
			return re && std::string_view(re) == "ManakinRace";
		}
		static RE::TESNPC* FindCarrierBase(int a_kind = -1)
		{
			if (a_kind >= 0) {
				static bool s_logged[kKindCount]{};
				auto* own = OwnCarrier(a_kind);
				if (!s_logged[std::clamp(a_kind, 0, kKindCount - 1)]) {
					s_logged[std::clamp(a_kind, 0, kKindCount - 1)] = true;
					logger::info("Carrier: {} base {:08X}{}", a_kind == kKindRow ? "rowboat" : "sailing boat", own ? own->GetFormID() : 0u, own ? " (our own, OarsAndSails.esl)" : " not in OarsAndSails.esl (old esl?): the mannequin instead");
				}
				if (own) return own;
			}
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) return nullptr;
			static RE::TESNPC* s_person = nullptr;
			static bool s_looked = false;
			if (!s_looked) {
				s_looked = true;
				RE::TESNPC* manakin = nullptr;
				bool manakinUnique = true;
				for (auto* npc : dh->GetFormArray<RE::TESNPC>()) {  // the mannequin (a non-unique one if there is one)
					if (!npc || !npc->race || (npc->GetFormID() >> 24) != 0) continue;
					const char* me = npc->race->formEditorID.c_str();
					if (!me || std::string_view(me) != "ManakinRace") continue;
					const bool u = npc->IsUnique();
					if (!manakin || (manakinUnique && !u) || (u == manakinUnique && npc->GetFormID() < manakin->GetFormID())) { manakin = npc; manakinUnique = u; }
				}
				// v1.7.72: the mannequin only, never a person (no townsperson fallback); a horse if it is somehow missing
				s_person = manakin;
				logger::info("Carrier: mannequin base {:08X} '{}'", manakin ? manakin->GetFormID() : 0u, manakin ? manakin->GetName() : "not found (a horse instead)");
			}
			if (s_person) return s_person;
			for (auto* npc : dh->GetFormArray<RE::TESNPC>()) {
				if (npc && !npc->IsUnique() && IsHorse(npc->race)) return npc;
			}
			return nullptr;
		}
		// v1.7.63: the person stays put and unharmed under the water: breathes there, never fights
		static void CalmCarrier(RE::Actor* a_actor)
		{
			if (!a_actor) return;
			auto* av = a_actor->AsActorValueOwner();
			if (!av) return;
			av->SetActorValue(RE::ActorValue::kWaterBreathing, 1.0f);
			av->SetActorValue(RE::ActorValue::kAggression, 0.0f);
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
			auto* base = FindCarrierBase(kind_);
			if (!player || !base) { Print("RowRim: no carrier base found; the hull draws itself"); return; }
			// v1.8.66: placed at the hull, never at the player (placed at the player inside a house, the carrier stood by the
			// door: Nexus, a mannequin by the cell's entrance), and only while the boat's own cell is loaded
			auto* hull = Ref();
			if (!hull || !HullCellLoaded()) {
				if (carrierWaitLogs_++ < 5) logger::info("Carrier: not placed yet (the boat is not loaded); placed once it is");
				return;
			}
			auto ref = hull->PlaceObjectAtMe(base, true);
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) { Print("RowRim: carrier spawn failed"); return; }
			carrierId_ = actor->GetFormID();
			CarrierAudio::ids[kind_].store(carrierId_, std::memory_order_relaxed);
			actor->SetCollision(false);  // no capsule in the boat, nothing to bump or pick
			actor->EnableAI(carrierAI_);
			ShieldCarrier(actor);
			MuteCarrier(actor);
			CalmCarrier(actor);  // v1.7.63
			actor->SetPosition(CarrierSpot(a_at), true);
			UnrideVisual("new carrier");  // v1.8.57
			visual_.reset();
			wbShown_ = nullptr;
			hiddenKids_ = 0;
			Print(std::format("RowRim: carrier {:08X} ('{}', base {:08X}) placed {} units under the boat, audio event gate on, AI {}", carrierId_, base->GetName(), base->GetFormID(),
				static_cast<int>(kCarrierDepth), carrierAI_ ? "on" : "off"));
		}

		// v1.8.66: a rowboat placed by an older version stands on the vanilla ShipRowBoat01: once its cell is loaded and you
		// are not aboard, it is placed again on our own copy (OarsAndSails.esl 0x80A) where it was and the old ref deleted,
		// so nothing vanilla of ours stays in the save. The new 3D sets itself up as after a cell hop.
		bool rebased_{ false };
		void RebaseHull()
		{
			auto* old = Ref();
			if (!old) return;
			auto* want = HullBase();
			if (!want || old->GetBaseObject() == want) { rebased_ = true; return; }
			if (!HullCellLoaded() || seated_ || PlayerAboard() || !old->Get3D()) return;
			auto ref = old->PlaceObjectAtMe(want, persist_);
			if (!ref) { rebased_ = true; logger::info("Hull: rebase failed (placing the new hull)"); return; }
			const RE::NiPoint3 at = old->GetPosition();
			const float yaw = old->data.angle.z;
			ref->data.angle = RE::NiPoint3{ 0.0f, 0.0f, yaw };
			ref->SetPosition(at);
			if (kind_ != kKindRow) {
				ref->extraList.SetExtraFlags(RE::ExtraFlags::Flag::kBlockActivate, true);
				ref->extraList.SetExtraFlags(RE::ExtraFlags::Flag::kBlockPlayerActivate, true);
				ref->extraList.SetExtraFlags(RE::ExtraFlags::Flag::kBlockActivateText, true);
			}
			if (persist_ && mobile_) MakeMobile(ref.get(), at, yaw);
			const RE::FormID was = refId_;
			refId_ = ref->GetFormID();
			old->Disable();
			old->SetDelete(true);
			rebased_ = true;
			logger::info("Hull: {} moved from the vanilla rowboat {:08X} onto our own hull {:08X}", Dims().label, was, refId_);
		}
		int carrierWaitLogs_{ 0 };
		float carrierRetry_{ 0.0f };
		static RE::NiPoint3 CarrierSpot(const RE::NiPoint3& a_at) { return { a_at.x, a_at.y, a_at.z - kCarrierDepth }; }

		// v1.8.31e: a stray carrier (a mannequin standing bare in the water, Miguel's screenshot): one we placed (an FF
		// actor of the carrier base) that is no boat's carrier now, from a boat put away, swapped or reloaded while its
		// actor stayed (Forget() lets go of a boat whose hull is gone, its carrier left standing). Every 2 s, any such
		// actor in the water of an exterior cell is removed. Placed mannequins (player homes, a plugin's own) are never FF
		// refs; the lake boats have no carriers (their model rides on the hull ref); the three boats' own carriers are kept.
		static inline float s_strayCarrierT = 0.0f;
		static void SweepStrayCarriers(float a_dt)
		{
			if ((s_strayCarrierT += a_dt) < 2.0f) return;
			s_strayCarrierT = 0.0f;
			auto* pl = RE::ProcessLists::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!pl || !player) return;
			std::vector<RE::Actor*> gone;
			for (auto& hnd : pl->highActorHandles) {
				auto a = hnd.get();
				if (!a || a.get() == player || (a->GetFormID() >> 24) != 0xFF || a->IsDisabled() || !IsCarrierBase(a->GetActorBase())) continue;
				bool ours = false;
				for (int k = 0; k < kKindCount; ++k) if (Of(k).carrierId_ == a->GetFormID()) ours = true;
				if (ours) continue;
				auto* c = a->GetParentCell();
				if (!c || !c->IsExteriorCell()) continue;
				// and only in the water (a carrier rides under its hull): a mannequin another mod placed on land is left be
				const RE::NiPoint3 at = a->GetPosition();
				float w = 0.0f;
				const bool wet = c->GetWaterHeight(at, w) && std::isfinite(w) && std::abs(w) < 1000000.0f && at.z < w;
				if (!wet) continue;
				gone.push_back(a.get());
			}
			for (auto* a : gone) {
				logger::info("Carrier: stray carrier {:08X} (no boat's) removed", a->GetFormID());
				LakeBoats::DeleteActorLater(a);
			}
		}
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
			CarrierAudio::ids[kind_].store(0, std::memory_order_relaxed);
		}

		// The boat model: a clone of the hull's own nif, physics stripped (the hull ref keeps the collision).
		// v1.8.48: every part found in a boat model is kept until the model changes, told by the model's address. A boat
		// placed again (Dock here, a dock calling it) frees the old model and makes a new one, and the new one can get the
		// old one's address: the parts were not looked up again and the tiller's freed node was turned (the crash at
		// TendSailBoat, Dock here while swimming). Each new model forgets them all.
		void ForgetVisualCaches()
		{
			locksOn_ = nullptr;
			lsOn_ = nullptr;
			sbOn_ = nullptr;
			sbMainT0On_ = nullptr;
			sbJawOn_ = nullptr;
			sbRudderOn_ = nullptr;
			sbRudder_ = nullptr;
			sbTiller_ = nullptr;
			sbFurlOn_ = nullptr;
			anOn_ = nullptr;
			anStowRope_ = nullptr;
			sailOn_ = nullptr;
			bagOn_ = nullptr;
			rollCulledOn_ = nullptr;
			paintedRoot_ = nullptr;
			drawnRoot_ = nullptr;
			tentOn_ = nullptr;
			helmHandLive_ = false;
			mkRoot_.reset();  // v1.8.51: the seat markers were on the old model (it owns them now)
			mkOn_ = nullptr;
		}
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
			// v1.7.87: or the sailing boat (oas model sail, RowRim.ini [Boat] Model=sail; local test)
			const std::string own = ModelPath();
			RE::BSModelDB::Demand(own.c_str(), src, args);
			if (!src) RE::BSModelDB::Demand((std::string("meshes\\") + own).c_str(), src, args);
			if (src) path = ModelPath();
			if (src) ApplyModelGeometry();  // v1.7.94 (v1.8.0: the rowboat's too; v1.8.8: each boat's own)
			if (!src) {  // v1.7.98: was an else of the line above (the rowboat logged "not found" and loaded its nif twice)
				logger::info("Carrier: {} not found, using the hull base's model", own);
				RE::BSModelDB::Demand(path, src, args);
			}
			if (!src) RE::BSModelDB::Demand((std::string("meshes\\") + path).c_str(), src, args);
			if (!src) { logger::warn("Carrier: model '{}' not loaded", path); return out; }
			auto* cloneObj = src->Clone();
			auto* clone = cloneObj ? cloneObj->AsNode() : nullptr;
			if (!clone) { logger::warn("Carrier: clone of '{}' failed", path); return out; }
			StripPhysics(clone);
			clone->local = RE::NiTransform{};
			clone->local.scale = ModelScale();  // v1.8.27: the sailing boat a quarter bigger
			out.reset(clone);
			logger::info("Carrier: boat model '{}' cloned", path);
			ApplyWaterBlock(clone);
			if (Kind() == kKindRow) AttachOars(clone);  // v1.8.26: oars on the rowboat only
			else for (auto& o : oars_) o.reset();
			shownModel_ = ModelPath();
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
		// v1.7.87: the medieval sailing boat (bought model, local test only): its own nif, the helm on its stern bench
		static constexpr const char* kSailBoatModel = "RowRim\\RowRim_SailBoat.nif";
		// v1.8.8: the Viking longship (massive-graphisme, Sketchfab, CC BY 4.0), src\Longship_v1808_Model.md
		static constexpr const char* kLongshipModel = "RowRim\\RowRim_Longship.nif";
		static inline const RE::NiPoint3 kRowHelmSeat{ 0.0f, -215.0f, 35.0f };
		// v1.7.94: the stern bench (top z 71), 15 further forward. v1.8.30i: 6.5 higher (was 41): the feet on the floorboards
		// (top z 47, raised in v1.8.27c; they sank in) and the seat of the pants on the bench
		static inline const RE::NiPoint3 kSailHelmSeat{ 16.0f, -200.0f, 47.5f };  // v1.8.40: 16 to starboard, his hand on the tiller (was 0)
		static inline const RE::NiPoint3 kSailHelmSeatOld{ 0.0f, -200.0f, 47.5f };
		static inline const RE::NiPoint3 kRowRowSeat{ 0.0f, 15.0f, 33.0f };
		static inline const RE::NiPoint3 kSailRowSeat{ 0.0f, -68.0f, 38.0f };  // v1.7.94: the thwart's aft edge (top z 69), feet over the well
		static inline const RE::NiPoint3 kRowOarLock{ 93.3f, 6.7f, 76.5f };
		static inline const RE::NiPoint3 kSailOarLock{ 90.7f, -76.3f, 91.5f };  // v1.7.94: on the gunwale beside the rower (8.3 aft, as the rowboat's)
		static inline const RE::NiPoint3 kSailAftLock{ 81.0f, -180.0f, 83.5f };  // where the shipped oars' blades rest, on the gunwale aft
		// v1.8.8: the longship: the helm on the aftmost sea chest, starboard (top z 86), by the steering oar (x 131, y -905);
		// no rower seat of your own (its crew row it: W / S at the helm); the oarlock pair at the oar ports amidships (y 32)
		static inline const RE::NiPoint3 kLongHelmSeat{ 108.0f, -787.0f, 55.0f };
		static inline const RE::NiPoint3 kLongRowSeat{ 0.0f, 32.0f, 50.0f };
		static inline const RE::NiPoint3 kLongOarLock{ 216.0f, 32.0f, 85.0f };
		static inline const RE::NiPoint3 kLongAftLock{ 200.0f, -110.0f, 85.0f };
		// v1.7.94: each boat's seats and oarlocks, set whenever its model is put on (saves from before keep working)
		void ApplyModelGeometry()
		{
			const int k = Kind();
			if (k == kKindSail) { lampOn_ = false; seat_ = kSailHelmSeat * kSailScale; rowSeat_ = kSailRowSeat * kSailScale; lockLocal_ = kSailOarLock * kSailScale; aftLock_ = kSailAftLock * kSailScale; }  // v1.8.27: scaled
			else if (k == kKindLong) { lampOn_ = false; seat_ = kLongHelmSeat; rowSeat_ = kLongRowSeat; lockLocal_ = kLongOarLock; aftLock_ = kLongAftLock; }
			else {
				lockLocal_ = kRowOarLock; aftLock_ = kAftLock;
				// v1.8.0: seats left at another boat's (a save from a sailing boat session) go back to the rowboat's;
				// a rowboat seat tuned by hand is kept
				auto atSeat = [](const RE::NiPoint3& a, const RE::NiPoint3& b) { return (a - b).Length() < 0.5f; };  // (not "near": a Windows macro)
				if (atSeat(seat_, kSailHelmSeat) || atSeat(seat_, kSailHelmSeatOld) || atSeat(seat_, kLongHelmSeat)) seat_ = kRowHelmSeat;
				if (atSeat(rowSeat_, kSailRowSeat) || atSeat(rowSeat_, kLongRowSeat)) rowSeat_ = kRowRowSeat;
			}
		}
		// v1.7.98: the sailing boat's sail test models: sail (the working one), sailskin (bones), sailcloth (moving cloth);
		// v1.8.8: RowRim.ini [Boat] SailModel (was [Boat] Model)
		static inline const char* kSailModels[3] = { "sail", "sailskin", "sailcloth" };
		static inline const char* kSailModelPaths[3] = { kSailBoatModel, "RowRim\\RowRim_SailBoatSkin.nif", "RowRim\\RowRim_SailBoatCloth.nif" };
		static int SailModelIndex()
		{
			char b[32]{};
			const auto ini = Plankton::IniPath();
			GetPrivateProfileStringA("Boat", "SailModel", "", b, sizeof(b), ini.c_str());
			if (!b[0]) GetPrivateProfileStringA("Boat", "Model", "sailskin", b, sizeof(b), ini.c_str());
			// v1.8.50: the old plain-sails model (RowRim_SailBoat.nif: blanket and sacks, no rudder or tiller) is never used:
			// "oas boat sail" wrote SailModel=sail and put it out. Only the moving cloth test is kept, else the sails on bones.
			// v1.8.53: the moving-cloth test is not packed either: always the sails on bones
			(void)b;
			return 1;  // v1.8.27: the sails on bones (RowRim_SailBoatSkin.nif) are the default
		}
		const char* ModelPath() const
		{
			switch (Kind()) {
			case kKindSail: return kSailModelPaths[SailModelIndex()];
			case kKindLong: return kLongshipModel;
			default: return kOwnHullModel;
			}
		}
		bool SailBoatModel() const { return Kind() != kKindRow; }  // a boat with its own rig (not the rowboat's lug sail)
		// the hull ref's base: the rowboat ShipRowBoat01 (its vanilla collision), the others their own from OarsAndSails.esl
		RE::TESBoundObject* HullBase() const
		{
			if (Kind() == kKindRow) {
				// v1.8.66: our own copy of ShipRowBoat01 (OarsAndSails.esl 0x80A): no vanilla rowboats left in a save
				if (auto* dh = RE::TESDataHandler::GetSingleton())
					if (auto* own = dh->LookupForm<RE::TESObjectSTAT>(kOwnRowHullLocal, "OarsAndSails.esl")) return own;
				RE::TESBoundObject* b = RE::TESForm::LookupByID<RE::TESObjectSTAT>(kRowBoatStatic);
				if (!b) b = RE::TESForm::LookupByEditorID<RE::TESObjectSTAT>("ShipRowBoat01");
				return b;
			}
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh ? dh->LookupForm<RE::TESObjectACTI>(Kind() == kKindSail ? kSailHullLocal : kLongHullLocal, "OarsAndSails.esl") : nullptr;
		}
	public:
		// v1.8.8: each boat is its own: its own hull and collision, model, seats and settings (RowRim.ini [Row] / [RowSail],
		// [SailBoat] / [Sail], [Longship] / [LongshipSail]) and its own page in the settings. One boat is out at a time:
		// oas boat row|sail|longship puts the one out away and places the other where it was (or ahead of you).
		// oas model row|sail|sailskin|sailcloth|longship does the same (the sail test models are the sailing boat's).
		void SetKind(const std::string& a_which)
		{
			const int k = KindFromName(a_which);
			if (k < 0) { Print(std::format("RowRim: the boat is the {} (oas boat row|sail)", Dims().label)); return; }
			if (k != kKindRow && !SailEnabled()) { logger::info("Boat: {} refused (sailing is off: RowRim.ini [Sail] Enabled=1)", a_which); Print("RowRim: only the rowboat while sailing is off (RowRim.ini [Sail] Enabled=1)"); return; }
			const auto ini = Plankton::IniPath();
			if (k == kKindSail) {
				// v1.8.50: only the sail test names choose the model ("sail" is the boat, not the old plain-sails model)
				for (int i = 1; i < 3; ++i)
					if (_stricmp(a_which.c_str(), kSailModels[i]) == 0) WritePrivateProfileStringA("Boat", "SailModel", kSailModels[i], ini.c_str());
			}
			WritePrivateProfileStringA("Boat", "Kind", DimsOf(k).name, ini.c_str());
			// v1.8.26: that boat is the one you use now; the others stay where they are (oas boat brings it out if it is not)
			if (&Of(k) != this) {
				SwitchActive(k, "oas boat");
				Of(k).SetKind(a_which);
				return;
			}
			ApplyModelGeometry();
			if (lampOn_ && k != kKindRow) SetLamp(false);
			LoadRowSettings();  // v1.7.98: each boat's own settings
			sailSpeed_ = -1.0f;
			LoadSailTuning();
			if (visual_ && shownModel_ != ModelPath()) {  // another model of this boat (the sail tests): rebuilt next frame
				if (visual_->parent) {
					RE::NiPointer<RE::NiAVObject> tmp;
					visual_->parent->DetachChild(visual_.get(), tmp);
				}
				visual_.reset();
				wbShown_ = nullptr;
			}
			const bool out = HaveBoat();
			logger::info("Boat: {} ({}), helm seat {:.0f} {:.0f} {:.0f}, rower seat {:.0f} {:.0f} {:.0f}", Dims().label, ModelPath(), seat_.x, seat_.y, seat_.z, rowSeat_.x, rowSeat_.y, rowSeat_.z);
			Print(std::format("RowRim: {}{}", Dims().label, out ? "" : " (oas boat to bring it)"));
		}
		const char* shownModel_{ nullptr };  // v1.8.26: the model the boat was built with
		void SetModel(const std::string& a_which) { SetKind(a_which); }
	private:
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
			if (!a || a->IsDeleted()) {
				// v1.8.66: no carrier (swapped while the boat was not loaded, or an old save's mannequin removed): placed now
				if (carrierOn_ && HullCellLoaded() && (carrierRetry_ -= a_dt) <= 0.0f) {
					carrierRetry_ = 2.0f;
					carrierId_ = 0;
					SpawnCarrier(a_at);
				}
				return;
			}
			// v1.7.63: a save's horse carrier (or one that died) is swapped for a person: removed, a new one placed
			// v1.7.71: also any carrier not of today's base (a townsperson from a test build before the mannequin), once a load
			auto* want = FindCarrierBase(kind_);
			auto* has = a->GetActorBase();
			// v1.8.66: an old version's mannequin is swapped for this boat's own carrier, only with the boat loaded
			const bool other = want && has && has != want && !carrierSwapped_ && HullCellLoaded();
			if (!carrierChecked_) {
				carrierChecked_ = true;
				logger::info("Carrier: {:08X} is '{}' ({:08X}), wanted '{}' ({:08X})", carrierId_, has ? has->GetName() : "?", has ? has->GetFormID() : 0u,
					want ? want->GetName() : "?", want ? want->GetFormID() : 0u);
			}
			if (a->IsDead() || other || (IsHorse(a->GetRace()) && want && !IsHorse(want->race))) {
				carrierSwapped_ = true;
				carrierChecked_ = false;
				logger::info("Carrier: {:08X} replaced ({})", carrierId_, a->IsDead() ? "dead" : other ? "not the mannequin" : "a horse from before v1.7.63");
				DeleteCarrier();
				SpawnCarrier(a_at);
				aiApplied_ = false;
				return;
			}
			// v1.7.80: after sleeping or waiting the game left the carrier in a cell it had unloaded, with no 3D: the boat model
			// rides on it, so the bare hull showed (no clutter, oars, sail). Lost (no 3D, or its cell detached) for half a
			// second: moved to the hull (MoveTo refiles it and loads its 3D), again each second; still lost after 4 s: a new
			// carrier is placed.
			{
				auto* pc = a->GetParentCell();
				// v1.8.57b: only while the boat itself is in a loaded cell (a docked boat far away is not "lost": its carrier was
				// moved and replaced every few seconds, the boat model rebuilt each time)
				const bool lost = HullCellLoaded() && (!a->Get3D() || !pc || !pc->IsAttached());
				const float before = carrierLost_;
				carrierLost_ = lost ? carrierLost_ + a_dt : 0.0f;
				if (lost && carrierLost_ > 0.5f && std::floor(carrierLost_ - 0.5f) != std::floor(before - 0.5f)) {
					if (auto* hull = Ref()) {
						a->MoveTo(hull);
						logger::info("Carrier: {:08X} lost ({}, cell {:08X}{}) after {:.1f} s: moved to the hull", carrierId_, a->Get3D() ? "3D in" : "no 3D",
							pc ? pc->GetFormID() : 0u, pc && pc->IsAttached() ? "" : " detached", carrierLost_);
					}
				}
				if (carrierLost_ > 4.0f) {
					logger::info("Carrier: {:08X} still lost after 4 s: a new one placed", carrierId_);
					carrierLost_ = 0.0f;
					// v1.8.57: the model riding on the player stays where it is (no rebuild, no blink)
					RE::NiPointer<RE::NiNode> keep = VisualOnPlayer() ? visual_ : RE::NiPointer<RE::NiNode>();
					if (keep) visual_.reset();
					DeleteCarrier();
					SpawnCarrier(a_at);
					if (keep) visual_ = keep;
					aiApplied_ = false;
					return;
				}
			}
			CarrierAudio::ids[kind_].store(carrierId_, std::memory_order_relaxed);
			MuteCarrier(a);  // also covers restored saves and process/3D reloads
			if (!aiApplied_) { a->EnableAI(carrierAI_); a->SetCollision(false); ShieldCarrier(a); CalmCarrier(a); aiApplied_ = true; }
			a->SetPosition(CarrierSpot(a_at), true);
			// v0.3.2: with AI off, SetPosition moved the actor (its cell followed) but not its 3D: the horse's model stayed at
			// the spawn spot (bound radius grew with the distance rowed) and the game faded the actor out by that spot's
			// distance, boat and all. Its 3D is now put where the actor is every frame.
			a->Update3DPosition(true);
			if (TendRide(a, a_at, a_rot, a_dt)) return;  // v1.8.57: third person aboard, the model rides on the player
			auto* aroot = a->Get3D();
			if (!aroot) return;
			if (auto* fade = aroot->AsFadeNode()) fade->GetRuntimeData().currentFade = 1.0f;  // not loaded yet: the hidden-collision rule keeps the hull ref drawn meanwhile
			auto* anode = aroot->AsNode();
			if (!anode) return;
			if (!visual_) {
				visual_ = MakeVisual();
				ForgetVisualCaches();  // v1.8.48: the new model may sit where the old one was freed (the same address)
			}
			if (!visual_) {
				// v1.8.31e: no boat model to ride on it yet (its hull not loaded): the mannequin is still kept hidden (it stood
				// bare in the water)
				for (auto& child : anode->GetChildren())
					if (child && !child->flags.any(RE::NiAVObject::Flag::kHidden)) child->flags.set(RE::NiAVObject::Flag::kHidden);
				return;
			}
			if (visual_->parent != anode) {  // first time, or the carrier's 3D was reloaded
				if (visual_->parent) {
					RE::NiPointer<RE::NiAVObject> tmp;
					visual_->parent->DetachChild(visual_.get(), tmp);
				}
				visual_->local.scale = ModelScale();  // v1.8.57: as built (it may come back from the player)
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
			DressVisual(anode, true, a_at, a_rot, a_dt);
		}
		// v1.8.56 / v1.8.57: the model kept drawn, painted and placed, on the carrier or on the player
		void DressVisual(RE::NiNode* a_parent, bool a_flagParent, const RE::NiPoint3& a_at, const RE::NiMatrix3& a_rot, float a_dt)
		{
			KeepOpaque(visual_.get());
			// v1.8.56: the boat is never culled. The sailing boat (a skinned model, its sails rebuilt every frame) flickered or
			// vanished for a frame when turning on heavier lists (occlusion, eFPS): the renderer tested a bound one frame stale.
			// Always drawn: the carrier's root and every node and shape of the boat, once per model
			if (a_flagParent && !a_parent->flags.any(RE::NiAVObject::Flag::kAlwaysDraw)) a_parent->flags.set(RE::NiAVObject::Flag::kAlwaysDraw);
			if (drawnRoot_ != visual_.get()) {
				AlwaysDraw(visual_.get());
				drawnRoot_ = visual_.get();
			}
			if (paintedRoot_ != visual_.get() || paintedAs_ != paint_) {  // v1.1.0: the OAS paint (rr paint ...), once per model / paint
				Paint::Apply(visual_.get(), paint_);
				paintedRoot_ = visual_.get();
				paintedAs_ = paint_;
			}
			PlaceNode(visual_.get(), a_at, a_rot);
			drawAt_ = a_at;  // v1.8.57: kept for re-placing it on the player after the body tilt
			drawRot_ = a_rot;
			haveDraw_ = true;
			TendLamp(a_dt);
			TendTent(a_dt);  // v1.7.34
			if (IsActive()) Crew::g.dogStern = kind_ == kKindRow && Gear::BedrollShown();  // v1.7.36: the dog sits at the stern while the bed is out
			// the carrier's bound must take in the boat, or the renderer culls them both by the horse alone (a frozen actor
			// does not rebuild it): rebuilt from the boat up to the scene root every frame
			for (RE::NiAVObject* n = a_parent; n; n = n->parent) n->UpdateWorldBound();
		}

		// ---- v1.8.57d: one-frame jumps of the seated body ----
		// Miguel's frames (Elysium / NEFARAM, seated at the helm, moving the camera): for one frame he and the boat riding on
		// him were turned and moved on the water, the camera and the sea steady. Not in vanilla: a mod sets his 3D for a frame.
		// Right before the draw, his root is checked against the boat as drawn: a jump of more than 12 units or 0.12 rad
		// since last frame is put back where he was on the boat (up to 3 frames running; a real change is taken after that).
		bool spikeHave_{ false };
		int spikeOdd_{ 0 }, spikeLogs_{ 0 };
		RE::NiPoint3 spikePos_{};
		RE::NiMatrix3 spikeRot_{};
	public:
		void RootSpikeGuard()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* root = player ? player->Get3D(false) : nullptr;
			if (!seated_ || !root || !root->parent || !havePose_ || gliding_) { spikeHave_ = false; return; }  // v1.8.71: the glide moves it on purpose
			RE::NiPoint3 bp;
			RE::NiMatrix3 R;
			DrawPose(bp, R);
			const RE::NiMatrix3 Rt = R.Transpose();
			const RE::NiPoint3 lp = Rt * (root->world.translate - bp);
			const RE::NiMatrix3 lr = Rt * root->world.rotate;
			if (spikeHave_) {
				const float dp = (lp - spikePos_).Length();
				const RE::NiPoint3 f0 = spikeRot_ * RE::NiPoint3{ 0.0f, 1.0f, 0.0f }, f1 = lr * RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
				const float ang = std::acos(std::clamp(f0.x * f1.x + f0.y * f1.y + f0.z * f1.z, -1.0f, 1.0f));
				if ((dp > 12.0f || ang > 0.12f) && spikeOdd_ < 3) {
					++spikeOdd_;
					const RE::NiPoint3 wp = bp + R * spikePos_;
					const RE::NiMatrix3 wr = R * spikeRot_;
					auto* par = root->parent;
					const RE::NiMatrix3 pinv = par->world.rotate.Transpose();
					const float sc = par->world.scale > 0.0f ? par->world.scale : 1.0f;
					root->local.rotate = pinv * wr;
					root->local.translate = (pinv * (wp - par->world.translate)) / sc;
					RE::NiUpdateData data{};
					root->Update(data);
					ReplaceRiding();
					if (spikeLogs_++ < 40) logger::info("Seat: body jumped {:.1f} units / {:.2f} rad in a frame (frame {} of it), put back on the seat", dp, ang, spikeOdd_);
					return;
				}
				if (spikeOdd_ >= 3) logger::info("Seat: body moved {:.1f} units / {:.2f} rad and stayed: taken as real", dp, ang);
			}
			spikePos_ = lp;
			spikeRot_ = lr;
			spikeHave_ = true;
			spikeOdd_ = 0;
		}
	private:
		// ---- v1.8.57: the boat model rides on the player ----
		// On Elysium the hidden carrier was lost (no 3D, a detached cell) hundreds of times a session, and the boat drawn on
		// it blinked out or lagged. In third person aboard the boat in use, the model is now a child of the player's own 3D,
		// placed where the boat is every frame: it moves with you, whatever the carrier does. The carrier stays (sound, docked
		// boats), every part of it hidden. First person (the third person body is not drawn) and off the boat: on the carrier,
		// as before. Kept 0.75 s after you are last aboard (a jump on deck), dropped at once if the boat is 2000 away.
		bool HullCellLoaded() const
		{
			auto* r = Ref();
			auto* c = r ? r->GetParentCell() : nullptr;
			return c && c->IsAttached();
		}
		float rideHold_{ 0.0f };
		RE::NiPoint3 drawAt_{};
		RE::NiMatrix3 drawRot_{};
		bool haveDraw_{ false };
		// the body tilt turns the player's root, and the boat riding on it with it: put back where the boat is
		void ReplaceRiding()
		{
			if (haveDraw_ && VisualOnPlayer()) PlaceNode(visual_.get(), drawAt_, drawRot_);
		}
		float rideHideTimer_{ 0.0f };
		float standIn_{ 0.0f };  // v1.8.64
		int standInLogs_{ 0 };
		static RE::NiNode* PlayerRideNode()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* o = p ? p->Get3D(false) : nullptr;
			return o ? o->AsNode() : nullptr;
		}
		bool VisualOnPlayer() const
		{
			if (!visual_ || !visual_->parent) return false;
			auto* pn = PlayerRideNode();
			return pn && visual_->parent == pn;
		}
		void UnrideVisual(const char* a_why)
		{
			rideHold_ = 0.0f;
			if (!VisualOnPlayer()) return;
			RE::NiPointer<RE::NiAVObject> tmp;
			visual_->parent->DetachChild(visual_.get(), tmp);
			visual_->local.scale = ModelScale();
			logger::info("Carrier: boat model off the player ({})", a_why);
		}
		// the boat in use only; any other boat (or one far away) never rides on you
		void GuardRide(bool a_inUse)
		{
			if (!VisualOnPlayer()) return;
			if (!a_inUse) { UnrideVisual("not the boat in use"); return; }
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !havePose_ || !carrierOn_) { UnrideVisual("no boat pose"); return; }
			const auto pp = p->GetPosition();
			if (std::hypot(pp.x - pos_.x, pp.y - pos_.y) > 2000.0f) UnrideVisual("the boat is far away");
		}
		bool TendRide(RE::Actor* a_carrier, const RE::NiPoint3& a_at, const RE::NiMatrix3& a_rot, float a_dt)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* cam = RE::PlayerCamera::GetSingleton();
			const bool third = cam && !cam->IsInFirstPerson();
			const bool inReach = p && std::hypot(p->GetPosition().x - a_at.x, p->GetPosition().y - a_at.y) < 2000.0f;
			if (IsActive() && inReach && (seated_ || PlayerAboard())) rideHold_ = 0.75f;
			else rideHold_ = (std::max)(0.0f, rideHold_ - a_dt);
			// v1.8.64: mode 1 (default): the model stays on the carrier and only stand in on you while the carrier is not in the
			// scene (no 3D, its 3D off the scene, its cell detached: the blink), back on the carrier the moment it is
			const int mode = HelmCamSet::Get().rideMode;
			bool use = mode == 2;
			if (mode == 1) {
				auto* r3 = a_carrier ? a_carrier->Get3D() : nullptr;
				auto* pc = a_carrier ? a_carrier->GetParentCell() : nullptr;
				const bool showing = r3 && r3->parent && pc && pc->IsAttached();
				if (!showing) {
					if (standIn_ <= 0.0f && standInLogs_ < 40) { ++standInLogs_; logger::info("Carrier: blinked out ({}), the boat model stands in on you", !r3 ? "no 3D" : !r3->parent ? "3D off the scene" : "cell detached"); }
					standIn_ = 0.15f;
				} else {
					standIn_ = (std::max)(0.0f, standIn_ - a_dt);
				}
				use = standIn_ > 0.0f;
			}
			RE::NiNode* pn = rideHold_ > 0.0f && third && inReach && use ? PlayerRideNode() : nullptr;
			if (!pn) {
				if (VisualOnPlayer()) UnrideVisual(mode == 0 ? "switched off" : mode == 1 && third && rideHold_ > 0.0f ? "the carrier is back" : !third ? "first person" : "off the boat");
				return false;
			}
			if (!visual_) {
				visual_ = MakeVisual();
				ForgetVisualCaches();
				if (!visual_) return false;
			}
			if (visual_->parent != pn) {
				if (visual_->parent) {
					RE::NiPointer<RE::NiAVObject> tmp;
					visual_->parent->DetachChild(visual_.get(), tmp);
				}
				pn->AttachChild(visual_.get(), true);
				hiddenKids_ = 0;
				logger::info("Carrier: boat model rides on the player (third person aboard)");
			}
			// the player's own scale taken out: the boat stays its built size
			const float ps = pn->world.scale > 0.0f ? pn->world.scale : 1.0f;
			visual_->local.scale = ModelScale() / ps;
			// the carrier: all of it hidden (twice a second: equipment can add parts)
			rideHideTimer_ += a_dt;
			if (a_carrier && (hiddenKids_ == 0 || rideHideTimer_ > 0.5f)) {
				rideHideTimer_ = 0.0f;
				if (auto* ar = a_carrier->Get3D(); ar && ar->AsNode()) {
					for (auto& child : ar->AsNode()->GetChildren())
						if (child && !child->flags.any(RE::NiAVObject::Flag::kHidden)) child->flags.set(RE::NiAVObject::Flag::kHidden);
					hiddenKids_ = 1;
				}
			}
			DressVisual(pn, false, a_at, a_rot, a_dt);
			return true;
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
			static constexpr bool kTentOn = true;  // v1.8.24: the tent is back (v1.7.40 had it off for that release)
			const bool bedOut = kind_ == kKindRow && Gear::BedrollShown();  // v1.8.26: the rowboat's bed
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
			if (auto* base = LakeBoats::LampBase()) {
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
					LakeBoats::PlaceAt(hull, LakeBoats::LightBase(), "bed light", bedLight_);
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
			auto* base = LakeBoats::LampBase();
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
			if (auto* base = LakeBoats::LampBase()) {
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
					LakeBoats::PlaceAt(hull, LakeBoats::LightBase(), "player light", lampLight_);
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

		static void AlwaysDraw(RE::NiAVObject* a_obj, int a_depth = 0)
		{
			if (!a_obj || a_depth > 16) return;
			a_obj->flags.set(RE::NiAVObject::Flag::kAlwaysDraw);
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) AlwaysDraw(child.get(), a_depth + 1);
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
		// ---- v1.8.9c: standing and jumping aboard a moving boat ----
		// Walking and running are the engine's alone (it carries you on the hull's collision).
		// Standing still: your spot on the deck is held. Once you have stood still 0.15 s your spot (boat space) is kept; if
		// you are found off it (the bump back when the hull's collision reloads on a cell hop, or any other slip), you are
		// put back on it, as the boat is now. Nothing is added while you are on your spot (the engine carries you).
		// Jumping (Jump key, standing aboard): ride: the boat's velocity at your spot is part of your own every frame
		// in the air (the last frame's share taken out first: it never piles up), or momentum: added once as you leave
		// the deck. Your own jump (up and down) is the engine's. v1.8.9's position moves in the air held you down.
		// RowRim.ini [Boat] Carry 0 off / 1 hold your spot standing still; [Boat] CarryJump 0 off / 1 ride / 2 momentum.
		// oas carry on|off, oas carry jump off|ride|momentum.
		int carryMode_{ -1 }, jumpMode_{ 1 };
		float carryLogT_{ 0.0f };
		int carryN_[3]{};  // spot restored, jump frames, momentum given
		bool holdHave_{ false };
		RE::NiPoint3 holdLocal_{};
		float stillT_{ 0.0f };
		bool jumping_{ false };
		float jumpT_{ 0.0f }, jumpLastZ_{ 0.0f };
		int jumpStill_{ 0 };
		RE::NiPoint3 jumpAdded_{};  // ride: the boat's velocity added last frame (havok units)
		bool jumpGiven_{ false };
		void LoadCarry()
		{
			if (carryMode_ >= 0) return;
			const auto ini = Plankton::IniPath();
			carryMode_ = std::clamp(static_cast<int>(GetPrivateProfileIntA("Boat", "Carry", 1, ini.c_str())), 0, 1);
			jumpMode_ = std::clamp(static_cast<int>(GetPrivateProfileIntA("Boat", "CarryJump", 1, ini.c_str())), 0, 2);
			logger::info("Carry: standing still {}, jumps {} (RowRim.ini [Boat] Carry / CarryJump)", carryMode_ ? "held" : "the engine's", kJumpNames[jumpMode_]);
		}
	public:
		static inline const char* kJumpNames[3] = { "off", "ride", "momentum" };
		int CarryMode() { LoadCarry(); return carryMode_; }
		int JumpMode() { LoadCarry(); return jumpMode_; }
		void SetCarry(const std::string& a_what, const std::string& a_arg)
		{
			LoadCarry();
			const auto ini = Plankton::IniPath();
			if (a_what == "jump") {
				int m = -1;
				for (int i = 0; i < 3; ++i) if (a_arg == kJumpNames[i]) m = i;
				if (a_arg == "on") m = 1;
				if (m < 0) { Print(std::format("RowRim: jumps {} (oas carry jump off|ride|momentum)", kJumpNames[jumpMode_])); return; }
				jumpMode_ = m;
				WritePrivateProfileStringA("Boat", "CarryJump", std::to_string(m).c_str(), ini.c_str());
			} else if (a_what == "on" || a_what == "off" || a_what == "position" || a_what == "velocity") {
				carryMode_ = a_what == "off" ? 0 : 1;
				WritePrivateProfileStringA("Boat", "Carry", carryMode_ ? "1" : "0", ini.c_str());
			} else { Print(std::format("RowRim: standing still {}, jumps {} (oas carry on|off, oas carry jump off|ride|momentum)", carryMode_ ? "held" : "off", kJumpNames[jumpMode_])); return; }
			holdHave_ = false;
			Print(std::format("RowRim: standing still on a moving boat: {}, jumps: {}", carryMode_ ? "held on your spot" : "the game's", kJumpNames[jumpMode_]));
		}
		void OnJumpKey()
		{
			if (seated_ || !HaveBoat() || !havePose_ || !PlayerAboard()) return;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			jumping_ = true;
			jumpT_ = 0.0f;
			jumpStill_ = 0;
			jumpLastZ_ = player->GetPosition().z;
			jumpAdded_ = {};
			jumpGiven_ = false;
			holdHave_ = false;
		}
	private:
		bool PlayerMoving(RE::PlayerCharacter* a_p) const
		{
			if (inFwd_ || inBack_ || inLeft_ || inRight_ || std::hypot(stickX_, stickY_) > 0.2f) return true;
			auto* st = a_p->AsActorState();
			return st && (st->actorState1.movingForward || st->actorState1.movingBack || st->actorState1.movingLeft || st->actorState1.movingRight);
		}
		void TendCarry(const RE::NiPoint3& a_at, const RE::NiMatrix3& a_rot, float a_dt)
		{
			LoadCarry();
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cc = player ? player->GetCharController() : nullptr;
			if (!player || !cc || seated_ || !havePose_ || a_dt <= 0.0f) { holdHave_ = false; jumping_ = false; return; }
			auto* st = player->AsActorState();
			const bool aboard = PlayerAboard() && !(st && st->IsSwimming());
			if (!aboard) { holdHave_ = false; jumping_ = false; return; }
			const RE::NiPoint3 p = player->GetPosition();
			const float hs = RE::bhkWorld::GetWorldScale();
			// ---- a jump ----
			if (jumping_) {
				jumpT_ += a_dt;
				jumpStill_ = std::abs(p.z - jumpLastZ_) < 0.4f ? jumpStill_ + 1 : 0;
				jumpLastZ_ = p.z;
				const bool landed = jumpT_ > 1.5f || (jumpT_ > 0.3f && jumpStill_ >= 3);
				RE::hkVector4 v;
				cc->GetLinearVelocityImpl(v);
				float f[4]{};
				_mm_storeu_ps(f, v.quad);
				// the boat's velocity at your spot this frame (its move and turn), level
				const RE::NiPoint3 local = lastRot_.Transpose() * (p - lastAt_);
				RE::NiPoint3 bv = (a_at + a_rot * local - p) / a_dt;
				bv.z = 0.0f;
				if (bv.Length() > 2000.0f) bv = {};
				if (landed) {
					if (jumpMode_ == 1) cc->SetLinearVelocityImpl(RE::hkVector4(f[0] - jumpAdded_.x, f[1] - jumpAdded_.y, f[2], 0.0f));  // the deck has you again
					jumping_ = false;
					jumpAdded_ = {};
				} else if (jumpMode_ == 1) {
					const RE::NiPoint3 add{ bv.x * hs, bv.y * hs, 0.0f };
					cc->SetLinearVelocityImpl(RE::hkVector4(f[0] - jumpAdded_.x + add.x, f[1] - jumpAdded_.y + add.y, f[2], 0.0f));
					jumpAdded_ = add;
					++carryN_[1];
				} else if (jumpMode_ == 2 && !jumpGiven_ && jumpT_ > 0.05f && p.z > jumpLastZ_ - 0.01f) {
					cc->SetLinearVelocityImpl(RE::hkVector4(f[0] + bv.x * hs, f[1] + bv.y * hs, f[2], 0.0f));
					jumpGiven_ = true;
					++carryN_[2];
				}
				holdHave_ = false;
				stillT_ = 0.0f;
			}
			// ---- standing still: your spot held ----
			// v1.8.9d: the log had the spot restored every frame (the engine does not carry a player standing still on the
			// keyframed hull reliably): a position snap every frame was the shaking. Now your controller is given the boat's
			// own velocity at your spot plus a gentle pull back onto the spot (8/s), as the boat itself is driven: smooth,
			// and it settles whether the engine carries you or not. A snap only if you are far off (over 60).
			else if (carryMode_ == 1 && !PlayerMoving(player)) {
				const RE::NiPoint3 here = lastRot_.Transpose() * (p - lastAt_);  // your spot on the boat as it is now
				stillT_ += a_dt;
				if (!holdHave_) {
					if (stillT_ >= 0.15f) { holdHave_ = true; holdLocal_ = here; }
				} else {
					RE::NiPoint3 spot = lastAt_ + lastRot_ * RE::NiPoint3{ holdLocal_.x, holdLocal_.y, here.z };
					RE::NiPoint3 e = spot - p;
					e.z = 0.0f;
					if (e.Length() > 60.0f) {
						holdHave_ = false;  // pushed far (a door, an NPC, a fall): a new spot once still again
						stillT_ = 0.0f;
					} else {
						RE::NiPoint3 bv = (a_at + a_rot * here - (lastAt_ + lastRot_ * here)) / a_dt;  // the boat's move at your spot
						bv.z = 0.0f;
						if (bv.Length() > 2000.0f) bv = {};
						const RE::NiPoint3 want = bv + e * 8.0f;
						RE::hkVector4 v;
						cc->GetLinearVelocityImpl(v);
						float f[4]{};
						_mm_storeu_ps(f, v.quad);
						cc->SetLinearVelocityImpl(RE::hkVector4(want.x * hs, want.y * hs, f[2], 0.0f));
						if (e.Length() > 1.0f) ++carryN_[0];
					}
				}
			} else {
				holdHave_ = false;
				stillT_ = 0.0f;
			}
			if ((carryN_[0] || carryN_[1] || carryN_[2]) && (carryLogT_ -= a_dt) <= 0.0f) {
				carryLogT_ = 2.0f;
				logger::info("Carry: standing {} frames more than 1 off the spot, {} jump frames ridden, {} momentum given", carryN_[0], carryN_[1], carryN_[2]);
				carryN_[0] = carryN_[1] = carryN_[2] = 0;
			}
		}

		// v1.8.71: the stand glide: seat to standing spot in the boat's frame, then collision back, the boat's speed and the nudge
		bool TendStandGlide(const RE::NiPoint3& a_at, const RE::NiMatrix3& a_rot)
		{
			if (!standGlide_) return false;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) { standGlide_ = false; return false; }
			const float t = (rideTime_ - standAt_) / kStandGlide;
			const float k = Smooth(t);
			player->SetPosition(a_at + a_rot * (standFrom_ + (standTo_ - standFrom_) * k), true);
			auto* cc = player->GetCharController();
			if (t < 1.0f) {
				if (cc) cc->SetLinearVelocityImpl(RE::hkVector4());
				return true;
			}
			standGlide_ = false;
			player->SetCollision(true);
			if (cc) {
				const float hs = RE::bhkWorld::GetWorldScale();
				const float hd = yaw_ + (bowFlip_ ? kPi : 0.0f);
				cc->SetLinearVelocityImpl(RE::hkVector4(std::sin(hd) * speed_ * hs, std::cos(hd) * speed_ * hs, 0.0f, 0.0f));
			}
			nudgeTime_ = nudgeOn_ ? 0.0f : -1.0f;
			nudgeFrames_ = 0;
			logger::info("Seat: glide out done, standing");
			return true;
		}

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
			// v1.8.29b: only a real body (a boat whose hull is not loaded has none, and nor does the player in the air: they
			// matched, so a far away boat counted you aboard and took the boat in use from under you as you stood up)
			// v1.8.35: in the water you are never aboard: the support body is left over from the deck you jumped off (the
			// Board prompt went missing)
			if (auto* st = player->AsActorState(); st && st->IsSwimming()) return false;
			const RE::NiPoint3 l = havePose_ ? PlayerLocal(player) : RE::NiPoint3{};
			const auto& dm = Dims();  // v1.8.8: each boat its own size
			if (auto* cc = player->GetCharController(); cc) {
				const auto* hb = HkBody();
				if (hb && cc->supportBody.get() == hb && (!havePose_ || l.z > dm.aboardZlo)) return true;
			}
			if (!havePose_) return false;
			return std::abs(l.x) < dm.aboardX && l.y > dm.aboardYlo && l.y < dm.aboardYhi && l.z > dm.aboardZlo && l.z < dm.aboardZhi;
		}

		// v1.8.72: movement is no longer switched off seated: with it off the HUD goes to its movement-disabled mode, which
		// hides the compass (Miguel: no compass at the helm). The walk is held instead: the game's move input zeroed every
		// frame seated (HoldMoveInput), before the player update; the seat glue holds the body as before.
		// [Helm] KeepCompass=0 goes back to switching movement off.
		static bool KeepCompass()
		{
			static const bool s_on = GetPrivateProfileIntA("Helm", "KeepCompass", 1, Plankton::IniPath().c_str()) != 0;
			return s_on;
		}
		static void SetControls(bool a_enable)
		{
			if (auto* cm = RE::ControlMap::GetSingleton()) {
				for (auto flag : { RE::ControlMap::UEFlag::kMovement, RE::ControlMap::UEFlag::kFighting, RE::ControlMap::UEFlag::kJumping,
						 RE::ControlMap::UEFlag::kSneaking, RE::ControlMap::UEFlag::kActivate }) {
					if (flag == RE::ControlMap::UEFlag::kMovement && KeepCompass() && !a_enable) continue;
					cm->ToggleControls(flag, a_enable, a_enable);  // back on at once when standing up
				}
			}
		}
	public:
		// v1.8.74: the spin-outs came back when the carriers got their own races (v1.8.66): mods that leave the mannequin
		// alone by its race no longer know ours. One way a camera dives under you and looks up is the game camera's target
		// set to the hidden carrier under the boat. The target is put back to the player every frame it is a carrier, and
		// logged ("Camera: target was ..."); any other target while seated is logged only.
		int camTgtLogs_{ 0 };
		RE::FormID camTgtLast_{ 0 };
		void GuardCameraTarget()
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			auto* pl = RE::PlayerCharacter::GetSingleton();
			if (!cam || !pl) return;
			auto tgt = cam->cameraTarget.get();
			RE::Actor* t = tgt.get();
			if (!t || t == pl) { camTgtLast_ = 0; return; }
			const bool carrier = IsCarrierBase(t->GetActorBase());
			if (!carrier && !seated_) return;
			if (t->GetFormID() != camTgtLast_ && camTgtLogs_ < 40) {
				++camTgtLogs_;
				logger::info("Camera: target was {:08X} '{}' (base {:08X}{}), {}", t->GetFormID(), t->GetName(),
					t->GetActorBase() ? t->GetActorBase()->GetFormID() : 0u, carrier ? ", our carrier" : "", carrier ? "put back on the player" : "left alone");
			}
			camTgtLast_ = t->GetFormID();
			if (carrier) cam->cameraTarget = RE::ActorHandle(pl);
		}
		void HoldMoveInput() const
		{
			if (!seated_ || !KeepCompass()) return;
			if (auto* pc = RE::PlayerControls::GetSingleton()) {
				pc->data.moveInputVec = RE::NiPoint2{ 0.0f, 0.0f };
				pc->data.autoMove = false;
			}
		}
	private:

		// v1.8.40: at the sailing boat's helm the helm clip (HelmTiller_Loop.hkx: sat by the tiller, the right hand on its
		// handle, the left on the thigh) replaces the chair sit, a moment after sitting; refused (the behaviours not built
		// with it): tried again every 0.5 s, 10 times, then the chair sit stays. Ended by the stand up's release events.
		// v1.8.41: three poses, each with the hand round the handle where the tiller is: at rest, the tiller over to the
		// left (steering right, HelmTiller_Left) and over to the right (steering left, HelmTiller_Right). Steering switches
		// the pose (the behaviour blends them over 0.6 s) and the tiller is turned over the same 0.6 s, so the hand stays on
		// it the whole way (no bones bent by the plugin: v1.8.40's did).
	public:
		// v1.8.65: Character Behaviors Enhanced (Nexus report on 1.0.86): seated, turning, backing or stopping the boat made
		// the helmsman stand / hop. Two tools:
		//  - CalmGraph: every frame seated, before and after the game's update, the body's movement values (Speed,
		//    Direction, TurnDelta and their damped / sampled copies) are set to rest, so a behaviour overhaul that starts
		//    turn / stop / step clips from them has nothing to start from (the boat moves and turns him, he does not walk).
		//  - GraphWatch: around each tiller pose change and each big change of the boat's speed or turn, the body's values
		//    are logged every 0.1 s for 1.2 s ("Graph:" lines), so a test with CBE shows what set him off.
		static void CalmGraph(RE::Actor* a_actor)
		{
			if (!a_actor) return;
			for (const char* v : { "Speed", "SpeedSampled", "SpeedDamped", "Direction", "DirectionDamped", "TurnDelta", "TurnDeltaDamped", "TurnDeltaSmooth", "SpeedAcc" })
				a_actor->SetGraphVariableFloat(v, 0.0f);
			a_actor->SetGraphVariableBool("IsMoving", false);
		}
		void CalmSeated()
		{
			if (!seated_ || !HelmCamSet::Get().calmGraph) return;
			CalmGraph(RE::PlayerCharacter::GetSingleton());
		}
		float gwLeft_{ 0.0f }, gwTick_{ 0.0f };
		int gwLines_{ 0 };
		float gwSpeed_{ 0.0f }, gwTurn_{ 0.0f };
		void GraphWatch(RE::Actor*, const char* a_why)
		{
			static const bool s_on = GetPrivateProfileIntA("Helm", "GraphLog", 0, Plankton::IniPath().c_str()) != 0;
			if (!s_on || gwLines_ > 400) return;
			logger::info("Graph: watch ({})", a_why);
			gwLeft_ = 1.2f;
			gwTick_ = 0.0f;
		}
		void TendGraphWatch(float a_dt, RE::PlayerCharacter* a_player)
		{
			// v1.8.67: off for the release (RowRim.ini [Helm] GraphLog=1 for a report)
			static const bool s_on = GetPrivateProfileIntA("Helm", "GraphLog", 0, Plankton::IniPath().c_str()) != 0;
			if (!s_on || !seated_ || !a_player) { gwLeft_ = 0.0f; return; }
			// big changes of the boat's way or turn start a watch too
			const float sp = std::abs(speed_), tr = std::abs(turn_);
			if (std::abs(sp - gwSpeed_) > 60.0f || std::abs(tr - gwTurn_) > 0.25f) {
				GraphWatch(a_player, std::format("boat speed {:.0f} -> {:.0f}, turn {:.2f} -> {:.2f}", gwSpeed_, sp, gwTurn_, tr).c_str());
				gwSpeed_ = sp;
				gwTurn_ = tr;
			}
			if (gwLeft_ <= 0.0f) return;
			gwLeft_ -= a_dt;
			if ((gwTick_ -= a_dt) > 0.0f) return;
			gwTick_ = 0.1f;
			float speed = 0, dir = 0, turn = 0;
			bool sit = false, jump = false, drive = false, moving = false;
			int state = 0;
			a_player->GetGraphVariableFloat("Speed", speed);
			a_player->GetGraphVariableFloat("Direction", dir);
			a_player->GetGraphVariableFloat("TurnDelta", turn);
			a_player->GetGraphVariableBool("bIsSitting", sit);
			a_player->GetGraphVariableBool("bInJumpState", jump);
			a_player->GetGraphVariableBool("bAnimationDriven", drive);
			a_player->GetGraphVariableBool("IsMoving", moving);
			a_player->GetGraphVariableInt("iState", state);
			const auto* st = a_player->AsActorState();
			++gwLines_;
			logger::info("Graph: Speed {:.1f} Dir {:.2f} TurnDelta {:.3f} | sitting {} jump {} driven {} moving {} iState {} | sit state {} | helm clip {} pose {} | boat {:.0f} turn {:.2f}",
				speed, dir, turn, sit, jump, drive, moving, state, st ? static_cast<int>(st->actorState1.sitSleepState) : -1, helmAnimOn_, helmPose_, speed_, turn_);
		}
	private:
		void TendHelmAnim(float a_dt, RE::PlayerCharacter* a_player)
		{
			const bool want = seated_ && seatKind_ == SeatKind::kHelm && Kind() == kKindSail;
			if (!want) {
				if (helmAnimOn_) {
					helmAnimOn_ = false;
					a_player->NotifyAnimationGraph(kRowStopEvent);
					TrySitAnim(a_player);
					logger::info("Helm anim: ended (not at the sailing boat's helm)");
				}
				return;
			}
			if (helmAnimOn_) {
				const int pose = !HelmCamSet::Get().helmPoses ? 0 : helmSteer_ > 0.25f ? 1 : helmSteer_ < -0.25f ? -1 : 0;  // v1.8.65: poses can be off
				if (pose != helmPose_ && rideTime_ - helmPoseAt_ >= 0.05f) {
					GraphWatch(a_player, pose > 0 ? "pose left" : pose < 0 ? "pose right" : "pose rest");  // v1.8.42: at once (was 0.25 s: let go early, the hand went on to the end first)
					const char* ev = pose > 0 ? kHelmLeftEvent : pose < 0 ? kHelmRightEvent : kHelmStartEvent;
					if (a_player->NotifyAnimationGraph(ev)) {
						helmPoseFromA_ = helmTillerA_;
						helmPose_ = pose;
					} else if (!helmPoseWarned_) {
						helmPoseWarned_ = true;
						logger::info("Helm anim: {} rejected (the behaviour has only the rest pose: run Pandora with RowRim's)", ev);
					}
					helmPoseAt_ = rideTime_;
				}
				// the tiller over to the pose's side over the clip's blend (0.6 s), eased at both ends
				const float t = std::clamp((rideTime_ - helmPoseAt_) / 0.6f, 0.0f, 1.0f), k = t * t * (3.0f - 2.0f * t);
				helmTillerA_ = helmPoseFromA_ + (static_cast<float>(helmPose_) * 0.45f - helmPoseFromA_) * k;
				return;
			}
			if (sitDeferred_ || rideTime_ - sitAt_ < 0.4f) return;  // v1.8.71: not while gliding onto the seat
			if ((helmRetry_ -= a_dt) > 0.0f) return;
			if (!helmEverOk_ && helmRejects_ >= 10) return;
			if (a_player->NotifyAnimationGraph(kHelmStartEvent)) {
				helmAnimOn_ = true;
				helmEverOk_ = true;
				helmAnimAt_ = rideTime_;
				helmPose_ = 0;
				helmPoseAt_ = rideTime_;
				helmPoseFromA_ = helmTillerA_ = helmHandA_ = rudderA_;
				helmPhiHave_ = false;
				helmLoggedPose_ = 99;
				logger::info("Helm anim: {} accepted{}", kHelmStartEvent, helmRejects_ ? std::format(" after {} tries", helmRejects_) : std::string{});
				helmRejects_ = 0;
			} else {
				helmRetry_ = 0.5f;
				if (++helmRejects_ == 1) logger::info("Helm anim: {} rejected, retrying every 0.5 s", kHelmStartEvent);
				if (helmRejects_ == 10 && !helmEverOk_)
					Print("RowRim: the helm animation was refused 10 times. Run Pandora (or Nemesis) with RowRim enabled. Sitting without it for now.");
			}
		}


	public:
		// v1.8.42: the tiller follows his hand. The game blends the helm poses its own way (and finishes or reverses a blend
		// as it likes when the steering changes half way), so a tiller turned on a timer ran ahead of the hand or behind it.
		// The tiller is turned to where his wrist is: its angle round the tiller's post, against that angle at rest (measured
		// once the rest pose has settled). v1.8.45: the tiller is the boat's again (it rolls and pitches with it; v1.8.43
		// kept it upright with him): the wrist's angle is taken in the boat's frame. v1.8.46: his body rolls and pitches with
		// the boat too (BodyTilt, before this), so his hand stays on it (v1.8.45's arm script removed). From the camera hooks, after the boat is placed for drawing (its tilt this frame).
		void TillerHelm()
		{
			if (!sbTiller_ || !visual_ || sbRudderOn_ != visual_.get() || !sbTiller_->parent || Kind() != kKindSail) { helmHandLive_ = false; return; }
			static constexpr float kPivX = 49.0f, kPivY = -226.0f, kMax = 0.55f, kRatio = 0.8f;
			const float now = rideTime_, dt = std::clamp(now - tillerHelmT_, 0.0f, 0.1f);
			tillerHelmT_ = now;
			const bool on = seated_ && helmAnimOn_ && !helmPoseWarned_;
			helmHandLive_ = false;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!on || !player || (cam && cam->IsInFirstPerson())) return;
			auto* r3 = player->Get3D(false);
			auto* hand = r3 ? r3->GetObjectByName("NPC R Hand [RHnd]") : nullptr;
			if (!hand) return;
			const RE::NiTransform& PW = sbTiller_->parent->world;
			const RE::NiPoint3 m = PW.Invert() * hand->world.translate;  // in the model (the boat's own frame)
			const float phi = std::atan2(m.x - kPivX, m.y - kPivY);     // round the post, from the bow toward starboard
			if (helmPose_ == 0 && now - helmPoseAt_ > 0.8f) {
				helmPhi0_ = helmPhiHave_ ? helmPhi0_ + (phi - helmPhi0_) * (1.0f - std::exp(-4.0f * dt)) : phi;
				helmPhiHave_ = true;
			}
			if (!helmPhiHave_) return;
			const float want = std::clamp(-(phi - helmPhi0_), -kMax, kMax) / kRatio;  // + turns it to port, as the rudder's +
			helmHandA_ += (want - helmHandA_) * (1.0f - std::exp(-25.0f * dt));
			helmHandLive_ = true;
			if (now - helmPoseAt_ > 1.0f && helmLoggedPose_ != helmPose_) {
				helmLoggedPose_ = helmPose_;
				logger::info("Helm anim: pose {} settled: wrist {:+.3f} rad round the post from rest, tiller {:+.3f} rad{}", helmPose_, -(phi - helmPhi0_),
					helmHandA_ * kRatio, std::abs(phi - helmPhi0_) > kMax ? " (held at the stop)" : "");
			}
		}

		// ---- v1.8.51: pink seat markers (Settings / Crew: Show seat markers, oas "crew markers on|off"): a figure on each
		// seat of the boat in use, sitting as they would (the chair sit, the ledge sit, the dog), facing their way, moved
		// at once as the seat is tuned. Drawn only (no collision), on the boat's own model so they ride with it. ----
		static constexpr const char* kSeatMarkerModel = "RowRim\\RowRim_SeatMarker.nif";
		static inline RE::NiPointer<RE::NiNode> s_mkSrc;
		static inline bool s_mkFailed = false;
		RE::NiPointer<RE::NiNode> mkRoot_;
		RE::NiNode* mkOn_{ nullptr };
		void DropSeatMarkers()
		{
			if (!mkRoot_) return;
			if (visual_ && mkOn_ == visual_.get() && mkRoot_->parent == visual_.get()) {
				RE::NiPointer<RE::NiAVObject> tmp;
				visual_->DetachChild(mkRoot_.get(), tmp);
			}
			mkRoot_.reset();
			mkOn_ = nullptr;
		}
	public:
		void SeatMarkers()
		{
			if (!Crew::g_markers || !visual_ || !IsActive() || !HaveBoat()) { DropSeatMarkers(); return; }
			if (mkRoot_ && mkOn_ != visual_.get()) { mkRoot_.reset(); mkOn_ = nullptr; }  // another model: the old one keeps its own
			if (!mkRoot_) {
				if (s_mkFailed) return;
				if (!Plankton::LoadModel(s_mkSrc, kSeatMarkerModel)) { s_mkFailed = true; logger::warn("Seat markers: meshes\\{} missing", kSeatMarkerModel); return; }
				auto* r = RE::NiNode::Create(8);
				r->name = "RowRim_SeatMarkers";
				for (int i = 0; i < 8; ++i) {
					auto* obj = s_mkSrc->Clone();
					auto* n = obj ? obj->AsNode() : nullptr;
					if (!n) break;
					n->name = "RowRim_SeatMarker";
					n->local = RE::NiTransform{};
					r->AttachChild(n, true);
				}
				mkRoot_ = RE::NiPointer<RE::NiNode>(r);
				visual_->AttachChild(r, true);
				mkOn_ = visual_.get();
				logger::info("Seat markers: on the {}", Dims().label);
			}
			const int k = Kind();
			const float sc = ModelScale();
			mkRoot_->local = RE::NiTransform{};
			mkRoot_->local.scale = 1.0f / sc;  // the markers in game units, as the seats are
			const int nh = Crew::HumanCount(k), nd = Crew::DogCount(k);
			int idx = 0;
			for (auto& c : mkRoot_->GetChildren()) {
				auto* n = c ? c->AsNode() : nullptr;
				const int i = idx++;
				if (!n) continue;
				if (i >= nh + nd) { n->SetAppCulled(true); continue; }
				const bool dog = i >= nh;
				const int slot = dog ? i - nh : i;
				float yaw = 0.0f;
				n->local.translate = Crew::SeatLocal(k, sc, dog, slot, yaw);
				const float cs = std::cos(yaw), sn = std::sin(yaw);  // the figure's front (+y) turned to (sin yaw, cos yaw)
				auto& m = n->local.rotate;
				m.entry[0][0] = cs;   m.entry[0][1] = sn; m.entry[0][2] = 0.0f;
				m.entry[1][0] = -sn;  m.entry[1][1] = cs; m.entry[1][2] = 0.0f;
				m.entry[2][0] = 0.0f; m.entry[2][1] = 0.0f; m.entry[2][2] = 1.0f;
				const std::string_view want = dog ? "Dog" : Crew::Def(k, false, slot).ledge ? "Ledge" : "Chair";
				n->SetAppCulled(false);
				for (auto& part : n->GetChildren()) {
					if (!part) continue;
					const char* nm = part->name.c_str();
					part->SetAppCulled(!nm || want != nm);
				}
			}
			RE::NiUpdateData ud{};
			mkRoot_->Update(ud);
		}
		static void SeatMarkersAll() { for (int k = 0; k < kKindCount; ++k) Of(k).SeatMarkers(); }
		// v1.8.55: the third person camera may zoom further out while you are on a boat (seated or aboard): the game's
		// fMaxCurrentZoom:Camera is raised to RowRim.ini [Camera] BoatZoomOut (default 6, the game's own 3 means "as before")
		// while aboard and put back as you step off (the zoom brought back in to the game's limit if it was further out)
		static bool AnySeated()
		{
			for (int k = 0; k < kKindCount; ++k)
				if (Of(k).Seated()) return true;
			return false;
		}
		// v1.8.78: seated on any boat the game's third person camera never follows an animated bone: the row clip and the tiller
		// clip (OarsHelmStart) turn a bone (animatedBoneName), the camera takes it into animationRotation / the orbit and spins
		// with it. Switched off and put to identity before and after the game's camera update; what it had done is logged.
		static void CutAnimCam(RE::ThirdPersonState* a_tps, const char* a_when)
		{
			static int s_logs = 0;
			static float s_lastX = 0.0f;
			const bool seated = AnySeated();
			if (!a_tps) return;
			if (std::isfinite(a_tps->freeRotation.x) && std::fabs(a_tps->freeRotation.x) > kPi) a_tps->freeRotation.x = std::remainder(a_tps->freeRotation.x, 2.0f * kPi);
			if (!std::isfinite(a_tps->freeRotation.x)) a_tps->freeRotation.x = 0.0f;
			if (!seated) { s_lastX = a_tps->freeRotation.x; return; }
			const bool toggled = a_tps->toggleAnimCam;
			const float aw = std::fabs(a_tps->animationRotation.w);
			const float dx = std::fabs(std::remainder(a_tps->freeRotation.x - s_lastX, 2.0f * kPi));
			if ((toggled || aw < 0.9999f || dx > 0.3f) && s_logs < 60) {
				++s_logs;
				logger::info("AnimCam {}: toggleAnimCam {} | animationRotation w {:.4f} xyz {:.3f} {:.3f} {:.3f} | bone '{}' | orbit x {:.3f} (moved {:.3f}) y {:.3f} enabled {} | yaw {:.3f}->{:.3f}",
					a_when, toggled, a_tps->animationRotation.w, a_tps->animationRotation.x, a_tps->animationRotation.y, a_tps->animationRotation.z,
					a_tps->animatedBoneName.c_str() ? a_tps->animatedBoneName.c_str() : "", a_tps->freeRotation.x, dx, a_tps->freeRotation.y,
					a_tps->freeRotationEnabled, a_tps->currentYaw, a_tps->targetYaw);
			}
			a_tps->toggleAnimCam = false;
			a_tps->animationRotation = RE::NiQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
			s_lastX = a_tps->freeRotation.x;
		}
		static bool AnyOnBoat()
		{
			for (int k = 0; k < kKindCount; ++k)
				if (Of(k).OnBoat()) return true;
			return false;
		}
		static void TendZoom(RE::ThirdPersonState* a_tps)
		{
			static float s_orig = -1.0f, s_boat = -1.0f;
			static bool s_raised = false;
			static auto* set = RE::INISettingCollection::GetSingleton() ? RE::INISettingCollection::GetSingleton()->GetSetting("fMaxCurrentZoom:Camera") : nullptr;
			if (!set) return;
			if (s_boat < 0.0f) {
				char b[32]{};
				GetPrivateProfileStringA("Camera", "BoatZoomOut", "14.0", b, sizeof(b), Plankton::IniPath().c_str());
				try { s_boat = std::clamp(std::stof(b), 1.0f, 40.0f); } catch (...) { s_boat = 14.0f; }
				logger::info("Camera: zoom out on the boat to {:.1f} (RowRim.ini [Camera] BoatZoomOut; the game's own {:.1f})", s_boat, set->data.f);
			}
			bool aboard = false;
			for (int k = 0; k < kKindCount && !aboard; ++k) aboard = Of(k).OnBoat();
			if (aboard && !s_raised) {
				s_orig = set->data.f;
				if (s_boat > s_orig) { set->data.f = s_boat; s_raised = true; }
			} else if (!aboard && s_raised) {
				set->data.f = s_orig;
				s_raised = false;
				if (a_tps) {
					a_tps->targetZoomOffset = (std::min)(a_tps->targetZoomOffset, s_orig);
					a_tps->currentZoomOffset = (std::min)(a_tps->currentZoomOffset, s_orig);
				}
			}
		}
		// v1.8.46: the seated body rolls and pitches with the boat. The actor is never tilted and never put on the boat's
		// nodes (it stays the game's, upright, with its own collision): only his drawn 3D (the third person root) is turned
		// by the boat's roll and pitch this frame (the boat's turn without its heading), about his own root (on the seat),
		// on top of the game's own turn of him; the game's turn is put back as he stands up. Eased in and out over 0.3 s.
		// From the camera hooks, after the boat is placed for drawing (its tilt this frame, the heel and rocking settings
		// and the wind's heel all in it).
		void BodyTilt()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* root = player ? player->Get3D(false) : nullptr;
			const float now = rideTime_, dt = std::clamp(now - bodyTiltT_, 0.0f, 0.1f);
			bodyTiltT_ = now;
			const bool on = seated_ && visual_ && root && havePose_;
			bodyTiltW_ = std::clamp(bodyTiltW_ + (on ? dt : -dt) / 0.3f, 0.0f, 1.0f);
			if (!root || root != bodyRoot_) { bodyRoot_ = root; bodyTiltOn_ = false; }
			if (!root) return;
			auto same = [](const RE::NiMatrix3& a, const RE::NiMatrix3& b) {
				for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) if (a.entry[r][c] != b.entry[r][c]) return false;
				return true;
			};
			// the game's own turn of him this frame (unless it left ours there)
			const RE::NiMatrix3 base = (bodyTiltOn_ && same(root->local.rotate, bodyLast_)) ? bodyBase_ : root->local.rotate;
			bodyBase_ = base;
			RE::NiUpdateData data{};
			if (bodyTiltW_ <= 0.0f || !visual_) {
				if (bodyTiltOn_) { root->local.rotate = base; root->Update(data); ReplaceRiding(); bodyTiltOn_ = false; }
				return;
			}
			const RE::NiMatrix3 B = haveDraw_ && VisualOnPlayer() ? drawRot_ : visual_->world.rotate;  // v1.8.57: riding on the root, its world is the tilt's own output
			RE::NiPoint3 f = B * RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
			f.z = 0.0f;
			if (f.Length() < 1e-3f) return;
			f = f / f.Length();
			const RE::NiPoint3 up{ 0.0f, 0.0f, 1.0f }, rt = f.Cross(up);
			RE::NiMatrix3 Y;
			for (int i = 0; i < 3; ++i) { Y.entry[i][0] = (&rt.x)[i]; Y.entry[i][1] = (&f.x)[i]; Y.entry[i][2] = (&up.x)[i]; }
			const RE::NiMatrix3 I = MakeMat(1, 0, 0, 0, 1, 0, 0, 0, 1);
			const float w = bodyTiltW_ * bodyTiltW_ * (3.0f - 2.0f * bodyTiltW_);
			const RE::NiMatrix3 tilt = SlerpM(I, B * Y.Transpose(), w);  // the boat's roll and pitch, in the world
			const RE::NiMatrix3 P = root->parent ? root->parent->world.rotate : I;
			root->local.rotate = P.Transpose() * (tilt * (P * base));
			root->Update(data);
			ReplaceRiding();  // v1.8.57
			bodyLast_ = root->local.rotate;
			bodyTiltOn_ = true;
		}
		// v1.8.54: the tilt is taken back off before the game's own frame (the player update) and before its camera update,
		// so the game never reads a tilted body: his heading (the sit keeps it from his 3D) and the camera's pivot on him
		// were picked up off the tilted root, the turn fed back into itself frame after frame and the third person camera
		// spun out. BodyTilt puts it on again after the camera update, for drawing only.
		void BodyUntilt()
		{
			if (!bodyTiltOn_) return;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* root = player ? player->Get3D(false) : nullptr;
			bodyTiltOn_ = false;
			if (!root || root != bodyRoot_) return;
			bool same = true;
			for (int r = 0; r < 3 && same; ++r) for (int c = 0; c < 3; ++c) if (root->local.rotate.entry[r][c] != bodyLast_.entry[r][c]) { same = false; break; }
			if (!same) return;  // the game already put its own turn there
			root->local.rotate = bodyBase_;
			RE::NiUpdateData data{};
			root->Update(data);
			ReplaceRiding();  // v1.8.57
		}
	private:
		float tillerHelmT_{ 0.0f }, bodyTiltT_{ 0.0f }, bodyTiltW_{ 0.0f };
		bool helmHandLive_{ false }, bodyTiltOn_{ false };
		int helmLoggedPose_{ 99 };
		RE::NiAVObject* bodyRoot_{ nullptr };
		RE::NiMatrix3 bodyBase_{}, bodyLast_{};

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
			const RE::NiPoint3 g = glideOff_ * (1.0f - GlideK());  // v1.8.71: the sit glide, from where you stood onto the seat
			if (seatKind_ != SeatKind::kRower) return seat_ + g;
			const float k = rowShiftW_ * rowShiftW_ * (3.0f - 2.0f * rowShiftW_);
			return RE::NiPoint3{ rowSeat_.x + g.x, rowSeat_.y + rowShift_ * k + g.y, rowSeat_.z + g.z };
		}
		float SeatFacing() const { return yaw_ + (bowFlip_ ? kPi : 0.0f) + (seatKind_ == SeatKind::kRower ? kPi : 0.0f); }
		// ---- v1.8.71: the sit and stand glides (Miguel: the body snapped ~120 units and half a turn in one frame onto the seat).
		// Sitting: the body slides from where you stood onto the seat and turns to the seat's way over kSitGlide, then sits.
		// Standing: it rises off the seat and steps to the standing spot over kStandGlide. All in the boat's frame, so a moving
		// boat carries the glide with it.
		static constexpr float kSitGlide = 0.45f;
		static constexpr float kStandGlide = 0.35f;
		bool gliding_{ false };
		float glideAt_{ 0.0f };
		RE::NiPoint3 glideOff_{};
		float glideYaw_{ 0.0f };  // the start facing, off the seat's
		bool sitDeferred_{ false };
		bool standGlide_{ false };
		float standAt_{ 0.0f };
		RE::NiPoint3 standFrom_{}, standTo_{};
		static float Smooth(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
		float GlideK() const { return gliding_ ? Smooth((rideTime_ - glideAt_) / kSitGlide) : 1.0f; }
		float SeatFacingNow() const { return SeatFacing() + glideYaw_ * (1.0f - GlideK()); }
		SeatKind NearestSeat() const
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return SeatKind::kRower;
			if (Kind() != kKindRow) return SeatKind::kHelm;  // v1.8.8 (v1.8.26: no oars on either sailing boat): the helm
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
		// v1.8.29c: R while seated switches seats (the rowboat: the oars <-> the stern seat; the sailing boats have only the
		// helm). Stood up, then sat down in the other seat a moment later. RowRim.ini [Controls] SeatSwitchKey (a DirectX
		// scan code, 19 = R; 0 off).
		int seatSwitchTo_{ -1 };
		float seatSwitchT_{ 0.0f };
		static int SeatSwitchKey()
		{
			static int k = -2;
			if (k == -2) k = GetPrivateProfileIntA("Controls", "SeatSwitchKey", 0x13, Plankton::IniPath().c_str());
			return k;
		}
	public:
		void OnSeatSwitch(std::uint32_t a_key)
		{
			if (!seated_ || SeatSwitchKey() <= 0 || static_cast<int>(a_key) != SeatSwitchKey()) return;
			if (Kind() != kKindRow) return;  // one seat only (the helm)
			const SeatKind to = seatKind_ == SeatKind::kRower ? SeatKind::kHelm : SeatKind::kRower;
			logger::info("Seat: switching to the {}", to == SeatKind::kRower ? "oars" : "stern seat");
			StandUp("switching seats");
			seatSwitchTo_ = static_cast<int>(to);
			seatSwitchT_ = 0.35f;
		}
	private:
		void TendSeatSwitch(float a_dt)
		{
			if (seatSwitchTo_ < 0) return;
			if ((seatSwitchT_ -= a_dt) > 0.0f) return;
			const SeatKind to = static_cast<SeatKind>(seatSwitchTo_);
			seatSwitchTo_ = -1;
			if (seated_ || !PlayerAboard()) return;
			SitDown(to);
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
			p.playerRows = seatKind_ == SeatKind::kRower || Kind() != kKindRow;  // v1.8.8 (v1.8.26: no oars on the sailing boats): no crew rower
			p.deckZ = Dims().deckZ;
			p.speed = speed_;
			p.scale = ModelScale();  // v1.8.27
			p.kind = Kind();         // v1.8.51: its own seats
			return p;
		}

		void SitDown(SeatKind a_kind)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			if (player->IsSneaking()) player->NotifyAnimationGraph("sneakStop");
			seatFlipAt_ = NowS();  // v1.8.57
			seated_ = true;
			standGlide_ = false;  // v1.8.71
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
			helmAnimOn_ = false; helmRetry_ = 0.0f; helmPose_ = 0;  // v1.8.40
			inFwd_ = inBack_ = inLeft_ = inRight_ = false;
			SetControls(false);
			HoldTalk();          // v1.7.22
			Crew::Stow(player);  // v1.7.22: weapon sheathed (still equipped), shield off, rowing or at the helm
			player->SetCollision(false);  // v1.2.3: OAS rides with no player collision ("tcl": no collision, no gravity); the glue holds him
			walkFix_ = 0.0f;
			// v1.8.71: the glide starts where you stand (in the boat's frame); a far start (a load, a teleport) snaps as before
			gliding_ = false;
			glideOff_ = {};
			glideYaw_ = 0.0f;
			if (havePose_ && glideOn_) {
				const RE::NiPoint3 seatW = lastAt_ + lastRot_ * SeatPos();
				const RE::NiPoint3 off = lastRot_.Transpose() * (player->GetPosition() - seatW);
				if (off.Length() < 250.0f) {
					glideOff_ = off;
					float dy = player->data.angle.z - SeatFacing();
					while (dy > kPi) dy -= 2.0f * kPi;
					while (dy < -kPi) dy += 2.0f * kPi;
					glideYaw_ = dy;
					glideAt_ = rideTime_;
					gliding_ = true;
					logger::info("Seat: glide in from {:.0f} off the seat, {:.2f} rad to turn", off.Length(), dy);
				}
			}
			player->data.angle.z = SeatFacingNow();
			sitDeferred_ = gliding_;
			const bool ok = gliding_ ? true : TrySitAnim(player);
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
			seatFlipAt_ = NowS();  // v1.8.57
			seated_ = false;
			gliding_ = sitDeferred_ = false;  // v1.8.71
			glideOff_ = {};
			glideYaw_ = 0.0f;
			inFwd_ = inBack_ = inLeft_ = inRight_ = false;
			autoRow_ = false;  // v1.7.62
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
			helmAnimOn_ = false; helmPose_ = 0;  // v1.8.40: the release events below end the helm clip too
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
					const float feetZ = rower ? Dims().deckZ + 2.0f : Dims().deckZ + 6.0f;
					const RE::NiPoint3 to{ sp.x, sp.y + offY, feetZ };
					if (glideOn_ && havePose_) {
						// v1.8.71: up off the seat and a step to the standing spot over kStandGlide (TendStandGlide), not one frame
						standFrom_ = lastRot_.Transpose() * (player->GetPosition() - lastAt_);
						standTo_ = to;
						standAt_ = rideTime_;
						standGlide_ = true;
						player->SetCollision(false);
						logger::info("Seat: glide out, {:.0f} to the standing spot", (standTo_ - standFrom_).Length());
					}
					const RE::NiPoint3 w = RE::NiPoint3{ pos_.x, pos_.y, pos_.z + heave_ } + rot * to;
					if (!standGlide_) player->SetPosition(w, true);
					if (auto* cc = standGlide_ ? nullptr : player->GetCharController()) {
						const float hs = RE::bhkWorld::GetWorldScale();
						const float hd = yaw_ + (bowFlip_ ? kPi : 0.0f);
						const RE::hkVector4 v(std::sin(hd) * speed_ * hs, std::cos(hd) * speed_ * hs, 0.0f, 0.0f);
						cc->SetLinearVelocityImpl(v);
					}
				}
				logger::info("Seat: release events sent: {}", sent);
				nudgeTime_ = nudgeOn_ && !standGlide_ ? 0.0f : -1.0f;  // v0.3.5: the tiny step after standing (see TendNudge; v1.8.71: after the glide)
				walkFix_ = 2.0f;  // v1.2.3: the walking flags held for 2 s (OAS PostExitStand), the get-up can set them again
			}
			// v1.8.27: up from a sailing boat's helm: the sail is stowed and she glides to a stop
			if (Kind() != kKindRow && SailEnabled() && (sail_ == 2 || sail_ == 3)) {
				SetSail(1);
				logger::info("Sail: stowed (stood up from the helm)");
			}
			Print(std::format("RowRim: stood up ({}); the boat slows to a stop", a_why));
		}

		bool helmF_{ false }, helmB_{ false };  // v1.8.27: W / S held last frame at a sailing boat's helm
		float helmSteer_{ 0.0f };  // v1.8.31: A / D at the helm: -1 left .. 1 right
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
			helmSteer_ = (r ? 1.0f : 0.0f) - (l ? 1.0f : 0.0f);  // v1.8.31: for the rudder and tiller
			// v1.4.0: the stern seat has no oars: sitting there alone does not move the boat (the old test motor is gone)
			noRowerNote_ -= a_dt;
			const bool longCrew = false;  // v1.8.26: no oars on the longship (was: its crew row it from the helm)  // v1.8.8: the longship's crew row (v1.8.9l: oars in under sail, W / S trim)
			if (seated_ && seatKind_ == SeatKind::kHelm && Kind() == kKindLong) {
				// v1.8.27: the longship (no oars): W sets the sail, S stows it (as Jump), A / D steer
				// v1.8.27b: not the sailing boat: there W / S do nothing, only Jump sets and stows the sail
				if (f && !helmF_) SetSail(2);
				if (b && !helmB_) SetSail(1);
			}
			helmF_ = seated_ && f;
			helmB_ = seated_ && b;
			if (seated_ && seatKind_ == SeatKind::kHelm && !longCrew) {
				// v1.7.74: under sail the tiller steers (A / D); only rowing needs a rower
				const bool sailing = SailDrawing();
				if (Kind() == kKindRow && !sailing && (f || b || l || r) && noRowerNote_ <= 0.0f) {  // v1.7.76: no nagging under sail (v1.8.27: the rowboat only)
					noRowerNote_ = 6.0f;
					RE::SendHUDMessage::ShowHUDMessage("No rower, move to rower seat or have crew");  // v1.7.54
				}
				f = b = false;
				if (!sailing) l = r = false;
			}
			// v1.8.8: the longship rowed by its crew: up to its own top speed ([Longship] MaxSpeed), slow to gather way
			const float maxFwd = longCrew ? rowMax_ : kMaxFwd, accel = longCrew ? kAccel * 0.5f : kAccel;
			if (f && !b) speed_ = (std::min)(maxFwd, speed_ + (speed_ < 0.0f ? kBrake : accel) * a_dt);
			else if (b && !f) speed_ = (std::max)(-kMaxBack, speed_ - (speed_ > 0.0f ? kBrake : accel) * a_dt);
			else if (seated_ || SailDrawing()) {  // v1.8.9b: under sail with nobody at the helm too (no stopping)
				speed_ *= std::exp(-kDrag * a_dt);
				if (std::abs(speed_) < 2.0f && !SailDrawing()) speed_ = 0.0f;  // v1.7.76: under sail it creeps up from rest
				if (SailDrawing()) speed_ /= std::exp(-kDrag * a_dt);  // v1.7.78: under sail SailDrive sets the drag
			} else if (Kind() != kKindRow) {  // v1.8.27: a sailing boat left with its sail stowed glides to a stop
				speed_ *= std::exp(-kDrag * a_dt);
				if (std::abs(speed_) < 2.0f) speed_ = 0.0f;
			} else {
				constexpr float kIdleBrake = 200.0f;  // nobody at the helm: stops in about a second
				speed_ = speed_ > 0.0f ? (std::max)(0.0f, speed_ - kIdleBrake * a_dt) : (std::min)(0.0f, speed_ + kIdleBrake * a_dt);
			}
			const float steer = (r ? 1.0f : 0.0f) - (l ? 1.0f : 0.0f);
			const float grip = 0.35f + 0.65f * (std::min)(1.0f, std::abs(speed_) / 120.0f);  // turns best when moving, a little at rest
			const float target = steer * kTurnMax * grip * turnScale_ * (speed_ < -2.0f ? -1.0f : 1.0f);  // v1.7.97: [Row] Turn too
			turn_ += (target - turn_) * (1.0f - std::exp(-(3.0f / turnLag_) * a_dt));  // v1.7.97: sluggish ([Row] TurnLag)
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
			if (autoRow_ && (!seated_ || inBack_ || stickY_ < -0.3f)) { autoRow_ = false; logger::info("Row: auto-row off (back or stood up)"); }
			const bool f = inFwd_ || stickY_ > 0.3f || autoRow_, b = inBack_ || stickY_ < -0.3f;  // v1.7.62: auto-row
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
			// v1.8.86: the stroke glide is back (v1.8.81 had flattened it): the boat gains speed while the blades pull and glides between strokes
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
				if (!SailDrawing()) speed_ *= std::exp(-rowDrag_ * a_dt);  // v1.7.78: under sail SailDrive sets the drag
				if (speed_ < 1.0f && !SailDrawing()) speed_ = 0.0f;  // v1.7.76
			} else {
				speed_ *= std::exp(-0.9f * a_dt);
				if (speed_ > -1.0f) speed_ = 0.0f;
			}
			const float auth = kTurnFloor + (1.0f - kTurnFloor) * std::clamp(std::abs(speed_) / kTurnAuth, 0.0f, 1.0f);
			const float maxTurn = (kTurnLow + (kTurnHigh - kTurnLow) * s01) * auth * turnScale_;  // v1.7.8: [Row] Turn
			turn_ += (steer * maxTurn - turn_) * (1.0f - std::exp(-(kTurnResp / turnLag_) * a_dt));  // v1.7.97: sluggish ([Row] TurnLag)
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
				if (sitDeferred_ || rideTime_ - lastRowStopAt_ < 0.3f || rideTime_ - sitAt_ < 0.3f) return;  // 0.3 s after a stop and after sitting (v1.8.71: and the glide)
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
				const float want = waterZ_ + WaterZ() + heightOff_;
				pos_.z += (want - pos_.z) * (1.0f - std::exp(-2.0f * a_dt));
			}
			beached_ = false;
			FeelApplyMotion(a_dt);
			UpdateGround(a_dt, fwd, right);
			UpdateHullFeel(a_dt, fwd, right);
			if (std::abs(speed_) < 0.5f) { onBeach_ = false; return; }
			const float dir = speed_ >= 0.0f ? 1.0f : -1.0f;
			const float tip = dir > 0.0f ? FeelLen() - 10.0f : FeelLen() - 20.0f;
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
		static constexpr float kFeelKeel = 20.0f;
		float FeelLen() const { return Dims().feelLen; }      // v1.8.8: each boat its own (the rowboat 300 x 85)
		float FeelWidth() const { return Dims().feelWidth; }
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
			const float kFeelLen = FeelLen(), kFeelWidth = FeelWidth();  // v1.8.8: this boat's
			const float tipF = kFeelLen - 10.0f, sternF = -(kFeelLen - 10.0f), side = kFeelWidth - 10.0f;
			const float hr[kGN] = { 0.0f, 0.0f, 0.0f, side, -side, 0.5f * kFeelWidth, -0.5f * kFeelWidth };
			const float hf[kGN] = { 0.0f, tipF, sternF, 0.0f, 0.0f, 0.8f * kFeelLen, 0.8f * kFeelLen };
			float req[kGN]{}, surf[kGN]{}, landA[kGN]{}, waterA[kGN]{};
			bool ok[kGN]{}, wetRaw[kGN]{}, touchRaw[kGN]{};
			bool tipSeen = false, govSteep = false, iceCentre = false;
			int iceN = 0, dry = 0;
			RE::NiPoint3 bowPos{};
			auto* tes = RE::TES::GetSingleton();
			const float base = pos_.z - WaterZ() - heightOff_;  // the waterline the boat floats at
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
				const float r = fl.right * FeelWidth(), f = fl.fwd * FeelLen();
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
		const RE::NiAVObject* drawnRoot_{ nullptr };  // v1.8.56: the model AlwaysDraw last flagged
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
			// v1.8.27d: the sailing boats rock their own amount ([Sail] Rock), more in a wind under sail and not less for going
			// fast; they heel their own amount too ([Sail] Heel)
			float rockK = 1.0f, heelK = 1.0f, windK = 1.0f;
			const bool sails = Kind() != kKindRow && SailEnabled();
			if (sails) {
				LoadSailTuning();
				rockK = sailRock_;
				heelK = sailHeelK_;
				if (SailDrawing()) {
					float phi = 0.0f, str = 0.0f;
					SailWind(phi, str);
					windK = 1.0f + 1.5f * std::clamp(str, 0.0f, 1.0f);
				}
			}
			const float slowK = sails ? 1.0f : 1.0f - 0.35f * smooth(0.0f, 0.6f, s01);
			const float r = idleBob_ * slowK * (onLand_ ? 0.0f : 1.0f) * (1.0f - smooth(0.0f, 8.0f, groundLift_));  // v1.4.0: grounded, no bob
			const float rk = Dims().rock;  // v1.8.8b: a big boat rocks less (its stern swung the helm and its prompt about)
			const float sw = rockK * windK;
			const float idleRoll = sw * rk * r * 0.019f * (0.68f * std::sin(t * 1.19f) + 0.32f * std::sin(t * 2.31f + 0.9f));
			const float idlePitch = sw * rk * r * 0.010f * (0.64f * std::sin(t * 1.67f + 1.7f) + 0.36f * std::sin(t * 0.83f + 0.4f));
			const float idleHeave = (sails ? 0.5f + 0.5f * sw : 1.0f) * r * 2.4f * (0.7f * std::sin(t * 1.43f + 0.3f) + 0.3f * std::sin(t * 2.7f + 2.1f));
			// turn lean (the boat's turn rate: + right)
			// v1.8.9j: plus the heel under sail (to leeward)
			const float leanTarget = std::clamp(turn_ * 0.35f * s01, -0.25f, 0.25f) * (0.4f + 0.6f * Dims().rock) + sailHeel_ * heelK * (0.4f + 0.6f * Dims().rock);  // v1.8.27d: times [Sail] Heel
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
				on = std::abs(d.x) < Dims().weightHalfWide && std::abs(d.y) < Dims().weightHalfLen && d.z > -60.0f && d.z < 160.0f && !(st && st->IsSwimming());
			}
			if ((landCheck_ -= a_dt) <= 0.0f) {
				landCheck_ = 1.0f;
				float wz = 0.0f;
				onLand_ = !(WaterAt(pos_, wz) && wz > pos_.z - 60.0f);
			}
			const float amt = onLand_ ? 0.33f : 1.0f;
			const float wRoll = on ? rk * amt * 0.10f * std::clamp(d.x / Dims().weightHalfWide, -1.0f, 1.0f) : 0.0f;
			const float wPitch = on ? -rk * amt * 0.015f * std::clamp(d.y / Dims().weightHalfLen, -1.0f, 1.0f) : 0.0f;  // v1.4.0: a third (was 0.045)
			const float wSink = on && !onLand_ ? 1.5f * rk : 0.0f;
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
			if (DrawModeNow() == 1 || DrawModeNow() == 2) player->Update3DPosition(true);  // v0.8.3 test path; OAS (draw prev) and the pin leave the 3D to the engine
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
			if (firstPerson && !camWasFirst_) player->data.angle.z = SeatFacingNow();  // OAS: first person starts looking the seat's way
			camWasFirst_ = firstPerson;
			MaskSitForFreeLook(player, firstPerson);
			if (firstPerson) {
				player->SetGraphVariableBool("bAllowRotation", true);
			} else {
				player->SetGraphVariableBool("bAllowRotation", false);
				player->data.angle.x = 0.0f;
				player->data.angle.z = SeatFacingNow();
			}
			if (auto* cc = player->GetCharController()) cc->SetLinearVelocityImpl(RE::hkVector4());
			// v1.8.71: the glide done: on the seat, sit now (the helm and row clips wait 0.4 / 0.3 s from here)
			if (gliding_ && rideTime_ - glideAt_ >= kSitGlide) {
				gliding_ = false;
				glideOff_ = {};
				glideYaw_ = 0.0f;
				spikeHave_ = false;
			}
			if (sitDeferred_ && !gliding_) {
				sitDeferred_ = false;
				sitAt_ = rideTime_;
				sitTimer_ = 0.0f;
				TrySitAnim(player);
				logger::info("Seat: glide in done, seated");
			}
			CalmSeated();                    // v1.8.65
			HoldMoveInput();                 // v1.8.72
			TendGraphWatch(a_dt, player);    // v1.8.65
			TendHelmAnim(a_dt, player);  // v1.8.40
			if (rowAnimOn_ || helmAnimOn_ || resitPending_ || fpSitMasked_) return;  // the row (or helm) clip replaces the sit pose; a masked sit is still a sit
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
		int orbitPreLogs_{ 0 };
		float lastFreeX_{ 0.0f };  // v1.8.70
		int spinLogs_{ 0 };

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
		bool carrierChecked_{ false }, carrierSwapped_{ false };
		float carrierLost_{ 0.0f };  // v1.7.80: seconds the carrier has had no 3D / a detached cell  // v1.7.71: the carrier's base logged / replaced once a load
		bool nudgeOn_{ true };
		bool glideOn_{ true };  // v1.8.71: rr "glide off" snaps on and off the seat as before
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
		bool lampOn_{ true };  // v1.8.0: on again for the rowboat (the sailing boat puts it out: ApplyModelGeometry)
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
		bool autoRow_{ false };  // v1.7.62
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
		float turnLag_{ 2.5f };  // v1.7.97
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
					diagFrames_, diagRower_.Text(), diagCam_.Text(), diagCam3_.Text(), diagBoat_.Text(), pinMoved_, pinMax_, DrawModeNow());
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
		// v1.8.81: the rowboat is drawn pinned (mode 3: the boat at this frame's pose, the rower pinned to the seat) unless a mode
		// is chosen with oas "draw ..." (the row clip moves the rower's root 5-17 units off the drawn boat every stroke; "oas go 250"
		// without the clip runs smooth). Uncertain: see the soft rules in RULES.md
		int DrawModeNow() const { LoadDrawSel(); return drawSel_ == 0 ? (kind_ == kKindRow ? 3 : 0) : drawSel_ - 1; }
		mutable int drawSel_{ 0 };
		mutable bool drawLoaded_{ false };
		int drawModeUnused_{ 0 };  // seated: 0 one frame behind (OAS, the default again since v1.6.1), 1 this frame's glue pose, 2 from the drawn player, 3 pinned (v1.2.3)
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
		// (v1.8.8: the footprint where standing people tip the boat is each boat's own: Dims().weightHalfLen / weightHalfWide)
		bool handLock_{ true };
		int oarLogs_{ 0 };
		float oarRoll_{ 90.0f * kPi / 180.0f };          // OAS BakedOarRoll default
		RE::NiPoint3 aftLock_{ 88.3f, -95.0f, 77.9f };  // v1.7.94: per model (kAftLock on the rowboat)
		RE::NiPoint3 lockLocal_{ 93.3f, 6.7f, 76.5f };  // v1.2.6: the middle oarlock forks where the row clip's oar shafts cross the gunwale (shaft centre; the cradle is 1.5 under it)


		RE::FormID refId_{ 0 };
		RE::FormID watchId_{ 0 };
		RE::NiPointer<RE::bhkRigidBody> body_;  // v1.8.49: held (was a bare pointer: freed under us on a coc)
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
		float seaHeave_{ 0.0f }, seaPitch_{ 0.0f }, seaRoll_{ 0.0f };  // v1.8.10
		SeaLink::State seaState_;  // v1.8.11
		float bowImm_ = 0.0f, bowCool_ = 0.0f;  // v1.8.15: how deep the bow is in the wave, and the splash's wait
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
					if (btn->device == RE::INPUT_DEVICE::kMouse && hull.HelmCamActive() && !blocked && btn->IsDown()) {  // v1.8.79: wheel zoom
						const auto wid = btn->GetIDCode();
						if (wid == RE::BSWin32MouseDevice::Key::kWheelUp) TestHull::g_wheel = TestHull::g_wheel.load() + 1.0f;
						else if (wid == RE::BSWin32MouseDevice::Key::kWheelDown) TestHull::g_wheel = TestHull::g_wheel.load() - 1.0f;
						continue;
					}
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
						if (btn->IsDown()) hull.OnSeatSwitch(id);  // v1.8.29c: R switches seats
						if (btn->IsDown() && hull.HelmCamActive() && id == static_cast<std::uint32_t>(HelmCamSet::Get().cineKey)) HelmCamSet::ToggleCine();  // v1.8.59
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
					// v1.7.62: Auto-Move toggles auto-row while seated
					const bool autoKey = (!user.empty() && ue->autoMove.c_str() && user == ue->autoMove.c_str()) || Pad::IsAutoMove(btn->device.get(), id);
					if (autoKey && btn->IsDown() && !blocked) hull.OnAutoRow();
					// v1.7.74: Jump while seated sets the sail (furled, full, reefed)
					const bool jumpKey = (!user.empty() && ue->jump.c_str() && user == ue->jump.c_str()) || Pad::IsMappedTo(ue->jump, btn->device.get(), id);
					if (jumpKey && btn->IsDown() && !blocked) {
						if (hull.AnchorKeys()) hull.OnAnchorKey();  // v1.8.29: the sailing boat: Jump drops and raises the anchor
						else hull.OnSailKey();
						hull.OnJumpKey();  // v1.8.9b: and a jump aboard goes with the boat
					}
					// v1.8.29: the sailing boat: Sprint sets, reefs and stows the sail
					const bool sprintKey = (!user.empty() && ue->sprint.c_str() && user == ue->sprint.c_str()) || Pad::IsMappedTo(ue->sprint, btn->device.get(), id);
					if (sprintKey && btn->IsDown() && !blocked && hull.AnchorKeys()) hull.OnSailKey();
					const bool act = is(ue->activate, 0x12) || (gamepad && Pad::IsActivate(id));
					if (act && btn->IsDown() && !blocked) hull.OnActivate();
				} else if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
					TestHull::g_mouseSeen = true;  // v1.8.79: the helm camera looks round with the raw mouse
					if (hull.HelmCamActive() && !blocked) {
						auto* m = static_cast<RE::MouseMoveEvent*>(e);
						TestHull::g_mx = TestHull::g_mx.load() + static_cast<float>(m->mouseInputX);
						TestHull::g_my = TestHull::g_my.load() + static_cast<float>(m->mouseInputY);
					}
				} else if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
					auto* t = static_cast<RE::ThumbstickEvent*>(e);
					if (t->IsLeft()) {
						Pad::g_leftY = t->yValue;
						// v1.7.8: R3 held + left stick zooms, the boat is not rowed meanwhile
						const bool zoom = Pad::R3Zooming();
						hull.OnStick(blocked || zoom ? 0.0f : t->xValue, blocked || zoom ? 0.0f : t->yValue);
					} else {
						Pad::g_rightY = t->yValue;
						TestHull::g_rx = blocked ? 0.0f : t->xValue;  // v1.8.79: the helm camera's look
						TestHull::g_ry = blocked ? 0.0f : t->yValue;
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
			TestHull::Get().BodyUntilt();  // v1.8.54
			Stage("first person camera: PinRower");
			TestHull::Get().PinRower();  // v1.2.3
			Stage("first person camera: game's original Update");
			func(a_this, a_nextState);
			Stage("first person camera: LatePlace");
			TestHull::Get().LatePlace("first person camera update");
			TestHull::Get().BodyTilt();    // v1.8.46: the seated body with the boat's roll and pitch
			TestHull::Get().TillerHelm();  // v1.8.43
			TestHull::SeatMarkersAll();    // v1.8.51
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
			TestHull::Get().BodyUntilt();  // v1.8.54: the camera is built from him upright (the tilt is drawn only)
			TestHull::Get().CamFrame();    // v1.7.17
			TestHull::TendZoom(a_this);    // v1.8.55: further zoom out while on a boat
			TestHull::CutAnimCam(a_this, "before");  // v1.8.78
			Stage("third person camera: PinRower");
			TestHull::Get().PinRower();  // v1.2.3: before the game places the camera from his bones
			Stage("third person camera: game's original Update");
			func(a_this, a_nextState);  // v1.8.77: the game's camera, as it is
			TestHull::CutAnimCam(a_this, "after");  // v1.8.78
			Stage("third person camera: LatePlace");
			TestHull::Get().LatePlace("third person camera update");  // v1.1.1
			TestHull::Get().RootSpikeGuard();  // v1.8.57d
			TestHull::Get().BodyTilt();    // v1.8.46
			TestHull::Get().TillerHelm();  // v1.8.43
			TestHull::Get().HelmCamTick(a_this);  // v1.8.58: the helm camera, ours
			TestHull::SeatMarkersAll();    // v1.8.51
			Stage("third person camera: lake boats");
			LakeBoats::LatePlace();  // v1.3.1
			Stage("third person camera: fog");
			Underwater::FogTend();   // v1.5.15
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
			// v1.8.58: installed now for the helm camera only (CamOut, the old seat guard, was never hooked: left out)
			TestHull::Get().HelmCamPos(a_translation);  // v1.8.58
		}
		static inline REL::Relocation<decltype(Thunk)> func;
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ThirdPersonState[0] };
			func = vtbl.write_vfunc(REL::Module::IsVR() ? 6 : 5, Thunk);
		}
	};

	// v1.8.58: the third person camera's turn: ours at the helm
	struct ThirdPersonStateRotationHook
	{
		static void Thunk(RE::ThirdPersonState* a_this, RE::NiQuaternion& a_rotation)
		{
			func(a_this, a_rotation);
			TestHull::Get().HelmCamRot(a_rotation);
		}
		static inline REL::Relocation<decltype(Thunk)> func;
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ThirdPersonState[0] };
			func = vtbl.write_vfunc(REL::Module::IsVR() ? 5 : 4, Thunk);
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
			if (a_event && CarrierAudio::Any()) {
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
			return a_ref && TestHull::IsAnyCarrier(a_ref->GetFormID());  // v1.8.26: any of the three boats' carriers
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
			const int kind = TestHull::Get().PickKindOf(a_ref);
			if (kind == TestHull::kPickBedroll) {
				a_dst = Gear::BedrollOut() ? "Pack Up\nBedroll" : "Unroll\nBedroll";
				return true;
			}
			if (kind == TestHull::kPickSail) { a_dst = TestHull::Get().SailPrompt(); return true; }  // v1.7.73
			return text(a_this, a_ref, a_dst);
		}
		static inline REL::Relocation<decltype(Text)> text;
		static bool Activate(RE::TESObjectACTI* a_this, RE::TESObjectREFR* a_target, RE::TESObjectREFR* a_activator, std::uint8_t a_arg3, RE::TESBoundObject* a_object, std::int32_t a_count)
		{
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
			TestHull::Get().BodyUntilt();  // v1.8.54: the game's frame sees him upright
			TestHull::Get().CalmSeated();  // v1.8.65: the body's movement values at rest before the game's update too
			TestHull::Get().HoldMoveInput();  // v1.8.72: movement stays on seated (the compass), the walk held here
			TestHull::Get().GuardCameraTarget();  // v1.8.74
			func(a_this, a_delta);
			TestHull::Get().GuardCameraTarget();  // v1.8.74: and after the game's update
			g_beat.fetch_add(1, std::memory_order_relaxed);
			Stage("TestHull::Update");
			TestHull::TickAll(a_delta);  // v1.8.26: every boat out
			Stage("LakeBoats::Update");
			LakeBoats::Update(a_delta);  // v1.3.0
			{
				Stage("Crew::Tick");
				RE::NiPoint3 at{};
				const bool hb = TestHull::Get().BoatAt(at);
				Crew::Tick(a_delta, hb, at);  // v1.6.0: summons, letting go after a load, the numpad
			}
			Stage("Underwater::Tend");
			Underwater::Tend(a_delta, TestHull::Get().OnBoat());   // v1.5.14: our own underwater look for the camera (v1.8.0: on the boat only)
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
		"oas boat (a boat ahead of you) | \"boat row|sail\" (that boat instead, each with its own settings) | unboat (remove it) | bring (under you) | sit (or E) | paint<blue|beige|black|red|white|bluedesign|vanilla> | "
		"\"height N\" | \"row speed 300\" | \"row drag 0.07\" | \"row turn 0.45\" | \"dock add <name>|del|list|on|off|reload\" | "
		"\"lakeboats on|off|0-10\" | \"crew add|remove|clear|list|summon\" | num (crew seat numpad) | \"plankton on|off|night|auto\" | "
		"menu (settings: SKSE Menu Framework, Oars and Sails) | \"underwater on|off\" | \"lamp on|off\" | \"num gear\" (place the bedroll) | \"bedroll roll|unroll\" | status | \"reset settings\" | dev (test commands). Look at the hull / stern seat / middle thwart for [E] Board / Steer / Row. Full sheet: TUNE_SHEET.txt";
	constexpr const char* kHelp =
		"oas spawn | bring | delete | info | sit (or E: the nearer of the oars / helm) | \"rowseat here|x y z\" | \"seat here|x y z\" | \"row speed 300|drag 0.07|shift 15\" | \"oar roll 90|lock x y z|stow x y z turn|rest 30 12|lock on|off\" | \"bob 1\" | \"lamp on|off\" | lakes | \"lakeboats on|off|now|0-10|view 1-10\" | \"lakehere name\" | ripples | \"plankton on|off|night|auto|density 3|size 0.5|open 1800|opensize 1|shore 400\" | \"underwater on|off\" | \"crew add|remove|clear|list|summon\" | \"num [off|reset]\" | bow | \"rock 6\" | calm | \"period 4\" | \"go 150\" | stop | \"turn 20\" | up | down | drive | warp | freeze | \"node on|off\" | \"layer anim|static\" | flip | watch";

	float NumArg(const std::vector<std::string>& a_w, float a_def)
	{
		if (a_w.size() < 2) return a_def;
		try { return std::stof(a_w[1]); } catch (...) { return a_def; }
	}

	void OverlayToggle();  // v1.7.86: MenuUi.inl

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
			// v1.7.11: oas "num gear": the numpad places the bedroll (the crew tuning off meanwhile)
			if (arg == "gear" || arg == "items" || arg == "bedroll") {
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
		else if (c == "sail" && w.size() >= 3 && w[1] == "headwind") {  // v1.8.34
			try { hull.SetSailHeadWind(std::stof(w[2])); } catch (...) { Print("RowRim: oas \"sail headwind 0.7\""); }
		}
		else if (c == "sail" && w.size() >= 3 && (w[1] == "rock" || w[1] == "heel")) {  // v1.8.27d
			try { const float v = std::stof(w[2]); hull.SetSailMotion(w[1] == "rock" ? v : -1.0f, w[1] == "heel" ? v : -1.0f); }
			catch (...) { Print("RowRim: oas \"sail rock 2.5\" | \"sail heel 1.5\""); }
		}
		else if (c == "sail" && w.size() >= 3 && (w[1] == "speed" || w[1] == "flap")) {  // v1.7.91
			try { const float v = std::stof(w[2]); hull.SetSailTuning(w[1] == "speed" ? v : -1.0f, w[1] == "flap" ? v : -1.0f); }
			catch (...) { Print("RowRim: oas \"sail speed 0.5\" | \"sail flap 0.6\""); }
		}
		else if (c == "sail") Print("RowRim: " + hull.SailCommand(w.size() >= 2 ? w[1] : std::string{}));  // v1.7.73
		else if (c == "menu" || c == "ui") OverlayToggle();  // v1.7.84
		else if (c == "model" || c == "modelsail" || c == "modelrow") hull.SetModel(c.size() > 5 ? c.substr(5) : (w.size() >= 2 ? w[1] : std::string{}));  // v1.7.87
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
			Print("RowRim: " + TestHull::Of(TestHull::kKindRow).DockCommand(args));  // v1.8.26: the docks keep the rowboat
		}
		else if (c == "home") {
			// v1.8.47: oas "home here <boat>" | "home reset <boat>" | "home" (each boat's own dock; no boat named: the one in use)
			const std::string v = w.size() >= 2 ? w[1] : std::string{};
			const int named = w.size() >= 3 ? TestHull::KindFromName(w[2]) : -1;
			auto& b = named >= 0 ? TestHull::Of(named) : TestHull::Get();
			if (v == "here") Print("RowRim: " + b.HomeHere());
			else if (v == "perm") Print("RowRim: " + b.HomeHere(true));  // v1.8.88
			else if (v == "reset") Print("RowRim: " + b.HomeReset(false));
			else if (v == "resetall") Print("RowRim: " + b.HomeReset(true));  // v1.8.50
			else Print(std::format("RowRim: {} | {} | {}  (oas \"home here <boat>\" / \"home reset <boat>\" / \"home resetall <boat>\" / \"home perm <boat>\")", TestHull::Of(TestHull::kKindRow).HomeStatus(),
				TestHull::Of(TestHull::kKindSail).HomeStatus(), TestHull::kLongshipOn ? TestHull::Of(TestHull::kKindLong).HomeStatus() : std::string("no longship in this build")));
		}
		else if ((c == "spawn" || c == "use") && w.size() >= 2 && TestHull::KindFromName(w[1]) >= 0) {  // v1.8.89: the menu's Spawn boat / Use this boat
			const int k = TestHull::KindFromName(w[1]);
			if (k != TestHull::kKindRow && !TestHull::SailingOn()) { Print("RowRim: only the rowboat while sailing is off (RowRim.ini [Sail] Enabled=1)"); return; }
			if (c == "spawn" || !TestHull::Of(k).BoatOut()) {
				TestHull::SwitchActive(k, "oas spawn");
				TestHull::Get().Spawn(false);
			} else TestHull::SwitchActive(k, "oas use");
			TestHull::HoldActive();
			Print(std::format("RowRim: the {} is the one in use", TestHull::Get().Dims().label));
		}
		else if (c == "spawn" || (c == "boat" && w.size() < 2)) hull.Spawn(false);
		else if (c == "boat" && w.size() >= 2 && TestHull::KindFromName(w[1]) >= 0) {  // v1.8.8: oas boat row|sail|longship
			hull.SetKind(w[1]);
			auto& now = TestHull::Get();  // v1.8.26: that boat (the others stay out)
			if (!now.BoatOut() && (TestHull::KindFromName(w[1]) == TestHull::kKindRow || TestHull::SailingOn())) now.Spawn(false);
		}
		else if (c == "bring") hull.Spawn(true);
		else if (c == "carry") hull.SetCarry(w.size() >= 2 ? w[1] : std::string{}, w.size() >= 3 ? w[2] : std::string{});  // v1.8.9
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
		else if (c == "glide") hull.SetGlide(w.size() < 2 || w[1] != "off");
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
				else if (w.size() >= 3 && w[1] == "lag") hull.SetTurnLag(std::stof(w[2]));  // v1.7.97
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

#include "MenuUi.inl"  // v1.7.86: the settings page in SKSE Menu Framework

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
			ThirdPersonStateTranslationHook::Install();  // v1.8.58: (was never installed) the helm camera place
			ThirdPersonStateRotationHook::Install();  // v1.8.58: its turn
			RegisterConsoleCommand();
			MenuUi::Register();  // v1.7.86
			if (auto* mgr = RE::BSInputDeviceManager::GetSingleton()) mgr->AddEventSink(InputSink::Get());
			if (auto* ui = RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::Get());  // v1.7.42
			logger::info("RowRim ready ({})", kVersion);
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			for (int k = 0; k < TestHull::kKindCount; ++k) TestHull::Of(k).Forget();  // then the co-save load (if any) hands the saved hulls and carriers back
			LakeBoats::OnGameLoading();
			Plankton::Flow::Clear();  // v1.7.8
			break;
		case SKSE::MessagingInterface::kNewGame:
			for (int k = 0; k < TestHull::kKindCount; ++k) TestHull::Of(k).Forget();
			LakeBoats::OnGameLoading();
			Plankton::Flow::Clear();
			Legacy::OnGameLoaded();  // v1.7.8
			break;
		case SKSE::MessagingInterface::kSaveGame:
			TestHull::Get().StandFor("saving the game");  // v1.7.42
			for (int k = 0; k < TestHull::kKindCount; ++k) TestHull::Of(k).UntameAll("saving the game");  // v1.7.52: never saved unaggressive (calmed again half a second later)
			break;
		case SKSE::MessagingInterface::kPostPostLoad:
			SmoothCamLink::Ask();  // v1.8.61
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			Legacy::OnGameLoaded();  // v1.7.8: the old-boat sweep starts a few seconds in; the OarsAndSails.dll warning
			TestHull::s_saveSweepIn = 4.0f;  // v1.8.66: the whole save's strays, a few seconds in
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
		for (int k = 0; k < TestHull::kKindCount; ++k) TestHull::Of(k).Save(a_i);  // v1.8.26: every boat out
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
				TestHull::LoadRecordAll(a_i, type, version);
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
			for (int k = 0; k < TestHull::kKindCount; ++k) TestHull::Of(k).Forget();  // v1.8.26
			LakeBoats::OnGameLoading();
		});
	}
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	SKSE::GetMessagingInterface()->RegisterListener("SmoothCam", SmoothCamLink::OnMsg);  // v1.8.61
	return true;
}

#undef GetPrivateProfileStringA
#undef GetPrivateProfileIntA
