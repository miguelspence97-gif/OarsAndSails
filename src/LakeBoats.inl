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

// Included by Plugin.cpp inside its anonymous namespace, after Oar and Paint (one translation unit, no new source in
// CMakeLists). v1.3.0: the OAS lake boats (Part 51-56, LakeBoats.cpp) on the RowRim boat model, and the OAS oar ripples
// and dig sound (BoatBow 56.39 / 56.67) for every rowing boat. See src\Port7_LakeBoats.md.

// ------------------------------------------------------------------
// Oar blades in the water: ripples and the dig sound (OAS BoatBow 56.39 / 56.67)
// ------------------------------------------------------------------
namespace Fx
{
	struct RippleTune
	{
		float oar = 0.05f;    // ripple size where a blade enters / drags through / leaves the water (OAS default)
		float space = 12.0f;  // the blade's glide trail: a smaller ripple every this many units it moves in the water
		float lift = 0.0f;    // height above (+) or below (-) the water surface
	};
	inline RippleTune g_ripple;
	inline bool g_on = true;
	inline int g_logs = 0;

	inline std::string IniPath()
	{
		std::error_code ec;
		return (std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "RowRim.ini").string();
	}

	inline void Ripple(const RE::NiPoint3& a_pos, float a_scale)
	{
		if (!g_on) return;
		auto* water = RE::TESWaterSystem::GetSingleton();
		if (!water) return;
		water->AddRipple(a_pos, a_scale);
		if (g_logs < 6) {
			++g_logs;
			logger::info("Ripple: ({:.0f},{:.0f},{:.0f}) scale={:.2f}", a_pos.x, a_pos.y, a_pos.z, a_scale);
		}
	}

	// OAS 56.67: the catch makes the game's own feet-in-water sound (the walk footsteps' impact for the water material, the
	// one most footstep sets share). RowRim.ini [Sound] OarDigSound=<form ID, hex> picks another (0 = off); OarDigVolume.
	inline RE::BGSSoundDescriptorForm* g_dig = nullptr;
	inline bool g_digPicked = false;
	inline float g_digVolume = 0.8f;

	inline void PickDig()
	{
		g_digPicked = true;
		const auto ini = IniPath();
		char buf[32]{};
		GetPrivateProfileStringA("Sound", "OarDigSound", "", buf, sizeof(buf), ini.c_str());
		GetPrivateProfileStringA("Sound", "OarDigVolume", "0.8", buf + 16, 16, ini.c_str());
		try { g_digVolume = std::clamp(std::stof(buf + 16), 0.0f, 1.0f); } catch (...) { g_digVolume = 0.8f; }
		if (buf[0]) {
			const auto id = static_cast<RE::FormID>(std::strtoul(buf, nullptr, 16));
			g_dig = id ? RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(id) : nullptr;
			logger::info("Sound: oar dig {} (ini {})", g_dig ? std::format("{:08X}", g_dig->GetFormID()) : std::string("off"), buf);
			if (id == 0 || g_dig) return;
		}
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) return;
		std::vector<std::pair<RE::BGSSoundDescriptorForm*, int>> votes;
		for (auto* fs : dh->GetFormArray<RE::BGSFootstep>()) {
			if (!fs || !fs->impactSet) continue;
			const std::string_view tag = fs->tag.c_str() ? fs->tag.c_str() : "";
			if (tag != "FootLeft" && tag != "FootRight") continue;
			for (auto& [mat, impact] : fs->impactSet->impactMap) {
				if (!mat || !impact || mat->materialID != RE::MATERIAL_ID::kWater || !impact->sound1) continue;
				auto it = std::find_if(votes.begin(), votes.end(), [&](const auto& v) { return v.first == impact->sound1; });
				if (it == votes.end()) votes.push_back({ impact->sound1, 1 });
				else ++it->second;
			}
		}
		std::sort(votes.begin(), votes.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		if (!votes.empty()) g_dig = votes.front().first;
		logger::info("Sound: oar dig {} ({} feet-in-water footstep sound(s) found), volume {:.2f}",
			g_dig ? std::format("{:08X}", g_dig->GetFormID()) : std::string("none found"), votes.size(), g_digVolume);
	}

	inline void PlayDig(const RE::NiPoint3& a_at)
	{
		if (!g_digPicked) PickDig();
		if (!g_dig || g_digVolume <= 0.0f) return;
		auto* audio = RE::BSAudioManager::GetSingleton();
		if (!audio) return;
		RE::BSSoundHandle h;
		if (!audio->GetSoundHandle(h, g_dig) || !h.IsValid()) return;
		h.SetPosition(a_at);
		h.SetVolume(g_digVolume);
		h.Play();
	}

	// One oar's blade against the water (OAS TendOar, ref mode): a ripple where it goes in (with the dig sound) and where it
	// comes out, and while it moves in the water a smaller ripple every g_ripple.space units (every 0.2 s while rowing).
	struct Blade
	{
		bool sub = false;
		bool trail = false;
		RE::NiPoint3 last{};
		RE::NiPoint3 prev{};  // v1.7.58: last frame's blade tip (the swoosh)
		float t = 0.0f;
		float cooldown = 0.0f;
	};

	// v1.5.0: a_glow is the player's oar side (0 right, 1 left) for the plankton, -1 for none (v1.5.11: 10/11 a lake boat's oar side)
	inline void TendBlade(Blade& a_b, const RE::NiAVObject* a_oar, float a_waterZ, float a_dt, bool a_rowing, int a_glow = -1)
	{
		if (a_b.cooldown > 0.0f) a_b.cooldown -= a_dt;
		if (!a_oar || !Oar::g_b.known) return;
		const RE::NiPoint3 tip = a_oar->world * (Oar::g_b.centre - Oar::Axis() * Oar::g_b.halfLen);
		const float surface = a_waterZ + g_ripple.lift;
		const bool sub = tip.z <= surface;
		if (sub != a_b.sub) {
			RE::NiPoint3 p = tip;
			p.z = surface;
			Ripple(p, g_ripple.oar);
			if (sub && a_b.cooldown <= 0.0f) {  // the catch
				PlayDig(p);
				if (a_glow >= 0) Plankton::Dig(p, a_waterZ, a_glow % 10, a_glow >= 10);  // v1.5.0 (v1.5.11: lake boats too)
				a_b.cooldown = 0.4f;
			}
			a_b.sub = sub;
		}
		if (!sub) { a_b.trail = false; return; }
		if (!a_b.trail) {
			a_b.trail = true;
			a_b.last = tip;
			a_b.prev = tip;
			a_b.t = 0.0f;
			return;
		}
		a_b.t += a_dt;
		// v1.7.65: the v1.7.58-60 swooshes (sprite streak and ribbon) are off: Plankton::Swoosh / Ribbon kept, not called
		a_b.prev = tip;
		const float dx = tip.x - a_b.last.x, dy = tip.y - a_b.last.y, d2 = dx * dx + dy * dy;
		const float sp = (std::max)(8.0f, g_ripple.space);
		if (d2 > 600.0f * 600.0f) {  // a cell hop or a new model: start over
			a_b.last = tip;
			a_b.t = 0.0f;
		} else if (d2 >= sp * sp || (a_rowing && a_b.t >= 0.2f)) {
			RE::NiPoint3 p = tip;
			p.z = surface;
			Ripple(p, g_ripple.oar * 0.7f);
			if (a_glow >= 0) Plankton::Trail(tip, a_waterZ);  // v1.5.0: the blade dragging through
			a_b.last = tip;
			a_b.t = 0.0f;
		}
	}
}

// ------------------------------------------------------------------
// The lake boats (OAS Part 51-56: LakeBoats.cpp, same behaviour). Unnamed townsfolk row around a lake, stop to fish, row
// on; a lantern at night. Each lake keeps its boats for at least a game day; only those in the loaded area are drawn.
// RowRim: the boat is our model (RowRim_Hull.nif on a hull ref of its own, no collision), painted at random, the oars
// are two ShipOar01 clones on it that rest in the oarlocks and follow the rower's clip like the player's.
// ------------------------------------------------------------------
namespace LakeBoats
{
	namespace
	{
		// ---- tunables (OAS values) --------------------------------------------------------------------------------------
		constexpr const char* kLakeFile = "Data/SKSE/Plugins/RowRim_Lakes.txt";
		constexpr float kSampleRadius = 9000.0f;
		constexpr float kSampleStep = 400.0f;
		constexpr int kSamplePerFrame = 450;
		constexpr float kResampleMove = 2500.0f;
		constexpr float kMinDepth = 120.0f;
		constexpr float kClearance = 300.0f;
		constexpr float kLevelTolerance = 30.0f;
		constexpr float kPointSpacing = 500.0f;
		constexpr int kMinAutoPoints = 25;
		constexpr float kSimRange = 30000.0f;
		constexpr float kLiveRange = 10000.0f;
		constexpr float kDropRange = 12000.0f;
		constexpr float kNoSpawnNear = 7000.0f;
		constexpr float kViewRange = 12000.0f;
		constexpr float kViewCos = 0.57f;
		constexpr int kPtsPerBoat = 50;
		constexpr int kMinLakePts = 40;
		constexpr float kSeaArea = 20000.0f;
		constexpr float kSeaForget = 26000.0f;
		constexpr float kHarbourRadius = 22000.0f;
		constexpr float kKeepClear = 900.0f;
		constexpr float kRowSpeed = 95.0f;
		constexpr float kFleeSpeed = 210.0f;
		constexpr float kTurnRate = 0.35f;
		constexpr float kRowMin = 2500.0f;
		constexpr float kRowMax = 9000.0f;
		constexpr float kGiveWay = 450.0f;
		constexpr float kFxRange = 4000.0f;  // ripples and the dig sound only this close to the player

		// ---- the RowRim boat (model space: +Y the bow, origin mid-boat) ------------------------------------------------
		constexpr RE::FormID kHullStatic = 0x00050368;  // ShipRowBoat01: the hull ref our model hangs on
		constexpr RE::FormID kLampStatic = 0x0002D847;  // CandleLanternwithCandle01 (OAS stern lamp)
		constexpr RE::FormID kLampLight = 0x0001CBB3;   // OAS lamp light (warm, radius 300)
		constexpr const char* kHullModel = "RowRim\\RowRim_Hull.nif";
		constexpr float kOriginBelowWater = 32.0f;      // the player's boat: kWaterZ -32
		const RE::NiPoint3 kRowerSeat{ 0.0f, 15.0f, 33.0f };   // v1.4.0: the oar thwart where the clip sits (TestHull rowSeat_), facing the stern
		constexpr float kRowShift = 0.0f;                      // (TestHull rowShift_)
		const RE::NiPoint3 kSternSeat{ 0.0f, -215.0f, 35.0f }; // the stern plank by the tiller (TestHull seat_), facing the bow
		const RE::NiPoint3 kLock{ 93.3f, 6.7f, 76.5f };         // the middle oarlocks (TestHull lockLocal_)
		// v1.4.2: the lantern hook, the centre of the stern spiral in the RowRim hull (measured from the mesh: spiral at
		// y -339, z 141, radius 10). It is the OAS player boat hook (0,-91,28 from its vehicle point, hull at 0,250,-112) in
		// this hull's frame. The lantern hangs from the top of its model there and swings like the OAS one.
		const RE::NiPoint3 kLampHook{ 0.0f, -341.0f, 140.0f };
		constexpr float kRestIn = 0.30f, kRestDip = 12.0f;      // resting oars (TestHull restIn_ / restDip_)
		constexpr float kOarRoll = 90.0f * kPi / 180.0f;
		constexpr float kOarBlendTime = 0.8f;
		constexpr int kPaints[] = { Paint::kVanilla, Paint::kBeige, Paint::kBlue, Paint::kBlack, Paint::kRed, Paint::kWhite, Paint::kBlueDesign };

