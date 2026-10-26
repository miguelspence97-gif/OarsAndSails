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

#pragma once
// BoatBow v3 - a real (collision-free) ShipRowBoat01 reference, anchored to the
// skim vehicle (not the camera) so the boat and camera can move independently.
// The reference is also re-anchored to the vehicle as it travels, and respawned
// if the engine ever unloads it, so it can't be left behind on long runs.

namespace BoatBow
{
	// Spawn the boat. Call when skim turns ON.
	bool Attach();

	// Remove the boat. Call when skim turns OFF.
	void Detach(bool a_handOff = false);  // 56.28: true at an unboard that parks here: the hull stays drawn until the parked boat shows, the props carry over

	// What the vehicle is doing this frame, so the boat can pitch to match.
	struct Flight
	{
		bool airborne = false;
		bool onRamp = false;          // riding the ground down a waterfall slope
		float verticalSpeed = 0.0f;   // units/s, positive = rising
		float horizontalSpeed = 0.0f; // units/s
		float rampPitch = 0.0f;       // slope angle in radians (negative = downhill) while onRamp
		float groundPitch = 0.0f;     // tilt from the ground under the hull (radians, + = bow up), added to the boat's pitch
		float groundRoll = 0.0f;      // (radians, + = starboard down), added to the boat's roll
		float groundBlend = 0.0f;     // 0 afloat .. 1 aground: fades out the idle rocking and the bow bob
	};

	// Call every frame while skimming.
	//   a_speed01  : speed as 0..1 of max skim speed
	//   a_turnRate : boat yaw rate in radians/sec (positive = turning right)
	//   a_dt       : frame delta in seconds
	//   a_vx/vy/vz : the vehicle's world position (the point the camera used to sit at)
	//   a_heading  : boat heading in radians (forward = sin(h), cos(h))
	void Update(float a_speed01, float a_turnRate, float a_dt,
		float a_vx, float a_vy, float a_vz, float a_heading,
		const Flight& a_flight = Flight{});

	// The periodic refresh of the boat and prop references (a fresh ref every few thousand units) waits while held:
	// fast, airborne, just landed, or a crew idle in flight. A held refresh still runs once the boat is 1.4x the
	// refresh distance from the old ref, or a warmed-up spare has waited 10 s, so the refs never fall out of the loaded area.
	void SetRefreshHold(bool a_hold);
	// True within 0.5 s of a refresh spawn or swap, or while a swap is about to happen: no animation events to the crew then.
	bool InRefreshWindow();

	// A point fixed to the boat (a_local is right/forward/up from the vehicle point, in the boat's
	// own pitched and rolled frame). Gives its world position plus the boat's forward and right
	// vectors. False if the boat isn't being driven.
	// The boat's full world rotation (heading, pitch, roll). False if the boat isn't being driven.
	bool RigidRotation(RE::NiMatrix3& a_out);
	// After a save load: find the nearest rowboat within a_range and treat it as the parked boat.
	bool AdoptNearby(const RE::NiPoint3& a_from, float a_range);
	bool RigidPoint(const RE::NiPoint3& a_local, RE::NiPoint3& a_world, RE::NiPoint3& a_forward, RE::NiPoint3& a_right);

	// A real, solid, parked rowboat the player can walk up to (collision ON).
	// Placed at world position (x, y, z) facing a_yaw (radians, forward = sin,cos).
	// Spawning again replaces the previous one.
	bool SpawnParked(float a_x, float a_y, float a_z, float a_yaw, bool a_keepProps = false);  // 56.28: keepProps after Detach(true)
	void RemoveParked();
	// Where the parked boat is (false if none): its centre and facing.
	bool GetParked(float& a_x, float& a_y, float& a_z, float& a_yaw);
	// Adopt the parked boat as the skim vehicle (collision off; Update() then drives it).
	bool AttachParked();
	// Convert between the vehicle point (where the player sits) and the boat's centre.
	void BoatCenterFromVehicle(float a_vx, float a_vy, float a_yaw, float& a_bx, float& a_by);
	void VehicleFromBoatCenter(float a_bx, float a_by, float a_yaw, float& a_vx, float& a_vy);
	void ForgetParked();  // drop the handle without touching the object (game loading)
	void NudgeParkedZ(float a_dz);  // 56.26: the parked boat up / down by a_dz (its props follow from its position)
	// The player teleported away (coc, fast travel, a door) while riding: drop the boat, props, lamps and lights without
	// calling anything on them (their cell may be gone) and stop driving. Strays are swept later.
	void Abandon();
	// Removes lamp / light references this plugin spawned that are no longer ours, around the player.
	void CleanupStrays();

