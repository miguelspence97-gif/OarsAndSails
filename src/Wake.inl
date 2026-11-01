// v1.5.16: the boat's own wake: a real V trailing behind the player's boat as far as the plankton wake (about 9 s), from
// the hull's shape: the two arms leave the bow at the Kelvin angle (19.5 deg: each arm moves out 0.354 of the distance
// it falls behind) and run past both sides of the hull, and a churned wash spreads behind the stern. The game's ripples
// are a 2D trick drawn into the water's normals; these are meshes lying on the water: flat quads (meshes\RowRim\
// RowRim_WakeCrest.nif / RowRim_WakeWash.nif, alpha blended, white textures) placed along spots the bow and stern
// passed, each turned along its arm, widening as it ages and fading out. Each piece has its own material so it can fade
// on its own; the colour follows the time of day (white foam by day, a dim blue-green at night).
// The spots are fixed in the world once laid, so the V bends with the boat's turns like a real wake.
namespace Wake
{
	constexpr const char* kCrestModel = "RowRim\\RowRim_WakeCrest.nif";
	constexpr const char* kWashModel = "RowRim\\RowRim_WakeWash.nif";
	constexpr int kSamples = 140;       // spots kept (one every kSpacing of travel)
	constexpr float kSpacing = 30.0f;
	constexpr float kLife = 9.0f;        // as long as the plankton wake lasts
	constexpr float kWashLife = 6.5f;
	constexpr float kKelvin = 0.354f;    // tan 19.5 deg
	constexpr float kEnd = 236.0f;       // the boat model's ends (model y)
	constexpr bool kArms = false;        // v1.5.18: the V arms are off for now (the stern trail only)
	constexpr float kWashScale = 1.9f;   // v1.5.18: the wash patch (120 across, its soft edge ~75% seen) as wide as the hull (173)

	struct Sample
	{
		bool live = false;
		RE::NiPoint3 bow{}, stern{}, fwd{};
		float speed = 0.0f, t = 0.0f, waterZ = 0.0f;
		float rot = 0.0f, jit = 1.0f;  // v1.5.17: the wash patch's own turn and size (no two alike)
		RE::NiPoint3 off{};
	};
	struct Piece
	{
		RE::NiPointer<RE::NiNode> node;
		RE::BSEffectShaderMaterial* mat = nullptr;
	};
	struct State
	{
		RE::NiPointer<RE::NiNode> src[2];
		RE::NiPointer<RE::NiNode> root;
		Piece arm[2][kSamples];
		Piece wash[kSamples];
		Sample s[kSamples];
		int head = 0;              // the newest sample
		bool haveLast = false;
		RE::NiPoint3 lastAt{}, vel{}, lastLaid{};
		bool laidAny = false;
		float time = 0.0f;
		bool failed = false;
		int logs = 0;
		int made = 0;
	};
	inline State g;

	inline bool On() { return Plankton::g_set.wake; }

	inline RE::NiNode* MakeOne(int a_kind, RE::BSEffectShaderMaterial*& a_mat)
	{
		a_mat = nullptr;
		auto& src = g.src[a_kind];
		if (!Plankton::LoadModel(src, a_kind == 0 ? kCrestModel : kWashModel)) { g.failed = true; return nullptr; }
		auto* obj = src->Clone();
		auto* n = obj ? obj->AsNode() : nullptr;
		if (!n) return nullptr;
		n->local = RE::NiTransform{};
		n->name = "RowRim_WakePiece";
		n->SetAppCulled(true);
		// its own material (as po3's texture swaps do), so it fades on its own
		for (auto& c : n->GetChildren()) {
			auto* geo = c ? c->AsGeometry() : nullptr;
			if (!geo) continue;
			auto* prop = netimmerse_cast<RE::BSEffectShaderProperty*>(geo->GetGeometryRuntimeData().shaderProperty.get());
			auto* mat = prop ? static_cast<RE::BSEffectShaderMaterial*>(prop->material) : nullptr;
			if (!mat) continue;
			auto* copy = static_cast<RE::BSEffectShaderMaterial*>(mat->Create());
			if (!copy) continue;
			copy->CopyMembers(mat);
			copy->baseColor.alpha = 0.999f - 0.0001f * static_cast<float>(g.made++ % 4000);  // never the same as another (no sharing)
			prop->SetMaterial(copy, true);
			copy->~BSEffectShaderMaterial();
			RE::free(copy);
			a_mat = static_cast<RE::BSEffectShaderMaterial*>(prop->material);
			break;
		}
		return n;
	}

