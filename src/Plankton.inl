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

// Included by Plugin.cpp inside its anonymous namespace, after Oar and before LakeBoats.inl (Fx calls it).
// v1.5.0: bioluminescent plankton (sea sparkle) round the player's boat from late afternoon into the night. See
// src\Plankton_v150.md.
//  - Each oar catch stirs a cluster that spirals out and sinks (to about a metre, 70 units), flaring then fading.
//  - The blade dragging through the water leaves a few more; the hull moving through the water lights a trail along both
//    sides and the stern, in the water where it was disturbed (they stay put as the boat goes on).
//  - A few cling to the outside of the hull at the waterline and to each oar blade; they flare at each dip and as you row.
// Every glow is a clone of meshes\RowRim\RowRim_Plankton.nif (a small additive camera-facing quad), sized by its
// brightness. Free ones hang under the player's cell node in world space; clinging ones under the boat model / the oars.
namespace Plankton
{
	constexpr const char* kModel = "RowRim\\RowRim_Plankton.nif";
	constexpr const char* kModelSoft = "RowRim\\RowRim_PlanktonSoft.nif";  // v1.5.2: the wide, dim, diffuse glow (under the surface)
	constexpr int kSharp = 1500;     // free sparkles (near the surface) (v1.5.11: 1500, finer and far more of them)
	constexpr int kSoft = 600;       // v1.5.2: free diffuse glows (under the surface, slower) (v1.5.11: 600)
	constexpr int kAmbSharp = 1000;  // (v1.5.12: 1000, half of them the wake's fine sparkles)
	constexpr int kAmbSharpOld = 200;   // v1.5.3: the open water's own plankton round you, untouched by the boat (v1.5.4: 600 at density 3)
	constexpr int kAmbSoft = 1000;   // (v1.5.12: 1000)
	constexpr int kShoreSharp = 180;  // v1.5.9: the shoreline's own plankton, in single-file lines along the water's edge
	constexpr int kShoreSoft = 220;
	constexpr int kAmbLo = kSharp + kSoft;                 // first open-water slot
	constexpr int kShoreLo = kAmbLo + kAmbSharp + kAmbSoft;  // first shoreline slot
	constexpr int kFree = kShoreLo + kShoreSharp + kShoreSoft;  // [sharp | soft | open sharp | open soft | shore sharp | shore soft]
	constexpr float kShoreStep = 128.0f;  // v1.5.9: the shoreline scan's grid spacing
	constexpr int kShoreN = 44;           // grid cells across (44 x 128: 2800 out each way)
	constexpr float kAmbRadius = 7000.0f;  // v1.5.3: how far out the open-water glows are kept (v1.5.12: 7000, was 3600)
	constexpr float kAnchorRadius = 3600.0f;  // v1.5.12: how far out things standing in the water are looked for
	constexpr float kAmbNear = 260.0f;     // and not right under the boat
	constexpr int kHull = 110;       // (v1.5.13: 110)        // clinging to the hull (v1.5.1: 40, scattered over the whole waterline band)
	constexpr int kOarStick = 14;    // (v1.5.13: 14)     // clinging to each oar blade
	constexpr float kDeep = 70.0f;   // deepest they sink (about a metre)
	constexpr float kWaterline = 32.0f;  // the boat model's waterline at rest (origin 32 under the water)

	// half width of the hull at the waterline along the boat (model y), from RowRim_Hull.nif
	constexpr float kOutlineY[] = { -240, -225, -210, -195, -180, -165, -150, -135, -120, -105, -90, -75, -60, -45, -30, -15, 0,
		15, 30, 45, 60, 75, 90, 105, 120, 135, 150, 165, 180, 195, 210, 225, 240 };
	constexpr float kOutlineX[] = { 5.0f, 27.2f, 42.4f, 54.0f, 60.1f, 65.4f, 70.4f, 74.6f, 76.1f, 79.0f, 80.3f, 81.6f, 82.8f, 84.6f, 85.1f, 85.3f, 86.6f,
		85.3f, 85.1f, 84.6f, 82.8f, 81.6f, 80.3f, 79.0f, 76.1f, 74.6f, 70.4f, 65.4f, 60.1f, 54.0f, 42.4f, 27.2f, 5.0f };
	// spots on the outside of the hull round the waterline (model space, 0.6 out from the skin)
	constexpr float kStick[][3] = { { 71.2f, -141.9f, 33.4f }, { 45.8f, 196.3f, 33.0f }, { -81.5f, -64.1f, 32.6f }, { 85.2f, -38.5f, 35.2f },
		{ -85.9f, -12.9f, 34.7f }, { 51.8f, -195.3f, 35.6f }, { 51.8f, 195.3f, 35.6f }, { 16.5f, 214.6f, 32.1f }, { -85.7f, -25.7f, 34.9f },
		{ -0.6f, -234.3f, 35.1f }, { -76.7f, 115.4f, 35.0f }, { 13.4f, 223.9f, 34.9f }, { 83.0f, 51.4f, 33.0f }, { 57.9f, -181.9f, 34.3f },
		{ 71.2f, 141.9f, 33.4f }, { 57.9f, 181.9f, 34.3f }, { 85.2f, 38.5f, 35.2f }, { 77.8f, -102.5f, 34.4f }, { 85.9f, -12.9f, 34.7f },
		{ -75.2f, -128.3f, 35.5f }, { -68.8f, 154.8f, 34.7f }, { 63.7f, 168.4f, 34.1f }, { 16.5f, -214.6f, 32.1f }, { 13.4f, -223.9f, 34.9f },
		{ 81.5f, 64.1f, 32.6f }, { 60.4f, -156.5f, 27.6f }, { 72.0f, -116.3f, 29.3f }, { -79.1f, -89.7f, 33.8f } };

	struct Settings
	{
		bool on = true;
		int force = 0;        // 0 by the time of day, 1 always (testing), -1 never
		float density = 3.0f; // how many (v1.5.4: 3 by default, up to 6)
		float size = 0.5f;    // how big each glow is (v1.5.4: 0.5 by default)
		int open = 1800;      // v1.5.5: how many open-water glows (v1.5.12: 1800 by default, 0-2000)
		float openSize = 1.0f;  // v1.5.5: open-water glow size, on top of size
		int shore = 400;        // v1.5.9: how many shoreline glows (0-400)
		bool wake = true;        // v1.5.16: the boat's own V wake ([Wake] On)
		float fogNear = 150.0f;  // v1.5.15: the camera underwater: clear to here...
		float fogFar = 2100.0f;  // ...and all blue by here (30 m)
		bool loaded = false;
	};
	inline Settings g_set;

	inline std::mt19937& Rng()
	{
		static std::mt19937 r{ 1447u };
		return r;
	}
	inline float Rand(float a_lo, float a_hi) { return std::uniform_real_distribution<float>(a_lo, a_hi)(Rng()); }

	inline std::string IniPath()
	{
		std::error_code ec;
		return (std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "RowRim.ini").string();
	}

	inline void LoadSettings()
	{
		if (g_set.loaded) return;
		g_set.loaded = true;
		const auto ini = IniPath();
		g_set.on = GetPrivateProfileIntA("Plankton", "On", 1, ini.c_str()) != 0;
		g_set.force = GetPrivateProfileIntA("Plankton", "Force", 0, ini.c_str());
		char buf[32]{};
		GetPrivateProfileStringA("Plankton", "Density", "3.0", buf, sizeof(buf), ini.c_str());
		try { g_set.density = std::clamp(std::stof(buf), 0.0f, 6.0f); } catch (...) {}
		GetPrivateProfileStringA("Plankton", "Size", "0.5", buf, sizeof(buf), ini.c_str());
		try { g_set.size = std::clamp(std::stof(buf), 0.2f, 4.0f); } catch (...) {}
		g_set.open = std::clamp(static_cast<int>(GetPrivateProfileIntA("Plankton", "OpenCount", 1800, ini.c_str())), 0, kAmbSharp + kAmbSoft);
		GetPrivateProfileStringA("Plankton", "OpenSize", "1.0", buf, sizeof(buf), ini.c_str());
		try { g_set.openSize = std::clamp(std::stof(buf), 0.2f, 4.0f); } catch (...) {}
		g_set.wake = GetPrivateProfileIntA("Wake", "On", 1, ini.c_str()) != 0;
		GetPrivateProfileStringA("Underwater", "FogNear", "150", buf, sizeof(buf), ini.c_str());
		try { g_set.fogNear = std::clamp(std::stof(buf), 0.0f, 20000.0f); } catch (...) {}
		GetPrivateProfileStringA("Underwater", "FogFar", "2100", buf, sizeof(buf), ini.c_str());
		try { g_set.fogFar = std::clamp(std::stof(buf), g_set.fogNear + 50.0f, 40000.0f); } catch (...) {}
		g_set.shore = std::clamp(static_cast<int>(GetPrivateProfileIntA("Plankton", "Shore", 400, ini.c_str())), 0, kShoreSharp + kShoreSoft);
		logger::info("Plankton: {} (force {}), density {:.2f}, size {:.2f}", g_set.on ? "on" : "off", g_set.force, g_set.density, g_set.size);
	}
	inline void SaveSettings()
	{
		const auto ini = IniPath();
		WritePrivateProfileStringA("Plankton", "On", g_set.on ? "1" : "0", ini.c_str());
		WritePrivateProfileStringA("Plankton", "Force", std::to_string(g_set.force).c_str(), ini.c_str());
		WritePrivateProfileStringA("Plankton", "Density", std::format("{:.2f}", g_set.density).c_str(), ini.c_str());
		WritePrivateProfileStringA("Plankton", "Size", std::format("{:.2f}", g_set.size).c_str(), ini.c_str());
		WritePrivateProfileStringA("Plankton", "OpenCount", std::to_string(g_set.open).c_str(), ini.c_str());
		WritePrivateProfileStringA("Plankton", "OpenSize", std::format("{:.2f}", g_set.openSize).c_str(), ini.c_str());
		WritePrivateProfileStringA("Plankton", "Shore", std::to_string(g_set.shore).c_str(), ini.c_str());
	}

	// 0 by day, faint from late afternoon (16:00, about a third by 17:30), full from 19:30 to 04:30, gone by 06:30.
	inline float NightAmount()
	{
		if (g_set.force > 0) return 1.0f;
		if (g_set.force < 0) return 0.0f;
		auto* cal = RE::Calendar::GetSingleton();
		const float h = cal ? cal->GetHour() : 12.0f;
		auto ss = [](float a_x) { a_x = std::clamp(a_x, 0.0f, 1.0f); return a_x * a_x * (3.0f - 2.0f * a_x); };
		if (h >= 16.0f) return h < 17.5f ? 0.35f * ss((h - 16.0f) / 1.5f) : 0.35f + 0.65f * ss((h - 17.5f) / 2.0f);
		if (h < 4.5f) return 1.0f;
		return 1.0f - ss((h - 4.5f) / 2.0f);
	}

