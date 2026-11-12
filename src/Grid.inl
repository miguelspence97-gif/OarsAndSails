// v1.7.20: cell grid (oas "grid on|off"): see the exterior world cells (4096 x 4096) you pass through. Each cell edge near you
// is a faint see-through wall (meshes\RowRim\RowRim_CellGrid.nif: a 4096 x 800 quad, alpha blended, both sides, texture
// textures\RowRim\cell_grid.dds: a light fill, a bright line at your height and bright posts at the cell corners). The
// walls box the 5 x 5 cells round yours (about 10000 out each way), stand from 300 under you to 500 over you, fade out
// with distance, and your own cell's four walls are drawn brightest. Crossing into another cell shows its grid
// coordinates on the HUD. Saved in RowRim.ini [Debug] CellGrid.
namespace CellGrid
{
	constexpr const char* kModel = "RowRim\\RowRim_CellGrid.nif";
	constexpr float kCell = 4096.0f;
	constexpr int kRadius = 2;                         // cells each way round yours
	constexpr int kLines = 2 * kRadius + 2;            // grid lines each way (6)
	constexpr int kSegs = 2 * kRadius + 1;             // wall segments along a line (5)
	constexpr int kWalls = 2 * kLines * kSegs;         // 60
	constexpr float kBelow = 300.0f;                   // the wall's foot under the player
	constexpr float kFadeFrom = 7000.0f, kFadeTo = 11000.0f;

	struct Piece
	{
		RE::NiPointer<RE::NiNode> node;
		RE::BSEffectShaderMaterial* mat = nullptr;
	};
	struct State
	{
		bool loaded = false, on = false, failed = false;
		RE::NiPointer<RE::NiNode> src, root;
		Piece wall[kWalls];
		int made = 0;
		int lastX = 0x7fffffff, lastY = 0x7fffffff;
	};
	inline State g;

	inline std::string Ini() { return Plankton::IniPath(); }

	inline RE::NiNode* MakeOne(RE::BSEffectShaderMaterial*& a_mat)
	{
		a_mat = nullptr;
		if (!Plankton::LoadModel(g.src, kModel)) { g.failed = true; return nullptr; }
		auto* obj = g.src->Clone();
		auto* n = obj ? obj->AsNode() : nullptr;
		if (!n) return nullptr;
		n->local = RE::NiTransform{};
		n->name = "RowRim_CellWall";
		n->SetAppCulled(true);
		for (auto& c : n->GetChildren()) {  // its own material, so each wall fades on its own (as the wake pieces)
			auto* geo = c ? c->AsGeometry() : nullptr;
			if (!geo) continue;
			auto* prop = netimmerse_cast<RE::BSEffectShaderProperty*>(geo->GetGeometryRuntimeData().shaderProperty.get());
			auto* mat = prop ? static_cast<RE::BSEffectShaderMaterial*>(prop->material) : nullptr;
			if (!mat) continue;
			auto* copy = static_cast<RE::BSEffectShaderMaterial*>(mat->Create());
			if (!copy) continue;
			copy->CopyMembers(mat);
			copy->baseColor.alpha = 0.998f - 0.0001f * static_cast<float>(g.made++ % 4000);
			prop->SetMaterial(copy, true);
			copy->~BSEffectShaderMaterial();
			RE::free(copy);
			a_mat = static_cast<RE::BSEffectShaderMaterial*>(prop->material);
			break;
		}
		return n;
	}

	inline bool EnsureRoot(RE::NiNode* a_cell)
	{
		if (g.failed || !a_cell) return false;
		if (!g.root) {
			auto* r = RE::NiNode::Create(static_cast<std::uint16_t>(kWalls));
			r->name = "RowRim_CellGrid";
			g.root = RE::NiPointer<RE::NiNode>(r);
			for (auto& w : g.wall) {
				auto* n = MakeOne(w.mat);
				if (!n) break;
				w.node = RE::NiPointer<RE::NiNode>(n);
				r->AttachChild(n, true);
			}
			logger::info("Cell grid: {} walls made", kWalls);
			if (g.failed) return false;
		}
		if (g.root->parent != a_cell) {
			if (auto* old = g.root->parent) {
				RE::NiPointer<RE::NiAVObject> keep;
				old->DetachChild(g.root.get(), keep);
			}
			a_cell->AttachChild(g.root.get(), true);
		}
		return true;
	}

