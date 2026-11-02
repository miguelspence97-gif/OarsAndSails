// v1.7.0: the Fishing Supplies (Creation Club Fishing, ccbgssse001-fish.esm 0x8D0) riding in the boat, left of the stern
// seat, scaled down, and usable ("Use Fishing Supplies"): a ref of the game's own activator, not a mesh in the hull, so
// its activation (and its script) still works. Placed from the hull ref, then moved with the boat every frame
// (SetPosition: its collision, which the crosshair needs to pick it, moves too); turned with the boat's heading.
// Saved refs are removed after a load and placed again (as the lake boat parts), so a save never piles them up.
// v1.7.11: a second item the same way, the bedroll: E on the bedroll rolled under the bow thwart ("Unroll Bedroll")
// lays the game's own bedroll (furniture: sleep in it) on the floorboards and hides the roll; it stays out for good
// ([Clutter] Bedroll=1; oas "bedroll roll" puts it away). oas "num gear": the numpad places them (Num5 picks which,
// 8/2 forward/back, 4/6 left/right, 9/3 up/down, 7/1 turn), saved in RowRim.ini [Clutter] <Item>X / Y / Z / Yaw.
// v1.7.45: the supplies' own mesh is hidden: the fish tub baked into the hull floor (RowRim_Clutter_FishBucket,
// scripts/clutter/fishbucket.py) is what you see, and the supplies ref is kept on the tub (fixed, not numpad placed) so
// looking at the tub still gives "Use Fishing Supplies" and its script.
namespace Gear
{
	constexpr float kTubX = -44.5f, kTubY = -143.0f, kTubZ = 40.4f;  // the tub's spot in the hull (fishbucket.py SPOT, floor)
	struct Item
	{
		const char* key;    // ini prefix
		const char* label;  // shown when picked
		float x, y, z, yaw, scale;
		bool on = true;
		RE::TESBoundObject* base = nullptr;
		bool looked = false;  // base looked up
		RE::ObjectRefHandle ref;
		float retry = 0.0f;
		RE::NiPoint3 lastAt{};
		float lastYaw = 1.0e9f;
		float no3D = 0.0f;  // v1.7.12: how long it has had no 3D
		bool had3D = false;  // v1.7.27
		double placedAt = -100.0;  // v1.7.34: when placed (steady clock s): it fades in over 1.5 s
		bool parked = false;  // v1.7.47: packed up: the ref is kept, disabled (no delete / place churn on every toggle)
	};
	enum : int { kSupplies = 0, kBedroll = 1, kItems = 2 };
	inline Item g_items[kItems] = {
		{ "Supplies", "Fishing Supplies", -24.0f, -150.0f, 40.3f, 0.0f, 0.5f, true },  // v1.7.40: 0.5
		{ "Bedroll", "Bedroll", 0.0f, 30.0f, 41.0f, 0.0f, 0.9f, false },  // v1.7.28: 0.9
	};
	inline bool g_loaded = false;
	inline std::vector<RE::FormID> g_purge;
	inline float g_sweepLeft = 0.0f, g_sweepTimer = 0.0f;  // v1.7.21: stray beds swept for a while after a load
	// numpad
	inline bool g_numOn = false;
	inline int g_sel = 0;
	inline bool g_held[10]{};
	inline bool g_dirty = false;

	inline float IniF(const char* a_k, float a_def)
	{
		char buf[32]{};
		GetPrivateProfileStringA("Clutter", a_k, std::format("{:.2f}", a_def).c_str(), buf, sizeof(buf), Plankton::IniPath().c_str());
		try { return std::stof(buf); } catch (...) { return a_def; }
	}
	inline void Load()
	{
		if (g_loaded) return;
		g_loaded = true;
		const auto ini = Plankton::IniPath();
		g_items[kSupplies].on = GetPrivateProfileIntA("Clutter", "Supplies", 1, ini.c_str()) != 0;
		g_items[kBedroll].on = GetPrivateProfileIntA("Clutter", "Bedroll", 0, ini.c_str()) != 0;
		for (auto& it : g_items) {
			const std::string k = it.key;
			it.x = IniF((k + "X").c_str(), it.x);
			it.y = IniF((k + "Y").c_str(), it.y);
			it.z = IniF((k + "Z").c_str(), it.z);
			it.yaw = IniF((k + "Yaw").c_str(), it.yaw);
			it.scale = std::clamp(IniF((k + "Scale").c_str(), it.scale), 0.1f, 2.0f);
		}
		auto& sup = g_items[kSupplies];  // v1.7.45: on the tub, whatever the ini says
		sup.x = kTubX; sup.y = kTubY; sup.z = kTubZ; sup.yaw = 0.0f;
	}
	inline void SaveItem(const Item& a_it)
	{
		const auto ini = Plankton::IniPath();
		const std::string k = a_it.key;
		WritePrivateProfileStringA("Clutter", (k + "X").c_str(), std::format("{:.1f}", a_it.x).c_str(), ini.c_str());
		WritePrivateProfileStringA("Clutter", (k + "Y").c_str(), std::format("{:.1f}", a_it.y).c_str(), ini.c_str());
		WritePrivateProfileStringA("Clutter", (k + "Z").c_str(), std::format("{:.1f}", a_it.z).c_str(), ini.c_str());
		WritePrivateProfileStringA("Clutter", (k + "Yaw").c_str(), std::format("{:.1f}", a_it.yaw).c_str(), ini.c_str());
	}