	struct Glow
	{
		RE::NiPointer<RE::NiNode> node;
		bool live = false;
		float age = 0.0f, life = 1.0f, size = 1.0f, flash = 0.15f, tw = 0.0f;
		// a free one: spirals about a centre, sinks, drifts
		RE::NiPoint3 centre{}, drift{};
		float r0 = 0.0f, grow = 0.0f, ang = 0.0f, spin = 0.0f, depth0 = 0.0f, sink = 0.0f, maxDepth = kDeep, drag = 1.5f;
		float waterZ = 0.0f;
		bool soft = false;    // v1.5.2: a diffuse one (its own model)
		bool amb = false;     // v1.5.3: open-water plankton (slow fade in, drift, fade out)
		float fadeIn = 0.2f, fadeOut = 0.6f;
		float pace = 1.0f;    // v1.5.2: how fast it moves (the deep, soft ones are slower)
		float turbA = 0.0f, turbW1 = 1.0f, turbW2 = 1.0f, turbP1 = 0.0f, turbP2 = 0.0f;  // v1.5.2: the swirl's churn
		float energy = 0.0f;  // clinging ones
		float boost = 0.0f;   // v1.5.6: stirred by someone wading or swimming through (flares)
		RE::NiPoint3 pvel{};  // v1.5.6: and pushed aside
		// v1.5.9: a shoreline one: sits in a single-file line along the water's edge, the line washing in and out together
		// (along nrm, out to sea) and creeping along the shore (along tan)
		bool shore = false;
		RE::NiPoint3 nrm{}, tan{};
		float washA = 0.0f, washW = 0.0f, washPh = 0.0f, slide = 0.0f;
	};

	struct State
	{
		RE::NiPointer<RE::NiNode> src;
		RE::NiPointer<RE::NiNode> srcSoft;
		RE::NiPointer<RE::NiNode> root;      // the free glows' parent, under the player's cell node
		Glow free[kFree];
		Glow hull[kHull];
		Glow oar[2][kOarStick];
		RE::NiAVObject* hullParent = nullptr;
		RE::NiAVObject* oarParent[2]{};
		int next = 0;
		int nextSoft = 0;
		int nextAmb[2]{};
		int nextShore[2]{};
		// v1.5.9: the shoreline: a grid of water depths round the camera, filled a few rows a frame, then the edge points
		struct ShorePt { float x, y, nx, ny; };
		std::vector<ShorePt> shorePts;
		float grid[kShoreN][kShoreN]{};
		float gridX = 0.0f, gridY = 0.0f;  // the grid's corner
		int gridRow = -1;                  // the row being filled (-1: start a new scan)
		float gridAge = 99.0f;
		float shoreAcc = 0.0f;
		int shoreLines = 0;
		float ambAcc = 0.0f;
		int ambMade = 0;
		float ambLog = 0.0f;
		bool failed = false;
		float spawnAcc = 0.0f, sternAcc = 0.0f;
		RE::NiPoint3 lastBoat{};
		bool haveLast = false;
		RE::NiPoint3 boatVel{};
		float night = 0.0f;
		int logs = 0;
		// v1.5.6: things the open-water plankton gathers round (rocks, jetties, moored boats: anything standing in the water)
		struct Anchor { float x, y, r; };
		std::vector<Anchor> anchors;
		float scanTimer = 0.0f;
		std::unordered_map<RE::FormID, RE::NiPoint3> wadeLast;  // v1.5.6: actors in the water, last spot
		std::unordered_map<RE::FormID, float> wadeAcc;
	};
	inline State g;

	inline bool LoadModel(RE::NiPointer<RE::NiNode>& a_out, const char* a_path)
	{
		if (a_out) return true;
		const RE::BSModelDB::DBTraits::ArgsType args{};
		RE::BSModelDB::Demand(a_path, a_out, args);
		if (!a_out) RE::BSModelDB::Demand((std::string("meshes\\") + a_path).c_str(), a_out, args);
		if (!a_out) {
			logger::warn("Plankton: meshes\\{} not loaded", a_path);
			return false;
		}
		logger::info("Plankton: sprite model '{}' loaded", a_path);
		return true;
	}

	inline RE::NiNode* MakeOne(bool a_soft = false)
	{
		if (g.failed) return nullptr;
		if (!LoadModel(g.src, kModel)) { g.failed = true; return nullptr; }
		if (a_soft && !LoadModel(g.srcSoft, kModelSoft)) a_soft = false;  // no soft model: sharp ones there too
		auto* obj = (a_soft ? g.srcSoft : g.src)->Clone();
		auto* n = obj ? obj->AsNode() : nullptr;
		if (!n) return nullptr;
		n->local = RE::NiTransform{};
		n->name = "RowRim_PlanktonGlow";
		n->SetAppCulled(true);
		return n;
	}

