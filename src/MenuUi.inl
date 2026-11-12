// v1.7.86: the Oars and Sails settings page in SKSE Menu Framework (its own hotkey opens it; our page is under
// "Oars and Sails"). Optional: without the framework nothing is registered and the console oas commands still do it
// all. Every change runs the same oas command the console does, so it acts at once and is saved the same way.
// (v1.7.84-85 drew our own overlay; replaced by this.)
namespace MenuUi
{
	namespace ui = ImGuiMCP;
	inline bool g_registered = false;
	inline int g_resetArm = 0;
	inline double g_readAt = -100.0;

	struct Vals
	{
		float rowMax = 300.0f, rowDrag = 0.07f, turn = 0.45f, height = 0.0f;
		int paint = 2, sail = 0, model = 0;
		bool lamp = true, docks = true, sailOn = false;
		bool lakeOn = true;
		int lakeCount = 3, lakeView = 5;
		bool plOn = true;
		int plForce = 0;
		bool plEverywhere = true;
		float plDensity = 3.0f, plSize = 0.5f, plOpenSize = 1.0f;
		int plOpen = 1800, plShore = 400;
		bool wake = true, underwater = true, grid = false, ripples = true;
		float standUp = 400.0f;
		float sailSpeed = 0.5f, sailFlap = 0.6f, turnLag = 2.5f;
		bool out = false;
		int sailModel = 0;
		int carry = 1;
		int carryJump = 1;
	};
	inline Vals g_v;

	// v1.8.8: each boat's own page: its settings read from its own RowRim.ini sections (the boat out: through the oas
	// commands, so they act at once; the others written to their sections, used when that boat comes out)
	struct BoatVals { float rowMax = 300.0f, rowDrag = 0.07f, turn = 0.45f, turnLag = 2.5f, sailSpeed = 0.5f, sailFlap = 0.6f, rock = 1.0f, heel = 1.0f, headWind = 0.35f; };
	inline BoatVals g_b[TestHull::kKindCount];
	inline float IniF(const char* a_sec, const char* a_key, const char* a_fallSec, float a_def)
	{
		const auto ini = Plankton::IniPath();
		char d[32]{}, b[32]{};
		GetPrivateProfileStringA(a_fallSec, a_key, "", d, sizeof(d), ini.c_str());
		GetPrivateProfileStringA(a_sec, a_key, d, b, sizeof(b), ini.c_str());
		try { return b[0] ? std::stof(b) : a_def; } catch (...) { return a_def; }
	}
	inline void ReadBoat(int a_k)
	{
		const auto& dm = TestHull::DimsOf(a_k);
		auto& v = g_b[a_k];
		const char* fb = "Row";
		v.rowMax = IniF(dm.rowSec, "MaxSpeed", fb, 300.0f);
		v.rowDrag = IniF(dm.rowSec, "Glide", fb, 0.07f);
		v.turn = 0.45f * IniF(dm.rowSec, "Turn", fb, 1.0f);
		v.turnLag = IniF(dm.rowSec, "TurnLag", fb, 2.5f);
		v.sailSpeed = IniF(dm.sailSec, "Speed", "Sail", 0.5f);
		v.sailFlap = IniF(dm.sailSec, "Flap", "Sail", 0.6f);
		const float dr = a_k == TestHull::kKindSail ? 2.5f : 1.0f, dh = a_k == TestHull::kKindSail ? 1.5f : 1.0f;  // v1.8.27d
		v.rock = IniF(dm.sailSec, "Rock", dm.sailSec, dr);
		v.heel = IniF(dm.sailSec, "Heel", dm.sailSec, dh);
		v.headWind = IniF(dm.sailSec, "HeadWind", "Wind", 0.35f);  // v1.8.34
	}

	inline void Run(const std::string& a_cmd)
	{
		logger::info("Menu: oas {}", a_cmd);
		RunRr(a_cmd);
	}

