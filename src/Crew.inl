// v1.6.0: crew and passengers, ported from OAS (oas crew add / remove / clear / list / summon, BoardPassengers,
// UpdateLateBoarding, the per-seat numpad tuning), on RowRim's own boat.
// - Followers (player teammates) and anyone added with rr "crew add" board with you when you sit down (E), if they are
//   within 3000: each gets a seat of the rowboat, is held there every frame (collision off, SetDontMove), sits (chair sit
//   events, re-sent every 3 s, a chair idle now and then) and stands up on the floor beside the seat when you stand up.
//   Dogs lie on the floor boards.
// - One who arrives later (within 400 of the boat for 1 s, the boat slow) hops in by themselves, one at a time.
// - Seats (model space, the frame of the player's seats; z is where the sitting actor's feet go): the stern seat (only
//   while you row), the aft thwart left/right, the bow thwart left/right, the middle thwart (only while you sit at the
//   stern). Two dog spots on the floor.
// - rr num: the numpad tunes the seat of whoever is selected (8/2 forward/back, 4/6 left/right, 9/3 up/down, 7/1 turn),
//   Num5 picks the next one aboard. Only the name is shown when you switch; the offsets are saved per seat to RowRim.ini
//   [CrewSeats]. rr "num off" (or rr num again) to stop.
// - The crew list and who was aboard when the game was saved are in the co-save; after a load those are let go (as
//   OAS's TendLoadRelease), so nobody is left frozen with no collision.
namespace Crew
{
	struct Pose
	{
		RE::NiPoint3 at{};      // the boat model's origin in the world
		RE::NiMatrix3 rot{};    // and its rotation
		float yaw = 0.0f;       // the way the boat faces (the stern seat's facing)
		bool playerRows = true; // the player is on the middle thwart (else at the stern seat)
		float deckZ = 31.0f;    // standing height on the floor (model z)
		float speed = 0.0f;
		float scale = 1.0f;     // v1.8.27: the boat drawn this much bigger (the sailing boat): the seats move out with it
		int kind = 0;           // v1.8.51: which boat (0 rowboat, 1 sailing boat, 2 longship): its own seats
	};
	// v1.8.51: ledge: the ledge sit (legs hanging over an edge) instead of the chair sit; standAt: standOff is where they
	// stand up (a spot on the floor, model frame), not an offset from the seat
	struct SeatDef { RE::NiPoint3 pos; float yaw; const char* name; RE::NiPoint3 standOff; bool ledge = false; bool standAt = false; };
	constexpr float kPi = 3.14159265f;
	// humans: z is the sitting feet (the player's seats: 35 on the stern seat, 33 on the middle thwart, thwart tops 64-66)
	inline const SeatDef kHuman[] = {
		// v1.6.3: 5 higher (the chair sit puts the feet at the root: at 33-35 they were 5-7 inside the floorboards, top 40.3)
		{ { 0.0f, -215.0f, 40.0f }, 0.0f, "stern seat", { 0.0f, 65.0f, 0.0f } },
		{ { -34.0f, -84.7f, 39.0f }, 0.0f, "aft thwart, left", { 0.0f, 34.0f, 0.0f } },
		{ { 34.0f, -84.7f, 39.0f }, 0.0f, "aft thwart, right", { 0.0f, 34.0f, 0.0f } },
		{ { -30.0f, 113.7f, 40.0f }, kPi, "bow thwart, left", { 0.0f, -34.0f, 0.0f } },
		{ { 30.0f, 113.7f, 40.0f }, kPi, "bow thwart, right", { 0.0f, -34.0f, 0.0f } },
		{ { 0.0f, 15.0f, 38.0f }, kPi, "middle thwart", { 0.0f, -51.0f, 0.0f } },
	};
	constexpr int kHumans = 6;
	// v1.6.2: one dog, in OAS's dog seat: bowSlot_ (right 0, forward 440, up -73, facing the bow) in the OAS vehicle frame,
	// moved into the model frame as the seats are (OAS rower slot 0, 259, -84 = RowRim's rower seat 0, 15, 33: y - 244,
	// z + 117)
	inline const SeatDef kDog[] = {
		{ { 0.0f, 196.0f, 44.0f }, 0.0f, "bow (the dog's seat)", { 0.0f, -30.0f, 0.0f } },
	};
	constexpr int kDogs = 1;
	// v1.8.51: the sailing boat's own seats (Miguel's sketch), in its model's frame (drawn 1.25 bigger). Chair sits: z is
	// the root, under the seat at the floor (the sit puts the seat about 20.8 higher in the model). Ledge sits: the root
	// stands about 10 over the edge and 12 back from it, the body forward and down onto the edge (as Miguel tuned the
	// starboard one in game). v1.8.52: Miguel's tunes are the seats now; the bench is gone, its sitter on the foredeck's
	// port edge with the chair sit and its fidgets, legs into the well; the one on the rail moved forward onto the low
	// rail past the raised bulwark (his legs went through it). Standing up, each is put on the floorboards in the well
	// first (never in the sea from the rail). Tuned in game: Settings / Crew, Show seat markers. v1.8.53: his later tunes
	// of seats 0 and 3 (Spot0 / Spot3) baked in too; the front ledge (2) is the first follower's seat.
	inline const SeatDef kSailHuman[] = {
		{ { -53.4f, -48.4f, 46.5f }, 2.565f, "foredeck edge, port", { -34.0f, -122.0f, 0.0f }, false, true },
		{ { -32.7f, -200.0f, 49.2f }, -0.541f, "stern seat, beside the helmsman", { -32.7f, -165.0f, 0.0f }, false, true },
		{ { 55.0f, -49.2f, 77.3f }, -2.47f, "foredeck edge, starboard (ledge)", { 40.0f, -112.0f, 0.0f }, true, true },
		{ { -42.3f, 182.8f, 94.2f }, -0.981f, "port rail forward, feet over the side (ledge)", { -30.0f, -112.0f, 0.0f }, true, true },
	};
	inline const SeatDef kSailDog[] = {
		{ { 51.0f, 78.0f, 69.5f }, 0.0f, "foredeck, right of the mast (the dog's seat)", { 26.0f, -124.0f, 0.0f }, false, true },
	};
	constexpr int kSailHumans = 4;
	inline int HumanCount(int a_kind) { return a_kind == 1 ? kSailHumans : kHumans; }
	inline int DogCount(int) { return kDogs; }
	inline const SeatDef& Def(int a_kind, bool a_dog, int a_slot)
	{
		if (a_kind == 1) return a_dog ? kSailDog[0] : kSailHuman[std::clamp(a_slot, 0, kSailHumans - 1)];
		return a_dog ? kDog[std::clamp(a_slot, 0, kDogs - 1)] : kHuman[std::clamp(a_slot, 0, kHumans - 1)];
	}
	inline bool g_markers = false;  // v1.8.51: pink markers on the seats of the boat in use (Settings / Crew)
	constexpr float kBoardRange = 3000.0f;
	constexpr float kLateRange = 400.0f, kLateDwell = 1.0f, kLateMaxSpeed = 60.0f, kLateTime = 0.7f, kLateArc = 40.0f;

	struct Tune { float x = 0.0f, y = 0.0f, z = 0.0f, yaw = 0.0f; };
	struct Rider
	{
		RE::ActorHandle actor;
		RE::FormID id = 0;
		bool dog = false;
		int slot = 0;
		bool ledge = false;     // v1.8.51: on a ledge seat (the ledge sit)
		float sentAt = -100.0f; // v1.8.51: when the ledge sit was last sent
		float sitTimer = 0.0f, idleTimer = 0.0f;
		float idleAt = -100.0f;  // v1.6.3: when a chair idle was last sent (its own stand-up events are not a stand-up)
		// v1.7.35: the dog lies down after sitting a while (and is no longer stood back up when it lies)
		float dogSat = 0.0f;
		float dogLieAfter = 20.0f;
		bool dogLie = false;
		int dogLieStep = 0;  // 0 sit, 1 stood up to lie, 2 gave up
	};
	inline bool g_dogLieLogged = false;
	inline bool DogTryLie(RE::Actor* a_dog)
	{
		for (const char* ev : { "idleLayDownEnter", "IdleLayDownEnter", "idleLayDownStart", "IdleLayDownStart", "idleLayDown", "IdleLayDown", "idleLayStart" }) {
			if (a_dog->NotifyAnimationGraph(ev)) {
				if (!g_dogLieLogged) { g_dogLieLogged = true; logger::info("Crew: the dog lies down ('{}')", ev); }
				return true;
			}
		}
		return false;
	}