		std::mt19937 g_rng{ std::random_device{}() };
		float Rand(float a_lo, float a_hi) { return std::uniform_real_distribution<float>(a_lo, a_hi)(g_rng); }
		template <class T> const T& Pick(const std::vector<T>& a_v) { return a_v[std::uniform_int_distribution<std::size_t>(0, a_v.size() - 1)(g_rng)]; }
		int RandomPaint() { return kPaints[std::uniform_int_distribution<int>(0, static_cast<int>(std::size(kPaints)) - 1)(g_rng)]; }

		// ---- spawned refs: kept in the co-save, removed after the next load (OAS TrackSpawn) ----------------------------
		std::vector<std::pair<RE::FormID, RE::FormID>> g_spawned;  // v1.6.4: (ref, its base)
		std::vector<std::pair<RE::FormID, RE::FormID>> g_purge;    // (ref, base; base 0 from an older save)
		struct PendingDelete { RE::ObjectRefHandle h; float t; };
		std::vector<PendingDelete> g_pendingDeletes;
		void AddSpawn(const RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) return;
			auto* base = a_ref->GetBaseObject();
			g_spawned.emplace_back(a_ref->GetFormID(), base ? base->GetFormID() : RE::FormID{ 0 });  // v1.6.4: with its base
		}
		void RemoveSpawn(RE::FormID a_id) { std::erase_if(g_spawned, [a_id](const auto& e) { return e.first == a_id; }); }
		void DeleteActorLater(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) return;
			RemoveSpawn(a_ref->GetFormID());
			a_ref->Disable();
			g_pendingDeletes.push_back({ a_ref->GetHandle(), 2.0f });  // OAS 51.1: never deleted in the frame it is disabled
		}

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
			float rowAt = -100.0f;  // boat time of the last accepted row start (the oars wait 0.3 s for the clip)
			float shiftW = 0.0f;    // 0..1 the rowing shift toward the bow
			float sitTimer = 0.0f;
			float idleTimer = 0.0f;
			float retry = 0.0f;
			float dressTimer = 2.0f;
			int gear = -1;
			const void* last3D = nullptr;
			bool closeBy = false;
			float stopHold = 0.0f;  // v1.4.0: how long the boat has not wanted a stroke (the clip stops after 1 s of that)
		};
		struct OarSet
		{
			RE::NiPointer<RE::NiNode> oar[2];
			float w = 0.0f;
			RE::NiTransform clip[2]{};
			bool have[2]{ false, false };
			Fx::Blade blade[2];
		};
		struct Boat
		{
			// kept (co-save)
			float x = 0.0f, y = 0.0f, yaw = 0.0f, speed = 0.0f;
			Mode mode = Mode::kFish;
			Pt target{};
			float modeTimer = 0.0f;
			std::vector<RE::FormID> people;  // NPC bases; [0] rows
			int paint = Paint::kBlue;
			// drawn
			RE::ObjectRefHandle hull, lantern, light;
			std::vector<Crew> crew;
			RE::NiPointer<RE::NiNode> visual;
			const void* visualOn = nullptr;  // the hull 3D the model hangs on (a reload gets a new one)
			OarSet oars;
			float time = 0.0f, refTimer = 0.0f, rayTimer = 0.0f, blockLog = 0.0f;
			float seed = 0.0f;     // v1.3.1: this boat's own phase for the idle rock (was taken from its position: it jumped as it moved)
			float turnVel = 0.0f;  // v1.3.1: the turn rate, eased like the player's (was a hard clamp: jerky)
			float lean = 0.0f;     // v1.3.1: the player's turn lean
			float pushX = 0.0f, pushY = 0.0f;  // v1.7.61: shoved by the player's boat: drifts off, slowing (units/s)
			struct Lamp
			{
				float pitch = 0.0f, roll = 0.0f, pitchVel = 0.0f, rollVel = 0.0f;
				RE::NiPoint3 prevPivot{}, prevVel{};
				bool have = false;
				RE::NiAVObject* flameRoot = nullptr;
				RE::NiPoint3 pos{};
				RE::NiMatrix3 rot{};
				bool placed = false;
			} lamp;  // v1.4.2: OAS pendulum lantern
			RE::NiPoint3 lastAt{};
			RE::NiMatrix3 lastRot{};
			bool havePose = false;
			float lastBlockAt = -100.0f, lastBlockX = 0.0f, lastBlockY = 0.0f;
			float pkWake = 0.0f, pkStern = 0.0f;  // v1.5.11: the plankton wake's spawn counters
			bool logged = false;
			bool crewPlaced = false;
			float hullWait = 0.0f;
			bool Live() const { return static_cast<bool>(hull); }
		};
		struct Lake
		{
			std::string name;
			RE::FormID world = 0;
			float water = 0.0f;
			std::vector<Pt> pts;
			std::vector<Avoid> avoid;
			bool saved = false;
			float born = -1.0f;
			bool fresh = false;
			int sea = -1;
			float fillTimer = 0.0f;
			std::vector<Boat> boats;
		};
		struct Harbour { std::string name; float x, y; };
		std::vector<Lake> g_fileLakes;
		std::vector<Lake> g_lakes;
		std::vector<Pt> g_sampledAt;
		bool g_loaded = false;
		bool g_on = true;
		int g_count = 3;  // v1.7.8: 3 per lake by default (was 6)
		int g_view = 5;
		RE::FormID g_harbourWorld = 0;
		std::vector<Harbour> g_harbours;
		float g_checkTimer = 0.0f, g_sampleTimer = 0.0f, g_saveTimer = -1.0f;

		float Dist2(float a_x, float a_y, const Pt& a_p) { const float dx = a_x - a_p.x, dy = a_y - a_p.y; return dx * dx + dy * dy; }
		float Today() { auto* cal = RE::Calendar::GetSingleton(); return cal ? cal->GetDaysPassed() : 0.0f; }

		void SaveSettings()
		{
			const auto ini = Fx::IniPath();
			WritePrivateProfileStringA("LakeBoats", "On", g_on ? "1" : "0", ini.c_str());
			WritePrivateProfileStringA("LakeBoats", "Count", std::to_string(g_count).c_str(), ini.c_str());
			WritePrivateProfileStringA("LakeBoats", "View", std::to_string(g_view).c_str(), ini.c_str());
		}

		void SaveLakes()
		{
			std::ofstream out(kLakeFile, std::ios::trunc);
			if (!out) { logger::warn("Lake: cannot write {}", kLakeFile); return; }
			out << "# RowRim lake routes (oas lakehere / lakepoint). lake <name> <worldspace> <water z>, then p <x> <y> per point, a <x> <y> <r> per blocked spot.\n";
			out << "v 55\n";
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
			logger::info("Lake: {} written ({} lakes)", kLakeFile, g_fileLakes.size());
		}

		void LoadLakes()
		{
			g_loaded = true;
			const auto ini = Fx::IniPath();
			g_on = GetPrivateProfileIntA("LakeBoats", "On", 1, ini.c_str()) != 0;
			g_count = std::clamp(static_cast<int>(GetPrivateProfileIntA("LakeBoats", "Count", 3, ini.c_str())), 0, 10);
			g_view = std::clamp(static_cast<int>(GetPrivateProfileIntA("LakeBoats", "View", 5, ini.c_str())), 1, 10);
			// v1.7.8: the default is 3 boats a lake; a count saved under the old default (6) is brought down once
			if (GetPrivateProfileIntA("LakeBoats", "Defaults", 0, ini.c_str()) < 2) {
				if (g_count > 3) {
					logger::info("Lake: boats per lake {} -> 3 (the new default, once; oas \"lakeboats N\" to change it)", g_count);
					g_count = 3;
				}
				SaveSettings();
				WritePrivateProfileStringA("LakeBoats", "Defaults", "2", ini.c_str());
			}
			std::ifstream in(kLakeFile);
			if (in) {
				std::string line;
				Lake* cur = nullptr;
				while (std::getline(in, line)) {
					std::istringstream ls(line);
					std::string tag;
					if (!(ls >> tag) || tag[0] == '#' || tag == "v") continue;
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
						if (ls >> a.x >> a.y >> a.r) cur->avoid.push_back(a);
					}
				}
			}
			for (const auto& f : g_fileLakes) {
				if (std::none_of(g_lakes.begin(), g_lakes.end(), [&](const Lake& l) { return l.name == f.name; })) g_lakes.push_back(f);
			}
			logger::info("Lake: settings on={} count={} view={}, {} lake(s) in the lakes file", g_on, g_count, g_view, g_fileLakes.size());
		}

		RE::FormID PlayerWorld()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* ws = player ? player->GetWorldspace() : nullptr;
			return ws ? ws->GetFormID() : 0;
		}

		bool IsSea(Lake& a_lake)
		{
			if (a_lake.sea < 0) {
				auto* ws = RE::TESForm::LookupByID<RE::TESWorldSpace>(a_lake.world);
				a_lake.sea = ws && std::abs(a_lake.water - ws->GetDefaultWaterHeight()) < 50.0f ? 1 : 0;
				if (a_lake.sea) logger::info("Lake: '{}' is the sea (water {:.0f})", a_lake.name, a_lake.water);
			}
			return a_lake.sea == 1;
		}

		const std::vector<Harbour>& Harbours(RE::FormID a_world)
		{
			if (g_harbourWorld == a_world) return g_harbours;
			g_harbourWorld = a_world;
			g_harbours.clear();
			auto* ws = RE::TESForm::LookupByID<RE::TESWorldSpace>(a_world);
			auto* cell = ws ? ws->persistentCell : nullptr;
			if (!cell) return g_harbours;
			static constexpr const char* kNames[] = { "Solitude", "Dawnstar", "Windhelm", "East Empire Company Warehouse" };
			cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
				auto* mm = a_ref ? a_ref->extraList.GetByType<RE::ExtraMapMarker>() : nullptr;
				const char* name = mm && mm->mapData ? mm->mapData->locationName.GetFullName() : nullptr;
				if (name && std::any_of(std::begin(kNames), std::end(kNames), [&](const char* n) { return std::string_view(name) == n; })) {
					const auto p = a_ref->GetPosition();
					g_harbours.push_back({ name, p.x, p.y });
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			logger::info("Lake: {} sea harbour(s)", g_harbours.size());
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

		std::uint32_t Roll(std::int64_t a_a, std::int64_t a_b, std::int64_t a_c)
		{
			std::uint64_t h = 1469598103934665603ull;
			for (auto v : { a_a, a_b, a_c }) { h ^= static_cast<std::uint64_t>(v); h *= 1099511628211ull; }
			return static_cast<std::uint32_t>(h ^ (h >> 29));
		}

		// ---- water --------------------------------------------------------------------------------------------------------
		enum class WS { kUnknown, kBlocked, kOpen };
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

		void SamplePoint(float a_x, float a_y, float a_level, std::vector<Pt>& a_pts)
		{
			if (!OpenWater(a_x, a_y, a_level, kMinDepth)) return;
			for (const auto& o : { Pt{ kClearance, 0.0f }, Pt{ -kClearance, 0.0f }, Pt{ 0.0f, kClearance }, Pt{ 0.0f, -kClearance } }) {
				if (!OpenWater(a_x + o.x, a_y + o.y, a_level, kMinDepth * 0.5f)) return;
			}
			if (std::none_of(a_pts.begin(), a_pts.end(), [&](const Pt& q) { return Dist2(a_x, a_y, q) < kPointSpacing * kPointSpacing; })) a_pts.push_back({ a_x, a_y });
		}

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

		struct SampleJob
		{
			bool active = false;
			Pt centre{};
			float level = 0.0f;
			int n = 0, idx = 0;
			std::vector<Pt> pts;
		};
		SampleJob g_job;

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

		// ---- the NPCs (OAS 56.63: random townsfolk of every race, most in a fishing outfit) -----------------------------
		struct RaceWeight { const char* id; float w; };
		constexpr RaceWeight kRaceWeights[] = { { "NordRace", 45.0f }, { "ImperialRace", 12.0f }, { "BretonRace", 10.0f }, { "RedguardRace", 9.0f },
			{ "DarkElfRace", 7.0f }, { "ArgonianRace", 6.0f }, { "WoodElfRace", 4.0f }, { "HighElfRace", 3.0f }, { "OrcRace", 3.0f }, { "KhajiitRace", 1.0f } };
		constexpr std::size_t kRaceCount = std::size(kRaceWeights);
		constexpr float kFishGearChance = 0.7f;
		std::vector<RE::TESNPC*> g_byRace[kRaceCount];
		std::vector<std::vector<RE::TESObjectARMO*>> g_fishGear;
		std::vector<RE::FormID> g_crewBases;
		bool g_npcsPicked = false;

		std::string Lower(const char* a_s)
		{
			std::string low(a_s ? a_s : "");
			std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return low;
		}
		bool IsRealFisher(const RE::TESNPC* a_npc) { return a_npc && Lower(a_npc->GetName()).find("fisher") != std::string::npos; }

		bool OutfitPieces(const RE::BGSOutfit* a_outfit, std::vector<RE::TESObjectARMO*>& a_out, bool a_casualOnly = true)
		{
			if (!a_outfit || a_outfit->outfitItems.empty()) return false;
			bool casual = true;
			auto take = [&](RE::TESForm* a_form) {
				auto* armo = a_form ? a_form->As<RE::TESObjectARMO>() : nullptr;
				if (!armo) return;
				if (armo->GetArmorType() != RE::BIPED_MODEL::ArmorType::kClothing) casual = false;
				a_out.push_back(armo);
			};
			for (auto* item : a_outfit->outfitItems) {
				if (auto* lvl = item ? item->As<RE::TESLevItem>() : nullptr) {
					for (auto& e : lvl->entries) take(e.form);
				} else take(item);
			}
			return (casual || !a_casualOnly) && !a_out.empty();
		}

		int RaceIndex(const RE::TESNPC* a_npc)
		{
			const char* id = a_npc && a_npc->race ? a_npc->race->GetFormEditorID() : nullptr;
			if (!id) return -1;
			for (std::size_t i = 0; i < kRaceCount; ++i) if (std::string_view(id) == kRaceWeights[i].id) return static_cast<int>(i);
			return -1;
		}

		// v1.7.8: robes and hoods (the mage / necromancer look)
		bool IsRobe(const RE::TESObjectARMO* a_armo)
		{
			if (!a_armo) return false;
			const std::string n = Lower(a_armo->GetName());
			return n.find("robe") != std::string::npos || n.find("hood") != std::string::npos || n.find("cowl") != std::string::npos;
		}
		std::vector<RE::FormID> g_robed;
		bool Robed(RE::FormID a_base) { return std::find(g_robed.begin(), g_robed.end(), a_base) != g_robed.end(); }

		// v1.7.8: a ghost: the base's ghost flag, or a standing ability with a glow (effect shader) on it
		bool Spectral(RE::TESNPC* a_npc)
		{
			if (!a_npc) return false;
			if (a_npc->IsGhost()) return true;
			const auto* fx = a_npc->actorEffects;
			if (!fx || !fx->spells) return false;
			for (std::uint32_t i = 0; i < fx->numSpells; ++i) {
				auto* sp = fx->spells[i];
				if (!sp || sp->GetSpellType() != RE::MagicSystem::SpellType::kAbility) continue;
				for (auto* e : sp->effects) {
					if (e && e->baseEffect && e->baseEffect->data.effectShader) return true;
				}
			}
			return false;
		}

		void PickNpcs()
		{
			g_npcsPicked = true;
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) return;
			static constexpr const char* kNot[] = { "guard", "soldier", "bandit", "mage", "necromancer", "conjurer", "warlock", "wizard", "sorcer", "witch",
				"vampire", "thalmor", "forsworn", "cultist", "thief", "assassin", "smuggler", "mercenary", "warrior", "prisoner", "stormcloak", "legion",
				"ghost", "dead", "dremora", "skeleton", "hunter", "adventurer", "hired thug", "jarl", "steward", "housecarl",
				// v1.7.8: more of the robed and the spectral
				"apprentice", "adept", "acolyte", "initiate", "priest", "spectral", "spirit", "shade", "wraith", "wisp", "phantom", "spectre", "specter",
				"haunt", "lost soul", "revenant", "draugr", "hagraven", "summoned", "dawnguard", "vigilant", "worshipper", "devotee", "psijic", "college",
				"court wizard", "battlemage", "spellsword", "nightblade" };
			// v1.7.8: classes of spellcasters and fighters never row a fishing boat
			static constexpr const char* kNotClass[] = { "mage", "necromancer", "conjur", "sorcer", "witch", "warlock", "wizard", "nightblade",
				"battlemage", "spellsword", "vampire", "priest", "healer", "monk", "warrior", "soldier", "guard", "bandit", "assassin", "thief", "barbarian" };
			std::size_t total = 0, ghosts = 0, robed = 0;
			std::vector<RE::TESNPC*> loose[kRaceCount];
			for (auto* npc : dh->GetFormArray<RE::TESNPC>()) {
				if (!npc || npc->IsUnique()) continue;
				const std::string name = Lower(npc->GetName());
				if (name.empty()) continue;
				if (IsRealFisher(npc)) {
					std::vector<RE::TESObjectARMO*> gear;
					if (OutfitPieces(npc->defaultOutfit, gear, false)) g_fishGear.push_back(std::move(gear));
					continue;  // the outfit only, never the face
				}
				if ((npc->GetFormID() >> 24) != 0x00) continue;  // townsfolk from Skyrim.esm only
				const int r = RaceIndex(npc);
				if (r < 0) continue;
				if (std::any_of(std::begin(kNot), std::end(kNot), [&](const char* n) { return name.find(n) != std::string::npos; })) continue;
				// v1.7.8: no ghosts (the ghost flag, or a standing glow from an ability), and no templated bases: those are put
				// together from leveled lists when placed and came out as necromancers and spectres
				if (Spectral(npc)) { ++ghosts; continue; }
				if (npc->UsesTemplate() || npc->baseTemplateForm) continue;
				if (npc->npcClass) {
					const std::string cls = Lower(npc->npcClass->GetFullName());
					if (std::any_of(std::begin(kNotClass), std::end(kNotClass), [&](const char* n) { return cls.find(n) != std::string::npos; })) continue;
				}
				std::vector<RE::TESObjectARMO*> own;
				if (!OutfitPieces(npc->defaultOutfit, own)) { loose[r].push_back(npc); continue; }
				if (std::any_of(own.begin(), own.end(), [](const RE::TESObjectARMO* a) { return IsRobe(a); })) {
					g_robed.push_back(npc->GetFormID());  // v1.7.8: rows, but in plain clothes (the robes and hood come off)
					++robed;
				}
				g_byRace[r].push_back(npc);
				g_crewBases.push_back(npc->GetFormID());
				++total;
			}
			if (total == 0) {
				for (std::size_t i = 0; i < kRaceCount; ++i) {
					g_byRace[i] = loose[i];
					for (auto* n : loose[i]) g_crewBases.push_back(n->GetFormID());
					total += loose[i].size();
				}
				logger::info("Lake: no townsfolk in casual outfits found, using {} without", total);
			}
			logger::info("Lake: {} townsfolk crew bases ({} in robes, re-dressed when aboard; {} ghosts left out), {} fishing outfit(s)", total, robed, ghosts,
				g_fishGear.size());
		}

		RE::FormID PickCrewBase()
		{
			if (!g_npcsPicked) PickNpcs();
			float sum = 0.0f;
			for (std::size_t i = 0; i < kRaceCount; ++i) if (!g_byRace[i].empty()) sum += kRaceWeights[i].w;
			if (sum <= 0.0f) return 0;
			float roll = Rand(0.0f, sum);
			for (std::size_t i = 0; i < kRaceCount; ++i) {
				if (g_byRace[i].empty()) continue;
				if ((roll -= kRaceWeights[i].w) <= 0.0f) return Pick(g_byRace[i])->GetFormID();
			}
			for (std::size_t i = kRaceCount; i-- > 0;) if (!g_byRace[i].empty()) return Pick(g_byRace[i])->GetFormID();
			return 0;
		}
		bool InPool(const RE::TESNPC* a_npc)
		{
			const int r = RaceIndex(a_npc);
			return r >= 0 && std::find(g_byRace[r].begin(), g_byRace[r].end(), a_npc) != g_byRace[r].end();
		}

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
		}

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

		// v1.7.8: robes and hoods off (taken away, so the outfit can't put them back on the boat), plain clothes on unless a
		// fishing outfit follows
		void RobeSwap(RE::Actor* a_actor, bool a_fishGearNext)
		{
			if (!g_clothesPicked) PickClothes();
			auto* eq = RE::ActorEquipManager::GetSingleton();
			if (!a_actor || !eq) return;
			using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
			int off = 0;
			for (auto slot : { Slot::kBody, Slot::kHead, Slot::kHair, Slot::kCirclet, Slot::kHands, Slot::kFeet }) {
				auto* worn = a_actor->GetWornArmor(slot);
				if (!worn || !IsRobe(worn)) continue;
				eq->UnequipObject(a_actor, worn, nullptr, 1, nullptr, false, true, false, true);
				a_actor->RemoveItem(worn, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				++off;
			}
			std::string put;
			if (!a_fishGearNext && !g_bodyClothes.empty()) {
				// fur or hide first (they read as working clothes on the water), farm clothes otherwise
				std::vector<RE::TESObjectARMO*> warm;
				for (auto* a : g_bodyClothes) {
					const std::string n = Lower(a->GetName());
					if (n.find("fur") != std::string::npos || n.find("hide") != std::string::npos) warm.push_back(a);
				}
				auto* body = Rand(0.0f, 1.0f) < 0.6f && !warm.empty() ? Pick(warm) : Pick(g_bodyClothes);
				a_actor->AddObjectToContainer(body, nullptr, 1, nullptr);
				eq->EquipObject(a_actor, body, nullptr, 1, nullptr, false, true, false, true);
				put = body->GetName();
				if (!a_actor->GetWornArmor(Slot::kFeet) && !g_feetClothes.empty()) {
					auto* feet = Pick(g_feetClothes);
					a_actor->AddObjectToContainer(feet, nullptr, 1, nullptr);
					eq->EquipObject(a_actor, feet, nullptr, 1, nullptr, false, true, false, true);
				}
			}
			logger::info("Lake: {:08X} robes off ({} piece(s)), {}", a_actor->GetFormID(), off, a_fishGearNext ? "fishing clothes on" : put.empty() ? "nothing to put on" : put + " on");
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
				if (Dist2(pp.x, pp.y, p) < 1200.0f * 1200.0f) continue;
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
			if (!longRuns.empty()) { a_b.target = Pick(longRuns); return; }
			if (!any.empty()) {
				a_b.target = *std::max_element(any.begin(), any.end(), [&](const Pt& p, const Pt& q) { return Dist2(a_b.x, a_b.y, p) < Dist2(a_b.x, a_b.y, q); });
				return;
			}
			if (!a_lake.pts.empty()) a_b.target = Pick(a_lake.pts);
		}

		bool NewBoat(Lake& a_lake, const Pt* a_near = nullptr, float a_radius = 0.0f)
		{
			if (!g_npcsPicked) PickNpcs();
			if (PickCrewBase() == 0 || a_lake.pts.empty()) return false;
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
			const std::size_t top = cands.size() < 3 ? cands.size() : 3;
			const C& chosen = cands[std::uniform_int_distribution<std::size_t>(0, top - 1)(g_rng)];
			const Pt* at = chosen.p;
			Boat b;
			b.x = at->x;
			b.y = at->y;
			b.yaw = Rand(0.0f, 6.28318f);
			b.mode = Rand(0.0f, 1.0f) < 0.5f ? Mode::kFish : Mode::kRow;
			b.modeTimer = Rand(20.0f, 120.0f);
			b.paint = RandomPaint();
			b.seed = Rand(0.0f, 97.0f);
			const int people = Rand(0.0f, 1.0f) < 0.5f ? 2 : 1;
			for (int k = 0; k < people; ++k) {  // v1.7.54: never the same person twice in one boat
				RE::FormID id = PickCrewBase();
				for (int t = 0; t < 12 && std::find(b.people.begin(), b.people.end(), id) != b.people.end(); ++t) id = PickCrewBase();
				if (std::find(b.people.begin(), b.people.end(), id) == b.people.end()) b.people.push_back(id);
			}
			if (b.mode == Mode::kRow) PickTarget(b, a_lake, false);
			logger::info("Lake: new boat on '{}' at ({:.0f},{:.0f}), paint {}, nearest boat or player {:.0f} away", a_lake.name, at->x, at->y,
				Paint::kNames[b.paint], std::sqrt(chosen.score));
			a_lake.boats.push_back(std::move(b));
			return true;
		}

		// ---- drawing a boat -----------------------------------------------------------------------------------------------
		std::vector<RE::ObjectRefHandle*> Refs(Boat& a_b) { return { &a_b.hull, &a_b.lantern, &a_b.light }; }

		// The boat's frame: origin mid-boat at the waterline the player's boat keeps, the idle rock (RowRim BoatMotion idle).
		void Pose(const Boat& a_b, const Lake& a_lake, RE::NiPoint3& a_at, RE::NiMatrix3& a_rot)
		{
			const float t = a_b.time + a_b.seed;
			const float roll = 0.019f * (0.68f * std::sin(t * 1.19f) + 0.32f * std::sin(t * 2.31f + 0.9f));
			const float pitch = 0.010f * (0.64f * std::sin(t * 1.67f + 1.7f) + 0.36f * std::sin(t * 0.83f + 0.4f));
			const float heave = 2.4f * (0.7f * std::sin(t * 1.43f + 0.3f) + 0.3f * std::sin(t * 2.7f + 2.1f));
			a_rot = RzM(a_b.yaw) * (RxM(pitch) * RyM(roll + a_b.lean));
			a_at = RE::NiPoint3{ a_b.x, a_b.y, a_lake.water - kOriginBelowWater + heave };
		}

		void PlaceAbs(RE::NiAVObject* a_node, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot, float a_scale)
		{
			if (!a_node) return;
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
			a_node->local.scale = a_scale / parentScale;
			a_node->SetAppCulled(false);
			RE::NiUpdateData data{};
			a_node->Update(data);
			for (RE::NiAVObject* n = a_node->parent; n; n = n->parent) n->UpdateWorldBound();
		}

		RE::TESObjectREFR* PlaceAt(RE::TESObjectREFR* a_at, RE::TESBoundObject* a_base, const char* a_type, RE::ObjectRefHandle& a_out)
		{
			if (!a_at || !a_base) return nullptr;
			auto ref = a_at->PlaceObjectAtMe(a_base, false);
			if (!ref) { logger::warn("Lake: placing {} failed", a_type); return nullptr; }
			ref->SetCollision(false);
			AddSpawn(ref.get());
			a_out = ref->GetHandle();
			return ref.get();
		}

		void DeleteRef(RE::ObjectRefHandle& a_h)
		{
			if (auto r = a_h.get()) {
				RemoveSpawn(r->GetFormID());
				r->Disable();
				r->SetDelete(true);
			}
			a_h.reset();
		}

		void Hide(Boat& a_b, const char* a_why)
		{
			for (auto& c : a_b.crew) {
				if (auto a = c.actor.get()) DeleteActorLater(a.get());
			}
			a_b.crew.clear();
			if (a_b.visual && a_b.visual->parent) {
				RE::NiPointer<RE::NiAVObject> tmp;
				a_b.visual->parent->DetachChild(a_b.visual.get(), tmp);
			}
			a_b.visual.reset();
			a_b.visualOn = nullptr;
			a_b.havePose = false;
			a_b.oars = OarSet{};
			for (auto* h : Refs(a_b)) DeleteRef(*h);
			a_b.logged = false;
			a_b.crewPlaced = false;
			logger::info("Lake: boat at ({:.0f},{:.0f}) no longer drawn ({})", a_b.x, a_b.y, a_why);
		}

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
			RE::NiPoint3 at;
			RE::NiMatrix3 rot;
			Pose(a_b, a_lake, at, rot);
			auto* anchor = AnchorAt(at);
			if (!anchor) return false;
			auto* hull = PlaceAt(anchor, RE::TESForm::LookupByID<RE::TESBoundObject>(kHullStatic), "hull", a_b.hull);
			if (!hull) return false;
			hull->SetPosition(at);
			hull->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, a_b.yaw });
			a_b.crewPlaced = false;
			a_b.hullWait = 0.0f;
			a_b.refTimer = 0.0f;
			auto* player = RE::PlayerCharacter::GetSingleton();
			const float d = player ? std::sqrt(Dist2(player->GetPosition().x, player->GetPosition().y, Pt{ a_b.x, a_b.y })) : 0.0f;
			logger::info("Lake: boat on '{}' drawn at ({:.0f},{:.0f}), {:.0f} from the player, {}, paint {} (crew once the hull shows)", a_lake.name, a_b.x, a_b.y, d,
				a_b.mode == Mode::kFish ? "resting" : "rowing", Paint::kNames[a_b.paint]);
			return true;
		}

		// Our model on the hull ref's 3D (as the player's carrier does): a clone of RowRim_Hull.nif, physics stripped, the
		// hull's own shapes hidden, painted, and the two oars on it.
		void BuildVisual(Boat& a_b, RE::NiAVObject* a_root)
		{
			auto* node = a_root ? a_root->AsNode() : nullptr;
			if (!node) return;
			if (a_b.visual && a_b.visual->parent) {
				RE::NiPointer<RE::NiAVObject> tmp;
				a_b.visual->parent->DetachChild(a_b.visual.get(), tmp);
			}
			a_b.visual.reset();
			a_b.oars = OarSet{};
			a_b.visualOn = a_root;
			RE::NiPointer<RE::NiNode> src;
			const RE::BSModelDB::DBTraits::ArgsType args{};
			RE::BSModelDB::Demand(kHullModel, src, args);
			if (!src) RE::BSModelDB::Demand((std::string("meshes\\") + kHullModel).c_str(), src, args);
			if (!src) { logger::warn("Lake: {} not loaded", kHullModel); return; }
			auto* cloneObj = src->Clone();
			auto* clone = cloneObj ? cloneObj->AsNode() : nullptr;
			if (!clone) return;
			Oar::StripTree(clone);
			clone->local = RE::NiTransform{};
			for (auto& child : node->GetChildren()) {
				if (child) child->flags.set(RE::NiAVObject::Flag::kHidden);
			}
			node->AttachChild(clone, true);
			a_b.visual.reset(clone);
			Paint::Apply(clone, a_b.paint);
			for (int k = 0; k < 2; ++k) {
				a_b.oars.oar[k].reset(Oar::MakeWrapped(k == 0 ? "RowRim Lake Oar R" : "RowRim Lake Oar L"));
				if (a_b.oars.oar[k]) {
					Paint::OarWood(a_b.oars.oar[k].get());
					clone->AttachChild(a_b.oars.oar[k].get(), true);
				}
			}
			if (auto* fade = a_root->AsFadeNode()) fade->GetRuntimeData().currentFade = 1.0f;
		}

		void PlaceCrew(Boat& a_b, RE::TESObjectREFR* a_hull)
		{
			a_b.crewPlaced = true;
			if (!g_npcsPicked) PickNpcs();
			std::string names;
			for (std::size_t k = 0; k < a_b.people.size(); ++k) {
				auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(a_b.people[k]);
				// v1.7.54: the same person already in this boat (older saves too): someone else
				const bool twin = std::find(a_b.people.begin(), a_b.people.begin() + k, a_b.people[k]) != a_b.people.begin() + k;
				if (!InPool(npc) || twin) {
					for (int t = 0; t < 12; ++t) {
						npc = RE::TESForm::LookupByID<RE::TESNPC>(PickCrewBase());
						if (npc && std::find(a_b.people.begin(), a_b.people.begin() + k, npc->GetFormID()) == a_b.people.begin() + k) break;
					}
					if (npc) a_b.people[k] = npc->GetFormID();
				}
				auto placed = npc ? a_hull->PlaceObjectAtMe(npc, false) : nullptr;
				auto* actor = placed ? placed->As<RE::Actor>() : nullptr;
				if (!actor) continue;
				actor->SetDisplayName(npc->IsFemale() ? "Fisherwoman" : "Fisherman", true);
				AddSpawn(actor);
				Crew c;
				c.actor = actor->GetHandle();
				c.rower = k == 0;
				c.idleTimer = Rand(15.0f, 40.0f);
				if (!g_fishGear.empty() && Rand(0.0f, 1.0f) < kFishGearChance) c.gear = static_cast<int>(std::uniform_int_distribution<std::size_t>(0, g_fishGear.size() - 1)(g_rng));
				a_b.crew.push_back(c);
				names += std::format(" {}{:08X}", c.rower ? "rower " : "stern ", actor->GetFormID());
			}
			logger::info("Lake: boat at ({:.0f},{:.0f}) hull shows, crew aboard:{}", a_b.x, a_b.y, names);
		}

		bool Night()
		{
			auto* cal = RE::Calendar::GetSingleton();
			const float h = cal ? cal->GetHour() : 12.0f;
			return h >= 18.5f || h < 5.5f;
		}

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
			if (layer == RE::COL_LAYER::kWater || layer == RE::COL_LAYER::kTrigger || layer == RE::COL_LAYER::kNonCollidable || id >= 47 ||
				layer == RE::COL_LAYER::kActorZone || layer == RE::COL_LAYER::kCharController || layer == RE::COL_LAYER::kBiped ||
				layer == RE::COL_LAYER::kProjectileZone || layer == RE::COL_LAYER::kAcousticSpace || layer == RE::COL_LAYER::kPortal ||
				layer == RE::COL_LAYER::kCollisionBox || layer == RE::COL_LAYER::kAvoidBox || layer == RE::COL_LAYER::kDoorDetection ||
				layer == RE::COL_LAYER::kDeadBip || layer == RE::COL_LAYER::kItemPick || layer == RE::COL_LAYER::kWeapon)
				return false;
			auto* ref = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
			if (ref && ref->As<RE::Actor>()) return false;
			if (ref && (ref->GetFormID() & 0xFF000000) == 0xFF000000) return false;  // ours: another lake boat or the player's boat
			a_hit = a_from + (a_to - a_from) * std::clamp(pick.rayOutput.hitFraction, 0.0f, 1.0f);
			const char* name = ref && ref->GetBaseObject() ? ref->GetBaseObject()->GetName() : nullptr;
			a_what = std::format("{} (layer {})", name && *name ? std::string(name) : (ref ? std::format("{:08X}", ref->GetFormID()) : std::string("terrain")), id);
			return true;
		}

		void Blocked(Boat& a_b, Lake& a_lake, const RE::NiPoint3& a_hit, const std::string& a_what)
		{
			const float bx = a_hit.x - a_b.lastBlockX, by = a_hit.y - a_b.lastBlockY;
			const bool chatter = a_b.time - a_b.lastBlockAt < 5.0f && bx * bx + by * by < 300.0f * 300.0f;
			if (!chatter && !InAvoid(a_lake, a_hit.x, a_hit.y)) {
				a_b.lastBlockAt = a_b.time;
				a_b.lastBlockX = a_hit.x;
				a_b.lastBlockY = a_hit.y;
				if (a_lake.avoid.size() >= 300) a_lake.avoid.erase(a_lake.avoid.begin());
				a_lake.avoid.push_back({ a_hit.x, a_hit.y, 350.0f });
				std::erase_if(a_lake.pts, [&](const Pt& p) { return InAvoid(a_lake, p.x, p.y, 150.0f); });
				if (a_lake.saved) g_saveTimer = 30.0f;
			}
			a_b.speed *= 0.3f;
			const float oldYaw = a_b.yaw;
			PickTarget(a_b, a_lake, false);
			if (a_b.blockLog <= 0.0f) {
				a_b.blockLog = 5.0f;
				logger::info("Lake: boat at ({:.0f},{:.0f}) heading {:.0f} deg blocked by {} at ({:.0f},{:.0f}), new target ({:.0f},{:.0f})",
					a_b.x, a_b.y, oldYaw * 57.2958f, a_what, a_hit.x, a_hit.y, a_b.target.x, a_b.target.y);
			}
		}

		RE::NiPoint3 SeatLocal(const Crew& a_c)
		{
			if (!a_c.rower) return kSternSeat;
			const float k = a_c.shiftW * a_c.shiftW * (3.0f - 2.0f * a_c.shiftW);
			return RE::NiPoint3{ kRowerSeat.x, kRowerSeat.y + kRowShift * k, kRowerSeat.z };
		}

		void TendCrew(Boat& a_b, Crew& a_c, const RE::NiPoint3& a_at, const RE::NiMatrix3& a_rot, float a_dt, bool a_wantRow)
		{
			auto actor = a_c.actor.get();
			if (!actor || !actor->Is3DLoaded()) return;
			if (!a_c.setup) {
				a_c.setup = true;
				actor->SetCollision(false);
				SetDontMove(actor.get(), true);
				if (auto* av = actor->AsActorValueOwner()) {
					av->SetActorValue(RE::ActorValue::kAggression, 0.0f);
					av->SetActorValue(RE::ActorValue::kConfidence, 0.0f);
				}
				a_c.sitTimer = 0.0f;
			}
			if (actor->IsDead()) return;
			// v1.7.8: crew whose own outfit is robes (the necromancer look) take them off: fishing clothes or plain clothes,
			// fur or hide instead, and no hood
			if (a_c.dressTimer > 0.0f && a_c.dressTimer - a_dt <= 0.0f) {
				auto* npcBase = actor->GetActorBase();
				if (npcBase && Robed(npcBase->GetFormID()))
					RobeSwap(actor.get(), a_c.gear >= 0 && a_c.gear < static_cast<int>(g_fishGear.size()));
			}
			if (a_c.dressTimer > 0.0f && a_c.dressTimer - a_dt <= 0.0f && a_c.gear >= 0 && a_c.gear < static_cast<int>(g_fishGear.size())) {
				if (auto* eq = RE::ActorEquipManager::GetSingleton()) {
					for (auto* armo : g_fishGear[a_c.gear]) {
						actor->AddObjectToContainer(armo, nullptr, 1, nullptr);
						eq->EquipObject(actor.get(), armo, nullptr, 1, nullptr, false, true, false, true);  // v1.7.8: at once, so the dress check below sees it
					}
				}
			}
			if (a_c.dressTimer > 0.0f && (a_c.dressTimer -= a_dt) <= 0.0f && Dress(actor.get()))
				logger::info("Lake: {} {:08X} had no clothes on, dressed", actor->GetName(), actor->GetFormID());
			a_c.shiftW = std::clamp(a_c.shiftW + (a_c.rower && a_c.rowing ? 1.0f : -1.0f) * a_dt / 0.5f, 0.0f, 1.0f);
			actor->SetPosition(a_at + a_rot * SeatLocal(a_c), true);
			actor->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, a_b.yaw + (a_c.rower ? kPi : 0.0f) });
			actor->SetGraphVariableBool("bHumanoidFootIKDisable", true);  // v1.6.5: foot IK lifted the feet onto the hull's collision
			a_c.retry -= a_dt;
			// v1.4.0: short pauses (giving way, a sharp turn, a yield) keep the stroke going: the stop waits 1 s
			if (a_c.rowing && !a_wantRow) {
				a_c.stopHold += a_dt;
				if (a_c.stopHold < 1.0f) a_wantRow = true;
			} else {
				a_c.stopHold = 0.0f;
			}
			if (a_c.rower && a_wantRow != a_c.rowing && a_c.retry <= 0.0f) {
				a_c.retry = 0.5f;
				const bool ok = actor->NotifyAnimationGraph(a_wantRow ? kRowStartEvent : kRowStopEvent);
				if (ok && !a_wantRow) {
					// v1.4.0: back on the seat in the same frame (the stop alone stood them up until the next sit, 0.3 s later)
					bool sat = false;
					for (const char* ev : { "IdleChairEnterInstant", "IdleChairSitting" }) {
						if (actor->NotifyAnimationGraph(ev)) { sat = true; break; }
					}
					if (!sat) logger::info("Lake: {} {:08X} the sit after the row stop was refused", actor->GetName(), actor->GetFormID());
				}
				if (ok) {
					a_c.rowing = a_wantRow;
					if (a_wantRow) a_c.rowAt = a_b.time;
					a_c.sitTimer = a_wantRow ? 0.0f : 0.05f;  // v1.4.0: and once more next frame (was 0.3 s)
					a_c.last3D = actor->Get3D(false);
					auto* player = RE::PlayerCharacter::GetSingleton();
					a_c.closeBy = player && (actor->GetPosition() - player->GetPosition()).Length() < 4000.0f;
				}
				if (!a_b.logged || !ok) logger::info("Lake: {} {:08X} {} {}", actor->GetName(), actor->GetFormID(), a_wantRow ? kRowStartEvent : kRowStopEvent, ok ? "accepted" : "rejected");
				a_b.logged = true;
				return;
			}
			if (a_c.rowing) {
				const void* now3D = actor->Get3D(false);
				auto* player = RE::PlayerCharacter::GetSingleton();
				const bool nearNow = player && (actor->GetPosition() - player->GetPosition()).Length() < 4000.0f;
				const char* why = now3D != a_c.last3D ? "3D loaded" : nearNow && !a_c.closeBy ? "within 4000 of the player" : nullptr;
				a_c.last3D = now3D;
				a_c.closeBy = nearNow;
				if (why) {
					const bool ok = actor->NotifyAnimationGraph(kRowStartEvent);
					if (ok) a_c.rowAt = a_b.time;
					logger::info("Lake: {} {:08X} {} again ({}): {}", actor->GetName(), actor->GetFormID(), kRowStartEvent, why, ok ? "accepted" : "rejected");
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

		// The two oars (the player's TendOars, for a lake rower): in the oarlocks at rest; while the rower's clip poses its
		// AnimObject bones, on those bones, turned through the oarlock with the grip on the hand; blended over 0.8 s.
		void TendOars(Boat& a_b, RE::Actor* a_rower, const Crew* a_c, const RE::NiMatrix3& a_rot, float a_waterZ, bool a_fx, float a_dt)
		{
			auto& s = a_b.oars;
			if (!a_b.visual) return;
			Oar::Learn();
			auto* root = a_rower && a_c ? a_rower->Get3D(false) : nullptr;
			RE::NiAVObject* bones[2]{};
			bool posed = root && a_c->rowing && a_b.time - a_c->rowAt >= 0.3f && !a_rower->IsDead();
			if (posed) {
				auto* npc = root->GetObjectByName("NPC Root [Root]");
				const RE::NiPoint3 c = npc ? npc->world.translate : root->world.translate;
				for (int i = 0; i < 2; ++i) {
					bones[i] = root->GetObjectByName(Oar::kBone[i]);
					if (!bones[i] || (bones[i]->world.translate - c).Length() < 20.0f) posed = false;
				}
			}
			s.w = std::clamp(s.w + (posed ? 1.0f : -1.0f) * a_dt / kOarBlendTime, 0.0f, 1.0f);
			RE::NiTransform boatW = a_b.visual->world;
			if (posed) {  // the hands read where the rower sits in the boat (his 3D may be a frame off the drawn boat)
				boatW.rotate = a_rot;
				boatW.translate = root->world.translate - a_rot * SeatLocal(*a_c);
			}
			const RE::NiTransform boatInv = boatW.Invert();
			if (posed) {
				for (int i = 0; i < 2; ++i) {
					const RE::NiTransform clip = bones[i]->world * Oar::BoneTune(i, kOarRoll);
					const RE::NiPoint3 mid = boatInv * (clip * Oar::g_b.centre);
					const int k = mid.x >= 0.0f ? 0 : 1;
					const float side = k == 0 ? 1.0f : -1.0f;
					const RE::NiPoint3 lock = boatW * RE::NiPoint3{ side * kLock.x, kLock.y, kLock.z };
					RE::NiTransform drawn = clip;
					if (auto* hand = root->GetObjectByName(Oar::kHand[i])) {
						bool palmOk = false, capped = false;
						const RE::NiPoint3 grip = Oar::PalmGrip(root, i, hand, 0.0f, 7.0f, 4.2f, i == 0 ? -1.0f : 1.0f, palmOk);
						drawn = Oar::HandLock(clip, grip, lock, 12.0f, capped, 120.0f);
					}
					s.clip[k] = boatInv * drawn;
					s.have[k] = true;
				}
			}
			const float w = s.w * s.w * (3.0f - 2.0f * s.w);
			for (int k = 0; k < 2; ++k) {
				auto& oar = s.oar[k];
				if (!oar) continue;
				const RE::NiTransform rest = Oar::RestLocal(k == 0 ? 1.0f : -1.0f, kLock, kRestIn, kRestDip);
				oar->local = s.have[k] && w > 0.0f ? Oar::Blend(rest, s.clip[k], w) : rest;
				if (w <= 0.0f) s.have[k] = false;
				Oar::KeepFade(oar.get());
				RE::NiUpdateData data{};
				data.flags.set(RE::NiUpdateData::Flag::kDirty);
				oar->Update(data);
				if (a_fx) Fx::TendBlade(s.blade[k], oar.get(), a_waterZ, a_dt, posed, 10 + k);  // v1.5.11: their oars stir plankton too
			}
		}

		// v1.4.2 (OAS CullFlames): the candle flame and its particles are switched off, the light ref still lights the boat.
		int CullFlames(RE::NiAVObject* a_obj, int a_depth = 0)
		{
			if (!a_obj || a_depth > 12) return 0;
			std::string name = a_obj->name.c_str() ? a_obj->name.c_str() : "";
			for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			const bool particles = netimmerse_cast<RE::NiParticleSystem*>(a_obj) != nullptr;
			if (particles || name.find("flame") != std::string::npos || name.find("fire") != std::string::npos ||
				name.find("smoke") != std::string::npos || name.find("ember") != std::string::npos) {
				a_obj->SetAppCulled(true);
				return 1;
			}
			int n = 0;
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) n += CullFlames(child.get(), a_depth + 1);
			}
			return n;
		}

		// v1.4.2 (OAS TendLamp): the lantern hangs world-down from the spiral hook, its top on the hook, and swings as a
		// pendulum (1.6 s period, damping 0.35, 12 deg max) driven by the hook's acceleration in the boat's frame. The
		// light sits at the lantern's centre.
		void TendLamp(Boat& a_b, RE::TESObjectREFR* a_lantern, const RE::NiPoint3& a_at, const RE::NiMatrix3& a_rot, float a_dt)
		{
			auto& st = a_b.lamp;
			auto* root = a_lantern->Get3D();
			if (root != st.flameRoot) {
				st.flameRoot = root;
				const int culled = CullFlames(root);
				logger::info("Lake: lantern {:08X} {} flame part(s) switched off", a_lantern->GetFormID(), culled);
			}
			const RE::NiPoint3 pivot = a_at + a_rot * kLampHook;
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
					const RE::NiPoint3 fwdW = a_rot * RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
					const RE::NiPoint3 rightW = a_rot * RE::NiPoint3{ 1.0f, 0.0f, 0.0f };
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
			const RE::NiMatrix3 lrot = RzM(a_b.yaw) * RxM(st.pitch) * RyM(-st.roll);
			float top = 0.0f, mid = 0.0f;
			if (auto* base = a_lantern->GetBaseObject()) {
				top = static_cast<float>(base->boundData.boundMax.z);
				mid = 0.5f * (static_cast<float>(base->boundData.boundMax.z) + static_cast<float>(base->boundData.boundMin.z));
			}
			const RE::NiPoint3 meshPos = pivot - lrot * RE::NiPoint3{ 0.0f, 0.0f, top };
			PlaceAbs(root, meshPos, lrot, 1.0f);
			st.pos = meshPos;
			st.rot = lrot;
			st.placed = true;
			const RE::NiPoint3 centre = meshPos + lrot * RE::NiPoint3{ 0.0f, 0.0f, mid };
			if (auto li = a_b.light.get()) {
				li->data.location = centre;
				if (li->Get3D()) PlaceAbs(li->Get3D(), centre, lrot, 1.0f);
			}
		}

		void TendBoat(Boat& a_b, Lake& a_lake, float a_dt)
		{
			const bool live = a_b.Live();
			auto* player = RE::PlayerCharacter::GetSingleton();
			const RE::NiPoint3 pp = player ? player->GetPosition() : RE::NiPoint3{};
			a_b.time += a_dt;
			a_b.blockLog -= a_dt;

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
					logger::info("Lake: a boat is attacked, rowing away");
				}
			}

			float want = 0.0f;
			const float dx = a_b.target.x - a_b.x, dy = a_b.target.y - a_b.y;
			const float dist = std::sqrt(dx * dx + dy * dy);
			const float toPlayer = std::sqrt(Dist2(pp.x, pp.y, Pt{ a_b.x, a_b.y }));
			// OAS 53: keep clear of the other boats (RowRim: the origin is the hull's middle)
			float avoidTurn = 0.0f, yield = 1.0f;
			{
				const RE::NiPoint3 f0{ std::sin(a_b.yaw), std::cos(a_b.yaw), 0.0f };
				for (auto& o : a_lake.boats) {
					if (&o == &a_b) continue;
					const float ox = o.x - a_b.x, oy = o.y - a_b.y, od = std::sqrt(ox * ox + oy * oy);
					if (od < 1.0f) { a_b.x += Rand(-5.0f, 5.0f); continue; }
					if (od < 650.0f) {
						const float push = (650.0f - od) * 0.8f * a_dt;
						a_b.x -= ox / od * push;
						a_b.y -= oy / od * push;
					}
					if (od > 2000.0f) continue;
					const float ahead = ox * f0.x + oy * f0.y;
					const float lateral = ox * f0.y - oy * f0.x;
					if (ahead <= 0.0f || std::abs(lateral) > kKeepClear) continue;
					const float closeness = 1.0f - ahead / 2000.0f;
					avoidTurn += (lateral >= 0.0f ? -1.0f : 1.0f) * 0.9f * closeness * (1.0f - std::abs(lateral) / kKeepClear);
					if (ahead < 1100.0f) yield = (std::min)(yield, &o < &a_b ? 0.15f : 0.55f);
				}
				avoidTurn = std::clamp(avoidTurn, -1.1f, 1.1f);
			}
			float wantTurn = 0.0f;  // v1.3.1: rad/s the boat wants to turn (eased into turnVel below)
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
					wantTurn = std::clamp(turn * 1.5f, -kTurnRate, kTurnRate);
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
				if (want > 0.0f && (a_b.rayTimer -= a_dt) <= 0.0f) {
					a_b.rayTimer = 0.25f;
					const float len = 350.0f + a_b.speed * 2.5f;
					const RE::NiPoint3 from{ a_b.x + fwd.x * 250.0f, a_b.y + fwd.y * 250.0f, a_lake.water + 40.0f };  // inside the bow (the bow tip is 330 out)
					for (float off : { 0.0f, 0.4f, -0.4f }) {
						const RE::NiPoint3 dir{ std::sin(a_b.yaw + off), std::cos(a_b.yaw + off), 0.0f };
						RE::NiPoint3 hit{};
						std::string what;
						if (SolidAhead(from, from + dir * len, hit, what)) { Blocked(a_b, a_lake, hit, what); want = 0.0f; break; }
					}
				}
			}
			a_b.speed += (want - a_b.speed) * (1.0f - std::exp(-0.8f * a_dt));
			// v1.3.1: turns ease in and out (the player's turn response, 2/s) and the boat leans into them like the player's
			a_b.turnVel += (wantTurn - a_b.turnVel) * (1.0f - std::exp(-2.0f * a_dt));
			a_b.yaw += a_b.turnVel * a_dt;
			if (a_b.yaw > 6.28318f) a_b.yaw -= 6.28318f;
			else if (a_b.yaw < 0.0f) a_b.yaw += 6.28318f;
			{
				const float s01 = std::clamp(a_b.speed / 300.0f, 0.0f, 1.0f);
				const float leanTarget = std::clamp(a_b.turnVel * 0.35f * s01, -0.25f, 0.25f);
				a_b.lean += (leanTarget - a_b.lean) * (1.0f - std::exp(-6.0f * a_dt));
			}
			const float nx = a_b.x + std::sin(a_b.yaw) * a_b.speed * a_dt, ny = a_b.y + std::cos(a_b.yaw) * a_b.speed * a_dt;
			const WS ws = WaterState(nx, ny, a_lake.water, 40.0f);
			if (ws != WS::kBlocked && !InAvoid(a_lake, nx, ny)) { a_b.x = nx; a_b.y = ny; }
			else { a_b.speed = 0.0f; if (haveRower) PickTarget(a_b, a_lake, false); }
			if (a_b.pushX != 0.0f || a_b.pushY != 0.0f) {  // v1.7.61: a bump's shove, gliding off and dying away
				const float sx = a_b.x + a_b.pushX * a_dt, sy = a_b.y + a_b.pushY * a_dt;
				if (WaterState(sx, sy, a_lake.water, 40.0f) != WS::kBlocked && !InAvoid(a_lake, sx, sy)) { a_b.x = sx; a_b.y = sy; }
				else a_b.pushX = a_b.pushY = 0.0f;
				const float k = std::exp(-0.9f * a_dt);
				a_b.pushX *= k;
				a_b.pushY *= k;
				if (a_b.pushX * a_b.pushX + a_b.pushY * a_b.pushY < 4.0f) a_b.pushX = a_b.pushY = 0.0f;
			}
			if (!live) return;

			auto hull = a_b.hull.get();
			if (!hull || hull->IsDeleted() || hull->IsDisabled()) { Hide(a_b, "hull gone"); return; }
			auto* root = hull->Get3D();
			if (!root) {
				if ((a_b.hullWait += a_dt) > 10.0f) Hide(a_b, "hull 3D never came in");
				return;
			}
			a_b.hullWait = 0.0f;
			if (a_b.visualOn != root || !a_b.visual) {
				hull->SetCollision(false);  // again now its 3D is in (placed, it had none to switch off)
				BuildVisual(a_b, root);
			}
			if (!a_b.crewPlaced) PlaceCrew(a_b, hull.get());
			const bool rowing = haveRower && a_b.speed > 15.0f && want > 0.0f;

			RE::NiPoint3 at;
			RE::NiMatrix3 rot;
			Pose(a_b, a_lake, at, rot);
			// v1.3.1: the refs carry the boat's spot in their data (as the player's hull ref does); SetPosition (which also
			// set their 3D to an unrocked, old-heading pose twice a second: the jumps) only when the boat enters another cell
			for (auto* h : Refs(a_b)) {
				if (auto r = h->get()) {
					r->data.location = at;
					r->data.angle = RE::NiPoint3{ 0.0f, 0.0f, a_b.yaw };
				}
			}
			if ((a_b.refTimer -= a_dt) <= 0.0f) {
				a_b.refTimer = 0.5f;
				auto* tes = RE::TES::GetSingleton();
				auto* cellNow = tes ? tes->GetCell(at) : nullptr;
				for (auto* h : Refs(a_b)) {
					if (auto r = h->get(); r && cellNow && cellNow->IsAttached() && r->GetParentCell() != cellNow) r->SetPosition(at);
				}
			}
			PlaceAbs(root, at, rot, 1.0f);
			a_b.lastAt = at;
			a_b.lastRot = rot;
			a_b.havePose = true;
			if (auto* fade = root->AsFadeNode()) fade->GetRuntimeData().currentFade = 1.0f;

			const Crew* rowerCrew = nullptr;
			RE::Actor* rowerActor = nullptr;
			for (auto& c : a_b.crew) {
				if (!c.rower) continue;
				if (auto a = c.actor.get()) { rowerCrew = &c; rowerActor = a.get(); }
			}
			TendOars(a_b, rowerActor, rowerCrew, rot, a_lake.water, toPlayer < kFxRange, a_dt);
			if (toPlayer < kFxRange) Plankton::LakeWake(a_b.visual.get(), a_b.yaw, a_b.speed, a_lake.water, a_dt, a_b.pkWake, a_b.pkStern);  // v1.5.11

			const bool night = Night();
			if (night && !a_b.lantern) {
				PlaceAt(hull.get(), RE::TESForm::LookupByID<RE::TESBoundObject>(kLampStatic), "lantern", a_b.lantern);
				PlaceAt(hull.get(), RE::TESForm::LookupByID<RE::TESBoundObject>(kLampLight), "light", a_b.light);
			} else if (!night && a_b.lantern) {
				DeleteRef(a_b.lantern);
				DeleteRef(a_b.light);
			}
			if (auto l = a_b.lantern.get(); l && l->Get3D()) TendLamp(a_b, l.get(), at, rot, a_dt);
			else a_b.lamp = Boat::Lamp{};
			for (auto& c : a_b.crew) TendCrew(a_b, c, at, rot, a_dt, c.rower && rowing);
		}

		int LakeWant(const Lake& a_lake)
		{
			const int pts = static_cast<int>(a_lake.pts.size());
			if (pts < kMinLakePts || g_count <= 0) return 0;
			return std::clamp(pts / kPtsPerBoat, 1, g_count);
		}

		void TendSea(Lake& a_lake, float a_dt)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			const RE::NiPoint3 pp = player->GetPosition();
			const auto before = a_lake.boats.size();
			std::erase_if(a_lake.boats, [&](const Boat& b) { return !b.Live() && Dist2(pp.x, pp.y, Pt{ b.x, b.y }) > kSeaForget * kSeaForget; });
			if (a_lake.boats.size() != before) logger::info("Lake: {} sea boat(s) rowed off (left behind), {} left", before - a_lake.boats.size(), a_lake.boats.size());
			if (!a_lake.fresh || g_count <= 0) return;
			if ((a_lake.fillTimer -= a_dt) > 0.0f) return;
			a_lake.fillTimer = 5.0f;
			const auto day = static_cast<std::int64_t>(Today());
			Pt centre{ pp.x, pp.y };
			float radius = kSeaArea;
			int want = 0;
			std::string where = "open sea";
			if (const Harbour* h = HarbourNear(pp.x, pp.y, a_lake.world)) {
				centre = { h->x, h->y };
				radius = kHarbourRadius;
				want = (std::min)(g_count, 3 + static_cast<int>(Roll(static_cast<std::int64_t>(h->x), static_cast<std::int64_t>(h->y), day) % 2));
				where = h->name;
			} else {
				const auto zx = static_cast<std::int64_t>(std::floor(pp.x / 30000.0f)), zy = static_cast<std::int64_t>(std::floor(pp.y / 30000.0f));
				want = (std::min)(g_count, static_cast<int>(Roll(zx, zy, day) % 2));
			}
			const int have = static_cast<int>(std::count_if(a_lake.boats.begin(), a_lake.boats.end(), [&](const Boat& b) { return Dist2(centre.x, centre.y, Pt{ b.x, b.y }) < radius * radius; }));
			if (have >= want) return;
			if (NewBoat(a_lake, &centre, radius)) {
				a_lake.fillTimer = Rand(60.0f, 150.0f);
				logger::info("Lake: sea boat added near {} ({} of {} wanted there)", where, have + 1, want);
			}
		}

		void TendPopulation(Lake& a_lake, float a_dt)
		{
			if (a_lake.pts.size() < 3) return;
			if (IsSea(a_lake)) { TendSea(a_lake, a_dt); return; }
			if (a_lake.born < 0.0f && !a_lake.fresh) return;
			const int want = LakeWant(a_lake);
			const float today = Today();
			const bool anyLive = std::any_of(a_lake.boats.begin(), a_lake.boats.end(), [](const Boat& b) { return b.Live(); });
			if (a_lake.born < 0.0f || (today - a_lake.born >= 1.0f && !anyLive)) {
				a_lake.boats.clear();
				a_lake.born = today;
				while (static_cast<int>(a_lake.boats.size()) < want && NewBoat(a_lake)) {}
				logger::info("Lake: '{}' population made: {} of {} boat(s) for the day ({} route points)", a_lake.name, a_lake.boats.size(), want, a_lake.pts.size());
				return;
			}
			int added = 0;
			if (static_cast<int>(a_lake.boats.size()) < want && (a_lake.fillTimer -= a_dt) <= 0.0f) {
				a_lake.fillTimer = 3.0f;
				if (NewBoat(a_lake)) ++added;
			}
			while (static_cast<int>(a_lake.boats.size()) > want) {
				auto victim = std::find_if(a_lake.boats.begin(), a_lake.boats.end(), [](const Boat& b) { return !b.Live(); });
				if (victim == a_lake.boats.end()) { victim = a_lake.boats.end() - 1; Hide(*victim, "fewer boats"); }
				a_lake.boats.erase(victim);
			}
			if (added) logger::info("Lake: '{}' now {} boat(s)", a_lake.name, a_lake.boats.size());
		}

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
					logger::info("Lake: '{}' grew by {} route points (now {})", l->name, added, l->pts.size());
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
			logger::info("Lake: found {} ({} route points, water {:.0f})", l.name, l.pts.size(), l.water);
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

		bool IsOurPart(RE::TESObjectREFR* a_ref)
		{
			auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!base) return false;
			const RE::FormID b = base->GetFormID();
			if (b == kHullStatic || b == kLampStatic || b == kLampLight) return true;
			if (!a_ref->As<RE::Actor>()) return false;
			const char* nm = a_ref->GetDisplayFullName();
			const std::string_view n(nm ? nm : "");
			return n == "Fisherman" || n == "Fisherwoman";
		}

		void TendDeletes(float a_dt)
		{
			if (!g_purge.empty()) {
				// v1.4.1: a saved id can belong to someone else after the load (the game reuses free FF ids, a Cryomancer crashed
				// us). Only our own bases and our renamed crew are removed, and nothing is deleted in the frame it is disabled.
				int n = 0, skipped = 0;
				for (const auto& [id, savedBase] : g_purge) {
					if ((id >> 24) != 0xFF) continue;
					auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
					if (!ref || ref->IsDeleted()) continue;
					// v1.6.4: the same base it had when saved makes it ours (the crew's "Fisherman" name is not kept by the
					// save, so the name check let the old crew stay, sitting on the water, while new crew rowed the boat off)
					auto* nowBase = ref->GetBaseObject();
					const bool sameBase = savedBase != 0 && nowBase && nowBase->GetFormID() == savedBase;
					// a save from before v1.6.4 has no bases: an actor of our crew pool (townsfolk) at a listed FF id is ours
					if (savedBase == 0 && !g_npcsPicked) PickNpcs();
					const bool poolCrew = savedBase == 0 && ref->As<RE::Actor>() && InPool(nowBase ? nowBase->As<RE::TESNPC>() : nullptr);
					if (!sameBase && !poolCrew && !IsOurPart(ref)) {
						++skipped;
						const char* nm = ref->GetDisplayFullName();
						logger::info("Lake: purge skipped {:08X} '{}', not one of ours", id, nm ? nm : "");
						continue;
					}
					ref->Disable();
					g_pendingDeletes.push_back({ ref->GetHandle(), 2.0f });
					++n;
				}
				logger::info("Lake: {} saved lake boat part(s) removed after the load ({} listed, {} not ours)", n, g_purge.size(), skipped);
				g_purge.clear();
			}
			for (auto& d : g_pendingDeletes) {
				if ((d.t -= a_dt) <= 0.0f) {
					if (auto r = d.h.get()) r->SetDelete(true);
				}
			}
			std::erase_if(g_pendingDeletes, [](const PendingDelete& d) { return d.t <= 0.0f; });
		}
	}

	// ---- the interface Plugin.cpp uses ----------------------------------------------------------------------------------
	void OnGameLoading()
	{
		g_spawned.clear();
		g_pendingDeletes.clear();
		g_purge.clear();
		g_lakes = g_fileLakes;  // the refs went with the old game; the co-save hands its own lakes back
		g_sampledAt.clear();
		g_job = SampleJob{};
	}

	std::string SaveState()
	{
		std::ostringstream out;
		out << "V 55\n";
		for (const auto& l : g_lakes) {
			out << std::format("L {} {:08X} {:.1f} {} {:.4f}\n", l.name, l.world, l.water, l.saved ? 1 : 0, l.born);
			for (const auto& p : l.pts) out << std::format("P {:.0f} {:.0f}\n", p.x, p.y);
			for (const auto& a : l.avoid) out << std::format("A {:.0f} {:.0f} {:.0f}\n", a.x, a.y, a.r);
			for (const auto& b : l.boats) {
				out << std::format("B {:.0f} {:.0f} {:.3f} {} {:.0f} {:.0f} {:.1f} {}", b.x, b.y, b.yaw, b.mode == Mode::kFish ? 1 : 0, b.target.x, b.target.y, b.modeTimer, b.people.size());
				for (auto id : b.people) out << std::format(" {:08X}", id);
				out << std::format(" p{}\n", b.paint);
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
		while (std::getline(in, line)) {
			std::istringstream ls(line);
			std::string tag;
			if (!(ls >> tag) || tag == "V") continue;
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
				if (ls >> a.x >> a.y >> a.r) cur->avoid.push_back(a);
			} else if (tag == "B") {
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
				std::string extra;
				b.paint = RandomPaint();
				b.seed = Rand(0.0f, 97.0f);
				if (ls >> extra && extra.size() > 1 && extra[0] == 'p') {
					try { b.paint = std::clamp(std::stoi(extra.substr(1)), 0, static_cast<int>(Paint::kCount) - 1); } catch (...) {}
				}
				if (b.people.empty()) continue;
				cur->boats.push_back(std::move(b));
				++boats;
			}
		}
		logger::info("Lake: {} lake(s), {} boat(s) from the save", lakes, boats);
	}

	// v1.3.1: from the camera update, right before the frame is drawn (as the player's boat, LatePlace / PinRower): every
	// drawn lake boat is put at its pose of this frame again, and its crew's bodies onto their seats, so nothing the engine
	// does to their 3D after our update (the crew follow their character controllers on their own timing) shows.
	void LatePlace()
	{
		for (auto& l : g_lakes) {
			for (auto& b : l.boats) {
				if (!b.havePose || !b.visual) continue;
				auto hull = b.hull.get();
				auto* root = hull ? hull->Get3D() : nullptr;
				if (!root || root != b.visualOn) continue;
				PlaceAbs(root, b.lastAt, b.lastRot, 1.0f);
				if (auto lamp = b.lantern.get(); lamp && lamp->Get3D() && b.lamp.placed) PlaceAbs(lamp->Get3D(), b.lamp.pos, b.lamp.rot, 1.0f);
				for (auto& c : b.crew) {
					auto actor = c.actor.get();
					auto* aroot = actor && !actor->IsDead() ? actor->Get3D(false) : nullptr;
					if (!aroot) continue;
					const RE::NiPoint3 d = b.lastAt + b.lastRot * SeatLocal(c) - aroot->world.translate;
					const float len = d.Length();
					if (len < 0.01f || len > 60.0f) continue;
					RE::NiPoint3 dl = d;
					if (auto* par = aroot->parent) {
						dl = par->world.rotate.Transpose() * d;
						if (par->world.scale > 0.0f) dl = dl / par->world.scale;
					}
					aroot->local.translate += dl;
					RE::NiUpdateData data{};
					data.flags.set(RE::NiUpdateData::Flag::kDirty);
					aroot->Update(data);
				}
			}
		}
	}

	std::vector<std::pair<RE::FormID, RE::FormID>> SpawnedNow() { return g_spawned; }
	// v1.7.8: one of the lake boats' own refs (live, or from the save and waiting for the after-load purge)
	bool IsOurs(RE::FormID a_id)
	{
		auto has = [a_id](const auto& e) { return e.first == a_id; };
		return std::any_of(g_spawned.begin(), g_spawned.end(), has) || std::any_of(g_purge.begin(), g_purge.end(), has);
	}
	void PurgeAfterLoad(std::vector<std::pair<RE::FormID, RE::FormID>> a_ids) { g_purge = std::move(a_ids); }

	void Update(float a_dt)
	{
		if (!g_loaded) LoadLakes();
		a_dt = (std::min)(a_dt, 0.1f);
		TendDeletes(a_dt);
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!player || !cell || !cell->IsAttached() || !player->Get3D()) return;
		const RE::NiPoint3 pp = player->GetPosition();
		if (g_saveTimer > 0.0f && (g_saveTimer -= a_dt) <= 0.0f) SaveLakes();
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

	// rr lakehere <name> | lakepoint <name> | lakedel <name> | lakes | lakeboats on|off|now|<0-10>|view <1-10> | ripples
	std::string Command(const std::string& a_lower)
	{
		if (!g_loaded) LoadLakes();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (a_lower == "ripples") {
			Fx::g_on = !Fx::g_on;
			Fx::g_logs = 0;
			return std::format("oar ripples {}", Fx::g_on ? "on" : "off");
		}
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
			l->name = name;
			l->saved = true;
			l->fresh = true;
			MergePoints(*l, pts);
			SaveLakes();
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
				list += std::format(" | {}{}{}: {} pts, {} boats ({} drawn), {} blocked", l.name, l.saved ? "" : " [auto]", l.sea == 1 ? " [sea]" : "", l.pts.size(), l.boats.size(), live, l.avoid.size());
			}
			const std::string text = std::format("lake boats {}, up to {} per lake (by its size), up to {} drawn ({} now){}", g_on ? "on" : "off", g_count, g_view, drawn, list);
			logger::info("Lake: {}", text);
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
			return std::format("lake boats {}, up to {} per lake (by its size; the sea 3-4 at harbours), up to {} drawn at once", g_on ? "on" : "off", g_count, g_view);
		}
		return {};
	}

	// ---- v1.7.54: the player's boat bumping a lake boat ----
	// Each hull a capsule (its middle line from 250 astern to 250 ahead, radius 82): overlapping hulls are pushed apart,
	// half each, and a boat closing on the other recoils (a small bounce back, the other slowed). The crew speak only on
	// a bump at least 3 s after the last one counted (one knock can touch a dozen frames): the 1st a vanilla "Ow!" /
	// "What was that for?" (the Hit topic, v1.7.55), the 2nd
	// nothing but they row away fast, the 3rd and later (each at most every 20 s) a vanilla "watch it" line (the
	// ActorCollidewithActor topic): it is your fault now. 90 s without a bump and they forget.
	namespace Bump
	{
		struct Memory { int count = 0; float last = -100.0f, spoke = -100.0f; };
		inline std::unordered_map<const void*, Memory> g_mem;
		inline float g_clock = 0.0f;
		inline RE::TESTopic* g_topic = nullptr;
		inline RE::TESTopic* g_hitTopic = nullptr;  // v1.7.55: "Ow!", "What was that for?" (the vanilla Hit lines)
		inline bool g_topicLooked = false;
		inline void LookTopics()
		{
			if (g_topicLooked) return;
			g_topicLooked = true;
			if (auto* dh = RE::TESDataHandler::GetSingleton()) {
				for (auto* t : dh->GetFormArray<RE::TESTopic>()) {
					if (!t) continue;
					const auto st = t->data.subtype.get();
					if (!g_topic && st == RE::DIALOGUE_DATA::Subtype::kActorCollidewithActor) g_topic = t;
					if (!g_hitTopic && st == RE::DIALOGUE_DATA::Subtype::kHit) g_hitTopic = t;
				}
			}
			logger::info("Bump: 'watch it' topic {}, 'ow' topic {}", g_topic ? std::format("{:08X}", g_topic->GetFormID()) : std::string("not found"),
				g_hitTopic ? std::format("{:08X}", g_hitTopic->GetFormID()) : std::string("not found"));
		}
		inline RE::TESTopic* CollideTopic() { LookTopics(); return g_topic; }
		inline RE::TESTopic* HitTopic() { LookTopics(); return g_hitTopic ? g_hitTopic : g_topic; }
		inline void Say(RE::Actor* a_actor, RE::TESTopic* a_topic)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!a_actor || !a_topic || !policy) return;
			const auto h = policy->GetHandleForObject(a_actor->GetFormType(), a_actor);
			if (!h) return;
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
			auto* args = RE::MakeFunctionArguments(static_cast<RE::TESTopic*>(a_topic), static_cast<RE::Actor*>(nullptr), false);
			vm->DispatchMethodCall(h, "ObjectReference", "Say", args, cb);
		}
		inline void SegClosest(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, float& px, float& py, float& qx, float& qy)
		{
			// closest points between segments AB and CD (2D)
			const float d1x = bx - ax, d1y = by - ay, d2x = dx - cx, d2y = dy - cy, rx = ax - cx, ry = ay - cy;
			const float a = d1x * d1x + d1y * d1y, e = d2x * d2x + d2y * d2y, f = d2x * rx + d2y * ry;
			float sp = 0.0f, tp = 0.0f;
			const float c = d1x * rx + d1y * ry, b = d1x * d2x + d1y * d2y, den = a * e - b * b;
			sp = den > 1e-6f ? std::clamp((b * f - c * e) / den, 0.0f, 1.0f) : 0.0f;
			tp = (b * sp + f) / e;
			if (tp < 0.0f) { tp = 0.0f; sp = std::clamp(-c / a, 0.0f, 1.0f); }
			else if (tp > 1.0f) { tp = 1.0f; sp = std::clamp((b - c) / a, 0.0f, 1.0f); }
			px = ax + d1x * sp; py = ay + d1y * sp; qx = cx + d2x * tp; qy = cy + d2y * tp;
		}
	}
	// a_pos / a_hd / a_speed: the player's boat (middle, heading, signed speed); changed in place. Returns a bump strength
	// (0 none) for the caller's dip.
	inline float BumpPlayerBoat(RE::NiPoint3& a_pos, float a_hd, float& a_speed, float a_dt)
	{
		using namespace Bump;
		g_clock += a_dt;
		constexpr float kHalf = 250.0f, kR = 82.0f;
		const float fx = std::sin(a_hd), fy = std::cos(a_hd);
		float hit = 0.0f;
		// v1.7.61: the player's boat's own small recoil, gliding off over a second or so
		static float s_px = 0.0f, s_py = 0.0f;
		if (s_px != 0.0f || s_py != 0.0f) {
			a_pos.x += s_px * a_dt;
			a_pos.y += s_py * a_dt;
			const float k = std::exp(-2.0f * a_dt);
			s_px *= k;
			s_py *= k;
			if (s_px * s_px + s_py * s_py < 1.0f) s_px = s_py = 0.0f;
		}
		for (auto& lake : g_lakes) {
			for (auto& b : lake.boats) {
				if (!b.Live()) continue;
				const float ddx = b.x - a_pos.x, ddy = b.y - a_pos.y;
				if (ddx * ddx + ddy * ddy > 900.0f * 900.0f) continue;
				const float gx = std::sin(b.yaw), gy = std::cos(b.yaw);
				float px, py, qx, qy;
				SegClosest(a_pos.x - fx * kHalf, a_pos.y - fy * kHalf, a_pos.x + fx * kHalf, a_pos.y + fy * kHalf,
					b.x - gx * kHalf, b.y - gy * kHalf, b.x + gx * kHalf, b.y + gy * kHalf, px, py, qx, qy);
				float nx = px - qx, ny = py - qy;
				const float dist = std::sqrt(nx * nx + ny * ny);
				if (dist >= 2.0f * kR) continue;
				if (dist > 0.01f) { nx /= dist; ny /= dist; } else { nx = -gy; ny = gx; }
				const float depth = 2.0f * kR - dist;
				// v1.7.61: apart mostly by the lake boat (the lighter feel: it gives way, yours barely does)
				a_pos.x += nx * depth * 0.2f; a_pos.y += ny * depth * 0.2f;
				b.x -= nx * depth * 0.8f; b.y -= ny * depth * 0.8f;
				// closing speed along the normal
				const float vp = (fx * nx + fy * ny) * a_speed, vb = (gx * nx + gy * ny) * b.speed;
				const float closing = vb - vp;
				if (closing > 5.0f) {
					// v1.7.61: the lake boat is shoved off along the hit, as hard as the hit (it glides away, slowing), and swung
					// about where it was struck; yours loses a little way head-on (none glancing) and recoils a touch
					const float push = (std::min)(closing * 0.9f, 260.0f);
					b.pushX -= nx * push;
					b.pushY -= ny * push;
					const float rx = qx - b.x, ry = qy - b.y;              // where it was struck, from its middle
					const float torque = rx * -ny - ry * -nx;             // about +z of the shove -n
					b.turnVel = std::clamp(b.turnVel - torque * push * 0.00003f, -0.7f, 0.7f);
					b.speed *= 0.5f;
					a_speed *= 1.0f - 0.35f * std::abs(fx * nx + fy * ny);
					const float recoil = (std::min)(closing * 0.18f, 45.0f);
					s_px += nx * recoil;
					s_py += ny * recoil;
					hit = (std::max)(hit, std::clamp(closing / 150.0f, 0.15f, 1.0f));
				}
				// the crew's reaction, counted at most every 3 s
				auto& m = g_mem[&b];
				if (g_clock - m.last > 90.0f) m.count = 0;
				if (g_clock - m.last < 3.0f) { m.last = g_clock; continue; }
				m.last = g_clock;
				++m.count;
				RE::Actor* speaker = nullptr;
				for (auto& c : b.crew) if (auto a = c.actor.get(); a && !a->IsDead()) { speaker = a.get(); if (!c.rower) break; }
				logger::info("Bump: the player's boat and a lake boat, bump {} (closing {:.0f})", m.count, closing);
				if (!speaker) continue;
				if (m.count == 1) {
					Say(speaker, HitTopic());  // v1.7.55: voiced, no HUD text ("Ow!", "What was that for?")
				} else if (m.count == 2) {
					b.mode = Mode::kFlee;
					PickTarget(b, lake, true);
					b.speed = (std::max)(b.speed, kFleeSpeed * 0.6f);
				} else if (g_clock - m.spoke > 20.0f) {
					m.spoke = g_clock;
					Say(speaker, CollideTopic());
					b.mode = Mode::kFlee;
					PickTarget(b, lake, true);
				}
			}
		}
		return hit;
	}

	// v1.7.69: stray lamps (lights and lanterns left glowing over lakes: refs whose boat or handle was lost, a save, a cell
	// unload, a hidden lake boat). Every 5 s round the player: a game-made (FF) ref of our lamp light or lantern base, over
	// the water, that no boat holds (no lake boat, not the player's boat light a_keep) is removed. The player's boat and
	// the lake boats both get theirs placed again when they need one.
	inline void SweepStrayLamps(float a_dt, RE::FormID a_keep)
	{
		static float s_t = 3.0f;
		if ((s_t -= a_dt) > 0.0f) return;
		s_t = 5.0f;
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!player || !tes) return;
		auto* lightBase = RE::TESForm::LookupByID<RE::TESBoundObject>(kLampLight);
		auto* lampBase = RE::TESForm::LookupByID<RE::TESBoundObject>(kLampStatic);
		std::unordered_set<RE::FormID> held;
		if (a_keep) held.insert(a_keep);
		for (auto& lake : g_lakes)
			for (auto& b : lake.boats)
				for (auto* h : { &b.lantern, &b.light })
					if (auto r = h->get()) held.insert(r->GetFormID());
		int n = 0;
		tes->ForEachReferenceInRange(player, 12000.0f, [&](RE::TESObjectREFR* a_ref) {
			if (!a_ref || (a_ref->GetFormID() >> 24) != 0xFF || a_ref->IsDeleted() || a_ref->IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;
			auto* base = a_ref->GetBaseObject();
			if (!base || (base != lightBase && base != lampBase) || held.contains(a_ref->GetFormID())) return RE::BSContainer::ForEachResult::kContinue;
			const RE::NiPoint3 p = a_ref->GetPosition();
			bool overWater = false;
			if (auto* c = a_ref->GetParentCell(); c && c->IsExteriorCell()) {
				float w = 0.0f;
				overWater = c->GetWaterHeight(p, w) && std::isfinite(w) && std::abs(w) < 1000000.0f && std::abs(p.z - w) < 400.0f;
			}
			if (!overWater) return RE::BSContainer::ForEachResult::kContinue;  // not ours (a lamp some mod placed on land)
			a_ref->Disable();
			a_ref->SetDelete(true);
			++n;
			logger::info("Lake: stray {} {:08X} over the water removed", base == lightBase ? "lamp light" : "lantern", a_ref->GetFormID());
			return RE::BSContainer::ForEachResult::kContinue;
		});
		if (n) logger::info("Lake: {} stray lamp part(s) swept", n);

		// v1.7.70: stray lake boat crew (Fishermen / Fisherwomen left standing in the water: from a save whose list missed
		// them, or a boat that went while they stayed). A game-made (FF) actor named Fisherman / Fisherwoman (the name we
		// give our crew) that is in no lake boat's crew, not a follower, and in the water is removed.
		std::unordered_set<RE::FormID> crew;
		for (auto& lake : g_lakes)
			for (auto& b : lake.boats)
				for (auto& c : b.crew)
					if (auto a = c.actor.get()) crew.insert(a->GetFormID());
		int m = 0;
		if (auto* pl = RE::ProcessLists::GetSingleton()) {
			std::vector<RE::Actor*> gone;
			for (auto& h : pl->highActorHandles) {
				auto a = h.get();
				if (!a || a.get() == player || (a->GetFormID() >> 24) != 0xFF || a->IsDisabled() || a->IsPlayerTeammate() || crew.contains(a->GetFormID())) continue;
				const char* nm = a->GetDisplayFullName();
				if (!nm || (std::string_view(nm) != "Fisherman" && std::string_view(nm) != "Fisherwoman")) continue;
				const RE::NiPoint3 p = a->GetPosition();
				bool wet = false;
				if (auto* st = a->AsActorState(); st && st->IsSwimming()) wet = true;
				if (auto* c = a->GetParentCell(); !wet && c && c->IsExteriorCell()) {
					float w = 0.0f;
					wet = c->GetWaterHeight(p, w) && std::isfinite(w) && std::abs(w) < 1000000.0f && p.z < w - 10.0f;
				}
				if (wet) gone.push_back(a.get());
			}
			for (auto* a : gone) {
				logger::info("Lake: stray crew {:08X} '{}' in the water removed", a->GetFormID(), a->GetDisplayFullName());
				DeleteActorLater(a);
				++m;
			}
		}
		if (m) logger::info("Lake: {} stray crew swept", m);
	}

}