	inline void ReadVals()
	{
		const auto h = TestHull::Get().GetUiValues();
		g_v.rowMax = h.rowMax; g_v.rowDrag = h.rowDrag; g_v.turn = h.turn; g_v.height = h.height;
		g_v.paint = h.paint; g_v.sail = h.sail; g_v.lamp = h.lamp; g_v.docks = h.docks; g_v.sailOn = h.sailOn;
		g_v.sailSpeed = h.sailSpeed; g_v.sailFlap = h.sailFlap; g_v.turnLag = h.turnLag; g_v.model = h.model;
		g_v.out = h.out; g_v.sailModel = h.sailModel; g_v.carry = h.carry; g_v.carryJump = h.carryJump;
		for (int k = 0; k < TestHull::kKindCount; ++k) ReadBoat(k);
		if (!LakeBoats::g_loaded) LakeBoats::LoadLakes();
		g_v.lakeOn = LakeBoats::g_on; g_v.lakeCount = LakeBoats::g_count; g_v.lakeView = LakeBoats::g_view;
		Plankton::LoadSettings();
		const auto& p = Plankton::g_set;
		g_v.plOn = p.on; g_v.plEverywhere = p.everywhere; g_v.plForce = p.force; g_v.plDensity = p.density; g_v.plSize = p.size;
		g_v.plOpen = p.open; g_v.plOpenSize = p.openSize; g_v.plShore = p.shore; g_v.wake = p.wake;
		Underwater::Load();
		g_v.underwater = Underwater::g_on;
		g_v.grid = CellGrid::On();
		g_v.ripples = Fx::g_on;
		g_v.standUp = Crew::kLingerLeave;
	}

	inline void Hint(const char* a_text)
	{
		ui::SameLine();
		ui::TextDisabled("(?)");
		if (ui::IsItemHovered()) ui::SetTooltip("%s", a_text);
	}
	// sliders apply when you let go (each change is saved to RowRim.ini)
	inline bool SliderF(const char* a_label, float& a_v, float a_lo, float a_hi, const char* a_fmt)
	{
		ui::SliderFloat(a_label, &a_v, a_lo, a_hi, a_fmt, ui::ImGuiSliderFlags_AlwaysClamp);
		return ui::IsItemDeactivatedAfterEdit();
	}
	inline bool SliderI(const char* a_label, int& a_v, int a_lo, int a_hi)
	{
		ui::SliderInt(a_label, &a_v, a_lo, a_hi, "%d", ui::ImGuiSliderFlags_AlwaysClamp);
		return ui::IsItemDeactivatedAfterEdit();
	}
	inline void OnOff(const char* a_label, bool& a_v, const char* a_on, const char* a_off)
	{
		if (ui::Checkbox(a_label, &a_v)) Run(a_v ? a_on : a_off);
	}

	// v1.8.53: the dock buttons, for one boat (the Boat tab and each boat's page)
	inline void DockButtons(int a_k)
	{
		const char* nm = TestHull::DimsOf(a_k).name;
		if (ui::Button("Spawn boat")) { Run(std::format("spawn {}", nm)); ReadVals(); }  // v1.8.89: puts it ahead of you, the one in use
		ui::SameLine();
		if (ui::Button("Use this boat")) { Run(std::format("use {}", nm)); ReadVals(); }  // v1.8.89: the one in use (spawns it if it is not out)
		ui::SameLine();
		if (ui::Button("Dock here")) Run(std::format("home here {}", nm));
		ui::SameLine();
		const int has = TestHull::Of(a_k).HomeCount();
		ui::BeginDisabled(has == 0);
		if (ui::Button("Reset dock")) Run(std::format("home reset {}", nm));
		ui::SameLine();
		if (ui::Button("Reset all")) Run(std::format("home resetall {}", nm));
		ui::EndDisabled();
		ui::SameLine();
		if (ui::Button("Dock here for good")) Run(std::format("home perm {}", nm));  // v1.8.88: a shipped dock (RowRim_Docks.ini)
		ui::SameLine();
		if (has) ui::TextDisabled("%d dock spot%s", has, has == 1 ? "" : "s");
		else ui::TextDisabled("no docks of its own");
		Hint("Dock here brings the boat to the water just ahead of you and docks it there. It keeps every spot you dock it at: come back near any of them and it is waiting there. Face the water first. Reset dock forgets the spot you are at (none near: all of them), Reset all forgets every one. The boat stays where it is.");
	}