	// the vanilla bedroll you sleep in: a Skyrim.esm furniture with "bedroll" in its model path, never the hay pile
	// (BedrollHay01); one named Bedroll / Bed Roll first. v1.7.12: the candidates are logged.
	inline RE::TESBoundObject* FindBedroll()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) return nullptr;
		auto low = [](std::string a_s) {
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		};
		RE::TESFurniture* best = nullptr;
		int bestScore = -1;
		std::string seen;
		for (auto* f : dh->GetFormArray<RE::TESFurniture>()) {
			if (!f || (f->GetFormID() >> 24) != 0x00) continue;
			const char* m = f->GetModel();
			if (!m) continue;
			const std::string model = low(m);
			if (model.find("bedroll") == std::string::npos) continue;
			std::string name = low(f->GetName() ? f->GetName() : "");
			seen += std::format(" {:08X} '{}' ({});", f->GetFormID(), f->GetName() ? f->GetName() : "", m);
			if (model.find("hay") != std::string::npos || name.find("hay") != std::string::npos) continue;
			name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
			int score = 1;
			if (name == "bedroll") score += 4;
			else if (name.find("bedroll") != std::string::npos) score += 2;
			if (model.find("ground") != std::string::npos) score += 1;
			if (score > bestScore) { bestScore = score; best = f; }
		}
		logger::info("Gear: bedroll furniture candidates:{}", seen.empty() ? " none" : seen);
		if (best) logger::info("Gear: bedroll furniture {:08X} '{}' ({})", best->GetFormID(), best->GetName() ? best->GetName() : "", best->GetModel());
		return best;
	}
	// v1.7.24: every Fishing Supplies placed in the CC file is linked (no keyword) to a ccBGSSSE001_FishingMarker (STAT 0x853)
	// out on the water, 100-250 from it: the fishing casts at that marker. Ours had none (turned to the boat's side, no cast,
	// stuck fishing): one is placed on the water off the side you face and linked before the supplies are used.
	inline RE::TESBoundObject* g_markerBase = nullptr;
	inline RE::ObjectRefHandle g_marker;
	inline RE::NiPoint3 g_fishSpot{};
	inline float g_fishYaw = 0.0f;
	inline bool g_haveSpot = false;
	inline void SetFishSpot(const RE::NiPoint3& a_at, float a_yaw) { g_fishSpot = a_at; g_fishYaw = a_yaw; g_haveSpot = true; }
	// v1.7.30: our own use of the supplies (marker set, linked) vs the game's when the crosshair is on the supplies themselves
	inline bool g_ourUse = false;
	inline bool IsSupplies(const RE::TESObjectREFR* a_ref);
	inline RE::TESBoundObject* Base(int a_i)
	{
		auto& it = g_items[a_i];
		if (!it.looked) {
			it.looked = true;
			if (a_i == kSupplies) {
				// v1.7.12: the plugin's real name is ccBGSSSE001-Fish.esm and the lookup by name is case sensitive: find the
				// file whatever its case, then look the form up under its own spelling
				auto* dh = RE::TESDataHandler::GetSingleton();
				std::string fname;
				RE::TESFile* file = nullptr;
				if (dh) {
					for (auto* f : dh->files) {
						if (!f) continue;
						const std::string_view n = f->GetFilename();
						constexpr std::string_view want = "ccbgssse001-fish.esm";
						if (n.size() == want.size() && std::equal(n.begin(), n.end(), want.begin(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; })) {
							fname = std::string(n);
							file = f;
							break;
						}
					}
				}
				// v1.7.21: LookupForm by name came back empty although the record is in the file (read from the esm:
				// 0x8D0 ccBGSSSE001_FishingFurniture01_Arctic, also 0x806 / 0x8A6 / 0x89F / 0x8D1, all FishingGear_Cluster01.nif):
				// the form ID is now built from the file's own load index, then the other variants, then any activator
				// of that file with the fishing gear model
				if (dh && file) {
					const bool light = file->IsLight();
					auto idOf = [&](std::uint32_t a_raw) -> RE::FormID {
						return light ? (0xFE000000u | (static_cast<std::uint32_t>(file->GetSmallFileCompileIndex()) << 12) | (a_raw & 0xFFF))
									 : ((static_cast<std::uint32_t>(file->GetCompileIndex()) << 24) | (a_raw & 0xFFFFFF));
					};
					if (auto* mk = RE::TESForm::LookupByID(idOf(0x853u))) g_markerBase = mk->As<RE::TESBoundObject>();  // v1.7.24: ccBGSSSE001_FishingMarker
					logger::info("Gear: fishing marker {}", g_markerBase ? std::format("{:08X}", g_markerBase->GetFormID()) : std::string("not found"));
					for (std::uint32_t raw : { 0x8D0u, 0x806u, 0x8A6u, 0x89Fu, 0x8D1u }) {
						if (auto* f = RE::TESForm::LookupByID(idOf(raw))) {
							if (auto* b = f->As<RE::TESBoundObject>()) {
								it.base = b;
								logger::info("Gear: fishing supplies {:08X} from {} (index {:02X}{})", b->GetFormID(), fname, light ? file->GetSmallFileCompileIndex() : file->GetCompileIndex(), light ? ", light" : "");
								break;
							}
						}
					}
					if (!it.base) {
						for (auto* a : dh->GetFormArray<RE::TESObjectACTI>()) {
							if (!a || a->GetFile(0) != file) continue;
							const char* m = a->GetModel();
							if (m && std::string_view(m).find("FishingGear_Cluster01") != std::string_view::npos) {
								it.base = a;
								logger::info("Gear: fishing supplies {:08X} found by its model", a->GetFormID());
								break;
							}
						}
					}
				}
				if (!fname.empty() && !it.base) logger::info("Gear: {} is loaded but none of its fishing supplies activators was found", fname);
				if (!it.base) logger::info("Gear: no Creation Club Fishing (ccbgssse001-fish.esm 0x8D0): no fishing supplies in the boat");
			} else {
				it.base = FindBedroll();
				if (!it.base) logger::info("Gear: no bedroll furniture found");
			}
		}
		return it.base;
	}
	inline bool BedrollOut() { Load(); return g_items[kBedroll].on; }
	// v1.7.48: the hull carries our own sleeping bag (RowRim_Clutter_SleepingBag): the game's bedroll mesh is hidden and the
	// bag drawn on the bed's spot instead (the furniture ref stays for sleeping in)
	inline bool g_ownBag = false;
	inline bool BedPose(RE::NiPoint3& a_at, float& a_yawDeg, float& a_scale)
	{
		Load();
		const auto& it = g_items[kBedroll];
		a_at = RE::NiPoint3{ it.x, it.y, it.z };
		a_yawDeg = it.yaw;
		a_scale = it.scale;
		return true;
	}
	inline bool g_seated = false;  // v1.7.12: the player is seated: the bedroll is stowed (rolled) meanwhile
	// v1.7.12: the bedroll is lying in the boat right now (out, not stowed for the ride, and drawn): the roll is hidden only then
	inline bool BedrollShown()
	{
		Load();
		auto& it = g_items[kBedroll];
		if (!it.on || g_seated) return false;
		auto r = it.ref.get();
		return r && !it.parked && !r->IsDisabled() && r->Get3D() != nullptr;
	}

	inline void SetScale(RE::TESObjectREFR* a_ref, float a_scale)
	{
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
		if (!a_ref || !policy) return;
		const auto handle = policy->GetHandleForObject(static_cast<RE::VMTypeID>(a_ref->GetFormType()), a_ref);
		if (!handle) return;
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		auto args = RE::MakeFunctionArguments(static_cast<float>(a_scale));
		vm->DispatchMethodCall(handle, RE::BSFixedString("ObjectReference"), RE::BSFixedString("SetScale"), args, callback);
	}

	inline void Remove(int a_i, const char* a_why)
	{
		auto& it = g_items[a_i];
		if (auto r = it.ref.get()) {
			r->Disable();
			r->SetDelete(true);
			logger::info("Gear: {} removed ({})", it.label, a_why);
		}
		it.ref = {};
		it.lastYaw = 1.0e9f;
		it.parked = false;
	}
	// v1.7.47: packed up (or stowed for the ride): the bed is only disabled, and enabled again when it is laid out. Placing
	// and deleting a new furniture ref on every toggle left it unseen at times and could hang the game on quick toggles.
	inline double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
	inline void Park(int a_i, const char* a_why)
	{
		auto& it = g_items[a_i];
		auto r = it.ref.get();
		if (!r || r->IsDeleted()) { Remove(a_i, a_why); return; }
		if (!it.parked) {
			if (!r->IsDisabled()) r->Disable();
			it.parked = true;
			logger::info("Gear: {} put away ({})", it.label, a_why);
		}
	}
	// v1.7.47: one pack-up / unroll at a time: E again within 1.6 s (the bed's fade in) is ignored
	inline double g_bedToggleAt = -100.0;
	inline bool BedToggleReady()
	{
		const double now = Now();
		if (now - g_bedToggleAt < 1.6) return false;
		g_bedToggleAt = now;
		return true;
	}

	// the crosshair is on one of ours (E then uses it; the boat's E sit is skipped)
	inline bool Targeted()
	{
		auto* pick = RE::CrosshairPickData::GetSingleton();
		if (!pick) return false;
		auto t = pick->GetActiveTarget().get();
		if (!t) return false;
		for (auto& it : g_items) {
			auto r = it.ref.get();
			if (r && r.get() == t.get()) return true;
		}
		return false;
	}
	// v1.7.22: E on the fishing supplies while seated: the player stands up (the fishing needs him on his feet and the
	// Activate control back) and the supplies are used a moment later
	inline float g_useIn = -1.0f;
	inline RE::ObjectRefHandle g_useRef;
	inline bool UseTargetedSoon()
	{
		auto* pick = RE::CrosshairPickData::GetSingleton();
		auto t = pick ? pick->GetActiveTarget().get() : RE::NiPointer<RE::TESObjectREFR>{};
		if (!t) return false;
		for (int i = 0; i < kItems; ++i) {
			auto& it = g_items[i];
			auto r = it.ref.get();
			if (r && r.get() == t.get() && i == kSupplies) {
				g_useRef = r->GetHandle();
				g_useIn = 0.6f;
				logger::info("Gear: {} used from the seat: standing up first", it.label);
				return true;
			}
		}
		return false;
	}
	// the game used it itself (the same E press, once the Activate control was back): no second use
	inline void OnUsed(const RE::TESObjectREFR* a_ref)
	{
		if (g_useIn < 0.0f || !a_ref) return;
		auto r = g_useRef.get();
		if (r && r.get() == a_ref) { g_useIn = -1.0f; g_useRef = {}; logger::info("Gear: the game used the supplies itself"); }
	}
	// v1.7.23: the supplies used through their own prompt activator (the crosshair never reached the supplies themselves
	// inside the hull): used a_delay later, so the player can stand up first
	inline bool UseSuppliesSoon(float a_delay)
	{
		Load();
		auto r = g_items[kSupplies].ref.get();
		if (!r) return false;
		if (g_useIn >= 0.0f) return true;  // already on its way (the same E press seen twice)
		g_useRef = r->GetHandle();
		g_useIn = a_delay;
		logger::info("Gear: Fishing Supplies {:08X} to be used in {:.1f} s", r->GetFormID(), a_delay);
		return true;
	}
	// where the supplies sit (boat model space), when they are out
	inline bool SuppliesSpot(RE::NiPoint3& a_out)
	{
		Load();
		const auto& it = g_items[kSupplies];
		if (!it.on || !it.ref.get()) return false;
		a_out = { it.x, it.y, it.z };
		return true;
	}
	inline void TendUse(float a_dt)
	{
		if (g_useIn < 0.0f) return;
		if ((g_useIn -= a_dt) > 0.0f) return;
		g_useIn = -1.0f;
		auto r = g_useRef.get();
		auto* player = RE::PlayerCharacter::GetSingleton();
		g_useRef = {};
		if (!r || !player) return;
		Base(kSupplies);
		if (g_haveSpot && g_markerBase) {  // v1.7.24: the cast spot on the water, linked from the supplies
			// v1.7.28: a fresh marker each time (the old one moved across cells kept its first cell: the catch was shown empty
			// after moving spots)
			if (auto old = g_marker.get()) { old->Disable(); old->SetDelete(true); }
			auto mk = player->PlaceObjectAtMe(g_markerBase, false);
			g_marker = mk ? mk->GetHandle() : RE::ObjectRefHandle{};
			if (mk) {
				mk->SetPosition(g_fishSpot);
				mk->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, g_fishYaw });
				r->extraList.SetLinkedRef(mk.get(), nullptr);
				logger::info("Gear: fishing marker {:08X} at ({:.0f},{:.0f},{:.0f}), linked from the supplies", mk->GetFormID(), g_fishSpot.x, g_fishSpot.y, g_fishSpot.z);
			}
		} else logger::info("Gear: no fishing marker ({})", g_markerBase ? "no spot" : "base not found");
		g_haveSpot = false;
		g_ourUse = true;
		const bool ok = r->ActivateRef(player, 0, nullptr, 1, false);
		g_ourUse = false;
		logger::info("Gear: {:08X} activated by the player ({})", r->GetFormID(), ok ? "ok" : "refused");
	}
	inline bool IsSupplies(const RE::TESObjectREFR* a_ref)
	{
		auto r = g_items[kSupplies].ref.get();
		return a_ref && r && r.get() == a_ref;
	}
	// v1.7.38: the boat deleted: every piece and the fishing marker go with it (the supplies were left hanging in the air)
	inline void RemoveAll(const char* a_why)
	{
		for (int i = 0; i < kItems; ++i) Remove(i, a_why);
		if (auto mk = g_marker.get()) { mk->Disable(); mk->SetDelete(true); }
		g_marker = {};
		g_useIn = -1.0f;
		g_useRef = {};
	}
	inline bool IsOurs(RE::FormID a_id)
	{
		for (auto& it : g_items) {
			auto r = it.ref.get();
			if (r && r->GetFormID() == a_id) return true;
		}
		return false;
	}

	// the rolled bedroll was clicked: out it comes, for good
	inline void Unroll()
	{
		Load();
		auto& it = g_items[kBedroll];
		if (it.on) return;
		it.on = true;
		it.retry = 0.0f;  // v1.7.12: placed at once, not after the 2 s retry
		WritePrivateProfileStringA("Clutter", "Bedroll", "1", Plankton::IniPath().c_str());
		RE::SendHUDMessage::ShowHUDMessage("Bedroll unrolled (oas \"num gear\" to move it)");
		logger::info("Gear: bedroll unrolled");
	}
	inline std::string BedrollCommand(const std::string& a_arg)
	{
		Load();
		if (a_arg == "roll" || a_arg == "off" || a_arg == "pack") {
			g_items[kBedroll].on = false;
			WritePrivateProfileStringA("Clutter", "Bedroll", "0", Plankton::IniPath().c_str());
			Park(kBedroll, "rolled up");
			return "bedroll rolled up under the bow thwart";
		}
		if (a_arg == "unroll" || a_arg == "on") { Unroll(); return "bedroll laid out in the boat"; }
		return "oas \"bedroll unroll\" | \"bedroll roll\"";
	}

	// v1.7.21: beds (and supplies) of ours the save kept but the co-save did not list (a cell that had unloaded, a save made
	// mid-move): the game's own bedroll or supplies ref (FF, our base), not the one we hold, lying over water or by our boat.
	// Swept every 3 s for the first 30 s after a load, as the cells round you come in.
	inline void SweepStrays(RE::TESObjectREFR* a_hull, float a_dt)
	{
		if (g_sweepLeft <= 0.0f) return;
		g_sweepLeft -= a_dt;
		if ((g_sweepTimer -= a_dt) > 0.0f) return;
		g_sweepTimer = 3.0f;
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!player || !tes) return;
		RE::TESBoundObject* bases[kItems] = { Base(kSupplies), Base(kBedroll) };
		const RE::NiPoint3 hullAt = a_hull ? a_hull->GetPosition() : RE::NiPoint3{};
		int n = 0;
		tes->ForEachReferenceInRange(player, 12000.0f, [&](RE::TESObjectREFR* a_ref) {
			if (!a_ref || (a_ref->GetFormID() >> 24) != 0xFF || a_ref->IsDeleted() || a_ref->IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;
			auto* base = a_ref->GetBaseObject();
			if (base && base == g_markerBase) {  // v1.7.24: a fishing marker of ours from before
				if (auto mk = g_marker.get(); !mk || mk.get() != a_ref) {
					a_ref->Disable();
					a_ref->SetDelete(true);
					logger::info("Gear: stray fishing marker {:08X} removed", a_ref->GetFormID());
				}
				return RE::BSContainer::ForEachResult::kContinue;
			}
			int k = -1;
			for (int i = 0; i < kItems; ++i) if (base && base == bases[i]) k = i;
			if (k < 0) return RE::BSContainer::ForEachResult::kContinue;
			if (auto cur = g_items[k].ref.get(); cur && cur.get() == a_ref) return RE::BSContainer::ForEachResult::kContinue;
			const RE::NiPoint3 p = a_ref->GetPosition();
			bool overWater = false;
			if (auto* c = a_ref->GetParentCell(); c && c->IsExteriorCell()) {
				float w = 0.0f;
				overWater = c->GetWaterHeight(p, w) && std::isfinite(w) && std::abs(w) < 1000000.0f && std::abs(p.z - w) < 200.0f;
			}
			const bool byBoat = a_hull && (p - hullAt).Length() < 600.0f;
			if (!overWater && !byBoat) return RE::BSContainer::ForEachResult::kContinue;
			a_ref->Disable();
			a_ref->SetDelete(true);
			++n;
			logger::info("Gear: stray {} {:08X} removed ({})", g_items[k].label, a_ref->GetFormID(), overWater ? "over the water" : "by the boat");
			return RE::BSContainer::ForEachResult::kContinue;
		});
		if (n) logger::info("Gear: {} stray piece(s) swept", n);
	}

	// v1.7.21: every player update, boat or not (the saved pieces used to wait for a boat to be placed before they went:
	// with no boat out after a load they stayed, beds hovering over the water)
	inline void TendLoad(RE::TESObjectREFR* a_hull, float a_dt)
	{
		Load();
		TendUse(a_dt);  // v1.7.22
		if (!g_purge.empty()) {
			for (auto id : g_purge) {
				auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
				if (!ref || ref->IsDeleted() || (id >> 24) != 0xFF || ref->As<RE::Actor>()) continue;
				// v1.7.12: every ref the co-save listed as ours goes (also one of a base we no longer use: the hay pile)
				ref->Disable();
				ref->SetDelete(true);
				logger::info("Gear: saved {:08X} ('{}') removed after the load", id, ref->GetBaseObject() ? ref->GetBaseObject()->GetName() : "");
			}
			g_purge.clear();
		}
		SweepStrays(a_hull, a_dt);
	}

	// every frame the boat is placed: a_at / a_rot the boat model's pose, a_yaw its facing
	// v1.7.46: the game's Fishing Supplies mesh is gone from the boat: every node culled and faded out, every frame (a
	// cell hop or a reload brings its 3D back), only its collision (the crosshair pick) and its script remain
	inline void HideMeshNode(RE::NiAVObject* a_obj)
	{
		if (!a_obj) return;
		if (!a_obj->GetAppCulled()) a_obj->SetAppCulled(true);
		if (auto* node = a_obj->AsNode()) {
			for (auto& c : node->GetChildren()) HideMeshNode(c.get());
		}
	}
	inline void HideMesh(RE::NiAVObject* a_root)
	{
		if (!a_root) return;
		if (auto* fade = a_root->AsFadeNode()) fade->GetRuntimeData().currentFade = 0.0f;
		HideMeshNode(a_root);
	}
	inline void Tend(RE::TESObjectREFR* a_hull, const RE::NiPoint3& a_at, const RE::NiMatrix3& a_rot, float a_yaw, float a_dt, bool a_seated)
	{
		Load();
		if (a_seated != g_seated) {
			g_seated = a_seated;
			// v1.7.21: sitting down rolls the bedroll up for good (it used to come back out on its own when you stood up or
			// left the boat): E on the roll ("Unroll Bedroll") lays it out again
			auto& bed = g_items[kBedroll];
			if (a_seated && bed.on) {
				bed.on = false;
				WritePrivateProfileStringA("Clutter", "Bedroll", "0", Plankton::IniPath().c_str());
				Park(kBedroll, "rolled up for the ride");
				RE::SendHUDMessage::ShowHUDMessage("Bedroll rolled up for the ride");
			}
			bed.retry = 0.0f;
		}
		for (int i = 0; i < kItems; ++i) {
			auto& it = g_items[i];
			// v1.7.12: the bedroll is stowed while you sit (rolled up for the ride; it rode along in the cell it was laid out
			// in and was lost when that cell unloaded)
			const bool want = it.on && a_hull && !(i == kBedroll && a_seated);
			if (!want) {
				if (it.ref) {
					if (i == kBedroll && a_hull) {
						Park(i, !it.on ? "off" : "stowed for the ride");  // v1.7.47: kept, disabled
						// v1.7.56: the put-away bed rides along with the boat (left where it was parked, laid out after rowing it
						// was far off in another cell: its 3D and its [E] took a moment to come back)
						if (auto pr = it.ref.get(); pr && it.parked && !pr->IsDeleted()) {
							const RE::NiPoint3 ps = a_at + a_rot * RE::NiPoint3{ it.x, it.y, it.z };
							if ((pr->GetPosition() - ps).Length() > 40.0f) {
								pr->SetPosition(ps);
								pr->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, a_yaw + it.yaw * 0.0174533f });
							}
						}
					}
					else Remove(i, !a_hull ? "no boat" : !it.on ? "off" : "stowed for the ride");
				}
				continue;
			}
			auto r = it.ref.get();
			if (r && it.parked) {  // v1.7.47: laid out again: the same bed enabled, set on its spot, fading in
				if (r->IsDeleted()) { Remove(i, "gone while put away"); r = nullptr; }
				else {
					// v1.7.56: set on its spot before it is enabled (its 3D comes in there, the [E] with it)
					r->SetPosition(a_at + a_rot * RE::NiPoint3{ it.x, it.y, it.z });
					r->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, a_yaw + it.yaw * 0.0174533f });
					r->Enable(false);
					it.parked = false;
					it.placedAt = Now();
					it.had3D = false;
					it.no3D = 0.0f;
					it.lastYaw = 1.0e9f;
					logger::info("Gear: {} {:08X} laid out again", it.label, r->GetFormID());
				}
			}
			// v1.7.12: left behind in another cell, or no 3D for 1.5 s (its cell unloaded): placed again from the boat
			if (r && !r->IsDeleted()) {
				// v1.7.27: placed during a load its 3D came in later where the ref was first put (at the hull): unseen until the
				// boat moved. When the 3D arrives, the ref is set on its spot again.
				if (r->Get3D() && !it.had3D) it.lastYaw = 1.0e9f;
				it.had3D = r->Get3D() != nullptr;
				if (i == kSupplies || (i == kBedroll && g_ownBag)) HideMesh(r->Get3D());  // v1.7.46: hidden before it is ever drawn (v1.7.48: the bed too, our bag shows)
				// v1.7.29: 3D that is not in the scene (no parent) counts as none: placed again after 1.5 s
				auto* r3 = r->Get3D();
				it.no3D = r3 && r3->parent ? 0.0f : it.no3D + a_dt;
				// v1.7.21: "another cell than the hull" was wrong near a cell edge: the piece sits a little ahead of or behind the
				// hull ref and is filed in the next cell, so it was removed and placed again every frame (dozens of beds a
				// second, the strays over the water). Now: its cell has unloaded, or it is far from its spot on the boat.
				auto* rc = r->GetParentCell();
				const RE::NiPoint3 spot = a_at + a_rot * RE::NiPoint3{ it.x, it.y, it.z };
				const bool lost = (rc && !rc->IsAttached()) || (r->GetPosition() - spot).Length() > 400.0f;
				if (lost || it.no3D > 1.5f) {
					Remove(i, lost ? "left behind (its cell unloaded or it was far from the boat)" : "its 3D was gone");
					it.retry = 0.5f;  // never placed again in the same frame
					it.no3D = 0.0f;
					r = nullptr;
				}
			}
			if (!r || r->IsDeleted()) {
				if ((it.retry -= a_dt) > 0.0f) continue;
				it.retry = 2.0f;
				auto* base = Base(i);
				if (!base) continue;
				auto placed = a_hull->PlaceObjectAtMe(base, false);
				if (!placed) continue;
				it.ref = placed->GetHandle();
				it.had3D = false;
				it.placedAt = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
				if (std::abs(it.scale - 1.0f) > 0.01f) SetScale(placed.get(), it.scale);
				r = placed;
				it.lastYaw = 1.0e9f;
				logger::info("Gear: {} placed {:08X} (scale {:.2f})", it.label, placed->GetFormID(), it.scale);
			}
			const RE::NiPoint3 w = a_at + a_rot * RE::NiPoint3{ it.x, it.y, it.z };
			const float yaw = a_yaw + it.yaw * 0.0174533f;
			if ((w - it.lastAt).Length() < 0.05f && std::abs(yaw - it.lastYaw) < 0.0005f) continue;
			it.lastAt = w;
			it.lastYaw = yaw;
			r->SetPosition(w);
			r->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, yaw });
		}
	}

	// v1.7.12: right before drawing, each item's 3D is put on the boat as drawn, rocking with it (roll, pitch, heave): the
	// boat model's world pose (bow +Y) times the item's own spot and turn. Its collision stays where Tend put it.
	inline void LatePin(const RE::NiAVObject* a_boat)
	{
		if (!a_boat) return;
		const auto& bw = a_boat->world;
		const float bs = bw.scale > 0.0f ? bw.scale : 1.0f;
		for (auto& it : g_items) {
			if (!it.on) continue;
			auto r = it.ref.get();
			if (it.parked || (r && r->IsDisabled())) continue;  // v1.7.47: put away
			auto* root = r ? r->Get3D() : nullptr;
			if (!root) continue;
			if (&it == &g_items[kSupplies] || (&it == &g_items[kBedroll] && g_ownBag)) { HideMesh(root); continue; }  // v1.7.48: the bed's mesh too  // v1.7.46: never drawn (the tub is), still picked
			// v1.7.21: a ref moved into the next cell reloads its 3D and fades in from nothing: held fully drawn (as the hull)
			// v1.7.34: except its first 1.5 s: laid out (the bed) it fades in slowly, with the tent
			if (auto* fade = root->AsFadeNode()) {
				const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
				const float f = static_cast<float>(std::clamp((now - it.placedAt) / 1.5, 0.0, 1.0));
				fade->GetRuntimeData().currentFade = f * f * (3.0f - 2.0f * f);
			}
			const RE::NiPoint3 want = bw.translate + bw.rotate * (RE::NiPoint3{ it.x, it.y, it.z } * bs);
			if ((want - root->world.translate).Length() > 80.0f) continue;  // a load or a teleport: not ours to fix
			const RE::NiMatrix3 rot = bw.rotate * RzM(it.yaw * 0.0174533f);
			if (auto* par = root->parent) {
				const RE::NiMatrix3 pinv = par->world.rotate.Transpose();
				const float ps = par->world.scale > 0.0f ? par->world.scale : 1.0f;
				root->local.rotate = pinv * rot;
				root->local.translate = (pinv * (want - par->world.translate)) / ps;
			} else {
				root->local.rotate = rot;
				root->local.translate = want;
			}
			RE::NiUpdateData data{};
			data.flags.set(RE::NiUpdateData::Flag::kDirty);
			root->Update(data);
		}
	}

	// ---- oas "num gear": the numpad places the items (DIK Num1-9) ----
	inline int NumIndex(std::uint32_t a_id)
	{
		switch (a_id) {
		case 0x4F: return 1; case 0x50: return 2; case 0x51: return 3;
		case 0x4B: return 4; case 0x4C: return 5; case 0x4D: return 6;
		case 0x47: return 7; case 0x48: return 8; case 0x49: return 9;
		default: return -1;
		}
	}
	inline bool Placeable(int a_i) { return a_i != kSupplies && g_items[a_i].on && g_items[a_i].ref; }  // v1.7.45: the supplies sit on the tub
	inline void ShowSel()
	{
		auto& it = g_items[g_sel];
		RE::SendHUDMessage::ShowHUDMessage(std::format("Placing: {}{}", it.label, it.on && it.ref ? "" : " (not in the boat)").c_str());
	}
	inline void NumKey(std::uint32_t a_id, bool a_pressed, bool a_downEdge)
	{
		if (!g_numOn) return;
		const int k = NumIndex(a_id);
		if (k < 0) return;
		if (k == 5) {
			if (a_downEdge) {
				for (int n = 0; n < kItems; ++n) {
					g_sel = (g_sel + 1) % kItems;
					if (Placeable(g_sel)) break;
				}
				ShowSel();
			}
			return;
		}
		const bool was = g_held[k];
		g_held[k] = a_pressed;
		if (was && !a_pressed && g_dirty) {  // saved when a key is let go
			SaveItem(g_items[g_sel]);
			g_dirty = false;
		}
	}
	inline void TendNum(float a_dt)
	{
		if (!g_numOn) return;
		auto& it = g_items[g_sel];
		const float mv = 15.0f * a_dt, tr = 45.0f * a_dt;  // 15 units / 45 deg a second
		const float bx = it.x, by = it.y, bz = it.z, byaw = it.yaw;
		if (g_held[8]) it.y += mv;
		if (g_held[2]) it.y -= mv;
		if (g_held[6]) it.x += mv;
		if (g_held[4]) it.x -= mv;
		if (g_held[9]) it.z += mv;
		if (g_held[3]) it.z -= mv;
		if (g_held[7]) it.yaw -= tr;
		if (g_held[1]) it.yaw += tr;
		if (it.x != bx || it.y != by || it.z != bz || it.yaw != byaw) g_dirty = true;
	}
	inline std::string NumCommand(bool a_on)
	{
		Load();
		if (!a_on) {
			if (g_dirty) { SaveItem(g_items[g_sel]); g_dirty = false; }
			g_numOn = false;
			for (auto& h : g_held) h = false;
			return "gear placing off";
		}
		g_numOn = true;
		if (!Placeable(g_sel)) {
			g_sel = kBedroll;
			for (int n = 0; n < kItems; ++n) if (Placeable(n)) { g_sel = n; break; }
		}
		ShowSel();
		return "gear placing on: Num5 picks the item, 8/2 forward/back, 4/6 left/right, 9/3 up/down, 7/1 turn (saved); oas \"num off\" to stop";
	}
	inline bool NumOn() { return g_numOn; }

	constexpr std::uint32_t kRec = 'GEAR';
	inline void Save(SKSE::SerializationInterface* a_i)
	{
		if (!a_i->OpenRecord(kRec, 2)) return;
		const std::uint32_t n = kItems;
		a_i->WriteRecordData(n);
		for (auto& it : g_items) {
			auto r = it.ref.get();
			const RE::FormID id = r ? r->GetFormID() : 0;
			a_i->WriteRecordData(id);
		}
	}
	inline bool LoadRecord(SKSE::SerializationInterface* a_i, std::uint32_t a_type, std::uint32_t a_version)
	{
		if (a_type != kRec) return false;
		std::uint32_t n = 1;
		if (a_version >= 2) a_i->ReadRecordData(n);
		for (std::uint32_t k = 0; k < n && k < 16; ++k) {
			RE::FormID id = 0, out = 0;
			a_i->ReadRecordData(id);
			if (id && a_i->ResolveFormID(id, out)) g_purge.push_back(out);
		}
		return true;
	}
	inline void Forget()
	{
		for (auto& it : g_items) {
			it.ref = {};
			it.retry = 3.0f;  // v1.7.29: first placed 3 s after a load, into a cell that is fully in (placed during the load it stayed unseen)
			it.lastYaw = 1.0e9f;
			it.parked = false;
		}
		g_purge.clear();
		g_marker = {};  // v1.7.24
		g_loaded = false;
		g_sweepLeft = 30.0f;  // v1.7.21
		g_sweepTimer = 2.0f;
	}
}
