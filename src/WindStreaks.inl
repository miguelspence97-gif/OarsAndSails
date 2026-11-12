// v1.8.39: wind streaks (Sea of Thieves style): thin white lines in the air that blow past with our own wind (OwnWind),
// weaving a little, some curling over in a loop, while you are aboard a boat. Each streak is a trail of flat pieces
// (meshes\RowRim\RowRim_WindStreak.nif, one additive quad 4 x 16, a soft line texture of its own) laid along where its
// head has been, each turned to face the camera about its own length. More and faster in a stronger wind; dimmer at
// night. RowRim.ini [Wind] Streaks (0 off, 1 default, 2 twice as many), the Wind page. See src\Wind_v1839_Streaks.md.
namespace WindStreaks
{
	constexpr const char* kModel = "RowRim\\RowRim_WindStreak.nif";
	constexpr int kStreaks = 16;
	constexpr int kSegs = 20;                        // pieces a streak
	constexpr float kStep = 16.0f;                   // a piece's length (the quad's)
	constexpr int kHist = kSegs + 1;

	struct Streak
	{
		bool live = false;
		RE::NiPoint3 head{};
		RE::NiPoint3 hist[kHist]{};                  // where the head was, every kStep, newest first
		int nh = 0;
		float acc = 0.0f;
		float heading = 0.0f, climb = 0.0f, speed = 0.0f, age = 0.0f, life = 1.0f;
		float weaveA = 0.0f, weaveF = 0.0f, weaveP = 0.0f;
		float curlAt = -1.0f, curlDur = 0.7f, curlDir = 1.0f;
		float bright = 1.0f;
	};
	struct Piece
	{
		RE::NiPointer<RE::NiNode> node;
		RE::BSEffectShaderMaterial* mat = nullptr;
	};
	struct State
	{
		RE::NiPointer<RE::NiNode> src, root;
		Streak s[kStreaks];
		Piece p[kStreaks * kSegs];
		bool failed = false, loaded = false;
		float amount = 1.0f, vis = 0.0f, spawnAcc = 0.0f;
		RE::NiPoint3 lastEye{}, eyeVel{};  // v1.8.39b: how fast you (the boat) are going, so streaks are put where you will see them
		bool haveEye = false;
		int made = 0, logs = 0;
	};
	inline State g;

	inline void Load()
	{
		if (g.loaded) return;
		g.loaded = true;
		char b[32]{};
		GetPrivateProfileStringA("Wind", "Streaks", "", b, sizeof(b), Plankton::IniPath().c_str());
		try { g.amount = b[0] ? std::stof(b) : 1.0f; } catch (...) { g.amount = 1.0f; }
		g.amount = std::clamp(g.amount, 0.0f, 2.0f);
		logger::info("Wind streaks: {:.2f} (RowRim.ini [Wind] Streaks)", g.amount);
	}
	inline void SetAmount(float a_v)
	{
		g.loaded = true;
		g.amount = std::clamp(a_v, 0.0f, 2.0f);
		WritePrivateProfileStringA("Wind", "Streaks", std::format("{:.2f}", g.amount).c_str(), Plankton::IniPath().c_str());
	}

	inline RE::NiNode* MakeOne(RE::BSEffectShaderMaterial*& a_mat)
	{
		a_mat = nullptr;
		if (!Plankton::LoadModel(g.src, kModel)) { g.failed = true; return nullptr; }
		auto* obj = g.src->Clone();
		auto* n = obj ? obj->AsNode() : nullptr;
		if (!n) return nullptr;
		n->local = RE::NiTransform{};
		n->name = "RowRim_WindStreakPiece";
		n->SetAppCulled(true);
		for (auto& c : n->GetChildren()) {  // its own material (as the swoosh pieces), so each fades on its own
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
			auto* r = RE::NiNode::Create(static_cast<std::uint16_t>(kStreaks * kSegs));
			r->name = "RowRim_WindStreaks";
			g.root = RE::NiPointer<RE::NiNode>(r);
			int made = 0;
			for (auto& p : g.p) {
				auto* n = MakeOne(p.mat);
				if (!n) break;
				p.node = RE::NiPointer<RE::NiNode>(n);
				r->AttachChild(n, true);
				++made;
			}
			logger::info("Wind streaks: {} pieces made{}", made, g.failed ? " (no meshes\\RowRim\\RowRim_WindStreak.nif)" : "");
			if (g.failed) return false;
		}
		if (g.root->parent != cell) {
			if (auto* old = g.root->parent) {
				RE::NiPointer<RE::NiAVObject> keep;
				old->DetachChild(g.root.get(), keep);
			}
			cell->AttachChild(g.root.get(), true);
			for (auto& s : g.s) s.live = false;  // a new cell: start afresh
		}
		return true;
	}