	// v1.8.53: the settings page, tidied for the release: Boat, Crew, Effects, Lake boats (test-only settings gone)
	inline void __stdcall Render()
	{
		// the values are read fresh when the page is first drawn after a while shut (console changes show up too)
		const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		if (now - g_readAt > 0.5) ReadVals();
		g_readAt = now;

		ui::TextDisabled("Oars and Sails %s", kVersionShort);
		ui::PushItemWidth(260.0f);
		if (ui::BeginTabBar("oas_tabs")) {
			if (ui::BeginTabItem("Boat")) {
				const int k = std::clamp(g_v.model, 0, 1);
				if (g_v.sailOn) {
					static const char* names[] = { "Rowboat", "Sailing boat" };
					static const char* cmd[] = { "boat row", "boat sail" };
					int pick = k;
					if (ui::Combo("Boat", &pick, names, 2)) { g_v.model = pick; Run(cmd[pick]); ReadVals(); }
					Hint("The boat these buttons work on. Both can be out at once: the one you sit in, stand on or are nearest to is the one in use. Speeds and sailing: their own pages (Oars and Sails / Rowboat, Sailing boat).");
				}
				ui::SeparatorText("Bring it");
				if (ui::Button("Boat here")) Run("boat");
				ui::SameLine();
				if (ui::Button("Bring under me")) Run("bring");
				ui::SameLine();
				if (ui::Button("Remove boat")) Run("unboat");
				ui::SeparatorText("Docks");
				DockButtons(k);
				OnOff("Rowboat waits at the town docks", g_v.docks, "dock on", "dock off");
				Hint("The rowboat waits at the docks of the lakeside and coastal towns.");
				ui::SeparatorText("Look");
				if (SliderF("Height in the water", g_v.height, -30.0f, 30.0f, "%.0f")) Run(std::format("height {:.0f}", g_v.height));
				Hint("Sits the boat higher or lower in the water. Default 0.");
				if (k == TestHull::kKindRow) {  // the rowboat's own
					if (ui::Combo("Paint", &g_v.paint, Paint::kNames, Paint::kCount)) Run(std::string("paint ") + Paint::kNames[g_v.paint]);
					OnOff("Lantern", g_v.lamp, "lamp on", "lamp off");
				}
				ui::SeparatorText("Walking aboard");
				{
					bool hold = g_v.carry != 0;
					if (ui::Checkbox("Hold my spot standing still", &hold)) { g_v.carry = hold ? 1 : 0; Run(hold ? "carry on" : "carry off"); }
					Hint("Standing still on a moving boat you stay on your spot. Walking and running are the game's own.");
					static const char* jm[] = { "The game's", "Ride with the boat", "Momentum when you jump" };
					static const char* jc[] = { "carry jump off", "carry jump ride", "carry jump momentum" };
					if (ui::Combo("Jumps aboard", &g_v.carryJump, jm, 3)) Run(jc[g_v.carryJump]);
					Hint("Ride: the boat's speed is part of yours all through the jump. Momentum: given once as you leave the deck.");
				}
				ui::EndTabItem();
			}
			if (ui::BeginTabItem("Crew")) {
				if (ui::Button("Summon crew")) Run("crew summon");
				Hint("Brings your followers to the boat.");
				if (SliderF("Get up at", g_v.standUp, 100.0f, 3000.0f, "%.0f")) {
					Crew::kLingerLeave = g_v.standUp;
					WritePrivateProfileStringA("Crew", "StandUpAt", std::format("{:.0f}", g_v.standUp).c_str(), Plankton::IniPath().c_str());
					logger::info("Menu: crew get up at {:.0f}", g_v.standUp);
				}
				Hint("Passengers stay seated until you walk this far from the boat (about 6 m by default).");
				ui::TextDisabled("Seated passengers are left alone by animals.");
				if (ui::CollapsingHeader("Adjust seats")) {  // v1.8.51: the passenger seats of the boat in use, moved by eye with pink markers
					const int k = std::clamp(RowRimBakedIni::g_iniKind, 0, 1);
					ui::TextDisabled("Seats on the %s (the boat in use)", TestHull::DimsOf(k).label);
					bool mk = Crew::g_markers;
					if (ui::Checkbox("Show seat markers", &mk)) Run(mk ? "crew markers on" : "crew markers off");
					Hint("Pink figures on each passenger seat, sitting as they will. Pick a seat and move it: the marker moves at once, saved as you let go. Passengers use it the next time they sit down.");
					const int nh = Crew::HumanCount(k), nd = Crew::DogCount(k), n = nh + nd;
					static int s_seat = 0;
					s_seat = std::clamp(s_seat, 0, n - 1);
					const char* names[16]{};
					for (int i = 0; i < n && i < 16; ++i) names[i] = Crew::Def(k, i >= nh, i >= nh ? i - nh : i).name;
					ui::Combo("Seat", &s_seat, names, (std::min)(n, 16));
					const bool dog = s_seat >= nh;
					const int slot = dog ? s_seat - nh : s_seat;
					Crew::Tune t = Crew::TuneOf(k, dog, slot);
					float deg = t.yaw * 57.2958f;
					bool ch = false, done = false;
					ch |= ui::SliderFloat("Forward / back", &t.y, -80.0f, 80.0f, "%.1f", ui::ImGuiSliderFlags_AlwaysClamp); done |= ui::IsItemDeactivatedAfterEdit();
					ch |= ui::SliderFloat("Left / right", &t.x, -80.0f, 80.0f, "%.1f", ui::ImGuiSliderFlags_AlwaysClamp); done |= ui::IsItemDeactivatedAfterEdit();
					ch |= ui::SliderFloat("Down / up", &t.z, -50.0f, 50.0f, "%.1f", ui::ImGuiSliderFlags_AlwaysClamp); done |= ui::IsItemDeactivatedAfterEdit();
					ch |= ui::SliderFloat("Turn", &deg, -180.0f, 180.0f, "%.0f deg", ui::ImGuiSliderFlags_AlwaysClamp); done |= ui::IsItemDeactivatedAfterEdit();
					if (ch || done) { t.yaw = deg / 57.2958f; Crew::SetTune(k, dog, slot, t, done); }
					if (ui::Button("Reset this seat")) Crew::SetTune(k, dog, slot, Crew::Tune{}, true);
				}
				ui::EndTabItem();
			}
			if (ui::BeginTabItem("Effects")) {
				ui::SeparatorText("Water");
				if (ui::Checkbox("Boat wake", &g_v.wake)) {
					Plankton::g_set.wake = g_v.wake;
					WritePrivateProfileStringA("Wake", "On", g_v.wake ? "1" : "0", Plankton::IniPath().c_str());
					logger::info("Menu: wake {}", g_v.wake ? "on" : "off");
				}
				if (ui::Checkbox("Oar ripples", &g_v.ripples)) {
					if (Fx::g_on != g_v.ripples) Run("ripples");
					g_v.ripples = Fx::g_on;
				}
				OnOff("Underwater look", g_v.underwater, "underwater on", "underwater off");
				Hint("The mod's own underwater look when you dive off your boat.");
				ui::SeparatorText("Glowing plankton");
				OnOff("Plankton", g_v.plOn, "plankton on", "plankton off");
		OnOff("Also away from the boat", g_v.plEverywhere, "plankton everywhere", "plankton boat");
		Hint("The glowing water on foot and swimming too, not only while you are aboard.");
				ui::BeginDisabled(!g_v.plOn);
				static const char* when[] = { "Night only", "Always", "Never" };
				int w = g_v.plForce > 0 ? 1 : g_v.plForce < 0 ? 2 : 0;
				if (ui::Combo("When", &w, when, 3)) {
					g_v.plForce = w == 1 ? 1 : w == 2 ? -1 : 0;
					Run(w == 1 ? "plankton night" : w == 2 ? "plankton never" : "plankton auto");
				}
				if (SliderF("Density", g_v.plDensity, 0.0f, 6.0f, "%.1f")) Run(std::format("plankton density {:.2f}", g_v.plDensity));
				if (SliderF("Glow size", g_v.plSize, 0.2f, 4.0f, "%.2f")) Run(std::format("plankton size {:.2f}", g_v.plSize));
				if (SliderI("Open water glows", g_v.plOpen, 0, 2000)) Run(std::format("plankton open {}", g_v.plOpen));
				if (SliderI("Shoreline glows", g_v.plShore, 0, 400)) Run(std::format("plankton shore {}", g_v.plShore));
				ui::EndDisabled();
				ui::EndTabItem();
			}
			if (ui::BeginTabItem("Lake boats")) {
				OnOff("Fishing boats on the lakes", g_v.lakeOn, "lakeboats on", "lakeboats off");
				ui::BeginDisabled(!g_v.lakeOn);
				if (SliderI("Per lake", g_v.lakeCount, 0, 10)) Run(std::format("lakeboats {}", g_v.lakeCount));
				Hint("Most boats on one lake (smaller lakes get fewer). Default 3.");
				if (SliderI("Drawn at once", g_v.lakeView, 1, 10)) Run(std::format("lakeboats view{}", g_v.lakeView));
				ui::EndDisabled();
				ui::EndTabItem();
			}
			ui::EndTabBar();
		}
		ui::PopItemWidth();
		ui::Separator();
		if (g_resetArm > 0) {
			--g_resetArm;
			if (ui::Button("Click again to reset")) { Run("reset settings"); ReadVals(); g_resetArm = 0; }
		} else if (ui::Button("Reset settings")) g_resetArm = 180;
		Hint("Puts every setting back to the mod's defaults (your docked spots are kept).");
	}