	// v1.6.3: OAS v1.0.11-1.0.13: the seated state comes from the actor's own animation events, and the sit is only sent
	// when they are NOT seated. The v1.6.0 blind re-send every 3 s restarted the sit on a seated actor: the flicker and
	// the jump. The sink may run off the main thread: it only sets flags, read on the main thread under the mutex.
	struct SeatFlags { bool seated = false; bool pick = false; bool stood = false; };  // v1.8.51: stood: back to standing (tailMTIdle)
	inline std::mutex g_flagMx;
	inline std::unordered_map<RE::FormID, SeatFlags> g_flags;
	struct AnimSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
	{
		RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
		{
			if (!a_event || !a_event->holder) return RE::BSEventNotifyControl::kContinue;
			const std::string_view tag(a_event->tag.c_str() ? a_event->tag.c_str() : "");
			auto eq = [&](std::string_view a_w) {
				return tag.size() == a_w.size() && std::equal(tag.begin(), tag.end(), a_w.begin(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
			};
			std::lock_guard lock(g_flagMx);
			auto it = g_flags.find(a_event->holder->GetFormID());
			if (it == g_flags.end()) return RE::BSEventNotifyControl::kContinue;
			// OAS NoteCrewEvent: boarding shows IdleStop, tailMTIdle, IdleOffsetStop, idleChairSitting, tailChairIdle: the last wins
			if (eq("tailChairIdle") || eq("idleChairSitting")) it->second.seated = true;
			if (eq("tailMTIdle") || eq("IdleStop") || eq("IdleChairExitStart") || eq("IdleChairExitToStand") || eq("IdleBedExitToStand")) it->second.seated = false;
			if (eq("tailMTIdle")) it->second.stood = true;
			if (eq("PickNewIdle")) it->second.pick = true;
			return RE::BSEventNotifyControl::kContinue;
		}
	};
	inline AnimSink g_sink;
	inline SeatFlags TakeFlags(RE::FormID a_id)
	{
		std::lock_guard lock(g_flagMx);
		auto it = g_flags.find(a_id);
		if (it == g_flags.end()) return {};
		SeatFlags f = it->second;
		it->second.pick = false;
		return f;
	}
	struct State
	{
		std::vector<RE::FormID> crew;      // rr "crew add"
		std::vector<Rider> riders;         // aboard now
		bool aboard = false;               // the player is seated (riders are held)
		bool lingering = false;            // v1.7.21: you stood up, they stay seated until the boat has settled
		float lingerT = 0.0f, lingerTotal = 0.0f;
		Pose pose{};
		bool havePose = false;
		// late boarding
		bool hop = false;
		RE::ActorHandle hopActor;
		bool hopDog = false;
		int hopSlot = 0;
		float hopT = 0.0f;
		RE::NiPoint3 hopFrom{};
		float lateTimer = 0.0f;
		std::vector<std::pair<RE::FormID, float>> dwell;
		// summon
		std::vector<RE::FormID> summon;
		float summonTimer = 0.0f;
		int summonIndex = 0, summonTotal = 0;
		RE::NiPoint3 summonCentre{};
		// after a load: let these go once they are loaded
		std::vector<RE::FormID> release;
		float releaseTimer = 0.0f;
		// numpad
		bool numOn = false;
		int sel = 0;
		bool held[10]{};
		bool tuneDirty = false;
		float time = 0.0f;
		// v1.7.9: with the player at the stern seat, one of the people aboard rows (the middle thwart, slot 5)
		RE::FormID rowerId = 0;
		bool rowerRowing = false;
		bool dogStern = false;  // v1.7.36: the bed is out: the dog's seat is at the stern, facing aft  // the row clip is on them (no chair sit re-sent meanwhile)
	};
	inline State g;

	inline void Say(const std::string& a_text)
	{
		logger::info("{}", a_text);
		if (auto* c = RE::ConsoleLog::GetSingleton()) c->Print("%s", a_text.c_str());
	}
	inline void Hud(const std::string& a_text) { RE::SendHUDMessage::ShowHUDMessage(a_text.c_str()); }

	inline void DontMove(RE::Actor* a_actor, bool a_value)
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
	// v1.7.12: any canine: the race's editor ID or name has dog / hound / husky / mastiff / canine / wolf in it (Menagerie
	// and other pet mods use their own races), never a person
	inline bool IsDog(RE::Actor* a_actor)
	{
		const auto* race = a_actor ? a_actor->GetRace() : nullptr;
		if (!race || race->HasKeywordString("ActorTypeNPC")) return false;
		std::string hay;
		if (const char* id = race->GetFormEditorID()) hay += id;
		if (const char* nm = race->GetName()) { hay += ' '; hay += nm; }
		std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		for (const char* k : { "dog", "hound", "husky", "mastiff", "canine", "wolf" })
			if (hay.find(k) != std::string::npos) return true;
		return false;
	}
	// v1.7.12: a follower: a player teammate, or in the game's CurrentFollowerFaction (some frameworks keep animal
	// followers out of the teammate flag)
	inline bool IsFollower(RE::Actor* a_actor)
	{
		if (!a_actor) return false;
		// v1.7.40 (log 12:23:25): Meeko sent home stays a player teammate but leaves the follower faction ("teammate yes,
		// follower rank -99"). A dog follows only while in CurrentFollowerFaction at rank 0+; a person while a teammate or in
		// the faction at rank 0+ (dismissed people lose the teammate flag and drop to rank -1)
		static auto* fac = RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84E);  // CurrentFollowerFaction
		const bool ranked = fac && a_actor->IsInFaction(fac) && a_actor->GetFactionRank(fac, false) >= 0;
		if (IsDog(a_actor)) return ranked;
		return ranked || a_actor->IsPlayerTeammate();
	}
	inline bool IsPerson(RE::Actor* a_actor)
	{
		const auto* race = a_actor ? a_actor->GetRace() : nullptr;
		return race && race->HasKeywordString("ActorTypeNPC");
	}
	inline bool IsCrew(RE::Actor* a_actor) { return a_actor && std::find(g.crew.begin(), g.crew.end(), a_actor->GetFormID()) != g.crew.end(); }
	inline bool IsAboard(RE::Actor* a_actor)
	{
		if (!a_actor) return false;
		for (auto& r : g.riders) if (r.id == a_actor->GetFormID()) return true;
		return false;
	}
	inline int CrewHumans()
	{
		int n = 0;
		for (auto id : g.crew) if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id); !a || !IsDog(a)) ++n;
		return n;
	}

	// ---- v1.7.9: the crew rower ----
	constexpr int kRowerSlot = 5;  // the middle thwart, facing aft (the player's own oar seat)
	inline Rider* RowerRider()
	{
		if (!g.rowerId) return nullptr;
		for (auto& r : g.riders) if (r.id == g.rowerId && !r.dog && r.slot == kRowerSlot) return &r;
		return nullptr;
	}
	// the one at the oars while the player sits at the stern (alive, drawn), or nullptr
	inline RE::Actor* Rower()
	{
		auto* r = RowerRider();
		if (!r) return nullptr;
		auto a = r->actor.get();
		if (!a || a->IsDead() || !a->Is3DLoaded()) return nullptr;
		return a.get();
	}
	// a person aboard is picked at random and moved onto the middle thwart (swapping with whoever sat there)
	inline void PickRower(const Pose& a_p)
	{
		if (a_p.playerRows) { g.rowerId = 0; g.rowerRowing = false; return; }
		if (Rower()) return;
		g.rowerId = 0;
		g.rowerRowing = false;
		std::vector<Rider*> people;
		for (auto& r : g.riders) {
			if (r.dog) continue;
			auto a = r.actor.get();
			if (a && !a->IsDead() && a->Is3DLoaded()) people.push_back(&r);
		}
		if (people.empty()) return;
		Rider* pick = people[(std::min)(people.size() - 1, static_cast<std::size_t>(Plankton::Rand(0.0f, static_cast<float>(people.size()))))];
		if (pick->slot != kRowerSlot) {
			for (auto& r : g.riders) {
				if (&r != pick && !r.dog && r.slot == kRowerSlot) { r.slot = pick->slot; r.sitTimer = 0.0f; break; }
			}
			pick->slot = kRowerSlot;
			pick->sitTimer = 0.0f;
		}
		g.rowerId = pick->id;
		auto a = pick->actor.get();
		logger::info("Crew: {} {:08X} takes the oars (you at the stern)", a ? a->GetName() : "", pick->id);
		if (a) logger::info("Crew: {} takes the oars", a->GetName());  // v1.7.68: no HUD text (you see them row)
	}
	inline void ResitRower()
	{
		if (auto* r = RowerRider()) {
			if (auto a = r->actor.get()) {
				for (const char* ev : { "IdleChairEnterInstant", "IdleChairSitting" }) {
					if (a->NotifyAnimationGraph(ev)) break;
				}
			}
		}
	}

	// ---- seat tunes (RowRim.ini [CrewSeats]; v1.8.26: each boat its own section; v1.8.51: cached per boat, the sailing
	// boat's new seats under their own keys Place0.. / DogPlace0 (its old Seat tunes were for the rowboat's seats)) ----
	struct KindTunes { bool loaded = false; Tune human[kHumans]{}, dog[kDogs]{}; };
	inline KindTunes g_tunes[3];
	// v1.8.52: the sailing boat's keys Spot0.. / DogSpot0 (its Place tunes of v1.8.51 are in the seats themselves now)
	inline std::string TuneKey(int a_kind, bool a_dog, int a_slot) { return a_kind == 1 ? std::format("{}{}", a_dog ? "DogSpot" : "Spot", a_slot) : std::format("{}{}", a_dog ? "Dog" : "Seat", a_slot); }
	inline KindTunes& TunesOf(int a_kind)
	{
		a_kind = std::clamp(a_kind, 0, 2);
		auto& kt = g_tunes[a_kind];
		if (kt.loaded) return kt;
		kt.loaded = true;
		const auto ini = Plankton::IniPath();
		auto read = [&](const std::string& a_key, Tune& a_t) {
			char buf[96]{};
			GetPrivateProfileStringA(RowRimBakedIni::BoatSec(a_kind, "CrewSeats"), a_key.c_str(), "", buf, sizeof(buf), ini.c_str());
			float v[4]{};
			if (std::sscanf(buf, "%f %f %f %f", &v[0], &v[1], &v[2], &v[3]) == 4) a_t = { v[0], v[1], v[2], v[3] / 57.2958f };
		};
		for (int i = 0; i < kHumans; ++i) read(TuneKey(a_kind, false, i), kt.human[i]);
		for (int i = 0; i < kDogs; ++i) read(TuneKey(a_kind, true, i), kt.dog[i]);
		return kt;
	}
	inline Tune& TuneOf(int a_kind, bool a_dog, int a_slot)
	{
		auto& kt = TunesOf(a_kind);
		return a_dog ? kt.dog[std::clamp(a_slot, 0, kDogs - 1)] : kt.human[std::clamp(a_slot, 0, kHumans - 1)];
	}
	inline void SaveTuneK(int a_kind, bool a_dog, int a_slot)
	{
		const Tune& t = TuneOf(a_kind, a_dog, a_slot);
		const std::string v = std::format("{:.1f} {:.1f} {:.1f} {:.1f}", t.x, t.y, t.z, t.yaw * 57.2958f);
		WritePrivateProfileStringA(RowRimBakedIni::BoatSec(a_kind, "CrewSeats"), TuneKey(a_kind, a_dog, a_slot).c_str(), v.c_str(), Plankton::IniPath().c_str());
	}
	inline void LoadTunes() { TunesOf(g.pose.kind); }
	inline void ReloadTunes() {}  // v1.8.51: each boat's own are kept apart (nothing to reload on a change of boat)
	inline void SaveTune(bool a_dog, int a_slot) { SaveTuneK(g.pose.kind, a_dog, a_slot); }
	// v1.8.51: the menu's seat tuning (Settings / Crew): a_save on letting go of a slider
	inline void SetTune(int a_kind, bool a_dog, int a_slot, const Tune& a_t, bool a_save)
	{
		TuneOf(a_kind, a_dog, a_slot) = a_t;
		if (a_save) {
			SaveTuneK(a_kind, a_dog, a_slot);
			logger::info("Crew: {} seat '{}' tuned: {:.1f} {:.1f} {:.1f} turn {:.0f} deg", a_kind == 1 ? "sailing boat" : a_kind == 2 ? "longship" : "rowboat", Def(a_kind, a_dog, a_slot).name, a_t.x, a_t.y, a_t.z, a_t.yaw * 57.2958f);
		}
	}

	// v1.8.51: the seat's spot in the boat's frame (game units, unrotated) and the way it faces from the bow
	inline RE::NiPoint3 SeatLocal(int a_kind, float a_scale, bool a_dog, int a_slot, float& a_yaw)
	{
		if (a_dog && g.dogStern && a_kind == 0) {  // v1.7.36: out of the bed's way, on the floorboards forward of the stern seat, looking aft
			a_yaw = 3.14159265f;
			return RE::NiPoint3{ 0.0f, -168.0f, 43.0f } * a_scale;
		}
		const SeatDef& s = Def(a_kind, a_dog, a_slot);
		const Tune& t = TuneOf(a_kind, a_dog, a_slot);
		a_yaw = s.yaw + t.yaw;
		return RE::NiPoint3{ s.pos.x * a_scale + t.x, s.pos.y * a_scale + t.y, s.pos.z * a_scale + t.z };
	}
	// where a rider sits (world) and faces
	inline RE::NiPoint3 SeatWorld(const Pose& a_p, bool a_dog, int a_slot, float& a_yaw)
	{
		float yl = 0.0f;
		const RE::NiPoint3 l = SeatLocal(a_p.kind, a_p.scale, a_dog, a_slot, yl);
		a_yaw = a_p.yaw + yl;
		return a_p.at + a_p.rot * l;
	}
	// v1.8.51: where one gets up (world): a seat's own floor spot, or by the seat
	inline RE::NiPoint3 StandWorld(const Pose& a_p, bool a_dog, int a_slot, bool a_tuned)
	{
		const SeatDef& s = Def(a_p.kind, a_dog, a_slot);
		const Tune& t = TuneOf(a_p.kind, a_dog, a_slot);
		const float up = a_p.deckZ + (a_dog ? 14.0f : 4.0f);
		const RE::NiPoint3 l = s.standAt ? RE::NiPoint3{ s.standOff.x * a_p.scale, s.standOff.y * a_p.scale, up }
		                                 : RE::NiPoint3{ (s.pos.x + s.standOff.x) * a_p.scale + (a_tuned ? t.x : 0.0f), (s.pos.y + s.standOff.y) * a_p.scale + (a_tuned ? t.y : 0.0f), up };
		return a_p.at + a_p.rot * l;
	}
	inline bool SlotFree(bool a_dog, int a_slot, const Pose& a_p)
	{
		if (!a_dog && a_p.kind != 1) {  // v1.8.51: the sailing boat's seats are all the passengers' (you are at the helm)
			if (a_slot == 0 && !a_p.playerRows) return false;  // the stern seat is the player's
			if (a_slot == 5 && a_p.playerRows) return false;   // the middle thwart is the player's
		}
		for (auto& r : g.riders) if (r.dog == a_dog && r.slot == a_slot) return false;
		if (g.hop && g.hopDog == a_dog && g.hopSlot == a_slot) return false;
		return true;
	}
	inline int FreeSlot(bool a_dog, const Pose& a_p)
	{
		static constexpr int kOrder[] = { 1, 2, 3, 4, 0, 5 };  // the thwarts first, then the stern seat / middle thwart
		if (a_dog) {
			for (int i = 0; i < DogCount(a_p.kind); ++i) if (SlotFree(true, i, a_p)) return i;
			return -1;
		}
		if (a_p.kind == 1) {  // v1.8.53: the front ledge (starboard foredeck edge) first, then port foredeck edge, beside you, the rail
			static constexpr int kSailOrder[] = { 2, 0, 1, 3 };
			for (int i : kSailOrder) if (SlotFree(false, i, a_p)) return i;
			return -1;
		}
		for (int i : kOrder) if (SlotFree(false, i, a_p)) return i;
		return -1;
	}

	inline bool Candidate(RE::Actor* a_actor)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!a_actor || a_actor == player || a_actor->IsDead() || a_actor->IsDisabled() || !a_actor->Is3DLoaded()) return false;
		if (!IsFollower(a_actor) && !IsCrew(a_actor)) return false;
		if (!IsDog(a_actor) && !IsPerson(a_actor)) return false;
		if (a_actor->IsOnMount()) return false;
		return !IsAboard(a_actor);
	}

	// v1.7.22: seated, weapons are sheathed (kept equipped) and shields taken off; the shield goes back on when they stand
	inline std::unordered_map<RE::FormID, RE::FormID> g_shields;
	inline void Stow(RE::Actor* a_actor)
	{
		if (!a_actor) return;
		if (auto* st = a_actor->AsActorState(); st && st->IsWeaponDrawn()) a_actor->DrawWeaponMagicHands(false);
		auto* sh = a_actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kShield);
		auto* em = RE::ActorEquipManager::GetSingleton();
		if (!sh || !em) return;
		em->UnequipObject(a_actor, sh, nullptr, 1, nullptr, false, false, false, true);
		g_shields[a_actor->GetFormID()] = sh->GetFormID();
		logger::info("Crew: {} {:08X} shield {} off for the ride", a_actor->GetName(), a_actor->GetFormID(), sh->GetName());
	}
	inline void Unstow(RE::Actor* a_actor)
	{
		if (!a_actor) return;
		const auto it = g_shields.find(a_actor->GetFormID());
		if (it == g_shields.end()) return;
		auto* sh = RE::TESForm::LookupByID<RE::TESObjectARMO>(it->second);
		g_shields.erase(it);
		auto* em = RE::ActorEquipManager::GetSingleton();
		if (!sh || !em || a_actor->IsDead()) return;
		const auto counts = a_actor->GetInventoryCounts([sh](RE::TESBoundObject& a_o) { return &a_o == sh; });
		if (counts.empty() || counts.begin()->second <= 0) return;  // no longer carried
		em->EquipObject(a_actor, sh, nullptr, 1, nullptr, false, false, false, true);
		logger::info("Crew: {} {:08X} shield {} back on", a_actor->GetName(), a_actor->GetFormID(), sh->GetName());
	}

	inline bool g_ledgeLogged = false;
	inline void SitNow(Rider& a_r, RE::Actor* a_actor)
	{
		if (a_r.dog) {
			a_actor->NotifyAnimationGraph("idleSitStart");
			return;
		}
		if (a_r.ledge) {  // v1.8.51: the ledge sit (the game's own, legs over the edge)
			const char* took = nullptr;
			for (const char* ev : { "IdleSitLedgeEnterInstant", "IdleSitLedgeEnter" }) {
				if (a_actor->NotifyAnimationGraph(ev)) { took = ev; break; }
			}
			a_r.sentAt = g.time;
			{
				std::lock_guard lock(g_flagMx);
				auto it = g_flags.find(a_r.id);
				if (it != g_flags.end()) it->second.stood = false;
			}
			if (!g_ledgeLogged || !took) { g_ledgeLogged = true; logger::info("Crew: {:08X} ledge sit {}", a_r.id, took ? took : "refused (no ledge sit in their graph)"); }
			return;
		}
		for (const char* ev : { "IdleChairEnterInstant", "IdleChairSitting" }) {
			if (a_actor->NotifyAnimationGraph(ev)) break;
		}
	}

	inline void Seat(RE::Actor* a_actor, bool a_dog, int a_slot, const Pose& a_p, const char* a_how)
	{
		a_actor->SetCollision(false);
		DontMove(a_actor, true);
		for (const char* ev : { "swimStop", "SwimStop", "IdleStop" }) a_actor->NotifyAnimationGraph(ev);
		if (!a_dog) Stow(a_actor);  // v1.7.22
		if (!a_dog) {
			{
				std::lock_guard lock(g_flagMx);
				g_flags[a_actor->GetFormID()] = SeatFlags{};
			}
			a_actor->AddAnimationGraphEventSink(&g_sink);
		}
		Rider r;
		r.actor = a_actor->GetHandle();
		r.id = a_actor->GetFormID();
		r.dog = a_dog;
		r.slot = a_slot;
		r.ledge = !a_dog && Def(a_p.kind, false, a_slot).ledge;  // v1.8.51
		r.sitTimer = 0.0f;
		r.idleTimer = Plankton::Rand(15.0f, 40.0f);
		r.dogLieAfter = Plankton::Rand(15.0f, 35.0f);  // v1.7.35
		float yaw = 0.0f;
		a_actor->SetPosition(SeatWorld(a_p, a_dog, a_slot, yaw), true);
		a_actor->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, yaw });
		SitNow(r, a_actor);
		g.riders.push_back(r);
		static auto* fol = RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84E);
		logger::info("Crew: {} {:08X} {} ({}; teammate {}, follower rank {}, crew {})", a_actor->GetName(), r.id, a_how, Def(a_p.kind, a_dog, a_slot).name,
			a_actor->IsPlayerTeammate() ? "yes" : "no", fol && a_actor->IsInFaction(fol) ? a_actor->GetFactionRank(fol, false) : -99, IsCrew(a_actor) ? "yes" : "no");
	}

	// the player sat down: everyone eligible within 3000 boards
	inline void Board(const Pose& a_p)
	{
		g.pose = a_p;
		g.havePose = true;
		if (g.aboard) return;  // already seated (no second boarding)
		g.aboard = true;
		g.riders.clear();
		g.hop = false;
		g.dwell.clear();
		g.lateTimer = 0.0f;
		auto* lists = RE::ProcessLists::GetSingleton();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!lists || !player) return;
		const RE::NiPoint3 pp = player->GetPosition();
		int n = 0;
		for (auto& h : lists->highActorHandles) {
			auto a = h.get();
			if (!a || !Candidate(a.get())) continue;
			if (a->GetPosition().GetDistance(pp) > kBoardRange) continue;
			const bool dog = IsDog(a.get());
			const int slot = FreeSlot(dog, a_p);
			if (slot < 0) { logger::info("Crew: no free {} for {:08X}", dog ? "dog spot" : "seat", a->GetFormID()); continue; }
			Seat(a.get(), dog, slot, a_p, "boarded");
			++n;
		}
		if (n) logger::info("Crew: {} aboard", n);
		// v1.7.12: animals close by that did not board, and why (the dog that would not get in)
		for (auto& h : lists->highActorHandles) {
			auto a = h.get();
			if (!a || a.get() == player || IsAboard(a.get()) || IsPerson(a.get())) continue;
			if (a->GetPosition().GetDistance(pp) > kBoardRange) continue;
			const auto* race = a->GetRace();
			const bool fol = IsFollower(a.get());
			if (!fol && !IsDog(a.get())) continue;
			logger::info("Crew: {} {:08X} (race '{}') not aboard: {}{}{}{}", a->GetName(), a->GetFormID(), race && race->GetFormEditorID() ? race->GetFormEditorID() : "?",
				fol ? "" : "not a follower; ", IsDog(a.get()) ? "" : "not a dog race; ", a->Is3DLoaded() ? "" : "not drawn; ", a->IsDead() ? "dead" : "");
		}
		if (g.sel >= static_cast<int>(g.riders.size())) g.sel = 0;
		PickRower(a_p);  // v1.7.9: you at the stern, one of them rows
	}

	inline void LetGo(RE::Actor* a_actor)
	{
		a_actor->RemoveAnimationGraphEventSink(&g_sink);
		{
			std::lock_guard lock(g_flagMx);
			g_flags.erase(a_actor->GetFormID());
		}
		a_actor->SetCollision(true);
		DontMove(a_actor, false);
		for (const char* ev : { "IdleChairExitStart", "IdleForceDefaultState" }) a_actor->NotifyAnimationGraph(ev);
		for (const char* v : { "bIsSitting", "isIdleSitting", "isInFurniture", "bHumanoidFootIKDisable" }) a_actor->SetGraphVariableBool(v, false);
		Unstow(a_actor);  // v1.7.22
	}

	// everyone stands on the floor by their seat, free to walk again
	inline void StandAll(const Pose& a_p)
	{
		g.lingering = false;
		for (auto& r : g.riders) {
			auto a = r.actor.get();
			if (!a) continue;
			if (a->IsDead() || !a->Is3DLoaded()) { LetGo(a.get()); continue; }
			// v1.7.21: put down first with no speed, a little higher for the dog (its body overlapped the hull's collision
			// when that came back on, and the physics threw it out), then its collision on. v1.8.51: the sailing boat's
			// seats each have their own spot on the floorboards (from the rail: in the boat, never the sea)
			a->SetPosition(StandWorld(a_p, r.dog, r.slot, true), true);
			if (auto* cc = a->GetCharController()) cc->SetLinearVelocityImpl(RE::hkVector4());
			LetGo(a.get());
			if (auto* cc = a->GetCharController()) cc->SetLinearVelocityImpl(RE::hkVector4());
		}
		if (auto a = g.hop ? g.hopActor.get() : RE::NiPointer<RE::Actor>{}) LetGo(a.get());
		if (!g.riders.empty()) logger::info("Crew: {} stood up", g.riders.size());
		g.riders.clear();
		g.hop = false;
		g.aboard = false;
		g.rowerId = 0;
		g.rowerRowing = false;
	}

	// v1.7.21: the player stood up: the passengers (and the dog) stay seated until the boat has settled (under 25 for 1.2 s,
	// 8 s at most), so a boat still gliding or stopped hard does not throw them about; then they stand
	inline void Release(const Pose& a_p)
	{
		if (!g.aboard || g.riders.empty()) { StandAll(a_p); return; }
		if (g.lingering) return;
		g.lingering = true;
		g.lingerT = g.lingerTotal = 0.0f;
		logger::info("Crew: you stood up, {} stay seated until you walk off", g.riders.size());
	}
	inline bool Lingering() { return g.lingering; }
	inline void Glue(const Pose& a_p, float a_dt, bool a_linger = false);  // below
	// every frame while they wait (the player standing, aboard or not): held on their seats on the boat, then up.
	// v1.7.28: they stay seated (followers and the dog) until you have walked off 600 from the boat, then get up and follow
	// (was: up once the boat settled, 8 s at most)
	inline float kLingerLeave = 400.0f;  // v1.7.29: 400 (about 6 m), RowRim.ini [Crew] StandUpAt
	inline bool g_leaveRead = false;
	inline void ReadLeave()
	{
		if (g_leaveRead) return;
		g_leaveRead = true;
		const auto ini = Plankton::IniPath();
		char b[32]{};
		GetPrivateProfileStringA("Crew", "StandUpAt", "", b, sizeof(b), ini.c_str());
		if (!b[0]) { WritePrivateProfileStringA("Crew", "StandUpAt", "400", ini.c_str()); return; }
		try { kLingerLeave = std::clamp(std::stof(b), 150.0f, 5000.0f); } catch (...) {}
		logger::info("Crew: passengers get up when you are {:.0f} from the boat", kLingerLeave);
	}
	inline float PlayerFrom(const Pose& a_p)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) return 0.0f;
		const RE::NiPoint3 p = player->GetPosition();
		return std::hypot(p.x - a_p.at.x, p.y - a_p.at.y);
	}
	inline void Linger(const Pose& a_p, float a_dt)
	{
		if (!g.lingering) return;
		ReadLeave();
		Glue(a_p, a_dt, true);
		g.lingerTotal += a_dt;
		const float d = PlayerFrom(a_p);
		if (d > kLingerLeave) {
			logger::info("Crew: you are {:.0f} from the boat after {:.1f} s, they get up and follow", d, g.lingerTotal);
			StandAll(a_p);
		}
	}
	// every player update, boat or not: still waiting but the boat no longer updates them (fast travel, a door, the boat
	// gone): they get up
	inline void LingerSafety()
	{
		if (!g.lingering || !g.havePose) return;
		if (PlayerFrom(g.pose) > kLingerLeave * 2.5f) {
			logger::info("Crew: you left the boat behind, the passengers get up");
			StandAll(g.pose);
		}
	}

	// every frame while the player is seated (after the player's own glue); a_linger: the player stood up, they wait
	// v1.8.22: seated aboard, they start no greetings, no "hello" as you pass and no idle remarks (v1.8.22b: they may
	// still chat with each other: the conversation timer is left alone):
	// the AI's cooldowns for those are held up every frame (they count down to the next allowed line). Lines the game
	// plays on purpose still play (the bump reactions, anything you start by talking to them).
	inline void Hush(RE::Actor* a_actor)
	{
		auto* proc = a_actor->GetActorRuntimeData().currentProcess;
		auto* high = proc ? proc->high : nullptr;
		if (!high) return;
		high->greetingTimer = (std::max)(high->greetingTimer, 20.0f);
		high->detectGreetTimer = (std::max)(high->detectGreetTimer, 20.0f);
		high->idleTimer = (std::max)(high->idleTimer, 20.0f);
	}

	// v1.8.29c: you sat where one of them still sat (they waited seated after you got up, then you took their seat: you sat
	// inside them): they move over to a free seat, or stand up if there is none
	inline void MakeRoom(const Pose& a_p)
	{
		for (auto it = g.riders.begin(); it != g.riders.end();) {
			const bool mine = !it->dog && ((a_p.playerRows && it->slot == 5) || (!a_p.playerRows && it->slot == 0));
			if (!mine) { ++it; continue; }
			if (it->id == g.rowerId) { g.rowerId = 0; g.rowerRowing = false; }
			const int was = it->slot;
			it->slot = -1;
			const int ns = FreeSlot(false, a_p);
			auto a = it->actor.get();
			if (ns >= 0) {
				it->slot = ns;
				it->sitTimer = 0.0f;
				logger::info("Crew: {:08X} moves over from seat {} to {} (your seat now)", it->id, was, ns);
				++it;
			} else {
				if (a) {
					a->SetPosition(StandWorld(a_p, false, was, false), true);
					if (auto* cc = a->GetCharController()) cc->SetLinearVelocityImpl(RE::hkVector4());
					LetGo(a.get());
				}
				logger::info("Crew: {:08X} gets up from seat {} for you (no other seat free)", it->id, was);
				it = g.riders.erase(it);
			}
		}
	}
	inline void Glue(const Pose& a_p, float a_dt, bool a_linger)
	{
		g.time += a_dt;
		g.pose = a_p;
		g.havePose = true;
		if (!g.aboard) return;
		if (!a_linger && g.lingering) { g.lingering = false; logger::info("Crew: you sat down again, they stay seated"); }
		if (!a_linger) MakeRoom(a_p);  // v1.8.29c: never two in one seat
		if (!a_linger && !a_p.playerRows && !g.riders.empty() && !Rower()) PickRower(a_p);  // v1.7.9: a new rower if the last one left
		for (auto it = g.riders.begin(); it != g.riders.end();) {
			auto a = it->actor.get();
			if (!a || a->IsDead()) {
				if (a) LetGo(a.get());
				logger::info("Crew: {:08X} left the boat ({})", it->id, a ? "dead" : "gone");
				it = g.riders.erase(it);
				continue;
			}
			// v1.7.38: dismissed while aboard (no longer a follower, not in the crew): put on the floor by their seat and let go,
			// never taken aboard again unless they follow you again
			if (!IsFollower(a.get()) && !IsCrew(a.get())) {
				a->SetPosition(StandWorld(a_p, it->dog, it->slot, false), true);
				if (auto* cc = a->GetCharController()) cc->SetLinearVelocityImpl(RE::hkVector4());
				LetGo(a.get());
				if (it->id == g.rowerId) { g.rowerId = 0; g.rowerRowing = false; }
				logger::info("Crew: {} {:08X} was dismissed, off the seat and let go", a->GetName(), it->id);
				it = g.riders.erase(it);
				continue;
			}
			if (!a->Is3DLoaded()) { ++it; continue; }
			float yaw = 0.0f;
			a->SetPosition(SeatWorld(a_p, it->dog, it->slot, yaw), true);
			// OAS: people stay upright; the dog leans with the boat's pitch
			float pitchX = 0.0f;
			if (it->dog) {
				const RE::NiPoint3 f = a_p.rot * RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
				pitchX = -std::asin(std::clamp(f.z, -1.0f, 1.0f));
			}
			a->SetAngle(RE::NiPoint3{ pitchX, 0.0f, yaw });
			if (auto* cc = a->GetCharController()) cc->SetLinearVelocityImpl(RE::hkVector4());
			a->SetGraphVariableBool("bHumanoidFootIKDisable", true);  // v1.6.5: foot IK lifted the feet onto the hull's collision
			Hush(a.get());  // v1.8.22: no proximity chatter aboard
			if ((it->sitTimer -= a_dt) <= 0.0f) {
				if (it->dog) {
					// OAS sit control: a dog only sits from a standing idle; lying down it must stand first (IdleStop)
					// v1.7.35: it sits, and after 15-35 s lies down where it is and is left lying (it was stood back up
					// every time it lay down: the dog never lay at its seat)
					it->sitTimer = 0.25f;
					bool sitting = false, lying = false;
					a->GetGraphVariableBool("isIdleSitting", sitting);
					a->GetGraphVariableBool("isIdleLay", lying);
					if (lying) {
						it->dogLie = true;
						it->dogLieStep = 0;
					} else if (it->dogLie && it->dogLieStep < 2) {
						// wants to lie: from sitting it may have to stand first
						if (DogTryLie(a.get())) it->dogLieStep = 0;
						else if (sitting && it->dogLieStep == 0) { a->NotifyAnimationGraph("IdleStop"); it->dogLieStep = 1; }
						else if (!sitting && it->dogLieStep == 1) {
							it->dogLieStep = 2;  // no lie-down event in this dog's graph: back to sitting
							it->dogLie = false;
							logger::info("Crew: {:08X} has no lie-down idle, it sits", it->id);
						}
					} else if (sitting) {
						it->dogSat += 0.25f;
						if (it->dogLieStep < 2 && it->dogSat >= it->dogLieAfter) { it->dogLie = true; it->dogLieStep = 0; }
					} else {
						a->NotifyAnimationGraph("idleSitStart");
					}
				} else {
					// v1.6.3 (OAS 1.0.13): checked 4 times a second, the sit sent only when they are not seated
					it->sitTimer = 0.25f;
					// v1.7.22: a weapon drawn again aboard (out of combat) is put away
					if (auto* st = a->AsActorState(); st && st->IsWeaponDrawn() && !a->IsInCombat()) a->DrawWeaponMagicHands(false);
					const SeatFlags f = TakeFlags(it->id);
					const bool rowing = g.rowerRowing && it->id == g.rowerId;  // v1.7.9: the row clip, not a chair sit
					const bool ledge = Def(a_p.kind, false, it->slot).ledge;  // v1.8.51
					if (ledge != it->ledge) { it->ledge = ledge; SitNow(*it, a.get()); }
					else if (ledge) { if (f.stood && g.time - it->sentAt > 2.0f) SitNow(*it, a.get()); }  // only once they are up again
					else if (!f.seated && !rowing && g.time - it->idleAt > 4.0f) SitNow(*it, a.get());
				}
			}
			if (!it->dog && !it->ledge && it->id != g.rowerId && (it->idleTimer -= a_dt) <= 0.0f) {
				it->idleTimer = Plankton::Rand(25.0f, 55.0f);
				const SeatFlags f = TakeFlags(it->id);
				if (f.seated) {  // a chair idle only from the seat
					static constexpr const char* kIdles[] = { "idleChairLookDown", "idleChairArmsCrossedVar1", "idleChairShoulderFlex", "idleChairVar2" };
					a->NotifyAnimationGraph(kIdles[static_cast<int>(Plankton::Rand(0.0f, 3.99f))]);
					it->idleAt = g.time;
				}
			}
			++it;
		}
		if (a_linger) return;  // v1.7.21: nobody hops in while they wait to get up
		// late boarding (OAS UpdateLateBoarding): one hop at a time
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (g.hop) {
			auto a = g.hopActor.get();
			if (!a || a->IsDead()) { g.hop = false; return; }
			if (!a->Is3DLoaded()) return;
			g.hopT = (std::min)(1.0f, g.hopT + a_dt / kLateTime);
			const float e = g.hopT * g.hopT * (3.0f - 2.0f * g.hopT);
			float yaw = 0.0f;
			const RE::NiPoint3 seat = SeatWorld(a_p, g.hopDog, g.hopSlot, yaw);
			RE::NiPoint3 pos = g.hopFrom + (seat - g.hopFrom) * e;
			pos.z += kLateArc * std::sin(kPi * g.hopT);
			a->SetPosition(pos, true);
			a->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, yaw });
			if (g.hopT >= 1.0f) {
				g.hop = false;
				Seat(a.get(), g.hopDog, g.hopSlot, a_p, "hopped in late");
			}
			return;
		}
		if (std::abs(a_p.speed) > kLateMaxSpeed) { g.dwell.clear(); g.lateTimer = 0.0f; return; }
		if ((g.lateTimer += a_dt) < 0.25f) return;
		const float step = g.lateTimer;
		g.lateTimer = 0.0f;
		auto* lists = RE::ProcessLists::GetSingleton();
		auto* ui = RE::UI::GetSingleton();
		if (!lists || !player || (ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME))) return;
		std::vector<std::pair<RE::FormID, float>> kept;
		for (auto& h : lists->highActorHandles) {
			auto a = h.get();
			if (!a || !Candidate(a.get())) continue;
			const RE::NiPoint3 ap = a->GetPosition();
			if (std::hypot(ap.x - a_p.at.x, ap.y - a_p.at.y) > kLateRange) continue;
			const auto* st = a->AsActorState();
			if (a->IsInCombat() || a->GetCurrentScene() || (st && st->IsBleedingOut())) continue;
			float d = step;
			for (auto& [id, t] : g.dwell) if (id == a->GetFormID()) d += t;
			if (d >= kLateDwell) {
				const bool dog = IsDog(a.get());
				const int slot = FreeSlot(dog, a_p);
				if (slot < 0) continue;
				g.hop = true;
				g.hopActor = a->GetHandle();
				g.hopDog = dog;
				g.hopSlot = slot;
				g.hopT = 0.0f;
				g.hopFrom = ap;
				a->SetCollision(false);
				DontMove(a.get(), true);
				logger::info("Crew: {} {:08X} arrived, hopping in", a->GetName(), a->GetFormID());
				g.dwell.clear();
				return;
			}
			kept.emplace_back(a->GetFormID(), d);
		}
		g.dwell = std::move(kept);
	}

	// v1.6.4: right before drawing (the camera update, after TestHull places the boat model where it is drawn), each
	// rider's 3D is moved onto their seat on that drawn boat. The glue (SetPosition) alone left them where the actor
	// update had put them, a frame of travel off the drawn boat: displaced by the rowing speed, worse on each stroke's
	// surge. Only the root's position; a gap over 60 is a load or a teleport and left alone.
	inline void LatePin(const Pose& a_p)
	{
		if (!g.aboard) return;
		for (auto& r : g.riders) {
			auto a = r.actor.get();
			if (!a || a->IsDead()) continue;
			auto* root = a->Get3D(false);
			if (!root) continue;
			float yaw = 0.0f;
			const RE::NiPoint3 want = SeatWorld(a_p, r.dog, r.slot, yaw);
			const RE::NiPoint3 d = want - root->world.translate;
			const float len = d.Length();
			if (len < 0.01f || len > 60.0f) continue;
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

	// ---- numpad seat tuning ----
	inline Rider* Selected()
	{
		if (g.riders.empty()) return nullptr;
		if (g.sel < 0 || g.sel >= static_cast<int>(g.riders.size())) g.sel = 0;
		return &g.riders[g.sel];
	}
	inline void ShowSelected()
	{
		auto* r = Selected();
		if (!r) { Hud("Seat tuning: nobody aboard"); return; }
		auto a = r->actor.get();
		Hud(std::format("Seat tuning: {} ({})", a ? a->GetName() : "?", Def(g.pose.kind, r->dog, r->slot).name));
	}
	// DIK: Num7 47, Num8 48, Num9 49, Num4 4B, Num5 4C, Num6 4D, Num1 4F, Num2 50, Num3 51
	inline int NumIndex(std::uint32_t a_id)
	{
		switch (a_id) {
		case 0x4F: return 1; case 0x50: return 2; case 0x51: return 3;
		case 0x4B: return 4; case 0x4C: return 5; case 0x4D: return 6;
		case 0x47: return 7; case 0x48: return 8; case 0x49: return 9;
		default: return -1;
		}
	}
	inline void NumKey(std::uint32_t a_id, bool a_pressed, bool a_downEdge)
	{
		if (!g.numOn) return;
		const int k = NumIndex(a_id);
		if (k < 0) return;
		if (k == 5) {
			if (a_downEdge) {
				if (!g.riders.empty()) g.sel = (g.sel + 1) % static_cast<int>(g.riders.size());
				ShowSelected();
			}
			return;
		}
		const bool was = g.held[k];
		g.held[k] = a_pressed;
		if (was && !a_pressed && g.tuneDirty) {  // saved when a key is let go
			if (auto* r = Selected()) SaveTune(r->dog, r->slot);
			g.tuneDirty = false;
		}
	}
	inline void TendNum(float a_dt)
	{
		if (!g.numOn) return;
		auto* r = Selected();
		if (!r) return;
		Tune& t = TuneOf(g.pose.kind, r->dog, r->slot);
		const float mv = 15.0f * a_dt, tr = 0.8f * a_dt;  // 15 units / 45 deg a second
		const Tune before = t;
		if (g.held[8]) t.y += mv;
		if (g.held[2]) t.y -= mv;
		if (g.held[6]) t.x += mv;
		if (g.held[4]) t.x -= mv;
		if (g.held[9]) t.z += mv;
		if (g.held[3]) t.z -= mv;
		if (g.held[7]) t.yaw -= tr;
		if (g.held[1]) t.yaw += tr;
		if (t.x != before.x || t.y != before.y || t.z != before.z || t.yaw != before.yaw) g.tuneDirty = true;
	}
	inline std::string NumCommand(const std::string& a_arg)
	{
		if (a_arg == "off" || (a_arg.empty() && g.numOn)) {
			if (g.tuneDirty) { if (auto* r = Selected()) SaveTune(r->dog, r->slot); g.tuneDirty = false; }
			g.numOn = false;
			for (auto& h : g.held) h = false;
			return "seat tuning off";
		}
		if (a_arg == "reset") {
			auto& kt = TunesOf(g.pose.kind);
			for (auto& t : kt.human) t = {};
			for (auto& t : kt.dog) t = {};
			for (int i = 0; i < kHumans; ++i) SaveTune(false, i);
			for (int i = 0; i < kDogs; ++i) SaveTune(true, i);
			return "all crew seat tunes reset";
		}
		g.numOn = true;
		LoadTunes();
		ShowSelected();
		return "seat tuning on: Num5 picks who, 8/2 forward/back, 4/6 left/right, 9/3 up/down, 7/1 turn (saved per seat); oas num to stop";
	}

	// ---- v1.8.53: peace aboard: no animal fights a seated passenger (crew, followers, the dog) ----
	// Every 0.2 s while anyone is seated: an animal (race keyword ActorTypeAnimal) whose combat target is a passenger stops
	// its fight, and a passenger fighting an animal stops too. Nothing else is touched (no actor values changed, nothing
	// saved): off the boat, or once they stand, the game is its own.
	inline bool IsAnimal(RE::Actor* a_actor)
	{
		const auto* race = a_actor ? a_actor->GetRace() : nullptr;
		return race && race->HasKeywordString("ActorTypeAnimal") && !race->HasKeywordString("ActorTypeNPC");
	}
	inline float g_peaceT = 0.0f;
	inline std::unordered_set<RE::FormID> g_peaceLogged;
	inline void TendPeace(float a_dt)
	{
		if (g.riders.empty()) { g_peaceLogged.clear(); return; }
		if ((g_peaceT -= a_dt) > 0.0f) return;
		g_peaceT = 0.2f;
		auto* pl = RE::ProcessLists::GetSingleton();
		if (!pl) return;
		std::vector<RE::Actor*> riders;
		for (auto& r : g.riders) if (auto a = r.actor.get(); a && !a->IsDead()) riders.push_back(a.get());
		if (riders.empty()) return;
		const auto isRider = [&](RE::Actor* a_t) { return a_t && std::find(riders.begin(), riders.end(), a_t) != riders.end(); };
		for (auto& h : pl->highActorHandles) {
			auto a = h.get();
			if (!a || a->IsDead() || !a->IsInCombat() || isRider(a.get()) || !IsAnimal(a.get())) continue;
			if (IsFollower(a.get()) || IsCrew(a.get())) continue;
			auto t = a->GetActorRuntimeData().currentCombatTarget.get();
			if (!t || !isRider(t.get())) continue;
			a->StopCombat();
			if (g_peaceLogged.insert(a->GetFormID()).second)
				logger::info("Crew: peace: {} {:08X} leaves {} alone (seated aboard)", a->GetName(), a->GetFormID(), t->GetName());
		}
		for (auto* r : riders) {
			if (!r->IsInCombat()) continue;
			auto t = r->GetActorRuntimeData().currentCombatTarget.get();
			if (t && IsAnimal(t.get())) r->StopCombat();
		}
	}

	// ---- every frame (player update): summons, letting go after a load, the numpad ----
	inline void Tick(float a_dt, bool a_haveBoat, const RE::NiPoint3& a_boatAt)
	{
		TendNum(a_dt);
		TendPeace(a_dt);  // v1.8.53
		if (!g.release.empty() && (g.releaseTimer -= a_dt) <= 0.0f) {
			g.releaseTimer = 1.0f;
			for (auto it = g.release.begin(); it != g.release.end();) {
				auto* a = RE::TESForm::LookupByID<RE::Actor>(*it);
				if (a && a->Is3DLoaded() && !IsAboard(a)) {
					LetGo(a);
					logger::info("Crew: {} {:08X} let go after the load", a->GetName(), *it);
					it = g.release.erase(it);
				} else if (!a) {
					it = g.release.erase(it);
				} else {
					++it;
				}
			}
		}
		if (g.summon.empty() || (g.summonTimer -= a_dt) > 0.0f) return;
		g.summonTimer = 0.3f;
		const RE::FormID id = g.summon.front();
		g.summon.erase(g.summon.begin());
		auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
		auto* cell = actor ? actor->GetParentCell() : nullptr;
		if (!actor || actor->IsDead() || !cell || !cell->IsAttached() || !actor->Is3DLoaded() || IsAboard(actor)) return;
		(void)a_haveBoat;
		(void)a_boatAt;
		const float ang = 6.2831853f * static_cast<float>(g.summonIndex++) / static_cast<float>((std::max)(1, g.summonTotal)) + 0.4f;
		RE::NiPoint3 pos{ g.summonCentre.x + std::sin(ang) * 250.0f, g.summonCentre.y + std::cos(ang) * 250.0f, g.summonCentre.z };
		float land = 0.0f, water = 0.0f;
		auto* tes = RE::TES::GetSingleton();
		const bool haveLand = tes && tes->GetLandHeight(pos, land) && std::isfinite(land);
		auto* wc = tes ? tes->GetCell(pos) : nullptr;
		const bool haveWater = wc && wc->IsAttached() && wc->GetWaterHeight(pos, water) && std::isfinite(water) && std::abs(water) < 1000000.0f;
		if (haveLand || haveWater) pos.z = (std::max)(haveLand ? land : -1.0e9f, haveWater ? water : -1.0e9f) + 2.0f;
		actor->SetPosition(pos, true);
		actor->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, std::atan2(g.summonCentre.x - pos.x, g.summonCentre.y - pos.y) });
		logger::info("Crew: {} {:08X} summoned to ({:.0f},{:.0f},{:.0f})", actor->GetName(), id, pos.x, pos.y, pos.z);
	}

	// rr crew add [refid] | remove [refid] | clear | list | summon
	inline void Command(const std::string& a_lower, bool a_haveBoat, const RE::NiPoint3& a_boatAt)
	{
		std::istringstream in(a_lower.size() > 4 ? a_lower.substr(4) : std::string{});
		std::string field, idText;
		in >> field >> idText;
		for (const char* w : { "add", "remove" }) {  // the console glues unquoted words (crewadd 1234 -> add1234)
			const std::string_view wv(w);
			if (idText.empty() && field.size() > wv.size() && field.rfind(w, 0) == 0) { idText = field.substr(wv.size()); field = w; }
		}
		auto target = [&]() -> RE::Actor* {
			if (!idText.empty()) {
				std::string hex = idText;
				if (hex.rfind("0x", 0) == 0) hex = hex.substr(2);
				const bool isHex = !hex.empty() && hex.size() <= 8 && std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
				return isHex ? RE::TESForm::LookupByID<RE::Actor>(static_cast<RE::FormID>(std::stoul(hex, nullptr, 16))) : nullptr;
			}
			auto sel = RE::Console::GetSelectedRef();
			return sel ? sel->As<RE::Actor>() : nullptr;
		};
		if (field == "add") {
			auto* a = target();
			if (!a) { Say("Crew: click an NPC in the console first (or: oas \"crew add <refid>\")"); return; }
			if (a == RE::PlayerCharacter::GetSingleton()) { Say("Crew: you are already aboard"); return; }
			if (a->IsDead()) { Say(std::format("Crew: {} is dead", a->GetName())); return; }
			const bool dog = IsDog(a);
			if (!dog && !IsPerson(a)) { Say(std::format("Crew: {} is not a person or a dog, so it cannot ride", a->GetName())); return; }
			if (a->IsPlayerTeammate()) { Say(std::format("Crew: {} is already a follower and boards by themselves, nothing to do", a->GetName())); return; }
			if (IsCrew(a)) { Say(std::format("Crew: {} is already in the crew", a->GetName())); return; }
			if (!dog && CrewHumans() >= 5) { Say("Crew: all 5 passenger seats are taken (oas \"crew remove\" someone first)"); return; }
			if (dog) {
				for (auto id : g.crew) if (auto* o = RE::TESForm::LookupByID<RE::Actor>(id); o && IsDog(o)) { Say(std::format("Crew: {} is already the boat's dog (one dog rides)", o->GetName())); return; }
			}
			g.crew.push_back(a->GetFormID());
			Say(std::format("Crew: {} added (people in the crew {}/5){}", a->GetName(), CrewHumans(), dog ? ", the dog: sits in the bow" : ""));
			return;
		}
		if (field == "remove") {
			auto* a = target();
			if (!a || !IsCrew(a)) { Say("Crew: that one is not in the crew (click them, or oas \"crew remove <refid>\")"); return; }
			g.crew.erase(std::remove(g.crew.begin(), g.crew.end(), a->GetFormID()), g.crew.end());
			Say(std::format("Crew: {} removed{}", a->GetName(), IsAboard(a) ? " (stays aboard until you stand up)" : ""));
			return;
		}
		if (field == "clear") { g.crew.clear(); Say("Crew: cleared"); return; }
		if (field == "markers") {  // v1.8.51: oas "crew markers on|off": pink markers on the seats of the boat in use
			g_markers = idText.empty() ? !g_markers : (idText == "on" || idText == "1");
			Say(std::format("Crew: seat markers {}", g_markers ? "on" : "off"));
			return;
		}
		if (field == "list") {
			if (g.crew.empty()) Say("Crew: nobody added yet (click an NPC, then oas \"crew add\")");
			for (auto id : g.crew) {
				auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
				if (!a) { Say(std::format("Crew: {:08X} (not loaded)", id)); continue; }
				std::string where = "not aboard";
				for (auto& r : g.riders) if (r.id == id) where = std::format("aboard, {}", Def(g.pose.kind, r.dog, r.slot).name);
				Say(std::format("Crew: {} {:08X} {}", a->GetName(), id, where));
			}
			for (auto& r : g.riders) {
				if (std::find(g.crew.begin(), g.crew.end(), r.id) != g.crew.end()) continue;
				auto a = r.actor.get();
				Say(std::format("Aboard (follower): {} {:08X}, {}", a ? a->GetName() : "?", r.id, Def(g.pose.kind, r.dog, r.slot).name));
			}
			return;
		}
		if (field == "summon") {
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			g.summon.clear();
			int queued = 0;
			std::string skipped;
			for (auto id : g.crew) {
				auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
				auto* cell = a ? a->GetParentCell() : nullptr;
				if (!a || a->IsDead() || IsAboard(a)) continue;
				if (!cell || !cell->IsAttached() || !a->Is3DLoaded() || a->GetWorldspace() != player->GetWorldspace()) { skipped += std::format(" {} (not nearby)", a->GetName()); continue; }
				if (a->IsInCombat()) { skipped += std::format(" {} (in combat)", a->GetName()); continue; }
				if (a->GetCurrentScene()) { skipped += std::format(" {} (in a scene)", a->GetName()); continue; }
				g.summon.push_back(id);
				++queued;
			}
			g.summonCentre = a_haveBoat ? a_boatAt : player->GetPosition();
			g.summonIndex = 0;
			g.summonTotal = queued;
			g.summonTimer = 0.0f;
			Say(std::format("Crew: summoning {} to the {}{}{}", queued, a_haveBoat ? "boat" : "you", skipped.empty() ? "" : ", skipped:", skipped));
			return;
		}
		Say("Crew: oas \"crew add [refid]\" | \"crew remove [refid]\" | crew clear | crew list | crew summon | \"crew markers on|off\" | oas num (numpad seat tuning)");
	}

	// ---- co-save ----
	constexpr std::uint32_t kRecCrew = 'CREW';
	constexpr std::uint32_t kRecRiders = 'RIDE';
	inline void Save(SKSE::SerializationInterface* a_i)
	{
		if (a_i->OpenRecord(kRecCrew, 1)) {
			const auto n = static_cast<std::uint32_t>(g.crew.size());
			a_i->WriteRecordData(n);
			for (auto id : g.crew) a_i->WriteRecordData(id);
		}
		if (a_i->OpenRecord(kRecRiders, 1)) {
			std::vector<RE::FormID> ids;
			for (auto& r : g.riders) ids.push_back(r.id);
			if (g.hop) if (auto a = g.hopActor.get()) ids.push_back(a->GetFormID());
			const auto n = static_cast<std::uint32_t>(ids.size());
			a_i->WriteRecordData(n);
			for (auto id : ids) a_i->WriteRecordData(id);
		}
	}
	inline bool LoadRecord(SKSE::SerializationInterface* a_i, std::uint32_t a_type)
	{
		if (a_type != kRecCrew && a_type != kRecRiders) return false;
		std::uint32_t n = 0;
		a_i->ReadRecordData(n);
		for (std::uint32_t k = 0; k < n && k < 256; ++k) {
			RE::FormID id = 0, out = 0;
			a_i->ReadRecordData(id);
			if (!a_i->ResolveFormID(id, out)) continue;
			if (a_type == kRecCrew) g.crew.push_back(out);
			else g.release.push_back(out);
		}
		if (a_type == kRecCrew) logger::info("Crew: {} member(s) loaded from the save", g.crew.size());
		else if (!g.release.empty()) logger::info("Crew: {} were aboard when saved, let go once loaded", g.release.size());
		g.releaseTimer = 1.0f;
		return true;
	}
	// a new game or a load: forget everything (no actor is touched here; RIDE lets the riders go after the load)
	inline void Forget()
	{
		g.crew.clear();
		g.riders.clear();
		g.release.clear();
		g.lingering = false;  // v1.7.21
		g.summon.clear();
		g.hop = false;
		g.aboard = false;
		g.havePose = false;
		g.rowerId = 0;
		g.rowerRowing = false;
	}
}
