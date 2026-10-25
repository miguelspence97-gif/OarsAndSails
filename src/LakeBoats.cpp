// Oars and Sails - rowboat SKSE plugin for Skyrim Special Edition / Anniversary Edition
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

#include "LakeBoats.h"

#include "BoatBow.h"

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// Part 52: the lake boats are a population that lives on each lake for at least an in-game day. Every boat has a real
// position and keeps rowing and fishing whether or not the player can see it ("virtual"); only boats in the loaded
// area are drawn (up to the in-view cap, nearest first), exactly where they are, like an NPC walking a road miles away.
// No spawning out of view any more. The population is kept in the co-save, so it survives saving and loading.

namespace LakeBoats
{
	namespace
	{
		// ---- tunables -------------------------------------------------------------------------------------------------
		constexpr const char* kLakeFile = "Data/SKSE/Plugins/OarsAndSails_Lakes.txt";
		constexpr float kVehicleAboveWater = 80.0f;  // 51.2: the vehicle point while the player rides (log: water 10624, vehicle 10704)
		constexpr float kSampleRadius = 9000.0f;     // 53: water is sampled this far around the player (about the whole loaded area; was 3600)
		constexpr float kSampleStep = 400.0f;
		constexpr int kSamplePerFrame = 450;         // ... a slice of the grid per frame, so a sample never stutters
		constexpr float kResampleMove = 2500.0f;     // a new sample once the player is this far from every earlier one
		constexpr float kMinDepth = 120.0f;          // a route point needs this much water under it
		constexpr float kClearance = 300.0f;         // ... and water (kMinDepth / 2) this far out on all four sides
		constexpr float kLevelTolerance = 30.0f;     // one lake = one water level (no waterfall pools or a river below)
		constexpr float kPointSpacing = 500.0f;      // route points are thinned to this spacing
		constexpr int kMinAutoPoints = 25;           // a new auto lake needs this many points (a real lake, not a stream)
		constexpr float kSimRange = 30000.0f;        // lakes this close to the player keep moving (virtually)
		constexpr float kLiveRange = 10000.0f;       // a boat is drawn within this (and only in a loaded cell)
		constexpr float kDropRange = 12000.0f;       // ... and back to virtual beyond this
		constexpr float kNoSpawnNear = 7000.0f;      // 54: a new boat never goes in a loaded cell closer than this ...
		constexpr float kViewRange = 12000.0f;       // ... nor where the camera looks, closer than this
		constexpr float kViewCos = 0.57f;            // ... (about 55 degrees either side of the view)
		constexpr int kPtsPerBoat = 50;              // 54: one boat per this many route points (a lake's size) ...
		constexpr int kMinLakePts = 40;              // ... and no boats on water smaller than this (a river strip, a pond)
		constexpr float kSeaArea = 20000.0f;         // 54: open sea: at most one boat this close to the player ...
		constexpr float kSeaForget = 26000.0f;       // ... and a sea boat this far behind (not drawn) has rowed off
		constexpr float kHarbourRadius = 22000.0f;   // 54: a sea harbour (Solitude, Dawnstar, Windhelm): 3 or 4 boats this close
		constexpr float kKeepClear = 900.0f;        // 53: boats steer to pass at least this far apart (hull centres; a hull is about 640 long)
		constexpr float kRowSpeed = 95.0f;           // units per second while rowing
		constexpr float kFleeSpeed = 210.0f;
		constexpr float kTurnRate = 0.35f;           // radians per second
		constexpr float kRowMin = 2500.0f;           // 52: a row is 2500 to 9000 units before the next stop (was 700 to 3200)
		constexpr float kRowMax = 9000.0f;
		constexpr float kGiveWay = 450.0f;           // a boat stops and turns away from the player this close
		constexpr float kRowStrokesPerSecond = 0.3366f;  // the 3 s rowing animation (Plugin kRowStrokesBase)
		constexpr const char* kRowStartEvent = "OarsRowStart";
		constexpr const char* kRowStopEvent = "IdleForceDefaultState";

		// Seats and props in the boat frame (right / forward / up from the vehicle point), the same as the player's boat.
		const RE::NiPoint3 kRowerSeat{ 0.0f, 259.0f, -84.0f };
		constexpr float kRowerYaw = 3.14159265f;  // faces the stern (56.11: 180, was 175, so the baked oars sit evenly)
		const RE::NiPoint3 kSternSeat{ 0.0f, 8.0f, -71.0f };
		constexpr float kSternYaw = 0.0f;

		std::mt19937 g_rng{ std::random_device{}() };
		float Rand(float a_lo, float a_hi) { return std::uniform_real_distribution<float>(a_lo, a_hi)(g_rng); }
		template <class T> const T& Pick(const std::vector<T>& a_v) { return a_v[std::uniform_int_distribution<std::size_t>(0, a_v.size() - 1)(g_rng)]; }

		// ---- data -------------------------------------------------------------------------------------------------------
		struct Pt { float x, y; };
		struct Avoid { float x, y, r; };
		enum class Mode : int { kRow = 0, kFish = 1, kFlee = 2 };
		struct Crew
		{
			RE::ActorHandle actor;
			bool rower = false;
			bool setup = false;
			bool rowing = false;
			float sitTimer = 0.0f;
			float idleTimer = 0.0f;
			float retry = 0.0f;
			float dressTimer = 2.0f;  // 54: the outfit is checked this long after the 3D is in
			// 56.19.5: the row clip is sent again when the 3D comes in (or is a new one) and when the rower comes within
			// 4000 of the player; a far rower's graph may never have played it.
			const void* last3D = nullptr;
			bool closeBy = false;
		};
		struct Boat
		{
			// kept (co-save)
			float x = 0.0f, y = 0.0f, yaw = 0.0f, speed = 0.0f;
			Mode mode = Mode::kFish;
			Pt target{};
			float modeTimer = 0.0f;
			std::vector<RE::FormID> people;  // NPC bases; [0] rows
			// drawn
			RE::ObjectRefHandle hull, oar[2], bench[2], lantern, light;
			std::vector<Crew> crew;
			float phase = 0.0f, amp = 0.0f, time = 0.0f, refTimer = 0.0f, rayTimer = 0.0f, blockLog = 0.0f;
			float lastBlockAt = -100.0f, lastBlockX = 0.0f, lastBlockY = 0.0f;  // 55.3: the last spot this boat added (chatter guard)
			bool logged = false;
			bool Live() const { return static_cast<bool>(hull); }
		};
		struct Lake
		{
			std::string name;
			RE::FormID world = 0;
			float water = 0.0f;
			std::vector<Pt> pts;
			std::vector<Avoid> avoid;  // 52: where a boat ran into something (a moored longboat, a dock, a rock): no route through it
			bool saved = false;        // in the lakes file (auto lakes live in the co-save only)
			float born = -1.0f;        // game day the population was made
			bool fresh = false;        // 53: sampled at full range this session (a population is only made after that, so it spreads out)
			int sea = -1;              // 54: the worldspace's sea level (lazy: -1 not known yet)
			float fillTimer = 0.0f;    // 54: time to the next new boat
			std::vector<Boat> boats;
		};
		struct Harbour { std::string name; float x, y; };
		std::vector<Lake> g_fileLakes;  // the lakes file, read once
		std::vector<Lake> g_lakes;      // this game's lakes and populations
		std::vector<Pt> g_sampledAt;
		bool g_loaded = false;
		bool g_on = true;
		int g_count = 6;  // boats per lake (0..10); 54.1: 6 by default
		int g_view = 5;   // drawn at once, all lakes together (1..10)
		RE::NiPoint3 g_rig{ 0.0f, 0.0f, 11.0f };  // 54: every NPC boat moved as one (right / forward / up from where it would be), numpad
		float g_rigSaveTimer = -1.0f;
		RE::FormID g_harbourWorld = 0;
		std::vector<Harbour> g_harbours;
		float g_checkTimer = 0.0f, g_sampleTimer = 0.0f, g_saveTimer = -1.0f;

		float Dist2(float a_x, float a_y, const Pt& a_p) { const float dx = a_x - a_p.x, dy = a_y - a_p.y; return dx * dx + dy * dy; }
		float Today() { auto* cal = RE::Calendar::GetSingleton(); return cal ? cal->GetDaysPassed() : 0.0f; }