	// v1.8.8: a boat's own page (v1.8.53: the rowboat and the sailing boat; test-only settings gone)
	inline void RenderBoat(int a_k)
	{
		const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		if (now - g_readAt > 0.5) ReadVals();
		g_readAt = now;
		const auto& dm = TestHull::DimsOf(a_k);
		auto& v = g_b[a_k];
		const bool live = g_v.model == a_k;  // this boat is the one out (or the next one out)
		ui::TextDisabled("Oars and Sails %s  -  %s", kVersionShort, dm.label);
		ui::PushItemWidth(260.0f);
		if (live && g_v.out) ui::TextDisabled("This boat is out.");
		DockButtons(a_k);
		// a change: the boat out through the oas command (acts at once, saved to its section); else straight to its section
		auto put = [&](const char* a_sec, const char* a_key, const std::string& a_val, const std::string& a_cmd) {
			if (live) Run(a_cmd);
			else {
				WritePrivateProfileStringA(a_sec, a_key, a_val.c_str(), Plankton::IniPath().c_str());
				logger::info("Menu: [{}] {}={} ({}, not out)", a_sec, a_key, a_val, dm.label);
			}
		};
		const bool sail = a_k == TestHull::kKindSail;
		ui::SeparatorText(sail ? "Handling" : "Rowing");
		if (!sail) {
			if (SliderF("Top speed", v.rowMax, 50.0f, 700.0f, "%.0f")) put(dm.rowSec, "MaxSpeed", std::format("{:.0f}", v.rowMax), std::format("row speed {:.0f}", v.rowMax));
			Hint("How fast rowing gets you.");
			if (SliderF("Glide drag", v.rowDrag, 0.01f, 0.8f, "%.3f")) put(dm.rowSec, "Glide", std::format("{:.3f}", v.rowDrag), std::format("row drag {:.3f}", v.rowDrag));
			Hint("Lower glides further between strokes.");
		}
		if (SliderF("Turning", v.turn, 0.15f, 1.35f, "%.2f")) put(dm.rowSec, "Turn", std::format("{:.2f}", v.turn / 0.45f), std::format("row turn {:.2f}", v.turn));
		Hint("How quickly the boat turns.");
		if (SliderF("Turn sluggishness", v.turnLag, 1.0f, 6.0f, "%.1f")) put(dm.rowSec, "TurnLag", std::format("{:.2f}", v.turnLag), std::format("row lag {:.1f}", v.turnLag));
		Hint("How slowly the boat swings into and out of a turn. 1 is a quick turn.");
		if (sail && g_v.sailOn) {
			ui::SeparatorText("Sailing");
			if (live && g_v.out) {
				if (ui::RadioButton("Stowed", g_v.sail != 2 && g_v.sail != 3)) { g_v.sail = 1; Run("sail furl"); }
				ui::SameLine();
				if (ui::RadioButton("Set", g_v.sail == 2)) { g_v.sail = 2; Run("sail full"); }
				ui::SameLine();
				if (ui::RadioButton("Reefed", g_v.sail == 3)) { g_v.sail = 3; Run("sail reef"); }
			}
			if (SliderF("Sailing speed", v.sailSpeed, 0.1f, 2.0f, "%.2f"))
				put(dm.sailSec, "Speed", std::format("{:.2f}", v.sailSpeed), std::format("sail speed {:.2f}", v.sailSpeed));
			Hint("How fast the wind drives the boat.");
			if (SliderF("Speed into the wind", v.headWind, 0.0f, 1.0f, "%.2f"))  // v1.8.34
				put(dm.sailSec, "HeadWind", std::format("{:.2f}", v.headWind), std::format("sail headwind {:.2f}", v.headWind));
			Hint("How fast it still goes heading straight into the wind, as a share of its full speed. 0 stops it dead, as real sails would.");
			if (SliderF("Sail flapping", v.sailFlap, 0.0f, 2.0f, "%.2f"))
				put(dm.sailSec, "Flap", std::format("{:.2f}", v.sailFlap), std::format("sail flap {:.2f}", v.sailFlap));
			Hint("How much the middle of the cloth ripples (its edges never move). 0 keeps it still.");
			if (SliderF("Rocking on the water", v.rock, 0.0f, 6.0f, "%.2f"))
				put(dm.sailSec, "Rock", std::format("{:.2f}", v.rock), std::format("sail rock {:.2f}", v.rock));
			Hint("How much it rolls, pitches and bobs; more in a wind under sail.");
			if (SliderF("Heel under sail", v.heel, 0.0f, 3.0f, "%.2f"))
				put(dm.sailSec, "Heel", std::format("{:.2f}", v.heel), std::format("sail heel {:.2f}", v.heel));
			Hint("How far it leans over with the wind across it.");
			ui::Separator();
			ui::TextDisabled("At the helm: Sprint sets, reefs and stows the sail, Jump lets the anchor go and hauls it up,");
			ui::TextDisabled("A / D steer. The sail swings to the wind by itself. Standing up stows it and she glides to a stop.");
		}
		ui::PopItemWidth();
	}
	inline void __stdcall RenderRow() { RenderBoat(TestHull::kKindRow); }
	inline void __stdcall RenderSailBoat() { RenderBoat(TestHull::kKindSail); }

