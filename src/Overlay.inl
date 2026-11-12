// v1.7.84: the Oars and Sails window. Our own overlay: Dear ImGui drawn with the game's own D3D11 device at the end of
// each frame, no other mod needed. F10 opens and closes it (RowRim.ini [UI] Key, a DirectInput scan code), or oas menu
// in the console. While it is open the game gets no input (mouse, keyboard, pad all go to the window); Esc or pad B
// closes it. Every change goes through the same oas commands the console uses, so it acts at once and is saved the same
// way. Nothing is set up until the window is first opened.
namespace Overlay
{
	inline bool g_open = false, g_ready = false, g_failed = false, g_hooked = false;
	inline float g_w = 1920.0f, g_h = 1080.0f, g_mx = 960.0f, g_my = 540.0f, g_scale = 1.0f;
	inline float g_padX = 0.0f, g_padY = 0.0f;
	inline std::uint32_t g_key = 0x44;  // F10
	inline std::chrono::steady_clock::time_point g_last{};
	inline int g_resetArm = 0;  // frames left to confirm "Reset settings"

	// what the window shows (read fresh each time it opens, then kept as you change it)
	struct Vals
	{
		float rowMax = 300.0f, rowDrag = 0.07f, turn = 0.45f, height = 0.0f;
		int paint = 2, sail = 0;
		bool lamp = true, docks = true, sailOn = false;
		bool lakeOn = true;
		int lakeCount = 3, lakeView = 5;
		bool plOn = true;
		int plForce = 0;
		float plDensity = 3.0f, plSize = 0.5f, plOpenSize = 1.0f;
		int plOpen = 1800, plShore = 400;
		bool wake = true, underwater = true, grid = false, ripples = true;
		float standUp = 400.0f;
	};
	inline Vals g_v;

	inline void Run(const std::string& a_cmd)
	{
		logger::info("Menu: oas {}", a_cmd);
		RunRr(a_cmd);
	}

	inline void ReadVals()
	{
		auto& hull = TestHull::Get();
		const auto h = hull.GetUiValues();
		g_v.rowMax = h.rowMax; g_v.rowDrag = h.rowDrag; g_v.turn = h.turn; g_v.height = h.height;
		g_v.paint = h.paint; g_v.sail = h.sail; g_v.lamp = h.lamp; g_v.docks = h.docks; g_v.sailOn = h.sailOn;
		LakeBoats::Command("lakes");  // loads the lake boat settings if not yet
		g_v.lakeOn = LakeBoats::g_on; g_v.lakeCount = LakeBoats::g_count; g_v.lakeView = LakeBoats::g_view;
		Plankton::LoadSettings();
		const auto& p = Plankton::g_set;
		g_v.plOn = p.on; g_v.plForce = p.force; g_v.plDensity = p.density; g_v.plSize = p.size;
		g_v.plOpen = p.open; g_v.plOpenSize = p.openSize; g_v.plShore = p.shore; g_v.wake = p.wake;
		Underwater::Load();
		g_v.underwater = Underwater::g_on;
		g_v.grid = CellGrid::On();
		g_v.ripples = Fx::g_on;
		g_v.standUp = Crew::kLingerLeave;
	}