	inline bool EnsureRoot()
	{
		if (g.failed) return false;
		auto* cell = Plankton::CellNode();
		if (!cell) return false;
		if (!g.root) {
			auto* r = RE::NiNode::Create(static_cast<std::uint16_t>(kSamples * 3));
			r->name = "RowRim_Wake";
			g.root = RE::NiPointer<RE::NiNode>(r);
			int made = 0, mats = 0;
			for (int i = 0; i < kSamples && !g.failed; ++i) {
				Piece* ps[3] = { &g.arm[0][i], &g.arm[1][i], &g.wash[i] };
				for (int k = 0; k < 3; ++k) {
					auto* n = MakeOne(k == 2 ? 1 : 0, ps[k]->mat);
					if (!n) break;
					ps[k]->node = RE::NiPointer<RE::NiNode>(n);
					r->AttachChild(n, true);
					++made;
					if (ps[k]->mat) ++mats;
				}
			}
			logger::info("Wake: {} pieces made, {} with their own material", made, mats);
			if (g.failed) return false;
		}
		if (g.root->parent != cell) {
			if (auto* old = g.root->parent) {
				RE::NiPointer<RE::NiAVObject> keep;
				old->DetachChild(g.root.get(), keep);
			}
			cell->AttachChild(g.root.get(), true);
		}
		return true;
	}

	inline void Place(Piece& a_p, const RE::NiPoint3& a_at, const RE::NiPoint3& a_dir, float a_scale, float a_alpha, const RE::NiColor& a_col)
	{
		if (!a_p.node) return;
		if (a_alpha < 0.01f || a_scale <= 0.0f) { a_p.node->SetAppCulled(true); return; }
		RE::NiPoint3 d{ a_dir.x, a_dir.y, 0.0f };
		const float l = d.Length();
		d = l > 0.001f ? d / l : RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
		auto& m = a_p.node->local.rotate;
		m.entry[0][0] = d.y;  m.entry[0][1] = d.x; m.entry[0][2] = 0.0f;   // local x: to the right of the arm, local y: along it
		m.entry[1][0] = -d.x; m.entry[1][1] = d.y; m.entry[1][2] = 0.0f;
		m.entry[2][0] = 0.0f; m.entry[2][1] = 0.0f; m.entry[2][2] = 1.0f;
		a_p.node->local.translate = a_at;
		a_p.node->local.scale = a_scale;
		if (a_p.mat) {
			a_p.mat->baseColor = RE::NiColorA{ a_col.red, a_col.green, a_col.blue, a_alpha };
			a_p.mat->baseColorScale = 1.0f;
		}
		a_p.node->SetAppCulled(false);
	}