	inline RE::NiNode* CellNode()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!cell || !cell->IsAttached()) return nullptr;
		auto* ld = cell->GetRuntimeData().loadedData;
		return ld ? ld->cell3D.get() : nullptr;
	}

	// the free glows' root: made once, kept under the player's current cell node
	inline bool EnsureRoot()
	{
		auto* cell = CellNode();
		if (!cell) return false;
		if (!g.root) {
			auto* r = RE::NiNode::Create(static_cast<std::uint16_t>(kFree));
			r->name = "RowRim_Plankton";
			g.root = RE::NiPointer<RE::NiNode>(r);
			int made = 0;
			for (int i = 0; i < kFree; ++i) {
				auto& p = g.free[i];
				p.amb = i >= kAmbLo;
				p.shore = i >= kShoreLo;
				p.soft = p.shore ? i >= kShoreLo + kShoreSharp : p.amb ? i >= kAmbLo + kAmbSharp : i >= kSharp;
				auto* n = MakeOne(p.soft);
				if (!n) break;
				p.node = RE::NiPointer<RE::NiNode>(n);
				r->AttachChild(n, true);
				++made;
			}
			logger::info("Plankton: {} free glows made", made);
		}
		if (g.root->parent != cell) {
			if (auto* old = g.root->parent) {
				RE::NiPointer<RE::NiAVObject> keep;
				old->DetachChild(g.root.get(), keep);
			}
			cell->AttachChild(g.root.get(), true);
			if (g.logs++ < 6) logger::info("Plankton: glows hung under the cell node '{}'", cell->name.c_str() ? cell->name.c_str() : "");
		}
		return true;
	}

	// clinging glows: children of the boat model / an oar, set up again when that node changes
	inline void EnsureStuck(Glow* a_set, int a_n, RE::NiAVObject*& a_parentSeen, RE::NiAVObject* a_parent, bool a_oar)
	{
		auto* pn = a_parent ? a_parent->AsNode() : nullptr;
		if (pn && a_oar) {
			// v1.5.1: hang on the oar model inside the wrapper: Oar::KeepFade resets every child of the wrapper to identity each
			// frame, which pulled these to the wrapper's origin (inside the oar). The model's frame is the wrapper's.
			RE::NiNode* model = nullptr;
			for (auto& c : pn->GetChildren()) {
				if (c && c->AsNode() && std::string_view(c->name.c_str() ? c->name.c_str() : "") != "RowRim_PlanktonGlow") { model = c->AsNode(); break; }
			}
			pn = model;
		}
		if (pn == a_parentSeen) return;
		for (int i = 0; i < a_n; ++i) {
			auto& s = a_set[i];
			if (s.node && s.node->parent) {
				RE::NiPointer<RE::NiAVObject> keep;
				s.node->parent->DetachChild(s.node.get(), keep);
			}
			if (!pn) continue;
			if (!s.node) {
				auto* n = MakeOne();
				if (!n) return;
				s.node = RE::NiPointer<RE::NiNode>(n);
			}
			if (a_oar) {
				// on the blade: from its tip inward, a little off the shaft
				const RE::NiPoint3 ax = Oar::Axis();
				const RE::NiPoint3 tip = Oar::g_b.centre - ax * Oar::g_b.halfLen;
				s.node->local.translate = tip + ax * Rand(1.0f, 28.0f) + RE::NiPoint3{ Rand(-2.4f, 2.4f), Rand(-2.4f, 2.4f), Rand(-2.4f, 2.4f) };  // v1.5.13: the whole blade
			} else {
				// v1.5.1: scattered anywhere along both sides in the waterline band (model z 22..37), on the skin
				const float y = Rand(-232.0f, 232.0f);
				float w = 0.0f;
				for (std::size_t j = 0; j + 1 < std::size(kOutlineY); ++j) {
					if (y >= kOutlineY[j] && y <= kOutlineY[j + 1]) {
						const float f = (y - kOutlineY[j]) / (kOutlineY[j + 1] - kOutlineY[j]);
						w = kOutlineX[j] + (kOutlineX[j + 1] - kOutlineX[j]) * f;
						break;
					}
				}
				const float z = Rand(30.0f, 40.0f);  // v1.5.13: at and just above the waterline (under it the water hid them)
				const float in = (kWaterline + 5.0f - z) * 0.18f;  // the hull narrows a little below the waterline
				const float side = (i & 1) ? 1.0f : -1.0f;
				s.node->local.translate = RE::NiPoint3{ side * ((std::max)(2.0f, w - (std::max)(0.0f, in)) + 0.6f), y, z };
			}
			s.node->local.scale = 0.01f;
			s.node->SetAppCulled(true);
			s.energy = Rand(0.3f, 0.7f);
			s.size = Rand(1.8f, 3.2f);  // v1.5.13: was 0.7-1.3, a fifth of a free glow's size: hardly seen
			s.tw = Rand(0.0f, 6.28f);
			pn->AttachChild(s.node.get(), true);
		}
		a_parentSeen = pn;
	}

	// a_kind 0 the boat's, 1 open water, 2 the shoreline
	inline Glow& Take(bool a_soft, int a_kind = 0)
	{
		// a free slot of that kind, else the one of that kind closest to its end
		const int lo = a_kind == 2 ? (a_soft ? kShoreLo + kShoreSharp : kShoreLo) : a_kind == 1 ? (a_soft ? kAmbLo + kAmbSharp : kAmbLo) : (a_soft ? kSharp : 0);
		const int n = a_kind == 2 ? (a_soft ? kShoreSoft : kShoreSharp) : a_kind == 1 ? (a_soft ? kAmbSoft : kAmbSharp) : (a_soft ? kSoft : kSharp);
		int& next = a_kind == 2 ? g.nextShore[a_soft ? 1 : 0] : a_kind == 1 ? g.nextAmb[a_soft ? 1 : 0] : (a_soft ? g.nextSoft : g.next);
		for (int k = 0; k < n; ++k) {
			auto& p = g.free[lo + (next + k) % n];
			if (!p.live && p.node) { next = (next + k + 1) % n; return p; }
		}
		Glow* best = &g.free[lo];
		float most = -1.0f;
		for (int k = 0; k < n; ++k) {
			auto& p = g.free[lo + k];
			const float f = p.age / (std::max)(0.01f, p.life);
			if (f > most && p.node) { most = f; best = &p; }
		}
		return *best;
	}

	// v1.5.3: the hull's half width at the waterline at model y (0 past the ends)
	inline float HalfWidth(float a_y)
	{
		for (std::size_t j = 0; j + 1 < std::size(kOutlineY); ++j) {
			if (a_y >= kOutlineY[j] && a_y <= kOutlineY[j + 1]) {
				const float f = (a_y - kOutlineY[j]) / (kOutlineY[j + 1] - kOutlineY[j]);
				return kOutlineX[j] + (kOutlineX[j + 1] - kOutlineX[j]) * f;
			}
		}
		return 0.0f;
	}

	inline Glow* Spawn(const RE::NiPoint3& a_at, float a_waterZ, float a_r0, float a_grow, float a_spin, float a_depth0, float a_sink, float a_max,
		const RE::NiPoint3& a_drift, float a_life, float a_size, bool a_soft = false, float a_pace = 1.0f, float a_churn = 0.0f, bool a_amb = false, bool a_shore = false)
	{
		auto& p = Take(a_soft, a_shore ? 2 : a_amb ? 1 : 0);
		if (!p.node) return nullptr;
		p.live = true;
		p.age = 0.0f;
		p.life = a_life;
		p.size = a_size * g_set.size;
		p.flash = Rand(0.08f, 0.25f);
		p.tw = Rand(0.0f, 6.28f);
		p.centre = a_at;
		p.r0 = a_r0;
		p.grow = a_grow;
		p.ang = Rand(0.0f, 6.2832f);
		p.spin = a_spin;
		p.depth0 = a_depth0;
		p.sink = a_sink;
		p.maxDepth = a_max;
		p.drift = a_drift;
		p.drag = Rand(1.0f, 2.2f);
		p.waterZ = a_waterZ;
		p.pace = a_pace;
		p.turbA = a_churn;
		p.turbW1 = Rand(0.5f, 2.4f);
		p.turbW2 = Rand(0.5f, 2.4f);
		p.turbP1 = Rand(0.0f, 6.28f);
		p.turbP2 = Rand(0.0f, 6.28f);
		p.fadeIn = a_amb ? Rand(1.5f, 4.0f) : p.flash;
		p.fadeOut = a_amb ? Rand(3.0f, 8.0f) : 0.6f;
		p.boost = 0.0f;
		p.pvel = RE::NiPoint3{};
		p.washA = 0.0f;
		p.slide = 0.0f;
		return &p;
	}

	// v1.5.11: a boat's wake (the player's and the lake boats'): fine sparkles right against the hull skin all along both
	// sides, pushed slowly outward so the trail left behind opens into a V, and a fan closing in behind the stern
	inline void Wake(const RE::NiTransform& a_w, const RE::NiPoint3& a_vel, float a_speed, float a_waterZ, float a_dt, float& a_acc, float& a_sternAcc, float a_mul)
	{
		const float s01 = std::clamp(std::abs(a_speed) / 300.0f, 0.0f, 1.0f);
		if (s01 <= 0.05f) { a_acc = a_sternAcc = 0.0f; return; }
		const float lead = (a_w.rotate.Transpose() * a_vel).y >= 0.0f ? 1.0f : -1.0f;  // which end leads
		a_acc += a_dt * 110.0f * s01 * g_set.density * a_mul;
		for (int guard = 0; a_acc >= 1.0f && guard < 120; ++guard) {
			a_acc -= 1.0f;
			const float y = Rand(-232.0f, 232.0f);
			const float side = Rand(0.0f, 1.0f) < 0.5f ? 1.0f : -1.0f;
			const RE::NiPoint3 at = a_w * RE::NiPoint3{ side * (HalfWidth(y) + Rand(0.5f, 5.0f)), y, kWaterline };
			const RE::NiPoint3 out = a_w.rotate * RE::NiPoint3{ side, 0.0f, 0.0f };
			const bool soft = Rand(0.0f, 1.0f) < 0.22f;
			Spawn(at, a_waterZ, Rand(0.0f, 1.5f), Rand(0.3f, 1.5f), Rand(-0.6f, 0.6f), soft ? Rand(5.0f, 18.0f) : Rand(0.0f, 3.0f), soft ? Rand(1.0f, 3.0f) : Rand(0.2f, 1.2f),
				soft ? 40.0f : 12.0f, out * Rand(30.0f, 90.0f) * s01, Rand(5.0f, 10.0f), soft ? Rand(2.6f, 4.5f) : Rand(1.4f, 2.8f), soft, soft ? Rand(0.4f, 0.7f) : 1.0f, Rand(0.3f, 1.5f));
		}
		a_sternAcc += a_dt * 45.0f * s01 * g_set.density * a_mul;
		for (int guard = 0; a_sternAcc >= 1.0f && guard < 60; ++guard) {
			a_sternAcc -= 1.0f;
			const float x = Rand(-26.0f, 26.0f);
			const RE::NiPoint3 at = a_w * RE::NiPoint3{ x, -lead * (236.0f + Rand(0.0f, 10.0f)), kWaterline };
			const RE::NiPoint3 fan = a_w.rotate * RE::NiPoint3{ (x >= 0.0f ? 1.0f : -1.0f) * Rand(0.4f, 1.0f), -lead * Rand(0.0f, 0.4f), 0.0f };
			const bool soft = Rand(0.0f, 1.0f) < 0.35f;
			Spawn(at, a_waterZ, Rand(0.5f, 4.0f), Rand(1.0f, 4.0f), Rand(-1.5f, 1.5f), soft ? Rand(6.0f, 24.0f) : Rand(0.0f, 4.0f), soft ? Rand(1.5f, 4.0f) : Rand(0.3f, 1.5f),
				soft ? kDeep : 14.0f, fan * Rand(20.0f, 60.0f) * s01, Rand(5.0f, 11.0f), soft ? Rand(2.8f, 5.0f) : Rand(1.4f, 2.8f), soft, soft ? Rand(0.4f, 0.7f) : 1.0f, Rand(0.5f, 2.5f));
		}
	}

	inline bool Active() { return g_set.on && g.night > 0.02f && !g.failed; }

	// v1.5.11: a lake boat's wake (a_boat its RowRim_Hull model, a_yaw its heading, bow +Y)
	inline void LakeWake(const RE::NiAVObject* a_boat, float a_yaw, float a_speed, float a_waterZ, float a_dt, float& a_acc, float& a_sternAcc)
	{
		if (!a_boat || !Active() || !EnsureRoot()) return;
		const RE::NiPoint3 vel{ std::sin(a_yaw) * a_speed, std::cos(a_yaw) * a_speed, 0.0f };
		Wake(a_boat->world, vel, a_speed, a_waterZ, a_dt, a_acc, a_sternAcc, 0.6f);
	}

	// an oar's catch (a_side 0 right, 1 left): a cluster spiralling out of the blade's swirl and sinking.
	// v1.5.2: chaotic: each glow its own spin speed (some against the swirl), radius and churn, a few flung out; about two
	// in five are near-surface sparkles, the rest diffuse glows deeper down that move slower.
	inline void Dig(const RE::NiPoint3& a_at, float a_waterZ, int a_side, bool a_lake = false)
	{
		LoadSettings();
		if (!Active() || !EnsureRoot()) return;
		// v1.5.11: about twice as many, finer, and the swirl lingers behind the boat (slow to sink, long lived); a lake boat's half
		const int n = static_cast<int>(std::round(60.0f * g_set.density * (0.6f + 0.4f * g.night) * (a_lake ? 0.5f : 1.0f)));
		const float dir = a_side == 0 ? 1.0f : -1.0f;  // the two oars swirl opposite ways
		// the blade throws the water aft (against the boat's travel)
		RE::NiPoint3 back = a_lake ? RE::NiPoint3{} : g.boatVel * -1.0f;
		const float bl = back.Length();
		back = bl > 5.0f ? back / bl : RE::NiPoint3{};
		// two or three eddies off the blade, not one neat vortex
		RE::NiPoint3 eddy[3];
		for (auto& e : eddy) e = a_at + RE::NiPoint3{ Rand(-10.0f, 10.0f), Rand(-10.0f, 10.0f), 0.0f };
		for (int i = 0; i < n; ++i) {
			const bool soft = Rand(0.0f, 1.0f) < 0.4f;
			const float sp = (Rand(0.0f, 1.0f) < 0.18f ? -1.0f : 1.0f) * dir * Rand(0.6f, 4.8f);
			RE::NiPoint3 drift = back * Rand(10.0f, 50.0f);
			if (Rand(0.0f, 1.0f) < 0.35f) {  // flung out of the swirl (v1.5.13: more of them)
				const float a = Rand(0.0f, 6.2832f);
				drift += RE::NiPoint3{ std::cos(a), std::sin(a), 0.0f } * Rand(40.0f, 110.0f);
			}
			const RE::NiPoint3 at = eddy[static_cast<int>(Rand(0.0f, 2.99f))] + RE::NiPoint3{ Rand(-4.0f, 4.0f), Rand(-4.0f, 4.0f), 0.0f };
			if (soft)
				Spawn(at, a_waterZ, Rand(3.0f, 22.0f), Rand(10.0f, 34.0f), sp * 0.8f, Rand(5.0f, 22.0f), Rand(1.5f, 5.0f), Rand(30.0f, kDeep),
					drift, Rand(2.5f, 5.0f), Rand(3.0f, 6.0f), true, Rand(0.5f, 0.9f), Rand(3.0f, 12.0f));  // v1.5.13: wider, spreading, gone sooner
			else
				Spawn(at, a_waterZ, Rand(3.0f, 20.0f), Rand(10.0f, 36.0f), sp * 1.2f, Rand(0.0f, 5.0f), Rand(0.3f, 2.0f), Rand(8.0f, 18.0f),
					drift, Rand(2.0f, 4.5f), Rand(1.2f, 2.6f), false, 1.0f, Rand(2.0f, 9.0f));  // v1.5.13: wider, spreading, gone sooner
		}
		if (a_lake) return;
		// it lights the blade's own hangers-on, and wakes a few on the hull
		for (auto& s : g.oar[a_side]) s.energy = (std::min)(1.6f, s.energy + 1.0f);
		for (int i = 0; i < 10; ++i) {
			auto& s = g.hull[static_cast<int>(Rand(0.0f, kHull - 0.01f))];
			s.energy = (std::min)(1.2f, s.energy + 0.35f);
		}
		if (g.logs++ < 8) logger::info("Plankton: dig ({}), {} glows at ({:.0f},{:.0f},{:.0f}), night {:.2f}", a_side == 0 ? "right" : "left", n, a_at.x, a_at.y, a_at.z, g.night);
	}

	// the blade dragging through the water: a few at a time along its path
	inline void Trail(const RE::NiPoint3& a_at, float a_waterZ)
	{
		if (!Active() || !g.root) return;
		const int n = (Rand(0.0f, 1.0f) < 0.5f * g_set.density ? 3 : 2) * 2;  // v1.5.11: twice as many, mostly fine sparkles, lingering
		for (int i = 0; i < n; ++i) {
			const bool soft = i % 3 == 2;
			Spawn(a_at + RE::NiPoint3{ Rand(-4.0f, 4.0f), Rand(-4.0f, 4.0f), 0.0f }, a_waterZ, Rand(0.5f, 4.0f), Rand(1.0f, 5.0f), Rand(-2.5f, 2.5f),
				soft ? Rand(8.0f, 28.0f) : Rand(0.0f, 4.0f), soft ? Rand(2.0f, 6.0f) : Rand(0.3f, 1.5f), kDeep, RE::NiPoint3{}, Rand(5.0f, 10.0f), soft ? Rand(3.0f, 5.5f) : Rand(1.2f, 2.2f),
				soft, soft ? Rand(0.4f, 0.75f) : 1.0f, Rand(1.0f, 5.0f));
		}
	}

	// v1.7.8: the current. Plankton in moving water rides with it: rivers and rapids carry it downstream at the water's own
	// speed. Two sources: placed water whose water type has a current (its linear velocity, turned with the water's own
	// placement), and named rapids / waterfall meshes (OAS 41C flow sources: downstream = the steepest drop round them).
	namespace Flow
	{
		struct Vol
		{
			RE::FormID id = 0;
			RE::NiPoint3 pos{};       // the water's origin
			RE::NiMatrix3 rotT{};     // world -> its local
			float scale = 1.0f;
			float r = 0.0f;           // a quick circle round it
			RE::NiPoint3 lo{}, hi{};  // its bounds (local)
			RE::NiPoint3 vel{};       // world units/s
		};
		struct Src
		{
			RE::FormID id = 0;
			RE::NiPoint3 pos{};
			float dirX = 0.0f, dirY = 1.0f;
			float speed = 0.0f;
			float range = 0.0f;
		};
		inline std::vector<Vol> g_vols;
		inline std::vector<Src> g_srcs;
		inline std::unordered_set<RE::FormID> g_seen;  // looked at already (water with no current, effects)
		inline float g_scan = 0.0f;
		inline RE::NiPoint3 g_scanAt{};
		inline int g_logs = 0;
		constexpr float kScanRadius = 8000.0f;
		constexpr float kMaxSpeed = 450.0f;
		constexpr float kRapidsSpeed = 170.0f;  // a named rapids mesh with no current of its own (OAS: lip speed 420 for the boat)
		constexpr float kFallSpeed = 260.0f;

		inline void Clear()
		{
			g_vols.clear();
			g_srcs.clear();
			g_seen.clear();
			g_scan = 0.0f;
		}

		inline float SurfaceH(RE::TES* a_tes, const RE::NiPoint3& a_p)
		{
			float h = -1.0e9f, w = 0.0f, land = 0.0f;
			auto* c = a_tes->GetCell(a_p);
			if (c && c->IsAttached() && c->GetWaterHeight(a_p, w) && std::isfinite(w) && std::abs(w) < 1000000.0f) h = w;
			if (a_tes->GetLandHeight(a_p, land) && std::isfinite(land)) h = (std::max)(h, land);
			return h;
		}

		inline bool FlowName(std::string a_hay)
		{
			std::transform(a_hay.begin(), a_hay.end(), a_hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (a_hay.find("rapids") == std::string::npos && a_hay.find("waterfall") == std::string::npos && a_hay.find("cascade") == std::string::npos &&
				a_hay.find("falls") == std::string::npos)
				return false;
			// OAS 47.7.1: the splash / foam / mist at the foot of a fall are visuals, not water that moves
			for (const char* bad : { "skirt", "splash", "churn", "ring", "foam", "mist", "spray", "salmon", "rocks", "fallstop", "amb" })
				if (a_hay.find(bad) != std::string::npos) return false;
			return true;
		}

		inline bool AddWater(RE::TESObjectREFR* a_ref, RE::TESWaterForm* a_water)
		{
			const RE::NiPoint3 lin = a_water->linearVelocity;
			const float raw = lin.Length();
			if (raw < 0.001f) return true;  // still water
			auto* base = a_ref->GetBaseObject();
			auto* bound = base ? base->As<RE::TESBoundObject>() : nullptr;
			auto* root = a_ref->Get3D();
			if (!bound) return true;
			if (!root) return false;  // not drawn yet: looked at again on the next scan
			Vol v;
			v.id = a_ref->GetFormID();
			v.pos = root->world.translate;
			const RE::NiMatrix3 rot = root->world.rotate;
			v.scale = root->world.scale > 0.0f ? root->world.scale : 1.0f;
			v.rotT = rot.Transpose();
			const auto& bd = bound->boundData;
			v.lo = { static_cast<float>(bd.boundMin.x), static_cast<float>(bd.boundMin.y), static_cast<float>(bd.boundMin.z) };
			v.hi = { static_cast<float>(bd.boundMax.x), static_cast<float>(bd.boundMax.y), static_cast<float>(bd.boundMax.z) };
			v.r = (std::max)((v.hi - v.lo).Length() * 0.5f * v.scale, 200.0f) + std::hypot((v.hi.x + v.lo.x) * 0.5f, (v.hi.y + v.lo.y) * 0.5f) * v.scale;
			// The CK's linear velocity is small (Havok metres a second): about 70 game units to the metre. A big one is
			// taken as game units already.
			const RE::NiPoint3 local = raw < 20.0f ? lin * 70.0f : lin;
			v.vel = rot * local;
			v.vel.z = 0.0f;
			if (const float s = v.vel.Length(); s > kMaxSpeed) v.vel = v.vel * (kMaxSpeed / s);
			if (g_logs < 24) {
				++g_logs;
				logger::info("Flow: water {:08X} '{}' current ({:.2f},{:.2f},{:.2f}) -> {:.0f} u/s heading {:.0f} deg, bounds {:.0f} x {:.0f}", v.id,
					a_water->GetFullName() ? a_water->GetFullName() : "", lin.x, lin.y, lin.z, v.vel.Length(), std::atan2(v.vel.x, v.vel.y) * 57.2958f,
					(v.hi.x - v.lo.x) * v.scale, (v.hi.y - v.lo.y) * v.scale);
			}
			g_vols.push_back(v);
			return true;
		}

		inline void AddNamed(RE::TES* a_tes, RE::TESObjectREFR* a_ref, const std::string& a_name)
		{
			Src s;
			s.id = a_ref->GetFormID();
			s.pos = a_ref->GetPosition();
			const float h0 = SurfaceH(a_tes, s.pos);
			if (h0 < -1.0e8f) return;
			auto dropAt = [&](float a_ang, float a_r) {
				const float h = SurfaceH(a_tes, RE::NiPoint3{ s.pos.x + std::sin(a_ang) * a_r, s.pos.y + std::cos(a_ang) * a_r, s.pos.z });
				return h < -1.0e8f ? -1.0e9f : h0 - h;
			};
			float best = -1.0e9f, ang = 0.0f;
			for (float r : { 300.0f, 600.0f }) {
				for (int k = 0; k < 16; ++k) {
					const float a = static_cast<float>(k) * 0.3927f;
					const float d = dropAt(a, r);
					if (d > best) { best = d; ang = a; }
				}
			}
			if (best < 4.0f) return;  // flat all round: nothing to say which way it runs
			s.dirX = std::sin(ang);
			s.dirY = std::cos(ang);
			const bool fall = best >= 80.0f;
			s.speed = fall ? kFallSpeed : kRapidsSpeed;
			s.range = fall ? 1100.0f : 900.0f;
			if (g_logs < 24) {
				++g_logs;
				logger::info("Flow: {} {:08X} at ({:.0f},{:.0f}) runs {:.0f} deg (drop {:.0f}), {:.0f} u/s within {:.0f}", fall ? "fall" : "rapids", s.id, s.pos.x, s.pos.y,
					ang * 57.2958f, best, s.speed, s.range);
			}
			g_srcs.push_back(s);
		}

		// every 2 s (or 1500 on): what moves round the camera
		inline void Tend(float a_dt, const RE::NiPoint3& a_cam)
		{
			g_scan -= a_dt;
			if (g_scan > 0.0f && std::hypot(a_cam.x - g_scanAt.x, a_cam.y - g_scanAt.y) < 1500.0f) return;
			g_scan = 2.0f;
			g_scanAt = a_cam;
			auto* tes = RE::TES::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!tes || !player) return;
			const float far2 = (kScanRadius + 4000.0f) * (kScanRadius + 4000.0f);
			std::erase_if(g_vols, [&](const Vol& v) { const float dx = v.pos.x - a_cam.x, dy = v.pos.y - a_cam.y; return dx * dx + dy * dy > far2; });
			std::erase_if(g_srcs, [&](const Src& s) { const float dx = s.pos.x - a_cam.x, dy = s.pos.y - a_cam.y; return dx * dx + dy * dy > far2; });
			if (g_seen.size() > 20000) g_seen.clear();
			std::vector<std::pair<RE::TESObjectREFR*, std::string>> named;
			tes->ForEachReferenceInRange(player, kScanRadius, [&](RE::TESObjectREFR* a_ref) {
				if (!a_ref || a_ref->IsDisabled() || a_ref->IsDeleted()) return RE::BSContainer::ForEachResult::kContinue;
				const RE::FormID id = a_ref->GetFormID();
				if (g_seen.contains(id)) return RE::BSContainer::ForEachResult::kContinue;
				if (std::any_of(g_vols.begin(), g_vols.end(), [&](const Vol& v) { return v.id == id; }) ||
					std::any_of(g_srcs.begin(), g_srcs.end(), [&](const Src& s) { return s.id == id; }))
					return RE::BSContainer::ForEachResult::kContinue;
				auto* base = a_ref->GetBaseObject();
				if (!base) return RE::BSContainer::ForEachResult::kContinue;
				if (auto* acti = base->As<RE::TESObjectACTI>(); acti && acti->waterForm) {
					if (AddWater(a_ref, acti->waterForm)) g_seen.insert(id);
					return RE::BSContainer::ForEachResult::kContinue;
				}
				std::string hay;
				if (auto* m = base->As<RE::TESModel>(); m && m->GetModel()) hay = m->GetModel();
				if (hay.empty() || !FlowName(hay)) {
					g_seen.insert(id);
					return RE::BSContainer::ForEachResult::kContinue;
				}
				g_seen.insert(id);
				named.emplace_back(a_ref, hay);
				return RE::BSContainer::ForEachResult::kContinue;
			});
			for (auto& [ref, hay] : named) {
				if (g_srcs.size() >= 64) break;
				AddNamed(tes, ref, hay);
			}
		}

		// the current at a spot on the water (world units/s, flat)
		inline RE::NiPoint3 At(const RE::NiPoint3& a_p, float a_waterZ)
		{
			RE::NiPoint3 v{};
			float wsum = 0.0f;
			for (const auto& vol : g_vols) {
				const float dx = a_p.x - vol.pos.x, dy = a_p.y - vol.pos.y;
				if (dx * dx + dy * dy > vol.r * vol.r) continue;
				if (std::abs(a_waterZ - vol.pos.z) > 400.0f) continue;  // another water above or below
				const RE::NiPoint3 l = vol.rotT * RE::NiPoint3{ dx, dy, a_waterZ - vol.pos.z } / vol.scale;
				constexpr float m = 40.0f;
				if (l.x < vol.lo.x - m || l.x > vol.hi.x + m || l.y < vol.lo.y - m || l.y > vol.hi.y + m) continue;
				v += vol.vel;
				wsum += 1.0f;
			}
			if (wsum > 0.0f) return v / wsum;
			for (const auto& s : g_srcs) {
				const float dx = a_p.x - s.pos.x, dy = a_p.y - s.pos.y;
				const float d2 = dx * dx + dy * dy;
				if (d2 > s.range * s.range) continue;
				const float t = 1.0f - std::sqrt(d2) / s.range;
				const float w = t * t * (3.0f - 2.0f * t);
				v += RE::NiPoint3{ s.dirX, s.dirY, 0.0f } * (s.speed * w);
			}
			if (const float sp = v.Length(); sp > kMaxSpeed) v = v * (kMaxSpeed / sp);
			return v;
		}
		inline bool Any() { return !g_vols.empty() || !g_srcs.empty(); }
	}

	struct Ctx
	{
		RE::NiAVObject* boat = nullptr;   // the boat model (model space: bow +Y, waterline z 32)
		RE::NiAVObject* oar[2]{};
		float speed = 0.0f;
		float waterZ = 0.0f;
		bool water = false;
	};

	inline float Bright(const Glow& a_p)
	{
		const float t = a_p.age;
		if (a_p.amb) {  // v1.5.3: open water: a slow fade in, a long glow, a slow fade out
			const float up = std::clamp(t / a_p.fadeIn, 0.0f, 1.0f);
			const float end = std::clamp((a_p.life - t) / a_p.fadeOut, 0.0f, 1.0f);
			return up * up * (3.0f - 2.0f * up) * end * end * (3.0f - 2.0f * end);
		}
		const float up = t < a_p.flash ? t / a_p.flash : 1.0f;
		const float down = t < a_p.flash ? 1.0f : std::exp(-(t - a_p.flash) / (a_p.life * 0.45f));
		const float end = std::clamp((a_p.life - t) / 0.6f, 0.0f, 1.0f);
		return up * down * end;
	}

	inline void Tick(float a_dt, const Ctx& a_c)
	{
		LoadSettings();
		const float dt = std::clamp(a_dt, 0.0f, 0.1f);
		g.night = g_set.on ? NightAmount() : 0.0f;
		const bool any = g.root != nullptr;
		if (!Active() && !any) return;
		if (g.failed) return;

		// the boat's velocity over the water (for the stroke's throw and the wake)
		if (a_c.boat) {
			const RE::NiPoint3 at = a_c.boat->world.translate;
			if (g.haveLast && dt > 0.0f) {
				const RE::NiPoint3 v = (at - g.lastBoat) / dt;
				if (v.Length() < 2000.0f) g.boatVel = g.boatVel + (v - g.boatVel) * (std::min)(1.0f, 6.0f * dt);
			}
			g.lastBoat = at;
			g.haveLast = true;
		} else {
			g.haveLast = false;
		}

		// the wake: the hull disturbing the water along both sides and the stern, more the faster it goes (v1.5.11: Wake())
		if (Active() && a_c.boat && a_c.water && EnsureRoot())
			Wake(a_c.boat->world, g.boatVel, a_c.speed, a_c.waterZ, dt, g.spawnAcc, g.sternAcc, 1.0f);

		// v1.5.1: where the camera is. From above, the water surface hides anything under it, so the glows are drawn on the
		// surface (a little above it), the deeper ones smaller and fainter. Under water they are drawn at their depth.
		float camZ = 1.0e9f;
		RE::NiPoint3 cam{};
		bool haveCam = false;
		if (auto* pc = RE::PlayerCamera::GetSingleton(); pc && pc->cameraRoot) {
			cam = pc->cameraRoot->world.translate;
			camZ = cam.z;
			haveCam = true;
		}
		if (Active() && haveCam) Flow::Tend(dt, cam);  // v1.7.8: the current round the camera
		const bool flowing = Active() && Flow::Any();

		// v1.5.3: the open water's own plankton all round you, untouched by the boat: kept topped up out to kAmbRadius, each
		// fading in somewhere on open water, drifting slowly and fading out again after 8-30 s
		if (Active() && haveCam && EnsureRoot()) {
			auto* tes = RE::TES::GetSingleton();
			int live = 0;
			for (int i = kAmbLo; i < kShoreLo; ++i) if (g.free[i].live) ++live;
			const int want = static_cast<int>(static_cast<float>(g_set.open) * (0.4f + 0.6f * g.night));  // v1.5.5: its own count
			g.ambAcc += dt * 400.0f;  // v1.5.12: 400/s (fills the wider area)
			int tries = 0;
			// v1.5.6: every 3 s, what stands in the water round you (statics, activators, movable statics whose bounds cross the
			// surface: rocks, jetty posts and decks, moored boats). Ours (FF refs) and actors are not counted.
			if ((g.scanTimer -= dt) <= 0.0f && tes) {
				g.scanTimer = 3.0f;
				g.anchors.clear();
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (player) {
					tes->ForEachReferenceInRange(player, kAnchorRadius, [&](RE::TESObjectREFR* a_ref) {
						if (!a_ref || g.anchors.size() >= 240) return RE::BSContainer::ForEachResult::kContinue;
						if ((a_ref->GetFormID() >> 24) == 0xFF || a_ref->IsDisabled() || a_ref->IsDeleted() || a_ref->As<RE::Actor>()) return RE::BSContainer::ForEachResult::kContinue;
						auto* base = a_ref->GetBaseObject();
						const auto ft = base ? base->GetFormType() : RE::FormType::None;
						if (ft != RE::FormType::Static && ft != RE::FormType::MovableStatic && ft != RE::FormType::Activator) return RE::BSContainer::ForEachResult::kContinue;
						auto* root = a_ref->Get3D();
						if (!root) return RE::BSContainer::ForEachResult::kContinue;
						const auto& bnd = root->worldBound;
						if (bnd.radius < 20.0f || bnd.radius > 2600.0f) return RE::BSContainer::ForEachResult::kContinue;
						auto* c = tes->GetCell(bnd.center);
						float w = 0.0f;
						if (!c || !c->IsAttached() || !c->GetWaterHeight(bnd.center, w) || !std::isfinite(w) || std::abs(w) > 1000000.0f) return RE::BSContainer::ForEachResult::kContinue;
						if (bnd.center.z - bnd.radius > w || bnd.center.z + bnd.radius < w) return RE::BSContainer::ForEachResult::kContinue;  // not in the water
						g.anchors.push_back({ bnd.center.x, bnd.center.y, bnd.radius });
						return RE::BSContainer::ForEachResult::kContinue;
					});
				}
				static int scanLogs = 0;
				if (scanLogs++ < 6) logger::info("Plankton: {} things standing in the water round you (rocks, jetties, boats)", g.anchors.size());
			}
			while (live < want && g.ambAcc >= 1.0f && tries < 32 && tes) {
				++tries;
				// v1.5.9: open water is even again; the shore and the things in the water have their own lines (below)
				const float a = Rand(0.0f, 6.2832f);
				const float r = std::sqrt(Rand(kAmbNear * kAmbNear / (kAmbRadius * kAmbRadius), 1.0f)) * kAmbRadius;  // even over the area
				RE::NiPoint3 at{ cam.x + std::cos(a) * r, cam.y + std::sin(a) * r, 0.0f };
				if (std::hypot(at.x - cam.x, at.y - cam.y) > kAmbRadius * 1.2f) continue;
				auto* cell = tes->GetCell(at);
				float w = 0.0f, land = 0.0f;
				if (!cell || !cell->IsAttached() || !cell->GetWaterHeight(at, w) || !std::isfinite(w) || std::abs(w) > 1000000.0f) continue;
				const float depth = tes->GetLandHeight(at, land) && std::isfinite(land) ? w - land : 9999.0f;
				if (depth < 40.0f) continue;  // dry land, and the shallows (the shoreline lines are there)
				if (a_c.boat) {
					const auto& bw = a_c.boat->world;
					const float bs = bw.scale > 0.0f ? bw.scale : 1.0f;
					const RE::NiPoint3 l = bw.rotate.Transpose() * (RE::NiPoint3{ at.x, at.y, w } - bw.translate) / bs;
					if (std::abs(l.y) < 250.0f && std::abs(l.x) < HalfWidth(l.y) + 6.0f) continue;
				}
				at.z = w;
				g.ambAcc -= 1.0f;
				++live;
				++g.ambMade;
				// v1.5.12: half diffuse, a quarter sparkles, a quarter the wake's fine scatter (tiny, just under the surface)
				const float kind = Rand(0.0f, 1.0f);
				const bool soft = kind < 0.5f, fine = kind >= 0.75f;
				const float d = soft ? Rand(6.0f, kDeep) : fine ? Rand(0.0f, 3.0f) : Rand(0.0f, 6.0f);
				const float cur = Rand(1.5f, 6.0f), ca = Rand(0.0f, 6.2832f);  // a slow current
				Spawn(at, w, Rand(0.0f, 2.0f), Rand(0.0f, 1.5f), Rand(-0.6f, 0.6f), d, Rand(0.0f, 1.5f), kDeep, RE::NiPoint3{ std::cos(ca), std::sin(ca), 0.0f } * cur * 8.0f,
					Rand(8.0f, 30.0f), (soft ? Rand(3.5f, 7.0f) : fine ? Rand(1.2f, 2.4f) : Rand(2.2f, 4.2f)) * g_set.openSize, soft, soft ? Rand(0.3f, 0.6f) : 1.0f, Rand(0.5f, 3.0f), true);
			}
			if (g.ambAcc > 120.0f) g.ambAcc = 120.0f;
			if ((g.ambLog -= dt) <= 0.0f) {
				g.ambLog = 10.0f;
				if (g.logs < 40) {
					++g.logs;
					logger::info("Plankton: open water {} of {} glowing ({} made in the last 10 s)", live, want, g.ambMade);
				}
				g.ambMade = 0;
			}
		}

		// v1.5.9: the shoreline's own plankton, apart from the open water: single-file lines along the water's edge (and
		// round rocks, jetty posts and moored boats), each line washing in and out with the water and creeping along the shore
		if (Active() && haveCam && EnsureRoot()) {
			auto* tes = RE::TES::GetSingleton();
			// water depth there: -1 no water (land), -999 not loaded (unknown, never an edge), 9999 unknown floor
			auto depthAt = [&](float a_x, float a_y, float& a_w) -> float {
				const RE::NiPoint3 q{ a_x, a_y, cam.z };
				auto* c = tes->GetCell(q);
				if (!c || !c->IsAttached()) return -999.0f;
				if (!c->GetWaterHeight(q, a_w) || !std::isfinite(a_w) || std::abs(a_w) > 1000000.0f) return -1.0f;
				float land = 0.0f;
				return tes->GetLandHeight(RE::NiPoint3{ a_x, a_y, a_w }, land) && std::isfinite(land) ? a_w - land : 9999.0f;
			};
			// the depth grid round the camera, 3 rows a frame (a full scan in about a quarter of a second), again every 5 s
			// or once the camera is 700 from where the last one was centred
			g.gridAge += dt;
			if (tes) {
				const float half = kShoreStep * kShoreN * 0.5f;
				if (g.gridRow < 0 && (g.gridAge > 5.0f || std::hypot(cam.x - (g.gridX + half), cam.y - (g.gridY + half)) > 700.0f)) {
					g.gridX = cam.x - half;
					g.gridY = cam.y - half;
					g.gridRow = 0;
				}
				for (int rows = 0; g.gridRow >= 0 && rows < 3; ++rows, ++g.gridRow) {
					if (g.gridRow >= kShoreN) {  // the scan is done: find the edge
						g.gridRow = -1;
						g.gridAge = 0.0f;
						g.shorePts.clear();
						auto dd = [&](int i, int j) { return std::clamp(g.grid[std::clamp(i, 0, kShoreN - 1)][std::clamp(j, 0, kShoreN - 1)], -60.0f, 300.0f); };
						for (int i = 1; i < kShoreN - 1; ++i)
							for (int j = 1; j < kShoreN - 1; ++j) {
								if (g.grid[i][j] < 3.0f || g.grid[i][j] > 250.0f) continue;  // a wet, shallow cell (not a sheer drop or an unknown floor)...
								auto dry = [&](int a, int b) { return g.grid[a][b] < 3.0f && g.grid[a][b] > -500.0f; };
								auto gone = [&](int a, int b) { return g.grid[a][b] < -500.0f; };
								if (gone(i - 1, j) || gone(i + 1, j) || gone(i, j - 1) || gone(i, j + 1)) continue;  // the loaded area's edge, not a shore
								if (!dry(i - 1, j) && !dry(i + 1, j) && !dry(i, j - 1) && !dry(i, j + 1)) continue;  // ...by dry land
								float nx = dd(i + 1, j) - dd(i - 1, j), ny = dd(i, j + 1) - dd(i, j - 1);  // toward deeper water
								const float l = std::hypot(nx, ny);
								if (l < 0.01f) continue;
								g.shorePts.push_back({ g.gridX + (i + 0.5f) * kShoreStep, g.gridY + (j + 0.5f) * kShoreStep, nx / l, ny / l });
							}
						static int shoreLogs = 0;
						if (shoreLogs++ < 8) logger::info("Plankton: {} shoreline spots round you, {} things in the water", g.shorePts.size(), g.anchors.size());
						break;
					}
					for (int i = 0; i < kShoreN; ++i) {
						float w = 0.0f;
						g.grid[i][g.gridRow] = depthAt(g.gridX + (i + 0.5f) * kShoreStep, g.gridY + (g.gridRow + 0.5f) * kShoreStep, w);
					}
				}
			}
			int live = 0;
			for (int i = kShoreLo; i < kFree; ++i) if (g.free[i].live) ++live;
			const int want = static_cast<int>(static_cast<float>(g_set.shore) * (0.4f + 0.6f * g.night));
			g.shoreAcc += dt * 3.0f;  // up to 3 new lines a second while short
			int tries = 0;
			while (tes && live < want - 4 && g.shoreAcc >= 1.0f && tries++ < 2 && (!g.shorePts.empty() || !g.anchors.empty())) {
				// three in four along the shore, the rest round something standing in the water
				const bool byThing = !g.anchors.empty() && (g.shorePts.empty() || Rand(0.0f, 1.0f) < 0.2f);
				RE::NiPoint3 base{}, n{};
				float ringR = 0.0f;
				if (byThing) {
					const auto& an = g.anchors[static_cast<std::size_t>(Rand(0.0f, static_cast<float>(g.anchors.size()) - 0.01f))];
					ringR = (std::max)(30.0f, an.r * 0.6f);
					base = RE::NiPoint3{ an.x, an.y, 0.0f };
				} else {
					const auto& sp = g.shorePts[static_cast<std::size_t>(Rand(0.0f, static_cast<float>(g.shorePts.size()) - 0.01f))];
					base = RE::NiPoint3{ sp.x, sp.y, 0.0f };
					n = RE::NiPoint3{ sp.nx, sp.ny, 0.0f };
				}
				if (std::hypot(base.x - cam.x, base.y - cam.y) > kAmbRadius) continue;
				g.shoreAcc -= 1.0f;
				// v1.5.10: a loose patch, not a line: 6-18 glows scattered along the shore (or round the thing in the water), thicker
				// at the edge and thinning out to sea, each at its own depth, so patches overlap into a ragged band that curls
				// with the coast
				const int count = static_cast<int>(Rand(6.0f, 18.99f) * (0.6f + 0.4f * (std::min)(1.0f, g_set.density / 3.0f)));
				const float washA = Rand(3.0f, 8.0f), washW = Rand(0.45f, 0.85f), washPh = Rand(0.0f, 6.2832f);
				const float life = Rand(14.0f, 34.0f);
				for (int k = 0; k < count && live < want; ++k) {
					RE::NiPoint3 at{}, nk{}, tk{};
					const float target = 2.0f + -std::log((std::max)(0.03f, Rand(0.0f, 1.0f))) * 14.0f;  // most just off the edge, a few farther out
					if (byThing) {  // anywhere round it, in or just outside its bounds
						const float ang = Rand(0.0f, 6.2832f);
						nk = RE::NiPoint3{ std::cos(ang), std::sin(ang), 0.0f };
						at = base + nk * (ringR * Rand(0.7f, 1.3f) + Rand(0.0f, 1.0f) * Rand(0.0f, 70.0f));
					} else {        // spread along the shore, then fitted to its own depth
						nk = n;
						at = base + RE::NiPoint3{ -n.y, n.x, 0.0f } * Rand(-kShoreStep * 0.9f, kShoreStep * 0.9f) + n * Rand(-10.0f, 25.0f);
					}
					tk = RE::NiPoint3{ -nk.y, nk.x, 0.0f };
					float w = 0.0f, d = depthAt(at.x, at.y, w);
					for (int step = 0; step < 8 && !byThing; ++step) {  // walk in or out along the normal to the line's depth
						if (d < -500.0f || d > 9000.0f) break;
						const float miss = target - d;
						if (std::abs(miss) < 4.0f) break;
						at += nk * std::clamp(-miss * 1.5f, -24.0f, 24.0f);
						d = depthAt(at.x, at.y, w);
					}
					if (d < 2.0f || d > 9000.0f) continue;  // still on land, or not known
					at += tk * Rand(-14.0f, 14.0f) + nk * Rand(-4.0f, 10.0f);  // off the depth line, so it never reads as a row
					if (a_c.boat) {
						const auto& bw = a_c.boat->world;
						const float bs = bw.scale > 0.0f ? bw.scale : 1.0f;
						const RE::NiPoint3 l = bw.rotate.Transpose() * (RE::NiPoint3{ at.x, at.y, w } - bw.translate) / bs;
						if (std::abs(l.y) < 250.0f && std::abs(l.x) < HalfWidth(l.y) + 6.0f) continue;
					}
					at.z = w;
					const bool soft = Rand(0.0f, 1.0f) < 0.5f;
					auto* p = Spawn(at, w, 0.0f, 0.0f, 0.0f, soft ? Rand(4.0f, (std::min)(30.0f, (std::max)(5.0f, d))) : Rand(0.0f, 3.0f), 0.0f, kDeep, RE::NiPoint3{},
						life * Rand(0.55f, 1.25f), (soft ? Rand(3.2f, 5.5f) : Rand(2.2f, 3.8f)) * g_set.openSize, soft, 1.0f, Rand(0.3f, 1.5f), true, true);
					if (!p) continue;
					p->shore = true;
					p->nrm = nk;
					p->tan = tk;
					p->washA = washA * Rand(0.6f, 1.3f);
					p->washW = washW * Rand(0.85f, 1.15f);
					p->washPh = washPh + Rand(-0.9f, 0.9f);  // the patch washes about together, not in step
					p->slide = Rand(-2.5f, 2.5f);
					p->fadeIn = Rand(2.0f, 5.0f);
					p->fadeOut = Rand(4.0f, 9.0f);
					++live;
				}
				++g.shoreLines;
			}
			if (g.shoreAcc > 6.0f) g.shoreAcc = 6.0f;
		}

		// v1.5.6: anyone wading or swimming (you, followers, NPCs) stirs the water: a burst round them as they move, and the
		// open-water glows near them flare and are pushed aside
		struct Wader { RE::NiPoint3 at; float w; float speed; };
		std::vector<Wader> waders;
		if (Active() && EnsureRoot()) {
			auto* tes = RE::TES::GetSingleton();
			std::vector<RE::Actor*> actors;
			if (auto* player = RE::PlayerCharacter::GetSingleton()) actors.push_back(player);
			if (auto* pl = RE::ProcessLists::GetSingleton()) {
				for (auto& h : pl->highActorHandles) {
					if (actors.size() >= 48) break;
					if (auto a = h.get(); a && !a->IsDisabled() && a->Is3DLoaded()) actors.push_back(a.get());
				}
			}
			for (auto* a : actors) {
				if (!tes) break;
				const RE::NiPoint3 at = a->GetPosition();
				if (haveCam && std::hypot(at.x - cam.x, at.y - cam.y) > kAmbRadius) continue;
				auto* cell = tes->GetCell(at);
				float w = 0.0f;
				if (!cell || !cell->IsAttached() || !cell->GetWaterHeight(at, w) || !std::isfinite(w) || std::abs(w) > 1000000.0f) continue;
				// feet under the surface, but not far under (the boat's hidden carrier rides 220 down)
				if (at.z > w - 4.0f || at.z < w - 130.0f) { g.wadeLast.erase(a->GetFormID()); continue; }
				auto it = g.wadeLast.find(a->GetFormID());
				float speed = 0.0f;
				if (it != g.wadeLast.end() && dt > 0.0f) speed = std::hypot(at.x - it->second.x, at.y - it->second.y) / dt;
				g.wadeLast[a->GetFormID()] = at;
				if (speed > 2000.0f) continue;  // a teleport
				waders.push_back({ at, w, speed });
				if (speed < 12.0f) continue;
				float& acc = g.wadeAcc[a->GetFormID()];
				acc += dt * 32.0f * std::clamp(speed / 250.0f, 0.25f, 1.2f) * (g_set.density / 3.0f);
				while (acc >= 1.0f) {
					acc -= 1.0f;
					const float ang = Rand(0.0f, 6.2832f);
					const RE::NiPoint3 p{ at.x + std::cos(ang) * Rand(10.0f, 34.0f), at.y + std::sin(ang) * Rand(10.0f, 34.0f), w };
					const bool soft = Rand(0.0f, 1.0f) < 0.55f;
					Spawn(p, w, Rand(0.5f, 6.0f), Rand(2.0f, 9.0f), Rand(-2.5f, 2.5f), soft ? Rand(6.0f, 35.0f) : Rand(0.0f, 6.0f), Rand(3.0f, 10.0f), kDeep,
						RE::NiPoint3{ std::cos(ang), std::sin(ang), 0.0f } * Rand(10.0f, 30.0f), Rand(2.5f, 6.0f), soft ? Rand(3.0f, 6.0f) : Rand(2.0f, 4.0f), soft,
						soft ? Rand(0.45f, 0.8f) : 1.0f, Rand(1.0f, 5.0f));
				}
			}
			if (g.wadeLast.size() > 200) { g.wadeLast.clear(); g.wadeAcc.clear(); }
		}

		// the free glows
		if (g.root) {
			const auto& pw = g.root->parent ? g.root->parent->world : RE::NiTransform{};
			const RE::NiTransform inv = pw.Invert();
			g.root->local = inv;  // world space underneath
			int alive = 0;
			for (auto& p : g.free) {
				if (!p.node) continue;
				if (!p.live) continue;
				p.age += dt;
				// v1.5.4: the distance out is measured flat (the v1.5.3 check used the centre's height 0 against a camera 14000 down at
				// the Sea of Ghosts, so every open-water glow was dropped the frame it was made)
				if (p.age >= p.life || g.night <= 0.0f ||
					(p.amb && haveCam && std::hypot(p.centre.x - cam.x, p.centre.y - cam.y) > kAmbRadius * 1.25f)) {
					p.live = false;
					p.node->SetAppCulled(true);
					continue;
				}
				++alive;
				// v1.5.6: stirred by someone moving through: flare and get pushed away
				if (p.amb) {
					for (const auto& wd : waders) {
						const float dx = p.centre.x - wd.at.x, dy = p.centre.y - wd.at.y;
						const float d = std::sqrt(dx * dx + dy * dy);
						if (d > 150.0f) continue;
						const float f = 1.0f - d / 150.0f;
						const float move = std::clamp(wd.speed / 200.0f, 0.3f, 1.2f);
						p.boost = (std::min)(2.0f, p.boost + dt * 4.0f * f * move);
						if (d > 1.0f) p.pvel += RE::NiPoint3{ dx / d, dy / d, 0.0f } * (dt * 160.0f * f * move);
					}
					p.centre += p.pvel * dt;
					p.pvel = p.pvel * std::exp(-2.5f * dt);
					p.boost *= std::exp(-dt / 1.4f);
				}
				// v1.7.8: carried by the current (rivers, rapids), the shoreline ones held back by the shallows
				if (flowing) p.centre += Flow::At(p.centre, p.waterZ) * (dt * (p.shore ? 0.35f : 1.0f));
				const float te = p.age * p.pace;                                 // v1.5.2: its own pace (deep ones slower)
				p.ang += p.spin * p.pace * std::exp(-0.35f * te) * dt;          // the swirl slows
				const float r = p.r0 + p.grow * (1.0f - std::exp(-0.6f * te)) / 0.6f;
				const float depth = (std::min)(p.maxDepth, p.depth0 + p.sink * te);
				const float k = p.amb ? te / 8.0f : (1.0f - std::exp(-p.drag * te)) / p.drag;  // the throw dies away (open water: a steady drift)
				const float ch = p.turbA * (1.0f - std::exp(-te / 1.5f));       // v1.5.2: churn grows as the swirl breaks up
				RE::NiPoint3 at = p.centre + p.drift * k + RE::NiPoint3{ std::cos(p.ang) * r + ch * std::sin(p.turbW1 * te + p.turbP1),
					std::sin(p.ang) * r + ch * std::cos(p.turbW2 * te + p.turbP2), 0.0f };
				if (p.shore)  // v1.5.9: washing in and out with the water, creeping along the shore
					at += p.nrm * (p.washA * std::sin(p.washW * p.age + p.washPh)) + p.tan * (p.slide * p.age);
				const bool above = camZ > p.waterZ;
				// from above the surface hides what is under it: sparkles sit on it, the diffuse ones just on it (soft-edged, so
				// they melt into it) and spread wider the deeper they are; under water both are drawn at their depth
				at.z = above ? p.waterZ + (p.soft ? 0.4f : 0.8f) : p.waterZ - depth;
				const float d01 = depth / kDeep;
				const float under = p.soft ? (above ? 0.85f - 0.45f * d01 : 1.0f - 0.5f * d01) : (above ? 1.0f - 0.8f * d01 : 1.0f - 0.65f * d01);
				const float tw = 0.8f + 0.2f * std::sin(p.age * (p.amb ? 2.5f : 9.0f) + p.tw);  // open-water ones twinkle slowly
				float b = Bright(p) * under * tw * g.night * (1.0f + 1.8f * p.boost);
				if (!above && haveCam) {  // v1.5.15: under water they fade into the blue like everything else (the glow ignores fog)
					const float f = std::clamp(((at - cam).Length() - g_set.fogNear) / (g_set.fogFar - g_set.fogNear), 0.0f, 1.0f);
					b *= 1.0f - f * f * (3.0f - 2.0f * f);
				}
				float sc = p.size * std::sqrt((std::max)(0.0f, b)) * (p.soft ? 1.0f + 0.8f * d01 : 1.0f);
				// v1.5.3: farther ones drawn a little bigger so they still read as points of light, not lost pixels
				if (haveCam) sc *= std::clamp((at - cam).Length() / 700.0f, 1.0f, p.amb ? 7.0f : 4.0f);  // v1.5.12: open water up to 7x out at 7000
				// v1.5.3: never through the hull: one inside the boat's waterline outline is not drawn
				if (sc >= 0.08f && a_c.boat) {
					const auto& bw = a_c.boat->world;
					const float bs = bw.scale > 0.0f ? bw.scale : 1.0f;
					const RE::NiPoint3 l = bw.rotate.Transpose() * (at - bw.translate) / bs;
					if (std::abs(l.y) < 250.0f && std::abs(l.x) < HalfWidth(l.y) + 4.0f && l.z < kWaterline + 60.0f) sc = 0.0f;
				}
				if (sc < 0.08f) {
					p.node->SetAppCulled(true);
					continue;
				}
				p.node->SetAppCulled(false);
				p.node->local.translate = at;
				p.node->local.scale = sc;
			}
			RE::NiUpdateData data{};
			data.flags.set(RE::NiUpdateData::Flag::kDirty);
			g.root->Update(data);
		}

		// the clinging ones
		EnsureStuck(g.hull, kHull, g.hullParent, Active() ? a_c.boat : nullptr, false);
		for (int k = 0; k < 2; ++k) EnsureStuck(g.oar[k], kOarStick, g.oarParent[k], Active() ? a_c.oar[k] : nullptr, true);
		const float s01 = std::clamp(std::abs(a_c.speed) / 300.0f, 0.0f, 1.0f);
		auto tend = [&](Glow& s, float a_base, float a_tau, RE::NiAVObject* a_parent) {
			if (!s.node || !a_parent || s.node->parent != a_parent) return;
			s.energy = a_base + (s.energy - a_base) * std::exp(-dt / a_tau);
			s.tw += dt * Rand(2.0f, 5.0f);
			const float b = s.energy * (0.75f + 0.25f * std::sin(s.tw)) * g.night;
			const float sc = s.size * 0.9f * std::sqrt((std::max)(0.0f, b)) * g_set.size;
			const float ps = a_parent->world.scale > 0.0f ? a_parent->world.scale : 1.0f;
			if (sc < 0.08f) { s.node->SetAppCulled(true); return; }
			s.node->SetAppCulled(false);
			s.node->local.scale = sc / ps;
			RE::NiUpdateData data{};
			s.node->Update(data);
		};
		for (auto& s : g.hull) tend(s, 0.4f + 0.5f * s01, 12.0f, g.hullParent);  // v1.5.13: brighter at rest
		for (int k = 0; k < 2; ++k)
			for (auto& s : g.oar[k]) tend(s, 0.45f, 8.0f, g.oarParent[k]);  // v1.5.13: still glowing with the oar out of the water
	}

	inline std::string Command(const std::vector<std::string>& a_w)
	{
		LoadSettings();
		const std::string a = a_w.size() > 1 ? a_w[1] : "";
		auto num = [&](float a_def) { try { return a_w.size() > 2 ? std::stof(a_w[2]) : a_def; } catch (...) { return a_def; } };
		if (a == "on" || a == "off") g_set.on = a == "on";
		else if (a == "night") g_set.force = 1;
		else if (a == "auto") g_set.force = 0;
		else if (a == "never") g_set.force = -1;
		else if (a == "density") g_set.density = std::clamp(num(3.0f), 0.0f, 6.0f);
		else if (a == "size") g_set.size = std::clamp(num(0.5f), 0.2f, 4.0f);
		else if (a == "open") g_set.open = std::clamp(static_cast<int>(num(1800.0f)), 0, kAmbSharp + kAmbSoft);
		else if (a == "opensize") g_set.openSize = std::clamp(num(1.0f), 0.2f, 4.0f);
		else if (a == "shore") g_set.shore = std::clamp(static_cast<int>(num(400.0f)), 0, kShoreSharp + kShoreSoft);
		else if (!a.empty()) return "plankton on|off | night (always, to test by day) | auto (by the time of day) | \"plankton density 3\" (0-6) | \"plankton size 0.5\" | \"plankton open 1800\" (open-water count, 0-2000) | \"plankton opensize 1\" | \"plankton shore 400\" (shoreline count, 0-400)";
		if (!a.empty()) SaveSettings();
		return std::format("plankton {}, {}, density {:.2f}, size {:.2f}, open water {} size {:.2f}, shore {}, glow now {:.2f}", g_set.on ? "on" : "off",
			g_set.force > 0 ? "always (night forced)" : g_set.force < 0 ? "never" : "by the time of day", g_set.density, g_set.size, g_set.open, g_set.openSize, g_set.shore,
			g_set.on ? NightAmount() : 0.0f);
	}
}

