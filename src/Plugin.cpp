// Oars and Sails - rowboat SKSE plugin for Skyrim Special Edition / Anniversary Edition
// Copyright (C) 2026 Miguel32
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details. You should have received a copy of the GNU General Public
// License along with this program (LICENSE.txt). If not, see <https://www.gnu.org/licenses/>.
//
// Built with CommonLibSSE-NG (GPL-3.0-or-later WITH Modding Exception AND GPL-3.0 Linking Exception).

// Oars and Sails - rowboats you can place, board and row (split from AutoCam v9.70)
// Target: Skyrim SE/AE, SKSE + CommonLibSSE-NG
//
// What it does:
//   - Repurposes an unused dev console command as "oas".
//   - "oas boat" places a solid rowboat on the water; walk up and press E to board.
//   - The player (and followers) ride it; W rows with the rowing animation, A/D steer, E gets off.

#include "PCH.h"

#include "BoatBow.h"
#include "LakeBoats.h"
#include "TrueDirectionalMovementAPI.h"  // 56.43

#undef PlaySound  // windows.h makes it PlaySoundA; RE::PlaySound is meant

#include <algorithm>
#include <functional>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <mutex>
#include <optional>
#include <random>
#include <source_location>
#include <sstream>
#include <string>
#include <unordered_map>
#include <string_view>
#include <vector>
#include <cstring>

namespace
{
	// ------------------------------------------------------------------
	// Tuning (game units; a person is roughly 128 units tall)
	// ------------------------------------------------------------------
	constexpr float kPi = 3.14159265f;

	constexpr float kSkimCliffDrop = 80.0f;          // ground this far below the water = a waterfall lip / cliff

	// Boat physics move the player directly; never changes SpeedMult.
	constexpr float kSkimEyeHeight = 35.0f;
	constexpr float kRampGravity = 420.0f;            // speed gained per second per unit of downhill slope while riding a waterfall slope
	constexpr float kRampDrag = 1100.0f;              // speed lost per second per unit of uphill slope
	constexpr float kFallLaunchSpeed = 420.0f;        // speed the current carries the boat to at a waterfall lip (above the rowing top speed)
	constexpr float kFallFlowAccel = 520.0f;          // u/s^2 of extra push from the flow while a fall is ahead
	constexpr float kFallMaxSpeed = 700.0f;           // top speed while riding / flying off a fall (decays back to the profile max afterwards)
	constexpr float kFallApproachLift = 55.0f;        // 35.1: extra height while a fall is ahead, so the boat goes over the lip's swell
	constexpr float kRampRideLift = 40.0f;            // 35.1: extra height riding a fall's slope / flying off it (through it, not on it)
	constexpr float kFallExitGrace = 1.6f;           // seconds after splashing down from a fall: ignore the wreckage around the pool so we never get pinned
	constexpr float kUnstickTime = 1.5f;              // pinned against something for this long with the throttle held: ghost through it
	constexpr float kRampDetachSlope = 1.2f;          // ground falling away steeper than this (rise/run) = fly off it
	constexpr float kBoardRange = 420.0f;              // how close you must be to the parked boat to board it
	constexpr float kAdoptRange = 1500.0f;             // v1.0.14: a parked boat this close is picked up (after a load), so its prompts and pick proxies are ready before the player reaches it
	constexpr float kFpEyeBoost = 22.0f;              // v1.0.15 (B6): first person eye height aboard, both seats (was 12; +10 so the bow and oars do not block the view)
	// v1.0.18 (Part 16): our own row loop, added to the behaviour graphs by Nemesis/Pandora from
	// meshes\actors\character\animations\OarsAndSails\FNIS_OarsAndSails_List.txt (a basic idle, so the alias is the event).
	// FNIS basic idles have no stop event of their own: the game's IdleForceDefaultState ends them.
	constexpr const char* kRowStartEvent = "OarsRowStart";
	constexpr const char* kRowStopEvent = "IdleForceDefaultState";
	constexpr float kParkedBoatForward = 450.0f;       // parked boat appears this far ahead of you (falls back to nearer water)
	constexpr float kParkedBoatZ = -10.0f;             // parked boat origin height relative to the water surface
	constexpr float kV7FeetClearance = 4.0f;          // v7: player's feet this far above the water (keeps it out of swim mode)
	constexpr float kShoreRange = 210.0f;             // exit: land within ~3 m of the boat puts us ashore, otherwise we splash in
	constexpr float kCrouchEyeLift = 36.0f;           // crouching drops the first person eye; lift the actor by this to keep the view at standing height
	constexpr float kBoatExitDrop = 30.0f;            // exit: height above the water where everyone is set down inside the boat
	constexpr float kV7PlayerDrop = 42.0f;            // v7: eye sits this much lower, matching the tuned boat height
	constexpr float kV7BoatRaise = 60.0f;             // v7: boat floats this far above the water so it stays in view from standing eye height
	constexpr float kSkimMaxSpeed = 900.0f;
	constexpr float kSkimAcceleration = 260.0f;  // outboard-like: ~5 s from rest to full speed (tapers as speed builds)
	constexpr float kSkimBrake = 380.0f;  // holding S: ~3.5 s from full speed to a stop (gentler as it slows)
	constexpr float kSkimGravity = 600.0f;
	constexpr float kSkimMinLaunchSpeed = 100.0f;
	constexpr float kSkimDropThreshold = 40.0f;
	constexpr float kSkimDropGrace = 0.35f;
	// Independent boat steering + restricted camera look.
	constexpr float kSkimTurnLow = 0.8f;              // max turn rate (rad/s) at low speed (heavy, lazy turns when crawling)
	constexpr float kSkimTurnHigh = 1.0f;             // max turn rate (rad/s) at full speed
	constexpr float kSkimTurnResponse = 2.0f;         // how quickly the boat reaches its turn rate (eases in and out)
	constexpr float kSkimTurnAuthoritySpeed = 400.0f; // steering reaches full bite at this speed
	constexpr float kSkimTurnDrag = 0.25f;            // hard turns at speed scrub speed (1/s at full speed and full turn rate)
	constexpr float kSkimReverseMax = 100.0f;         // top reverse speed (u/s)
	constexpr float kSkimReverseAccel = 35.0f;        // ~3 s to reach it

	// Speed model. The motor profile is the original outboard/speedboat behaviour (kept for the speedboat mod).
	// The row profile is a heavy rowing boat: slow, with speed gained only on the drive stroke and a long glide.
	struct SkimProfile
	{
		float maxSpeed;       // top speed (u/s)
		float accel;          // forward acceleration (u/s^2). Row: peak drive thrust scale (accel/coastDrag = steady top speed)
		float brake;          // holding S while moving
		float coastDrag;      // exp decay rate (1/s) with no throttle
		float reverseMax;
		float reverseAccel;
		float turnLow;        // max turn rate (rad/s) at low speed
		float turnHigh;       // max turn rate (rad/s) at top speed
		float turnAuthority;  // steering reaches full bite at this speed
		float turnFloor;      // fraction of the turn rate available when stopped (rowers can spin the boat on the spot)
	};
	constexpr SkimProfile kMotorProfile{ kSkimMaxSpeed, kSkimAcceleration, kSkimBrake, 0.12f, kSkimReverseMax, kSkimReverseAccel, kSkimTurnLow, kSkimTurnHigh, kSkimTurnAuthoritySpeed, 0.5f };
	constexpr float kRowMaxSpeed = 300.0f;  // 56.50: Miguel's tune (was 280)
	constexpr float kRowCoastDrag = 0.07f;            // 56.50: Miguel's tune (56.49 0.05, was 0.25)
	constexpr float kRowAccelRate = 0.6f;             // 56.49: stroke pull = top speed x this, on its own (was top speed x drag: 0.25)
	constexpr float kRowStrokesBase = 0.3366f;        // strokes per second: your tuned speed for the 3 second rowing animation (about 2.97 s per stroke, 20 per minute)
	constexpr float kRowStrokesGain = 0.0f;           // no extra strokes with speed any more, so the oars stay in step with the animation
	// Land / obstacle behaviour: bleed speed instead of stopping dead.
	constexpr float kSkimLandMaxSpeed = 120.0f;       // crawl speed on dry land
	constexpr float kSkimLandDecel = 900.0f;          // units/s^2 lost while above crawl speed on land
	constexpr float kSkimBeachMaxDistance = 1500.0f;  // how far up a beach / bank the boat may slide (outer safety limit)
	// Fake collision, stage A: the hull is sampled at five points around its centre (the hull centre is BoatBow's offset,
	// about 250 forward of the vehicle point) and tilted / lifted to the ground under it. Never real collision.
	constexpr float kHullBowFwd = 220.0f;              // bow sample, forward of the hull centre
	constexpr float kHullSternFwd = -220.0f;           // stern sample
	constexpr float kHullSide = 70.0f;                 // port / starboard samples, either side of the centre line
	constexpr float kMaxGroundPitch = 0.4363f;         // 25 degrees
	constexpr float kMaxGroundRoll = 0.1745f;          // 10 degrees
	constexpr float kGroundTiltRate = 8.0f;            // ease toward the ground's pitch (1/s)
	constexpr float kGroundRollRate = 5.0f;            // ease toward the ground's roll (1/s), calmer than pitch
	constexpr float kBeachMaxRise = 40.0f;             // ground higher than this above the water is a bank / cliff, not a beach
	constexpr float kMaxBankSlope = 0.4663f;           // tan(25 deg): a sample steeper than this from the next one inward is a bank
	constexpr float kMaxGroundLift = 30.0f;            // the ground never lifts the boat more than this above where the water puts it
	constexpr float kLiftRiseRate = 6.0f;              // ease of the lift going up (1/s)
	constexpr float kLiftFallRate = 10.0f;             // and coming down (1/s)
	constexpr float kDryHysteresis = 0.2f;             // a hull sample must stay touching / clear this long before it counts as changed
	constexpr float kBankStopGap = 0.0f;               // a bank ahead: the tip stops where the bank was found (39.4: was 20 past a sample 80 short of the tip)
	constexpr int kGroundSamples = 7;                  // 39.2: centre, tip, stern, starboard, port, two bow quarters
	// Contact auto-off: a waterfall or drop ahead suspends the fake collision until it has been quiet for a while.
	constexpr float kContactResumeDelay = 2.0f;        // seconds with nothing ahead (and not falling / on a slope) before contact returns
	constexpr float kContactResumeTime = 0.5f;         // contact eases back in over this long
	constexpr float kLookAheadMin = 250.0f;            // look-ahead distances: speed x 1 s and x 2 s, clamped to this range
	constexpr float kLookAheadMax = 900.0f;
	constexpr float kLookAheadInterval = 0.1f;         // the waterfall scan walks the cell's references, so it runs 10 times a second
	constexpr float kSuspendLiftRate = 40.0f;          // suspended (or no dry point): the lift drops this fast (units/s)
	constexpr float kSuspendTiltRate = 1.0472f;        // and the tilt this fast (60 deg/s)
	constexpr float kGroundDryRise = 12.0f;            // ground this far above the water counts as dry for the lift / tilt (gravel bars below it do not)
	constexpr float kGroundLiftAllowance = 4.0f;       // the hull may sit this far into a dry sample before it is lifted
	constexpr float kPostLandHold = 1.5f;              // after landing from a launch or a fall, no lift or tilt for this long
	// Late boarding: followers who arrive after the player boarded walk up and hop in.
	constexpr float kLateBoardMaxSpeed = 80.0f;        // only while the boat is this slow (and calm)
	constexpr float kLateBoardRange = 380.0f;          // a follower this close to the hull centre (horizontal) ...
	constexpr float kLateBoardDwell = 0.8f;            // ... for this long boards
	constexpr float kLateBoardCheck = 0.25f;           // the check runs four times a second
	constexpr float kLateBoardTime = 0.8f;             // the hop from the shore to the seat takes this long
	constexpr float kLateBoardArc = 30.0f;             // and rises this high in the middle
	constexpr float kHullClearance = 6.0f;             // gap kept between the ground and the hull bottom
	constexpr float kBeachMaxSlide = 330.0f;           // the bow slides at most this far up the sand past where it first touched
	constexpr float kBeachDragStart = 0.5f;            // heavy drag from this fraction of kBeachMaxSlide on
	constexpr float kBeachDrag = 40.0f;                // that drag at the cap (1/s, exponential)
	constexpr float kSkimSlideDecel = 300.0f;         // units/s^2 lost while sliding fast over dirt (gentler than the crawl brake)
	constexpr float kSpeedSink = 16.0f;               // at full speed the boat, player and passengers sit this much lower
	constexpr float kSkimBlockedDrag = 5.0f;          // exp decay rate while held against an obstacle
	constexpr float kSkimUnloadedDrag = 2.5f;         // exp decay rate while the next cell isn't loaded
	// Buoyant dip on water landing (damped spring on camera height).
	constexpr float kSkimDipStiffness = 90.0f;
	constexpr float kSkimDipDamping = 8.5f;
	constexpr float kSkimDipMax = 28.0f;              // deepest dip below normal eye height
	constexpr float kSkimDipSpeedLoss = 0.20f;        // max fraction of speed lost on the hardest landing
	constexpr float kSkimRecoverAccel = 250.0f;       // extra accel back to pre-landing speed
	constexpr float kSkimRecoverTime = 1.6f;

	// Any dev console command we'll never otherwise use. "TestCode" is an
	// unused Bethesda leftover in the console command table.
	constexpr const char* kHijackedCommand = "TestCode";

	// ------------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------------
	float WrapAngle(float a)
	{
		while (a > kPi) a -= 2.0f * kPi;
		while (a < -kPi) a += 2.0f * kPi;
		return a;
	}

	void DumpActorAnimation(RE::Actor* a_actor);
	void DumpAllGraphVars(RE::Actor* a_actor, const char* a_tag);

	class OarsAndSails
	{
	public:
		static OarsAndSails& Get()
		{
			static OarsAndSails instance;
			return instance;
		}

		// Hides/shows just the HUD Menu (health/stamina/magicka bars,
		// crosshair, compass, active effects, etc.) via the same message
		// the game itself uses - this leaves every other menu, including
		// the ~ console, completely untouched, unlike UI::ShowMenus(false)
		// which would hide the whole menu stack at once.
		void HideHUD()
		{
			if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
				queue->AddMessage(RE::HUDMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			}
		}

		void ShowHUD()
		{
			if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
				queue->AddMessage(RE::HUDMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
			}
		}

		// Stronger than ShowHUD: also makes the HUD movie itself visible and opaque, and says in the log what it found
		// (the movie's own _visible / _alpha before and after, which shows whether another mod is hiding it).
		void ForceHudVisible(bool a_log)
		{
			ShowHUD();
			auto* ui = RE::UI::GetSingleton();
			if (!ui) return;
			const bool open = ui->IsMenuOpen(RE::HUDMenu::MENU_NAME);
			auto menu = ui->GetMenu(RE::HUDMenu::MENU_NAME);
			bool movie = false;
			if (menu && menu->uiMovie) {
				movie = true;
				auto* mv = menu->uiMovie.get();
				mv->SetVisible(true);
				RE::GFxValue rv, ra, bv, ba;
				const bool gotRv = mv->GetVariable(&rv, "_root._visible");
				const bool gotRa = mv->GetVariable(&ra, "_root._alpha");
				const bool gotBv = mv->GetVariable(&bv, "_root.HUDMovieBaseInstance._visible");
				const bool gotBa = mv->GetVariable(&ba, "_root.HUDMovieBaseInstance._alpha");
				if (a_log) {
					logger::info("HUD before: root visible={} alpha={:.0f}  base visible={} alpha={:.0f}  menu flags={:#x}",
						gotRv && rv.IsBool() ? (rv.GetBool() ? 1 : 0) : -1, gotRa && ra.IsNumber() ? ra.GetNumber() : -1.0,
						gotBv && bv.IsBool() ? (bv.GetBool() ? 1 : 0) : -1, gotBa && ba.IsNumber() ? ba.GetNumber() : -1.0,
						static_cast<std::uint32_t>(menu->menuFlags.underlying()));
				}
				mv->SetVariable("_root._visible", RE::GFxValue(true));
				mv->SetVariable("_root._alpha", RE::GFxValue(100.0));
				mv->SetVariable("_root.HUDMovieBaseInstance._visible", RE::GFxValue(true));
				mv->SetVariable("_root.HUDMovieBaseInstance._alpha", RE::GFxValue(100.0));
			}
			if (a_log) logger::info("HUD: shown (menu open={}, movie found={})", open, movie);
		}

		// Look through a short runway for a lower, separate water plane.
		// This distinguishes a waterfall lip from an ordinary dry shore.
		bool SkimDropAhead(const RE::NiPoint3& a_from, const RE::NiPoint3& a_forward, float a_speed, float& a_lowerWater)
		{
			// Longer reach at speed: real waterfalls overhang the lip by
			// several metres, so the lower pool can be well past the edge.
			const float runway = (std::max)(96.0f, a_speed * 0.25f);
			for (float distance : { 48.0f, 96.0f, 160.0f, runway }) {
				float lower = 0.0f;
				const RE::NiPoint3 probe{ a_from.x + a_forward.x * distance, a_from.y + a_forward.y * distance, a_from.z };
				if (SkimWaterAt(probe, lower) && skimWaterHeight_ - lower >= kSkimDropThreshold) {
					a_lowerWater = lower;
					return true;
				}
			}
			return false;
		}

		// Long-range look for a lower, separate water plane: a fall ahead whose current should pull the boat along.
		bool SkimFlowAhead(const RE::NiPoint3& a_from, const RE::NiPoint3& a_forward)
		{
			for (float distance : { 150.0f, 250.0f, 350.0f, 450.0f, 550.0f }) {
				float lower = 0.0f;
				const RE::NiPoint3 probe{ a_from.x + a_forward.x * distance, a_from.y + a_forward.y * distance, a_from.z };
				if (SkimWaterAt(probe, lower) && skimWaterHeight_ - lower >= kSkimDropThreshold) return true;
			}
			return false;
		}

		// A waterfall lip is often a rock ledge at about water level with the
		// ground falling away beyond it and no lower water plane in reach. Called
		// only when we just ran out of water; true if the ground right ahead is
		// still near water level (a ledge, not a bank) and drops sharply further on.
		bool SkimCliffAhead(const RE::NiPoint3& a_from, const RE::NiPoint3& a_forward, float a_speed, float& a_lowLand)
		{
			auto* tes = RE::TES::GetSingleton();
			if (!tes) return false;
			float nearLand = 0.0f;
			const RE::NiPoint3 nearP{ a_from.x + a_forward.x * 16.0f, a_from.y + a_forward.y * 16.0f, a_from.z };
			if (!tes->GetLandHeight(nearP, nearLand) || !std::isfinite(nearLand)) return false;
			if (nearLand > skimWaterHeight_ + 24.0f) return false;  // a bank or wall, not a ledge
			const float runway = (std::max)(240.0f, a_speed * 0.35f);
			for (float distance : { 48.0f, 96.0f, 160.0f, 240.0f, runway }) {
				float land = 0.0f;
				const RE::NiPoint3 p{ a_from.x + a_forward.x * distance, a_from.y + a_forward.y * distance, a_from.z };
				if (tes->GetLandHeight(p, land) && std::isfinite(land) && skimWaterHeight_ - land >= kSkimCliffDrop) {
					a_lowLand = land;
					return true;
				}
			}
			return false;
		}

		// Waterfall/rapids meshes (STAT/ACTI refs whose model path or scene
		// node name contains "waterfall"/"rapids") count as water, not rock:
		// the camera glides over the lip and through the falls instead of
		// stopping on them or treating them as launchable rocks.
		bool SkimWaterfallNear(const RE::NiPoint3& a_pos)
		{
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(a_pos) : nullptr;
			if (!cell || !cell->IsAttached()) {
				return false;
			}
			bool found = false;
			cell->ForEachReferenceInRange(a_pos, 300.0f,
				[&](RE::TESObjectREFR* a_ref) -> RE::BSContainer::ForEachResult {
					if (found || !a_ref) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
					auto* base = a_ref->GetBaseObject();
					if (!base) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
					const char* model = nullptr;
					switch (base->GetFormType()) {
					case RE::FormType::Static:
						model = static_cast<RE::TESObjectSTAT*>(base)->GetModel();
						break;
					case RE::FormType::Activator:
						model = static_cast<RE::TESObjectACTI*>(base)->GetModel();
						break;
					default:
						break;
					}
					std::string hay;
					if (model) {
						hay += model;
					}
					if (auto* root = a_ref->Get3D()) {
						if (root->name.c_str()) {
							hay += ' ';
							hay += root->name.c_str();
						}
					}
					std::transform(hay.begin(), hay.end(), hay.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (hay.find("waterfall") != std::string::npos || hay.find("rapids") != std::string::npos) {
						found = true;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			return found;
		}

		// Reject unloaded cells and dry landscape, even if the exterior
		// cell has a nominal water plane. Rivers with placed water are
		// queried at the actual sample position by the engine.
		// a_why (optional) says why a sample is not skimmable:
		//   1 = cell not loaded/exterior/physics (hold + slow, it may load in)
		//   2 = no usable water height here (hold + slow)
		//   3 = land is at or above the water surface (a shore: beach glide)
		bool SkimWaterAt(const RE::NiPoint3& a_pos, float& a_height, int* a_why = nullptr)
		{
			if (a_why) *a_why = 0;
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(a_pos) : nullptr;
			if (!cell || !cell->IsExteriorCell() || !cell->IsAttached() || !cell->GetbhkWorld()) { if (a_why) *a_why = 1; return false; }
			float water = 0.0f;
			float land = 0.0f;
			if (!cell->GetWaterHeight(a_pos, water) || !std::isfinite(water) || std::abs(water) > 1000000.0f) { if (a_why) *a_why = 2; return false; }
			// Very shallow water (gravel bars, river shallows) used to count as
			// "land" and stopped the camera for no visible reason. Only treat
			// the ground as a shore once it actually rises above the surface.
			if (!tes->GetLandHeight(a_pos, land) || !std::isfinite(land)) { if (a_why) *a_why = 2; return false; }
			if (land >= water + 4.0f) { if (a_why) *a_why = 3; return false; }
			a_height = water;
			NotePatch(a_pos, water);  // 41B
			return true;
		}

		bool SkimSweep(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float& a_fraction, RE::COL_LAYER* a_layer = nullptr, RE::TESObjectREFR** a_ref = nullptr)
		{
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(a_from) : nullptr;
			auto* world = cell && cell->IsAttached() ? cell->GetbhkWorld() : nullptr;
			if (!world) { a_fraction = 0.0f; return true; }
			RE::bhkPickData pick{};
			const float scale = RE::bhkWorld::GetWorldScale();
			pick.rayInput.from = RE::hkVector4(a_from * scale);
			pick.rayInput.to = RE::hkVector4(a_to * scale);
			pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kCameraPick);
			pick.rayOutput.Reset();
			world->PickObject(pick);
			if (!pick.rayOutput.HasHit()) return false;
			const auto layer = pick.rayOutput.rootCollidable->GetCollisionLayer();
			if (layer == RE::COL_LAYER::kWater || layer == RE::COL_LAYER::kTrigger || layer == RE::COL_LAYER::kNonCollidable) return false;
			// Layers that are not real obstacles. The log showed 'blocked' events on
			// open water from layer 57 (not a named layer; it appeared the moment the
			// boat model loaded), plus actor/zone volumes. These caused the random stops.
			const int layerId = static_cast<int>(layer);
			if (layerId >= 47 ||
				layer == RE::COL_LAYER::kActorZone || layer == RE::COL_LAYER::kCharController ||
				layer == RE::COL_LAYER::kProjectileZone || layer == RE::COL_LAYER::kAcousticSpace ||
				layer == RE::COL_LAYER::kPortal || layer == RE::COL_LAYER::kCollisionBox ||
				layer == RE::COL_LAYER::kAvoidBox || layer == RE::COL_LAYER::kDoorDetection) return false;
			a_fraction = std::clamp(pick.rayOutput.hitFraction, 0.0f, 1.0f);
			if (boardedParked_ && layer == RE::COL_LAYER::kStatic) {
				// The parked boat's own physics body can linger after SetCollision(false)
				// and pins the skimmer in place. Ignore statics right at the boarding spot.
				const RE::NiPoint3 hit = a_from + (a_to - a_from) * a_fraction;
				const float dx = hit.x - boardSpot_.x, dy = hit.y - boardSpot_.y;
				if (dx * dx + dy * dy < 450.0f * 450.0f) return false;
			}
			if (a_layer) *a_layer = layer;
			if (a_ref) *a_ref = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
			return true;
		}

		// Rate-limited log so the next "random stop" tells us exactly why.
		void SkimLogSlow(const char* a_kind, int a_why, bool a_hit, RE::COL_LAYER a_layer, const RE::NiPoint3& a_pos)
		{
			if (skimStopLogCooldown_ > 0.0f) return;
			skimStopLogCooldown_ = 1.0f;
			logger::info("Skim {}: why={} (1=cell/physics not loaded, 2=no water height, 3=shore) hit={} layer={} speed={:.1f} pos=({:.0f},{:.0f},{:.0f})",
				a_kind, a_why, a_hit, static_cast<int>(a_layer), skimSpeed_, a_pos.x, a_pos.y, a_pos.z);
		}

		// True if there is ground just ahead that is not a wall (at or below our surface).
		bool SkimGroundRideable(const RE::NiPoint3& a_pos)
		{
			auto* tes = RE::TES::GetSingleton();
			float land = 0.0f;
			return tes && tes->GetLandHeight(a_pos, land) && std::isfinite(land) && land <= vehiclePos_.z - kSkimEyeHeight + 24.0f;
		}

		// Riding the ground down a waterfall slope. Follows the surface under the
		// vehicle (highest of terrain and a downward ray, so rock cascades count),
		// gains speed downhill, flies off if the ground falls away too steeply, and
		// splashes into the pool at the bottom.
		void RampStep(float step, const RE::NiPoint3& forward)
		{
			if (descent_) { DescentStep(step, forward); return; }  // 44.2
			auto* tes = RE::TES::GetSingleton();
			RE::NiPoint3 next = vehiclePos_ + forward * (skimSpeed_ * step);
			// 44.1.5: on the slide the current is extra downhill speed (a floor), not a position term.
			if (flowOn_ && flowW_ > 0.05f) {
				const float fv = flowVel_.x * forward.x + flowVel_.y * forward.y;
				if (fv > skimSpeed_) {
					skimSpeed_ += (fv - skimSpeed_) * (std::min)(1.0f, 4.0f * step);
					next = vehiclePos_ + forward * (skimSpeed_ * step);
				}
			}
			const float hx = skimSpeed_ * step;
			// 36.1 (v1.0.33): the ride lift is taken off again here, so the detach and water tests see the real ground height
			// (in v1.0.32 the 40 lift tripped the detach test on every step: slope/fly chatter all the way down a fall).
			const float rideLift = fallLiftOn_ ? (std::max)(kRampRideLift, fallLift_) : 0.0f;
			const float curGround = vehiclePos_.z - kSkimEyeHeight - rideLift;

			auto groundAt = [&](const RE::NiPoint3& a_at) {
				float g = -1.0e9f;
				float land = 0.0f;
				if (tes && tes->GetLandHeight(a_at, land) && std::isfinite(land)) g = land;
				float frac = 1.0f;
				const RE::NiPoint3 from{ a_at.x, a_at.y, vehiclePos_.z + 80.0f };
				const RE::NiPoint3 to{ a_at.x, a_at.y, vehiclePos_.z - 500.0f };
				if (SkimSweep(from, to, frac) && frac > 0.01f) {
					const float hitZ = from.z + (to.z - from.z) * frac;
					if (hitZ > g) g = hitZ;
				}
				return g;
			};
			float groundZ = groundAt(next);
			// 35.1.3: a rise ahead (the swell) is met early; on the way down the max is the ground under us, so it still sticks.
			if (fallLiftOn_ && groundZ > -1.0e8f) {
				for (float ahead : { 70.0f, 140.0f }) groundZ = (std::max)(groundZ, groundAt(next + forward * ahead));
			}

			// 37.5: what the ramp rides over, twice a second.
			if (rampPoolLogCooldown_ <= 0.0f) {
				rampPoolLogCooldown_ = 0.5f;
				float wHere = 0.0f;
				const bool wetHere = SkimWaterAt(vehiclePos_, wHere);
				logger::info("Ramp: z {:.1f} ground {:.1f} water {} (under={})", vehiclePos_.z, groundZ,
					wetHere ? std::format("{:.1f}", wHere) : std::string("none"), wetHere && wHere > vehiclePos_.z - kSkimEyeHeight ? 1 : 0);
			}

			// Reached water below us: splash and carry on skimming.
			float water = 0.0f;
			const bool wetNext = SkimWetAt(next, water);  // 41B.3: pool re-entry also under a remembered surface
			if (wetNext && water <= curGround + 40.0f) {
				const float impact = (std::max)(0.0f, -skimRampVz_);
				const float hardness = std::clamp(impact / 600.0f, 0.0f, 1.0f);
				skimOnRamp_ = false;
				skimAirborne_ = false;
				skimOnLand_ = false;
				next.z = water + kSkimEyeHeight + fallLift_;
				skimWaterHeight_ = water;
				skimLastWaterPos_ = next;
				skimDip_ = 0.0f;
				skimDipVel_ = -std::clamp(impact * 0.5f, 60.0f, 450.0f);
				skimRecoverSpeed_ = skimSpeed_;
				skimSpeed_ *= 1.0f - kSkimDipSpeedLoss * (std::max)(0.25f, hardness);
				skimRecoverTime_ = kSkimRecoverTime;
				skimLaunchCooldown_ = 0.8f;
				skimFallExit_ = kFallExitGrace;
				BoatBow::Splash((std::max)(0.2f, hardness));
				if (hardness >= 0.5f) CrewBump("hard landing at the foot of a slope");
				vehiclePos_ = next;
				logger::info("Skim: slope ended in water, speed={:.1f}", skimSpeed_);
				NoteBoatLanding();  // Part 33: re-seat the crew
				return;
			}
			// 37.1 (v1.0.34): a pool on the way down: the ground ahead is under a real water surface ABOVE the boat. Splash in
			// and rise to the surface (eased, poolRise_) instead of riding the pool floor under the water. 37.4: a surface
			// more than 320 above the boat (a cell-wide plane over a lower area) is ignored as before.
			if (wetNext && water > curGround + 40.0f && groundZ < water - 15.0f && water < vehiclePos_.z + 320.0f) {
				const float impact = (std::max)(0.0f, -skimRampVz_);
				const float hardness = std::clamp(impact / 600.0f, 0.0f, 1.0f);
				logger::info("Ramp: pool at {:.1f} over ground {:.1f}, boat z {:.1f}, surfacing", water, groundZ, vehiclePos_.z);
				++hPools_;
				skimOnRamp_ = false;
				skimAirborne_ = false;
				skimOnLand_ = false;
				skimWaterHeight_ = water;
				skimDip_ = 0.0f;
				skimDipVel_ = 0.0f;
				StartPoolRise(water, vehiclePos_.z);
				next.z = water + kSkimEyeHeight + fallLift_ + poolRise_;
				skimLastWaterPos_ = next;
				skimRecoverSpeed_ = skimSpeed_;
				skimSpeed_ *= 1.0f - kSkimDipSpeedLoss * (std::max)(0.25f, hardness);
				skimRecoverTime_ = kSkimRecoverTime;
				skimLaunchCooldown_ = 0.8f;
				skimFallExit_ = kFallExitGrace;
				BoatBow::Splash((std::max)(0.2f, hardness));
				if (hardness >= 0.5f) CrewBump("hard landing in a pool on a slope");
				vehiclePos_ = next;
				NoteBoatLanding();  // Part 33: re-seat the crew
				return;
			}

			if (groundZ < -1.0e8f) {
				// Nothing to ride (unloaded?): bleed speed and hold.
				skimSpeed_ *= std::exp(-kSkimUnloadedDrag * step);
				return;
			}

			// Ground drops away steeper than we can follow: leave it and fly.
			const float drop = curGround - groundZ;
			const float detachLimit = (std::max)(6.0f, kRampDetachSlope * hx);
			if (rampLogCooldown_ <= 0.0f) {
				rampLogCooldown_ = 1.0f;
				logger::info("Ramp: ride lift {:.0f}, drop {:.1f}, detach limit {:.1f}", rideLift, drop, detachLimit);
			}
			// 36.1.2: just landed on the slope (or just entered it): no detach for a moment unless the ground really falls away.
			// 36.1.4: chatter (more than 3 detaches in 1 s) holds the ramp for 1 s outright.
			const bool held = rampChatterHold_ > 0.0f || (skimRampDetachHold_ > 0.0f && drop <= 60.0f);
			if (drop <= detachLimit) rampDetachTimer_ = 0.0f;
			// 41D.1: leave the slope only after the ground has been away for 0.12 s, never on a single step.
			if (drop > detachLimit && !held && (!cascadeOn_ || (rampDetachTimer_ += step) >= 0.12f)) {
				rampDetachTimer_ = 0.0f;
				rampDetachTimes_[rampDetachNext_] = rideTime_;
				rampDetachNext_ = (rampDetachNext_ + 1) % 4;
				if (rideTime_ - rampDetachTimes_[rampDetachNext_] < 1.0f) {  // the oldest of the last 4 detaches is under 1 s old
					rampChatterHold_ = 1.0f;
					rampDetachTimes_.fill(-10.0f);
					logger::info("Ramp: chatter, detach disabled for 1 s");
				}
				skimOnRamp_ = false;
				skimAirborne_ = true;
				skimDropGrace_ = 0.0f;
				skimVerticalSpeed_ = std::clamp(skimRampVz_, -900.0f, 0.0f);
				skimRampTime_ = 4.0f;
				logger::info("Skim: left the slope, flying (speed={:.1f} vz={:.1f})", skimSpeed_, skimVerticalSpeed_);
				return;
			}

			// Follow the surface: stick on the way down, ease up on the way up.
			const float wantZ = groundZ + kSkimEyeHeight + (fallLiftOn_ ? kRampRideLift : 0.0f);
			float newZ = wantZ < vehiclePos_.z ? wantZ : vehiclePos_.z + (wantZ - vehiclePos_.z) * (std::min)(1.0f, 25.0f * step);
			const float dz = newZ - vehiclePos_.z;
			skimRampVz_ += (dz / step - skimRampVz_) * (std::min)(1.0f, 10.0f * step);
			if (hx > 0.01f) {
				const float slope = dz / hx;
				skimRampPitch_ += (std::atan2(dz, hx) - skimRampPitch_) * (std::min)(1.0f, 12.0f * step);
				if (slope < 0.0f) skimSpeed_ += (std::min)(-slope, 1.5f) * kRampGravity * step;
				else skimSpeed_ -= (std::min)(slope, 2.0f) * kRampDrag * step;
				skimSpeed_ = std::clamp(skimSpeed_, 0.0f, (std::max)(P().maxSpeed, kFallMaxSpeed));
			} else {
				skimRampPitch_ += (0.0f - skimRampPitch_) * (std::min)(1.0f, 12.0f * step);
			}
			next.z = newZ;

			// Out of waterfall range for a while: settle into the normal land crawl.
			if (skimRampTime_ <= 0.0f) {
				skimOnRamp_ = false;
				float wEnd = 0.0f;
				if (SkimWetAt(next, wEnd) && wEnd < next.z + 320.0f) {
					// 37.3: the slope ran out in water (a pool): float, do not crawl the pool floor as dry land.
					skimOnLand_ = false;
					skimWaterHeight_ = wEnd;
					StartPoolRise(wEnd, next.z);
					next.z = wEnd + kSkimEyeHeight + skimDip_ + fallLift_ + poolRise_;
					skimLastWaterPos_ = next;
					logger::info("Skim: slope finished in water at {:.1f}", wEnd);
				} else if (float uwEnd = 0.0f; descentOn_ && !UnderWaterSurface(next, uwEnd) && (GroundSlope(next) > 0.0524f || flowW_ > 0.05f || FlowOverride())) {
					// 44.2.1a: still sloping (or in a stream): the ride goes on as a descent slide, never a stop midway.
					EnterDescent('a', next);
				} else {
					skimOnLand_ = true;
					skimLastWaterPos_ = next;
					logger::info("Skim: slope finished on dry ground");
				}
			}
			vehiclePos_ = next;
		}

		// Shared skim physics: steering + speed + water/rock/waterfall handling.
		// throttle/strafe are only used by sign (W/S and D/A). Moves vehiclePos_ (the vehicle).
		void SkimStep(float dt, float throttle, float strafe)
		{
			// Boat steering (A/D), independent of where the camera is looking.
			const float steer = strafe > 0.01f ? 1.0f : (strafe < -0.01f ? -1.0f : 0.0f);
			if (!skimOnRamp_) descent_ = false;  // 44.2: the descent is a kind of slide
			if (throttle != 0.0f || steer != 0.0f) lastInputAt_ = rideTime_;  // 45.2: rowing or steering beats the weathervane
			UpdateFlow(dt, steer);  // 41C: the current, weathervane and the throw
			if (FlowOverride()) skimReverse_ = 0.0f;  // 44.1.1
			// 56.36: a light boat (the player the only human aboard; dogs don't count) rows a little faster.
			// Two or more humans keep the normal row speed.
			SkimProfile prof = P();
			if (rowMode_) {
				bool light = true;
				for (auto& hp : passengers_) { auto a = hp.get(); if (a && !IsDogActor(a.get())) { light = false; break; } }
				if (light) {
					// 56.38: and it glides further between strokes (less drag), with the push per stroke matched so the
					// average still reaches the faster top speed: speed dips about 18% between digs instead of about 26%.
					prof.maxSpeed *= kLightBoatFactor;
					prof.coastDrag *= kLightBoatDrag;
					prof.accel = prof.maxSpeed * kRowAccelRate;
				}
				if (light != lightBoatLogged_) {
					lightBoatLogged_ = light;
					logger::info("Row: {} (top speed {:.0f})", light ? "light boat, only the player aboard (dogs don't count): faster" : "crew aboard: normal speed", prof.maxSpeed);
				}
			}
			const float speed01Now = std::clamp(skimSpeed_ / prof.maxSpeed, 0.0f, 1.0f);
			const float authority = prof.turnFloor + (1.0f - prof.turnFloor) * std::clamp(skimSpeed_ / prof.turnAuthority, 0.0f, 1.0f);
			const float maxTurn = (prof.turnLow + (prof.turnHigh - prof.turnLow) * speed01Now) * authority * (flowRapids_ && flowW_ > 0.05f ? 0.6f : 1.0f);
			// Rowing: the oars are driven by the throttle. Motor: they follow the boat's speed (see BoatBow).
			// 35.3 (v1.0.32): the oars wait for the row clip (accepted, plus the clip's lead-in), with a 2.5 s safety release,
			// and keep going through the stop debounce so they stop with the clip.
			const bool clipLive = rowAnimOn_ && rideTime_ - rowAnimOnAt_ >= kRowClipLeadIn;
			const bool safetyRelease = rowStartPending_ && rowPendingFor_ > 2.5f;
			const bool oarsMayRow = !rowerAnimOn_ || clipLive || safetyRelease;
			const bool oarsDrive = rowMode_ && ((throttle > 0.01f && oarsMayRow) || (throttle <= 0.01f && rowAnimOn_ && rowAnimOffTimer_ < kRowStopDebounce));
			if (rowMode_ && throttle > 0.01f && oarsMayRow && rowerAnimOn_ && !rowSyncLogged_) {
				rowSyncLogged_ = true;
				if (clipLive) logger::info("Row sync: oars released with the clip after {:.0f} ms (pending {:.2f} s)", (rideTime_ - rowRequestAt_) * 1000.0f, rowPendingFor_);
				else logger::info("Row sync: safety release, clip never started");
			}
			BoatBow::SetStrokeDrive(rowMode_, oarsDrive, kRowStrokesBase + kRowStrokesGain * speed01Now);
			rowWantAnim_ = rowMode_ && throttle > 0.01f;
			skimYawRate_ += (steer * maxTurn - skimYawRate_) * (1.0f - std::exp(-kSkimTurnResponse * dt));
			skimHeading_ = WrapAngle(skimHeading_ + skimYawRate_ * dt);
			// Hard turns at speed scrub speed, with or without throttle.
			if (!skimAirborne_ && !skimOnLand_ && skimSpeed_ > 0.0f) {
				const float load = std::clamp(std::fabs(skimYawRate_) / prof.turnHigh, 0.0f, 1.0f);
				skimSpeed_ *= std::exp(-kSkimTurnDrag * load * load * speed01Now * dt);
			}
			const RE::NiPoint3 forward{ std::sin(skimHeading_), std::cos(skimHeading_), 0.0f };

			skimWaterfallLogCooldown_ = (std::max)(0.0f, skimWaterfallLogCooldown_ - dt);
			rockHitLogCooldown_ = (std::max)(0.0f, rockHitLogCooldown_ - dt);
			skimStopLogCooldown_ = (std::max)(0.0f, skimStopLogCooldown_ - dt);
			skimLaunchCooldown_ = (std::max)(0.0f, skimLaunchCooldown_ - dt);
			skimRampTime_ = (std::max)(0.0f, skimRampTime_ - dt);
			skimFallExit_ = (std::max)(0.0f, skimFallExit_ - dt);
			skimRampDetachHold_ = (std::max)(0.0f, skimRampDetachHold_ - dt);
			rampChatterHold_ = (std::max)(0.0f, rampChatterHold_ - dt);
			rampLogCooldown_ = (std::max)(0.0f, rampLogCooldown_ - dt);
			rampPoolLogCooldown_ = (std::max)(0.0f, rampPoolLogCooldown_ - dt);
			// v1.0.13: say what carries the boat past the top speed (the rowing stroke no longer can; a fall's current still does).
			overCapLogCooldown_ = (std::max)(0.0f, overCapLogCooldown_ - dt);
			if (skimSpeed_ > prof.maxSpeed + 5.0f && !skimAirborne_ && overCapLogCooldown_ <= 0.0f) {
				overCapLogCooldown_ = 2.0f;
				logger::info("Speed over the cap: {:.0f} > {:.0f} (fall current ahead={} slope={} fall exit={:.1f} s landing recovery={:.1f} s to {:.0f} on land={})",
					skimSpeed_, prof.maxSpeed, skimFlowAhead_, skimOnRamp_, skimFallExit_, skimRecoverTime_, skimRecoverSpeed_, skimOnLand_);
			}
			skimFlowTimer_ -= dt;
			if (skimFlowTimer_ <= 0.0f) {
				skimFlowTimer_ = 0.08f;
				skimFlowAhead_ = !skimAirborne_ && !skimOnLand_ && !skimOnRamp_ && SkimFlowAhead(vehiclePos_, forward);
			}
			if (skimOnRamp_ && SkimWaterfallNear(vehiclePos_)) skimRampTime_ = 4.0f;
			UpdateFallLift(dt);
			if (!helmSweepOn_ && !helmSweepLogged_) {
				helmSweepLogged_ = true;
				logger::info("Skim: helm sweep off (temporary)");
			}
			swellLogCooldown_ = (std::max)(0.0f, swellLogCooldown_ - dt);
			const int steps = (std::max)(1, static_cast<int>(std::ceil(dt / 0.008f)));
			const float step = dt / static_cast<float>(steps);
			for (int i = 0; i < steps && step > 0.0f; ++i) {
				const float maxSpeed = skimOnLand_ ? kSkimLandMaxSpeed : prof.maxSpeed;
				const bool rowDrive = rowMode_ && !skimOnLand_ && !skimAirborne_ && !skimOnRamp_;
				if ((skimOnLand_ && !contactOn_) || skimAirborne_ || skimOnRamp_) skimReverse_ = 0.0f;
				if (throttle > 0.01f && skimReverse_ > 0.0f) {
					skimReverse_ = (std::max)(0.0f, skimReverse_ - prof.brake * 0.6f * step);  // brake out of reverse first
				}
				else if (throttle > 0.01f) {
					if (rowDrive) {
						// Speed is gained only while the blades pull through the water; between strokes the boat glides.
						// v1.0.13: a stroke never pushes past the top speed (the strokes overshot it to 363..435 before,
						// since only the slow ease below held it back). Faster than the top speed (a fall) the oars add nothing.
						// 43.2.2: same average thrust, the per-stroke pulse scaled by StrokeSurge (0.5: half the surge).
						const float rawThrust = BoatBow::StrokeThrust();
						const float thrust = oarsDrive ? (1.0f - BoatIni().strokeSurge) + BoatIni().strokeSurge * rawThrust : rawThrust;
						if (skimSpeed_ < maxSpeed) skimSpeed_ = (std::min)(maxSpeed, skimSpeed_ + prof.accel * thrust * step);
						skimSpeed_ *= std::exp(-prof.coastDrag * (onIce_ ? 0.5f : 1.0f) * step);
						if (skimSpeed_ > maxSpeed) skimSpeed_ += (maxSpeed - skimSpeed_) * (1.0f - std::exp(-1.2f * step));  // fall speed eases back down
					} else if (skimSpeed_ < maxSpeed) {
						skimSpeed_ = (std::min)(maxSpeed, skimSpeed_ + prof.accel * (rowMode_ ? 1.0f : (1.0f - 0.55f * std::clamp(skimSpeed_ / prof.maxSpeed, 0.0f, 1.0f))) * step);
					}
				}
				else if (throttle < -0.01f) {
					skimRecoverTime_ = 0.0f;
					if (skimSpeed_ > 8.0f || (skimOnLand_ && !contactOn_) || skimAirborne_ || skimOnRamp_) {
						skimSpeed_ = (std::max)(0.0f, skimSpeed_ - prof.brake * (0.4f + 0.6f * std::clamp(skimSpeed_ / prof.maxSpeed, 0.0f, 1.0f)) * step);
					} else {
						// Stopped and still holding S: back up slowly.
						skimSpeed_ = 0.0f;
						skimReverse_ = (std::min)(prof.reverseMax, skimReverse_ + prof.reverseAccel * step);
					}
				}
				else {
					skimSpeed_ *= std::exp(-prof.coastDrag * (onIce_ ? 0.5f : 1.0f) * step);  // 41E.4: ice glides
					skimReverse_ *= std::exp(-0.9f * step);
					if (skimReverse_ < 1.0f) skimReverse_ = 0.0f;
				}
				// The current: with a fall ahead the flow sweeps the boat toward the lip, rowing or not.
				if (flowOn_ && skimFlowAhead_ && !skimOnLand_ && !skimAirborne_ && !skimOnRamp_ && skimSpeed_ < kFallLaunchSpeed && flowW_ <= 0.05f) {  // 45.3: flow off = no fall launch
					skimSpeed_ = (std::min)(kFallLaunchSpeed, skimSpeed_ + kFallFlowAccel * step);
				}
				// Dry land has real friction: with no throttle the boat slides to rest.
				if (skimOnLand_ && throttle <= 0.01f) skimSpeed_ = (std::max)(0.0f, skimSpeed_ - (skimSpeed_ > kSkimLandMaxSpeed ? kSkimSlideDecel : kSkimLandDecel) * step);

				// After a water landing, ease back up to the speed we had before it.
				if (skimRecoverTime_ > 0.0f && !skimAirborne_ && !skimOnLand_ && throttle >= -0.01f) {
					skimSpeed_ = (std::min)(skimRecoverSpeed_, skimSpeed_ + kSkimRecoverAccel * step);
					skimRecoverTime_ = (std::max)(0.0f, skimRecoverTime_ - step);
				}

				// Buoyancy spring: skimDip_ is the camera's offset from normal eye height.
				skimDipVel_ += (-kSkimDipStiffness * skimDip_ - kSkimDipDamping * skimDipVel_) * step;
				skimDip_ = std::clamp(skimDip_ + skimDipVel_ * step, -kSkimDipMax, 12.0f);
				poolRise_ *= std::exp(-12.0f * step);  // 37: about 0.3 s up to the surface
				if (poolRise_ > -0.5f) poolRise_ = 0.0f;

				if (skimOnRamp_ && !skimAirborne_) {
					RampStep(step, forward);
					continue;
				}

				if (skimReverse_ > 0.0f) {
					// Backing up on open water: same water test as forward, plus a short probe astern.
					// Beached (contact on): backing over the sand is allowed too, until the boat is afloat again.
					RE::NiPoint3 back = vehiclePos_ - forward * (skimReverse_ * step);
					back.x += flowVel_.x * step;
					back.y += flowVel_.y * step;
					float w = skimWaterHeight_;
					int whyR = 0;
					const bool wetBack = SkimWetAt(back, w, &whyR);
					bool blocked = !wetBack && !(skimOnLand_ && contactOn_ && whyR == 3);
					if (!blocked) {
						float fr = 1.0f;
						const float probeZ = skimOnLand_ ? vehiclePos_.z - kSkimEyeHeight + 12.0f : skimWaterHeight_ + 12.0f;
						const RE::NiPoint3 from{ vehiclePos_.x, vehiclePos_.y, probeZ };
						blocked = SkimSweep(from, from - forward * (skimReverse_ * step + 80.0f), fr);
					}
					if (blocked) {
						skimReverse_ = 0.0f;
					} else if (wetBack) {
						if (skimOnLand_) LogContact("afloat again (reversed off the shore)");
						skimOnLand_ = false;
						back.z = w + kSkimEyeHeight + skimDip_;
						skimWaterHeight_ = w;
						skimLastWaterPos_ = back;
						vehiclePos_ = back;
					} else {
						float land = 0.0f;
						auto* tesBack = RE::TES::GetSingleton();
						const float targetZ = (tesBack && tesBack->GetLandHeight(back, land) && std::isfinite(land)) ? land + kSkimEyeHeight : vehiclePos_.z;
						back.z = vehiclePos_.z + (targetZ - vehiclePos_.z) * (std::min)(1.0f, 8.0f * step);
						vehiclePos_ = back;
					}
					continue;
				}
				RE::NiPoint3 next = vehiclePos_ + forward * (skimSpeed_ * step);
				next.x += flowVel_.x * step;  // 41C: the current carries the boat, rowing or not
				next.y += flowVel_.y * step;
				float water = skimWaterHeight_;
				int why = 0;
				const bool wet = SkimWetAt(next, water, &why);  // 41B: a known pool counts as water
				float lowerWater = 0.0f;
				const bool dropAhead = SkimDropAhead(vehiclePos_, forward, skimSpeed_, lowerWater);
				if (!skimAirborne_) {
					// Three short hull-height probes catch narrow exposed rocks.
					float fraction = 1.0f;
					bool hit = false;
					RE::COL_LAYER hitLayer = RE::COL_LAYER::kUnidentified;
					float cliffLow = 0.0f;
					// 35.1.1: with a fall near, a static/prop hit on water is the lip's swell: never stop or pin on it.
					const bool fallNear = skimFlowAhead_ || contactSuspended_ || skimRampTime_ > 0.0f || skimFallExit_ > 0.0f;
					// 35.2: the helm sweep is off for now (temporary), except on land so a beached boat still stops at walls.
					const bool sweep = helmSweepOn_ || skimOnLand_;
					for (float offset : { -12.0f, 0.0f, 12.0f }) {
						if (!sweep) break;
						const float probeZ = skimOnLand_ ? vehiclePos_.z - kSkimEyeHeight + 12.0f : skimWaterHeight_ + 12.0f;
						RE::NiPoint3 from{ vehiclePos_.x + forward.y * offset, vehiclePos_.y - forward.x * offset, probeZ };
						const auto to = from + forward * (skimSpeed_ * step + 20.0f);
						RE::COL_LAYER layer = RE::COL_LAYER::kUnidentified;
						float frac = 1.0f;
						if (!SkimSweep(from, to, frac, &layer)) continue;
						if (wet && fallNear && (layer == RE::COL_LAYER::kStatic || layer == RE::COL_LAYER::kAnimStatic || layer == RE::COL_LAYER::kProps)) {
							if (swellLogCooldown_ <= 0.0f) {
								swellLogCooldown_ = 1.0f;
								logger::info("Skim: swell hit ignored (layer {}, fall near)", static_cast<int>(layer));
							}
							continue;
						}
						fraction = frac;
						hitLayer = layer;
						hit = true;
						break;
					}
					// Just splashed down from a fall, or pinned too long: the pool is full of fall meshes and rocks.
					// Ignore sweep hits on water so the boat can never get wedged (the old flicker loop at the base of a fall).
					if (hit && wet && !dropAhead && skimFallExit_ > 0.0f) hit = false;
					// Terrain banks are stops, not launch ramps. Static collision
					// meshes include rocks but also docks/other solid props.
					const bool launchable = hitLayer == RE::COL_LAYER::kStatic || hitLayer == RE::COL_LAYER::kAnimStatic || hitLayer == RE::COL_LAYER::kClutter || hitLayer == RE::COL_LAYER::kProps;
					// A gentle dirt bank at speed is something to slide up, not a wall.
					bool bankGlide = false;
					if (hit && !launchable && (hitLayer == RE::COL_LAYER::kTerrain || hitLayer == RE::COL_LAYER::kGround) && skimSpeed_ > 120.0f) {
						auto* tesBank = RE::TES::GetSingleton();
						const RE::NiPoint3 ahead = vehiclePos_ + forward * 120.0f;
						float landAhead = 0.0f;
						if (tesBank && tesBank->GetLandHeight(ahead, landAhead) && std::isfinite(landAhead) && !SkimWaterfallNear(next)) {
							const float groundNow = skimOnLand_ ? vehiclePos_.z - kSkimEyeHeight : skimWaterHeight_;
							bankGlide = landAhead - groundNow < 70.0f;  // about 30 degrees or gentler
						}
					}
					if ((wet && skimWaterHeight_ - water >= kSkimDropThreshold) || ((hit || !wet) && dropAhead)) {
						// A lower, disconnected water plane is a waterfall lip, not
						// a shore: become airborne with a small hop and keep speed.
						skimAirborne_ = true;
						skimDropGrace_ = kSkimDropGrace;
						skimVerticalSpeed_ = 80.0f + 0.12f * (std::max)(skimSpeed_, kFallLaunchSpeed);
						skimSpeed_ = (std::max)(skimSpeed_, kFallLaunchSpeed);  // the current shoots the boat off the lip
						skimWaterHeight_ = dropAhead ? lowerWater : water;
						next = vehiclePos_;
						BoatBow::Impact(0.3f);
						CrewBump("waterfall drop");
							skimRampTime_ = 4.0f;
						logger::info("Skim waterfall drop: speed={:.1f} lowerWater={:.1f}", skimSpeed_, skimWaterHeight_);
						FlowThrow();  // 41C.5
					} else if (hit || !wet) {
						// 31.2 (v1.0.30): the speedboat's rock launch is gone. A rock, dock or bank hit falls through to the
						// blocked branch below (hold position, speed bleeds off): a bump, never a lift.
						if (TryDescentOrFlow(next, wet, why, step)) {
							// 44.1 / 44.2: a stream pulls the boat over the ground, or a rocky descent: a slide, not a stop.
							next = vehiclePos_;
						} else if (SkimWaterfallNear(next)) {
							// Waterfall lip/face: ride the ground down the slope instead of
							// hovering over empty space (big vertical drops detach from it
							// and fly; see RampStep).
							skimOnRamp_ = true;
							skimOnLand_ = false;
							skimRampTime_ = 4.0f;
							skimRampPitch_ = 0.0f;
							skimRampVz_ = 0.0f;
							skimRampDetachHold_ = 0.35f;  // 36.1.3: entered from the water (up to the approach lift high): no detach yet
							next = vehiclePos_;
							if (skimWaterfallLogCooldown_ <= 0.0f) {
								skimWaterfallLogCooldown_ = 2.0f;
								logger::info("Skim: waterfall ahead, riding the slope (speed={:.1f})", skimSpeed_);
							}
						} else if (!hit && !wet && (why == 3 || why == 2) && !skimOnLand_ && SkimCliffAhead(vehiclePos_, forward, skimSpeed_, cliffLow)) {
							// Ran out of water at a ledge with the ground dropping away:
							// a waterfall. Launch off it and keep our speed.
							skimAirborne_ = true;
							skimDropGrace_ = kSkimDropGrace;
							skimVerticalSpeed_ = 80.0f + 0.12f * (std::max)(skimSpeed_, kFallLaunchSpeed);
							skimSpeed_ = (std::max)(skimSpeed_, kFallLaunchSpeed);
							skimWaterHeight_ = cliffLow;
							next = vehiclePos_;
							BoatBow::Impact(0.3f);
							CrewBump("cliff drop");
							skimRampTime_ = 4.0f;
							logger::info("Skim cliff drop: speed={:.1f} groundBelow={:.1f}", skimSpeed_, cliffLow);
							FlowThrow();  // 41C.5
						} else if (!hit && !wet && why == 2 && !skimOnLand_ && SkimGroundRideable(next)) {
							// Ran out of water height at a lip with walkable ground ahead
							// (this used to stall the boat): ride the ground down instead.
							skimOnRamp_ = true;
							skimOnLand_ = false;
							skimRampTime_ = 4.0f;
							skimRampPitch_ = 0.0f;
							skimRampVz_ = 0.0f;
							skimRampDetachHold_ = 0.35f;  // 36.1.3: entered from the water (up to the approach lift high): no detach yet
							next = vehiclePos_;
							SkimLogSlow("lip-ramp", why, hit, hitLayer, next);
						} else if ((!hit && !wet && why == 3) || bankGlide) {
							// Shore: glide up the beach, bleeding speed, instead of stopping dead.
							skimOnLand_ = true;
							if (skimSpeed_ > kSkimLandMaxSpeed) skimSpeed_ = (std::max)(kSkimLandMaxSpeed, skimSpeed_ - kSkimSlideDecel * step);
							next = vehiclePos_ + forward * (skimSpeed_ * step);
							const float bx = next.x - skimLastWaterPos_.x;
							const float by = next.y - skimLastWaterPos_.y;
							if (std::sqrt(bx * bx + by * by) > kSkimBeachMaxDistance) {
								skimSpeed_ *= std::exp(-kSkimBlockedDrag * step);
								next = vehiclePos_;
							} else {
								float land = 0.0f;
								auto* tesLand = RE::TES::GetSingleton();
								const float targetZ = (tesLand && tesLand->GetLandHeight(next, land) && std::isfinite(land)) ? land + kSkimEyeHeight : vehiclePos_.z;
								next.z = vehiclePos_.z + (targetZ - vehiclePos_.z) * (std::min)(1.0f, 8.0f * step);
							}
							SkimLogSlow("beach", why, hit, hitLayer, next);
						} else {
							// Obstacle or unloaded/invalid water: hold position but let speed
							// bleed off smoothly (and recover) instead of snapping to zero.
							if (hit && launchable && !skimOnLand_ && skimSpeed_ >= kSkimMinLaunchSpeed && rockHitLogCooldown_ <= 0.0f) {
								rockHitLogCooldown_ = 1.0f;
								CrewBump("rock hit");
								logger::info("Contact: rock hit at speed {:.1f}, no launch (layer {})", skimSpeed_, static_cast<int>(hitLayer));
							}
							skimSpeed_ *= std::exp(-((why == 1 || why == 2) && !hit ? kSkimUnloadedDrag : kSkimBlockedDrag) * step);
							next = vehiclePos_;
							lastBlockedAt_ = rideTime_;  // 44.1.4
							if (hit && wet && throttle > 0.01f) {
								skimBlockedTime_ += step;
								if (skimBlockedTime_ > kUnstickTime) {
									skimBlockedTime_ = 0.0f;
									skimFallExit_ = kFallExitGrace;
									logger::info("Skim: pinned for {:.1f}s, ghosting through to get free", kUnstickTime);
								}
							}
							SkimLogSlow("blocked", why, hit, hitLayer, next);
						}
					} else {
						// 37.2: a surface well above the boat (it was under water): rise to it eased, not in one frame.
						if (vehiclePos_.z < water + kSkimEyeHeight + skimDip_ + fallLift_ + poolRise_ - 20.0f) StartPoolRise(water, vehiclePos_.z);
						stuckTime_ = 0.0f;
						next.z = water + kSkimEyeHeight + skimDip_ + fallLift_ + poolRise_;
						skimOnLand_ = false;
						skimWaterHeight_ = water;
						skimLastWaterPos_ = next;
						skimBlockedTime_ = 0.0f;
					}
				}
				if (skimAirborne_) {
					// Do not enter unloaded space while airborne either.
					auto* tes = RE::TES::GetSingleton();
					auto* cell = tes ? tes->GetCell(next) : nullptr;
					if (!cell || !cell->IsAttached()) { next = vehiclePos_; skimSpeed_ = 0.0f; }
					const float oldVz = skimVerticalSpeed_;
					skimVerticalSpeed_ -= kSkimGravity * step;
					next.z = vehiclePos_.z + (oldVz + skimVerticalSpeed_) * 0.5f * step;
					float fraction = 1.0f;
					skimDropGrace_ = (std::max)(0.0f, skimDropGrace_ - step);
					// The lip itself can occupy the first moments of a waterfall
					// path. Give that confirmed drop a brief collision grace.
					RE::COL_LAYER landLayer = RE::COL_LAYER::kUnidentified;
					if (skimDropGrace_ <= 0.0f && SkimSweep(vehiclePos_, next, fraction, &landLayer)) {
						if (skimVerticalSpeed_ < 0.0f) {
							if (!wet && skimRampTime_ > 0.0f) {
								// Came down a waterfall and touched ground under/after it: keep
								// our speed and ride the slope on down instead of resetting.
								next = vehiclePos_ + (next - vehiclePos_) * (std::max)(0.0f, fraction - 0.02f);
								if (!cascadeOn_) next.z += kSkimEyeHeight;  // 41D.2: no jump; the slope eases up to the ride height
								skimAirborne_ = false;
								skimOnLand_ = false;
								skimOnRamp_ = true;
								skimRampTime_ = 4.0f;
								skimRampPitch_ = 0.0f;
								skimRampVz_ = skimVerticalSpeed_;
								skimRampDetachHold_ = 0.35f;  // 36.1.2
								const float touchVz = -skimVerticalSpeed_;
								skimVerticalSpeed_ = 0.0f;
								// 41D.1: a touch is an event (bump, kick, re-seat) only when hard and not within 0.8 s of the last one.
								if (!cascadeOn_ || (touchVz >= 350.0f && rideTime_ - lastFallEventAt_ > 0.8f)) {
									lastFallEventAt_ = rideTime_;
									++hCascadeEvents_;
									BoatBow::Impact(0.4f);
									CrewBump("landing on a waterfall slope");
									logger::info("Skim: landed on a waterfall slope, riding it (speed={:.1f} vz={:.0f})", skimSpeed_, touchVz);
									NoteBoatLanding();  // Part 33: re-seat the crew
								} else {
									++hCascadeTouches_;
									++softContacts_;
								}
							} else if (!wet && (landLayer == RE::COL_LAYER::kTerrain || landLayer == RE::COL_LAYER::kGround) && !SkimWaterfallNear(next)) {
								// Touched down on open ground: slide to a stop on it
								// rather than snapping back to the water.
								next = vehiclePos_ + (next - vehiclePos_) * (std::max)(0.0f, fraction - 0.02f);
								next.z += kSkimEyeHeight;  // camera sits at eye height above the ground it touched
								skimAirborne_ = false;
								skimOnLand_ = true;
								skimVerticalSpeed_ = 0.0f;
								skimSpeed_ *= 0.6f;
								BoatBow::Impact(std::clamp(0.4f + 0.6f * (skimSpeed_ / P().maxSpeed), 0.0f, 1.0f));
								CrewBump("beach landing");
								logger::info("Skim: beach landing, speed={:.1f}", skimSpeed_);
							} else if (wet || SkimWaterfallNear(next)) {
								// Falling beside a waterfall lip: hold XY and let the
								// descent clear the edge rather than teleporting back.
								next.x = vehiclePos_.x;
								next.y = vehiclePos_.y;
							} else {
								// Dry landing: recover to last water, never trap the
								// camera in a rock or leave it cruising on land.
								next = skimLastWaterPos_;
								skimAirborne_ = false;
								skimVerticalSpeed_ = skimSpeed_ = 0.0f;
								logger::info("Skim: solid landing, recovered to water");
							}
						} else {
							// Rise alongside the rock until clear; retain forward
							// momentum. A ceiling instead stops upward movement.
							RE::NiPoint3 vertical{ vehiclePos_.x, vehiclePos_.y, next.z };
							float verticalFraction = 1.0f;
							if (!SkimSweep(vehiclePos_, vertical, verticalFraction)) next = vertical;
							else {
								next = vehiclePos_ + (vertical - vehiclePos_) * (std::max)(0.0f, verticalFraction - 0.05f);
								skimVerticalSpeed_ = 0.0f;
							}
						}
					}
					if (wet && skimVerticalSpeed_ <= 0.0f && next.z <= water + kSkimEyeHeight + fallLift_) {
						// Hard floor avoids underwater frames, even on a river
						// height transition. Water landing keeps forward speed.
						next.z = water + kSkimEyeHeight + fallLift_;
						skimAirborne_ = false;
						skimOnLand_ = false;
						{
							// Buoyant dip: start the spring moving down, shave some speed,
							// then ease back up to the pre-landing speed.
							const float impact = (std::max)(0.0f, -skimVerticalSpeed_);
							const float hardness = std::clamp(impact / 600.0f, 0.0f, 1.0f);
							skimDip_ = 0.0f;
							skimDipVel_ = -std::clamp(impact * 0.5f, 60.0f, 450.0f);
							skimRecoverSpeed_ = skimSpeed_;
							skimSpeed_ *= 1.0f - kSkimDipSpeedLoss * (std::max)(0.25f, hardness);
							skimRecoverTime_ = kSkimRecoverTime;
						}
						BoatBow::Splash(std::clamp(-skimVerticalSpeed_ / 600.0f, 0.1f, 1.0f));
						if (-skimVerticalSpeed_ >= 300.0f) CrewBump("hard water landing");
						skimVerticalSpeed_ = 0.0f;
						skimWaterHeight_ = water;
						skimLastWaterPos_ = next;
						skimLaunchCooldown_ = 0.8f;
						if (skimRampTime_ > 0.0f) skimFallExit_ = kFallExitGrace;
						logger::info("Skim: water landing, speed={:.1f}", skimSpeed_);
						NoteBoatLanding();  // Part 33: re-seat the crew
					}
				}
				vehiclePos_ = next;
			}
		}

		// 37 (v1.0.34): the boat is under a water surface (a pool on the way down a fall): start an eased rise to it.
		// poolRise_ is the (negative) offset from the surface height; it decays to 0 in about 0.3 s, capped at -260.
		void StartPoolRise(float a_water, float a_fromZ)
		{
			const float gap = a_fromZ - (a_water + kSkimEyeHeight + skimDip_ + fallLift_);
			poolRise_ = std::clamp(gap, -260.0f, 0.0f);
			if (gap < -20.0f) ++hPools_;
		}

		// 35.1.2 (v1.0.32): fallLift_ raises the boat over a fall's lip (kFallApproachLift while a fall is ahead) and a bit
		// through the fall (kRampRideLift on the slope or flying off it). It rises over about 0.4 s and settles about 1 s
		// after the splash. `oas falllift off` = no lift, for comparison.
		void UpdateFallLift(float a_dt)
		{
			float target = 0.0f;
			if (fallLiftOn_) {
				const float nearDist = (std::max)(600.0f, skimSpeed_ * 1.4f);
				if (skimOnRamp_ || (skimAirborne_ && skimRampTime_ > 0.0f)) target = kRampRideLift;
				else if (skimFlowAhead_ || (lookHit_ >= 0.0f && lookHit_ <= nearDist)) target = kFallApproachLift;
			}
			const float rate = target > fallLift_ ? 6.0f : 3.0f;
			fallLift_ += (target - fallLift_) * (1.0f - std::exp(-rate * a_dt));
			if (fallLift_ < 0.05f && target <= 0.0f) fallLift_ = 0.0f;
			fallLiftLogCooldown_ = (std::max)(0.0f, fallLiftLogCooldown_ - a_dt);
			if (fallLift_ > 0.0f && fallLiftLogCooldown_ <= 0.0f) {
				fallLiftLogCooldown_ = 1.0f;
				logger::info("FallLift: target {:.0f} now {:.0f} (ahead={:.0f} ramp={})", target, fallLift_, lookHit_, skimOnRamp_ || skimAirborne_ ? 1 : 0);
			}
		}

		// Part 38A (v1.0.35): 12 hull feelers, the soft collision. 38B (v1.0.36): with hullfeel on they react (bow STOP,
		// side/stern SCRAPE, keel BUMP); every effect is a velocity that fades out, vehiclePos_ is never set from a feeler.
		// Anchored to the hull outline (not the helm, see Part 35.2): bow 3 (STOP group), sides 4 and stern corners 2 (SCRAPE
		// group), keel 3 (BUMP group). Bow every frame, the rest staggered over 3 frames (about 20 Hz). Gated like the rest of
		// the contact system: off near falls, in the air, on slopes and land, after landings and in the first 3 s aboard.
		enum FeelGroup : int { kFeelBow = 0, kFeelSide = 1, kFeelStern = 2, kFeelKeel = 3 };
		struct Feeler { const char* name; FeelGroup group; float right, fwd; float dirRight, dirFwd; int side; };  // right/fwd in kFeelWidth/kFeelLen units; side -1 L, +1 R
		static constexpr int kFeelers = 12;
		static constexpr float kFeelReach = 25.0f;
		static const char* FeelLayerName(RE::COL_LAYER a_layer)
		{
			switch (a_layer) {
			case RE::COL_LAYER::kTerrain: return "terrain";
			case RE::COL_LAYER::kGround: return "ground";
			case RE::COL_LAYER::kStatic: return "static";
			case RE::COL_LAYER::kAnimStatic: return "animstatic";
			case RE::COL_LAYER::kProps: return "props";
			case RE::COL_LAYER::kClutter: return "clutter";
			default: return "other";
			}
		}
		bool FeelOwnRef(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) return false;
			if (a_ref == RE::PlayerCharacter::GetSingleton()) return true;
			if (auto* actor = a_ref->As<RE::Actor>(); actor && IsBoatParty(actor)) return true;
			const RE::FormID id = a_ref->GetFormID();
			for (const auto& t : BoatBow::TrackedRefsForSave()) if (t.first == id) return true;  // bench, oars, lamps, chest, proxies, hull
			return false;
		}
		void UpdateHullFeel(float a_dt)
		{
			static constexpr float kS = 0.70710678f;
			static constexpr Feeler kFeel[kFeelers] = {
				{ "bow tip", kFeelBow, 0.0f, 1.0f, 0.0f, 1.0f, 0 },
				{ "bow L", kFeelBow, -0.45f, 0.8f, 0.0f, 1.0f, -1 },
				{ "bow R", kFeelBow, 0.45f, 0.8f, 0.0f, 1.0f, 1 },
				{ "side L fore", kFeelSide, -1.0f, 0.35f, -1.0f, 0.0f, -1 },
				{ "side L aft", kFeelSide, -1.0f, -0.35f, -1.0f, 0.0f, -1 },
				{ "side R fore", kFeelSide, 1.0f, 0.35f, 1.0f, 0.0f, 1 },
				{ "side R aft", kFeelSide, 1.0f, -0.35f, 1.0f, 0.0f, 1 },
				{ "stern L", kFeelStern, -0.7f, -0.9f, -kS, -kS, -1 },
				{ "stern R", kFeelStern, 0.7f, -0.9f, kS, -kS, 1 },
				{ "keel aft", kFeelKeel, 0.0f, -0.5f, 0.0f, 0.0f, 0 },
				{ "keel centre", kFeelKeel, 0.0f, 0.0f, 0.0f, 0.0f, 0 },
				{ "keel fore", kFeelKeel, 0.0f, 0.5f, 0.0f, 0.0f, 0 },
			};
			for (auto& c : feelLogCooldown_) c = (std::max)(0.0f, c - a_dt);
			for (auto& c : feelReactCooldown_) c = (std::max)(0.0f, c - a_dt);
			feelTerrainLogCooldown_ = (std::max)(0.0f, feelTerrainLogCooldown_ - a_dt);
			feelSummaryCooldown_ = (std::max)(0.0f, feelSummaryCooldown_ - a_dt);
			FeelApplyMotion(a_dt);
			const bool gated = hullFeelMode_ == 0 || !contactOn_ || contactSuspended_ || skimAirborne_ || skimOnRamp_ || skimOnLand_ ||
				skimFallExit_ > 0.0f || postLandTimer_ > 0.0f || rideTime_ < 3.0f;
			float cx = 0.0f, cy = 0.0f;
			BoatBow::BoatCenterFromVehicle(0.0f, 0.0f, 0.0f, cx, cy);
			const float sh = std::sin(skimHeading_), ch = std::cos(skimHeading_);
			auto toWorld = [&](float a_r, float a_f, float a_z) { return RE::NiPoint3{ vehiclePos_.x + ch * a_r + sh * a_f, vehiclePos_.y - sh * a_r + ch * a_f, a_z }; };
			float wc = 0.0f;
			if (gated || !SkimWaterAt(toWorld(cx, cy, vehiclePos_.z), wc)) {
				feelTouch_.fill(false);
				return;
			}
			const auto t0 = std::chrono::steady_clock::now();
			const int slot = static_cast<int>(feelFrame_++ % 3);
			const float water = skimWaterHeight_;
			const bool fallNear = skimFlowAhead_ || contactSuspended_ || skimRampTime_ > 0.0f || skimFallExit_ > 0.0f;
			const float moved = skimSpeed_ * a_dt;
			auto* tes = RE::TES::GetSingleton();
			int ran = 0;
			for (int i = 0; i < kFeelers; ++i) {
				const Feeler& fl = kFeel[i];
				if (fl.group != kFeelBow && i % 3 != slot) continue;
				++ran;
				const float r = cx + fl.right * feelWidth_, f = cy + fl.fwd * feelLen_;
				bool hit = false;
				RE::COL_LAYER layer = RE::COL_LAYER::kUnidentified;
				float frac = 1.0f, dist = 0.0f, rise = 0.0f;
				bool landHit = false;
				if (fl.group != kFeelKeel) {
					// Horizontal sweep at trim height, outward, kFeelReach plus what the boat moves that way this frame.
					const float reach = kFeelReach + (std::max)(0.0f, moved * fl.dirFwd);
					const RE::NiPoint3 from = toWorld(r, f, water + 30.0f);
					const RE::NiPoint3 to = toWorld(r + fl.dirRight * reach, f + fl.dirFwd * reach, water + 30.0f);
					RE::TESObjectREFR* ref = nullptr;
					if (SkimSweep(from, to, frac, &layer, &ref) && !FeelOwnRef(ref)) {
						hit = true;
						dist = frac * reach;
					}
				} else {
					// Keel: anything rising into the hull bottom (sweep down to the keel line), or terrain above it.
					const float keelLine = water - feelKeel_;
					RE::TESObjectREFR* ref = nullptr;
					if (SkimSweep(toWorld(r, f, water + 5.0f), toWorld(r, f, keelLine + 4.0f), frac, &layer, &ref) && !FeelOwnRef(ref)) {
						hit = true;
						rise = (water + 5.0f) - frac * ((water + 5.0f) - (keelLine + 4.0f)) - keelLine;
					}
					float land = 0.0f;
					if (!hit && tes && tes->GetLandHeight(toWorld(r, f, water), land) && std::isfinite(land) && land >= keelLine + kGroundDryRise) {
						hit = true;
						landHit = true;
						layer = RE::COL_LAYER::kTerrain;
						rise = land - keelLine;
					}
				}
				if (hit) {
					const bool counts = layer == RE::COL_LAYER::kTerrain || layer == RE::COL_LAYER::kGround || layer == RE::COL_LAYER::kStatic ||
						layer == RE::COL_LAYER::kAnimStatic || layer == RE::COL_LAYER::kProps || layer == RE::COL_LAYER::kClutter;
					const bool swell = fallNear && (layer == RE::COL_LAYER::kStatic || layer == RE::COL_LAYER::kAnimStatic || layer == RE::COL_LAYER::kProps);
					hit = counts && !swell;
				}
				feelTouch_[i] = hit;
				if (!hit) continue;
				++feelHits_[i];
				if (hullFeelMode_ == 2 && !FlowOverride()) {  // 44.1.1: feelers only log in a stream
					// 40.1.1: terrain is the beach system's job; 40.1.2: react only on real contact.
					const bool terrain = landHit || layer == RE::COL_LAYER::kTerrain || layer == RE::COL_LAYER::kGround;
					// Closing speed: the feeler point's velocity (forward speed and yaw) along its outward direction, or how fast
					// the hit distance shrinks, whichever is larger.
					const float vRight = skimYawRate_ * f, vFwd = skimSpeed_ - skimYawRate_ * r;
					float closing = vRight * fl.dirRight + vFwd * fl.dirFwd;
					if (fl.group == kFeelKeel) closing = skimSpeed_;
					if (fl.group != kFeelKeel && feelPrevAt_[i] > 0.0f && rideTime_ - feelPrevAt_[i] < 0.2f && rideTime_ > feelPrevAt_[i]) {
						closing = (std::max)(closing, (feelPrevDist_[i] - dist) / (rideTime_ - feelPrevAt_[i]));
					}
					feelPrevDist_[i] = dist;
					feelPrevAt_[i] = rideTime_;
					const bool contact = fl.group == kFeelKeel || dist <= kFeelTouch + (std::max)(0.0f, closing) * a_dt;
					// 48.2: a tall, steep terrain face at the waterline (a bank boulder) is a rock, not a shore.
					bool rock = false;
					if (terrain && !landHit && fl.group != kFeelKeel && feelRocksOn_ && rideTime_ >= rockOffUntil_ && !skimOnLand_ && appliedLift_ < 0.5f && !onIce_) {
						auto* tesR = RE::TES::GetSingleton();
						const float hr = r + fl.dirRight * dist, hf = f + fl.dirFwd * dist;
						const RE::NiPoint3 hitPt = toWorld(hr, hf, water);
						const RE::NiPoint3 beyond = toWorld(hr + fl.dirRight * 40.0f, hf + fl.dirFwd * 40.0f, water);
						float l0 = 0.0f, l1 = 0.0f;
						if (tesR && tesR->GetLandHeight(hitPt, l0) && std::isfinite(l0) && tesR->GetLandHeight(beyond, l1) && std::isfinite(l1)) {
							const float riseW = l0 - water, steep = l1 - l0;
							if (riseW > 50.0f && steep >= 40.0f) {
								rock = true;
								// Chatter guard: more than 6 in 2 s on the same spot switches the rule off for 3 s.
								const float sx = hitPt.x - rockSpot_.x, sy = hitPt.y - rockSpot_.y;
								if (rideTime_ - rockWindowAt_ > 2.0f || sx * sx + sy * sy > 200.0f * 200.0f) { rockWindowAt_ = rideTime_; rockSpot_ = hitPt; rockFires_ = 0; }
								if (contact && ++rockFires_ > 6) {
									rockOffUntil_ = rideTime_ + 3.0f;
									rock = false;
									logger::info("Feel: rock rule chatter at ({:.0f},{:.0f}), off for 3 s", hitPt.x, hitPt.y);
								}
								if (rock && rideTime_ - rockLogAt_[i] >= 1.0f) {
									rockLogAt_[i] = rideTime_;
									logger::info("Feel: rock (terrain, tall+steep) {} rise {:.0f} steep {:.0f} at ({:.0f},{:.0f},{:.0f})", fl.name, riseW, steep, hitPt.x, hitPt.y, l0);
								}
								if (rock && fl.group == kFeelBow && dist <= 40.0f) rockAheadUntil_ = rideTime_ + 0.25f;
							}
						}
					}
					if (rock) {
						if (contact) { ++hRocks_; FeelReact(i, fl, sh, ch, closing); }
					} else if (terrain) {
						++hFeelTerrain_;
						if (feelTerrainLogCooldown_ <= 0.0f) {
							feelTerrainLogCooldown_ = 1.0f;
							logger::info("Feel: (terrain, left to the beach system) {} {}", fl.name, landHit ? "land height" : FeelLayerName(layer));
						}
					} else if (contact) {
						FeelReact(i, fl, sh, ch, closing);
					}
				}
				if (feelLogCooldown_[fl.group] > 0.0f) continue;
				feelLogCooldown_[fl.group] = 0.25f;  // at most 4 lines a second per group
				const RE::NiPoint3 at = toWorld(r, f, water);
				if (fl.group == kFeelKeel) {
					logger::info("Feel: {} hit {}{}, rises {:.0f} above keel, boat speed {:.0f}, heading {:.0f} pos ({:.0f},{:.0f},{:.0f})", fl.name, FeelLayerName(layer),
						landHit ? " (land height)" : "", rise, skimSpeed_, skimHeading_ * 57.2958f, at.x, at.y, at.z);
				} else {
					logger::info("Feel: {} hit {} at {:.0f} u, boat speed {:.0f}, fraction {:.2f}, heading {:.0f} pos ({:.0f},{:.0f},{:.0f})", fl.name, FeelLayerName(layer),
						dist, skimSpeed_, frac, skimHeading_ * 57.2958f, at.x, at.y, at.z);
				}
			}
			int bow = 0, sideL = 0, sideR = 0, stern = 0, keel = 0;
			for (int i = 0; i < kFeelers; ++i) {
				if (!feelTouch_[i]) continue;
				switch (kFeel[i].group) {
				case kFeelBow: ++bow; break;
				case kFeelSide: (kFeel[i].side < 0 ? sideL : sideR)++; break;
				case kFeelStern: ++stern; break;
				case kFeelKeel: ++keel; break;
				}
			}
			if (bow + sideL + sideR + stern + keel > 0 && feelSummaryCooldown_ <= 0.0f) {
				feelSummaryCooldown_ = 1.0f;
				logger::info("Feel: touching bow={} sides={}/{} stern={} keel={}", bow, sideL, sideR, stern, keel);
			}
			feelCostMs_ += std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
			++feelCostFrames_;
			if ((feelCostTimer_ += a_dt) >= 10.0f) {
				logger::info("Feel: cost {:.3f} ms/frame ({} feelers, {} this frame)", feelCostMs_ / static_cast<float>((std::max)(1, feelCostFrames_)), kFeelers, ran);
				feelCostTimer_ = feelCostMs_ = 0.0f;
				feelCostFrames_ = 0;
			}
		}
		// 38B: soft reactions. Bow above kFeelStopSpeed: stop over 0.15 s, 30 u/s rebound, bump; slower: a scrape. One bow
		// quarter alone also turns the bow away. Sides/stern: a fading push away from the contact and a 10-20% speed loss,
		// halved when the other side touches too. Keel: a small dip kick, a 1-2 deg jolt and 5% speed loss.
		static constexpr float kFeelStopSpeed = 150.0f;
		static constexpr float kFeelPushSpeed = 40.0f;
		static constexpr float kFeelPushMax = 60.0f;     // 40.1.3 (was 80)
		static constexpr float kFeelTouch = 6.0f;        // 40.1.2: a hit is contact within this (plus this frame's closing)
		static constexpr float kFeelMinSpeed = 25.0f;    // 40.1.3: slower than this, or closing under kFeelMinClosing: no scrape/push/yaw
		static constexpr float kFeelMinClosing = 30.0f;
		static constexpr float kFeelYawKick = 0.2094f;  // 12 deg/s
		void FeelScrape(int a_i, const Feeler& a_fl, float a_sh, float a_ch, const char* a_what, float a_closing)
		{
			if (feelReactCooldown_[a_i] > 0.0f) return;
			if (skimSpeed_ < kFeelMinSpeed || a_closing < kFeelMinClosing) return;  // 40.1.3: a slow or stopped boat is never moved
			feelReactCooldown_[a_i] = 0.5f;
			// Push away from the contact: the feeler's outward direction reversed, in world space.
			const float wx = a_fl.dirRight * a_ch + a_fl.dirFwd * a_sh;
			const float wy = -a_fl.dirRight * a_sh + a_fl.dirFwd * a_ch;
			bool otherSide = false;
			for (int j = 0; j < kFeelers; ++j) {
				if (j != a_i && feelTouch_[j] && a_fl.side != 0 && FeelSideOf(j) == -a_fl.side) otherSide = true;
			}
			const float push = kFeelPushSpeed * (otherSide ? 0.5f : 1.0f);
			feelPush_.x -= wx * push;
			feelPush_.y -= wy * push;
			const float len = std::sqrt(feelPush_.x * feelPush_.x + feelPush_.y * feelPush_.y);
			if (len > kFeelPushMax) { feelPush_.x *= kFeelPushMax / len; feelPush_.y *= kFeelPushMax / len; }
			const float before = skimSpeed_;
			const float loss = 0.04f * (otherSide ? 0.5f : 1.0f);  // 40.1.3: 4% (was 10-20%)
			skimSpeed_ *= 1.0f - loss;
			logger::info("Feel: scrape {} {} ({}), speed {:.0f} -> {:.0f}, push {:.0f} u/s{}", a_fl.side < 0 ? "L" : (a_fl.side > 0 ? "R" : "C"), a_fl.name, a_what,
				before, skimSpeed_, push, otherSide ? " (both sides touching: halved)" : "");
		}
		int FeelSideOf(int a_j) const
		{
			static constexpr int kSides[kFeelers] = { 0, -1, 1, -1, -1, 1, 1, -1, 1, 0, 0, 0 };
			return kSides[a_j];
		}
		void FeelReact(int a_i, const Feeler& a_fl, float a_sh, float a_ch, float a_closing)
		{
			++hFeelReact_;
			switch (a_fl.group) {
			case kFeelBow: {
				const float closing = a_closing;
				if (closing > kFeelStopSpeed && feelReactCooldown_[a_i] <= 0.0f && feelStopTimer_ <= 0.0f) {
					feelReactCooldown_[a_i] = 0.5f;
					feelStopTimer_ = kFeelStopTime;
					feelStopFrom_ = skimSpeed_;
					lastStopAt_ = rideTime_;
					glueZeroTimer_ = 1.0f;  // 40.2
					CrewBump("rock hit");
					BoatBow::Impact(std::clamp(closing / P().maxSpeed, 0.3f, 1.0f));
					logger::info("Feel: STOP bow {} at speed {:.0f}, stopping over {:.1f} s (no rebound)", a_fl.name, skimSpeed_, kFeelStopTime);
				} else if (closing <= kFeelStopSpeed) {
					FeelScrape(a_i, a_fl, a_sh, a_ch, "slow bow hit", closing);
				}
				// Only one bow quarter touching: turn the bow away from it (not when slow, 40.1.3).
				if (a_fl.side != 0 && !feelTouch_[0] && !feelTouch_[a_fl.side < 0 ? 2 : 1] && skimSpeed_ >= kFeelMinSpeed && closing >= kFeelMinClosing) {
					feelYawKick_ = std::clamp(feelYawKick_ - static_cast<float>(a_fl.side) * kFeelYawKick, -kFeelYawKick, kFeelYawKick);
				}
				break;
			}
			case kFeelSide:
			case kFeelStern:
				FeelScrape(a_i, a_fl, a_sh, a_ch, a_fl.group == kFeelSide ? "side" : "stern corner", a_closing);
				break;
			case kFeelKeel: {
				if (feelReactCooldown_[a_i] > 0.0f) break;
				feelReactCooldown_[a_i] = 0.4f;
				const float before = skimSpeed_;
				skimSpeed_ *= 0.95f;
				skimDipVel_ += -80.0f;
				const float jolt = 0.026f;  // 1.5 deg
				groundPitch_ += a_fl.fwd > 0.0f ? jolt : (a_fl.fwd < 0.0f ? -jolt : 0.0f);
				groundRoll_ += (feelFrame_ & 1u) ? jolt * 0.6f : -jolt * 0.6f;
				logger::info("Feel: bump {} speed {:.0f} -> {:.0f}", a_fl.name, before, skimSpeed_);
				break;
			}
			}
		}
		// Every frame: the fading push, the bow yaw kick and the timed stop with its rebound.
		void FeelApplyMotion(float a_dt)
		{
			if (feelStopTimer_ > 0.0f) {
				// 40.1.4: ease-out to 0 over kFeelStopTime, no rebound.
				feelStopTimer_ -= a_dt;
				const float t = std::clamp(1.0f - feelStopTimer_ / kFeelStopTime, 0.0f, 1.0f);
				skimSpeed_ = (std::min)(skimSpeed_, feelStopFrom_ * (1.0f - t * t));
				if (feelStopTimer_ <= 0.0f) skimSpeed_ = 0.0f;
			}
			if (inLeft_ || inRight_) feelYawKick_ = 0.0f;  // 44.3.3: no ground/feeler turning while the player steers
			if (std::abs(feelYawKick_) > 0.001f) {
				skimHeading_ = WrapAngle(skimHeading_ + feelYawKick_ * a_dt);
				feelYawKick_ *= std::exp(-7.5f * a_dt);  // gone in about 0.4 s
			} else {
				feelYawKick_ = 0.0f;
			}
			if (feelPush_.x != 0.0f || feelPush_.y != 0.0f) {
				const RE::NiPoint3 to{ vehiclePos_.x + feelPush_.x * a_dt, vehiclePos_.y + feelPush_.y * a_dt, vehiclePos_.z };
				float w = 0.0f;
				if (!skimAirborne_ && !skimOnRamp_ && SkimWaterAt(to, w)) {
					vehiclePos_.x = to.x;
					vehiclePos_.y = to.y;
				} else {
					feelPush_ = {};  // never push onto land or into unloaded space
				}
				const float k = std::exp(-6.0f * a_dt);
				feelPush_.x *= k;
				feelPush_.y *= k;
				if (feelPush_.x * feelPush_.x + feelPush_.y * feelPush_.y < 0.25f) feelPush_ = {};
			}
		}
		// 39.1: hull extents from the hull centre. The mesh bounds are bounding spheres (690 x 690), not the outline, so
		// the 38A numbers are the one source of truth for the ground samples and the feelers (oas "hullfeel len|width N").
		float HullBowTip() const { return feelLen_; }
		float HullStern() const { return feelLen_; }
		float HullHalfWidth() const { return feelWidth_; }
		std::string HullFeelStatus()
		{
			static constexpr const char* kModes[3] = { "off", "log", "on" };
			int touching = 0, total = 0;
			for (int i = 0; i < kFeelers; ++i) { touching += feelTouch_[i] ? 1 : 0; total += feelHits_[i]; }
			return std::format("hullfeel={} (len {:.0f} width {:.0f} keel {:.0f}, rocks {}, touching {}, hits this session {})", kModes[std::clamp(hullFeelMode_, 0, 2)],
				feelLen_, feelWidth_, feelKeel_, feelRocksOn_ ? "on" : "off", touching, total);
		}

		// 43 (v1.0.39): [Boat] settings in OarsAndSails.ini.
		struct BoatIniValues { float strokeSurge = 1.0f; bool reequipPlayer = true; float riderPitchScale = 0.40f; float riderRollScale = 0.60f; float riderPivotUp = 60.0f; bool crewTilt = false; };
		static const BoatIniValues& BoatIni()
		{
			static BoatIniValues v;
			static bool loaded = false;
			if (!loaded) {
				loaded = true;
				std::error_code ec;
				const auto ini = (std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OarsAndSails.ini").string();
				auto num = [&](const char* a_key, float a_def) {
					char buf[64]{};
					GetPrivateProfileStringA("Boat", a_key, "", buf, sizeof(buf), ini.c_str());
					try { return buf[0] ? std::stof(buf) : a_def; } catch (...) { return a_def; }
				};
				v.strokeSurge = std::clamp(num("StrokeSurge", 1.0f), 0.0f, 1.0f);  // 56.30: full surge per dig (was 0.5)
				v.reequipPlayer = num("ReequipPlayer", 1.0f) != 0.0f;
				v.riderPitchScale = std::clamp(num("RiderPitchScale", 0.40f), 0.0f, 1.0f);
				v.riderRollScale = std::clamp(num("RiderRollScale", 0.60f), 0.0f, 1.0f);
				v.riderPivotUp = std::clamp(num("RiderPivotUp", 60.0f), 0.0f, 150.0f);
				v.crewTilt = num("CrewTilt", 0.0f) != 0.0f; // Default 0: physics-safe (no HDT-SMP mesh culling)
				logger::info("Boat ini: StrokeSurge {:.2f} ReequipPlayer {} RiderPitchScale {:.2f} RiderRollScale {:.2f} RiderPivotUp {:.0f} CrewTilt {}", v.strokeSurge,
					v.reequipPlayer ? 1 : 0, v.riderPitchScale, v.riderRollScale, v.riderPivotUp, v.crewTilt ? 1 : 0);
			}
			return v;
		}

		// 43.1: weapons are unequipped at boarding (sheathing failed: the drawn state fought the row clip and the glue) and
		// kept off while aboard; the player gets them back at the exit.
		struct Unequipped { RE::FormID actor; RE::FormID right; RE::FormID left; };
		std::vector<Unequipped> unequipped_;
		std::vector<std::pair<RE::FormID, float>> weaponNext_;
		void UnequipWeapons(RE::Actor* a_actor, const char* a_why)
		{
			if (!a_actor || IsDogActor(a_actor)) return;
			auto* em = RE::ActorEquipManager::GetSingleton();
			if (!em) return;
			auto* st = a_actor->AsActorState();
			const bool drawn = st && st->IsWeaponDrawn();
			auto* leftSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x00013F43);
			auto* rightSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x00013F42);
			bool any = false;
			for (bool left : { false, true }) {
				auto* form = a_actor->GetEquippedObject(left);
				auto* weap = form ? form->As<RE::TESObjectWEAP>() : nullptr;
				if (!weap) continue;
				any = true;
				auto it = std::find_if(unequipped_.begin(), unequipped_.end(), [&](const Unequipped& u) { return u.actor == a_actor->GetFormID(); });
				if (it == unequipped_.end()) it = unequipped_.insert(unequipped_.end(), Unequipped{ a_actor->GetFormID(), 0u, 0u });
				(left ? it->left : it->right) = weap->GetFormID();
				em->UnequipObject(a_actor, weap, nullptr, 1, left ? leftSlot : rightSlot, false, true, true, true);
				logger::info("Weapons: {} unequipped {} ({}) drawn was {} ({})", a_actor->GetName(), weap->GetName(), left ? "left" : "right", drawn ? "yes" : "no", a_why);
			}
			if (any || drawn) a_actor->DrawWeaponMagicHands(false);
		}
		// Throttled to once every 0.5 s per actor.
		void KeepWeaponsOff(RE::Actor* a_actor)
		{
			if (!a_actor) return;
			const RE::FormID id = a_actor->GetFormID();
			auto it = std::find_if(weaponNext_.begin(), weaponNext_.end(), [&](const auto& e) { return e.first == id; });
			if (it == weaponNext_.end()) it = weaponNext_.insert(weaponNext_.end(), { id, 0.0f });
			if (rideTime_ < it->second) return;
			it->second = rideTime_ + 0.5f;
			auto* st = a_actor->AsActorState();
			const bool armed = (a_actor->GetEquippedObject(false) && a_actor->GetEquippedObject(false)->As<RE::TESObjectWEAP>()) ||
				(a_actor->GetEquippedObject(true) && a_actor->GetEquippedObject(true)->As<RE::TESObjectWEAP>());
			if (a_actor == RE::PlayerCharacter::GetSingleton()) {
				if (armed || (st && st->IsWeaponDrawn())) UnequipWeapons(a_actor, "while aboard");
				return;
			}
			// 56.8: the crew: no unequip loop. Anything that turned up since boarding goes into the stash (once, it is then
			// gone from the inventory), combat is stopped, a drawn weapon is put away.
			if (a_actor->IsInCombat()) a_actor->StopCombat();
			bool stashable = false;
			for (auto& [obj, data] : a_actor->GetInventory()) {
				if (IsStashable(obj) && data.first > 0 && !(data.second && data.second->IsQuestObject())) { stashable = true; break; }
			}
			if (stashable) StashWeapons(a_actor, "while aboard");
			if (st && st->IsWeaponDrawn()) a_actor->DrawWeaponMagicHands(false);
		}
		void ReequipPlayer()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* em = RE::ActorEquipManager::GetSingleton();
			if (!player || !em) return;
			auto it = std::find_if(unequipped_.begin(), unequipped_.end(), [&](const Unequipped& u) { return u.actor == player->GetFormID(); });
			if (it == unequipped_.end()) return;
			if (!BoatIni().reequipPlayer) { logger::info("Weapons: player left unarmed (ReequipPlayer=0)"); unequipped_.erase(it); return; }
			auto* leftSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x00013F43);
			auto* rightSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x00013F42);
			for (bool left : { false, true }) {
				auto* weap = RE::TESForm::LookupByID<RE::TESObjectWEAP>(left ? it->left : it->right);
				if (!weap) continue;
				const auto inv = player->GetInventoryCounts();
				if (auto c = inv.find(weap); c == inv.end() || c->second <= 0) continue;
				em->EquipObject(player, weap, nullptr, 1, left ? leftSlot : rightSlot, false, false, false, true);
				logger::info("Weapons: player re-equipped {} ({})", weap->GetName(), left ? "left" : "right");
			}
			unequipped_.erase(it);
		}

		// =====================================================================
		// 56.8 (v1.0.60): the crew's weapons, shields and ammo go into a hidden container of ours at boarding and come back
		// when they are no longer aboard (exit, release, death, teleport, a load). Unequipping them every second made them
		// re-equip every second (tailCombatIdle, arrowDetach, idleChairGetUp = STOOD UP). The list is in the co-save, so a
		// crash or a save while aboard never loses items. The player keeps the old unequip / re-equip.
		// =====================================================================
		struct StashItem { RE::FormID item; std::int32_t count; };
		struct StashEntry { RE::FormID actor; std::vector<StashItem> items; };
		std::vector<StashEntry> stash_;
		RE::FormID stashRefId_{ 0 };
		float stashTick_{ 0.0f };

		// 56.11.4: a save made while aboard (rowing or not) loads with the player still in our seat / row clip and the
		// controls the ride switched off. The co-save says so ('ABRD'); 1 s after the player's 3D is in, it is undone.
		bool loadRelease_{ false };
		float loadReleaseWait_{ 0.0f };
		bool AboardForSave() const { return playerSkim_; }

		// ---- 56.26 (v1.0.75): saves ----
		// SaveHook (below) sees every save request before the game captures anything. Aboard, a manual save, quicksave or
		// console save is held: the player gets off the normal way (ExitPlayerSkim), and once stood the held save is sent on
		// with its own type and name. An autosave aboard is skipped and sent again on the first tick on foot (once).
		// saving_ covers a save going through until 0.5 s after kSaveGame: our per-frame work (boat, props, oars, stash,
		// AI and graph events) holds still meanwhile.
		using SaveFn = bool (*)(RE::BGSSaveLoadManager*, std::int32_t, std::uint32_t, const char*);
		enum class SaveKind { kManual, kQuick, kAuto };
		struct HeldSave
		{
			RE::BGSSaveLoadManager* mgr{ nullptr };
			std::int32_t type{ 0 };
			std::uint32_t stats{ 0 };
			bool hasName{ false };
			std::string name;
			SaveKind kind{ SaveKind::kManual };
		};
		SaveFn saveOriginal_{ nullptr };
		bool savePassThrough_{ false };
		std::optional<HeldSave> heldSave_;      // waits for the stand-up
		std::optional<HeldSave> heldAutosave_;  // skipped aboard, sent again after the ride
		bool saveExitPending_{ false };
		float saveWait_{ 0.0f };
		float saveStood_{ 0.0f };
		bool saving_{ false };
		float savingLeft_{ 0.0f };  // 0.5 s once kSaveGame came; a 3 s cap before that (a save the game refused)

		static const char* SaveKindName(SaveKind a_kind)
		{
			return a_kind == SaveKind::kAuto ? "autosave" : a_kind == SaveKind::kQuick ? "quicksave" : "save";
		}
		// The name decides when there is one; without one the type does (1.7.104: 3 Autosave, 4 Quicksave, else Save).
		static SaveKind ClassifySave(std::int32_t a_type, const char* a_name)
		{
			if (a_name && *a_name) {
				const auto lower = [&] { std::string s(a_name); for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }();
				if (lower.starts_with("autosave")) return SaveKind::kAuto;
				if (lower.starts_with("quicksave")) return SaveKind::kQuick;
				return SaveKind::kManual;
			}
			return a_type == 3 ? SaveKind::kAuto : a_type == 4 ? SaveKind::kQuick : SaveKind::kManual;
		}

		void OnSaveGameMessage(const char* a_name)
		{
			saving_ = true;
			savingLeft_ = 0.5f;
			logger::info("Save: {} aboard={} rowers={} boat={:08X} speed={:.0f}", a_name ? a_name : "?", AboardForSave() ? "y" : "n",
				rowAnimOn_ ? 1 : 0, BoatBow::ParkedRefId(), skimSpeed_);
		}

		void MarkSaving()
		{
			saving_ = true;
			savingLeft_ = 3.0f;
		}

		// True: the request is held or skipped (the hook returns without saving).
		bool OnSaveRequest(RE::BGSSaveLoadManager* a_mgr, std::int32_t a_type, std::uint32_t a_stats, const char* a_name)
		{
			const SaveKind kind = ClassifySave(a_type, a_name);
			const std::string name = a_name ? a_name : "";
			logger::info("Save: request {} (type {}, name '{}') aboard={}", SaveKindName(kind), a_type, name, AboardForSave() ? "y" : "n");
			if (!AboardForSave()) return false;  // on foot (or beside the parked boat): untouched
			HeldSave h{ a_mgr, a_type, a_stats, a_name != nullptr, name, kind };
			if (kind == SaveKind::kAuto) {
				if (!heldAutosave_) {
					heldAutosave_ = std::move(h);
					logger::info("Save: autosave skipped while aboard, sent again once the ride is over");
				}
				return true;
			}
			if (heldSave_) {
				logger::info("Save: a {} already waits for the stand-up, this {} dropped", SaveKindName(heldSave_->kind), SaveKindName(kind));
				return true;
			}
			heldSave_ = std::move(h);
			saveExitPending_ = true;  // the exit runs on the next player tick, not inside the game's save call
			saveWait_ = 0.0f;
			saveStood_ = 0.0f;
			RE::SendHUDMessage::ShowHUDMessage("Leaving the boat to save");
			logger::info("Save: unboarding for the save");
			return true;
		}

		void SendHeldSave(HeldSave a_save)
		{
			auto run = [this, s = std::move(a_save)]() {
				if (!saveOriginal_) return;
				savePassThrough_ = true;
				MarkSaving();
				const bool ok = saveOriginal_(s.mgr, s.type, s.stats, s.hasName ? s.name.c_str() : nullptr);
				savePassThrough_ = false;
				logger::info("Save: held {} sent ({})", SaveKindName(s.kind), ok ? "written" : "the game refused it");
			};
			if (auto* tasks = SKSE::GetTaskInterface()) tasks->AddTask(run);  // on the main thread's task point, like a queued save
			else run();
		}

		// Every player tick, before anything else of ours.
		void TendSave(float a_dt)
		{
			if (saving_ && (savingLeft_ -= a_dt) <= 0.0f) saving_ = false;
			if (heldSave_) {
				if (saveExitPending_) {
					saveExitPending_ = false;
					ExitPlayerSkim("save requested");
				}
				saveWait_ += a_dt;
				const bool stood = !playerSkim_ && !getUpPending_ && exitHold_ <= 0.0f;
				saveStood_ = stood ? saveStood_ + a_dt : 0.0f;
				if ((stood && saveStood_ >= 1.0f) || saveWait_ >= 4.0f) {
					if (stood) logger::info("Save: stood, saving now ({} ms)", static_cast<int>(saveWait_ * 1000.0f));
					else logger::warn("Save: the exit did not finish in 4 s (aboard={}), saving anyway ({} ms){}", playerSkim_ ? "y" : "n",
						static_cast<int>(saveWait_ * 1000.0f), playerSkim_ ? ", the co-save marks it aboard" : "");
					SendHeldSave(std::move(*heldSave_));
					heldSave_.reset();
				}
				return;
			}
			if (heldAutosave_ && !playerSkim_) {
				logger::info("Save: the ride is over, autosave sent again");
				SendHeldSave(std::move(*heldAutosave_));
				heldAutosave_.reset();
			}
		}

		void TendLoadRelease(RE::PlayerCharacter* a_player, float a_dt)
		{
			if (!loadRelease_ || playerSkim_ || !a_player || !a_player->Get3D() || !a_player->GetParentCell()) return;
			if ((loadReleaseWait_ += a_dt) < 1.0f) return;
			loadRelease_ = false;
			std::string sent;
			for (const char* ev : { kRowStopEvent, "OffsetGPMAStop", "IdleChairExitStart", "IdleFurnitureExit", "sneakStop", "IdleForceDefaultState" }) {
				if (a_player->NotifyAnimationGraph(ev)) sent += std::format("{}{}", sent.empty() ? "" : ", ", ev);
			}
			a_player->SetGraphVariableBool("bOffsetGPMA", false);
			for (const char* v : { "bIsSitting", "isIdleSitting", "isInFurniture", "bAnimationDriven", "bHumanoidFootIKDisable" }) a_player->SetGraphVariableBool(v, false);
			if (auto* st = a_player->AsActorState()) st->actorState1.sitSleepState = RE::SIT_SLEEP_STATE::kNormal;
			SetSkimControls(true, true);
			a_player->SetCollision(true);
			SetActorHeadTracking(a_player, true);
			RestoreCamera("load");
			SyncBodyFlagToCamera(a_player, false);
			BoatBow::TendBoatBakedOars(nullptr, false, 0.0f, "load");
			standTimer_ = 6.0f;  // the usual post-exit checks (WalkState, stuck lift) run as after a normal exit
			standTick_ = 0.0f;
			standLogs_ = 0;
			logger::info("Load: the save was made aboard; player released (graph events accepted: {})", sent.empty() ? "none" : sent);
		}

		static bool IsStashable(const RE::TESBoundObject* a_obj)
		{
			if (!a_obj) return false;
			if (a_obj->Is(RE::FormType::Weapon) || a_obj->Is(RE::FormType::Ammo)) return true;
			const auto* armo = a_obj->As<RE::TESObjectARMO>();
			return armo && armo->IsShield();
		}

		// An empty, non-respawning container of the game's that is none of our props (the chest's base and model are taken
		// by AdoptOldChests and the orphan sweep).
		static RE::TESBoundObject* StashBase()
		{
			static RE::TESBoundObject* base = nullptr;
			static bool searched = false;
			if (searched) return base;
			searched = true;
			auto* chest = RE::TESForm::LookupByID<RE::TESObjectCONT>(0x000F3923);
			const std::string chestModel = chest && chest->GetModel() ? chest->GetModel() : "";
			if (auto* dh = RE::TESDataHandler::GetSingleton()) {
				for (auto* c : dh->GetFormArray<RE::TESObjectCONT>()) {
					if (!c || c == chest || c->numContainerObjects != 0 || c->data.flags.any(RE::CONT_DATA::Flag::kRespawn)) continue;
					const char* m = c->GetModel();
					if (!m || !*m || EqualsNoCase(m, chestModel)) continue;
					base = c;
					break;
				}
			}
			logger::info("Stash: container base {}", base ? std::format("{:08X} '{}'", base->GetFormID(), base->As<RE::TESObjectCONT>()->GetModel()) : std::string("NOT FOUND"));
			return base;
		}

		RE::TESObjectREFR* StashRef(bool a_create)
		{
			if (stashRefId_) {
				if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(stashRefId_); ref && !ref->IsDeleted()) return ref;
				if (!a_create) return nullptr;
			}
			if (!a_create) return nullptr;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* base = StashBase();
			if (!player || !base || !player->GetParentCell()) return nullptr;
			auto ref = player->PlaceObjectAtMe(base, false);
			if (!ref) return nullptr;
			const auto p = player->GetPosition();
			ref->SetPosition(RE::NiPoint3{ p.x, p.y, p.z - 2000.0f });  // out of sight under the boat
			stashRefId_ = ref->GetFormID();
			logger::info("Stash: container {:08X} placed", stashRefId_);
			return ref.get();
		}

		// Moves every weapon, shield and ammo stack (with its own data: enchanted, tempered, named) into the stash.
		void StashWeapons(RE::Actor* a_actor, const char* a_why)
		{
			if (!a_actor || IsDogActor(a_actor) || a_actor == RE::PlayerCharacter::GetSingleton()) return;
			auto* box = StashRef(true);
			if (!box) { logger::warn("Stash: no container, {} keeps the weapons", a_actor->GetName()); return; }
			auto it = std::find_if(stash_.begin(), stash_.end(), [&](const StashEntry& e) { return e.actor == a_actor->GetFormID(); });
			if (it == stash_.end()) it = stash_.insert(stash_.end(), StashEntry{ a_actor->GetFormID(), {} });
			std::string names;
			for (auto& [obj, data] : a_actor->GetInventory()) {
				if (!IsStashable(obj) || data.first <= 0) continue;
				if (auto* entry = data.second.get(); entry && entry->IsQuestObject()) continue;
				int left = data.first;
				if (auto* entry = data.second.get(); entry && entry->extraLists) {
					std::vector<RE::ExtraDataList*> lists;
					for (auto* xl : *entry->extraLists) if (xl) lists.push_back(xl);
					for (auto* xl : lists) {
						const int c = (std::max)(1, xl->GetCount());
						a_actor->RemoveItem(obj, c, RE::ITEM_REMOVE_REASON::kStoreInContainer, xl, box);
						left -= c;
					}
				}
				if (left > 0) a_actor->RemoveItem(obj, left, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, box);
				auto si = std::find_if(it->items.begin(), it->items.end(), [&](const StashItem& s) { return s.item == obj->GetFormID(); });
				if (si == it->items.end()) it->items.push_back({ obj->GetFormID(), data.first });
				else si->count += data.first;
				names += std::format("{}{} x{}", names.empty() ? "" : ", ", obj->GetName(), data.first);
			}
			if (!names.empty()) logger::info("Stash: {} {:08X} weapons into {:08X} ({}): {}", a_actor->GetName(), a_actor->GetFormID(), box->GetFormID(), a_why, names);
			if (it->items.empty()) stash_.erase(it);
		}

		// Gives one actor's items back (counts as stashed, extra data moves with them). False if it cannot happen yet.
		bool RestoreStash(const StashEntry& a_e, const char* a_why)
		{
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_e.actor);
			auto* box = StashRef(false);
			if (!actor || !box) return false;
			std::string names;
			for (const auto& si : a_e.items) {
				auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(si.item);
				if (!obj) continue;
				int want = si.count;
				for (auto& [o, data] : box->GetInventory()) {
					if (o != obj || data.first <= 0 || want <= 0) continue;
					if (auto* entry = data.second.get(); entry && entry->extraLists) {
						std::vector<RE::ExtraDataList*> lists;
						for (auto* xl : *entry->extraLists) if (xl) lists.push_back(xl);
						for (auto* xl : lists) {
							if (want <= 0) break;
							const int c = (std::min)(want, (std::max)(1, xl->GetCount()));
							box->RemoveItem(obj, c, RE::ITEM_REMOVE_REASON::kStoreInContainer, xl, actor);
							want -= c;
						}
					}
					if (want > 0) {
						const int c = (std::min)(want, data.first);
						box->RemoveItem(obj, c, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, actor);
						want -= c;
					}
				}
				names += std::format("{}{} x{}{}", names.empty() ? "" : ", ", obj->GetName(), si.count - want, want > 0 ? std::format(" ({} missing)", want) : std::string{});
			}
			logger::info("Stash: {} {:08X} weapons back ({}): {}", actor->GetName(), actor->GetFormID(), a_why, names);
			return true;
		}

		bool AboardNow(RE::FormID a_id)
		{
			if (!playerSkim_) return false;
			for (auto& h : passengers_) if (auto a = h.get(); a && a->GetFormID() == a_id) return true;
			return false;
		}

		// Once a second: whoever is in the stash but no longer aboard (or dead) gets the items back; the container follows
		// the player while it holds anything, so its cell stays loaded.
		void TendStash(float a_dt)
		{
			if (stash_.empty() || (stashTick_ += a_dt) < 1.0f) return;
			stashTick_ = 0.0f;
			std::erase_if(stash_, [&](const StashEntry& e) {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(e.actor);
				if (!actor) return false;  // not loaded yet: kept (co-save) until it is
				if (AboardNow(e.actor) && !actor->IsDead()) return false;
				return RestoreStash(e, actor->IsDead() ? "died aboard" : playerSkim_ ? "left the boat" : "not aboard");
			});
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (auto* box = StashRef(false); box && player && !stash_.empty() && box->GetParentCell() != player->GetParentCell() && player->GetParentCell()) {
				box->MoveTo(player);
				const auto p = player->GetPosition();
				box->SetPosition(RE::NiPoint3{ p.x, p.y, p.z - 2000.0f });
			}
		}

		// 56.8.A1: what gets equipped while aboard (at most one line a second).
		struct EquipSink : public RE::BSTEventSink<RE::TESEquipEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
			{
				auto& self = OarsAndSails::Get();
				if (!a_event || !a_event->actor || !self.playerSkim_) return RE::BSEventNotifyControl::kContinue;
				static auto last = std::chrono::steady_clock::time_point{};
				const auto now = std::chrono::steady_clock::now();
				if (now - last < std::chrono::seconds(1)) return RE::BSEventNotifyControl::kContinue;
				last = now;
				auto* form = RE::TESForm::LookupByID(a_event->baseObject);
				logger::info("Equip: {} {:08X} {} {} ({:08X}) while aboard", a_event->actor->GetName(), a_event->actor->GetFormID(), a_event->equipped ? "equipped" : "unequipped",
					form && form->GetName() ? form->GetName() : "", a_event->baseObject);
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		void RegisterEquipSink()
		{
			static EquipSink sink;
			static CombatSink combatSink;  // 56.12
			static bool done = false;
			if (done) return;
			if (auto* src = RE::ScriptEventSourceHolder::GetSingleton()) {
				src->AddEventSink<RE::TESEquipEvent>(&sink);
				src->AddEventSink<RE::TESCombatEvent>(&combatSink);
				done = true;
			}
		}

		// =====================================================================
		// 56.12 (v1.0.63): the crew stay seated aboard, rowing or resting: no combat, no alert, no drawn weapon. Every frame
		// in the glue pass (cheap reads) and at once on a combat-start event: what started it is logged on each change, then
		// StopCombat, the combat target cleared, the AI alert flag cleared (what Papyrus SetAlert(false) does), a drawn weapon
		// sheathed. A seat lost within 2 s of that gets one sit straight away (the 56.8 combat-idle path). Nothing is kept
		// once they are off the boat: it only runs for passengers aboard. Damage and death are left alone.
		// =====================================================================
		struct CalmState { RE::FormID id; bool combat{ false }, alert{ false }, drawn{ false }; RE::FormID package{ 0 }; float logAt{ -100.0f }; };
		std::vector<CalmState> calm_;
		std::mutex calmMx_;
		struct CalmEvent { RE::FormID actor; RE::FormID target; std::chrono::steady_clock::time_point at; };
		std::vector<CalmEvent> calmEvents_;  // combat-start events while aboard (anyone's; dropped after 1 s), guarded by calmMx_

		struct CombatSink : public RE::BSTEventSink<RE::TESCombatEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* a_event, RE::BSTEventSource<RE::TESCombatEvent>*) override
			{
				if (!a_event || !a_event->actor || !a_event->newState.any(RE::ACTOR_COMBAT_STATE::kCombat, RE::ACTOR_COMBAT_STATE::kSearching)) return RE::BSEventNotifyControl::kContinue;
				auto& self = OarsAndSails::Get();
				if (!self.playerSkim_) return RE::BSEventNotifyControl::kContinue;
				const auto now = std::chrono::steady_clock::now();
				std::lock_guard lock(self.calmMx_);
				std::erase_if(self.calmEvents_, [&](const CalmEvent& e) { return now - e.at > std::chrono::seconds(1); });
				if (self.calmEvents_.size() < 64) self.calmEvents_.push_back({ a_event->actor->GetFormID(), a_event->targetActor ? a_event->targetActor->GetFormID() : 0u, now });
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		void HoldCrewCalm(RE::Actor* a_actor)
		{
			if (!a_actor || a_actor->IsDead() || a_actor == RE::PlayerCharacter::GetSingleton()) return;
			const RE::FormID id = a_actor->GetFormID();
			RE::FormID eventTarget = 0;
			bool event = false;
			{
				std::lock_guard lock(calmMx_);
				for (auto it = calmEvents_.begin(); it != calmEvents_.end();) {
					if (it->actor == id) { event = true; eventTarget = it->target; it = calmEvents_.erase(it); }
					else ++it;
				}
			}
			auto st = std::find_if(calm_.begin(), calm_.end(), [&](const CalmState& c) { return c.id == id; });
			if (st == calm_.end()) st = calm_.insert(calm_.end(), CalmState{ id });
			auto& rd = a_actor->GetActorRuntimeData();
			auto* proc = rd.currentProcess;
			const bool combat = a_actor->IsInCombat();
			const bool alert = proc && proc->lowProcessFlags.all(RE::AIProcess::LowProcessFlags::kAlert);
			const auto* as = a_actor->AsActorState();
			const bool drawn = as && as->IsWeaponDrawn();
			const auto* pkg = a_actor->GetCurrentPackage();
			const RE::FormID package = pkg ? pkg->GetFormID() : 0u;
			// What fires, on each change (rising edges, a package change, a combat-start event).
			if (event || (combat && !st->combat) || (alert && !st->alert) || (drawn && !st->drawn) || package != st->package) {
				auto* target = rd.currentCombatTarget.get().get();
				logger::info("Calm: {} {:08X} trigger:{}{}{}{}{} package {:08X}{} target {}", a_actor->GetName(), id, event ? " combat-start event" : "",
					combat && !st->combat ? " combat" : "", alert && !st->alert ? " alert" : "", drawn && !st->drawn ? " weapon drawn" : "",
					package != st->package ? " package change" : "", package, package != st->package ? std::format(" (was {:08X})", st->package) : std::string{},
					target ? std::format("{} {:08X}", target->GetName(), target->GetFormID()) : eventTarget ? std::format("{:08X}", eventTarget) : std::string("none"));
			}
			st->combat = combat;
			st->alert = alert;
			st->drawn = drawn;
			st->package = package;
			if (!(event || combat || alert || drawn)) return;
			if (combat || event) a_actor->StopCombat();
			rd.currentCombatTarget.reset();
			if (proc) proc->lowProcessFlags.reset(RE::AIProcess::LowProcessFlags::kAlert);
			if (drawn) a_actor->DrawWeaponMagicHands(false);  // sheathe only
			{
				std::lock_guard lock(crewIdleMx_);
				if (auto* ci = CrewIdleFor(id)) ci->combatIdleAt = rideTime_;  // a seat lost now gets one sit at once
			}
			if (rideTime_ - st->logAt >= 1.0f) {
				st->logAt = rideTime_;
				logger::info("Calm: {} {:08X} suppressed ({}{}{}{}): StopCombat, target cleared, alert off{}", a_actor->GetName(), id, event ? "event " : "",
					combat ? "combat " : "", alert ? "alert " : "", drawn ? "drawn" : "", drawn ? ", sheathed" : "");
			}
		}

		// 56.8.A3: combat-idle events (the weapon pass used to cause them) are not a stand-up: one sit, not the ladder.
		static bool IsCombatIdleEvent(std::string_view a_tag)
		{
			return EqualsNoCase(a_tag, "tailCombatIdle") || EqualsNoCase(a_tag, "IdleCombatStart") || EqualsNoCase(a_tag, "IdleWebAlert") ||
				(a_tag.size() >= 12 && EqualsNoCase(a_tag.substr(0, 12), "combatStance"));
		}

		// 43.2.1: frame-to-frame smoothness, once a second when something jerked (else every 10 s).
		float jitPrev_[5]{}, jitPrev2_[5]{};
		bool jitHave_{ false };
		float jitMaxStep_[6]{};
		int jitJerks_{ 0 }, hJerks_{ 0 };
		float jitTimer_{ 0.0f }, jitQuiet_{ 0.0f };
		float lastGlueRide_{ -1.0f };
		bool glueTwiceLogged_{ false };
		int hullLagFrames_{ 0 };
		float hullLagCooldown_{ 0.0f };
		int hullBigLagFrames_{ 0 };
		float hullReplaceCooldown_{ 0.0f };
		bool hullCheckAfterLanding_{ false };
		float lastAirAt_{ -100.0f };
		void UpdateJit(RE::PlayerCharacter* a_player, float a_dt)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			RE::NiMatrix3 br;
			float pitch = 0.0f, roll = 0.0f;
			if (BoatBow::RigidRotation(br)) { pitch = std::asin(std::clamp(br.entry[2][1], -1.0f, 1.0f)); roll = std::asin(std::clamp(br.entry[2][0], -1.0f, 1.0f)); }
			const float now[5] = { a_player->GetPositionZ(), cam && cam->cameraRoot ? cam->cameraRoot->world.translate.z : 0.0f, vehiclePos_.z, pitch, roll };
			const bool flight = skimAirborne_ || skimOnRamp_;
			if (jitHave_) {
				for (int i = 0; i < 5; ++i) jitMaxStep_[i] = (std::max)(jitMaxStep_[i], std::abs(now[i] - jitPrev_[i]));
				jitMaxStep_[5] = (std::max)(jitMaxStep_[5], std::abs(skimSpeed_ - jitPrevSpeed_));
				const float d2z = std::abs(now[2] - 2.0f * jitPrev_[2] + jitPrev2_[2]);
				const float d2p = std::abs(now[3] - 2.0f * jitPrev_[3] + jitPrev2_[3]);
				const float d2r = std::abs(now[4] - 2.0f * jitPrev_[4] + jitPrev2_[4]);
				if (!flight && (d2z > 6.0f || d2p > 0.0262f || d2r > 0.0262f)) { ++jitJerks_; ++hJerks_; }
			}
			for (int i = 0; i < 5; ++i) { jitPrev2_[i] = jitHave_ ? jitPrev_[i] : now[i]; jitPrev_[i] = now[i]; }
			jitPrevSpeed_ = skimSpeed_;
			jitHave_ = true;
			if ((jitTimer_ += a_dt) < 1.0f) return;
			jitTimer_ = 0.0f;
			jitQuiet_ += 1.0f;
			const bool loud = jitJerks_ > 0 || jitMaxStep_[1] > 8.0f || jitMaxStep_[0] > 4.0f;
			if (loud || jitQuiet_ >= 10.0f) {
				jitQuiet_ = 0.0f;
				int touching = 0;
				for (bool t : feelTouch_) touching += t ? 1 : 0;
				std::string stage;
				auto add = [&](bool a_on, const char* a_name) { if (a_on) { stage += stage.empty() ? "" : "/"; stage += a_name; } };
				add(appliedLift_ > 0.5f, "groundv2");
				add(flowW_ > 0.05f, "flow");
				add(skimOnRamp_ || skimAirborne_, "ramp");
				add(touching > 0, "feel");
				add(contactSuspended_, "suspend");
				add(glueZeroTimer_ > 0.0f, "glue");
				logger::info("Jit: playerZ max-step {:.1f} camZ max-step {:.1f} vehicle max-step {:.1f} pitch step {:.2f} roll step {:.2f} speed step {:.1f} jerks {} | stage {}",
					jitMaxStep_[0], jitMaxStep_[1], jitMaxStep_[2], jitMaxStep_[3] * 57.2958f, jitMaxStep_[4] * 57.2958f, jitMaxStep_[5], jitJerks_, stage.empty() ? "-" : stage);
			}
			for (float& m : jitMaxStep_) m = 0.0f;
			jitJerks_ = 0;
		}
		float jitPrevSpeed_{ 0.0f };

		// 43.5: oas flowprobe: the game's own pull on a swimming player, once a second for 20 s.
		float flowProbeTimer_{ 0.0f };
		float flowProbeTick_{ 0.0f };
		RE::NiPoint3 flowProbeLast_{};
		void TendFlowProbe(RE::PlayerCharacter* a_player, float a_dt)
		{
			if (flowProbeTimer_ <= 0.0f || a_dt <= 0.0f) return;
			flowProbeTimer_ -= a_dt;
			if ((flowProbeTick_ += a_dt) < 1.0f) return;
			const RE::NiPoint3 pos = a_player->GetPosition();
			const RE::NiPoint3 d = (pos - flowProbeLast_) / flowProbeTick_;
			flowProbeTick_ = 0.0f;
			flowProbeLast_ = pos;
			RE::NiPoint3 lv{};
			a_player->GetLinearVelocity(lv);
			std::string nearest = "none";
			float best = 1.0e9f;
			if (auto* tes = RE::TES::GetSingleton()) {
				tes->ForEachReferenceInRange(a_player, 3000.0f, [&](RE::TESObjectREFR* a_ref) {
					std::string hay = RefHay(a_ref);
					std::string low = hay;
					if (!hay.empty() && FlowName(low)) {
						const float dist = (a_ref->GetPosition() - pos).Length();
						if (dist < best) { best = dist; nearest = hay.substr(0, hay.find(' ')); }
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			logger::info("Flow probe: player vel ({:.0f},{:.0f},{:.0f}) speed {:.0f} (engine vel ({:.0f},{:.0f},{:.0f})) pos ({:.0f},{:.0f},{:.0f}) swimming {} nearest {} dist {:.0f}",
				d.x, d.y, d.z, std::sqrt(d.x * d.x + d.y * d.y), lv.x, lv.y, lv.z, pos.x, pos.y, pos.z, a_player->AsActorState() && a_player->AsActorState()->IsSwimming() ? 1 : 0,
				nearest, best < 1.0e8f ? best : -1.0f);
		}

		// ===== Part 44 (v1.0.40): flow overrides terrain stops, rocky descents are a slide =====
		bool FlowOverride() const { return flowOn_ && (flowW_ >= 0.15f || rideTime_ < flowHoldUntil_); }
		float GroundAt(const RE::NiPoint3& a_p)
		{
			float g = -1.0e9f, land = 0.0f;
			auto* tes = RE::TES::GetSingleton();
			if (tes && tes->GetLandHeight(a_p, land) && std::isfinite(land)) g = land;
			float frac = 1.0f;
			const RE::NiPoint3 from{ a_p.x, a_p.y, vehiclePos_.z + 80.0f }, to{ a_p.x, a_p.y, vehiclePos_.z - 500.0f };
			if (SkimSweep(from, to, frac) && frac > 0.01f) g = (std::max)(g, from.z + (to.z - from.z) * frac);
			return g;
		}
		// Ground slope (radians) at a point and the downhill direction.
		float GroundSlope(const RE::NiPoint3& a_p, RE::NiPoint3* a_down = nullptr)
		{
			const float gx0 = GroundAt({ a_p.x - 40.0f, a_p.y, a_p.z }), gx1 = GroundAt({ a_p.x + 40.0f, a_p.y, a_p.z });
			const float gy0 = GroundAt({ a_p.x, a_p.y - 40.0f, a_p.z }), gy1 = GroundAt({ a_p.x, a_p.y + 40.0f, a_p.z });
			if (gx0 < -1.0e8f || gx1 < -1.0e8f || gy0 < -1.0e8f || gy1 < -1.0e8f) return 0.0f;
			const float dx = (gx1 - gx0) / 80.0f, dy = (gy1 - gy0) / 80.0f;
			const float g = std::sqrt(dx * dx + dy * dy);
			if (a_down) *a_down = g > 1.0e-4f ? RE::NiPoint3{ -dx / g, -dy / g, 0.0f } : RE::NiPoint3{};
			return std::atan(g);
		}
		// 44.2.4: real water for the descent's exit test: SkimWetAt, then the TES query, then a water-layer ray; at least 25 deep.
		bool DescentWater(const RE::NiPoint3& a_p, float a_ground, float& a_w)
		{
			float w = 0.0f;
			const float cap = vehiclePos_.z + 20.0f;  // never a plane far above the boat
			if (SkimWetAt(a_p, w) && w - a_ground >= 25.0f && w <= cap) { a_w = w; return true; }
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(a_p) : nullptr;
			if (tes && cell) {
				const float tw = tes->GetWaterHeight(a_p, cell);
				if (std::isfinite(tw) && std::abs(tw) < 1.0e6f && tw - a_ground >= 25.0f && tw <= cap) { a_w = tw; return true; }
			}
			if (WaterRayHeight(a_p, w) && w - a_ground >= 25.0f && w <= cap) { a_w = w; return true; }
			return false;
		}
		void EnterDescent(char a_reason, const RE::NiPoint3& a_at)
		{
			descent_ = true;
			skimOnRamp_ = true;
			skimOnLand_ = false;
			skimAirborne_ = false;
			skimRampTime_ = 4.0f;
			skimRampVz_ = 0.0f;
			descentTime_ = descentWaterT_ = descentFlatT_ = descentDetachT_ = descentRiseT_ = 0.0f;
			descentStopping_ = false;
			stuckTime_ = 0.0f;
			++hDescents_;
			logger::info("Descent: enter (reason {}) slope {:.1f} deg pos ({:.0f},{:.0f},{:.0f}) speed {:.0f} flow {:.2f}", a_reason, GroundSlope(a_at) * 57.2958f,
				a_at.x, a_at.y, a_at.z, skimSpeed_, flowW_);
		}
		// Called from the shore / blocked chain in SkimStep: true when the boat was put into the descent slide instead.
		// 47.4: the boat is under a water surface (a known patch or the engine's water above its waterline).
		bool UnderWaterSurface(const RE::NiPoint3& a_p, float& a_w)
		{
			const float line = a_p.z - kSkimEyeHeight + 5.0f;
			float w = 0.0f;
			if (SkimWaterAt(a_p, w) && w > line) { a_w = w; return true; }
			for (auto& pt : patches_) {
				const float dx = a_p.x - pt.cx, dy = a_p.y - pt.cy;
				if (dx * dx + dy * dy < pt.radius * pt.radius && pt.height > line && pt.height <= a_p.z + 320.0f) { a_w = pt.height; return true; }
			}
			return false;
		}
		bool TryDescentOrFlow(const RE::NiPoint3& a_next, bool a_wet, int a_why, float a_step)
		{
			if (!descentOn_) return false;
			if (float uw = 0.0f; UnderWaterSurface(vehiclePos_, uw)) return false;  // 47.4: under water: the pool path, not a slide
			if (skimSpeed_ < 25.0f && (a_why == 2 || a_why == 3)) stuckTime_ += a_step;
			// 44.1.3: in a stream the boat is pulled over the ground: a rim up to 60 within 300 is slid over.
			if (FlowOverride() && flowW_ >= 0.15f) {
				const float g0 = GroundAt(vehiclePos_);
				const RE::NiPoint3 fwd{ std::sin(skimHeading_), std::cos(skimHeading_), 0.0f };
				const float g1 = GroundAt(vehiclePos_ + fwd * 300.0f);
				const float base = (std::max)(g0, skimWaterHeight_);
				if (g1 < -1.0e8f || g1 - base <= 60.0f) { EnterDescent('f', a_next); return true; }
			}
			const float slope = GroundSlope(vehiclePos_);
			const bool recent = rideTime_ - lastFlowNearAt_ < 5.0f || rideTime_ - lastAirAt_ < 3.0f;
			if (!a_wet && slope > 0.1393f && recent) { EnterDescent('b', a_next); return true; }  // 8 degrees
			if (stuckTime_ >= 0.6f && slope > 0.0524f) { EnterDescent('c', a_next); return true; }  // 3 degrees
			return false;
		}
		// 44.2.2: the descent slide: ride the ground, accelerate down the steepest descent, never stop midway (140 floor).
		// The water exit of the descent (shared by 44.2.3 and 47.4).
		void DescentToWater(float a_w, RE::NiPoint3 a_next, const char* a_why)
		{
			const float impact = (std::max)(0.0f, -skimRampVz_);
			const float hardness = std::clamp(impact / 600.0f, 0.0f, 1.0f);
			descent_ = false;
			skimOnRamp_ = false;
			skimAirborne_ = false;
			skimOnLand_ = false;
			skimWaterHeight_ = a_w;
			skimDip_ = 0.0f;
			skimDipVel_ = -std::clamp(impact * 0.5f, 60.0f, 300.0f);
			StartPoolRise(a_w, vehiclePos_.z);
			a_next.z = a_w + kSkimEyeHeight + fallLift_ + poolRise_;
			skimLastWaterPos_ = a_next;
			skimFallExit_ = kFallExitGrace;
			skimLaunchCooldown_ = 0.8f;
			BoatBow::Splash((std::max)(0.2f, hardness));
			vehiclePos_ = a_next;
			logger::info("Descent: exit (water at {:.1f}, {}) after {:.1f} s, speed {:.0f}", a_w, a_why, descentTime_, skimSpeed_);
			NoteBoatLanding();
		}
		void DescentStep(float step, const RE::NiPoint3& a_forward)
		{
			descentTime_ += step;
			// 47.4: the descent never runs below a water surface.
			if (float uw = 0.0f; UnderWaterSurface(vehiclePos_, uw)) { DescentToWater(uw, vehiclePos_, "under the surface"); return; }
			RE::NiPoint3 down{};
			const float slope = GroundSlope(vehiclePos_, &down);
			// Weathervane to the stream, else downhill: 70 deg/s, 35 while steering.
			RE::NiPoint3 want = down;
			if (flowW_ > 0.05f && (flowVel_.x != 0.0f || flowVel_.y != 0.0f)) {
				const float l = std::sqrt(flowVel_.x * flowVel_.x + flowVel_.y * flowVel_.y);
				want = RE::NiPoint3{ flowVel_.x / l, flowVel_.y / l, 0.0f };
			}
			if (want.x != 0.0f || want.y != 0.0f) {
				const float steering = (inLeft_ || inRight_ || (!r3ZoomActive_ && std::abs(stickX_) > 0.3f)) ? 0.6109f : 1.2217f;
				const float diff = WrapAngle(std::atan2(want.x, want.y) - skimHeading_);
				skimHeading_ = WrapAngle(skimHeading_ + std::clamp(diff, -steering * step, steering * step));
			}
			const RE::NiPoint3 fwd{ std::sin(skimHeading_), std::cos(skimHeading_), 0.0f };
			// 47.2: the signed slope along the travel (+ = ground rising ahead): uphill slows the slide down to 0.
			const float signedSlope = std::atan(-std::tan(slope) * (down.x * fwd.x + down.y * fwd.y));
			skimSpeed_ += std::clamp(-900.0f * std::sin(signedSlope), -1400.0f, 1400.0f) * step;
			// 47.2: a wall ahead (over 25 deg within 150, or 60 of rise within 300): the slide ends against it.
			{
				const float g0 = GroundAt(vehiclePos_);
				const float g150 = GroundAt(vehiclePos_ + fwd * 150.0f), g300 = GroundAt(vehiclePos_ + fwd * 300.0f);
				const float rise150 = (g0 > -1.0e8f && g150 > -1.0e8f) ? g150 - g0 : 0.0f;
				const float rise300 = (g0 > -1.0e8f && g300 > -1.0e8f) ? g300 - g0 : 0.0f;
				if (rise150 > 70.0f || rise300 > 60.0f) {
					const float rise = (std::max)(rise150, rise300);
					++hWalls_;
					logger::info("Descent: wall ahead rise {:.0f} slope {:.1f}", rise, signedSlope * 57.2958f);
					logger::info("Descent: exit (wall ahead, rise {:.0f}) after {:.1f} s, speed {:.0f}", rise, descentTime_, skimSpeed_);
					descent_ = false;
					skimOnRamp_ = false;
					skimOnLand_ = true;
					skimLastWaterPos_ = vehiclePos_;
					feelStopTimer_ = kFeelStopTime;  // the Part 40.1 stop: ease-out over 0.2 s, no rebound
					feelStopFrom_ = skimSpeed_;
					return;
				}
			}
			skimSpeed_ *= std::exp(-P().coastDrag * 0.15f * step);
			if (flowW_ > 0.05f) {
				const float fv = flowVel_.x * fwd.x + flowVel_.y * fwd.y;
				if (fv > skimSpeed_) skimSpeed_ += (fv - skimSpeed_) * (std::min)(1.0f, 4.0f * step);
			}
			if (descentStopping_) {
				descentStopT_ += step;
				const float t = std::clamp(descentStopT_ / 1.2f, 0.0f, 1.0f);
				skimSpeed_ = (std::min)(skimSpeed_, descentStopFrom_ * (1.0f - t * t));
			} else if (signedSlope < -0.0873f) {
				skimSpeed_ = (std::max)(skimSpeed_, 140.0f);  // never stop midway on a real downhill (over 5 degrees), 47.2
			}
			skimSpeed_ = std::clamp(skimSpeed_, 0.0f, kFallMaxSpeed);
			RE::NiPoint3 next = vehiclePos_ + fwd * (skimSpeed_ * step);
			const float hx = skimSpeed_ * step;
			float groundZ = GroundAt(next);
			if (groundZ < -1.0e8f) { skimSpeed_ *= std::exp(-kSkimUnloadedDrag * step); return; }
			for (float ahead : { 70.0f, 140.0f }) groundZ = (std::max)(groundZ, GroundAt(next + fwd * ahead));
			const float rideLift = fallLiftOn_ ? kRampRideLift : 0.0f;
			const float curGround = vehiclePos_.z - kSkimEyeHeight - rideLift;
			// The ground falls away steeply: fly (only after 0.12 s, like the cascade rule).
			const float drop = curGround - groundZ;
			if (drop > (std::max)(6.0f, kRampDetachSlope * hx)) {
				if ((descentDetachT_ += step) >= 0.12f) {
					descent_ = false;
					skimOnRamp_ = false;
					skimAirborne_ = true;
					skimDropGrace_ = 0.0f;
					skimVerticalSpeed_ = std::clamp(skimRampVz_, -900.0f, 0.0f);
					skimRampTime_ = 4.0f;
					logger::info("Descent: exit (flying off) after {:.1f} s, speed {:.0f}", descentTime_, skimSpeed_);
					return;
				}
			} else {
				descentDetachT_ = 0.0f;
			}
			// 44.2.3: real water (25 deep) on a gentle slope for 0.4 s: a water landing.
			float w = 0.0f;
			if (slope < 0.1393f && DescentWater(next, groundZ, w)) {
				if ((descentWaterT_ += step) >= 0.4f) {
					DescentToWater(w, next, "found");
					return;
				}
			} else {
				descentWaterT_ = 0.0f;
			}
			// 44.2.3: flat and no stream for 1.5 s: ease out to a stop over 1.2 s, then beached.
			if (slope < 0.0524f && flowW_ <= 0.05f && !FlowOverride()) {
				if (!descentStopping_ && (descentFlatT_ += step) >= 1.5f) { descentStopping_ = true; descentStopFrom_ = skimSpeed_; descentStopT_ = 0.0f; }
			} else {
				descentFlatT_ = 0.0f;
				descentStopping_ = false;
			}
			if (descentStopping_ && descentStopT_ >= 1.2f) {
				descent_ = false;
				skimOnRamp_ = false;
				skimOnLand_ = true;
				skimSpeed_ = 0.0f;
				skimLastWaterPos_ = next;
				logger::info("Descent: exit (flat) after {:.1f} s", descentTime_);
			}
			// Follow the surface like the slope ride: stick on the way down, ease up on the way up.
			const float wantZ = groundZ + kSkimEyeHeight + rideLift;
			float newZ = wantZ < vehiclePos_.z ? wantZ : vehiclePos_.z + (wantZ - vehiclePos_.z) * (std::min)(1.0f, 25.0f * step);
			// 47.2: follow downhill only: up at most 25 per 100 travelled, vz at most +60, and never rising for more than 0.4 s.
			if (newZ > vehiclePos_.z) {
				const float up = (std::min)({ newZ - vehiclePos_.z, 0.25f * hx, 60.0f * step });
				descentRiseT_ += step;
				newZ = descentRiseT_ > 0.4f ? vehiclePos_.z : vehiclePos_.z + up;
			} else {
				descentRiseT_ = 0.0f;
			}
			const float dz = newZ - vehiclePos_.z;
			skimRampVz_ += (dz / step - skimRampVz_) * (std::min)(1.0f, 10.0f * step);
			if (hx > 0.01f) skimRampPitch_ += (std::atan2(dz, hx) - skimRampPitch_) * (std::min)(1.0f, 12.0f * step);
			next.z = newZ;
			vehiclePos_ = next;
			if ((descentLogCooldown_ -= step) <= 0.0f) {
				descentLogCooldown_ = 1.0f;
				logger::info("Descent: speed {:.0f} slope {:.1f} deg flow {:.2f}{}", skimSpeed_, slope * 57.2958f, flowW_, descentStopping_ ? " (stopping, flat)" : "");
			}
		}

		// =====================================================================
		// Part 41/42 (v1.0.38): water patches (B), current field (C), smooth cascades (D), ice (E) and ground v2 (A, in
		// UpdateGround). Every stage has its own switch (groundv2, waterpatch, flow, cascade, ice); off = the old code path.
		// =====================================================================

		struct WaterPatch { float height; float cx, cy; float radius; float lastSeen; };
		struct FlowSource { RE::FormID id; std::string name; RE::NiPoint3 pos; float dirX, dirY; RE::NiPoint3 lip; float lipWater; bool fall; float vLip; bool valid; float drop; bool fallsLine; RE::FormID keeperId; };
		// --- B: water patches. Every confirmed water sample is remembered by height, so a pool the engine's point query
		// calls "shore" (land at or above the cell water) still counts as water where we have seen its surface.
		void NotePatch(const RE::NiPoint3& a_pos, float a_h)
		{
			if (!waterPatchOn_ || !playerSkim_) return;
			for (auto& p : patches_) {
				const float dx = a_pos.x - p.cx, dy = a_pos.y - p.cy;
				const float d = std::sqrt(dx * dx + dy * dy);
				if (std::abs(p.height - a_h) < 8.0f && d < 1200.0f) {
					p.radius = std::clamp((std::max)(p.radius, d + 60.0f), 250.0f, 1200.0f);
					p.lastSeen = stageClock_;
					return;
				}
			}
			const WaterPatch np{ a_h, a_pos.x, a_pos.y, 250.0f, stageClock_ };
			if (patches_.size() < 12) patches_.push_back(np);
			else *std::min_element(patches_.begin(), patches_.end(), [](const WaterPatch& a, const WaterPatch& b) { return a.lastSeen < b.lastSeen; }) = np;
		}
		void ExpirePatches()
		{
			std::erase_if(patches_, [&](const WaterPatch& p) {
				const float dx = vehiclePos_.x - p.cx, dy = vehiclePos_.y - p.cy;
				return stageClock_ - p.lastSeen > 60.0f || dx * dx + dy * dy > 4000.0f * 4000.0f;
			});
		}
		// Downward ray that stops at the first water-layer hit (B.4 diagnostics only).
		bool WaterRayHeight(const RE::NiPoint3& a_pos, float& a_h)
		{
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(a_pos) : nullptr;
			auto* world = cell && cell->IsAttached() ? cell->GetbhkWorld() : nullptr;
			if (!world) return false;
			RE::NiPoint3 from{ a_pos.x, a_pos.y, vehiclePos_.z + 300.0f };
			const RE::NiPoint3 to{ a_pos.x, a_pos.y, vehiclePos_.z - 700.0f };
			const float scale = RE::bhkWorld::GetWorldScale();
			for (int k = 0; k < 4; ++k) {
				RE::bhkPickData pick{};
				pick.rayInput.from = RE::hkVector4(from * scale);
				pick.rayInput.to = RE::hkVector4(to * scale);
				pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kCameraPick);
				pick.rayOutput.Reset();
				world->PickObject(pick);
				if (!pick.rayOutput.HasHit()) return false;
				const float fr = std::clamp(pick.rayOutput.hitFraction, 0.0f, 1.0f);
				const float z = from.z + (to.z - from.z) * fr;
				if (pick.rayOutput.rootCollidable->GetCollisionLayer() == RE::COL_LAYER::kWater) { a_h = z; return true; }
				from.z = z - 2.0f;
				if (from.z <= to.z) return false;
			}
			return false;
		}
		bool InPatchRadius(const RE::NiPoint3& a_pos, const WaterPatch** a_out = nullptr)
		{
			for (auto& p : patches_) {
				const float dx = a_pos.x - p.cx, dy = a_pos.y - p.cy;
				if (dx * dx + dy * dy < p.radius * p.radius) { if (a_out) *a_out = &p; return true; }
			}
			return false;
		}
		// B.2: SkimWaterAt, or a remembered patch over this spot (the ground under its surface, not too deep, not far above
		// the boat). Floating or flying, a higher patch is never used, so the boat cannot jump up to it.
		bool SkimWetAt(const RE::NiPoint3& a_pos, float& a_height, int* a_why = nullptr)
		{
			int why = 0;
			if (SkimWaterAt(a_pos, a_height, &why)) { if (a_why) *a_why = 0; return true; }
			if (a_why) *a_why = why;
			if (!waterPatchOn_ || why == 1 || patches_.empty() || skimAirborne_) return false;
			auto* tes = RE::TES::GetSingleton();
			float land = 0.0f;
			if (!tes || !tes->GetLandHeight(a_pos, land) || !std::isfinite(land)) return false;
			const WaterPatch* best = nullptr;
			const float boatLine = vehiclePos_.z - kSkimEyeHeight;
			for (auto& p : patches_) {
				const float dx = a_pos.x - p.cx, dy = a_pos.y - p.cy;
				if (dx * dx + dy * dy >= p.radius * p.radius) continue;
				// 47.1: depth up to 1500, no depth limit at all for a patch confirmed by a real water query in the last 5 s.
				const bool fresh = stageClock_ - p.lastSeen < 5.0f;
				if (!(land < p.height - 2.0f && (fresh || p.height - land <= 1500.0f) && p.height <= vehiclePos_.z + 320.0f)) continue;
				if (!skimOnRamp_ && !skimOnLand_ && p.height > skimWaterHeight_ + 40.0f) continue;
				if (!best || std::abs(p.height - boatLine) < std::abs(best->height - boatLine)) best = &p;
			}
			if (!best) {
				if (why == 3) {
					// 44.4.2: only a shore call under a patch surface counts (land below it), with the reason the patch was not used.
					for (auto& p : patches_) {
						const float dx = a_pos.x - p.cx, dy = a_pos.y - p.cy;
						if (dx * dx + dy * dy >= p.radius * p.radius || !(land < p.height - 2.0f)) continue;
						++hShoreInWater_;
						if (poolSkipLogCooldown_ <= 0.0f) {
							poolSkipLogCooldown_ = 1.0f;
							const char* reason = (p.height - land > 1500.0f && stageClock_ - p.lastSeen >= 5.0f) ? "deeper than 1500 (not confirmed in 5 s)" : (p.height > vehiclePos_.z + 320.0f ? "over 320 above the boat" :
								(!skimOnRamp_ && !skimOnLand_ && p.height > skimWaterHeight_ + 40.0f ? "floating, patch higher than the boat's water" : "other"));
							logger::info("Water: patches {}, pool test skipped: {} (patch {:.1f}, land {:.1f}, boat z {:.1f})", patches_.size(), reason, p.height, land, vehiclePos_.z);
						}
						break;
					}
					ShoreDiag(a_pos, land, nullptr);
				}
				return false;
			}
			a_height = best->height;
			if (a_why) *a_why = 0;
			patchUsedAt_ = stageClock_;
			return true;
		}
		void ShoreDiag(const RE::NiPoint3& a_pos, float a_land, const WaterPatch* a_used)
		{
			if (waterDiagCooldown_ > 0.0f) return;
			const WaterPatch* p = a_used;
			if (!p) {
				for (auto& c : patches_) {
					const float dx = a_pos.x - c.cx, dy = a_pos.y - c.cy;
					if (dx * dx + dy * dy < 800.0f * 800.0f) { p = &c; break; }
				}
			}
			if (!p) return;
			waterDiagCooldown_ = 1.0f;
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(a_pos) : nullptr;
			float cw = 0.0f;
			const bool hasCw = cell && cell->GetWaterHeight(a_pos, cw) && std::isfinite(cw);
			const float tesW = tes ? tes->GetWaterHeight(a_pos, cell) : 0.0f;
			float rayW = 0.0f;
			const bool hasRay = WaterRayHeight(a_pos, rayW);
			logger::info("Water: at ({:.0f},{:.0f}) cell water {} cell-land {:.1f} patch {:.1f} (center {:.0f},{:.0f} radius {:.0f}) depth {:.1f} result {} | TES water {:.1f} water ray {}",
				a_pos.x, a_pos.y, hasCw ? std::format("{:.1f}", cw) : std::string("none"), a_land, p->height, p->cx, p->cy, p->radius, p->height - a_land,
				a_used ? "wet" : "shore", tesW, hasRay ? std::format("{:.1f}", rayW) : std::string("none"));
		}

		// --- C: current field. Named waterfall/rapids refs become flow sources with a downstream direction and a lip;
		// near one the boat gets a current velocity (flowVel_), is turned downstream and thrown off the lip.
		static bool FlowName(std::string& a_hay)
		{
			std::transform(a_hay.begin(), a_hay.end(), a_hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_hay.find("waterfall") != std::string::npos || a_hay.find("rapids") != std::string::npos ||
				a_hay.find("falls") != std::string::npos || a_hay.find("cascade") != std::string::npos;
		}
		static std::string RefHay(RE::TESObjectREFR* a_ref)
		{
			std::string hay;
			auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!base) return hay;
			if (auto* m = base->As<RE::TESModel>(); m && m->GetModel()) hay += m->GetModel();
			if (auto* root = a_ref->Get3D(); root && root->name.c_str()) { hay += ' '; hay += root->name.c_str(); }
			return hay;
		}
		float FlowH(const RE::NiPoint3& a_p)
		{
			float h = -1.0e9f, w = 0.0f, land = 0.0f;
			if (SkimWaterAt(a_p, w)) h = w;
			auto* tes = RE::TES::GetSingleton();
			if (tes && tes->GetLandHeight(a_p, land) && std::isfinite(land)) h = (std::max)(h, land);
			return h;
		}
		void BuildFlow(RE::TESObjectREFR* a_ref, std::string a_name)
		{
			FlowSource fs{};
			fs.id = a_ref->GetFormID();
			fs.name = std::move(a_name);
			fs.pos = a_ref->GetPosition();
			const float h0 = FlowH(fs.pos);
			auto dropAt = [&](float a_ang, float a_r) {
				const float h = FlowH(RE::NiPoint3{ fs.pos.x + std::sin(a_ang) * a_r, fs.pos.y + std::cos(a_ang) * a_r, fs.pos.z });
				return (h < -1.0e8f || h0 < -1.0e8f) ? -1.0e9f : h0 - h;
			};
			// 44.1.6: the downstream direction at 250 and at 500, each from 12 directions refined by +/-15, 7.5 and 3.75
			// degrees, averaged.
			auto search = [&](float a_r, float& a_drop) {
				float ang = 0.0f, best = -1.0e9f;
				for (int k = 0; k < 12; ++k) {
					const float a = static_cast<float>(k) * 0.5236f;
					const float d = dropAt(a, a_r);
					if (d > best) { best = d; ang = a; }
				}
				for (float step : { 0.2618f, 0.1309f, 0.0654f }) {
					const float base = ang;
					for (float da : { -step, step }) {
						const float d = dropAt(base + da, a_r);
						if (d > best) { best = d; ang = base + da; }
					}
				}
				a_drop = best;
				return ang;
			};
			float drop250 = 0.0f, drop500 = 0.0f;
			const float a250 = search(250.0f, drop250), a500 = search(500.0f, drop500);
			float bestAng = std::atan2(std::sin(a250) + std::sin(a500), std::cos(a250) + std::cos(a500));
			const float bestDrop = (std::max)(drop250, drop500);
			if (bestDrop < 40.0f) {
				// Rapids with no clear fall: the steepest descent over a 600 ring (44.1.6, was 400).
				float b2 = 0.0f;
				bestAng = search(600.0f, b2);
				fs.fall = false;
				fs.vLip = 420.0f;  // 43.5: above the rowing top speed (was 280)
			} else {
				fs.fall = bestDrop >= 80.0f;
				fs.vLip = fs.fall ? 600.0f : 420.0f;  // 43.5 (was 480 / 280)
			}
			fs.dirX = std::sin(bestAng);
			fs.dirY = std::cos(bestAng);
			fs.drop = bestDrop;
			// 47.7.1: the foam line where rapids meet a fall is rapids only (and only without a fall body near, see UpdateFlow).
			{
				std::string ln = fs.name;
				std::transform(ln.begin(), ln.end(), ln.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				fs.fallsLine = ln.find("fallsline") != std::string::npos;
				if (fs.fallsLine) { fs.fall = false; fs.vLip = 420.0f; }
			}
			fs.keeperId = 0;
			// C.3: the lip. March downstream (from 600 upstream of the source) until the water ends, the ground falls 80, or
			// a lower plane starts; the lip is the last wet point.
			fs.lip = fs.pos;
			fs.lipWater = h0;
			bool wasWet = false;
			float prevH = -1.0e9f, prevW = 0.0f;
			for (int k = 0; k <= 40; ++k) {
				const RE::NiPoint3 p{ fs.pos.x + fs.dirX * (-600.0f + 40.0f * k), fs.pos.y + fs.dirY * (-600.0f + 40.0f * k), fs.pos.z };
				float w = 0.0f;
				const bool wet = SkimWaterAt(p, w);
				const float h = FlowH(p);
				const bool fellAway = prevH > -1.0e8f && h > -1.0e8f && prevH - h >= 80.0f;
				const bool lower = wasWet && wet && prevW - w >= kSkimDropThreshold;
				if (wasWet && (!wet || fellAway || lower)) break;
				if (wet) { wasWet = true; prevW = w; fs.lip = p; fs.lipWater = w; }
				prevH = h;
			}
			fs.valid = true;
			logger::info("Flow: {} at ({:.0f},{:.0f},{:.0f}) dir {:.0f} deg drop {:.0f} kind {} lip at ({:.0f},{:.0f}) water {:.0f}", fs.name, fs.pos.x, fs.pos.y, fs.pos.z,
				bestAng * 57.2958f, bestDrop, fs.fall ? "fall" : "rapids", fs.lip.x, fs.lip.y, fs.lipWater);
			flows_.push_back(std::move(fs));
			// 47.7.3: two sources within 300 (a body and its skirt) are one fall: the larger drop keeps it, the other merges in.
			{
				FlowSource& nf = flows_.back();
				for (auto& o : flows_) {
					if (&o == &nf || o.keeperId != 0) continue;
					const float dx = o.pos.x - nf.pos.x, dy = o.pos.y - nf.pos.y;
					if (dx * dx + dy * dy > 300.0f * 300.0f) continue;
					if (o.drop >= nf.drop) nf.keeperId = o.id;
					else o.keeperId = nf.id;
					logger::info("Flow: {} and {} are one fall, kept {}", o.name, nf.name, o.drop >= nf.drop ? o.name : nf.name);
					break;
				}
			}
		}
		void ScanFlows()
		{
			auto* tes = RE::TES::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!tes || !player) return;
			std::vector<std::pair<RE::TESObjectREFR*, std::string>> found;
			tes->ForEachReferenceInRange(player, 1800.0f, [&](RE::TESObjectREFR* a_ref) {
				if (!a_ref || a_ref->IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;
				const RE::FormID id = a_ref->GetFormID();
				if (std::any_of(flows_.begin(), flows_.end(), [&](const FlowSource& f) { return f.id == id; })) return RE::BSContainer::ForEachResult::kContinue;
				std::string hay = RefHay(a_ref);
				if (hay.empty()) return RE::BSContainer::ForEachResult::kContinue;
				if (std::find(flowIgnoredIds_.begin(), flowIgnoredIds_.end(), id) != flowIgnoredIds_.end()) return RE::BSContainer::ForEachResult::kContinue;
				std::string low = hay;
				if (FlowName(low)) {
					// 47.7.1: the splash / skirt / foam / mist effects at the foot of a fall are visuals, never flow sources.
					std::string file = low.substr(0, low.find(' '));
					if (const auto sl = file.find_last_of("\\/"); sl != std::string::npos) file = file.substr(sl + 1);
					const std::string node = low.find(' ') != std::string::npos ? low.substr(low.find(' ') + 1) : std::string();
					bool effect = false;
					for (const char* bad : { "skirt", "splash", "churn", "ring", "foam", "mist", "spray", "salmon", "rocks", "fallstop", "falls top", "amb" }) {
						if (file.find(bad) != std::string::npos || node.find(bad) != std::string::npos) { effect = true; break; }
					}
					if (effect) {
						flowIgnoredIds_.push_back(id);
						if (std::find(flowIgnoredNames_.begin(), flowIgnoredNames_.end(), file) == flowIgnoredNames_.end()) {
							flowIgnoredNames_.push_back(file);
							logger::info("Flow: ignored effect {}", file);
						}
					} else {
						found.emplace_back(a_ref, hay);
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			for (auto& [ref, hay] : found) {
				if (flows_.size() >= 48) break;
				std::string name = hay.substr(0, hay.find(' '));
				if (const auto slash = name.find_last_of("\\/"); slash != std::string::npos) name = name.substr(slash + 1);
				BuildFlow(ref, name.empty() ? std::format("{:08X}", ref->GetFormID()) : name);
			}
			std::erase_if(flows_, [&](const FlowSource& f) {
				const float dx = f.pos.x - vehiclePos_.x, dy = f.pos.y - vehiclePos_.y;
				return dx * dx + dy * dy > 6000.0f * 6000.0f;
			});
		}
		static float Smooth(float a_e0, float a_e1, float a_x)
		{
			const float t = std::clamp((a_x - a_e0) / (a_e1 - a_e0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}
		// Called at the start of SkimStep: the pull, the weathervane, the rapids roll, and the throw at the lip.
		// 45 (v1.0.41): the pull's numbers, tunable with oas flow <name> <value>, kept in OarsAndSails.ini [Flow].
		struct FlowCfg { float pull = 180.0f, rapids = 80.0f, range = 1800.0f, curve = 1.6f, capture = 120.0f, turn = 45.0f, thr = 250.0f, thrMax = 300.0f, kick = 160.0f; };  // 54.1: Miguel's F: ini values
		FlowCfg flowCfg_{};
		static std::string FlowIniPath()
		{
			std::error_code ec;
			return (std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OarsAndSails.ini").string();
		}
		static constexpr std::pair<const char*, float FlowCfg::*> kFlowKeys[9] = { { "pull", &FlowCfg::pull }, { "rapids", &FlowCfg::rapids }, { "range", &FlowCfg::range },
			{ "curve", &FlowCfg::curve }, { "capture", &FlowCfg::capture }, { "turn", &FlowCfg::turn }, { "throw", &FlowCfg::thr }, { "throwmax", &FlowCfg::thrMax }, { "kick", &FlowCfg::kick } };
		void LoadFlowCfg()
		{
			const auto ini = FlowIniPath();
			feelRocksOn_ = GetPrivateProfileIntA("HullFeel", "Rocks", 1, ini.c_str()) != 0;  // 48.2
			for (auto& [key, mem] : kFlowKeys) {
				char buf[64]{};
				GetPrivateProfileStringA("Flow", key, "", buf, sizeof(buf), ini.c_str());
				if (buf[0]) { try { flowCfg_.*mem = std::stof(buf); } catch (...) {} }
			}
			logger::info("{}", FlowCfgLine());
		}
		void SaveFlowCfg()
		{
			const auto ini = FlowIniPath();
			for (auto& [key, mem] : kFlowKeys) WritePrivateProfileStringA("Flow", key, std::format("{:g}", flowCfg_.*mem).c_str(), ini.c_str());
		}
		std::string FlowCfgLine()
		{
			const auto& c = flowCfg_;
			return std::format("Flow cfg: {} pull {:g} rapids {:g} range {:g} curve {:g} capture {:g} turn {:g} throw {:g} throwmax {:g} kick {:g}", flowOn_ ? "on" : "off",
				c.pull, c.rapids, c.range, c.curve, c.capture, c.turn, c.thr, c.thrMax, c.kick);
		}
		// Console: oas flow | flow on|off | flow <name> <value> | flow reset | flow probe. Returns the text to print.
		std::string FlowCommand(const std::string& a_args)
		{
			std::istringstream in(a_args);
			std::string field;
			in >> field;
			if (field.empty()) {
				// 45 addendum / 46: bare "oas flow" shows everything, and turns the pull on if it was off.
				if (!flowOn_) { flowOn_ = true; logger::info("Flow: set on (oas flow)"); }
				return FlowCfgLine();
			}
			if (field == "on" || field == "off") {
				flowOn_ = field == "on";
				if (!flowOn_) { flowVel_ = {}; flowW_ = 0.0f; }
				logger::info("Flow: set {}", field);
				return FlowCfgLine();
			}
			if (field == "reset") {
				flowCfg_ = FlowCfg{};
				SaveFlowCfg();
				logger::info("Flow: reset to the defaults");
				return FlowCfgLine();
			}
			if (field == "probe") {
				flowProbeTimer_ = 20.0f;
				flowProbeTick_ = 0.0f;
				if (auto* pl = RE::PlayerCharacter::GetSingleton()) flowProbeLast_ = pl->GetPosition();
				logger::info("Flow probe: started (20 s)");
				return "flow probe running for 20 s (swim into the rapids)";
			}
			for (auto& [key, mem] : kFlowKeys) {
				if (field != key) continue;
				float v = 0.0f;
				if (!(in >> v)) return std::format("flow {} is {:g}", key, flowCfg_.*mem);
				const float was = flowCfg_.*mem;
				flowCfg_.*mem = std::clamp(v, 0.0f, 5000.0f);
				SaveFlowCfg();
				logger::info("Flow: set {} {:g} (was {:g})", key, flowCfg_.*mem, was);
				return std::format("Flow: {} {:g} (was {:g}){}", key, flowCfg_.*mem, was, flowOn_ ? "" : "  -- flow is OFF: type oas flow to turn it on");
			}
			return "flow: pull rapids range curve capture turn throw throwmax kick <value> | on | off | reset | probe";
		}

		// Called at the start of SkimStep: the pull, the weathervane, the rapids roll, and the throw at the lip.
		void UpdateFlow(float a_dt, float a_steer)
		{
			const auto t0 = std::chrono::steady_clock::now();
			const bool onWater = !skimAirborne_ && !skimOnRamp_ && !skimOnLand_;
			const auto& cfg = flowCfg_;
			float w = 0.0f;
			const FlowSource* src = nullptr;
			if (flowOn_) {
				if ((flowScanTimer_ -= a_dt) <= 0.0f) { flowScanTimer_ = 0.25f; ScanFlows(); }
				auto isBody = [](const FlowSource& a_f) {
					std::string ln = a_f.name;
					std::transform(ln.begin(), ln.end(), ln.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					return ln.find("waterfallbody") != std::string::npos || ln.find("waterfallthin") != std::string::npos;
				};
				for (auto& f : flows_) {
					if (!f.valid) continue;
					if (!skimAirborne_ && skimWaterHeight_ < f.lipWater - 60.0f) continue;  // below the fall: no pull back up there
					// 47.7.1: a foam line with a fall body within 800 is ignored.
					if (f.fallsLine && std::any_of(flows_.begin(), flows_.end(), [&](const FlowSource& o) {
						const float dx = o.pos.x - f.pos.x, dy = o.pos.y - f.pos.y;
						return o.valid && isBody(o) && dx * dx + dy * dy < 800.0f * 800.0f; })) continue;
					const float rx = vehiclePos_.x - f.lip.x, ry = vehiclePos_.y - f.lip.y;
					const float s = rx * f.dirX + ry * f.dirY;
					const float q = rx * f.dirY - ry * f.dirX;
					const float R = (std::max)(1.0f, f.fall ? cfg.range : cfg.range * 0.5f);  // 45.2 (rapids 50%)
					const float wUp = Smooth(R, 0.0f, -s);
					const float wDown = Smooth(400.0f, 150.0f, s);  // past the lip it fades (the pool below is not pulled on)
					const float wLat = f.fall ? Smooth(500.0f, 180.0f, std::abs(q)) : Smooth(350.0f, 200.0f, std::abs(q));
					const float wi = wUp * wDown * wLat;
					if (wi > w) {
						// 47.7.3: a merged source counts for its keeper (max, not a sum), measured from the keeper's lip.
						const FlowSource* keep = &f;
						if (f.keeperId) for (auto& k : flows_) if (k.id == f.keeperId && k.valid) { keep = &k; break; }
						w = wi;
						src = keep;
						const float kx = vehiclePos_.x - keep->lip.x, ky = vehiclePos_.y - keep->lip.y;
						flowS_ = kx * keep->dirX + ky * keep->dirY;
						flowQ_ = kx * keep->dirY - ky * keep->dirX;
					}
				}
			}
			const float prevW = flowW_;
			flowW_ = w;
			flowRapids_ = src && !src->fall;
			flowName_ = src ? src->name : std::string();
			if (w > 0.3f && prevW <= 0.3f) ++hFlowPulled_;
			// 45.4: leaving a zone after it had a real hold on the boat.
			flowPeakW_ = (std::max)(flowPeakW_, w);
			if (w <= 0.0f && prevW > 0.0f) {
				if (flowPeakW_ >= 0.5f) logger::info("Flow: escaped (w from {:.2f} to {:.2f})", flowPeakW_, w);
				flowPeakW_ = 0.0f;
			}
			// 45.2: the capture area: inside capture of a fall's lip the current rises to the throw speed (no rowing out).
			const bool captured = src && src->fall && cfg.capture > 0.0f && flowS_ >= -cfg.capture && w > 0.15f;
			// The current velocity: max * w^curve downstream, plus a drift toward the stream line. Rowing against it nets the
			// difference (one velocity, no extra drag).
			RE::NiPoint3 target{};
			const float landingGrace = Smooth(0.0f, 2.0f, rideTime_ - lastLandingAt_);
			float dirX = src ? src->dirX : 0.0f, dirY = src ? src->dirY : 0.0f;
			float pullScale = 1.0f;
			if (src && !skimAirborne_ && w > 0.0f) {
				// 47.3: a source with no known fall (drop under 40) pulls along the local downhill, at 0.6.
				if (src->drop < 40.0f) {
					RE::NiPoint3 dn{};
					if (GroundSlope(vehiclePos_, &dn) > 0.01f && (dn.x != 0.0f || dn.y != 0.0f)) { dirX = dn.x; dirY = dn.y; }
					pullScale = 0.6f;
				}
				// 47.3: never uphill. Ground (or water) at the boat and 200 ahead along dir and +/-45 degrees.
				const float h0 = FlowH(vehiclePos_);
				if (h0 > -1.0e8f) {
					float bestRise = 1.0e9f, bx = dirX, by = dirY;
					float riseAlong = 0.0f;
					for (int k = -1; k <= 1; ++k) {
						const float a = static_cast<float>(k) * 0.7854f, ca = std::cos(a), sa = std::sin(a);
						const float rx = dirX * ca + dirY * sa, ry = -dirX * sa + dirY * ca;
						const float h = FlowH(RE::NiPoint3{ vehiclePos_.x + rx * 200.0f, vehiclePos_.y + ry * 200.0f, vehiclePos_.z });
						const float rise = h > -1.0e8f ? h - h0 : 0.0f;
						if (k == 0) riseAlong = rise;
						if (rise < bestRise) { bestRise = rise; bx = rx; by = ry; }
					}
					if (riseAlong > 40.0f) {
						if (bestRise > 40.0f) {
							pullScale = 0.0f;
							++hUphill_;
							if (rideTime_ - uphillLogAt_ >= 2.0f) {
								uphillLogAt_ = rideTime_;
								logger::info("Flow: pull ignored, uphill (rise {:.0f}) source {}", riseAlong, src->name);
							}
						} else {
							dirX = bx;
							dirY = by;
						}
					}
				}
				// 47.7.2: no pull right after a landing, back to full over 2 s.
				pullScale *= landingGrace;
				float v = (src->fall ? cfg.pull : cfg.rapids) * std::pow(w, (std::max)(0.1f, cfg.curve)) * pullScale;
				if (captured && pullScale > 0.0f) v += ((std::max)(v, cfg.thr) - v) * Smooth(-cfg.capture, 0.0f, flowS_);
				const float c = std::clamp(-flowQ_ * 1.2f, -140.0f, 140.0f) * w * (pullScale > 0.0f ? 1.0f : 0.0f);
				target = RE::NiPoint3{ dirX * v + dirY * c, dirY * v - dirX * c, 0.0f };
			}
			const float tau = skimAirborne_ ? 0.13f : 0.5f;  // in the air it is gone in about 0.4 s
			const float k = 1.0f - std::exp(-a_dt / tau);
			flowVel_.x += (target.x - flowVel_.x) * k;
			flowVel_.y += (target.y - flowVel_.y) * k;
			if (flowVel_.x * flowVel_.x + flowVel_.y * flowVel_.y < 0.25f) flowVel_ = {};
			// Weathervane (45.2): only while the player is idle (no W/S/A/D or stroke for 1 s), turn deg/s * w^2.
			const bool idle = rideTime_ - lastInputAt_ >= 1.0f;
			if (src && onWater && w > 0.05f && idle && a_steer == 0.0f && pullScale > 0.0f) {
				const float want = std::atan2(dirX, dirY);
				const float diff = WrapAngle(want - skimHeading_);
				const float rate = cfg.turn * 0.0174533f * w * w;
				skimHeading_ = WrapAngle(skimHeading_ + std::clamp(diff, -rate * a_dt, rate * a_dt));
			}
			// Rapids: a little roll turbulence.
			if (flowRapids_ && onWater && w > 0.05f) {
				flowPhase_ += a_dt * 0.7f * 6.2832f;
				flowRoll_ = 0.0262f * w * std::sin(flowPhase_);
			} else {
				flowRoll_ *= std::exp(-4.0f * a_dt);
			}
			// 44.1: the flow hold (w >= 0.15 and 1 s after) overrides every terrain stop.
			if (flowOn_ && w >= 0.15f) flowHoldUntil_ = rideTime_ + 1.0f;
			for (auto& f : flows_) {
				const float dx = f.pos.x - vehiclePos_.x, dy = f.pos.y - vehiclePos_.y;
				if (dx * dx + dy * dy < 1500.0f * 1500.0f) { lastFlowNearAt_ = rideTime_; break; }
			}
			flowSlowTime_ = (src && flowS_ >= -120.0f && skimSpeed_ < 80.0f && !skimAirborne_) ? flowSlowTime_ + a_dt : 0.0f;
			flowBlockedTime_ = (rideTime_ - lastBlockedAt_ < 0.1f) ? flowBlockedTime_ + a_dt : 0.0f;
			// 45.2: the throw only inside the capture area, or forced when blocked / stopped 0.3 s right at the lip with w >= 0.6.
			const bool atLip = captured && flowS_ >= -20.0f && flowS_ <= 60.0f;
			const bool stuckAtLip = src && src->fall && w >= 0.6f && std::abs(flowS_) <= 120.0f && (flowSlowTime_ >= 0.3f || flowBlockedTime_ >= 0.3f);
			// 47.7.2: no new throw within 2 s of a landing, unless the boat is on a slope / descent over the lip.
			const bool throwArmed = rideTime_ - lastLandingAt_ >= 2.0f || skimOnRamp_;
			if (!skimAirborne_ && throwArmed && (atLip || stuckAtLip) && skimLaunchCooldown_ <= 0.0f) {
				if (stuckAtLip && !atLip) logger::info("Flow: throw at lip (forced, blocked {:.1f} s, slow {:.1f} s)", flowBlockedTime_, flowSlowTime_);
				skimOnLand_ = false;
				skimOnRamp_ = false;
				descent_ = false;
				skimReverse_ = 0.0f;
				skimLaunchCooldown_ = 0.8f;
				skimAirborne_ = true;
				skimDropGrace_ = kSkimDropGrace;
				skimRampTime_ = 4.0f;
				CrewBump("thrown off a waterfall");
				FlowThrow();
			}
			if (w > 0.05f && (flowLogCooldown_ -= a_dt) <= 0.0f) {
				flowLogCooldown_ = 0.5f;
				logger::info("Flow: {} w {:.2f} s {:.0f} q {:.0f} flow {:.0f} heading {:.0f} capture {} (rowing {}, speed {:.0f})", flowName_, w, flowS_, flowQ_,
					std::sqrt(flowVel_.x * flowVel_.x + flowVel_.y * flowVel_.y), skimHeading_ * 57.2958f, captured ? "Y" : "N", rowWantAnim_ ? 1 : 0, skimSpeed_);
			}
			costFlowMs_ += std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
		}
		// C.5 / 45.2: leaving a lip: horizontal = clamp(ground speed, throw, throwmax), upward kick (no speed term).
		void FlowThrow()
		{
			if (!flowOn_ || flowW_ <= 0.05f) return;
			const RE::NiPoint3 fwd{ std::sin(skimHeading_), std::cos(skimHeading_), 0.0f };
			const float vx = fwd.x * skimSpeed_ + flowVel_.x, vy = fwd.y * skimSpeed_ + flowVel_.y;
			const float lo = flowCfg_.thr, hi = (std::max)(flowCfg_.thr, flowCfg_.thrMax);
			const float horizontal = std::clamp(std::sqrt(vx * vx + vy * vy), lo, hi);
			if (vx * vx + vy * vy > 1.0f) skimHeading_ = std::atan2(vx, vy);
			skimSpeed_ = horizontal;
			skimVerticalSpeed_ = flowCfg_.kick;
			flowVel_ = {};
			BoatBow::Impact(0.3f);
			++hFlowThrown_;
			logger::info("Flow: throw at lip speed {:.0f} vz {:.0f} ({})", horizontal, skimVerticalSpeed_, flowName_);
		}

		// --- E: ice sheets (Sea of Ghosts). Ice refs are never obstacles; the hull rides on their tops, water-like.
		void LoadIceHints()
		{
			if (iceHintsLoaded_) return;
			iceHintsLoaded_ = true;
			char buf[1024]{};
			std::error_code ec;
			const auto ini = std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OarsAndSails.ini";
			GetPrivateProfileStringA("Ice", "Hints", "icefloe,ice_floe,floe,iceberg,icesheet,ice_sheet,frozen", buf, sizeof(buf), ini.string().c_str());
			std::string all(buf);
			std::transform(all.begin(), all.end(), all.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::stringstream in(all);
			std::string item;
			while (std::getline(in, item, ',')) {
				while (!item.empty() && item.front() == ' ') item.erase(item.begin());
				while (!item.empty() && item.back() == ' ') item.pop_back();
				if (!item.empty()) iceHints_.push_back(item);
			}
			logger::info("Ice: {} name hints", iceHints_.size());
		}
		bool IsIceRef(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref || !iceOn_) return false;
			const RE::FormID id = a_ref->GetFormID();
			if (auto it = iceCache_.find(id); it != iceCache_.end()) return it->second;
			LoadIceHints();
			std::string hay = RefHay(a_ref);
			std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			bool ice = false;
			for (auto& h : iceHints_) if (hay.find(h) != std::string::npos) { ice = true; break; }
			if (!ice && hay.find("ice") != std::string::npos) {
				for (const char* s : { "floe", "sheet", "berg", "plate" }) if (hay.find(s) != std::string::npos) { ice = true; break; }
			}
			if (iceCache_.size() > 4096) iceCache_.clear();
			iceCache_[id] = ice;
			return ice;
		}
		// E.3: the top of an ice ref under this point, within 90 of the water.
		bool IceTopAt(const RE::NiPoint3& a_pos, float a_water, float& a_top, RE::TESObjectREFR** a_ref)
		{
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(a_pos) : nullptr;
			auto* world = cell && cell->IsAttached() ? cell->GetbhkWorld() : nullptr;
			if (!world) return false;
			const RE::NiPoint3 from{ a_pos.x, a_pos.y, a_water + 120.0f }, to{ a_pos.x, a_pos.y, a_water - 60.0f };
			const float scale = RE::bhkWorld::GetWorldScale();
			RE::bhkPickData pick{};
			pick.rayInput.from = RE::hkVector4(from * scale);
			pick.rayInput.to = RE::hkVector4(to * scale);
			pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kCameraPick);
			pick.rayOutput.Reset();
			world->PickObject(pick);
			if (!pick.rayOutput.HasHit() || !pick.rayOutput.rootCollidable) return false;
			auto* ref = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
			if (!IsIceRef(ref)) return false;
			a_top = from.z + (to.z - from.z) * std::clamp(pick.rayOutput.hitFraction, 0.0f, 1.0f);
			if (a_top - a_water > 90.0f) return false;
			if (a_ref) *a_ref = ref;
			return true;
		}
		void IceList()
		{
			auto* tes = RE::TES::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!tes || !player) return;
			int n = 0;
			tes->ForEachReferenceInRange(player, 1500.0f, [&](RE::TESObjectREFR* a_ref) {
				auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
				if (!base || (base->GetFormType() != RE::FormType::Static && base->GetFormType() != RE::FormType::Activator && base->GetFormType() != RE::FormType::MovableStatic))
					return RE::BSContainer::ForEachResult::kContinue;
				auto* root = a_ref->Get3D();
				const float top = root ? root->worldBound.center.z + root->worldBound.radius : a_ref->GetPositionZ();
				// The collision layer seen by a ray straight down onto the ref's centre.
				std::string layer = "n/a";
				{
					const RE::NiPoint3 c = root ? root->worldBound.center : a_ref->GetPosition();
					auto* cell = tes->GetCell(c);
					auto* world = cell && cell->IsAttached() ? cell->GetbhkWorld() : nullptr;
					if (world) {
						const float scale = RE::bhkWorld::GetWorldScale();
						RE::bhkPickData pick{};
						pick.rayInput.from = RE::hkVector4(RE::NiPoint3{ c.x, c.y, top + 50.0f } * scale);
						pick.rayInput.to = RE::hkVector4(RE::NiPoint3{ c.x, c.y, top - 1000.0f } * scale);
						pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kCameraPick);
						pick.rayOutput.Reset();
						world->PickObject(pick);
						if (pick.rayOutput.HasHit() && pick.rayOutput.rootCollidable &&
							RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable) == a_ref)
							layer = std::to_string(static_cast<int>(pick.rayOutput.rootCollidable->GetCollisionLayer()));
					}
				}
				logger::info("Ice list: {:08X} model '{}' node '{}' layer {} top {:.0f} ice={}", a_ref->GetFormID(), RefHay(a_ref).substr(0, RefHay(a_ref).find(' ')),
					root && root->name.c_str() ? root->name.c_str() : "", layer, top, IsIceRef(a_ref));
				++n;
				return RE::BSContainer::ForEachResult::kContinue;
			});
			logger::info("Ice list: {} statics within 1500", n);
		}

		// --- 42: the per-session logs. Stages line, 1 s summary, 30 s health, cost.
		std::string StagesLine()
		{
			return std::format("Stages: groundv2={} waterpatch={} flow={} cascade={} ice={} hullfeel={}", groundV2On_ ? "on" : "off", waterPatchOn_ ? "on" : "off",
				flowOn_ ? "on" : "off", cascadeOn_ ? "on" : "off", iceOn_ ? "on" : "off", hullFeelMode_ == 2 ? "on" : (hullFeelMode_ == 1 ? "log" : "off"));
		}
		void LogHealth()
		{
			int feelHits = 0;
			for (int h : feelHits_) feelHits += h;
			logger::info("Health: rocks {} replaced {} hops {} cellswaps {} | walls {} uphill-ignored {} | jumps {} descents {} | jerks {} | groundv2 snaps {} | waterpatch pools {} shore-in-water {} patches {} | flow pulled {} thrown {} sources {} | cascade touches {} events {} | ice sheets {} | feelers react {} ignored-terrain {} | glue drift {}",
				hRocks_, BoatBow::ReplacedCount(), BoatBow::HopCount(), BoatBow::CellSwapCount(), hWalls_, hUphill_, hJumps_, hDescents_, hJerks_, hSnaps_, hPools_, hShoreInWater_, patches_.size(), hFlowPulled_, hFlowThrown_, flows_.size(), hCascadeTouches_, hCascadeEvents_, iceSheets_.size(),
				hFeelReact_, hFeelTerrain_, hGlueDrift_);
		}
		void UpdateStageLogs(float a_dt)
		{
			stageClock_ += a_dt;
			waterDiagCooldown_ = (std::max)(0.0f, waterDiagCooldown_ - a_dt);
			poolSkipLogCooldown_ = (std::max)(0.0f, poolSkipLogCooldown_ - a_dt);
			if ((patchExpireTimer_ -= a_dt) <= 0.0f) { patchExpireTimer_ = 1.0f; ExpirePatches(); }
			// D.4: falling or sliding, once a second.
			if ((skimAirborne_ || skimOnRamp_) && (fallLogCooldown_ -= a_dt) <= 0.0f) {
				fallLogCooldown_ = 1.0f;
				logger::info("Fall: {} (vz {:.0f}, dt since last event {:.1f})", skimAirborne_ ? "flying" : "sliding", skimAirborne_ ? skimVerticalSpeed_ : skimRampVz_, rideTime_ - lastFallEventAt_);
			}
			if ((sumTimer_ += a_dt) >= 1.0f) {
				sumTimer_ = 0.0f;
				int touching = 0;
				for (bool t : feelTouch_) touching += t ? 1 : 0;
				const bool patch = stageClock_ - patchUsedAt_ < 1.0f;
				const std::string key = std::format("{:.0f}|{:.0f}|{:.1f}|{}|{}|{}|{}|{:.0f}|{:.0f}|{}", skimSpeed_ / 10.0f, vehiclePos_.z / 10.0f, flowW_, skimOnLand_, patch, onIce_,
					skimAirborne_ || skimOnRamp_, appliedLift_, groundPitch_ * 57.2958f, touching);
				if (key != sumKey_) { sumKey_ = key; sumSame_ = 0.0f; }
				else sumSame_ += 1.0f;
				if (sumSame_ < 5.0f) {
					logger::info("Sum: speed {:.0f} z {:.0f} flow {:.2f}({}) {} patch {} ice {} airborne {} descent {} lift {:.1f} pitch {:.1f} roll {:.1f} feelers {} stop {:.2f} soft {}",
						skimSpeed_, vehiclePos_.z, flowW_, flowName_.empty() ? "-" : flowName_, skimOnLand_ ? "dry" : "wet", patch ? "Y" : "N", onIce_ ? "Y" : "N",
						skimAirborne_ ? "Y" : (skimOnRamp_ ? "slope" : "N"), descent_ ? "Y" : "N", appliedLift_, groundPitch_ * 57.2958f, (groundRoll_ + flowRoll_) * 57.2958f, touching, feelStopTimer_, softContacts_);
					softContacts_ = 0;
				}
			}
			if ((healthTimer_ += a_dt) >= 30.0f) { healthTimer_ = 0.0f; LogHealth(); }
			++costFrames_;
			if ((costTimer_ += a_dt) >= 10.0f) {
				logger::info("Cost: flow {:.3f} ground {:.3f} ms/frame (patches {}, flow sources {})", costFlowMs_ / static_cast<float>((std::max)(1, costFrames_)),
					costGroundMs_ / static_cast<float>((std::max)(1, costFrames_)), patches_.size(), flows_.size());
				costTimer_ = costFlowMs_ = costGroundMs_ = 0.0f;
				costFrames_ = 0;
			}
		}

		// Contact auto-off. Looks for a waterfall now, speed x 1 s and speed x 2 s ahead, plus the existing drop-ahead and
		// falls-current checks. Any hit suspends the fake collision (no lift, tilt or contact reactions; the waterfall, drop,
		// ramp and airborne logic run as they always did). It resumes once nothing has been seen for kContactResumeDelay and
		// the boat is not falling or on a slope, easing back in over kContactResumeTime. `oas contact` off overrides all this.
		void UpdateContactSuspend(float a_dt)
		{
			if (!contactOn_) { contactSuspended_ = false; contactScale_ = 1.0f; return; }
			const RE::NiPoint3 fwd{ std::sin(skimHeading_), std::cos(skimHeading_), 0.0f };
			if ((lookTimer_ -= a_dt) <= 0.0f) {
				lookTimer_ = kLookAheadInterval;
				lookHit_ = -1.0f;
				const float d1 = std::clamp(skimSpeed_ * 1.0f, kLookAheadMin, kLookAheadMax);
				const float d2 = std::clamp(skimSpeed_ * 2.0f, kLookAheadMin, kLookAheadMax);
				for (float d : { 0.0f, d1, d2 }) {
					if (SkimWaterfallNear(vehiclePos_ + fwd * d)) { lookHit_ = d; break; }
				}
			}
			float lower = 0.0f;
			const bool drop = !skimAirborne_ && !skimOnLand_ && SkimDropAhead(vehiclePos_, fwd, skimSpeed_, lower);
			if (lookHit_ >= 0.0f || drop || skimFlowAhead_ || flowW_ > 0.05f) {
				contactQuiet_ = 0.0f;
				if (!contactSuspended_) {
					contactSuspended_ = true;
					if (lookHit_ >= 0.0f) logger::info("Contact suspended: waterfall ahead at {:.0f} units", lookHit_);
					else logger::info("Contact suspended: {} ahead", drop ? "a drop" : "the current of a fall");
				}
			} else if (contactSuspended_) {
				if (skimOnRamp_ || skimAirborne_ || skimRampTime_ > 0.0f) contactQuiet_ = 0.0f;
				else contactQuiet_ += a_dt;
				if (contactQuiet_ >= kContactResumeDelay) {
					contactSuspended_ = false;
					contactScale_ = 0.0f;
					logger::info("Contact resumed (nothing ahead for {:.1f} s)", kContactResumeDelay);
				}
			}
			if (!contactSuspended_) contactScale_ = (std::min)(1.0f, contactScale_ + a_dt / kContactResumeTime);
		}

		// Fake collision, stage A (v1.0.3 fix). Samples the ground under five hull points (centre, bow, stern, starboard,
		// port). The hull bottom sits hullDraft_ below the waterline, so a point touches once the ground there is higher than
		// that. A point touches / clears only after kDryHysteresis, so the count cannot flicker. While touching, the boat
		// tilts to the ground (plane fit, clamped, eased) and is lifted, at most kMaxGroundLift, so the hull clears the
		// samples. Banks are obstacles, not beaches: ground more than kBeachMaxRise above the water anywhere under the hull
		// switches the lift and tilt off, a sample steeper than 25 degrees from the next one inward is ignored, and a bank at
		// the bow stops the boat gently. The lift is applied after SkimStep and taken off again before it, so the skim
		// physics (sweeps, launches) never see it. Open water and waterfalls are left exactly as they were.
		void UpdateGround(float a_dt)
		{
			const auto t0 = std::chrono::steady_clock::now();
			const float liftBefore = appliedLift_, pitchBefore = groundPitch_;
			UpdateGroundImpl(a_dt);
			// 42.4: a snap is a frame where the lift moved more than 25 or the pitch more than 8 degrees.
			if (std::abs(appliedLift_ - liftBefore) > 25.0f || std::abs(groundPitch_ - pitchBefore) > 0.1396f) ++hSnaps_;
			costGroundMs_ += std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
		}
		void UpdateGroundImpl(float a_dt)
		{
			const float kp = 1.0f - std::exp(-kGroundTiltRate * a_dt);
			const float kr = 1.0f - std::exp(-kGroundRollRate * a_dt);
			float targetPitch = 0.0f, targetRoll = 0.0f, liftTarget = 0.0f;
			bool grounded = false, bankAhead = false, capped = false;
			int dry = 0, rejected = 0, shallow = 0;
			bool tooHighNow = false, basin = false;
			// Just launched / landed, pinned, or ghosting free: no lift or tilt (v1.0.4b).
			const bool inAir = skimAirborne_ || skimOnRamp_;
			if (wasInAir_ && !inAir) postLandTimer_ = kPostLandHold;
			wasInAir_ = inAir;
			postLandTimer_ = (std::max)(0.0f, postLandTimer_ - a_dt);
			const bool pinnedHold = skimFallExit_ > 0.0f || skimBlockedTime_ > 0.0f || postLandTimer_ > 0.0f;
			RE::NiPoint3 bowPos{};
			waterfallTimer_ -= a_dt;
			UpdateContactSuspend(a_dt);
			const float sh = std::sin(skimHeading_), ch = std::cos(skimHeading_);
			// 39 (v1.0.37): seven samples, from the hull extents (one source of truth with the 38A feelers): centre, bow tip,
			// stern, starboard, port, and the two bow quarters, so the tip and the bow corners can no longer dig in.
			static constexpr int kN = kGroundSamples;
			struct HullPoint { float right, fwd; };
			const float tipFwd = HullBowTip() - 10.0f, sternFwd = -(HullStern() - 10.0f), side = HullHalfWidth() - 10.0f;
			const HullPoint kHull[kN] = { { 0.0f, 0.0f }, { 0.0f, tipFwd }, { 0.0f, sternFwd }, { side, 0.0f }, { -side, 0.0f },
				{ 0.5f * HullHalfWidth(), 0.8f * HullBowTip() }, { -0.5f * HullHalfWidth(), 0.8f * HullBowTip() } };
			float diagLand[kN]{}, diagWater[kN]{}, diagReq[kN]{};
			bool diagOk[kN]{};
			bool tipSeen = false;
			int iceN = 0, iceIdx = -1;
			bool iceCentre = false;
			float iceTop = 0.0f;
			RE::TESObjectREFR* iceRef = nullptr;
			bool govSteep = false;
			const bool v2 = groundV2On_;
			float req[kN]{}, surf[kN]{}, sr[kN]{}, sf[kN]{};
			bool ok[kN]{};
			if (contactOn_ && !contactSuspended_ && !skimAirborne_ && !skimOnRamp_ && !pinnedHold) {
				float cx = 0.0f, cy = 0.0f;
				BoatBow::BoatCenterFromVehicle(0.0f, 0.0f, 0.0f, cx, cy);  // hull centre: right / forward of the vehicle point
				bool wetRaw[kN]{}, touchRaw[kN]{};
				auto* tes = RE::TES::GetSingleton();
				for (int i = 0; i < kN; ++i) {
					sr[i] = cx + kHull[i].right;
					sf[i] = cy + kHull[i].fwd;
					const RE::NiPoint3 pt{ vehiclePos_.x + ch * sr[i] + sh * sf[i], vehiclePos_.y - sh * sr[i] + ch * sf[i], vehiclePos_.z };
					if (i == 1) bowPos = pt;  // 39.4: the tip
					float w = 0.0f, land = 0.0f;
					int why = 0;
					const bool wet = SkimWetAt(pt, w, &why);
					const bool landOk = why != 1 && tes && tes->GetLandHeight(pt, land) && std::isfinite(land);
					if (!wet && !landOk) continue;
					const float water = wet ? w : skimWaterHeight_;
					diagLand[i] = landOk ? land : -99999.0f;
					diagWater[i] = water;
					if (i == 1) tipSeen = landOk;
					// 41E.3: an ice sheet under this sample: ride its top, water-like (never dry, never a bank).
					if (iceOn_ && wet) {
						float top = 0.0f;
						RE::TESObjectREFR* iref = nullptr;
						if (IceTopAt(pt, water, top, &iref)) {
							req[i] = (std::max)(water, top + hullDraft_);
							surf[i] = top;
							ok[i] = true;
							wetRaw[i] = true;
							touchRaw[i] = true;
							if (iceN++ == 0) { iceIdx = i; iceTop = top; iceRef = iref; }
							if (i == 0) iceCentre = true;
							continue;
						}
					}
					// 40.1.5: a bank is hysteretic: it starts above kBeachMaxRise and ends only below 32 for 0.3 s.
					const float rise = landOk ? land - water : -1.0e9f;
					if (bankState_[i]) {
						if (rise < 32.0f) { if ((bankClearTimer_[i] += a_dt) >= 0.3f) { bankState_[i] = false; bankClearTimer_[i] = 0.0f; } }
						else bankClearTimer_[i] = 0.0f;
					} else if (rise > kBeachMaxRise) {
						bankState_[i] = true;
						bankClearTimer_[i] = 0.0f;
					}
					if (bankState_[i] && v2) {
						// 41A.3: a bank is not a switch: it stays in the fit and the lift, the governor slows the boat.
						bankAhead = bankAhead || i == 1 || i == 5 || i == 6;
					} else if (bankState_[i]) {
						// A bank or cliff under the hull: no lift, no tilt from it. At the tip or a bow quarter it is an obstacle ahead.
						bankAhead = bankAhead || i == 1 || i == 5 || i == 6;
						++rejected;
						tooHighNow = true;
						continue;
					}
					// Dry for the lift and tilt only when the ground is kGroundDryRise above the water: gravel bars and
					// shallow rock a few units high count as water (the beach glide keeps its own 4 unit test).
					const bool dryFit = landOk && land >= water + kGroundDryRise;
					if (landOk && !wet && !dryFit) ++shallow;
					// What the waterline (the reference the boat floats at) must be at this point: at least the water, and
					// high enough that the hull bottom (hullDraft_ below it) sits no more than the allowance into the ground.
					const float keel = dryFit ? land + hullDraft_ - kGroundLiftAllowance : -1.0e9f;
					req[i] = (std::max)(water, keel);
					surf[i] = dryFit ? land : water;
					ok[i] = true;
					wetRaw[i] = !dryFit;
					touchRaw[i] = dryFit;
				}
				// Steep step from the centre: a bank face, not a slope the hull can rest on.
				if (ok[0]) {
					for (int i = 1; i < kN; ++i) {
						if (!ok[i]) continue;
						const float run = std::hypot(kHull[i].fwd, kHull[i].right);
						if (std::abs(surf[i] - surf[0]) > kMaxBankSlope * run && v2) {
							if ((i == 1 || i == 5 || i == 6) && surf[i] > surf[0]) govSteep = true;  // 41A.3: the governor, not a cut-off
						} else if (std::abs(surf[i] - surf[0]) > kMaxBankSlope * run) {
							ok[i] = false;
							touchRaw[i] = false;
							++rejected;
							if ((i == 1 || i == 5 || i == 6) && surf[i] > surf[0]) bankAhead = true;
						}
					}
				}
				// Hysteresis: a sample's touching / dry state only changes after it has held for kDryHysteresis.
				for (int i = 0; i < kN; ++i) {
					auto settle = [&](bool a_raw, bool& a_state, float& a_timer) {
						if (a_raw == a_state) { a_timer = 0.0f; return; }
						if ((a_timer += a_dt) >= kDryHysteresis) { a_state = a_raw; a_timer = 0.0f; }
					};
					settle(touchRaw[i], touchState_[i], touchTimer_[i]);
					settle(ok[i] && !wetRaw[i], dryState_[i], dryTimer_[i]);
					if (dryState_[i]) ++dry;
				}
				bool touch = false;
				for (int i = 0; i < kN; ++i) touch = touch || (ok[i] && touchState_[i]);
				for (int i = 0; i < kN; ++i) { diagOk[i] = ok[i]; diagReq[i] = req[i]; }
				if (ok[0] && touch && !tooHighNow) {
					if (waterfallTimer_ <= 0.0f) {
						waterfallTimer_ = 0.25f;
						waterfallNear_ = SkimWaterfallNear(vehiclePos_);
					}
					if (!waterfallNear_) {
						// 39.2: least squares plane over every valid sample: req = a + b * fwd + c * right.
						float b = 0.0f, c = 0.0f;  // rise per unit forward / per unit to the right
						double n = 0, Sf = 0, Sr = 0, Sff = 0, Srr = 0, Sfr = 0, Sz = 0, Sfz = 0, Srz = 0;
						for (int i = 0; i < kN; ++i) {
							if (!ok[i]) continue;
							const double f = kHull[i].fwd, r = kHull[i].right, z = req[i] - req[0];
							n += 1; Sf += f; Sr += r; Sff += f * f; Srr += r * r; Sfr += f * r; Sz += z; Sfz += f * z; Srz += r * z;
						}
						// Centred normal equations for b and c.
						const double cff = Sff - Sf * Sf / n, crr = Srr - Sr * Sr / n, cfr = Sfr - Sf * Sr / n;
						const double cfz = Sfz - Sf * Sz / n, crz = Srz - Sr * Sz / n;
						const double det = cff * crr - cfr * cfr;
						if (n >= 3 && std::abs(det) > 1.0) {
							b = static_cast<float>((cfz * crr - crz * cfr) / det);
							c = static_cast<float>((crz * cff - cfz * cfr) / det);
						} else {
							if (ok[1] && ok[2]) b = (req[1] - req[2]) / (tipFwd - sternFwd);
							else if (ok[1]) b = (req[1] - req[0]) / tipFwd;
							else if (ok[2]) b = (req[0] - req[2]) / -sternFwd;
							if (ok[3] && ok[4]) c = (req[3] - req[4]) / (2.0f * side);
							else if (ok[3]) c = (req[3] - req[0]) / side;
							else if (ok[4]) c = (req[0] - req[4]) / side;
						}
						if (v2) {
							// 41A.1: soft clamps, never hard.
							targetPitch = kMaxGroundPitch * std::tanh(std::atan(b) / kMaxGroundPitch);
							targetRoll = kMaxGroundRoll * std::tanh(-std::atan(c) / kMaxGroundRoll);
						} else {
							targetPitch = std::clamp(std::atan(b), -kMaxGroundPitch, kMaxGroundPitch);   // + = bow up
							targetRoll = std::clamp(-std::atan(c), -kMaxGroundRoll, kMaxGroundRoll);     // + = starboard down
						}
						grounded = true;
						if (dry >= 3 && targetPitch < 0.0f && skimSpeed_ > 5.0f) {
							// Basin guard: mostly dry and bow down while moving on = a basin or a rock ledge, not a beach.
							basin = true;
							grounded = false;
							targetPitch = targetRoll = 0.0f;
						}
					}
					if (grounded) {
						// Lift with the tilt actually shown, so the hull clears every valid sample.
						const float np = groundPitch_ + (targetPitch - groundPitch_) * kp;
						const float nr = groundRoll_ + (targetRoll - groundRoll_) * kr;
						const float spn = std::sin(np), cpsr = std::cos(np) * std::sin(nr);
						float sv = -1.0e9f;
						for (int i = 0; i < kN; ++i) if (ok[i]) sv = (std::max)(sv, req[i] - (spn * sf[i] - cpsr * sr[i]));
						liftTarget = sv + kSkimEyeHeight - vehiclePos_.z;
						if (v2) {
							// 41A.1: a smooth maximum of what each sample needs above the boat's own waterline (no snap when the
							// leading sample changes): (1/k) ln(1 + sum(exp(k e_i) - 1)), k = 0.15.
							const float base = vehiclePos_.z - kSkimEyeHeight;
							double acc = 1.0;
							for (int i = 0; i < kN; ++i) {
								if (!ok[i]) continue;
								const float e = std::clamp(req[i] - (spn * sf[i] - cpsr * sr[i]) - base, 0.0f, 200.0f);
								acc += std::exp(0.15 * e) - 1.0;
							}
							liftTarget = static_cast<float>(std::log(acc) / 0.15);
						}
					}
				}
			}
			// 39.3: the lift may reach 60 while the bow (tip or a quarter) is on dry ground, 30 afloat.
			const bool bowDry = dryState_[1] || dryState_[5] || dryState_[6];
			const float liftCap = iceN > 0 ? 95.0f : (bowDry ? 60.0f : kMaxGroundLift);
			const float rawLiftTarget = liftTarget;
			if (liftTarget > liftCap) { liftTarget = liftCap; capped = true; }
			liftTarget = (std::max)(0.0f, liftTarget);
			// 39.6: the hull bottom at the tip and how far the ground is above it.
			float tipLand = diagLand[1], tipBottom = 0.0f, tipSink = -1.0e9f;
			{
				const float wl = vehiclePos_.z - kSkimEyeHeight + groundLift_;
				tipBottom = wl + std::sin(groundPitch_) * sf[1] - std::cos(groundPitch_) * std::sin(groundRoll_) * sr[1] - hullDraft_;
				if (tipSeen) tipSink = tipLand - tipBottom;
			}
			// 39.5: the basin guard holds the lift while any sample is dry, until the hull is clear.
			if (basin && dry > 0) basinHold_ = true;
			if (dry == 0) basinHold_ = false;
			const bool hold = contactSuspended_ || basin || pinnedHold;
			tooHigh_ = tooHighNow;
			// 41E.5: on / off an ice sheet.
			if (iceN > 0 && iceSamplesPrev_ == 0) {
				const RE::FormID iid = iceRef ? iceRef->GetFormID() : 0u;
				if (std::find(iceSheets_.begin(), iceSheets_.end(), iid) == iceSheets_.end()) iceSheets_.push_back(iid);
				logger::info("Ice: on {} top {:.1f} water {:.1f} (hull sample {})", iceRef ? RefHay(iceRef).substr(0, RefHay(iceRef).find(' ')) : std::string("?"), iceTop, diagWater[iceIdx < 0 ? 0 : iceIdx], iceIdx);
			} else if (iceN == 0 && iceSamplesPrev_ > 0) {
				logger::info("Ice: left the sheet");
			}
			iceSamplesPrev_ = iceN;
			onIce_ = iceCentre;
			if (v2) {
				// 41A.2: critically damped springs for lift, pitch and roll (no overshoot); targets go to 0 when held.
				const float tp = hold || !grounded ? 0.0f : targetPitch, tr = hold || !grounded ? 0.0f : targetRoll;
				const bool anyGround = !hold && (grounded || dry > 0 || iceN > 0);
				float tl = (!contactOn_ || skimAirborne_ || skimOnRamp_) ? 0.0f : (basinHold_ && !pinnedHold ? groundLift_ : (anyGround ? liftTarget : 0.0f));
				if (rideTime_ < rockAheadUntil_) tl = 0.0f;  // 48.2: no bow lift up a rock face
				const float wl = iceN > 0 ? 8.0f : 9.0f;
				const int n = (std::max)(1, static_cast<int>(std::ceil(a_dt / 0.01f)));
				const float h = a_dt / static_cast<float>(n);
				for (int k = 0; k < n; ++k) {
					liftVel_ += (wl * wl * (tl - groundLift_) - 2.0f * wl * liftVel_) * h;
					groundLift_ += liftVel_ * h;
					pitchVel_ += (100.0f * (tp - groundPitch_) - 20.0f * pitchVel_) * h;
					groundPitch_ += pitchVel_ * h;
					rollVel_ += (49.0f * (tr - groundRoll_) - 14.0f * rollVel_) * h;
					groundRoll_ += rollVel_ * h;
				}
				if (!contactOn_ || skimAirborne_ || skimOnRamp_) { groundLift_ = 0.0f; liftVel_ = 0.0f; }
				groundLift_ = std::clamp(groundLift_, 0.0f, liftCap);
				// 41A.3: the bank governor: the speed allowed shrinks as the needed lift approaches the cap (or the tip
				// meets a slope steeper than kMaxBankSlope), blended in over 0.3 s, so the boat eases to a stop at the bank.
				// 44.3.1: also look ahead of the tip: a bank within 60 slows the boat so the tip stops 8 short of it.
				float tipBank = 1.0e9f;
				if (tipSeen) {
					auto* tesG = RE::TES::GetSingleton();
					for (float d = 10.0f; d <= 60.0f && tesG; d += 10.0f) {
						float l = 0.0f;
						const RE::NiPoint3 q{ bowPos.x + sh * d, bowPos.y + ch * d, bowPos.z };
						if (tesG->GetLandHeight(q, l) && std::isfinite(l) && l - diagWater[1] > kBeachMaxRise) { tipBank = d; break; }
					}
				}
				const bool rockAhead = rideTime_ < rockAheadUntil_;  // 48.2: a rock face ahead: stop at it, never climb it
				const bool engage = !hold && !FlowOverride() && skimSpeed_ > 0.5f && (rawLiftTarget > 0.75f * liftCap || govSteep || tipBank < 1.0e8f || rockAhead);
				float govTarget = 1.0f;
				if (engage) govTarget = govSteep ? 0.0f : std::clamp((liftCap - rawLiftTarget) / (0.25f * liftCap), 0.0f, 1.0f);
				if (engage && tipBank < 1.0e8f) govTarget = (std::min)(govTarget, std::clamp((tipBank - 8.0f) / 52.0f, 0.0f, 1.0f));
				if (engage && rockAhead) govTarget = 0.0f;
				if (FlowOverride()) { governor_ = 1.0f; govEngaged_ = false; }  // 44.1.1
				if (engage && !govEngaged_) {
					govEngaged_ = true;
					govRefSpeed_ = (std::max)(skimSpeed_, 30.0f);
					logger::info("Ground2: bank governor engaged at tip rise {:.0f} (need {:.0f}, cap {:.0f}{})", tipSeen ? diagLand[1] - diagWater[1] : 0.0f, rawLiftTarget, liftCap, govSteep ? ", steep" : "");
				}
				governor_ += (govTarget - governor_) * (1.0f - std::exp(-a_dt / 0.3f));
				if (govEngaged_) {
					skimSpeed_ = (std::min)(skimSpeed_, governor_ * govRefSpeed_);
					if (!engage && governor_ > 0.98f) { govEngaged_ = false; logger::info("Ground2: bank governor released"); }
				}
				if (dry > 0 && (ground2LogTimer_ += a_dt) >= 1.0f) {
					ground2LogTimer_ = 0.0f;
					logger::info("Ground2: lift {:.1f} (target {:.1f}) pitch {:.1f} roll {:.1f} governor {:.2f} bow-tip rise {:.1f}", groundLift_, liftTarget,
						groundPitch_ * 57.2958f, groundRoll_ * 57.2958f, governor_, tipSeen ? diagLand[1] - diagWater[1] : 0.0f);
				}
			} else if (hold) {
				const float step = kSuspendTiltRate * a_dt;
				groundPitch_ = groundPitch_ > 0.0f ? (std::max)(0.0f, groundPitch_ - step) : (std::min)(0.0f, groundPitch_ + step);
				groundRoll_ = groundRoll_ > 0.0f ? (std::max)(0.0f, groundRoll_ - step) : (std::min)(0.0f, groundRoll_ + step);
			} else if (tooHighNow && !grounded) {
				// 40.1.5: a bank under the hull: hold the tilt and ease it toward 0 at 1 rad/s (no snap).
				const float step = 1.0f * a_dt;
				groundPitch_ = groundPitch_ > 0.0f ? (std::max)(0.0f, groundPitch_ - step) : (std::min)(0.0f, groundPitch_ + step);
				groundRoll_ = groundRoll_ > 0.0f ? (std::max)(0.0f, groundRoll_ - step) : (std::min)(0.0f, groundRoll_ + step);
			} else {
				groundPitch_ += (targetPitch - groundPitch_) * kp;
				groundRoll_ += (targetRoll - groundRoll_) * kr;
			}
			groundBlend_ += ((grounded ? 1.0f : 0.0f) - groundBlend_) * kp;
			if (grounded && !wasGrounded_) LogContact("hull touched the ground");
			if (!grounded && wasGrounded_) LogContact("hull clear of the ground");
			wasGrounded_ = grounded;

			// Lift above where the skim physics put the boat: eased, capped, never below the water line.
			if (v2) {
				// 41A: the spring above already moved the lift.
			} else if (!contactOn_ || skimAirborne_ || skimOnRamp_) {
				groundLift_ = 0.0f;  // also means it is 0 the moment the boat lands after a launch or a fall
			} else if (basinHold_ && !contactSuspended_ && !pinnedHold) {
				// 39.5: hold the lift (no decay) while the basin guard is on and something is dry.
			} else if (hold || (dry == 0 && rejected == 0 && iceN == 0)) {
				// Nothing dry under the hull (open water, shallows after a landing) or contact suspended: drop quickly.
				groundLift_ = (std::max)(0.0f, groundLift_ - kSuspendLiftRate * a_dt);
			} else if (tooHighNow && !grounded) {
				// 40.1.5: a bank under the hull: hold the lift and ease it toward 0 at 20 u/s (no snap).
				groundLift_ = (std::max)(0.0f, groundLift_ - 20.0f * a_dt);
			} else {
				// 39.3: rise twice as fast while the tip's hull bottom is more than 4 inside the ground.
				const float rate = liftTarget > groundLift_ ? kLiftRiseRate * (tipSink > 4.0f ? 2.0f : 1.0f) : kLiftFallRate;
				groundLift_ += (liftTarget - groundLift_) * (1.0f - std::exp(-rate * a_dt));
				groundLift_ = std::clamp(groundLift_, 0.0f, liftCap);
			}
			appliedLift_ = groundLift_ * contactScale_;
			vehiclePos_.z += appliedLift_;

			const RE::NiPoint3 fwd{ sh, ch, 0.0f };
			// A bank at the bow: bleed speed and hold the tip where the bank was found (39.4: kBankStopGap is 0 now).
			if (bankAhead && !contactSuspended_ && !FlowOverride()) {
				if (!hasBankMark_) { hasBankMark_ = true; bankMark_ = bowPos; glueZeroTimer_ = 1.0f; LogContact("bank ahead, stopping"); }
				skimSpeed_ *= std::exp(-kSkimBlockedDrag * a_dt);
				const float into = (bowPos.x - bankMark_.x) * fwd.x + (bowPos.y - bankMark_.y) * fwd.y;
				const float gap = v2 ? 10.0f : kBankStopGap;  // 41A.3: with the governor this is only the last resort
				if (into > gap) {
					// 44.3.1: never a jump: back off at most 120 u/s.
					skimSpeed_ = 0.0f;
					EaseBack(fwd, (std::min)(into - gap, 120.0f * a_dt));
				}
			} else {
				hasBankMark_ = false;
			}

			// Beach slide: measured at the tip from where it first came out of the water. Heavy drag toward the cap, then stop.
			bowSlide_ = 0.0f;
			if (grounded && dryState_[1] && !hold) {
				if (!hasBowMark_) { hasBowMark_ = true; bowMark_ = bowPos; LogContact("bow on the sand"); }
				bowSlide_ = (bowPos.x - bowMark_.x) * fwd.x + (bowPos.y - bowMark_.y) * fwd.y;
				const float t = bowSlide_ / kBeachMaxSlide;
				if (t > kBeachDragStart && skimSpeed_ > 0.0f) {
					const float d = std::clamp((t - kBeachDragStart) / (1.0f - kBeachDragStart), 0.0f, 1.0f);
					skimSpeed_ *= std::exp(-kBeachDrag * d * d * a_dt);
				}
				if (bowSlide_ > kBeachMaxSlide) {
					if (skimSpeed_ > 1.0f) LogContact("came to rest on the beach");
					skimSpeed_ = 0.0f;
					EaseBack(fwd, (std::min)(bowSlide_ - kBeachMaxSlide, 120.0f * a_dt));  // 44.3.1: eased, not a jump
				}
			} else if (!dryState_[1]) {
				hasBowMark_ = false;
			}

			// 39.6: the tip, twice a second while grounded, and once when it is in the ground.
			beachLogTimer_ += a_dt;
			if (grounded && tipSink > -1.0e8f) {
				if (beachLogTimer_ >= 0.5f) {
					beachLogTimer_ = 0.0f;
					lastBeachLineAt_ = rideTime_;
					logger::info("Beach: tip land {:.1f} water {:.1f} hull bottom at tip {:.1f} sink {:.1f} lift {:.1f} pitch {:.1f} dry {}",
						tipLand, diagWater[1], tipBottom, tipSink, groundLift_, groundPitch_ * 57.2958f, dry);
				}
				// 44.3.1: the tip deeper than 10 in the ground: ease back along the heading (at most 120 u/s), never a jump.
				if (tipSink > 10.0f && skimSpeed_ < 60.0f) EaseBack(fwd, (std::min)(tipSink - 10.0f, 120.0f * a_dt));
				if (tipSink > 5.0f && !bowInGroundLogged_) {
					bowInGroundLogged_ = true;
					logger::info("Beach: BOW IN GROUND by {:.1f} at slide {:.0f}", tipSink, bowSlide_);
				} else if (tipSink < 2.0f) {
					bowInGroundLogged_ = false;
				}
			}

			if ((grounded || rejected > 0 || basin) && (groundLogTimer_ += a_dt) >= 1.0f) {
				groundLogTimer_ = 0.0f;
				if (bowSlide_ > 0.0f && targetPitch < 0.0f) {
					// Sliding up a beach with the bow pointing down: write what each hull sample saw.
					static constexpr const char* kName[kN] = { "centre", "tip", "stern", "starboard", "port", "bow quarter stbd", "bow quarter port" };
					std::string line;
					for (int i = 0; i < kN; ++i) {
						line += std::format("  {}: {} land {:.0f} water {:.0f} need {:.0f}", kName[i], diagOk[i] ? "ok" : "OUT",
							diagLand[i], diagWater[i], diagReq[i]);
					}
					logger::info("Ground samples (bow down while sliding):{}", line);
				}
				logger::info("Ground: pitch {:.1f} deg  roll {:.1f} deg  dry points {}  rejected samples {}{}  shallow {}  bow slide {:.0f}  lift {:.1f}{}{}{}{}",
					groundPitch_ * 57.2958f, groundRoll_ * 57.2958f, dry, rejected, tooHigh_ ? " (bank/cliff: off)" : "", shallow,
					bowSlide_, groundLift_, capped ? "  capped" : "", bankAhead ? "  bank ahead" : "", basin ? "  basin" : "", basinHold_ ? "  basin hold" : "");
			}
		}

		// 44.3.1: a gentle back-off instead of a position jump.
		void EaseBack(const RE::NiPoint3& a_fwd, float a_by)
		{
			if (a_by <= 0.0f || FlowOverride()) return;
			vehiclePos_.x -= a_fwd.x * a_by;
			vehiclePos_.y -= a_fwd.y * a_by;
			easedBack_ += a_by;
			if (rideTime_ - easedLogAt_ >= 1.0f) {
				easedLogAt_ = rideTime_;
				logger::info("Beach: eased back by {:.1f}", easedBack_);
				easedBack_ = 0.0f;
			}
		}

		void LogContact(const char* a_what)
		{
			if (contactLog_) logger::info("Contact: {}", a_what);
		}

		// =====================================================================
		// Riding: the player stays a normal, ticking actor (so the engine keeps
		// its cell and streaming): collision off ("tcl"), movement controls off,
		// god mode, moved every frame by the skim physics. Animals stay hostile.
		// =====================================================================
		struct SkimInputSink : public RE::BSTEventSink<RE::InputEvent*>
		{
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events,
				RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!a_events) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto& self = OarsAndSails::Get();
				auto* ue = RE::UserEvents::GetSingleton();
				for (auto* e = *a_events; e; e = e->next) {
					if (auto* btn = e->AsButtonEvent()) {
						// v1.0.14: the game's own actions (Activate, Forward, Back, Strafe), on any device, so a rebound key and a
						// gamepad button work too. The keyboard scan codes stay as a fallback for an event without its user event.
						const bool keyboard = btn->device == RE::INPUT_DEVICE::kKeyboard;
						const bool gamepad = btn->device == RE::INPUT_DEVICE::kGamepad;
						if (!keyboard && !gamepad) continue;
						const std::uint32_t id = btn->GetIDCode();
						const bool down = btn->IsPressed();
						const std::string_view user = btn->QUserEvent().c_str() ? std::string_view(btn->QUserEvent().c_str()) : std::string_view{};
						auto is = [&](const RE::BSFixedString& a_event, std::uint32_t a_key) {
							if (ue && !user.empty() && a_event.c_str() && user == a_event.c_str()) return true;
							return keyboard && id == a_key;
						};
						// B2 (v1.0.15): aboard, A did not unboard. While seated / crouched the game may hand the button another
						// user event (or none), so the gamepad key mapped to Activate counts too, and what A really sends is logged.
						if (gamepad && btn->IsDown() && self.playerSkim_ && self.padLogs_ < 12) {
							++self.padLogs_;
							logger::info("Input: gamepad button {:#x} user event '{}' (Activate is mapped to {:#x})", id, user, self.padActivate_);
						}
						// B5 (v1.0.15): third person zoom on the controller = hold L3 (the Sneak button, switched off while aboard)
						// and push the right stick up / down.
						if (gamepad && ue && !user.empty() && ue->sneak.c_str() && user == ue->sneak.c_str()) self.padZoomHeld_ = down;
						// 55.1 (v1.0.55): vanilla's controller zoom = hold R3 (Toggle POV) and push the LEFT stick up / down.
						if (gamepad && ((ue && !user.empty() && ue->togglePOV.c_str() && user == ue->togglePOV.c_str()) ||
										   (user.empty() && id == RE::BSWin32GamepadDevice::Key::kRightThumb))) {
							if (down && !self.padViewHeld_) {
								self.zoomedWithR3_ = false;
								self.r3StartFirst_ = RE::PlayerCamera::GetSingleton() && RE::PlayerCamera::GetSingleton()->IsInFirstPerson();
							}
							self.padViewHeld_ = down;
						}
						if (gamepad) self.NotePadEvent(true, user);
						const bool padActivate = gamepad && self.padActivate_ != 0 && id == self.padActivate_;
						if (padActivate && btn->IsDown() && self.playerSkim_) {
							self.activatePressed_ = true;
							self.activateStamp_ = std::chrono::steady_clock::now();
						}
						if (is(ue ? ue->forward : RE::BSFixedString{}, self.keyFwd_)) self.inFwd_ = down;
						if (is(ue ? ue->back : RE::BSFixedString{}, self.keyBack_)) self.inBack_ = down;
						if (is(ue ? ue->strafeLeft : RE::BSFixedString{}, self.keyLeft_)) self.inLeft_ = down;
						if (is(ue ? ue->strafeRight : RE::BSFixedString{}, self.keyRight_)) self.inRight_ = down;
						// Boarded: the numpad tunes. On foot beside the parked boat it only tunes the bench and oars
						// (Num5 picks which), so the numpad is left alone the rest of the time.
						// 56.26: Num9 / Num3 raise and lower the player's own boat (Shift+Num9 / Shift+Num3 are the old per-target up / down).
						if (keyboard && (id == 0x2A || id == 0x36)) self.shiftHeld_ = down;
						if (keyboard && id == 0x52) self.num0Held_ = down;
						// 56.29: the numpad is off by default; oas "num <target>" turns it on for one thing at a time (oas "num off").
						if (keyboard && down && self.numMode_ != OarsAndSails::NumMode::kOff && OarsAndSails::IsNumpadScan(id)) self.NumpadByMode(id);
						// Activate only matters while aboard (it unboards); on foot the game's own activation handles our objects.
						if (is(ue ? ue->activate : RE::BSFixedString{}, self.keyActivate_) && btn->IsDown()) {
							auto* ui = RE::UI::GetSingleton();
							if (!(ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME)))) {
								self.activatePressed_ = true;
								self.activateStamp_ = std::chrono::steady_clock::now();
								if (!self.inputLogged_) {
									self.inputLogged_ = true;
									logger::info("Input: activate via {} ({})", keyboard ? "keyboard" : "gamepad", user.empty() ? "scan code" : "user event");
								}
							}
						}
					} else if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
						auto* t = static_cast<RE::ThumbstickEvent*>(e);
						if (t->device == RE::INPUT_DEVICE::kGamepad) self.NotePadEvent(false, {});
						if (t->IsLeft()) {
							self.stickX_ = t->xValue;
							self.stickY_ = t->yValue;
						} else {
							self.rStickY_ = t->yValue;
						}
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		void RegisterSkimInput()
		{
			static SkimInputSink sink;
			static bool registered = false;
			if (registered) return;
			RefreshSkimKeys();
			if (auto* mgr = RE::BSInputDeviceManager::GetSingleton()) {
				mgr->AddEventSink(&sink);
				registered = true;
			}
		}

		void RefreshSkimKeys()
		{
			keyFwd_ = 0x11; keyBack_ = 0x1F; keyLeft_ = 0x1E; keyRight_ = 0x20; keyActivate_ = 0x12;  // W S A D E defaults
			auto* cm = RE::ControlMap::GetSingleton();
			auto* ue = RE::UserEvents::GetSingleton();
			if (!cm || !ue) return;
			auto fetch = [&](const RE::BSFixedString& a_event, std::uint32_t& a_out) {
				const std::uint32_t k = cm->GetMappedKey(std::string_view{ a_event.c_str() }, RE::INPUT_DEVICE::kKeyboard);
				if (k != 0 && k != 0xFF && k != 0xFFFFFFFF) a_out = k;
			};
			fetch(ue->forward, keyFwd_);
			fetch(ue->back, keyBack_);
			fetch(ue->strafeLeft, keyLeft_);
			fetch(ue->strafeRight, keyRight_);
			fetch(ue->activate, keyActivate_);
			const std::uint32_t pad = cm->GetMappedKey(std::string_view{ ue->activate.c_str() }, RE::INPUT_DEVICE::kGamepad);
			padActivate_ = (pad != 0xFF && pad != 0xFFFFFFFF) ? pad : 0;
		}

		// Turns the game's own walking/fighting/jumping/sneaking off so only our
		// skim physics moves the player. Looking stays on (mouse look is native).
		void SetSkimControls(bool a_enable, bool a_immediate = false)
		{
			if (auto* cm = RE::ControlMap::GetSingleton()) {
				for (auto flag : { RE::ControlMap::UEFlag::kMovement, RE::ControlMap::UEFlag::kFighting,
						 RE::ControlMap::UEFlag::kJumping, RE::ControlMap::UEFlag::kSneaking,
						 RE::ControlMap::UEFlag::kActivate }) {
					cm->ToggleControls(flag, a_enable, a_immediate);
				}
			}
		}

		// Hides (or restores) the third person body so only the boat shows when the camera pulls back.
		void SetPlayerBodyHidden(RE::PlayerCharacter* a_player, bool a_hide)
		{
			if (!a_player) return;
			auto* body = a_player->Get3D(false);
			if (a_hide && !bodyHideLogged_) {
				bodyHideLogged_ = true;
				logger::info("SkimPlayer: hiding third person body (node {})", body ? "found" : "MISSING");
			}
			if (body) {
				if (a_hide) body->flags.set(RE::NiAVObject::Flag::kHidden);
				else body->flags.reset(RE::NiAVObject::Flag::kHidden);
			}
		}

		// 55.2 (v1.0.55): off the boat, the third person body's hidden flag follows the camera the way the game keeps it:
		// set in first person (clearing it there showed the own body through the view), clear in third person.
		// Other camera states (sitting, mount, ...) are left alone.
		void SyncBodyFlagToCamera(RE::PlayerCharacter* a_player, bool a_log)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			auto* body = a_player ? a_player->Get3D(false) : nullptr;
			if (!cam || !cam->currentState || !body) return;
			const auto id = cam->currentState->id;
			if (id != RE::CameraState::kFirstPerson && id != RE::CameraState::kThirdPerson) return;
			const bool first = id == RE::CameraState::kFirstPerson;
			const bool hidden = body->flags.any(RE::NiAVObject::Flag::kHidden);
			if (first && !hidden) body->flags.set(RE::NiAVObject::Flag::kHidden);
			else if (!first && hidden) body->flags.reset(RE::NiAVObject::Flag::kHidden);
			if (a_log) logger::info("Camera: body flag after exit: {}", first ? "first person, set hidden" : "third person, shown");
		}

		// 56 (v1.0.56): the oars hang on the rower's AnimObject bones while the row clip plays ([Boat] BakedOars). Not on the
		// OffsetGPMA fallback (no oars in it), and not on the player in first person: the third person body is hidden there
		// and the oars with it, so our own oar refs row instead.
		void TendBakedBoatOars(RE::PlayerCharacter* a_player, float a_dt)
		{
			RE::NiPointer<RE::Actor> rower = playerSkim_ ? RowActor(a_player) : nullptr;
			auto* cam = RE::PlayerCamera::GetSingleton();
			const bool firstPerson = rower.get() == a_player && cam && cam->IsInFirstPerson();
			const bool want = playerSkim_ && rowAnimOn_ && !rowAnimGpma_ && !firstPerson;
			const char* why = !playerSkim_ ? "exit" : firstPerson ? "first person: the boat's own oars row" : rowAnimGpma_ ? "OffsetGPMA fallback" : "rowing -> resting swap";
			BoatBow::TendBoatBakedOars(rower.get(), want, a_dt, why);
		}

		// Post-exit watcher: for 3 s after unboarding, and on every camera state change while not riding.
		void WatchBodyFlag(RE::PlayerCharacter* a_player, float a_delta)
		{
			if (playerSkim_ || !a_player) return;
			auto* cam = RE::PlayerCamera::GetSingleton();
			const int state = cam && cam->currentState ? static_cast<int>(cam->currentState->id) : -1;
			const bool changed = state != lastBodyCamState_;
			lastBodyCamState_ = state;
			if (bodyWatchTimer_ > 0.0f) bodyWatchTimer_ -= a_delta;
			else if (!changed) return;
			SyncBodyFlagToCamera(a_player, false);
		}

		// On exit: leave the flag set in first person (the game expects it) and show the body in third person.
		void ExitBodyFlag(RE::PlayerCharacter* a_player)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			const bool first = exitWasFirst_ || (cam && cam->IsInFirstPerson());
			if (!first) SetPlayerBodyHidden(a_player, false);
			SyncBodyFlagToCamera(a_player, true);
			bodyWatchTimer_ = 3.0f;
		}

		// The player's third person body rides locked in the boat: it sits at the tuned seat and always
		// faces along the boat (plus a numpad turn), whatever way the camera looks. Only the body's
		// skeleton is moved, so the actor, first person view and third person camera stay free.
		// 56.45: the rower sits facing the stern, so the game's camera (behind the player) looked at his back and W rowed
		// the boat toward the camera. On boarding as the rower, and each time the view goes from first to third person on
		// the rowing seat, the third person camera is swung round once to face him (freeRotation.x = pi): W then moves the
		// boat away from the camera. The mouse turns it freely afterwards. [Camera] FaceRower=0 turns it off.
		bool faceRowerPending_{ false };
		bool faceRowerOn_{ true };

		// 56.46: first person stays seated (no stand-up, no sneak crouch that followers copy). The game limits the look
		// while its sit state is set, so that state is cleared while the view is first person and put back when it leaves.
		// The seated animation itself is never touched, so switching views no longer stands anyone up. [Camera] FirstPersonFreeLook
		bool fpSitMasked_{ false };
		RE::SIT_SLEEP_STATE fpSitSaved_{ RE::SIT_SLEEP_STATE::kNormal };  // set from the real state before any restore
		int fpFreeLook_{ -1 };

		void MaskSeatForFreeLook(RE::PlayerCharacter* a_player, bool a_on)
		{
			if (!a_player) return;
			auto* st = a_player->AsActorState();
			if (!st) return;
			if (a_on) {
				if (fpFreeLook_ < 0) fpFreeLook_ = GetPrivateProfileIntA("Camera", "FirstPersonFreeLook", 1, FlowIniPath().c_str()) != 0 ? 1 : 0;
				if (!fpFreeLook_ || !playerSit_ || sitFailed_) return;
				const auto cur = st->actorState1.sitSleepState;
				if (cur == RE::SIT_SLEEP_STATE::kNormal) return;
				if (!fpSitMasked_) {
					fpSitSaved_ = cur;
					logger::info("First person: seated, sit state {} cleared so the view can look around freely", static_cast<int>(cur));
				}
				fpSitMasked_ = true;
				st->actorState1.sitSleepState = RE::SIT_SLEEP_STATE::kNormal;
			} else if (fpSitMasked_) {
				fpSitMasked_ = false;
				// 56.48: the graph's bIsSitting mirrors the engine state, so it reads false while masked even though the seated
				// animation never stopped. Put the state back unconditionally; the seat watcher takes over from here.
				if (st->actorState1.sitSleepState == RE::SIT_SLEEP_STATE::kNormal) st->actorState1.sitSleepState = fpSitSaved_;
				a_player->SetGraphVariableBool("bIsSitting", true);
				sitMiss_ = 0;
				logger::info("First person left: sit state put back ({})", static_cast<int>(fpSitSaved_));
			}
		}
		bool faceRowerLoaded_{ false };
		void FaceRowerCamera(RE::PlayerCamera* a_cam)
		{
			if (!faceRowerLoaded_) {
				faceRowerLoaded_ = true;
				faceRowerOn_ = GetPrivateProfileIntA("Camera", "FaceRower", 1, FlowIniPath().c_str()) != 0;
			}
			if (!faceRowerOn_ || !a_cam || !a_cam->currentState || a_cam->currentState->id != RE::CameraState::kThirdPerson) { faceRowerPending_ = faceRowerOn_ && faceRowerPending_; return; }
			auto* tps = static_cast<RE::ThirdPersonState*>(a_cam->currentState.get());
			if (faceRowerPending_) {
				// 56.50: the game resets the orbit a frame or two after a view switch or boarding (log: freeRotation back to 0),
				// which left the camera behind the rower. Hold the turn for 45 frames, re-applying only if it was reset.
				faceRowerPending_ = false;
				faceRowerHold_ = 45;
				tps->freeRotationEnabled = true;
				tps->freeRotation.x = 3.14159265f;
				logger::info("Camera: third person turned to face the rower (W rows away from the camera)");
				return;
			}
			if (faceRowerHold_ <= 0) return;
			--faceRowerHold_;
			if (!tps->freeRotationEnabled || std::fabs(tps->freeRotation.x) < 0.5f) {
				tps->freeRotationEnabled = true;
				tps->freeRotation.x = 3.14159265f;
				if (faceRowerResets_++ < 8) logger::info("Camera: the game reset the orbit behind the rower, turned back to face him ({} frames left)", faceRowerHold_);
			}
		}
		int faceRowerHold_{ 0 };
		int faceRowerResets_{ 0 };

		void ApplyPlayerBody(RE::PlayerCharacter* a_player)
		{
			if (!a_player) return;
			// The body is carried along by position, so its animation graph thinks it is walking when
			// W is held (first person too: that is where the splashing wade footsteps came from).
			// Keep the movement inputs at zero so the crouch idle plays.
			for (const char* v : { "Speed", "SpeedSampled", "SpeedDesired", "Direction", "TurnDelta" }) a_player->SetGraphVariableFloat(v, 0.0f);
			auto* cam = RE::PlayerCamera::GetSingleton();
			const bool firstPerson = cam && cam->IsInFirstPerson();
			if (!firstPerson && wasFirstPerson_ && playerOnRowSeat_) faceRowerPending_ = true;  // 56.45: into third person on the rowing seat
			if (!firstPerson && (faceRowerPending_ || faceRowerHold_ > 0)) FaceRowerCamera(cam);
			if (firstPerson && !wasFirstPerson_) {
				// Coming back to first person: look the way the seat faces (the rowing seat looks at the stern, like a rower).
				RE::NiPoint3 ang = a_player->GetAngle();
				ang.x = 0.0f;
				ang.z = skimHeading_ + playerSeatYaw_;
				a_player->SetAngle(ang);
			}
			wasFirstPerson_ = firstPerson;
			SetPlayerBodyHidden(a_player, firstPerson);
			MaskSeatForFreeLook(a_player, firstPerson);
			if (firstPerson) {
				if (lockSeatedFacing_) a_player->SetGraphVariableBool("bAllowRotation", true);  // first person look turns the actor
				return;
			}
			if (!playerTilt_) {
				// Physics-friendly mode (default for the player): no skeleton changes, which HDT-SMP/CBPC dislike. The body is
				// kept facing along the boat through the plain actor heading instead (no roll/pitch tilt).
				// The seated body must not turn with the mouse: stop the animation graph rotating the actor too.
				if (lockSeatedFacing_) a_player->SetGraphVariableBool("bAllowRotation", false);
				RE::NiPoint3 ang = a_player->GetAngle();
				ang.x = 0.0f;
				ang.z = skimHeading_ + playerSeatYaw_;
				a_player->SetAngle(ang);
				return;
			}
			auto* root = a_player->Get3D(false);
			if (!root) return;
			const float sh = std::sin(skimHeading_), ch = std::cos(skimHeading_);
			const RE::NiPoint3 base = a_player->GetPosition();
			root->local.translate = RE::NiPoint3{
				base.x + ch * playerSeat_.x + sh * playerSeat_.y,
				base.y - sh * playerSeat_.x + ch * playerSeat_.y,
				base.z - kCrouchEyeLift + playerSeat_.z };
			RE::NiMatrix3 boatRot;
			if (!BoatBow::RigidRotation(boatRot)) {
				boatRot.entry[0][0] = ch;  boatRot.entry[0][1] = sh;  boatRot.entry[0][2] = 0.0f;
				boatRot.entry[1][0] = -sh; boatRot.entry[1][1] = ch;  boatRot.entry[1][2] = 0.0f;
				boatRot.entry[2][0] = 0.0f; boatRot.entry[2][1] = 0.0f; boatRot.entry[2][2] = 1.0f;
			}
			const float c = std::cos(playerSeatYaw_), s = std::sin(playerSeatYaw_);
			RE::NiMatrix3 yawRot;
			yawRot.entry[0][0] = c;  yawRot.entry[0][1] = s;  yawRot.entry[0][2] = 0.0f;
			yawRot.entry[1][0] = -s; yawRot.entry[1][1] = c;  yawRot.entry[1][2] = 0.0f;
			yawRot.entry[2][0] = 0.0f; yawRot.entry[2][1] = 0.0f; yawRot.entry[2][2] = 1.0f;
			root->local.rotate = yawRot * boatRot;
			RE::NiUpdateData data{};
			root->Update(data);
		}

		// Called after the camera has updated, which is when the game may have un-hidden the body.
		// 17.3 / 18.2 / 19 (v1.0.21): with our row clip on the player, the third person view turned upside down.
		// v1.0.20 corrected the camera root after PlayerCamera::Update, but the game calls that function directly, so the
		// vtable hook never ran. The fix now sits in the hook that does fire every frame: ThirdPersonState slot 4, which in
		// CommonLibSSE-NG on SE/AE is GetRotation(NiQuaternion&) (Update is 3, GetTranslation 5). The roll is removed from
		// the rotation it hands back, the one the view is drawn with. Runs while the player is boarded and is the rower
		// (rowing, about to row, or between strokes): with no roll it changes nothing. Never in first person.
		bool CamUprightWanted()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			return player && playerSkim_ && camFixOn_ && !rowAnimGpma_ && RowActor(player).get() == player;
		}

		// Keeps the forward axis (heading and pitch) of a rotation and sets its up to world +Z. Columns: right (X),
		// forward (Y), up (Z). Returns false when there is nothing to change (upright, or looking straight up or down).
		static bool UprightRotation(const RE::NiMatrix3& a_in, RE::NiMatrix3& a_out, float& a_rollDeg, float& a_upZ)
		{
			RE::NiPoint3 f{ a_in.entry[0][1], a_in.entry[1][1], a_in.entry[2][1] };
			const RE::NiPoint3 u{ a_in.entry[0][2], a_in.entry[1][2], a_in.entry[2][2] };
			a_upZ = u.z;
			a_rollDeg = 0.0f;
			const float fl = f.Length();
			if (fl < 1e-4f) return false;
			f = f / fl;
			RE::NiPoint3 r{ f.y, -f.x, 0.0f };  // forward x world up
			const float rl = r.Length();
			if (rl < 1e-3f) return false;
			r = r / rl;
			const RE::NiPoint3 up{ r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x };  // right x forward
			const float cosRoll = std::clamp(u.x * up.x + u.y * up.y + u.z * up.z, -1.0f, 1.0f);
			a_rollDeg = std::acos(cosRoll) * 57.2958f;
			if (cosRoll > 0.99999f) return false;
			a_out.entry[0][0] = r.x; a_out.entry[1][0] = r.y; a_out.entry[2][0] = r.z;
			a_out.entry[0][1] = f.x; a_out.entry[1][1] = f.y; a_out.entry[2][1] = f.z;
			a_out.entry[0][2] = up.x; a_out.entry[1][2] = up.y; a_out.entry[2][2] = up.z;
			return true;
		}

		void LogCamNodesOnce(RE::PlayerCharacter* a_player)
		{
			if (camNodesLogged_) return;
			camNodesLogged_ = true;
			auto* root3d = a_player->Get3D(false);
			for (const char* n : { "NPC Root [Root]", "NPC Head [Head]", "Camera3rd [Cam3]" }) {
				auto* node = root3d ? root3d->GetObjectByName(n) : nullptr;
				if (!node) { logger::info("Cam3rd node '{}': not found", n); continue; }
				const auto& m = node->world.rotate;
				logger::info("Cam3rd node '{}': world up=({:.2f},{:.2f},{:.2f}) forward=({:.2f},{:.2f},{:.2f})", n,
					m.entry[0][2], m.entry[1][2], m.entry[2][2], m.entry[0][1], m.entry[1][1], m.entry[2][1]);
			}
		}

		// 21.4 (v1.0.22): our clip may write the camera bones with Blender's rest pose (OffsetGPMA never did this). The local
		// transform of these nodes is stored just before OarsRowStart is sent to the player; while the clip plays it is
		// written back before the camera reads them (ThirdPersonState::Update, slot 3) if a node turned over 5 degrees.
		// 28 (v1.0.27): AnimObjectA and AnimObjectB (indices 3 and 4) are held by HoldAnimObjects, not by GuardCamBones.
		static constexpr const char* kCamBones[] = { "Camera3rd [Cam3]", "Camera Control", "NPC Root [Root]", "AnimObjectA", "AnimObjectB" };
		static constexpr int kCamBoneGuarded = 3;  // the first three are the camfix bones
		struct CamBone { std::string name; RE::NiMatrix3 rot; RE::NiPoint3 pos; bool valid{ false }; float delta{ 0.0f }; };
		CamBone camBase_[std::size(kCamBones)];
		bool camBaseValid_{ false };
		std::vector<float> camBoneLogAt_;  // ride times for the bone log (1 s and 3 s after the player's OarsRowStart)

		void StoreCamBaseline(RE::PlayerCharacter* a_player)
		{
			auto* root3d = a_player ? a_player->Get3D(false) : nullptr;
			camBaseValid_ = false;
			for (int i = 0; i < static_cast<int>(std::size(kCamBones)); ++i) {
				auto& b = camBase_[i];
				b = {};
				b.name = kCamBones[i];
				auto* node = root3d ? root3d->GetObjectByName(kCamBones[i]) : nullptr;
				if (!node) { logger::info("Cam3rd: baseline '{}' not found", kCamBones[i]); continue; }
				b.rot = node->local.rotate;
				b.pos = node->local.translate;
				b.valid = camBaseValid_ = true;
				const auto& m = b.rot;
				logger::info("Cam3rd: baseline local rot ({:.2f},{:.2f},{:.2f} | {:.2f},{:.2f},{:.2f} | {:.2f},{:.2f},{:.2f}) pos ({:.1f},{:.1f},{:.1f}) '{}'",
					m.entry[0][0], m.entry[0][1], m.entry[0][2], m.entry[1][0], m.entry[1][1], m.entry[1][2], m.entry[2][0], m.entry[2][1], m.entry[2][2],
					b.pos.x, b.pos.y, b.pos.z, b.name);
			}
		}

		static float RotDeltaDeg(const RE::NiMatrix3& a, const RE::NiMatrix3& b)
		{
			float tr = 0.0f;  // trace(a^T b)
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c) tr += a.entry[r][c] * b.entry[r][c];
			return std::acos(std::clamp((tr - 1.0f) * 0.5f, -1.0f, 1.0f)) * 57.2958f;
		}

		// ThirdPersonState::Update (slot 3), before the game's own: the camera bones back to their baseline.
		void GuardCamBones()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!camBaseValid_ || !player || !playerSkim_ || !camFixOn_ || !rowAnimOn_ || rowAnimGpma_ || RowActor(player).get() != player) return;
			auto* root3d = player->Get3D(false);
			if (!root3d) return;
			for (int i = 0; i < kCamBoneGuarded; ++i) {
				auto& b = camBase_[i];
				if (!b.valid) continue;
				auto* node = root3d->GetObjectByName(b.name.c_str());
				if (!node) continue;
				b.delta = RotDeltaDeg(b.rot, node->local.rotate);
				const float moved = (node->local.translate - b.pos).Length();
				if (b.delta > 5.0f || moved > 2.0f) {
					node->local.rotate = b.rot;
					node->local.translate = b.pos;
					RE::NiUpdateData data{};
					node->Update(data);
				}
			}
		}

		// 28 (v1.0.27): the root cause of the flip. Our clip turns AnimObjectA 180 degrees; ThirdPersonState follows that
		// bone (animatedBoneName 'AnimObjectA') into animationRotation, and the orbit camera turns over with it. While the
		// player rows, and after the stop until the field is identity again (1 s, at most 3 s), both AnimObject nodes are
		// written back to their pre-clip local transform every time, whatever the delta, and the field is reset to identity.
		static bool QuatIsIdentity(const RE::NiQuaternion& q) { return std::abs(q.w) > 0.9999f; }

		bool AnimObjHoldWanted()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!animObjFixOn_ || !camBaseValid_ || !player || !playerSkim_ || rowAnimGpma_ || RowActor(player).get() != player) return false;
			if (rowAnimOn_) {
				animObjHoldUntil_ = rideTime_ + 1.0f;
				animObjHoldCap_ = rideTime_ + 3.0f;
				return true;
			}
			if (rideTime_ <= animObjHoldUntil_) return true;
			auto* cam = RE::PlayerCamera::GetSingleton();
			const auto* tps = cam && cam->currentState && cam->currentState->id == RE::CameraState::kThirdPerson ?
				static_cast<const RE::ThirdPersonState*>(cam->currentState.get()) : nullptr;
			return tps && !QuatIsIdentity(tps->animationRotation) && rideTime_ <= animObjHoldCap_;
		}

		void HoldAnimObjects(const char* a_where)
		{
			if (!AnimObjHoldWanted()) return;
			// 56 (v1.0.56): the AnimObject bones carry the oars now; they are left to the clip. The camera's animationRotation is
			// still reset to identity (ResetAnimRotation), which is what stopped the flip.
			if (BoatBow::BakedOarsOn()) {
				if (!animObjHoldOffLogged_) {
					animObjHoldOffLogged_ = true;
					logger::info("Cam3rd: AnimObjectA/B left to the clip (baked oars); animationRotation still reset [{}]", a_where);
				}
				return;
			}
			auto* root3d = RE::PlayerCharacter::GetSingleton()->Get3D(false);
			if (!root3d) return;
			float deltas[2]{};
			for (int i = kCamBoneGuarded; i < static_cast<int>(std::size(kCamBones)); ++i) {
				auto& b = camBase_[i];
				if (!b.valid) continue;
				auto* node = root3d->GetObjectByName(b.name.c_str());
				if (!node) continue;
				deltas[i - kCamBoneGuarded] = RotDeltaDeg(b.rot, node->local.rotate);
				node->local.rotate = b.rot;
				node->local.translate = b.pos;
				RE::NiUpdateData data{};
				node->Update(data);
			}
			animObjMaxDelta_[0] = (std::max)(animObjMaxDelta_[0], deltas[0]);
			animObjMaxDelta_[1] = (std::max)(animObjMaxDelta_[1], deltas[1]);
			const auto now = std::chrono::steady_clock::now();
			if (now - animObjLogAt_ >= std::chrono::seconds(1)) {
				animObjLogAt_ = now;
				logger::info("Cam3rd: AnimObjectA delta {:.1f} restored | AnimObjectB delta {:.1f} restored (largest this second {:.1f} | {:.1f}) [{}]{}",
					deltas[0], deltas[1], animObjMaxDelta_[0], animObjMaxDelta_[1], a_where, rowAnimOn_ ? "" : " (after the stop)");
				animObjMaxDelta_[0] = animObjMaxDelta_[1] = 0.0f;
			}
		}

		// The camera's own copy of the bone: reset before and after ThirdPersonState::Update while the hold runs.
		void ResetAnimRotation(RE::ThirdPersonState* a_tps, const char* a_where)
		{
			if (!a_tps || QuatIsIdentity(a_tps->animationRotation) || !AnimObjHoldWanted()) return;
			const auto q = a_tps->animationRotation;
			a_tps->animationRotation = RE::NiQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
			const auto now = std::chrono::steady_clock::now();
			if (now - animRotLogAt_ >= std::chrono::seconds(1)) {
				animRotLogAt_ = now;
				logger::info("Cam3rd: animationRotation reset from ({:.3f},{:.3f},{:.3f},{:.3f}) [{}]", q.w, q.x, q.y, q.z, a_where);
			}
		}

		// ThirdPersonState::GetRotation (slot 4), after the game filled a_rot: the view is rebuilt from its forward
		// direction only (21): heading kept, pitch clamped to +/-80 degrees, up = world +Z. The camera's own up is not used.
		void FixCamRotation(RE::ThirdPersonState* a_state, RE::NiQuaternion& a_rot)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !playerSkim_ || RowActor(player).get() != player) return;  // only when the player is the rower
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!camHookLogged_) {
				camHookLogged_ = true;
				logger::info("Cam3rd: hook ThirdPersonState::GetRotation (slot 4) fired");
			}
			const int stateId = cam && cam->currentState ? static_cast<int>(cam->currentState->id) : -1;
			const bool firstPerson = cam && cam->IsInFirstPerson();
			const auto now = std::chrono::steady_clock::now();
			const bool logNow = now - camFixLogAt_ >= std::chrono::seconds(1);
			if (logNow) camFixLogAt_ = now;
			// 20.3: the bones 1 s and 3 s after OarsRowStart on the player (the clip is playing by then).
			if (!camBoneLogAt_.empty() && rideTime_ >= camBoneLogAt_.front() && camLogOn_) {
				camBoneLogAt_.erase(camBoneLogAt_.begin());
				auto* root3d = player->Get3D(false);
				for (const char* n : { "NPC Root [Root]", "NPC Head [Head]", "Camera3rd [Cam3]", "NPC COM [COM ]", "Camera Control" }) {
					auto* node = root3d ? root3d->GetObjectByName(n) : nullptr;
					if (!node) { logger::info("Cam3rd node '{}' (clip playing): not found", n); continue; }
					const auto& m = node->world.rotate;
					logger::info("Cam3rd node '{}' (clip playing): world up=({:.2f},{:.2f},{:.2f}) forward=({:.2f},{:.2f},{:.2f})", n,
						m.entry[0][2], m.entry[1][2], m.entry[2][2], m.entry[0][1], m.entry[1][1], m.entry[2][1]);
				}
			}
			if (firstPerson || !CamUprightWanted()) {
				// 25: read only. The game's own rotation is measured and left exactly as it is.
				if (logNow && camLogOn_) {
					const float w = a_rot.w, x = a_rot.x, y = a_rot.y, z = a_rot.z;
					const RE::NiPoint3 fr{ 2.0f * (x * y - w * z), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + w * x) };
					logger::info("Cam3rd: state {}, fix running no ({}), up.z {:.2f}, fwd ({:.2f},{:.2f},{:.2f}) pitch {:.1f} yaw {:.1f}{}", stateId,
						firstPerson ? "first person" : !camFixOn_ ? "oas camfix is off" : "row loop is the fallback", 1.0f - 2.0f * (x * x + y * y),
						fr.x, fr.y, fr.z, std::asin(std::clamp(fr.z, -1.0f, 1.0f)) * 57.2958f, std::atan2(fr.x, fr.y) * 57.2958f, rowAnimOn_ ? "" : " (between strokes)");
				}
				return;
			}
			// Quaternion to matrix: the exact inverse of NiQuaternion::SetRotation in CommonLibSSE-NG. Column 1 is forward.
			const float w = a_rot.w, x = a_rot.x, y = a_rot.y, z = a_rot.z;
			RE::NiPoint3 f{ 2.0f * (x * y - w * z), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + w * x) };
			const float upZ = 1.0f - 2.0f * (x * x + y * y);
			const float fl = f.Length();
			if (fl < 1e-4f) return;
			f = f / fl;
			const float yaw = std::atan2(f.x, f.y);
			const float pitch = std::asin(std::clamp(f.z, -1.0f, 1.0f));  // + = looking up
			static constexpr float kMaxPitch = 80.0f * 0.0174533f;
			const float pitchC = std::clamp(pitch, -kMaxPitch, kMaxPitch);
			const RE::NiPoint3 fc{ std::sin(yaw) * std::cos(pitchC), std::cos(yaw) * std::cos(pitchC), std::sin(pitchC) };
			const RE::NiPoint3 r{ std::cos(yaw), -std::sin(yaw), 0.0f };  // forward x world up
			const RE::NiPoint3 up{ r.y * fc.z - r.z * fc.y, r.z * fc.x - r.x * fc.z, r.x * fc.y - r.y * fc.x };
			RE::NiMatrix3 m;
			m.entry[0][0] = r.x; m.entry[1][0] = r.y; m.entry[2][0] = r.z;
			m.entry[0][1] = fc.x; m.entry[1][1] = fc.y; m.entry[2][1] = fc.z;
			m.entry[0][2] = up.x; m.entry[1][2] = up.y; m.entry[2][2] = up.z;
			a_rot = RE::NiQuaternion(m);
			camStateFwd_ = fc;  // 23: the force step builds the final camera from this, not from the camera root's own forward
			camStateFwdValid_ = true;
			if (logNow) {
				const auto* tps = cam && cam->currentState && cam->currentState->id == RE::CameraState::kThirdPerson ? static_cast<RE::ThirdPersonState*>(cam->currentState.get()) : nullptr;
				const char* target = tps && tps->thirdPersonCameraObj ? tps->thirdPersonCameraObj->name.c_str() : "";
				logger::info("Cam3rd: state {}, fix running yes, up.z before {:.2f}, fwd ({:.2f},{:.2f},{:.2f}) pitch {:.1f} yaw {:.1f}, player pitch {:.1f}, target '{}'{}",
					stateId, upZ, f.x, f.y, f.z, pitch * 57.2958f, yaw * 57.2958f, player->GetAngle().x * -57.2958f, target ? target : "", rowAnimOn_ ? "" : " (between strokes)");
				if (camBaseValid_ && rowAnimOn_) {
					logger::info("Cam3rd: bone delta {} {:.1f} | {} {:.1f} | {} {:.1f}{}", camBase_[0].name, camBase_[0].delta, camBase_[1].name, camBase_[1].delta,
						camBase_[2].name, camBase_[2].delta, (camBase_[0].delta > 5.0f || camBase_[1].delta > 5.0f || camBase_[2].delta > 5.0f) ? " (put back)" : "");
				}
			}
			if (std::abs(pitchC - pitch) > 0.0005f && (logNow || !camClampLogged_)) {
				camClampLogged_ = true;
				logger::info("Cam3rd: pitch clamped from {:.1f} to {:.1f}", pitch * 57.2958f, pitchC * 57.2958f);
			}
		}

		// Second chance on the camera root after PlayerCamera::Update. The game seems to call that function directly (no
		// line in the v1.0.20 log), so this probably never runs; it says so once if it ever does.
		// 47.5 (v1.0.43): a camera Z step over 100 in one frame (a hard landing) is low-passed with a 60 ms critically damped
		// filter until it has caught up; otherwise the camera is left alone.
		float camFiltZ_{ 0.0f }, camFiltV_{ 0.0f }, camFiltPrev_{ 0.0f };
		bool camFiltHave_{ false }, camFiltOn_{ false };
		void FilterCameraZ(RE::PlayerCamera* a_cam)
		{
			auto* root = a_cam ? a_cam->cameraRoot.get() : nullptr;
			if (!root || !playerSkim_) { camFiltHave_ = camFiltOn_ = false; return; }
			const float dt = std::clamp(RE::GetSecondsSinceLastFrame(), 0.001f, 0.1f);
			const float z = root->world.translate.z;
			if (!camFiltHave_) { camFiltHave_ = true; camFiltPrev_ = camFiltZ_ = z; camFiltV_ = 0.0f; return; }
			if (!camFiltOn_ && std::abs(z - camFiltPrev_) > 100.0f) {
				camFiltOn_ = true;
				camFiltZ_ = camFiltPrev_;
				camFiltV_ = 0.0f;
				logger::info("Cam: Z step {:.0f} in one frame, smoothing it", z - camFiltPrev_);
			}
			camFiltPrev_ = z;
			if (!camFiltOn_) return;
			constexpr float om = 1.0f / 0.06f;
			const int n = (std::max)(1, static_cast<int>(std::ceil(dt / 0.005f)));
			const float h = dt / static_cast<float>(n);
			for (int i = 0; i < n; ++i) {
				camFiltV_ += (om * om * (z - camFiltZ_) - 2.0f * om * camFiltV_) * h;
				camFiltZ_ += camFiltV_ * h;
			}
			if (std::abs(z - camFiltZ_) < 1.0f && std::abs(camFiltV_) < 20.0f) { camFiltOn_ = false; return; }
			const float pScale = root->parent && root->parent->world.scale > 0.0f ? root->parent->world.scale : 1.0f;
			root->local.translate.z += (camFiltZ_ - z) / pScale;
			RE::NiUpdateData data{};
			root->Update(data);
		}

		void FixCameraUpright(RE::PlayerCamera* a_cam)
		{
			if (!camRootHookSeen_) {
				camRootHookSeen_ = true;
				logger::info("Cam hook: PlayerCamera::Update thunk called");
			}
			auto* root = a_cam ? a_cam->cameraRoot.get() : nullptr;
			if (!root || a_cam->IsInFirstPerson() || !CamUprightWanted()) return;
			RE::NiMatrix3 fixed;
			float roll = 0.0f, upZ = 0.0f;
			if (!UprightRotation(root->world.rotate, fixed, roll, upZ)) return;
			root->local.rotate = root->parent ? root->parent->world.rotate.Transpose() * fixed : fixed;
			RE::NiUpdateData data{};
			root->Update(data);
		}

		// 22 (v1.0.23): every rotation logged up to v1.0.22 was upright while the view was still inverted, so the flip is
		// after GetRotation. The node the renderer draws with is the NiCamera under PlayerCamera::cameraRoot. Its rotation
		// relative to cameraRoot is stored while the game owns it (third person, not rowing); while the player rows in third
		// person the camera root is rebuilt from its forward (pitch clamp +/-80, up = world +Z) and the NiCamera is put back
		// on that stored offset. Runs at the end of the player update and after ThirdPersonState::Update (slot 3).
		static RE::NiCamera* FindNiCamera(RE::NiNode* a_root)
		{
			if (a_root) {
				for (auto& c : a_root->GetChildren()) {
					if (auto* nc = c ? netimmerse_cast<RE::NiCamera*>(c.get()) : nullptr) return nc;
				}
			}
			return RE::Main::WorldRootCamera();
		}

		// The class of a camera state, read from its vtable (22.3: is state 9 really ThirdPersonState?).
		static const char* CamStateClass(const RE::TESCameraState* a_state)
		{
			if (!a_state) return "none";
			const auto vt = *reinterpret_cast<const std::uintptr_t*>(a_state);
			const std::pair<std::uintptr_t, const char*> known[] = {
				{ RE::VTABLE_ThirdPersonState[0].address(), "ThirdPersonState" },
				{ RE::VTABLE_HorseCameraState[0].address(), "HorseCameraState" },
				{ RE::VTABLE_BleedoutCameraState[0].address(), "BleedoutCameraState" },
				{ RE::VTABLE_DragonCameraState[0].address(), "DragonCameraState" },
				{ RE::VTABLE_FurnitureCameraState[0].address(), "FurnitureCameraState" },
				{ RE::VTABLE_FirstPersonState[0].address(), "FirstPersonState" },
				{ RE::VTABLE_FreeCameraState[0].address(), "FreeCameraState" },
				{ RE::VTABLE_TweenMenuCameraState[0].address(), "TweenMenuCameraState" },
				{ RE::VTABLE_VATSCameraState[0].address(), "VATSCameraState" },
				{ RE::VTABLE_AutoVanityState[0].address(), "AutoVanityState" },
				{ RE::VTABLE_IronSightsState[0].address(), "IronSightsState" },
				{ RE::VTABLE_PlayerCameraTransitionState[0].address(), "PlayerCameraTransitionState" } };
			for (const auto& [addr, name] : known) {
				if (vt == addr) return name;
			}
			return "unknown";
		}

		// Node rotation built from a forward direction only: heading kept, pitch clamped to +/-80, up = world +Z.
		// Columns: right (X), forward (Y), up (Z).
		static RE::NiMatrix3 UprightFromForward(const RE::NiPoint3& a_fwd)
		{
			const float yaw = std::atan2(a_fwd.x, a_fwd.y);
			static constexpr float kMaxPitch = 80.0f * 0.0174533f;
			const float pitch = std::clamp(std::asin(std::clamp(a_fwd.z, -1.0f, 1.0f)), -kMaxPitch, kMaxPitch);
			const RE::NiPoint3 fc{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), std::sin(pitch) };
			const RE::NiPoint3 r{ std::cos(yaw), -std::sin(yaw), 0.0f };
			const RE::NiPoint3 up{ r.y * fc.z - r.z * fc.y, r.z * fc.x - r.x * fc.z, r.x * fc.y - r.y * fc.x };
			RE::NiMatrix3 m;
			m.entry[0][0] = r.x; m.entry[1][0] = r.y; m.entry[2][0] = r.z;
			m.entry[0][1] = fc.x; m.entry[1][1] = fc.y; m.entry[2][1] = fc.z;
			m.entry[0][2] = up.x; m.entry[1][2] = up.y; m.entry[2][2] = up.z;
			return m;
		}

		static std::string CamCols(const RE::NiMatrix3& m)
		{
			return std::format("x=({:.2f},{:.2f},{:.2f}) y=({:.2f},{:.2f},{:.2f}) z=({:.2f},{:.2f},{:.2f})",
				m.entry[0][0], m.entry[1][0], m.entry[2][0], m.entry[0][1], m.entry[1][1], m.entry[2][1], m.entry[0][2], m.entry[1][2], m.entry[2][2]);
		}

		void ForceFinalCamera(const char* a_where)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			auto* root = cam ? cam->cameraRoot.get() : nullptr;
			auto* nc = FindNiCamera(root);
			if (!root || !nc || !cam->currentState) return;
			auto* player = RE::PlayerCharacter::GetSingleton();
			const bool thirdPerson = cam->currentState->id == RE::CameraState::kThirdPerson;
			const bool rower3rd = thirdPerson && player && playerSkim_ && RowActor(player).get() == player;
			const bool rowing3rd = rower3rd && camFixOn_ && !rowAnimGpma_;
			if (!rowing3rd) {
				// The game's own NiCamera offset from the camera root, taken while we do not touch either.
				if (thirdPerson && !playerSkim_) {
					ncRel0_ = root->world.rotate.Transpose() * nc->world.rotate;
					ncRel0Valid_ = true;
				}
				// 25: with camfix off the camera is the game's; it is only read and logged once a second.
				if (rower3rd && camLogOn_) {
					if (!camStateLogged_) {
						camStateLogged_ = true;
						logger::info("Cam3rd: active camera state id {} class {}, NiCamera '{}' parent '{}' (read only)",
							static_cast<int>(cam->currentState->id), CamStateClass(cam->currentState.get()), nc->name.c_str(), nc->parent ? nc->parent->name.c_str() : "none");
					}
					const auto now = std::chrono::steady_clock::now();
					if (now - camFinalLogAt_ >= std::chrono::seconds(1)) {
						camFinalLogAt_ = now;
						const auto* par = root->parent;
						logger::info("Cam3rd: NiCamera world {} | cameraRoot world {} | root parent '{}' {} [{}, read only{}]",
							CamCols(nc->world.rotate), CamCols(root->world.rotate), par ? par->name.c_str() : "none",
							par ? CamCols(par->world.rotate) : std::string{}, a_where, rowAnimOn_ ? "" : ", between strokes");
					}
				}
				return;
			}
			if (!ncRel0Valid_) {
				ncRel0_ = root->world.rotate.Transpose() * nc->world.rotate;
				ncRel0Valid_ = true;
				logger::info("Cam3rd: NiCamera offset taken while rowing (none stored before boarding): {}", CamCols(ncRel0_));
			}
			if (!camStateLogged_) {
				camStateLogged_ = true;
				logger::info("Cam3rd: active camera state id {} class {}, NiCamera '{}' parent '{}' (cameraRoot '{}'), WorldRootCamera {}, stored offset {}",
					static_cast<int>(cam->currentState->id), CamStateClass(cam->currentState.get()), nc->name.c_str(),
					nc->parent ? nc->parent->name.c_str() : "none", root->name.c_str(),
					nc == RE::Main::WorldRootCamera() ? "is this NiCamera" : "is another node", CamCols(ncRel0_));
			}
			const auto now = std::chrono::steady_clock::now();
			const bool logNow = now - camFinalLogAt_ >= std::chrono::seconds(1);
			if (logNow) camFinalLogAt_ = now;

			// Before: what the renderer would draw with. NiCamera axes are Gamebryo's (x = view, y = up); the stored offset
			// maps them, so "up.z" is read through it as the camera root's z axis.
			const RE::NiMatrix3 ncBefore = nc->world.rotate;
			const RE::NiMatrix3 ncAsRoot = ncBefore * ncRel0_.Transpose();  // the NiCamera expressed in root axes
			const float ncUpZ = ncAsRoot.entry[2][2];
			// 23 (v1.0.24): while the clip plays the camera root comes out turned 180 degrees about its right axis (forward and
			// up negated), so its own forward is the reversed one. The rotation is built from the forward the state returned.
			RE::NiPoint3 f = camStateFwdValid_ ? camStateFwd_
				: RE::NiPoint3{ root->world.rotate.entry[0][1], root->world.rotate.entry[1][1], root->world.rotate.entry[2][1] };
			const float fl = f.Length();
			if (fl < 1e-4f) return;
			f = f / fl;
			const RE::NiMatrix3 rootWant = UprightFromForward(f);
			const RE::NiMatrix3 ncWant = rootWant * ncRel0_;
			const RE::NiPoint3 xBefore{ ncBefore.entry[0][0], ncBefore.entry[1][0], ncBefore.entry[2][0] };  // NiCamera view direction
			const float dotBefore = xBefore.x * f.x + xBefore.y * f.y + xBefore.z * f.z;
			if (logNow) {
				const auto* par = root->parent;
				logger::info("Cam3rd: NiCamera world {} (up.z {:.2f}, dot {:.2f}) | cameraRoot world {} | root parent '{}' {} [{}]",
					CamCols(ncBefore), ncUpZ, dotBefore, CamCols(root->world.rotate),
					par ? par->name.c_str() : "none", par ? CamCols(par->world.rotate) : std::string{}, a_where);
			}
			// 27 (v1.0.26): the flip is the orbit camera pitched 180 degrees: it sits on the far side of the player looking
			// back. Forcing the rotation alone (v1.0.24) turned it to look away from the player. Only that flipped case is
			// corrected, and position and rotation together: the camera is mirrored through the point it looks at.
			if (dotBefore >= 0.0f) return;
			auto* root3d = player->Get3D(false);
			auto* c3 = root3d ? root3d->GetObjectByName("Camera3rd [Cam3]") : nullptr;
			if (!c3) return;
			const RE::NiPoint3 camPos = nc->world.translate;
			const float d = (camPos - c3->world.translate).Length();
			if (d < 1.0f) return;
			const RE::NiPoint3 pivot = camPos + xBefore * d;
			const RE::NiPoint3 fixedPos = pivot - f * d;
			const RE::NiPoint3 rootPos = root->world.translate + (fixedPos - camPos);
			RE::NiUpdateData data{};
			if (auto* par = root->parent) {
				const float sc = par->world.scale != 0.0f ? par->world.scale : 1.0f;
				root->local.translate = (par->world.rotate.Transpose() * (rootPos - par->world.translate)) / sc;
				root->local.rotate = par->world.rotate.Transpose() * rootWant;
			} else {
				root->local.translate = rootPos;
				root->local.rotate = rootWant;
			}
			root->Update(data);
			if (RotDeltaDeg(nc->world.rotate, ncWant) >= 1.0f) {
				nc->local.rotate = nc->parent ? nc->parent->world.rotate.Transpose() * ncWant : ncWant;
				nc->Update(data);
			}
			if (logNow || !camForceLogged_) {
				camForceLogged_ = true;
				const auto& w = nc->world.rotate;
				const RE::NiMatrix3 after = w * ncRel0_.Transpose();
				const float dotAfter = w.entry[0][0] * f.x + w.entry[1][0] * f.y + w.entry[2][0] * f.z;
				auto* head = root3d->GetObjectByName("NPC Head [Head]");
				const RE::NiPoint3 hp = head ? head->world.translate : RE::NiPoint3{};
				const RE::NiPoint3 np = nc->world.translate;
				logger::info("Cam3rd: mirrored position d={:.1f} dot before {:.2f} after {:.2f} up.z {:.2f}{} | camera was ({:.0f},{:.0f},{:.0f}) now ({:.0f},{:.0f},{:.0f}) pivot ({:.0f},{:.0f},{:.0f}) Camera3rd ({:.0f},{:.0f},{:.0f}) head ({:.0f},{:.0f},{:.0f}) [{}]",
					d, dotBefore, dotAfter, after.entry[2][2], dotAfter < 0.99f || after.entry[2][2] < 0.17f ? " (NOT FIXED)" : "",
					camPos.x, camPos.y, camPos.z, np.x, np.y, np.z, pivot.x, pivot.y, pivot.z,
					c3->world.translate.x, c3->world.translate.y, c3->world.translate.z, hp.x, hp.y, hp.z, a_where);
			}
		}

		// 27.1 (v1.0.26): every frame for 3 s after the row clip starts or stops on the player (third person), the
		// ThirdPersonState angles and offsets in one line, to find the field that carries the extra 180 degrees.
		void LogTpsFields()
		{
			++camFrame_;
			auto* cam = RE::PlayerCamera::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!camLogOn_ || !cam || !player || !playerSkim_ || RowActor(player).get() != player || !cam->currentState ||
				cam->currentState->id != RE::CameraState::kThirdPerson) return;
			// 28 (v1.0.27): every frame for 40 frames after each row start (animationRotation must stay identity), otherwise
			// once a second.
			if (rowAnimOn_ != tpsPrevRowAnim_) {
				tpsPrevRowAnim_ = rowAnimOn_;
				if (rowAnimOn_) tpsBurst_ = 40;
			}
			const auto now = std::chrono::steady_clock::now();
			if (tpsBurst_ > 0) --tpsBurst_;
			else if (now - tpsLogAt_ >= std::chrono::seconds(1)) tpsLogAt_ = now;
			else return;
			if (tpsLogLines_ >= 1500) return;
			if (++tpsLogLines_ == 1500) logger::info("Cam3rd: TPS fields log reached 1500 lines this boarding, stopping");
			auto* t = static_cast<RE::ThirdPersonState*>(cam->currentState.get());
			auto* nc = FindNiCamera(cam->cameraRoot.get());
			const float dot = nc && camStateFwdValid_ ?
				nc->world.rotate.entry[0][0] * camStateFwd_.x + nc->world.rotate.entry[1][0] * camStateFwd_.y + nc->world.rotate.entry[2][0] * camStateFwd_.z : 0.0f;
			const auto ang = player->GetAngle();
			logger::info("Cam3rd: TPS fields frame {} {} rotation=({:.3f},{:.3f},{:.3f},{:.3f}) translation=({:.1f},{:.1f},{:.1f}) posOffsetExpected=({:.1f},{:.1f},{:.1f}) "
				"posOffsetActual=({:.1f},{:.1f},{:.1f}) targetZoomOffset={:.3f} currentZoomOffset={:.3f} targetYaw={:.3f} currentYaw={:.3f} savedZoomOffset={:.3f} "
				"pitchZoomOffset={:.3f} unk8C={:.3f} collisionPos=({:.1f},{:.1f},{:.1f}) collisionPosValid={:.3f} animatedBoneName='{}' "
				"animationRotation=({:.3f},{:.3f},{:.3f},{:.3f}) freeRotation=({:.3f},{:.3f}) freeRotationEnabled={} stateNotActive={} toggleAnimCam={} applyOffsets={} "
				"| camera yaw={:.3f} pos=({:.1f},{:.1f},{:.1f}) | player pitch={:.3f} heading={:.3f} | dot(NiCamera x, state fwd)={:.2f}",
				camFrame_, rowAnimOn_ ? "rowing" : "between strokes",
				t->rotation.w, t->rotation.x, t->rotation.y, t->rotation.z, t->translation.x, t->translation.y, t->translation.z,
				t->posOffsetExpected.x, t->posOffsetExpected.y, t->posOffsetExpected.z, t->posOffsetActual.x, t->posOffsetActual.y, t->posOffsetActual.z,
				t->targetZoomOffset, t->currentZoomOffset, t->targetYaw, t->currentYaw, t->savedZoomOffset, t->pitchZoomOffset, t->unk8C,
				t->collisionPos.x, t->collisionPos.y, t->collisionPos.z, t->collisionPosValid, t->animatedBoneName.c_str(),
				t->animationRotation.w, t->animationRotation.x, t->animationRotation.y, t->animationRotation.z, t->freeRotation.x, t->freeRotation.y,
				t->freeRotationEnabled ? 1 : 0, t->stateNotActive ? 1 : 0, t->toggleAnimCam ? 1 : 0, t->applyOffsets ? 1 : 0,
				cam->yaw, cam->pos.x, cam->pos.y, cam->pos.z, ang.x, ang.z, dot);
		}

		void CamAfterStateUpdate(RE::ThirdPersonState* a_tps)
		{
			if (!camSlot3Logged_ && playerSkim_) {
				camSlot3Logged_ = true;
				logger::info("Cam hook: ThirdPersonState::Update (slot 3) fired");
			}
			LogTpsFields();  // reads animationRotation as the game left it, before the reset below
			ResetAnimRotation(a_tps, "after ThirdPersonState::Update");
			ForceFinalCamera("after ThirdPersonState::Update");
		}

		// ---- 56.43: other camera / movement mods ----
		// True Directional Movement turns the player to face the movement input and, in its free camera, takes that turn
		// back off ThirdPersonState::freeRotation so the view stays still. Aboard, ApplyPlayerBody puts the player's heading
		// back on the seat every frame (with no matching freeRotation change), so while W is held for rowing each frame TDM
		// turns, takes the turn off the camera, we undo the turn: the camera spins on and on. TDM also forces headtracking on
		// every frame (the seated rower twists toward the camera). Its modder API (V1) pauses both: requested on boarding,
		// released on leaving. Calls must be made on TDM's thread (the main thread), so it is done from the player update.
		// SmoothCam is only detected and logged (its camera follows the player's heading like the game's own camera).
		TDM_API::IVTDM1* tdm_{ nullptr };
		bool compatLooked_{ false };
		bool tdmHeld_{ false };
		void TendModCompat(bool a_aboard)
		{
			if (!compatLooked_) {
				compatLooked_ = true;
				if (auto* mod = GetModuleHandleA("TrueDirectionalMovement.dll")) {
					using RequestFn = void* (*)(TDM_API::InterfaceVersion);
					if (auto fn = reinterpret_cast<RequestFn>(GetProcAddress(mod, "RequestPluginAPI"))) tdm_ = static_cast<TDM_API::IVTDM1*>(fn(TDM_API::InterfaceVersion::V1));
					logger::info("Compat: True Directional Movement loaded, API {}", tdm_ ? "found (its directional movement and headtracking pause while aboard)" : "NOT found (an old TDM? the camera may spin while rowing)");
				} else {
					logger::info("Compat: True Directional Movement not loaded");
				}
				logger::info("Compat: SmoothCam {}", GetModuleHandleA("SmoothCam.dll") ? "loaded" : "not loaded");
			}
			if (!tdm_ || a_aboard == tdmHeld_) return;
			if (GetCurrentThreadId() != tdm_->GetTDMThreadId()) return;  // not TDM's thread: next frame
			const auto handle = SKSE::GetPluginHandle();
			auto name = [](TDM_API::APIResult a_r) {
				switch (a_r) {
				case TDM_API::APIResult::OK: return "OK";
				case TDM_API::APIResult::NotOwner: return "not owner";
				case TDM_API::APIResult::MustKeep: return "TDM must keep it";
				case TDM_API::APIResult::AlreadyGiven: return "already ours";
				case TDM_API::APIResult::AlreadyTaken: return "held by another mod";
				case TDM_API::APIResult::BadThread: return "wrong thread";
				default: return "?";
				}
			};
			if (a_aboard) {
				const auto m = tdm_->RequestDisableDirectionalMovement(handle);
				const auto h = tdm_->RequestDisableHeadtracking(handle);
				logger::info("Compat: TDM paused for the ride (directional movement: {}, headtracking: {})", name(m), name(h));
			} else {
				const auto m = tdm_->ReleaseDisableDirectionalMovement(handle);
				const auto h = tdm_->ReleaseDisableHeadtracking(handle);
				logger::info("Compat: TDM given back (directional movement: {}, headtracking: {})", name(m), name(h));
			}
			tdmHeld_ = a_aboard;
		}

		void ReapplyBodyHide()
		{
			if (playerSkim_) ApplyPlayerBody(RE::PlayerCharacter::GetSingleton());
			ApplyPassengerTilt();
		}

		bool IsPlayerSkim() const { return playerSkim_; }

		// ---- Passengers: followers ride in fixed slots on the boat ----------
		// Slots are in the vehicle frame: x = right, y = forward, z = up from the vehicle point.
		// Only dogs (the bow slot) for now. Humans need seats, which come with our own mesh.

		// Tilts each passenger's skeleton to the boat's full rotation (the game ignores the roll
		// angle on actors, so the root node is rotated directly). Called after the player update and
		// again from the camera updates, which run closer to rendering.
		static bool IsDogActor(RE::Actor* a_actor)
		{
			const auto* race = a_actor ? a_actor->GetRace() : nullptr;
			const char* id = race ? race->GetFormEditorID() : nullptr;
			return id && std::string_view(id).find("Dog") != std::string_view::npos;
		}

		static constexpr bool kFollowerRows = true;  // the follower in the first seat rows (the player sits at the stern)

		// The actor that plays the rowing animation: the human follower in seat 0, or the player when there is none.
		RE::NiPointer<RE::Actor> RowActor(RE::PlayerCharacter* a_player)
		{
			if (kFollowerRows) {
				for (auto& h : passengers_) {
					auto a = h.get();
					if (a && !a->IsDead() && !IsDogActor(a.get()) && GetHumanSeat(a.get()).slot == 0) return a;
				}
			}
			return RE::NiPointer<RE::Actor>(a_player);
		}

		struct HumanSeat { RE::FormID id; int slot; RE::NiPoint3 pos; float yaw; };
		enum class BoardChoice { kDefault, kSteer, kRow };  // v1.0.14: what the player activated (hull, helm bench, rower's bench)

		// Three benches, two seats each: front (250), middle (150), back (50). Rough guesses, tune with numpad.
		// Seat defaults per slot (right / forward / up, and the turn), from Miguel's numpad tuning in v1.0.9. Slot 0 is the
		// rowing seat. A seat tuned live with the numpad is kept per actor for the session; these are the starting values.
		// Slot 6 (v1.0.14) is the NPC helm seat: the player's stern seat (forward 8), only used while the player rows (14.8).
		// Its height is the helm bench's (up -57) less the 14 the seated body sits below the bench top, as on the rower's bench.
		static constexpr int kHelmSlot = 6;
		static constexpr int kSeatCount = 7;
		static inline const RE::NiPoint3 kSeatSlots[kSeatCount] = {
			{ 0.0f, 259.0f, -84.0f }, { 41.0f, 351.0f, -86.0f },  // slot 0: v1.0.19 (17.1) Miguel's rower tuning (was 262 / -80)
			{ -40.0f, 150.0f, -84.0f }, { 40.0f, 147.0f, -84.0f },
			{ -40.0f, 352.0f, -84.0f }, { -27.0f, 289.0f, -84.0f },
			{ 0.0f, 8.0f, -71.0f } };
		static constexpr float kSeatYaw[kSeatCount] = { 3.14159265f, 0.2967f, 0.0f, 0.0f, 0.0f, -0.6458f, 0.0f };  // 180 (56.11: was 175, the oars sit evenly now), 17, 0, 0, 0, -37, 0 degrees
		// v1.0.14: seats are tuned per slot (the actors change seats every boarding now), starting from the defaults above.
		RE::NiPoint3 slotPos_[kSeatCount] = { kSeatSlots[0], kSeatSlots[1], kSeatSlots[2], kSeatSlots[3], kSeatSlots[4], kSeatSlots[5], kSeatSlots[6] };
		float slotYaw_[kSeatCount] = { kSeatYaw[0], kSeatYaw[1], kSeatYaw[2], kSeatYaw[3], kSeatYaw[4], kSeatYaw[5], kSeatYaw[6] };

		bool SlotTaken(int a_slot) const
		{
			for (auto& hs : humanSeats_) if (hs.slot == a_slot) return true;
			for (auto& hs : tempSeats_) if (hs.slot == a_slot) return true;
			return false;
		}

		HumanSeat& GetHumanSeat(RE::Actor* a_actor)
		{
			for (auto& hs : tempSeats_) if (hs.id == a_actor->GetFormID()) return hs;  // a late boarder's seat for this ride only
			for (auto& hs : humanSeats_) if (hs.id == a_actor->GetFormID()) return hs;
			// Not seated at boarding (a late boarder while an NPC rows): the first free seat. Never the helm seat (14.8 only).
			int slot = 5;
			for (int i = 0; i < 6; ++i) {
				if (!SlotTaken(i)) { slot = i; break; }
			}
			humanSeats_.push_back({ a_actor->GetFormID(), slot, slotPos_[slot], slotYaw_[slot] });
			logger::info("Seat: {:08X} '{}' takes default seat {}", a_actor->GetFormID(), a_actor->GetName(), slot);
			return humanSeats_.back();
		}

		// 14.6 / 14.8: who sits where, decided at every boarding. The humans aboard in FormID order; the rotation counter
		// (rowerTurn_, kept in the co-save, reset when that list changes) moves on by one each boarding. The player steers:
		// the NPC at rowerTurn_ % N rows (slot 0), the rest fill 1..5. The player rows: the helm (slot 6) is filled first (B4, any
		// number of NPCs): the next one after the would-be rower, or the only NPC; everyone else takes 4, 5, 2, 3, 1 (never
		// slot 0, the player's rowing seat).
		void AssignBoardingSeats(bool a_playerRows)
		{
			humanSeats_.clear();
			tempSeats_.clear();
			std::vector<RE::NiPointer<RE::Actor>> humans = BoardedHumans();
			std::sort(humans.begin(), humans.end(), [](const auto& x, const auto& y) { return x->GetFormID() < y->GetFormID(); });
			const std::size_t n = humans.size();
			if (n == 0) {
				logger::info("Seats: rower {} helm {} (nobody else aboard)", a_playerRows ? "the player" : "nobody", a_playerRows ? "nobody" : "the player");
				return;
			}
			std::uint64_t hash = 1469598103934665603ull;
			for (auto& h : humans) hash = (hash ^ h->GetFormID()) * 1099511628211ull;
			if (hash != rowerListHash_) { rowerListHash_ = hash; rowerTurn_ = 0; } else ++rowerTurn_;
			const std::size_t first = rowerTurn_ % n;
			std::vector<RE::Actor*> order;
			for (std::size_t i = 0; i < n; ++i) order.push_back(humans[(first + i) % n].get());
			auto seat = [&](RE::Actor* a_actor, int a_slot) { humanSeats_.push_back({ a_actor->GetFormID(), a_slot, slotPos_[a_slot], slotYaw_[a_slot] }); };
			std::string rower = "the player", helm = "the player";
			if (!a_playerRows) {
				seat(order[0], 0);
				rower = order[0]->GetName();
				for (std::size_t i = 1; i < n; ++i) seat(order[i], static_cast<int>(i));
			} else {
				// B4 (v1.0.15): the helm is filled first with any number of NPCs (1 or more), before every other seat.
				std::vector<RE::Actor*> rest;
				const std::size_t h = n >= 2 ? 1 : 0;  // the next one after the would-be rower; with one NPC, that NPC
				seat(order[h], kHelmSlot);
				helm = order[h]->GetName();
				logger::info("Helm: {} (priority, {} aboard)", helm, n + 1);
				for (std::size_t i = 0; i < n; ++i) if (i != h) rest.push_back(order[i]);
				const int kOrder[] = { 4, 5, 2, 3, 1 };
				for (std::size_t i = 0; i < rest.size() && i < 5; ++i) seat(rest[i], kOrder[i]);
			}
			for (const auto& hs : humanSeats_) {
				logger::info("Seat: {:08X} takes seat {}", hs.id, hs.slot);
			}
			logger::info("Seats: rower {} helm {} (turn {} of {})", rower, helm, first + 1, n);
		}

		// A late boarder while the player rows: never the rowing seat (slot 0) or the front bench next to the player at
		// forward 263; the stern bench first, then the middle bench. Kept for this ride only (cleared on exit), so it never
		// becomes his permanent seat.
		void AssignLateSeat(RE::Actor* a_actor)
		{
			for (auto& hs : tempSeats_) if (hs.id == a_actor->GetFormID()) return;
			int slot = -1;
			// 14.8: the helm first, if it is empty (the player rows, so it is never the player's seat).
			bool helmUsed = SlotTaken(kHelmSlot);
			for (auto& h : passengers_) {
				auto a = h.get();
				if (a && a.get() != a_actor && !IsDogActor(a.get()) && GetHumanSeat(a.get()).slot == kHelmSlot) helmUsed = true;
			}
			if (!helmUsed) {
				slot = kHelmSlot;
				logger::info("Helm: {} (priority, late boarder)", a_actor->GetName());
			}
			for (int want : { 4, 5, 2, 3 }) {
				if (slot >= 0) break;
				bool used = false;
				for (auto& h : passengers_) {
					auto a = h.get();
					if (a && !IsDogActor(a.get()) && GetHumanSeat(a.get()).slot == want) used = true;
				}
				for (auto& hs : tempSeats_) if (hs.slot == want) used = true;
				if (!used) { slot = want; break; }
			}
			if (slot < 0) slot = 5;
			tempSeats_.push_back({ a_actor->GetFormID(), slot, slotPos_[slot], slotYaw_[slot] });
			logger::info("Late boarder {} seated at slot {} (player is rowing)", a_actor->GetName(), slot);
		}

		std::vector<RE::NiPointer<RE::Actor>> BoardedHumans()
		{
			std::vector<RE::NiPointer<RE::Actor>> out;
			for (auto& h : passengers_) { auto a = h.get(); if (a && !IsDogActor(a.get())) out.push_back(a); }
			return out;
		}

		void ApplyPassengerTilt()
		{
			// Dogs and followers tilt with the boat; 'oas tilt' switches that off for followers.
			if (!playerSkim_ || passengers_.empty()) return;
			RE::NiMatrix3 boatRot;
			if (!BoatBow::RigidRotation(boatRot)) return;
			auto yawMatrix = [](float a_yaw) {
				const float c = std::cos(a_yaw), s = std::sin(a_yaw);
				RE::NiMatrix3 m;
				m.entry[0][0] = c;  m.entry[0][1] = s;  m.entry[0][2] = 0.0f;
				m.entry[1][0] = -s; m.entry[1][1] = c;  m.entry[1][2] = 0.0f;
				m.entry[2][0] = 0.0f; m.entry[2][1] = 0.0f; m.entry[2][2] = 1.0f;
				return m;
			};
			int tiltDogIndex = 0;  // 56.29: per-dog turn
			// 43.6 (v1.0.39): humans get a reduced tilt (pitch x0.40, roll x0.60, backward lean at most 18 deg, forward 12),
			// pivoting at the hip (RiderPivotUp above the root) so the pelvis stays on the seat instead of sweeping the deck.
			RE::NiMatrix3 riderRot = boatRot;
			float riderPitchDeg = 0.0f;
			{
				const RE::NiPoint3 bf{ boatRot.entry[0][1], boatRot.entry[1][1], boatRot.entry[2][1] };
				const RE::NiMatrix3 level = yawMatrix(std::atan2(bf.x, bf.y));
				const RE::NiMatrix3 t = level.Transpose() * boatRot;
				const float tr = std::clamp((t.entry[0][0] + t.entry[1][1] + t.entry[2][2] - 1.0f) * 0.5f, -1.0f, 1.0f);
				const float th = std::acos(tr);
				RE::NiPoint3 v{};
				if (th > 1.0e-4f) {
					const float k = th / (2.0f * std::sin(th));
					v = RE::NiPoint3{ (t.entry[2][1] - t.entry[1][2]) * k, (t.entry[0][2] - t.entry[2][0]) * k, (t.entry[1][0] - t.entry[0][1]) * k };
				}
				v.x = std::clamp(v.x * BoatIni().riderPitchScale, -0.3142f, 0.2094f);  // nose-down boat = backward lean, 18 deg; forward 12 deg
				v.y *= BoatIni().riderRollScale;
				riderPitchDeg = v.x * 57.2958f;
				const float a = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
				RE::NiMatrix3 r = yawMatrix(0.0f);
				if (a > 1.0e-5f) {
					const float kx = v.x / a, ky = v.y / a, kz = v.z / a, sa = std::sin(a), ca = 1.0f - std::cos(a);
					r.entry[0][0] = 1.0f + ca * (-ky * ky - kz * kz); r.entry[0][1] = -sa * kz + ca * kx * ky;          r.entry[0][2] = sa * ky + ca * kx * kz;
					r.entry[1][0] = sa * kz + ca * kx * ky;           r.entry[1][1] = 1.0f + ca * (-kx * kx - kz * kz); r.entry[1][2] = -sa * kx + ca * ky * kz;
					r.entry[2][0] = -sa * ky + ca * kx * kz;          r.entry[2][1] = sa * kx + ca * ky * kz;           r.entry[2][2] = 1.0f + ca * (-kx * kx - ky * ky);
				}
				riderRot = level * r;
			}
			const float nowT = rideTime_;
			const bool tiltLog = nowT - crewTiltLogAt_ >= 0.25f && (skimAirborne_ || skimOnRamp_);
			if (tiltLog) crewTiltLogAt_ = nowT;
			for (auto& handle : passengers_) {
				auto actor = handle.get();
				if (!actor) continue;
				auto* root = actor->Get3D();
				if (!root) continue;
				if (!nodeOverride_) continue;

				const bool dog = IsDogActor(actor.get());
				const RE::NiMatrix3 dogRot = dog ? yawMatrix(DogYaw(tiltDogIndex++)) * boatRot : boatRot;

				// Faster HDT-SMP Fix: skip overriding root->local transforms for human crew.
				// UpdatePassengers() already placed their body and oriented their heading smoothly!
				if (!dog && !BoatIni().crewTilt) {
					continue;
				}

				root->local.rotate = dog ? dogRot : (yawMatrix(GetHumanSeat(actor.get()).yaw) * riderRot);
				if (!dog) {
					// 43.6.2: rotate about the hip, not the root.
					const RE::NiPoint3 pv{ 0.0f, 0.0f, BoatIni().riderPivotUp };
					root->local.translate = actor->GetPosition() + (pv - root->local.rotate * pv);
				}
				RE::NiUpdateData data{};
				root->Update(data);
				// 36.2.3: does the root follow the boat's pitch in a fall?
				if (!dog && tiltLog && (skimAirborne_ || skimOnRamp_)) {
					const auto& m = root->local.rotate;
					const auto it = crewBoatPitch_.find(actor->GetFormID());
					const float boatPitch = it != crewBoatPitch_.end() ? it->second : 0.0f;
					logger::info("Tilt: {} (scale {:.2f}/{:.2f} pivot {:.0f}, rider pitch {:.1f}) boat pitch {:.1f} deg, root pitch {:.1f} deg, roll {:.1f} deg", actor->GetName(),
						BoatIni().riderPitchScale, BoatIni().riderRollScale, BoatIni().riderPivotUp, riderPitchDeg,
						boatPitch * 57.2958f, std::asin(std::clamp(m.entry[2][1], -1.0f, 1.0f)) * 57.2958f, std::asin(std::clamp(m.entry[2][0], -1.0f, 1.0f)) * 57.2958f);
				}
			}
		}

		// Watches a passenger's animation events (dogs and, since v1.0.11, humans). The idle cycle fires 'PickNewIdle'
		// every few seconds, which stands them up. While we're cruising flat we answer it at once so they stay seated.
		// After a jump or landing we leave them alone: recovering is realistic.
		struct PassengerAnimSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
			{
				if (a_event && a_event->holder) {
					OarsAndSails::Get().OnPassengerAnimEvent(const_cast<RE::TESObjectREFR*>(a_event->holder), std::string_view(a_event->tag.c_str()));
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		void OnPassengerAnimEvent(RE::TESObjectREFR* a_ref, std::string_view a_tag)
		{
			auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
			if (!actor || !playerSkim_) return;
			if (!IsDogActor(actor)) NoteCrewEvent(actor, a_tag);
			if (a_tag == "PickNewIdle") OnPassengerPickNewIdle(actor);
		}

		// 17.2 (v1.0.19): after a release, every graph event on the released crew (and the player) for 6 s, so we can see
		// what starts the row loop again. Until the next boarding, the row loop seen on anyone not aboard is stopped:
		// OarsRowStart itself, or our clip's own annotation (SoundPlay.NPCWritingMain, left in it by the Blender export).
		struct StrayWatchSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
			{
				if (a_event && a_event->holder) OarsAndSails::Get().OnStrayWatchEvent(a_event->holder->GetFormID(), std::string_view(a_event->tag.c_str()), std::string_view(a_event->payload.c_str()));
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		struct StrayWatch { RE::FormID id; RE::ActorHandle actor; std::string name; float logLeft; };
		struct RowKick { RE::ActorHandle actor; std::string name; float t; int next; };
		StrayWatchSink strayWatchSink_;
		std::mutex strayMx_;                   // guards strayWatch_ and strayPending_ (the sink may run off the main thread)
		std::vector<StrayWatch> strayWatch_;
		std::vector<RE::FormID> strayPending_;
		std::vector<RE::FormID> rowedThisRide_;  // everyone who played the row loop this ride (the player too)
		std::vector<RowKick> rowKicks_;

		void OnStrayWatchEvent(RE::FormID a_id, std::string_view a_tag, std::string_view a_payload)
		{
			std::lock_guard lock(strayMx_);
			auto it = std::find_if(strayWatch_.begin(), strayWatch_.end(), [&](const StrayWatch& w) { return w.id == a_id; });
			if (it == strayWatch_.end()) return;
			if (it->logLeft > 0.0f) logger::info("ReleaseEv: {} {:08X} {}{}{} ({:.2f} s left)", it->name, a_id, a_tag, a_payload.empty() ? "" : " payload=", a_payload, it->logLeft);
			if (EqualsNoCase(a_tag, kRowStartEvent) || ContainsNoCase(a_tag, "NPCWritingMain")) {
				if (std::find(strayPending_.begin(), strayPending_.end(), a_id) == strayPending_.end()) strayPending_.push_back(a_id);
			}
		}

		void WatchAfterRelease(RE::Actor* a_actor)
		{
			if (!a_actor) return;
			std::lock_guard lock(strayMx_);
			const RE::FormID id = a_actor->GetFormID();
			auto it = std::find_if(strayWatch_.begin(), strayWatch_.end(), [&](const StrayWatch& w) { return w.id == id; });
			if (it != strayWatch_.end()) { it->logLeft = 6.0f; return; }
			strayWatch_.push_back({ id, a_actor->GetHandle(), a_actor->GetName(), 6.0f });
			a_actor->AddAnimationGraphEventSink(&strayWatchSink_);
		}

		// At boarding and on load: the watch ends (the passenger sink takes over aboard).
		void ClearStrayWatch()
		{
			std::vector<StrayWatch> old;
			{
				std::lock_guard lock(strayMx_);
				old.swap(strayWatch_);
				strayPending_.clear();
			}
			for (auto& w : old) {
				if (auto a = w.actor.get()) a->RemoveAnimationGraphEventSink(&strayWatchSink_);
			}
			rowKicks_.clear();
		}

		void TendStrays(float a_dt)
		{
			std::vector<RE::FormID> pending;
			{
				std::lock_guard lock(strayMx_);
				for (auto& w : strayWatch_) {
					if (w.logLeft > 0.0f && (w.logLeft -= a_dt) <= 0.0f) logger::info("ReleaseEv: {} {:08X} end of the 6 s event log", w.name, w.id);
				}
				pending.swap(strayPending_);
			}
			for (const RE::FormID id : pending) {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
				if (!actor || !actor->Is3DLoaded()) continue;
				if (playerSkim_) {
					// Aboard (the player, or a passenger) the loop may be ours: leave it.
					if (actor->IsPlayerRef()) continue;
					const bool aboard = std::any_of(passengers_.begin(), passengers_.end(), [&](const RE::ActorHandle& h) { auto p = h.get(); return p && p.get() == actor; });
					if (aboard) continue;
				}
				const bool ok = actor->NotifyAnimationGraph(kRowStopEvent);
				logger::info("Row anim: stray on {} while not boarded, stopped ({} {})", actor->GetName(), kRowStopEvent, ok ? "accepted" : "rejected");
			}
			std::erase_if(rowKicks_, [&](RowKick& k) {
				static constexpr float kMarks[] = { 0.3f, 1.0f, 3.0f };
				k.t += a_dt;
				auto a = k.actor.get();
				if (!a) return true;
				while (k.next < 3 && k.t >= kMarks[k.next]) {
					const bool ok = a->Is3DLoaded() && a->NotifyAnimationGraph(kRowStopEvent);
					logger::info("Row anim: release stop {:.1f} s on {} ({} {})", kMarks[k.next], k.name, kRowStopEvent, ok ? "accepted" : "rejected");
					++k.next;
				}
				return k.next >= 3;
			});
		}

		// Seated crew (v1.0.11): one entry per human aboard. The animation sink may run off the main thread, so the
		// fields it touches (lastEvents, resitFrames, sitEvent) and the vector itself are guarded by crewIdleMx_.
		// Never call NotifyAnimationGraph while holding it: the graph can call the sink back.
		struct CrewIdleState
		{
			RE::FormID id{ 0 };
			std::string sitEvent;                 // the sit event the game last accepted for them (resent on PickNewIdle)
			std::vector<std::string> lastEvents;  // the last 5 animation events, oldest first
			float resitFrames{ 0.0f };            // > 0: IdleChairEnterInstant every frame for this long
			float boardedAt{ 0.0f };
			// v1.0.13: the stand-up watcher is fed by the sink (seated true -> false), so a stand-up 1 ms after a sit is
			// seen too (the 0.25 s poll missed the three boarding stand-ups in the v1.0.12 log). Guarded by crewIdleMx_.
			float seatedSince{ 0.0f };
			bool stoodPending{ false };
			float stoodSitFor{ 0.0f };
			bool rejectLogged{ false };
			float idleTimer{ 0.0f };              // seconds until the next seated idle
			std::string lastIdle;
			float eatStop{ 0.0f };                // > 0: ChairEatingStop is sent when this runs out
			float flinchDelay{ -1.0f };           // >= 0: FlinchStart is sent when this runs out
			float flinchResit{ 0.0f };            // after a flinch: wait this long, then re-sit quickly even if the boat is not calm yet
			bool postFlinch{ false };
			// v1.0.12: bIsSitting never goes true for a crew member posed by our sit events, so "seated" is tracked from
			// the graph's own events instead (set by the sink, guarded by crewIdleMx_). Last event wins.
			bool seated{ false };
			float idleSentAt{ -100.0f };          // ride time of the last seated idle (a stand-up right after one is noted)
			bool flagsLogged{ false };            // the one-time flag check 3 s after boarding
			// v1.0.13 re-sit ladder (13.4), run on the 0.25 s check while not seated: 0 = try the sit event; if refused,
			// 1 = next frame send IdleStop (the graph wants a standing pose), 2 = next frame try the sit again.
			int resitStep{ 0 };
			float resitWait{ 0.0f };              // > 0: a ladder just failed, wait this long before the next one
			int ladderLogged{ 0 };                // bit 1: a refused ladder was logged, bit 2: an accepted one
			int quickStands{ 0 };                 // sat, then stood again within 2 s (the game is fighting the sit)
			float quickStandWindow{ 0.0f };
			// v1.0.14 fair idle rotation (14.9)
			float lastIdleTime{ -1.0e9f };        // ride time of their last seated idle (never: first in line)
			float idleCooldown{ 0.0f };           // > 0: the graph refused every idle for them, skip them this long
			// v1.0.31 re-seat (Part 33): armed by a landing check or a STOOD UP; starts once the seat flag has been false
			// for kReseatDownFor, then IdleStop + sit every kReseatGap, up to kReseatTries.
			float unseatedAt{ 0.0f };             // ride time the seat flag went false (guarded by crewIdleMx_)
			const char* reseatWhy{ nullptr };     // armed: why ("after landing", "up (STOOD UP)")
			bool reseatActive{ false };
			int reseatTries{ 0 };
			float reseatNext{ 0.0f };             // ride time of the next try
			float reseatStart{ 0.0f };
			bool reseatSitNext{ false };          // IdleStop was sent this try: the sit goes out next frame
			// 56.8.A3: a combat-idle event (tailCombatIdle, combatStance*, IdleCombatStart, IdleWebAlert) and the stand-up
			// right after it are not a STOOD UP: one sit, no ladder (guarded by crewIdleMx_).
			float combatIdleAt{ -100.0f };
			bool combatResit{ false };
		};

		// The rower and the helmsman (14.9, replaces v1.0.13's kNoIdleSlots): no eat / drink / fidget idles and no bump
		// reactions. Decided by the seat they hold right now, which 14.6 changes every boarding: slot 0 is the rower's seat,
		// and the helm is every seat at the stern, forward kSternForwardMax or less (the NPC helm seat, slot kHelmSlot at
		// forward 8, and the stern bench, slots 2 and 3 at forward 150 and 147). A seat retuned to the stern counts as well.
		static constexpr float kSternForwardMax = 160.0f;
		static constexpr float kRefreshHoldSpeed = 300.0f;  // the ref refresh waits while faster than this (13.2)

		bool RowerOrHelmSeat(const HumanSeat& a_seat) const
		{
			return a_seat.slot == 0 || a_seat.pos.y <= kSternForwardMax;
		}

		CrewIdleState* CrewIdleFor(RE::FormID a_id)
		{
			for (auto& s : crewIdle_) if (s.id == a_id) return &s;
			return nullptr;
		}

		void ClearCrewIdle()
		{
			std::lock_guard lock(crewIdleMx_);
			crewIdle_.clear();
			crumbRepeat_.clear();
			dogPickAt_.clear();
		}

		float CrewRand(float a_min, float a_max)
		{
			return std::uniform_real_distribution<float>(a_min, a_max)(crewRng_);
		}

		static bool ContainsNoCase(std::string_view a_text, std::string_view a_word)
		{
			if (a_word.size() > a_text.size()) return false;
			for (std::size_t i = 0; i + a_word.size() <= a_text.size(); ++i) {
				std::size_t j = 0;
				while (j < a_word.size() && std::tolower(static_cast<unsigned char>(a_text[i + j])) == std::tolower(static_cast<unsigned char>(a_word[j]))) ++j;
				if (j == a_word.size()) return true;
			}
			return false;
		}

		// Diagnostics: keeps the last 5 events per human and logs the sit/stand related ones (400 lines per ride).
		void NoteCrewEvent(RE::Actor* a_actor, std::string_view a_tag)
		{
			float boardedAt = 0.0f;
			{
				std::lock_guard lock(crewIdleMx_);
				auto* st = CrewIdleFor(a_actor->GetFormID());
				if (!st) return;
				st->lastEvents.emplace_back(a_tag);
				if (st->lastEvents.size() > 5) st->lastEvents.erase(st->lastEvents.begin());
				boardedAt = st->boardedAt;
				const bool before = st->seated;
				// Boarding shows IdleStop, tailMTIdle, IdleOffsetStop, idleChairSitting, tailChairIdle: the last one wins.
				for (std::string_view on : { "tailChairIdle", "idleChairSitting" }) {
					if (EqualsNoCase(a_tag, on)) st->seated = true;
				}
				for (std::string_view off : { "tailMTIdle", "IdleStop", "IdleChairExitStart", "IdleChairExitToStand", "IdleBedExitToStand" }) {
					if (EqualsNoCase(a_tag, off)) st->seated = false;
				}
				if (IsCombatIdleEvent(a_tag)) st->combatIdleAt = rideTime_;  // 56.8.A3
				if (!before && st->seated) st->seatedSince = rideTime_;
				if (before && !st->seated) {
					st->unseatedAt = rideTime_;
					if (rideTime_ - st->combatIdleAt < 2.0f) st->combatResit = true;  // combat idle: one sit, not a STOOD UP
					else { st->stoodPending = true; st->stoodSitFor = rideTime_ - st->seatedSince; }
				}
			}
			bool match = false;
			for (std::string_view w : { "Stand", "Exit", "GetUp", "Pick", "Sit", "Chair", "Idle", "Flinch", "Recoil", "Stagger" }) {
				if (ContainsNoCase(a_tag, w)) { match = true; break; }
			}
			if (!match || animEvLogs_ >= 400) return;
			++animEvLogs_;
			logger::info("AnimEv: {} {:08X} {} seated={} t={:.2f}", a_actor->GetName(), a_actor->GetFormID(), a_tag, CrewSeated(a_actor->GetFormID()), rideTime_ - boardedAt);
		}

		static bool EqualsNoCase(std::string_view a_a, std::string_view a_b)
		{
			return a_a.size() == a_b.size() && ContainsNoCase(a_a, a_b);
		}

		// Our event-driven seated state for a human aboard (false when unknown).
		bool CrewSeated(RE::FormID a_id)
		{
			std::lock_guard lock(crewIdleMx_);
			auto* st = CrewIdleFor(a_id);
			return st && st->seated;
		}

		// v1.0.13 crash breadcrumb: every animation event sent to a boat passenger goes through here and is logged BEFORE it
		// is sent, with the reason and the source line, so after a crash the last line names it. Each actor + event + reason
		// is logged the 1st time and every 20th time after that (counts reset at boarding), except in the first 2 s after
		// boarding (all of them, to find what stands people up). a_gate: skip the event when the actor is dead, has no 3D, or the boat refs
		// are being refreshed (13.2 suspect 3). An IdleStop to a seated human is refused unless a_gate is false (13.4).
		// Safe from the animation sink thread: the shared fields are guarded by crewIdleMx_, released before the send.
		bool CrewNotify(RE::Actor* a_actor, std::string_view a_event, const char* a_why, bool a_gate = true, std::source_location a_loc = std::source_location::current())
		{
			if (!a_actor) return false;
			if (a_gate && (a_actor->IsDead() || !a_actor->Is3DLoaded() || BoatBow::InRefreshWindow())) return false;
			const RE::FormID id = a_actor->GetFormID();
			bool early = false, blocked = false;
			int repeat = 1;
			{
				std::lock_guard lock(crewIdleMx_);
				if (auto* st = CrewIdleFor(id)) {
					early = rideTime_ - st->boardedAt < 2.0f;
					blocked = a_gate && st->seated && EqualsNoCase(a_event, "IdleStop");
				}
				repeat = ++crumbRepeat_[id][std::format("{}|{}", a_event, a_why)];
			}
			if (blocked) {
				logger::info("Notify: {} {:08X} IdleStop BLOCKED ({}; line {}): seated, not released", a_actor->GetName(), id, a_why, a_loc.line());
				return false;
			}
			if (early || repeat == 1 || repeat % 20 == 0) {
				logger::info("Notify: {} {:08X} {} ({}; line {}{})", a_actor->GetName(), id, a_event, a_why, a_loc.line(), repeat > 1 ? std::format(", repeat {}", repeat) : std::string{});
			}
			const std::string ev(a_event);
			return a_actor->NotifyAnimationGraph(ev);
		}

		// True while a crew idle, flinch or re-sit is in flight: the boat ref refresh waits (13.2 suspect 1).
		bool CrewBusy()
		{
			std::lock_guard lock(crewIdleMx_);
			for (const auto& s : crewIdle_) {
				if (s.eatStop > 0.0f || s.flinchDelay >= 0.0f || s.postFlinch || s.resitStep != 0 || s.resitFrames > 0.0f || s.reseatActive || rideTime_ - s.idleSentAt < 3.0f) return true;
			}
			return false;
		}

		// A human's PickNewIdle while calm and NOT seated (by our event-driven flag): resend the sit event that worked for
		// them, and if the game refuses it, IdleChairEnterInstant every frame for 0.5 s (TendCrewHuman). v1.0.13: when they
		// are seated nothing is sent; the retry against a sitting actor was always refused and only flooded the graph.
		void OnHumanPickNewIdle(RE::Actor* a_actor)
		{
			if (CrewSeated(a_actor->GetFormID())) return;
			if (rowAnimOn_ && GetHumanSeat(a_actor).slot == 0) return;  // the rower is in the row animation: leave it alone
			std::string ev;
			{
				std::lock_guard lock(crewIdleMx_);
				auto* st = CrewIdleFor(a_actor->GetFormID());
				if (!st) return;
				ev = st->sitEvent.empty() ? std::string("IdleChairEnterInstant") : st->sitEvent;
			}
			const bool ok = CrewNotify(a_actor, ev, "PickNewIdle while not seated");
			if (!ok) {
				std::lock_guard lock(crewIdleMx_);
				if (auto* st = CrewIdleFor(a_actor->GetFormID())) st->resitFrames = 0.5f;
			}
			logger::info("Passengers: human {:08X} '{}' PickNewIdle while not seated and calm -> {} {}", a_actor->GetFormID(), a_actor->GetName(), ev, ok ? "accepted" : "rejected (retrying every frame for 0.5 s)");
		}

		void OnPassengerPickNewIdle(RE::Actor* actor)
		{
			bool ours = false;
			for (auto& handle : passengers_) {
				if (auto p = handle.get(); p && p.get() == actor) { ours = true; break; }
			}
			if (!ours || calmTime_ < 3.0f) return;  // recently airborne or landed: let him react naturally
			if (!IsDogActor(actor)) { OnHumanPickNewIdle(actor); return; }
			bool sitting = false;
			actor->GetGraphVariableBool("isIdleSitting", sitting);
			if (!sitting) return;
			// v1.0.15: the graph sends PickNewIdle several times within 50 ms; one answer a second is enough.
			{
				std::lock_guard lock(crewIdleMx_);
				auto& last = dogPickAt_[actor->GetFormID()];
				if (rideTime_ - last < 1.0f && last > 0.0f) return;
				last = rideTime_ > 0.0f ? rideTime_ : 0.001f;
			}
			const bool ok = CrewNotify(actor, "idleSitStart", "dog PickNewIdle");
			if (pickLogs_ < 60) {
				++pickLogs_;
				logger::info("Passengers: {:08X} PickNewIdle while sitting and calm -> idleSitStart {}", actor->GetFormID(), ok ? "accepted" : "rejected");
			}
		}

		// Sits a human down: the event that worked for them first, then the usual three. "Rejected" is logged once per boarding.
		bool TrySitHuman(RE::Actor* a_actor, CrewIdleState& a_st, const char* a_why = "sit")
		{
			std::string first;
			{
				std::lock_guard lock(crewIdleMx_);
				first = a_st.sitEvent;
			}
			std::vector<std::string> order;
			if (!first.empty()) order.push_back(first);
			for (const char* ev : { "IdleChairEnterInstant", "IdleChairSitting", "IdleChairEnterStart" }) {
				if (first != ev) order.emplace_back(ev);
			}
			for (const auto& ev : order) {
				if (CrewNotify(a_actor, ev, a_why)) {
					{
						std::lock_guard lock(crewIdleMx_);
						a_st.sitEvent = ev;
					}
					logger::info("Passengers: human {:08X} '{}' sit event '{}' accepted", a_actor->GetFormID(), a_actor->GetName(), ev);
					return true;
				}
			}
			if (!a_st.rejectLogged) {
				a_st.rejectLogged = true;
				logger::info("Passengers: human {:08X} '{}' sit event rejected (logged once per boarding)", a_actor->GetFormID(), a_actor->GetName());
			}
			return false;
		}

		// True while the numpad is being used on the crew seats (a crew key in the last 10 s): no idles then.
		bool CrewTuningNow() const
		{
			return kNumpadTargets == NumpadTargets::kCrew && std::chrono::steady_clock::now() - lastCrewTune_ < std::chrono::seconds(10);
		}

		// One weighted seated idle (eat 3, drink 3, each fidget 1), never the same one twice in a row for one actor.
		bool PlaySeatedIdle(RE::Actor* a_actor, CrewIdleState& a_st)
		{
			struct Choice { const char* ev; int weight; };
			static constexpr Choice kChoices[] = {
				{ "ChairEatingStart", 3 }, { "ChairDrinkingStart", 3 },
				{ "idleChairArmsCrossedVar1", 1 }, { "idleChairArmsCrossedVar2", 1 }, { "idleChairLookDown", 1 },
				{ "idleChairShoulderFlex", 1 }, { "idleChairVar2", 1 }, { "IdleChairVar1", 1 } };
			int total = 0;
			for (const auto& c : kChoices) if (a_st.lastIdle != c.ev) total += c.weight;
			int roll = std::uniform_int_distribution<int>(0, total - 1)(crewRng_);
			const char* ev = kChoices[0].ev;
			for (const auto& c : kChoices) {
				if (a_st.lastIdle == c.ev) continue;
				if (roll < c.weight) { ev = c.ev; break; }
				roll -= c.weight;
			}
			a_st.lastIdle = ev;
			a_st.idleSentAt = rideTime_;
			auto sendEvent = [&](const char* a_ev) {
				const bool ok = CrewNotify(a_actor, a_ev, "seated idle");
				if (ok) {
					if (std::string_view(a_ev) == "ChairEatingStart") a_st.eatStop = CrewRand(6.0f, 10.0f);
					logger::info("Idle: {} {:08X} {} accepted", a_actor->GetName(), a_actor->GetFormID(), a_ev);
				} else if (std::find(rejectedIdles_.begin(), rejectedIdles_.end(), a_ev) == rejectedIdles_.end()) {
					rejectedIdles_.emplace_back(a_ev);
					logger::info("Idle: {} {:08X} {} rejected (this graph refuses it; logged once per session)", a_actor->GetName(), a_actor->GetFormID(), a_ev);
				}
				return ok;
			};
			// Route (12.3), first that works is kept: (a) the picked graph event, then the others in the list;
			// (b) the game's chair idle forms through PlayIdle; (c) nothing worked: say so once.
			if (idleRoute_ != IdleRoute::kIdleForms) {
				if (sendEvent(ev)) { NoteIdleRoute(IdleRoute::kEvents); return true; }
				for (const auto& c : kChoices) {
					if (std::string_view(c.ev) == ev) continue;
					if (sendEvent(c.ev)) { a_st.lastIdle = c.ev; NoteIdleRoute(IdleRoute::kEvents); return true; }
				}
			}
			if (PlayChairIdleForm(a_actor, a_st)) { NoteIdleRoute(IdleRoute::kIdleForms); return true; }
			if (idleRoute_ != IdleRoute::kNone) {
				idleRoute_ = IdleRoute::kNone;
				logger::warn("Idle: the graph refused every seated idle event and no chair idle form played; refused: [{}]", [&] {
					std::string s;
					for (const auto& r : rejectedIdles_) { if (!s.empty()) s += ", "; s += r; }
					return s;
				}());
			}
			return false;
		}

		enum class IdleRoute { kUnknown, kEvents, kIdleForms, kNone };
		void NoteIdleRoute(IdleRoute a_route)
		{
			if (idleRoute_ == a_route) return;
			idleRoute_ = a_route;
			logger::info("Idle: seated idles play through {}", a_route == IdleRoute::kEvents ? "animation graph events" : "the game's chair idle forms (PlayIdle)");
		}

		// Route (b): idle forms whose editor ID names a chair eat / drink / fidget idle, found once in the data handler.
		bool PlayChairIdleForm(RE::Actor* a_actor, CrewIdleState& a_st)
		{
			if (!chairIdlesScanned_) {
				chairIdlesScanned_ = true;
				if (auto* dh = RE::TESDataHandler::GetSingleton()) {
					for (auto* idle : dh->GetFormArray<RE::TESIdleForm>()) {
						const char* edid = idle ? idle->GetFormEditorID() : nullptr;
						if (!edid || !*edid) continue;
						for (std::string_view w : { "ChairEat", "ChairDrink", "ChairFidget", "ChairLook", "ChairArms" }) {
							if (ContainsNoCase(edid, w)) { chairIdles_.push_back(idle); break; }
						}
					}
				}
				std::string names;
				for (auto* i : chairIdles_) { if (!names.empty()) names += ", "; names += std::format("{} {:08X}", i->GetFormEditorID(), i->GetFormID()); }
				logger::info("Idle: {} chair idle form(s) found: [{}]", chairIdles_.size(), names);
			}
			auto* proc = a_actor->GetActorRuntimeData().currentProcess;
			if (chairIdles_.empty() || !proc) return false;
			auto* idle = chairIdles_[std::uniform_int_distribution<std::size_t>(0, chairIdles_.size() - 1)(crewRng_)];
			if (!a_actor->Is3DLoaded() || BoatBow::InRefreshWindow()) return false;
			logger::info("Notify: {} {:08X} PlayIdle {} (seated idle form)", a_actor->GetName(), a_actor->GetFormID(), idle->GetFormEditorID());  // crash breadcrumb (v1.0.13)
			const bool ok = proc->PlayIdle(a_actor, idle, nullptr);
			const std::string name = idle->GetFormEditorID();
			if (ok) {
				logger::info("Idle: {} {:08X} idle form {} {:08X} accepted", a_actor->GetName(), a_actor->GetFormID(), name, idle->GetFormID());
			} else if (std::find(rejectedIdles_.begin(), rejectedIdles_.end(), name) == rejectedIdles_.end()) {
				rejectedIdles_.push_back(name);
				logger::info("Idle: {} {:08X} idle form {} {:08X} rejected (logged once per session)", a_actor->GetName(), a_actor->GetFormID(), name, idle->GetFormID());
			}
			a_st.lastIdle = name;
			return ok;
		}

		// The bump reaction (11.5 / 12.3): FlinchStart, or if the chair state refuses it, a head duck or crossed arms.
		bool SendBumpReaction(RE::Actor* a_actor, bool a_seated)
		{
			if (CrewNotify(a_actor, bumpEvent_, "bump reaction")) {
				logger::info("Flinch: {} {:08X} {} accepted (seated={})", a_actor->GetName(), a_actor->GetFormID(), bumpEvent_, a_seated);
				return true;
			}
			logger::info("Flinch: {} {:08X} {} rejected (seated={})", a_actor->GetName(), a_actor->GetFormID(), bumpEvent_, a_seated);
			if (!a_seated) return false;
			for (const char* alt : { "FlinchStart", "idleChairLookDown", "idleChairArmsCrossedVar2" }) {
				if (bumpEvent_ == alt) continue;
				if (CrewNotify(a_actor, alt, "bump reaction (fallback)")) {
					logger::info("Flinch: {} refused while seated, the bump reaction is now {} (accepted on {:08X})", bumpEvent_, alt, a_actor->GetFormID());
					bumpEvent_ = alt;
					return true;
				}
			}
			flinchRefused_ = true;
			logger::info("Flinch: FlinchStart, idleChairLookDown and idleChairArmsCrossedVar2 are all refused while seated; bump reactions are off this session");
			return false;
		}

		// ---- Re-seat after a landing or a stand-up (Part 33) -------------------------------------------------------------
		// A waterfall or cliff landing stands the crew rower up within 0.2 s and he went on rowing standing until the next
		// row stop. Now: 0.25 s and 1.0 s after every landing (NoteBoatLanding) anyone aboard who is not seated is armed,
		// and so is anyone a STOOD UP line names. Once their seat flag has been false for kReseatDownFor (the idleChairGetUp
		// right after OarsRowStart leaves the flag true, so it never counts), the rower's clip is stopped, then IdleStop and
		// the sit, every kReseatGap, up to kReseatTries. The row clip restarts by itself (TryRowStart waits for seated).
		static constexpr float kReseatDownFor = 0.3f;
		static constexpr float kReseatGap = 0.4f;
		static constexpr int kReseatTries = 5;
		std::array<float, 2> landingChecks_{ -1.0f, -1.0f };  // ride times of the two checks after the last landing
		bool landingCheckNow_{ false };                       // a landing check is due this frame (UpdatePassengers)

		// 40.3: in a fall nothing may bend the crew's torso: the graph pitch variables are held at 0 (logged once per flight).
		void ZeroGraphPitch(RE::Actor* a_actor)
		{
			for (const char* name : { "Pitch", "PitchOverride", "fPitch" }) {
				float v = 0.0f;
				if (!a_actor->GetGraphVariableFloat(name, v)) continue;
				const RE::FormID id = a_actor->GetFormID();
				if (std::find(pitchZeroLogged_.begin(), pitchZeroLogged_.end(), id) == pitchZeroLogged_.end() && std::string_view(name) == "Pitch") {
					pitchZeroLogged_.push_back(id);
					logger::info("Tilt: {} graph Pitch was {:.2f}, set to 0", a_actor->GetName(), v);
				}
				if (v != 0.0f) a_actor->SetGraphVariableFloat(name, 0.0f);
			}
		}

		void NoteBoatLanding()
		{
			lastLandingAt_ = rideTime_;  // 47.7.2
			hullCheckAfterLanding_ = true;  // 45.1.3
			glueZeroTimer_ = (std::max)(glueZeroTimer_, 1.0f);  // 40.2
			landingChecks_ = { rideTime_ + 0.25f, rideTime_ + 1.0f };
		}

		// True while the re-seat owns this actor's sit (the usual re-sit stays out of the way).
		bool TendReseat(RE::Actor* a_actor, CrewIdleState& a_st, bool a_isRower, bool a_sitting, bool a_graphSafe)
		{
			const bool flinch = a_st.postFlinch || a_st.flinchDelay >= 0.0f;
			if (landingCheckNow_ && !a_sitting && !flinch && !a_st.reseatActive && !a_st.reseatWhy && rideTime_ - a_st.boardedAt >= 2.0f) a_st.reseatWhy = "after landing";
			if (!a_st.reseatActive) {
				if (!a_st.reseatWhy) return false;
				if (a_sitting) { a_st.reseatWhy = nullptr; return false; }
				float downFor = 0.0f;
				{
					std::lock_guard lock(crewIdleMx_);
					downFor = rideTime_ - a_st.unseatedAt;
				}
				if (downFor < kReseatDownFor || !a_graphSafe) return false;
				a_st.reseatActive = true;
				a_st.reseatTries = 0;
				a_st.reseatStart = rideTime_;
				a_st.reseatNext = rideTime_;
				a_st.reseatSitNext = false;
			}
			if (a_sitting) {
				logger::info("Reseat: {} {:08X} seated again after {:.0f} ms", a_actor->GetName(), a_actor->GetFormID(), (rideTime_ - a_st.reseatStart) * 1000.0f);
				a_st.reseatActive = false;
				a_st.reseatWhy = nullptr;
				a_st.reseatSitNext = false;
				return false;
			}
			if (!a_graphSafe) return true;
			if (a_st.reseatSitNext) {
				a_st.reseatSitNext = false;
				TrySitHuman(a_actor, a_st, "re-seat: sit");
				return true;
			}
			if (rideTime_ < a_st.reseatNext) return true;
			if (a_st.reseatTries >= kReseatTries) {
				logger::info("Reseat: {} {:08X} failed, will retry at the next row stop", a_actor->GetName(), a_actor->GetFormID());
				a_st.reseatActive = false;
				a_st.reseatWhy = nullptr;
				return false;
			}
			++a_st.reseatTries;
			a_st.reseatNext = rideTime_ + kReseatGap;
			logger::info("Reseat: {} {:08X} stood {}, sitting again (try {})", a_actor->GetName(), a_actor->GetFormID(), a_st.reseatWhy ? a_st.reseatWhy : "up", a_st.reseatTries);
			if (a_isRower && rowAnimOn_) {
				// Never let the row clip play on a standing rower: stop it, sit him, and the clip starts again once seated.
				StopRowAnim(RE::PlayerCharacter::GetSingleton());
				std::lock_guard lock(crewIdleMx_);
				a_st.resitFrames = 0.0f;  // the re-seat sits him, not the PickNewIdle retry
			} else {
				CrewNotify(a_actor, "IdleStop", "re-seat: stand first");
			}
			a_st.reseatSitNext = true;  // the sit goes out next frame
			return true;
		}

		// Per frame, per human aboard: the stand-up watcher (every 0.25 s), the re-sit, the seated idles and the flinch.
		void TendCrewHuman(RE::Actor* a_actor, bool a_isRower, bool a_noIdle, float a_dt, bool a_tick, bool a_slowTick)
		{
			auto* st = CrewIdleFor(a_actor->GetFormID());  // main thread is the only one that adds or removes entries
			if (!st) return;
			if (rowMenuHold_ > 0.0f) return;  // 18.1: 0.5 s after a menu closes, nothing is sent (timers wait too)
			const bool sitting = CrewSeated(st->id);  // event-driven (v1.0.12); bIsSitting never goes true for the crew
			const bool rowing = a_isRower && (rowAnimOn_ || rowWantAnim_);
			if (!st->flagsLogged && rideTime_ - st->boardedAt >= 3.0f) {
				// One-time check: which game flag, if any, follows our event-driven seated state.
				st->flagsLogged = true;
				bool bIsSitting = false, isIdleSitting = false;
				a_actor->GetGraphVariableBool("bIsSitting", bIsSitting);
				a_actor->GetGraphVariableBool("isIdleSitting", isIdleSitting);
				const auto* as = a_actor->AsActorState();
				logger::info("SeatFlags: {} {:08X} 3 s after boarding: bIsSitting={} isIdleSitting={} sitSleepState={} seated(events)={}", a_actor->GetName(), a_actor->GetFormID(),
					bIsSitting, isIdleSitting, as ? static_cast<int>(as->actorState1.sitSleepState) : -1, sitting);
			}
			// No events at all while the actor has no 3D or the boat refs are being refreshed (13.2): timers wait.
			const bool graphSafe = a_actor->Is3DLoaded() && !a_actor->IsDead() && !BoatBow::InRefreshWindow();
			if (a_tick) {
				// Only a stand-up we did not cause: not a flinch, not a launch / landing (calm). Boarding stand-ups are logged.
				bool stood = false;
				float sitFor = 0.0f;
				std::string evs;
				{
					std::lock_guard lock(crewIdleMx_);
					if (st->stoodPending) {
						stood = true;
						sitFor = st->stoodSitFor;
						st->stoodPending = false;
						for (const auto& e : st->lastEvents) { if (!evs.empty()) evs += ", "; evs += e; }
					}
				}
				const bool expected = st->postFlinch || st->flinchDelay >= 0.0f || (calmTime_ < 3.0f && rideTime_ - st->boardedAt > 2.0f);
				if (stood && !expected) {
					const float sinceStop = rideTime_ - lastRowStopAt_;
					std::string note = a_isRower && sinceStop < 2.0f ? std::format(" ({:.2f} s after the row stop)", sinceStop) : std::string{};
					if (rideTime_ - st->idleSentAt < 15.0f) note += std::format(" ({:.1f} s after seated idle {})", rideTime_ - st->idleSentAt, st->lastIdle);
					if (rideTime_ - st->boardedAt < 2.0f) note += " (boarding)";
					logger::info("STOOD UP: {} {:08X} after {:.2f} s sitting, last 5 events: [{}]{} calm={:.1f}", a_actor->GetName(), a_actor->GetFormID(), sitFor, evs, note, calmTime_);
				}
				// Part 33: a stand-up we did not cause is acted on, not only logged (not at boarding, not from a flinch).
				const bool flinchStand = st->postFlinch || st->flinchDelay >= 0.0f;
				if (stood && !flinchStand && rideTime_ - st->boardedAt >= 2.0f && !st->reseatActive && !st->reseatWhy) st->reseatWhy = "up (STOOD UP)";
				if (stood && sitFor < 2.0f) {
					// Sat, then stood again at once: something keeps standing them up. Three in 30 s: re-sit only every 5 s.
					if (rideTime_ - st->quickStandWindow > 30.0f) { st->quickStandWindow = rideTime_; st->quickStands = 0; }
					if (++st->quickStands == 3) {
						st->resitWait = 5.0f;
						logger::info("Passengers: {} {:08X} stood up again right after sitting 3 times in 30 s; re-sitting every 5 s", a_actor->GetName(), a_actor->GetFormID());
					}
				}
			}
			// Flinch (11.5): sent after its small random delay; then a quick re-sit once it has played. Never on a no-idle seat.
			if (st->flinchDelay >= 0.0f && a_noIdle) st->flinchDelay = -1.0f;
			if (st->flinchDelay >= 0.0f && graphSafe && (st->flinchDelay -= a_dt) < 0.0f) {
				st->flinchDelay = -1.0f;
				if (!flinchRefused_ && flinchOn_ && SendBumpReaction(a_actor, sitting)) { st->postFlinch = true; st->flinchResit = 1.2f; }
			}
			if (st->flinchResit > 0.0f) st->flinchResit -= a_dt;
			if (st->postFlinch && sitting && st->flinchResit <= 0.0f) st->postFlinch = false;
			// Re-sit (11.3 / 13.4): every 0.25 s when calm (or once a flinch is over), once a second after a launch or landing
			// so they can react first. A refused sit climbs the ladder: IdleStop next frame, the sit again the frame after.
			// A refused PickNewIdle answer retries every frame for 0.5 s.
			if (st->resitWait > 0.0f) st->resitWait -= a_dt;
			{
				// 56.8.A3: up from a combat idle: stop combat and sit once; the ladder waits 1.5 s.
				bool combat = false;
				{
					std::lock_guard lock(crewIdleMx_);
					combat = st->combatResit;
					st->combatResit = false;
				}
				if (combat && graphSafe && !sitting) {
					if (a_actor->IsInCombat()) a_actor->StopCombat();
					if (!(rowAnimOn_ && a_isRower)) TrySitHuman(a_actor, *st, "combat idle: sit once");
					st->resitWait = (std::max)(st->resitWait, 1.5f);
					st->resitStep = 0;
					logger::info("Passengers: {} {:08X} up from a combat idle / calm hold, sat again at once (no ladder)", a_actor->GetName(), a_actor->GetFormID());
				}
			}
			const bool reseating = TendReseat(a_actor, *st, a_isRower, sitting, graphSafe);  // Part 33 owns the sit while it runs
			if (!reseating && !sitting && graphSafe && !(rowAnimOn_ && a_isRower)) {
				float resit = 0.0f;
				{
					std::lock_guard lock(crewIdleMx_);
					if (st->resitFrames > 0.0f) { resit = st->resitFrames; st->resitFrames -= a_dt; }
				}
				bool seated = false;
				if (resit > 0.0f && CrewNotify(a_actor, "IdleChairEnterInstant", "PickNewIdle retry")) {
					seated = true;
					std::lock_guard lock(crewIdleMx_);
					st->resitFrames = 0.0f;
				}
				const bool flinching = st->postFlinch && st->flinchResit > 0.0f;
				const bool quick = !skimAirborne_ && !skimOnRamp_ && (calmTime_ >= 3.0f || (st->postFlinch && st->flinchResit <= 0.0f));
				if (seated || flinching) {
					st->resitStep = 0;
				} else if (st->resitStep == 1) {
					CrewNotify(a_actor, "IdleStop", "re-sit ladder: stand first");
					st->resitStep = 2;
				} else if (st->resitStep == 2) {
					st->resitStep = 0;
					const bool ok = TrySitHuman(a_actor, *st, "re-sit ladder: sit after IdleStop");
					if (!ok) st->resitWait = (std::max)(st->resitWait, 1.0f);
					const int bit = ok ? 2 : 1;  // the first accepted and the first refused ladder, once each per boarding
					if (!(st->ladderLogged & bit)) {
						st->ladderLogged |= bit;
						logger::info("Passengers: {} {:08X} re-sit after IdleStop {}", a_actor->GetName(), a_actor->GetFormID(), ok ? "accepted" : "rejected (retry in 1 s)");
					}
				} else if ((quick ? a_tick : a_slowTick) && st->resitWait <= 0.0f) {
					if (!TrySitHuman(a_actor, *st, "re-sit")) st->resitStep = 1;
				}
			} else if (sitting) {
				st->resitStep = 0;
			}
			// Seated idles (11.4): started by the shared scheduler (ScheduleSeatedIdles, 14.9); here only the per-actor timers
			// and the end of a meal. Someone who just became the rower or helmsman puts the bowl down at once.
			if (st->idleTimer > 0.0f) st->idleTimer -= a_dt;
			if (st->idleCooldown > 0.0f) st->idleCooldown -= a_dt;
			if (st->eatStop > 0.0f && graphSafe && (a_noIdle || rowing)) st->eatStop = 0.001f;
			if (st->eatStop > 0.0f && graphSafe && (st->eatStop -= a_dt) <= 0.0f) {
				const bool ok = CrewNotify(a_actor, "ChairEatingStop", a_noIdle || rowing ? "role changed: stop eating" : "eating done");
				logger::info("Idle: {} {:08X} ChairEatingStop {}", a_actor->GetName(), a_actor->GetFormID(), ok ? "accepted" : "rejected");
			}
		}

		// ---- Fair idle rotation (14.9) -------------------------------------------------------------------------------
		// One scheduler for the whole crew: roughly every 8 to 20 s, while fewer than kMaxActiveIdles are playing, the
		// eligible seated crew member who has gone longest without an idle gets one (never the same one twice in a row if
		// anyone else can). Each actor still waits 20 to 60 s between their own idles. Refused by the graph: 30 s cooldown
		// for them, and the next in line gets it right away.
		static constexpr int kMaxActiveIdles = 3;
		static constexpr float kIdleActiveSeconds = 8.0f;  // a fidget / drink counts as playing this long (a meal until its stop)
		float idleSchedTimer_{ 10.0f };
		RE::FormID lastIdleActor_{ 0 };

		bool IdleActive(const CrewIdleState& a_st) const
		{
			return a_st.eatStop > 0.0f || rideTime_ - a_st.idleSentAt < kIdleActiveSeconds;
		}

		int ActiveIdles()
		{
			int n = 0;
			for (const auto& s : crewIdle_) if (IdleActive(s)) ++n;
			return n;
		}

		// May this crew member start a seated idle now? a_why says why not. a_force (oas "idles now") skips the
		// per-actor gap and the cooldown, never the role rule.
		bool IdleEligible(RE::Actor* a_actor, CrewIdleState& a_st, std::string& a_why, bool a_force)
		{
			auto no = [&](std::string a_reason) { a_why = std::move(a_reason); return false; };
			if (!a_actor || a_actor->IsDead() || !a_actor->Is3DLoaded()) return no("not loaded");
			if (IsDogActor(a_actor)) return no("dog");
			const auto& seat = GetHumanSeat(a_actor);
			if (seat.slot == 0 || RowActor(RE::PlayerCharacter::GetSingleton()).get() == a_actor) return no("rower");
			if (RowerOrHelmSeat(seat)) return no(std::format("helm (stern seat, forward {:.0f})", seat.pos.y));
			if (!CrewSeated(a_st.id)) return no("not seated");
			if ((hopActive_ && hopActor_.get().get() == a_actor) || rideTime_ - a_st.boardedAt < 3.0f) return no("boarding");
			if (IdleActive(a_st)) return no("playing an idle");
			if (a_st.flinchDelay >= 0.0f || a_st.postFlinch) return no("flinching");
			if (a_actor->IsInCombat()) return no("in combat");
			if (!a_force && a_st.idleCooldown > 0.0f) return no(std::format("refused idles, cooldown {:.0f} s", a_st.idleCooldown));
			if (!a_force && a_st.idleTimer > 0.0f) return no(std::format("own gap, {:.0f} s left", a_st.idleTimer));
			a_why = "eligible";
			return true;
		}

		// Starts one idle on the fair pick. Returns the actor's name, or empty when nobody could take one.
		std::string StartFairIdle(bool a_force)
		{
			struct Cand { RE::NiPointer<RE::Actor> actor; CrewIdleState* st; };
			std::vector<Cand> cands;
			for (auto& h : passengers_) {
				auto a = h.get();
				if (!a || IsDogActor(a.get())) continue;
				auto* st = CrewIdleFor(a->GetFormID());
				std::string why;
				if (st && IdleEligible(a.get(), *st, why, a_force)) cands.push_back({ a, st });
			}
			if (cands.empty()) return {};
			std::stable_sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.st->lastIdleTime < y.st->lastIdleTime; });
			if (cands.size() > 1 && cands.front().st->id == lastIdleActor_) std::rotate(cands.begin(), cands.begin() + 1, cands.end());
			for (auto& c : cands) {
				if (PlaySeatedIdle(c.actor.get(), *c.st)) {
					c.st->lastIdleTime = rideTime_;
					c.st->idleTimer = CrewRand(kIdleMinGap, kIdleMaxGap);
					lastIdleActor_ = c.st->id;
					idleSchedTimer_ = CrewRand(8.0f, 20.0f);
					logger::info("Idle: {} {} (active {}/{}, next in {:.0f} s)", c.actor->GetName(), c.st->lastIdle, ActiveIdles(), kMaxActiveIdles, idleSchedTimer_);
					return c.actor->GetName();
				}
				c.st->idleSentAt = -100.0f;  // nothing is playing
				c.st->idleCooldown = 30.0f;  // the graph refused everything for them: the next in line, right away
				logger::info("Idle: {} refused every seated idle, 30 s cooldown; trying the next crew member", c.actor->GetName());
			}
			return {};
		}

		void ScheduleSeatedIdles(float a_dt)
		{
			if (!idlesOn_ || (idleSchedTimer_ -= a_dt) > 0.0f) return;
			const bool calm = calmTime_ >= 3.0f && !skimAirborne_ && !skimOnRamp_ && !CrewTuningNow() && !BoatBow::InRefreshWindow();
			if (!calm || ActiveIdles() >= kMaxActiveIdles) { idleSchedTimer_ = 1.0f; return; }
			if (StartFairIdle(false).empty()) idleSchedTimer_ = 2.0f;  // nobody eligible yet: look again soon
		}

		// The row animation starts: an eating rower puts the bowl down first.
		void CancelSeatedIdle(RE::Actor* a_actor)
		{
			auto* st = a_actor ? CrewIdleFor(a_actor->GetFormID()) : nullptr;
			if (!st || st->eatStop <= 0.0f) return;
			st->eatStop = 0.0f;
			CrewNotify(a_actor, "ChairEatingStop", "row start: put the bowl down");
		}

		// Launch, hard landing or drop: a random half of the human crew flinch, each 0 to 0.4 s late. Not the rower while rowing.
		void CrewBump(const char* a_why)
		{
			if (!playerSkim_ || flinchRefused_) return;
			std::vector<CrewIdleState*> pool;
			for (auto& h : passengers_) {
				auto a = h.get();
				if (!a || a->IsDead() || IsDogActor(a.get())) continue;
				if (RowerOrHelmSeat(GetHumanSeat(a.get()))) continue;  // rower and helm: no bump reactions (13.3, 14.9)
				auto* st = CrewIdleFor(a->GetFormID());
				if (st && st->flinchDelay < 0.0f && !st->postFlinch) pool.push_back(st);
			}
			if (pool.empty()) return;
			std::shuffle(pool.begin(), pool.end(), crewRng_);
			const std::size_t n = (pool.size() + 1) / 2;
			for (std::size_t i = 0; i < n; ++i) pool[i]->flinchDelay = CrewRand(0.0f, 0.4f);
			logger::info("Flinch: {} -> {} of {} crew flinch", a_why, n, pool.size());
		}

		// Actor.SetHeadTracking(bool) through the script engine: false stops the head turning to follow the camera / things around.
		void SetActorHeadTracking(RE::Actor* a_actor, bool a_value)
		{
			if (!a_actor) return;
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!policy) return;
			const auto handle = policy->GetHandleForObject(static_cast<RE::VMTypeID>(a_actor->GetFormType()), a_actor);
			if (!handle) return;
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
			auto args = RE::MakeFunctionArguments(static_cast<bool>(a_value));
			vm->DispatchMethodCall(handle, RE::BSFixedString("Actor"), RE::BSFixedString("SetHeadTracking"), args, callback);
			logger::info("Head tracking {}", a_value ? "restored" : "turned off");
		}

		// v1.0.13 (13.4): SetDontMove runs later on the script engine. Log when it has run, with the actor's seated state, to
		// see whether it lines up with the IdleStop that stood three of the crew up right after their sit at boarding.
		struct DontMoveDone : public RE::BSScript::IStackCallbackFunctor
		{
			DontMoveDone(RE::FormID a_id, bool a_value) : id(a_id), value(a_value) {}
			void operator()(RE::BSScript::Variable) override
			{
				auto& oas = OarsAndSails::Get();
				logger::info("SetDontMove({}) done for {:08X} at ride t={:.3f} seated={}", value, id, oas.rideTime_, oas.CrewSeated(id));
			}
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
			RE::FormID id;
			bool value;
		};

		void SetActorDontMove(RE::Actor* a_actor, bool a_value)
		{
			if (!a_actor) return;
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!policy) return;
			const auto handle = policy->GetHandleForObject(static_cast<RE::VMTypeID>(a_actor->GetFormType()), a_actor);
			if (!handle) return;
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
			if (!a_actor->IsPlayerRef()) callback = RE::make_smart<DontMoveDone>(a_actor->GetFormID(), a_value);
			auto args = RE::MakeFunctionArguments(static_cast<bool>(a_value));
			vm->DispatchMethodCall(handle, RE::BSFixedString("Actor"), RE::BSFixedString("SetDontMove"), args, callback);
		}

		// Numpad tuning of the dog's slot while boarded. 8/2 forward/back, 4/6 left/right,
		// 9/3 up/down, 7/1 turn the dog 5 degrees left/right.
		// Numpad tuning. Default target is the PLAYER (seat position in the boat, boat-local:
		// right/forward/up). Numpad 5 flips between player and dog.
		static constexpr bool kTuneFollowers = false;  // true: Num5 also cycles the dog and followers (off while we place the plank)
		// Top-left text for the oar numpad: the key legend of the current page, and the current values.
		void RippleHud(bool a_legend)
		{
			const auto& rt = BoatBow::Ripples();
			if (a_legend) {
				oarHudLegend_ = std::chrono::steady_clock::now();
				if (ripplePage_ == 0) {
					RE::SendHUDMessage::ShowHUDMessage("RIPPLES 1/2 STERN TRAIL: 8/2 start closer-further, 4/6 spread, 9/3 up-down, 7/1 rotate, 0/. spacing, +/- size. Num Enter = oar rings, Num5 = oars");
				} else {
					RE::SendHUDMessage::ShowHUDMessage("RIPPLES 2/2 OAR RINGS: 8/2 along the shaft, 4/6 sideways, 9/3 up-down, +/- size. Num Enter = stern trail, Num5 = oars");
				}
			}
			if (ripplePage_ == 0) {
				RE::SendHUDMessage::ShowHUDMessage(std::format("Trail: back {:.0f}  spread {:.0f}  up {:.0f}  rotate {:.0f} deg  spacing {:.0f}  size {:.3f}",
					rt.stern, rt.lateral, rt.lift, rt.yaw * 57.2958f, rt.space, rt.wake).c_str());
			} else {
				RE::SendHUDMessage::ShowHUDMessage(std::format("Oar rings: reach {:.0f}  side {:.0f}  up {:.0f}  size {:.3f}", rt.oarReach, rt.oarSide, rt.oarLift, rt.oar).c_str());
			}
		}

		void ChestHud(bool a_legend)
		{
			if (a_legend) {
				oarHudLegend_ = std::chrono::steady_clock::now();
				RE::SendHUDMessage::ShowHUDMessage("CHEST: 8/2 forward-back, 4/6 left-right, 9/3 up-down, 7/1 turn, 0/. forward-back x10, +/- size. Num5 = next (player seat, then everything together)");
			}
			const auto& c = BoatBow::Chest();
			RE::SendHUDMessage::ShowHUDMessage(std::format("Chest: right {:.0f}  forward {:.0f}  up {:.0f}  turn {:.0f} deg  size {:.2f}", c.offset.x, c.offset.y, c.offset.z, c.yaw * 57.2958f, c.scale).c_str());
		}

		// The seat of the follower who rows (seat slot 0), or null when nobody is aboard to row.
		HumanSeat* RowerSeat()
		{
			for (auto& h : passengers_) {
				auto a = h.get();
				if (a && !a->IsDead() && !IsDogActor(a.get())) {
					auto& hs = GetHumanSeat(a.get());
					if (hs.slot == 0) return &hs;
				}
			}
			return nullptr;
		}

		// The whole rowing rig on one key: the rower's seat, both oars and the chest move by the same step.
		void AllHud(bool a_legend)
		{
			if (a_legend) {
				oarHudLegend_ = std::chrono::steady_clock::now();
				RE::SendHUDMessage::ShowHUDMessage("EVERYTHING TOGETHER (rower + oars + chest): 8/2 forward-back, 4/6 left-right, 9/3 up-down, 0/. forward-back x10. Num5 = next (rower alone, oars, chest, player seat)");
			}
			const auto* hs = RowerSeat();
			const auto& c = BoatBow::Chest();
			const auto& o = BoatBow::Oar(0);
			RE::SendHUDMessage::ShowHUDMessage(std::format("Moved so far: right {:.0f}  forward {:.0f}  up {:.0f}", allShift_.x, allShift_.y, allShift_.z).c_str());
			if (hs) RE::SendHUDMessage::ShowHUDMessage(std::format("Rower: right {:.0f}  forward {:.0f}  up {:.0f}", hs->pos.x, hs->pos.y, hs->pos.z).c_str());
			RE::SendHUDMessage::ShowHUDMessage(std::format("Oars: out {:.0f}  forward {:.0f}  up {:.0f}   Chest: right {:.0f}  forward {:.0f}  up {:.0f}", o.offset.x, o.offset.y, o.offset.z, c.offset.x, c.offset.y, c.offset.z).c_str());
		}

		// What the numpad is moving when it is on a seat: the player, the dog, or a human aboard.
		void SeatHud(bool a_legend)
		{
			std::string who = "the player";
			const RE::NiPoint3* pos = &playerSeat_;
			float yaw = playerSeatYaw_;
			if (tuneMode_ == 1) {
				who = "the dog";
				pos = &bowSlot_;
				yaw = dogYaw_;
			} else if (tuneMode_ >= 2) {
				const auto humans = BoardedHumans();
				if (static_cast<size_t>(tuneMode_ - 2) < humans.size()) {
					auto& h = *humans[static_cast<size_t>(tuneMode_ - 2)];
					const auto& hs = GetHumanSeat(&h);
					who = std::format("{} ({:08X})", h.GetName(), h.GetFormID());
					pos = &hs.pos;
					yaw = hs.yaw;
				} else {
					who = "no human in this slot";
				}
			}
			if (tuneMode_ == 0 && !playerTilt_) {
				// Physics-friendly player: no skeleton override, so only the height and the turn can be tuned.
				if (a_legend) {
					oarHudLegend_ = std::chrono::steady_clock::now();
					RE::SendHUDMessage::ShowHUDMessage("PLAYER SEAT: 8/2 forward-back (toward the oars), 4/6 left-right, 9/3 up-down, 7/1 turn. (Num5: next = oars, chest, player, rower)");
				}
				RE::SendHUDMessage::ShowHUDMessage(std::format("Player: fwd {:.0f}  side {:.0f}  up {:.0f}  turn {:.0f} deg", playerBodyFwd_, playerBodySide_, playerBodyLift_, playerSeatYaw_ * 57.2958f).c_str());
				return;
			}
			if (a_legend) {
				oarHudLegend_ = std::chrono::steady_clock::now();
				RE::SendHUDMessage::ShowHUDMessage(std::format("SEAT: {}. 8/2 forward-back, 4/6 left-right, 9/3 up-down, 7/1 turn. Num5 = next (oars, chest, player, rower)", who).c_str());
			}
			RE::SendHUDMessage::ShowHUDMessage(std::format("{}: right {:.0f}  forward {:.0f}  up {:.0f}  turn {:.0f} deg", who, pos->x, pos->y, pos->z, yaw * 57.2958f).c_str());
		}

		// The lamp being tuned: legend and current values, like the chest.
		void LampHud(bool a_legend)
		{
			const int side = tuneMode_ == kLampBowModeId ? 1 : 0;
			if (a_legend) {
				oarHudLegend_ = std::chrono::steady_clock::now();
				RE::SendHUDMessage::ShowHUDMessage(std::format("{} LAMP: 8/2 forward-back, 4/6 left-right, 9/3 up-down, 7/1 turn, +/- size, * and / swing. {}", side == 0 ? "STERN" : "BOW",
					kNumpadLampsOnly ? "Num5 = the other lamp" : "Num Lock = next (stern lamp, bow lamp, oars)").c_str());
			}
			const auto& t = BoatBow::Lamp(side);
			RE::SendHUDMessage::ShowHUDMessage(std::format("{} lamp: right {:.0f}  forward {:.0f}  up {:.0f}  turn {:.0f} deg  size {:.2f}  swing {:.2f}", side == 0 ? "Stern" : "Bow",
				t.offset.x, t.offset.y, t.offset.z, t.yaw * 57.2958f, t.scale, t.swing).c_str());
		}

		void TuneLamp(int a_side, std::uint32_t a_key)
		{
			auto& t = BoatBow::Lamp(a_side);
			bool changed = true;
			switch (a_key) {
			case 0x48: t.offset.y += 1.0f; break;  // Num8
			case 0x50: t.offset.y -= 1.0f; break;  // Num2
			case 0x4B: t.offset.x -= 1.0f; break;  // Num4
			case 0x4D: t.offset.x += 1.0f; break;  // Num6
			case 0x49: t.offset.z += 1.0f; break;  // Num9
			case 0x51: t.offset.z -= 1.0f; break;  // Num3
			case 0x47: t.yaw += 0.0436f; break;    // Num7
			case 0x4F: t.yaw -= 0.0436f; break;    // Num1
			case 0x4E: t.scale = (std::min)(5.0f, t.scale * 1.02f); break;   // Num+
			case 0x4A: t.scale = (std::max)(0.1f, t.scale / 1.02f); break;   // Num-
			case 0x37: t.swing = (std::min)(4.0f, t.swing + 0.05f); break;   // Num*
			case 0xB5: t.swing = (std::max)(0.0f, t.swing - 0.05f); break;   // Num/
			case 0x9C: LampHud(true); changed = false; break;               // Num Enter: show the legend again
			default: changed = false; break;
			}
			if (changed) {
				BoatBow::LogLamp(a_side);
				oarHudDirty_ = true;
			}
		}

		void OarHud(bool a_legend)
		{
			if (tuneMode_ == kLampSternModeId || tuneMode_ == kLampBowModeId) { LampHud(a_legend); return; }
			if (tuneMode_ == kAllModeId) { AllHud(a_legend); return; }
			if (tuneMode_ == kChestModeId) { ChestHud(a_legend); return; }
			if (tuneMode_ == kRippleModeId) { RippleHud(a_legend); return; }
			if (a_legend) {
				oarHudLegend_ = std::chrono::steady_clock::now();
				if (oarPage_ == 0) {
					RE::SendHUDMessage::ShowHUDMessage("OARS 1/2 POSITION + STROKE: 8/2 forward-back, 4/6 in-out, 9/3 up-down, 0/. sweep, 7/1 dip, +/- size, * and / stroke speed. Num Enter = angle page");
				} else {
					RE::SendHUDMessage::ShowHUDMessage("OARS 2/2 ANGLE+STROKE: 8/2 blade up-down, 6/4 turn aft-fwd, 9/3 roll, 7/1 dip, 0/. sweep, +/- size, * and / STROKE SPEED (fine). Num Enter = position page");
				}
			}
			const auto& r = BoatBow::Oar(0);
			const auto& st = BoatBow::Stroke();
			if (oarPage_ == 0) {
				RE::SendHUDMessage::ShowHUDMessage(std::format("Oars: out {:.0f}  forward {:.0f}  up {:.0f}  sweep {:.0f}  dip {:.0f}  size {:.2f}  stroke speed x{:.3f} ({:.2f} s per stroke)", r.offset.x, r.offset.y, r.offset.z, st.swing * 57.2958f, st.dip * 57.2958f, r.scale, st.rate, 1.0f / (kRowStrokesBase * st.rate)).c_str());
			} else {
				RE::SendHUDMessage::ShowHUDMessage(std::format("Oars: blade {:.1f} deg  turn {:.1f} deg  roll {:.1f} deg  dip {:.0f}  sweep {:.0f}  stroke speed x{:.3f} ({:.2f} s)",
					r.pitch * 57.2958f, r.yaw * 57.2958f, r.roll * 57.2958f, st.dip * 57.2958f, st.swing * 57.2958f, st.rate, 1.0f / (kRowStrokesBase * st.rate)).c_str());
			}
		}

		// Shows the latest values at most about once a second, so held keys don't flood the notification queue.
		void FlushOarHud()
		{
			if (!oarHudDirty_) return;
			const auto now = std::chrono::steady_clock::now();
			if (now - oarHudLast_ < std::chrono::milliseconds(900)) return;
			oarHudLast_ = now;
			oarHudDirty_ = false;
			OarHud(false);
		}

		// =====================================================================
		// 56.5 (v1.0.57): two locked numpad modes aboard with [Boat] BakedOars=1. Num Lock = ROWER (the rower's seat and turn
		// only), Num Enter = BAKED OARS (the oars hung on AnimObjectA/B only, mirrored). The same key again, or Num5, leaves.
		// Neither touches the stowed oars (OarStow) or the boat's own rowing oar refs.
		// =====================================================================
		static constexpr std::uint32_t kRowerModeKey = 0x45;  // Num Lock
		static constexpr std::uint32_t kBakedModeKey = 0x9C;  // Num Enter
		enum class LockMode { kNone, kRower, kBaked };
		LockMode lockMode_{ LockMode::kNone };

		bool TuneLockModes(std::uint32_t a_key)
		{
			const bool modeKey = a_key == kRowerModeKey || a_key == kBakedModeKey;
			if (lockMode_ == LockMode::kNone && !modeKey) return false;
			if (modeKey || a_key == 0x4C) {
				// One physical press repeats for a few frames: ignore repeats.
				static auto lastModeKey = std::chrono::steady_clock::time_point{};
				const auto now = std::chrono::steady_clock::now();
				if (now - lastModeKey < std::chrono::milliseconds(300)) return true;
				lastModeKey = now;
				if (modeKey && (!playerSkim_ || !BoatBow::BakedOarsOn())) {
					RE::SendHUDMessage::ShowHUDMessage("ROWER / BAKED OARS tuning: aboard, with [Boat] BakedOars=1");
					lockMode_ = LockMode::kNone;
					return true;
				}
				const LockMode want = a_key == kRowerModeKey ? LockMode::kRower : a_key == kBakedModeKey ? LockMode::kBaked : LockMode::kNone;
				lockMode_ = want == lockMode_ ? LockMode::kNone : want;
				logger::info("Numpad: {}", lockMode_ == LockMode::kRower ? "ROWER mode" : lockMode_ == LockMode::kBaked ? "BAKED OARS mode" : "tuning mode left");
				if (lockMode_ == LockMode::kNone) RE::SendHUDMessage::ShowHUDMessage("Numpad tuning: off (Num Lock = ROWER, Num Enter = BAKED OARS)");
				else LockHud(true);
				return true;
			}
			if (!playerSkim_) { lockMode_ = LockMode::kNone; return false; }
			const auto now = std::chrono::steady_clock::now();
			if (now - lastTuneStep_ < std::chrono::milliseconds(70)) return true;
			lastTuneStep_ = now;
			if (lockMode_ == LockMode::kRower) TuneRowerLocked(a_key);
			else TuneBakedOarLocked(a_key);
			return true;
		}

		// The rower's seat: a follower rowing (seat slot 0), or the player on the rowing seat.
		struct RowerSeatRef { RE::NiPoint3* pos; float* yaw; float* side; float* fwd; float* lift; const char* who; };
		RowerSeatRef CurrentRowerSeat()
		{
			if (auto* hs = RowerSeat()) return { &hs->pos, &hs->yaw, nullptr, nullptr, nullptr, "follower" };
			if (playerOnRowSeat_) return { nullptr, &playerSeatYaw_, &playerBodySide_, &playerBodyFwd_, &playerBodyLift_, "player" };
			return { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		}

		void LockHud(bool a_title)
		{
			oarHudLegend_ = std::chrono::steady_clock::now();
			if (lockMode_ == LockMode::kRower) {
				if (a_title) RE::SendHUDMessage::ShowHUDMessage("ROWER: 8/2 forward-back, 4/6 left-right, 9/3 up-down, 7/1 turn. Num Lock or Num5 = leave");
				const auto s = CurrentRowerSeat();
				if (!s.yaw) { RE::SendHUDMessage::ShowHUDMessage("ROWER: nobody on the rowing seat"); return; }
				const RE::NiPoint3 p = s.pos ? *s.pos : RE::NiPoint3{ *s.side, *s.fwd, *s.lift };
				RE::SendHUDMessage::ShowHUDMessage(std::format("ROWER ({}): right {:.0f}  forward {:.0f}  up {:.0f}  turn {:.1f} deg", s.who, p.x, p.y, p.z, *s.yaw * 57.2958f).c_str());
			} else if (lockMode_ == LockMode::kBaked) {
				if (a_title) RE::SendHUDMessage::ShowHUDMessage("BAKED OARS: 8/2 forward-back, 4/6 in-out, 9/3 up-down, 7/1 yaw, 0/. roll, * and / pitch, +/- size. Num Enter or Num5 = leave");
				const auto& t = BoatBow::BakedOar();
				RE::SendHUDMessage::ShowHUDMessage(std::format("BAKED OARS: out {:.0f}  forward {:.0f}  up {:.0f}  yaw {:.1f}  pitch {:.1f}  roll {:.1f}  size {:.2f}",
					t.offset.x, t.offset.y, t.offset.z, t.yaw * 57.2958f, t.pitch * 57.2958f, t.roll * 57.2958f, t.scale).c_str());
			}
		}

		void TuneRowerLocked(std::uint32_t a_key)
		{
			const auto s = CurrentRowerSeat();
			if (!s.yaw) { LockHud(false); return; }
			float dx = 0.0f, dy = 0.0f, dz = 0.0f, dyaw = 0.0f;
			switch (a_key) {
			case 0x48: dy = 1.0f; break;      // Num8 / Num2 forward / back
			case 0x50: dy = -1.0f; break;
			case 0x4B: dx = -1.0f; break;     // Num4 / Num6 left / right
			case 0x4D: dx = 1.0f; break;
			case 0x49: dz = 1.0f; break;      // Num9 / Num3 up / down
			case 0x51: dz = -1.0f; break;
			case 0x47: dyaw = 0.0436f; break; // Num7 / Num1 turn (2.5 degrees)
			case 0x4F: dyaw = -0.0436f; break;
			default: return;
			}
			RE::NiPoint3 p;
			if (s.pos) {
				s.pos->x += dx; s.pos->y += dy; s.pos->z += dz;
				p = *s.pos;
				slotPos_[0] = *s.pos;  // the seat keeps it, whoever rows next
			} else {
				*s.side += dx; *s.fwd += dy; *s.lift += dz;
				p = RE::NiPoint3{ *s.side, *s.fwd, *s.lift };
			}
			*s.yaw += dyaw;
			if (s.pos) slotYaw_[0] = *s.yaw;
			const float def = kSeatYaw[0];  // 56.11: 180 for both
			logger::info("Rower seat tuning: right={:.0f} forward={:.0f} up={:.0f} yaw={:.1f} turn={:.1f} ({})", p.x, p.y, p.z, *s.yaw * 57.2958f,
				(*s.yaw - def) * 57.2958f, s.who);
			SaveRowerIni(s.pos != nullptr);
			LockHud(false);
		}

		void TuneBakedOarLocked(std::uint32_t a_key)
		{
			auto& t = BoatBow::BakedOar();
			switch (a_key) {
			case 0x48: t.offset.y += 1.0f; break;      // Num8 / Num2 forward / back
			case 0x50: t.offset.y -= 1.0f; break;
			case 0x4B: t.offset.x -= 1.0f; break;      // Num4 / Num6 in / out
			case 0x4D: t.offset.x += 1.0f; break;
			case 0x49: t.offset.z += 1.0f; break;      // Num9 / Num3 up / down
			case 0x51: t.offset.z -= 1.0f; break;
			case 0x47: t.yaw += 0.0436f; break;        // Num7 / Num1 yaw
			case 0x4F: t.yaw -= 0.0436f; break;
			case 0x52: t.roll += 0.0436f; break;       // Num0 / Num. roll about the shaft
			case 0x53: t.roll -= 0.0436f; break;
			case 0x37: t.pitch += 0.0436f; break;      // Num* / Num/ pitch
			case 0xB5: t.pitch -= 0.0436f; break;
			case 0x4E: t.scale = (std::min)(5.0f, t.scale + 0.02f); break;  // Num+ / Num- size
			case 0x4A: t.scale = (std::max)(0.1f, t.scale - 0.02f); break;
			default: return;
			}
			BoatBow::LogBakedOar();
			BoatBow::SaveBakedOar();
			LockHud(false);
		}

		// [Rower]: the follower's rowing seat (slot 0) and the player's rowing seat, written on every ROWER step.
		void SaveRowerIni(bool a_follower)
		{
			const auto ini = FlowIniPath();
			auto put = [&](const char* a_k, float a_v) { WritePrivateProfileStringA("Rower", a_k, std::format("{:g}", std::round(a_v * 100.0f) / 100.0f).c_str(), ini.c_str()); };
			if (a_follower) {
				put("SeatRight", slotPos_[0].x); put("SeatForward", slotPos_[0].y); put("SeatUp", slotPos_[0].z); put("SeatYaw", slotYaw_[0] * 57.2958f);
			} else {
				put("PlayerRight", playerBodySide_); put("PlayerForward", playerBodyFwd_); put("PlayerUp", playerBodyLift_); put("PlayerYaw", playerSeatYaw_ * 57.2958f);
			}
		}

		void LoadRowerIni()
		{
			const auto ini = FlowIniPath();
			auto get = [&](const char* a_k, float& a_out) {
				char buf[64]{};
				GetPrivateProfileStringA("Rower", a_k, "", buf, sizeof(buf), ini.c_str());
				if (buf[0]) { try { a_out = std::stof(buf); return true; } catch (...) {} }
				return false;
			};
			float deg = 0.0f;
			get("SeatRight", slotPos_[0].x); get("SeatForward", slotPos_[0].y); get("SeatUp", slotPos_[0].z);
			if (get("SeatYaw", deg)) slotYaw_[0] = deg * 0.0174533f;
			// The player's rowing seat is parked while the player sits at the stern (SelectPlayerSeat swaps them).
			PlayerSeatSet& ps = parkedPlayerSeat_;
			get("PlayerRight", ps.side); get("PlayerForward", ps.fwd); get("PlayerUp", ps.lift);
			if (get("PlayerYaw", deg)) ps.yaw = deg * 0.0174533f;
			logger::info("Rower ini: follower seat right={:.0f} forward={:.0f} up={:.0f} yaw={:.1f} | player rowing seat right={:.0f} forward={:.0f} up={:.0f} yaw={:.1f}",
				slotPos_[0].x, slotPos_[0].y, slotPos_[0].z, slotYaw_[0] * 57.2958f, ps.side, ps.fwd, ps.lift, ps.yaw * 57.2958f);
		}

		// ---- 56.29: numpad off by default, one target at a time from the console ----
		// oas "num stow"   the stowed oars (on foot by the parked boat or aboard)
		// oas "num rower"  ROWER: the rowing seat and turn (aboard, [Boat] BakedOars=1)
		// oas "num oars"   BAKED OARS: the oars in the rower's hands (aboard, [Boat] BakedOars=1)
		// oas "num height" the player's boat height: Num9 / Num3, Num0 + Num9 resets
		// oas "num npc"    the NPC lake boats' height: Num9 / Num3, Num0 resets (all of them as one)
		// oas "num crew"   the human crew seats, one at a time (Num5 = next human)
		// oas "num dog"    the dog seats, one at a time (Num5 = next dog, Num0 resets that dog)
		// oas "num off"    numpad off again (also off at every game start). Benches and chest are not tunable any more.
		enum class NumMode { kOff, kStow, kRower, kOars, kHeight, kNpcHeight, kCrew, kDogs, kFloor, kRipple, kSpeed };
		int rippleSide_{ 0 };  // 56.37: which stern ripple oas "num ripple" moves (0 = left / port, 1 = right / starboard)
		NumMode numMode_{ NumMode::kOff };
		static constexpr int kDogSeats = 4;
		RE::NiPoint3 dogSeatOff_[kDogSeats]{};  // per dog, added to the shared bow slot (dog i sits 70 behind dog i-1)
		float dogSeatYaw_[kDogSeats]{};
		std::chrono::steady_clock::time_point lastNumStep_{};
		std::chrono::steady_clock::time_point lastNumRefuse_{};

		static bool IsNumpadScan(std::uint32_t a_id)
		{
			return a_id == 0x37 || a_id == 0x45 || (a_id >= 0x47 && a_id <= 0x53) || a_id == 0x9C || a_id == 0xB5;
		}
		static const char* NumModeName(NumMode a_m)
		{
			switch (a_m) {
			case NumMode::kStow: return "stow";
			case NumMode::kRower: return "rower";
			case NumMode::kOars: return "oars";
			case NumMode::kHeight: return "height";
			case NumMode::kNpcHeight: return "npc";
			case NumMode::kCrew: return "crew";
			case NumMode::kDogs: return "dog";
			case NumMode::kFloor: return "floor";
			case NumMode::kRipple: return "ripple";
			case NumMode::kSpeed: return "speed";
			default: return "off";
			}
		}

		RE::NiPoint3 DogLocal(int a_i) const
		{
			const int i = std::clamp(a_i, 0, kDogSeats - 1);
			return RE::NiPoint3{ bowSlot_.x + dogSeatOff_[i].x, bowSlot_.y - 70.0f * static_cast<float>(a_i) + dogSeatOff_[i].y, bowSlot_.z + dogSeatOff_[i].z };
		}
		float DogYaw(int a_i) const { return dogYaw_ + dogSeatYaw_[std::clamp(a_i, 0, kDogSeats - 1)]; }

		void SaveDogSeat(int a_i)
		{
			const auto& o = dogSeatOff_[a_i];
			WritePrivateProfileStringA("Dog", std::format("Seat{}", a_i).c_str(), std::format("{:.0f},{:.0f},{:.0f},{:.1f}", o.x, o.y, o.z, dogSeatYaw_[a_i] * 57.2958f).c_str(), FlowIniPath().c_str());
		}
		void LoadDogSeatsIni()
		{
			for (int i = 0; i < kDogSeats; ++i) {
				char buf[96]{};
				GetPrivateProfileStringA("Dog", std::format("Seat{}", i).c_str(), "", buf, sizeof(buf), FlowIniPath().c_str());
				float x = 0.0f, y = 0.0f, z = 0.0f, yawDeg = 0.0f;
				if (buf[0] && std::sscanf(buf, "%f,%f,%f,%f", &x, &y, &z, &yawDeg) >= 3) {
					dogSeatOff_[i] = RE::NiPoint3{ x, y, z };
					dogSeatYaw_[i] = yawDeg / 57.2958f;
					logger::info("Dog seat {}: offset right {:.0f} forward {:.0f} up {:.0f} turn {:.1f} deg (ini)", i, x, y, z, yawDeg);
				}
			}
		}

		// The console side: true and a_msg filled when the word is a target (or off / empty = show).
		bool SetNumMode(std::string_view a_word, std::string& a_msg)
		{
			NumMode want = NumMode::kOff;
			if (a_word.empty()) {
				a_msg = std::format("numpad: {} (targets: stow, rower, oars, height, npc, crew, dog, floor, ripple, speed, off)", NumModeName(numMode_));
				return true;
			}
			if (a_word == "off") want = NumMode::kOff;
			else if (a_word == "stow" || a_word == "stowed") want = NumMode::kStow;
			else if (a_word == "rower") want = NumMode::kRower;
			else if (a_word == "oars" || a_word == "baked") want = NumMode::kOars;
			else if (a_word == "height" || a_word == "boat") want = NumMode::kHeight;
			else if (a_word == "npc" || a_word == "npcheight" || a_word == "lake") want = NumMode::kNpcHeight;
			else if (a_word == "crew") want = NumMode::kCrew;
			else if (a_word == "dog" || a_word == "dogs") want = NumMode::kDogs;
			else if (a_word == "floor") want = NumMode::kFloor;
			else if (a_word == "speed" || a_word == "row") want = NumMode::kSpeed;
			else if (a_word == "ripple" || a_word == "ripples" || a_word == "wake") want = NumMode::kRipple;
			else { a_msg = "num takes: stow, rower, oars, height, npc, crew, dog, floor, ripple, speed, off"; return false; }
			if (want == NumMode::kRower || want == NumMode::kOars) {
				if (!playerSkim_ || !BoatBow::BakedOarsOn()) { a_msg = "rower / oars tuning: aboard, with [Boat] BakedOars=1"; return false; }
			}
			// Leave whatever was on before.
			if (numMode_ == NumMode::kStow) BoatBow::SetOarStowPreview(false);
			if (lockMode_ != LockMode::kNone) { lockMode_ = LockMode::kNone; logger::info("Numpad: tuning mode left"); }
			numMode_ = want;
			crewTargetId_ = 0;
			switch (want) {
			case NumMode::kStow: BoatBow::SetOarStowPreview(true); OarStowHud(true); break;
			case NumMode::kRower: lockMode_ = LockMode::kRower; LockHud(true); break;
			case NumMode::kOars: lockMode_ = LockMode::kBaked; LockHud(true); break;
			case NumMode::kHeight: RE::SendHUDMessage::ShowHUDMessage(std::format("BOAT HEIGHT {:+.0f}: Num9 / Num3 up-down, Num0 + Num9 reset", boatHeight_).c_str()); break;
			case NumMode::kNpcHeight: LakeBoats::TuneRig(0, true); break;
			case NumMode::kCrew: RE::SendHUDMessage::ShowHUDMessage("CREW SEATS: 8/2 forward-back, 4/6 left-right, 9/3 up-down, 7/1 turn. Num5 = next crew member"); break;
			case NumMode::kSpeed: RE::SendHUDMessage::ShowHUDMessage("ROW SPEED: Num8 / Num2 top speed, Num9 / Num3 glide (less / more slowing between strokes), Num4 / Num6 turning, Num0 reset"); RowSpeedHud(); break;
			case NumMode::kRipple: rippleSide_ = 0; RE::SendHUDMessage::ShowHUDMessage("STERN RIPPLE: 8/2 forward-back, 4/6 left-right, 9/3 up-down, Num0 reset. Num5 = the other one"); RippleSideHud(); break;
			case NumMode::kFloor: RE::SendHUDMessage::ShowHUDMessage(std::format("FLOOR {:+.1f}: Num9 / Num3 up-down (0.5 a step), Num0 back to +3", BoatBow::FloorOffset()).c_str()); break;
			case NumMode::kDogs: RE::SendHUDMessage::ShowHUDMessage("DOG SEATS: 8/2 forward-back, 4/6 left-right, 9/3 up-down, 7/1 turn, 0 reset. Num5 = next dog"); break;
			default: RE::SendHUDMessage::ShowHUDMessage("Numpad: off"); break;
			}
			logger::info("Numpad: target {} (oas num)", NumModeName(numMode_));
			a_msg = std::format("numpad: {}", NumModeName(numMode_));
			return true;
		}

		// 56.44: the row speed, tuned in game (oas "num speed" or oas "row speed N" / "row drag N"), kept in the ini
		// [Row] MaxSpeed / Glide. The solo-rower boost (kLightBoatFactor) applies on top.
		void SetRowSpeed(float a_max, float a_drag, const char* a_why, bool a_save = true)
		{
			rowProfile_.maxSpeed = std::clamp(std::round(a_max), 100.0f, 700.0f);
			rowProfile_.coastDrag = std::clamp(std::round(a_drag * 100.0f) / 100.0f, 0.01f, 0.80f);
			rowProfile_.accel = rowProfile_.maxSpeed * kRowAccelRate;  // 56.49: quick pick-up whatever the glide
			if (a_save) {
				WritePrivateProfileStringA("Row", "Model", "2", FlowIniPath().c_str());
				WritePrivateProfileStringA("Row", "MaxSpeed", std::format("{:.0f}", rowProfile_.maxSpeed).c_str(), FlowIniPath().c_str());
				WritePrivateProfileStringA("Row", "Glide", std::format("{:.2f}", rowProfile_.coastDrag).c_str(), FlowIniPath().c_str());
			}
			logger::info("Row: top speed {:.0f}, glide drag {:.2f} ({})", rowProfile_.maxSpeed, rowProfile_.coastDrag, a_why);
		}
		// 56.45: turning rate as a scale of the default (0.5 rad/s slow, 0.45 at speed), [Row] Turn.
		float rowTurnScale_{ 1.0f };
		void SetRowTurn(float a_scale, const char* a_why, bool a_save = true)
		{
			rowTurnScale_ = std::clamp(std::round(a_scale * 20.0f) / 20.0f, 0.3f, 3.0f);
			rowProfile_.turnLow = 0.5f * rowTurnScale_;
			rowProfile_.turnHigh = 0.45f * rowTurnScale_;
			if (a_save) WritePrivateProfileStringA("Row", "Turn", std::format("{:.2f}", rowTurnScale_).c_str(), FlowIniPath().c_str());
			logger::info("Row: turning {:.0f}% ({})", rowTurnScale_ * 100.0f, a_why);
		}
		void LoadRowSpeedIni()
		{
			{
				char t[32]{};
				GetPrivateProfileStringA("Row", "Turn", "", t, sizeof(t), FlowIniPath().c_str());
				if (t[0]) { try { SetRowTurn(std::stof(t), "ini", false); } catch (...) {} }
			}
			char a[32]{}, b[32]{};
			GetPrivateProfileStringA("Row", "MaxSpeed", "", a, sizeof(a), FlowIniPath().c_str());
			GetPrivateProfileStringA("Row", "Glide", "", b, sizeof(b), FlowIniPath().c_str());
			// 56.49: a Glide saved before the new model (drag 0.05, own acceleration) is dropped once for the new default.
			if (GetPrivateProfileIntA("Row", "Model", 1, FlowIniPath().c_str()) < 2) {
				if (b[0]) logger::info("Row: old saved glide {} dropped for the new default {:.2f}", b, kRowCoastDrag);
				b[0] = 0;
			}
			if (!a[0] && !b[0]) return;
			float m = kRowMaxSpeed, d = kRowCoastDrag;
			try { if (a[0]) m = std::stof(a); } catch (...) {}
			try { if (b[0]) d = std::stof(b); } catch (...) {}
			SetRowSpeed(m, d, "ini", false);
		}
		void RowSpeedHud()
		{
			const float pct = 100.0f * rowProfile_.maxSpeed / kRowMaxSpeed;
			RE::SendHUDMessage::ShowHUDMessage(std::format("Row speed {:.0f} ({:.0f}% of default)   glide drag {:.2f} (default {:.2f})   turning {:.0f}%", rowProfile_.maxSpeed, pct, rowProfile_.coastDrag, kRowCoastDrag, rowTurnScale_ * 100.0f).c_str());
		}

		// 56.45: ripple sizes kept in the ini [Ripple] Wake / Oar / Stern / Space (oas "ripple wake N" etc.).
		static void SaveRippleIni()
		{
			const auto& rt = BoatBow::Ripples();
			const auto ini = FlowIniPath();
			WritePrivateProfileStringA("Ripple", "Wake", std::format("{:.3f}", rt.wake).c_str(), ini.c_str());
			WritePrivateProfileStringA("Ripple", "Oar", std::format("{:.3f}", rt.oar).c_str(), ini.c_str());
			WritePrivateProfileStringA("Ripple", "Stern", std::format("{:.0f}", rt.stern).c_str(), ini.c_str());
			WritePrivateProfileStringA("Ripple", "Space", std::format("{:.0f}", rt.space).c_str(), ini.c_str());
		}
		void LoadRippleIni()
		{
			auto& rt = BoatBow::Ripples();
			const auto ini = FlowIniPath();
			auto rd = [&](const char* a_key, float& a_v, float a_min) {
				char b[32]{};
				GetPrivateProfileStringA("Ripple", a_key, "", b, sizeof(b), ini.c_str());
				if (!b[0]) return;
				try { const float v = std::stof(b); if (v >= a_min) a_v = v; } catch (...) {}
			};
			rd("Wake", rt.wake, 0.001f);
			rd("Oar", rt.oar, 0.001f);
			rd("Stern", rt.stern, -10000.0f);
			rd("Space", rt.space, 1.0f);
			logger::info("Ripple: wake {:.3f} oar {:.3f} stern {:.0f} space {:.0f} (ini or defaults)", rt.wake, rt.oar, rt.stern, rt.space);
		}

		// 56.45: oas "reset settings": every value a player can tune back to the mod's defaults (and saved so).
		void ResetPlayerSettings()
		{
			SetBoatHeight(kDefaultBoatHeight, "reset settings", false);
			BoatBow::SetFloorOffset(3.0f);
			SetRowSpeed(kRowMaxSpeed, kRowCoastDrag, "reset settings");
			SetRowTurn(1.0f, "reset settings");
			BoatBow::Ripples() = BoatBow::RippleTune{};
			SaveRippleIni();
			BoatBow::WakeSideOffset(0) = RE::NiPoint3{};
			BoatBow::WakeSideOffset(1) = RE::NiPoint3{};
			BoatBow::SaveWakeSideOffsets();
			for (int i = 0; i < kDogSeats; ++i) { dogSeatOff_[i] = RE::NiPoint3{}; dogSeatYaw_[i] = 0.0f; SaveDogSeat(i); }
			LakeBoats::Command("lakeboats on");
			LakeBoats::Command("lakeboats 6");
			LakeBoats::Command("lakeboats view 5");
			LakeBoats::TuneRig(0x52, false);  // NPC boat height back to its default
			numMode_ = NumMode::kOff;
			RE::SendHUDMessage::ShowHUDMessage("Oars and Sails: settings back to the defaults");
			logger::info("Settings: all player settings reset to the defaults");
		}

		void RippleSideHud()
		{
			const auto& o = BoatBow::WakeSideOffset(rippleSide_);
			RE::SendHUDMessage::ShowHUDMessage(std::format("Stern ripple {} (boat's {} side): right {:.0f}  forward {:.0f}  up {:.0f}", rippleSide_ == 0 ? "LEFT" : "RIGHT",
				rippleSide_ == 0 ? "left" : "right", o.x, o.y, o.z).c_str());
		}
		bool NumStepReady()
		{
			const auto now = std::chrono::steady_clock::now();
			if (now - lastNumStep_ < std::chrono::milliseconds(70)) return false;  // a held key repeats about 14 times a second
			lastNumStep_ = now;
			return true;
		}
		void NumRefuse(const char* a_why)
		{
			const auto now = std::chrono::steady_clock::now();
			if (now - lastNumRefuse_ < std::chrono::seconds(2)) return;
			lastNumRefuse_ = now;
			RE::SendHUDMessage::ShowHUDMessage(a_why);
			logger::info("Numpad ({}): {}", NumModeName(numMode_), a_why);
		}

		void NumpadByMode(std::uint32_t a_key)
		{
			switch (numMode_) {
			case NumMode::kHeight:
				if (a_key != 0x49 && a_key != 0x51) return;
				if (!OwnBoatHere()) { NumRefuse("Boat height: no boat of yours here"); return; }
				if (!NumStepReady()) return;
				if (num0Held_ && a_key == 0x49) SetBoatHeight(kDefaultBoatHeight, "reset, Num0 + Num9");
				else SetBoatHeight(boatHeight_ + (a_key == 0x49 ? 1.0f : -1.0f), "Num9/Num3");
				return;
			case NumMode::kSpeed: {
				if (a_key != 0x48 && a_key != 0x50 && a_key != 0x49 && a_key != 0x51 && a_key != 0x52 && a_key != 0x4B && a_key != 0x4D) return;
				if (!NumStepReady()) return;
				float maxS = rowProfile_.maxSpeed, drag = rowProfile_.coastDrag;
				if (a_key == 0x4B || a_key == 0x4D) {  // Num4 / Num6: turn slower / faster (56.45)
					SetRowTurn(rowTurnScale_ + (a_key == 0x4D ? 0.05f : -0.05f), "numpad");
					RowSpeedHud();
					return;
				}
				switch (a_key) {
				case 0x48: maxS += 10.0f; break;   // Num8 faster
				case 0x50: maxS -= 10.0f; break;   // Num2 slower
				case 0x49: drag -= 0.01f; break;   // Num9 glides further (less slowing between strokes)
				case 0x51: drag += 0.01f; break;   // Num3 slows more between strokes
				case 0x52: maxS = kRowMaxSpeed; drag = kRowCoastDrag; SetRowTurn(1.0f, "numpad reset"); break;  // Num0 defaults (turn too)
				default: break;
				}
				SetRowSpeed(maxS, drag, "numpad");
				RowSpeedHud();
				return;
			}
			case NumMode::kRipple: {
				if (a_key == 0x4C) {  // Num5: the other stern ripple
					static auto lastFlip = std::chrono::steady_clock::time_point{};
					const auto now = std::chrono::steady_clock::now();
					if (now - lastFlip < std::chrono::milliseconds(300)) return;
					lastFlip = now;
					rippleSide_ = 1 - rippleSide_;
					RippleSideHud();
					return;
				}
				if (!NumStepReady()) return;
				auto& o = BoatBow::WakeSideOffset(rippleSide_);
				switch (a_key) {
				case 0x48: o.y += 1.0f; break;  // Num8 / Num2 forward / back
				case 0x50: o.y -= 1.0f; break;
				case 0x4B: o.x -= 1.0f; break;  // Num4 / Num6 left / right
				case 0x4D: o.x += 1.0f; break;
				case 0x49: o.z += 1.0f; break;  // Num9 / Num3 up / down
				case 0x51: o.z -= 1.0f; break;
				case 0x52: o = RE::NiPoint3{}; break;  // Num0 reset
				default: return;
				}
				BoatBow::SaveWakeSideOffsets();
				logger::info("Ripple: stern {} offset right={:.0f} forward={:.0f} up={:.0f}", rippleSide_ == 0 ? "left" : "right", o.x, o.y, o.z);
				RippleSideHud();
				return;
			}
			case NumMode::kFloor: {
				if (a_key != 0x49 && a_key != 0x51 && a_key != 0x52) return;
				if (!NumStepReady()) return;
				const float f = a_key == 0x52 ? 3.0f : BoatBow::FloorOffset() + (a_key == 0x49 ? 0.5f : -0.5f);
				BoatBow::SetFloorOffset(f);
				RE::SendHUDMessage::ShowHUDMessage(std::format("Floor {:+.1f}", BoatBow::FloorOffset()).c_str());
				return;
			}
			case NumMode::kNpcHeight:
				if (a_key != 0x49 && a_key != 0x51 && a_key != 0x52) return;  // height only (Num0 = default)
				if (!NumStepReady()) return;
				LakeBoats::TuneRig(a_key, false);
				return;
			case NumMode::kStow:
				if (!OwnBoatHere()) { NumRefuse("Stowed oars: no boat of yours here"); return; }
				if (!NumStepReady()) return;
				TuneOarStow(a_key);
				return;
			case NumMode::kRower:
			case NumMode::kOars: {
				const LockMode want = numMode_ == NumMode::kRower ? LockMode::kRower : LockMode::kBaked;
				if (!playerSkim_ || lockMode_ != want) {
					// Left (Num5, the mode key, or the unboard): numpad off until the next oas num.
					lockMode_ = LockMode::kNone;
					numMode_ = NumMode::kOff;
					RE::SendHUDMessage::ShowHUDMessage("Numpad: off (oas num rower / oas num oars to tune again)");
					logger::info("Numpad: off (rower / oars mode ended)");
					return;
				}
				if (a_key == kRowerModeKey || a_key == kBakedModeKey) return;  // only oas num switches between them
				TuneLockModes(a_key);
				if (lockMode_ == LockMode::kNone) { numMode_ = NumMode::kOff; logger::info("Numpad: off (left with Num5)"); }
				return;
			}
			case NumMode::kCrew:
			case NumMode::kDogs:
				if (a_key != 0x4C && !NumStepReady()) return;
				TuneSeats(numMode_ == NumMode::kDogs, a_key);
				return;
			default:
				return;
			}
		}

		// Crew seats (humans only) or dog seats (dogs only), one at a time; Num5 = next of that kind.
		void TuneSeats(bool a_dogs, std::uint32_t a_key)
		{
			lastCrewTune_ = std::chrono::steady_clock::now();  // no seated idles while the seats are being tuned
			std::vector<CrewTarget> list;
			for (auto& t : CrewTargets()) if (t.dog == a_dogs) list.push_back(t);
			if (list.empty()) { NumRefuse(a_dogs ? "Dog seats: no dog aboard" : "Crew seats: no crew aboard"); return; }
			std::size_t idx = 0;
			bool found = false;
			for (std::size_t i = 0; i < list.size(); ++i) {
				if (list[i].actor->GetFormID() == crewTargetId_) { idx = i; found = true; break; }
			}
			auto announce = [&](std::size_t a_i) {
				const auto& t = list[a_i];
				crewTargetId_ = t.actor->GetFormID();
				logger::info("Numpad now tunes {} ({} slot {})", t.actor->GetName(), t.dog ? "dog" : "human", t.slot);
				CrewHud(t, true);
			};
			if (a_key == 0x4C) {  // Num5: next (one physical press repeats for a few frames: ignore repeats)
				static auto lastSeatFlip = std::chrono::steady_clock::time_point{};
				const auto nowKey = std::chrono::steady_clock::now();
				if (nowKey - lastSeatFlip < std::chrono::milliseconds(300)) return;
				lastSeatFlip = nowKey;
				announce(found ? (idx + 1) % list.size() : 0);
				return;
			}
			if (!found) { idx = 0; announce(0); }
			const auto& t = list[idx];
			if (t.dog) {
				const int i = std::clamp(t.slot, 0, kDogSeats - 1);
				auto& o = dogSeatOff_[i];
				bool changed = true;
				switch (a_key) {
				case 0x48: o.y += 1.0f; break;  // Num8
				case 0x50: o.y -= 1.0f; break;  // Num2
				case 0x4B: o.x -= 1.0f; break;  // Num4
				case 0x4D: o.x += 1.0f; break;  // Num6
				case 0x49: o.z += 1.0f; break;  // Num9
				case 0x51: o.z -= 1.0f; break;  // Num3
				case 0x47: dogSeatYaw_[i] += 0.0436f; break;  // Num7
				case 0x4F: dogSeatYaw_[i] -= 0.0436f; break;  // Num1
				case 0x52: o = RE::NiPoint3{}; dogSeatYaw_[i] = 0.0f; break;  // Num0: this dog back to the shared bow slot
				default: changed = false; break;
				}
				if (changed) {
					SaveDogSeat(i);
					logger::info("Dog {:08X} {} seat {}: offset right={:.0f} forward={:.0f} up={:.0f} turn={:.1f}deg", t.actor->GetFormID(), t.actor->GetName(), i, o.x, o.y, o.z, dogSeatYaw_[i] * 57.2958f);
					CrewHud(t, false);
				}
				return;
			}
			auto& hs = GetHumanSeat(t.actor.get());
			bool changed = true;
			switch (a_key) {
			case 0x48: hs.pos.y += 1.0f; break;  // Num8
			case 0x50: hs.pos.y -= 1.0f; break;  // Num2
			case 0x4B: hs.pos.x -= 1.0f; break;  // Num4
			case 0x4D: hs.pos.x += 1.0f; break;  // Num6
			case 0x49: hs.pos.z += 1.0f; break;  // Num9
			case 0x51: hs.pos.z -= 1.0f; break;  // Num3
			case 0x47: hs.yaw += 0.0436f; break;  // Num7 (+2.5 deg)
			case 0x4F: hs.yaw -= 0.0436f; break;  // Num1
			default: changed = false; break;
			}
			if (changed) {
				if (hs.slot >= 0 && hs.slot < kSeatCount) { slotPos_[hs.slot] = hs.pos; slotYaw_[hs.slot] = hs.yaw; }  // the seat keeps it, whoever sits there next
				logger::info("Human {:08X} {} seat tuning slot {}: right={:.0f} forward={:.0f} up={:.0f} yaw={:.0f}deg",
					hs.id, t.actor->GetName(), hs.slot, hs.pos.x, hs.pos.y, hs.pos.z, hs.yaw * 57.2958f);
				CrewHud(t, false);
			}
		}

		// ---- 56.26 (v1.0.75): the player's boat height in the water ----
		// One offset (units, -30..+30) added to every height the boat is set to: parked (kParkedBoatZ), underway (with the
		// kSpeedSink sink) and the ride height of the player and passengers (kV7PlayerDrop), so the hull and everything on
		// it keep their places. Num9 / Num3 aboard or beside our parked boat; Num0 + Num9 resets; oas "boat height N".
		// Kept in [Rower] BoatHeight and the co-save ('BHGT').
		static constexpr float kDefaultBoatHeight = 5.0f;  // 56.34 / 56.42: Miguel's tuned hull height (5), locked in as the default
		float boatHeight_{ kDefaultBoatHeight };
		bool shiftHeld_{ false };
		bool num0Held_{ false };
		bool heightFloorWarned_{ false };
		std::chrono::steady_clock::time_point lastHeightStep_{};
		std::chrono::steady_clock::time_point lastHeightRefuse_{};
		static constexpr float kBilgeFloorZ = 28.0f;  // the bilge floor's model height

		bool OwnBoatHere()
		{
			float x = 0.0f, y = 0.0f, z = 0.0f, yaw = 0.0f;
			return playerSkim_ || BoatBow::GetParked(x, y, z, yaw);
		}

		void SetBoatHeight(float a_height, const char* a_why, bool a_hud = true)
		{
			const float old = boatHeight_;
			boatHeight_ = std::clamp(std::round(a_height), -30.0f, 30.0f);
			if (!playerSkim_ && boatHeight_ != old) BoatBow::NudgeParkedZ(boatHeight_ - old);  // aboard, the ride picks it up next frame
			WritePrivateProfileStringA("Rower", "BoatHeight", std::format("{:.0f}", boatHeight_).c_str(), FlowIniPath().c_str());
			if (a_hud) RE::SendHUDMessage::ShowHUDMessage(std::format("Boat height {:+.0f}", boatHeight_).c_str());
			logger::info("Boat: height {:.0f} ({})", boatHeight_, a_why);
			const float water = -kParkedBoatZ + kSpeedSink - boatHeight_;  // the speed waterline in model space
			if (water >= kBilgeFloorZ) {
				if (!heightFloorWarned_) logger::warn("Boat: height {:.0f} puts the water above the bilge floor (floor {:.0f}, water {:.0f})", boatHeight_, kBilgeFloorZ, water);
				heightFloorWarned_ = true;
			} else {
				heightFloorWarned_ = false;
			}
		}

		void BoatHeightKey(std::uint32_t a_key)
		{
			const auto now = std::chrono::steady_clock::now();
			if (!OwnBoatHere()) {
				if (now - lastHeightRefuse_ >= std::chrono::seconds(1)) {
					lastHeightRefuse_ = now;
					logger::info("Numpad: Num9/Num3 ignored (no player boat)");
				}
				return;
			}
			if (shiftHeld_ || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) {
				// The old per-target up / down, our own boat's targets only (never the NPC lake boats).
				if (playerSkim_ || TuneWhileParked(a_key)) TuneNumpad(a_key);
				return;
			}
			if (now - lastHeightStep_ < std::chrono::milliseconds(70)) return;  // a held key repeats about 14 times a second
			lastHeightStep_ = now;
			if (num0Held_ && a_key == 0x49) {
				SetBoatHeight(kDefaultBoatHeight, "reset, Num0 + Num9");
				return;
			}
			SetBoatHeight(boatHeight_ + (a_key == 0x49 ? 1.0f : -1.0f), "Num9/Num3");
		}

		void LoadBoatHeightIni()
		{
			char buf[64]{};
			GetPrivateProfileStringA("Rower", "BoatHeight", "", buf, sizeof(buf), FlowIniPath().c_str());
			float v = kDefaultBoatHeight;  // 56.34
			if (buf[0]) { try { v = std::stof(buf); } catch (...) {} }
			boatHeight_ = std::clamp(std::round(v), -30.0f, 30.0f);
			logger::info("Boat: height {:.0f} (ini)", boatHeight_);
		}

		bool TuneWhileParked(std::uint32_t a_id)
		{
			float x = 0.0f, y = 0.0f, z = 0.0f, yaw = 0.0f;
			// 56.26: no boat of our own parked: the numpad does nothing (the NPC lake boats are no longer tuned from here).
			if (!BoatBow::GetParked(x, y, z, yaw)) return false;
			if (kNumpadTargets == NumpadTargets::kCrew) return a_id == 0x4C || crewTargetId_ == kHelmBenchTarget || crewTargetId_ == kRowBenchTarget || crewTargetId_ == kOarStowTarget;  // the benches and stowed oars, on foot
			return a_id == 0x4C || tuneMode_ >= 100;
		}

		void TuneNumpad(std::uint32_t a_key)
		{
			if (TuneLockModes(a_key)) return;  // 56.5: ROWER / BAKED OARS
			// Fine and slow: 1 unit (2.5 degrees, 2% scale) per step, and a held key repeats about 14 times a second.
			if (a_key != 0x4C) {
				const auto now = std::chrono::steady_clock::now();
				if (now - lastTuneStep_ < std::chrono::milliseconds(70)) return;
				lastTuneStep_ = now;
			}
			constexpr int kPlankMode = 100;
			constexpr int kOarRMode = 101;
			constexpr int kOarLMode = 102;
			// Lamps only (v1.0.7, while the lamps are being tuned): the numpad starts on the stern lamp, Num5 flips stern <-> bow,
			// and nothing else (oars, the follower in slot one) is reachable. kNumpadLampsOnly = false brings back v1.0.6.
			if (kNumpadTargets == NumpadTargets::kCrew) {
				float px = 0.0f, py = 0.0f, pz = 0.0f, pyaw = 0.0f;
				if (!playerSkim_ && !BoatBow::GetParked(px, py, pz, pyaw)) return;  // 56.26: no boat of ours, the numpad does nothing
				TuneCrew(a_key);
				return;
			}
			if (kNumpadLampsOnly) {
				if (tuneMode_ != kLampSternModeId && tuneMode_ != kLampBowModeId) tuneMode_ = kLampSternModeId;
				numCycleInit_ = true;
				if (a_key == 0x4C) {
					static auto lastLampFlip = std::chrono::steady_clock::time_point{};
					const auto nowKey = std::chrono::steady_clock::now();
					if (nowKey - lastLampFlip < std::chrono::milliseconds(300)) return;
					lastLampFlip = nowKey;
					tuneMode_ = tuneMode_ == kLampSternModeId ? kLampBowModeId : kLampSternModeId;
					logger::info("Numpad now tunes {}", tuneMode_ == kLampSternModeId ? "the stern lamp" : "the bow lamp");
					LampHud(true);
					return;
				}
				TuneLamp(tuneMode_ == kLampSternModeId ? 0 : 1, a_key);
				return;
			}
			// v1.0.6 behaviour: Num Lock cycles the oars -> the stern lamp -> the bow lamp -> the oars (Num0 is the oar sweep).
			if (kNumpadTargets == NumpadTargets::kDefault && a_key == kLampKey) {
				static auto lastLampKey = std::chrono::steady_clock::time_point{};
				const auto nowKey = std::chrono::steady_clock::now();
				if (nowKey - lastLampKey < std::chrono::milliseconds(300)) return;
				lastLampKey = nowKey;
				tuneMode_ = tuneMode_ == kLampSternModeId ? kLampBowModeId : (tuneMode_ == kLampBowModeId ? kOarRMode : kLampSternModeId);
				numCycleInit_ = true;
				logger::info("Numpad now tunes {}", tuneMode_ == kLampSternModeId ? "the stern lamp" : (tuneMode_ == kLampBowModeId ? "the bow lamp" : "the oars"));
				OarHud(true);
				return;
			}
			if ((tuneMode_ == kLampSternModeId || tuneMode_ == kLampBowModeId) && a_key != 0x4C) {
				TuneLamp(tuneMode_ == kLampSternModeId ? 0 : 1, a_key);
				return;
			}
			// Old single-target locks, kept off; the Num5 cycle below replaces them.
			constexpr bool kLockNumToPlayerOnly = false;
			constexpr bool kLockNumToOarsOnly = false;
			// Num5 cycles the four things being tuned: the rower (the follower in the first seat) -> the oars -> the chest -> the
			// player's own seat -> back to the rower. Num Enter flips the oar page. Every key works on whichever is shown.
			constexpr bool kCycleRowerOarsChest = true;
			// The numpad starts on the oars. Num5 flips between the oars and the follower in slot one (the first passenger seat).
			constexpr bool kLockNumToFollowerSlot1 = true;
			if (kLockNumToFollowerSlot1) {
				int mode = -1, firstOther = -1;
				const auto humans = BoardedHumans();
				for (size_t i = 0; i < humans.size(); ++i) {
					const int slot = GetHumanSeat(humans[i].get()).slot;
					if (slot == 1) { mode = 2 + static_cast<int>(i); break; }
					if (slot != 0 && firstOther < 0) firstOther = 2 + static_cast<int>(i);
				}
				if (mode < 0) mode = firstOther >= 0 ? firstOther : (humans.empty() ? 0 : 2);  // nobody in slot one: the first non-rowing human, else the player's seat
				if (!numCycleInit_) tuneMode_ = kOarRMode;  // the numpad starts on the oars
				const bool onOars = tuneMode_ == kOarRMode || tuneMode_ == kOarLMode;
				if (a_key == 0x4C) {
					// One physical press repeats for a few frames and flipped the page several times in the log: ignore repeats.
					static auto lastNum5 = std::chrono::steady_clock::time_point{};
					const auto nowKey = std::chrono::steady_clock::now();
					if (nowKey - lastNum5 < std::chrono::milliseconds(300)) return;
					lastNum5 = nowKey;
					tuneMode_ = onOars ? mode : kOarRMode;
					numCycleInit_ = true;
					logger::info("Numpad now tunes {}", tuneMode_ == kOarRMode ? "the oars" : "the follower in slot one");
					if (tuneMode_ == kOarRMode) OarHud(true);
					else SeatHud(true);
					return;
				}
				if (!onOars && tuneMode_ != mode) {
					tuneMode_ = mode;
					logger::info("Numpad locked to the follower in slot one (mode {})", mode);
				}
				numCycleInit_ = true;
			}
			if (kCycleRowerOarsChest && !kLockNumToFollowerSlot1) {
				// Num5 order: everything together -> the rower alone -> the oars -> the chest -> the player's seat -> everything together.
				int rowerMode = -1;
				{
					const auto humans = BoardedHumans();
					for (size_t i = 0; i < humans.size(); ++i) {
						if (GetHumanSeat(humans[i].get()).slot == 0) { rowerMode = 2 + static_cast<int>(i); break; }
					}
				}
				const bool valid = tuneMode_ == 0 || (tuneMode_ >= 2 && tuneMode_ < 100) || tuneMode_ == kOarRMode || tuneMode_ == kOarLMode || tuneMode_ == kChestModeId || tuneMode_ == kAllModeId;
				if (!numCycleInit_ || !valid) {
					numCycleInit_ = true;
					tuneMode_ = kAllModeId;
				}
				if (a_key == 0x4C) {
					if (tuneMode_ == kAllModeId) tuneMode_ = rowerMode >= 0 ? rowerMode : kOarRMode;
					else if (tuneMode_ >= 2 && tuneMode_ < 100) tuneMode_ = kOarRMode;
					else if (tuneMode_ == kOarRMode || tuneMode_ == kOarLMode) tuneMode_ = kChestModeId;
					else if (tuneMode_ == kChestModeId) tuneMode_ = 0;
					else tuneMode_ = kAllModeId;
					const char* name = tuneMode_ == kAllModeId ? "everything together (rower, oars, chest)" : (tuneMode_ == kChestModeId ? "the chest" : (tuneMode_ == kOarRMode ? "the oars" : (tuneMode_ == 0 ? "the player's seat" : "the rower alone")));
					logger::info("Numpad now tunes {}", name);
					if (tuneMode_ == kAllModeId) AllHud(true);
					else if (tuneMode_ == kChestModeId) ChestHud(true);
					else if (tuneMode_ == kOarRMode) OarHud(true);
					else SeatHud(true);
					return;
				}
			}
			if (kLockNumToPlayerOnly) {
				tuneMode_ = 0;
				if (a_key == 0x4C || a_key == 0x9C) { SeatHud(true); return; }
			}
			if (kLockNumToOarsOnly) {
				if (tuneMode_ != kOarRMode && tuneMode_ != kOarLMode) tuneMode_ = kOarRMode;
				if (a_key == 0x4C) { OarHud(true); return; }
				if (a_key == 0x9C) { oarPage_ = 1 - oarPage_; OarHud(true); return; }
			}
			// For now the numpad is locked to the oars: Num5 no longer switches, Num Enter flips between the two pages.
			constexpr bool kLockNumToOars = true;
			if (kLockNumToOars) {
				// The numpad moves the ripples (default) or the oars. Num5 switches between them, Num Enter flips the page.
				const bool seatMode = tuneMode_ >= 0 && tuneMode_ < 100;  // 0 player, 1 dog, 2 and up = the humans aboard, in boarding order
				if (!seatMode && tuneMode_ != kOarRMode && tuneMode_ != kOarLMode && tuneMode_ != kRippleModeId && tuneMode_ != kChestModeId && tuneMode_ != kAllModeId) tuneMode_ = kAllModeId;
				if (a_key == 0x4C) {  // Num5: chest -> ripples -> oars -> player -> dog -> each human aboard -> chest
					const int lastSeat = 1 + static_cast<int>(BoardedHumans().size());
					if (tuneMode_ == kChestModeId) tuneMode_ = kRippleModeId;
					else if (tuneMode_ == kRippleModeId) tuneMode_ = kOarRMode;
					else if (tuneMode_ == kOarRMode || tuneMode_ == kOarLMode) tuneMode_ = 0;
					else if (tuneMode_ >= lastSeat) tuneMode_ = kChestModeId;
					else ++tuneMode_;
					if (tuneMode_ < 100) { SeatHud(true); return; }
					OarHud(true);
					return;
				}
				if (a_key == 0x9C) {  // Num Enter
					if (tuneMode_ < 100) { SeatHud(true); return; }
					if (tuneMode_ == kAllModeId) { AllHud(true); return; }
					if (tuneMode_ == kChestModeId) { ChestHud(true); return; }
					if (tuneMode_ == kRippleModeId) ripplePage_ = 1 - ripplePage_;
					else oarPage_ = 1 - oarPage_;
					OarHud(true);
					return;
				}
			}
			if (a_key == 0x4C) {  // Num5 switches what the numpad moves
				std::string who;
				if (!kTuneFollowers) {
					// Player -> bench -> right oar -> left oar -> player. No dogs or followers for now.
					if (tuneMode_ == 0) tuneMode_ = kPlankMode;
					else if (tuneMode_ == kPlankMode) tuneMode_ = kOarRMode;
					else if (tuneMode_ == kOarRMode && !oarLink_) tuneMode_ = kOarLMode;
					else tuneMode_ = 0;
					if (tuneMode_ == kPlankMode) who = "the bench (8/2 forward, 4/6 side, 9/3 up, 7/1 turn, +/- scale)";
					else if (tuneMode_ == kOarRMode && oarLink_) who = "BOTH oars, mirrored (4/6 out/in, 8/2 forward, 9/3 up, 7/1 turn, 0/. tilt, * and / roll, +/- scale)";
					else if (tuneMode_ == kOarRMode) who = "the RIGHT oar (8/2 forward, 4/6 side, 9/3 up, 7/1 turn, 0/. tilt, * and / roll, +/- scale)";
					else if (tuneMode_ == kOarLMode) who = "the LEFT oar (8/2 forward, 4/6 side, 9/3 up, 7/1 turn, 0/. tilt, * and / roll, +/- scale)";
					else who = "the player (7/1 turns the body)";
				} else {
					const auto humans = BoardedHumans();
					tuneMode_ = (tuneMode_ + 1) % (2 + static_cast<int>(humans.size()));
					who = "the player";
					if (tuneMode_ == 1) who = "the dog";
					else if (tuneMode_ >= 2) { auto& h = *humans[static_cast<size_t>(tuneMode_ - 2)]; who = std::format("{} ({:08X})", h.GetName(), h.GetFormID()); }
				}
				logger::info("Numpad now tunes {}", who);
				if (auto* console = RE::ConsoleLog::GetSingleton()) console->Print(("Oars and Sails: numpad moves " + who).c_str());
				return;
			}
			if (tuneMode_ == kAllModeId) {
				// One press moves the rower's seat, both oars and the chest by the same step (boat-local, so they stay lined up).
				float dx = 0.0f, dy = 0.0f, dz = 0.0f;
				switch (a_key) {
				case 0x48: dy = 1.0f; break;     // Num8 / Num2 forward / back
				case 0x50: dy = -1.0f; break;
				case 0x4B: dx = -1.0f; break;    // Num4 / Num6 left / right
				case 0x4D: dx = 1.0f; break;
				case 0x49: dz = 1.0f; break;     // Num9 / Num3 up / down
				case 0x51: dz = -1.0f; break;
				case 0x52: dy = 10.0f; break;    // Num0 / Num. forward / back, ten at a time
				case 0x53: dy = -10.0f; break;
				default: break;
				}
				if (dx != 0.0f || dy != 0.0f || dz != 0.0f) {
					auto shift = [&](RE::NiPoint3& p) { p.x += dx; p.y += dy; p.z += dz; };
					HumanSeat* hs = RowerSeat();
					if (hs) shift(hs->pos);
					shift(BoatBow::Chest().offset);
					shift(BoatBow::Oar(0).offset);  // the left oar's x is stored as the negative of the right one, so the same step moves both
					shift(BoatBow::Oar(1).offset);
					allShift_.x += dx; allShift_.y += dy; allShift_.z += dz;
					const auto& o = BoatBow::Oar(0);
					const auto& ch = BoatBow::Chest().offset;
					logger::info("Rig moved ({:+.0f},{:+.0f},{:+.0f}), total right={:.0f} forward={:.0f} up={:.0f} | rower seat right={:.0f} forward={:.0f} up={:.0f} yaw={:.0f}deg | oars right={:.0f} forward={:.0f} up={:.0f} | chest right={:.0f} forward={:.0f} up={:.0f}",
						dx, dy, dz, allShift_.x, allShift_.y, allShift_.z,
						hs ? hs->pos.x : 0.0f, hs ? hs->pos.y : 0.0f, hs ? hs->pos.z : 0.0f, hs ? hs->yaw * 57.2958f : 0.0f,
						o.offset.x, o.offset.y, o.offset.z, ch.x, ch.y, ch.z);
					const auto now = std::chrono::steady_clock::now();
					if (now - oarHudLegend_ > std::chrono::seconds(10)) AllHud(true);
					oarHudDirty_ = true;
				}
				return;
			}
			if (tuneMode_ == kChestModeId) {
				auto& c = BoatBow::Chest();
				bool moved = false;
				switch (a_key) {
				case 0x48: c.offset.y += 1.0f; moved = true; break;       // Num8 / Num2 forward / back
				case 0x50: c.offset.y -= 1.0f; moved = true; break;
				case 0x4B: c.offset.x -= 1.0f; moved = true; break;       // Num4 / Num6 left / right
				case 0x4D: c.offset.x += 1.0f; moved = true; break;
				case 0x49: c.offset.z += 1.0f; moved = true; break;       // Num9 / Num3 up / down
				case 0x51: c.offset.z -= 1.0f; moved = true; break;
				case 0x47: c.yaw += 0.0436f; moved = true; break;         // Num7 / Num1 turn (2.5 degrees)
				case 0x4F: c.yaw -= 0.0436f; moved = true; break;
				case 0x52: c.offset.y += 10.0f; moved = true; break;      // Num0 / Num. forward / back, ten at a time
				case 0x53: c.offset.y -= 10.0f; moved = true; break;
				case 0x4E: c.scale += 0.02f; moved = true; break;         // Num+ / Num- size
				case 0x4A: c.scale = (std::max)(0.05f, c.scale - 0.02f); moved = true; break;
				default: break;
				}
				if (moved) {
					BoatBow::LogChest();
					const auto now = std::chrono::steady_clock::now();
					if (now - oarHudLegend_ > std::chrono::seconds(10)) ChestHud(true);
					oarHudDirty_ = true;
				}
				return;
			}
			if (tuneMode_ == kRippleModeId) {
				auto& rt = BoatBow::Ripples();
				bool moved = false;
				if (ripplePage_ == 0) {
					// Stern trail: where it starts, how wide it is, which way it points, how thick, how big.
					switch (a_key) {
					case 0x48: rt.stern = (std::max)(-300.0f, rt.stern - 2.0f); moved = true; break;           // Num8 / Num2 start closer to / further behind the boat
					case 0x50: rt.stern += 2.0f; moved = true; break;
					case 0x4B: rt.lateral = (std::max)(0.0f, rt.lateral - 1.0f); moved = true; break;         // Num4 / Num6 narrower / wider
					case 0x4D: rt.lateral += 1.0f; moved = true; break;
					case 0x49: rt.lift += 1.0f; moved = true; break;                                           // Num9 / Num3 up / down
					case 0x51: rt.lift -= 1.0f; moved = true; break;
					case 0x47: rt.yaw += 0.0436f; moved = true; break;                                         // Num7 / Num1 swing the trail's start round the boat
					case 0x4F: rt.yaw -= 0.0436f; moved = true; break;
					case 0x52: rt.space = (std::max)(8.0f, rt.space - 2.0f); moved = true; break;            // Num0 / Num. denser / sparser
					case 0x53: rt.space += 2.0f; moved = true; break;
					case 0x4E: rt.wake += 0.005f; moved = true; break;                                         // Num+ / Num- size
					case 0x4A: rt.wake = (std::max)(0.005f, rt.wake - 0.005f); moved = true; break;
					default: break;
					}
				} else {
					// Oar rings: where on the blade they appear and how big.
					switch (a_key) {
					case 0x48: rt.oarReach += 2.0f; moved = true; break;                                       // Num8 / Num2 out toward the tip / in toward the oarlock
					case 0x50: rt.oarReach = (std::max)(0.0f, rt.oarReach - 2.0f); moved = true; break;
					case 0x4D: rt.oarSide += 1.0f; moved = true; break;                                        // Num6 / Num4 sideways across the shaft
					case 0x4B: rt.oarSide -= 1.0f; moved = true; break;
					case 0x49: rt.oarLift += 1.0f; moved = true; break;                                        // Num9 / Num3 up / down
					case 0x51: rt.oarLift -= 1.0f; moved = true; break;
					case 0x4E: rt.oar += 0.005f; moved = true; break;                                          // Num+ / Num- size
					case 0x4A: rt.oar = (std::max)(0.005f, rt.oar - 0.005f); moved = true; break;
					default: break;
					}
				}
				if (moved) {
					logger::info("Ripple tuning: wake={:.3f} oar={:.3f} stern={:.0f} space={:.0f} spread={:.0f} lift={:.0f} yaw={:.1f}deg oarReach={:.0f} oarSide={:.0f} oarLift={:.0f}",
						rt.wake, rt.oar, rt.stern, rt.space, rt.lateral, rt.lift, rt.yaw * 57.2958f, rt.oarReach, rt.oarSide, rt.oarLift);
					const auto now = std::chrono::steady_clock::now();
					if (now - oarHudLegend_ > std::chrono::seconds(10)) RippleHud(true);
					oarHudDirty_ = true;
				}
				return;
			}
			if (tuneMode_ == kOarRMode || tuneMode_ == kOarLMode) {
				// Linked (default): one set of keys moves both oars. Out/in, turn and roll are mirrored, so the other
				// oar goes the opposite way; forward/back, up/down, tilt and scale are the same for both.
				// Unlinked (`oas "oar link"`): Num5 picks the right or the left oar and only that one moves.
				const bool link = oarLink_;
				auto& a = BoatBow::Oar(tuneMode_ == kOarLMode && !link ? 1 : 0);
				auto& b = BoatBow::Oar(1);
				bool moved = false;
				auto both = [&](auto&& a_field, float a_d) { a_field(a) += a_d; if (link) a_field(b) += a_d; };
				auto mirrored = [&](auto&& a_field, float a_d) { a_field(a) += a_d; if (link) a_field(b) -= a_d; };
				auto fx = [](BoatBow::OarTune& o) -> float& { return o.offset.x; };
				auto fy = [](BoatBow::OarTune& o) -> float& { return o.offset.y; };
				auto fz = [](BoatBow::OarTune& o) -> float& { return o.offset.z; };
				auto fyaw = [](BoatBow::OarTune& o) -> float& { return o.yaw; };
				auto fpitch = [](BoatBow::OarTune& o) -> float& { return o.pitch; };
				auto froll = [](BoatBow::OarTune& o) -> float& { return o.roll; };
				auto fscale = [](BoatBow::OarTune& o) -> float& { return o.scale; };
				auto& st = BoatBow::Stroke();
				if (a_key == 0x4E || a_key == 0x4A) {
					// Num+ / Num- = oar size on both pages (both oars): 0.02 per press.
					both(fscale, a_key == 0x4E ? 0.02f : -0.02f);
					moved = true;
				} else if (a_key == 0x37 || a_key == 0xB5) {
					// Num* / Num/ = stroke speed on both pages: 0.5 percent per press.
					st.rate = std::clamp(st.rate + (a_key == 0x37 ? 0.005f : -0.005f), 0.5f, 2.0f);
					moved = true;
				} else if (oarPage_ == 0) {
					// Page 1, POSITION: where the oarlocks are, and the oar size.
					switch (a_key) {
					case 0x48: both(fy, 1.0f); moved = true; break;           // Num8 / Num2 forward / back
					case 0x50: both(fy, -1.0f); moved = true; break;
					case 0x4B: mirrored(fx, -1.0f); moved = true; break;      // Num4 / Num6 in / out (the other oar goes the opposite way)
					case 0x4D: mirrored(fx, 1.0f); moved = true; break;
					case 0x49: both(fz, 1.0f); moved = true; break;           // Num9 / Num3 up / down
					case 0x51: both(fz, -1.0f); moved = true; break;
					case 0x47: st.dip = (std::max)(0.0f, st.dip + 0.0262f); moved = true; break;      // Num7 / Num1 dip deeper / shallower (also on page 2)
					case 0x4F: st.dip = (std::max)(0.0f, st.dip - 0.0262f); moved = true; break;
					case 0x52: st.swing = (std::max)(0.0f, st.swing + 0.0262f); moved = true; break;  // Num0 / Num. sweep more / less (also on page 2)
					case 0x53: st.swing = (std::max)(0.0f, st.swing - 0.0262f); moved = true; break;
					default: break;
					}
				} else {
					// Page 2, ANGLE and STROKE: how the oars sit, and how they row.
					switch (a_key) {
					case 0x48: both(fpitch, 0.0436f); moved = true; break;    // Num8 / Num2 blade up / down (2.5 degrees)
					case 0x50: both(fpitch, -0.0436f); moved = true; break;
					case 0x4D: mirrored(fyaw, 0.0436f); moved = true; break;  // Num6 / Num4 turn the blades aft / forward
					case 0x4B: mirrored(fyaw, -0.0436f); moved = true; break;
					case 0x49: mirrored(froll, 0.0436f); moved = true; break; // Num9 / Num3 roll about the shaft
					case 0x51: mirrored(froll, -0.0436f); moved = true; break;
					case 0x47: st.dip = (std::max)(0.0f, st.dip + 0.0262f); moved = true; break;      // Num7 / Num1 dip deeper / shallower (1.5 degrees)
					case 0x4F: st.dip = (std::max)(0.0f, st.dip - 0.0262f); moved = true; break;
					case 0x52: st.swing = (std::max)(0.0f, st.swing + 0.0262f); moved = true; break;  // Num0 / Num. sweep more / less
					case 0x53: st.swing = (std::max)(0.0f, st.swing - 0.0262f); moved = true; break;
					default: break;
					}
				}
				if (moved) {
					a.scale = (std::max)(0.05f, a.scale);
					if (link) b.scale = (std::max)(0.05f, b.scale);
					BoatBow::LogOars();
					const auto now = std::chrono::steady_clock::now();
					if (now - oarHudLegend_ > std::chrono::seconds(10)) OarHud(true);  // idle for a while: remind what the keys do
					oarHudDirty_ = true;
				}
				return;
			}
			if (tuneMode_ == kPlankMode) {
				auto& pl = BoatBow::Plank();
				bool moved = false;
				switch (a_key) {
				case 0x48: pl.offset.y += 1.0f; moved = true; break;
				case 0x50: pl.offset.y -= 1.0f; moved = true; break;
				case 0x4B: pl.offset.x -= 1.0f; moved = true; break;
				case 0x4D: pl.offset.x += 1.0f; moved = true; break;
				case 0x49: pl.offset.z += 1.0f; moved = true; break;
				case 0x51: pl.offset.z -= 1.0f; moved = true; break;
				case 0x47: pl.yaw += 0.0436f; moved = true; break;
				case 0x4F: pl.yaw -= 0.0436f; moved = true; break;
				case 0x4E: pl.scale += 0.02f; moved = true; break;
				case 0x4A: pl.scale = (std::max)(0.05f, pl.scale - 0.02f); moved = true; break;
				default: break;
				}
				if (moved) BoatBow::LogPlank();
				return;
			}
			if (tuneMode_ == 1) { TuneDogSlot(a_key); return; }
			if (tuneMode_ == 0 && !playerTilt_) {
				// No skeleton override: the whole actor is moved instead - forward/back (8/2), left/right (4/6), up/down (9/3), turn (7/1).
				bool changedBody = false;
				switch (a_key) {
				case 0x48: playerBodyFwd_ += 2.0f; changedBody = true; break;
				case 0x50: playerBodyFwd_ -= 2.0f; changedBody = true; break;
				case 0x4D: playerBodySide_ += 2.0f; changedBody = true; break;
				case 0x4B: playerBodySide_ -= 2.0f; changedBody = true; break;
				case 0x49: playerBodyLift_ += 1.0f; changedBody = true; break;
				case 0x51: playerBodyLift_ -= 1.0f; changedBody = true; break;
				case 0x47: playerSeatYaw_ += 0.0436f; changedBody = true; break;
				case 0x4F: playerSeatYaw_ -= 0.0436f; changedBody = true; break;
				default: break;
				}
				if (changedBody) {
					logger::info("Player body tuning fwd={:.0f} side={:.0f} lift={:.0f} yaw={:.0f}deg", playerBodyFwd_, playerBodySide_, playerBodyLift_, playerSeatYaw_ * 57.2958f);
					SeatHud(false);
				}
				return;
			}
			const auto humans = BoardedHumans();
			if (tuneMode_ >= 2 && static_cast<size_t>(tuneMode_ - 2) >= humans.size()) tuneMode_ = 0;
			HumanSeat* hs = tuneMode_ >= 2 ? &GetHumanSeat(humans[static_cast<size_t>(tuneMode_ - 2)].get()) : nullptr;
			RE::NiPoint3& seat = hs ? hs->pos : playerSeat_;
			float& yaw = hs ? hs->yaw : playerSeatYaw_;
			bool changed = false;
			switch (a_key) {
			case 0x48: seat.y += 1.0f; changed = true; break;
			case 0x50: seat.y -= 1.0f; changed = true; break;
			case 0x4B: seat.x -= 1.0f; changed = true; break;
			case 0x4D: seat.x += 1.0f; changed = true; break;
			case 0x49: seat.z += 1.0f; changed = true; break;
			case 0x51: seat.z -= 1.0f; changed = true; break;
			case 0x47: yaw += 0.0436f; changed = true; break;  // Num7 (+5 deg)
			case 0x4F: yaw -= 0.0436f; changed = true; break;  // Num1 (-5 deg)
			default: break;
			}
			if (changed) {
				logger::info("{} seat tuning right={:.0f} forward={:.0f} up={:.0f} yaw={:.0f}deg", hs ? std::format("Human {:08X}", hs->id) : std::string("Player"), seat.x, seat.y, seat.z, yaw * 57.2958f);
				SeatHud(false);
			}
		}

		// Crew numpad (v1.0.9): everyone aboard, humans in seat order then dogs in bow order. Num5 moves to the next one.
		struct CrewTarget { RE::NiPointer<RE::Actor> actor; bool dog; int slot; };
		std::vector<CrewTarget> CrewTargets()
		{
			std::vector<CrewTarget> humans, dogs;
			for (auto& h : passengers_) {
				auto a = h.get();
				if (!a || a->IsDead()) continue;
				if (IsDogActor(a.get())) dogs.push_back({ a, true, static_cast<int>(dogs.size()) });
				else humans.push_back({ a, false, GetHumanSeat(a.get()).slot });
			}
			std::stable_sort(humans.begin(), humans.end(), [](const CrewTarget& x, const CrewTarget& y) { return x.slot < y.slot; });
			humans.insert(humans.end(), dogs.begin(), dogs.end());
			return humans;
		}

		void CrewHud(const CrewTarget& a_t, bool a_title)
		{
			const std::string who = std::format("{} ({} slot {})", a_t.actor->GetName(), a_t.dog ? "dog bow" : "human", a_t.slot);
			if (a_title) {
				oarHudLegend_ = std::chrono::steady_clock::now();
				RE::SendHUDMessage::ShowHUDMessage(std::format("SEAT: {}. 8/2 forward-back, 4/6 left-right, 9/3 up-down, 7/1 turn. Num5 = next aboard", who).c_str());
			}
			if (a_t.dog) {
				{ const RE::NiPoint3 dl = DogLocal(a_t.slot); RE::SendHUDMessage::ShowHUDMessage(std::format("{}: right {:.0f}  forward {:.0f}  up {:.0f}  turn {:.0f} deg", who, dl.x, dl.y, dl.z, DogYaw(a_t.slot) * 57.2958f).c_str()); }
			} else {
				const auto& hs = GetHumanSeat(a_t.actor.get());
				RE::SendHUDMessage::ShowHUDMessage(std::format("{}: right {:.0f}  forward {:.0f}  up {:.0f}  turn {:.0f} deg", who, hs.pos.x, hs.pos.y, hs.pos.z, hs.yaw * 57.2958f).c_str());
			}
		}

		void TuneCrew(std::uint32_t a_key)
		{
			lastCrewTune_ = std::chrono::steady_clock::now();  // no seated idles while the seats are being tuned
			const auto list = CrewTargets();
			// v1.0.14 (14.7): after the crew, Num5 reaches the helm bench (Plank tuning) and the rower's bench (RowBench tuning).
			// 50: then the stowed oars, then back to the first. 56.26: only our boat's things (the NPC lake boats are gone from
			// the cycle); a target left on them by an older build starts again at the first crew member.
			if (crewTargetId_ == kLakeRigTarget) {
				crewTargetId_ = 0;
				logger::info("Numpad: target was the NPC lake boats (older build), back to the first aboard");
			}
			const std::size_t total = list.size() + 3;
			std::size_t idx = 0;
			bool found = false;
			for (std::size_t i = 0; i < list.size(); ++i) {
				if (list[i].actor->GetFormID() == crewTargetId_) { idx = i; found = true; break; }
			}
			if (crewTargetId_ == kHelmBenchTarget) { idx = list.size(); found = true; }
			if (crewTargetId_ == kRowBenchTarget) { idx = list.size() + 1; found = true; }
			if (crewTargetId_ == kOarStowTarget) { idx = list.size() + 2; found = true; }
			auto announce = [&](std::size_t a_i) {
				BoatBow::SetOarStowPreview(a_i == list.size() + 2);
				if (a_i == list.size() + 2) {
					crewTargetId_ = kOarStowTarget;
					logger::info("Numpad now tunes the stowed oars");
					OarStowHud(true);
					return;
				}
				if (a_i >= list.size()) {
					const bool row = a_i == list.size() + 1;
					crewTargetId_ = row ? kRowBenchTarget : kHelmBenchTarget;
					logger::info("Numpad now tunes the {}", row ? "rower's bench" : "helm bench");
					BenchHud(row, true);
					return;
				}
				const auto& t = list[a_i];
				crewTargetId_ = t.actor->GetFormID();
				logger::info("Numpad now tunes {} ({} slot {})", t.actor->GetName(), t.dog ? "dog bow" : "human", t.slot);
				CrewHud(t, true);
			};
			if (a_key == 0x4C) {  // Num5: next (one physical press repeats for a few frames: ignore repeats)
				static auto lastCrewFlip = std::chrono::steady_clock::time_point{};
				const auto nowKey = std::chrono::steady_clock::now();
				if (nowKey - lastCrewFlip < std::chrono::milliseconds(300)) return;
				lastCrewFlip = nowKey;
				announce(found ? (idx + 1) % total : 0);
				return;
			}
			if (!found) { idx = 0; announce(0); }  // starts on the first human aboard (or the helm bench with nobody aboard)
			if (idx == list.size() + 2) { TuneOarStow(a_key); return; }
			if (idx >= list.size()) { TuneBench(idx == list.size() + 1, a_key); return; }
			const auto& t = list[idx];
			if (t.dog) { TuneDogSlot(a_key); return; }
			auto& hs = GetHumanSeat(t.actor.get());
			bool changed = true;
			switch (a_key) {
			case 0x48: hs.pos.y += 1.0f; break;  // Num8
			case 0x50: hs.pos.y -= 1.0f; break;  // Num2
			case 0x4B: hs.pos.x -= 1.0f; break;  // Num4
			case 0x4D: hs.pos.x += 1.0f; break;  // Num6
			case 0x49: hs.pos.z += 1.0f; break;  // Num9
			case 0x51: hs.pos.z -= 1.0f; break;  // Num3
			case 0x47: hs.yaw += 0.0436f; break;  // Num7 (+5 deg)
			case 0x4F: hs.yaw -= 0.0436f; break;  // Num1 (-5 deg)
			default: changed = false; break;
			}
			if (changed) {
				if (hs.slot >= 0 && hs.slot < kSeatCount) { slotPos_[hs.slot] = hs.pos; slotYaw_[hs.slot] = hs.yaw; }  // the seat keeps it, whoever sits there next
				logger::info("Human {:08X} {} seat tuning slot {}: right={:.0f} forward={:.0f} up={:.0f} yaw={:.0f}deg",
					hs.id, t.actor->GetName(), hs.slot, hs.pos.x, hs.pos.y, hs.pos.z, hs.yaw * 57.2958f);
				CrewHud(t, false);
			}
		}

		static constexpr RE::FormID kHelmBenchTarget = 0xFFFFFFF1;  // crewTargetId_ values for the two benches
		static constexpr RE::FormID kRowBenchTarget = 0xFFFFFFF2;
		static constexpr RE::FormID kOarStowTarget = 0xFFFFFFF3;  // 50: the stowed oars (parked pose)
		static constexpr RE::FormID kLakeRigTarget = 0xFFFFFFF4;  // 54: the NPC lake boats; 56.26: no longer a target, only reset away from

		void BenchHud(bool a_row, bool a_title)
		{
			const auto& b = a_row ? BoatBow::RowBench() : BoatBow::Plank();
			const char* who = a_row ? "ROWER'S BENCH" : "HELM BENCH";
			if (a_title) {
				oarHudLegend_ = std::chrono::steady_clock::now();
				RE::SendHUDMessage::ShowHUDMessage(std::format("{}: 8/2 forward-back, 4/6 left-right, 9/3 up-down, 7/1 turn, +/- size. Num5 = next", who).c_str());
			}
			RE::SendHUDMessage::ShowHUDMessage(std::format("{}: right {:.0f}  forward {:.0f}  up {:.0f}  turn {:.0f} deg  size {:.2f}", who, b.offset.x, b.offset.y, b.offset.z, b.yaw * 57.2958f, b.scale).c_str());
		}

		// 50: where the oars lie while the boat is parked. Linked like the rowing oars: in-out, turn and roll mirrored.
		void OarStowHud(bool a_title)
		{
			const auto& o = BoatBow::OarStow(0);
			if (a_title) {
				oarHudLegend_ = std::chrono::steady_clock::now();
				RE::SendHUDMessage::ShowHUDMessage("STOWED OARS: 8/2 forward-back, 4/6 in-out, 9/3 up-down, 7/1 turn, 0/. roll, * and / tilt. Num5 = next");
			}
			RE::SendHUDMessage::ShowHUDMessage(std::format("Stowed oars: right {:.0f}  forward {:.0f}  up {:.0f}  turn {:.1f} deg  roll {:.1f} deg  tilt {:.1f} deg",
				o.offset.x, o.offset.y, o.offset.z, o.yaw * 57.2958f, o.roll * 57.2958f, o.pitch * 57.2958f).c_str());
		}

		void TuneOarStow(std::uint32_t a_key)
		{
			auto& r = BoatBow::OarStow(0);
			auto& l = BoatBow::OarStow(1);
			bool moved = true;
			switch (a_key) {
			case 0x48: r.offset.y += 1.0f; l.offset.y += 1.0f; break;  // Num8 / Num2 forward / back
			case 0x50: r.offset.y -= 1.0f; l.offset.y -= 1.0f; break;
			case 0x4B: r.offset.x -= 1.0f; l.offset.x += 1.0f; break;  // Num4 / Num6 in / out
			case 0x4D: r.offset.x += 1.0f; l.offset.x -= 1.0f; break;
			case 0x49: r.offset.z += 1.0f; l.offset.z += 1.0f; break;  // Num9 / Num3 up / down
			case 0x51: r.offset.z -= 1.0f; l.offset.z -= 1.0f; break;
			case 0x47: r.yaw += 0.0436f; l.yaw -= 0.0436f; break;       // Num7 / Num1 turn
			case 0x4F: r.yaw -= 0.0436f; l.yaw += 0.0436f; break;
			case 0x52: r.roll += 0.0436f; l.roll -= 0.0436f; break;     // Num0 / Num. roll about the shaft
			case 0x53: r.roll -= 0.0436f; l.roll += 0.0436f; break;
			case 0x37: r.pitch += 0.0436f; l.pitch += 0.0436f; break;   // Num* / Num/ tilt
			case 0xB5: r.pitch -= 0.0436f; l.pitch -= 0.0436f; break;
			default: moved = false; break;
			}
			BoatBow::SetOarStowPreview(true);  // keeps the preview on while keys are pressed
			if (!moved) return;
			BoatBow::LogOars();
			OarStowHud(false);
		}

		// The helm bench (Plank tuning, the old mode 100) and the rower's bench (RowBench tuning): same keys and steps.
		void TuneBench(bool a_row, std::uint32_t a_key)
		{
			auto& b = a_row ? BoatBow::RowBench() : BoatBow::Plank();
			bool moved = true;
			switch (a_key) {
			case 0x48: b.offset.y += 1.0f; break;  // Num8
			case 0x50: b.offset.y -= 1.0f; break;  // Num2
			case 0x4B: b.offset.x -= 1.0f; break;  // Num4
			case 0x4D: b.offset.x += 1.0f; break;  // Num6
			case 0x49: b.offset.z += 1.0f; break;  // Num9
			case 0x51: b.offset.z -= 1.0f; break;  // Num3
			case 0x47: b.yaw += 0.0436f; break;    // Num7
			case 0x4F: b.yaw -= 0.0436f; break;    // Num1
			case 0x4E: b.scale += 0.02f; break;    // Num+
			case 0x4A: b.scale = (std::max)(0.05f, b.scale - 0.02f); break;  // Num-
			default: moved = false; break;
			}
			if (!moved) return;
			if (a_row) BoatBow::LogRowBench(); else BoatBow::LogPlank();
			BenchHud(a_row, false);
		}

		void TuneDogSlot(std::uint32_t a_key)
		{
			bool changed = false;
			switch (a_key) {
			case 0x48: bowSlot_.y += 1.0f; changed = true; break;  // Num8
			case 0x50: bowSlot_.y -= 1.0f; changed = true; break;  // Num2
			case 0x4B: bowSlot_.x -= 1.0f; changed = true; break;  // Num4
			case 0x4D: bowSlot_.x += 1.0f; changed = true; break;  // Num6
			case 0x49: bowSlot_.z += 1.0f; changed = true; break;  // Num9
			case 0x51: bowSlot_.z -= 1.0f; changed = true; break;  // Num3
			case 0x47: dogYaw_ += 0.0436f; changed = true; break;  // Num7
			case 0x4F: dogYaw_ -= 0.0436f; changed = true; break;  // Num1
			default: break;
			}
			if (changed) {
				logger::info("Passengers: dog slot tuning right={:.0f} forward={:.0f} up={:.0f} yaw={:.0f}deg", bowSlot_.x, bowSlot_.y, bowSlot_.z, dogYaw_ * 57.2958f);
				SeatHud(false);
			}
		}

		void SetBowSlot(float a_x, float a_y, float a_z)
		{
			bowSlot_ = RE::NiPoint3{ a_x, a_y, a_z };
			logger::info("Passengers: bow slot now ({:.0f}, {:.0f}, {:.0f})", a_x, a_y, a_z);
		}

		// The player has two seats: the stern (a follower rows) and the rowing seat (nobody else to row). The active one lives in
		// playerBodyFwd_/Side_/Lift_/SeatYaw_ so the numpad tunes it; the other is parked here and swapped in at boarding.
		struct PlayerSeatSet { float fwd, side, lift, yaw; };
		PlayerSeatSet parkedPlayerSeat_{ 263.0f, 0.0f, 15.0f, 3.14159265f };  // the rowing seat, in line with the rower seat and oars (56.11: 180)
		bool playerOnRowSeat_{ false };
		void SelectPlayerSeat(bool a_rowing)
		{
			if (a_rowing == playerOnRowSeat_) return;
			const PlayerSeatSet now{ playerBodyFwd_, playerBodySide_, playerBodyLift_, playerSeatYaw_ };
			playerBodyFwd_ = parkedPlayerSeat_.fwd; playerBodySide_ = parkedPlayerSeat_.side; playerBodyLift_ = parkedPlayerSeat_.lift; playerSeatYaw_ = parkedPlayerSeat_.yaw;
			parkedPlayerSeat_ = now;
			playerOnRowSeat_ = a_rowing;
			logger::info("Player seat: {} (fwd {:.0f} side {:.0f} up {:.0f} turn {:.0f} deg)", a_rowing ? "rowing seat, nobody else to row" : "stern, a follower rows", playerBodyFwd_, playerBodySide_, playerBodyLift_, playerSeatYaw_ * 57.2958f);
		}

		// May this actor ride? Teammates (and Meeko, and dogs among them), alive, not aboard yet, and a free seat for humans.
		// a_log writes the candidate line (and, for humans, the one-off animation dump) the first boarding pass always wrote.
		bool CanBoardActor(RE::Actor* a_actor, const RE::NiPoint3& a_playerPos, bool a_log, bool& a_isDog)
		{
			if (!a_actor || a_actor->IsDead()) return false;
			for (auto& h : passengers_) { if (auto p = h.get(); p && p.get() == a_actor) return false; }
			const auto* race = a_actor->GetRace();
			const std::string raceId = race && race->GetFormEditorID() ? race->GetFormEditorID() : "";
			// Follower frameworks (Nefaram etc.) may not set the teammate flag on every companion.
			// Dump what each nearby actor looks like so the log shows how they are marked.
			if (a_log && (raceId.find("Dog") != std::string::npos || a_actor->IsPlayerTeammate())) {
				std::string factions;
				a_actor->VisitFactions([&](RE::TESFaction* a_faction, std::int8_t a_rank) {
					if (a_faction) {
						const char* id = a_faction->GetFormEditorID();
						factions += std::format(" {:08X}:{}:{}", a_faction->GetFormID(), id ? id : "?", static_cast<int>(a_rank));
					}
					return false;
				});
				const auto* baseObj = a_actor->GetActorBase();
				logger::info("Passengers: candidate {:08X} base={:08X} race='{}' teammate={} dist={:.0f} factions:{}",
					a_actor->GetFormID(), baseObj ? baseObj->GetFormID() : 0u, raceId, a_actor->IsPlayerTeammate(),
					a_actor->GetPosition().GetDistance(a_playerPos), factions);
			}
			// Meeko himself is always welcome; everyone else needs the teammate flag.
			if (!a_actor->IsPlayerTeammate() && !IsMeeko(a_actor) && !IsCrew(a_actor)) return false;
			a_isDog = raceId.find("Dog") != std::string::npos;
			if (!a_isDog) {
				int humans = 0;
				for (auto& h : passengers_) { auto a = h.get(); if (a && !IsDogActor(a.get())) ++humans; }
				if (humans >= 6) { logger::info("Passengers: no free seat for {:08X}", a_actor->GetFormID()); return false; }
				// One-off: write this follower's animation events to the log so the sit event can be read.
				if (a_log) DumpActorAnimation(a_actor);
			}
			return true;
		}

		static bool IsMeeko(RE::Actor* a_actor)
		{
			const auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
			return base && base->GetFormID() == 0x000D95E9u;
		}

		// Makes the actor a passenger: collision off, held still, swim state cleared, dogs get the sit watcher.
		void SetupPassenger(RE::Actor* a_actor, bool a_isDog)
		{
			a_actor->SetCollision(false);
			SetActorDontMove(a_actor, true);
			ClearSwimState(a_actor, "passenger");
			passengers_.push_back(a_actor->GetHandle());
			if (!a_isDog) {
				CrewIdleState st;
				st.id = a_actor->GetFormID();
				st.boardedAt = rideTime_;
				st.idleTimer = 0.0f;  // v1.0.14: the shared scheduler staggers them (14.9); nobody has had one yet
				std::lock_guard lock(crewIdleMx_);
				if (!CrewIdleFor(st.id)) crewIdle_.push_back(std::move(st));
			}
			a_actor->AddAnimationGraphEventSink(&passengerSink_);  // humans too since v1.0.11 (PickNewIdle answer, stand-up log)
			humanSitTimer_ = 0.0f;
			const auto* race = a_actor->GetRace();
			logger::info("Passengers: boarded {:08X} (race '{}') in bow slot {}", a_actor->GetFormID(), race && race->GetFormEditorID() ? race->GetFormEditorID() : "", passengers_.size() - 1);
		}

		bool TryBoardActor(RE::Actor* a_actor, const RE::NiPoint3& a_playerPos, bool a_log)
		{
			bool isDog = false;
			if (!CanBoardActor(a_actor, a_playerPos, a_log, isDog)) return false;
			SetupPassenger(a_actor, isDog);
			return true;
		}

		void BoardPassengers(RE::PlayerCharacter* a_player)
		{
			// B11: boarding again within the release steps: finish them at once (collision on, free to move) first.
			for (auto& r : releases_) {
				if (auto a = r.actor.get()) { a->SetCollision(true); SetActorDontMove(a.get(), false); }
			}
			releases_.clear();
			passengers_.clear();
			ClearCrewIdle();
			rideTime_ = 0.0f;
			animEvLogs_ = 0;
			idleSchedTimer_ = CrewRand(8.0f, 20.0f);
			lastIdleActor_ = 0;
			lastRowStopAt_ = -100.0f;
			landingChecks_ = { -1.0f, -1.0f };
			rowAnimFallback_.clear();  // Part 16: the fallback is decided again every boarding
			camNodesLogged_ = false;
			camHookLogged_ = false;
			camClampLogged_ = false;
			camBaseValid_ = false;
			camStateLogged_ = false;
			camForceLogged_ = false;
			camSlot3Logged_ = false;
			camStateFwdValid_ = false;
			tpsPrevRowAnim_ = false;
			tpsBurst_ = 0;
			tpsLogLines_ = 0;
			animObjHoldUntil_ = animObjHoldCap_ = -1.0f;
			camBoneLogAt_.clear();
			ClearStrayWatch();  // 17.2
			rowedThisRide_.clear();
			rowStartPending_ = false;
			rowMenuHold_ = 0.0f;
			playerSitAt_ = -100.0f;
			passengerTimer_ = 0.0f;
			CancelHop();
			lateDwell_.clear();
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists || !a_player) return;
			const auto playerPos = a_player->GetPosition();
			for (auto& handle : lists->highActorHandles) {
				auto actor = handle.get();
				if (!actor || actor.get() == a_player || actor->IsDead()) continue;
				if (actor->GetPosition().GetDistance(playerPos) > 3000.0f) continue;
				TryBoardActor(actor.get(), playerPos, true);
			}
		}

		// Where a passenger sits, in the world: the same placement UpdatePassengers uses.
		RE::NiPoint3 SeatWorld(RE::Actor* a_actor, bool a_isDog, const RE::NiPoint3& a_boatPos, float& a_yaw)
		{
			int dogs = 0;
			for (auto& h : passengers_) { auto a = h.get(); if (a && IsDogActor(a.get())) ++dogs; }
			const float h = skimHeading_;
			const RE::NiPoint3 right{ std::cos(h), -std::sin(h), 0.0f };
			const RE::NiPoint3 fwd{ std::sin(h), std::cos(h), 0.0f };
			const RE::NiPoint3 local = a_isDog ? DogLocal(dogs) : GetHumanSeat(a_actor).pos;  // 56.29: per-dog seat
			RE::NiPoint3 world{
				a_boatPos.x + right.x * local.x + fwd.x * local.y,
				a_boatPos.y + right.y * local.x + fwd.y * local.y,
				a_boatPos.z + boardRaise_ - skimSink_ + boatHeight_ + local.z };
			a_yaw = h;
			RE::NiPoint3 boatFwd{}, boatRight{};
			if (BoatBow::RigidPoint(local, world, boatFwd, boatRight)) a_yaw = std::atan2(boatFwd.x, boatFwd.y);
			a_yaw += a_isDog ? dogYaw_ : GetHumanSeat(a_actor).yaw;
			return world;
		}

		void CancelHop()
		{
			if (!hopActive_) return;
			hopActive_ = false;
			if (auto a = hopActor_.get()) {
				a->SetCollision(true);
				SetActorDontMove(a.get(), false);
			}
		}

		// Late boarding (v1.0.6): a follower who reaches the boat after the player boarded hops in by themselves, while the
		// boat is slow and calm. Checked four times a second; one hop at a time; nobody is teleported.
		void UpdateLateBoarding(const RE::NiPoint3& a_boatPos, float a_dt)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			if (hopActive_) {
				auto actor = hopActor_.get();
				if (!actor || actor->IsDead()) { hopActive_ = false; return; }
				if (auto* hcell = actor->GetParentCell(); !hcell || !hcell->IsAttached() || !actor->Is3DLoaded()) return;
				hopT_ = (std::min)(1.0f, hopT_ + a_dt / kLateBoardTime);
				const float e = hopT_ * hopT_ * (3.0f - 2.0f * hopT_);
				float yaw = 0.0f;
				const RE::NiPoint3 seat = SeatWorld(actor.get(), hopDog_, a_boatPos, yaw);
				RE::NiPoint3 pos = hopFrom_ + (seat - hopFrom_) * e;
				pos.z += kLateBoardArc * std::sin(3.14159f * hopT_);
				++glueMoves_;
				actor->SetPosition(pos, true);
				RE::NiPoint3 ang = actor->GetAngle();
				ang.x = 0.0f;
				ang.z = yaw;
				actor->SetAngle(ang);
				if (hopT_ >= 1.0f) {
					hopActive_ = false;
					SetupPassenger(actor.get(), hopDog_);
					BoatBow::Impact(0.12f);
					BoatBow::RippleAt(RE::NiPoint3{ seat.x, seat.y, skimWaterHeight_ }, 0.05f);
					if (!hopDog_) {
						const int slot = GetHumanSeat(actor.get()).slot;
						logger::info("Late boarding: {:08X} '{}' is aboard in seat {}", actor->GetFormID(), actor->GetName(), slot);
					} else {
						logger::info("Late boarding: {:08X} '{}' is aboard in the bow", actor->GetFormID(), actor->GetName());
					}
				}
				return;
			}
			const bool calm = skimSpeed_ < kLateBoardMaxSpeed && !skimAirborne_ && !skimOnRamp_ && !contactSuspended_;
			if (!calm) { lateDwell_.clear(); lateTimer_ = 0.0f; return; }
			if ((lateTimer_ += a_dt) < kLateBoardCheck) return;
			const float step = lateTimer_;
			lateTimer_ = 0.0f;
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) return;
			float cx = 0.0f, cy = 0.0f;
			BoatBow::BoatCenterFromVehicle(vehiclePos_.x, vehiclePos_.y, skimHeading_, cx, cy);
			auto* ui = RE::UI::GetSingleton();
			const bool talking = ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
			std::vector<std::pair<RE::FormID, float>> kept;
			for (auto& handle : lists->highActorHandles) {
				auto actor = handle.get();
				if (!actor || actor.get() == player || actor->IsDead()) continue;
				if (!actor->IsPlayerTeammate() && !IsMeeko(actor.get()) && !IsCrew(actor.get())) continue;
				bool aboard = false;
				for (auto& h : passengers_) { if (auto p = h.get(); p && p.get() == actor.get()) { aboard = true; break; } }
				if (aboard) continue;
				const RE::NiPoint3 ap = actor->GetPosition();
				const float dx = ap.x - cx, dy = ap.y - cy;
				if (dx * dx + dy * dy > kLateBoardRange * kLateBoardRange) continue;
				const auto* st = actor->AsActorState();
				if (talking || actor->IsInCombat() || actor->GetCurrentScene() || actor->IsOnMount() || (st && st->IsBleedingOut())) continue;  // busy: retried later
				float dwell = step;
				for (auto& [id, t] : lateDwell_) if (id == actor->GetFormID()) dwell += t;
				if (dwell >= kLateBoardDwell) {
					bool isDog = false;
					if (CanBoardActor(actor.get(), player->GetPosition(), false, isDog)) {
						if (!isDog && playerOnRowSeat_) AssignLateSeat(actor.get());  // the player rows: keep the rowing seat theirs
						hopActive_ = true;
						hopActor_ = actor->GetHandle();
						hopDog_ = isDog;
						hopT_ = 0.0f;
						hopFrom_ = ap;
						actor->SetCollision(false);
						SetActorDontMove(actor.get(), true);
						logger::info("Late boarding: {:08X} '{}' arrived, hopping in", actor->GetFormID(), actor->GetName());
						lateDwell_.clear();
						return;
					}
					continue;
				}
				kept.emplace_back(actor->GetFormID(), dwell);
			}
			lateDwell_ = std::move(kept);
		}

		// Cancels a leftover swimming state (from being dropped in the water) so the actor can sit/crouch.
		void ClearSwimState(RE::Actor* a_actor, const char* a_who)
		{
			if (!a_actor) return;
			for (const char* ev : { "swimStop", "SwimStop", "swimEnd" }) {
				const bool ok = CrewNotify(a_actor, ev, "clear swim", false);
				logger::info("ClearSwim {}: '{}' {}", a_who, ev, ok ? "accepted" : "rejected");
			}
			CrewNotify(a_actor, "IdleStop", "clear swim (before the sit)", false);
		}

		// Keeps the player in a locked crouch while riding (the sneak control is disabled, so
		// nothing else can stand him up). Re-sent every second in case a graph reset drops it.
		void HoldPlayerCrouch(float a_dt)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			// Part 16: our row loop is an idle of its own (not an offset layer on the seat), so the seat looks "lost" while it
			// plays. Leave the player alone until it stops; StopRowAnim then asks for the seat (or crouch) again.
			if (rowAnimOn_ && !rowAnimGpma_ && RowActor(player).get() == player) return;
			// Once seated, watch the seat four times a second so a dropped seat is fixed in well under a second.
			if ((crouchTimer_ += a_dt) < (sitAccepted_ ? 0.25f : 1.0f)) return;
			crouchTimer_ = 0.0f;
			auto* camNow = RE::PlayerCamera::GetSingleton();
			const bool firstPersonNow = camNow && camNow->IsInFirstPerson();
			// 56.46: first person stays seated too (MaskSeatForFreeLook frees the look), so both views share the seat logic below.
			(void)firstPersonNow;
			if (playerSit_ && !sitFailed_) {
				// Seated pose. The game accepts the sit event once, from a standing pose; any later send is
				// rejected because he is already seated, so never treat a rejection as a failure after an accept.
				// 56.48: while first person masks the sit state, bIsSitting reads false too (it mirrors the state), so the
				// "seat lost" test would stand the player up every second. The body is hidden then: trust the seat.
				if (fpSitMasked_ && sitAccepted_) { sitMiss_ = 0; return; }
				bool sitting = false;
				player->GetGraphVariableBool("bIsSitting", sitting);
				// The graph variable lags the engine's own sit state for a moment after the sit event, so trust either.
				const auto* seatState = player->AsActorState();
				const bool stateSitting = (seatState && seatState->actorState1.sitSleepState != RE::SIT_SLEEP_STATE::kNormal) || (fpSitMasked_ && sitting);
				if (sitting || stateSitting) { sitMiss_ = 0; return; }
				if (sitAccepted_) {
					// The game accepted the sit but we are standing: something dropped the seat. Sit again.
					if (++sitMiss_ >= 2) {
						sitMiss_ = 0;
						sitAccepted_ = false;
						if (sitTries_ >= 3) sitTries_ = 2;
						logger::info("PlayerSit: the seat was lost, sitting down again");
						player->NotifyAnimationGraph("IdleForceDefaultState");
					}
					return;
				}
				if (!sitAccepted_) {
					if (player->IsSneaking()) { player->NotifyAnimationGraph("sneakStop"); return; }
					if (sitTries_ >= 4) {
						sitFailed_ = true;
						logger::warn("PlayerSit: the game would not seat the player after {} tries, using the crouch instead", sitTries_);
					} else {
						++sitTries_;
						for (const char* ev : { "IdleChairEnterInstant", "IdleChairSitting", "IdleChairEnterStart" }) {
							const bool ok = player->NotifyAnimationGraph(ev);
							logger::info("PlayerSit: '{}' {}", ev, ok ? "accepted" : "rejected");
							if (ok) { sitAccepted_ = true; playerSitAt_ = rideTime_; break; }
						}
						return;
					}
				} else {
					return;  // seated (or at least accepted): nothing to do, and no crouch on top of it
				}
			}
			if (!player->IsSneaking()) {
				const bool ok = player->NotifyAnimationGraph("sneakStart");
				if (crouchLogs_++ < 4) logger::info("Crouch: sneakStart {}", ok ? "accepted" : "rejected");
			}
		}

		void UpdatePassengers(const RE::NiPoint3& a_boatPos, float a_dt)
		{
			HoldPlayerCrouch(a_dt);  // the player's crouch must not depend on having passengers
			rideTime_ += a_dt;
			if (passengers_.empty()) return;
			const float h = skimHeading_;
			const RE::NiPoint3 right{ std::cos(h), -std::sin(h), 0.0f };
			const RE::NiPoint3 fwd{ std::sin(h), std::cos(h), 0.0f };
			if (skimAirborne_ || skimOnRamp_) calmTime_ = 0.0f; else calmTime_ += a_dt;
			crewPitchBlend_ = (skimAirborne_ || skimOnRamp_) ? 1.0f : (std::max)(0.0f, crewPitchBlend_ - a_dt);  // 36.2/40.3: in the air or on a slope, and 1 s after
			if (crewPitchBlend_ <= 0.0f) pitchZeroLogged_.clear();
			landingCheckNow_ = false;
			for (auto& t : landingChecks_) {
				if (t >= 0.0f && rideTime_ >= t) { t = -1.0f; landingCheckNow_ = true; }
			}
			const bool tick = (passengerTimer_ += a_dt) >= 0.25f;
			if (tick) passengerTimer_ = 0.0f;
			int dogIndex = 0, humanIndex = 0;
			humanSitTimer_ += a_dt;
			const bool humanTick = humanSitTimer_ >= 1.0f;
			if (humanTick) humanSitTimer_ = 0.0f;
			for (auto& handle : passengers_) {
				auto actor = handle.get();
				if (!actor || actor->IsDead()) continue;
				if (auto* acell = actor->GetParentCell(); !acell || !acell->IsAttached() || !actor->Is3DLoaded()) continue;  // unloaded: leave alone
				const bool dog = IsDogActor(actor.get());
				const int index = dog ? dogIndex++ : humanIndex++;
				HumanSeat* seatPtr = dog ? nullptr : &GetHumanSeat(actor.get());
				const RE::NiPoint3 local = dog
					? DogLocal(index)  // 56.29: per-dog seat
					: seatPtr->pos;
				RE::NiPoint3 world{
					a_boatPos.x + right.x * local.x + fwd.x * local.y,
					a_boatPos.y + right.y * local.x + fwd.y * local.y,
					a_boatPos.z + boardRaise_ - skimSink_ + boatHeight_ + local.z };
				float yaw = h;
				float pitchUp = 0.0f;
				// Glued to the boat: follow its pitch, roll and bounce, not just its heading.
				RE::NiPoint3 boatFwd{}, boatRight{};
				if (BoatBow::RigidPoint(local, world, boatFwd, boatRight)) {
					yaw = std::atan2(boatFwd.x, boatFwd.y);
					pitchUp = std::asin(std::clamp(boatFwd.z, -1.0f, 1.0f));
				}
				++glueMoves_;
				actor->SetPosition(world, true);
				if (glueZeroTimer_ > 0.0f) ZeroVelocity(actor.get());  // 40.2
				if (!dog) KeepWeaponsOff(actor.get());  // 43.1.3
				if (!dog) HoldCrewCalm(actor.get());    // 56.12: every frame, rowing or resting
				RE::NiPoint3 angle = actor->GetAngle();
				// Skyrim: positive x looks down; people stay upright (40.3: the root tilt already carries the boat's pitch; the
				// actor angle also feeds the graph's look pitch, which bent the torso a second time in falls).
				angle.x = dog ? -pitchUp : 0.0f;
				if (!dog && crewPitchBlend_ > 0.0f) ZeroGraphPitch(actor.get());
				if (!dog) crewBoatPitch_[actor->GetFormID()] = pitchUp;
				angle.z = yaw + (dog ? DogYaw(index) : seatPtr->yaw);
				actor->SetAngle(angle);
				if (!dog) {
					// Human: sit on the seat (the game only accepts the sit event from a standing pose), plus the
					// stand-up watcher, seated idles and flinches.
					TendCrewHuman(actor.get(), seatPtr->slot == 0, RowerOrHelmSeat(*seatPtr), a_dt, tick, humanTick);
					continue;
				}
				if (tick) {
					// Sit control, from the log: a dog only sits from a standing idle. When lying
					// down it must stand first (IdleStop), then idleSitStart is accepted.
					bool sitting = false, lying = false;
					actor->GetGraphVariableBool("isIdleSitting", sitting);
					actor->GetGraphVariableBool("isIdleLay", lying);
					if (!sitting) {
						const bool ok = CrewNotify(actor.get(), lying ? "IdleStop" : "idleSitStart", "dog sit control");
						if (passengerLogs_ < 150) {
							++passengerLogs_;
							logger::info("Passengers: {:08X} sitting={} lying={} -> {} {}", actor->GetFormID(), sitting, lying, lying ? "IdleStop" : "idleSitStart", ok ? "accepted" : "rejected");
						}
					}
				}
			}
			ScheduleSeatedIdles(a_dt);  // v1.0.14: one shared scheduler for the seated idles (14.9)
			ApplyPassengerTilt();
		}

		// If dry land is close to the boat, returns a spot on it (nearest ring first). Beyond
		// kShoreRange we return false and everybody just splashes into the water.
		bool FindShoreSpot(const RE::NiPoint3& a_from, float a_heading, RE::NiPoint3& a_out)
		{
			auto* tes = RE::TES::GetSingleton();
			if (!tes) return false;
			float w0 = 0.0f;
			if (!SkimWaterAt(a_from, w0)) return false;
			float bcx = 0.0f, bcy = 0.0f;
			BoatBow::BoatCenterFromVehicle(a_from.x, a_from.y, a_heading, bcx, bcy);
			for (float r = 100.0f; r <= kShoreRange; r += 55.0f) {
				for (int i = 0; i < 24; ++i) {
					const float a = static_cast<float>(i) * 6.2831853f / 24.0f;
					const float sx = std::sin(a), sy = std::cos(a);
					// Step a little past the edge so we stand on it, not at the very rim.
					for (float extra : { 0.0f, 60.0f }) {
						RE::NiPoint3 p{ a_from.x + sx * (r + extra), a_from.y + sy * (r + extra), a_from.z };
						float w = 0.0f, land = 0.0f;
						// 1) natural terrain above the water
						if (!SkimWaterAt(p, w) && tes->GetLandHeight(p, land) && std::isfinite(land) && land >= w0 - 40.0f && land <= w0 + 250.0f) {
							p.z = land + 2.0f;
							a_out = p;
							return true;
						}
						// 2) placed objects: jetties, docks, stone platforms. Ray straight down; the top must
						// sit just above the water (not a roof or a high ledge). Skip the boat itself.
						const float dxb = p.x - bcx, dyb = p.y - bcy;
						if (dxb * dxb + dyb * dyb < 300.0f * 300.0f) continue;
						const RE::NiPoint3 top{ p.x, p.y, w0 + 220.0f };
						const RE::NiPoint3 bottom{ p.x, p.y, w0 - 60.0f };
						float frac = 0.0f;
						RE::COL_LAYER layer{};
						if (!SkimSweep(top, bottom, frac, &layer) || layer != RE::COL_LAYER::kStatic) continue;
						const float hitZ = top.z + (bottom.z - top.z) * frac;
						if (hitZ < w0 + 10.0f || hitZ > w0 + 160.0f) continue;
						p.z = hitZ + 2.0f;
						a_out = p;
						return true;
					}
				}
			}
			return false;
		}

		// Everyone gets off inside the parked boat: the player amidships, passengers forward of
		// them. No shore search; they drop onto the hull floor from just above it.
		void ReleasePassengers(const RE::NiPoint3& a_vehicle, float a_heading)
		{
			float cx = 0.0f, cy = 0.0f;
			BoatBow::BoatCenterFromVehicle(a_vehicle.x, a_vehicle.y, a_heading, cx, cy);
			const RE::NiPoint3 fwd{ std::sin(a_heading), std::cos(a_heading), 0.0f };
			const RE::NiPoint3 side{ std::cos(a_heading), -std::sin(a_heading), 0.0f };
			float w = 0.0f;
			const float z = (SkimWaterAt(RE::NiPoint3{ cx, cy, a_vehicle.z }, w) ? w + kBoatExitDrop : a_vehicle.z) + 10.0f;  // a little above the deck
			// v1.0.15 (B8): with dry ground beside the boat, the crew step out onto it instead of standing in the hull.
			RE::NiPoint3 shore{};
			const bool toShore = FindShoreSpot(a_vehicle, a_heading, shore);
			// B11: free spots on the deck, away from the player's exit spot (helm, about 242 behind the centre, or the rower's
			// seat, about 13 ahead of it), the chest (30 ahead) and the benches: two columns at the bow and stern ends.
			static constexpr float kSpots[][2] = { { -175.0f, -32.0f }, { -175.0f, 32.0f }, { -125.0f, -32.0f }, { -125.0f, 32.0f },
				{ 100.0f, -32.0f }, { 100.0f, 32.0f }, { 150.0f, -32.0f }, { 150.0f, 32.0f }, { 200.0f, -30.0f }, { 200.0f, 30.0f },
				{ -80.0f, -32.0f }, { -80.0f, 32.0f } };
			// 17.2 (v1.0.19): the player steps off at their seat (as in ExitPlayerSkim). Spots closer than 150 to it go last,
			// so nobody is put where the player has to walk (two "Stuck" right after an exit had Faendal 40 to 75 away).
			const RE::NiPoint3 playerExit{ a_vehicle.x + std::sin(a_heading) * playerBodyFwd_ + std::cos(a_heading) * playerBodySide_,
				a_vehicle.y + std::cos(a_heading) * playerBodyFwd_ - std::sin(a_heading) * playerBodySide_, 0.0f };
			int order[12];
			float spotDist[12];
			for (int i = 0; i < 12; ++i) {
				order[i] = i;
				const float sx = cx + fwd.x * kSpots[i][0] + side.x * kSpots[i][1] - playerExit.x;
				const float sy = cy + fwd.y * kSpots[i][0] + side.y * kSpots[i][1] - playerExit.y;
				spotDist[i] = std::sqrt(sx * sx + sy * sy);
			}
			std::stable_sort(order, order + 12, [&](int a, int b) {
				const bool fa = spotDist[a] >= 150.0f, fb = spotDist[b] >= 150.0f;
				if (fa != fb) return fa;
				return !fa && spotDist[a] > spotDist[b];
			});
			int index = 0;
			for (auto& handle : passengers_) {
				auto actor = handle.get();
				if (!actor) { ++index; continue; }
				const auto& spot = kSpots[order[index % 12]];
				RE::NiPoint3 pos{ cx + fwd.x * spot[0] + side.x * spot[1], cy + fwd.y * spot[0] + side.y * spot[1], z };
				if (toShore) {
					const float s = 55.0f * static_cast<float>((index + 1) / 2) * (index % 2 ? 1.0f : -1.0f);  // a loose line along the shore
					pos = RE::NiPoint3{ shore.x + side.x * s, shore.y + side.y * s, shore.z + 4.0f };
				}
				logger::info("Move: {:08X} '{}' release {}", actor->GetFormID(), actor->GetName(), toShore ? "onto the ground beside the boat" : "onto the parked boat's deck");  // crash breadcrumb (v1.0.13)
				actor->RemoveAnimationGraphEventSink(&passengerSink_);
				// B11: released in steps. Held at the spot with collision off (and still held still) until the new parked boat
				// and its props are loaded; then velocity zeroed and collision on; SetDontMove(false) a frame later; then a 3 s
				// launch guard (TendRelease).
				ZeroVelocity(actor.get());
				const bool dog = IsDogActor(actor.get());
				CrewNotify(actor.get(), "IdleStop", "release", false);
				if (dog) {
					// 20.1 (v1.0.22): moved while still sitting or lying, the dog was kicked off (Launch guard: Meeko speed 1529).
					// He stands first where he is (TendRelease phase -1, up to 1.5 s), then goes a little higher above the deck.
					pos.z += 10.0f;
					releases_.push_back({ actor->GetHandle(), pos, actor->GetName(), toShore, -1, 0.0f, actor->GetPosition() });
				} else {
					actor->SetPosition(pos, true);
					CrewNotify(actor.get(), "IdleChairExitStart", "release", false);
					CrewNotify(actor.get(), "IdleFurnitureExit", "release", false);
					releases_.push_back({ actor->GetHandle(), pos, actor->GetName(), toShore, 0, 0.0f, pos });
				}
				if (!toShore) logger::info("Release spot: {} {:.0f} from the player's exit spot", actor->GetName(), spotDist[order[index % 12]]);
				WatchAfterRelease(actor.get());
				if (std::find(rowedThisRide_.begin(), rowedThisRide_.end(), actor->GetFormID()) != rowedThisRide_.end() && !IsDogActor(actor.get()))
					rowKicks_.push_back({ actor->GetHandle(), actor->GetName(), 0.0f, 0 });
				++index;
			}
			passengers_.clear();
			ClearCrewIdle();
		}

		static void ZeroVelocity(RE::Actor* a_actor)
		{
			if (auto* cc = a_actor ? a_actor->GetCharController() : nullptr) {
				const RE::hkVector4 zero(0.0f, 0.0f, 0.0f, 0.0f);
				cc->SetLinearVelocityImpl(zero);
			}
		}

		// B11: a released passenger. phase 0 held at the spot, 1 collision on (DontMove off next frame), 2 launch guard.
		struct Release { RE::ActorHandle actor; RE::NiPoint3 spot; std::string name; bool shore; int phase; float t; RE::NiPoint3 last; };
		std::vector<Release> releases_;
		void TendRelease(float a_dt)
		{
			if (releases_.empty() || a_dt <= 0.0f) return;
			const bool deckReady = BoatBow::ParkedSolid();
			std::erase_if(releases_, [&](Release& r) {
				auto a = r.actor.get();
				if (!a || a->IsDead()) return true;
				r.t += a_dt;
				if (r.phase == -1) {
					// 20.1: the dog stands up where he is (held still, collision off) before he is moved.
					a->SetPosition(r.last, true);
					ZeroVelocity(a.get());
					bool sitting = false, lying = false;
					a->GetGraphVariableBool("isIdleSitting", sitting);
					a->GetGraphVariableBool("isIdleLay", lying);
					if ((sitting || lying) && r.t < 1.5f) {
						if (std::fmod(r.t, 0.25f) < a_dt) CrewNotify(a.get(), "IdleStop", "release stand", false);
						return false;
					}
					if (sitting || lying) logger::info("Release: {} did not stand within 1.5 s (sitting={} lying={}), moved anyway", r.name, sitting, lying);
					else logger::info("Release: {} stood after {:.0f} ms", r.name, r.t * 1000.0f);
					a->SetPosition(r.spot, true);
					ZeroVelocity(a.get());
					r.phase = 0;
					r.t = 0.0f;
					return false;
				}
				if (r.phase == 0) {
					a->SetPosition(r.spot, true);
					ZeroVelocity(a.get());
					// 20.1: a dog keeps no collision for its first second on the deck (clear of the props as they spawn).
					const float minHold = IsDogActor(a.get()) ? 1.0f : 0.3f;
					if (!((r.shore || deckReady) && r.t >= minHold) && r.t < 2.0f) return false;
					a->SetCollision(true);
					r.phase = 1;
					r.t = 0.0f;
					r.last = a->GetPosition();
					return false;
				}
				if (r.phase == 1) {
					SetActorDontMove(a.get(), false);
					if (!SheatheIfCalm(a.get(), "unboard") && !IsDogActor(a.get())) sheatheWatch_.emplace_back(a->GetHandle(), 30.0f);
					logger::info("Passengers: released {:08X} {}{}", a->GetFormID(), r.shore ? "beside the boat" : "on the deck", deckReady || r.shore ? "" : " (the deck was not solid after 2 s)");
					r.phase = 2;
					r.t = 0.0f;
					return false;
				}
				const RE::NiPoint3 p = a->GetPosition();
				const float speed = (p - r.last).Length() / a_dt;
				const float before = r.t - a_dt;
				for (float mark : { 0.5f, 1.0f, 2.0f }) {
					if (before < mark && r.t >= mark) {
						logger::info("Release: {} at ({:.0f},{:.0f},{:.0f}) speed {:.0f}", r.name, p.x, p.y, p.z, speed);
						if (speed > 400.0f) logger::warn("Release: {} moving fast after release (speed {:.0f}), check the stand-up before the move", r.name, speed);
					}
				}
				if (speed > 1000.0f || p.z > r.spot.z + 300.0f) {
					logger::warn("Launch guard: {} speed {:.0f} z {:.0f}, put back", r.name, speed, p.z);
					a->SetPosition(r.spot, true);
					ZeroVelocity(a.get());
					r.last = r.spot;
				} else {
					r.last = p;
				}
				return r.t >= 3.0f;
			});
		}

		// B1 (v1.0.15): a crew member with a weapon out and nobody fighting puts it away, and the AI picks its package again
		// (seated and held still, their own "sheathe when calm" never ran, and the drawn state is saved on the actor).
		bool SheatheIfCalm(RE::Actor* a_actor, const char* a_why)
		{
			if (!sheatheOn_) return false;
			if (!a_actor || a_actor->IsDead() || !a_actor->Is3DLoaded() || a_actor->IsPlayerRef()) return false;
			const auto* st = a_actor->AsActorState();
			if (!st || !st->IsWeaponDrawn() || a_actor->IsInCombat()) return false;
			if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->IsInCombat()) return false;
			a_actor->DrawWeaponMagicHands(false);
			a_actor->EvaluatePackage(true, false);
			logger::info("Sheathe: {} {:08X} ({})", a_actor->GetName(), a_actor->GetFormID(), a_why);
			sheatheVerify_.push_back({ a_actor->GetHandle(), 1.0f, 1 });  // B10: checked 1 s later, other methods if it did not work
			return true;
		}

		// B10 (v1.0.15): 1 s after a sheathe, is the weapon away? If not, try again with another method, up to 3 tries:
		// DrawWeaponMagicHands(false), then the script's SheatheWeapon(), then the graph event weaponSheathe.
		struct SheatheCheck { RE::ActorHandle actor; float timer; int tries; };
		std::vector<SheatheCheck> sheatheVerify_;
		void SheatheViaScript(RE::Actor* a_actor)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!policy) return;
			const auto handle = policy->GetHandleForObject(static_cast<RE::VMTypeID>(a_actor->GetFormType()), a_actor);
			if (!handle) return;
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
			auto args = RE::MakeFunctionArguments();
			vm->DispatchMethodCall(handle, RE::BSFixedString("Actor"), RE::BSFixedString("SheatheWeapon"), args, callback);
		}
		void TendSheatheVerify(float a_dt)
		{
			std::erase_if(sheatheVerify_, [&](SheatheCheck& c) {
				if ((c.timer -= a_dt) > 0.0f) return false;
				auto a = c.actor.get();
				if (!a || a->IsDead()) return true;
				const auto* st = a->AsActorState();
				const bool drawn = st && st->IsWeaponDrawn();
				logger::info("Sheathe: {} {:08X} result drawn={} (try {})", a->GetName(), a->GetFormID(), drawn ? "yes" : "no", c.tries);
				if (!drawn || c.tries >= 3 || a->IsInCombat()) return true;
				++c.tries;
				c.timer = 1.0f;
				if (c.tries == 2) { SheatheViaScript(a.get()); logger::info("Sheathe: {} retry with SheatheWeapon()", a->GetName()); }
				else { const bool ok = a->NotifyAnimationGraph("weaponSheathe"); logger::info("Sheathe: {} retry with weaponSheathe ({})", a->GetName(), ok ? "accepted" : "rejected"); }
				return false;
			});
		}

		// The crew just released are checked once a second for 30 s (a fight may still be going on at the moment they get off),
		// and the co-save crew a few seconds after a load.
		// B8: holding the player on the parked boat's deck after an exit until the deck is solid, then a check 1 s later.
		float exitHold_{ 0.0f };
		float exitCheck_{ -1.0f };
		RE::NiPoint3 exitPos_{};
		void TendExitHold(RE::PlayerCharacter* a_player, float a_dt)
		{
			if (playerSkim_ || !a_player) { exitHold_ = 0.0f; exitCheck_ = -1.0f; return; }
			if (exitHold_ > 0.0f) {
				exitHold_ -= a_dt;
				const bool ready = BoatBow::ParkedSolid();
				if (!ready && exitHold_ > 0.0f) {
					a_player->SetPosition(exitPos_, true);  // zero velocity: placed, not falling
					return;
				}
				exitHold_ = 0.0f;
				a_player->SetCollision(true);
				logger::info("Exit: deck {} , player collision on", ready ? "solid" : "still not solid after 1.5 s");
			}
			if (exitCheck_ > 0.0f && (exitCheck_ -= a_dt) <= 0.0f) {
				exitCheck_ = -1.0f;
				const auto p = a_player->GetPosition();
				float wz = 0.0f;
				const bool overWater = SkimWaterAt(p, wz);
				const auto* st = a_player->AsActorState();
				const bool swimming = st && st->IsSwimming();
				logger::info("Exit: 1 s later player at ({:.0f},{:.0f},{:.0f}), water z {}, swimming {}", p.x, p.y, p.z, overWater ? std::format("{:.0f}", wz) : std::string("none"), swimming ? "yes" : "no");
				if (swimming) ClearSwimState(a_player, "player after exit");  // B8 (2): the same swim clear as at boarding
				// B3: the chest after a ride.
				if (auto* chest = BoatBow::ChestRef()) {
					float bx = 0.0f, by = 0.0f, bz = 0.0f, byaw = 0.0f;
					const bool parked = BoatBow::GetParked(bx, by, bz, byaw);
					const auto cp = chest->GetPosition();
					std::int32_t items = 0;
					for (auto& [obj, data] : chest->GetInventory()) if (obj && data.first > 0) items += data.first;
					logger::info("Chest: after exit {:08X} 3D={} disabled={} items={} ref {:.0f} below / {:.0f} from the boat, pick proxy {}", chest->GetFormID(), chest->Is3DLoaded(), chest->IsDisabled(), items,
						parked ? bz - cp.z : 0.0f, parked ? std::sqrt((cp.x - bx) * (cp.x - bx) + (cp.y - by) * (cp.y - by)) : -1.0f, "see the Pick: lines");
				} else {
					logger::info("Chest: after exit none spawned yet");
				}
			}
		}

		std::vector<std::pair<RE::ActorHandle, float>> sheatheWatch_;
		float sheatheTick_{ 0.0f };
		float sheatheLoadTimer_{ -1.0f };
		void TendSheathe(float a_dt)
		{
			TendSheatheVerify(a_dt);
			if (sheatheLoadTimer_ > 0.0f) {
				const float before = sheatheLoadTimer_;
				sheatheLoadTimer_ -= a_dt;
				if (before > 6.0f && sheatheLoadTimer_ <= 6.0f) SheatheCrewOnLoad();  // 4 s after the load
				if (sheatheLoadTimer_ <= 0.0f) SheatheCrewOnLoad();                   // and again at 10 s
			}
			if (sheatheWatch_.empty() || (sheatheTick_ += a_dt) < 1.0f) return;
			sheatheTick_ = 0.0f;
			std::erase_if(sheatheWatch_, [&](auto& a_entry) {
				a_entry.second -= 1.0f;
				auto a = a_entry.first.get();
				if (!a) return true;
				if (SheatheIfCalm(a.get(), "calm")) return true;
				return a_entry.second <= 0.0f;
			});
		}

		// After a load: every crew member (co-save list) and every follower nearby who has a weapon out without a fight.
		void SheatheCrewOnLoad()
		{
			int checked = 0, sheathed = 0;
			std::vector<RE::FormID> done;
			auto one = [&](RE::Actor* a_actor) {
				if (!a_actor || std::find(done.begin(), done.end(), a_actor->GetFormID()) != done.end()) return;
				done.push_back(a_actor->GetFormID());
				++checked;
				if (SheatheIfCalm(a_actor, "load")) ++sheathed;
			};
			for (auto id : crew_) one(RE::TESForm::LookupByID<RE::Actor>(id));
			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				for (auto& h : lists->highActorHandles) {
					auto a = h.get();
					if (a && (a->IsPlayerTeammate() || IsCrew(a.get()))) one(a.get());
				}
			}
			logger::info("Sheathe: load check, {} crew / follower(s) looked at, {} sheathed", checked, sheathed);
		}

		// "oas boat": drop a real, solid rowboat in front of the player (in water).
		bool SpawnParkedBoat()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || playerSkim_) return false;
			const RE::NiPoint3 pp = player->GetPosition();
			const float yaw = player->GetAngleZ();
			const RE::NiPoint3 fwd{ std::sin(yaw), std::cos(yaw), 0.0f };
			for (float d : { kParkedBoatForward, kParkedBoatForward * 0.66f, kParkedBoatForward * 0.33f, 0.0f }) {
				const RE::NiPoint3 p{ pp.x + fwd.x * d, pp.y + fwd.y * d, pp.z };
				float water = 0.0f;
				if (SkimWaterAt(p, water)) {
					return BoatBow::SpawnParked(p.x, p.y, water + kParkedBoatZ + boatHeight_, yaw);
				}
			}
			logger::info("SpawnParkedBoat: no loaded water ahead of the player");
			return false;
		}

		bool ToggleSkimPlayer()
		{
			if (playerSkim_) {
				ExitPlayerSkim("toggle");
				return false;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return false;
			// Near a parked boat: board that one (same as pressing E).
			float bx = 0.0f, by = 0.0f, bz = 0.0f, byaw = 0.0f;
			if (BoatBow::GetParked(bx, by, bz, byaw)) {
				const RE::NiPoint3 pp = player->GetPosition();
				const float dx = pp.x - bx;
				const float dy = pp.y - by;
				if (dx * dx + dy * dy < 900.0f * 900.0f) {
					return BoardParkedBoat();
				}
			}
			return StartPlayerSkim(player->GetPosition(), player->GetAngleZ(), false);
		}

		// Sit down in the parked boat: it becomes the vehicle (collision off) and we
		// take off from its stern, facing the bow.
		bool BoardParkedBoat()
		{
			float bx = 0.0f, by = 0.0f, bz = 0.0f, byaw = 0.0f;
			if (!BoatBow::GetParked(bx, by, bz, byaw)) return false;
			float vx = 0.0f, vy = 0.0f;
			BoatBow::VehicleFromBoatCenter(bx, by, byaw, vx, vy);
			return StartPlayerSkim(RE::NiPoint3{ vx, vy, bz }, byaw, true);
		}

		bool StartPlayerSkim(const RE::NiPoint3& a_start, float a_heading, bool a_adopt)
		{
			if (playerSkim_) return false;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!player || !cam) return false;

			RE::NiPoint3 start = a_start;
			float water = 0.0f;
			if (!SkimWaterAt(start, water)) {
				logger::info("SkimPlayer: start rejected (stand in loaded open water first)");
				if (auto* console = RE::ConsoleLog::GetSingleton()) console->Print("Oars and Sails: the boat is not in loaded open water");
				return false;
			}
			if (!haveSavedFlags_) {
				haveSavedFlags_ = true;
				bool v = false;
				// bAllowRotation and foot IK are always OFF on a walking player (first baseline of every session), so they are not read:
				// a save made after a broken ride could hold the wrong value. Only head tracking varies between saves.
				savedAllowRot_ = false;
				savedFootIKOff_ = false;
				if (player->GetGraphVariableBool("bHeadTracking", v)) savedHeadTrack_ = v;
				logger::info("Saved walking flags: bAllowRotation={} bHumanoidFootIKDisable={} bHeadTracking={}", savedAllowRot_, savedFootIKOff_, savedHeadTrack_);
			}
			if (kDebugDumps) {
				CheckInputHandlers("before boarding", false);
				DumpController("before boarding");
				snapBefore_ = CaptureRaw();
				DumpAllGraphVars(player, "before");  // baseline of a normally walking player
			}
			if (cam->IsInFreeCameraMode()) cam->ToggleFreeCameraMode(false);
			// stays in whatever view the player was already using (first or third person)

			if (a_adopt) {
				// Seat the player at the stern, looking along the boat.
				logger::info("Move: player onto the stern seat (boarding)");  // crash breadcrumb (v1.0.13)
				player->SetPosition(RE::NiPoint3{ start.x, start.y, water + kV7FeetClearance - kV7PlayerDrop + kCrouchEyeLift + boatHeight_ }, true);
				RE::NiPoint3 ang = player->GetAngle();
				ang.x = 0.0f;
				ang.z = a_heading;
				player->SetAngle(ang);
			}

			RefreshSkimKeys();
			inFwd_ = inBack_ = inLeft_ = inRight_ = false;
			stickX_ = stickY_ = 0.0f;
			activatePressed_ = false;

			skimHeading_ = a_heading;
			skimSpeed_ = 0.0f;
			skimReverse_ = 0.0f;
			skimVerticalSpeed_ = 0.0f;
			skimYawRate_ = 0.0f;
			skimAirborne_ = false;
			skimDropGrace_ = 0.0f;
			skimOnLand_ = false;
			skimOnRamp_ = false;
			skimRampTime_ = 0.0f;
			skimFallExit_ = skimBlockedTime_ = skimFlowTimer_ = 0.0f;
			skimFlowAhead_ = false;
			fallLift_ = 0.0f;
			poolRise_ = 0.0f;
			logger::info("Hull extents: bow tip +{:.0f}, stern -{:.0f}, half width {:.0f} (from hull centre; feeler values, mesh bounds are bounding spheres only)",
				HullBowTip(), HullStern(), HullHalfWidth());
			glueZeroTimer_ = 0.0f;
			glueDriftFrames_ = 0;
			patches_.clear();
			lastLandingAt_ = -100.0f;
			flowVel_ = {};
			flowW_ = flowRoll_ = 0.0f;
			descent_ = false;
			jumpHave_ = false;
			flowHoldUntil_ = lastFlowNearAt_ = lastBlockedAt_ = -100.0f;
			liftVel_ = pitchVel_ = rollVel_ = 0.0f;
			governor_ = 1.0f;
			govEngaged_ = false;
			onIce_ = false;
			iceSamplesPrev_ = 0;
			glueGraphLogged_ = false;
			feelPush_ = {};
			feelYawKick_ = feelStopTimer_ = 0.0f;
			feelPrevAt_.fill(0.0f);
			helmSweepLogged_ = false;
			skimRampPitch_ = skimRampVz_ = 0.0f;
			skimDip_ = skimDipVel_ = 0.0f;
			skimRecoverTime_ = 0.0f;
			skimLaunchCooldown_ = 0.0f;
			skimWaterHeight_ = water;
			vehiclePos_ = RE::NiPoint3{ start.x, start.y, water + kSkimEyeHeight };
			skimLastWaterPos_ = vehiclePos_;
			groundPitch_ = groundRoll_ = groundBlend_ = 0.0f;
			groundLift_ = appliedLift_ = 0.0f;
			contactSuspended_ = false;
			contactScale_ = 1.0f;
			contactQuiet_ = lookTimer_ = postLandTimer_ = 0.0f;
			wasInAir_ = false;
			lookHit_ = -1.0f;
			for (int i = 0; i < kGroundSamples; ++i) { touchState_[i] = dryState_[i] = bankState_[i] = false; touchTimer_[i] = dryTimer_[i] = bankClearTimer_[i] = 0.0f; }
			basinHold_ = false;
			wasGrounded_ = hasBowMark_ = hasBankMark_ = tooHigh_ = false;
			waterfallTimer_ = 0.0f;
			hasPrevVehicle_ = false;
			hasLastGood_ = false;
			bodyHideLogged_ = false;
			edgeHold_ = 0.0f;
			lastPlayerCellOk_ = true;
			diagTimer_ = 0.0f;

			player->SetCollision(false);  // "tcl": no player collision, no gravity
			unequipped_.clear();
			weaponNext_.clear();
			UnequipWeapons(player, "boarding");  // 43.1
			player->DrawWeaponMagicHands(false);
			SetSkimControls(false);
			if (hideHudInBoat_) HideHUD(); else ForceHudVisible(true);  // the game HUD (bars, compass, crosshair) stays visible in the boat
			hudTimer_ = 0.0f;
			hudLogs_ = 0;
			if (lockSeatedFacing_) SetActorHeadTracking(player, false);
			if (!physicsCmd_.empty()) physicsDelay_ = 1.5f;  // let the seat settle, then reset the physics mods once

			boardSpot_ = start;
			boardedParked_ = a_adopt && BoatBow::AttachParked();
			if (!boardedParked_) {
				BoatBow::Attach();
			}
			// A boarded boat starts on the water and lifts off; the bow-only boat starts lifted.
			boardRaise_ = boardedParked_ ? 0.0f : kV7BoatRaise;
			playerSkim_ = true;
			boardPrompted_ = true;
			passengerLogs_ = 0;
			pickLogs_ = 0;
			skimSink_ = 0.0f;
			calmTime_ = 10.0f;
			ClearSwimState(player, "player");
			getUpPending_ = false;
			standTimer_ = 0.0f;  // a stand-up watchdog left over from the last exit must not stand us up again
			sitMiss_ = 0;
			crouchTimer_ = 1.0f;
			crouchLogs_ = 0;
			sitTries_ = 0;
			sitAccepted_ = false;
			sitFailed_ = false;
			BoardPassengers(player);
			// 43.1: crew and followers put their weapons away for good (sheathing kept failing).
			// 56.8: into the stash instead (late boarders: KeepWeaponsOff stashes them on their first glue pass).
			RegisterEquipSink();
			calm_.clear();  // 56.12: fresh edges for this ride
			for (auto& hnd : passengers_) {
				if (auto a = hnd.get(); a && !IsDogActor(a.get())) {
					if (a->IsInCombat()) a->StopCombat();
					StashWeapons(a.get(), "boarding");
				}
			}
			logger::info("Weapons: blocked draw while aboard (fighting controls off)");
			BoatBow::SweepOrphans(true, "board");  // 43.4.3
			logger::info("Chest: {} (board)", BoatBow::ChestSummary());  // 43.4.5
			// v1.0.14: Row on the rower's bench, or nobody else to row: the player rows. Otherwise (Steer, or Board with crew) an NPC rows.
			const bool playerRows = boardChoice_ == BoardChoice::kRow || BoardedHumans().empty();
			boardChoice_ = BoardChoice::kDefault;
			AssignBoardingSeats(playerRows);
			SelectPlayerSeat(RowerSeat() == nullptr);  // nobody to row: the player takes the rowing seat, otherwise the stern
			if (playerOnRowSeat_) faceRowerPending_ = true;  // 56.47: boarding as the rower in third person: the camera faces him at once
			faceRowerPending_ = playerOnRowSeat_ && faceRowerOn_;  // 56.45: third person view turned to the rower's face once
			BoatBow::SetPickAllowed(false);
			inputLogged_ = false;
			padLogs_ = 0;
			r3ZoomLogged_ = false;  // 55.1: the zoom line once per boarding
			padButtons_ = padSticks_ = 0;
			padUserEvents_.clear();
			if (greetOffOn_) SuppressGreetings(true);
			logger::info("Camera 1P offset: {:.0f} (oas \"eye N\" to change)", fpEyeBoost_);
			lastFeet_ = player->GetPosition();
			boardWorld_ = player->GetWorldspace();
			boardInterior_ = player->GetParentCell() && player->GetParentCell()->IsInteriorCell();
			cellLostTime_ = 0.0f;
			logger::info("SkimPlayer ON: pos=({:.1f},{:.1f},{:.1f}) heading={:.2f} boarded={} keys W={:#x} S={:#x} A={:#x} D={:#x} E={:#x}",
				start.x, start.y, start.z, skimHeading_, boardedParked_, keyFwd_, keyBack_, keyLeft_, keyRight_, keyActivate_);
			return true;
		}

		// Writes the animation graph flags that can stop WASD to OarsAndSails.log (used after getting out and by `oas unstick`).
		void DumpWalkState(RE::PlayerCharacter* a_player, const char* a_tag)
		{
			if (!a_player) return;
			std::string line;
			for (const char* v : { "bOffsetGPMA", "bAnimationDriven", "bIsSitting", "isIdleSitting", "isInFurniture", "bAllowRotation", "bInJumpState", "IsSneaking", "IsMoving", "bForceIdleStop", "bDisableInterp" }) {
				bool b = false;
				const bool ok = a_player->GetGraphVariableBool(v, b);
				line += std::format(" {}={}", v, ok ? (b ? "1" : "0") : "?");
			}
			for (const char* v : { "Speed", "SpeedSampled", "SpeedDamped", "Direction" }) {
				float f = 0.0f;
				const bool ok = a_player->GetGraphVariableFloat(v, f);
				line += ok ? std::format(" {}={:.2f}", v, f) : std::format(" {}=?", v);
			}
			for (const char* v : { "iGPMAOffsetType", "iGPMAAnimationType", "iState_NPCDefault", "iState_NPCSneaking", "iSyncIdleLocomotion", "iSyncIdleState" }) {
				std::int32_t i = 0;
				const bool ok = a_player->GetGraphVariableInt(v, i);
				line += ok ? std::format(" {}={}", v, i) : std::format(" {}=?", v);
			}
			if (auto* cm = RE::ControlMap::GetSingleton()) {
				line += std::format(" moveControls={} sneakControls={}", cm->IsMovementControlsEnabled() ? 1 : 0, cm->IsSneakingControlsEnabled() ? 1 : 0);
			}
			if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->currentState) {
				line += std::format(" cameraState={}", static_cast<int>(cam->currentState->id));
			}
			const auto pos = a_player->GetPosition();
			logger::info("WalkState [{}]:{} pos=({:.0f},{:.0f},{:.0f})", a_tag, line, pos.x, pos.y, pos.z);
		}

		// Everything we know that can leave the player unable to walk, cleared in one go. `oas unstick` runs it by hand.
		void Unstick()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || playerSkim_) return;
			DumpWalkState(player, "unstick before");
			for (const char* ev : { "OffsetGPMAStop", "IdleFurnitureExit", "IdleForceDefaultState", "sneakStop" }) {
				const bool ok = player->NotifyAnimationGraph(ev);
				logger::info("Unstick: '{}' {}", ev, ok ? "accepted" : "rejected");
			}
			for (const char* v : { "bOffsetGPMA", "bAnimationDriven", "bIsSitting", "isIdleSitting", "isInFurniture", "bForceIdleStop" }) player->SetGraphVariableBool(v, false);
			player->SetGraphVariableBool("bAllowRotation", savedAllowRot_);
			player->SetGraphVariableBool("bHumanoidFootIKDisable", savedFootIKOff_);
			if (auto* st = player->AsActorState()) st->actorState1.sitSleepState = RE::SIT_SLEEP_STATE::kNormal;
			player->SetCollision(true);
			SetSkimControls(false);
			SetSkimControls(true, true);
			if (kDebugDumps) {
				CheckInputHandlers("unstick", false);
				DumpController("unstick");
				DumpAllGraphVars(player, "unstick");
			}
			DumpWalkState(player, "unstick after");
		}

		// Getting out leaves the player inside the parked boat. If WASD is held but he does not move for a moment in the
		// first 20 seconds, log the walk state (it is a diagnostic only: lifting him out of the boat did not help).
		void WatchStuck(RE::PlayerCharacter* a_player, float a_dt)
		{
			if (!a_player || playerSkim_ || stuckWindow_ <= 0.0f) { stuckTimer_ = 0.0f; return; }
			stuckWindow_ -= a_dt;
			const auto pos = a_player->GetPosition();
			if (!(inFwd_ || inBack_ || inLeft_ || inRight_)) { stuckTimer_ = 0.0f; stuckPos_ = pos; return; }
			const float dx = pos.x - stuckPos_.x, dy = pos.y - stuckPos_.y;
			if (dx * dx + dy * dy > 9.0f) { stuckTimer_ = 0.0f; stuckPos_ = pos; return; }
			stuckTimer_ += a_dt;
			if (stuckTimer_ < 0.6f) return;
			stuckTimer_ = 0.0f;
			float bx = 0.0f, by = 0.0f, bz = 0.0f, byaw = 0.0f;
			if (!BoatBow::GetParked(bx, by, bz, byaw)) return;
			const float ddx = pos.x - bx, ddy = pos.y - by;
			logger::info("Stuck: WASD held but no movement at ({:.0f},{:.0f},{:.0f}), parked boat at ({:.0f},{:.0f},{:.0f}) distance {:.0f}", pos.x, pos.y, pos.z, bx, by, bz, std::sqrt(ddx * ddx + ddy * ddy));
			DumpWalkState(a_player, "stuck");
			// 17.2: everyone close enough to be in the way.
			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				for (auto& h : lists->highActorHandles) {
					auto a = h.get();
					if (!a || a.get() == a_player) continue;
					const auto p = a->GetPosition();
					const float d = std::sqrt((p.x - pos.x) * (p.x - pos.x) + (p.y - pos.y) * (p.y - pos.y));
					if (d < 250.0f) logger::info("Stuck: near the player: {} {:08X} at ({:.0f},{:.0f},{:.0f}) distance {:.0f}", a->GetName(), a->GetFormID(), p.x, p.y, p.z, d);
				}
			}
		}

		// For a few seconds after getting off, keep clearing any leftover seated state so the player can walk.
		// Logs what the game reports so a stuck state can be read from OarsAndSails.log.
		// Raw memory snapshots of the player object and its character controller. Taken standing still before boarding and
		// again standing still after getting out, so every 4-byte word that differs is something the ride changed.
		struct RawSnap { std::vector<unsigned char> player, ctrl; };
		RawSnap snapBefore_;
		RawSnap CaptureRaw()
		{
			RawSnap r;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return r;
			const std::size_t n = sizeof(RE::PlayerCharacter) + 0x40;
			r.player.assign(reinterpret_cast<const unsigned char*>(player), reinterpret_cast<const unsigned char*>(player) + n);
			if (auto* ctrl = player->GetCharController()) {
				r.ctrl.assign(reinterpret_cast<const unsigned char*>(ctrl), reinterpret_cast<const unsigned char*>(ctrl) + sizeof(RE::bhkCharacterController));
			}
			return r;
		}
		void DiffRaw(const RawSnap& a_before, const RawSnap& a_after)
		{
			auto diff = [](const char* a_name, const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
				if (a.empty() || b.empty()) { logger::info("RawDiff[{}]: no data", a_name); return; }
				const std::size_t n = (std::min)(a.size(), b.size());
				int shown = 0, total = 0;
				for (std::size_t o = 0; o + 4 <= n; o += 4) {
					std::uint32_t x = 0, y = 0;
					std::memcpy(&x, &a[o], 4);
					std::memcpy(&y, &b[o], 4);
					if (x == y) continue;
					++total;
					if (shown++ < 300) {
						float fx, fy;
						std::memcpy(&fx, &x, 4);
						std::memcpy(&fy, &y, 4);
						logger::info("RawDiff[{}] +0x{:X}: {:08X} -> {:08X}   (as float {:.3f} -> {:.3f})", a_name, o, x, y, fx, fy);
					}
				}
				logger::info("RawDiff[{}]: {} words differ", a_name, total);
			};
			diff("player", a_before.player, a_after.player);
			diff("controller", a_before.ctrl, a_after.ctrl);
		}

		// The player's Havok character controller: what it thinks it is doing (on ground / jumping / in air / swimming),
		// its flags, the speed it was told and the push of its last step. Logged before boarding and after exit to compare.
		void DumpController(const char* a_tag)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* ctrl = player ? player->GetCharController() : nullptr;
			if (!ctrl) { logger::info("Controller [{}]: none", a_tag); return; }
			auto vec = [](const RE::hkVector4& v) {
				alignas(16) float f[4];
				_mm_store_ps(f, v.quad);
				return std::format("({:.1f},{:.1f},{:.1f})", f[0], f[1], f[2]);
			};
			logger::info("Controller [{}]: state={} prev={} want={} flags=0x{:08X} speedPct={:.2f} gravity={:.2f} jumpHeight={:.1f} acro={:.2f} velTime={:.3f} support={} bumped={} direction={} outVel={} initVel={} velMod={} filter={} maxVel={:.1f}",
				a_tag, static_cast<int>(ctrl->context.currentState), static_cast<int>(ctrl->context.previousState), static_cast<int>(ctrl->wantState),
				static_cast<std::uint32_t>(ctrl->flags.underlying()), ctrl->speedPct, ctrl->gravity, ctrl->jumpHeight, ctrl->acrobatics, ctrl->velocityTime,
				*reinterpret_cast<void* const*>(&ctrl->supportBody) ? 1 : 0, *reinterpret_cast<void* const*>(&ctrl->bumpedBody) ? 1 : 0, vec(ctrl->direction), vec(ctrl->outVelocity), vec(ctrl->initialVelocity), vec(ctrl->velocityMod),
				ctrl->context.filterEnable ? 1 : 0, ctrl->context.maxLinearVelocity);
		}

		// The game's input handlers (movement, look, jump, sneak ...) each have an "enabled" switch of their own that is
		// separate from the control-map flags. Logs them and the movement input the game sees, and switches back on any that
		// are off (a dead movement handler would explain WASD not working while jump still does).
		void CheckInputHandlers(const char* a_tag, bool a_fix)
		{
			auto* pc = RE::PlayerControls::GetSingleton();
			if (!pc) return;
			std::string line;
			int idx = 0;
			for (auto* h : pc->handlers) {
				if (h) {
					line += std::format(" [{}]={}", idx, h->inputEventHandlingEnabled ? 1 : 0);
					if (a_fix && !h->inputEventHandlingEnabled) {
						h->inputEventHandlingEnabled = true;
						line += "(fixed)";
					}
				}
				++idx;
			}
			logger::info("Handlers [{}]:{} moveInput=({:.2f},{:.2f})", a_tag, line, pc->data.moveInputVec.x, pc->data.moveInputVec.y);
		}

		// The sitting camera (9) or the bleedout camera (11) left behind after getting out keeps WASD dead (jump still works).
		// Put the camera back to the normal first/third person view whenever it is in one of those.
		void RestoreCamera(const char* a_why)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!cam || !cam->currentState) return;
			const int id = static_cast<int>(cam->currentState->id);
			// by number: 9 = sitting camera, 10 = mount, 11 = bleedout
			if (id != 9 && id != 10 && id != 11) return;
			// ForceThirdPerson is ignored while the sitting camera is active (log: stayed in state 9), so switch the state
			// directly: that ends the sitting camera properly and begins the normal one.
			auto target = cam->cameraStates[exitWasFirst_ ? RE::CameraState::kFirstPerson : RE::CameraState::kThirdPerson];
			if (target) cam->SetState(target.get());
			SyncBodyFlagToCamera(RE::PlayerCharacter::GetSingleton(), false);  // 55.2.3: right after the switch
			const int after = cam->currentState ? static_cast<int>(cam->currentState->id) : -1;
			logger::info("Camera: was in state {} ({}), switched to {} person, now state {}", id, a_why, exitWasFirst_ ? "first" : "third", after);
		}

		void PostExitStand(RE::PlayerCharacter* a_player, float a_delta)
		{
			if (playerSkim_) { standTimer_ = 0.0f; return; }
			if (standTimer_ <= 0.0f) return;
			standTimer_ -= a_delta;
			if ((standTick_ += a_delta) < 0.25f) return;
			standTick_ = 0.0f;
			if (getUpPending_) {
				getUpElapsed_ += 0.25f;
				auto* gst = a_player->AsActorState();
				bool gSit = false;
				a_player->GetGraphVariableBool("bIsSitting", gSit);
				const int gss = gst ? static_cast<int>(gst->actorState1.sitSleepState) : -1;
				logger::info("GetUp: {:.2f}s sitSleep={} bIsSitting={}", getUpElapsed_, gss, gSit);
				const bool done = !gSit && gss == 0;
				if (!done && getUpElapsed_ >= 0.75f && getUpStep_ == 0) {
					getUpStep_ = 1;
					const bool ok = a_player->NotifyAnimationGraph("IdleFurnitureExit");
					logger::info("GetUp: still sitting, sent IdleFurnitureExit ({})", ok ? "accepted" : "rejected");
				}
				if (done || getUpElapsed_ >= 3.0f) {
					if (!done) {
						logger::info("GetUp: did not finish, forcing the stand state");
						a_player->NotifyAnimationGraph("IdleForceDefaultState");
						if (gst) gst->actorState1.sitSleepState = RE::SIT_SLEEP_STATE::kNormal;
						a_player->SetGraphVariableBool("bIsSitting", false);
						a_player->SetGraphVariableBool("isIdleSitting", false);
					} else {
						logger::info("GetUp: finished after {:.2f}s", getUpElapsed_);
					}
					getUpPending_ = false;
					a_player->NotifyAnimationGraph("sneakStop");
					standLogs_ = 0;
				}
				SetSkimControls(true, true);
				return;
			}
			a_player->SetGraphVariableBool("bAllowRotation", savedAllowRot_);
			a_player->SetGraphVariableBool("bHumanoidFootIKDisable", savedFootIKOff_);
			if (kDebugDumps) {
				CheckInputHandlers(standLogs_ < 3 ? "after exit" : "after exit again", false);
				DumpController(standLogs_ < 3 ? "after exit" : "after exit again");
				if (standLogs_ == 8) { DumpAllGraphVars(a_player, "after"); DiffRaw(snapBefore_, CaptureRaw()); }
			}
			if (standLogs_ == 0 || standLogs_ == 8) {
				DumpWalkState(a_player, standLogs_ == 0 ? "just after exit" : "2 s after exit");
				for (const char* v : { "bAnimationDriven", "isInFurniture", "bOffsetGPMA" }) a_player->SetGraphVariableBool(v, false);
			}
			auto* st = a_player->AsActorState();
			bool sitVar = false;
			a_player->GetGraphVariableBool("bIsSitting", sitVar);
			const int ss = st ? static_cast<int>(st->actorState1.sitSleepState) : -1;
			if (standLogs_++ < 16) logger::info("PostExit: sitSleep={} bIsSitting={} sneaking={}", ss, sitVar, a_player->IsSneaking());
			if (st && st->actorState1.sitSleepState != RE::SIT_SLEEP_STATE::kNormal) st->actorState1.sitSleepState = RE::SIT_SLEEP_STATE::kNormal;
			if (sitVar || ss != 0) {
				a_player->NotifyAnimationGraph("IdleFurnitureExit");
				a_player->NotifyAnimationGraph("IdleForceDefaultState");
			}
			SetSkimControls(true, true);
		}

		// Rowing animation (Part 16: our OarsRowStart loop, OffsetGPMA only as the fallback): plays while W is held, back to
		// the plain seated idle when it is released. a_exit: the player is getting out, so no re-seat is asked for.
		void StopRowAnim(RE::PlayerCharacter* a_player, bool a_exit = false)
		{
			if (rowAnimOn_ && a_player) {
				auto rower = RowActor(a_player);
				const bool ok = CrewNotify(rower.get(), rowAnimGpma_ ? "OffsetGPMAStop" : kRowStopEvent, "row stop", false);
				logger::info("Row anim: stopped on {} {:08X} ({} {})", rower->GetName(), rower->GetFormID(), rowAnimGpma_ ? "OffsetGPMAStop" : kRowStopEvent, ok ? "accepted" : "rejected");
				if (!rowAnimGpma_) {
					// Our loop replaced the seat (the GPMA offset ran on top of it): sit down again, at once.
					if (rower.get() == a_player) {
						sitAccepted_ = false;  // a_exit: the exit then takes the plain stand-up path, the chair get-up would be refused
						sitTries_ = 0;
						sitMiss_ = 0;
						crouchTimer_ = 1.0f;  // HoldPlayerCrouch re-seats (or re-crouches in first person) on its next frame
					} else if (!a_exit) {
						std::lock_guard lock(crewIdleMx_);
						if (auto* st = CrewIdleFor(rower->GetFormID())) {
							st->seated = false;      // no STOOD UP line: we stood them up
							st->unseatedAt = rideTime_;
							st->resitFrames = 0.5f;  // IdleChairEnterInstant every frame for 0.5 s (TendCrewHuman)
						}
						// 56.19.4: checked 0.3 s on: forward still held on the keyboard means the game dropped our key state.
						rowRestartId_ = rower->GetFormID();
						rowRestartAt_ = rideTime_ + 0.3f;
						rowRestartTries_ = 0;
					}
				}
				lastRowStopAt_ = rideTime_;
			}
			rowAnimOn_ = false;
			rowAnimGpma_ = false;
			rowStartPending_ = false;
			rowAnimOffTimer_ = 0.0f;
		}

		void UpdateRowAnim(RE::PlayerCharacter* a_player, float a_dt)
		{
			if (!a_player) return;
			// 18.1: a menu (the console too) pauses the row timers; nothing is sent until 0.5 s after it closes.
			if (auto* ui = RE::UI::GetSingleton(); ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME))) {
				rowMenuHold_ = 0.5f;
				return;
			}
			if (rowMenuHold_ > 0.0f) { rowMenuHold_ -= a_dt; return; }
			TendRowRestart();
			// Follows W only (like the oars). Stopping it in the air or on a slope made the seat drop for a moment.
			const bool want = rowWantAnim_ && playerSkim_ && rowerAnimOn_;
			if (want) {
				rowAnimOffTimer_ = 0.0f;
				if (!rowAnimOn_) {
					if (!rowStartPending_) {
						BoatBow::RestartStroke();  // oars and body both start at the catch
						rowRequestAt_ = rideTime_;
						rowSyncLogged_ = false;
						auto rower = RowActor(a_player);
						if (rower.get() != a_player) CancelSeatedIdle(rower.get());
						rowStartPending_ = true;
						rowRetryTries_ = 0;
						rowRetryWait_ = 0.0f;
						// 56.8.B: short W taps keep the waiting time (it only starts again after 3 s without W), so the
						// 2 s "try anyway" is reached.
						if (rideTime_ - lastRowWantAt_ > 3.0f) rowPendingFor_ = 0.0f;
					}
					lastRowWantAt_ = rideTime_;
					TryRowStart(a_player, a_dt);
				}
			} else {
				rowStartPending_ = false;
				if (rowAnimOn_) {
					rowAnimOffTimer_ += a_dt;
					if (rowAnimOffTimer_ >= kRowStopDebounce) StopRowAnim(a_player);  // debounce so short W taps do not stop and restart it
				}
			}
		}

		// 56.19.4: the forward key, read from the keyboard itself (our key state follows input events, and a cell crossing
		// can lose the release/press pair while W stays down).
		bool ForwardKeyHeld() const
		{
			UINT sc = keyFwd_ & 0xFF;
			if (sc & 0x80) sc = 0xE000 | (sc & 0x7F);  // DirectInput extended keys (arrows etc.)
			const UINT vk = MapVirtualKeyA(sc, MAPVK_VSC_TO_VK_EX);
			return vk && (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
		}

		// 56.19.4: after the crew rower's clip was stopped, while the boat is still meant to be rowed (W held on the
		// keyboard although our state says released): the key state is put back, so the clip starts again through the
		// normal path. 0.3 s after the stop, then up to 3 times 1 s apart. A key state we put back is cleared again the
		// moment W is really up.
		void TendRowRestart()
		{
			if (fwdForced_ && !ForwardKeyHeld()) {
				fwdForced_ = false;
				inFwd_ = false;
			}
			if (!rowRestartId_) return;
			if (!playerSkim_ || rowAnimOn_ || !rowMode_) {
				rowRestartId_ = 0;
				rowRestartFinal_ = false;
				return;
			}
			if (rideTime_ < rowRestartAt_) return;
			if (rowRestartFinal_) {
				// 56.23.3: three restarts and still no clip: why.
				rowRestartFinal_ = false;
				LogRowRestartFailed(rowRestartId_);
				rowRestartId_ = 0;
				return;
			}
			auto* who = RE::TESForm::LookupByID<RE::Actor>(rowRestartId_);
			const char* name = who ? who->GetName() : "?";
			if (!ForwardKeyHeld() && std::abs(stickY_) <= 0.3f) {
				logger::info("Row restart: {} not needed (forward not held, the stop was a release)", name);
				rowRestartId_ = 0;
				return;
			}
			++rowRestartTries_;
			if (!inFwd_ && ForwardKeyHeld()) {
				inFwd_ = true;
				fwdForced_ = true;
			}
			logger::info("Row restart: {} after package change ({}/3) (forward held, our key state was {})", name, rowRestartTries_, fwdForced_ ? "released: put back" : "held");
			if (rowRestartTries_ >= 3) {
				rowRestartFinal_ = true;  // one more look 1 s on: accepted by then, or logged why not
				rowRestartAt_ = rideTime_ + 1.0f;
			}
			else rowRestartAt_ = rideTime_ + 1.0f;
		}

		void LogRowRestartFailed(RE::FormID a_id)
		{
			auto* who = RE::TESForm::LookupByID<RE::Actor>(a_id);
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto rowerNow = player ? RowActor(player) : RE::NiPointer<RE::Actor>{};
			std::string crew = "no crew state";
			{
				std::lock_guard lock(crewIdleMx_);
				if (auto* st = CrewIdleFor(a_id))
					crew = std::format("seated {} reseat {} resitStep {} stoodPending {} boarded {:.1f} s ago", st->seated, st->reseatActive, st->resitStep, st->stoodPending,
						rideTime_ - st->boardedAt);
			}
			auto* as = who ? who->AsActorState() : nullptr;
			const float sinceRespawn = BoatBow::SecondsSinceRespawn();
			logger::info("Row restart: {} 3/3 ended without an accepted OarsRowStart | crew: {} | sitSleep {} | rower now {} | rowStartPending {} retries {} pending {:.1f} s | "
						 "W ours {} keyboard {} | respawn in the last 5 s: {}",
				who ? who->GetName() : "?", crew, as ? static_cast<int>(as->actorState1.sitSleepState) : -1, rowerNow ? rowerNow->GetName() : "none", rowStartPending_,
				rowRetryTries_, rowPendingFor_, inFwd_, ForwardKeyHeld(), sinceRespawn < 5.0f ? std::format("yes ({:.1f} s ago)", sinceRespawn) : std::string("no"));
		}

		// 18.1: one row start attempt, when the rower is ready for it (0.3 s after a stop and after sitting down).
		void TryRowStart(RE::PlayerCharacter* a_player, float a_dt)
		{
			auto rower = RowActor(a_player);
			if (!rower) return;
			rowPendingFor_ += a_dt;
			if ((rowRetryWait_ -= a_dt) > 0.0f) return;
			const RE::FormID id = rower->GetFormID();
			const bool isPlayer = rower.get() == a_player;
			if (rideTime_ - lastRowStopAt_ < 0.3f) return;
			if (isPlayer) {
				if (rowPendingFor_ < 2.0f && rideTime_ - playerSitAt_ < 0.3f) return;  // 0.3 s after the sit; after 2 s, try anyway
			} else {
				// 56.8.B: a crew rower is not waited on to be seated (Faendal stood up at boarding, the re-sit was refused
				// and the clip started 20 s later): 0.3 s after boarding (and after the last row stop, above) is enough.
				// Part 33's re-seat still holds the clip back, but for 1.5 s at most.
				std::lock_guard lock(crewIdleMx_);
				if (auto* st = CrewIdleFor(id)) {
					if (rideTime_ - st->boardedAt < 0.3f) return;
					if (st->reseatActive && rideTime_ - st->reseatStart < 1.5f) return;
				}
			}
			const char* who = isPlayer ? "player" : "crew";
			auto* cam = RE::PlayerCamera::GetSingleton();
			const std::pair<RE::FormID, bool> key{ id, isPlayer && cam && cam->IsInFirstPerson() };
			if (key.second) who = "player, first person";
			auto streak = std::find_if(rowRejectStreak_.begin(), rowRejectStreak_.end(), [&](const auto& e) { return e.first == id; });
			if (streak == rowRejectStreak_.end()) streak = rowRejectStreak_.insert(rowRejectStreak_.end(), { id, 0 });
			bool gpma = std::find(rowAnimFallback_.begin(), rowAnimFallback_.end(), key) != rowAnimFallback_.end();
			if (!gpma) {
				if (isPlayer) StoreCamBaseline(a_player);  // 21.4: the camera bones before our clip touches them
				const bool ok = CrewNotify(rower.get(), kRowStartEvent, "row start", false);
				if (ok) {
					if (isPlayer) camBoneLogAt_ = { rideTime_ + 1.0f, rideTime_ + 3.0f };  // 20.3
					if (rowRetryTries_ > 0) logger::info("Row anim: retry ok after {} tries on {} ({})", rowRetryTries_, rower->GetName(), who);
					logger::info("Row anim: {} accepted on {} ({})", kRowStartEvent, rower->GetName(), who);
					rowEverAccepted_ = true;
					streak->second = 0;
				} else {
					++rowRetryTries_;
					++streak->second;
					rowRetryWait_ = 0.4f;
					if (rowRetryTries_ == 1) logger::info("Row anim: {} rejected on {} ({}), retrying every 0.4 s", kRowStartEvent, rower->GetName(), who);
					if (rowEverAccepted_ || streak->second < 10) return;
					// Safety only: never accepted this session and refused 10 times: Nemesis/Pandora not run, or the list not in.
					gpma = true;
					rowAnimFallback_.push_back(key);
					logger::warn("Row anim: {} rejected on {} ({}) 10 times and never accepted this session, using OffsetGPMA fallback (run Nemesis/Pandora and enable \"Oars and Sails\")", kRowStartEvent, rower->GetName(), who);
				}
			}
			if (gpma) {
				const bool ok = CrewNotify(rower.get(), "OffsetGPMA", "row start (fallback)", false);
				logger::info("Row anim: OffsetGPMA fallback {} on {} ({})", ok ? "accepted" : "rejected", rower->GetName(), who);
			}
			rowAnimOn_ = true;
			rowAnimOnAt_ = rideTime_;
			BoatBow::RestartStroke();  // 35.3: the oars start at the catch exactly when the clip does
			rowAnimGpma_ = gpma;
			rowStartPending_ = false;
			if (std::find(rowedThisRide_.begin(), rowedThisRide_.end(), id) == rowedThisRide_.end()) rowedThisRide_.push_back(id);
		}

		void ExitPlayerSkim(const char* a_why)
		{
			if (!playerSkim_) return;
			CancelHop();
			BoatBow::SetPickAllowed(true);  // v1.0.14: on foot again, the benches and chest can be looked at
			SuppressGreetings(false);  // B7: greetings back exactly as they were
			lateDwell_.clear();
			tempSeats_.clear();
			StopRowAnim(RE::PlayerCharacter::GetSingleton(), true);
			if (auto* c0 = RE::PlayerCamera::GetSingleton()) exitWasFirst_ = c0->IsInFirstPerson();
			BoatBow::TendBoatBakedOars(nullptr, false, 0.0f, "exit");  // 56: no oar left on a bone after the exit
			lockMode_ = LockMode::kNone;  // 56.5
			LogPadEvents();  // 55.1 diagnostic, once per boarding
			playerSkim_ = false;
			SetSkimControls(true, true);
			inFwd_ = inBack_ = inLeft_ = inRight_ = false;
			activatePressed_ = false;
			const bool wasBoarded = boardedParked_;
			boardedParked_ = false;
			const RE::NiPoint3 vehicle = vehiclePos_;
			const float heading = skimHeading_;
			ReleasePassengers(vehicle, heading);
			WatchAfterRelease(RE::PlayerCharacter::GetSingleton());  // 17.2: the player's events too, and the stray guard
			BoatBow::Detach(wasBoarded);  // removes the skim boat (56.28: hull kept until the parked boat shows, props carried over)
			BoatBow::SweepOrphans(true, "unboard");  // 43.4.3
			logger::info("Chest: {} (unboard)", BoatBow::ChestSummary());  // 43.4.5
			ReequipPlayer();  // 43.1.4
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				player->SetCollision(true);
				SetActorHeadTracking(player, savedHeadTrack_);
				player->SetGraphVariableBool("bAllowRotation", savedAllowRot_);
				player->SetGraphVariableBool("bHumanoidFootIKDisable", savedFootIKOff_);
				{
					// Always end the rowing animation on the way out, whether or not W was held, and clear its graph flag,
					// so nothing from it can keep walking (WASD) locked after getting out. Part 16: the GPMA stop only matters
					// when the fallback ran; our own loop was ended by StopRowAnim above (the stand-up below ends it anyway).
					if (std::any_of(rowAnimFallback_.begin(), rowAnimFallback_.end(), [&](const auto& f) { return f.first == player->GetFormID(); })) {
						const bool stopOk = player->NotifyAnimationGraph("OffsetGPMAStop");
						logger::info("Exit: OffsetGPMAStop (fallback) {}", stopOk ? "accepted" : "rejected");
					}
					player->SetGraphVariableBool("bOffsetGPMA", false);
				}
				MaskSeatForFreeLook(player, false);  // 56.46: the get-up needs the real sit state
				const bool exitSeated = playerSit_ && sitAccepted_;
				logger::info("Exit: state was {}", exitSeated ? "seated" : "crouch-fp or rowing");
				if (exitSeated) {
					// Play the game's own get-up-from-chair first and let it finish (PostExitStand watches it). Crouch is
					// released afterwards, and nothing is forced unless the get-up never completes.
					bool ok = player->NotifyAnimationGraph("IdleChairExitStart");
					logger::info("PlayerSit exit: 'IdleChairExitStart' {}", ok ? "accepted" : "rejected");
					if (!ok) {
						ok = player->NotifyAnimationGraph("IdleFurnitureExit");
						logger::info("PlayerSit exit: 'IdleFurnitureExit' {}", ok ? "accepted" : "rejected");
					}
					if (auto* st = player->AsActorState())
						logger::info("PlayerSit exit: sit/sleep state was {}", static_cast<int>(st->actorState1.sitSleepState));
					getUpPending_ = true;
					getUpElapsed_ = 0.0f;
					getUpStep_ = 0;
					standTimer_ = 6.0f;
					standTick_ = 0.0f;
					standLogs_ = 0;
				} else {
					// v1.0.15 (B8): the first person crouch (or anything in between) gets the same stand-up: out of sneak, out of
					// any sit / furniture state, and the PostExitStand checks (WalkState, PostExit lines) run as for a seated exit.
					for (const char* ev : { "sneakStop", "IdleFurnitureExit", "IdleForceDefaultState" }) {
						const bool ok = player->NotifyAnimationGraph(ev);
						logger::info("Exit (crouch-fp): '{}' {}", ev, ok ? "accepted" : "rejected");
					}
					if (auto* st = player->AsActorState()) st->actorState1.sitSleepState = RE::SIT_SLEEP_STATE::kNormal;
					for (const char* v : { "bIsSitting", "isIdleSitting", "isInFurniture", "bAnimationDriven" }) player->SetGraphVariableBool(v, false);
					getUpPending_ = false;
					standTimer_ = 6.0f;
					standTick_ = 0.0f;
					standLogs_ = 0;
				}
				ExitBodyFlag(player);  // 55.2
				RE::NiPoint3 pos = player->GetPosition();
				float w = 0.0f;
				float land = 0.0f;
				auto* tes = RE::TES::GetSingleton();
				if (wasBoarded) {
					// Step out into the middle of the boat (which is parked again just below).
					float cx = 0.0f, cy = 0.0f;
					BoatBow::BoatCenterFromVehicle(vehicle.x, vehicle.y, heading, cx, cy);
					(void)cx; (void)cy;
					// Step off where he was sitting (his seat), not the middle of the boat where the rower now sits.
					pos.x = vehicle.x + std::sin(heading) * playerBodyFwd_ + std::cos(heading) * playerBodySide_;
					pos.y = vehicle.y + std::cos(heading) * playerBodyFwd_ - std::sin(heading) * playerBodySide_;
					pos.z = SkimWaterAt(RE::NiPoint3{ cx, cy, vehicle.z }, w) ? w + kBoatExitDrop + 30.0f + boatHeight_ : vehicle.z;  // set down a little above the deck, he drops onto it
				} else if (SkimWaterAt(pos, w)) {
					pos.z = w + 2.0f;
				} else if (tes && tes->GetLandHeight(pos, land) && std::isfinite(land)) {
					pos.z = land + 2.0f;
				}
				logger::info("Move: player off the boat (exit)");  // crash breadcrumb (v1.0.13)
				player->SetPosition(pos, true);
				player->Update3DPosition(true);  // drop any skeleton override (tilt/seat) left on the body
				RE::NiPoint3 ang = player->GetAngle();
				ang.x = 0.0f;  // no pitch left over from the boat
				player->SetAngle(ang);
				if (wasBoarded) {
					// B8: the parked boat that is spawned below needs a few frames before its deck is solid. Until then the
					// player is held on the deck spot (collision off, placed every frame), or a moving exit drops into the water.
					player->SetCollision(false);
					exitHold_ = 1.5f;
					exitPos_ = pos;
					exitCheck_ = 1.0f;
				}
				float wz = 0.0f;
				const bool overWater = SkimWaterAt(pos, wz);
				const auto* pst = player->AsActorState();
				logger::info("Exit: speed {:.0f}, player at ({:.0f},{:.0f},{:.0f}), water z {}, swimming {}, placed on {}", skimSpeed_, pos.x, pos.y, pos.z,
					overWater ? std::format("{:.0f}", wz) : std::string("none"), pst && pst->IsSwimming() ? "yes" : "no", wasBoarded ? "deck" : "the spot below");
			}
			if (wasBoarded) {
				// Leave the boat parked right here, solid again.
				float bx = 0.0f, by = 0.0f;
				BoatBow::BoatCenterFromVehicle(vehicle.x, vehicle.y, heading, bx, by);
				float w = 0.0f;
				const float z = SkimWaterAt(RE::NiPoint3{ bx, by, vehicle.z }, w) ? w + kParkedBoatZ + boatHeight_ : vehicle.z - kSkimEyeHeight;
				BoatBow::SpawnParked(bx, by, z, heading, true);  // 56.28: keep the carried-over props
			}
			ShowHUD();
			if (!physicsCmd_.empty()) physicsDelay_ = 1.5f;
			skimSpeed_ = 0.0f;
			stuckWindow_ = wasBoarded ? 20.0f : 0.0f;
			stuckTimer_ = 0.0f;
			peaceLinger_ = kPeaceLingerSeconds;  // the crew gets off in peace
			logger::info("SkimPlayer OFF ({}) boat left parked={}", a_why, wasBoarded);
		}

		// Save load / new game: just drop our state; don't touch the old player.
		void ForceReset()
		{
			boardedParked_ = false;
			boardPrompted_ = false;
			BoatBow::SetPickAllowed(true);
			SuppressGreetings(false);
			sheatheWatch_.clear();
			sheatheVerify_.clear();
			releases_.clear();  // a load drops the old world's actors; nothing to finish
			sheatheLoadTimer_ = 10.0f;  // B1 / B10: 4 s and 10 s after the load
			exitHold_ = 0.0f;
			activatePressed_ = false;
			passengers_.clear();
			ClearCrewIdle();
			hopActive_ = false;
			lateDwell_.clear();
			tempSeats_.clear();
			rowAnimOn_ = false;
			ClearStrayWatch();  // 17.2: a load ends the post-release watch
			rowedThisRide_.clear();
			rowWantAnim_ = false;
			heldSave_.reset();  // 56.26: a save held or skipped in the old game goes with it
			heldAutosave_.reset();
			saveExitPending_ = false;
			saving_ = false;
			if (!playerSkim_) return;
			playerSkim_ = false;
			SetSkimControls(true, true);
			BoatBow::Detach();
			logger::info("SkimPlayer: reset by game load");
		}

		bool ConsumeActivate()
		{
			const bool fresh = activatePressed_ &&
				(std::chrono::steady_clock::now() - activateStamp_) < std::chrono::milliseconds(300);
			activatePressed_ = false;
			return fresh;
		}

		// ---- 56.51: docks. Boat spots placed by hand (oas "dock add"), shipped in OarsAndSails_Docks.ini. There is one boat in
		// this mod (the parked boat), so a dock "calls" it: walk near a dock while your boat is somewhere far away and it is
		// waiting at the dock, facing the way it was placed. A boat you left near you is never taken. ----
		struct Dock { RE::FormID world{ 0 }; float x{ 0 }, y{ 0 }, z{ 0 }, yaw{ 0 }; std::string name; };
		std::vector<Dock> docks_;
		bool docksLoaded_{ false };
		bool docksOn_{ true };
		std::chrono::steady_clock::time_point dockTick_{};
		int dockServed_{ -1 };  // the dock the boat was last brought to (not brought again until the player has left it)
		static constexpr float kDockCallRange = 2500.0f;   // the player this close to a dock (and its cell loaded)
		static constexpr float kDockOwnBoatRange = 3000.0f; // your boat this close to you: it stays where you left it
		static constexpr float kDockAtSpot = 300.0f;        // the boat this close to the dock counts as there

		static std::string DockIniPath()
		{
			std::string p = FlowIniPath();
			const auto slash = p.find_last_of("\\/");
			return (slash == std::string::npos ? std::string() : p.substr(0, slash + 1)) + "OarsAndSails_Docks.ini";
		}

		void LoadDocks()
		{
			docksLoaded_ = true;
			docks_.clear();
			const std::string path = DockIniPath();
			docksOn_ = GetPrivateProfileIntA("Docks", "Enabled", 1, path.c_str()) != 0;
			const int n = GetPrivateProfileIntA("Docks", "Count", 0, path.c_str());
			for (int i = 1; i <= n; ++i) {
				const std::string sec = std::format("Dock{}", i);
				char w[32]{}, x[32]{}, y[32]{}, z[32]{}, a[32]{}, nm[128]{};
				GetPrivateProfileStringA(sec.c_str(), "World", "", w, sizeof(w), path.c_str());
				GetPrivateProfileStringA(sec.c_str(), "X", "", x, sizeof(x), path.c_str());
				GetPrivateProfileStringA(sec.c_str(), "Y", "", y, sizeof(y), path.c_str());
				GetPrivateProfileStringA(sec.c_str(), "Z", "", z, sizeof(z), path.c_str());
				GetPrivateProfileStringA(sec.c_str(), "Yaw", "", a, sizeof(a), path.c_str());
				GetPrivateProfileStringA(sec.c_str(), "Name", "", nm, sizeof(nm), path.c_str());
				if (!w[0] || !x[0] || !y[0]) continue;
				try {
					Dock d;
					d.world = static_cast<RE::FormID>(std::stoul(w, nullptr, 16));
					d.x = std::stof(x); d.y = std::stof(y); d.z = z[0] ? std::stof(z) : 0.0f; d.yaw = a[0] ? std::stof(a) : 0.0f;
					d.name = nm[0] ? nm : sec;
					docks_.push_back(std::move(d));
				} catch (...) { logger::warn("Docks: {} unreadable, skipped", sec); }
			}
			logger::info("Docks: {} loaded from {} ({})", docks_.size(), path, docksOn_ ? "on" : "off");
		}

		void SaveDocks()
		{
			const std::string path = DockIniPath();
			// Old sections past the new count would linger: blank them first.
			const int old = GetPrivateProfileIntA("Docks", "Count", 0, path.c_str());
			for (int i = 1; i <= old; ++i) WritePrivateProfileStringA(std::format("Dock{}", i).c_str(), nullptr, nullptr, path.c_str());
			WritePrivateProfileStringA("Docks", "Count", std::to_string(docks_.size()).c_str(), path.c_str());
			WritePrivateProfileStringA("Docks", "Enabled", docksOn_ ? "1" : "0", path.c_str());
			for (std::size_t i = 0; i < docks_.size(); ++i) {
				const auto& d = docks_[i];
				const std::string sec = std::format("Dock{}", i + 1);
				WritePrivateProfileStringA(sec.c_str(), "Name", d.name.c_str(), path.c_str());
				WritePrivateProfileStringA(sec.c_str(), "World", std::format("{:08X}", d.world).c_str(), path.c_str());
				WritePrivateProfileStringA(sec.c_str(), "X", std::format("{:.1f}", d.x).c_str(), path.c_str());
				WritePrivateProfileStringA(sec.c_str(), "Y", std::format("{:.1f}", d.y).c_str(), path.c_str());
				WritePrivateProfileStringA(sec.c_str(), "Z", std::format("{:.1f}", d.z).c_str(), path.c_str());
				WritePrivateProfileStringA(sec.c_str(), "Yaw", std::format("{:.4f}", d.yaw).c_str(), path.c_str());
			}
		}

		static RE::FormID WorldOf(RE::PlayerCharacter* a_player)
		{
			auto* ws = a_player ? a_player->GetWorldspace() : nullptr;
			return ws ? ws->GetFormID() : 0u;
		}

		// oas "dock ..." console text.
		std::string DockCommand(const std::string& a_args)
		{
			if (!docksLoaded_) LoadDocks();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return "no player";
			std::istringstream in(a_args);
			std::string verb;
			in >> verb;
			std::string rest;
			std::getline(in, rest);
			while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
			const RE::NiPoint3 pp = player->GetPosition();
			const RE::FormID world = WorldOf(player);
			auto nearest = [&](float& a_dist) {
				int best = -1;
				a_dist = 1e30f;
				for (int i = 0; i < static_cast<int>(docks_.size()); ++i) {
					const auto& d = docks_[i];
					if (d.world != world) continue;
					const float dd = std::hypot(d.x - pp.x, d.y - pp.y);
					if (dd < a_dist) { a_dist = dd; best = i; }
				}
				return best;
			};
			if (verb == "add") {
				if (playerSkim_) return "get off the boat first (the dock takes the parked boat's spot)";
				if (!world) return "docks work outdoors only";
				float bx = 0, by = 0, bz = 0, byaw = 0;
				const bool have = BoatBow::GetParked(bx, by, bz, byaw) && std::hypot(bx - pp.x, by - pp.y) < 1500.0f;
				if (!have) {
					// No boat beside you: place one ahead (as oas boat does) and save that.
					if (!SpawnParkedBoat() || !BoatBow::GetParked(bx, by, bz, byaw)) return "no boat near you and no loaded water ahead to place one";
				}
				Dock d{ world, bx, by, bz, byaw, rest.empty() ? std::format("Dock{}", docks_.size() + 1) : rest };
				docks_.push_back(d);
				SaveDocks();
				dockServed_ = static_cast<int>(docks_.size()) - 1;
				logger::info("Docks: added '{}' world {:08X} at ({:.0f}, {:.0f}, {:.0f}) facing {:.0f} deg", d.name, d.world, d.x, d.y, d.z, d.yaw * 57.2958f);
				return std::format("dock {} '{}' saved where the boat is, facing {:.0f} deg ({} docks)", docks_.size(), d.name, d.yaw * 57.2958f, docks_.size());
			}
			if (verb == "del" || verb == "delete" || verb == "remove") {
				float dist = 0;
				const int i = nearest(dist);
				if (i < 0 || dist > 2000.0f) return "no dock within 2000 of you";
				const std::string name = docks_[i].name;
				docks_.erase(docks_.begin() + i);
				SaveDocks();
				dockServed_ = -1;
				logger::info("Docks: removed '{}'", name);
				return std::format("dock '{}' removed ({} left)", name, docks_.size());
			}
			if (verb == "list" || verb.empty()) {
				auto* console = RE::ConsoleLog::GetSingleton();
				for (std::size_t i = 0; i < docks_.size(); ++i) {
					const auto& d = docks_[i];
					const std::string line = d.world == world ? std::format("  {} '{}' {:.0f} away, facing {:.0f} deg", i + 1, d.name, std::hypot(d.x - pp.x, d.y - pp.y), d.yaw * 57.2958f)
					                                          : std::format("  {} '{}' (other worldspace {:08X})", i + 1, d.name, d.world);
					if (console) console->Print(line.c_str());
					logger::info("Docks:{}", line);
				}
				return std::format("{} docks, {} (file {})", docks_.size(), docksOn_ ? "on" : "off", DockIniPath());
			}
			if (verb == "on" || verb == "off") {
				docksOn_ = verb == "on";
				SaveDocks();
				return std::format("docks {}", docksOn_ ? "on" : "off");
			}
			if (verb == "reload") { LoadDocks(); return std::format("{} docks reloaded", docks_.size()); }
			return "usage: oas \"dock add <name>\" | \"dock del\" | \"dock list\" | \"dock on\" / \"dock off\" | \"dock reload\"";
		}

		// Once a second while not boating: bring the boat to a dock the player is near, unless their boat is near them already.
		void TendDocks(RE::PlayerCharacter* a_player)
		{
			const auto now = std::chrono::steady_clock::now();
			if (now - dockTick_ < std::chrono::seconds(1)) return;
			dockTick_ = now;
			if (!docksLoaded_) LoadDocks();
			if (!docksOn_ || docks_.empty() || !a_player || savedPark_.valid) return;  // the save's own boat is resolved first
			const RE::FormID world = WorldOf(a_player);
			if (!world) return;
			auto* cell = a_player->GetParentCell();
			if (!cell || !cell->IsAttached()) return;
			const RE::NiPoint3 pp = a_player->GetPosition();
			int best = -1;
			float bestD = kDockCallRange;
			for (int i = 0; i < static_cast<int>(docks_.size()); ++i) {
				const auto& d = docks_[i];
				if (d.world != world) continue;
				const float dd = std::hypot(d.x - pp.x, d.y - pp.y);
				if (dd < bestD) { bestD = dd; best = i; }
			}
			if (best < 0) {
				if (dockServed_ >= 0) {
					const auto& d = docks_[dockServed_];
					if (d.world != world || std::hypot(d.x - pp.x, d.y - pp.y) > kDockCallRange + 1500.0f) dockServed_ = -1;  // left it
				}
				return;
			}
			if (best == dockServed_) return;
			const auto& d = docks_[best];
			float bx = 0, by = 0, bz = 0, byaw = 0;
			if (BoatBow::GetParked(bx, by, bz, byaw)) {
				if (std::hypot(bx - d.x, by - d.y) < kDockAtSpot) { dockServed_ = best; return; }  // already there
				if (std::hypot(bx - pp.x, by - pp.y) < kDockOwnBoatRange) { dockServed_ = best; return; }  // theirs, left nearby: keep it
			}
			float water = 0.0f;
			const float z = SkimWaterAt(RE::NiPoint3{ d.x, d.y, d.z + 100.0f }, water) ? water + kParkedBoatZ + boatHeight_ : d.z;
			{
				// Each dock has its own colour (blue or beige), picked from its name so the same dock always shows the same boat.
				std::uint32_t h = 2166136261u;
				for (const char c : d.name) h = (h ^ static_cast<std::uint8_t>(c)) * 16777619u;
				const int paint = (h & 1u) ? BoatBow::kPaintBeige : BoatBow::kPaintBlue;
				if (BoatBow::Paint() != paint) BoatBow::SetPaint(paint, "dock colour");
			}
			if (BoatBow::SpawnParked(d.x, d.y, z, d.yaw)) {
				dockServed_ = best;
				logger::info("Docks: the boat is waiting at '{}' ({:.0f} from the player, facing {:.0f} deg)", d.name, bestD, d.yaw * 57.2958f);
			}
		}

		// E: board the parked boat when close to it, or get off when skimming.
		void HandleBoardingInput(RE::PlayerCharacter* a_player)
		{
			const bool pressed = ConsumeActivate();
			if (playerSkim_) {
				if (pressed) ExitPlayerSkim("Activate pressed");
				return;
			}
			TendDocks(a_player);
			float bx = 0.0f, by = 0.0f, bz = 0.0f, byaw = 0.0f;
			if (!BoatBow::GetParked(bx, by, bz, byaw)) {
				// After loading a save our in-memory boat is gone but the boat itself is still in
				// the world: pick up the nearest rowboat so it can be boarded again.
				adoptTimer_ -= 0.016f;
				if (adoptTimer_ <= 0.0f) {
					adoptTimer_ = 1.0f;
					// B9 (v1.0.15): the boat from the co-save first (by its ref; if it is gone, a new one at its saved spot once
					// the player is near it and the cell is loaded), then the nearest of our boats. Never a vanilla one.
					bool got = false;
					if (savedPark_.valid) {
						got = BoatBow::AdoptRef(savedPark_.id);
						const RE::NiPoint3 pp = a_player->GetPosition();
						const float dx = pp.x - savedPark_.x, dy = pp.y - savedPark_.y;
						auto* cell = a_player->GetParentCell();
						const bool sameWorld = (a_player->GetWorldspace() ? a_player->GetWorldspace()->GetFormID() : 0u) == savedPark_.world;
						if (!got && sameWorld && cell && cell->IsAttached() && dx * dx + dy * dy < 3000.0f * 3000.0f && ++savedPark_.tries >= 3) {
							logger::info("BoatBow: the saved parked boat {:08X} is gone, placing a new one at its saved spot", savedPark_.id);
							got = BoatBow::SpawnParked(savedPark_.x, savedPark_.y, savedPark_.z, savedPark_.yaw);
						}
						if (got) { savedPark_.valid = false; BoatBow::CleanupOldBoats(); BoatBow::PurgeStaleRefs("boat adopted"); }
					}
					if (!got) BoatBow::AdoptNearby(a_player->GetPosition(), kAdoptRange);
				}
				if (!BoatBow::GetParked(bx, by, bz, byaw)) {
					boardPrompted_ = false;
					return;
				}
			}
			// v1.0.14: no proximity boarding and no hint any more (14.2). The player looks at the hull, the helm bench or the
			// rower's bench and presses the game's Activate; the activate event (OnPickActivated) leaves the choice here.
			PollPick();
			if (pendingPick_.empty()) return;
			const std::string tag = std::move(pendingPick_);
			pendingPick_.clear();
			// B12 (4): only if the crosshair is on that target this frame, within reach (not a stale pick while running away).
			{
				auto ref = CrosshairRef();
				const char* now = ref ? BoatBow::PickTag(ref.get()) : nullptr;
				const float dist = ref ? ref->GetPosition().GetDistance(a_player->GetPosition()) : 1.0e9f;
				if (!now || tag != now || dist > 400.0f) {
					logger::info("Pick: {} ignored, the crosshair is no longer on it ({}, {:.0f} units)", tag, now ? now : "nothing of ours", dist);
					return;
				}
			}
			if (tag == "Chest") { OpenBoatChest(a_player); return; }
			BoardWith(tag == "HelmBench" ? BoardChoice::kSteer : (tag == "RowBench" ? BoardChoice::kRow : BoardChoice::kDefault));
		}

		// The activate event for one of our objects (main thread, inside the game's activation: act next frame).
		void OnPickActivated(const char* a_tag)
		{
			if (playerSkim_) return;
			// B12: once per press. A target fires at most every 0.5 s, and more than 3 times in 1 s stops it for good.
			const auto now = std::chrono::steady_clock::now();
			auto& g = pickGuard_[a_tag];
			if (now < g.stoppedUntil || now - g.last < std::chrono::milliseconds(500)) return;
			if (now - g.windowStart > std::chrono::seconds(1)) { g.windowStart = now; g.count = 0; }
			if (++g.count > 3) {
				logger::warn("Activate repeat guard: {} (ignored for 5 s)", a_tag);
				g.stoppedUntil = now + std::chrono::seconds(5);
				return;
			}
			g.last = now;
			pendingPick_ = a_tag;
			logger::info("Pick: activated {}", a_tag);
		}
		struct PickGuard { std::chrono::steady_clock::time_point last{}, windowStart{}, stoppedUntil{}; int count{ 0 }; };
		std::unordered_map<std::string, PickGuard> pickGuard_;

		static void ErrorSound()
		{
			RE::PlaySound("UIMenuCancel");
		}

		// Humans who would board with the player now (the same rules and range as BoardPassengers), without boarding them.
		int BoardableHumansNearby(RE::PlayerCharacter* a_player)
		{
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists || !a_player) return 0;
			const auto playerPos = a_player->GetPosition();
			int n = 0;
			for (auto& handle : lists->highActorHandles) {
				auto actor = handle.get();
				if (!actor || actor.get() == a_player || actor->IsDead() || IsDogActor(actor.get())) continue;
				if (actor->GetPosition().GetDistance(playerPos) > 3000.0f) continue;
				if (actor->IsPlayerTeammate() || IsCrew(actor.get())) ++n;
			}
			return n;
		}

		// 14.4: Board (hull) = row alone, or steer when crew are near. Steer needs someone to row. Row is always allowed.
		void BoardWith(BoardChoice a_choice)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			const int nearby = BoardableHumansNearby(player);
			if (a_choice == BoardChoice::kSteer && nearby == 0) {
				logger::info("Steer refused: no rower");
				RE::SendHUDMessage::ShowHUDMessage("Nobody to row. Use the rower bench.");
				ErrorSound();
				return;
			}
			boardChoice_ = a_choice == BoardChoice::kDefault ? (nearby == 0 ? BoardChoice::kRow : BoardChoice::kSteer) : a_choice;
			logger::info("Board: {} ({} crew / follower(s) nearby) -> the player {}", a_choice == BoardChoice::kSteer ? "Steer" : (a_choice == BoardChoice::kRow ? "Row" : "Board"),
				nearby, boardChoice_ == BoardChoice::kRow ? "rows" : "steers");
			if (!BoardParkedBoat()) {
				boardChoice_ = BoardChoice::kDefault;
				// B9 (4): say why, with the numbers, once per press.
				float bx = 0.0f, by = 0.0f, bz = 0.0f, byaw = 0.0f, vx = 0.0f, vy = 0.0f, w = 0.0f;
				const bool parked = BoatBow::GetParked(bx, by, bz, byaw);
				if (parked) BoatBow::VehicleFromBoatCenter(bx, by, byaw, vx, vy);
				const bool water = parked && SkimWaterAt(RE::NiPoint3{ vx, vy, bz }, w);
				auto* cell = player->GetParentCell();
				logger::info("Board: refused, the boat is not in open water: boat ({:.0f},{:.0f},{:.0f}) ref {:08X}, seat point ({:.0f},{:.0f}) water {}, cell {:08X} loaded={}",
					bx, by, bz, BoatBow::ParkedRefId(), vx, vy, water ? std::format("{:.0f}", w) : std::string("none"), cell ? cell->GetFormID() : 0u, cell && cell->IsAttached());
				RE::SendHUDMessage::ShowHUDMessage("The boat is not in open water.");
				ErrorSound();
			}
		}

		// The chest prop's ref is parked underground (its 3D is placed by hand), so its pick proxy opens it for the player.
		void OpenBoatChest(RE::PlayerCharacter* a_player)
		{
			auto* chest = BoatBow::ChestRef();
			if (!chest) { ErrorSound(); return; }
			if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME)) return;  // B12 (3): already open
			logger::info("Chest: opened {:08X} through its pick proxy", chest->GetFormID());
			chest->ActivateRef(a_player, 0, nullptr, 1, false);
		}

		// What the crosshair is on: a Pick: line when it changes (on foot), and `oas pick`.
		static RE::NiPointer<RE::TESObjectREFR> CrosshairRef()
		{
			auto* pick = RE::CrosshairPickData::GetSingleton();
			return pick ? pick->GetActiveTarget().get() : RE::NiPointer<RE::TESObjectREFR>{};
		}

		std::string PickInfo()
		{
			auto ref = CrosshairRef();
			if (!ref) return "Pick: none";
			auto* player = RE::PlayerCharacter::GetSingleton();
			const char* tag = BoatBow::PickTag(ref.get());
			const float dist = player ? player->GetPosition().GetDistance(ref->GetPosition()) : 0.0f;
			auto* root = ref->Get3D();
			return std::format("Pick: '{}' {:08X} tag={} distance={:.0f} layer={}", ref->GetName(), ref->GetFormID(), tag ? tag : "none", dist,
				root ? static_cast<int>(root->GetCollisionLayer()) : -1);
		}

		void PollPick()
		{
			auto ref = CrosshairRef();
			if (ref.get() == lastPickRef_) return;
			lastPickRef_ = ref.get();
			const bool ours = ref && BoatBow::PickTag(ref.get());
			if (ours || lastPickOurs_) logger::info("{}", PickInfo());  // our objects, and leaving one
			lastPickOurs_ = ours;
		}

		// Runs right after PlayerCharacter::Update every frame.
		// Teleport / cell guard (v1.0.8). `coc` while rowing crashed the game: the first frame after the load drove the boat
		// at the old coordinates, put the player back there and respawned the boat into a cell that was not loaded. While
		// riding the player only ever moves where we put them, so a big jump, a new worldspace / interior, a missing 3D or a
		// lost cell (for more than a second; the edge-of-the-world pull-back gets that long) means they were moved away.
		bool TeleportGuard(RE::PlayerCharacter* a_player, float a_delta)
		{
			const RE::NiPoint3 pos = a_player->GetPosition();
			const float dx = pos.x - lastFeet_.x, dy = pos.y - lastFeet_.y;
			const float dist = std::sqrt(dx * dx + dy * dy);
			auto* cell = a_player->GetParentCell();
			cellLostTime_ = cell ? 0.0f : cellLostTime_ + a_delta;
			const char* reason = nullptr;
			if (dist > 3000.0f) reason = "moved far away";
			else if (a_player->GetWorldspace() != boardWorld_) reason = "worldspace changed";
			else if (cell && cell->IsInteriorCell() != boardInterior_) reason = "entered or left an interior";
			else if (!a_player->Get3D()) reason = "player 3D not loaded";
			else if (cellLostTime_ > 1.0f) reason = "player cell lost";
			if (!reason) return false;
			EmergencyEndSkim(reason, dist);
			return true;
		}

		// The same clean-up as getting out, but nothing stale is touched: the boat, props, lamps and lights are forgotten
		// (their cell may be gone), passengers are only reset if they are still loaded, and every ride state is cleared.
		void EmergencyEndSkim(const char* a_reason, float a_dist)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			playerSkim_ = false;
			BoatBow::SetPickAllowed(true);
			SuppressGreetings(false);
			boardedParked_ = false;
			BoatBow::Abandon();
			for (auto& handle : passengers_) {
				auto actor = handle.get();
				auto* cell = actor ? actor->GetParentCell() : nullptr;
				if (!actor || !cell || !cell->IsAttached() || !actor->Is3DLoaded()) continue;
				actor->RemoveAnimationGraphEventSink(&passengerSink_);
				actor->SetCollision(true);
				SetActorDontMove(actor.get(), false);
				CrewNotify(actor.get(), "IdleStop", "emergency end", false);
			}
			passengers_.clear();
			ClearCrewIdle();
			if (hopActive_) {
				hopActive_ = false;
				auto actor = hopActor_.get();
				auto* cell = actor ? actor->GetParentCell() : nullptr;
				if (actor && cell && cell->IsAttached() && actor->Is3DLoaded()) { actor->SetCollision(true); SetActorDontMove(actor.get(), false); }
			}
			lateDwell_.clear();
			tempSeats_.clear();
			SetSkimControls(true, true);
			if (player) {
				player->SetCollision(true);
				if (player->Get3D()) {
					SetActorHeadTracking(player, savedHeadTrack_);
					player->SetGraphVariableBool("bAllowRotation", savedAllowRot_);
					player->SetGraphVariableBool("bHumanoidFootIKDisable", savedFootIKOff_);
					player->NotifyAnimationGraph(rowAnimGpma_ ? "OffsetGPMAStop" : kRowStopEvent);  // Part 16: whichever loop ran
					player->SetGraphVariableBool("bOffsetGPMA", false);
					if (sitAccepted_) { player->NotifyAnimationGraph("IdleChairExitStart"); player->NotifyAnimationGraph("IdleForceDefaultState"); }
					if (auto* st = player->AsActorState()) st->actorState1.sitSleepState = RE::SIT_SLEEP_STATE::kNormal;
					player->NotifyAnimationGraph("sneakStop");
				}
			}
			if (auto* c0 = RE::PlayerCamera::GetSingleton()) exitWasFirst_ = c0->IsInFirstPerson();
			BoatBow::TendBoatBakedOars(nullptr, false, 0.0f, "exit");  // 56
			RestoreCamera("teleport");
			if (player && player->Get3D()) ExitBodyFlag(player);  // 55.2: after the camera switch, before the first rendered frame
			ShowHUD();
			rowAnimOn_ = rowWantAnim_ = false;
			inFwd_ = inBack_ = inLeft_ = inRight_ = false;
			activatePressed_ = false;
			skimSpeed_ = skimReverse_ = skimVerticalSpeed_ = skimYawRate_ = 0.0f;
			skimAirborne_ = skimOnLand_ = skimOnRamp_ = false;
			skimRampTime_ = skimFallExit_ = skimBlockedTime_ = fallLift_ = 0.0f;
			contactSuspended_ = false;
			contactScale_ = 1.0f;
			groundLift_ = appliedLift_ = groundPitch_ = groundRoll_ = groundBlend_ = 0.0f;
			sitAccepted_ = false;
			getUpPending_ = false;
			standTimer_ = 2.0f;  // the post-exit watchdog clears any seated state left on the player
			standTick_ = 0.0f;
			standLogs_ = 0;
			oarHudDirty_ = false;
			hasPrevVehicle_ = hasLastGood_ = false;
			strayCleanupPending_ = true;
			peaceLinger_ = 0.0f;
			RestorePeace("emergency end");
			logger::warn("Teleport detected ({}, distance {:.0f}): skim ended safely", a_reason, a_dist);
		}

		void UpdatePlayerSkim(RE::PlayerCharacter* a_player, float a_delta)
		{
			if (!a_player || a_player != RE::PlayerCharacter::GetSingleton()) return;
			TendSave((std::min)(a_delta, 0.1f));  // 56.26
			if (saving_) return;  // 56.26: nothing of ours moves, respawns or sends graph events while a save is taken
			if (playerSkim_ && TeleportGuard(a_player, a_delta)) return;
			UpdateSummon(a_player, a_delta);
			UpdatePeace(a_player, a_delta);
			if (propPurgePending_ && !playerSkim_) {
				auto* cell = a_player->GetParentCell();
				if (cell && cell->IsAttached() && a_player->Get3D()) {
					propPurgePending_ = false;
					BoatBow::PurgeStaleRefs("load");
					BoatBow::SweepOrphans(true, "load");
				}
			}
			if (strayCleanupPending_ && !playerSkim_) {
				// After an emergency end: once the player stands in a loaded cell again, sweep the lamp / light strays.
				auto* cell = a_player->GetParentCell();
				if (cell && cell->IsAttached() && a_player->Get3D()) {
					strayCleanupPending_ = false;
					BoatBow::CleanupStrays();
				}
			}
			HandleBoardingInput(a_player);
			TendFlowProbe(a_player, a_delta);  // 43.5
			BoatBow::UpdateParked((std::min)(a_delta, 0.1f));
			LakeBoats::Update(a_delta);  // 51
			FlushOarHud();
			PostExitStand(a_player, a_delta);
			WatchBodyFlag(a_player, a_delta);  // 55.2
			TendStash(a_delta);  // 56.8
			TendLoadRelease(a_player, (std::min)(a_delta, 0.1f));  // 56.11.4
			TendBakedBoatOars(a_player, (std::min)(a_delta, 0.1f));  // 56
			TendExitHold(a_player, a_delta);
			TendRelease((std::min)(a_delta, 0.1f));
			TendStrays(a_delta);
			WatchStuck(a_player, a_delta);
			TendSheathe(a_delta);
			if (playerSkim_ && !hideHudInBoat_ && (hudTimer_ += a_delta) >= 1.5f) {
				hudTimer_ = 0.0f;
				ForceHudVisible(false);  // v1.0.15: logged once at boarding only (the re-show every 1.5 s is not the old board hint)
				++hudLogs_;
			}
			if (physicsDelay_ > 0.0f && (physicsDelay_ -= a_delta) <= 0.0f) {
				physicsDelay_ = -1.0f;
				RunPhysicsCommand();
			}
			TendModCompat(playerSkim_);  // 56.43: TDM directional movement / headtracking paused while aboard
			if (!playerSkim_) return;
			if (a_player->IsDead()) {
				ExitPlayerSkim("player died");
				return;
			}
			auto* ui = RE::UI::GetSingleton();
			const bool paused = ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME));
			const float dt = paused ? 0.0f : (std::min)(a_delta, 0.1f);

			auto* cam = RE::PlayerCamera::GetSingleton();
			// First person is the default, but F / the scroll wheel can swing out to a third
			// person view of the boat. The player's own body is hidden in both.
			ApplyPlayerBody(a_player);
			PadZoom(dt);

			// God mode: keep health full every frame.
			if (auto* avo = a_player->AsActorValueOwner()) {
				const float cur = avo->GetActorValue(RE::ActorValue::kHealth);
				const float max = avo->GetPermanentActorValue(RE::ActorValue::kHealth);
				if (cur < max) {
					avo->RestoreActorValue(RE::ActorValue::kHealth, max - cur);
				}
			}

			// Diagnostic: did the player keep its cell?
			const bool cellOk = a_player->GetParentCell() != nullptr;
			if (cellOk != lastPlayerCellOk_) {
				const auto pp = a_player->GetPosition();
				logger::warn("PlayerCell {} (v7) at ({:.0f},{:.0f},{:.0f})", cellOk ? "RESTORED" : "LOST", pp.x, pp.y, pp.z);
				lastPlayerCellOk_ = cellOk;
			}
			// Edge of the loaded world: the player loses its cell out there, which breaks
			// everything (boat spawns, streaming). Pull back to the last good spot and hold.
			if (cellOk) {
				skimLastGoodPos_ = vehiclePos_;
				hasLastGood_ = true;
			} else if (hasLastGood_) {
				vehiclePos_ = skimLastGoodPos_;
				skimSpeed_ = 0.0f;
				edgeHold_ = 1.0f;
				logger::info("Move: player back to the last good spot (edge of the loaded world)");  // crash breadcrumb (v1.0.13)
				a_player->SetPosition(RE::NiPoint3{ vehiclePos_.x, vehiclePos_.y, vehiclePos_.z - kSkimEyeHeight + kV7FeetClearance - kV7PlayerDrop + kCrouchEyeLift + boatHeight_ }, true);
				lastFeet_ = a_player->GetPosition();
				if (auto* console = RE::ConsoleLog::GetSingleton()) console->Print("Oars and Sails: edge of the loaded world - turn back");
			}

			float throttle = (inFwd_ ? 1.0f : 0.0f) - (inBack_ ? 1.0f : 0.0f);
			float strafe = (inRight_ ? 1.0f : 0.0f) - (inLeft_ ? 1.0f : 0.0f);
			if (!r3ZoomActive_ && std::abs(stickY_) > 0.3f) throttle = stickY_ > 0.0f ? 1.0f : -1.0f;  // 55.1: not while R3 zooms
			if (!r3ZoomActive_ && std::abs(stickX_) > 0.3f) strafe = stickX_ > 0.0f ? 1.0f : -1.0f;
			if (edgeHold_ > 0.0f) { edgeHold_ -= dt; throttle = 0.0f; }
			if (paused) { throttle = 0.0f; strafe = 0.0f; }

			if (dt > 0.0f) {
				vehiclePos_.z -= appliedLift_;  // the skim physics work on the un-lifted height
				SkimStep(dt, throttle, strafe);
				UpdateGround(dt);
				UpdateHullFeel(dt);  // 38A
				UpdateStageLogs(dt);  // 42
				UpdateRowAnim(a_player, dt);
				{
					// Fast and level on the water the hull rides lower; everything aboard follows.
					const bool onWater = !(skimAirborne_ || skimOnRamp_ || skimOnLand_);
					const float sinkTarget = onWater ? kSpeedSink * std::clamp(skimSpeed_ / P().maxSpeed, 0.0f, 1.0f) : 0.0f;
					skimSink_ += (sinkTarget - skimSink_) * (std::min)(1.0f, 4.0f * dt);
				}
				const bool firstPerson = cam && cam->IsInFirstPerson();
				// Slide along / across the boat (heading-relative, third person only so the first-person view stays on the eye).
				const float slideFwd = playerBodyFwd_;  // first person too: the eye sits at the oars when you row
				const float slideSide = playerBodySide_;
				const float slideSin = std::sin(skimHeading_), slideCos = std::cos(skimHeading_);
				RE::NiPoint3 slide{ slideSin * slideFwd + slideCos * slideSide, slideCos * slideFwd - slideSin * slideSide, 0.0f };
				if (slideFwd != 0.0f || slideSide != 0.0f) {
					// Ride the boat's pitch and roll like the passengers do: when the bow dips off a fall, a seat 164 units
					// forward of the middle drops with it instead of floating.
					RE::NiMatrix3 boatRot;
					if (BoatBow::RigidRotation(boatRot)) slide = boatRot * RE::NiPoint3{ slideSide, slideFwd, 0.0f };
				}
				// First person: the view does not ride the ground lift (only the boat and the body do), so it stays steady.
				const float liftOut = firstPerson ? appliedLift_ : 0.0f;
				const RE::NiPoint3 feet{ vehiclePos_.x + slide.x, vehiclePos_.y + slide.y, vehiclePos_.z - liftOut - kSkimEyeHeight + kV7FeetClearance - kV7PlayerDrop + kCrouchEyeLift - skimSink_ + boatHeight_ + slide.z + (firstPerson ? fpEyeBoost_ : playerBodyLift_) };
				// 40.2: drift detector: the seated player must be where the last glue put them.
				{
					const RE::NiPoint3 now = a_player->GetPosition();
					const float dx = now.x - lastFeet_.x, dy = now.y - lastFeet_.y;
					const float drift = std::sqrt(dx * dx + dy * dy);
					glueDriftFrames_ = drift > 20.0f ? glueDriftFrames_ + 1 : 0;
					if (glueDriftFrames_ >= 3) {
						glueDriftFrames_ = 0;
						glueZeroTimer_ = (std::max)(glueZeroTimer_, 1.0f);
						++hGlueDrift_;
						logger::info("Glue: player drifted {:.0f} (speed {:.0f}, stop timer {:.2f}, last STOP {:.1f} s ago, boat vs player separation {:.0f})", drift, skimSpeed_, feelStopTimer_, rideTime_ - lastStopAt_,
							BoatBow::HullLag(vehiclePos_.x, vehiclePos_.y));
						if (!glueGraphLogged_) {
							glueGraphLogged_ = true;
							float gs = 0.0f, gs2 = 0.0f;
							const bool h1 = a_player->GetGraphVariableFloat("Speed", gs);
							const bool h2 = a_player->GetGraphVariableFloat("speed", gs2);
							logger::info("Glue: player graph speed Speed={} speed={}", h1 ? std::format("{:.1f}", gs) : std::string("n/a"), h2 ? std::format("{:.1f}", gs2) : std::string("n/a"));
						}
					}
				}
				// 40.2: a sudden deceleration (over 600 u/s^2) also clears the velocity for a second.
				if (std::abs(skimSpeed_ - glueLastSpeed_) / dt > 600.0f) glueZeroTimer_ = (std::max)(glueZeroTimer_, 1.0f);
				glueLastSpeed_ = skimSpeed_;
				if (feelStopTimer_ > 0.0f || postLandTimer_ > 0.0f) glueZeroTimer_ = (std::max)(glueZeroTimer_, 0.05f);
				// 43.2.3: one glue per frame.
				if (lastGlueRide_ == rideTime_ && rideTime_ > 0.0f && !glueTwiceLogged_) {
					glueTwiceLogged_ = true;
					logger::info("Glue: the player was placed twice in one frame");
				}
				lastGlueRide_ = rideTime_;
				++glueMoves_;
				a_player->SetPosition(feet, true);
				lastFeet_ = feet;
				if (glueZeroTimer_ > 0.0f) {
					glueZeroTimer_ -= dt;
					ZeroVelocity(a_player);
				}
				KeepWeaponsOff(a_player);  // 43.1.3
				// 44.3.2: the Jump: detector.
				if (jumpHave_ && !skimAirborne_ && rideTime_ - lastAirAt_ > 0.3f) {
					const float jx = vehiclePos_.x - jumpPrev_.x, jy = vehiclePos_.y - jumpPrev_.y;
					const float jd = std::sqrt(jx * jx + jy * jy);
					const float vmax = skimSpeed_ + skimReverse_ + std::sqrt(flowVel_.x * flowVel_.x + flowVel_.y * flowVel_.y) + std::sqrt(feelPush_.x * feelPush_.x + feelPush_.y * feelPush_.y);
					if (jd > (std::max)(3.0f * vmax * dt, 12.0f)) {
						++hJumps_;
						std::string st;
						auto add = [&](bool a_on, const char* a_n) { if (a_on) { st += st.empty() ? "" : "/"; st += a_n; } };
						add(rideTime_ - lastBeachLineAt_ < 1.0f, "beach");
						add(appliedLift_ > 0.5f, "ground2");
						add(feelStopTimer_ > 0.0f || feelPush_.x != 0.0f || feelPush_.y != 0.0f, "feel");
						add(flowW_ > 0.05f, "flow");
						add(descent_, "descent");
						add(glueZeroTimer_ > 0.0f, "glue");
						add(BoatBow::RespawnActive(), "respawn");
						logger::info("Jump: {:.0f} units (speed {:.0f}, state {}, last Beach line {:.1f} s ago)", jd, skimSpeed_, st.empty() ? "-" : st, rideTime_ - lastBeachLineAt_);
					}
				}
				jumpPrev_ = vehiclePos_;
				jumpHave_ = true;
				UpdateJit(a_player, dt);   // 43.2.1
				// 43.3.2: no prop respawn in a flight, a fall, within 3 s after one, or while fast.
				if (skimAirborne_ || skimOnRamp_) lastAirAt_ = rideTime_;
				BoatBow::SetFlightHold(skimAirborne_ || skimOnRamp_ || rideTime_ - lastAirAt_ < 3.0f || skimSpeed_ >= 200.0f);
				// 43.3.1: the hull watchdog: the hull must stay with the vehicle.
				hullLagCooldown_ = (std::max)(0.0f, hullLagCooldown_ - dt);
				const float lag = BoatBow::HullLag(vehiclePos_.x, vehiclePos_.y);
				hullLagFrames_ = lag > 80.0f ? hullLagFrames_ + 1 : 0;
				hullBigLagFrames_ = lag > 600.0f ? hullBigLagFrames_ + 1 : 0;
				const bool cellOk = BoatBow::HullCellOk();
				// 45.1: a detached hull cell or a lag over 600 for 2 frames: the whole set fresh in the player's cell, now.
				if (hullBigLagFrames_ >= 2 && hullReplaceCooldown_ <= 0.0f) {
					// 48.1: a real loss (over 600 for 2 frames): the whole set fresh.
					hullReplaceCooldown_ = 0.5f;
					hullBigLagFrames_ = hullLagFrames_ = 0;
					logger::info("Glue: hull lagged by {:.0f} units, cell {}: re-placing the boat", lag, cellOk ? "ok" : "detached");
					BoatBow::ReplaceAll("hull lag over 600");
				} else if (!cellOk && lag >= 0.0f && hullReplaceCooldown_ <= 0.0f) {
					// 48.1: a cell boundary with a small lag: move the same refs into the player's cell (no fade).
					hullReplaceCooldown_ = 0.5f;
					logger::info("Glue: hull cell detached, lag {:.0f}: cell hop", lag);
					BoatBow::CellHop(vehiclePos_.x, vehiclePos_.y, vehiclePos_.z);
				} else if (hullLagFrames_ >= 3 && hullLagCooldown_ <= 0.0f) {
					hullLagFrames_ = 0;
					hullLagCooldown_ = 1.0f;
					logger::info("Glue: hull lagged by {:.0f} units (state: airborne {} held respawn {} ramp {} suspended {}), moving the boat back", lag,
						skimAirborne_ ? 1 : 0, BoatBow::RespawnActive() ? 1 : 0, skimOnRamp_ ? 1 : 0, contactSuspended_ ? 1 : 0);
					BoatBow::ForceReattach(vehiclePos_.x, vehiclePos_.y, vehiclePos_.z, "hull watchdog");
				}
				hullReplaceCooldown_ = (std::max)(0.0f, hullReplaceCooldown_ - dt);
				// 45.1.3: one hull check right after each landing.
				if (hullCheckAfterLanding_ && !skimAirborne_) {
					hullCheckAfterLanding_ = false;
					logger::info("Glue: hull check after landing: lag {:.0f} cell {}", lag, cellOk ? "ok" : "detached");
					if (lag > 600.0f) { hullReplaceCooldown_ = 0.5f; BoatBow::ReplaceAll("after landing"); }
					else if (!cellOk) { hullReplaceCooldown_ = 0.5f; BoatBow::CellHop(vehiclePos_.x, vehiclePos_.y, vehiclePos_.z); }
				}
			}

			// The camera shows last frame's player position, so the boat follows the
			// previous frame's vehicle position to stay glued to the view.
			const RE::NiPoint3 boatPos = hasPrevVehicle_ ? prevVehicle_ : vehiclePos_;
			{
				BoatBow::Flight flight{};
				flight.airborne = skimAirborne_;
				flight.onRamp = skimOnRamp_;
				flight.verticalSpeed = skimVerticalSpeed_;
				flight.horizontalSpeed = skimSpeed_;
				flight.rampPitch = skimRampPitch_;
				flight.groundPitch = groundPitch_;
				flight.groundRoll = groundRoll_ + flowRoll_;  // 41C: rapids turbulence
				flight.groundBlend = groundBlend_;
				boardRaise_ += std::clamp(kV7BoatRaise - boardRaise_, -90.0f * dt, 90.0f * dt);  // lifts off the water after boarding
				// v1.0.13: the periodic ref refresh waits for a calm moment (13.2 suspect 1).
				if (skimAirborne_ || skimOnRamp_) refreshCalm_ = 0.0f; else refreshCalm_ += dt;
				BoatBow::SetRefreshHold(skimSpeed_ > kRefreshHoldSpeed || refreshCalm_ < 3.0f || CrewBusy());
				BoatBow::SetWaterZ(skimWaterHeight_, !skimAirborne_ && !skimOnLand_ && !onIce_);  // 56.31: bilge floor height
				BoatBow::Update(skimSpeed_ / P().maxSpeed, skimYawRate_, dt, boatPos.x, boatPos.y, boatPos.z + boardRaise_ - skimSink_ + boatHeight_, skimHeading_, flight);
				BoatBow::EmitWake(dt, skimSpeed_ / P().maxSpeed, skimHeading_, vehiclePos_.x, vehiclePos_.y, skimWaterHeight_, !skimAirborne_ && !skimOnLand_ && !skimOnRamp_ && !onIce_);
			}
			UpdatePassengers(boatPos, dt);
			UpdateLateBoarding(boatPos, dt);
			prevVehicle_ = vehiclePos_;
			hasPrevVehicle_ = true;

			diagTimer_ += dt;
			if (diagTimer_ >= 1.0f) {
				diagTimer_ = 0.0f;
				float camZ = 0.0f;
				if (cam && cam->cameraRoot) camZ = cam->cameraRoot->world.translate.z;
				// glue: per-frame SetPosition calls on passengers since the last heartbeat (too many to breadcrumb one by one)
				logger::info("SkimPlayer heartbeat: speed={:.1f} airborne={} water={:.1f} vehicleZ={:.1f} playerZ={:.1f} cameraZ={:.1f} cell={} glue={}",
					skimSpeed_, skimAirborne_, skimWaterHeight_, vehiclePos_.z, a_player->GetPositionZ(), camZ, cellOk, glueMoves_);
				glueMoves_ = 0;
			}
		}


	private:
		float diagTimer_{ 0.0f };

		RE::NiPoint3 vehiclePos_{};  // the boat's reference point (the skim physics move this; player and boat follow it)
		bool lastPlayerCellOk_ = true;
		bool skimAirborne_{ false };
		float skimSpeed_{ 0.0f };
		float skimReverse_{ 0.0f };  // reverse speed magnitude (u/s), kept apart from forward speed
		float skimVerticalSpeed_{ 0.0f };
		float skimDropGrace_{ 0.0f };
		float skimWaterHeight_{ 0.0f };
		float skimWaterfallLogCooldown_{ 0.0f };
		float rockHitLogCooldown_{ 0.0f };  // 31.2: "Contact: rock hit" at most once a second
		float skimStopLogCooldown_{ 0.0f };
		float skimLaunchCooldown_{ 0.0f };  // stops back-to-back hop loops at a rock
		float skimFallExit_{ 0.0f };        // grace after splashing down from a fall: obstacle sweeps are ignored on water
		float skimBlockedTime_{ 0.0f };     // how long we've been pinned against something with the throttle held
		float skimFlowTimer_{ 0.0f };
		float fallLift_{ 0.0f };            // 35.1: extra height over / through a fall, eased (see UpdateFallLift)
		float fallLiftLogCooldown_{ 0.0f };
		float skimRampDetachHold_{ 0.0f };  // 36.1: after landing on / entering a slope, no "left the slope" unless drop > 60
		float rampChatterHold_{ 0.0f };     // 36.1: chatter seen, the ramp is held outright
		float rampLogCooldown_{ 0.0f };
		float rampPoolLogCooldown_{ 0.0f };
		float poolRise_{ 0.0f };            // 37: eased rise to a pool surface the boat came up under (negative, decays to 0)
		std::array<float, 4> rampDetachTimes_{ -10.0f, -10.0f, -10.0f, -10.0f };
		int rampDetachNext_{ 0 };
		float crewPitchBlend_{ 0.0f };      // 36.2: 1 while the boat falls or rides a slope, eases to 0 over 1 s after
		float crewTiltLogAt_{ -10.0f };
		std::unordered_map<RE::FormID, float> crewBoatPitch_;  // 36.2.3: last boat pitch at each human's seat (rad)
		float swellLogCooldown_{ 0.0f };
		bool fallLiftOn_{ true };           // 35.1: oas falllift
		bool helmSweepOn_{ false };         // 35.2: oas helmsweep; off for now (temporary): the boat passes rocks/docks over water
		bool helmSweepLogged_{ false };     // "helm sweep off" logged this ride
		bool skimFlowAhead_{ false };     // a lower water plane (a fall) is ahead: the current pulls the boat along
		float skimHeading_{ 0.0f };      // boat heading, radians (forward = sin,cos)
		float skimYawRate_{ 0.0f };
		bool skimOnLand_{ false };
		float skimDip_{ 0.0f };
		float skimDipVel_{ 0.0f };
		float skimRecoverSpeed_{ 0.0f };
		float skimRecoverTime_{ 0.0f };
		RE::NiPoint3 skimLastWaterPos_{};

		bool skimOnRamp_{ false };
		float skimRampTime_{ 0.0f };
		float skimRampPitch_{ 0.0f };
		float skimRampVz_{ 0.0f };

		// v7 (player-driven skim)
		bool playerSkim_{ false };
		bool boardedParked_{ false };
		bool bodyHideLogged_{ false };
		float bodyWatchTimer_{ 0.0f };   // 55.2: post-exit body flag watch
		bool animObjHoldOffLogged_{ false };  // 56
		int lastBodyCamState_{ -1 };
		std::vector<RE::ActorHandle> passengers_;
		RE::NiPoint3 bowSlot_{ 0.0f, 440.0f, -73.0f };
		float passengerTimer_{ 0.0f };
		int sitMiss_{ 0 };  // consecutive checks that found the player standing after an accepted sit
		float crouchTimer_{ 0.0f };
		float skimSink_{ 0.0f };
		float groundPitch_{ 0.0f };   // ground tilt fed to the boat (radians, + = bow up / starboard down)
		float groundRoll_{ 0.0f };
		float groundBlend_{ 0.0f };   // 0 afloat .. 1 aground (fades the idle rocking out)
		float groundLift_{ 0.0f };    // how far the ground lifts the boat above where the skim physics put it (0..kMaxGroundLift)
		bool touchState_[kGroundSamples]{};        // per hull sample, after hysteresis
		bool dryState_[kGroundSamples]{};
		bool bankState_[kGroundSamples]{};      // 40.1.5: per sample, a bank (hysteretic: on above 40, off below 32 for 0.3 s)
		float bankClearTimer_[kGroundSamples]{};
		bool basinHold_{ false };               // 39.5: the basin guard holds the lift until the hull is clear
		float beachLogTimer_{ 0.0f };
		bool bowInGroundLogged_{ false };
		float glueZeroTimer_{ 0.0f };           // 40.2: the player and crew velocity is zeroed after every glue while this runs
		float glueLastSpeed_{ 0.0f };
		int glueDriftFrames_{ 0 };
		float lastStopAt_{ -100.0f };           // rideTime_ of the last feeler STOP
		bool glueGraphLogged_{ false };
		std::vector<RE::FormID> pitchZeroLogged_;  // 40.3: crew whose graph pitch was logged this flight
		float touchTimer_[kGroundSamples]{};
		float dryTimer_[kGroundSamples]{};
		bool tooHigh_{ false };       // a bank / cliff is under the hull: lift and tilt off
		bool wasInAir_{ false };
		float postLandTimer_{ 0.0f };
		float appliedLift_{ 0.0f };   // groundLift_ x contactScale_: what was actually added to the boat's height this frame
		bool contactSuspended_{ false };  // a waterfall / drop ahead: fake collision paused (automatic, see UpdateContactSuspend)
		float contactScale_{ 1.0f };  // 0..1 while contact eases back in after a suspension
		float contactQuiet_{ 0.0f };
		float lookTimer_{ 0.0f };
		float lookHit_{ -1.0f };      // distance ahead of the last waterfall the look-ahead saw (-1 = none)
		bool hasBankMark_{ false };
		RE::NiPoint3 bankMark_{};
		float groundLogTimer_{ 0.0f };
		float waterfallTimer_{ 0.0f };
		bool waterfallNear_{ false };
		bool wasGrounded_{ false };
		bool hasBowMark_{ false };
		RE::NiPoint3 bowMark_{};
		float bowSlide_{ 0.0f };
	public:
		bool nodeOverride_{ true };   // followers/dogs tilt with the boat
		bool contactOn_{ true };      // fake collision (ground tilt, beach slide); `oas contact` toggles
		bool contactLog_{ false };    // one log line per contact event; `oas "contact log"` toggles
		float hullDraft_{ 0.0f };     // hull bottom this far below the waterline (0: at it, so a bar 8 above the water lifts ~4); `oas "contact draft N"`
		// 38A/38B: hull feelers. Mode 0 off, 1 log only, 2 on (the reactions, default since v1.0.36).
		int hullFeelMode_{ 2 };       // 38B: on by default
		float feelLen_{ 300.0f };     // kFeelLen: half length, hull centre to the bow tip; oas "hullfeel len N"
		float feelWidth_{ 85.0f };    // kFeelWidth: half width at the widest; oas "hullfeel width N"
		float feelKeel_{ 20.0f };     // keel line this far below the water (hullDraft_ is 0 by default); oas "hullfeel keel N"
		std::array<bool, 12> feelTouch_{};
		std::array<int, 12> feelHits_{};
		std::array<float, 4> feelLogCooldown_{};
		float feelSummaryCooldown_{ 0.0f };
		float feelCostTimer_{ 0.0f };
		float feelCostMs_{ 0.0f };
		int feelCostFrames_{ 0 };
		unsigned feelFrame_{ 0 };
		std::array<float, 12> feelReactCooldown_{};  // 38B: per-feeler reaction cooldowns
		RE::NiPoint3 feelPush_{};     // 38B: fading sideways push (world u/s)
		float feelYawKick_{ 0.0f };   // 38B: bow turn-away (rad/s)
		float feelStopTimer_{ 0.0f }; // 38B: a bow STOP in progress
		float feelStopFrom_{ 0.0f };
		static constexpr float kFeelStopTime = 0.2f;  // 40.1.4
		std::array<float, 12> feelPrevDist_{};
		std::array<float, 12> feelPrevAt_{};
		float feelTerrainLogCooldown_{ 0.0f };
		bool feelRocksOn_{ true };                 // 48.2: oas "hullfeel rocks on|off"
		float rockOffUntil_{ -100.0f }, rockWindowAt_{ -100.0f }, rockAheadUntil_{ -100.0f };
		RE::NiPoint3 rockSpot_{};
		int rockFires_{ 0 }, hRocks_{ 0 };
		std::array<float, 12> rockLogAt_{ -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f };
		// Part 41/42 stages (switches in Features(), all on by default)
		bool groundV2On_{ true };
		bool descentOn_{ true };            // 44.2: oas descent
		bool descent_{ false };
		float descentTime_{ 0.0f }, descentWaterT_{ 0.0f }, descentFlatT_{ 0.0f }, descentDetachT_{ 0.0f };
		bool descentStopping_{ false };
		float descentStopFrom_{ 0.0f }, descentStopT_{ 0.0f }, descentLogCooldown_{ 0.0f };
		float stuckTime_{ 0.0f };
		float descentRiseT_{ 0.0f };
		int hWalls_{ 0 }, hUphill_{ 0 };
		float uphillLogAt_{ -100.0f };
		float lastFlowNearAt_{ -100.0f }, flowHoldUntil_{ -100.0f }, flowSlowTime_{ 0.0f }, flowBlockedTime_{ 0.0f }, lastBlockedAt_{ -100.0f };
		int hDescents_{ 0 }, hJumps_{ 0 };
		RE::NiPoint3 jumpPrev_{};
		bool jumpHave_{ false };
		float lastBeachLineAt_{ -100.0f }, easedLogAt_{ -100.0f };
		float easedBack_{ 0.0f };
		float poolSkipLogCooldown_{ 0.0f };
		bool waterPatchOn_{ true };
		bool flowOn_{ true };
		bool cascadeOn_{ true };
		bool iceOn_{ true };
		std::vector<WaterPatch> patches_;
		float stageClock_{ 0.0f };
		float patchUsedAt_{ -100.0f };
		float patchExpireTimer_{ 0.0f };
		float waterDiagCooldown_{ 0.0f };
		std::vector<FlowSource> flows_;
		float flowScanTimer_{ 0.0f };
		RE::NiPoint3 flowVel_{};
		float flowW_{ 0.0f };
		float flowS_{ 0.0f };
		float flowQ_{ 0.0f };
		std::string flowName_;
		bool flowRapids_{ false };
		float flowRoll_{ 0.0f };
		float flowPhase_{ 0.0f };
		float flowLogCooldown_{ 0.0f };
		float flowPeakW_{ 0.0f };
		float lastLandingAt_{ -100.0f };
		std::vector<RE::FormID> flowIgnoredIds_;
		std::vector<std::string> flowIgnoredNames_;
		float lastInputAt_{ -100.0f };
		float lastFallEventAt_{ -100.0f };
		float rampDetachTimer_{ 0.0f };
		float fallLogCooldown_{ 0.0f };
		int softContacts_{ 0 };
		bool iceHintsLoaded_{ false };
		std::vector<std::string> iceHints_;
		std::unordered_map<RE::FormID, bool> iceCache_;
		std::vector<RE::FormID> iceSheets_;
		bool onIce_{ false };
		int iceSamplesPrev_{ 0 };
		float liftVel_{ 0.0f };
		float pitchVel_{ 0.0f };
		float rollVel_{ 0.0f };
		float governor_{ 1.0f };
		float govRefSpeed_{ 0.0f };
		bool govEngaged_{ false };
		float ground2LogTimer_{ 0.0f };
		float prevAppliedLift_{ 0.0f };
		float prevPitch_{ 0.0f };
		float sumTimer_{ 0.0f };
		float sumSame_{ 0.0f };
		std::string sumKey_;
		float healthTimer_{ 0.0f };
		float costTimer_{ 0.0f };
		float costFlowMs_{ 0.0f };
		float costGroundMs_{ 0.0f };
		int costFrames_{ 0 };
		int hSnaps_{ 0 }, hPools_{ 0 }, hShoreInWater_{ 0 }, hFlowPulled_{ 0 }, hFlowThrown_{ 0 }, hCascadeTouches_{ 0 }, hCascadeEvents_{ 0 };
		int hFeelReact_{ 0 }, hFeelTerrain_{ 0 }, hGlueDrift_{ 0 };
		bool playerTilt_{ false };
        bool crewTilt_{ false };		// OFF by default since v9.43: moving the player's skeleton root made face/hair/bag physics warp and stutter even at rest. "oas ptilt" turns the old tilt back on  // rotate/move the body and passenger skeleton roots (can fight physics mods)
	private:
		float adoptTimer_{ 0.0f };
		int tuneMode_{ 105 };  // numpad target: 0 player, 1 dog, 2 human follower, 100 bench, 101 oars (the default), 102 left oar when unlinked
	public:
		// One test ripple ahead of the player (the console block is outside the class, so the private state is read here).
		void PlaceTestRipple(float a_scale)
		{
			auto* tp = RE::PlayerCharacter::GetSingleton();
			if (!tp) return;
			const float h = playerSkim_ ? skimHeading_ : tp->GetAngleZ();
			const RE::NiPoint3 pp = tp->GetPosition();
			const float z = playerSkim_ ? skimWaterHeight_ : pp.z;
			BoatBow::RippleAt(RE::NiPoint3{ pp.x + std::sin(h) * 400.0f, pp.y + std::cos(h) * 400.0f, z }, a_scale);
			logger::info("Ripple test at scale {:.3f}", a_scale);
		}
		int hudLogs_{ 0 };
		float hudTimer_{ 0.0f };      // re-shows the HUD about every 1.5 s while riding, in case something else hides it again
		bool hideHudInBoat_{ false };  // `oas hud` hides the game HUD while riding (it used to be hidden)
		void ToggleHudInBoat()
		{
			hideHudInBoat_ = !hideHudInBoat_;
			if (playerSkim_) { if (hideHudInBoat_) HideHUD(); else ForceHudVisible(true); }
		}
		bool rowMode_{ true };  // true = heavy rowing boat (default), false = motor / speedboat profile (`oas row` toggles)
		SkimProfile rowProfile_{ kRowMaxSpeed, kRowMaxSpeed * kRowAccelRate, 120.0f, kRowCoastDrag, 60.0f, 20.0f, 0.5f, 0.45f, 120.0f, 0.9f };
		const SkimProfile& P() const { return rowMode_ ? rowProfile_ : kMotorProfile; }
		static constexpr float kLightBoatFactor = 1.0f;   // 56.49: off for now, same speed with or without crew (was 1.12)  // 56.36: 12% faster with only the player (and dogs) aboard
		static constexpr float kLightBoatDrag = 1.0f;     // 56.49: off for now (was 0.7)
		bool lightBoatLogged_{ false };
		bool oarLink_{ true };  // the numpad moves both oars, mirrored (`oas "oar link"` toggles)
	private:
		static constexpr int kRippleModeId = 103;  // numpad target: the ripples
		static constexpr int kChestModeId = 104;   // numpad target: the chest
		static constexpr int kAllModeId = 105;     // numpad target: rower seat + both oars + chest, all moved together (the default)
		static constexpr int kLampSternModeId = 106;  // numpad target: the stern lamp
		static constexpr int kLampBowModeId = 107;    // numpad target: the bow lamp
		static constexpr std::uint32_t kLampKey = 0x45;  // Num Lock cycles the lamp modes; only used when kNumpadLampsOnly is false
		// What the numpad tunes. kCrew (default since v1.0.9): the seat of everyone aboard, Num5 cycles them. kLamps (v1.0.7):
		// only the two lamps, Num5 flips stern / bow. kDefault (v1.0.6): oars / follower in slot one, Num Lock for the lamps.
		enum class NumpadTargets { kDefault, kLamps, kCrew };
		static constexpr NumpadTargets kNumpadTargets = NumpadTargets::kCrew;
		static constexpr bool kNumpadLampsOnly = kNumpadTargets == NumpadTargets::kLamps;
		RE::FormID crewTargetId_{ 0 };     // who the crew numpad is on (kept by form ID so it survives people boarding / leaving)
		std::chrono::steady_clock::time_point lastNoCrewHud_{};
		RE::NiPoint3 allShift_{};                  // total the whole rig has been moved by this session
		int ripplePage_{ 0 };  // 0 = stern trail, 1 = oar rings (Num Enter flips)
		int oarPage_{ 0 };  // 0 = position page, 1 = angle and stroke page (Num Enter flips)
		bool oarHudDirty_{ false };
		std::chrono::steady_clock::time_point oarHudLast_{};
		std::chrono::steady_clock::time_point oarHudLegend_{};
		// One seat per human follower (keyed by form ID, remembered between rides). right / forward / up
		// from the vehicle point. New followers take the next free default spot; Faendal keeps the tuned one.
		std::vector<HumanSeat> humanSeats_;
		// v1.0.14 boarding choice (14.4) and input
		BoardChoice boardChoice_{ BoardChoice::kDefault };
		std::string pendingPick_;        // our object the player activated (set by the activate event, handled next frame)
		bool inputLogged_{ false };
		std::uint32_t padActivate_{ 0 };  // the gamepad key the game maps to Activate (B2)
		bool padZoomHeld_{ false };       // B5: L3 held
		float rStickY_{ 0.0f };
		bool zoomLogged_{ false };
		// 55.1 (v1.0.55): R3 (Toggle POV) held, the zoom it did this hold, the view when it went down, and the
		// left stick taken for zoom this frame (rowing / steering ignore it then).
		bool padViewHeld_{ false };
		bool zoomedWithR3_{ false };
		bool r3StartFirst_{ false };
		bool r3ZoomActive_{ false };
		float r3UndoWindow_{ 0.0f };
		bool r3ZoomLogged_{ false };
		// 55.1 diagnostic: what the gamepad sends while aboard.
		bool padEverSeen_{ false };
		int padButtons_{ 0 }, padSticks_{ 0 };
		std::vector<std::string> padUserEvents_;
		void NotePadEvent(bool a_button, std::string_view a_user)
		{
			if (!padEverSeen_) {
				padEverSeen_ = true;
				logger::info("Input: gamepad seen (device kGamepad)");
			}
			if (!playerSkim_) return;
			++(a_button ? padButtons_ : padSticks_);
			if (a_button && !a_user.empty() && padUserEvents_.size() < 8 &&
				std::find(padUserEvents_.begin(), padUserEvents_.end(), a_user) == padUserEvents_.end())
				padUserEvents_.emplace_back(a_user);
		}
		void LogPadEvents()
		{
			std::string list;
			for (const auto& u : padUserEvents_) list += (list.empty() ? "" : ", ") + u;
			logger::info("Input: gamepad events seen: {} button, {} thumbstick, user events: [{}]", padButtons_, padSticks_, list);
			padButtons_ = padSticks_ = 0;
			padUserEvents_.clear();
		}
		// B5: hold L3 + right stick up / down zooms the third person camera aboard (vanilla's gamepad zoom does not reach it).
		// 55.1: also vanilla's way, R3 held + left stick up / down, without the view switch R3 would otherwise do.
		void PadZoom(float a_dt)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			r3ZoomActive_ = padViewHeld_ && std::abs(stickY_) > 0.2f;
			if (!cam || !cam->currentState) return;
			// R3 used to zoom: if the game switched third to first person on that press / release, switch straight back.
			if (padViewHeld_) r3UndoWindow_ = 0.5f;
			else if (r3UndoWindow_ > 0.0f) r3UndoWindow_ -= a_dt;
			if (zoomedWithR3_ && !r3StartFirst_ && (padViewHeld_ || r3UndoWindow_ > 0.0f) && cam->IsInFirstPerson()) {
				if (auto target = cam->cameraStates[RE::CameraState::kThirdPerson]) cam->SetState(target.get());
				logger::info("Zoom: R3 toggle undone");
				zoomedWithR3_ = false;
			}
			if (cam->currentState->id != RE::CameraState::kThirdPerson) return;
			auto* tps = static_cast<RE::ThirdPersonState*>(cam->currentState.get());
			float input = 0.0f;
			if (r3ZoomActive_) input = stickY_;
			else if (padZoomHeld_ && std::abs(rStickY_) >= 0.2f) input = rStickY_;
			if (input == 0.0f) return;
			tps->targetZoomOffset = std::clamp(tps->targetZoomOffset - input * 1.2f * a_dt, 0.0f, 1.0f);  // stick up: closer
			tps->savedZoomOffset = tps->targetZoomOffset;  // so the game does not snap back
			if (r3ZoomActive_) {
				zoomedWithR3_ = true;
				if (!r3ZoomLogged_) {
					r3ZoomLogged_ = true;
					logger::info("Zoom: R3 held + left stick, zoom now {:.2f}", tps->targetZoomOffset);
				}
			} else if (!zoomLogged_) {
				zoomLogged_ = true;
				logger::info("Zoom: L3 (Sneak button) held + right stick up/down, zoom now {:.2f}", tps->targetZoomOffset);
			}
		}
		int padLogs_{ 0 };                // gamepad buttons logged this boarding (B2 diagnostic)
		const RE::TESObjectREFR* lastPickRef_{ nullptr };  // what the crosshair was on last frame (Pick: lines on change)
		bool lastPickOurs_{ false };
	public:
		// 18.3 (v1.0.20): every on/off feature, on by default, switched with oas commands, kept in the co-save.
		bool rowerAnimOn_{ true };   // the rower plays the row loop (the oars move either way)
		bool flinchOn_{ true };      // seated crew flinch on bumps
		bool greetOffOn_{ true };    // no greetings aimed at the player while boarded (B7)
		bool sheatheOn_{ true };     // crew put weapons away after unboarding and on load (B1/B10)
		bool camFixOn_{ false };     // third person view kept upright while the player rows (18.2); off since v1.0.27 (28: animobj fixes the cause)
		bool camLogOn_{ true };      // 25: read-only camera measurements once a second (oas camlog)
		struct Feature { const char* name; std::function<bool()> get; std::function<void(bool)> set; bool def; };
		std::vector<Feature> Features()
		{
			return {
				{ "rower", [this] { return rowerAnimOn_; }, [this](bool v) { rowerAnimOn_ = v; if (!v) StopRowAnim(RE::PlayerCharacter::GetSingleton()); }, true },
				{ "idles", [this] { return idlesOn_; }, [this](bool v) { idlesOn_ = v; }, true },
				{ "peace", [this] { return peaceOn_; }, [this](bool v) { peaceOn_ = v; if (!v) RestorePeace("peace switched off"); }, true },
				{ "lampauto", [] { return BoatBow::LampAuto(); }, [](bool v) { BoatBow::SetLampAuto(v); }, true },
				{ "ripples", [] { return BoatBow::RipplesOn(); }, [](bool v) { BoatBow::SetRipplesOn(v); }, true },
				{ "flinch", [this] { return flinchOn_; }, [this](bool v) { flinchOn_ = v; }, true },
				{ "contact", [this] { return contactOn_; }, [this](bool v) { contactOn_ = v; }, true },
				{ "greet", [this] { return greetOffOn_; }, [this](bool v) { greetOffOn_ = v; if (!v) SuppressGreetings(false); }, true },
				{ "sheathe", [this] { return sheatheOn_; }, [this](bool v) { sheatheOn_ = v; }, true },
				{ "tilt", [this] { return nodeOverride_; }, [this](bool v) { nodeOverride_ = v; }, true },
				{ "face", [this] { return lockSeatedFacing_; }, [this](bool v) { lockSeatedFacing_ = v; }, true },
				{ "sit", [this] { return playerSit_; }, [this](bool v) { playerSit_ = v; }, true },
				{ "oarlink", [this] { return oarLink_; }, [this](bool v) { oarLink_ = v; }, true },
				{ "camlog", [this] { return camLogOn_; }, [this](bool v) { camLogOn_ = v; }, true },
				// 28 (v1.0.27): the root cause (AnimObjectA) is held by animobj; camfix (Part 27 mirror) is off by default again.
				{ "animobj", [this] { return animObjFixOn_; }, [this](bool v) { animObjFixOn_ = v; }, true },
				{ "camfix", [this] { return camFixOn_; }, [this](bool v) { camFixOn_ = v; }, false },
				// 35 (v1.0.32): lift over / through falls; the helm sweep is off for now (temporary).
				{ "falllift", [this] { return fallLiftOn_; }, [this](bool v) { fallLiftOn_ = v; }, true },
				{ "helmsweep", [this] { return helmSweepOn_; }, [this](bool v) { helmSweepOn_ = v; }, false },
				// 42 (v1.0.38): one switch per Part 41 stage; off = the old behaviour.
				{ "groundv2", [this] { return groundV2On_; }, [this](bool v) { groundV2On_ = v; }, true },
				{ "waterpatch", [this] { return waterPatchOn_; }, [this](bool v) { waterPatchOn_ = v; if (!v) patches_.clear(); }, true },
				{ "flow", [this] { return flowOn_; }, [this](bool v) { flowOn_ = v; if (!v) { flowVel_ = {}; flowW_ = 0.0f; } }, true },
				{ "cascade", [this] { return cascadeOn_; }, [this](bool v) { cascadeOn_ = v; }, true },
				{ "descent", [this] { return descentOn_; }, [this](bool v) { descentOn_ = v; if (!v) descent_ = false; }, true },
				{ "ice", [this] { return iceOn_; }, [this](bool v) { iceOn_ = v; iceCache_.clear(); }, true },
				// Off on purpose (see the comments where they are declared): skeleton-root tilt warps physics, hud hides the HUD,
				// contact log is a debug log.
				{ "ptilt", [this] { return playerTilt_; }, [this](bool v) { playerTilt_ = v; }, false },
				{ "hud", [this] { return hideHudInBoat_; }, [this](bool v) { hideHudInBoat_ = v; }, false },
				{ "contactlog", [this] { return contactLog_; }, [this](bool v) { contactLog_ = v; }, false },
			};
		}
		std::string FeatureStatus()
		{
			std::string line;
			for (auto& f : Features()) line += std::format("{}{}={}", line.empty() ? "" : " ", f.name, f.get() ? "on" : "off");
			line += " " + HullFeelStatus();  // 38A
			line += std::format(" mirror={}", spdlog::default_logger()->sinks().size() > 1 ? "on (ini)" : "off (ini)");
			line += std::format(" mesh={}", BoatBow::HullMeshStatus());  // 29.6
			line += std::format(" paint: {}", BoatBow::PaintName(BoatBow::Paint()));  // 32.3
			line += std::format(" textures: {}", BoatBow::BoatTextureStatus());  // 30.1
			return line;
		}
		void ResetFeatures()
		{
			for (auto& f : Features()) f.set(f.def);
		}
		static void LogRowerSeatDefault()
		{
			logger::info("Rower seat default: right={:.0f} forward={:.0f} up={:.0f} yaw={:.0f}", kSeatSlots[0].x, kSeatSlots[0].y, kSeatSlots[0].z, kSeatYaw[0] * 57.2958f);
		}
		// B9: the parked boat in the co-save (ref id, spot, heading, worldspace), read at load and used by the adopt step.
		struct SavedPark { bool valid{ false }; RE::FormID id{ 0 }; float x{ 0 }, y{ 0 }, z{ 0 }, yaw{ 0 }; RE::FormID world{ 0 }; int tries{ 0 }; };
		SavedPark savedPark_;
		bool propPurgePending_{ false };  // 31.1: after a load, the last session's prop refs are purged once the cell is loaded
		std::uint32_t rowerTurn_{ 0 };     // 14.6 rotation, kept in the co-save
		std::uint64_t rowerListHash_{ 0 }; // the eligible humans it counts over; a different list starts again at 0
	private:
		float humanSitTimer_{ 0.0f };
		RE::NiPoint3 playerSeat_{ 4.0f, -4.0f, 68.0f };  // tuned seat: right / forward / up from the player's feet
		float playerSeatYaw_{ 6.1959f };  // tuned: 175 degrees, he sits facing the stern like a rower
		bool wasFirstPerson_{ true };
		int crouchLogs_{ 0 };
		std::chrono::steady_clock::time_point lastTuneStep_{};
		// How the player's animation flags were before the first ride. The ride changes them (bAllowRotation, foot IK, head
		// tracking) and the old exit forced bAllowRotation to TRUE, but a walking player has it FALSE (see the VarDump logs:
		// healthy at the start of each session, stuck for good after the first ride). Put back exactly what was there.
		// Heavy diagnostics from hunting the walking bug (full animation-variable dumps, memory diffs, controller and handler
		// logs). Off now that it is fixed; switch on to investigate something similar.
		static constexpr bool kDebugDumps = false;
		bool numCycleInit_{ false };
		bool haveSavedFlags_{ false };
		bool savedAllowRot_{ false };
		bool savedFootIKOff_{ false };
		bool savedHeadTrack_{ true };
		bool exitWasFirst_{ false };
		bool getUpPending_{ false };
		float getUpElapsed_{ 0.0f };
		int getUpStep_{ 0 };
		float standTimer_{ 0.0f };
		float standTick_{ 0.0f };
		int standLogs_{ 0 };
		int sitTries_{ 0 };
		bool sitAccepted_{ false };
		float playerBodyLift_{ 26.0f };  // your tuned stern seat  // tuned for the rowing animation; raises the seated body (no skeleton override needed)
		float playerBodyFwd_{ 8.0f };  // tuned for the rowing animation; slides the seated body along the boat (+ = toward the bow / oars), third person only
		float stuckWindow_{ 0.0f };      // seconds left after getting out in which a stuck player is lifted out of the boat
		float stuckTimer_{ 0.0f };
		RE::NiPoint3 stuckPos_{};
		bool rowWantAnim_{ false };      // W held in the rowboat this frame
		bool rowAnimOn_{ false };        // the rowing animation (OarsRowStart, or the OffsetGPMA fallback) is playing
		RE::FormID rowRestartId_{ 0 };   // 56.19.4: crew rower whose clip was stopped, checked for a dropped W
		float rowRestartAt_{ 0.0f };
		int rowRestartTries_{ 0 };
		bool fwdForced_{ false };
		bool rowRestartFinal_{ false };  // 56.23.3: the 3/3 restart was sent, the next look logs why if no clip        // inFwd_ put back by TendRowRestart (cleared when W is really up)
		bool rowAnimGpma_{ false };
		// 18.1 (v1.0.20): a refused OarsRowStart is retried (every 0.4 s, once the rower is seated again), not a reason for
		// the fallback; OffsetGPMA only if nobody's OarsRowStart was ever accepted this session and 10 were refused in a row.
		bool rowStartPending_{ false };
		float rowRetryWait_{ 0.0f };
		int rowRetryTries_{ 0 };
		float rowPendingFor_{ 0.0f };
		float lastRowWantAt_{ -100.0f };  // 56.8.B: ride time W was last held (rowPendingFor_ is kept over short taps)
		float rowAnimOnAt_{ 0.0f };     // 35.3: rideTime_ when the row clip was accepted
		float rowRequestAt_{ 0.0f };    // 35.3: rideTime_ when the row start was requested
		bool rowSyncLogged_{ true };    // 35.3: the "Row sync" line was written for this start
		static constexpr float kRowClipLeadIn = 0.12f;  // 35.3: oars hold at the catch this long after the clip is accepted (its blend in)
		float rowMenuHold_{ 0.0f };      // after a menu (console too) closes: nothing is sent for 0.5 s
		float playerSitAt_{ -100.0f };   // ride time the player's sit was last accepted
		static inline bool rowEverAccepted_{ false };  // OarsRowStart accepted once this session (not reset by loads)
		std::vector<std::pair<RE::FormID, int>> rowRejectStreak_;
		bool camNodesLogged_{ false };   // 17.3: node rotations logged once per boarding
		bool camHookLogged_{ false };    // 19: "hook fired" once per boarding
		bool camClampLogged_{ false };   // 21: the first pitch clamp of a boarding is logged at once
		bool camRootHookSeen_{ false };  // 19: the PlayerCamera::Update thunk ever ran
		bool camStateLogged_{ false };   // 22.3: camera state class once per boarding
		bool camForceLogged_{ false };   // 22.2: the first NiCamera force of a boarding is logged at once
		bool camSlot3Logged_{ false };   // 22.1: ThirdPersonState::Update (slot 3) fired, once per boarding
		RE::NiMatrix3 ncRel0_{};         // 22: the game's NiCamera rotation relative to cameraRoot
		bool ncRel0Valid_{ false };
		RE::NiPoint3 camStateFwd_{};     // 23: the forward ThirdPersonState::GetRotation handed back (after our clamp)
		bool camStateFwdValid_{ false };
		bool tpsPrevRowAnim_{ false };   // 27.1: row clip state last frame, to open the 3 s field log on a change
		int tpsBurst_{ 0 };              // 28: frames left in the per-frame burst after a row start
		std::chrono::steady_clock::time_point tpsLogAt_{};
		bool animObjFixOn_{ true };      // 28: AnimObjectA/B held at their pre-clip pose while the player rows (oas animobj)
		float animObjHoldUntil_{ -1.0f };
		float animObjHoldCap_{ -1.0f };
		float animObjMaxDelta_[2]{};
		std::chrono::steady_clock::time_point animObjLogAt_{};
		std::chrono::steady_clock::time_point animRotLogAt_{};
		int tpsLogLines_{ 0 };
		std::uint64_t camFrame_{ 0 };
		std::chrono::steady_clock::time_point camFinalLogAt_{};
		std::chrono::steady_clock::time_point camFixLogAt_{};      // the playing loop is the OffsetGPMA fallback, so it needs OffsetGPMAStop
		std::vector<std::pair<RE::FormID, bool>> rowAnimFallback_;  // Part 16: graphs (actor, first person) that refused OarsRowStart this boarding (use OffsetGPMA)
		float rowAnimOffTimer_{ 0.0f };
		float playerBodySide_{ 0.0f };   // slides the seated body across the boat (+ = right), third person only
		bool sitFailed_{ false };
		float physicsDelay_{ -1.0f };
	public:
		std::string physicsCmd_{ "smp reset" };  // console command run 1.5 s after boarding and after getting out, to clear stuck hair / face / bag physics (empty = off)
		// Runs one console command the way typing it in the ~ console would.
		void RunConsoleLine(const std::string& a_cmd)
		{
			if (a_cmd.empty()) return;
			auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
			auto* script = factory ? factory->Create() : nullptr;
			if (!script) { logger::warn("Console: could not create a script object for '{}'", a_cmd); return; }
			script->SetCommand(a_cmd);
			script->CompileAndRun(nullptr);
			delete script;
			logger::info("Console: ran '{}'", a_cmd);
		}
		void RunPhysicsCommand() { RunConsoleLine(physicsCmd_); }
		void ToggleFaceLock()
		{
			lockSeatedFacing_ = !lockSeatedFacing_;
			if (auto* pl = RE::PlayerCharacter::GetSingleton(); pl && playerSkim_) {
				SetActorHeadTracking(pl, !lockSeatedFacing_);
				pl->SetGraphVariableBool("bAllowRotation", true);
			}
		}
		bool lockSeatedFacing_{ true };  // the seated body faces along the boat and does not turn with the camera or mouse (`oas face` toggles)
		float fpEyeBoost_{ kFpEyeBoost };  // extra eye height in first person only (the body is hidden there); `oas "eye N"`
		bool playerSit_{ true };  // seat the player on the plank (falls back to a crouch if the game refuses)
	private:
		float dogYaw_{ 0.0f };
		float calmTime_{ 0.0f };
		int pickLogs_{ 0 };
		PassengerAnimSink passengerSink_;
		bool hopActive_{ false };     // late boarding: one follower hopping from the shore to their seat
		RE::ActorHandle hopActor_{};
		bool hopDog_{ false };
		float hopT_{ 0.0f };
		RE::NiPoint3 hopFrom_{};
		float lateTimer_{ 0.0f };
		std::vector<std::pair<RE::FormID, float>> lateDwell_;  // followers near the boat and how long they have been there
		std::vector<HumanSeat> tempSeats_;  // late boarders' seats for this ride only (see AssignLateSeat)
		// Seated crew (v1.0.11): stand-up watcher, faster re-sit, seated idles, flinch on bumps.
		static constexpr float kIdleMinGap = 20.0f;      // seconds between one actor's seated idles
		static constexpr float kIdleMaxGap = 60.0f;
		static constexpr float kRowStopDebounce = 0.6f;  // W released this long before the row animation stops (was 0.15)
		std::vector<CrewIdleState> crewIdle_;
		std::recursive_mutex crewIdleMx_;
		std::mt19937 crewRng_{ std::random_device{}() };
		float rideTime_{ 0.0f };         // seconds since boarding
		float lastRowStopAt_{ -100.0f }; // ride time of the last row animation stop
		int animEvLogs_{ 0 };            // AnimEv lines this ride (cap 400)
		std::unordered_map<RE::FormID, std::unordered_map<std::string, int>> crumbRepeat_;  // CrewNotify repeat counts (crewIdleMx_)
		float overCapLogCooldown_{ 0.0f };
		int glueMoves_{ 0 };
		std::unordered_map<RE::FormID, float> dogPickAt_;  // ride time of the last PickNewIdle answer per dog (crewIdleMx_)             // per-frame passenger / player SetPosition calls since the last heartbeat
		float refreshCalm_{ 0.0f };      // seconds since the last flight or slope (the ref refresh waits 3 s after a landing)
		bool flinchRefused_{ false };    // FlinchStart was refused while seated: no more flinches this session
		std::vector<std::string> rejectedIdles_;  // seated idle events this graph refused (logged once per session)
		IdleRoute idleRoute_{ IdleRoute::kUnknown };  // how seated idles play (12.3): graph events, idle forms, or nothing works
		bool chairIdlesScanned_{ false };
		std::vector<RE::TESIdleForm*> chairIdles_;    // ChairEat / ChairDrink / ChairFidget / ChairLook / ChairArms idle forms
		std::string bumpEvent_{ "FlinchStart" };      // the bump reaction; falls back to a head duck if the chair state refuses it
		std::chrono::steady_clock::time_point lastCrewTune_{};
	public:
		bool idlesOn_{ true };  // random eat / drink / fidget idles for the seated crew (`oas idles`)
		// ---- B7 (v1.0.15): no NPC greets the player while boarded ----------------------------------------------------------
		// The greeting distance game settings (the "hello when the player is close" AI) are set to 0 while boarded and put
		// back exactly on every exit; game settings are not saved, so nothing can be left changed in a save. NPC to NPC talk
		// does not use them. Every dialogue line spoken while boarded is logged (Dialogue:), and a dialogue menu that opens on
		// its own while boarded is closed.
		struct SavedSetting { RE::Setting* setting; float value; };
		std::vector<SavedSetting> greetSaved_;
		int dialogueLogs_{ 0 };
		void SuppressGreetings(bool a_on)
		{
			if (a_on) {
				if (!greetSaved_.empty()) return;
				auto* gs = RE::GameSettingCollection::GetSingleton();
				std::string found;
				for (const char* name : { "fAIMinGreetingDistance", "fAIMaxGreetingDistance", "fAIGreetingDistance" }) {
					auto* s = gs ? gs->GetSetting(name) : nullptr;
					if (!s || s->GetType() != RE::Setting::Type::kFloat) continue;
					greetSaved_.push_back({ s, s->data.f });
					found += std::format(" {}={:.0f}", name, s->data.f);
					s->data.f = 0.0f;
				}
				dialogueLogs_ = 0;
				logger::info("Dialogue: greetings off while boarded (set to 0:{})", found.empty() ? " no greeting setting found" : found);
			} else {
				if (greetSaved_.empty()) return;
				for (auto& g : greetSaved_) g.setting->data.f = g.value;
				greetSaved_.clear();
				logger::info("Dialogue: greetings back to normal");
			}
		}
		struct DialogueSink : public RE::BSTEventSink<RE::TESTopicInfoEvent>, public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::TESTopicInfoEvent* a_event, RE::BSTEventSource<RE::TESTopicInfoEvent>*) override
			{
				auto& self = OarsAndSails::Get();
				if (!a_event || !self.playerSkim_ || a_event->type != RE::TESTopicInfoEvent::TopicInfoEventType::kTopicBegin || self.dialogueLogs_ >= 60) return RE::BSEventNotifyControl::kContinue;
				++self.dialogueLogs_;
				auto* speaker = a_event->speakerRef.get();
				auto* player = RE::PlayerCharacter::GetSingleton();
				const float d = speaker && player ? speaker->GetPosition().GetDistance(player->GetPosition()) : -1.0f;
				bool crew = false;
				for (auto& h : self.passengers_) if (auto p = h.get(); p && speaker && p.get() == speaker) crew = true;
				logger::info("Dialogue: {} {:08X} line {:08X} (bark, {:.0f} units from the player{})", speaker ? speaker->GetName() : "?", speaker ? speaker->GetFormID() : 0u,
					a_event->topicInfoFormID, d, crew ? ", crew aboard" : "");
				return RE::BSEventNotifyControl::kContinue;
			}
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event) {
					// B12 (2): a menu eats the button's release; any menu opening or closing clears the Activate latch.
					OarsAndSails::Get().activatePressed_ = false;
					OarsAndSails::Get().pendingPick_.clear();
				}
				if (a_event && a_event->opening && OarsAndSails::Get().playerSkim_ && a_event->menuName == RE::DialogueMenu::MENU_NAME) {
					logger::info("Dialogue: the dialogue menu opened by itself while boarded, closing it");
					if (auto* q = RE::UIMessageQueue::GetSingleton()) q->AddMessage(RE::DialogueMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		void RegisterDialogueSink()
		{
			static DialogueSink sink;
			if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) holder->AddEventSink<RE::TESTopicInfoEvent>(&sink);
			if (auto* ui = RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
		}

		// v1.0.14: `oas pick` and the game's activate event for our Board / Steer / Row / chest objects.
		std::string PickReport() { return PickInfo(); }
		struct ActivateSink : public RE::BSTEventSink<RE::TESActivateEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override
			{
				if (a_event && a_event->actionRef && a_event->actionRef->IsPlayerRef() && a_event->objectActivated) {
					// B12 (v1.0.15): opening the chest activates the chest prop itself, which comes back here as "Chest" and
					// opened it again every frame (2077 times in the log). Only the player's own presses on our objects count.
					if (a_event->objectActivated.get() == BoatBow::ChestRef()) return RE::BSEventNotifyControl::kContinue;
					if (const char* tag = BoatBow::PickTag(a_event->objectActivated.get())) OarsAndSails::Get().OnPickActivated(tag);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		void RegisterActivateSink()
		{
			static ActivateSink sink;
			if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) holder->AddEventSink<RE::TESActivateEvent>(&sink);
		}

		// `oas "idles now"` (v1.0.14): one idle on the next fair pick, now. Returns who got it (empty: nobody could).
		std::string IdlesNow()
		{
			if (!playerSkim_) return {};
			return StartFairIdle(true);
		}

		// `oas idles` report: per crew member, eligible or why not, last idle age, cooldown left.
		std::string IdleReport()
		{
			std::string out = std::format("active {}/{}, next in {:.0f} s", ActiveIdles(), kMaxActiveIdles, (std::max)(0.0f, idleSchedTimer_));
			for (auto& h : passengers_) {
				auto a = h.get();
				if (!a || IsDogActor(a.get())) continue;
				auto* st = CrewIdleFor(a->GetFormID());
				if (!st) continue;
				std::string why;
				const bool ok = IdleEligible(a.get(), *st, why, false);
				const std::string age = st->lastIdleTime < -1.0e8f ? std::string("never") : std::format("{:.0f} s ago", rideTime_ - st->lastIdleTime);
				out += std::format("\n  {} (slot {}): {}{}, last idle {}, cooldown {:.0f} s", a->GetName(), GetHumanSeat(a.get()).slot, ok ? "eligible" : "no: ", why, age, (std::max)(0.0f, st->idleCooldown));
			}
			return out;
		}
		// Peace (v1.0.10): slaughterfish and mudcrabs leave the boat party alone while riding and kPeaceLingerSeconds after.
		// Their Aggression is set to 0 (they still defend themselves if attacked) and the original is always put back:
		// on exit / linger end, on an emergency end, when they are far away, and right before every save (so a save never
		// holds a pacified creature); the originals are also in the co-save in case the game stops mid-ride.
		static constexpr const char* kPeaceRaces[] = { "slaughterfish", "mudcrab" };
		static constexpr float kPeaceLingerSeconds = 15.0f;
		struct Calmed { RE::FormID id; float aggression; };
		std::vector<Calmed> calmed_;
		bool peaceOn_{ true };
		float peaceScan_{ 0.0f };
		float peaceLinger_{ 0.0f };
		std::size_t peaceLastCount_{ 0 };

		static bool IsPeaceRace(RE::Actor* a_actor)
		{
			const auto* race = a_actor ? a_actor->GetRace() : nullptr;
			if (!race) return false;
			std::string id = race->GetFormEditorID() ? race->GetFormEditorID() : "";
			id += ' ';
			id += race->GetName() ? race->GetName() : "";
			for (auto& ch : id) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			for (const char* r : kPeaceRaces) if (id.find(r) != std::string::npos) return true;
			return false;
		}

		bool IsBoatParty(RE::Actor* a_actor)
		{
			if (!a_actor) return false;
			if (a_actor == RE::PlayerCharacter::GetSingleton()) return true;
			return IsAboard(a_actor) || IsCrew(a_actor);
		}

		void RestorePeace(const char* a_why)
		{
			if (calmed_.empty()) return;
			int restored = 0;
			for (const auto& c : calmed_) {
				if (auto* a = RE::TESForm::LookupByID<RE::Actor>(c.id)) {
					if (auto* avo = a->AsActorValueOwner()) { avo->SetBaseActorValue(RE::ActorValue::kAggression, c.aggression); ++restored; }
				}
			}
			logger::info("Peace: {} restored ({})", restored, a_why);
			calmed_.clear();
			peaceLastCount_ = 0;
		}

		void UpdatePeace(RE::PlayerCharacter* a_player, float a_dt)
		{
			if (!playerSkim_ && peaceLinger_ > 0.0f) peaceLinger_ -= a_dt;
			const bool active = peaceOn_ && (playerSkim_ || peaceLinger_ > 0.0f);
			if (!active) { RestorePeace(peaceOn_ ? "the ride is over" : "peace switched off"); return; }
			if ((peaceScan_ -= a_dt) > 0.0f) return;
			peaceScan_ = 0.5f;
			const RE::NiPoint3 pp = a_player->GetPosition();
			// Hand back the ones that wandered off or unloaded.
			for (auto it = calmed_.begin(); it != calmed_.end();) {
				auto* a = RE::TESForm::LookupByID<RE::Actor>(it->id);
				if (a && a->GetPosition().GetDistance(pp) <= 5000.0f) { ++it; continue; }
				if (a) if (auto* avo = a->AsActorValueOwner()) avo->SetBaseActorValue(RE::ActorValue::kAggression, it->aggression);
				it = calmed_.erase(it);
			}
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) return;
			const bool first = calmed_.empty();
			int added = 0;
			for (auto& handle : lists->highActorHandles) {
				auto actor = handle.get();
				if (!actor || actor->IsDead() || !IsPeaceRace(actor.get())) continue;
				if (actor->GetPosition().GetDistance(pp) > 4000.0f) continue;
				const RE::FormID id = actor->GetFormID();
				if (std::any_of(calmed_.begin(), calmed_.end(), [&](const Calmed& c) { return c.id == id; })) continue;
				auto* avo = actor->AsActorValueOwner();
				if (!avo) continue;
				calmed_.push_back({ id, avo->GetBaseActorValue(RE::ActorValue::kAggression) });
				avo->SetBaseActorValue(RE::ActorValue::kAggression, 0.0f);
				++added;
				// Already fighting us when it was calmed: stop that once. A fight we start later is left alone.
				if (actor->IsInCombat()) {
					auto target = actor->GetActorRuntimeData().currentCombatTarget.get();
					if (target && IsBoatParty(target.get())) {
						actor->StopCombat();
						if (target.get() != a_player && target->IsInCombat()) target->StopCombat();
					}
				}
			}
			if (first && added > 0) logger::info("Peace: {} creature(s) calmed", added);
			else if (calmed_.size() != peaceLastCount_) logger::info("Peace: {} creature(s) calm now", calmed_.size());
			peaceLastCount_ = calmed_.size();
		}

		// Crew (v1.0.9): any NPC or dog added with `oas crew add` boards like a follower. Kept in the SKSE co-save.
		std::vector<RE::FormID> crew_;
		std::vector<RE::FormID> summonQueue_;  // `oas crew summon`: placed one at a time, 0.3 s apart
		float summonTimer_{ 0.0f };
		RE::NiPoint3 summonCentre_{};
		int summonIndex_{ 0 }, summonTotal_{ 0 };
		bool IsCrew(RE::Actor* a_actor) const
		{
			return a_actor && std::find(crew_.begin(), crew_.end(), a_actor->GetFormID()) != crew_.end();
		}
		bool IsAboard(RE::Actor* a_actor)
		{
			for (auto& h : passengers_) { if (auto p = h.get(); p && p.get() == a_actor) return true; }
			return false;
		}
		int CrewSeat(RE::Actor* a_actor)
		{
			if (!IsAboard(a_actor) || IsDogActor(a_actor)) return -1;
			return GetHumanSeat(a_actor).slot;
		}
		int CrewHumans()
		{
			int n = 0;
			for (auto id : crew_) {
				auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
				if (!a || !IsDogActor(a)) ++n;  // an unloaded member counts as a human seat
			}
			return n;
		}
		// Where summoned crew go: the parked boat, the boat being ridden, or the player.
		RE::NiPoint3 SummonCentre(RE::PlayerCharacter* a_player)
		{
			float bx = 0.0f, by = 0.0f, bz = 0.0f, byaw = 0.0f;
			if (playerSkim_) {
				BoatBow::BoatCenterFromVehicle(vehiclePos_.x, vehiclePos_.y, skimHeading_, bx, by);
				return RE::NiPoint3{ bx, by, skimWaterHeight_ };
			}
			if (BoatBow::GetParked(bx, by, bz, byaw)) return RE::NiPoint3{ bx, by, bz };
			return a_player->GetPosition();
		}
		void UpdateSummon(RE::PlayerCharacter* a_player, float a_dt)
		{
			if (summonQueue_.empty() || (summonTimer_ -= a_dt) > 0.0f) return;
			summonTimer_ = 0.3f;
			const RE::FormID id = summonQueue_.front();
			summonQueue_.erase(summonQueue_.begin());
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
			auto* cell = actor ? actor->GetParentCell() : nullptr;
			if (!actor || actor->IsDead() || !cell || !cell->IsAttached() || !actor->Is3DLoaded() || IsAboard(actor)) return;
			// A loose ring about 250 units out, facing the centre, on the ground (or the water surface) there.
			const float a = 6.2831853f * static_cast<float>(summonIndex_++) / static_cast<float>((std::max)(1, summonTotal_)) + 0.4f;
			RE::NiPoint3 pos{ summonCentre_.x + std::sin(a) * 250.0f, summonCentre_.y + std::cos(a) * 250.0f, summonCentre_.z };
			float land = 0.0f, water = 0.0f;
			auto* tes = RE::TES::GetSingleton();
			const bool haveLand = tes && tes->GetLandHeight(pos, land) && std::isfinite(land);
			const bool haveWater = SkimWaterAt(pos, water);
			if (haveLand || haveWater) pos.z = (std::max)(haveLand ? land : -1.0e9f, haveWater ? water : -1.0e9f) + 2.0f;
			logger::info("Move: {:08X} '{}' summoned", actor->GetFormID(), actor->GetName());  // crash breadcrumb (v1.0.13)
			actor->SetPosition(pos, true);
			RE::NiPoint3 ang = actor->GetAngle();
			ang.z = std::atan2(summonCentre_.x - pos.x, summonCentre_.y - pos.y);
			actor->SetAngle(ang);
			logger::info("Crew: {} {:08X} summoned to ({:.0f},{:.0f},{:.0f})", actor->GetName(), id, pos.x, pos.y, pos.z);
			(void)a_player;
		}
	private:
		RE::NiPoint3 lastFeet_{};           // where the player was last put while riding (teleport guard)
		RE::TESWorldSpace* boardWorld_{ nullptr };
		bool boardInterior_{ false };
		float cellLostTime_{ 0.0f };
		bool strayCleanupPending_{ false };
		int passengerLogs_{ 0 };
		RE::NiPoint3 boardSpot_{};
		RE::NiPoint3 skimLastGoodPos_{};
		bool hasLastGood_{ false };
		float edgeHold_{ 0.0f };
		bool boardPrompted_{ false };
		float boardRaise_{ 0.0f };
		bool activatePressed_{ false };
		std::chrono::steady_clock::time_point activateStamp_{};
		std::uint32_t keyActivate_{ 0x12 };
		bool hasPrevVehicle_{ false };
		RE::NiPoint3 prevVehicle_{};
		std::uint32_t keyFwd_{ 0x11 }, keyBack_{ 0x1F }, keyLeft_{ 0x1E }, keyRight_{ 0x20 };
		bool inFwd_{ false }, inBack_{ false }, inLeft_{ false }, inRight_{ false };
		float stickX_{ 0.0f }, stickY_{ 0.0f };
	};

	// First person camera update: same idea, so passengers keep their tilt in first person.
	struct FirstPersonUpdateHook
	{
		static void Thunk(RE::FirstPersonState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_nextState)
		{
			func(a_this, a_nextState);
			OarsAndSails::Get().ReapplyBodyHide();
		}

		static inline REL::Relocation<decltype(Thunk)> func;

		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_FirstPersonState[0] };
			func = vtbl.write_vfunc(4, Thunk);
		}
	};

	// Third person camera update: runs after the player update, right before
	// rendering, so the hidden body stays hidden even if the game re-shows it.
	// v1.0.21 (Part 19): slot 4 of ThirdPersonState is GetRotation on SE/AE (TESCameraState: Begin 1, End 2, Update 3,
	// GetRotation 4, GetTranslation 5), not Update as this hook was first named. It is called every frame, so the body
	// hide and the upright camera fix both run here.
	struct ThirdPersonUpdateHook
	{
		static void Thunk(RE::ThirdPersonState* a_this, RE::NiQuaternion& a_rotation)
		{
			func(a_this, a_rotation);
			OarsAndSails::Get().ReapplyBodyHide();
			OarsAndSails::Get().FixCamRotation(a_this, a_rotation);
		}

		static inline REL::Relocation<decltype(Thunk)> func;

		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ThirdPersonState[0] };
			func = vtbl.write_vfunc(4, Thunk);
		}
	};

	// 21.4 (v1.0.22): ThirdPersonState::Update (slot 3 on SE/AE), before the game's own: the camera bones our clip may
	// have turned are put back first, so the camera reads them as they were.
	struct ThirdPersonStateUpdateHook
	{
		static void Thunk(RE::ThirdPersonState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_nextState)
		{
			OarsAndSails::Get().GuardCamBones();
			OarsAndSails::Get().HoldAnimObjects("before ThirdPersonState::Update");
			OarsAndSails::Get().ResetAnimRotation(a_this, "before ThirdPersonState::Update");
			func(a_this, a_nextState);
			OarsAndSails::Get().CamAfterStateUpdate(a_this);
		}

		static inline REL::Relocation<decltype(Thunk)> func;

		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ThirdPersonState[0] };
			func = vtbl.write_vfunc(3, Thunk);
		}
	};

	// 18.2 (v1.0.20): PlayerCamera::Update (TESCamera slot 2), so the upright fix runs after the camera, in any state.
	// (v1.0.19 hooked ThirdPersonState slot 5, which on SE/AE is GetTranslation, not GetRotation: it never ran.)
	struct PlayerCameraUpdateHook
	{
		static void Thunk(RE::PlayerCamera* a_this)
		{
			func(a_this);
			OarsAndSails::Get().FixCameraUpright(a_this);
			OarsAndSails::Get().FilterCameraZ(a_this);  // 47.5
		}

		static inline REL::Relocation<decltype(Thunk)> func;

		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCamera[0] };
			func = vtbl.write_vfunc(2, Thunk);
		}
	};

	// ------------------------------------------------------------------
	// 56.26 (v1.0.75): BGSSaveLoadManager::Save_Impl (34818 / 35727), the function every save goes through (the save queue
	// for autosave / quicksave / menu save, console save, Papyrus) before anything is captured. Its args are
	// (manager, type, flags, name); with no name the game names it from the type (1.7.104: 3 Autosave, 4 Quicksave, else
	// Save). The first whole instructions (5 bytes or more) move to a stub that jumps back; an entry that already jumps
	// (another hook) is chained. Unknown entry bytes: not hooked, logged, the 56.11.4 load release stays the safety net.
	// ------------------------------------------------------------------
	struct SaveHook
	{
		static bool Thunk(RE::BGSSaveLoadManager* a_this, std::int32_t a_type, std::uint32_t a_stats, const char* a_name)
		{
			auto& oas = OarsAndSails::Get();
			if (!oas.savePassThrough_ && oas.OnSaveRequest(a_this, a_type, a_stats, a_name)) return false;
			oas.MarkSaving();
			return oas.saveOriginal_(a_this, a_type, a_stats, a_name);
		}

		// Length of the whole instructions covering a_need bytes, if they are all ones that can run from anywhere: push,
		// and mov / lea / add-sub style reg or [reg+disp] forms. 0 for anything else (rip-relative, branches, unknown).
		static std::size_t PrologueLength(const std::uint8_t* a_code, std::size_t a_need)
		{
			std::size_t at = 0;
			while (at < a_need) {
				const std::uint8_t* p = a_code + at;
				const std::size_t i = (p[0] & 0xF0) == 0x40 ? 1 : 0;  // REX
				const std::uint8_t op = p[i];
				std::size_t len = 0;
				if (op >= 0x50 && op <= 0x57) {
					len = i + 1;  // push reg
				} else if (op == 0x89 || op == 0x8B || op == 0x8D || op == 0x83 || op == 0x81) {
					const std::uint8_t modrm = p[i + 1];
					const int mod = modrm >> 6, rm = modrm & 7;
					if (mod == 0 && rm == 5) return 0;  // rip-relative
					if (op == 0x8D && mod == 3) return 0;
					len = i + 2;
					if (mod != 3 && rm == 4) {
						if (mod == 0 && (p[i + 2] & 7) == 5) return 0;  // SIB with no base (disp32)
						len += 1;
					}
					len += mod == 1 ? 1 : mod == 2 ? 4 : 0;
					if (op == 0x83) len += 1;
					if (op == 0x81) len += 4;
				} else {
					return 0;
				}
				at += len;
			}
			return at;
		}

		static void Install()
		{
			REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(34818, 35727) };
			const std::uintptr_t addr = target.address();
			const auto* code = reinterpret_cast<const std::uint8_t*>(addr);
			std::string bytes;
			for (int i = 0; i < 16; ++i) bytes += std::format("{}{:02X}", i ? " " : "", code[i]);
			auto& tramp = SKSE::GetTrampoline();
			std::uintptr_t original = 0;
			std::string how;
			if (code[0] == 0xE9) {
				std::int32_t rel = 0;
				std::memcpy(&rel, code + 1, 4);
				original = addr + 5 + static_cast<std::intptr_t>(rel);
				how = "chained after an existing jmp";
			} else if (code[0] == 0xFF && code[1] == 0x25) {
				std::int32_t disp = 0;
				std::memcpy(&disp, code + 2, 4);
				original = *reinterpret_cast<const std::uintptr_t*>(addr + 6 + static_cast<std::intptr_t>(disp));
				how = "chained after an existing jmp [rip]";
			} else {
				const std::size_t n = PrologueLength(code, 5);
				if (n == 0 || n > 16) {
					logger::warn("Save: not hooked (Save_Impl entry {} not recognised): saves aboard are not held; kSaveGame is logged and the load release frees the player", bytes);
					return;
				}
				auto* stub = static_cast<std::uint8_t*>(tramp.allocate(n + 14));
				std::memcpy(stub, code, n);
				stub[n] = 0xFF;  // jmp [rip+0] -> back into Save_Impl after the moved instructions
				stub[n + 1] = 0x25;
				std::memset(stub + n + 2, 0, 4);
				const std::uintptr_t back = addr + n;
				std::memcpy(stub + n + 6, &back, 8);
				original = reinterpret_cast<std::uintptr_t>(stub);
				how = std::format("{} entry bytes moved to a stub", n);
			}
			OarsAndSails::Get().saveOriginal_ = reinterpret_cast<OarsAndSails::SaveFn>(original);
			tramp.write_branch<5>(addr, &Thunk);
			logger::info("Save: hooked BGSSaveLoadManager::Save_Impl (id {}, {}; entry {})", REL::Module::IsAE() ? 35727 : 34818, how, bytes);
		}
	};

	// ------------------------------------------------------------------
	// v7 per-frame hook: PlayerCharacter::Update (vtable slot 0xAD; 0xAF on VR).
	// ------------------------------------------------------------------
	struct PlayerUpdateHook
	{
		static void Thunk(RE::PlayerCharacter* a_this, float a_delta)
		{
			func(a_this, a_delta);
			OarsAndSails::Get().UpdatePlayerSkim(a_this, a_delta);
			OarsAndSails::Get().HoldAnimObjects("player update");
			OarsAndSails::Get().ForceFinalCamera("player update");
		}

		static inline REL::Relocation<decltype(Thunk)> func;

		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
			func = vtbl.write_vfunc(REL::Module::IsVR() ? 0xAF : 0xAD, Thunk);
		}
	};

	// ------------------------------------------------------------------
	// Console command
	// ------------------------------------------------------------------
	// Single optional freeform-text parameter (kChar -> StringChunk):
	// oas                 -> list the subcommands
	// oas boat / unboat   -> place / remove the parked rowboat (walk up, E to board, E to get off)
	// oas skim            -> ride a boat from where you stand (no parked boat needed)
	// oas "debug <cmd>"   -> diagnostics: watchme, unwatchme, anim, idles, idle, sit, stand
	static RE::SCRIPT_PARAMETER kOasParams[] = {
		{ "arg", RE::SCRIPT_PARAM_TYPE::kChar, true }
	};

	// ---- Idle Animation records (the game's list of animations and the .hkx file each one plays) ----
	std::string LowerCopy(const char* a_text)
	{
		std::string r = a_text ? a_text : "";
		std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return r;
	}

	struct IdleInfo
	{
		std::uint32_t id{ 0 };
		std::string   editor;      // EditorID when the game still has it (often empty at runtime)
		std::string   eventName;   // animation event that triggers it, e.g. IdleChairSitting
		std::string   file;        // path of the .hkx inside the game's animation archive
		std::string   eventLower;
		std::string   fileLower;
	};

	// Built once, the first time it is needed.
	const std::vector<IdleInfo>& IdleTable()
	{
		static std::vector<IdleInfo> s_table;
		static bool s_built = false;
		if (s_built) return s_table;
		s_built = true;
		// The data handler's idle list came back empty (0 records in the first test), so walk every form
		// the game has loaded and keep the Idle Animation ones.
		const auto& [allForms, allFormsLock] = RE::TESForm::GetAllForms();
		if (!allForms) return s_table;
		const RE::BSReadLockGuard formLock{ allFormsLock };
		for (auto& [formId, form] : *allForms) {
			if (!form || form->GetFormType() != RE::FormType::Idle) continue;
			auto* idle = static_cast<RE::TESIdleForm*>(form);
			IdleInfo info;
			info.id = idle->GetFormID();
			const char* ed = idle->GetFormEditorID();
			info.editor = ed ? ed : "";
			info.eventName = idle->animEventName.c_str() ? idle->animEventName.c_str() : "";
			info.file = idle->animFileName.c_str() ? idle->animFileName.c_str() : "";
			info.eventLower = LowerCopy(info.eventName.c_str());
			info.fileLower = LowerCopy(info.file.c_str());
			s_table.push_back(std::move(info));
		}
		logger::info("IdleTable: {} idle animation records loaded", s_table.size());
		return s_table;
	}

	// oas idles <word>: writes every idle record whose event, file or editor name contains the word.
	std::uint32_t LogIdleSearch(const std::string& a_wordLower)
	{
		std::uint32_t n = 0;
		for (const auto& i : IdleTable()) {
			if (i.eventLower.find(a_wordLower) == std::string::npos &&
				i.fileLower.find(a_wordLower) == std::string::npos &&
				LowerCopy(i.editor.c_str()).find(a_wordLower) == std::string::npos) continue;
			logger::info("IdleSearch: {:08X} editor '{}' event '{}' file '{}'", i.id, i.editor, i.eventName, i.file);
			++n;
		}
		logger::info("IdleSearch: '{}' -> {} match(es)", a_wordLower, n);
		return n;
	}

	// Passive recorder: logs every animation event on a watched actor, with a few
	// behavior-graph variables, so we can see exactly how the game makes a dog sit.
	// Each event is also looked up in the idle records so the .hkx file it plays is written next to it.
	struct AnimWatchSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
	{
		RE::ActorHandle target;

		RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
		{
			if (!a_event) return RE::BSEventNotifyControl::kContinue;
			auto actor = target.get();
			if (!actor) return RE::BSEventNotifyControl::kContinue;
			const std::string ev = LowerCopy(a_event->tag.c_str());
			// Skip the constant walking / tail / head-tracking chatter so the log stays readable.
			if (ev.rfind("foot", 0) == 0 || ev.rfind("tail", 0) == 0 || ev.rfind("turn", 0) == 0 ||
				ev == "headtrackingon" || ev == "headtrackingoff" || ev == "idleoffsetstop" || ev == "idlestop") {
				return RE::BSEventNotifyControl::kContinue;
			}
			bool sitting = false, lying = false;
			std::int32_t state = -1, dogState = -1, idleLoco = -1;
			actor->GetGraphVariableBool("isIdleSitting", sitting);
			actor->GetGraphVariableBool("isIdleLay", lying);
			actor->GetGraphVariableInt("iState", state);
			actor->GetGraphVariableInt("iState_DogDefault", dogState);
			actor->GetGraphVariableInt("iSyncIdleLocomotion", idleLoco);
			logger::info("AnimWatch: event '{}' payload '{}' | isIdleSitting={} isIdleLay={} iState={} iState_DogDefault={} iSyncIdleLocomotion={}",
				a_event->tag.c_str(), a_event->payload.c_str(), sitting, lying, state, dogState, idleLoco);

			if (!ev.empty()) {
				std::uint32_t shown = 0;
				for (const auto& i : IdleTable()) {
					if (i.eventLower != ev) continue;
					logger::info("AnimWatch:    -> idle record {:08X} editor '{}' file '{}'", i.id, i.editor, i.file);
					if (++shown >= 6) break;
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
	inline AnimWatchSink g_animWatch;

	// Writes the race and every animation event / variable name the actor's behavior
	// graph defines, so the right sit event for a creature can be read from the log.
	void DumpActorAnimation(RE::Actor* a_actor)
	{
		if (!a_actor) return;
		const auto* race = a_actor->GetRace();
		logger::info("AnimDump: actor {:08X} base {:08X} race '{}' ({:08X})",
			a_actor->GetFormID(), a_actor->GetBaseObject() ? a_actor->GetBaseObject()->GetFormID() : 0u,
			race ? race->GetFormEditorID() : "?", race ? race->GetFormID() : 0u);
		RE::BSTSmartPointer<RE::BSAnimationGraphManager> mgr;
		if (!a_actor->GetAnimationGraphManager(mgr) || !mgr) {
			logger::warn("AnimDump: no animation graph manager");
			return;
		}
		const auto count = mgr->graphs.size();
		logger::info("AnimDump: {} graph(s), active index {}", count, mgr->GetRuntimeData().activeGraph);
		for (std::uint32_t g = 0; g < count; ++g) {
			auto& graph = mgr->graphs[g];
			auto* behavior = graph ? graph->behaviorGraph : nullptr;
			auto* data = behavior ? behavior->data.get() : nullptr;
			auto* strings = data ? data->stringData.get() : nullptr;
			if (!strings) {
				logger::warn("AnimDump: graph {} has no string data", g);
				continue;
			}
			logger::info("AnimDump: graph {}: {} events, {} variables", g, strings->eventNames.size(), strings->variableNames.size());
			for (int i = 0; i < strings->eventNames.size(); ++i) {
				logger::info("AnimDump: graph {} event[{}] {}", g, i, strings->eventNames[i].c_str());
			}
			for (int i = 0; i < strings->variableNames.size(); ++i) {
				logger::info("AnimDump: graph {} variable[{}] {}", g, i, strings->variableNames[i].c_str());
			}
		}
	}

	// Every animation graph variable of the actor with its value read as bool / int / float. Taken before boarding and again
	// after getting out, the two lists can be compared to see exactly what the ride left different.
	void DumpAllGraphVars(RE::Actor* a_actor, const char* a_tag)
	{
		if (!a_actor) return;
		RE::BSTSmartPointer<RE::BSAnimationGraphManager> mgr;
		if (!a_actor->GetAnimationGraphManager(mgr) || !mgr) return;
		int total = 0;
		for (std::uint32_t g = 0; g < mgr->graphs.size(); ++g) {
			auto& graph = mgr->graphs[g];
			auto* behavior = graph ? graph->behaviorGraph : nullptr;
			auto* data = behavior ? behavior->data.get() : nullptr;
			auto* strings = data ? data->stringData.get() : nullptr;
			if (!strings) continue;
			for (int i = 0; i < strings->variableNames.size(); ++i) {
				const char* name = strings->variableNames[i].c_str();
				if (!name) continue;
				bool b = false;
				std::int32_t n = 0;
				float f = 0.0f;
				const bool okB = a_actor->GetGraphVariableBool(name, b);
				const bool okI = a_actor->GetGraphVariableInt(name, n);
				const bool okF = a_actor->GetGraphVariableFloat(name, f);
				logger::info("VarDump[{}] g{} {} b={} i={} f={}", a_tag, g, name,
					okB ? (b ? "1" : "0") : "-", okI ? std::to_string(n) : std::string("-"), okF ? std::format("{:.3f}", f) : std::string("-"));
				++total;
			}
		}
		logger::info("VarDump[{}] done, {} variables", a_tag, total);
	}

	// 46 (v1.0.42): console forgiveness. The game passes one word unless it is quoted, so glued spellings are split:
	// "flowpull180", "flow pull180", "hullfeellen300", "paintblue", "icelist", "eye10" -> "flow pull 180" etc.
	std::string SplitGlued(const std::string& a_tok)
	{
		std::size_t i = 0;
		while (i < a_tok.size() && std::isalpha(static_cast<unsigned char>(a_tok[i]))) ++i;
		if (i == 0 || i == a_tok.size()) return a_tok;
		const char c = a_tok[i];
		if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-')) return a_tok;
		return a_tok.substr(0, i) + " " + a_tok.substr(i);
	}
	std::string NormalizeOas(std::string a_in)
	{
		std::erase(a_in, '"');
		std::istringstream in(a_in);
		std::vector<std::string> toks;
		for (std::string t; in >> t;) toks.push_back(t);
		if (toks.empty()) return a_in;
		static constexpr const char* kPrefixes[] = { "hullfeel", "flow", "paint", "ice", "eye", "lakehere", "lakepoint", "lakedel", "lakeboats" };
		// One glued word: peel the command name off the front.
		if (toks.size() == 1) {
			for (const char* pre : kPrefixes) {
				const std::string_view pv(pre);
				if (toks[0].size() > pv.size() && toks[0].rfind(pre, 0) == 0) {
					const std::string rest = toks[0].substr(pv.size());
					const std::string split = SplitGlued(rest);
					return std::string(pre) + " " + split;
				}
			}
			return toks[0];
		}
		// Spaced: split glued name+number tokens after a known command name ("flow pull180").
		const bool known = std::any_of(std::begin(kPrefixes), std::end(kPrefixes), [&](const char* p) { return toks[0] == p; });
		std::string out;
		for (std::size_t i = 0; i < toks.size(); ++i) {
			out += i ? " " : "";
			out += (known && i > 0) ? SplitGlued(toks[i]) : toks[i];
		}
		return out;
	}
	bool IsFlowKey(const std::string& a_name)
	{
		for (const char* k : { "pull", "rapids", "range", "curve", "capture", "turn", "throw", "throwmax", "kick", "reset", "probe", "on", "off" })
			if (a_name == k) return true;
		return false;
	}

	bool ExecuteOas(
		const RE::SCRIPT_PARAMETER*,
		RE::SCRIPT_FUNCTION::ScriptData* a_scriptData,
		RE::TESObjectREFR*,
		RE::TESObjectREFR*,
		RE::Script*,
		RE::ScriptLocals*,
		double&,
		std::uint32_t&)
	{
		// numParams == 0 means the user typed bare "oas" with no argument;
		// the console parser only populates chunks when an argument was actually typed.
		if (a_scriptData && a_scriptData->numParams > 0) {
			if (auto* strChunk = a_scriptData->GetStringChunk()) {
				std::string arg = strChunk->GetString();
				std::string lower = arg;
				std::transform(lower.begin(), lower.end(), lower.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (lower.rfind("debug ", 0) != 0) {
					const std::string norm = NormalizeOas(lower);
					if (norm != lower) logger::info("ExecuteOas: '{}' read as '{}'", arg, norm);
					lower = norm;
				}

				auto* console = RE::ConsoleLog::GetSingleton();

				// oas "debug <cmd>": the animation diagnostics live behind this prefix.
				bool debug = false;
				if (lower.rfind("debug ", 0) == 0) {
					debug = true;
					arg = arg.substr(6);
					lower = lower.substr(6);
				}
				if (debug) {

					if (lower == "sit" || lower == "stand") {
						auto selected = RE::Console::GetSelectedRef();
						auto* actor = selected ? selected->As<RE::Actor>() : nullptr;
						if (!actor) {
							if (console) console->Print("Oars and Sails: click an actor first, then: oas \"debug sit\" / oas \"debug stand\"");
							return true;
						}
						const bool sit = lower == "sit";
						bool viaEvent = false;
						if (sit) {
							viaEvent = actor->NotifyAnimationGraph("idleSitStart");
							logger::info("oas sit: event idleSitStart -> {}", viaEvent);
							if (!viaEvent) {
								const bool a = actor->SetGraphVariableBool("isIdleLay", false);
								const bool b = actor->SetGraphVariableBool("isIdleSitting", true);
								logger::info("oas sit: variables isIdleLay=false -> {}, isIdleSitting=true -> {}", a, b);
							}
						} else {
							for (const char* ev : { "returnToIdle", "idleExit", "IdleStop" }) {
								const bool ok = actor->NotifyAnimationGraph(ev);
								logger::info("oas stand: event {} -> {}", ev, ok);
								if (ok) { viaEvent = true; break; }
							}
							if (!viaEvent) {
								actor->SetGraphVariableBool("isIdleSitting", false);
								actor->SetGraphVariableBool("isIdleLay", false);
								logger::info("oas stand: cleared isIdleSitting / isIdleLay");
							}
						}
						if (console) console->Print(sit ? "Oars and Sails: sit tried - see OarsAndSails.log" : "Oars and Sails: stand tried - see OarsAndSails.log");
						return true;
					}
					if (lower == "anim") {
						// Dumps every animation event / variable name the clicked actor's behavior graph knows to the log.
						auto selected = RE::Console::GetSelectedRef();
						auto* actor = selected ? selected->As<RE::Actor>() : nullptr;
						if (!actor) {
							if (console) console->Print("Oars and Sails: click an actor first, then: oas \"debug anim\"");
							return true;
						}
						DumpActorAnimation(actor);
						if (console) console->Print("Oars and Sails: animation names written to OarsAndSails.log");
						return true;
					}
					if (lower == "watchme" || lower == "unwatchme") {
						// Watches YOU (no clicking needed): sit down, stand up, then read OarsAndSails.log.
						auto* player = RE::PlayerCharacter::GetSingleton();
						if (!player) return true;
						if (lower == "watchme") {
							g_animWatch.target = player->GetHandle();
							player->AddAnimationGraphEventSink(&g_animWatch);
							(void)IdleTable();
							if (console) console->Print("Oars and Sails: watching your animation events in OarsAndSails.log - sit, stand, then read it (oas \"debug unwatchme\" to stop)");
						} else {
							player->RemoveAnimationGraphEventSink(&g_animWatch);
							if (console) console->Print("Oars and Sails: stopped watching you");
						}
						return true;
					}
					if (lower.rfind("idles", 0) == 0) {
						// oas "idles <word>": lists idle animation records (event + .hkx file) matching the word.
						std::string word = arg.size() > 5 ? arg.substr(5) : std::string{};
						while (!word.empty() && (word.front() == ':' || word.front() == ' ')) word.erase(word.begin());
						std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						if (word.empty()) {
							if (console) console->Print("Oars and Sails: usage: oas \"debug idles stool\" (results go to OarsAndSails.log)");
							return true;
						}
						const auto n = LogIdleSearch(word);
						if (console) console->Print("Oars and Sails: %u idle record(s) match '%s' - see OarsAndSails.log", n, word.c_str());
						return true;
					}
					if (lower.rfind("idle", 0) == 0) {
						// oas idle:<AnimationEvent> (or "idle <AnimationEvent>") plays that event on the clicked actor.
						std::string ev = arg.size() > 4 ? arg.substr(4) : std::string{};
						while (!ev.empty() && (ev.front() == ':' || ev.front() == ' ')) ev.erase(ev.begin());
						auto selected = RE::Console::GetSelectedRef();
						auto* actor = selected ? selected->As<RE::Actor>() : nullptr;
						if (!actor || ev.empty()) {
							if (console) console->Print("Oars and Sails: click an actor first, then: oas \"debug idle:<AnimationEvent>\"");
							return true;
						}
						const bool ok = actor->NotifyAnimationGraph(ev.c_str());
						bool sitting = false, lying = false;
						std::int32_t state = -1;
						actor->GetGraphVariableBool("isIdleSitting", sitting);
						actor->GetGraphVariableBool("isIdleLay", lying);
						actor->GetGraphVariableInt("iState", state);
						logger::info("oas idle: actor {:08X} event '{}' -> {} (isIdleSitting={} isIdleLay={} iState={})", actor->GetFormID(), ev, ok, sitting, lying, state);
						if (console) console->Print(ok ? "Oars and Sails: animation event accepted" : "Oars and Sails: animation event rejected (wrong name for this actor)");
						return true;
					}
					if (console) console->Print("Oars and Sails: debug commands: watchme, unwatchme, anim, idles <word>, idle:<event>, sit, stand");
					return true;
				}

				if (lower == "reset settings" || lower == "settings reset" || lower == "resetsettings") {
					OarsAndSails::Get().ResetPlayerSettings();
					if (console) console->Print("Oars and Sails: settings back to the defaults (height, floor, speed, turning, ripples, dog seats, NPC boats)");
					return true;
				}
				if (lower == "floor" || lower.rfind("floor ", 0) == 0) {
					// 56.35: oas "floor N" sets the bilge floor's height offset (units from its built height, -30..20); oas floor shows it
					const std::string rest = lower.size() > 5 ? lower.substr(6) : std::string{};
					if (rest.find_first_not_of(' ') != std::string::npos) {
						try { BoatBow::SetFloorOffset(std::stof(rest)); } catch (...) {
							if (console) console->Print("Oars and Sails: floor takes a number, -30 to 20 (0 = built height)");
							return true;
						}
					}
					if (console) console->Print("Oars and Sails: floor %+.1f (oas \"num floor\" to move it with Num9 / Num3)", BoatBow::FloorOffset());
					return true;
				}
				if (lower == "num" || lower.rfind("num ", 0) == 0) {
					// 56.29: oas "num <target>" turns the numpad on for one thing; oas "num off"; oas num shows it
					std::string word = lower.size() > 3 ? lower.substr(4) : std::string{};
					while (!word.empty() && word.front() == ' ') word.erase(word.begin());
					while (!word.empty() && word.back() == ' ') word.pop_back();
					std::string msg;
					OarsAndSails::Get().SetNumMode(word, msg);
					if (console) console->Print("Oars and Sails: %s", msg.c_str());
					return true;
				}
				if (lower.rfind("boat height", 0) == 0) {
					// 56.26: oas "boat height N" sets the player's boat height (-30..30); oas "boat height" shows it
					auto& oas = OarsAndSails::Get();
					const std::string rest = lower.size() > 11 ? lower.substr(11) : std::string{};
					if (rest.find_first_not_of(' ') != std::string::npos) {
						try { oas.SetBoatHeight(std::stof(rest), "oas boat height"); } catch (...) {
							if (console) console->Print("Oars and Sails: boat height takes a number, -30 to 30");
							return true;
						}
					}
					if (console) console->Print("Oars and Sails: boat height %+.0f", oas.boatHeight_);
					return true;
				}
				if (lower == "paint" || lower.rfind("paint ", 0) == 0) {
					// oas paint : the current paint and its files; oas "paint blue|beige|vanilla" switches it now (32)
					if (lower.size() > 6) {
						const int p = BoatBow::ParsePaint(lower.substr(6));
						if (p < 0) {
							if (console) console->Print("Oars and Sails: paint is blue, beige or vanilla");
							return true;
						}
						BoatBow::SetPaint(p, "oas paint");
					}
					const int cur = BoatBow::Paint();
					const std::string text = std::format("paint: {} ({}); blue: {}; beige: {}", BoatBow::PaintName(cur), BoatBow::PaintFiles(cur),
						BoatBow::PaintFiles(BoatBow::kPaintBlue), BoatBow::PaintFiles(BoatBow::kPaintBeige));
					logger::info("oas {}", text);
					if (console) console->Print("Oars and Sails %s", text.c_str());
					return true;
				}
				if (lower == "help") {
					// 46.4: every command with one example (unquoted single words work; quote anything with spaces)
					static constexpr const char* kHelp[] = {
						"oas status                      all switches",
						"oas flow                        flow values (turns the pull on if it was off)",
						"oas flowpull180  or  oas \"flow pull 180\"   set a flow value: pull rapids range curve capture turn throw throwmax kick",
						"oas flowoff / oas flowon / oas flowreset / oas flowprobe",
						"oas hullfeel / oas hullfeellog / oas hullfeellen300",
						"oas paintblue / paintbeige / paintvanilla",
						"oas icelist                     statics near you, for the ice ini hints",
						"oas <switch> / oas \"<switch> off\"   rower idles peace contact tilt groundv2 waterpatch flow cascade ice descent ...",
						"oas sweep / oas boat / oas unboat / oas unstick",
						"oas \"num <target>\"   numpad tuning: speed height floor ripple stow rower oars crew dog npc off",
						"oas \"row speed 300\" / \"row drag 0.25\" / \"row turn 0.45\"   oas \"floor 3\"   oas \"boat height 5\"",
						"oas \"ripple wake 0.05\" / \"ripple oar 0.05\"   oas \"lakeboats off|on|<0-10>\"   oas \"reset settings\"",
						"oas \"dock add <name>\" / \"dock del\" / \"dock list\" / \"dock on|off\"   boat spots: your boat waits at a dock you walk near",
					};
					for (const char* line : kHelp) {
						logger::info("Help: {}", line);
						if (console) console->Print("Oars and Sails: %s", line);
					}
					return true;
				}
				if (lower == "flow" || lower.rfind("flow ", 0) == 0) {
					// 45.3: oas flow [on|off|reset|probe|<name> <value>]
					const std::string text = OarsAndSails::Get().FlowCommand(lower.size() > 4 ? lower.substr(5) : std::string());
					if (console) console->Print("Oars and Sails %s", text.c_str());
					return true;
				}
				if (lower == "flowprobe") {
					// 43.5: oas flowprobe : 20 s of the game's own pull on the swimming player, once a second
					auto& oas = OarsAndSails::Get();
					oas.flowProbeTimer_ = 20.0f;
					oas.flowProbeTick_ = 0.0f;
					if (auto* pl = RE::PlayerCharacter::GetSingleton()) oas.flowProbeLast_ = pl->GetPosition();
					logger::info("Flow probe: started (20 s)");
					if (console) console->Print("Oars and Sails: flow probe running for 20 s (swim into the rapids)");
					return true;
				}
				if (lower == "ice list") {
					// 41E: every static within 1500 with its model, node name, layer and top, so ice names can go in the ini
					OarsAndSails::Get().IceList();
					if (console) console->Print("Oars and Sails: ice list written to the log");
					return true;
				}
				if (lower == "hullfeel" || lower.rfind("hullfeel ", 0) == 0) {
					// 38A: oas "hullfeel off|log|on" ; oas "hullfeel len N" | "hullfeel width N" | "hullfeel keel N" ; oas hullfeel = status
					auto& oas = OarsAndSails::Get();
					std::istringstream in(lower.substr(8));
					std::string field;
					float value = 0.0f;
					in >> field;
					std::string rv;
					if (field == "rocks" || field == "rockson" || field == "rocksoff") {
						in >> rv;
						const bool on = field == "rockson" || (field == "rocks" && rv != "off" && (rv == "on" || !oas.feelRocksOn_));
						oas.feelRocksOn_ = field == "rocksoff" ? false : on;
						WritePrivateProfileStringA("HullFeel", "Rocks", oas.feelRocksOn_ ? "1" : "0", OarsAndSails::FlowIniPath().c_str());
						logger::info("Feel: rocks {}", oas.feelRocksOn_ ? "on" : "off");
					} else if (field == "off") oas.hullFeelMode_ = 0;
					else if (field == "log") oas.hullFeelMode_ = 1;
					else if (field == "on") oas.hullFeelMode_ = 2;
					else if ((field == "len" || field == "width" || field == "keel") && (in >> value)) {
						if (field == "len") oas.feelLen_ = std::clamp(value, 50.0f, 600.0f);
						else if (field == "width") oas.feelWidth_ = std::clamp(value, 20.0f, 300.0f);
						else oas.feelKeel_ = std::clamp(value, 0.0f, 120.0f);
					}
					const std::string text = oas.HullFeelStatus();
					logger::info("oas {}", text);
					if (console) console->Print("Oars and Sails %s", text.c_str());
					return true;
				}
				if (lower == "sweep" || lower == "sweep list") {
					// oas sweep : delete our prop refs that no live boat owns (orphans in the save); oas "sweep list" only lists (31.1)
					BoatBow::SweepOrphans(lower == "sweep", "oas sweep");
					return true;
				}
				if (lower == "status") {
					// oas status : every feature switch, on or off (18.3)
					const std::string text = "Features: " + OarsAndSails::Get().FeatureStatus();
					logger::info("{}", text);
					if (console) console->Print("Oars and Sails %s", text.c_str());
					return true;
				}
				{
					// oas rower | flinch | greet | sheathe | camfix | camlog | animobj  : toggle;  oas "<name> on" / "<name> off"  (18.3)
					std::istringstream in(lower);
					std::string name, value;
					in >> name >> value;
					if ((name == "rower" || name == "flinch" || name == "greet" || name == "sheathe" || name == "camfix" || name == "camlog" || name == "animobj" || name == "falllift" || name == "helmsweep" ||
						name == "groundv2" || name == "waterpatch" || name == "flow" || name == "cascade" || name == "ice" || name == "descent") && (value.empty() || value == "on" || value == "off")) {
						auto& oas = OarsAndSails::Get();
						for (auto& f : oas.Features()) {
							if (name != f.name) continue;
							f.set(value.empty() ? !f.get() : value == "on");
							const std::string text = std::format("Oars and Sails: {} {}", f.name, f.get() ? "ON" : "OFF");
							logger::info("{}", text);
							if (console) console->Print("%s", text.c_str());
						}
						return true;
					}
				}
				if (lower.rfind("ripple", 0) == 0) {
					// oas ripples            : toggle all ripples (wake behind the boat and oar-dip rings)
					// oas "ripple wake N"    : size of the stern wake ripples (default 1.0)
					// oas "ripple oar N"     : size of the oar blade ripples (default 0.6)
					// oas "ripple stern N"   : how far behind the vehicle the wake starts (default 120)
					std::istringstream in(arg.substr(6));
					std::string field;
					float value = 0.0f;
					in >> field;
					auto& rt = BoatBow::Ripples();
					if (field == "test") {
						// One single ripple, 400 units ahead on the water, at the size given: shows what one ripple looks like.
						in >> value;
						if (value <= 0.0f) value = rt.oar;
						OarsAndSails::Get().PlaceTestRipple(value);
						if (console) console->Print("Oars and Sails: one test ripple placed ahead (see OarsAndSails.log)");
						return true;
					}
					if (field.empty() || field == "s") {
						BoatBow::SetRipplesOn(!BoatBow::RipplesOn());
						logger::info("Ripples now {}", BoatBow::RipplesOn() ? "ON" : "OFF");
						if (console) console->Print(BoatBow::RipplesOn() ? "Oars and Sails: ripples ON" : "Oars and Sails: ripples OFF");
						return true;
					}
					in >> value;
					if (field == "wake" && value > 0.0f) rt.wake = value;
					else if (field == "oar" && value > 0.0f) rt.oar = value;
					else if (field == "stern") rt.stern = value;
					else if (field == "space" && value > 0.0f) rt.space = value;
					else {
						if (console) console->Print("Oars and Sails: ripples | ripple wake N | ripple oar N | ripple stern N | ripple space N");
						return true;
					}
					logger::info("Ripple tuning: wake={:.2f} oar={:.2f} stern={:.0f} space={:.0f}", rt.wake, rt.oar, rt.stern, rt.space);
					OarsAndSails::SaveRippleIni();  // 56.45: kept across launches
					if (console) console->Print("Oars and Sails: ripple tuning updated (see OarsAndSails.log)");
					return true;
				}
				if (lower.rfind("row", 0) == 0 && lower.rfind("rowing", 0) != 0) {
					// oas row            : toggle between the rowboat and the motor (speedboat) speed model
					// oas "row speed N"  : rowboat top speed in u/s (default 360)
					// oas "row drag N"   : glide drag per second (default 0.25; lower = longer glide)
					// oas "row brake N", "row turn N" (rad/s at top speed; low-speed turn follows)
					auto& ac = OarsAndSails::Get();
					std::istringstream in(arg.substr(3));
					std::string field;
					float value = 0.0f;
					in >> field;
					if (field.empty()) {
						ac.rowMode_ = !ac.rowMode_;
						logger::info("Speed model now {}", ac.rowMode_ ? "ROW" : "MOTOR");
						if (console) console->Print(ac.rowMode_ ? "Oars and Sails: ROWBOAT speed model" : "Oars and Sails: MOTOR speed model (speedboat)");
						return true;
					}
					in >> value;
					if (field == "speed" && value > 20.0f) {
						ac.SetRowSpeed(value, ac.rowProfile_.coastDrag, "oas row speed");  // 56.44: kept in the ini
					} else if (field == "drag" && value > 0.02f) {
						ac.SetRowSpeed(ac.rowProfile_.maxSpeed, value, "oas row drag");
					} else if (field == "brake" && value > 1.0f) {
						ac.rowProfile_.brake = value;
					} else if (field == "turn" && value > 0.05f) {
						ac.SetRowTurn(value / 0.45f, "oas row turn");  // 56.45: kept in the ini as a scale
					} else {
						if (console) console->Print("Oars and Sails: row | row speed N | row drag N | row brake N | row turn N");
						return true;
					}
					logger::info("Row profile: max={:.0f} accel={:.1f} drag={:.2f} brake={:.0f} turn={:.2f}/{:.2f}", ac.rowProfile_.maxSpeed, ac.rowProfile_.accel, ac.rowProfile_.coastDrag, ac.rowProfile_.brake, ac.rowProfile_.turnLow, ac.rowProfile_.turnHigh);
					if (console) console->Print("Oars and Sails: row profile updated (see OarsAndSails.log)");
					return true;
				}
				if (lower == "oars") {
					const bool on = !BoatBow::OarDemo();
					BoatBow::SetOarDemo(on);
					logger::info("Oar test stroke now {}", on ? "ON" : "OFF");
					if (console) console->Print(on ? "Oars and Sails: oars rowing (test stroke)" : "Oars and Sails: oars still");
					return true;
				}
				if (lower.rfind("oar", 0) == 0) {
					// oas "oar <x|y|z|yaw|pitch|roll|scale|slide> <value>" sets the RIGHT oar and mirrors it onto the left;
					// "oar flip" turns the model end for end; "oar mirror" re-mirrors; "oar" alone logs the values.
					std::istringstream in(arg.substr(3));
					std::string field;
					float value = 0.0f;
					in >> field;
					auto& r = BoatBow::Oar(0);
					bool known = true;
					if (field.empty()) {
						BoatBow::LogOars();
						if (console) console->Print("Oars and Sails: oar values are in OarsAndSails.log");
						return true;
					}
					if (field == "mirror") { BoatBow::MirrorOars(); }
					else if (field == "link") {
						auto& ac = OarsAndSails::Get();
						ac.oarLink_ = !ac.oarLink_;
						logger::info("Oars linked (mirrored numpad) now {}", ac.oarLink_ ? "ON" : "OFF");
						if (console) console->Print(ac.oarLink_ ? "Oars and Sails: numpad moves both oars, mirrored" : "Oars and Sails: numpad moves one oar at a time (Num5 picks)");
						return true;
					}
					else if (field == "flip") { BoatBow::ToggleOarMeshFlip(); }
					else if (in >> value) {
						if (field == "x") r.offset.x = value;
						else if (field == "y") r.offset.y = value;
						else if (field == "z") r.offset.z = value;
						else if (field == "yaw") r.yaw = value * 0.0174533f;
						else if (field == "pitch") r.pitch = value * 0.0174533f;
						else if (field == "roll") r.roll = value * 0.0174533f;
						else if (field == "scale") r.scale = (std::max)(0.05f, value);
						else if (field == "slide") r.slide = value;
						else known = false;
						if (known) BoatBow::MirrorOars();
					} else {
						known = false;
					}
					if (console) console->Print(known ? "Oars and Sails: oars updated (see OarsAndSails.log)" : "Oars and Sails: usage: oas \"oar yaw 90\"  (x y z yaw pitch roll scale slide, or: oar flip / oar mirror / oar)");
					if (known) BoatBow::LogOars();
					return true;
				}
				if (lower == "face") {
					auto& ac = OarsAndSails::Get();
					ac.ToggleFaceLock();
					logger::info("Seated body facing lock now {}", ac.lockSeatedFacing_ ? "ON" : "OFF");
					if (console) console->Print(ac.lockSeatedFacing_ ? "Oars and Sails: seated body faces forward (head tracking off)" : "Oars and Sails: seated body may turn and track the camera");
					return true;
				}
				if (lower.rfind("physics", 0) == 0) {
					// oas physics           : show the command
					// oas "physics smp reset" : run this console command 1.5 s after boarding and after getting out
					// oas "physics off"      : do nothing   |   oas "physics now" : run it right now
					auto& ac = OarsAndSails::Get();
					std::string text = arg.size() > 7 ? arg.substr(7) : std::string{};
					while (!text.empty() && text.front() == ' ') text.erase(text.begin());
					if (text == "off") ac.physicsCmd_.clear();
					else if (text == "now") ac.RunPhysicsCommand();
					else if (!text.empty()) ac.physicsCmd_ = text;
					logger::info("Physics reset command is '{}'", ac.physicsCmd_);
					if (console) console->Print(ac.physicsCmd_.empty() ? "Oars and Sails: physics reset is OFF" : ("Oars and Sails: runs '" + ac.physicsCmd_ + "' after boarding and after getting out").c_str());
					return true;
				}
				if (lower.rfind("eye", 0) == 0) {
					// oas "eye 20" : first person eye height boost in game units (0 = none)
					std::istringstream in(arg.substr(3));
					float amount = 0.0f;
					auto& ac = OarsAndSails::Get();
					if (in >> amount) ac.fpEyeBoost_ = std::clamp(amount, -40.0f, 80.0f);
					logger::info("First person eye boost is {:.0f}", ac.fpEyeBoost_);
					if (console) console->Print(std::format("Oars and Sails: first person eye boost {:.0f} (oas \"eye 20\" to change)", ac.fpEyeBoost_).c_str());
					return true;
				}
				if (lower.rfind("bob", 0) == 0) {
					// oas bob : idle rocking on/off.  oas "bob 2" : twice as strong (0 = off)
					std::istringstream in(arg.substr(3));
					float amount = 0.0f;
					if (in >> amount) BoatBow::SetIdleBob(amount);
					else BoatBow::SetIdleBob(BoatBow::IdleBob() > 0.0f ? 0.0f : 1.0f);
					logger::info("Idle boat bob now x{:.2f}", BoatBow::IdleBob());
					if (console) console->Print(BoatBow::IdleBob() > 0.0f ? "Oars and Sails: idle boat rocking ON (oas \"bob 2\" = stronger)" : "Oars and Sails: idle boat rocking OFF");
					return true;
				}
				if (lower == "hud") {
					auto& ac = OarsAndSails::Get();
					ac.ToggleHudInBoat();
					logger::info("Game HUD in the boat now {}", ac.hideHudInBoat_ ? "HIDDEN" : "SHOWN");
					if (console) console->Print(ac.hideHudInBoat_ ? "Oars and Sails: game HUD hidden while riding" : "Oars and Sails: game HUD shown while riding");
					return true;
				}
				if (lower.rfind("chest", 0) == 0) {
					// oas chest            : log the chest's placement
					// oas "chest <text>"   : pick another container by text in its model path (empty ones preferred), or a form ID in hex
					std::string text = arg.size() > 5 ? arg.substr(5) : std::string{};
					while (!text.empty() && text.front() == ' ') text.erase(text.begin());
					if (text.empty()) {
						BoatBow::LogChest();
						if (console) console->Print("Oars and Sails: usage: oas \"chest <text in the model path or a form ID>\" (matches are in OarsAndSails.log)");
					} else {
						const bool ok = BoatBow::SetChestModel(text.c_str());
						if (console) console->Print(ok ? "Oars and Sails: chest changed (see OarsAndSails.log)" : "Oars and Sails: nothing to look for");
					}
					return true;
				}
				if (lower.rfind("plank", 0) == 0) {
					const std::string text = arg.size() > 5 ? arg.substr(5) : std::string{};
					std::string trimmed = text;
					while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());
					if (trimmed.empty()) {
						BoatBow::LogPlank();
						if (console) console->Print("Oars and Sails: usage: oas \"plank <text in the model path>\", e.g. oas \"plank crate\" (matches are in OarsAndSails.log)");
					} else {
						const bool ok = BoatBow::SetPlankModel(trimmed.c_str());
						if (console) console->Print(ok ? "Oars and Sails: plank model changed (see OarsAndSails.log)" : "Oars and Sails: no static model path contains that text");
					}
					return true;
				}
				if (lower == "psit" || lower == "sit") {
					auto& ac = OarsAndSails::Get();
					ac.playerSit_ = !ac.playerSit_;
					logger::info("Player seated pose now {}", ac.playerSit_ ? "ON" : "OFF (crouch)");
					if (console) console->Print(ac.playerSit_ ? "Oars and Sails: player sits (applies from the next boarding)" : "Oars and Sails: player crouches (applies from the next boarding)");
					return true;
				}
				if (lower == "ptilt") {
					auto& ac = OarsAndSails::Get();
					ac.playerTilt_ = !ac.playerTilt_;
					logger::info("Player tilt now {}", ac.playerTilt_ ? "ON" : "OFF");
					if (console) console->Print(ac.playerTilt_ ? "Oars and Sails: player body tilt ON" : "Oars and Sails: player body tilt OFF");
					return true;
				}
				if (lower == "pick") {
					// oas pick : what the crosshair is on (name, our tag Boat|HelmBench|RowBench|Chest|none, distance, layer)
					const std::string text = OarsAndSails::Get().PickReport();
					logger::info("{}", text);
					if (console) console->Print("%s", text.c_str());
					return true;
				}
				if (lower.rfind("idles", 0) == 0) {
					// oas idles : toggle the seated crew idles and list who may take one.  oas "idles now" : one idle on the next fair pick
					auto& oas = OarsAndSails::Get();
					std::string text;
					if (lower.find("now") != std::string::npos) {
						const std::string who = oas.IdlesNow();
						text = who.empty() ? std::string("Oars and Sails: nobody can take a seated idle now (see oas idles)") : std::format("Oars and Sails: seated idle sent to {}", who);
					} else {
						oas.idlesOn_ = !oas.idlesOn_;
						text = std::format("Oars and Sails: seated crew idles (eat, drink, fidget) {}; {}", oas.idlesOn_ ? "ON" : "OFF", oas.IdleReport());
					}
					logger::info("{}", text);
					if (console) console->Print("%s", text.c_str());
					return true;
				}
				if (lower.rfind("peace", 0) == 0) {
					// oas peace : show.  oas "peace on" / "peace off"
					auto& oas = OarsAndSails::Get();
					std::string rest = arg.size() > 5 ? arg.substr(5) : std::string{};
					while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
					if (rest == "on") oas.peaceOn_ = true;
					else if (rest == "off") { oas.peaceOn_ = false; oas.RestorePeace("peace switched off"); }
					const std::string text = std::format("Oars and Sails: peace with slaughterfish and mudcrabs while boating is {} ({} calm now)", oas.peaceOn_ ? "ON" : "OFF", oas.calmed_.size());
					logger::info("{}", text);
					if (console) console->Print("%s", text.c_str());
					return true;
				}
				if (lower.rfind("crew", 0) == 0) {
					// oas crew add [refid] | remove [refid] | clear | list | summon
					auto& oas = OarsAndSails::Get();
					std::istringstream in(arg.substr(4));
					std::string field, idText;
					in >> field >> idText;
					auto say = [&](const std::string& a_text) {
						logger::info("{}", a_text);
						if (console) console->Print("%s", a_text.c_str());
					};
					auto target = [&]() -> RE::Actor* {
						if (!idText.empty()) {
							std::string hex = idText;
							if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) hex = hex.substr(2);
							const bool isHex = !hex.empty() && hex.size() <= 8 &&
								std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
							return isHex ? RE::TESForm::LookupByID<RE::Actor>(static_cast<RE::FormID>(std::stoul(hex, nullptr, 16))) : nullptr;
						}
						auto selected = RE::Console::GetSelectedRef();
						return selected ? selected->As<RE::Actor>() : nullptr;
					};
					if (field == "add") {
						auto* actor = target();
						if (!actor) { say("Crew: click an NPC in the console first (or: oas \"crew add <refid>\")"); return true; }
						if (actor == RE::PlayerCharacter::GetSingleton()) { say("Crew: the player is already aboard"); return true; }
						if (actor->IsDead()) { say(std::format("Crew: {} is dead", actor->GetName())); return true; }
						const auto* race = actor->GetRace();
						const bool dog = OarsAndSails::IsDogActor(actor);
						if (!dog && !(race && race->HasKeywordString("ActorTypeNPC"))) { say(std::format("Crew: {} is not a person or a dog, so it cannot ride", actor->GetName())); return true; }
						if (actor->IsPlayerTeammate()) { say(std::format("Crew: {} is already a follower and boards by themselves, nothing to do", actor->GetName())); return true; }
						if (oas.IsCrew(actor)) { say(std::format("Crew: {} is already in the crew", actor->GetName())); return true; }
						if (!dog && oas.CrewHumans() >= 6) { say("Crew: all 6 seats are taken (oas \"crew remove\" someone first)"); return true; }
						oas.crew_.push_back(actor->GetFormID());
						bool vendor = false;
						actor->VisitFactions([&](RE::TESFaction* a_faction, std::int8_t) { if (a_faction && a_faction->IsVendor()) vendor = true; return false; });
						const auto* base = actor->GetActorBase();
						const bool important = (base && (base->IsEssential() || base->IsProtected())) || vendor || actor->extraList.HasType(RE::ExtraDataType::kAliasInstanceArray);
						logger::info("Crew: {} {:08X} added", actor->GetName(), actor->GetFormID());
						say(std::format("Crew: {} added (humans in the crew {}/6){}", actor->GetName(), oas.CrewHumans(),
							important ? " - an essential, merchant or quest NPC: only borrowed for the trip" : ""));
						return true;
					}
					if (field == "remove") {
						auto* actor = target();
						if (!actor || !oas.IsCrew(actor)) { say("Crew: that actor is not in the crew (click them, or oas \"crew remove <refid>\")"); return true; }
						oas.crew_.erase(std::remove(oas.crew_.begin(), oas.crew_.end(), actor->GetFormID()), oas.crew_.end());
						logger::info("Crew: {} {:08X} removed", actor->GetName(), actor->GetFormID());
						say(std::format("Crew: {} removed{}", actor->GetName(), oas.IsAboard(actor) ? " (stays aboard until you get out)" : ""));
						return true;
					}
					if (field == "clear") {
						for (auto id : oas.crew_) {
							auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
							logger::info("Crew: {} {:08X} removed", a ? a->GetName() : "?", id);
						}
						oas.crew_.clear();
						oas.summonQueue_.clear();
						say("Crew: cleared");
						return true;
					}
					if (field == "list") {
						if (oas.crew_.empty()) { say("Crew: nobody added yet (click an NPC, then oas \"crew add\")"); return true; }
						for (auto id : oas.crew_) {
							auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
							if (!a) { say(std::format("Crew: {:08X} (not loaded)", id)); continue; }
							const auto* race = a->GetRace();
							const int seat = oas.CrewSeat(a);
							say(std::format("Crew: {} {:08X} race '{}' {}", a->GetName(), id, race && race->GetFormEditorID() ? race->GetFormEditorID() : "?",
								oas.IsAboard(a) ? (seat >= 0 ? std::format("aboard, seat {}", seat) : std::string("aboard, bow")) : std::string("not aboard")));
						}
						return true;
					}
					if (field == "summon") {
						auto* player = RE::PlayerCharacter::GetSingleton();
						if (!player) return true;
						oas.summonQueue_.clear();
						int queued = 0;
						std::string skipped;
						for (auto id : oas.crew_) {
							auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
							auto* cell = a ? a->GetParentCell() : nullptr;
							if (!a || a->IsDead() || oas.IsAboard(a)) continue;
							if (!cell || !cell->IsAttached() || !a->Is3DLoaded() || a->GetWorldspace() != player->GetWorldspace()) { skipped += std::format(" {} (not nearby)", a->GetName()); continue; }
							if (a->IsInCombat()) { skipped += std::format(" {} (in combat)", a->GetName()); continue; }
							if (a->GetCurrentScene()) { skipped += std::format(" {} (in a scene)", a->GetName()); continue; }
							oas.summonQueue_.push_back(id);
							++queued;
						}
						oas.summonCentre_ = oas.SummonCentre(player);
						oas.summonIndex_ = 0;
						oas.summonTotal_ = queued;
						oas.summonTimer_ = 0.0f;
						say(std::format("Crew: summoning {} to the boat{}{}", queued, skipped.empty() ? "" : ", skipped:", skipped));
						return true;
					}
					say("Crew: oas \"crew add [refid]\" | \"crew remove [refid]\" | crew clear | crew list | crew summon");
					return true;
				}
				if (lower.rfind("lamp", 0) == 0) {
					// oas lamp                      : log both lamps
					// oas "lamp list [text]"        : log light / lantern forms and the best candidates
					// oas "lamp stern <id|text|off>", oas "lamp bow <id|text|off>"
					// oas "lamp auto on|off"        : lit only from 18:00 to 06:00
					std::istringstream in(arg.substr(4));
					std::string field, rest;
					in >> field;
					std::getline(in, rest);
					while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
					if (field.empty()) {
						BoatBow::LogLamp(0);
						BoatBow::LogLamp(1);
						if (console) console->Print("Oars and Sails: lamp values are in OarsAndSails.log");
					} else if (field == "list") {
						BoatBow::ListLamps(rest.c_str());
						if (console) console->Print("Oars and Sails: light forms written to OarsAndSails.log");
					} else if ((field == "stern" || field == "bow") && !rest.empty()) {
						const bool ok = BoatBow::SetLampForm(field == "stern" ? 0 : 1, rest.c_str());
						if (console) console->Print(ok ? "Oars and Sails: lamp changed (see OarsAndSails.log)" : "Oars and Sails: nothing matches - try oas \"lamp list\"");
					} else if (field == "light" && !rest.empty()) {
						const bool ok = BoatBow::SetLampLight(rest.c_str());
						if (console) console->Print(ok ? "Oars and Sails: lamp light changed (see OarsAndSails.log)" : "Oars and Sails: not a light form ID");
					} else if (field == "auto") {
						BoatBow::SetLampAuto(rest == "on");
						if (console) console->Print(BoatBow::LampAuto() ? "Oars and Sails: lamps lit only at night (18:00-06:00)" : "Oars and Sails: lamps always lit");
					} else if (console) {
						console->Print("Oars and Sails: lamp | lamp list [text] | lamp stern <id|text|off> | lamp bow <id|text|off> | lamp light <id> | lamp auto on|off");
					}
					return true;
				}
				if (lower.rfind("contact", 0) == 0) {
					// oas contact : fake collision on/off.  oas "contact log" : log contact events.  oas "contact draft N" : hull depth
					auto& oas = OarsAndSails::Get();
					std::istringstream in(arg.substr(7));
					std::string field;
					in >> field;
					float value = 0.0f;
					if (field.empty()) {
						oas.contactOn_ = !oas.contactOn_;
						logger::info("Fake collision now {}", oas.contactOn_ ? "ON" : "OFF");
						if (console) console->Print(oas.contactOn_ ? "Oars and Sails: fake collision ON" : "Oars and Sails: fake collision OFF");
					} else if (field == "log") {
						oas.contactLog_ = !oas.contactLog_;
						if (console) console->Print(oas.contactLog_ ? "Oars and Sails: contact events go to OarsAndSails.log" : "Oars and Sails: contact log OFF");
					} else if (field == "draft" && (in >> value)) {
						oas.hullDraft_ = std::clamp(value, 0.0f, 120.0f);
						logger::info("Hull draft now {:.0f}", oas.hullDraft_);
						if (console) console->Print("Oars and Sails: hull draft %.0f", oas.hullDraft_);
					} else if (console) {
						console->Print("Oars and Sails: contact | contact log | contact draft N");
					}
					return true;
				}
				if (lower == "tilt") {
					auto& ac = OarsAndSails::Get();
					ac.nodeOverride_ = !ac.nodeOverride_;
					logger::info("Skeleton overrides now {}", ac.nodeOverride_ ? "ON" : "OFF");
					if (console) console->Print(ac.nodeOverride_ ? "Oars and Sails: body/passenger skeleton overrides ON" : "Oars and Sails: body/passenger skeleton overrides OFF");
					return true;
				}
				if (lower.rfind("slot", 0) == 0) {
					float x = 0.0f, y = 0.0f, z = 0.0f;
					if (std::sscanf(arg.c_str() + 4, " %f %f %f", &x, &y, &z) == 3) {
						OarsAndSails::Get().SetBowSlot(x, y, z);
						if (console) console->Print("Oars and Sails: bow slot set (applies to the next frame while boarded)");
					} else if (console) {
						console->Print("Oars and Sails: usage: oas \"slot <right> <forward> <up>\"  e.g. oas \"slot 0 430 15\"");
					}
					return true;
				}
				if (lower == "unstick") {
					// Stuck and cannot walk after getting out? This clears every seated / animation flag we know of and logs before and after.
					OarsAndSails::Get().Unstick();
					if (console) console->Print("Oars and Sails: cleared the walk blockers, try WASD (details in OarsAndSails.log)");
					return true;
				}
				if (lower == "skim") {
					const bool on = OarsAndSails::Get().ToggleSkimPlayer();
					if (console) console->Print(on ? "Oars and Sails: skim ON (player) - W/S throttle, A/D steer, mouse looks anywhere" : "Oars and Sails: skim OFF / start rejected - stand in loaded open water first");
					return true;
				}
				// 51: lake boats (oas lakehere <name>, lakepoint <name>, lakes, lakedel <name>, lakeboats on|off|now|1|2|3)
				if (auto lake = LakeBoats::Command(lower); !lake.empty()) {
					if (console) console->Print("Oars and Sails: %s", lake.c_str());
					return true;
				}
				if (lower == "dock" || lower == "docks" || lower.rfind("dock ", 0) == 0 || lower.rfind("docks ", 0) == 0) {
					const auto sp = lower.find(' ');
					const std::string text = OarsAndSails::Get().DockCommand(sp == std::string::npos ? std::string() : lower.substr(sp + 1));
					if (console) console->Print(std::format("Oars and Sails: {}", text).c_str());
					return true;
				}
				if (lower == "boat") {
					const bool ok = OarsAndSails::Get().SpawnParkedBoat();
					if (console) console->Print(ok ? "Oars and Sails: boat placed in front of you" : "Oars and Sails: no loaded water in front of you - stand in or beside shallow water");
					return true;
				}
				if (lower == "unboat") {
					BoatBow::RemoveParked();
					if (console) console->Print("Oars and Sails: boat removed");
					return true;
				}

				// 46.1: nothing else matched: a bare flow key ("pull180", "pull 180", "on", "reset") goes to flow.
				{
					std::istringstream fin(SplitGlued(lower.find(' ') == std::string::npos ? lower : lower));
					std::string name;
					fin >> name;
					if (IsFlowKey(name)) {
						const std::string text = OarsAndSails::Get().FlowCommand(SplitGlued(lower));
						if (console) console->Print("Oars and Sails %s", text.c_str());
						return true;
					}
				}
				logger::info("ExecuteOas: unrecognized '{}' (read as '{}'; tried: commands, flow keys)", arg, lower);
				if (console) {
					console->Print("Oars and Sails: unrecognized '%s'. Try: oas \"flow pull 180\"  or  oas flowpull180  or  oas flow  (shows all), oas help", arg.c_str());
				}
				return true;
			}
		}

		if (auto* console = RE::ConsoleLog::GetSingleton()) {
			console->Print("Oars and Sails: oas boat | unboat | skim | crew | peace | contact | lamp | row | oar | ripples | bob | hud | chest | plank | slot | face | eye | physics | psit | tilt | unstick");
		}
		return true;
	}

	void RegisterConsoleCommand()
	{
		auto* cmd = RE::SCRIPT_FUNCTION::LocateConsoleCommand(kHijackedCommand);
		if (!cmd) {
			logger::error("Console command '{}' not found; oas not registered", kHijackedCommand);
			return;
		}

		// CONFIRMED against RE/C/CommandTable.h: SCRIPT_FUNCTION's field names
		// (functionName, shortName, helpString, referenceFunction, executeFunction,
		// editorFilter) and the Execute_t signature match the skeleton exactly.
		cmd->functionName = "OarsAndSails";
		cmd->shortName = "oas";
		cmd->helpString = "oas [boat|unboat|skim|row|oar|oars|ripples|bob|hud|chest|plank|slot|face|eye|physics|sit|tilt|ptilt|unstick|debug <cmd>]: boat/unboat place or remove the rowboat (walk up, E to board, E to get off); skim rides from where you stand";
		cmd->referenceFunction = false;
		cmd->SetParameters(kOasParams);
		cmd->executeFunction = &ExecuteOas;
		cmd->editorFilter = false;

		logger::info("Registered 'OarsAndSails' / 'oas' (replaced '{}')", kHijackedCommand);
	}

	// The crew list lives in the SKSE co-save, so it follows the save. Form IDs are re-resolved on load (load order
	// changes) and members that no longer exist are dropped. A new game starts with no crew.
	constexpr std::uint32_t kCoSaveId = 'OASC';
	constexpr std::uint32_t kCrewRecord = 'CREW';
	constexpr std::uint32_t kPeaceRecord = 'PEAC';
	constexpr std::uint32_t kRowerRecord = 'ROWT';
	constexpr std::uint32_t kParkRecord = 'PARK';
	constexpr std::uint32_t kFeatureRecord = 'FEAT';  // 18.3: the feature switches, by name
	constexpr std::uint32_t kPropRecord = 'PROP';     // 31.1: every prop ref we spawned (id, type), purged after a load
	constexpr std::uint32_t kPaintRecord = 'PANT';    // 32: the paint job (vanilla, beige, blue)
	constexpr std::uint32_t kLakeRecord = 'LAKB';     // 52: the lakes and their boat populations (text)
	constexpr std::uint32_t kStashRecord = 'STSH';    // 56.8: the crew's weapons in our container (container id, per actor: items)
	constexpr std::uint32_t kAboardRecord = 'ABRD';   // 56.11.4: the save was made while the player was aboard
	constexpr std::uint32_t kBoatHeightRecord = 'BHGT';  // 56.26: the player's boat height in the water (float)

	void OnCoSave(SKSE::SerializationInterface* a_intfc)
	{
		if (OarsAndSails::Get().AboardForSave() && a_intfc->OpenRecord(kAboardRecord, 1)) a_intfc->WriteRecordData(static_cast<std::uint8_t>(1));
		{
			const auto& oas = OarsAndSails::Get();
			if (!oas.stash_.empty() && a_intfc->OpenRecord(kStashRecord, 1)) {
				a_intfc->WriteRecordData(oas.stashRefId_);
				a_intfc->WriteRecordData(static_cast<std::uint32_t>(oas.stash_.size()));
				for (const auto& e : oas.stash_) {
					a_intfc->WriteRecordData(e.actor);
					a_intfc->WriteRecordData(static_cast<std::uint32_t>(e.items.size()));
					for (const auto& si : e.items) { a_intfc->WriteRecordData(si.item); a_intfc->WriteRecordData(si.count); }
				}
			}
		}
		if (a_intfc->OpenRecord(kPaintRecord, 1)) a_intfc->WriteRecordData(static_cast<std::int32_t>(BoatBow::Paint()));
		if (a_intfc->OpenRecord(kBoatHeightRecord, 1)) a_intfc->WriteRecordData(OarsAndSails::Get().boatHeight_);
		{
			const std::string lakes = LakeBoats::SaveState();
			if (a_intfc->OpenRecord(kLakeRecord, 1)) {
				a_intfc->WriteRecordData(static_cast<std::uint32_t>(lakes.size()));
				if (!lakes.empty()) a_intfc->WriteRecordData(lakes.data(), static_cast<std::uint32_t>(lakes.size()));
			}
		}
		{
			const auto refs = BoatBow::TrackedRefsForSave();
			if (a_intfc->OpenRecord(kPropRecord, 1)) {
				a_intfc->WriteRecordData(static_cast<std::uint32_t>(refs.size()));
				for (const auto& [id, type] : refs) {
					a_intfc->WriteRecordData(id);
					a_intfc->WriteRecordData(static_cast<std::uint32_t>(type.size()));
					a_intfc->WriteRecordData(type.data(), static_cast<std::uint32_t>(type.size()));
				}
			}
		}
		const auto& crew = OarsAndSails::Get().crew_;
		if (!a_intfc->OpenRecord(kCrewRecord, 1)) return;
		const auto count = static_cast<std::uint32_t>(crew.size());
		a_intfc->WriteRecordData(count);
		for (auto id : crew) a_intfc->WriteRecordData(id);
		// 56.26: the creatures calmed at save time (saves no longer restore them first); the load puts their Aggression back.
		const auto& calmed = OarsAndSails::Get().calmed_;
		if (!a_intfc->OpenRecord(kPeaceRecord, 1)) return;
		const auto n = static_cast<std::uint32_t>(calmed.size());
		a_intfc->WriteRecordData(n);
		for (const auto& c : calmed) { a_intfc->WriteRecordData(c.id); a_intfc->WriteRecordData(c.aggression); }
		// v1.0.14: who rows next (14.6). The list hash is over load-order form IDs, so a changed load order starts again at 0.
		if (!a_intfc->OpenRecord(kRowerRecord, 1)) return;
		a_intfc->WriteRecordData(OarsAndSails::Get().rowerTurn_);
		a_intfc->WriteRecordData(OarsAndSails::Get().rowerListHash_);
		// 18.3 (v1.0.20): the feature switches (name, on/off), so what Miguel switches off stays off after a load.
		{
			auto features = OarsAndSails::Get().Features();
			if (!a_intfc->OpenRecord(kFeatureRecord, 4)) return;  // 4 (v1.0.27): camfix already migrated to off
			a_intfc->WriteRecordData(static_cast<std::uint32_t>(features.size()));
			for (auto& f : features) {
				const std::string name(f.name);
				a_intfc->WriteRecordData(static_cast<std::uint32_t>(name.size()));
				a_intfc->WriteRecordData(name.data(), static_cast<std::uint32_t>(name.size()));
				a_intfc->WriteRecordData(static_cast<std::uint8_t>(f.get() ? 1 : 0));
			}
		}
		// B9 (v1.0.15): the parked boat, so the same boat is found (or put back at its spot) after a load.
		float x = 0.0f, y = 0.0f, z = 0.0f, yaw = 0.0f;
		if (BoatBow::GetParked(x, y, z, yaw) && (BoatBow::ParkedRefId() >> 24) == 0xFF) {
			auto* player = RE::PlayerCharacter::GetSingleton();
			const RE::FormID world = player && player->GetWorldspace() ? player->GetWorldspace()->GetFormID() : 0u;
			if (!a_intfc->OpenRecord(kParkRecord, 1)) return;
			a_intfc->WriteRecordData(BoatBow::ParkedRefId());
			for (float v : { x, y, z, yaw }) a_intfc->WriteRecordData(v);
			a_intfc->WriteRecordData(world);
		}
	}

	// SKSE's kSaveGame. 56.26: no RestorePeace here any more: giving every calmed creature its Aggression back reset
	// their AI mid-animation and the next scan calmed them again 0.5 s later (restarted animations). The co-save keeps
	// calmed_ ('PEAC') and the load path puts the creatures back. Our per-frame work stays still for 0.5 s from here.
	void OnBeforeSave(const char* a_name)
	{
		OarsAndSails::Get().LogHealth();  // 42.4
		OarsAndSails::Get().OnSaveGameMessage(a_name);
	}

	void OnCoLoad(SKSE::SerializationInterface* a_intfc)
	{
		auto& crew = OarsAndSails::Get().crew_;
		crew.clear();
		OarsAndSails::Get().stash_.clear();
		OarsAndSails::Get().stashRefId_ = 0;
		std::uint32_t type = 0, version = 0, length = 0;
		bool paintLoaded = false;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type == kAboardRecord) {
				std::uint8_t on = 0;
				a_intfc->ReadRecordData(on);
				OarsAndSails::Get().loadRelease_ = on != 0;
				OarsAndSails::Get().loadReleaseWait_ = 0.0f;
				logger::info("Load: the save was made aboard; the player is released once loaded");
				continue;
			}
			if (type == kBoatHeightRecord) {
				float h = 0.0f;
				a_intfc->ReadRecordData(h);
				auto& oas = OarsAndSails::Get();
				oas.boatHeight_ = std::isfinite(h) ? std::clamp(std::round(h), -30.0f, 30.0f) : OarsAndSails::kDefaultBoatHeight;  // the parked boat in this save sits at it already
				logger::info("Boat: height {:.0f} (co-save)", oas.boatHeight_);
				continue;
			}
			if (type == kStashRecord) {
				// 56.8: weapons left in our container by a save (or a crash) while aboard: given back once each actor is loaded.
				auto& oas = OarsAndSails::Get();
				RE::FormID box = 0, boxNew = 0;
				std::uint32_t n = 0;
				a_intfc->ReadRecordData(box);
				a_intfc->ReadRecordData(n);
				if (a_intfc->ResolveFormID(box, boxNew)) oas.stashRefId_ = boxNew;
				for (std::uint32_t i = 0; i < n && i < 64; ++i) {
					RE::FormID actor = 0, actorNew = 0;
					std::uint32_t m = 0;
					a_intfc->ReadRecordData(actor);
					a_intfc->ReadRecordData(m);
					decltype(oas.stash_)::value_type e{};
					for (std::uint32_t k = 0; k < m && k < 256; ++k) {
						RE::FormID item = 0, itemNew = 0;
						std::int32_t count = 0;
						a_intfc->ReadRecordData(item);
						a_intfc->ReadRecordData(count);
						if (a_intfc->ResolveFormID(item, itemNew)) e.items.push_back({ itemNew, count });
					}
					if (a_intfc->ResolveFormID(actor, actorNew) && !e.items.empty()) { e.actor = actorNew; oas.stash_.push_back(std::move(e)); }
				}
				logger::info("Stash: {} actor(s) with weapons in container {:08X} from the save, given back once loaded", oas.stash_.size(), oas.stashRefId_);
				continue;
			}
			if (type == kPaintRecord) {
				std::int32_t p = BoatBow::kPaintBlue;
				a_intfc->ReadRecordData(p);
				BoatBow::SetPaint(p >= 0 && p < BoatBow::kPaintCount ? p : BoatBow::kPaintBlue, "from the save");
				paintLoaded = true;
				continue;
			}
			if (type == kLakeRecord) {
				std::uint32_t n = 0;
				a_intfc->ReadRecordData(n);
				std::string text(std::min<std::uint32_t>(n, 4u * 1024u * 1024u), ' ');
				if (!text.empty()) a_intfc->ReadRecordData(text.data(), static_cast<std::uint32_t>(text.size()));
				LakeBoats::LoadState(text, [a_intfc](RE::FormID a_old) { RE::FormID out = 0; return a_intfc->ResolveFormID(a_old, out) ? out : 0u; });
				continue;
			}
			if (type == kPropRecord) {
				std::uint32_t n = 0;
				a_intfc->ReadRecordData(n);
				std::vector<std::pair<RE::FormID, std::string>> refs;
				for (std::uint32_t i = 0; i < n && i < 4096; ++i) {
					RE::FormID oldId = 0, newId = 0;
					std::uint32_t len = 0;
					a_intfc->ReadRecordData(oldId);
					a_intfc->ReadRecordData(len);
					std::string what(std::min<std::uint32_t>(len, 64), '\0');
					a_intfc->ReadRecordData(what.data(), static_cast<std::uint32_t>(what.size()));
					if (a_intfc->ResolveFormID(oldId, newId)) refs.push_back({ newId, what });
				}
				BoatBow::SetStaleRefs(refs);
				continue;
			}
			if (type == kParkRecord) {
				auto& sp = OarsAndSails::Get().savedPark_;
				a_intfc->ReadRecordData(sp.id);
				a_intfc->ReadRecordData(sp.x);
				a_intfc->ReadRecordData(sp.y);
				a_intfc->ReadRecordData(sp.z);
				a_intfc->ReadRecordData(sp.yaw);
				a_intfc->ReadRecordData(sp.world);
				sp.valid = true;
				sp.tries = 0;
				logger::info("BoatBow: parked boat {:08X} at ({:.0f},{:.0f},{:.0f}) loaded from the co-save", sp.id, sp.x, sp.y, sp.z);
				continue;
			}
			if (type == kFeatureRecord) {
				auto features = OarsAndSails::Get().Features();
				std::uint32_t n = 0;
				a_intfc->ReadRecordData(n);
				for (std::uint32_t i = 0; i < n && i < 64; ++i) {
					std::uint32_t len = 0;
					a_intfc->ReadRecordData(len);
					std::string name(std::min<std::uint32_t>(len, 64), '\0');
					a_intfc->ReadRecordData(name.data(), static_cast<std::uint32_t>(name.size()));
					std::uint8_t on = 1;
					a_intfc->ReadRecordData(on);
					for (auto& f : features) {
						if (name == f.name) f.set(on != 0);
					}
				}
				// 28: saves from before v1.0.27 may have camfix on (the v1.0.26 default); the cause is fixed by animobj now, so it
				// goes off once.
				if (version < 4) {
					for (auto& f : features) {
						if (std::string_view(f.name) == "camfix" && f.get()) {
							f.set(false);
							logger::info("Cam3rd: camfix off (Part 28 default, saved before v1.0.27)");
						}
					}
				}
				logger::info("Features (from the save): {}", OarsAndSails::Get().FeatureStatus());
				continue;
			}
			if (type == kRowerRecord) {
				a_intfc->ReadRecordData(OarsAndSails::Get().rowerTurn_);
				a_intfc->ReadRecordData(OarsAndSails::Get().rowerListHash_);
				logger::info("Seats: rower turn {} loaded from the save", OarsAndSails::Get().rowerTurn_);
				continue;
			}
			if (type == kPeaceRecord) {
				std::uint32_t n = 0;
				a_intfc->ReadRecordData(n);
				for (std::uint32_t i = 0; i < n; ++i) {
					RE::FormID oldId = 0, newId = 0;
					float aggression = 0.0f;
					a_intfc->ReadRecordData(oldId);
					a_intfc->ReadRecordData(aggression);
					if (!a_intfc->ResolveFormID(oldId, newId)) continue;
					if (auto* a = RE::TESForm::LookupByID<RE::Actor>(newId)) {
						if (auto* avo = a->AsActorValueOwner()) avo->SetBaseActorValue(RE::ActorValue::kAggression, aggression);
						logger::info("Peace: {:08X} Aggression {:.0f} restored from the co-save", newId, aggression);
					}
				}
				continue;
			}
			if (type != kCrewRecord) continue;
			std::uint32_t count = 0;
			a_intfc->ReadRecordData(count);
			for (std::uint32_t i = 0; i < count; ++i) {
				RE::FormID oldId = 0, newId = 0;
				a_intfc->ReadRecordData(oldId);
				if (a_intfc->ResolveFormID(oldId, newId)) crew.push_back(newId);  // the plugin it came from is gone: dropped
				else logger::info("Crew: {:08X} no longer exists, dropped", oldId);
			}
		}
		logger::info("Crew: {} member(s) loaded from the save", crew.size());
		if (!paintLoaded) BoatBow::SetPaint(BoatBow::kPaintBlue, "no paint in the save, the default");  // 32.5
	}

	void OnCoRevert(SKSE::SerializationInterface*)
	{
		OarsAndSails::Get().calmed_.clear();  // the world they belonged to is being unloaded
		OarsAndSails::Get().crew_.clear();
		OarsAndSails::Get().stash_.clear();   // 56.8: the co-save brings its own list back, if it has one
		OarsAndSails::Get().loadRelease_ = false;  // 56.11.4
		OarsAndSails::Get().stashRefId_ = 0;
		OarsAndSails::Get().rowerTurn_ = 0;
		OarsAndSails::Get().rowerListHash_ = 0;
		OarsAndSails::Get().savedPark_ = {};
		BoatBow::ResetSpawnTracking();                    // 31.1: the co-save fills the old list again, if it has one
		OarsAndSails::Get().propPurgePending_ = true;      // purged (and swept) once the player stands in a loaded cell
		OarsAndSails::Get().summonQueue_.clear();
		OarsAndSails::Get().ResetFeatures();  // 18.3: a new game (or a save without the record) starts with everything on
		if (BoatBow::Paint() != BoatBow::kPaintBlue) BoatBow::SetPaint(BoatBow::kPaintBlue, "reset, the default");  // 32.5
	}

	void RegisterCoSave()
	{
		if (auto* ser = SKSE::GetSerializationInterface()) {
			ser->SetUniqueID(kCoSaveId);
			ser->SetSaveCallback(OnCoSave);
			ser->SetLoadCallback(OnCoLoad);
			ser->SetRevertCallback(OnCoRevert);
		}
	}

	void OnGameLoading()
	{
		BoatBow::ForgetParked();
		LakeBoats::OnGameLoading();  // 51
		OarsAndSails::Get().ForceReset();
	}

	constexpr bool kLampListOnLoad = false;  // the one-off list the lamp forms were picked from (oas "lamp list" writes it again)

	void OnDataLoaded()
	{
		PlayerUpdateHook::Install();
		SaveHook::Install();  // 56.26
		ThirdPersonUpdateHook::Install();
		PlayerCameraUpdateHook::Install();
		ThirdPersonStateUpdateHook::Install();
		FirstPersonUpdateHook::Install();
		OarsAndSails::Get().RegisterSkimInput();
		OarsAndSails::Get().RegisterActivateSink();
		OarsAndSails::Get().RegisterDialogueSink();
		BoatBow::InitPickForms();
		RegisterConsoleCommand();
		BoatBow::RegisterTuningHotkeys();
		if (kLampListOnLoad) BoatBow::ListLamps("");  // one-off: the light records the lamp defaults were picked from
		logger::info("Oars and Sails ready (build v1.0.75 (Part 56.26: bilge floor hull ([Boat] BilgeFloor=1), saves made aboard unboard first (autosaves wait for the ride to end), saves no longer give calmed creatures their Aggression back or disturb animations, Num9 / Num3 raise and lower the player's boat (Shift for the old per-target up / down), Num5 and the numpad act only on the player's boat (NPC lake boats out of the cycle)); v1.0.74 (Part 56.24: one oar set on the player's boat: its own OarR/OarL refs take the baked pose (ref mode, no clones), never culled or left behind, the respawn's fresh ref follows; OarGuard per oar once a second while rowing, re-hung after 2 bad ticks); v1.0.73 (Part 56.23: palm side from the hand (left Y x X0, right X0 x Y), not the finger curl sign; oar guard 10 s after the resting swap and every respawn (logged, culled or 3D-less rest oars put back); 3/3 row restart failure explained); v1.0.72 (Part 56.22: the hand lock aims through the boat model oarlock fittings ([Boat] OarlockX/Y/Z 94.3/0/70.7 model space, OarlockFromBoat=1) instead of the old prop tune lock 30 inboard; fitting vs old lock logged per row start); v1.0.71 (Part 56.21: hand lock onto the palm grip: the shaft runs from a palm point G (hand node + palm-frame offset from the finger nodes, [Boat] HandGripX/Y/Z 0/7/4.2) to the oarlock, the handle end HandGripGap (12) past it; finger distances logged); v1.0.70 (Part 56.20: lake rowers drive their boat own oar refs from the baked pose ([Boat] LakeOarMode=ref, default; clone = hung copies): the same bone, hand lock and blend maths on detached wrappers, written onto the refs after their stroke, refs never hidden; lake ref fade diagnostics); v1.0.69 (Part 56.19: baked oars show only once the clip really poses the bones (over 20 from NPC Root and moving), else the rest oars stay and it retries once a second; watchdog back to the rest oars if the bones drop for 0.5 s; crew row clip restarted when W is still held after a stop (3 tries); lake rowers sent OarsRowStart again on 3D load and within 4000); v1.0.68 (Part 56.18: baked oars locked to the rower hands and the oarlocks ([Boat] HandLock=1): turned about the grip onto the hand->oarlock line and moved onto the hand (30 max), faded in with the blend; tune offset/yaw/pitch ignored with the lock, saved numpad tune reset once; Oar lock log); v1.0.67 (Part 56.16: baked oars cloned from a fresh model demand on every row start, no node kept between rows (the old one went stale once the first boat cell unloaded: nothing drawn at the lakes); source, shader, texture and fade logged per attach); v1.0.66 (Part 56.15: no end-for-end turn in the rest->row oar blend (it left oar A blade inboard), shaft dot log only, flip check also after the blend); v1.0.65 (Part 56.14: rest->row oar blend pairs the oars by boat side (the rower faces aft, AnimObjectA is the right oar), shaft direction checked, hung oars wait for the bones to be 50 out; blend frames 1/5/10/20 logged boat-local); v1.0.64 (Part 56.13: oars start and end the row from their resting spots: the hung oars show only once the clip holds the bones and blend between the rest oars and the clip over [Boat] OarBlendSeconds (0.8), both ways; first frames logged); v1.0.63 (Part 56.12: the crew stay seated aboard, rowing or resting: combat, alert and drawn weapons stopped every frame and on combat start (trigger and suppress logged), a seat lost to it gets one sit at once); v1.0.62 (Part 56.11: oar pivot clip (oars turn on the boat's oarlocks) in RowboatRow_Loopv3.hkx, rower seat yaw 180 (was 175) so both oars sit evenly, a save made aboard releases the player after the load); v1.0.61 (Part 56.9: baked oar roll defaults to 90 (blades squared on the drive, flat on the recovery), blade normal and tip height logged every 0.25 s for 4 s after each row start, blade-outboard flip check); v1.0.60 (Part 56.8: crew weapons, shields and ammo go into a hidden container of ours at boarding and come back when they leave the boat (co-save kept), no more unequip loop; combat stopped aboard, combat-idle stand-ups get one sit not the ladder; equip events logged; crew rower's first row clip no longer waits to be seated, short W taps keep the wait); v1.0.59 (Part 56.7: the baked oar clone's root (and the cached model's, once) reset to identity, it carried a ref's world spot and drew the oars about 41000 away; clone locals and geometry-vs-wrapper distance logged); v1.0.58 (Part 56.6: baked oars drawn: the oar's BSFadeNode fade kept at 1, world transform and bound rebuilt every frame, [Boat] BakedOarAttach=1 hangs them on NPC Root placed from the bones; draw diagnostics (bone/wrapper/geometry/shader/alpha/parent chain) and oar-to-camera line per row start); v1.0.57 (Part 56.5: locked numpad modes aboard with BakedOars=1: Num Lock = ROWER (rower seat and turn only), Num Enter = BAKED OARS (the hung oars only, mirrored, offset/yaw/pitch/roll/size), Num5 or the same key leaves, values kept in the ini ([Rower], [Boat] BakedOar*); visibility flags in the oarlock log); v1.0.56 (Part 56: oars baked into the rowing clip (v5): ShipOar01 hangs on the rower's AnimObjectA/B at full size while rowing, our oar refs hidden then ([Boat] BakedOars=1), AnimObject bones left to the clip with the camera's animationRotation still reset; first person keeps our oars); v1.0.55 (Part 55: controller zoom with R3 held + left stick (no view switch, no rowing while zooming), own body hidden in first person after unboarding, lake boats no longer blocked by each other (old blocked spots cleared), respawn queue logged once per cycle); v1.0.54 (Part 54.1: Miguel's tuned ini values are the defaults: flow pull 180 rapids 80 throw 250/300, 6 boats per lake, NPC boats up 11); v1.0.53 (Part 54: lake boats by lake size (none on river strips), never placed in view or near the player, the sea has 3-4 near Solitude/Dawnstar/Windhelm and 0-1 on the open sea as you row, numpad moves all NPC boats as one, undressed crew dressed); v1.0.52 (Part 53: lake boats spread farthest-first over the whole loaded lake (9000 sample, sliced over frames), not bunched at the player, steer around each other and give way, no targets near other boats); v1.0.51 (Part 52: lake boats are a lasting population (up to 10 per lake, a game day, kept in the save), drawn where they are in the loaded area up to 5 at once, longer rows, rays stop them at longboats/docks/rocks); v1.0.50 (Part 51.2: lake boats ride at the player boat height, no longer half sunk); v1.0.49 (Part 51.1: lake crew disabled first and deleted 2 s later, crash fix); v1.0.48 (Part 51: NPC rowboats on the lakes: unnamed hunters/fishermen row and fish, lantern at night, out of view spawns, oas lakehere/lakepoint/lakes/lakeboats); v1.0.47 (Part 50: oars stowed in the parked boat, swing out on boarding, back in 3 s after getting off, Num5 STOWED OARS page); v1.0.46 (Part 49: the boat and props swap to a fresh ref in the player's cell before the old cell unloads, no fade at cell loads; lamp light replaced if its cell unloads); v1.0.45 (Part 48: bank boulders (tall+steep terrain) stop and scrape the boat, cell hop moves the boat instead of re-placing it); v1.0.44 (Part 47.7: waterfall foot effects are not flow sources, no pull for 2 s after a landing, one source per fall); v1.0.43 (Part 47: pool depth cap 1500, Descent never climbs (signed slope, wall exit), flow never pulls uphill, camera landing filter); v1.0.42 (Part 46: console accepts oas flow pull 180 in any spelling, help); v1.0.41 (Part 45: weaker fightable pull, flow console commands, cross-cell hull re-place after long falls); v1.0.40 (Part 44: rapids and falls override terrain stops and force the throw, rocky descents are a slide with a speed floor, no BOW IN GROUND teleports, Jump: detector, texture override once per spawn); v1.0.39 (Part 43: weapons off on boarding, chest leak fix, hover watchdog, jitter log, softer rider lean, stronger falls pull, half stroke surge); v1.0.38 (Parts 39-41: smooth beach, feelers fixed, glue/rower fixes, water patches, flow pull+throw, smooth cascades, ice); v1.0.37 (Part 40: feelers ignore terrain and need real contact, no rebound or push when slow, player glue clears velocity and detects drift, rower no longer double-pitched; Part 39: hull extents, 7 ground samples, bow tip drives the beach lift/slide/bank stop, basin guard holds the lift); v1.0.36 (Part 38B: hull feeler reactions on: bow stops, sides and stern scrape, keel bumps); v1.0.35 (Part 38A: 12 hull feelers, log only, oas hullfeel); v1.0.34 (Part 37: a pool on the way down a fall is treated as water, no riding the floor under the surface, eased rise instead of the pop); v1.0.33 (Part 36: fall slope chatter fixed (ride lift no longer trips the detach test), rower pitches with the boat in a fall); v1.0.32 (Part 35: fall swell ignored and boat lifted over/through falls, helm sweep off for now, oars wait for the row clip); v1.0.31 (Part 33: re-seat crew right after a waterfall or cliff landing, STOOD UP re-sits instead of only logging, no row clip on a standing rower; Part 34: FNIS behavior file shipped); v1.0.30 (Part 31: every spawned prop tracked in the co-save and purged on load, oas sweep for orphans, bench props have no collision, rock launch removed; Part 32: oas paint blue|beige|vanilla, blue by default, kept in the co-save, paint: in oas status); v1.0.29 (Part 30: own diffuse textures on our boat's side and hull shapes, swapped at runtime on our boat only, rope stays vanilla, textures: in oas status); v1.0.28 (Part 29: boat hull copied from the game's own Skyrim - Meshes BSA to meshes\\OarsAndSails\\vanilla and used by the Board activator, so mesh replacers leave our boat alone; hull bounds logged; mesh= in oas status); v1.0.27 (Part 28: AnimObjectA and AnimObjectB held at their pre-clip pose while the player rows and the camera's animationRotation kept identity, oas animobj, camfix off again, TPS field log 40 frames after each row start then once a second); v1.0.26 (Part 27: flipped orbit camera mirrored back through its pivot, position and rotation together, camfix on, front view removed, per-frame ThirdPersonState field log for 3 s after each row start and stop); v1.0.25 (camera reset to the game default: camfix and camfront off, no camera writes); v1.0.24 (Part 23: final camera rebuilt from the state's forward instead of the reversed camera root, after-force check, camera starts in front of the rower, oas camfront); v1.0.23 (Part 22: final NiCamera measured and forced upright with the pitch clamp, camera state class logged at boarding, slot 3 hook logged); v1.0.22 (Parts 20-21: dog stands before release, camera rebuilt from forward with pitch clamp +/-80, camera bones held at their pre-clip baseline, bone and camera logs while the clip plays); v1.0.21 (Part 19: upright camera fix moved into the ThirdPersonState GetRotation hook that fires every frame, runs while the player is the rower); v1.0.20 (row start retry instead of permanent fallback, menu pause, camera upright runs in any camera state, all features on by default, oas status); v1.0.19 (Part 17: rower seat defaults, stray row loop guard and release event log, crew released away from the player, third person camera kept upright while the player rows, log mirror); v1.0.18 (own rowing animation via Nemesis/Pandora events OarsRowStart/IdleForceDefaultState, no GPMA file replaced); v1.0.17 (Part 15: rower bench defaults right=3 forward=228 up=-101 yaw=180); v1.0.16 (bug log B9-B12: parked boat kept in the co-save and never a vanilla boat, refusal says why, load sheathe for followers with result check, release in steps with launch guard, chest no longer re-opens every frame); v1.0.15 (bug log B1-B8: crew sheathe after unboard and load, controller A unboards, chest proxy collision and chest contents kept, helm priority with any NPC, L3 + right stick zoom, first person eye +10, no greetings while boarded, full stand-up and deck hold on every exit); v1.0.14 (activate-button input for gamepad, look at hull/helm bench/row bench for [E] Board/Steer/Row, steer refused with no rower, E only unboards while aboard, rotating rower, NPC helm priority, fair idle rotation, row bench numpad); v1.0.13 (crash breadcrumbs and flush, respawn waits for calm, no sit-retry spam, no idles on rower or stern seats, boarding stand-up fix); v1.0.12 (crew seated state tracked from graph events: idles, flinch and stand-up watcher now work; STOOD UP log); v1.0.11 (seated crew stay seated: humans answer PickNewIdle, faster re-sit, rower stop debounce 0.6 s; random eat/drink/fidget idles, oas idles; flinch on bumps); v1.0.10 (crew seat defaults from Miguel's tuning; slaughterfish/mudcrab peace while boating, oas peace); v1.0.9 (crew command, numpad targets: crew); v1.0.8 (lamp positions, teleport guard); v1.0.7 (lamp light follows))");
		OarsAndSails::LogRowerSeatDefault();
		logger::info("Features: {}", OarsAndSails::Get().FeatureStatus());
		logger::info("{}", OarsAndSails::Get().StagesLine());  // 42.2
		OarsAndSails::Get().LoadFlowCfg();  // 45.3: logs the Flow cfg line
		OarsAndSails::Get().LoadRowerIni();  // 56.5
		OarsAndSails::Get().LoadBoatHeightIni();  // 56.26
		OarsAndSails::Get().LoadDogSeatsIni();  // 56.29
		OarsAndSails::Get().LoadRowSpeedIni();  // 56.44
		OarsAndSails::Get().LoadRippleIni();  // 56.45
		BoatBow::BakedOarsOn();               // 56.5: logs the BakedOars switch and the baked oar tune
		{ const auto& b = BoatBow::RowBench(); logger::info("RowBench default: right={:.0f} forward={:.0f} up={:.0f} yaw={:.0f} scale={:.2f}", b.offset.x, b.offset.y, b.offset.z, b.yaw * 57.2958f, b.scale); }
	}
}

// ------------------------------------------------------------------
// Logging + entry point
// ------------------------------------------------------------------
void SetupLog()
{
	auto logsFolder = logger::log_directory();
	if (!logsFolder) {
		util::report_and_fail("SKSE log_directory not provided, logs can't be written");
	}

	const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
	const auto  logName = plugin ? std::string{ plugin->GetName() } + ".log" : "OarsAndSails.log";
	auto        logPath = *logsFolder / logName;

	auto                          fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
	std::vector<spdlog::sink_ptr> sinks{ fileSink };
	if (IsDebuggerPresent()) {
		sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
	}

	// 17.4 (v1.0.19): a copy of the log in a folder Miguel picks. Data\SKSE\Plugins\OarsAndSails.ini, [Log]
	// LogMirrorDir=<folder>; empty or missing = no mirror (the release packages ship without the ini).
	std::filesystem::path mirrorDir;
	std::string mirrorNote;
	{
		char buf[MAX_PATH]{};
		std::error_code cwdEc;
		const auto ini = std::filesystem::current_path(cwdEc) / "Data" / "SKSE" / "Plugins" / "OarsAndSails.ini";  // the game folder
		GetPrivateProfileStringA("Log", "LogMirrorDir", "", buf, MAX_PATH, ini.string().c_str());  // a relative path would mean C:\Windows
		std::string dir(buf);
		while (!dir.empty() && (dir.back() == ' ' || dir.back() == '\\' || dir.back() == '/')) dir.pop_back();
		if (!dir.empty()) {
			std::error_code ec;
			mirrorDir = dir;
			std::filesystem::create_directories(mirrorDir, ec);
			if (!std::filesystem::is_directory(mirrorDir, ec)) {
				mirrorNote = std::format("Log mirror: could not create '{}', no mirror", dir);
				mirrorDir.clear();
			}
		}
	}
	if (!mirrorDir.empty()) {
		std::time_t now = std::time(nullptr);
		std::tm tm{};
		localtime_s(&tm, &now);
		char stamp[32]{};
		std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);
		sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>((mirrorDir / std::format("OarsAndSails_{}.log", stamp)).string(), true));
		sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>((mirrorDir / "OarsAndSails_latest.log").string(), true));
		// Keep the last 20 timestamped logs (the names sort by time).
		std::vector<std::filesystem::path> old;
		std::error_code ec;
		for (const auto& e : std::filesystem::directory_iterator(mirrorDir, ec)) {
			const auto name = e.path().filename().string();
			if (name.starts_with("OarsAndSails_") && name != "OarsAndSails_latest.log" && e.path().extension() == ".log") old.push_back(e.path());
		}
		std::sort(old.begin(), old.end());
		for (std::size_t i = 0; i + 20 < old.size(); ++i) std::filesystem::remove(old[i], ec);
	}

	auto spdlogger = std::make_shared<spdlog::logger>("global", sinks.begin(), sinks.end());
	spdlog::set_default_logger(std::move(spdlogger));
	spdlog::set_pattern("[%H:%M:%S.%e] [%l] [%s:%#] %v");
#ifdef NDEBUG
	spdlog::set_level(spdlog::level::info);
#else
	spdlog::set_level(spdlog::level::trace);
#endif
	spdlog::flush_on(spdlog::level::info);

	if (!mirrorNote.empty()) logger::warn("{}", mirrorNote);
	if (mirrorDir.empty()) return;
	logger::info("Log mirror: {}", (mirrorDir / "OarsAndSails_latest.log").string());
	// Crash Logger's newest crash-*.log (same folder as our log) goes next to it as crash_latest.log, when newer.
	std::error_code ec;
	std::filesystem::path newest;
	std::filesystem::file_time_type newestTime{};
	for (const auto& e : std::filesystem::directory_iterator(*logsFolder, ec)) {
		const auto name = e.path().filename().string();
		if (!name.starts_with("crash-") || e.path().extension() != ".log") continue;
		const auto t = e.last_write_time(ec);
		if (newest.empty() || t > newestTime) { newest = e.path(); newestTime = t; }
	}
	const auto dest = mirrorDir / "crash_latest.log";
	if (newest.empty()) {
		logger::info("Log mirror: no crash log to copy");
	} else if (std::filesystem::exists(dest, ec) && std::filesystem::last_write_time(dest, ec) >= newestTime) {
		logger::info("Log mirror: crash_latest.log is already {}", newest.filename().string());
	} else if (std::filesystem::copy_file(newest, dest, std::filesystem::copy_options::overwrite_existing, ec)) {
		std::filesystem::last_write_time(dest, newestTime, ec);  // so the "newer" check compares crash times
		logger::info("Log mirror: copied {} to crash_latest.log", newest.filename().string());
	} else {
		logger::warn("Log mirror: could not copy {} ({})", newest.filename().string(), ec.message());
	}
}

// Plugin version info (name, version, address library flag) is declared by
// the CMake template via add_commonlibsse_plugin(... USE_ADDRESS_LIBRARY),
// which auto-generates SKSEPlugin_Version. Left untouched.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	{
		SKSE::InitInfo info{};
		info.trampoline = true;  // 56.26: the save hook's branch and stub
		info.trampolineSize = 128;
		SKSE::Init(a_skse, info);
	}
	RegisterCoSave();
	SetupLog();
	logger::info("Oars and Sails loading");

	const auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging) {
		logger::error("Failed to get SKSE messaging interface");
		return false;
	}

	if (!messaging->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
			if (a_msg->type == SKSE::MessagingInterface::kPostLoad) {
				BoatBow::PrepareVanillaHull();  // 29: before any spawn
				BoatBow::CheckBoatTextures();   // 30
			} else if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
				OnDataLoaded();
			} else if (a_msg->type == SKSE::MessagingInterface::kSaveGame) {
				OnBeforeSave(a_msg->data && a_msg->dataLen ? static_cast<const char*>(a_msg->data) : nullptr);
			} else if (a_msg->type == SKSE::MessagingInterface::kPreLoadGame ||
					   a_msg->type == SKSE::MessagingInterface::kNewGame) {
				OnGameLoading();
			}
		})) {
		logger::error("Failed to register messaging listener");
		return false;
	}

	return true;
}