		std::string IniPath()
		{
			std::error_code ec;
			return (std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OarsAndSails.ini").string();
		}
		void SaveSettings()
		{
			const auto ini = IniPath();
			WritePrivateProfileStringA("LakeBoats", "On", g_on ? "1" : "0", ini.c_str());
			WritePrivateProfileStringA("LakeBoats", "Count", std::to_string(g_count).c_str(), ini.c_str());
			WritePrivateProfileStringA("LakeBoats", "View", std::to_string(g_view).c_str(), ini.c_str());
		}
		void SaveRig()
		{
			const auto ini = IniPath();
			WritePrivateProfileStringA("LakeBoats", "RigRight", std::format("{:.0f}", g_rig.x).c_str(), ini.c_str());
			WritePrivateProfileStringA("LakeBoats", "RigForward", std::format("{:.0f}", g_rig.y).c_str(), ini.c_str());
			WritePrivateProfileStringA("LakeBoats", "RigUp", std::format("{:.0f}", g_rig.z).c_str(), ini.c_str());
			SKSE::log::info("Lake: NPC boat offset right={:.0f} forward={:.0f} up={:.0f} saved", g_rig.x, g_rig.y, g_rig.z);
		}

		void SaveLakes()
		{
			std::ofstream out(kLakeFile, std::ios::trunc);
			if (!out) { SKSE::log::warn("Lake: cannot write {}", kLakeFile); return; }
			out << "# Oars and Sails lake routes (oas lakehere / lakepoint). lake <name> <worldspace> <water z>, then p <x> <y> per point, a <x> <y> <r> per blocked spot.\n";
			out << "v 55\n";  // 55.3: blocked spots from before 55 (other lake boats taken for obstacles) are dropped on load
			g_fileLakes.clear();
			for (const auto& l : g_lakes) {
				if (!l.saved) continue;
				out << std::format("lake {} {:08X} {:.1f}\n", l.name, l.world, l.water);
				for (const auto& p : l.pts) out << std::format("p {:.0f} {:.0f}\n", p.x, p.y);
				for (const auto& a : l.avoid) out << std::format("a {:.0f} {:.0f} {:.0f}\n", a.x, a.y, a.r);
				Lake copy;
				copy.name = l.name;
				copy.world = l.world;
				copy.water = l.water;
				copy.pts = l.pts;
				copy.avoid = l.avoid;
				copy.saved = true;
				g_fileLakes.push_back(std::move(copy));
			}
			SKSE::log::info("Lake: {} written ({} lakes)", kLakeFile, g_fileLakes.size());
		}

		void LoadLakes()
		{
			g_loaded = true;
			const auto ini = IniPath();
			g_on = GetPrivateProfileIntA("LakeBoats", "On", 1, ini.c_str()) != 0;
			g_count = std::clamp(static_cast<int>(GetPrivateProfileIntA("LakeBoats", "Count", 6, ini.c_str())), 0, 10);
			g_view = std::clamp(static_cast<int>(GetPrivateProfileIntA("LakeBoats", "View", 5, ini.c_str())), 1, 10);
			g_rig.x = static_cast<float>(static_cast<int>(GetPrivateProfileIntA("LakeBoats", "RigRight", 0, ini.c_str())));
			g_rig.y = static_cast<float>(static_cast<int>(GetPrivateProfileIntA("LakeBoats", "RigForward", 0, ini.c_str())));
			g_rig.z = static_cast<float>(static_cast<int>(GetPrivateProfileIntA("LakeBoats", "RigUp", 11, ini.c_str())));
			std::ifstream in(kLakeFile);
			if (in) {
				std::string line;
				Lake* cur = nullptr;
				int fileVersion = 0, dropped = 0;
				while (std::getline(in, line)) {
					std::istringstream ls(line);
					std::string tag;
					if (!(ls >> tag) || tag[0] == '#') continue;
					if (tag == "v") { ls >> fileVersion; continue; }
					if (tag == "lake") {
						Lake l;
						std::string world;
						if (!(ls >> l.name >> world >> l.water)) continue;
						l.world = static_cast<RE::FormID>(std::stoul(world, nullptr, 16));
						l.saved = true;
						g_fileLakes.push_back(std::move(l));
						cur = &g_fileLakes.back();
					} else if (tag == "p" && cur) {
						Pt p{};
						if (ls >> p.x >> p.y) cur->pts.push_back(p);
					} else if (tag == "a" && cur) {
						Avoid a{};
						if (!(ls >> a.x >> a.y >> a.r)) continue;
						if (fileVersion < 55) { ++dropped; continue; }  // 55.3
						cur->avoid.push_back(a);
					}
				}
				if (dropped > 0) {
					SKSE::log::info("Lake: cleared {} old blocked spots ({})", dropped, kLakeFile);
					g_saveTimer = 30.0f;  // rewrite the file without them
				}
			}
			for (const auto& f : g_fileLakes) {
				if (std::none_of(g_lakes.begin(), g_lakes.end(), [&](const Lake& l) { return l.name == f.name; })) g_lakes.push_back(f);
			}
			SKSE::log::info("Lake: settings on={} count={} view={} offset ({:.0f},{:.0f},{:.0f}), {} lake(s) in the lakes file", g_on, g_count, g_view,
				g_rig.x, g_rig.y, g_rig.z, g_fileLakes.size());
		}

		RE::FormID PlayerWorld()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* ws = player ? player->GetWorldspace() : nullptr;
			return ws ? ws->GetFormID() : 0;
		}

		// 54: the sea is the water at the worldspace's own water level (Tamriel: the Sea of Ghosts and the coast).
		bool IsSea(Lake& a_lake)
		{
			if (a_lake.sea < 0) {
				auto* ws = RE::TESForm::LookupByID<RE::TESWorldSpace>(a_lake.world);
				a_lake.sea = ws && std::abs(a_lake.water - ws->GetDefaultWaterHeight()) < 50.0f ? 1 : 0;
				if (a_lake.sea) SKSE::log::info("Lake: '{}' is the sea (water {:.0f})", a_lake.name, a_lake.water);
			}
			return a_lake.sea == 1;
		}

		// 54: the sea harbours, from the world's map markers (so no form IDs: any load order, any map mod that keeps the names).
		const std::vector<Harbour>& Harbours(RE::FormID a_world)
		{
			if (g_harbourWorld == a_world) return g_harbours;
			g_harbourWorld = a_world;
			g_harbours.clear();
			auto* ws = RE::TESForm::LookupByID<RE::TESWorldSpace>(a_world);
			auto* cell = ws ? ws->persistentCell : nullptr;
			if (!cell) return g_harbours;
			static constexpr const char* kNames[] = { "Solitude", "Dawnstar", "Windhelm", "East Empire Company Warehouse" };
			std::string list;
			cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
				auto* mm = a_ref ? a_ref->extraList.GetByType<RE::ExtraMapMarker>() : nullptr;
				const char* name = mm && mm->mapData ? mm->mapData->locationName.GetFullName() : nullptr;
				if (name && std::any_of(std::begin(kNames), std::end(kNames), [&](const char* n) { return std::string_view(name) == n; })) {
					const auto p = a_ref->GetPosition();
					g_harbours.push_back({ name, p.x, p.y });
					list += std::format(" {} ({:.0f},{:.0f})", name, p.x, p.y);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			SKSE::log::info("Lake: {} sea harbour(s):{}", g_harbours.size(), list);
			return g_harbours;
		}

		const Harbour* HarbourNear(float a_x, float a_y, RE::FormID a_world)
		{
			const Harbour* best = nullptr;
			float bestD = kHarbourRadius * kHarbourRadius;
			for (const auto& h : Harbours(a_world)) {
				const float d2 = Dist2(a_x, a_y, Pt{ h.x, h.y });
				if (d2 < bestD) { bestD = d2; best = &h; }
			}
			return best;
		}

		// 54: a new boat may go here: in water that is not loaded, or loaded but far from the player and out of the camera's view.
		bool SpawnAllowed(const Pt& a_p, float a_water)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* tes = RE::TES::GetSingleton();
			if (!player || !tes) return false;
			auto* cell = tes->GetCell(RE::NiPoint3{ a_p.x, a_p.y, a_water });
			if (!cell || !cell->IsAttached()) return true;
			const RE::NiPoint3 pp = player->GetPosition();
			const float dx = a_p.x - pp.x, dy = a_p.y - pp.y, d = std::sqrt(dx * dx + dy * dy);
			if (d < kNoSpawnNear) return false;
			if (d >= kViewRange) return true;
			auto* cam = RE::Main::WorldRootCamera();
			if (!cam) return false;
			const auto& m = cam->world.rotate;  // NiCamera: x is the view direction
			const float fx = m.entry[0][0], fy = m.entry[1][0], fl = std::sqrt(fx * fx + fy * fy);
			if (fl < 0.01f) return false;
			return (dx * fx + dy * fy) / (d * fl) < kViewCos;
		}

		// 54: a small stable hash for per-day, per-place rolls (the same answer every frame).
		std::uint32_t Roll(std::int64_t a_a, std::int64_t a_b, std::int64_t a_c)
		{
			std::uint64_t h = 1469598103934665603ull;
			for (auto v : { a_a, a_b, a_c }) { h ^= static_cast<std::uint64_t>(v); h *= 1099511628211ull; }
			return static_cast<std::uint32_t>(h ^ (h >> 29));
		}

		// 54: the vehicle point of a boat with the numpad offset (right / forward in the boat's frame, up).
		RE::NiPoint3 VehicleAt(float a_x, float a_y, float a_yaw, float a_z)
		{
			const float s = std::sin(a_yaw), c = std::cos(a_yaw);
			return { a_x + c * g_rig.x + s * g_rig.y, a_y - s * g_rig.x + c * g_rig.y, a_z + g_rig.z };
		}

		// ---- water --------------------------------------------------------------------------------------------------------
		enum class WS { kUnknown, kBlocked, kOpen };
		// kUnknown where the cell is not loaded (a virtual boat goes on trusting its route there).
		WS WaterState(float a_x, float a_y, float a_level, float a_minDepth, float* a_water = nullptr, float* a_depth = nullptr)
		{
			auto* tes = RE::TES::GetSingleton();
			if (!tes) return WS::kUnknown;
			const RE::NiPoint3 p{ a_x, a_y, 0.0f };
			auto* cell = tes->GetCell(p);
			if (!cell || !cell->IsAttached()) return WS::kUnknown;
			float w = 0.0f, land = 0.0f;
			if (!cell->GetWaterHeight(p, w) || !std::isfinite(w) || std::abs(w) > 1000000.0f) return WS::kBlocked;
			if (!tes->GetLandHeight(p, land) || !std::isfinite(land)) return WS::kBlocked;
			if (a_water) *a_water = w;
			if (a_depth) *a_depth = w - land;
			if (a_level > -1.0e8f && std::abs(w - a_level) > kLevelTolerance) return WS::kBlocked;
			return w - land >= a_minDepth ? WS::kOpen : WS::kBlocked;
		}
		bool OpenWater(float a_x, float a_y, float a_level, float a_minDepth) { return WaterState(a_x, a_y, a_level, a_minDepth) == WS::kOpen; }

		bool InAvoid(const Lake& a_lake, float a_x, float a_y, float a_pad = 0.0f)
		{
			return std::any_of(a_lake.avoid.begin(), a_lake.avoid.end(), [&](const Avoid& a) {
				const float dx = a_x - a.x, dy = a_y - a.y, r = a.r + a_pad;
				return dx * dx + dy * dy < r * r;
			});
		}

		// A straight run between two points: never through a blocked spot, never over known land or shallows.
		bool ClearRun(const Lake& a_lake, const Pt& a_a, const Pt& a_b)
		{
			const float dx = a_b.x - a_a.x, dy = a_b.y - a_a.y;
			const float len = std::sqrt(dx * dx + dy * dy);
			const int steps = (std::max)(1, static_cast<int>(len / 250.0f));
			for (int k = 1; k <= steps; ++k) {
				const float t = static_cast<float>(k) / static_cast<float>(steps);
				const float x = a_a.x + dx * t, y = a_a.y + dy * t;
				if (InAvoid(a_lake, x, y, 150.0f)) return false;
				if (WaterState(x, y, a_lake.water, kMinDepth * 0.6f) == WS::kBlocked) return false;
			}
			return true;
		}

