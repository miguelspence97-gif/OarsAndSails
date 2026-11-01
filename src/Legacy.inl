// v1.7.8: Oars and Sails (OAS 1.0.x) saves. RowRim is the Oars and Sails update: its boat is a hull on a hidden carrier, the
// OAS boat was an activator (OarsAndSails.esl, kept in the release as an obsolete stub so old saves still find it) or the
// vanilla ShipRowBoat01 with its own benches, chest, oars and lanterns placed as references. Those old pieces are still in
// a save made with OAS. Round the player every 5 s, a piece of the old boat seen on two scans in a row is removed: anything
// placed from OarsAndSails.esl, and our-made (FF) ShipRowBoat01 hulls that are not RowRim's own; the benches, chest, oars,
// lanterns and lights only within 800 of such a hull. A chest with things in it gives them to the player first.
// Nothing is touched while OarsAndSails.dll itself is loaded (it still owns them then).

namespace Legacy
{
	constexpr RE::FormID kHull = 0x00050368;   // ShipRowBoat01
	constexpr RE::FormID kChest = 0x000F3923;  // the OAS boat chest
	constexpr RE::FormID kBench = 0x00041EC7;  // the OAS helm / row benches
	constexpr RE::FormID kOar = 0x00050E77;    // ShipOar01
	constexpr RE::FormID kLamp = 0x0002D847;   // the OAS lanterns
	constexpr RE::FormID kLight = 0x0001CBB3;  // their light
	constexpr float kPropRange = 800.0f;
	constexpr float kScanRange = 9000.0f;

	inline bool g_oasLoaded = false;  // OarsAndSails.dll is running too (checked at data loaded)
	inline bool g_warned = false;
	inline float g_timer = 8.0f;
	inline std::vector<RE::FormID> g_seen;  // candidates from the last scan (removed when seen again)
	inline int g_removed = 0;
	inline int g_items = 0;