	inline void HideAll()
	{
		for (auto& s : g.s) s.live = false;
		for (auto& p : g.p) if (p.node) p.node->SetAppCulled(true);
	}

	// v1.8.39b: a new streak where you will see it: most of them aimed to cross your view half way through their life
	// (where you are looking then, the boat's way allowed for), the rest anywhere round you, mostly upwind. Placed only by
	// where you were they blew past behind you running before the wind (you kept pace with them) and were seen only
	// looking astern.
	inline void Spawn(const RE::NiPoint3& a_eye, const RE::NiPoint3& a_fwd, const RE::NiPoint3& a_wdir, float a_wsp, float a_waterZ)
	{
		Streak* s = nullptr;
		for (auto& q : g.s) if (!q.live) { s = &q; break; }
		if (!s) return;
		const RE::NiPoint3 side{ a_wdir.y, -a_wdir.x, 0.0f };
		s->speed = Plankton::Rand(0.85f, 1.15f) * (320.0f + 480.0f * a_wsp);
		s->life = Plankton::Rand(2.4f, 3.8f);
		if (Plankton::Rand(0.0f, 1.0f) < 0.8f) {
			RE::NiPoint3 f{ a_fwd.x, a_fwd.y, 0.0f };
			const float fl = f.Length();
			f = fl > 0.1f ? f / fl : a_wdir;
			const RE::NiPoint3 r{ f.y, -f.x, 0.0f };
			const float tm = s->life * Plankton::Rand(0.35f, 0.55f);      // when it crosses the view
			const float d = Plankton::Rand(250.0f, 1100.0f);
			RE::NiPoint3 x = a_eye + g.eyeVel * tm + f * d + r * (d * Plankton::Rand(-0.7f, 0.7f));
			x.z = a_waterZ + Plankton::Rand(40.0f, 320.0f);
			s->head = x - a_wdir * (s->speed * tm);                       // blown there by then
		} else {
			s->head = a_eye + a_wdir * Plankton::Rand(-1100.0f, 200.0f) + side * Plankton::Rand(-750.0f, 750.0f);
			s->head.z = a_waterZ + Plankton::Rand(40.0f, 320.0f);
		}
		s->heading = std::atan2(a_wdir.x, a_wdir.y) + Plankton::Rand(-0.12f, 0.12f);
		s->climb = Plankton::Rand(-0.04f, 0.06f);
		s->weaveA = Plankton::Rand(0.05f, 0.18f);
		s->weaveF = Plankton::Rand(1.2f, 2.6f);
		s->weaveP = Plankton::Rand(0.0f, 6.2832f);
		s->curlAt = Plankton::Rand(0.0f, 1.0f) < 0.45f ? Plankton::Rand(0.5f, (std::max)(0.6f, s->life - 1.4f)) : -1.0f;
		s->curlDur = Plankton::Rand(0.6f, 0.9f);
		s->curlDir = Plankton::Rand(0.0f, 1.0f) < 0.5f ? 1.0f : -1.0f;
		s->bright = Plankton::Rand(0.6f, 1.0f);
		s->age = 0.0f;
		s->acc = 0.0f;
		s->nh = 1;
		for (auto& h : s->hist) h = s->head;
		s->live = true;
	}