	// A plank (or crate) glued to the boat at the player's seat, so the player has something to sit on.
	// right / forward / up from the vehicle point, yaw in radians, uniform scale. Tuned with the numpad.
	struct PlankTune
	{
		RE::NiPoint3 offset{ 0.0f, 8.0f, -57.0f };  // tuned in game (56.59: centred, was -2)
		float yaw = 0.0f;
		float scale = 0.65f;
	};
	// The chest in the middle of the boat (an empty container prop, placed like the bench).
	struct ChestTune
	{
		RE::NiPoint3 offset{ 0.0f, 280.0f, -93.0f };  // right / forward / up from the vehicle point (tuned in game)
		float yaw = 0.0f;
		float scale = 0.84f;
	};
	ChestTune& Chest();
	// Idle rocking of the floating boat: 0 = off, 1 = normal (default), 2 = double.
	void SetIdleBob(float a_amount);
	float IdleBob();
	void LogChest();
	// Choose the chest: a form ID (hex), or text in the model path of a container (the empty ones are preferred).
	bool SetChestModel(const char* a_text);
	PlankTune& Plank();
	void LogPlank();
	// v1.0.14: the rower's bench (same model as the helm bench / plank), a numpad target next to it.
	PlankTune& RowBench();
	void LogRowBench();

	// Part 29: copy the vanilla rowboat hull out of the game's own archives (kPostLoad), so mesh replacers do not change
	// our boat. HullMeshStatus is "vanilla copy" or "game path".
	void PrepareVanillaHull();
	const char* HullMeshStatus();
	// Part 30: our own diffuse textures for the boat's shapes (Data\textures\OarsAndSails). Checked at load; the status
	// is "rope=vanilla side=ours hull=ours".
	void CheckBoatTextures();
	std::string BoatTextureStatus();
	// Part 32: the paint job (oas paint), kept in the co-save. Blue is the default; vanilla is no override at all.
	// 56.52: black, red, white and blue-with-design side planks (same premium wood hull). The co-save keeps the number.
	enum : int { kPaintVanilla = 0, kPaintBeige = 1, kPaintBlue = 2, kPaintBlack = 3, kPaintRed = 4, kPaintWhite = 5, kPaintBlueDesign = 6, kPaintCount = 7 };
	int Paint();
	const char* PaintName(int a_paint);
	int ParsePaint(std::string_view a_name);  // -1 for an unknown name
	std::string PaintFiles(int a_paint);      // "side found, hull missing"
	void SetPaint(int a_paint, const char* a_why);  // re-applies to the loaded boat right away