	inline bool InLoadOrMainMenu()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) return true;
		return ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || !RE::PlayerCharacter::GetSingleton() ||
		       !RE::PlayerCharacter::GetSingleton()->Is3DLoaded();
	}

	inline void SetOpen(bool a_open)
	{
		if (a_open == g_open) return;
		if (a_open && (g_failed || InLoadOrMainMenu())) return;
		g_open = a_open;
		g_resetArm = 0;
		g_padX = g_padY = 0.0f;
		if (g_open) { ReadVals(); g_last = std::chrono::steady_clock::now(); }
		if (g_ready) {
			auto& io = ImGui::GetIO();
			for (int b = 0; b < 3; ++b) io.AddMouseButtonEvent(b, false);  // nothing left held down
		}
		logger::info("Menu: {}", g_open ? "open" : "closed");
	}

	// ---- set up (once, the first time the window opens) ----
	inline void ApplyStyle()
	{
		auto& s = ImGui::GetStyle();
		ImGui::StyleColorsDark();
		s.WindowRounding = 6.0f; s.FrameRounding = 4.0f; s.GrabRounding = 4.0f; s.TabRounding = 4.0f;
		s.WindowPadding = { 14.0f, 12.0f }; s.FramePadding = { 8.0f, 5.0f }; s.ItemSpacing = { 10.0f, 7.0f };
		s.WindowBorderSize = 1.0f;
		auto* c = s.Colors;
		// muted, matte: tarred wood and old canvas, nothing bright
		const ImVec4 bg{ 0.09f, 0.085f, 0.075f, 0.94f }, frame{ 0.18f, 0.16f, 0.13f, 1.0f }, frameHi{ 0.25f, 0.22f, 0.17f, 1.0f };
		const ImVec4 accent{ 0.55f, 0.45f, 0.30f, 1.0f }, accentHi{ 0.66f, 0.55f, 0.37f, 1.0f }, text{ 0.86f, 0.82f, 0.74f, 1.0f };
		c[ImGuiCol_WindowBg] = bg;
		c[ImGuiCol_Border] = { 0.35f, 0.30f, 0.22f, 0.6f };
		c[ImGuiCol_Text] = text;
		c[ImGuiCol_TextDisabled] = { 0.55f, 0.52f, 0.46f, 1.0f };
		c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgCollapsed] = { 0.13f, 0.11f, 0.09f, 1.0f };
		c[ImGuiCol_TitleBgActive] = { 0.20f, 0.16f, 0.11f, 1.0f };
		c[ImGuiCol_FrameBg] = frame; c[ImGuiCol_FrameBgHovered] = frameHi; c[ImGuiCol_FrameBgActive] = frameHi;
		c[ImGuiCol_SliderGrab] = accent; c[ImGuiCol_SliderGrabActive] = accentHi; c[ImGuiCol_CheckMark] = accentHi;
		c[ImGuiCol_Button] = { 0.24f, 0.20f, 0.15f, 1.0f }; c[ImGuiCol_ButtonHovered] = { 0.32f, 0.27f, 0.19f, 1.0f }; c[ImGuiCol_ButtonActive] = accent;
		c[ImGuiCol_Header] = { 0.22f, 0.19f, 0.14f, 1.0f }; c[ImGuiCol_HeaderHovered] = { 0.30f, 0.25f, 0.18f, 1.0f }; c[ImGuiCol_HeaderActive] = accent;
		c[ImGuiCol_Tab] = { 0.16f, 0.14f, 0.11f, 1.0f }; c[ImGuiCol_TabHovered] = { 0.32f, 0.27f, 0.19f, 1.0f };
		c[ImGuiCol_TabActive] = { 0.28f, 0.23f, 0.16f, 1.0f };
		c[ImGuiCol_Separator] = { 0.35f, 0.30f, 0.22f, 0.5f };
		s.ScaleAllSizes(g_scale);
	}

	inline void Init()
	{
		auto* rd = RE::BSGraphics::Renderer::GetRendererDataSingleton();
		if (!rd || !rd->forwarder || !rd->context || !rd->renderWindows[0].swapChain) return;  // not yet: try again next open
		auto* sc = reinterpret_cast<IDXGISwapChain*>(rd->renderWindows[0].swapChain);
		DXGI_SWAP_CHAIN_DESC d{};
		if (FAILED(sc->GetDesc(&d))) { g_failed = true; logger::warn("Menu: no swap chain description, the window is off"); return; }
		g_w = static_cast<float>(d.BufferDesc.Width);
		g_h = static_cast<float>(d.BufferDesc.Height);
		g_scale = std::clamp(g_h / 1080.0f, 0.75f, 3.0f);
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.LogFilename = nullptr;
		io.MouseDrawCursor = true;  // the game hides the Windows cursor: ImGui draws its own
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		io.DisplaySize = { g_w, g_h };
		char windir[MAX_PATH]{};
		GetWindowsDirectoryA(windir, MAX_PATH);
		const std::string font = std::string(windir) + "\\Fonts\\segoeui.ttf";
		std::error_code ec;
		if (std::filesystem::exists(font, ec)) io.Fonts->AddFontFromFileTTF(font.c_str(), 18.0f * g_scale);
		else { ImFontConfig cfg; cfg.SizePixels = 13.0f * g_scale; io.Fonts->AddFontDefault(&cfg); }
		ApplyStyle();
		if (!ImGui_ImplDX11_Init(reinterpret_cast<ID3D11Device*>(rd->forwarder), reinterpret_cast<ID3D11DeviceContext*>(rd->context))) {
			g_failed = true;
			logger::warn("Menu: ImGui could not start on the game's device, the window is off");
			ImGui::DestroyContext();
			return;
		}
		g_mx = g_w * 0.5f; g_my = g_h * 0.5f;
		g_ready = true;
		logger::info("Menu: ready ({:.0f} x {:.0f}, scale {:.2f})", g_w, g_h, g_scale);
	}

	// ---- the window ----
	inline void Hint(const char* a_text)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", a_text);
	}
	// a slider that applies when you let go (each change is saved to RowRim.ini)
	inline bool SliderF(const char* a_label, float& a_v, float a_lo, float a_hi, const char* a_fmt)
	{
		ImGui::SliderFloat(a_label, &a_v, a_lo, a_hi, a_fmt, ImGuiSliderFlags_AlwaysClamp);
		return ImGui::IsItemDeactivatedAfterEdit();
	}
	inline bool SliderI(const char* a_label, int& a_v, int a_lo, int a_hi)
	{
		ImGui::SliderInt(a_label, &a_v, a_lo, a_hi, "%d", ImGuiSliderFlags_AlwaysClamp);
		return ImGui::IsItemDeactivatedAfterEdit();
	}
	inline void OnOff(const char* a_label, bool& a_v, const std::string& a_cmdOn, const std::string& a_cmdOff)
	{
		if (ImGui::Checkbox(a_label, &a_v)) Run(a_v ? a_cmdOn : a_cmdOff);
	}

	inline void Draw()
	{
		ImGui::SetNextWindowPos({ g_w * 0.5f, g_h * 0.5f }, ImGuiCond_FirstUseEver, { 0.5f, 0.5f });
		bool keep = true;
		if (!ImGui::Begin("Oars and Sails", &keep, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::End();
			if (!keep) SetOpen(false);
			return;
		}
		ImGui::TextDisabled("%s   |   F10 or Esc to close", kVersionShort);
		ImGui::PushItemWidth(260.0f * g_scale);
		if (ImGui::BeginTabBar("tabs")) {
			if (ImGui::BeginTabItem("Boat")) {
				if (ImGui::Button("Boat here")) Run("boat");
				ImGui::SameLine();
				if (ImGui::Button("Bring under me")) Run("bring");
				ImGui::SameLine();
				if (ImGui::Button("Remove boat")) Run("unboat");
				ImGui::Separator();
				if (SliderF("Top speed", g_v.rowMax, 50.0f, 700.0f, "%.0f")) Run(std::format("row speed {:.0f}", g_v.rowMax));
				Hint("How fast rowing gets you. Default 300.");
				if (SliderF("Glide drag", g_v.rowDrag, 0.01f, 0.8f, "%.3f")) Run(std::format("row drag {:.3f}", g_v.rowDrag));
				Hint("Lower glides further after each stroke. Default 0.07.");
				if (SliderF("Turning", g_v.turn, 0.15f, 1.35f, "%.2f")) Run(std::format("row turn {:.2f}", g_v.turn));
				Hint("How quickly the boat turns. Default 0.45.");
				if (SliderF("Height", g_v.height, -30.0f, 30.0f, "%.0f")) Run(std::format("height {:.0f}", g_v.height));
				Hint("Sits the boat higher or lower in the water. Default 0.");
				if (ImGui::Combo("Paint", &g_v.paint, Paint::kNames, Paint::kCount)) Run(std::string("paint ") + Paint::kNames[g_v.paint]);
				OnOff("Lantern", g_v.lamp, "lamp on", "lamp off");
				OnOff("Boat waits at the docks", g_v.docks, "dock on", "dock off");
				ImGui::EndTabItem();
			}
			if (g_v.sailOn && ImGui::BeginTabItem("Sail")) {
				const char* st[] = { "Mast down", "Furled", "Full", "Reefed" };
				for (int i = 0; i < 4; ++i) {
					if (i) ImGui::SameLine();
					if (ImGui::RadioButton(st[i], g_v.sail == i)) {
						g_v.sail = i;
						static const char* cmd[] = { "sail off", "sail furl", "sail full", "sail reef" };
						Run(cmd[i]);
					}
				}
				ImGui::TextDisabled("Seated: Jump sets the sail. Standing: [E] on the mast.");
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Lake boats")) {
				OnOff("Lake boats", g_v.lakeOn, "lakeboats on", "lakeboats off");
				ImGui::BeginDisabled(!g_v.lakeOn);
				if (SliderI("Per lake", g_v.lakeCount, 0, 10)) Run(std::format("lakeboats {}", g_v.lakeCount));
				Hint("Most boats on one lake (smaller lakes get fewer). Default 3.");
				if (SliderI("Drawn at once", g_v.lakeView, 1, 10)) Run(std::format("lakeboats view{}", g_v.lakeView));
				ImGui::EndDisabled();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Plankton")) {
				OnOff("Glowing plankton", g_v.plOn, "plankton on", "plankton off");
				ImGui::BeginDisabled(!g_v.plOn);
				const char* when[] = { "Night only", "Always", "Never" };
				int w = g_v.plForce > 0 ? 1 : g_v.plForce < 0 ? 2 : 0;
				if (ImGui::Combo("When", &w, when, 3)) {
					g_v.plForce = w == 1 ? 1 : w == 2 ? -1 : 0;
					Run(w == 1 ? "plankton night" : w == 2 ? "plankton never" : "plankton auto");
				}
				if (SliderF("Density", g_v.plDensity, 0.0f, 6.0f, "%.1f")) Run(std::format("plankton density {:.2f}", g_v.plDensity));
				if (SliderF("Glow size", g_v.plSize, 0.2f, 4.0f, "%.2f")) Run(std::format("plankton size {:.2f}", g_v.plSize));
				if (SliderI("Open water glows", g_v.plOpen, 0, 2000)) Run(std::format("plankton open {}", g_v.plOpen));
				if (SliderF("Open water size", g_v.plOpenSize, 0.2f, 4.0f, "%.2f")) Run(std::format("plankton opensize {:.2f}", g_v.plOpenSize));
				if (SliderI("Shoreline glows", g_v.plShore, 0, 400)) Run(std::format("plankton shore {}", g_v.plShore));
				ImGui::EndDisabled();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Effects")) {
				if (ImGui::Checkbox("Boat wake", &g_v.wake)) {
					Plankton::g_set.wake = g_v.wake;
					WritePrivateProfileStringA("Wake", "On", g_v.wake ? "1" : "0", Plankton::IniPath().c_str());
					logger::info("Menu: wake {}", g_v.wake ? "on" : "off");
				}
				if (ImGui::Checkbox("Oar ripples", &g_v.ripples)) {
					if (Fx::g_on != g_v.ripples) Run("ripples");
					g_v.ripples = Fx::g_on;
				}
				OnOff("Underwater filter", g_v.underwater, "underwater on", "underwater off");
				OnOff("Show cell edges (testing)", g_v.grid, "grid on", "grid off");
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Crew")) {
				if (ImGui::Button("Summon crew")) Run("crew summon");
				ImGui::SameLine();
				if (ImGui::Button("Crew list (console)")) Run("crew list");
				if (SliderF("Get up at", g_v.standUp, 100.0f, 3000.0f, "%.0f")) {
					Crew::kLingerLeave = g_v.standUp;
					WritePrivateProfileStringA("Crew", "StandUpAt", std::format("{:.0f}", g_v.standUp).c_str(), Plankton::IniPath().c_str());
					logger::info("Menu: crew get up at {:.0f}", g_v.standUp);
				}
				Hint("Passengers stay seated until you walk this far from the boat. Default 400 (about 6 m).");
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::PopItemWidth();
		ImGui::Separator();
		if (g_resetArm > 0) {
			--g_resetArm;
			if (ImGui::Button("Click again to reset")) { Run("reset settings"); ReadVals(); g_resetArm = 0; }
		} else if (ImGui::Button("Reset settings")) g_resetArm = 180;
		ImGui::SameLine();
		if (ImGui::Button("Close")) keep = false;
		ImGui::End();
		if (!keep) SetOpen(false);
	}

	// ---- each frame, at the end of the game's own drawing ----
	inline void Frame()
	{
		if (!g_open) return;
		if (InLoadOrMainMenu()) { SetOpen(false); return; }
		if (!g_ready) {
			if (g_failed) { g_open = false; return; }
			Init();
			if (!g_ready) { g_open = false; return; }
		}
		auto* rd = RE::BSGraphics::Renderer::GetRendererDataSingleton();
		auto* sc = rd ? reinterpret_cast<IDXGISwapChain*>(rd->renderWindows[0].swapChain) : nullptr;
		auto* dev = rd ? reinterpret_cast<ID3D11Device*>(rd->forwarder) : nullptr;
		auto* ctx = rd ? reinterpret_cast<ID3D11DeviceContext*>(rd->context) : nullptr;
		if (!sc || !dev || !ctx) return;
		auto& io = ImGui::GetIO();
		DXGI_SWAP_CHAIN_DESC d{};
		if (SUCCEEDED(sc->GetDesc(&d)) && d.BufferDesc.Width && d.BufferDesc.Height) {
			g_w = static_cast<float>(d.BufferDesc.Width);
			g_h = static_cast<float>(d.BufferDesc.Height);
		}
		io.DisplaySize = { g_w, g_h };
		const auto now = std::chrono::steady_clock::now();
		io.DeltaTime = std::clamp(std::chrono::duration<float>(now - g_last).count(), 1.0f / 240.0f, 0.25f);
		g_last = now;
		// the pad's left stick moves the cursor
		if (g_padX != 0.0f || g_padY != 0.0f) {
			const float sp = 900.0f * g_scale * io.DeltaTime;
			g_mx += g_padX * sp;
			g_my -= g_padY * sp;
		}
		g_mx = std::clamp(g_mx, 0.0f, g_w - 1.0f);
		g_my = std::clamp(g_my, 0.0f, g_h - 1.0f);
		io.AddMousePosEvent(g_mx, g_my);

		ImGui_ImplDX11_NewFrame();
		ImGui::NewFrame();
		Draw();
		ImGui::Render();
		// draw straight onto the back buffer (a view made for this frame only, so the game can still resize its buffers)
		ID3D11Texture2D* back = nullptr;
		ID3D11RenderTargetView* rtv = nullptr;
		if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) && back) {
			dev->CreateRenderTargetView(back, nullptr, &rtv);
			back->Release();
		}
		if (!rtv) return;
		ID3D11RenderTargetView* oldRtv = nullptr;
		ID3D11DepthStencilView* oldDsv = nullptr;
		ctx->OMGetRenderTargets(1, &oldRtv, &oldDsv);
		ctx->OMSetRenderTargets(1, &rtv, nullptr);
		D3D11_VIEWPORT vp{ 0.0f, 0.0f, g_w, g_h, 0.0f, 1.0f };
		UINT nvp = 1;
		D3D11_VIEWPORT oldVp{};
		ctx->RSGetViewports(&nvp, &oldVp);
		ctx->RSSetViewports(1, &vp);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		if (nvp) ctx->RSSetViewports(1, &oldVp);
		ctx->OMSetRenderTargets(1, &oldRtv, oldDsv);
		if (oldRtv) oldRtv->Release();
		if (oldDsv) oldDsv->Release();
		rtv->Release();
	}

	// ---- input: while the window is open it takes everything, the game gets nothing ----
	inline ImGuiKey KeyFor(std::uint32_t a_dik)
	{
		switch (a_dik) {
		case 0x0E: return ImGuiKey_Backspace;
		case 0x1C: case 0x9C: return ImGuiKey_Enter;
		case 0x0F: return ImGuiKey_Tab;
		case 0xC8: return ImGuiKey_UpArrow;
		case 0xD0: return ImGuiKey_DownArrow;
		case 0xCB: return ImGuiKey_LeftArrow;
		case 0xCD: return ImGuiKey_RightArrow;
		case 0xD3: return ImGuiKey_Delete;
		case 0xC7: return ImGuiKey_Home;
		case 0xCF: return ImGuiKey_End;
		case 0x1D: return ImGuiKey_LeftCtrl;
		case 0x9D: return ImGuiKey_RightCtrl;
		case 0x2A: return ImGuiKey_LeftShift;
		case 0x36: return ImGuiKey_RightShift;
		default: return ImGuiKey_None;
		}
	}
	inline char CharFor(std::uint32_t a_dik)
	{
		if (a_dik >= 0x02 && a_dik <= 0x0A) return static_cast<char>('1' + (a_dik - 0x02));
		switch (a_dik) {
		case 0x0B: case 0x52: return '0';
		case 0x4F: return '1'; case 0x50: return '2'; case 0x51: return '3';
		case 0x4B: return '4'; case 0x4C: return '5'; case 0x4D: return '6';
		case 0x47: return '7'; case 0x48: return '8'; case 0x49: return '9';
		case 0x34: case 0x53: return '.';
		case 0x0C: case 0x4A: return '-';
		default: return 0;
		}
	}
	inline void Feed(RE::InputEvent* a_e)
	{
		if (!g_ready) return;
		auto& io = ImGui::GetIO();
		for (auto* e = a_e; e; e = e->next) {
			if (auto* m = e->AsMouseMoveEvent()) {
				g_mx += static_cast<float>(m->mouseInputX) * g_scale;
				g_my += static_cast<float>(m->mouseInputY) * g_scale;
				continue;
			}
			if (auto* t = e->AsThumbstickEvent()) {
				if (t->IsLeft()) {
					g_padX = std::abs(t->xValue) > 0.15f ? t->xValue : 0.0f;
					g_padY = std::abs(t->yValue) > 0.15f ? t->yValue : 0.0f;
				}
				continue;
			}
			auto* b = e->AsButtonEvent();
			if (!b || !(b->IsDown() || b->IsUp())) continue;
			const bool down = b->IsDown();
			const std::uint32_t id = b->GetIDCode();
			switch (b->GetDevice()) {
			case RE::INPUT_DEVICE::kMouse:
				if (id <= 2) io.AddMouseButtonEvent(static_cast<int>(id), down);
				else if (down && id == 8) io.AddMouseWheelEvent(0.0f, 1.0f);
				else if (down && id == 9) io.AddMouseWheelEvent(0.0f, -1.0f);
				break;
			case RE::INPUT_DEVICE::kKeyboard:
				if (down && id == 0x01) { SetOpen(false); return; }  // Esc
				if (const ImGuiKey k = KeyFor(id); k != ImGuiKey_None) {
					io.AddKeyEvent(k, down);
					if (id == 0x1D || id == 0x9D) io.AddKeyEvent(ImGuiMod_Ctrl, down);
					if (id == 0x2A || id == 0x36) io.AddKeyEvent(ImGuiMod_Shift, down);
				}
				if (down) if (const char c = CharFor(id)) io.AddInputCharacter(static_cast<unsigned int>(c));
				break;
			case RE::INPUT_DEVICE::kGamepad:
				if (id == 0x1000) io.AddMouseButtonEvent(0, down);        // A clicks
				else if (down && id == 0x2000) { SetOpen(false); return; }  // B closes
				break;
			default:
				break;
			}
		}
	}

	struct PresentHook
	{
		static void thunk(std::uint32_t a_timer)
		{
			func(a_timer);
			Frame();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct InputHook
	{
		static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_src, RE::InputEvent* const* a_events)
		{
			bool toggle = false;
			if (a_events) {
				for (auto* e = *a_events; e; e = e->next) {
					if (auto* b = e->AsButtonEvent(); b && b->GetDevice() == RE::INPUT_DEVICE::kKeyboard && b->GetIDCode() == g_key && b->IsDown()) toggle = true;
				}
			}
			if (toggle) {
				auto* ui = RE::UI::GetSingleton();
				const bool console = ui && ui->IsMenuOpen(RE::Console::MENU_NAME);
				if (!console || g_open) SetOpen(!g_open);
			}
			if (!g_open) { func(a_src, a_events); return; }
			if (a_events) Feed(*a_events);
			// v1.7.85: an empty list the next hooks may write to (SKSE Menu Framework rewrites the list head in place: a
			// read-only one crashed it). Cleared every time.
			static RE::InputEvent* s_none[2]{};
			s_none[0] = s_none[1] = nullptr;
			func(a_src, s_none);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	inline bool CallAt(std::uintptr_t a_addr, const char* a_what)
	{
		if (*reinterpret_cast<const std::uint8_t*>(a_addr) != 0xE8) {
			logger::warn("Menu: the {} hook spot is not a call (another game version?): the window is off", a_what);
			return false;
		}
		return true;
	}

	inline void Install()
	{
		if (g_hooked) return;
		const auto ini = Plankton::IniPath();
		g_key = static_cast<std::uint32_t>(GetPrivateProfileIntA("UI", "Key", 0x44, ini.c_str()));
		if (g_key == 0 || g_key > 0xFF) g_key = 0x44;
		const std::uintptr_t present = REL::RelocationID(75461, 77246).address() + 0x9;
		const std::uintptr_t input = REL::RelocationID(67315, 68617).address() + 0x7B;
		if (!CallAt(present, "draw") || !CallAt(input, "input")) { g_failed = true; return; }
		auto& tr = SKSE::GetTrampoline();
		PresentHook::func = tr.write_call<5>(present, PresentHook::thunk);
		InputHook::func = tr.write_call<5>(input, InputHook::thunk);
		g_hooked = true;
		logger::info("Menu: installed (key {:#x}, RowRim.ini [UI] Key; or oas menu)", g_key);
	}
}

void OverlayToggle()
{
	if (Overlay::g_failed || !Overlay::g_hooked) { Print("RowRim: the Oars and Sails window is not available (see RowRim.log)"); return; }
	Overlay::SetOpen(!Overlay::g_open);
}