// v1.5.14: our own underwater look for the camera. The game's only shows while the camera is just under the surface (it keys
// it to the player), and forcing the water system underwater crashed its culling (v1.5.8). This touches neither: while the
// camera itself is under the water, the image space manager's override base data points at our own (the same hook a
// photo mode uses), faded in and out over half a second, and given back as it was when the camera comes up.
// The look: a clear, cool teal, a little darker and less saturated, contrast up and bloom easier, so the water reads as
// deep but the plankton glows stand out of it instead of drowning in grey murk. No depth of field blur.
namespace Underwater
{
	inline bool g_on = true;
	inline bool g_loaded = false;
	inline float g_amt = 0.0f;           // 0 our look off, 1 full
	inline bool g_holding = false;       // the override is ours
	inline RE::ImageSpaceBaseData* g_prev = nullptr;  // what the override was before us
	inline RE::ImageSpaceBaseData g_base{};  // the game's look, last seen while we were not holding
	inline bool g_haveBase = false;
	inline RE::ImageSpaceBaseData g_ours{};
	// v1.5.15: the distance fog. Sky works its fog out from the weather every frame; while the camera is under we write
	// ours over it (at the player update and again at the camera update, right before drawing), blended in with g_amt.
	struct Fog { float nearD, farD, power, clamp; RE::NiColor cNear, cFar; };
	inline Fog g_skyFog{};       // the sky's own, as last worked out
	inline Fog g_wrote{};        // what we last wrote
	inline bool g_fogHeld = false;
	constexpr int kFogNearColour = 1, kFogFarColour = 12;  // TESWeather::ColorType kFogNear / kFogFar
	inline bool g_was = false;
	inline int g_logs = 0;