	// v1.0.14 look-and-activate: OarsAndSails.esl activators. Call once at data loaded; false if the ESL is missing.
	bool InitPickForms();
	// "Boat", "HelmBench", "RowBench" or "Chest" when a_ref is one of ours (the parked boat, a pick proxy, the chest prop), else null.
	const char* PickTag(const RE::TESObjectREFR* a_ref);
	RE::TESObjectREFR* ChestRef();     // the chest prop (its container), or null
	bool ParkedSolid();                // v1.0.15: the parked boat's 3D (and so its deck collision) is loaded
	// B9: the parked boat follows the save through the co-save (its ref id, position and heading).
	RE::FormID ParkedRefId();
	bool AdoptRef(RE::FormID a_id);    // our boat by ref id (FF, our base, loaded); swaps an old static boat for the activator one
	void CleanupOldBoats();            // after a load: our leftover boats, pick proxies and lamps around the player
	// Part 31: every prop ref we spawn is tracked and kept in the co-save; after a load the last session's refs are
	// purged, and SweepOrphans finds our prop refs that no list knows (a_delete false: list only).
	std::vector<std::pair<RE::FormID, std::string>> TrackedRefsForSave();
	void ResetSpawnTracking();
	void SetStaleRefs(const std::vector<std::pair<RE::FormID, std::string>>& a_refs);
	int PurgeStaleRefs(const char* a_when);
	int SweepOrphans(bool a_delete, const char* a_when);
	// 43.3/43.4 (v1.0.39): no respawn while flying (a move instead), the hull watchdog, the chest count.
	void SetFlightHold(bool a_hold);
	float HullLag(float a_vx, float a_vy);  // hull root vs where it should be for this vehicle (-1: unknown)
	bool RespawnActive();
	void ForceReattach(float a_vx, float a_vy, float a_vz, const char* a_why);
	void ReplaceAll(const char* a_why);  // 45.1
	bool HullCellOk();
	void CellHop(float a_vx, float a_vy, float a_vz);  // 48.1
	int ReplacedCount();
	int HopCount();
	int CellSwapCount();

	// 51: shared with the lake boats (LakeBoats.cpp).
	RE::TESBoundObject* HullBase();
	RE::TESBoundObject* OarBase();
	RE::TESBoundObject* BenchBase();
	RE::TESBoundObject* LanternBase();
	RE::TESBoundObject* LanternLightBase();
	RE::NiPoint3 SternLampLocal();
	float LanternScale();
	RE::NiMatrix3 YawMatrix(float a_yaw);
	RE::NiMatrix3 RockedFrame(float a_yaw, float a_time, float& a_heave);
	void PlaceHullNode(RE::NiAVObject* a_root, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot);
	RE::NiPoint3 HullCentre(const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot);
	void PlaceOarNode(RE::NiAVObject* a_root, int a_side, float a_out, float a_phase, float a_amp, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot);
	// 56 (v1.0.56): oars baked into the rowing clip ([Boat] BakedOars, default 1). TendBakedOars hangs ShipOar01 on the
	// actor's AnimObjectA / AnimObjectB while a_want (re-attached after any 3D reload) and takes it off otherwise; true
	// while the oars hang there. a_vehicle / a_rot: the boat frame, for the one oarlock log line. a_why names the detach.
	// 56.13: true only while the hung oars draw (the caller hides its oar refs then); they take over from the refs once
	// the clip holds the bones and blend between the refs' pose (a_out / a_phase / a_amp, as PlaceOarNode) and the clip
	// over [Boat] OarBlendSeconds, both ways.
	bool BakedOarsOn();
	float SecondsSinceRespawn();  // 56.23.3: since the last "Respawn: end" (huge if none yet)
	// 56.20: a_refs (lake boats: their two oar refs' 3D, by side) with [Boat] LakeOarMode=ref: no copies are shown, the
	// baked pose is written onto those refs once the clip poses the bones (call after placing their stroke); never true.
	bool TendBakedOars(RE::Actor* a_actor, bool a_want, float a_dt, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot, float a_out, float a_phase, float a_amp,
		const char* a_why, RE::NiAVObject* const* a_refs = nullptr);
	bool LakeOarRefMode();
	// The player's boat: the same for its rower, and while true the boat's own oar refs are hidden with no stroke.
	bool TendBoatBakedOars(RE::Actor* a_rower, bool a_want, float a_dt, const char* a_why);
	void ForgetBakedOars();  // a game is loading
	// The baked oars' numpad tune: offset in each bone's own space (x out along the shaft, y forward, z up), yaw about z,
	// pitch about y, roll about x (the shaft), radians; size. For the right oar, the left one mirrored. [Boat] BakedOar*.
	struct BakedOarTune
	{
		RE::NiPoint3 offset{};
		float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
		float scale = 1.0f;
	};
	BakedOarTune& BakedOar();
	void LogBakedOar();   // "Oar bake tuning: right=.. forward=.. up=.. yaw=.. pitch=.. roll=.. scale=.."
	void SaveBakedOar();  // to the ini, and the oarlock line logged again
	RE::NiPoint3 PlaceRigidNode(RE::NiAVObject* a_root, const RE::NiPoint3& a_local, float a_yaw, float a_scale, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot);
	void StripNodePhysics(RE::NiAVObject* a_root);
	void AddLakeRef(const RE::TESObjectREFR* a_ref, std::string_view a_type);
	void RemoveLakeRef(RE::FormID a_id);
	void DeleteActorLater(RE::TESObjectREFR* a_ref);  // 51.1: disable now, delete 2 s later (never in the same frame)
	void ForgetPendingDeletes();                      // a game is loading: the handles are gone
	std::string ChestSummary();
	void SetPickAllowed(bool a_allowed);  // false while boarded: the pick proxies are removed
	// Choose the plank's model: the shortest model path (of all statics) containing this text.
	// Logs the matches. Returns false if nothing matches. Default text is "plank", then "crate".
	bool SetPlankModel(const char* a_substr);