	inline void HideAll()
	{
		for (auto& w : g.wall) if (w.node) w.node->SetAppCulled(true);
	}

	inline void Load()
	{
		g.loaded = true;
		g.on = GetPrivateProfileIntA("Debug", "CellGrid", 0, Ini().c_str()) != 0;
	}

	inline void Set(bool a_on)
	{
		if (!g.loaded) Load();
		g.on = a_on;
		WritePrivateProfileStringA("Debug", "CellGrid", a_on ? "1" : "0", Ini().c_str());
		if (!a_on) HideAll();
		g.lastX = g.lastY = 0x7fffffff;
	}
	inline bool On()
	{
		if (!g.loaded) Load();
		return g.on;
	}

	inline int CellOf(float a_v) { return static_cast<int>(std::floor(a_v / kCell)); }

	// one wall: from a at (x, y) along +x (a_alongX) or +y, its foot at z
	inline void Place(Piece& a_p, float a_x, float a_y, float a_z, bool a_alongX, float a_alpha)
	{
		if (!a_p.node) return;
		if (a_alpha < 0.02f) { a_p.node->SetAppCulled(true); return; }
		auto& m = a_p.node->local.rotate;
		if (a_alongX) {
			m = RE::NiMatrix3{};
			m.entry[0][0] = 1.0f; m.entry[1][1] = 1.0f; m.entry[2][2] = 1.0f;
		} else {  // turned 90 deg: model x (its length) along world y
			m = RE::NiMatrix3{};
			m.entry[0][1] = -1.0f; m.entry[1][0] = 1.0f; m.entry[2][2] = 1.0f;
		}
		a_p.node->local.translate = RE::NiPoint3{ a_x, a_y, a_z };
		a_p.node->local.scale = 1.0f;
		if (a_p.mat) {
			a_p.mat->baseColor = RE::NiColorA{ 1.0f, 1.0f, 1.0f, a_alpha };
			a_p.mat->baseColorScale = 1.0f;
		}
		a_p.node->SetAppCulled(false);
	}

	inline void Tick()
	{
		if (!On()) return;
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!player || !cell || !cell->IsExteriorCell() || !cell->IsAttached()) { HideAll(); return; }
		if (!EnsureRoot(Plankton::CellNode())) return;
		const RE::NiPoint3 p = player->GetPosition();
		const int cx = CellOf(p.x), cy = CellOf(p.y);
		if (cx != g.lastX || cy != g.lastY) {
			if (g.lastX != 0x7fffffff) RE::SendHUDMessage::ShowHUDMessage(std::format("Cell {}, {}", cx, cy).c_str());
			logger::info("Cell grid: player in cell {}, {}", cx, cy);
			g.lastX = cx;
			g.lastY = cy;
		}
		const float z = p.z - kBelow;
		auto fade = [&](float a_mx, float a_my) {
			const float d = std::hypot(a_mx - p.x, a_my - p.y);
			return std::clamp(1.0f - (d - kFadeFrom) / (kFadeTo - kFadeFrom), 0.0f, 1.0f);
		};
		int k = 0;
		// lines of constant y (walls along x), then lines of constant x (walls along y)
		for (int dir = 0; dir < 2; ++dir) {
			for (int li = 0; li < kLines; ++li) {
				const int line = (dir == 0 ? cy : cx) - kRadius + li;
				for (int si = 0; si < kSegs; ++si) {
					const int seg = (dir == 0 ? cx : cy) - kRadius + si;
					const float a = static_cast<float>(seg) * kCell;   // the wall's start along its length
					const float c = static_cast<float>(line) * kCell;  // the line's place across
					const bool own = dir == 0 ? (seg == cx && (line == cy || line == cy + 1)) : (seg == cy && (line == cx || line == cx + 1));
					const float alpha = (own ? 1.0f : 0.55f) * (dir == 0 ? fade(a + kCell * 0.5f, c) : fade(c, a + kCell * 0.5f));
					if (dir == 0) Place(g.wall[k], a, c, z, true, alpha);
					else Place(g.wall[k], c, a, z, false, alpha);
					++k;
				}
			}
		}
		RE::NiUpdateData data{};
		data.flags.set(RE::NiUpdateData::Flag::kDirty);
		g.root->Update(data);
	}
}