	inline void Tick(float a_dt, const Plankton::Ctx& a_c)
	{
		const float dt = std::clamp(a_dt, 0.0f, 0.1f);
		g.time += dt;
		const bool on = On() && !g.failed;
		if (!on && !g.root) return;

		// lay a spot every kSpacing the boat moves through the water
		if (on && a_c.boat && a_c.water && EnsureRoot()) {
			const auto& w = a_c.boat->world;
			const RE::NiPoint3 at = w.translate;
			if (g.haveLast && dt > 0.0f) {
				const RE::NiPoint3 v = (at - g.lastAt) / dt;
				if (v.Length() < 2000.0f) g.vel = g.vel + (v - g.vel) * (std::min)(1.0f, 6.0f * dt);
				else g.laidAny = false;  // a jump: start the trail over
			}
			g.lastAt = at;
			g.haveLast = true;
			RE::NiPoint3 hv{ g.vel.x, g.vel.y, 0.0f };
			const float speed = hv.Length();
			if (speed > 25.0f) {
				const float lead = (w.rotate.Transpose() * hv).y >= 0.0f ? 1.0f : -1.0f;
				RE::NiPoint3 bow = w * RE::NiPoint3{ 0.0f, lead * kEnd, Plankton::kWaterline };
				RE::NiPoint3 stern = w * RE::NiPoint3{ 0.0f, -lead * (kEnd - 10.0f), Plankton::kWaterline };
				bow.z = stern.z = a_c.waterZ;
				const float moved = g.laidAny ? std::hypot(bow.x - g.lastLaid.x, bow.y - g.lastLaid.y) : 1.0e9f;
				if (moved >= kSpacing) {
					g.head = (g.head + 1) % kSamples;
					auto& s = g.s[g.head];
					s.live = true;
					s.bow = bow;
					s.stern = stern;
					s.fwd = hv / speed;
					s.speed = speed;
					s.t = g.time;
					s.waterZ = a_c.waterZ;
					s.rot = Plankton::Rand(0.0f, 6.2832f);
					s.jit = Plankton::Rand(0.9f, 1.1f);
					s.off = RE::NiPoint3{ Plankton::Rand(-12.0f, 12.0f), Plankton::Rand(-12.0f, 12.0f), 0.0f };
					g.lastLaid = bow;
					g.laidAny = true;
					if (g.logs < 3) { ++g.logs; logger::info("Wake: laying the trail (speed {:.0f})", speed); }
				}
			} else {
				g.laidAny = false;
			}
		} else {
			g.haveLast = false;
			g.laidAny = false;
		}
		if (!g.root) return;

		// the colour: v1.5.17: the arms a faint grey-green sheen on the water (not white), the wash pale foam; dim at night
		const float night = Plankton::NightAmount();
		auto lerpc = [night](float a_day, float a_night) { return a_day + (a_night - a_day) * night; };
		const RE::NiColor col{ lerpc(0.55f, 0.08f), lerpc(0.63f, 0.18f), lerpc(0.66f, 0.21f) };
		const RE::NiColor washCol{ lerpc(0.88f, 0.14f), lerpc(0.92f, 0.28f), lerpc(0.93f, 0.32f) };
		const float dayA = 1.0f - 0.35f * night;

		// the arms' spots this frame (newest first), to turn each piece along its arm
		RE::NiPoint3 armAt[2][kSamples];
		bool armOk[kSamples]{};
		for (int i = 0; i < kSamples; ++i) {
			auto& s = g.s[i];
			if (!s.live) continue;
			const float age = g.time - s.t;
			if (age > kLife || !on) { s.live = false; continue; }
			const RE::NiPoint3 right{ s.fwd.y, -s.fwd.x, 0.0f };
			const float out = (std::min)(kKelvin * s.speed * age, 1100.0f);
			armAt[0][i] = s.bow + right * out;
			armAt[1][i] = s.bow - right * out;
			armOk[i] = true;
		}
		// the player's boat now (pieces under it are not drawn)
		const RE::NiTransform* bw = a_c.boat ? &a_c.boat->world : nullptr;
		auto underBoat = [&](const RE::NiPoint3& a_p, float a_pad) {
			if (!bw) return false;
			const float bs = bw->scale > 0.0f ? bw->scale : 1.0f;
			const RE::NiPoint3 l = bw->rotate.Transpose() * (RE::NiPoint3{ a_p.x, a_p.y, bw->translate.z } - bw->translate) / bs;
			return std::abs(l.y) < 245.0f && std::abs(l.x) < Plankton::HalfWidth(l.y) + a_pad;
		};
		for (int i = 0; i < kSamples; ++i) {
			auto& s = g.s[i];
			if (!armOk[i]) {
				for (int k = 0; k < 2; ++k) if (g.arm[k][i].node) g.arm[k][i].node->SetAppCulled(true);
				if (g.wash[i].node) g.wash[i].node->SetAppCulled(true);
				continue;
			}
			const float age = g.time - s.t;
			const float s01 = std::clamp((s.speed - 25.0f) / 275.0f, 0.0f, 1.0f);
			const float in = std::clamp(age / 0.4f, 0.0f, 1.0f);
			// the newer and older neighbours along the trail (for each arm's direction)
			const int newer = (i + 1) % kSamples, older = (i + kSamples - 1) % kSamples;
			for (int k = 0; k < 2; ++k) {
				RE::NiPoint3 dir;
				if (armOk[older] && i != g.head) dir = armAt[k][newer] - armAt[k][older];
				else if (armOk[older]) dir = armAt[k][i] - armAt[k][older];
				else if (i != g.head && armOk[newer]) dir = armAt[k][newer] - armAt[k][i];
				else dir = s.fwd + RE::NiPoint3{ s.fwd.y, -s.fwd.x, 0.0f } * (k == 0 ? -kKelvin : kKelvin);
				if (i == g.head || !armOk[newer] || (armAt[k][newer] - armAt[k][i]).Length() > 400.0f) {
					dir = s.fwd + RE::NiPoint3{ s.fwd.y, -s.fwd.x, 0.0f } * (k == 0 ? -kKelvin : kKelvin);
				}
				const float f = age / kLife;
				float a = in * (1.0f - f) * (1.0f - f) * (0.06f + 0.16f * s01) * dayA;  // v1.5.17: a faint V
				RE::NiPoint3 p = armAt[k][i];
				p.z = s.waterZ + 1.0f;
				if (underBoat(p, 18.0f) || !kArms) a = 0.0f;
				Place(g.arm[k][i], p, dir, (std::min)(1.0f + 0.25f * age, 3.0f), a, col);
			}
			// the stern's churned wash
			{
				const float f = std::clamp(age / kWashLife, 0.0f, 1.0f);
				float a = in * std::pow(1.0f - f, 1.5f) * (0.4f + 0.45f * s01) * dayA;  // v1.5.19: stronger (was 0.18 + 0.3)
				RE::NiPoint3 p = s.stern + s.off;
				p.z = s.waterZ + 1.3f;
				if (underBoat(p, 4.0f)) a = 0.0f;
				const RE::NiPoint3 d{ std::cos(s.rot), std::sin(s.rot), 0.0f };  // v1.5.17: each patch turned its own way, so the trail never repeats
				Place(g.wash[i], p, d, (std::min)(kWashScale + 0.05f * age, kWashScale * 1.25f) * s.jit, a, washCol);
			}
		}
		const auto& pw = g.root->parent ? g.root->parent->world : RE::NiTransform{};
		g.root->local = pw.Invert();  // world space underneath
		RE::NiUpdateData data{};
		data.flags.set(RE::NiUpdateData::Flag::kDirty);
		g.root->Update(data);
	}
}