	// Two oars (0 = right, 1 = left) hung on the boat. Each pivots about its oarlock point, which is tuned with
	// the numpad: offset is right / forward / up from the vehicle point in the boat's frame. Angles in radians.
	struct OarTune
	{
		RE::NiPoint3 offset{ 64.0f, 244.0f, -35.0f };  // the oarlock (tuned in game)
		float yaw = 1.39626f;   // 80 degrees: the shaft points out to the right, a little forward
		float pitch = -0.21817f; // -12.5 degrees: the blade end is lowered toward the water
		float roll = -1.2252f;   // -70.2 degrees: blade face angle
		float scale = 0.80f;
		float slide = -50.0f;   // moves the pivot along the shaft (+ is toward the model's +X end); -50 leaves about 70 inboard
		bool flipped = false;   // turns the whole oar end for end INCLUDING the mechanics (rarely wanted; `oar flip` flips the mesh only)
	};
	// How the oars row. They follow the boat's speed: still when it is still, faster strokes as it speeds up.
	struct StrokeTune
	{
		float swing = 0.52f;  // fore-and-aft sweep each way, radians
		float dip = 0.1815f;  // how far the blade lowers into the water on the drive and lifts on the recovery, radians
		float rate = 1.0f;    // stroke rate multiplier
	};
	StrokeTune& Stroke();
	// Rowing driven from outside (the player's throttle) instead of from the boat's speed. a_external false = the
	// oars follow the boat's speed (the old behaviour, kept for the speedboat). a_active: the rower is pulling.
	void SetStrokeDrive(bool a_external, bool a_active, float a_strokesPerSecond);
	// Puts the stroke back at the catch (called when the rowing animation starts, so body and oars begin together).
	void RestartStroke();
	// 0 while the blades are out of the water; swells through each drive stroke. Averages about 1 over a full stroke.
	float StrokeThrust();
	OarTune& Oar(int a_side);
	OarTune& OarStow(int a_side);           // 50: where each oar lies while the boat is parked
	void SetOarStowPreview(bool a_on);      // 50: show the stowed pose even aboard (while its numpad page is open)
	void LogOars();
	void MirrorOars();                // the left oar becomes a mirror image of the right one
	void ToggleOarMeshFlip();         // turns the oar MODEL end for end, about its own centre; positions, pivot and stroke are untouched
	void SetOarDemo(bool a_on);       // test stroke: rows a plain back-and-forth so the oars can be seen moving
	bool OarDemo();
	// Keeps the bench and oars on the parked boat while the player is walking about (not skimming).
	void UpdateParked(float a_dt);

	// Call on a rock bounce. a_strength roughly 0..1.
	void Impact(float a_strength);

