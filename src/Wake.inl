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
	// v1.7.43: the boat's own ripples in the water, as the oars make them (the game's ripples): one source at the tiller
	// (rudder) and the hull's body (the stem and both sides along the waterline); often while moving or turning, now and
	// then at rest as she bobs. Tuned with oas "num stern" (RowRim.ini [Wake] SternY = the tiller's place, SternSpread =
	// how far apart along the water while moving, SternZ = up / down, SternSize = the tiller's ripple size, SternStrength =
	// the hull's ripple size; 0 turns that source off). Drawn by TestHull::TendHullRipples.
	struct Stern
	{
		float y = -318.0f, spread = 22.0f, z = 0.0f, size = 1.0f, strength = 1.0f;
		bool loaded = false;
	};
	inline Stern g_stern;
	inline void LoadStern()
	{
		if (g_stern.loaded) return;
		g_stern.loaded = true;
		const auto ini = Plankton::IniPath();
		auto f = [&](const char* a_k, float a_def) {
			char b[32]{};
			GetPrivateProfileStringA(RowRimBakedIni::BoatSec("Wake"), a_k, "", b, sizeof(b), ini.c_str());  // v1.8.26: each boat its own
			try { return b[0] ? std::stof(b) : a_def; } catch (...) { return a_def; }
		};
		g_stern.y = f("SternY", g_stern.y);
		g_stern.spread = f("SternSpread", g_stern.spread);
		g_stern.z = f("SternZ", g_stern.z);
		g_stern.size = f("SternSize", g_stern.size);
		g_stern.strength = f("SternStrength", g_stern.strength);
	}
	inline void SaveStern()
	{
		const auto ini = Plankton::IniPath();
		auto w = [&](const char* a_k, float a_v) { WritePrivateProfileStringA(RowRimBakedIni::BoatSec("Wake"), a_k, std::format("{:.2f}", a_v).c_str(), ini.c_str()); };
		w("SternY", g_stern.y);
		w("SternSpread", g_stern.spread);
		w("SternZ", g_stern.z);
		w("SternSize", g_stern.size);
		w("SternStrength", g_stern.strength);
	}
	// v1.7.44: the hull's ripples get their own settings and numpad (oas "num hull"); "num stern" is the tiller only
	struct Hull
	{
		float size = 0.6f;    // ripple size (x the base)
		float out = 3.0f;     // how far outside the hull's side
		float every = 35.0f;  // travel between ripples at each spot (bigger = fewer)
		float z = 0.0f;       // up / down
		int count = 3;        // spots along each side (0-8), spread evenly from the bow thwart to the aft thwart
		bool stem = true;     // a ripple off the stem too
		bool rest = false;    // the odd ripple at rest
		bool loaded = false;
	};
	inline Hull g_hull;
	inline void LoadHull()
	{
		if (g_hull.loaded) return;
		g_hull.loaded = true;
		const auto ini = Plankton::IniPath();
		auto f = [&](const char* a_k, float a_def) {
			char b[32]{};
			GetPrivateProfileStringA(RowRimBakedIni::BoatSec("Wake"), a_k, "", b, sizeof(b), ini.c_str());  // v1.8.26: each boat its own
			try { return b[0] ? std::stof(b) : a_def; } catch (...) { return a_def; }
		};
		g_hull.size = f("HullSize", g_hull.size);
		g_hull.out = f("HullOut", g_hull.out);
		g_hull.every = f("HullEvery", g_hull.every);
		g_hull.z = f("HullZ", g_hull.z);
		g_hull.count = std::clamp(static_cast<int>(f("HullCount", static_cast<float>(g_hull.count))), 0, 8);
		g_hull.stem = f("HullStem", g_hull.stem ? 1.0f : 0.0f) != 0.0f;
		g_hull.rest = f("HullRest", g_hull.rest ? 1.0f : 0.0f) != 0.0f;
	}
	inline void SaveHull()
	{
		const auto ini = Plankton::IniPath();
		auto w = [&](const char* a_k, const std::string& a_v) { WritePrivateProfileStringA(RowRimBakedIni::BoatSec("Wake"), a_k, a_v.c_str(), ini.c_str()); };
		w("HullSize", std::format("{:.2f}", g_hull.size));
		w("HullOut", std::format("{:.1f}", g_hull.out));
		w("HullEvery", std::format("{:.0f}", g_hull.every));
		w("HullZ", std::format("{:.1f}", g_hull.z));
		w("HullCount", std::to_string(g_hull.count));
		w("HullStem", g_hull.stem ? "1" : "0");
		w("HullRest", g_hull.rest ? "1" : "0");
	}

	// the numpad: oas "num stern" (the tiller) or oas "num hull"; saved when a key is let go
	inline int g_mode = 0;  // 0 off, 1 tiller, 2 hull
	inline bool g_held[16]{};
	inline bool g_dirty = false;
	inline float g_hudT = 0.0f;
	inline int NumSlot(std::uint32_t a_id)
	{
		switch (a_id) {
		case 0x48: return 8; case 0x50: return 2; case 0x4B: return 4; case 0x4D: return 6;
		case 0x49: return 9; case 0x51: return 3; case 0x47: return 7; case 0x4F: return 1;
		case 0x4A: return 10; case 0x4E: return 11; case 0x52: return 0; case 0x53: return 12;
		default: return -1;
		}
	}
	inline bool NumOn() { return g_mode != 0; }
	inline void SaveMode()
	{
		if (!g_dirty) return;
		if (g_mode == 1) SaveStern();
		if (g_mode == 2) SaveHull();
		g_dirty = false;
	}
	inline std::string TillerText()
	{
		return std::format("tiller ripples: at {:.0f}, apart {:.0f}, height {:.1f}, size x{:.2f}", g_stern.y, g_stern.spread, g_stern.z, g_stern.size);
	}
	inline std::string HullText()
	{
		return std::format("hull ripples: size x{:.2f}, out {:.0f}, every {:.0f}, height {:.1f}, {} spots a side, stem {}, at rest {}", g_hull.size, g_hull.out,
			g_hull.every, g_hull.z, g_hull.count, g_hull.stem ? "on" : "off", g_hull.rest ? "on" : "off");
	}
	// a_mode 1 tiller, 2 hull, 0 off
	inline std::string NumCommand(int a_mode)
	{
		LoadStern();
		LoadHull();
		SaveMode();
		g_mode = a_mode;
		for (auto& h : g_held) h = false;
		if (a_mode == 1) return "numpad: " + TillerText() + ". 8/2 aft/forward, 4/6 closer/further apart, 9/3 up/down, 7/1 smaller/bigger (0 = off); oas \"num stern\" again to stop";
		if (a_mode == 2) return "numpad: " + HullText() + ". 7/1 smaller/bigger (0 = off), 4/6 closer to / further from the hull, 8/2 fewer/more often, 9/3 up/down, Num-/Num+ fewer/more spots, Num0 stem on/off, Num. at rest on/off; oas \"num hull\" again to stop";
		return "boat ripples numpad off";
	}
	inline void NumKey(std::uint32_t a_id, bool a_pressed)
	{
		if (!g_mode) return;
		const int k = NumSlot(a_id);
		if (k < 0) return;
		const bool was = g_held[k];
		g_held[k] = a_pressed;
		if (g_mode == 2 && a_pressed && !was) {  // steps and switches on the press
			bool changed = true;
			if (k == 10) g_hull.count = (std::max)(0, g_hull.count - 1);
			else if (k == 11) g_hull.count = (std::min)(8, g_hull.count + 1);
			else if (k == 0) g_hull.stem = !g_hull.stem;
			else if (k == 12) g_hull.rest = !g_hull.rest;
			else changed = false;
			if (changed) { g_dirty = true; RE::SendHUDMessage::ShowHUDMessage(HullText().c_str()); }
		}
		if (was && !a_pressed) SaveMode();
	}
	inline void TendNum(float a_dt)
	{
		if (!g_mode) return;
		bool changed = false;
		auto step = [&](float& a_v, int a_down, int a_up, float a_rate, float a_lo, float a_hi) {
			const float b = a_v;
			if (g_held[a_down]) a_v -= a_rate * a_dt;
			if (g_held[a_up]) a_v += a_rate * a_dt;
			a_v = std::clamp(a_v, a_lo, a_hi);
			if (a_v != b) changed = true;
		};
		if (g_mode == 1) {
			auto& s = g_stern;
			step(s.y, 8, 2, 15.0f, -400.0f, -150.0f);
			step(s.spread, 4, 6, 15.0f, 6.0f, 150.0f);
			step(s.z, 3, 9, 3.0f, -10.0f, 10.0f);
			step(s.size, 7, 1, 0.4f, 0.0f, 3.0f);
		} else {
			auto& h = g_hull;
			step(h.size, 7, 1, 0.3f, 0.0f, 3.0f);
			step(h.out, 4, 6, 6.0f, -5.0f, 40.0f);
			step(h.every, 2, 8, 20.0f, 8.0f, 300.0f);
			step(h.z, 3, 9, 3.0f, -10.0f, 10.0f);
		}
		if (changed) {
			g_dirty = true;
			if ((g_hudT -= a_dt) <= 0.0f) {
				g_hudT = 0.2f;
				RE::SendHUDMessage::ShowHUDMessage((g_mode == 1 ? TillerText() : HullText()).c_str());
			}
		}
	}

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