	// v1.8.28b: our own wind for the sails (RowRim.ini [Wind])
	inline void __stdcall RenderWind()
	{
		OwnWind::Load();
		auto& w = OwnWind::g_set;
		ui::PushItemWidth(260.0f);
		{
			// the wind now: where it blows from (the direction is the way it blows toward)
			float deg = std::fmod(OwnWind::Dir() * 57.2958f + 180.0f, 360.0f);
			if (deg < 0.0f) deg += 360.0f;
			const char* pts[8] = { "north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west" };
			ui::TextDisabled("Wind now: from the %s, strength %.2f (weather %.2f)", pts[static_cast<int>((deg + 22.5f) / 45.0f) % 8], OwnWind::Str(), OwnWind::g.base);
		}
		if (SliderF("Strength", w.strength, 0.2f, 2.0f, "%.2f")) OwnWind::Save("Strength", w.strength);
		Hint("Times the weather's own wind (stronger in storms, rain and snow). Default 1.");
		if (SliderF("Shifts", w.shifts, 0.0f, 3.0f, "%.2f")) OwnWind::Save("Shifts", w.shifts);
		Hint("How often and how far it swings round (every few minutes, 30 to 120 degrees). 0 keeps its direction. Default 1.");
		if (SliderF("Wander", w.wander, 0.0f, 3.0f, "%.2f")) OwnWind::Save("Wander", w.wander);
		Hint("The slow drift of its direction between shifts. 0 holds it still. Default 1.");
		if (SliderF("Gusts", w.gusts, 0.0f, 3.0f, "%.2f")) OwnWind::Save("Gusts", w.gusts);
		Hint("How often and how hard it gusts and lulls (and veers a little with them). 0 is a steady wind. Default 1.");
		{
			WindStreaks::Load();  // v1.8.39
			float st = WindStreaks::g.amount;
			if (SliderF("Wind streaks", st, 0.0f, 2.0f, "%.2f")) WindStreaks::SetAmount(st);
			Hint("White streaks in the air blowing past with the wind while you are aboard (more in a stronger wind). 0 is off. Default 1.");
		}
		ui::TextDisabled("How fast each boat goes into the wind: Speed into the wind, on its own page.");  // v1.8.34
		if (ui::Button("New wind now")) OwnWind::ShiftNow();
		Hint("Starts a big shift right away. The game's own wind is never used or changed.");
		ui::PopItemWidth();
	}