		// The lake's level: the nearest deep water to the player.
		bool FindLevel(const RE::NiPoint3& a_pp, float& a_level)
		{
			for (float r = 0.0f; r <= 3600.0f; r += 300.0f) {
				for (int k = 0; k < 16; ++k) {
					const float a = 6.28318f * static_cast<float>(k) / 16.0f;
					float w = 0.0f;
					if (WaterState(a_pp.x + r * std::sin(a), a_pp.y + r * std::cos(a), -1.0e9f, kMinDepth, &w) == WS::kOpen) { a_level = w; return true; }
					if (r == 0.0f) break;
				}
			}
			return false;
		}

		// One grid point: deep open water at the level, with water on all four sides. Added if not too close to another.
		void SamplePoint(float a_x, float a_y, float a_level, std::vector<Pt>& a_pts)
		{
			if (!OpenWater(a_x, a_y, a_level, kMinDepth)) return;
			for (const auto& o : { Pt{ kClearance, 0.0f }, Pt{ -kClearance, 0.0f }, Pt{ 0.0f, kClearance }, Pt{ 0.0f, -kClearance } }) {
				if (!OpenWater(a_x + o.x, a_y + o.y, a_level, kMinDepth * 0.5f)) return;
			}
			if (std::none_of(a_pts.begin(), a_pts.end(), [&](const Pt& q) { return Dist2(a_x, a_y, q) < kPointSpacing * kPointSpacing; })) a_pts.push_back({ a_x, a_y });
		}

		// Deep open water around the player at one level (lakehere: all at once).
		bool SampleHere(std::vector<Pt>& a_pts, float& a_level, std::string& a_why)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			if (!player || !cell || !cell->IsExteriorCell()) { a_why = "not outdoors"; return false; }
			const RE::NiPoint3 pp = player->GetPosition();
			if (!FindLevel(pp, a_level)) { a_why = "no deep water nearby"; return false; }
			const int n = static_cast<int>(kSampleRadius / kSampleStep);
			for (int i = -n; i <= n; ++i)
				for (int j = -n; j <= n; ++j) SamplePoint(pp.x + static_cast<float>(i) * kSampleStep, pp.y + static_cast<float>(j) * kSampleStep, a_level, a_pts);
			return true;
		}

		// 53: the same sample spread over frames (the automatic one while exploring).
		struct SampleJob
		{
			bool active = false;
			Pt centre{};
			float level = 0.0f;
			int n = 0, idx = 0;
			std::vector<Pt> pts;
		};
		SampleJob g_job;

		// New points into a lake: kept apart, never in a blocked spot. Returns how many were added.
		int MergePoints(Lake& a_lake, const std::vector<Pt>& a_new)
		{
			int added = 0;
			for (const auto& p : a_new) {
				if (InAvoid(a_lake, p.x, p.y, 150.0f)) continue;
				if (std::any_of(a_lake.pts.begin(), a_lake.pts.end(), [&](const Pt& q) { return Dist2(p.x, p.y, q) < kPointSpacing * kPointSpacing; })) continue;
				a_lake.pts.push_back(p);
				++added;
			}
			return added;
		}

		// The lake these points belong to: same world, same level, and touching its known points.
		Lake* LakeFor(const std::vector<Pt>& a_pts, float a_level)
		{
			const RE::FormID world = PlayerWorld();
			for (auto& l : g_lakes) {
				if (l.world != world || std::abs(l.water - a_level) > kLevelTolerance) continue;
				for (const auto& p : a_pts) {
					if (std::any_of(l.pts.begin(), l.pts.end(), [&](const Pt& q) { return Dist2(p.x, p.y, q) < 1500.0f * 1500.0f; })) return &l;
				}
			}
			return nullptr;
		}

		// ---- the NPCs -----------------------------------------------------------------------------------------------------
		std::vector<RE::TESNPC*> g_npcs;
		bool g_npcsPicked = false;