	inline void Load()
	{
		if (g_loaded) return;
		g_loaded = true;
		g_on = GetPrivateProfileIntA("Underwater", "Filter", 1, Plankton::IniPath().c_str()) != 0;
	}

	inline void Release(RE::ImageSpaceManager* a_ism)
	{
		if (!g_holding) return;
		auto& rd = a_ism->GetRuntimeData();
		if (rd.overrideBaseData == &g_ours) rd.overrideBaseData = g_prev;
		g_holding = false;
		g_prev = nullptr;
	}

	inline Fog ReadFog(RE::Sky* a_sky)
	{
		return { a_sky->fogNear, a_sky->fogFar, a_sky->fogPower, a_sky->fogClamp, a_sky->skyColor[kFogNearColour], a_sky->skyColor[kFogFarColour] };
	}
	inline bool Same(const Fog& a, const Fog& b)
	{
		return a.nearD == b.nearD && a.farD == b.farD && a.power == b.power && a.clamp == b.clamp;
	}

	// called at the player update (from Tend) and at the camera update
	inline void FogTend()
	{
		auto* sky = RE::Sky::GetSingleton();
		if (!sky) return;
		const Fog now = ReadFog(sky);
		if (!g_fogHeld || !Same(now, g_wrote)) g_skyFog = now;  // the sky worked its own out again since we wrote
		if (g_amt <= 0.0f) {
			if (g_fogHeld && Same(now, g_wrote)) {  // not worked out again yet: put the sky's back
				sky->fogNear = g_skyFog.nearD; sky->fogFar = g_skyFog.farD; sky->fogPower = g_skyFog.power; sky->fogClamp = g_skyFog.clamp;
				sky->skyColor[kFogNearColour] = g_skyFog.cNear; sky->skyColor[kFogFarColour] = g_skyFog.cFar;
			}
			g_fogHeld = false;
			return;
		}
		const float k = g_amt * g_amt * (3.0f - 2.0f * g_amt);
		auto mix = [k](float a, float b) { return a + (b - a) * k; };
		auto mixc = [&](const RE::NiColor& a, const RE::NiColor& b) { return RE::NiColor{ mix(a.red, b.red), mix(a.green, b.green), mix(a.blue, b.blue) }; };
		const float night = Plankton::NightAmount();
		const float lum = 1.0f - 0.75f * night;  // the deep blue is darker at night
		Fog f;
		f.nearD = mix(g_skyFog.nearD, Plankton::g_set.fogNear);
		f.farD = mix(g_skyFog.farD, Plankton::g_set.fogFar);
		f.power = mix(g_skyFog.power, 1.0f);
		f.clamp = mix(g_skyFog.clamp, 1.0f);
		f.cNear = mixc(g_skyFog.cNear, RE::NiColor{ 0.03f * lum, 0.20f * lum, 0.26f * lum });
		f.cFar = mixc(g_skyFog.cFar, RE::NiColor{ 0.02f * lum, 0.13f * lum, 0.19f * lum });
		sky->fogNear = f.nearD; sky->fogFar = f.farD; sky->fogPower = f.power; sky->fogClamp = f.clamp;
		sky->skyColor[kFogNearColour] = f.cNear;
		sky->skyColor[kFogFarColour] = f.cFar;
		g_wrote = ReadFog(sky);
		g_fogHeld = true;
	}