	// v1.8.79: the helm camera's page (RowRim.ini [HelmCam]); every change is saved as you let go
	inline void __stdcall RenderHelmCam()
	{
		HelmCamSet::Get();
		auto& c = HelmCamSet::g;
		ui::TextDisabled("Oars and Sails %s  -  Helm camera", kVersionShort);
		ui::PushItemWidth(260.0f);
		auto chk = [&](const char* a_label, bool& a_v, const char* a_hint) {
			if (ui::Checkbox(a_label, &a_v)) HelmCamSet::Save();
			Hint(a_hint);
		};
		auto sl = [&](const char* a_label, float& a_v, float a_lo, float a_hi, const char* a_fmt, const char* a_hint) {
			if (SliderF(a_label, a_v, a_lo, a_hi, a_fmt)) HelmCamSet::Save();
			Hint(a_hint);
		};
		ui::SeparatorText("Camera");
		chk("Our helm camera", c.on, "Seated on a boat the camera orbits the boat (ours). Off: the game's own third person camera.");
		chk("Cinematic shots", c.cine, "Cuts between shots every few seconds (V toggles it while you steer).");
		chk("Invert Y", c.invertY, "Mouse up looks down.");
		chk("Swing back behind the boat when idle", c.recenter, "Left alone the camera drifts back behind the boat.");
		ui::SeparatorText("Look");
		sl("Mouse speed", c.sens, 0.02f, 3.0f, "%.2f", "Orbit speed for the mouse and the right stick.");
		sl("Mouse counts", c.mouseScale, 0.001f, 0.2f, "%.3f", "How far a mouse movement turns the orbit before the speed above. Raise it if the mouse feels slow.");
		sl("Smoothing (s)", c.smooth, 0.0f, 1.5f, "%.2f", "How softly the orbit eases to where the mouse put it. 0 is instant.");
		sl("Follow lag (s)", c.follow, 0.0f, 1.0f, "%.2f", "How much the camera trails the boat. 0 locks it on.");
		sl("Height above the boat", c.height, -200.0f, 1000.0f, "%.0f", "The point it looks at, above the boat.");
		sl("Look down limit (deg)", c.pitchMax, 0.0f, 89.0f, "%.0f", "How high above the boat it may swing.");
		sl("Look up limit (deg)", c.pitchMin, -80.0f, 0.0f, "%.0f", "How far below level it may swing.");
		sl("Swing-back delay (s)", c.recenterDelay, 0.5f, 30.0f, "%.1f", "Idle time before it swings back.");
		ui::SeparatorText("Zoom");
		sl("Closest", c.minDist, 50.0f, 2000.0f, "%.0f", "Nearest the wheel brings it.");
		sl("Furthest", c.maxDist, 300.0f, 60000.0f, "%.0f", "Furthest the wheel takes it (at least 12000, up to 60000). Mouse wheel, or R3 + left stick / L3 + right stick on a pad.");
		ui::PopItemWidth();
	}

