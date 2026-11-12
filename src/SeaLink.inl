// v1.8.10: boats ride the OceanRim sea, when that plugin is there (nothing changes without it). See src\Sea_v1810_Waves.md
namespace Plankton { inline std::string IniPath(); }

namespace SeaLink
{
	using SurfaceFn = bool (*)(float, float, float, float*);
	using HullFn = void (*)(std::uint32_t, float, float, float, float, float, float);
	inline SurfaceFn g_surface = nullptr;
	inline HullFn g_hull = nullptr;
	inline bool g_looked = false;
	using GetFn = bool (*)(const char*, float*);
	using SetFn = bool (*)(const char*, float);
	inline GetFn g_get = nullptr;
	inline SetFn g_set = nullptr;
	using SplashFn = void (*)(float, float, float, float, float, int);
	inline SplashFn g_splash = nullptr;
	inline SurfaceFn g_mean = nullptr;  // v1.8.20: OceanRim 0.6.8 resting level (sea level + trough lift)  // v1.8.15: OceanRim 0.6.0 wakes and splashes

	// v1.8.11: RowRim.ini [Sea], and the Sea page in the menu. v1.8.26: each boat its own: [Sea] the rowboat (and the lake
	// boats), [SailBoat.Sea] the sailing boat, [Longship.Sea] the longship
	struct Tune
	{
		float follow = 1.36f;   // how much of the wave height the hull takes
		float tilt = 1.5f;      // how much of the wave slope it takes
		float soft = 0.2f;      // seconds to answer a wave; a longship takes half as long again
		float maxTilt = 14.0f;  // degrees (v1.8.19: Miguel's tuned values as defaults)
		float trim = 2.5f;      // v1.8.15: degrees the bow rises at full speed
		float squat = 3.0f;     // v1.8.15: units the hull sinks at full speed
	};
	inline Tune g_t[3];
	inline Tune& T(int a_k) { return g_t[std::clamp(a_k, 0, 2)]; }
	inline const char* Sec(int a_k) { return a_k == 1 ? "SailBoat.Sea" : a_k == 2 ? "Longship.Sea" : "Sea"; }
	// v1.8.13: what the waves did to the player's boat last frame, for the Sea page
	inline bool g_playerOk = false;
	inline float g_rawH = 0.0f, g_outH = 0.0f, g_outP = 0.0f, g_outR = 0.0f;
	inline float ReadF(int a_k, const char* a_key, float a_def)
	{
		char b[32]{};
		GetPrivateProfileStringA(Sec(a_k), a_key, "", b, sizeof(b), Plankton::IniPath().c_str());
		try { return b[0] ? std::stof(b) : a_def; } catch (...) { return a_def; }
	}
	inline void Save(int a_k, const char* a_key, float a_v)
	{
		WritePrivateProfileStringA(Sec(a_k), a_key, std::format("{:.3f}", a_v).c_str(), Plankton::IniPath().c_str());
	}

	// The hull's answer: a soft spring with a little give, so it lifts and settles like something floating
	struct State
	{
		float h = 0.0f, p = 0.0f, r = 0.0f, hv = 0.0f, pv = 0.0f, rv = 0.0f, last = 0.0f;
	};
	inline void Step(State& a_s, float a_h, float a_p, float a_r, float a_dt, float a_softScale, int a_k = 0)
	{
		const float w = 2.0f / (std::max)(T(a_k).soft * a_softScale, 0.1f);
		float left = std::clamp(a_dt, 0.0f, 0.25f);
		while (left > 0.0f) {
			const float d = (std::min)(left, 0.02f);
			left -= d;
			const auto one = [&](float& x, float& v, float t) {
				v += (w * w * (t - x) - 2.0f * 0.75f * w * v) * d;
				x += v * d;
			};
			one(a_s.h, a_s.hv, a_h);
			one(a_s.p, a_s.pv, a_p);
			one(a_s.r, a_s.rv, a_r);
		}
	}