// v1.7.60: the oars' glowing swoosh, a ribbon instead of dots: streak pieces (meshes\RowRim\RowRim_Swoosh.nif, one flat
// additive quad on the water, white streak texture tinted here) laid every few units along each blade's sweep, turned
// along it. Every piece then turns about the stroke's catch point and drifts outward, so the ribbon curls and opens
// into a spiral, widening and fading. The catch itself throws a tight, bright spiral of them. Night only, like the
// plankton (the player's oars; the lake boats keep the sprite swoosh). See src\Oars_v1760_Ribbon.md.
namespace Ribbon
{
	constexpr const char* kModel = "RowRim\\RowRim_Swoosh.nif";
	constexpr int kPieces = 700;
	constexpr float kStep = 4.0f;  // a piece every this many units of the blade's sweep

	struct Piece
	{
		RE::NiPointer<RE::NiNode> node;
		RE::BSEffectShaderMaterial* mat = nullptr;
		bool live = false;
		RE::NiPoint3 c{};                                // the catch it turns about
		float r = 0.0f, ang = 0.0f, rot = 0.0f;         // where it is about c, and its turn off that angle
		float spin = 0.0f, out = 0.0f;                  // rad/s, units/s (both die away)
		float age = 0.0f, life = 1.0f, size = 1.0f, grow = 1.0f, bright = 1.0f, waterZ = 0.0f;
		RE::NiPoint3 drift{};
	};
	struct State
	{
		RE::NiPointer<RE::NiNode> src, root;
		Piece p[kPieces];
		int next = 0, made = 0, live = 0;
		bool failed = false;
		RE::NiPoint3 catchAt[2]{};
		bool haveCatch[2]{};
		float acc[2]{};
		int logs = 0;
	};
	inline State g;
	constexpr float kSpinDir[2] = { 1.0f, -1.0f };  // the two oars swirl opposite ways (as the plankton dig)