	// Call when landing in water: the nose kicks up after the splash, then settles level.
	void Splash(float a_strength);

	// Water ripples from the engine's own water system: a wake behind the boat and rings where the oar blades dip.
	struct RippleTune
	{
		float wake = 0.05f;    // ripple size for the stern trail (same as the oar rings; many small ones make a trail)
		float oar = 0.05f;      // ripple size where a blade enters / drags through / leaves the water
		float stern = 24.0f;   // how far behind the vehicle point the wake starts
		float space = 12.0f;   // distance the boat travels between two trail ripples (smaller = a denser, more continuous trail)
		float lateral = 22.0f; // how far the trail ripples sit to either side of the centre line
		float lift = 9.0f;      // trail ripples: height above (+) or below (-) the water surface
		float yaw = 0.0873f;    // trail ripples: swing the trail's start point round the boat (radians, + = toward starboard)
		float oarReach = 95.0f;  // oar ripples: how far along the shaft from its middle the blade ring sits (model units)
		float oarSide = 0.0f;    // oar ripples: shift across the shaft
		float oarLift = 0.0f;    // oar ripples: height above (+) or below (-) the water surface
	};
	RippleTune& Ripples();
	// 56.37: the two stern trail ripples each get their own extra offset (boat frame: right, forward, up), side 0 = the
	// boat's left (port, looking forward), 1 = its right (starboard). Kept in the ini [Ripple] Side0 / Side1.
	RE::NiPoint3& WakeSideOffset(int a_side);
	void SaveWakeSideOffsets();
	void RippleAt(const RE::NiPoint3& a_pos, float a_scale);  // one ripple, regardless of the on/off switch (for testing the size)
	void SetRipplesOn(bool a_on);
	bool RipplesOn();
	// Call every frame while skimming. a_waterZ is the water surface height. a_onWater false (airborne, on a slope or
	// on land) stops the wake and the blade ripples.
	void EmitWake(float a_dt, float a_speed01, float a_heading, float a_x, float a_y, float a_waterZ, bool a_onWater);
	// 56.31: the water height under the boat this frame (before Update), so the bilge floor can sit just above it.
	void SetWaterZ(float a_z, bool a_valid);
	// 56.35: the bilge floor's height offset from its built z 28 (ini [Boat] BilgeFloorOffset), oas "floor N" / oas "num floor".
	float FloorOffset();
	void SetFloorOffset(float a_offset);

	// Two hanging lamps (0 = stern, 1 = bow): lit lanterns placed like the chest. They hang straight down from their hook
	// and swing like a pendulum when the boat speeds up, slows, turns or rocks.
	struct LampTune
	{
		RE::NiPoint3 offset;    // the hook: right / forward / up from the vehicle point
		float yaw = 0.0f;
		float scale = 1.0f;
		float swing = 1.0f;     // how strongly it swings (0 = hangs still)
		float damping = 0.35f;  // pendulum damping ratio
		float bright = 1.0f;    // light strength multiplier
		bool on = true;
	};
	LampTune& Lamp(int a_side);
	void LogLamp(int a_side);
	// The lantern model: form ID (hex), text in a static's model path, or "off". Returns false if nothing matches.
	bool SetLampForm(int a_side, const char* a_text);
	// The light both lamps give: a light form ID (hex). Empty = pick a small warm model-less light from the game's data.
	bool SetLampLight(const char* a_text);
	// Logs every light / lantern-like form whose editor ID or model path contains the text (empty = lantern, lamp, torch,
	// brazier), then the model-less lights a lamp can use, best first.
	void ListLamps(const char* a_filter);
	void SetLampAuto(bool a_on);  // on: lamps lit only from 18:00 to 06:00 game time (2 s fade)
	bool LampAuto();

	// Numpad tuning (values are written to the SKSE log):
	//   Num8/Num2 forward/back   Num4/Num6 left/right   Num9/Num3 up/down
	//   Num+/Num- scale          Num7/Num1 rotate boat left/right
	void RegisterTuningHotkeys();
}