		void PickNpcs()
		{
			g_npcsPicked = true;
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) return;
			static constexpr const char* kNames[] = { "Hunter", "Fisherman", "Fisher", "Farmer", "Traveler", "Citizen", "Laborer", "Lumberjack", "Miner", "Woodcutter" };
			static constexpr const char* kRaces[] = { "NordRace", "ImperialRace", "BretonRace", "RedguardRace" };
			std::string list;
			std::vector<RE::TESNPC*> undressed;
			for (auto* npc : dh->GetFormArray<RE::TESNPC>()) {
				if (!npc || (npc->GetFormID() >> 24) != 0x00 || npc->IsUnique()) continue;  // Skyrim.esm only: no dependency
				const char* name = npc->GetName();
				if (!name || std::none_of(std::begin(kNames), std::end(kNames), [&](const char* n) { return std::string_view(name) == n; })) continue;
				const char* race = npc->race ? npc->race->GetFormEditorID() : nullptr;
				if (!race || std::none_of(std::begin(kRaces), std::end(kRaces), [&](const char* r) { return std::string_view(race) == r; })) continue;
				// 54: only bases that wear an outfit of their own (some take their gear from a template and came out naked).
				if (!npc->defaultOutfit || npc->defaultOutfit->outfitItems.empty()) { undressed.push_back(npc); continue; }
				g_npcs.push_back(npc);
				if (g_npcs.size() <= 24) list += std::format(" {:08X}:{}", npc->GetFormID(), name);
			}
			if (g_npcs.empty()) g_npcs = undressed;  // none with an outfit (a mod changed them): these, and Dress() clothes them
			SKSE::log::info("Lake: {} crew bases (unnamed hunters, fishermen, farmers...), {} without an outfit left out:{}", g_npcs.size(), undressed.size(), list);
		}

		// 54: plain clothes for a crewman who still has nothing on (Skyrim.esm, found by name, unenchanted).
		std::vector<RE::TESObjectARMO*> g_bodyClothes, g_feetClothes;
		bool g_clothesPicked = false;

		void PickClothes()
		{
			g_clothesPicked = true;
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) return;
			static constexpr const char* kBody[] = { "Farm Clothes", "Roughspun Tunic", "Hide Armor", "Fur Armor" };
			static constexpr const char* kFeet[] = { "Boots", "Footwraps", "Hide Boots", "Fur Shoes" };
			for (auto* a : dh->GetFormArray<RE::TESObjectARMO>()) {
				if (!a || (a->GetFormID() >> 24) != 0x00 || a->formEnchanting || a->templateArmor) continue;
				const char* name = a->GetName();
				if (!name) continue;
				const std::string_view n(name);
				if (a->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kBody) && std::any_of(std::begin(kBody), std::end(kBody), [&](const char* k) { return n == k; })) g_bodyClothes.push_back(a);
				else if (a->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kFeet) && std::any_of(std::begin(kFeet), std::end(kFeet), [&](const char* k) { return n == k; })) g_feetClothes.push_back(a);
			}
			SKSE::log::info("Lake: {} body and {} feet clothes for undressed crew", g_bodyClothes.size(), g_feetClothes.size());
		}

		// Returns true when something was put on.
		bool Dress(RE::Actor* a_actor)
		{
			if (!g_clothesPicked) PickClothes();
			auto* eq = RE::ActorEquipManager::GetSingleton();
			if (!a_actor || !eq) return false;
			bool any = false;
			auto wear = [&](RE::BGSBipedObjectForm::BipedObjectSlot a_slot, const std::vector<RE::TESObjectARMO*>& a_from) {
				if (a_from.empty() || a_actor->GetWornArmor(a_slot)) return;
				auto* armor = Pick(a_from);
				a_actor->AddObjectToContainer(armor, nullptr, 1, nullptr);
				eq->EquipObject(a_actor, armor);
				any = true;
			};
			wear(RE::BGSBipedObjectForm::BipedObjectSlot::kBody, g_bodyClothes);
			wear(RE::BGSBipedObjectForm::BipedObjectSlot::kFeet, g_feetClothes);
			return any;
		}

		void SetDontMove(RE::Actor* a_actor, bool a_value)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!a_actor || !policy) return;
			const auto handle = policy->GetHandleForObject(static_cast<RE::VMTypeID>(a_actor->GetFormType()), a_actor);
			if (!handle) return;
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
			auto args = RE::MakeFunctionArguments(static_cast<bool>(a_value));
			vm->DispatchMethodCall(handle, RE::BSFixedString("Actor"), RE::BSFixedString("SetDontMove"), args, callback);
		}

		// ---- routes -------------------------------------------------------------------------------------------------------
		void PickTarget(Boat& a_b, const Lake& a_lake, bool a_flee)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			const RE::NiPoint3 pp = player ? player->GetPosition() : RE::NiPoint3{};
			const Pt here{ a_b.x, a_b.y };
			std::vector<Pt> longRuns, any;
			const Pt* best = nullptr;
			float bestD = -1.0f;
			for (const auto& p : a_lake.pts) {
				const float d2 = Dist2(a_b.x, a_b.y, p);
				if (d2 < 600.0f * 600.0f) continue;
				if (a_flee) {
					const float fromPlayer = Dist2(pp.x, pp.y, p);
					if (fromPlayer > bestD && ClearRun(a_lake, here, p)) { bestD = fromPlayer; best = &p; }
					continue;
				}
				if (Dist2(pp.x, pp.y, p) < 1200.0f * 1200.0f) continue;  // not right at the player
				// 53: not where another boat is or is heading, and not through a resting one.
				bool taken = false;
				for (const auto& o : a_lake.boats) {
					if (&o == &a_b) continue;
					if (Dist2(o.x, o.y, p) < 1500.0f * 1500.0f || (o.mode != Mode::kFish && Dist2(o.target.x, o.target.y, p) < 1500.0f * 1500.0f)) { taken = true; break; }
					if (o.mode == Mode::kFish) {
						const float sx = p.x - here.x, sy = p.y - here.y, sl2 = sx * sx + sy * sy;
						const float t = sl2 > 1.0f ? std::clamp(((o.x - here.x) * sx + (o.y - here.y) * sy) / sl2, 0.0f, 1.0f) : 0.0f;
						if (Dist2(here.x + sx * t, here.y + sy * t, Pt{ o.x, o.y }) < 600.0f * 600.0f) { taken = true; break; }
					}
				}
				if (taken) continue;
				if (d2 > kRowMax * kRowMax || !ClearRun(a_lake, here, p)) continue;
				any.push_back(p);
				if (d2 >= kRowMin * kRowMin) longRuns.push_back(p);
			}
			if (a_flee && best) { a_b.target = *best; return; }
			if (!longRuns.empty()) { a_b.target = Pick(longRuns); return; }  // 52: long rows first
			if (!any.empty()) {
				// The lake is small (or mostly blocked) here: the farthest reachable point.
				a_b.target = *std::max_element(any.begin(), any.end(), [&](const Pt& p, const Pt& q) { return Dist2(a_b.x, a_b.y, p) < Dist2(a_b.x, a_b.y, q); });
				return;
			}
			if (!a_lake.pts.empty()) a_b.target = Pick(a_lake.pts);
		}

		// A new boat on a free spot of the lake (spaced from the others), half rowing, half resting.
		bool NewBoat(Lake& a_lake, const Pt* a_near = nullptr, float a_radius = 0.0f)
		{
			if (!g_npcsPicked) PickNpcs();
			if (g_npcs.empty() || a_lake.pts.empty()) return false;
			// 53: farthest first: the open spot farthest from every boat already out, and from the player (who counts
			// as one needing 1.5x the room), so the boats spread over the whole lake instead of bunching where you stand.
			// 54: only where the player cannot see it appear (SpawnAllowed); a_near limits it to an area (a harbour, the sea round the player).
			auto* player = RE::PlayerCharacter::GetSingleton();
			const RE::NiPoint3 pp = player ? player->GetPosition() : RE::NiPoint3{};
			struct C { const Pt* p; float score; };
			std::vector<C> cands;
			for (const auto& p : a_lake.pts) {
				if (a_near && Dist2(a_near->x, a_near->y, p) > a_radius * a_radius) continue;
				if (InAvoid(a_lake, p.x, p.y, 200.0f) || !SpawnAllowed(p, a_lake.water)) continue;
				float m = Dist2(pp.x, pp.y, p) / 2.25f;
				for (const auto& ob : a_lake.boats) m = (std::min)(m, Dist2(ob.x, ob.y, p));
				cands.push_back({ &p, m });
			}
			if (cands.empty()) return false;
			std::sort(cands.begin(), cands.end(), [](const C& a, const C& b) { return a.score > b.score; });
			const std::size_t top = cands.size() < 3 ? cands.size() : 3;  // a little variety among the best three
			const C& chosen = cands[std::uniform_int_distribution<std::size_t>(0, top - 1)(g_rng)];
			const Pt* at = chosen.p;
			SKSE::log::info("Lake: new boat on '{}' at ({:.0f},{:.0f}), nearest boat or player {:.0f} away", a_lake.name, at->x, at->y, std::sqrt(chosen.score));
			Boat b;
			b.x = at->x;
			b.y = at->y;
			b.yaw = Rand(0.0f, 6.28318f);
			b.mode = Rand(0.0f, 1.0f) < 0.5f ? Mode::kFish : Mode::kRow;
			b.modeTimer = Rand(20.0f, 120.0f);
			const int people = Rand(0.0f, 1.0f) < 0.5f ? 2 : 1;
			for (int k = 0; k < people; ++k) b.people.push_back(g_npcs[std::uniform_int_distribution<std::size_t>(0, g_npcs.size() - 1)(g_rng)]->GetFormID());
			if (b.mode == Mode::kRow) PickTarget(b, a_lake, false);
			a_lake.boats.push_back(std::move(b));
			return true;
		}

		// ---- drawing a boat -----------------------------------------------------------------------------------------------
		std::vector<RE::ObjectRefHandle*> Refs(Boat& a_b)
		{
			return { &a_b.hull, &a_b.oar[0], &a_b.oar[1], &a_b.bench[0], &a_b.bench[1], &a_b.lantern, &a_b.light };
		}

		RE::TESObjectREFR* PlaceAt(RE::TESObjectREFR* a_at, RE::TESBoundObject* a_base, const char* a_type, RE::ObjectRefHandle& a_out)
		{
			if (!a_at || !a_base) return nullptr;
			auto ref = a_at->PlaceObjectAtMe(a_base, false);
			if (!ref) { SKSE::log::warn("Lake: placing {} failed", a_type); return nullptr; }
			ref->SetCollision(false);
			BoatBow::AddLakeRef(ref.get(), std::string("Lake ") + a_type);
			a_out = ref->GetHandle();
			return ref.get();
		}

		void DeleteRef(RE::ObjectRefHandle& a_h)
		{
			if (auto r = a_h.get()) {
				BoatBow::RemoveLakeRef(r->GetFormID());
				r->Disable();
				r->SetDelete(true);
			}
			a_h.reset();
		}

		// Back to virtual: the refs go, the boat stays where it is on the lake.
		void Hide(Boat& a_b, const char* a_why)
		{
			for (auto& c : a_b.crew) {
				if (auto a = c.actor.get()) BoatBow::DeleteActorLater(a.get());  // 51.1: never deleted in the frame it is disabled
			}
			a_b.crew.clear();
			for (auto* h : Refs(a_b)) DeleteRef(*h);
			a_b.logged = false;
			SKSE::log::info("Lake: boat at ({:.0f},{:.0f}) no longer drawn ({})", a_b.x, a_b.y, a_why);
		}

		// A ref in the cell at this spot, so what is placed there belongs to that cell (not to the player's).
		RE::TESObjectREFR* AnchorAt(const RE::NiPoint3& a_pos)
		{
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(a_pos) : nullptr;
			if (!cell || !cell->IsAttached()) return nullptr;
			RE::TESObjectREFR* found = nullptr;
			cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
				if (a_ref && !a_ref->IsDisabled() && !a_ref->IsDeleted() && a_ref->Is3DLoaded() && !a_ref->As<RE::Actor>()) { found = a_ref; return RE::BSContainer::ForEachResult::kStop; }
				return RE::BSContainer::ForEachResult::kContinue;
			});
			return found;
		}

		bool Show(Boat& a_b, const Lake& a_lake)
		{
			const RE::NiPoint3 vehicle = VehicleAt(a_b.x, a_b.y, a_b.yaw, a_lake.water + kVehicleAboveWater);
			const RE::NiPoint3 centre = BoatBow::HullCentre(vehicle, BoatBow::YawMatrix(a_b.yaw));
			auto* anchor = AnchorAt(centre);
			if (!anchor) return false;
			auto* hull = PlaceAt(anchor, BoatBow::HullBase(), "hull", a_b.hull);
			if (!hull) return false;
			hull->SetPosition(centre);
			hull->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, a_b.yaw });
			for (int i = 0; i < 2; ++i) {
				if (auto* o = PlaceAt(hull, BoatBow::OarBase(), "oar", a_b.oar[i])) o->SetPosition(centre);
				if (auto* bn = PlaceAt(hull, BoatBow::BenchBase(), "bench", a_b.bench[i])) bn->SetPosition(centre);
			}
			std::string names;
			for (std::size_t k = 0; k < a_b.people.size(); ++k) {
				auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(a_b.people[k]);
				auto placed = npc ? hull->PlaceObjectAtMe(npc, false) : nullptr;
				auto* actor = placed ? placed->As<RE::Actor>() : nullptr;
				if (!actor) continue;
				BoatBow::AddLakeRef(actor, "Lake crew");
				Crew c;
				c.actor = actor->GetHandle();
				c.rower = k == 0;
				c.idleTimer = Rand(15.0f, 40.0f);
				a_b.crew.push_back(c);
				names += std::format(" {}{:08X}", c.rower ? "rower " : "stern ", actor->GetFormID());
			}
			a_b.refTimer = 0.0f;
			auto* player = RE::PlayerCharacter::GetSingleton();
			const float d = player ? std::sqrt(Dist2(player->GetPosition().x, player->GetPosition().y, Pt{ a_b.x, a_b.y })) : 0.0f;
			SKSE::log::info("Lake: boat on '{}' drawn at ({:.0f},{:.0f}), {:.0f} from the player, {}:{}", a_lake.name, a_b.x, a_b.y, d,
				a_b.mode == Mode::kFish ? "resting" : "rowing", names);
			return true;
		}

		bool Night()
		{
			auto* cal = RE::Calendar::GetSingleton();
			const float h = cal ? cal->GetHour() : 12.0f;
			return h >= 18.5f || h < 5.5f;
		}

		// 52: something solid ahead (a moored longboat, a dock, a rock): a horizontal ray at the hull, like the player's feelers.
		bool SolidAhead(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, RE::NiPoint3& a_hit, std::string& a_what)
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
			// The same "not an obstacle" layers as the player's boat sweep, plus actors (the give-way handles the player).
			if (layer == RE::COL_LAYER::kWater || layer == RE::COL_LAYER::kTrigger || layer == RE::COL_LAYER::kNonCollidable || id >= 47 ||
				layer == RE::COL_LAYER::kActorZone || layer == RE::COL_LAYER::kCharController || layer == RE::COL_LAYER::kBiped ||
				layer == RE::COL_LAYER::kProjectileZone || layer == RE::COL_LAYER::kAcousticSpace || layer == RE::COL_LAYER::kPortal ||
				layer == RE::COL_LAYER::kCollisionBox || layer == RE::COL_LAYER::kAvoidBox || layer == RE::COL_LAYER::kDoorDetection ||
				layer == RE::COL_LAYER::kDeadBip || layer == RE::COL_LAYER::kItemPick || layer == RE::COL_LAYER::kWeapon)
				return false;
			auto* ref = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
			if (ref && ref->As<RE::Actor>()) return false;
			// 55.3 (v1.0.55): a dynamic ref (FF......) is ours: another lake boat's hull, oars, bench or lantern, or the player's
			// boat. Boat to boat avoidance stays with the steering, give way and push apart.
			if (ref && (ref->GetFormID() & 0xFF000000) == 0xFF000000) return false;
			a_hit = a_from + (a_to - a_from) * std::clamp(pick.rayOutput.hitFraction, 0.0f, 1.0f);
			const char* name = ref && ref->GetBaseObject() ? ref->GetBaseObject()->GetName() : nullptr;
			a_what = std::format("{} (layer {})", name && *name ? std::string(name) : (ref ? std::format("{:08X}", ref->GetFormID()) : std::string("terrain")), id);
			return true;
		}

		// Blocked at a_hit: no route through there for any boat of the lake, and this one turns to a new target.
		void Blocked(Boat& a_b, Lake& a_lake, const RE::NiPoint3& a_hit, const std::string& a_what)
		{
			// 55.3: the same boat blocked again within 5 s and 300 units adds no new spot (stops the chatter).
			const float bx = a_hit.x - a_b.lastBlockX, by = a_hit.y - a_b.lastBlockY;
			const bool chatter = a_b.time - a_b.lastBlockAt < 5.0f && bx * bx + by * by < 300.0f * 300.0f;
			if (!chatter && !InAvoid(a_lake, a_hit.x, a_hit.y)) {
				a_b.lastBlockAt = a_b.time;
				a_b.lastBlockX = a_hit.x;
				a_b.lastBlockY = a_hit.y;
				if (a_lake.avoid.size() >= 300) a_lake.avoid.erase(a_lake.avoid.begin());
				a_lake.avoid.push_back({ a_hit.x, a_hit.y, 350.0f });
				std::erase_if(a_lake.pts, [&](const Pt& p) { return InAvoid(a_lake, p.x, p.y, 150.0f); });
				if (a_lake.saved) g_saveTimer = 30.0f;  // written to the lakes file a little later (batched)
			}
			a_b.speed *= 0.3f;
			const float oldYaw = a_b.yaw;
			PickTarget(a_b, a_lake, false);
			if (a_b.blockLog <= 0.0f) {
				a_b.blockLog = 5.0f;
				SKSE::log::info("Lake: boat at ({:.0f},{:.0f}) heading {:.0f} deg blocked by {} at ({:.0f},{:.0f}), new target ({:.0f},{:.0f}), {} blocked spot(s) on '{}'",
					a_b.x, a_b.y, oldYaw * 57.2958f, a_what, a_hit.x, a_hit.y, a_b.target.x, a_b.target.y, a_lake.avoid.size(), a_lake.name);
			}
		}

		void TendCrew(Boat& a_b, Crew& a_c, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot, float a_dt, bool a_wantRow)
		{
			auto actor = a_c.actor.get();
			if (!actor || !actor->Is3DLoaded()) return;
			if (!a_c.setup) {
				a_c.setup = true;
				actor->SetCollision(false);
				SetDontMove(actor.get(), true);
				if (auto* av = actor->AsActorValueOwner()) {
					av->SetActorValue(RE::ActorValue::kAggression, 0.0f);  // never starts a fight
					av->SetActorValue(RE::ActorValue::kConfidence, 0.0f);  // rows away when attacked
				}
				a_c.sitTimer = 0.0f;
			}
			if (actor->IsDead()) return;
			if (a_c.dressTimer > 0.0f && (a_c.dressTimer -= a_dt) <= 0.0f && Dress(actor.get()))
				SKSE::log::info("Lake: {} {:08X} had no clothes on, dressed", actor->GetName(), actor->GetFormID());
			const RE::NiPoint3 seat = BoatBow::PlaceRigidNode(nullptr, a_c.rower ? kRowerSeat : kSternSeat, 0.0f, 1.0f, a_vehicle, a_rot);
			actor->SetPosition(seat, true);
			actor->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, a_b.yaw + (a_c.rower ? kRowerYaw : kSternYaw) });
			a_c.retry -= a_dt;
			if (a_c.rower && a_wantRow != a_c.rowing && a_c.retry <= 0.0f) {
				a_c.retry = 0.5f;
				const bool ok = actor->NotifyAnimationGraph(a_wantRow ? kRowStartEvent : kRowStopEvent);
				if (ok) {
					a_c.rowing = a_wantRow;
					a_c.sitTimer = a_wantRow ? 0.0f : 0.3f;
					// 56.19.5: this start counts for the 3D and distance it was sent at.
					a_c.last3D = actor->Get3D(false);
					auto* player = RE::PlayerCharacter::GetSingleton();
					a_c.closeBy = player && (actor->GetPosition() - player->GetPosition()).Length() < 4000.0f;
				}
				if (!a_b.logged || !ok) SKSE::log::info("Lake: {} {:08X} {} {}", actor->GetName(), actor->GetFormID(), a_wantRow ? kRowStartEvent : kRowStopEvent, ok ? "accepted" : "rejected");
				a_b.logged = true;
				return;
			}
			if (a_c.rowing) {
				constexpr float kRowResendNear = 4000.0f;
				const void* now3D = actor->Get3D(false);
				auto* player = RE::PlayerCharacter::GetSingleton();
				const bool nearNow = player && (actor->GetPosition() - player->GetPosition()).Length() < kRowResendNear;
				const char* why = now3D != a_c.last3D ? "3D loaded" : nearNow && !a_c.closeBy ? "within 4000 of the player" : nullptr;
				a_c.last3D = now3D;
				a_c.closeBy = nearNow;
				if (why) {
					const bool ok = actor->NotifyAnimationGraph(kRowStartEvent);
					SKSE::log::info("Lake: {} {:08X} {} again ({}): {}", actor->GetName(), actor->GetFormID(), kRowStartEvent, why, ok ? "accepted" : "rejected");
				}
				return;
			}
			if ((a_c.sitTimer -= a_dt) <= 0.0f) {
				a_c.sitTimer = 3.0f;
				for (const char* ev : { "IdleChairEnterInstant", "IdleChairSitting" }) {
					if (actor->NotifyAnimationGraph(ev)) break;
				}
			}
			if (a_b.mode == Mode::kFish && (a_c.idleTimer -= a_dt) <= 0.0f) {
				a_c.idleTimer = Rand(20.0f, 45.0f);
				static constexpr const char* kIdles[] = { "ChairDrinkingStart", "idleChairLookDown", "idleChairArmsCrossedVar1", "idleChairShoulderFlex", "idleChairVar2" };
				actor->NotifyAnimationGraph(kIdles[std::uniform_int_distribution<int>(0, 4)(g_rng)]);
				a_c.sitTimer = 6.0f;
			}
		}

		// One boat, one frame: the same movement drawn or not; drawn boats also feel the water, obstacles, the player and the others.
		void TendBoat(Boat& a_b, Lake& a_lake, float a_dt)
		{
			const bool live = a_b.Live();
			auto* player = RE::PlayerCharacter::GetSingleton();
			const RE::NiPoint3 pp = player ? player->GetPosition() : RE::NiPoint3{};
			a_b.time += a_dt;
			a_b.blockLog -= a_dt;

			// Who is aboard (drawn boats only: a virtual crew is always fine).
			bool haveRower = !a_b.people.empty();
			if (live) {
				haveRower = false;
				bool hurt = false;
				for (auto& c : a_b.crew) {
					auto a = c.actor.get();
					if (!a) continue;
					if (c.rower && !a->IsDead()) haveRower = true;
					if (a->IsDead() || a->IsInCombat()) hurt = true;
				}
				if (hurt && haveRower && a_b.mode != Mode::kFlee) {
					a_b.mode = Mode::kFlee;
					PickTarget(a_b, a_lake, true);
					SKSE::log::info("Lake: a boat is attacked, rowing away");
				}
			}

			float want = 0.0f;
			const float dx = a_b.target.x - a_b.x, dy = a_b.target.y - a_b.y;
			const float dist = std::sqrt(dx * dx + dy * dy);
			const float toPlayer = std::sqrt(Dist2(pp.x, pp.y, Pt{ a_b.x, a_b.y }));
			// 53: keep clear of the other boats. Hull centres (about 250 ahead of the stern seat) are compared: a boat
			// ahead within kKeepClear to the side is steered around, and of two boats closing in, the later one gives way.
			float avoidTurn = 0.0f, yield = 1.0f;
			{
				const RE::NiPoint3 f0{ std::sin(a_b.yaw), std::cos(a_b.yaw), 0.0f };
				const float cx = a_b.x + f0.x * 250.0f, cy = a_b.y + f0.y * 250.0f;
				for (auto& o : a_lake.boats) {
					if (&o == &a_b) continue;
					const float ocx = o.x + std::sin(o.yaw) * 250.0f, ocy = o.y + std::cos(o.yaw) * 250.0f;
					const float ox = ocx - cx, oy = ocy - cy, od = std::sqrt(ox * ox + oy * oy);
					if (od < 1.0f) { a_b.x += Rand(-5.0f, 5.0f); continue; }
					if (od < 650.0f) {  // overlapping hulls: pushed apart (both get this)
						const float push = (650.0f - od) * 0.8f * a_dt;
						a_b.x -= ox / od * push;
						a_b.y -= oy / od * push;
					}
					if (od > 2000.0f) continue;
					const float ahead = ox * f0.x + oy * f0.y;
					const float lateral = ox * f0.y - oy * f0.x;  // + = to the right
					if (ahead <= 0.0f || std::abs(lateral) > kKeepClear) continue;
					const float closeness = 1.0f - ahead / 2000.0f;
					avoidTurn += (lateral >= 0.0f ? -1.0f : 1.0f) * 0.9f * closeness * (1.0f - std::abs(lateral) / kKeepClear);
					if (ahead < 1100.0f) yield = (std::min)(yield, &o < &a_b ? 0.15f : 0.55f);
				}
				avoidTurn = std::clamp(avoidTurn, -1.1f, 1.1f);
			}
			if (haveRower && a_b.mode != Mode::kFish) {
				if (dist < 200.0f) {
					a_b.mode = Mode::kFish;
					a_b.modeTimer = Rand(40.0f, 120.0f);
				} else {
					want = a_b.mode == Mode::kFlee ? kFleeSpeed : kRowSpeed;
					want *= std::clamp(dist / 400.0f, 0.35f, 1.0f) * yield;
					float turn = std::atan2(dx, dy) + avoidTurn - a_b.yaw;
					while (turn > 3.14159f) turn -= 6.28318f;
					while (turn < -3.14159f) turn += 6.28318f;
					a_b.yaw += std::clamp(turn, -kTurnRate * a_dt, kTurnRate * a_dt);
					if (std::abs(turn) > 1.2f) want *= 0.4f;
				}
			} else if (haveRower && (a_b.modeTimer -= a_dt) <= 0.0f) {
				a_b.mode = Mode::kRow;
				PickTarget(a_b, a_lake, false);
			}
			if (live) {
				if (haveRower && toPlayer < kGiveWay && a_b.mode == Mode::kRow) {
					want = 0.0f;
					PickTarget(a_b, a_lake, true);
					a_b.mode = Mode::kFlee;
				}
				if (a_b.mode == Mode::kFlee && toPlayer > 2500.0f && dist < 300.0f) { a_b.mode = Mode::kFish; a_b.modeTimer = Rand(30.0f, 60.0f); }
				const RE::NiPoint3 fwd{ std::sin(a_b.yaw), std::cos(a_b.yaw), 0.0f };
				// 52: solid things ahead, three rays at the hull every quarter second.
				if (want > 0.0f && (a_b.rayTimer -= a_dt) <= 0.0f) {
					a_b.rayTimer = 0.25f;
					const float len = 350.0f + a_b.speed * 2.5f;
					const RE::NiPoint3 from{ a_b.x + fwd.x * 300.0f, a_b.y + fwd.y * 300.0f, a_lake.water + 40.0f };  // from mid-boat toward the bow (the vehicle point is at the stern)
					for (float off : { 0.0f, 0.4f, -0.4f }) {
						const RE::NiPoint3 dir{ std::sin(a_b.yaw + off), std::cos(a_b.yaw + off), 0.0f };
						RE::NiPoint3 hit{};
						std::string what;
						if (SolidAhead(from, from + dir * len, hit, what)) { Blocked(a_b, a_lake, hit, what); want = 0.0f; break; }
					}
				}
			}
			a_b.speed += (want - a_b.speed) * (1.0f - std::exp(-0.8f * a_dt));
			const float nx = a_b.x + std::sin(a_b.yaw) * a_b.speed * a_dt, ny = a_b.y + std::cos(a_b.yaw) * a_b.speed * a_dt;
			const WS ws = WaterState(nx, ny, a_lake.water, 40.0f);
			if (ws != WS::kBlocked && !InAvoid(a_lake, nx, ny)) { a_b.x = nx; a_b.y = ny; }
			else { a_b.speed = 0.0f; if (haveRower) PickTarget(a_b, a_lake, false); }
			if (!live) return;

			// The oars follow the rower: a stroke while rowing, resting at the oarlocks while still.
			auto hull = a_b.hull.get();
			if (!hull) return;
			const bool rowing = haveRower && a_b.speed > 15.0f && want > 0.0f;
			a_b.amp += ((rowing ? 1.0f : 0.0f) - a_b.amp) * (1.0f - std::exp(-3.0f * a_dt));
			a_b.phase = std::fmod(a_b.phase + a_dt * 6.28318f * kRowStrokesPerSecond * BoatBow::Stroke().rate * (a_b.mode == Mode::kFlee ? 1.6f : 1.0f), 6.28318f);
			if (a_b.amp < 0.01f) a_b.phase = 0.0f;

			float heave = 0.0f;
			const RE::NiMatrix3 rot = BoatBow::RockedFrame(a_b.yaw, a_b.time + static_cast<float>(hull->GetFormID() % 97), heave);
			const RE::NiPoint3 vehicle = VehicleAt(a_b.x, a_b.y, a_b.yaw, a_lake.water + kVehicleAboveWater + heave);
			if ((a_b.refTimer -= a_dt) <= 0.0f) {
				a_b.refTimer = 0.5f;
				const RE::NiPoint3 c = BoatBow::HullCentre(vehicle, rot);
				for (auto* h : Refs(a_b)) if (auto r = h->get()) r->SetPosition(c);
			}
			BoatBow::PlaceHullNode(hull->Get3D(), vehicle, rot);
			// 56 (v1.0.56): while the rower's clip plays, the oars hang on its AnimObject bones and our two refs hide; resting or
			// fishing, the refs are back (the swap happens once the clip has stopped, with the rower seated).
			bool baked = false;
			const bool refMode = BoatBow::LakeOarRefMode();
			if (refMode) {
				// 56.20: ref mode: the refs row their own stroke first, then the baked pose (once the clip poses the rower's
				// bones) is written onto them; they are never hidden (the hung copies did not draw out here).
				RE::NiAVObject* refs[2]{};
				for (int i = 0; i < 2; ++i) {
					auto o = a_b.oar[i].get();
					refs[i] = o ? o->Get3D() : nullptr;
					if (!refs[i]) continue;
					refs[i]->SetAppCulled(false);
					BoatBow::StripNodePhysics(refs[i]);
					BoatBow::PlaceOarNode(refs[i], i, 1.0f, a_b.phase, a_b.amp, vehicle, rot);
				}
				for (auto& c : a_b.crew) {
					if (!c.rower) continue;
					if (auto actor = c.actor.get())
						BoatBow::TendBakedOars(actor.get(), c.rowing && !actor->IsDead(), a_dt, vehicle, rot, 1.0f, a_b.phase, a_b.amp, "rowing -> resting swap", refs);
				}
			} else {
				for (auto& c : a_b.crew) {
					if (!c.rower) continue;
					if (auto actor = c.actor.get())
						baked = BoatBow::TendBakedOars(actor.get(), c.rowing && !actor->IsDead(), a_dt, vehicle, rot, 1.0f, a_b.phase, a_b.amp, "rowing -> resting swap");
				}
			}
			for (int i = 0; i < 2 && !refMode; ++i) {
				if (auto o = a_b.oar[i].get(); o && o->Get3D()) {
					o->Get3D()->SetAppCulled(baked);
					if (baked) continue;
					BoatBow::StripNodePhysics(o->Get3D());
					BoatBow::PlaceOarNode(o->Get3D(), i, 1.0f, a_b.phase, a_b.amp, vehicle, rot);
				}
			}
			const auto& helm = BoatBow::Plank();
			const auto& rowB = BoatBow::RowBench();
			if (auto bn = a_b.bench[0].get(); bn && bn->Get3D()) BoatBow::PlaceRigidNode(bn->Get3D(), helm.offset, helm.yaw, helm.scale, vehicle, rot);
			if (auto bn = a_b.bench[1].get(); bn && bn->Get3D()) BoatBow::PlaceRigidNode(bn->Get3D(), rowB.offset, rowB.yaw, rowB.scale, vehicle, rot);

			const bool night = Night();
			if (night && !a_b.lantern) {
				PlaceAt(hull.get(), BoatBow::LanternBase(), "lantern", a_b.lantern);
				PlaceAt(hull.get(), BoatBow::LanternLightBase(), "light", a_b.light);
			} else if (!night && a_b.lantern) {
				DeleteRef(a_b.lantern);
				DeleteRef(a_b.light);
			}
			if (auto l = a_b.lantern.get(); l && l->Get3D()) {
				const RE::NiPoint3 at = BoatBow::PlaceRigidNode(l->Get3D(), BoatBow::SternLampLocal(), 0.0f, BoatBow::LanternScale(), vehicle, rot);
				if (auto li = a_b.light.get()) {
					li->SetPosition(at);
					if (li->Get3D()) BoatBow::PlaceRigidNode(li->Get3D(), BoatBow::SternLampLocal(), 0.0f, 1.0f, vehicle, rot);
				}
			}
			for (auto& c : a_b.crew) TendCrew(a_b, c, vehicle, rot, a_dt, c.rower && rowing);
		}

		// The population of a lake: made once it has route points, kept a game day (re-made only while none is drawn),
		// resized at once when the count setting changes.
		// 54: how many boats a lake wants: one per kPtsPerBoat route points, up to the setting; none on a river strip or pond.
		int LakeWant(const Lake& a_lake)
		{
			const int pts = static_cast<int>(a_lake.pts.size());
			if (pts < kMinLakePts || g_count <= 0) return 0;
			return std::clamp(pts / kPtsPerBoat, 1, g_count);
		}

		// 54: the sea is not one population (it is far too big): a few boats near the player. Near a harbour 3 or 4 (by the
		// day), on the open sea 0 or 1 (by the day and the stretch of sea), new ones slowly and out of sight; a boat left far
		// behind (not drawn) has rowed off and is forgotten.
		void TendSea(Lake& a_lake, float a_dt)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			const RE::NiPoint3 pp = player->GetPosition();
			const auto before = a_lake.boats.size();
			std::erase_if(a_lake.boats, [&](const Boat& b) { return !b.Live() && Dist2(pp.x, pp.y, Pt{ b.x, b.y }) > kSeaForget * kSeaForget; });
			if (a_lake.boats.size() != before) SKSE::log::info("Lake: {} sea boat(s) rowed off (left behind), {} left", before - a_lake.boats.size(), a_lake.boats.size());
			if (!a_lake.fresh || g_count <= 0) return;
			if ((a_lake.fillTimer -= a_dt) > 0.0f) return;
			a_lake.fillTimer = 5.0f;
			const auto day = static_cast<std::int64_t>(Today());
			Pt centre{ pp.x, pp.y };
			float radius = kSeaArea;
			int want = 0;
			const char* where = "open sea";
			if (const Harbour* h = HarbourNear(pp.x, pp.y, a_lake.world)) {
				centre = { h->x, h->y };
				radius = kHarbourRadius;
				want = (std::min)(g_count, 3 + static_cast<int>(Roll(static_cast<std::int64_t>(h->x), static_cast<std::int64_t>(h->y), day) % 2));
				where = h->name.c_str();
			} else {
				const auto zx = static_cast<std::int64_t>(std::floor(pp.x / 30000.0f)), zy = static_cast<std::int64_t>(std::floor(pp.y / 30000.0f));
				want = (std::min)(g_count, static_cast<int>(Roll(zx, zy, day) % 2));
			}
			const int have = static_cast<int>(std::count_if(a_lake.boats.begin(), a_lake.boats.end(), [&](const Boat& b) { return Dist2(centre.x, centre.y, Pt{ b.x, b.y }) < radius * radius; }));
			if (have >= want) return;
			if (NewBoat(a_lake, &centre, radius)) {
				a_lake.fillTimer = Rand(60.0f, 150.0f);  // the next one a while later, so the sea never fills up at once
				SKSE::log::info("Lake: sea boat added near {} ({} of {} wanted there)", where, have + 1, want);
			}
		}

		void TendPopulation(Lake& a_lake, float a_dt)
		{
			if (a_lake.pts.size() < 3) return;
			if (IsSea(a_lake)) { TendSea(a_lake, a_dt); return; }
			if (a_lake.born < 0.0f && !a_lake.fresh) return;  // 53: wait for the full-range sample, or the boats bunch where the old points are
			const int want = LakeWant(a_lake);
			const float today = Today();
			const bool anyLive = std::any_of(a_lake.boats.begin(), a_lake.boats.end(), [](const Boat& b) { return b.Live(); });
			if (a_lake.born < 0.0f || (today - a_lake.born >= 1.0f && !anyLive)) {
				a_lake.boats.clear();
				a_lake.born = today;
				while (static_cast<int>(a_lake.boats.size()) < want && NewBoat(a_lake)) {}
				SKSE::log::info("Lake: '{}' population made: {} of {} boat(s) for the day ({} route points)", a_lake.name, a_lake.boats.size(), want, a_lake.pts.size());
				return;
			}
			// 54: the rest come one at a time, every few seconds, wherever the player cannot see them appear.
			int added = 0;
			if (static_cast<int>(a_lake.boats.size()) < want && (a_lake.fillTimer -= a_dt) <= 0.0f) {
				a_lake.fillTimer = 3.0f;
				if (NewBoat(a_lake)) ++added;
			}
			while (static_cast<int>(a_lake.boats.size()) > want) {
				// Fewer wanted: a virtual one goes first (a drawn one only if all are drawn).
				auto victim = std::find_if(a_lake.boats.begin(), a_lake.boats.end(), [](const Boat& b) { return !b.Live(); });
				if (victim == a_lake.boats.end()) { victim = a_lake.boats.end() - 1; Hide(*victim, "fewer boats"); }
				a_lake.boats.erase(victim);
			}
			if (added) SKSE::log::info("Lake: '{}' now {} boat(s)", a_lake.name, a_lake.boats.size());
		}

		// Which boats are drawn: those in a loaded cell within kLiveRange, nearest first, up to the in-view cap (all lakes together).
		void TendDrawn()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			const RE::NiPoint3 pp = player->GetPosition();
			const RE::FormID world = PlayerWorld();
			auto* tes = RE::TES::GetSingleton();
			struct Cand { Boat* b; Lake* l; float d2; };
			std::vector<Cand> live, wait;
			for (auto& l : g_lakes) {
				for (auto& b : l.boats) {
					const float d2 = Dist2(pp.x, pp.y, Pt{ b.x, b.y });
					if (b.Live()) {
						auto h = b.hull.get();
						auto* cell = h ? h->GetParentCell() : nullptr;
						auto* here = tes ? tes->GetCell(RE::NiPoint3{ b.x, b.y, l.water }) : nullptr;
						if (l.world != world || d2 > kDropRange * kDropRange || !cell || !cell->IsAttached() || !here || !here->IsAttached()) { Hide(b, "out of the loaded area"); continue; }
						live.push_back({ &b, &l, d2 });
					} else if (l.world == world && d2 < kLiveRange * kLiveRange) {
						wait.push_back({ &b, &l, d2 });
					}
				}
			}
			std::sort(live.begin(), live.end(), [](const Cand& a, const Cand& b) { return a.d2 < b.d2; });
			while (static_cast<int>(live.size()) > g_view) { Hide(*live.back().b, "over the in-view cap"); live.pop_back(); }
			std::sort(wait.begin(), wait.end(), [](const Cand& a, const Cand& b) { return a.d2 < b.d2; });
			for (auto& c : wait) {
				if (static_cast<int>(live.size()) >= g_view) break;
				if (Show(*c.b, *c.l)) live.push_back(c);
			}
		}

		void HideAll(const char* a_why)
		{
			for (auto& l : g_lakes) for (auto& b : l.boats) if (b.Live()) Hide(b, a_why);
		}

		Lake* NearestLake(float a_x, float a_y, float a_range)
		{
			const RE::FormID world = PlayerWorld();
			Lake* best = nullptr;
			float bestD = a_range * a_range;
			for (auto& l : g_lakes) {
				if (l.world != world) continue;
				for (const auto& p : l.pts) {
					const float d2 = Dist2(a_x, a_y, p);
					if (d2 < bestD) { bestD = d2; best = &l; }
				}
			}
			return best;
		}

		// Sample the water around the player now and then (a slice per frame): new lakes are found, known ones grow.
		void TendSampling(const RE::NiPoint3& a_pp, bool a_mayStart)
		{
			if (!g_job.active) {
				if (!a_mayStart) return;
				if (std::any_of(g_sampledAt.begin(), g_sampledAt.end(), [&](const Pt& p) { return Dist2(a_pp.x, a_pp.y, p) < kResampleMove * kResampleMove; })) return;
				g_sampledAt.push_back({ a_pp.x, a_pp.y });
				if (g_sampledAt.size() > 400) g_sampledAt.erase(g_sampledAt.begin());
				float level = 0.0f;
				if (!FindLevel(a_pp, level)) return;
				g_job = SampleJob{ true, Pt{ a_pp.x, a_pp.y }, level, static_cast<int>(kSampleRadius / kSampleStep), 0, {} };
				return;
			}
			const int side = 2 * g_job.n + 1;
			for (int k = 0; k < kSamplePerFrame && g_job.idx < side * side; ++k, ++g_job.idx) {
				const int i = g_job.idx / side - g_job.n, j = g_job.idx % side - g_job.n;
				SamplePoint(g_job.centre.x + static_cast<float>(i) * kSampleStep, g_job.centre.y + static_cast<float>(j) * kSampleStep, g_job.level, g_job.pts);
			}
			if (g_job.idx < side * side) return;
			g_job.active = false;
			std::vector<Pt> pts = std::move(g_job.pts);
			const float level = g_job.level;
			if (pts.empty()) return;
			if (Lake* l = LakeFor(pts, level)) {
				l->fresh = true;
				const int added = MergePoints(*l, pts);
				if (added) {
					SKSE::log::info("Lake: '{}' grew by {} route points (now {})", l->name, added, l->pts.size());
					if (l->saved) g_saveTimer = 30.0f;
				}
				return;
			}
			if (static_cast<int>(pts.size()) < kMinAutoPoints) return;
			auto* cell = RE::PlayerCharacter::GetSingleton()->GetParentCell();
			Lake l;
			l.name = std::format("auto-{:08X}", cell ? cell->GetFormID() : 0u);
			l.world = PlayerWorld();
			l.water = level;
			l.pts = std::move(pts);
			l.fresh = true;
			SKSE::log::info("Lake: found {} ({} route points, water {:.0f})", l.name, l.pts.size(), l.water);
			g_lakes.push_back(std::move(l));
		}

		std::string Arg(const std::string& a_lower, std::size_t a_skip)
		{
			std::string rest = a_lower.size() > a_skip ? a_lower.substr(a_skip) : std::string{};
			rest.erase(0, rest.find_first_not_of(' '));
			std::erase(rest, '"');
			std::replace(rest.begin(), rest.end(), ' ', '_');
			return rest;
		}
	}

	void OnGameLoading()
	{
		BoatBow::ForgetBakedOars();  // 56
		for (auto& l : g_lakes) {
			for (auto& b : l.boats) {
				for (auto& c : b.crew) if (auto a = c.actor.get()) BoatBow::RemoveLakeRef(a->GetFormID());
				for (auto* h : Refs(b)) if (auto r = h->get()) BoatBow::RemoveLakeRef(r->GetFormID());
			}
		}
		BoatBow::ForgetPendingDeletes();
		// The refs went with the old game (the co-save purge removes saved ones). Lakes: the file's, until the co-save adds its own.
		g_lakes = g_fileLakes;
		g_sampledAt.clear();
	}

	std::string SaveState()
	{
		std::ostringstream out;
		out << "V 55\n";  // 55: blocked spots made before (other lake boats taken for obstacles) are dropped on load  // populations made by 53+ (spread out); older ones are re-made on load; 54: the sea's few boats
		for (const auto& l : g_lakes) {
			out << std::format("L {} {:08X} {:.1f} {} {:.4f}\n", l.name, l.world, l.water, l.saved ? 1 : 0, l.born);
			for (const auto& p : l.pts) out << std::format("P {:.0f} {:.0f}\n", p.x, p.y);
			for (const auto& a : l.avoid) out << std::format("A {:.0f} {:.0f} {:.0f}\n", a.x, a.y, a.r);
			for (const auto& b : l.boats) {
				out << std::format("B {:.0f} {:.0f} {:.3f} {} {:.0f} {:.0f} {:.1f} {}", b.x, b.y, b.yaw, b.mode == Mode::kFish ? 1 : 0, b.target.x, b.target.y, b.modeTimer, b.people.size());
				for (auto id : b.people) out << std::format(" {:08X}", id);
				out << "\n";
			}
		}
		return out.str();
	}

	void LoadState(const std::string& a_text, const std::function<RE::FormID(RE::FormID)>& a_resolve)
	{
		if (!g_loaded) LoadLakes();
		std::istringstream in(a_text);
		std::string line;
		Lake* cur = nullptr;
		int lakes = 0, boats = 0;
		bool spread = false, rolling = false, freshAvoid = false;
		int dropped = 0;
		while (std::getline(in, line)) {
			std::istringstream ls(line);
			std::string tag;
			if (!(ls >> tag)) continue;
			if (tag == "V") { int v = 0; ls >> v; spread = v >= 53; rolling = v >= 54; freshAvoid = v >= 55; continue; }
			if (tag == "L") {
				Lake l;
				std::string world;
				int saved = 0;
				if (!(ls >> l.name >> world >> l.water >> saved >> l.born)) { cur = nullptr; continue; }
				l.world = static_cast<RE::FormID>(std::stoul(world, nullptr, 16));
				l.saved = saved != 0;
				auto it = std::find_if(g_lakes.begin(), g_lakes.end(), [&](const Lake& o) { return o.name == l.name; });
				if (it != g_lakes.end()) {
					l.saved = l.saved || it->saved;
					*it = std::move(l);
					cur = &*it;
				} else {
					g_lakes.push_back(std::move(l));
					cur = &g_lakes.back();
				}
				++lakes;
			} else if (!cur) {
				continue;
			} else if (tag == "P") {
				Pt p{};
				if (ls >> p.x >> p.y) cur->pts.push_back(p);
			} else if (tag == "A") {
				Avoid a{};
				if (!(ls >> a.x >> a.y >> a.r)) continue;
				if (!freshAvoid) { ++dropped; continue; }  // 55.3: made before 55 (other lake boats taken for obstacles)
				cur->avoid.push_back(a);
			} else if (tag == "B") {
				if (!spread) { cur->born = -1.0f; continue; }  // a v1.0.51 population (bunched): made again, spread out
				if (!rolling && IsSea(*cur)) continue;  // 54: a whole-sea population from 53: the sea starts again with its few boats
				Boat b;
				int mode = 0;
				std::size_t n = 0;
				if (!(ls >> b.x >> b.y >> b.yaw >> mode >> b.target.x >> b.target.y >> b.modeTimer >> n)) continue;
				b.mode = mode == 1 ? Mode::kFish : Mode::kRow;
				for (std::size_t k = 0; k < n && k < 4; ++k) {
					std::string id;
					if (!(ls >> id)) break;
					const RE::FormID resolved = a_resolve(static_cast<RE::FormID>(std::stoul(id, nullptr, 16)));
					if (resolved && RE::TESForm::LookupByID<RE::TESNPC>(resolved)) b.people.push_back(resolved);
				}
				if (b.people.empty()) continue;
				cur->boats.push_back(std::move(b));
				++boats;
			}
		}
		SKSE::log::info("Lake: {} lake(s), {} boat(s) from the save", lakes, boats);
		if (dropped > 0) SKSE::log::info("Lake: cleared {} old blocked spots (co-save)", dropped);
	}

	void Update(float a_dt)
	{
		if (!g_loaded) LoadLakes();
		a_dt = (std::min)(a_dt, 0.1f);
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!player || !cell || !cell->IsAttached() || !player->Get3D()) return;
		const RE::NiPoint3 pp = player->GetPosition();
		if (g_saveTimer > 0.0f && (g_saveTimer -= a_dt) <= 0.0f) SaveLakes();
		if (g_rigSaveTimer > 0.0f && (g_rigSaveTimer -= a_dt) <= 0.0f) SaveRig();
		if (!g_on || !cell->IsExteriorCell()) {
			HideAll(g_on ? "indoors" : "lake boats off");
			return;
		}
		{
			const bool mayStart = (g_sampleTimer -= a_dt) <= 0.0f && !player->IsInCombat();
			if (mayStart) g_sampleTimer = 4.0f;
			TendSampling(pp, mayStart);
		}
		const RE::FormID world = PlayerWorld();
		for (auto& l : g_lakes) {
			if (l.world != world) continue;
			const bool close = std::any_of(l.pts.begin(), l.pts.end(), [&](const Pt& p) { return Dist2(pp.x, pp.y, p) < kSimRange * kSimRange; });
			if (!close) continue;
			TendPopulation(l, a_dt);
			for (auto& b : l.boats) TendBoat(b, l, a_dt);
		}
		if ((g_checkTimer -= a_dt) <= 0.0f) {
			g_checkTimer = 0.5f;
			TendDrawn();
		}
	}

	std::string Command(const std::string& a_lower)
	{
		if (!g_loaded) LoadLakes();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (a_lower.rfind("lakehere", 0) == 0) {
			const std::string name = Arg(a_lower, 8);
			if (name.empty()) return "usage: oas \"lakehere <name>\" while standing at the lake";
			std::vector<Pt> pts;
			float level = 0.0f;
			std::string why;
			if (!SampleHere(pts, level, why)) return std::format("no lake here ({})", why);
			Lake* l = LakeFor(pts, level);
			if (!l) {
				Lake n;
				n.world = PlayerWorld();
				n.water = level;
				g_lakes.push_back(std::move(n));
				l = &g_lakes.back();
			}
			const std::string old = l->name;
			l->name = name;  // an auto lake is renamed and kept (its boats too)
			l->saved = true;
			l->fresh = true;
			MergePoints(*l, pts);
			SaveLakes();
			SKSE::log::info("Lake: lakehere '{}' (was '{}') water {:.0f}, {} points", l->name, old, l->water, l->pts.size());
			return std::format("lake '{}' saved: {} route points, water {:.0f}", l->name, l->pts.size(), l->water);
		}
		if (a_lower.rfind("lakepoint", 0) == 0) {
			const std::string name = Arg(a_lower, 9);
			if (name.empty() || !player) return "usage: oas \"lakepoint <name>\" at a spot on the water";
			const RE::NiPoint3 pp = player->GetPosition();
			float w = 0.0f, d = 0.0f;
			if (WaterState(pp.x, pp.y, -1.0e9f, 1.0f, &w, &d) != WS::kOpen) return "no water under you here";
			auto it = std::find_if(g_lakes.begin(), g_lakes.end(), [&](const Lake& o) { return o.name == name; });
			if (it == g_lakes.end()) {
				Lake n;
				n.name = name;
				n.world = PlayerWorld();
				n.water = w;
				n.saved = true;
				g_lakes.push_back(std::move(n));
				it = g_lakes.end() - 1;
			}
			it->pts.push_back({ pp.x, pp.y });
			SaveLakes();
			SKSE::log::info("Lake: lakepoint '{}' ({:.0f},{:.0f}) depth {:.0f}", name, pp.x, pp.y, d);
			return std::format("lake '{}': point {} added (depth {:.0f})", name, it->pts.size(), d);
		}
		if (a_lower.rfind("lakedel", 0) == 0) {
			const std::string name = Arg(a_lower, 7);
			auto it = std::find_if(g_lakes.begin(), g_lakes.end(), [&](const Lake& o) { return o.name == name; });
			if (it == g_lakes.end()) return std::format("no lake '{}'", name);
			for (auto& b : it->boats) if (b.Live()) Hide(b, "lake deleted");
			g_lakes.erase(it);
			SaveLakes();
			return std::format("lake '{}' deleted", name);
		}
		if (a_lower == "lakes") {
			int drawn = 0;
			std::string list;
			for (const auto& l : g_lakes) {
				const int live = static_cast<int>(std::count_if(l.boats.begin(), l.boats.end(), [](const Boat& b) { return b.Live(); }));
				drawn += live;
				list += std::format(" | {}{}{}: {} pts, {} boats{} ({} drawn), {} blocked", l.name, l.saved ? "" : " [auto]", l.sea == 1 ? " [sea]" : "", l.pts.size(), l.boats.size(),
					l.sea == 1 ? std::string(" near you") : std::format(" of {} wanted", LakeWant(l)), live, l.avoid.size());
			}
			const std::string text = std::format("lake boats {}, up to {} per lake (by its size), up to {} drawn ({} now){}", g_on ? "on" : "off", g_count, g_view, drawn, list);
			SKSE::log::info("Lake: {}", text);
			return text;
		}
		if (a_lower.rfind("lakeboats", 0) == 0) {
			std::string v = Arg(a_lower, 9);
			if (v == "off") { g_on = false; HideAll("lake boats off"); }
			else if (v == "on") g_on = true;
			else if (v.rfind("view", 0) == 0) {
				std::string n = v.substr(4);
				std::erase(n, '_');
				if (n.empty() || !std::all_of(n.begin(), n.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) return "usage: oas \"lakeboats view <1-10>\"";
				g_view = std::clamp(std::stoi(n), 1, 10);
				g_checkTimer = 0.0f;
			} else if (v == "now") {
				Lake* l = player ? NearestLake(player->GetPosition().x, player->GetPosition().y, kSimRange) : nullptr;
				if (!l) return "not near a known lake (stand at open water a few seconds, or oas \"lakehere <name>\")";
				g_count = (std::min)(10, (std::max)(g_count, static_cast<int>(l->boats.size()) + 1));
				g_checkTimer = 0.0f;
			} else if (!v.empty() && std::all_of(v.begin(), v.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
				g_count = std::clamp(std::stoi(v), 0, 10);
				g_checkTimer = 0.0f;
			} else return "usage: oas \"lakeboats on|off|now|<0-10>|view <1-10>\"";
			SaveSettings();
			SKSE::log::info("Lake: boats {}, {} per lake, up to {} drawn", g_on ? "on" : "off", g_count, g_view);
			return std::format("lake boats {}, up to {} per lake (by its size; the sea 3-4 at harbours), up to {} drawn at once", g_on ? "on" : "off", g_count, g_view);
		}
		return {};
	}

	bool AnyDrawn()
	{
		for (const auto& l : g_lakes) for (const auto& b : l.boats) if (b.Live()) return true;
		return false;
	}

	bool IsRigKey(std::uint32_t a_key)
	{
		switch (a_key) {
		case 0x47: case 0x48: case 0x49: case 0x4B: case 0x4C: case 0x4D: case 0x4F: case 0x50: case 0x51: case 0x52: return true;
		default: return false;
		}
	}

	void TuneRig(std::uint32_t a_key, bool a_title)
	{
		if (!g_loaded) LoadLakes();
		if (a_title) RE::SendHUDMessage::ShowHUDMessage("NPC BOATS (all of them, hull + oars + benches + lantern + crew): 8/2 forward-back, 4/6 left-right, 9/3 up-down, 0 reset");
		bool moved = true;
		switch (a_key) {
		case 0x48: g_rig.y += 1.0f; break;  // Num8 / Num2 forward / back
		case 0x50: g_rig.y -= 1.0f; break;
		case 0x4B: g_rig.x -= 1.0f; break;  // Num4 / Num6 left / right
		case 0x4D: g_rig.x += 1.0f; break;
		case 0x49: g_rig.z += 1.0f; break;  // Num9 / Num3 up / down
		case 0x51: g_rig.z -= 1.0f; break;
		case 0x52: g_rig = { 0.0f, 0.0f, 11.0f }; break;  // Num0 back to the default
		default: moved = false; break;
		}
		if (moved) g_rigSaveTimer = 1.0f;  // written to the ini once the keys rest
		static auto lastHud = std::chrono::steady_clock::time_point{};
		const auto now = std::chrono::steady_clock::now();
		if (a_title || (moved && now - lastHud > std::chrono::milliseconds(900))) {  // held keys don't flood the notifications
			lastHud = now;
			int drawn = 0;
			for (const auto& l : g_lakes) for (const auto& b : l.boats) if (b.Live()) ++drawn;
			RE::SendHUDMessage::ShowHUDMessage(std::format("NPC boats: right {:.0f}  forward {:.0f}  up {:.0f}  ({} drawn)", g_rig.x, g_rig.y, g_rig.z, drawn).c_str());
		}
	}
}