	inline RE::NiNode* MakeOne(RE::BSEffectShaderMaterial*& a_mat)
	{
		a_mat = nullptr;
		if (!Plankton::LoadModel(g.src, kModel)) { g.failed = true; return nullptr; }
		auto* obj = g.src->Clone();
		auto* n = obj ? obj->AsNode() : nullptr;
		if (!n) return nullptr;
		n->local = RE::NiTransform{};
		n->name = "RowRim_SwooshPiece";
		n->SetAppCulled(true);
		for (auto& c : n->GetChildren()) {  // its own material (as the wake pieces), so it fades on its own
			auto* geo = c ? c->AsGeometry() : nullptr;
			if (!geo) continue;
			auto* prop = netimmerse_cast<RE::BSEffectShaderProperty*>(geo->GetGeometryRuntimeData().shaderProperty.get());
			auto* mat = prop ? static_cast<RE::BSEffectShaderMaterial*>(prop->material) : nullptr;
			if (!mat) continue;
			auto* copy = static_cast<RE::BSEffectShaderMaterial*>(mat->Create());
			if (!copy) continue;
			copy->CopyMembers(mat);
			copy->baseColor.alpha = 0.999f - 0.0001f * static_cast<float>(g.made++ % 4000);
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
			auto* r = RE::NiNode::Create(static_cast<std::uint16_t>(kPieces));
			r->name = "RowRim_Swoosh";
			g.root = RE::NiPointer<RE::NiNode>(r);
			int made = 0;
			for (auto& p : g.p) {
				auto* n = MakeOne(p.mat);
				if (!n) break;
				p.node = RE::NiPointer<RE::NiNode>(n);
				r->AttachChild(n, true);
				++made;
			}
			logger::info("Swoosh: {} ribbon pieces made", made);
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

	inline Piece* Take()
	{
		for (int k = 0; k < kPieces; ++k) {
			auto& p = g.p[(g.next + k) % kPieces];
			if (!p.live && p.node) { g.next = (g.next + k + 1) % kPieces; return &p; }
		}
		Piece* best = nullptr;
		float most = -1.0f;
		for (auto& p : g.p) {
			const float f = p.age / (std::max)(0.01f, p.life);
			if (p.node && f > most) { most = f; best = &p; }
		}
		return best;
	}

	// a piece at a_at, turning about a_c, its streak a_dirAng (world, radians) at first
	inline void Spawn(int a_side, const RE::NiPoint3& a_c, const RE::NiPoint3& a_at, float a_dirAng, float a_waterZ, float a_life, float a_size, float a_grow,
		float a_bright, float a_spinMul, float a_outMul)
	{
		auto* p = Take();
		if (!p) return;
		p->live = true;
		p->c = a_c;
		p->r = std::hypot(a_at.x - a_c.x, a_at.y - a_c.y);
		p->ang = p->r > 0.01f ? std::atan2(a_at.y - a_c.y, a_at.x - a_c.x) : a_dirAng;
		p->rot = a_dirAng - p->ang;
		p->spin = kSpinDir[a_side] * Plankton::Rand(0.5f, 1.1f) * a_spinMul;
		p->out = Plankton::Rand(6.0f, 14.0f) * a_outMul;
		p->age = 0.0f;
		p->life = a_life;
		p->size = a_size;
		p->grow = a_grow;
		p->bright = a_bright;
		p->waterZ = a_waterZ;
		RE::NiPoint3 back = Plankton::g.boatVel * -1.0f;
		const float bl = back.Length();
		p->drift = bl > 5.0f ? back / bl * Plankton::Rand(4.0f, 12.0f) : RE::NiPoint3{};
	}

	// the catch (a_side 0 right, 1 left): a tight, bright spiral off the blade, and the stroke's turning point
	inline void Catch(int a_side, const RE::NiPoint3& a_at, float a_waterZ)
	{
		if (a_side < 0 || a_side > 1) return;
		g.catchAt[a_side] = a_at;
		g.haveCatch[a_side] = Plankton::StillWater(a_at, a_waterZ);  // v1.7.64: none in streams / rivers
		g.acc[a_side] = 0.0f;
		if (!g.haveCatch[a_side] || !Plankton::Active() || !EnsureRoot()) return;
		const float dir = kSpinDir[a_side];
		const float a0 = Plankton::Rand(0.0f, 6.2832f);
		constexpr int kArm = 28;
		for (int k = 0; k < kArm; ++k) {
			const float t = static_cast<float>(k) / static_cast<float>(kArm - 1);
			const float a = a0 + dir * t * 4.2f;  // about two thirds of a turn, opening out
			const float rr = 2.0f + 16.0f * t;
			const RE::NiPoint3 at = a_at + RE::NiPoint3{ std::cos(a) * rr, std::sin(a) * rr, 0.0f };
			Spawn(a_side, a_at, at, a + dir * 1.5708f, a_waterZ, Plankton::Rand(2.4f, 3.4f), 1.25f - 0.45f * t, Plankton::Rand(1.8f, 2.6f), 1.0f - 0.3f * t, 1.6f, 1.0f);
		}
		for (int k = 0; k < 3; ++k)  // the bright heart where the blade went in
			Spawn(a_side, a_at, a_at, Plankton::Rand(0.0f, 6.2832f), a_waterZ, Plankton::Rand(1.4f, 2.0f), 2.2f, 1.6f, 0.9f, 2.0f, 0.3f);
		if (g.logs++ < 6) logger::info("Swoosh: catch ({}) at ({:.0f},{:.0f}), {} pieces live", a_side == 0 ? "right" : "left", a_at.x, a_at.y, g.live);
	}

	// the blade sweeping through the water this frame: the ribbon laid along it
	inline void Sweep(int a_side, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_waterZ)
	{
		if (a_side < 0 || a_side > 1 || !g.haveCatch[a_side]) return;
		if (!Plankton::Active() || !EnsureRoot()) return;
		RE::NiPoint3 d = a_to - a_from;
		d.z = 0.0f;
		const float len = d.Length();
		if (len < 0.3f || len > 200.0f) return;  // still, or a cell hop
		const float dirAng = std::atan2(d.y, d.x);
		float& acc = g.acc[a_side];
		acc += len;
		for (int n = 0; acc >= kStep && n < 24; ++n) {
			acc -= kStep;
			const float f = std::clamp(1.0f - acc / len, 0.0f, 1.0f);
			const RE::NiPoint3 at = a_from + d * f;
			Spawn(a_side, g.catchAt[a_side], at, dirAng, a_waterZ, Plankton::Rand(2.0f, 3.0f), Plankton::Rand(0.9f, 1.2f), Plankton::Rand(1.6f, 2.4f),
				Plankton::Rand(0.55f, 0.75f), 1.0f, 1.0f);
		}
		if (acc > kStep) acc = 0.0f;
	}

	inline void Tick(float a_dt)
	{
		if (!g.root) return;
		const float dt = std::clamp(a_dt, 0.0f, 0.1f);
		const float night = Plankton::Active() ? Plankton::g.night : 0.0f;
		const float spinK = std::exp(-dt * 0.6f), outK = std::exp(-dt * 0.8f);
		int live = 0;
		for (auto& p : g.p) {
			if (!p.live) continue;
			p.age += dt;
			if (p.age >= p.life || night <= 0.0f) {
				p.live = false;
				if (p.node) p.node->SetAppCulled(true);
				continue;
			}
			++live;
			p.ang += p.spin * dt;
			p.r += p.out * dt;
			p.spin *= spinK;
			p.out *= outK;
			p.c += p.drift * dt;
			const float t = p.age / p.life;
			const float up = std::clamp(p.age / 0.08f, 0.0f, 1.0f);
			const float a = p.bright * up * std::pow(1.0f - t, 1.5f) * night;
			const float s = t * t * (3.0f - 2.0f * t);
			const float th = p.ang + p.rot;
			const RE::NiPoint3 dir{ std::cos(th), std::sin(th), 0.0f };
			RE::NiPoint3 at = p.c + RE::NiPoint3{ std::cos(p.ang), std::sin(p.ang), 0.0f } * p.r;
			at.z = p.waterZ + 0.6f;
			if (!p.node) continue;
			auto& m = p.node->local.rotate;  // local y along the streak (as Wake::Place)
			m.entry[0][0] = dir.y;  m.entry[0][1] = dir.x; m.entry[0][2] = 0.0f;
			m.entry[1][0] = -dir.x; m.entry[1][1] = dir.y; m.entry[1][2] = 0.0f;
			m.entry[2][0] = 0.0f;   m.entry[2][1] = 0.0f;  m.entry[2][2] = 1.0f;
			p.node->local.translate = at;
			p.node->local.scale = p.size * (1.0f + (p.grow - 1.0f) * s);
			if (p.mat) {
				p.mat->baseColor = RE::NiColorA{ 0.30f, 0.85f, 1.0f, a };
				p.mat->baseColorScale = 2.0f;
			}
			p.node->SetAppCulled(a < 0.005f);
		}
		g.live = live;
		if (live && !EnsureRoot()) return;
		RE::NiUpdateData data{};
		data.flags.set(RE::NiUpdateData::Flag::kDirty);
		g.root->Update(data);
	}
}