	inline void Find()
	{
		if (g_looked) return;
		g_looked = true;
		// v1.8.53: OceanRim support is off for this release (the boats stay on the game's own flat water, OceanRim or not)
		constexpr bool kOceanRim = false;
		if (auto* mod = kOceanRim ? GetModuleHandleA("OceanRim.dll") : nullptr) {
			g_surface = reinterpret_cast<SurfaceFn>(GetProcAddress(mod, "OceanRim_SurfaceAt"));
			g_hull = reinterpret_cast<HullFn>(GetProcAddress(mod, "OceanRim_SetHull"));
			g_get = reinterpret_cast<GetFn>(GetProcAddress(mod, "OceanRim_GetSetting"));
			g_set = reinterpret_cast<SetFn>(GetProcAddress(mod, "OceanRim_SetSetting"));
			g_splash = reinterpret_cast<SplashFn>(GetProcAddress(mod, "OceanRim_AddSplash"));
			g_mean = reinterpret_cast<SurfaceFn>(GetProcAddress(mod, "OceanRim_MeanAt"));
		}
		for (int k = 0; k < 3; ++k) {  // v1.8.26: each boat its own
			auto& t = g_t[k];
			t.follow = ReadF(k, "Follow", t.follow);
			t.tilt = ReadF(k, "Tilt", t.tilt);
			t.soft = ReadF(k, "Softness", t.soft);
			t.maxTilt = ReadF(k, "MaxTilt", t.maxTilt);
			t.trim = ReadF(k, "Trim", t.trim);
			t.squat = ReadF(k, "Squat", t.squat);
		}
		logger::info("Sea: OceanRim {}", !kOceanRim ? "support off (v1.8.53)" : g_surface && g_hull ? "found, boats ride its waves" : "not found");
	}

	// What the waves do to a hull at this spot: rises with the average of five points, tilts with the difference between
	// bow and stern, port and starboard. Pitch + bow up, roll + starboard down, as the boat's other motion. a_z: its water.
	inline bool Motion(float a_x, float a_y, float a_z, float a_yaw, float a_halfLen, float a_halfWide, float& a_heave, float& a_pitch, float& a_roll, int a_k = 0)
	{
		Find();
		const Tune& t = T(a_k);
		if (!g_surface) return false;
		const float fx = std::sin(a_yaw), fy = std::cos(a_yaw);
		float mid = 0.0f, bow = 0.0f, stern = 0.0f, port = 0.0f, star = 0.0f;
		if (!g_surface(a_x, a_y, a_z, &mid) ||
			!g_surface(a_x + fx * a_halfLen, a_y + fy * a_halfLen, a_z, &bow) ||
			!g_surface(a_x - fx * a_halfLen, a_y - fy * a_halfLen, a_z, &stern) ||
			!g_surface(a_x - fy * a_halfWide, a_y + fx * a_halfWide, a_z, &port) ||
			!g_surface(a_x + fy * a_halfWide, a_y - fx * a_halfWide, a_z, &star)) return false;
		const float maxTilt = t.maxTilt * 0.0174533f;
		// v1.8.12: mostly the water under its middle. v1.8.20: the raised sea level (trough lift) is taken as it is, only the
		// waves above or below it are scaled by Follow (above 1 it lifted the boat off a raised sea)
		float mean = a_z;
		if (g_mean && !g_mean(a_x, a_y, a_z, &mean)) mean = a_z;
		a_heave = (mean - a_z) + (mid * 0.5f + (bow + stern + port + star) * 0.125f - mean) * t.follow;
		a_pitch = std::clamp(std::atan2(bow - stern, 2.0f * a_halfLen) * t.tilt, -maxTilt, maxTilt);
		a_roll = std::clamp(std::atan2(port - star, 2.0f * a_halfWide) * t.tilt, -maxTilt, maxTilt);
		return true;
	}

	// v1.8.15: a splash on the OceanRim sea (nothing off it): a_kind 0 an oar or the hull's own ripples, 1 the bow digging in
	inline void Splash(float a_x, float a_y, float a_z, float a_radius, float a_strength, int a_kind)
	{
		Find();
		if (g_splash) g_splash(a_x, a_y, a_z, a_radius, a_strength, a_kind);
	}

	// The hull's footprint, so the sea stays out of it. a_waterline: the height the hull floats at.
	inline void Hull(std::uint32_t a_id, float a_x, float a_y, float a_yaw, float a_halfLen, float a_halfWide, float a_waterline)
	{
		Find();
		if (g_hull) g_hull(a_id, a_x, a_y, a_yaw, a_halfLen, a_halfWide, a_waterline - 8.0f);
	}
}