	// once a frame: a_aboard (you are on a boat), a_waterZ (its water)
	inline void Tick(float a_dt, bool a_aboard, float a_waterZ)
	{
		Load();
		const float dt = std::clamp(a_dt, 0.0f, 0.1f);
		const float want = (a_aboard && g.amount > 0.0f) ? 1.0f : 0.0f;
		g.vis += (want - g.vis) * (1.0f - std::exp(-2.0f * dt));
		if (!g.root && want <= 0.0f) return;
		if (g.vis < 0.01f && want <= 0.0f) { if (g.root) HideAll(); return; }
		if (!EnsureRoot()) return;
		auto* cam = RE::PlayerCamera::GetSingleton();
		if (!cam || !cam->cameraRoot) return;
		const RE::NiPoint3 eye = cam->cameraRoot->world.translate;
		const RE::NiPoint3 fwd = cam->cameraRoot->world.rotate * RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
		if (g.haveEye && dt > 0.0f) {
			RE::NiPoint3 v = (eye - g.lastEye) / dt;
			if (v.Length() > 3000.0f) v = RE::NiPoint3{};  // a load or a cell hop, not the boat's way
			g.eyeVel = g.eyeVel + (v - g.eyeVel) * (1.0f - std::exp(-1.5f * dt));
		}
		g.lastEye = eye; g.haveEye = true;
		const float wang = OwnWind::Dir(), wsp = OwnWind::Str();
		const RE::NiPoint3 wdir{ std::sin(wang), std::cos(wang), 0.0f };  // the way it blows toward (as SailWind)
		g.spawnAcc += want * g.amount * (0.8f + 3.2f * wsp) * dt;  // streaks a second
		while (g.spawnAcc >= 1.0f) { g.spawnAcc -= 1.0f; Spawn(eye, fwd, wdir, wsp, a_waterZ); }
		const float dim = g.vis * (1.0f - 0.6f * Plankton::NightAmount()) * (0.55f + 0.45f * wsp);
		int live = 0;
		for (int si = 0; si < kStreaks; ++si) {
			auto& s = g.s[si];
			Piece* pc = &g.p[si * kSegs];
			if (!s.live) { for (int k = 0; k < kSegs; ++k) if (pc[k].node) pc[k].node->SetAppCulled(true); continue; }
			s.age += dt;
			if (s.age >= s.life) { s.live = false; for (int k = 0; k < kSegs; ++k) if (pc[k].node) pc[k].node->SetAppCulled(true); continue; }
			++live;
			// the head's way: the wind's, weaving; in a curl it loops over (a vertical loop, sliding sideways a little)
			float h = s.heading + s.weaveA * std::sin(s.weaveF * s.age + s.weaveP);
			float pitch = s.climb;
			if (s.curlAt >= 0.0f && s.age > s.curlAt && s.age < s.curlAt + s.curlDur) {
				const float u = (s.age - s.curlAt) / s.curlDur, sm = u * u * (3.0f - 2.0f * u);
				pitch += 6.2832f * sm;
				h += s.curlDir * 0.6f * sm;
			} else if (s.curlAt >= 0.0f && s.age >= s.curlAt + s.curlDur) h += s.curlDir * 0.6f;
			const RE::NiPoint3 dir{ std::sin(h) * std::cos(pitch), std::cos(h) * std::cos(pitch), std::sin(pitch) };
			const float step = s.speed * dt;
			s.head += dir * step;
			s.acc += step;
			if (s.acc >= kStep) {
				s.acc = std::fmod(s.acc, kStep);
				for (int k = kHist - 1; k > 0; --k) s.hist[k] = s.hist[k - 1];
				s.hist[0] = s.head;
				if (s.nh < kHist) ++s.nh;
			}
			const float env = std::clamp(s.age / 0.4f, 0.0f, 1.0f) * std::clamp((s.life - s.age) / 0.8f, 0.0f, 1.0f);
			for (int k = 0; k < kSegs; ++k) {
				auto& p = pc[k];
				if (!p.node) continue;
				if (k + 1 >= s.nh) { p.node->SetAppCulled(true); continue; }
				const RE::NiPoint3 a = s.hist[k], b = s.hist[k + 1];
				RE::NiPoint3 d = a - b;
				const float len = d.Length();
				if (len < 0.5f || len > 4.0f * kStep) { p.node->SetAppCulled(true); continue; }
				d = d / len;
				const RE::NiPoint3 mid = (a + b) * 0.5f;
				RE::NiPoint3 to = eye - mid;
				const float dist = to.Length();
				to = to - d * to.Dot(d);
				const float tl = to.Length();
				if (tl < 1e-3f) { p.node->SetAppCulled(true); continue; }
				const RE::NiPoint3 nz = to / tl;
				const RE::NiPoint3 nx = d.Cross(nz);  // right-handed: x = y cross z
				auto& m = p.node->local.rotate;  // local y along the streak, local z (the quad's face) toward the camera
				m.entry[0][0] = nx.x; m.entry[0][1] = d.x; m.entry[0][2] = nz.x;
				m.entry[1][0] = nx.y; m.entry[1][1] = d.y; m.entry[1][2] = nz.y;
				m.entry[2][0] = nx.z; m.entry[2][1] = d.z; m.entry[2][2] = nz.z;
				p.node->local.translate = mid;
				p.node->local.scale = len / kStep;
				const float t = (static_cast<float>(k) + 0.5f) / static_cast<float>(kSegs);
				const float taper = std::pow(std::sin(3.14159f * t), 0.8f);
				const float nearK = std::clamp((dist - 90.0f) / 160.0f, 0.0f, 1.0f), farK = std::clamp((3000.0f - dist) / 900.0f, 0.0f, 1.0f);
				const float al = 0.55f * s.bright * env * taper * nearK * farK * dim;
				if (p.mat) {
					p.mat->baseColor = RE::NiColorA{ 0.92f, 0.96f, 1.0f, al };
					p.mat->baseColorScale = 1.0f;
				}
				p.node->SetAppCulled(al < 0.004f);
			}
		}
		if (g.logs < 3 && live) { ++g.logs; logger::info("Wind streaks: {} live, wind toward {:.0f} deg, strength {:.2f}", live, wang * 57.2958f, wsp); }
		RE::NiUpdateData data{};
		data.flags.set(RE::NiUpdateData::Flag::kDirty);
		g.root->Update(data);
	}
}