	inline void Tend(float a_dt)
	{
		Load();
		auto* ism = RE::ImageSpaceManager::GetSingleton();
		auto* pc = RE::PlayerCamera::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!ism || !pc || !tes || !pc->cameraRoot) return;
		const RE::NiPoint3 cam = pc->cameraRoot->world.translate;
		auto* cell = tes->GetCell(cam);
		float w = 0.0f;
		const bool known = cell && cell->IsAttached() && cell->GetWaterHeight(cam, w) && std::isfinite(w) && std::abs(w) < 1000000.0f;
		auto* ui = RE::UI::GetSingleton();
		const bool paused = ui && ui->GameIsPaused();
		const bool under = g_on && known && cam.z < w - 2.0f;
		if (under != g_was) {
			g_was = under;
			if (g_logs++ < 12) logger::info("Underwater: camera {} the water (camera z {:.0f}, water {:.0f}), our filter {}", under ? "under" : "out of", cam.z, w, g_on ? "on" : "off");
		}
		const float dt = paused ? 0.0f : std::clamp(a_dt, 0.0f, 0.1f);
		g_amt = std::clamp(g_amt + (under ? 1.0f : -1.0f) * dt / 0.5f, 0.0f, 1.0f);
		auto& rd = ism->GetRuntimeData();
		if (!g_holding) {  // the game's look, to fade from (whatever it is using now)
			const auto* cur = rd.overrideBaseData ? rd.overrideBaseData : rd.currentBaseData;
			if (cur && cur != &g_ours) { g_base = *cur; g_haveBase = true; }
		}
		FogTend();  // v1.5.15
		if (g_amt <= 0.0f || !g_haveBase) { Release(ism); return; }
		if (!g_holding) {
			g_prev = rd.overrideBaseData;
			g_holding = true;
		} else if (rd.overrideBaseData != &g_ours) {
			// someone else took the override (a weather, a spell, another mod): let them have it, and look again later
			g_holding = false;
			g_prev = nullptr;
			return;
		}
		const float k = g_amt * g_amt * (3.0f - 2.0f * g_amt);
		auto mix = [k](float a_from, float a_to) { return a_from + (a_to - a_from) * k; };
		g_ours = g_base;
		g_ours.hdr.bloomThreshold = mix(g_base.hdr.bloomThreshold, g_base.hdr.bloomThreshold * 0.6f);
		g_ours.hdr.bloomScale = mix(g_base.hdr.bloomScale, (std::max)(g_base.hdr.bloomScale * 1.8f, 0.6f));
		g_ours.hdr.receiveBloomThreshold = mix(g_base.hdr.receiveBloomThreshold, g_base.hdr.receiveBloomThreshold * 0.6f);
		g_ours.cinematic.saturation = mix(g_base.cinematic.saturation, g_base.cinematic.saturation * 0.7f);
		g_ours.cinematic.brightness = mix(g_base.cinematic.brightness, g_base.cinematic.brightness * 0.8f);
		g_ours.cinematic.contrast = mix(g_base.cinematic.contrast, g_base.cinematic.contrast * 1.15f);
		g_ours.tint.amount = mix(g_base.tint.amount, 0.38f);
		g_ours.tint.color.red = mix(g_base.tint.color.red, 0.06f);
		g_ours.tint.color.green = mix(g_base.tint.color.green, 0.36f);
		g_ours.tint.color.blue = mix(g_base.tint.color.blue, 0.48f);
		g_ours.depthOfField.strength = mix(g_base.depthOfField.strength, 0.0f);
		rd.overrideBaseData = &g_ours;
	}

	inline std::string Command(const std::string& a_arg)
	{
		Load();
		if (a_arg == "on" || a_arg == "off") {
			g_on = a_arg == "on";
			WritePrivateProfileStringA("Underwater", "Filter", g_on ? "1" : "0", Plankton::IniPath().c_str());
		}
		return std::format("underwater filter {} (camera under the water: {})", g_on ? "on" : "off", g_was ? "yes" : "no");
	}
}