	// v1.8.82: the Advanced page: how each boat is drawn while you are seated (it is saved per boat in RowRim.ini [Advanced])
	inline void __stdcall RenderAdvanced()
	{
		ui::TextDisabled("Oars and Sails %s  -  Advanced", kVersionShort);
		ui::PushItemWidth(260.0f);
		ui::SeparatorText("Seated draw mode");
		static const char* modes[5] = { "Automatic", "Old (a frame behind)", "This frame", "From the drawn player", "Pinned to the seat" };
		struct Row { int kind; const char* label; };
		for (const Row r : { Row{ TestHull::kKindRow, "Rowboat" }, Row{ TestHull::kKindSail, "Sailing boat" } }) {
			auto& hull = TestHull::Of(r.kind);
			int sel = hull.DrawSel();
			if (ui::Combo(r.label, &sel, modes, 5)) hull.SetDrawSel(sel);
		}
		Hint("How the boat is drawn while you sit in it. Automatic: the rowboat pinned to the seat (the row clip cannot shake you against the hull), the sailing boat a frame behind (as before). If a boat jitters, try the others here; the change shows at once.");
		ui::PopItemWidth();
	}

	inline void Register()
	{
		if (g_registered) return;
		const auto module = GetModuleHandleW(L"SKSEMenuFramework.dll");
		if (!module) { logger::info("Menu: SKSE Menu Framework not loaded (settings by oas commands)"); return; }
		for (const char* sym : { "AddSectionItem", "GetMenuFrameworkVersion", "igSliderFloat", "igSliderInt", "igCheckbox", "igButton", "igCombo_Str_arr",
				 "igBeginTabBar", "igBeginTabItem", "igIsItemDeactivatedAfterEdit", "igBeginDisabled", "igRadioButton_Bool", "igSeparatorText", "igCollapsingHeader_TreeNodeFlags" }) {
			if (!GetProcAddress(module, sym)) { logger::warn("Menu: SKSE Menu Framework has no {}, page not added", sym); return; }
		}
		const float v = SKSEMenuFramework::GetMenuFrameworkVersion();
		if (v < 3.0f) { logger::warn("Menu: SKSE Menu Framework {} (3.x needed), page not added", v); return; }
		SKSEMenuFramework::SetSection("Oars and Sails");
		SKSEMenuFramework::AddSectionItem("Settings", Render);
		SKSEMenuFramework::AddSectionItem("Helm camera", RenderHelmCam);  // v1.8.79
		SKSEMenuFramework::AddSectionItem("Rowboat", RenderRow);
		SKSEMenuFramework::AddSectionItem("Advanced", RenderAdvanced);  // v1.8.82  // v1.8.8: each boat its own page
		if (TestHull::SailingOn()) {
			SKSEMenuFramework::AddSectionItem("Sailing boat", RenderSailBoat);
			SKSEMenuFramework::AddSectionItem("Wind", RenderWind);  // v1.8.28b
		}
		// v1.8.53: no Sea page (OceanRim support off) and no Longship page (not in this build)
		g_registered = true;
		logger::info("Menu: SKSE Menu Framework {}: Oars and Sails/Settings added", v);
	}
}

void OverlayToggle()
{
	Print(MenuUi::g_registered ? "RowRim: settings are in SKSE Menu Framework (its hotkey), under Oars and Sails"
	                           : "RowRim: install SKSE Menu Framework for the settings page; the oas commands do the same (oas help)");
}