	inline void CheckConflict()
	{
		// v1.7.9: the release ships this plugin AS OarsAndSails.dll (installing over Oars and Sails replaces the old one), so
		// only an OarsAndSails.dll that is not this very module is the old plugin
		HMODULE self = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&CheckConflict), &self);
		const HMODULE oas = GetModuleHandleA("OarsAndSails.dll");
		g_oasLoaded = oas != nullptr && oas != self;
		if (g_oasLoaded)
			logger::warn("Legacy: OarsAndSails.dll is loaded as well. Remove it: RowRim replaces it (old boats are left alone and 'oas' stays with it)");
	}

	inline void OnGameLoaded()
	{
		g_timer = 8.0f;
		g_seen.clear();
		if (g_oasLoaded && !g_warned) {
			g_warned = true;
			RE::DebugMessageBox("RowRim: the old Oars and Sails plugin (OarsAndSails.dll) is still installed. RowRim replaces it: uninstall the old "
								"version (keep OarsAndSails.esl from the RowRim download). Until then RowRim's console command is rr.");
		}
	}

	inline bool FromOasPlugin(const RE::TESForm* a_base)
	{
		const auto* file = a_base ? a_base->GetFile(0) : nullptr;
		if (!file) return false;
		const std::string_view n = file->GetFilename();
		constexpr std::string_view want = "oarsandsails.esl";
		return n.size() == want.size() && std::equal(n.begin(), n.end(), want.begin(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
	}

	// a chest's contents go to the player (each stack with its own extra data: tempering, enchantments)
	inline int GiveContents(RE::TESObjectREFR* a_from, RE::PlayerCharacter* a_to)
	{
		int moved = 0;
		auto inv = a_from->GetInventory();
		for (auto& [obj, data] : inv) {
			if (!obj || data.first <= 0) continue;
			std::int32_t left = data.first;
			std::vector<RE::ExtraDataList*> lists;
			if (auto* entry = data.second.get(); entry && entry->extraLists) {
				for (auto* xl : *entry->extraLists) if (xl) lists.push_back(xl);
			}
			for (auto* xl : lists) {
				const std::int32_t n = (std::max)(1, static_cast<std::int32_t>(xl->GetCount()));
				a_from->RemoveItem(obj, n, RE::ITEM_REMOVE_REASON::kStoreInContainer, xl, a_to);
				left -= n;
				moved += n;
			}
			if (left > 0) {
				a_from->RemoveItem(obj, left, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, a_to);
				moved += left;
			}
		}
		return moved;
	}

	// a_ours: RowRim's own refs (the boat, its carrier, the lake boats' parts)
	// v1.7.9: a_ourBoat: our own prompt activators left from before a load (by our boat) are removed, but never taken for an
	// old hull (our lantern light is in a_ours)
	inline void Tend(float a_dt, const std::function<bool(RE::FormID)>& a_ours, const RE::NiPoint3* a_ourBoat)
	{
		if (g_oasLoaded) return;
		if ((g_timer -= a_dt) > 0.0f) return;
		g_timer = 5.0f;
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!player || !tes || !player->GetParentCell() || !player->GetParentCell()->IsAttached()) return;
		struct Cand { RE::TESObjectREFR* ref; bool hull; bool sure; bool boat; };
		std::vector<Cand> found;
		std::vector<RE::NiPoint3> hulls;
		tes->ForEachReferenceInRange(player, kScanRange, [&](RE::TESObjectREFR* a_ref) {
			if (!a_ref || (a_ref->GetFormID() >> 24) != 0xFF || a_ref->IsDeleted() || a_ref->As<RE::Actor>()) return RE::BSContainer::ForEachResult::kContinue;
			auto* base = a_ref->GetBaseObject();
			if (!base || a_ours(a_ref->GetFormID())) return RE::BSContainer::ForEachResult::kContinue;
			const RE::FormID b = base->GetFormID();
			const RE::NiPoint3 at = a_ref->GetPosition();
			const bool byOurs = a_ourBoat && std::hypot(at.x - a_ourBoat->x, at.y - a_ourBoat->y) < kPropRange;
			if (FromOasPlugin(base)) {
				found.push_back({ a_ref, false, true, !byOurs });  // by our boat too: a prompt left from before a load
				if (!byOurs && std::string_view(base->GetName()) == "Rowboat") hulls.push_back(at);
			} else if (b == kHull) {
				found.push_back({ a_ref, true, true, true });  // an old OAS hull, also one at a dock our boat came to
				hulls.push_back(at);
			} else if (b == kChest || b == kBench || b == kOar || b == kLamp || b == kLight) {
				found.push_back({ a_ref, false, false, false });
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		std::vector<RE::FormID> now;
		int removed = 0, items = 0, boats = 0;
		for (auto& c : found) {
			if (!c.sure) {
				const RE::NiPoint3 p = c.ref->GetPosition();
				const bool nearHull = std::any_of(hulls.begin(), hulls.end(), [&](const RE::NiPoint3& h) { return std::hypot(h.x - p.x, h.y - p.y) < kPropRange && std::abs(h.z - p.z) < 400.0f; });
				if (!nearHull) continue;
			}
			const RE::FormID id = c.ref->GetFormID();
			now.push_back(id);
			if (std::find(g_seen.begin(), g_seen.end(), id) == g_seen.end()) continue;  // twice in a row before it goes
			auto* base = c.ref->GetBaseObject();
			if (base && base->Is(RE::FormType::Container)) items += GiveContents(c.ref, player);
			logger::info("Legacy: old Oars and Sails {} {:08X} ('{}', base {:08X}) at ({:.0f},{:.0f},{:.0f}) removed", c.hull ? "hull" : "piece", id,
				base ? base->GetName() : "", base ? base->GetFormID() : 0, c.ref->GetPosition().x, c.ref->GetPosition().y, c.ref->GetPosition().z);
			c.ref->Disable();
			c.ref->SetDelete(true);
			++removed;
			if (c.boat) ++boats;
		}
		g_seen = std::move(now);
		if (removed > 0 && boats == 0 && items == 0) logger::info("Legacy: {} leftover piece(s) removed", removed);
		if (removed > 0 && (boats > 0 || items > 0)) {
			g_removed += removed;
			g_items += items;
			const std::string msg = items > 0 ? std::format("RowRim: an old Oars and Sails boat was cleared away; {} item(s) from its chest are in your inventory", items)
			                                  : std::string("RowRim: an old Oars and Sails boat was cleared away");
			RE::SendHUDMessage::ShowHUDMessage(msg.c_str());
			logger::info("{} ({} piece(s))", msg, removed);
		}
	}
}
