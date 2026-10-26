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

#include "BoatBow.h"

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace BoatBow
{
	namespace
	{
		constexpr RE::FormID kBoatStatic = 0x00050368;  // ShipRowBoat01 (Skyrim.esm)

		// ---- Part 29 (v1.0.28): our boat keeps the vanilla hull whatever a mesh replacer installs ----
		// The hull is read straight out of the game's own Skyrim - Meshes0/1.bsa (never the loose file, and never shipped
		// with the mod) and written to meshes\OarsAndSails\vanilla\ShipRowBoat01.nif; the Board activator draws that copy.
		constexpr const char* kVanillaHullModel = "OarsAndSails\\vanilla\\ShipRowBoat01.nif";  // relative to meshes
		constexpr std::string_view kHullArchiveDir = "meshes\\dungeons\\ship";
		constexpr std::string_view kHullArchiveFile = "shiprowboat01.nif";
		bool g_hullCopyReady = false;  // the copy was on disk (or written) at startup
		bool g_hullUsingCopy = false;  // the Board activator draws the copy
		bool g_hullModelLogged = false;
		// ---- Part 56.26 (v1.0.75): the bilge floor hull (vanilla + one flat "BilgeFloor" shape at z=28, sharing the hull's
		// shader property), shipped as its own file: the vanilla copy above is rewritten from the BSA, so it must stay vanilla.
		// [Boat] BilgeFloor=0 goes back to the vanilla copy. Lake boats (kBoatStatic) keep the vanilla static.
		constexpr const char* kBilgeHullModel = "OarsAndSails\\ShipRowBoat01_BilgeFloor.nif";  // relative to meshes
		bool g_bilgeLogged = false;
		bool g_hullBilge = false;  // the Board activator draws the bilge floor hull
		RE::FormID g_hullBoundsLoggedFor = 0;

		std::filesystem::path GameDataDir()
		{
			wchar_t buf[1024]{};
			const auto n = REX::W32::GetModuleFileNameW(nullptr, buf, 1024);
			return std::filesystem::path(std::wstring(buf, n)).parent_path() / "Data";
		}

		bool IEquals(std::string_view a, std::string_view b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
				return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
			});
		}

		// LZ4 frame (what SSE archives compress with): a header, then blocks until a zero size. All blocks are decoded into
		// one buffer, so linked blocks can refer back across block borders.
		bool Lz4FrameDecode(const std::uint8_t* a_src, std::size_t a_len, std::vector<std::uint8_t>& a_out, std::string& a_err)
		{
			auto u32 = [&](std::size_t a_at) { std::uint32_t v; std::memcpy(&v, a_src + a_at, 4); return v; };
			if (a_len < 7 || u32(0) != 0x184D2204) { a_err = "not an LZ4 frame"; return false; }
			const std::uint8_t flg = a_src[4];
			std::size_t p = 7 + ((flg & 0x08) ? 8 : 0) + ((flg & 0x01) ? 4 : 0);
			auto extra = [&](std::size_t& a_n, std::size_t a_end) {  // 15 means more length bytes follow, until one is not 255
				std::uint8_t more = 255;
				while (more == 255) {
					if (p >= a_end) return false;
					more = a_src[p++];
					a_n += more;
				}
				return true;
			};
			while (true) {
				if (p + 4 > a_len) { a_err = "frame cut short"; return false; }
				const std::uint32_t bs = u32(p);
				p += 4;
				if (bs == 0) break;
				const std::size_t n = bs & 0x7FFFFFFF;
				if (p + n > a_len) { a_err = "block past the end"; return false; }
				if (bs & 0x80000000) {  // stored, not compressed
					a_out.insert(a_out.end(), a_src + p, a_src + p + n);
					p += n;
				} else {
					const std::size_t end = p + n;
					while (p < end) {
						const std::uint8_t tok = a_src[p++];
						std::size_t lit = tok >> 4;
						if (lit == 15 && !extra(lit, end)) { a_err = "literal length past the block"; return false; }
						if (p + lit > end) { a_err = "literals past the block"; return false; }
						a_out.insert(a_out.end(), a_src + p, a_src + p + lit);
						p += lit;
						if (p >= end) break;  // the last sequence has literals only
						if (p + 2 > end) { a_err = "match offset past the block"; return false; }
						const std::size_t off = a_src[p] | (static_cast<std::size_t>(a_src[p + 1]) << 8);
						p += 2;
						std::size_t ml = tok & 15;
						if (ml == 15 && !extra(ml, end)) { a_err = "match length past the block"; return false; }
						ml += 4;
						if (off == 0 || off > a_out.size()) { a_err = "bad match offset"; return false; }
						const std::size_t from = a_out.size() - off;
						for (std::size_t i = 0; i < ml; ++i) {
							const std::uint8_t v = a_out[from + i];  // may overlap what this match writes
							a_out.push_back(v);
						}
					}
				}
				if (flg & 0x10) p += 4;  // block checksum
			}
			return true;
		}

		// One file from an SSE (v105) archive, found by folder and name (both stored in the archive), decompressed.
		bool ReadFromBsa(const std::filesystem::path& a_bsa, std::string_view a_dir, std::string_view a_file, std::vector<std::uint8_t>& a_out, std::string& a_err)
		{
			std::ifstream f(a_bsa, std::ios::binary);
			if (!f) { a_err = "archive not found"; return false; }
			struct Header
			{
				char magic[4];
				std::uint32_t version, folderOffset, archiveFlags, folderCount, fileCount, folderNamesLen, fileNamesLen;
				std::uint16_t fileFlags, pad;
			} hdr{};
			static_assert(sizeof(Header) == 36);
			f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
			if (!f || std::memcmp(hdr.magic, "BSA", 4) != 0 || hdr.version != 105) { a_err = std::format("not an SSE archive (version {})", hdr.version); return false; }
			if (!(hdr.archiveFlags & 0x1) || !(hdr.archiveFlags & 0x2)) { a_err = "archive without folder or file names"; return false; }
			struct Folder { std::uint64_t hash; std::uint32_t count, pad; std::uint64_t offset; };
			static_assert(sizeof(Folder) == 24);
			struct FileRec { std::uint64_t hash; std::uint32_t size, offset; };
			static_assert(sizeof(FileRec) == 16);
			std::vector<Folder> folders(hdr.folderCount);
			f.seekg(hdr.folderOffset);
			f.read(reinterpret_cast<char*>(folders.data()), static_cast<std::streamsize>(folders.size() * sizeof(Folder)));
			// The file record blocks follow, in folder order: the folder name (length-prefixed), then its file records.
			std::vector<std::pair<std::size_t, FileRec>> inDir;  // (index among all files, record) for the wanted folder
			std::size_t index = 0;
			for (const auto& fo : folders) {
				std::uint8_t n = 0;
				f.read(reinterpret_cast<char*>(&n), 1);
				std::string name(n, '\0');
				f.read(name.data(), n);
				if (!name.empty() && name.back() == '\0') name.pop_back();
				const bool want = IEquals(name, a_dir);
				for (std::uint32_t i = 0; i < fo.count; ++i, ++index) {
					FileRec r{};
					f.read(reinterpret_cast<char*>(&r), sizeof(r));
					if (want) inDir.push_back({ index, r });
				}
			}
			if (!f) { a_err = "folder records cut short"; return false; }
			if (inDir.empty()) { a_err = std::format("folder '{}' not in the archive", a_dir); return false; }
			std::string names(hdr.fileNamesLen, '\0');  // every file name, in file order, each ending in a zero
			f.read(names.data(), static_cast<std::streamsize>(names.size()));
			if (!f) { a_err = "file names cut short"; return false; }
			std::vector<std::string_view> list;
			list.reserve(hdr.fileCount);
			for (std::size_t s = 0; s < names.size();) {
				const std::size_t e = names.find('\0', s);
				list.emplace_back(names.data() + s, (e == std::string::npos ? names.size() : e) - s);
				s = e == std::string::npos ? names.size() : e + 1;
			}
			const FileRec* rec = nullptr;
			for (const auto& [i, r] : inDir) {
				if (i < list.size() && IEquals(list[i], a_file)) { rec = &r; break; }
			}
			if (!rec) { a_err = std::format("'{}' not in folder '{}'", a_file, a_dir); return false; }
			const bool compressed = ((hdr.archiveFlags & 0x4) != 0) != ((rec->size & 0x40000000) != 0);
			std::uint32_t size = rec->size & 0x3FFFFFFF;
			f.seekg(rec->offset);
			if (hdr.archiveFlags & 0x100) {  // the full path is stored in front of the data
				std::uint8_t n = 0;
				f.read(reinterpret_cast<char*>(&n), 1);
				f.seekg(n, std::ios::cur);
				size -= 1u + n;
			}
			if (!compressed) {
				a_out.resize(size);
				f.read(reinterpret_cast<char*>(a_out.data()), size);
				if (!f) { a_err = "file data cut short"; return false; }
				return true;
			}
			std::uint32_t orig = 0;
			f.read(reinterpret_cast<char*>(&orig), 4);
			std::vector<std::uint8_t> packed(size - 4);
			f.read(reinterpret_cast<char*>(packed.data()), static_cast<std::streamsize>(packed.size()));
			if (!f) { a_err = "file data cut short"; return false; }
			a_out.clear();
			a_out.reserve(orig);
			if (!Lz4FrameDecode(packed.data(), packed.size(), a_out, a_err)) return false;
			if (a_out.size() != orig) { a_err = std::format("unpacked {} bytes, expected {}", a_out.size(), orig); return false; }
			return true;
		}

		// The Board activator's model: the vanilla copy when it is on disk, otherwise the game path (replacer risk).
		void ApplyHullModel(RE::TESObjectACTI* a_acti, const char* a_when)
		{
			if (!a_acti) return;
			std::error_code ec;
			// 56.26: the bilge floor hull when it is on disk and [Boat] BilgeFloor is not 0 (default 1).
			const auto bilgePath = GameDataDir() / "meshes" / kBilgeHullModel;
			const bool bilgeOn = GetPrivateProfileIntA("Boat", "BilgeFloor", 1,
				(std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OarsAndSails.ini").string().c_str()) != 0;
			const bool bilge = bilgeOn && std::filesystem::exists(bilgePath, ec);
			if (!g_bilgeLogged) {
				g_bilgeLogged = true;
				if (bilge) SKSE::log::info("Mesh: bilge floor hull in use ({} bytes)", std::filesystem::file_size(bilgePath, ec));
				else if (!bilgeOn) SKSE::log::info("Mesh: bilge floor off ([Boat] BilgeFloor=0), vanilla hull");
				else SKSE::log::info("Mesh: bilge floor not found, vanilla hull");
			}
			if (bilge) {
				a_acti->SetModel(kBilgeHullModel);
				g_hullModelLogged = true;
				g_hullUsingCopy = true;
				g_hullBilge = true;
				return;
			}
			g_hullBilge = false;
			const bool copy = g_hullCopyReady && std::filesystem::exists(GameDataDir() / "meshes" / kVanillaHullModel, ec);
			if (copy) {
				a_acti->SetModel(kVanillaHullModel);
			} else if (auto* stat = RE::TESForm::LookupByID<RE::TESObjectSTAT>(kBoatStatic); stat && stat->GetModel()) {
				a_acti->SetModel(stat->GetModel());
			}
			if (copy != g_hullUsingCopy || !g_hullModelLogged) {
				g_hullModelLogged = true;
				if (copy) SKSE::log::info("Mesh: boat uses the vanilla copy 'meshes\\{}' ({})", kVanillaHullModel, a_when);
				else SKSE::log::warn("Mesh: using game path (replacer risk) '{}' ({})", a_acti->GetModel() ? a_acti->GetModel() : "", a_when);
			}
			g_hullUsingCopy = copy;
		}

		// ---- Part 30 (v1.0.29): our boat's own diffuse textures (Miguel's images), on our boat's 3D only ----
		// Swapped at runtime per shape: the Board activator's model is assigned at runtime, so a MODS list in the ESL would
		// have to guess the 3D indices. Only the diffuse slot changes; the normals and every other slot keep the vanilla
		// paths, and the shapes' alpha properties are left as the nif has them (the side texture's alpha is a detail map,
		// not transparency). A slot whose file is missing keeps the vanilla texture.
		// Part 32 (v1.0.30): the paint picks which of our files a shape uses (vanilla = none). The rope has no blue file and
		// stays vanilla.
		struct BoatTex
		{
			const char* slot;
			const char* shape;           // shape name in shiprowboat01.nif (NifSkope)
			const char* vanillaDiffuse;  // the vanilla diffuse file, also used to find the shape if the name differs
			const char* vanillaPath;     // the vanilla diffuse path, put back for "vanilla" until the shape's own is read
			const char* ours[kPaintCount];  // per paint, relative to Data (null: vanilla)
			bool found[kPaintCount]{};
			std::string vanillaSeen;     // the shape's vanilla diffuse path as the nif has it
		};
		BoatTex g_boatTex[] = {
			{ "rope", "L1_BoatDetails", "shiprope01.dds", "textures\\dungeons\\ships\\ShipRope01.dds",
				{ nullptr, "textures\\OarsAndSails\\boat_rope_d.dds", nullptr, nullptr, nullptr, nullptr, nullptr } },
			{ "side", "ShipRowBoat01:3", "shipwoodside01.dds", "textures\\dungeons\\ships\\ShipWoodSide01.dds",
				{ nullptr, "textures\\OarsAndSails\\boat_side_d.dds", "textures\\OarsAndSails\\boat_side_blue_d.dds",
					"textures\\OarsAndSails\\boat_side_black_d.dds", "textures\\OarsAndSails\\boat_side_red_d.dds",
					"textures\\OarsAndSails\\boat_side_white_d.dds", "textures\\OarsAndSails\\boat_side_bluedesign_d.dds" } },
			{ "hull", "ShipRowBoat01:5 - L1_BoatDetails:5", "shipwood01.dds", "textures\\dungeons\\ships\\ShipWood01.dds",
				{ nullptr, "textures\\OarsAndSails\\boat_hull_d.dds", "textures\\OarsAndSails\\boat_hull_blue_d.dds",
					"textures\\OarsAndSails\\boat_hull_d.dds", "textures\\OarsAndSails\\boat_hull_d.dds",
					"textures\\OarsAndSails\\boat_hull_d.dds", "textures\\OarsAndSails\\boat_hull_d.dds" } },
		};
		int g_paint = kPaintBlue;  // 32.5: blue unless the co-save says otherwise
		RE::FormID g_texShapesLoggedFor = 0;
		// 44.4.1: the override runs once per ref and 3D (it ran every frame for the live and the spare hull, a hitch).
		std::vector<std::pair<RE::FormID, const RE::NiAVObject*>> g_texDone;
		std::vector<RE::FormID> g_texShapesLogged;
		constexpr const char* kPaintNames[kPaintCount] = { "vanilla", "beige", "blue", "black", "red", "white", "bluedesign" };

		bool EndsWithI(std::string_view a_text, std::string_view a_end)
		{
			return a_text.size() >= a_end.size() && IEquals(a_text.substr(a_text.size() - a_end.size()), a_end);
		}

		bool IsOurTexture(const BoatTex& a_t, std::string_view a_path)
		{
			for (const char* p : a_t.ours) {
				if (p && IEquals(a_path, p)) return true;
			}
			return false;
		}

		void ApplyBoatTextures(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root)
		{
			if (!a_ref || !a_root) return;
			const std::pair<RE::FormID, const RE::NiAVObject*> key{ a_ref->GetFormID(), a_root };
			if (std::find(g_texDone.begin(), g_texDone.end(), key) != g_texDone.end()) return;
			const bool logShapes = std::find(g_texShapesLogged.begin(), g_texShapesLogged.end(), a_ref->GetFormID()) == g_texShapesLogged.end();
			int index = 0;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geo) {
				const int i = index++;
				auto* prop = a_geo->lightingShaderProp_cast();
				auto* mat = prop ? static_cast<RE::BSLightingShaderMaterialBase*>(prop->material) : nullptr;
				auto* set = mat ? mat->textureSet.get() : nullptr;
				if (!set) return RE::BSVisit::BSVisitControl::kContinue;
				const std::string_view name = a_geo->name.c_str() ? a_geo->name.c_str() : "";
				const char* d = set->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
				const char* n = set->GetTexturePath(RE::BSTextureSet::Texture::kNormal);
				const std::string_view diffuse = d ? d : "";
				if (logShapes) SKSE::log::info("Mesh: shape {} '{}' textures {} | {}", i, name, diffuse, n ? n : "");
				for (auto& t : g_boatTex) {
					const bool ours = IsOurTexture(t, diffuse);
					if (name != t.shape && !EndsWithI(diffuse, t.vanillaDiffuse) && !ours) continue;
					if (!ours && !diffuse.empty()) t.vanillaSeen = diffuse;
					// The paint's file, or the vanilla texture when the paint is vanilla or its file is missing (32.2).
					const char* paintFile = t.ours[g_paint];
					const char* want = paintFile && t.found[g_paint] ? paintFile :
					                   !t.vanillaSeen.empty()        ? t.vanillaSeen.c_str() :
					                                                   t.vanillaPath;
					if (IEquals(diffuse, want)) break;  // already wearing it
					// A copy of the material with a texture set that differs only in slot 0 (as po3's texture swaps do).
					auto* newSet = RE::BSShaderTextureSet::Create();
					for (std::uint32_t s = 0; s < RE::BSTextureSet::Texture::kTotal; ++s) {
						const auto slot = static_cast<RE::BSTextureSet::Texture>(s);
						const char* path = set->GetTexturePath(slot);
						newSet->SetTexturePath(slot, path ? path : "");
					}
					newSet->SetTexturePath(RE::BSTextureSet::Texture::kDiffuse, want);
					auto* newMat = static_cast<RE::BSLightingShaderMaterialBase*>(mat->Create());
					newMat->CopyMembers(mat);
					newMat->ClearTextures();
					newMat->OnLoadTextureSet(0, newSet);
					prop->SetMaterial(newMat, true);
					prop->SetupGeometry(a_geo);
					prop->FinishSetupGeometry(a_geo);
					newMat->~BSLightingShaderMaterialBase();
					RE::free(newMat);
					SKSE::log::info("Mesh: texture override applied to '{}' ({} -> {}), paint {}, ref {:08X}", name, diffuse, want, kPaintNames[g_paint], a_ref->GetFormID());
					break;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (index > 0) {
				if (logShapes) { g_texShapesLogged.push_back(a_ref->GetFormID()); if (g_texShapesLogged.size() > 64) g_texShapesLogged.erase(g_texShapesLogged.begin()); }
				g_texDone.push_back(key);
				if (g_texDone.size() > 64) g_texDone.erase(g_texDone.begin());
			}
		}

		// 29.5: once per boat reference, the hull's box in boat units (scale taken out), from the mesh parts' bounding
		// spheres. A replaced or rescaled hull shows up as different numbers.
		void LogHullBoundsOnce(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root)
		{
			if (!a_ref || !a_root || a_ref->GetFormID() == g_hullBoundsLoggedFor) return;
			RE::NiPoint3 mn{ FLT_MAX, FLT_MAX, FLT_MAX }, mx{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
			int parts = 0;
			const RE::NiMatrix3 inv = a_root->world.rotate.Transpose();
			const float sc = a_root->world.scale > 0.0f ? a_root->world.scale : 1.0f;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geo) {
				const auto& wb = a_geo->worldBound;
				if (wb.radius <= 0.0f) return RE::BSVisit::BSVisitControl::kContinue;
				const RE::NiPoint3 c = (inv * (wb.center - a_root->world.translate)) / sc;
				const float r = wb.radius / sc;
				mn.x = (std::min)(mn.x, c.x - r); mn.y = (std::min)(mn.y, c.y - r); mn.z = (std::min)(mn.z, c.z - r);
				mx.x = (std::max)(mx.x, c.x + r); mx.y = (std::max)(mx.y, c.y + r); mx.z = (std::max)(mx.z, c.z + r);
				++parts;
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (parts == 0) return;  // bounds not computed yet: try again next frame
			g_hullBoundsLoggedFor = a_ref->GetFormID();
			const auto* model = a_ref->GetBaseObject() ? a_ref->GetBaseObject()->As<RE::TESModel>() : nullptr;
			SKSE::log::info("Mesh: boat bounds min ({:.0f},{:.0f},{:.0f}) max ({:.0f},{:.0f},{:.0f}) size ({:.0f} x {:.0f} x {:.0f}) from {} mesh parts (bounding spheres, boat units), ref {:08X} model '{}'",
				mn.x, mn.y, mn.z, mx.x, mx.y, mx.z, mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, parts, a_ref->GetFormID(),
				model && model->GetModel() ? model->GetModel() : "");
		}

		// One boat reference in use, plus a spare spawned ahead of time. The
		// engine kills a spawned reference once the cell it was created in falls
		// out of the loaded area (that is what made the boat vanish ~30s into a
		// long run). So we keep swapping in a fresh reference spawned near the
		// player, and only drop the old one after the new one's model is loaded.
		struct Slot
		{
			RE::ObjectRefHandle handle;
			RE::NiPoint3 spawnPos{};
			bool collisionOff = false;  // SetCollision(false) re-applied once the model has loaded
			bool frozen = false;        // physics switched off (movable statics like the oar would otherwise fall and sink)
			float loadedTime = 0.0f;    // seconds since the model finished loading
		};
		Slot g_cur;
		Slot g_next;
		Slot g_leaving;              // 56.28: the boarded hull after an unboard, kept drawn until the parked boat's 3D shows
		float g_leavingTime = 0.0f;
		int g_leavingFrames = 0;     // frames the parked boat's 3D has been there
		constexpr float kRefreshDistance = 6000.0f;  // units travelled before spawning a fresh ref
		constexpr float kWarmupTime = 0.9f;          // a fresh boat fades in underneath the old one this long before the swap
		constexpr float kParkDepth = 3000.0f;        // fresh refs are created this far below the player, out of the ray path
		// v1.0.13: refresh hold (see SetRefreshHold) and the quiet window around each refresh spawn / swap.
		constexpr float kRefreshForceFactor = 1.4f;  // a held refresh still starts beyond this many refresh distances
		constexpr float kRefreshMaxHold = 10.0f;     // a warmed-up spare swaps in after waiting this long, held or not
		constexpr float kRefreshQuiet = 0.5f;
		bool g_refreshHold = false;
		float g_refreshQuiet = 0.0f;  // > 0: a refresh spawned or swapped less than kRefreshQuiet ago
		// 43.3 (v1.0.39): no respawn in a flight / fall (a move instead), one respawn at a time, forced end after 12 s.
		bool g_flightHold = false;
		bool g_flightPostponeLogged = false;
		const char* g_respawnTag = nullptr;
		float g_respawnAge = 0.0f;
		std::vector<std::string> g_queuedLogged;  // 55.4: props whose "queued behind" was logged this respawn cycle
		int g_cellSwapCount = 0;  // 49: refreshes started because the ref's cell fell behind
		constexpr float kRespawnMaxAge = 12.0f;
		// 43.3.1: where the hull root and the vehicle were at the last good placement (the hull watchdog compares against it).
		RE::NiPoint3 g_goodRoot{};
		RE::NiPoint3 g_goodVehicle{};
		bool g_goodValid = false;

		// 49: a ref whose own cell is a cell behind the player's is swapped early (the warm swap, never seen), before the
		// game unloads that cell at a cell load and takes the 3D with it (the fade). At 2 cells behind it no longer waits
		// for the refresh hold; the game unloads at 3 (uGridsToLoad 5). kCellRefreshMin stops chatter on a cell border.
		constexpr float kCellRefreshMin = 1500.0f;

		int CellsFromPlayer(RE::TESObjectREFR* a_ref)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* rc = a_ref ? a_ref->GetParentCell() : nullptr;
			auto* pc = player ? player->GetParentCell() : nullptr;
			if (!rc || !pc || rc == pc || !rc->IsExteriorCell() || !pc->IsExteriorCell()) return 0;
			const auto* a = rc->GetCoordinates();
			const auto* b = pc->GetCoordinates();
			if (!a || !b) return 0;
			return (std::max)(std::abs(a->cellX - b->cellX), std::abs(a->cellY - b->cellY));
		}

		// Should a refresh start now? a_dist2 is the squared distance travelled from the current ref, a_limit2 the squared
		// threshold, a_cells how many cells the ref's own cell is from the player's (49).
		bool RefreshMayBegin(float a_dist2, float a_limit2, const char* a_tag, int a_cells = 0)
		{
			const bool byCell = a_cells >= 1 && a_dist2 > kCellRefreshMin * kCellRefreshMin;
			if (a_dist2 <= a_limit2 && !byCell) return false;
			const bool forced = a_dist2 > a_limit2 * kRefreshForceFactor * kRefreshForceFactor || a_cells >= 2;
			if (g_refreshHold && !forced) return false;
			if (g_flightHold) {
				if (!g_flightPostponeLogged) { g_flightPostponeLogged = true; SKSE::log::info("Respawn: postponed (flight)"); }
				return false;
			}
			if (g_respawnTag && std::string_view(g_respawnTag) != a_tag) {
				// 55.4 (v1.0.55): once per respawn cycle per prop, not every frame.
				if (std::find(g_queuedLogged.begin(), g_queuedLogged.end(), a_tag) == g_queuedLogged.end()) {
					g_queuedLogged.emplace_back(a_tag);
					SKSE::log::info("Respawn: {} queued behind {}", a_tag, g_respawnTag);
				}
				return false;
			}
			SKSE::log::info("Respawn: begin {} (travelled {:.0f}{}{})", a_tag, std::sqrt(a_dist2), byCell ? std::format(", cell {} behind", a_cells) : std::string{},
				g_refreshHold ? ", forced while held" : "");
			if (byCell) ++g_cellSwapCount;
			g_refreshQuiet = kRefreshQuiet;
			g_respawnTag = a_tag;
			g_respawnAge = 0.0f;
			std::erase(g_queuedLogged, std::string_view(a_tag));  // this prop spawns now: its next wait is logged again
			return true;
		}

		bool RefreshMaySwap(float a_loadedTime)
		{
			return a_loadedTime >= kWarmupTime && (!g_refreshHold || a_loadedTime >= kWarmupTime + kRefreshMaxHold);
		}

		bool g_active = false;  // true between Attach() and Detach()

		// Tunables (Skyrim units, ~70 = 1 metre). Offsets are in BOAT space,
		// relative to the vehicle point: +X right, +Y forward, +Z up.
		// The vehicle point sits ~35 above the water, so -45 puts the boat's
		// origin ~10 below the surface.
		// Roll and pitch swing about this point below the vehicle point (about where the rider's hips are),
		// not about eye height. Rolling about the eyes slid the hull and bench sideways past the seated player.
		constexpr float kRollPivotDrop = 35.0f;
		float g_scale = 1.0f;
		RE::NiPoint3 g_offset{ 0.0f, 250.0f, -112.0f };  // 56.59: dead centre (was -2)
		float g_yaw = 0.0f;  // extra rotation of the mesh relative to the heading, radians
		// The boat's last rigid transform, so passengers can be glued to it.
		RE::NiMatrix3 g_rigidRot;
		RE::NiPoint3 g_rigidPivot{};
		bool g_rigidValid = false;

		float g_time = 0.0f;
		float g_roll = 0.0f;
		float g_pitch = 0.0f;
		float g_impulse = 0.0f;
		float g_pitchVel = 0.0f;  // nose pitch spring velocity (rad/s)
		float g_airTime = 0.0f;   // seconds airborne, drives the head-first dive
		float g_bobPhaseA = 0.0f; // bow bob phases (frequency rises with speed, so we integrate)
		float g_bobPhaseB = 0.0f;
		float g_idleBob = 1.0f;   // 0 = off, 1 = normal: the slow rocking of a boat floating on water (strongest at rest)
		// The slow rocking of a floating boat: roll about 1.1 degrees, pitch about 0.6 degrees, a couple of units of heave.
		// Two sines per axis so it never repeats exactly. a_rest is 1 at rest and smaller while rowing.
		void IdleRock(float a_t, float a_rest, float& a_roll, float& a_pitch, float& a_heave)
		{
			const float r = a_rest * g_idleBob;
			a_roll = r * 0.019f * (0.68f * std::sin(a_t * 1.19f) + 0.32f * std::sin(a_t * 2.31f + 0.9f));
			a_pitch = r * 0.010f * (0.64f * std::sin(a_t * 1.67f + 1.7f) + 0.36f * std::sin(a_t * 0.83f + 0.4f));
			a_heave = r * 2.4f * (0.7f * std::sin(a_t * 1.43f + 0.3f) + 0.3f * std::sin(a_t * 2.7f + 2.1f));
		}
		float g_logTimer = 0.0f;
		float g_missingTime = 0.0f;     // how long the boat's 3D has been absent
		float g_respawnCooldown = 0.0f;
		bool g_loggedLoaded = false;

		void LogTuning()
		{
			SKSE::log::info("BoatBow tuning: scale={:.2f} offset=({:.1f}, {:.1f}, {:.1f}) yaw={:.1f}deg",
				g_scale, g_offset.x, g_offset.y, g_offset.z, g_yaw * 57.2958f);
		}

		bool PlayerReadyForSpawnBoat()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			return cell && cell->IsAttached() && player->Get3D();
		}

		RE::NiMatrix3 MakeMat(float a00, float a01, float a02,
			float a10, float a11, float a12,
			float a20, float a21, float a22)
		{
			RE::NiMatrix3 m;
			m.entry[0][0] = a00; m.entry[0][1] = a01; m.entry[0][2] = a02;
			m.entry[1][0] = a10; m.entry[1][1] = a11; m.entry[1][2] = a12;
			m.entry[2][0] = a20; m.entry[2][1] = a21; m.entry[2][2] = a22;
			return m;
		}

		float g_spawnRetry = 0.0f;  // seconds until another spawn attempt is allowed after a failure
		int g_spawnFailures = 0;

		RE::TESObjectACTI* HullActi();  // 56.32: defined after g_actiBoat
		bool Spawn(Slot& a_slot)
		{
			if (g_spawnRetry > 0.0f) {
				return false;  // recent failure: don't hammer the engine every frame
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				SKSE::log::error("BoatBow: no player");
				g_spawnRetry = 1.0f;
				return false;
			}

			if (!PlayerReadyForSpawnBoat()) { g_spawnRetry = 1.0f; return false; }  // never into an unloaded cell (crashed after a teleport)
			auto* stat = RE::TESForm::LookupByID<RE::TESObjectSTAT>(kBoatStatic);
			// 56.32: the riding hull's respawns use our Board activator too (its model is the bilge floor hull or the
			// vanilla copy), not the game's boat static: the fresh ref after a cell swap had no floor (and a mesh replacer
			// on the static would have shown on it). The static stays the fallback.
			RE::TESBoundObject* spawnBase = HullActi();
			if (!spawnBase && !stat) {
				SKSE::log::warn("BoatBow: static {:08X} not found, trying editorID", kBoatStatic);
				stat = RE::TESForm::LookupByEditorID<RE::TESObjectSTAT>("ShipRowBoat01");
			}
			if (!spawnBase) spawnBase = stat;
			if (!spawnBase) {
				SKSE::log::error("BoatBow: boat static not found by formID or editorID");
				return false;
			}

			SKSE::log::info("Spawn: boat ref, placing ({})", spawnBase == stat ? "boat static" : "Board activator hull");  // crash breadcrumb (v1.0.13)
			auto ref = player->PlaceObjectAtMe(spawnBase, false);
			if (!ref) {
				g_spawnRetry = 1.0f;
				++g_spawnFailures;
				// Log the first few failures with context (then every 20th) so the next
				// log shows WHY the engine refuses: no cell, cell not attached, etc.
				if (g_spawnFailures <= 5 || g_spawnFailures % 20 == 0) {
					auto* cell = player->GetParentCell();
					const RE::NiPoint3 pp = player->GetPosition();
					SKSE::log::error("BoatBow: PlaceObjectAtMe failed (#{}): playerCell={} attached={} exterior={} player3D={} pos=({:.0f},{:.0f},{:.0f})",
						g_spawnFailures, cell != nullptr, cell ? cell->IsAttached() : false,
						cell ? cell->IsExteriorCell() : false, player->Is3DLoaded(), pp.x, pp.y, pp.z);
				}
				return false;
			}
			g_spawnFailures = 0;

			const RE::NiPoint3 pos = ref->GetPosition();
			a_slot.spawnPos = pos;
			// A fresh boat appears at the player's feet with real collision for its first
			// moments, right where the skim rays are cast; that launched the camera at
			// every swap. Park the reference deep below instead (same cell column, so
			// it stays loaded). We position the visible 3D ourselves every frame.
			ref->SetPosition(pos.x, pos.y, pos.z - kParkDepth);
			ref->SetCollision(false);
			a_slot.handle = ref->GetHandle();
			a_slot.collisionOff = false;
			a_slot.loadedTime = 0.0f;

			SKSE::log::info("BoatBow: spawned ref {:08X}", ref->GetFormID());
			return true;
		}

		void Despawn(Slot& a_slot)
		{
			if (!a_slot.handle) {
				return;
			}
			if (auto ref = a_slot.handle.get()) {
				SKSE::log::info("Delete: ref {:08X}", ref->GetFormID());  // crash breadcrumb (v1.0.13)
				ref->Disable();
				ref->SetDelete(true);
			}
			a_slot.handle.reset();
		}

		// ---- Part 31 (v1.0.30): every prop ref we create is tracked, and the list goes into the co-save, so a load can
		// delete the refs the last session left in the save (the floating bench). Deleted refs are dropped when saving.
		struct TrackedRef
		{
			RE::FormID id;
			std::string type;
		};
		std::vector<TrackedRef> g_spawnedRefs;  // created this session
		std::vector<TrackedRef> g_staleRefs;    // read from the co-save: the last session's, purged after the load

		void TrackSpawn(const RE::TESObjectREFR* a_ref, std::string_view a_type)
		{
			if (a_ref) g_spawnedRefs.push_back({ a_ref->GetFormID(), std::string(a_type) });
		}

		// 51: the lake boats' refs (LakeBoats.cpp): never adopted, swept or purged while they are live.
		std::vector<RE::FormID> g_lakeRefs;
		bool IsLakeRef(const RE::TESObjectREFR* a_ref)
		{
			return a_ref && std::find(g_lakeRefs.begin(), g_lakeRefs.end(), a_ref->GetFormID()) != g_lakeRefs.end();
		}

		// 51.1: an actor deleted in the frame it is disabled crashed the game (its movement job still ran on it, crash
		// 01:03:31). Actors are disabled now and deleted 2 s later.
		struct PendingDelete { RE::ObjectRefHandle ref; float t; };
		std::vector<PendingDelete> g_pendingDeletes;
		void TendPendingDeletes(float a_dt)
		{
			std::erase_if(g_pendingDeletes, [&](PendingDelete& p) {
				if ((p.t -= a_dt) > 0.0f) return false;
				if (auto r = p.ref.get()) {
					std::erase(g_lakeRefs, r->GetFormID());
					SKSE::log::info("Delete: actor {:08X} (disabled 2 s ago)", r->GetFormID());
					r->SetDelete(true);
				}
				return true;
			});
		}

		// ---- the plank (a prop at the player's seat) ---------------------------------
		// Each prop is its own spawned reference (plus a spare for swapping, like the boat) whose 3D root we place
		// every frame. They work both while skimming and on the parked boat.
		struct Prop
		{
			std::string text;               // form ID or text in a model path
			RE::TESBoundObject* base = nullptr;
			Slot cur;
			Slot next;
			float retry = 0.0f;
			int failLogs = 0;
			bool isPlank = false;           // the bench falls back to a plank, then a crate, if its form is missing
			bool needsFreeze = false;       // a movable static with Havok physics: pin it so it can't drop into the sea
			const char* tag = "Prop";
			float collTimer = 0.0f;         // pinned props get collision switched off again every half second
			int freezeTries = 0;            // pinning is repeated a few times in case the physics body was not built yet
			int placeLogs = 0;
			bool keepForm = false;          // keep the exact form (a container) instead of swapping in a plain static with the same model
			RE::NiAVObject* strippedRoot = nullptr;  // 31.1.4: the bench 3D whose physics body was already removed
			RE::NiAVObject* strippedNext = nullptr;
			int stripLogs = 0;
		};
		PlankTune g_plank;
		ChestTune g_chest;
		Prop g_chestProp{ "000F3923", nullptr, {}, {}, 0.0f, 0, false, true, "Chest", 0.0f, 0, 0, true };
		// Hanging lamps. The game's data has no light form with a lantern model (the lantern-named lights are all torches,
		// see `oas "lamp list"`), so each lamp is a plain lantern static plus a small warm model-less light at its centre.
		// CandleLanternwithCandle01 (Skyrim.esm STAT 0002D847): the candle lantern with a ring on top, used for both
		// (the data has no larger / smaller pair in the same style); the bow one is scaled down.
		constexpr const char* kLampSternForm = "0002D847";
		constexpr const char* kLampBowForm = "0002D847";
		LampTune g_lamp[2] = {
			LampTune{ RE::NiPoint3{ 0.0f, -91.0f, 28.0f }, 0.0f, 1.0f },   // 56.59: centred (was -2); stern post (Miguel's numpad tuning, v1.0.7)
			LampTune{ RE::NiPoint3{ 0.0f, 577.0f, 0.0f }, 0.0f, 0.85f } };  // 56.59: centred (was -2); bow tip (Miguel's numpad tuning, v1.0.7)
		Prop g_lampProp[2] = {
			Prop{ kLampSternForm, nullptr, {}, {}, 0.0f, 0, false, false, "LampStern" },
			Prop{ kLampBowForm, nullptr, {}, {}, 0.0f, 0, false, false, "LampBow" } };
		// The light both lamps give. Default: 0001CBB3 from Skyrim.esm (warm 251,162,96, radius 300). A released mod may only
		// depend on the base-game masters, so the automatic pick never uses a light from another plugin.
		constexpr const char* kLampLightDefault = "0001CBB3";
		std::string g_lampLightText;
		RE::TESBoundObject* g_lampLightBase = nullptr;
		bool g_lampLightPicked = false;
		Slot g_lampLight[2];
		bool g_lampCleanup = true;   // after a game load: remove lamp / light refs left in the world by the last session
		struct LampState
		{
			bool have = false;
			RE::NiPoint3 prevPivot{};
			RE::NiPoint3 prevVel{};
			float pitch = 0.0f, roll = 0.0f, pitchVel = 0.0f, rollVel = 0.0f;  // pendulum, radians (+ pitch = bottom toward the bow, + roll = toward starboard)
			RE::NiPointLight* light = nullptr;
			float baseFade = 1.0f;
			bool lightLogged = false;
			float nightFade = 1.0f;
			float lightRetry = 0.0f;
			RE::NiAVObject* flameRoot = nullptr;      // the lantern 3D whose flames were already switched off
			RE::NiAVObject* flameNextRoot = nullptr;
			float followTimer = 0.0f;                 // time since the light was spawned, for the two follow checks
			int followLogs = 0;
		};
		LampState g_lampState[2];
		bool g_lampAuto = true;  // v1.0.20 (18.3): on by default (lit 18:00 to 06:00); `oas "lamp auto off"` keeps them lit
		float g_lampBudgetTimer = 0.0f;
		bool g_lampBudgetWarned = false;
		Prop g_plankProp{ "00041EC7", nullptr, {}, {}, 0.0f, 0, true, false, "Plank" };  // the bench from Skyrim.esm
		// v1.0.14: the rower's bench, the same plank model as the helm bench. Its default is the helm bench's offset from
		// the helm seat (right -2, forward 0, up +14 above where the seated body sits, turn 0) applied to the rower seat
		// (right 0, forward 262, body at -80 facing the stern at 175 degrees): right -2, forward 262, up -66, turn 175.
		PlankTune g_rowBench{ { 0.0f, 228.0f, -101.0f }, 3.14159265f, 0.65f };  // 56.59: centred (was right 3)  // Part 15: Miguel's numpad values (v1.0.16 session)
		Prop g_rowBenchProp{ "00041EC7", nullptr, {}, {}, 0.0f, 0, true, false, "RowBench" };
		OarTune g_oar[2] = {
			OarTune{},
			OarTune{ RE::NiPoint3{ -64.0f, 244.0f, -35.0f }, -1.39626f, -0.21817f, 1.2252f, 0.80f, -50.0f, false } };
		Prop g_oarProp[2] = {
			Prop{ "00050E77", nullptr, {}, {}, 0.0f, 0, false, true, "OarR" },  // ShipOar01 from Skyrim.esm
			Prop{ "00050E77", nullptr, {}, {}, 0.0f, 0, false, true, "OarL" } };
		// 50: the oars lie stowed in the boat while it is parked (lengthwise on the floor, no collision), swing out to the
		// oarlocks on boarding and back in a few seconds after getting off. g_oarOut: 0 stowed, 1 at the oarlocks. The
		// stowed pose is tuned on the third oar numpad page; size always comes from the rowing tune.
		OarTune g_oarStow[2] = {
			// 56.35: Miguel's tuned stowed pose (right 18, forward 309, up -53, turn 7.9 deg), mirrored for the left oar.
			OarTune{ RE::NiPoint3{ 18.0f, 309.0f, -53.0f }, 0.13788f, 0.0f, 0.0f, 0.80f, 0.0f, false },
			OarTune{ RE::NiPoint3{ -18.0f, 309.0f, -53.0f }, -0.13788f, 0.0f, 0.0f, 0.80f, 0.0f, false } };
		float g_oarOut = 0.0f;
		float g_oarStowDelay = 0.0f;   // seconds left before the oars come in after getting off
		float g_oarStowPreview = 0.0f; // > 0: the stowed oars are being tuned, show that pose even aboard (runs out after 10 s)
		constexpr float kOarOutTime = 0.5f;
		constexpr float kOarInTime = 0.8f;
		constexpr float kOarStowAfter = 3.0f;
		bool g_oarDemo = false;
		bool g_oarMeshFlip = true;     // the oar model is drawn end for end (blade out); every position and the stroke stay as tuned
		// 56 (v1.0.56): the player's boat's rower has the oars baked into the clip: our oar refs are hidden and the stroke
		static RE::NiPoint3 g_oarTrailLast[2]{};  // 56.39: blade glide trail: where the last trail ripple went, per oar
		static bool g_oarTrailHave[2] = { false, false };
		static float g_oarTrailTime[2] = { 0.0f, 0.0f };
		static bool g_oarWasSubmerged[2] = { false, false };// does not move them. g_oarCulledByBake: shown again once (the ref may be hidden for other reasons too).
		bool g_boatOarsBaked = false;
		bool g_oarCulledByBake[2] = { false, false };
		// 56.24: one oar set on the player's boat: its own OarR/OarL refs take the baked pose (ref mode, as the lake boats),
		// no clones. g_boatRefDriven: the baked pose drives them now (TendOar leaves them alone, only follows with the swap
		// ref); g_forceRefMode: set around the player's boat call so ref mode applies whatever LakeOarMode says.
		bool g_boatRefDriven = false;
		bool g_forceRefMode = false;
		// 56.23.2: oar vanish guard. After the rowing -> resting swap and after every respawn the rest oar refs are checked
		// once a second for 10 s (logged, and put back if culled or without 3D while not baked).
		float g_oarGuardFor = 0.0f, g_oarGuardTick = 0.0f;
		int g_oarGuardNo3D[2] = { 0, 0 };
		std::chrono::steady_clock::time_point g_lastRespawnEnd{};
		bool g_anyRespawnEnd = false;
		void ArmOarGuard(const std::string& a_why)
		{
			g_oarGuardFor = 10.0f;
			g_oarGuardTick = 0.0f;
			g_oarGuardNo3D[0] = g_oarGuardNo3D[1] = 0;
			SKSE::log::info("Oar guard: watching the rest oars for 10 s ({})", a_why);
		}
		void NoteRespawnEnd(const char* a_tag)
		{
			g_lastRespawnEnd = std::chrono::steady_clock::now();
			g_anyRespawnEnd = true;
			ArmOarGuard(std::format("respawn end {}", a_tag ? a_tag : ""));
		}
		void TendOarGuard(float a_dt, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot);
		RE::NiPoint3 g_frameVehicle{};  // the player's boat frame of the last FollowProps (oarlock log)
		RE::NiMatrix3 g_frameRot{};
		float g_oarPhase = 0.0f;
		bool g_oarBoundsKnown = false;
		bool g_oarAxisX = false;        // the oar model's long axis is X (otherwise Y)
		RE::NiPoint3 g_oarCentre{};     // centre of the oar model's bounds, in model units
		float g_oarHalfLen = 121.0f;    // 56.9: half the model's length along its long axis
		StrokeTune g_stroke;
		float g_oarSpeed = 0.0f;   // the boat's speed as 0..1, set by Update() (0 on the parked boat)
		bool g_rowExternal = false;    // the oars are driven by the player's throttle (rowing), not by the boat's speed
		bool g_rowActive = false;
		float g_rowRate = 0.4f;        // strokes per second while rowing
		// Ripples (engine water system). The wake is driven from EmitWake; the blade ripples from the stroke phase.
		RippleTune g_ripple;
		RE::NiPoint3 g_wakeSideOff[2]{};  // 56.37: per stern ripple extra offset (0 = port / left, 1 = starboard / right)
		bool g_wakeSideLoaded = false;
		std::string RippleIniPath()
		{
			std::error_code ec;
			return (std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OarsAndSails.ini").string();
		}
		void LoadWakeSideOffsets()
		{
			if (g_wakeSideLoaded) return;
			g_wakeSideLoaded = true;
			for (int i = 0; i < 2; ++i) {
				char buf[64]{};
				GetPrivateProfileStringA("Ripple", i == 0 ? "Side0" : "Side1", "", buf, sizeof(buf), RippleIniPath().c_str());
				float x = 0.0f, y = 0.0f, z = 0.0f;
				if (buf[0] && std::sscanf(buf, "%f,%f,%f", &x, &y, &z) == 3) g_wakeSideOff[i] = RE::NiPoint3{ x, y, z };
			}
		}
		bool g_rippleOn = true;   // the oar ripples look right at the default size; `oas ripples` toggles
		float g_rippleWaterZ = 0.0f;
		float g_rippleWaterAge = 99.0f;   // seconds since EmitWake last said we are on water (blade ripples need it fresh)
		float g_wakeTimer = 0.0f;
		bool g_wakeHave = false;     // the trail has a previous drop point
		float g_wakeLastX = 0.0f, g_wakeLastY = 0.0f;
		float g_wakeSide = 1.0f;
		float g_prevPhase = 0.0f;
		float g_dragTimer = 0.0f;
		bool g_catchNow = false;
		bool g_releaseNow = false;
		bool g_dragNow = false;
		int g_rippleLogs = 0;
		int g_wakeCount = 0, g_oarCount = 0;   // ripples placed since the last stats line
		float g_rippleStatTimer = 0.0f;

		void AddRippleAt(const RE::NiPoint3& a_pos, float a_scale)
		{
			if (!g_rippleOn) return;
			auto* water = RE::TESWaterSystem::GetSingleton();
			if (!water) return;
			water->AddRipple(a_pos, a_scale);
			++g_oarCount;
			if (g_rippleLogs < 6) {
				++g_rippleLogs;
				SKSE::log::info("Ripple: ({:.0f},{:.0f},{:.0f}) scale={:.2f}", a_pos.x, a_pos.y, a_pos.z, a_scale);
			}
		}

		float g_oarAmp = 0.0f;     // 0 = oars at rest, 1 = full stroke; eased so they settle back when the boat stops

		// A container whose model path contains the text. Empty containers are preferred, then the shortest model path.
		RE::TESBoundObject* FindContainerBase(const std::string& a_text, bool a_log)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) return nullptr;
			std::string needle = a_text;
			for (auto& ch : needle) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			RE::TESObjectCONT* best = nullptr;
			int bestScore = -1;
			bool bestEmpty = false;
			std::string bestPath;
			int matches = 0;
			for (auto* cont : dh->GetFormArray<RE::TESObjectCONT>()) {
				if (!cont) continue;
				const char* mp = cont->GetModel();
				if (!mp || !*mp) continue;
				std::string path = mp;
				std::string lower = path;
				for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
				if (lower.find(needle) == std::string::npos) continue;
				// Not a plain chest: barrels, urns, sacks, coffins and trapped or special containers.
				bool skip = false;
				for (const char* bad : { "barrel", "urn", "sack", "coffin", "sarcoph", "trap", "dragon", "crate", "dwe", "falmer", "dwarven", "vamp", "hagraven", "ice", "snow", "ash", "tomb", "ruin", "stone", "metal" }) {
					if (lower.find(bad) != std::string::npos) { skip = true; break; }
				}
				if (skip) continue;
				const bool empty = static_cast<RE::TESContainer*>(cont)->numContainerObjects == 0;
				++matches;
				if (a_log && matches <= 25) SKSE::log::info("Chest: match {:08X} '{}' model '{}' {}", cont->GetFormID(), cont->GetName(), path, empty ? "EMPTY" : "has contents");
				// A plain wooden chest first, then an empty one, then the shortest model path.
				const bool wooden = lower.find("wood") != std::string::npos || lower.find("common") != std::string::npos;
				const int score = (wooden ? 2 : 0) + (empty ? 1 : 0);
				const bool better = !best || score > bestScore || (score == bestScore && path.size() < bestPath.size());
				if (better) { best = cont; bestScore = score; bestEmpty = empty; bestPath = path; }
			}
			if (best) SKSE::log::info("Chest: using {:08X} '{}' model '{}' ({}; {} matches for '{}')", best->GetFormID(), best->GetName(), bestPath, bestEmpty ? "empty" : "NOT empty", matches, a_text);
			else SKSE::log::warn("Chest: no container with a model path containing '{}'", a_text);
			return best;
		}

		RE::TESBoundObject* FindPlankBase(const std::string& a_text, bool a_log)
		{
			if (a_text.rfind("cont:", 0) == 0) return FindContainerBase(a_text.substr(5), a_log);
			// A form ID (5-8 hex digits, optional 0x) is looked up directly.
			{
				std::string hex = a_text;
				if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) hex = hex.substr(2);
				const bool isHex = hex.size() >= 5 && hex.size() <= 8 &&
					std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
				if (isHex) {
					const auto id = static_cast<RE::FormID>(std::stoul(hex, nullptr, 16));
					if (auto* form = RE::TESForm::LookupByID<RE::TESBoundObject>(id)) {
						SKSE::log::info("Plank: using form {:08X} ('{}')", id, form->GetName());
						return form;
					}
					SKSE::log::warn("Plank: no object with form ID {:08X}", id);
					return nullptr;
				}
			}
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) return nullptr;
			std::string needle = a_text;
			for (auto& ch : needle) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			RE::TESObjectSTAT* best = nullptr;
			std::string bestPath;
			int matches = 0;
			for (auto* stat : dh->GetFormArray<RE::TESObjectSTAT>()) {
				if (!stat) continue;
				const char* mp = stat->GetModel();
				if (!mp || !*mp) continue;
				std::string path = mp;
				std::string lower = path;
				for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
				if (lower.find(needle) == std::string::npos) continue;
				++matches;
				if (a_log && matches <= 25) SKSE::log::info("Plank: match {:08X} model '{}'", stat->GetFormID(), path);
				if (!best || path.size() < bestPath.size()) { best = stat; bestPath = path; }
			}
			if (best) SKSE::log::info("Plank: using {:08X} model '{}' ({} matches for '{}')", best->GetFormID(), bestPath, matches, a_text);
			else SKSE::log::warn("Plank: no static with a model path containing '{}'", a_text);
			return best;
		}

		// 56.31: the bilge floor (a flat 'BilgeFloor' shape at model z 28 in the bilge hull) rides just above the real
		// waterline: 3 above the water at the hull centre, never higher than its built height, and down to z 12 (16 lower,
		// inside the planks, out of sight) when the water is low (parked, slow). So it only shows when the water would.
		float g_waterZ = 0.0f;
		bool g_waterValid = false;
		constexpr float kBilgeBuiltZ = 28.0f;
		constexpr float kBilgeLowest = -16.0f;
		// 56.35: the floor's own height offset (model units, from its built z 28), tuned with oas "floor N" / oas "num floor",
		// kept in the ini [Boat] BilgeFloorOffset. Applied to every hull we draw (riding, its respawn, parked).
		std::string FloorIniPath()
		{
			std::error_code ec;
			return (std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OarsAndSails.ini").string();
		}
		constexpr float kDefaultFloorOffset = 3.0f;  // 56.42: Miguel's tuned floor height (+3, floor at model z 31)
		float g_floorOffset = kDefaultFloorOffset;
		bool g_floorOffsetLoaded = false;
		void LoadFloorOffset()
		{
			if (g_floorOffsetLoaded) return;
			g_floorOffsetLoaded = true;
			char buf[32]{};
			GetPrivateProfileStringA("Boat", "BilgeFloorOffset", "3", buf, sizeof(buf), FloorIniPath().c_str());
			try { g_floorOffset = std::clamp(std::stof(buf), -30.0f, 20.0f); } catch (...) { g_floorOffset = kDefaultFloorOffset; }
			SKSE::log::info("Bilge: floor offset {:+.1f} (floor at model z {:.1f}, ini)", g_floorOffset, 28.0f + g_floorOffset);
		}
		void ApplyFloorOffset(RE::NiAVObject* a_root)
		{
			if (!a_root) return;
			LoadFloorOffset();
			RE::NiAVObject* floor = nullptr;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geo) {
				if (a_geo && a_geo->name.c_str() && std::string_view(a_geo->name.c_str()) == "BilgeFloor") { floor = a_geo; return RE::BSVisit::BSVisitControl::kStop; }
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (!floor || std::abs(floor->local.translate.z - g_floorOffset) < 0.01f) return;
			floor->local.translate.z = g_floorOffset;
			RE::NiUpdateData data{};
			floor->Update(data);
		}

		[[maybe_unused]] void TendBilgeFloor(RE::NiAVObject* a_root, bool a_haveWater)
		{
			if (!a_root) return;
			// 56.32: found by walking the shapes (GetObjectByName never found it: no 'Bilge:' line in the log).
			RE::NiAVObject* floor = nullptr;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geo) {
				if (a_geo && a_geo->name.c_str() && std::string_view(a_geo->name.c_str()) == "BilgeFloor") { floor = a_geo; return RE::BSVisit::BSVisitControl::kStop; }
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (!floor) {
				static int missLogs = 0;
				if (missLogs++ < 5) SKSE::log::info("Bilge: no BilgeFloor shape in '{}'", a_root->name.c_str() ? a_root->name.c_str() : "");
				return;
			}
			float off = kBilgeLowest;
			float waterModel = -999.0f;
			if (a_haveWater && g_waterValid) {
				const auto& w = a_root->world;
				const float sc = w.scale > 0.0f ? w.scale : 1.0f;
				const RE::NiPoint3 centre = w.translate + w.rotate * (RE::NiPoint3{ 0.0f, 0.0f, kBilgeBuiltZ } * sc);
				const RE::NiPoint3 local = (w.rotate.Transpose() * (RE::NiPoint3{ centre.x, centre.y, g_waterZ } - w.translate)) / sc;
				waterModel = local.z;
				off = std::clamp(waterModel + 3.0f - kBilgeBuiltZ, kBilgeLowest, 0.0f);
			}
			if (std::abs(floor->local.translate.z - off) > 0.01f) {
				floor->local.translate.z = off;
				RE::NiUpdateData data{};
				floor->Update(data);
			}
			static float logT = 0.0f;
			static int logs = 0;
			logT += 1.0f;
			if (logs < 40 && logT >= 120.0f) {  // about every 2 s at 60 fps, 40 lines
				logT = 0.0f; ++logs;
				SKSE::log::info("Bilge: water at model z {:.1f}, floor at z {:.1f} ({})", waterModel, kBilgeBuiltZ + off, off <= kBilgeLowest + 0.01f ? "hidden low" : "showing");
			}
		}

		// 56.28: a fresh ref's 3D fades in from 0 (BSFadeNode currentFade), the fade seen at cell swaps and at board / unboard.
		// Our refs are always held fully drawn: every BSFadeNode in the model kept at 1, every frame. Logged when one was below 1
		// (if the same ref keeps logging every frame, the engine rewrites the fade after us).
		int KeepOpaqueNodes(RE::NiAVObject* a_obj, float& a_minSeen, int a_depth)
		{
			if (!a_obj || a_depth > 6) return 0;
			int n = 0;
			if (auto* fade = a_obj->AsFadeNode()) {
				auto& rd = fade->GetRuntimeData();
				if (rd.currentFade < a_minSeen) a_minSeen = rd.currentFade;
				rd.currentFade = 1.0f;
				++n;
			}
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) n += KeepOpaqueNodes(child.get(), a_minSeen, a_depth + 1);
			}
			return n;
		}
		void KeepOpaque(RE::NiAVObject* a_root, const char* a_tag)
		{
			if (!a_root) return;
			float minSeen = 1.0f;
			const int n = KeepOpaqueNodes(a_root, minSeen, 0);
			static int logs = 0;
			if (n > 0 && minSeen < 0.99f && logs < 60) {
				++logs;
				SKSE::log::info("Fade: {} '{}' was {:.2f}, held at 1 ({} fade node(s))", a_tag ? a_tag : "ref", a_root->name.c_str() ? a_root->name.c_str() : "", minSeen, n);
			}
		}

		// 56.27: SetCollision(false) only sets the ref's 'collisions disabled' flag; a Havok body already in the world stays
		// until the 3D reloads (it pushed the player's and crew's feet). This takes it out at once: layer NonCollidable, then
		// DetachHavok. The collision objects themselves are left in place (stripping them can strip the cached model too).
		void KillHavok(RE::TESObjectREFR* a_ref, const char* a_tag)
		{
			auto* root = a_ref ? a_ref->Get3D() : nullptr;
			if (!root) return;
			root->SetCollisionLayer(RE::COL_LAYER::kNonCollidable);
			const bool detached = a_ref->DetachHavok(root);
			static int logs = 0;
			if (logs++ < 30) SKSE::log::info("Havok off: {} {:08X} (layer NonCollidable, DetachHavok {})", a_tag ? a_tag : "ref", a_ref->GetFormID(), detached ? 1 : 0);
		}

		// A movable static (the oar) carries a Havok body that rewrites its node's position every frame (the log showed
		// the node snapping to a fixed spot right after we placed it). Dropping the collision objects leaves a plain
		// model that stays exactly where we put it.
		void StripPhysics(RE::NiAVObject* a_obj, int a_depth = 0)
		{
			if (!a_obj || a_depth > 8) return;
			if (a_obj->collisionObject) {
				static int logs = 0;
				if (logs++ < 8) SKSE::log::info("Props: removed the physics body from node '{}'", a_obj->name.c_str());
				a_obj->collisionObject.reset();
			}
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) {
					StripPhysics(child.get(), a_depth + 1);
				}
			}
		}

		// 43.3: move a ref (kept far below) under the boat instead of respawning it; its cell becomes the boat's.
		void MoveSlot(Slot& a_slot, const RE::NiPoint3& a_to, const char* a_tag, const char* a_why)
		{
			auto ref = a_slot.handle.get();
			if (!ref || ref->IsDeleted()) return;
			ref->SetPosition(a_to.x, a_to.y, a_to.z - kParkDepth);
			a_slot.spawnPos = a_to;
			a_slot.collisionOff = false;
			SKSE::log::info("Respawn: moved {} {:08X} to the boat instead ({})", a_tag, ref->GetFormID(), a_why);
		}
		void HideRef(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) return;
			if (auto* root = a_ref->Get3D()) {
				root->SetAppCulled(true);
				root->local.translate = a_ref->GetPosition();  // back down to where the ref really is (far below)
				RE::NiUpdateData data{};
				root->Update(data);
			}
		}

		bool RefUsable(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref || a_ref->IsDeleted()) return false;
			auto* cell = a_ref->GetParentCell();
			return cell && cell->IsAttached();
		}

		// Spawning into a cell that is not attached (just after a teleport or a cell change) crashed the game. Quietly wait.
		bool PlayerReadyForSpawn()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			return cell && cell->IsAttached() && player->Get3D();
		}

		bool SpawnProp(Prop& a_prop, Slot& a_slot)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !a_prop.base || !PlayerReadyForSpawn()) return false;
			SKSE::log::info("Spawn: {}, placing", a_prop.tag);  // crash breadcrumb (v1.0.13)
			auto ref = player->PlaceObjectAtMe(a_prop.base, false);
			if (!ref) {
				if (a_prop.failLogs++ < 3) SKSE::log::error("{}: PlaceObjectAtMe failed", a_prop.tag);
				return false;
			}
			const RE::NiPoint3 pos = ref->GetPosition();
			a_slot.spawnPos = pos;
			ref->SetPosition(pos.x, pos.y, pos.z - kParkDepth);
			ref->SetCollision(false);
			a_slot.handle = ref->GetHandle();
			a_slot.collisionOff = false;
			a_slot.loadedTime = 0.0f;
			a_prop.freezeTries = 0;
			a_prop.placeLogs = 0;
			TrackSpawn(ref.get(), a_prop.tag);  // 31.1
			SKSE::log::info("{}: spawned ref {:08X}", a_prop.tag, ref->GetFormID());
			return true;
		}

		// ---- v1.0.15 (B3): the chest's contents survive every chest ref change ---------------------------------------------
		// The chest prop is a container ref that is replaced on the move (the refresh), removed at every exit and spawned again
		// for the parked boat, and left behind in a save. Its items now always move to the next chest: at a refresh swap
		// straight across; otherwise the old chest is kept (not deleted) while it holds items, and the next chest that is
		// spawned takes everything from every one of our old chests nearby (this also picks up a chest left in a save).
		int CountItems(RE::TESObjectREFR* a_ref)
		{
			int n = 0;
			if (!a_ref) return 0;
			for (auto& [obj, data] : a_ref->GetInventory()) if (obj && data.first > 0) n += data.first;
			return n;
		}

		int MoveAllItems(RE::TESObjectREFR* a_from, RE::TESObjectREFR* a_to)
		{
			if (!a_from || !a_to || a_from == a_to) return 0;
			int moved = 0;
			for (auto& [obj, data] : a_from->GetInventory()) {
				if (!obj || data.first <= 0) continue;
				int left = data.first;
				// Items with their own data (enchanted, tempered, named) move with it, one extra list at a time.
				if (auto* entry = data.second.get(); entry && entry->extraLists) {
					std::vector<RE::ExtraDataList*> lists;
					for (auto* xl : *entry->extraLists) if (xl) lists.push_back(xl);
					for (auto* xl : lists) {
						const int c = (std::max)(1, xl->GetCount());
						a_from->RemoveItem(obj, c, RE::ITEM_REMOVE_REASON::kStoreInContainer, xl, a_to);
						left -= c;
						moved += c;
					}
				}
				if (left > 0) {
					a_from->RemoveItem(obj, left, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, a_to);
					moved += left;
				}
			}
			return moved;
		}

		// Our chest refs that are not the current one, around the player (the old chest after an exit, or one left in a save).
		void AdoptOldChests(RE::TESObjectREFR* a_new)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* tes = RE::TES::GetSingleton();
			if (!player || !tes || !a_new || !g_chestProp.base) return;
			std::vector<RE::TESObjectREFR*> old;
			tes->ForEachReferenceInRange(player, 9000.0f, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && a_ref != a_new && (a_ref->GetFormID() >> 24) == 0xFF && a_ref->GetBaseObject() == g_chestProp.base && !a_ref->IsDeleted()) old.push_back(a_ref);
				return RE::BSContainer::ForEachResult::kContinue;
			});
			for (auto* ref : old) {
				const int n = MoveAllItems(ref, a_new);
				if (n > 0) SKSE::log::info("Chest: {} item(s) moved from the old chest {:08X} into {:08X}", n, ref->GetFormID(), a_new->GetFormID());
				SKSE::log::info("Delete: old chest ref {:08X}", ref->GetFormID());  // crash breadcrumb
				ref->Disable();
				ref->SetDelete(true);
			}
		}

		void DespawnChestSlot(Slot& a_slot)
		{
			auto ref = a_slot.handle.get();
			if (ref && CountItems(ref.get()) > 0) {
				// 43.4.1: another of our chests alive (the other slot)? Move the items there and delete this one now.
				Slot& other = &a_slot == &g_chestProp.cur ? g_chestProp.next : g_chestProp.cur;
				if (auto o = other.handle.get(); o && o.get() != ref.get() && !o->IsDeleted()) {
					const int n = MoveAllItems(ref.get(), o.get());
					SKSE::log::info("Chest: {} item(s) moved from {:08X} into {:08X}, old chest deleted", n, ref->GetFormID(), o->GetFormID());
					Despawn(a_slot);
					return;
				}
				// No other chest yet: keep it far below and HIDDEN (its 3D used to stay on the boat's last spot, the
				// duplicate chest), until the next chest that spawns takes its items (AdoptOldChests).
				HideRef(ref.get());
				SKSE::log::info("Chest: {:08X} holds {} item(s), hidden until the next chest takes them", ref->GetFormID(), CountItems(ref.get()));
				a_slot.handle.reset();
				return;
			}
			Despawn(a_slot);
		}

		void DespawnProp(Prop& a_prop)
		{
			if (&a_prop == &g_chestProp) {
				DespawnChestSlot(a_prop.cur);
				DespawnChestSlot(a_prop.next);
				return;
			}
			Despawn(a_prop.cur);
			Despawn(a_prop.next);
		}

		void ForgetProp(Prop& a_prop)
		{
			a_prop.cur = Slot{};
			a_prop.next = Slot{};
		}

		void ApplyPlankNode(RE::NiAVObject* a_node, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot, float a_scale)
		{
			RE::NiMatrix3 parentRotInv = MakeMat(1, 0, 0, 0, 1, 0, 0, 0, 1);
			RE::NiPoint3 parentPos{};
			float parentScale = 1.0f;
			if (auto* parent = a_node->parent) {
				parentRotInv = parent->world.rotate.Transpose();
				parentPos = parent->world.translate;
				parentScale = parent->world.scale > 0.0f ? parent->world.scale : 1.0f;
			}
			a_node->local.rotate = parentRotInv * a_rot;
			a_node->local.translate = (parentRotInv * (a_pos - parentPos)) / parentScale;
			a_node->local.scale = a_scale / parentScale;
			a_node->SetAppCulled(false);
			RE::NiUpdateData data{};
			a_node->Update(data);
		}

		// 48.1: move a ref into the player's cell (same ref, no delete/spawn, so no fade). False if it did not land there.
		int g_replacedCount = 0, g_hopCount = 0;
		bool HopSlot(Slot& a_slot, const RE::NiPoint3& a_to)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto ref = a_slot.handle.get();
			if (!player || !ref || ref->IsDeleted()) return false;
			ref->MoveTo(player);
			ref->SetPosition(a_to.x, a_to.y, a_to.z - kParkDepth);
			a_slot.spawnPos = a_to;
			a_slot.collisionOff = false;
			auto* c = ref->GetParentCell();
			return c && c == player->GetParentCell() && c->IsAttached();
		}

		// 45.1: a prop whose ref cannot follow (its cell detached): a fresh ref at the player now; the chest's items go first.
		void RespawnPropNow(Prop& a_prop, const char* a_why)
		{
			if (!a_prop.base) return;
			if (&a_prop == &g_chestProp) {
				Slot fresh{};
				if (!SpawnProp(a_prop, fresh)) return;
				for (Slot* sl : { &a_prop.cur, &a_prop.next }) {
					if (auto o = sl->handle.get(); o && fresh.handle.get()) MoveAllItems(o.get(), fresh.handle.get().get());
				}
				Despawn(a_prop.cur);
				Despawn(a_prop.next);
				a_prop.cur = fresh;
				a_prop.next = Slot{};
			} else {
				Despawn(a_prop.cur);
				Despawn(a_prop.next);
				a_prop.cur = Slot{};
				a_prop.next = Slot{};
				a_prop.retry = 0.0f;  // TendProp spawns it again at once
			}
			a_prop.strippedRoot = a_prop.strippedNext = nullptr;
			if (g_respawnTag == a_prop.tag) g_respawnTag = nullptr;
			SKSE::log::info("Respawn: {} re-placed at the player ({})", a_prop.tag, a_why);
		}

		// Keeps a prop's reference alive and loaded. Mirrors the boat's own reference-swapping so it survives long
		// runs. Returns false until the prop's model is loaded; then a_root (and a_nextRoot while a replacement is
		// warming up underneath) are the 3D roots to place.
		bool TendProp(Prop& a_prop, float a_dt, const RE::NiPoint3& a_vehicle, RE::NiAVObject*& a_root, RE::NiAVObject*& a_nextRoot)
		{
			a_root = nullptr;
			a_nextRoot = nullptr;
			a_prop.retry = (std::max)(0.0f, a_prop.retry - a_dt);
			a_prop.collTimer -= a_dt;
			if (!a_prop.base) {
				if (a_prop.retry > 0.0f) return false;
				a_prop.retry = 2.0f;
				a_prop.base = FindPlankBase(a_prop.text, true);
				if (a_prop.base && a_prop.needsFreeze && !a_prop.keepForm) {
					// A movable static has Havok physics, which fights us placing it. If a plain static uses the same
					// model, use that instead: no physics at all.
					auto* model = skyrim_cast<RE::TESModel*>(a_prop.base);
					const char* mp = model ? model->GetModel() : nullptr;
					if (mp && *mp) {
						std::string want = mp;
						for (auto& ch : want) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
						RE::TESObjectSTAT* twin = nullptr;
						if (auto* dh = RE::TESDataHandler::GetSingleton()) {
							for (auto* stat : dh->GetFormArray<RE::TESObjectSTAT>()) {
								if (!stat) continue;
								const char* sp = stat->GetModel();
								if (!sp || !*sp) continue;
								std::string have = sp;
								for (auto& ch : have) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
								if (have == want) { twin = stat; break; }
							}
						}
						if (twin) {
							SKSE::log::info("{}: using plain static {:08X} with the same model '{}' (no physics)", a_prop.tag, twin->GetFormID(), mp);
							a_prop.base = twin;
							a_prop.needsFreeze = false;
						} else {
							SKSE::log::info("{}: no plain static shares model '{}', pinning the movable static instead", a_prop.tag, mp);
						}
					}
				}
				if (!a_prop.base && a_prop.isPlank) {
					for (const char* fallback : { "plank", "crate" }) {
						a_prop.text = fallback;
						a_prop.base = FindPlankBase(a_prop.text, true);
						if (a_prop.base) break;
					}
				}
				if (!a_prop.base) return false;
			}
			if (!a_prop.cur.handle) {
				if (a_prop.retry <= 0.0f) {
					a_prop.retry = 1.0f;
					if (SpawnProp(a_prop, a_prop.cur) && &a_prop == &g_chestProp) AdoptOldChests(a_prop.cur.handle.get().get());
				}
				return false;
			}
			auto ref = a_prop.cur.handle.get();
			if (!ref) { a_prop.cur.handle.reset(); return false; }
			if (!RefUsable(ref.get())) {
				// 43.3: its cell fell out of the loaded area (a long flight with the respawn held): move it to the boat.
				// 45.1: SetPosition does nothing on a ref in a detached cell: a fresh one in the player's cell instead.
				// 48.1: hop the same ref into the player's cell; only if that fails, a fresh one.
				if (!ref->IsDeleted() && a_prop.retry <= 0.0f) {
					a_prop.retry = 0.5f;
					if (HopSlot(a_prop.cur, a_vehicle)) SKSE::log::info("Respawn: cell hop ({} moved, no re-place)", a_prop.tag);
					else RespawnPropNow(a_prop, "cell detached, hop failed");
				}
				return false;
			}
			if (g_flightHold && !a_prop.next.handle) {
				const float fx = a_vehicle.x - a_prop.cur.spawnPos.x, fy = a_vehicle.y - a_prop.cur.spawnPos.y;
				const float lim = kRefreshDistance * 0.5f * kRefreshForceFactor;
				if (fx * fx + fy * fy > lim * lim) MoveSlot(a_prop.cur, a_vehicle, a_prop.tag, "flight");
			}

			RE::NiAVObject* nextRoot = nullptr;
			if (!a_prop.next.handle) {
				const float dx = a_vehicle.x - a_prop.cur.spawnPos.x;
				const float dy = a_vehicle.y - a_prop.cur.spawnPos.y;
				if (a_prop.retry <= 0.0f && RefreshMayBegin(dx * dx + dy * dy, kRefreshDistance * kRefreshDistance * 0.5f, a_prop.tag, CellsFromPlayer(ref.get()))) {
					a_prop.retry = 1.0f;
					SpawnProp(a_prop, a_prop.next);
				}
			} else if (auto nextRef = a_prop.next.handle.get()) {
				nextRoot = nextRef->Get3D();
				const bool forceEnd = g_respawnTag == a_prop.tag && g_respawnAge > kRespawnMaxAge;
				if (!nextRoot && forceEnd) {
					SKSE::log::info("Respawn: forced end {} (the fresh ref never loaded, dropped)", a_prop.tag);
					Despawn(a_prop.next);
					a_prop.next = Slot{};
					g_respawnTag = nullptr;
					NoteRespawnEnd(a_prop.tag);
				}
				if (nextRoot) {
					if (!a_prop.next.collisionOff) { nextRef->SetCollision(false); KillHavok(nextRef.get(), a_prop.tag); a_prop.next.collisionOff = true; }
					a_prop.next.loadedTime += a_dt;
					if (RefreshMaySwap(a_prop.next.loadedTime) || forceEnd) {
						if (forceEnd) SKSE::log::info("Respawn: forced end {}", a_prop.tag);
						if (&a_prop == &g_chestProp) {
							const int n = MoveAllItems(ref.get(), nextRef.get());
							if (n > 0) SKSE::log::info("Chest: {} item(s) moved to the fresh chest {:08X}", n, nextRef->GetFormID());
						}
						Despawn(a_prop.cur);
						a_prop.cur = a_prop.next;
						a_prop.next = Slot{};
						g_refreshQuiet = kRefreshQuiet;
						SKSE::log::info("Respawn: end {}", a_prop.tag);
						g_respawnTag = nullptr;
						NoteRespawnEnd(a_prop.tag);
						nextRoot = nullptr;
						ref = a_prop.cur.handle.get();
						if (!ref) return false;
					}
				}
			} else {
				a_prop.next.handle.reset();
			}

			RE::NiAVObject* root = ref->Get3D();
			if (!root) return false;  // the engine loads the model a few frames after spawning
			if (!a_prop.cur.collisionOff) { ref->SetCollision(false); KillHavok(ref.get(), a_prop.tag); a_prop.cur.collisionOff = true; }
			KeepOpaque(root, a_prop.tag);  // 56.28
			if (nextRoot) KeepOpaque(nextRoot, a_prop.tag);
			if (a_prop.needsFreeze) {
				StripPhysics(root);
				if (nextRoot) StripPhysics(nextRoot);
			}
			a_root = root;
			a_nextRoot = nextRoot;
			return true;
		}

		// Where a prop tuned as a_tune sits in the world, for a boat whose rigid frame is a_vehicle / a_boatRot.
		void PropPlacement(const PlankTune& a_tune, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_boatRot, RE::NiPoint3& a_pos, RE::NiMatrix3& a_rot)
		{
			const float cy = std::cos(a_tune.yaw), sy = std::sin(a_tune.yaw);
			a_rot = a_boatRot * MakeMat(cy, sy, 0.0f, -sy, cy, 0.0f, 0.0f, 0.0f, 1.0f);
			const RE::NiPoint3 off = a_boatRot * RE::NiPoint3{ a_tune.offset.x, a_tune.offset.y, a_tune.offset.z + kRollPivotDrop };
			a_pos = RE::NiPoint3{ a_vehicle.x + off.x, a_vehicle.y + off.y, a_vehicle.z - kRollPivotDrop + off.z };
		}

		// A bench (the helm bench at the player's stern seat, or the rower's bench).
		// 31.1.4: the benches are drawn props, but their model kept its own physics body (the bump an orphan bench gave).
		// Removed once per bench 3D, every node logged (the shared StripPhysics log stops after 8 lines).
		int StripPhysicsCounted(RE::NiAVObject* a_obj, const char* a_tag, bool a_log, int a_depth = 0)
		{
			if (!a_obj || a_depth > 8) return 0;
			int removed = 0;
			if (a_obj->collisionObject) {
				if (a_log) SKSE::log::info("Props: removed the physics body from node '{}' ({})", a_obj->name.c_str(), a_tag);
				a_obj->collisionObject.reset();
				++removed;
			}
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) removed += StripPhysicsCounted(child.get(), a_tag, a_log, a_depth + 1);
			}
			return removed;
		}

		void StripBenchPhysics(Prop& a_prop, RE::NiAVObject* a_root, RE::NiAVObject*& a_done)
		{
			if (!a_root || a_root == a_done) return;
			a_done = a_root;
			const bool log = a_prop.stripLogs < 6;
			const int removed = StripPhysicsCounted(a_root, a_prop.tag, log);
			if (log) {
				++a_prop.stripLogs;
				if (removed == 0) SKSE::log::info("Props: {} had no physics body", a_prop.tag);
			}
		}

		// 56.40: the helm bench wears the hull's premium wood (its own copy, boat_bench_d.dds), on this bench only
		// (a per-instance material copy, like the hull paint; the rower's bench and the game's benches keep theirs).
		constexpr const char* kBenchWood = "textures\\OarsAndSails\\boat_bench_d.dds";
		std::vector<const RE::NiAVObject*> g_benchWoodDone;
		void ApplyBenchWood(RE::NiAVObject* a_root)
		{
			if (!a_root || std::find(g_benchWoodDone.begin(), g_benchWoodDone.end(), a_root) != g_benchWoodDone.end()) return;
			g_benchWoodDone.push_back(a_root);
			if (g_benchWoodDone.size() > 16) g_benchWoodDone.erase(g_benchWoodDone.begin());
			std::error_code ec;
			if (!std::filesystem::exists(GameDataDir() / "textures" / "OarsAndSails" / "boat_bench_d.dds", ec)) {
				static bool warned = false;
				if (!warned) { warned = true; SKSE::log::info("Bench: {} missing, the helm bench keeps its own texture", kBenchWood); }
				return;
			}
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geo) {
				auto* prop = a_geo->lightingShaderProp_cast();
				auto* mat = prop ? static_cast<RE::BSLightingShaderMaterialBase*>(prop->material) : nullptr;
				auto* set = mat ? mat->textureSet.get() : nullptr;
				if (!set) return RE::BSVisit::BSVisitControl::kContinue;
				const char* d = set->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
				if (d && IEquals(d, kBenchWood)) return RE::BSVisit::BSVisitControl::kContinue;
				auto* newSet = RE::BSShaderTextureSet::Create();
				for (std::uint32_t sl = 0; sl < RE::BSTextureSet::Texture::kTotal; ++sl) {
					const auto slot = static_cast<RE::BSTextureSet::Texture>(sl);
					const char* path = set->GetTexturePath(slot);
					newSet->SetTexturePath(slot, path ? path : "");
				}
				newSet->SetTexturePath(RE::BSTextureSet::Texture::kDiffuse, kBenchWood);
				auto* newMat = static_cast<RE::BSLightingShaderMaterialBase*>(mat->Create());
				newMat->CopyMembers(mat);
				newMat->ClearTextures();
				newMat->OnLoadTextureSet(0, newSet);
				prop->SetMaterial(newMat, true);
				prop->SetupGeometry(a_geo);
				prop->FinishSetupGeometry(a_geo);
				newMat->~BSLightingShaderMaterialBase();
				RE::free(newMat);
				SKSE::log::info("Bench: helm bench shape '{}' ({} -> {})", a_geo->name.c_str() ? a_geo->name.c_str() : "", d ? d : "", kBenchWood);
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		void TendBench(Prop& a_prop, const PlankTune& a_tune, float a_dt, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_boatRot)
		{
			RE::NiAVObject* root = nullptr;
			RE::NiAVObject* nextRoot = nullptr;
			if (!TendProp(a_prop, a_dt, a_vehicle, root, nextRoot)) return;
			if (&a_prop == &g_plankProp) { ApplyBenchWood(root); if (nextRoot) ApplyBenchWood(nextRoot); }  // 56.40
			StripBenchPhysics(a_prop, root, a_prop.strippedRoot);  // 31.1.4
			if (nextRoot) StripBenchPhysics(a_prop, nextRoot, a_prop.strippedNext);
			RE::NiPoint3 pos;
			RE::NiMatrix3 rot;
			PropPlacement(a_tune, a_vehicle, a_boatRot, pos, rot);
			ApplyPlankNode(root, pos, rot, a_tune.scale);
			if (nextRoot) ApplyPlankNode(nextRoot, pos, rot, a_tune.scale);
		}

		void TendPlank(float a_dt, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_boatRot)
		{
			TendBench(g_plankProp, g_plank, a_dt, a_vehicle, a_boatRot);
			TendBench(g_rowBenchProp, g_rowBench, a_dt, a_vehicle, a_boatRot);
		}

		// The chest, in the middle of the hull.
		void TendChest(float a_dt, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_boatRot)
		{
			RE::NiAVObject* root = nullptr;
			RE::NiAVObject* nextRoot = nullptr;
			if (!TendProp(g_chestProp, a_dt, a_vehicle, root, nextRoot)) return;
			const float cy = std::cos(g_chest.yaw), sy = std::sin(g_chest.yaw);
			const RE::NiMatrix3 rot = a_boatRot * MakeMat(cy, sy, 0.0f, -sy, cy, 0.0f, 0.0f, 0.0f, 1.0f);
			const RE::NiPoint3 off = a_boatRot * RE::NiPoint3{ g_chest.offset.x, g_chest.offset.y, g_chest.offset.z + kRollPivotDrop };
			const RE::NiPoint3 pos{ a_vehicle.x + off.x, a_vehicle.y + off.y, a_vehicle.z - kRollPivotDrop + off.z };
			ApplyPlankNode(root, pos, rot, g_chest.scale);
			if (nextRoot) ApplyPlankNode(nextRoot, pos, rot, g_chest.scale);
		}

		RE::NiPointLight* FindPointLight(RE::NiAVObject* a_obj, int a_depth = 0)
		{
			if (!a_obj || a_depth > 10) return nullptr;
			if (auto* light = netimmerse_cast<RE::NiPointLight*>(a_obj)) return light;
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) {
					if (auto* found = FindPointLight(child.get(), a_depth + 1)) return found;
				}
			}
			return nullptr;
		}

		// A small warm light with no model (so no torch mesh shows), not off by default, no shadows: scored from the game's
		// light records. Radius about 400 and a flicker are preferred.
		struct LightPick { int score; RE::TESObjectLIGH* light; };
		std::vector<LightPick> RankLampLights()
		{
			std::vector<LightPick> out;
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) return out;
			for (auto* light : dh->GetFormArray<RE::TESObjectLIGH>()) {
				if (!light) continue;
				const char* mp = light->GetModel();
				if (mp && *mp) continue;
				const auto& d = light->data;
				if (d.flags.any(RE::TES_LIGHT_FLAGS::kOffByDefault, RE::TES_LIGHT_FLAGS::kNegative, RE::TES_LIGHT_FLAGS::kSpotlight,
						RE::TES_LIGHT_FLAGS::kSpotShadow, RE::TES_LIGHT_FLAGS::kHemiShadow, RE::TES_LIGHT_FLAGS::kOmniShadow)) continue;
				if (d.radius < 200 || d.radius > 800) continue;
				if (!(d.color.red >= d.color.green && d.color.green >= d.color.blue && d.color.red >= 180)) continue;  // warm
				int score = 100 - static_cast<int>(std::abs(static_cast<int>(d.radius) - 400) / 10);
				if (d.flags.any(RE::TES_LIGHT_FLAGS::kFlicker, RE::TES_LIGHT_FLAGS::kFlickerSlow)) score += 15;
				if (d.flags.any(RE::TES_LIGHT_FLAGS::kPulse, RE::TES_LIGHT_FLAGS::kPulseSlow)) score -= 10;
				if (light->fade < 0.5f || light->fade > 2.5f) score -= 20;
				out.push_back({ score, light });
			}
			std::sort(out.begin(), out.end(), [](const LightPick& a, const LightPick& b) { return a.score > b.score; });
			return out;
		}

		bool FromBaseGame(const RE::TESForm* a_form)
		{
			const auto* file = a_form ? a_form->GetFile(0) : nullptr;
			if (!file) return false;
			std::string name{ file->GetFilename() };
			for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			for (const char* master : { "skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm", "dragonborn.esm" }) {
				if (name == master) return true;
			}
			return false;
		}

		RE::TESBoundObject* LampLightBase()
		{
			if (g_lampLightBase || g_lampLightPicked) return g_lampLightBase;
			g_lampLightPicked = true;
			if (!g_lampLightText.empty()) {
				g_lampLightBase = RE::TESForm::LookupByID<RE::TESBoundObject>(static_cast<RE::FormID>(std::stoul(g_lampLightText, nullptr, 16)));
			} else {
				if (auto* def = RE::TESForm::LookupByID<RE::TESBoundObject>(static_cast<RE::FormID>(std::stoul(kLampLightDefault, nullptr, 16)));
					def && def->Is(RE::FormType::Light) && FromBaseGame(def)) {
					g_lampLightBase = def;
				}
				const auto ranked = RankLampLights();
				int shown = 0;
				for (const auto& c : ranked) {
					if (!FromBaseGame(c.light)) continue;
					const auto* l = c.light;
					if (shown++ < 5) {
						SKSE::log::info("Lamps: base-game light candidate {:08X} radius {} color {},{},{} fade {:.2f} score {}", l->GetFormID(), l->data.radius,
							l->data.color.red, l->data.color.green, l->data.color.blue, l->fade, c.score);
					}
					if (!g_lampLightBase) g_lampLightBase = c.light;  // the default is missing: best base-game candidate
				}
			}
			if (g_lampLightBase && !FromBaseGame(g_lampLightBase)) {
				SKSE::log::warn("Lamps: light {:08X} is not from a base-game master (fine for testing, not for a release)", g_lampLightBase->GetFormID());
			}
			if (g_lampLightBase && g_lampLightBase->Is(RE::FormType::Light)) {
				SKSE::log::info("Lamps: using light {:08X} for both lamps", g_lampLightBase->GetFormID());
			} else {
				g_lampLightBase = nullptr;
				SKSE::log::warn("Lamps: no usable light form found, so no lamps are placed (a lamp without light is skipped)");
			}
			return g_lampLightBase;
		}

		// The candle lantern's flame is a particle system that simulates in world space, so it smeared behind a moving boat.
		// Switch off every particle system (and flame / fire / smoke / ember node) in the lantern; the real light stays.
		int CullFlames(RE::NiAVObject* a_obj, int a_depth = 0)
		{
			if (!a_obj || a_depth > 12) return 0;
			std::string name = a_obj->name.c_str() ? a_obj->name.c_str() : "";
			for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			const bool particles = netimmerse_cast<RE::NiParticleSystem*>(a_obj) != nullptr;
			if (particles || name.find("flame") != std::string::npos || name.find("fire") != std::string::npos ||
				name.find("smoke") != std::string::npos || name.find("ember") != std::string::npos) {
				a_obj->SetAppCulled(true);
				SKSE::log::info("Lamps: switched off '{}' in the lantern ({})", a_obj->name.c_str() ? a_obj->name.c_str() : "", particles ? "particle system" : "flame node");
				return 1;
			}
			int n = 0;
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) n += CullFlames(child.get(), a_depth + 1);
			}
			return n;
		}

		void LogNodeTree(RE::NiAVObject* a_obj, int a_depth = 0)
		{
			if (!a_obj || a_depth > 12) return;
			const auto* rtti = a_obj->GetRTTI();
			SKSE::log::info("Lamps: lantern node {}'{}' ({}){}", std::string(static_cast<std::size_t>(a_depth) * 2, ' '), a_obj->name.c_str() ? a_obj->name.c_str() : "",
				rtti && rtti->name ? rtti->name : "?", a_obj->GetAppCulled() ? " culled" : "");
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) LogNodeTree(child.get(), a_depth + 1);
			}
		}

		// Lamp and light references are plain spawned refs, so the save keeps them. After a load, remove the ones the
		// last session left behind (only refs created in game, never placed world objects that share the form).
		void CleanupStrayLamps()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* tes = RE::TES::GetSingleton();
			if (!player || !tes) return;
			std::vector<RE::FormID> bases;
			for (int i = 0; i < 2; ++i) {
				if (!g_lampProp[i].base && !g_lampProp[i].text.empty()) g_lampProp[i].base = FindPlankBase(g_lampProp[i].text, false);
				if (g_lampProp[i].base) bases.push_back(g_lampProp[i].base->GetFormID());
			}
			if (auto* light = LampLightBase()) bases.push_back(light->GetFormID());
			if (bases.empty()) return;
			int removed = 0;
			tes->ForEachReferenceInRange(player, 8000.0f, [&](RE::TESObjectREFR* a_ref) {
				if (!a_ref || (a_ref->GetFormID() >> 24) != 0xFF) return RE::BSContainer::ForEachResult::kContinue;
				auto* base = a_ref->GetBaseObject();
				if (!base || std::find(bases.begin(), bases.end(), base->GetFormID()) == bases.end()) return RE::BSContainer::ForEachResult::kContinue;
				for (int i = 0; i < 2; ++i) {
					if (g_lampProp[i].cur.handle.get().get() == a_ref || g_lampProp[i].next.handle.get().get() == a_ref || g_lampLight[i].handle.get().get() == a_ref) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
				}
				SKSE::log::info("Delete: stray lamp ref {:08X}", a_ref->GetFormID());  // crash breadcrumb (v1.0.13)
				a_ref->Disable();
				a_ref->SetDelete(true);
				++removed;
				return RE::BSContainer::ForEachResult::kContinue;
			});
			SKSE::log::info("Lamps: removed {} stray lamp / light refs left by the last session", removed);
		}

		void LogLampRefCount(const char* a_why)
		{
			int lanterns = 0, lights = 0;
			for (int i = 0; i < 2; ++i) {
				if (g_lampProp[i].cur.handle.get()) ++lanterns;
				if (g_lampProp[i].next.handle.get()) ++lanterns;
				if (g_lampLight[i].handle.get()) ++lights;
			}
			SKSE::log::info("Lamps: {}: {} lantern ref(s), {} light ref(s) live", a_why, lanterns, lights);
		}

		// The lamp's light: its own reference, one per lantern. Every frame it is moved to the lantern's centre with
		// SetPosition (so the reference changes cell with the boat) and its 3D (with the light node under it) is placed
		// and updated in the same frame, so the glow never stays behind.
		void TendLampLight(int a_side, float a_dt, const RE::NiPoint3& a_pos, float a_fade)
		{
			auto& slot = g_lampLight[a_side];
			auto& st = g_lampState[a_side];
			auto* base = LampLightBase();
			if (!base) return;
			const char* tag = a_side == 0 ? "LampStern" : "LampBow";
			auto ref = slot.handle.get();
			if (!ref) {
				slot.handle.reset();
				if ((st.lightRetry -= a_dt) > 0.0f) return;
				st.lightRetry = 1.0f;
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!PlayerReadyForSpawn()) return;
				SKSE::log::info("Spawn: {} light, placing", tag);  // crash breadcrumb (v1.0.13)
				auto placed = player ? player->PlaceObjectAtMe(base, false) : nullptr;
				if (!placed) return;
				slot.handle = placed->GetHandle();
				placed->SetCollision(false);
				TrackSpawn(placed.get(), std::format("{} light", tag));  // 31.1
				SKSE::log::info("{}: spawned light ref {:08X}", tag, placed->GetFormID());
				st.followTimer = 0.0f;
				st.followLogs = 0;
				st.lightLogged = false;
				ref = placed;
				LogLampRefCount("light spawned");
			}
			if (!RefUsable(ref.get())) {
				// 49: its cell unloaded (the light stayed there, dark): delete it, a fresh one is placed at the player next frame.
				SKSE::log::info("{}: light ref {:08X} in an unloaded cell, replaced", tag, ref->GetFormID());
				Despawn(slot);
				st.lightRetry = 0.0f;
				return;
			}
			if ((ref->GetPosition() - a_pos).Length() > 0.5f) ref->SetPosition(a_pos);
			auto* root = ref->Get3D();
			if (root) {
				const RE::NiMatrix3 ident = MakeMat(1, 0, 0, 0, 1, 0, 0, 0, 1);
				ApplyPlankNode(root, a_pos, ident, root->local.scale > 0.0f ? root->local.scale : 1.0f);  // also updates the world data
			}
			auto* light = FindPointLight(root);
			if (light != st.light) {
				st.light = light;
				if (light) st.baseFade = light->GetLightRuntimeData().fade;
			}
			if (light && light != root && (light->world.translate - a_pos).Length() > 1.0f) {
				// The light node is not where its root was put (an offset inside the light's own 3D): move it directly.
				RE::NiAVObject* parent = light->parent;
				const RE::NiPoint3 parentPos = parent ? parent->world.translate : RE::NiPoint3{};
				const RE::NiMatrix3 parentInv = parent ? parent->world.rotate.Transpose() : MakeMat(1, 0, 0, 0, 1, 0, 0, 0, 1);
				const float parentScale = parent && parent->world.scale > 0.0f ? parent->world.scale : 1.0f;
				light->local.translate = (parentInv * (a_pos - parentPos)) / parentScale;
				RE::NiUpdateData data{};
				light->Update(data);
			}
			if (light) light->GetLightRuntimeData().fade = st.baseFade * a_fade;
			// Follow check: once 1 s after the spawn and once 10 s later (by then the boat has usually moved).
			st.followTimer += a_dt;
			if (st.followLogs < 2 && st.followTimer >= (st.followLogs == 0 ? 1.0f : 11.0f)) {
				++st.followLogs;
				const RE::NiPoint3 lp = light ? light->world.translate : ref->GetPosition();
				const float d = (lp - a_pos).Length();
				if (d > 5.0f) SKSE::log::warn("Lamp light follow ({}): distance lantern<->light = {:.0f} units (light node {})", tag, d, light ? "found" : "MISSING");
				else SKSE::log::info("Lamp light follow ({}): distance lantern<->light = {:.0f} units", tag, d);
			}
		}

		// Two lamps (the stern one and the bow one) are the whole light budget: say once if the player adds a torch or a light spell.
		void CheckLightBudget(float a_dt)
		{
			if (g_lampBudgetWarned || (g_lampBudgetTimer -= a_dt) > 0.0f) return;
			g_lampBudgetTimer = 5.0f;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			bool extra = false;
			for (bool left : { true, false }) {
				if (auto* obj = player->GetEquippedObject(left); obj && obj->Is(RE::FormType::Light)) extra = true;
			}
			if (auto* target = player->AsMagicTarget()) {
				if (auto* effects = target->GetActiveEffectList()) {
					for (auto* effect : *effects) {
						auto* setting = effect ? effect->GetBaseObject() : nullptr;
						if (setting && setting->GetArchetype() == RE::EffectSetting::Archetype::kLight) extra = true;
					}
				}
			}
			if (extra) {
				g_lampBudgetWarned = true;
				SKSE::log::warn("Lamps: the player also carries a torch or a light spell; with the two boat lamps that is three dynamic lights");
			}
		}

		// A hanging lamp. The hook rides the boat (pitch, roll, yaw); the lamp itself hangs straight down and swings as a
		// pendulum (period about 1.6 s) driven by how the hook accelerates, so speeding up, slowing down, turning and the
		// idle rocking all move it. The bow lamp swings a little less.
		void TendLamp(int a_side, float a_dt, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_boatRot)
		{
			auto& t = g_lamp[a_side];
			auto& prop = g_lampProp[a_side];
			auto& st = g_lampState[a_side];
			if (!t.on || prop.text.empty()) {
				if (prop.cur.handle || prop.next.handle) DespawnProp(prop);
				Despawn(g_lampLight[a_side]);
				st = LampState{};
				return;
			}
			if (!LampLightBase()) {  // a lamp without light is not placed
				if (prop.cur.handle || prop.next.handle) DespawnProp(prop);
				return;
			}
			if (g_lampCleanup && prop.base && g_lampLightBase) {
				g_lampCleanup = false;
				CleanupStrayLamps();
			}
			const bool hadLantern = static_cast<bool>(prop.cur.handle);
			RE::NiAVObject* root = nullptr;
			RE::NiAVObject* nextRoot = nullptr;
			const bool ready = TendProp(prop, a_dt, a_vehicle, root, nextRoot);
			if (!hadLantern && prop.cur.handle) LogLampRefCount("lantern spawned");
			if (!ready) return;
			CheckLightBudget(a_dt);
			if (root != st.flameRoot) {
				st.flameRoot = root;
				const int culled = CullFlames(root);
				SKSE::log::info("{}: {} flame part(s) switched off", prop.tag, culled);
				static bool treeLogged = false;
				if (culled == 0 && !treeLogged) { treeLogged = true; LogNodeTree(root); }  // so the right flame node can be found
			}
			if (nextRoot && nextRoot != st.flameNextRoot) { st.flameNextRoot = nextRoot; CullFlames(nextRoot); }

			const RE::NiPoint3 off = a_boatRot * RE::NiPoint3{ t.offset.x, t.offset.y, t.offset.z + kRollPivotDrop };
			const RE::NiPoint3 pivot{ a_vehicle.x + off.x, a_vehicle.y + off.y, a_vehicle.z - kRollPivotDrop + off.z };

			// Pendulum, driven by the hook's acceleration in the boat's frame.
			if (a_dt > 0.0f) {
				if (st.have) {
					RE::NiPoint3 move = pivot - st.prevPivot;
					RE::NiPoint3 vel = move / a_dt;
					RE::NiPoint3 acc{};
					if (move.Length() < 200.0f) acc = (vel - st.prevVel) / a_dt;
					else vel = RE::NiPoint3{};  // a jump (reference swap, boarding): no kick
					const float am = acc.Length();
					if (am > 2000.0f) acc = acc * (2000.0f / am);
					st.prevVel = vel;
					const RE::NiPoint3 fwdW = a_boatRot * RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
					const RE::NiPoint3 rightW = a_boatRot * RE::NiPoint3{ 1.0f, 0.0f, 0.0f };
					const float aF = acc.x * fwdW.x + acc.y * fwdW.y + acc.z * fwdW.z;
					const float aR = acc.x * rightW.x + acc.y * rightW.y + acc.z * rightW.z;
					const float w = 6.28318f / 1.6f;           // period 1.6 s
					const float len = 686.0f / (w * w);        // pendulum length for that period (gravity ~686 units/s^2)
					const float zeta = (std::max)(0.0f, t.damping);
					const float maxAngle = 0.2094f * (a_side == 1 ? 0.7f : 1.0f);  // 12 degrees, the bow lamp a little less
					const float drive = (a_side == 1 ? 0.7f : 1.0f) * t.swing / len;
					float left = (std::min)(a_dt, 0.1f);
					while (left > 0.0f) {
						const float d = (std::min)(0.01f, left);
						st.pitchVel += (-w * w * st.pitch - 2.0f * zeta * w * st.pitchVel - drive * aF) * d;
						st.rollVel += (-w * w * st.roll - 2.0f * zeta * w * st.rollVel - drive * aR) * d;
						st.pitch += st.pitchVel * d;
						st.roll += st.rollVel * d;
						left -= d;
					}
					if (std::abs(st.pitch) > maxAngle) { st.pitch = std::clamp(st.pitch, -maxAngle, maxAngle); st.pitchVel = 0.0f; }
					if (std::abs(st.roll) > maxAngle) { st.roll = std::clamp(st.roll, -maxAngle, maxAngle); st.rollVel = 0.0f; }
				}
				st.prevPivot = pivot;
				st.have = true;
			}

			// Hangs straight down (world vertical, boat heading), plus the swing and the tuned yaw.
			const RE::NiPoint3 f = a_boatRot * RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
			const float hd = std::atan2(f.x, f.y);
			const float sh = std::sin(hd), ch = std::cos(hd);
			const float cp = std::cos(st.pitch), sp = std::sin(st.pitch);
			const float cr = std::cos(-st.roll), sr = std::sin(-st.roll);
			const float cy = std::cos(t.yaw), sy = std::sin(t.yaw);
			const RE::NiMatrix3 rot = MakeMat(ch, sh, 0.0f, -sh, ch, 0.0f, 0.0f, 0.0f, 1.0f) *
				MakeMat(1.0f, 0.0f, 0.0f, 0.0f, cp, -sp, 0.0f, sp, cp) *
				MakeMat(cr, 0.0f, sr, 0.0f, 1.0f, 0.0f, -sr, 0.0f, cr) *
				MakeMat(cy, sy, 0.0f, -sy, cy, 0.0f, 0.0f, 0.0f, 1.0f);
			// The lantern hangs from the top of its model, and the light sits at its centre.
			float top = 0.0f, mid = 0.0f;
			if (prop.base) {
				const auto& bd = prop.base->boundData;
				top = static_cast<float>(bd.boundMax.z);
				mid = 0.5f * (static_cast<float>(bd.boundMax.z) + static_cast<float>(bd.boundMin.z));
			}
			const RE::NiPoint3 hang = rot * RE::NiPoint3{ 0.0f, 0.0f, top * t.scale };
			const RE::NiPoint3 meshPos = pivot - hang;
			ApplyPlankNode(root, meshPos, rot, t.scale);
			if (nextRoot) ApplyPlankNode(nextRoot, meshPos, rot, t.scale);
			const RE::NiPoint3 centre = meshPos + rot * RE::NiPoint3{ 0.0f, 0.0f, mid * t.scale };

			// Night only (optional): fade the light out by day over 2 s; the lantern itself stays.
			float want = 1.0f;
			if (g_lampAuto) {
				if (auto* cal = RE::Calendar::GetSingleton()) {
					const float hour = cal->GetHour();
					want = (hour >= 18.0f || hour < 6.0f) ? 1.0f : 0.0f;
				}
			}
			st.nightFade += std::clamp(want - st.nightFade, -0.5f * a_dt, 0.5f * a_dt);
			TendLampLight(a_side, a_dt, centre, (std::max)(0.0f, t.bright) * st.nightFade);
		}

		// Works out the oar model's long axis and centre from its bounds, once.
		void LearnOarBounds()
		{
			auto* base = g_oarProp[0].base ? g_oarProp[0].base : RE::TESForm::LookupByID<RE::TESBoundObject>(0x00050E77);  // 51: lake boats before any boarding
			if (g_oarBoundsKnown || !base) return;
			const auto& bd = base->boundData;
			const float minX = bd.boundMin.x, minY = bd.boundMin.y, minZ = bd.boundMin.z;
			const float maxX = bd.boundMax.x, maxY = bd.boundMax.y, maxZ = bd.boundMax.z;
			const float ex = maxX - minX, ey = maxY - minY, ez = maxZ - minZ;
			g_oarAxisX = ex > ey && ex >= ez;
			g_oarCentre = RE::NiPoint3{ (minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f };
			g_oarHalfLen = 0.5f * (g_oarAxisX ? ex : ey);  // 56.9
			g_oarBoundsKnown = true;
			SKSE::log::info("Oar model bounds: x {:.0f}..{:.0f}  y {:.0f}..{:.0f}  z {:.0f}..{:.0f}  long axis {}  centre ({:.0f},{:.0f},{:.0f})",
				minX, maxX, minY, maxY, minZ, maxZ, g_oarAxisX ? "X" : "Y", g_oarCentre.x, g_oarCentre.y, g_oarCentre.z);
		}

		RE::NiMatrix3 RzM(float a_h) { const float c = std::cos(a_h), s = std::sin(a_h); return MakeMat(c, s, 0.0f, -s, c, 0.0f, 0.0f, 0.0f, 1.0f); }
		RE::NiMatrix3 RxM(float a_p) { const float c = std::cos(a_p), s = std::sin(a_p); return MakeMat(1.0f, 0.0f, 0.0f, 0.0f, c, -s, 0.0f, s, c); }
		RE::NiMatrix3 RyM(float a_r) { const float c = std::cos(a_r), s = std::sin(a_r); return MakeMat(c, 0.0f, s, 0.0f, 1.0f, 0.0f, -s, 0.0f, c); }

		// Stroke shape read off the rowing animation's keys (90 frames at 30 per second; frame 1 is the catch, 46 the finish).
		// Reach: how far forward the hands are (1 at the catch, 0 at the finish). Water: how low the blade is (1 in the water,
		// 0 lifted clear), following the hand height: hands at chest level through the drive, dropped to the thighs for the recovery.
		float CurveAt(const float* a_xs, const float* a_ys, int a_n, float a_x)
		{
			if (a_x <= a_xs[0]) return a_ys[0];
			if (a_x >= a_xs[a_n - 1]) return a_ys[a_n - 1];
			int i = 0;
			while (i < a_n - 2 && a_x > a_xs[i + 1]) ++i;
			auto secant = [&](int k) { return (a_ys[k + 1] - a_ys[k]) / (a_xs[k + 1] - a_xs[k]); };
			auto slope = [&](int k) {
				if (k <= 0 || k >= a_n - 1) return 0.0f;
				const float d0 = secant(k - 1), d1 = secant(k);
				if (d0 * d1 <= 0.0f) return 0.0f;  // a turning point or a flat run stays flat (no overshoot)
				const float h0 = a_xs[k] - a_xs[k - 1], h1 = a_xs[k + 1] - a_xs[k];
				const float w1 = 2.0f * h1 + h0, w2 = h1 + 2.0f * h0;
				return (w1 + w2) / (w1 / d0 + w2 / d1);
			};
			const float h = a_xs[i + 1] - a_xs[i];
			const float t = (a_x - a_xs[i]) / h;
			const float t2 = t * t, t3 = t2 * t;
			return (2.0f * t3 - 3.0f * t2 + 1.0f) * a_ys[i] + (t3 - 2.0f * t2 + t) * h * slope(i) +
			       (-2.0f * t3 + 3.0f * t2) * a_ys[i + 1] + (t3 - t2) * h * slope(i + 1);
		}

		void StrokeShape(float a_phase, float& a_reach, float& a_water)
		{
			static constexpr float kFrames[] = { 1, 24, 46, 52, 58, 64, 70, 76, 82, 87, 91 };
			static constexpr float kReach[] = { 1.00f, 0.54f, 0.00f, 0.065f, 0.18f, 0.38f, 0.56f, 0.76f, 0.93f, 1.00f, 1.00f };
			static constexpr float kWFrames[] = { 1, 24, 42, 46, 49, 52, 55, 58, 62, 76, 80, 84, 87, 90, 91 };
			static constexpr float kWater[] = { 1.0f, 1.0f, 1.0f, 0.95f, 0.80f, 0.55f, 0.30f, 0.12f, 0.0f, 0.0f, 0.15f, 0.50f, 0.73f, 0.92f, 1.0f };
			float u = a_phase / 6.28318f;
			u -= std::floor(u);
			const float frame = 1.0f + 90.0f * u;
			a_reach = CurveAt(kFrames, kReach, 11, frame);
			a_water = std::clamp(CurveAt(kWFrames, kWater, 15, frame), 0.0f, 1.0f);
		}

		// The two oars, each turning about its oarlock. a_side 0 is the right one, 1 the left one.
		// One oar's world pose for a boat frame (a_vehicle / a_boatRot): a_outRaw 0 stowed .. 1 at the oarlock, the stroke at
		// a_phase with a_amp. Part 51: the lake boats use it too.
		struct OarPose
		{
			OarTune t;
			RE::NiMatrix3 rot;       // the mechanics (pivot about the oarlock)
			RE::NiPoint3 pos;
			RE::NiMatrix3 meshRot;   // what is drawn (the mesh may be flipped end for end)
			RE::NiPoint3 meshPos;
		};
		OarPose ComputeOarPose(int a_side, float a_outRaw, float a_phase, float a_amp, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_boatRot)
		{
			const float side = a_side == 0 ? 1.0f : -1.0f;
			// 50: between the stowed pose (0) and the oarlock (1), eased.
			const float out = a_outRaw * a_outRaw * (3.0f - 2.0f * a_outRaw);
			OarTune t = g_oar[a_side];
			{
				const OarTune& s = g_oarStow[a_side];
				auto mix = [out](float a_stow, float a_row) { return a_stow + (a_row - a_stow) * out; };
				t.offset = RE::NiPoint3{ mix(s.offset.x, t.offset.x), mix(s.offset.y, t.offset.y), mix(s.offset.z, t.offset.z) };
				t.yaw = mix(s.yaw, t.yaw);
				t.pitch = mix(s.pitch, t.pitch);
				t.roll = mix(s.roll, t.roll);
				t.slide = mix(s.slide, t.slide);
			}

			// A plain stroke: blade forward and in the water at the catch, swept aft through the drive, then lifted
			// clear and carried forward again.
			float swing = 0.0f, dip = 0.0f;
			if (a_amp > 0.0005f) {
				float reach = 0.0f, water = 0.0f;
				StrokeShape(a_phase, reach, water);  // follows the keys of the rowing animation
				swing = side * g_stroke.swing * a_amp * out * (1.0f - 2.0f * reach);
				dip = -(t.flipped ? -1.0f : 1.0f) * g_stroke.dip * a_amp * out * (2.0f * water - 1.0f);
			}

			// The model's long axis is turned onto Y first, so yaw 90 always points the shaft straight out.
			const RE::NiMatrix3 fix = g_oarAxisX ? RzM(-1.5708f) : MakeMat(1, 0, 0, 0, 1, 0, 0, 0, 1);
			const RE::NiMatrix3 rot = a_boatRot * RzM(t.yaw + (t.flipped ? 3.14159f : 0.0f) + swing) * RxM(t.pitch + dip) * RyM(t.roll) * fix;

			// The pivot sits at the middle of the model, slid along the shaft. The oarlock point is where it goes.
			const RE::NiPoint3 pivotModel{
				g_oarCentre.x + (g_oarAxisX ? t.slide : 0.0f),
				g_oarCentre.y + (g_oarAxisX ? 0.0f : t.slide),
				g_oarCentre.z };
			const RE::NiPoint3 lockOff = a_boatRot * RE::NiPoint3{ t.offset.x, t.offset.y, t.offset.z + kRollPivotDrop };
			const RE::NiPoint3 lock{ a_vehicle.x + lockOff.x, a_vehicle.y + lockOff.y, a_vehicle.z - kRollPivotDrop + lockOff.z };
			const RE::NiPoint3 pm = rot * RE::NiPoint3{ pivotModel.x * t.scale, pivotModel.y * t.scale, pivotModel.z * t.scale };
			const RE::NiPoint3 pos{ lock.x - pm.x, lock.y - pm.y, lock.z - pm.z };
			// Mesh only: turn the model 180 degrees about its own centre. The centre stays exactly where it was, so the
			// oar still spans the same space and still pivots about the same oarlock; only which end is which swaps.
			RE::NiMatrix3 meshRot = rot;
			RE::NiPoint3 meshPos = pos;
			if (g_oarMeshFlip) {
				meshRot = rot * RzM(3.14159265f);
				const RE::NiPoint3 c{ g_oarCentre.x * t.scale, g_oarCentre.y * t.scale, g_oarCentre.z * t.scale };
				const RE::NiPoint3 before = rot * c;
				const RE::NiPoint3 after = meshRot * c;
				meshPos = RE::NiPoint3{ pos.x + before.x - after.x, pos.y + before.y - after.y, pos.z + before.z - after.z };
			}
			return OarPose{ t, rot, pos, meshRot, meshPos };
		}

		void TendOar(int a_side, float a_dt, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_boatRot)
		{
			RE::NiAVObject* root = nullptr;
			RE::NiAVObject* nextRoot = nullptr;
			if (!TendProp(g_oarProp[a_side], a_dt, a_vehicle, root, nextRoot)) return;
			// 56.24: the baked pose drives the refs (written by TendBoatBakedOars): no stroke of ours, never hidden; a respawn's
			// fresh ref follows the current one so the swap lands in place.
			if (g_boatRefDriven) {
				if (root->GetAppCulled()) root->SetAppCulled(false);
				g_oarCulledByBake[a_side] = false;
				if (nextRoot) ApplyPlankNode(nextRoot, root->world.translate, root->world.rotate, root->world.scale);

				// Trigger ripple when the animated oar blade touches or leaves water
				if (g_rippleOn && g_rippleWaterAge < 0.3f && root) {
					const RE::NiPoint3 axis = g_oarAxisX ? RE::NiPoint3{ 1.0f, 0.0f, 0.0f } : RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
					const RE::NiPoint3 bladePos = root->world * (g_oarCentre - axis * g_oarHalfLen);
					const float waterSurface = g_rippleWaterZ + g_ripple.oarLift;
					const bool isSubmerged = (bladePos.z <= waterSurface);

					// Blade enters water ("catch") or exits water ("release")
					if (isSubmerged != g_oarWasSubmerged[a_side]) {
						RE::NiPoint3 ripplePos = bladePos;
						ripplePos.z = waterSurface;
						AddRippleAt(ripplePos, g_ripple.oar);
						g_oarWasSubmerged[a_side] = isSubmerged;
					}
					// 56.39: while the blade is in the water it leaves a glide trail like the stern does: a smaller ripple every
					// g_ripple.space units the blade moves through the water (or every 0.2 s if it barely moves). Stops when it lifts out.
					if (isSubmerged) {
						g_oarTrailTime[a_side] += a_dt;
						if (!g_oarTrailHave[a_side]) {
							g_oarTrailHave[a_side] = true;
							g_oarTrailLast[a_side] = bladePos;
							g_oarTrailTime[a_side] = 0.0f;
						} else {
							const float tdx = bladePos.x - g_oarTrailLast[a_side].x, tdy = bladePos.y - g_oarTrailLast[a_side].y;
							const float td2 = tdx * tdx + tdy * tdy;
							const float sp = (std::max)(8.0f, g_ripple.space);
							if (td2 > 600.0f * 600.0f) {  // cell swap / respawn: start over
								g_oarTrailLast[a_side] = bladePos;
								g_oarTrailTime[a_side] = 0.0f;
							} else if (td2 >= sp * sp || g_oarTrailTime[a_side] >= 0.2f) {
								RE::NiPoint3 tp = bladePos;
								tp.z = waterSurface;
								AddRippleAt(tp, g_ripple.oar * 0.7f);
								g_oarTrailLast[a_side] = bladePos;
								g_oarTrailTime[a_side] = 0.0f;
							}
						}
					} else {
						g_oarTrailHave[a_side] = false;
					}
				}

				return;
			}
			// 56: the rower's clip carries the oars: ours are hidden and get no stroke of their own.
			if (g_boatOarsBaked) {
				root->SetAppCulled(true);
				if (nextRoot) nextRoot->SetAppCulled(true);
				g_oarCulledByBake[a_side] = true;
				return;
			}
			if (g_oarCulledByBake[a_side]) {
				g_oarCulledByBake[a_side] = false;
				root->SetAppCulled(false);
				if (nextRoot) nextRoot->SetAppCulled(false);
			}
			LearnOarBounds();
			const OarPose op = ComputeOarPose(a_side, g_oarOut, g_oarPhase, g_oarAmp, a_vehicle, a_boatRot);
			const OarTune& t = op.t;
			const RE::NiMatrix3& rot = op.rot;
			const RE::NiPoint3& pos = op.pos;
			const RE::NiMatrix3& meshRot = op.meshRot;
			const RE::NiPoint3& meshPos = op.meshPos;
			ApplyPlankNode(root, meshPos, meshRot, t.scale);
			if (nextRoot) ApplyPlankNode(nextRoot, meshPos, meshRot, t.scale);
			if ((g_catchNow || g_releaseNow || g_dragNow) && g_rippleOn && g_rippleWaterAge < 0.3f) {
				// The blade is the end of the shaft that is farther from the boat's middle.
				const float half = g_ripple.oarReach * t.scale;
				RE::NiPoint3 best{};
				float bestD = -1.0f;
				for (float sgn : { -1.0f, 1.0f }) {
					const RE::NiPoint3 m{
						(g_oarCentre.x + (g_oarAxisX ? sgn * half / t.scale : 0.0f)) * t.scale,
						(g_oarCentre.y + (g_oarAxisX ? 0.0f : sgn * half / t.scale)) * t.scale,
						g_oarCentre.z * t.scale };
					const RE::NiPoint3 r = rot * m;
					const RE::NiPoint3 w{ pos.x + r.x, pos.y + r.y, pos.z + r.z };
					const float dx = w.x - a_vehicle.x, dy = w.y - a_vehicle.y;
					const float d = dx * dx + dy * dy;
					if (d > bestD) { bestD = d; best = w; }
				}
				// Shift across the shaft (horizontal, at right angles to it), then up or down.
				{
					const float sx = best.x - pos.x, sy = best.y - pos.y;
					const float sl = std::sqrt(sx * sx + sy * sy);
					if (sl > 1.0f) { best.x += -sy / sl * g_ripple.oarSide; best.y += sx / sl * g_ripple.oarSide; }
				}
				best.z = g_rippleWaterZ + g_ripple.oarLift;
				AddRippleAt(best, g_ripple.oar * (g_catchNow || g_releaseNow ? 1.0f : 0.6f) * (0.6f + 0.4f * g_oarAmp));
			}
			auto& prop = g_oarProp[a_side];
			if (prop.placeLogs < 4) {
				++prop.placeLogs;
				SKSE::log::info("{}: placed at ({:.0f},{:.0f},{:.0f}); node is now at ({:.0f},{:.0f},{:.0f}) culled={} hasCollisionObject={}",
					prop.tag, pos.x, pos.y, pos.z, root->world.translate.x, root->world.translate.y, root->world.translate.z,
					root->GetAppCulled(), static_cast<bool>(root->collisionObject));
			}
		}

		// Bench and oars, for a boat whose rigid frame is a_vehicle / a_boatRot.
		void FollowProps(float a_dt, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_boatRot)
		{
			// The stroke follows the boat: no speed, no rowing (the oars ease back to rest); faster boat, faster strokes.
			{
				float targetAmp = 0.0f;
				float strokesPerSecond = 0.0f;
				if (g_oarDemo) {
					targetAmp = 1.0f;
					strokesPerSecond = 0.4f * g_stroke.rate;
				} else if (g_rowExternal) {
					targetAmp = g_rowActive ? 1.0f : 0.0f;
					strokesPerSecond = g_rowRate * g_stroke.rate;  // base rate fixed by the plugin to the animation length; Num+ / Num- fine tune the multiplier
				} else {
					const float speed = g_oarSpeed;
					const float t = std::clamp((speed - 0.01f) / 0.11f, 0.0f, 1.0f);
					targetAmp = t * t * (3.0f - 2.0f * t);
					strokesPerSecond = (0.25f + 1.5f * speed) * g_stroke.rate;
				}
				g_oarAmp += (targetAmp - g_oarAmp) * (1.0f - std::exp(-4.0f * a_dt));
				g_prevPhase = g_oarPhase;
				if (g_oarAmp > 0.0005f) {
					g_oarPhase += a_dt * 6.28318f * strokesPerSecond;
					if (g_oarPhase > 6.28318f) g_oarPhase -= 6.28318f;
				} else {
					g_oarPhase = 0.0f;  // at rest: the next stroke starts from the catch
					g_prevPhase = 0.0f;
				}
				// Blade ripple moments: the catch (blade goes in), the release (blade comes out), and a trailing swirl in between.
				g_rippleWaterAge += a_dt;
				constexpr float kCatch = 5.75f, kRelease = 3.45f;  // blade goes in about frame 84 and comes out about frame 50 of the rowing animation
				g_catchNow = g_oarAmp > 0.2f && g_prevPhase < kCatch && g_oarPhase >= kCatch;
				g_releaseNow = g_oarAmp > 0.2f && g_prevPhase < kRelease && g_oarPhase >= kRelease;
				g_dragTimer -= a_dt;
				g_dragNow = false;
				if (g_oarAmp > 0.2f && std::sin(g_oarPhase) > 0.45f && g_dragTimer <= 0.0f) {
					g_dragNow = true;
					g_dragTimer = 0.28f;
				}
			}
			// 50: out while someone is aboard, in a few seconds after getting off (and at once while the stowed page is tuned).
			g_oarStowPreview = (std::max)(0.0f, g_oarStowPreview - a_dt);
			if (g_active && g_oarStowPreview <= 0.0f) {
				g_oarStowDelay = kOarStowAfter;
				g_oarOut = (std::min)(1.0f, g_oarOut + a_dt / kOarOutTime);
			} else if (g_oarOut > 0.0f && ((g_oarStowDelay -= a_dt) <= 0.0f || g_oarStowPreview > 0.0f)) {
				g_oarOut = (std::max)(0.0f, g_oarOut - a_dt / kOarInTime);
				if (g_oarOut == 0.0f) SKSE::log::info("Oars: stowed");
			}
			g_frameVehicle = a_vehicle;
			g_frameRot = a_boatRot;
			TendPlank(a_dt, a_vehicle, a_boatRot);
			TendChest(a_dt, a_vehicle, a_boatRot);
			TendOar(0, a_dt, a_vehicle, a_boatRot);
			TendOar(1, a_dt, a_vehicle, a_boatRot);
			TendOarGuard(a_dt, a_vehicle, a_boatRot);  // 56.23.2
			TendLamp(0, a_dt, a_vehicle, a_boatRot);
			TendLamp(1, a_dt, a_vehicle, a_boatRot);
		}

		void DespawnProps()
		{
			DespawnProp(g_plankProp);
			DespawnProp(g_rowBenchProp);
			DespawnProp(g_chestProp);
			DespawnProp(g_oarProp[0]);
			DespawnProp(g_oarProp[1]);
			DespawnProp(g_lampProp[0]);
			DespawnProp(g_lampProp[1]);
			Despawn(g_lampLight[0]);
			Despawn(g_lampLight[1]);
			g_lampState[0] = LampState{};
			g_lampState[1] = LampState{};
		}
	}

	ChestTune& Chest() { return g_chest; }
	void SetIdleBob(float a_amount) { g_idleBob = (std::max)(0.0f, a_amount); }
	float IdleBob() { return g_idleBob; }

	void LogChest()
	{
		SKSE::log::info("Chest tuning: right={:.0f} forward={:.0f} up={:.0f} yaw={:.0f}deg scale={:.2f}",
			g_chest.offset.x, g_chest.offset.y, g_chest.offset.z, g_chest.yaw * 57.2958f, g_chest.scale);
	}

	bool SetChestModel(const char* a_text)
	{
		const std::string text = a_text ? a_text : "";
		if (text.empty()) return false;
		// Hex digits are a form ID; anything else is searched for in the model paths of containers.
		std::string hex = text;
		if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) hex = hex.substr(2);
		const bool isHex = hex.size() >= 5 && hex.size() <= 8 &&
			std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
		g_chestProp.text = isHex ? text : "cont:" + text;
		g_chestProp.base = nullptr;
		g_chestProp.retry = 0.0f;
		DespawnProp(g_chestProp);  // respawned with the new form on the next frame
		return true;
	}

	PlankTune& Plank() { return g_plank; }
	PlankTune& RowBench() { return g_rowBench; }
	void LogRowBench()
	{
		SKSE::log::info("RowBench tuning: right={:.0f} forward={:.0f} up={:.0f} yaw={:.0f}deg scale={:.2f}",
			g_rowBench.offset.x, g_rowBench.offset.y, g_rowBench.offset.z, g_rowBench.yaw * 57.2958f, g_rowBench.scale);
	}

	void LogPlank()
	{
		SKSE::log::info("Plank tuning: right={:.0f} forward={:.0f} up={:.0f} yaw={:.0f}deg scale={:.2f}",
			g_plank.offset.x, g_plank.offset.y, g_plank.offset.z, g_plank.yaw * 57.2958f, g_plank.scale);
	}

	LampTune& Lamp(int a_side) { return g_lamp[a_side == 0 ? 0 : 1]; }

	void LogLamp(int a_side)
	{
		const int i = a_side == 0 ? 0 : 1;
		const auto& t = g_lamp[i];
		SKSE::log::info("Lamp {} tuning: right={:.0f} forward={:.0f} up={:.0f} yaw={:.1f}deg scale={:.2f} swing={:.2f} damping={:.2f} bright={:.2f}{} form='{}'",
			i == 0 ? "S" : "B", t.offset.x, t.offset.y, t.offset.z, t.yaw * 57.2958f, t.scale, t.swing, t.damping, t.bright, t.on ? "" : " OFF", g_lampProp[i].text);
	}

	bool SetLampForm(int a_side, const char* a_text)
	{
		const int i = a_side == 0 ? 0 : 1;
		std::string text = a_text ? a_text : "";
		auto& prop = g_lampProp[i];
		auto& t = g_lamp[i];
		if (text == "off") {
			t.on = false;
			DespawnProp(prop);
			g_lampState[i] = LampState{};
			return true;
		}
		if (text.empty()) return false;
		RE::TESBoundObject* base = FindPlankBase(text, true);  // a form ID, or the static with the shortest model path containing the text
		if (!base) {
			SKSE::log::warn("Lamp {}: nothing matches '{}', nothing changed", i == 0 ? "S" : "B", text);
			return false;
		}
		prop.text = std::format("{:08X}", base->GetFormID());
		prop.base = base;
		t.on = true;
		DespawnProp(prop);  // respawned with the new model on the next frame
		g_lampState[i] = LampState{};
		LogLamp(i);
		return true;
	}

	bool SetLampLight(const char* a_text)
	{
		std::string hex = a_text ? a_text : "";
		if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) hex = hex.substr(2);
		const bool isHex = hex.size() >= 5 && hex.size() <= 8 &&
			std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
		if (!isHex) return false;
		auto* form = RE::TESForm::LookupByID<RE::TESBoundObject>(static_cast<RE::FormID>(std::stoul(hex, nullptr, 16)));
		if (!form || !form->Is(RE::FormType::Light)) {
			SKSE::log::warn("Lamps: {} is not a light form, nothing changed", hex);
			return false;
		}
		g_lampLightText = hex;
		g_lampLightBase = form;
		g_lampLightPicked = true;
		for (int i = 0; i < 2; ++i) {
			Despawn(g_lampLight[i]);  // respawned with the new light on the next frame
			g_lampState[i].light = nullptr;
			g_lampState[i].lightLogged = false;
		}
		SKSE::log::info("Lamps: using light {:08X} for both lamps", form->GetFormID());
		return true;
	}

	void SetLampAuto(bool a_on) { g_lampAuto = a_on; }
	bool LampAuto() { return g_lampAuto; }

	void ListLamps(const char* a_filter)
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) return;
		std::vector<std::string> needles;
		if (a_filter && *a_filter) needles.emplace_back(a_filter);
		else needles = { "lantern", "lamp", "torch", "brazier" };
		for (auto& n : needles) for (auto& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		auto lower = [](const char* a_text) {
			std::string r = a_text ? a_text : "";
			for (auto& ch : r) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			return r;
		};
		auto matches = [&](const std::string& a_ed, const std::string& a_model) {
			for (auto& n : needles) if (a_ed.find(n) != std::string::npos || a_model.find(n) != std::string::npos) return true;
			return false;
		};
		struct Candidate { int score; RE::FormID id; std::string why; std::string model; };
		std::vector<Candidate> ranked;
		int count = 0;
		for (auto* light : dh->GetFormArray<RE::TESObjectLIGH>()) {
			if (!light) continue;
			const char* ed = light->GetFormEditorID();
			const std::string edL = lower(ed), model = lower(light->GetModel());
			if (!matches(edL, model)) continue;
			++count;
			const auto& d = light->data;
			const bool carry = d.flags.any(RE::TES_LIGHT_FLAGS::kCanCarry);
			const bool offDefault = d.flags.any(RE::TES_LIGHT_FLAGS::kOffByDefault);
			const bool flicker = d.flags.any(RE::TES_LIGHT_FLAGS::kFlicker, RE::TES_LIGHT_FLAGS::kFlickerSlow);
			const bool shadow = d.flags.any(RE::TES_LIGHT_FLAGS::kHemiShadow, RE::TES_LIGHT_FLAGS::kOmniShadow, RE::TES_LIGHT_FLAGS::kSpotShadow, RE::TES_LIGHT_FLAGS::kSpotlight);
			const bool torch = model.find("torch") != std::string::npos || edL.find("torch") != std::string::npos;
			SKSE::log::info("LampList: LIGH {:08X} '{}' name '{}' model '{}' radius {} color {},{},{} carry={} offByDefault={} flicker={} shadow={} torch={} fade {:.2f}",
				light->GetFormID(), ed ? ed : "", light->GetName(), light->GetModel() ? light->GetModel() : "", d.radius,
				d.color.red, d.color.green, d.color.blue, carry, offDefault, flicker, shadow, torch, light->fade);
			if (model.empty()) continue;
			int score = 0;
			std::string why;
			auto add = [&](int a_pts, const char* a_text) { score += a_pts; why += a_text; };
			if (model.find("lantern") != std::string::npos) add(4, "lantern model; ");
			else if (model.find("lamp") != std::string::npos) add(3, "lamp model; ");
			if (model.find("hang") != std::string::npos || model.find("chain") != std::string::npos) add(3, "hangs; ");
			if (carry) add(2, "carryable; ");
			if (d.radius >= 300 && d.radius <= 600) add(2, "radius 300-600; ");
			else if (d.radius > 150 && d.radius < 900) add(1, "radius near range; ");
			if (d.color.red >= d.color.green && d.color.green >= d.color.blue && d.color.red > 150) add(2, "warm; ");
			if (!shadow) add(1, "no shadow; "); else add(-3, "SHADOW; ");
			if (offDefault) add(-4, "OFF BY DEFAULT; ");
			if (torch) add(-2, "torch; ");
			ranked.push_back({ score, light->GetFormID(), why, light->GetModel() });
		}
		auto other = [&](auto a_tag, auto& a_array) {
			for (auto* form : a_array) {
				if (!form) continue;
				const char* ed = form->GetFormEditorID();
				const std::string edL = lower(ed), model = lower(form->GetModel());
				if (!matches(edL, model)) continue;
				++count;
				SKSE::log::info("LampList: {} {:08X} '{}' model '{}'", a_tag, form->GetFormID(), ed ? ed : "", form->GetModel() ? form->GetModel() : "");
			}
		};
		other("MISC", dh->GetFormArray<RE::TESObjectMISC>());
		other("STAT", dh->GetFormArray<RE::TESObjectSTAT>());
		other("ACTI", dh->GetFormArray<RE::TESObjectACTI>());
		std::sort(ranked.begin(), ranked.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
		SKSE::log::info("LampList: {} matching forms. Lights with a lantern-like model, best first:", count);
		for (std::size_t i = 0; i < ranked.size() && i < 10; ++i) {
			SKSE::log::info("LampList:  #{} {:08X} score {} model '{}' ({})", i + 1, ranked[i].id, ranked[i].score, ranked[i].model, ranked[i].why);
		}
		const auto lights = RankLampLights();
		SKSE::log::info("LampList: {} model-less warm lights a lamp can use, best first:", lights.size());
		for (std::size_t i = 0; i < lights.size() && i < 10; ++i) {
			const auto* l = lights[i].light;
			SKSE::log::info("LampList:  light #{} {:08X} radius {} color {},{},{} fade {:.2f} flicker={} score {}", i + 1, l->GetFormID(), l->data.radius,
				l->data.color.red, l->data.color.green, l->data.color.blue, l->fade,
				l->data.flags.any(RE::TES_LIGHT_FLAGS::kFlicker, RE::TES_LIGHT_FLAGS::kFlickerSlow), lights[i].score);
		}
	}

	OarTune& Oar(int a_side) { return g_oar[a_side == 0 ? 0 : 1]; }
	OarTune& OarStow(int a_side) { return g_oarStow[a_side == 0 ? 0 : 1]; }
	void SetOarStowPreview(bool a_on) { g_oarStowPreview = a_on ? 10.0f : 0.0f; }

	void LogOars()
	{
		SKSE::log::info("Oar stroke: sweep={:.1f}deg dip={:.1f}deg rate={:.3f} meshFlipped={}", g_stroke.swing * 57.2958f, g_stroke.dip * 57.2958f, g_stroke.rate, g_oarMeshFlip);
		for (int i = 0; i < 2; ++i) {
			const auto& t = g_oar[i];
			SKSE::log::info("Oar {} tuning: right={:.0f} forward={:.0f} up={:.0f} yaw={:.1f}deg pitch={:.1f}deg roll={:.1f}deg scale={:.2f} slide={:.0f}{}",
				i == 0 ? "R" : "L", t.offset.x, t.offset.y, t.offset.z, t.yaw * 57.2958f, t.pitch * 57.2958f, t.roll * 57.2958f, t.scale, t.slide, t.flipped ? " flipped" : "");
			const auto& s = g_oarStow[i];
			SKSE::log::info("Oar {} stowed: right={:.0f} forward={:.0f} up={:.0f} yaw={:.1f}deg pitch={:.1f}deg roll={:.1f}deg slide={:.0f}",
				i == 0 ? "R" : "L", s.offset.x, s.offset.y, s.offset.z, s.yaw * 57.2958f, s.pitch * 57.2958f, s.roll * 57.2958f, s.slide);
		}
	}

	void MirrorOars()
	{
		const OarTune& r = g_oar[0];
		OarTune& l = g_oar[1];
		l = r;
		l.offset.x = -r.offset.x;
		l.yaw = -r.yaw;
		l.roll = -r.roll;
	}

	void ToggleOarMeshFlip()
	{
		g_oarMeshFlip = !g_oarMeshFlip;
		SKSE::log::info("Oar mesh flipped now {}", g_oarMeshFlip ? "ON" : "OFF");
	}

	StrokeTune& Stroke() { return g_stroke; }

	void RestartStroke()
	{
		g_oarPhase = 0.0f;  // back to the catch, so the oars start together with the rowing animation
		g_prevPhase = 0.0f;
	}

	void SetStrokeDrive(bool a_external, bool a_active, float a_strokesPerSecond)
	{
		g_rowExternal = a_external;
		g_rowActive = a_active;
		g_rowRate = a_strokesPerSecond;
	}

	float StrokeThrust()
	{
		if (!g_rowExternal || !g_rowActive || g_oarDemo) return 0.0f;
		return 3.14159f * (std::max)(0.0f, std::sin(g_oarPhase)) * (std::min)(1.0f, g_oarAmp * 2.0f);
	}

	// 45.1: the whole set placed fresh in the player's cell right now (hull, then every prop, the chest's items first).
	void ReplaceAll(const char* a_why)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto oldRef = g_cur.handle.get();
		auto* hullCell = oldRef ? oldRef->GetParentCell() : nullptr;
		auto* playerCell = player ? player->GetParentCell() : nullptr;
		++g_replacedCount;
		SKSE::log::info("Respawn: cross-cell re-place ({}; hull cell {:08X} attached {}, player cell {:08X})", a_why, hullCell ? hullCell->GetFormID() : 0u,
			hullCell && hullCell->IsAttached(), playerCell ? playerCell->GetFormID() : 0u);
		Despawn(g_next);
		g_next = Slot{};
		Slot old = g_cur;
		g_cur = Slot{};
		g_spawnRetry = 0.0f;
		if (Spawn(g_cur)) Despawn(old);
		else g_cur = old;  // could not spawn now: keep the old one, the lost-boat path tries again
		for (Prop* pr : { &g_plankProp, &g_rowBenchProp, &g_chestProp, &g_oarProp[0], &g_oarProp[1], &g_lampProp[0], &g_lampProp[1] }) {
			if (pr->cur.handle || pr->next.handle) RespawnPropNow(*pr, a_why);
		}
		g_respawnTag = nullptr;
		g_goodValid = false;
		g_loggedLoaded = false;
	}

	// 48.1: the hull and every prop moved into the player's cell (same refs). Falls back to ReplaceAll if the hull will not move.
	void CellHop(float a_vx, float a_vy, float a_vz)
	{
		const RE::NiPoint3 to{ a_vx, a_vy, a_vz };
		if (!HopSlot(g_cur, to)) { ReplaceAll("cell hop failed for the hull"); return; }
		int moved = 1;
		Despawn(g_next);  // a warming spare would be in the old cell; the distance respawn makes a new one when needed
		g_next = Slot{};
		if (g_respawnTag && std::string_view(g_respawnTag) == "boat") g_respawnTag = nullptr;
		for (Prop* pr : { &g_plankProp, &g_rowBenchProp, &g_chestProp, &g_oarProp[0], &g_oarProp[1], &g_lampProp[0], &g_lampProp[1] }) {
			if (!pr->cur.handle) continue;
			if (HopSlot(pr->cur, to)) ++moved;
			else RespawnPropNow(*pr, "cell hop failed");
		}
		++g_hopCount;
		SKSE::log::info("Respawn: cell hop (moved {} refs, no re-place)", moved);
	}
	int ReplacedCount() { return g_replacedCount; }
	int HopCount() { return g_hopCount; }
	int CellSwapCount() { return g_cellSwapCount; }

	// ---- 51: what the lake boats (LakeBoats.cpp) borrow: the same hull, oars, lantern and placement as the player's boat ----
	RE::TESBoundObject* HullBase()
	{
		auto* stat = RE::TESForm::LookupByID<RE::TESObjectSTAT>(kBoatStatic);
		return stat ? stat : RE::TESForm::LookupByEditorID<RE::TESObjectSTAT>("ShipRowBoat01");
	}
	RE::TESBoundObject* OarBase()
	{
		if (g_oarProp[0].base) return g_oarProp[0].base;  // the plain static twin once the player's oars found it
		return RE::TESForm::LookupByID<RE::TESBoundObject>(0x00050E77);
	}
	RE::TESBoundObject* BenchBase() { return g_plankProp.base ? g_plankProp.base : FindPlankBase(g_plankProp.text, false); }
	RE::TESBoundObject* LanternBase() { return FindPlankBase(kLampSternForm, false); }
	RE::TESBoundObject* LanternLightBase() { return LampLightBase(); }
	RE::NiPoint3 SternLampLocal() { return g_lamp[0].offset; }
	float LanternScale() { return g_lamp[0].scale; }

	RE::NiMatrix3 YawMatrix(float a_yaw)
	{
		return MakeMat(std::cos(a_yaw), std::sin(a_yaw), 0.0f, -std::sin(a_yaw), std::cos(a_yaw), 0.0f, 0.0f, 0.0f, 1.0f);
	}

	// The boat frame rocked by the idle motion (same as a moored boat), and the heave to add to the vehicle point.
	RE::NiMatrix3 RockedFrame(float a_yaw, float a_time, float& a_heave)
	{
		float rl = 0.0f, pt = 0.0f;
		a_heave = 0.0f;
		IdleRock(a_time, 1.0f, rl, pt, a_heave);
		return YawMatrix(a_yaw) * (RxM(pt) * RyM(rl));
	}

	void PlaceHullNode(RE::NiAVObject* a_root, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot)
	{
		if (!a_root) return;
		const RE::NiPoint3 ro = a_rot * RE::NiPoint3{ g_offset.x, g_offset.y, g_offset.z + kRollPivotDrop };
		ApplyPlankNode(a_root, RE::NiPoint3{ a_vehicle.x + ro.x, a_vehicle.y + ro.y, a_vehicle.z - kRollPivotDrop + ro.z }, a_rot, g_scale);
	}

	RE::NiPoint3 HullCentre(const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot)
	{
		const RE::NiPoint3 ro = a_rot * RE::NiPoint3{ g_offset.x, g_offset.y, g_offset.z + kRollPivotDrop };
		return RE::NiPoint3{ a_vehicle.x + ro.x, a_vehicle.y + ro.y, a_vehicle.z - kRollPivotDrop + ro.z };
	}

	void PlaceOarNode(RE::NiAVObject* a_root, int a_side, float a_out, float a_phase, float a_amp, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot)
	{
		if (!a_root) return;
		LearnOarBounds();
		const OarPose op = ComputeOarPose(a_side, a_out, a_phase, a_amp, a_vehicle, a_rot);
		ApplyPlankNode(a_root, op.meshPos, op.meshRot, op.t.scale);
	}

	// =====================================================================
	// 56 (v1.0.56): oars baked into the rowing clip. The clip moves AnimObjectA (left oar) and AnimObjectB (right oar)
	// together with the hands; ShipOar01 is hung on those bones while the rower rows, full size (scale 1.0).
	// =====================================================================
	namespace
	{
		constexpr const char* kBakedBone[2] = { "AnimObjectA", "AnimObjectB" };
		constexpr const char* kBakedOarNode[2] = { "OarsAndSails BakedOar A", "OarsAndSails BakedOar B" };
		constexpr int kBakedTune[2] = { 1, 0 };  // AnimObjectA is the left oar (g_oar[1]), B the right one (g_oar[0])

		std::string BoatIniPath()
		{
			std::error_code ec;
			return (std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OarsAndSails.ini").string();
		}

		// The baked oars' tune: an offset in each bone's own space (x = out along the shaft, y = forward, z = up), a turn
		// (yaw about z, pitch about y, roll about x = the shaft) and a size. Given for the right oar (AnimObjectB); the left
		// one is its mirror image (y, yaw and roll change sign). [Boat] BakedOarRight/Forward/Up/Yaw/Pitch/Roll/Scale.
		// 56.13: OarBlendSeconds, the swap between the rest oars and the clip's; OarClipSettle, the least wait after the
		// attach before the hung oars show (the graph's transition from the bind pose).
		constexpr float kOarBlendSeconds = 0.8f;
		constexpr float kOarClipSettle = 0.3f;
		struct BakedIni { bool on = true; BakedOarTune tune{}; float blend = kOarBlendSeconds; float settle = kOarClipSettle; bool handLock = true; bool lakeRef = true;
			// 56.21: palm grip: the handle end sticks HandGripGap past the grip; the grip point is offset from the hand node
			// along the palm frame (X across the knuckles, Y toward the fingers, Z out of the palm).
			float gripGap = 12.0f, gripX = 0.0f, gripY = 7.0f, gripZ = 4.2f;
			// 56.22: the hand lock's oarlock = the boat model's fittings (model space, +X the right one), not the old prop tune.
			bool lockFromBoat = true;
			RE::NiPoint3 lockModel{ 94.3f, 0.0f, 70.7f }; };
		BakedIni& BakedSettings()
		{
			static BakedIni v;
			static bool loaded = false;
			if (!loaded) {
				loaded = true;
				const auto ini = BoatIniPath();
				auto num = [&](const char* a_key, float a_def) {
					char buf[64]{};
					GetPrivateProfileStringA("Boat", a_key, "", buf, sizeof(buf), ini.c_str());
					try { return buf[0] ? std::stof(buf) : a_def; } catch (...) { return a_def; }
				};
				v.on = num("BakedOars", 1.0f) != 0.0f;
				// 56.18.3: once on v1.0.68, the saved numpad tune goes (it was fitted around the missing hand lock).
				if (GetPrivateProfileIntA("Boat", "BakedTuneReset", 0, ini.c_str()) < 68) {
					for (const char* k : { "BakedOarRight", "BakedOarForward", "BakedOarUp", "BakedOarYaw", "BakedOarPitch", "BakedOarRoll", "BakedOarScale" })
						WritePrivateProfileStringA("Boat", k, nullptr, ini.c_str());
					WritePrivateProfileStringA("Boat", "BakedTuneReset", "68", ini.c_str());
					SKSE::log::info("Oar bake: saved numpad tune removed from the ini (v1.0.68 reset), built-in defaults from now on");
				}
				v.handLock = num("HandLock", 1.0f) != 0.0f;
				v.gripGap = std::clamp(num("HandGripGap", 12.0f), 0.0f, 60.0f);
				v.gripX = num("HandGripX", 0.0f);
				v.gripY = num("HandGripY", 7.0f);
				v.gripZ = num("HandGripZ", 4.2f);
				v.lockFromBoat = num("OarlockFromBoat", 1.0f) != 0.0f;
				v.lockModel = RE::NiPoint3{ num("OarlockX", 94.3f), num("OarlockY", 0.0f), num("OarlockZ", 70.7f) };
				{
					// 56.20.2: [Boat] LakeOarMode=ref (default): lake rowers' own oar refs take the baked pose; clone = the hung copies.
					char mode[32]{};
					GetPrivateProfileStringA("Boat", "LakeOarMode", "ref", mode, sizeof(mode), ini.c_str());
					v.lakeRef = _stricmp(mode, "clone") != 0;
					SKSE::log::info("Boat ini: LakeOarMode {}", v.lakeRef ? "ref (lake boats' own oar refs take the baked pose)" : "clone (hung copies, as before)");
				}
				auto& t = v.tune;
				t.offset = RE::NiPoint3{ num("BakedOarRight", 0.0f), num("BakedOarForward", 0.0f), num("BakedOarUp", 0.0f) };
				t.yaw = num("BakedOarYaw", 0.0f) * 0.0174533f;
				t.pitch = num("BakedOarPitch", 0.0f) * 0.0174533f;
				// 56.9: the blade's thin axis is the nif's Z; 90 squares it through the drive like the Blender clip
				// (mirrored per side). -90 if the log shows it the other way round.
				t.roll = num("BakedOarRoll", 90.0f) * 0.0174533f;
				t.scale = std::clamp(num("BakedOarScale", 1.0f), 0.1f, 5.0f);
				v.blend = std::clamp(num("OarBlendSeconds", kOarBlendSeconds), 0.05f, 5.0f);
				v.settle = std::clamp(num("OarClipSettle", kOarClipSettle), 0.0f, 1.0f);
				SKSE::log::info("Boat ini: BakedOars {} OarBlendSeconds {:.2f} OarClipSettle {:.2f} HandLock {} HandGripGap {:.1f} HandGripX {:.1f} HandGripY {:.1f} HandGripZ {:.1f} OarlockFromBoat {} OarlockX {:.1f} OarlockY {:.1f} OarlockZ {:.1f}",
					v.on ? 1 : 0, v.blend, v.settle, v.handLock ? 1 : 0, v.gripGap, v.gripX, v.gripY, v.gripZ, v.lockFromBoat ? 1 : 0, v.lockModel.x, v.lockModel.y, v.lockModel.z);
				LogBakedOar();
			}
			return v;
		}

		// 56.16: no model node is kept between row starts. Before, the first Demand's node was held for the session; a static
		// ref's 3D is that same node, so once the first boat's cell unloaded we kept cloning an orphan (its local frozen at the
		// old boat, nothing drawn anywhere else). Each attach demands the model again, clones it and lets go.
		const RE::NiNode* g_lastBakedSource = nullptr;  // only compared, never used
		std::string g_bakedSourceKind;
		RE::NiPointer<RE::NiNode> BakedOarModel()
		{
			RE::NiPointer<RE::NiNode> out;
			auto* base = OarBase();
			auto* model = base ? base->As<RE::TESModel>() : nullptr;
			const char* path = model ? model->GetModel() : nullptr;
			if (!path || !*path) {
				SKSE::log::warn("Oar bake: ShipOar01 has no model path");
				return out;
			}
			const RE::BSModelDB::DBTraits::ArgsType args{};
			auto err = RE::BSModelDB::Demand(path, out, args);
			if (!out) {
				const std::string withMeshes = std::string("meshes\\") + path;
				err = RE::BSModelDB::Demand(withMeshes.c_str(), out, args);
			}
			if (!out) {
				SKSE::log::info("Oar bake: model '{}' NOT loaded (code {})", path, static_cast<int>(err));
				return out;
			}
			// The model database hands back its own node: new, the same one as the last row start, or one a loaded oar ref
			// draws right now (parent set). Its local is left alone (a ref may be using it); only the clone's is reset.
			const bool same = out.get() == g_lastBakedSource;
			g_lastBakedSource = out.get();
			const auto& l = out->local;
			g_bakedSourceKind = std::format("fresh demand, {}{}, source local ({:.0f},{:.0f},{:.0f})", same ? "same node as the last row start" : "new node",
				out->parent ? std::format(", in use (parent '{}')", out->parent->name.c_str() ? out->parent->name.c_str() : "") : std::string(", not in the scene"),
				l.translate.x, l.translate.y, l.translate.z);
			SKSE::log::info("Oar bake: model '{}' loaded (code {}): {}", path, static_cast<int>(err), g_bakedSourceKind);
			return out;
		}

		RE::NiAVObject* FindChild(RE::NiNode* a_node, const char* a_name)
		{
			if (!a_node) return nullptr;
			for (auto& child : a_node->GetChildren()) {
				if (child && child->name.c_str() && std::string_view(child->name.c_str()) == a_name) return child.get();
			}
			return nullptr;
		}

		// Side a_i's wrapper transform on its bone: the model's centre on the bone origin (plus the tuned offset), its long
		// axis on the bone's X; the nif has its blade at -X, so it is turned end for end (g_oarMeshFlip) to put the blade
		// outboard and the handle at the hands. AnimObjectA (a_i 0, left) mirrors the right oar's tune.
		RE::NiTransform BakedTuneLocal(int a_i)
		{
			// 56.18.2: with the hand lock on, the lock places and aims the oar: only roll is kept from the tune, and the size
			// unless it is the old 0.86 fit.
			BakedOarTune t = BakedSettings().tune;
			if (BakedSettings().handLock) {
				t.offset = RE::NiPoint3{};
				t.yaw = 0.0f;
				t.pitch = 0.0f;
				if (std::abs(t.scale - 0.86f) < 0.005f) t.scale = 1.0f;
			}
			const float m = a_i == 0 ? -1.0f : 1.0f;
			const RE::NiMatrix3 axis = g_oarAxisX ? MakeMat(1, 0, 0, 0, 1, 0, 0, 0, 1) : RzM(1.5708f);  // long axis onto X
			const RE::NiMatrix3 flip = g_oarMeshFlip ? RzM(3.14159265f) : MakeMat(1, 0, 0, 0, 1, 0, 0, 0, 1);
			const RE::NiMatrix3 rot = RzM(m * t.yaw) * RyM(t.pitch) * RxM(m * t.roll) * flip * axis;
			const RE::NiPoint3 c = rot * g_oarCentre * t.scale;
			RE::NiTransform out;
			out.rotate = rot;
			out.translate = RE::NiPoint3{ t.offset.x - c.x, m * t.offset.y - c.y, t.offset.z - c.z };
			out.scale = t.scale;
			return out;
		}

		// 56.6: the oar's own root is a BSFadeNode (the nif's root). Outside a cell's fade list its fade stays 0, so the
		// geometry draws nothing while every flag reads visible. Kept at 1 every frame.
		// 56.7: that root also came with a ref's world spot in its local (about 41000 away): kept at identity every frame,
		// so the wrapper alone places the oar. The shapes' own locals are the model's and are left alone.
		void KeepBakedFade(RE::NiAVObject* a_wrap)
		{
			auto* wrap = a_wrap ? a_wrap->AsNode() : nullptr;
			if (!wrap) return;
			for (auto& child : wrap->GetChildren()) {
				if (!child) continue;
				child->local = RE::NiTransform{};
				if (auto* fade = child->AsFadeNode()) fade->GetRuntimeData().currentFade = 1.0f;
			}
		}

		// Places side a_i's wrapper: on the bone, its local is the tune; on the root (BakedOarAttach=1), its local is
		// inverse(parent world) * bone world * tune. Then the world transform and bound are rebuilt from the parent at once.
		void ApplyBakedLocal(RE::NiAVObject* a_wrap, int a_i, const RE::NiAVObject* a_bone = nullptr)
		{
			const RE::NiTransform tune = BakedTuneLocal(a_i);
			if (a_bone && a_wrap->parent && a_wrap->parent != a_bone) a_wrap->local = a_wrap->parent->world.Invert() * a_bone->world * tune;
			else a_wrap->local = tune;
			KeepBakedFade(a_wrap);
			RE::NiUpdateData data{};
			data.flags.set(RE::NiUpdateData::Flag::kDirty);
			a_wrap->Update(data);
		}

		void CollectGeometry(RE::NiAVObject* a_obj, std::vector<RE::BSGeometry*>& a_out, int& a_count, int a_depth = 0);

		// One oar for side a_i: a wrapper node on the bone, a clone of a_model (56.16: demanded for this attach) under it.
		RE::NiNode* MakeBakedOar(int a_i, RE::NiNode* a_model)
		{
			auto* model = a_model;
			if (!model) return nullptr;
			LearnOarBounds();
			auto* cloneObj = model->Clone();
			auto* clone = cloneObj ? cloneObj->AsNode() : nullptr;
			if (!clone) return nullptr;
			StripPhysics(clone);
			{
				// 56.7.1: what the clone came with, then its root back to identity (shapes untouched).
				const auto& l = clone->local;
				SKSE::log::info("Oar bake: clone {} root local ({:.0f},{:.0f},{:.0f}) row0 ({:.2f},{:.2f},{:.2f}) scale {:.3f} -> identity", a_i == 0 ? "A" : "B",
					l.translate.x, l.translate.y, l.translate.z, l.rotate.entry[0][0], l.rotate.entry[0][1], l.rotate.entry[0][2], l.scale);
				std::vector<RE::BSGeometry*> shapes;
				int n = 0;
				CollectGeometry(clone, shapes, n);
				for (auto* g : shapes)
					SKSE::log::info("Oar bake: clone {} shape '{}' local ({:.0f},{:.0f},{:.0f}) scale {:.3f}", a_i == 0 ? "A" : "B", g->name.c_str() ? g->name.c_str() : "",
						g->local.translate.x, g->local.translate.y, g->local.translate.z, g->local.scale);
				clone->local = RE::NiTransform{};
			}
			static int fadeLogs = 0;
			if (fadeLogs++ < 2) {
				auto* fade = clone->AsFadeNode();
				SKSE::log::info("Oar bake: clone root '{}' is {}{}", clone->name.c_str() ? clone->name.c_str() : "", fade ? "a BSFadeNode" : "a plain node",
					fade ? std::format(", fade was {:.2f} (now kept at 1)", fade->GetRuntimeData().currentFade) : std::string{});
			}
			auto* wrap = RE::NiNode::Create(1);
			wrap->name = kBakedOarNode[a_i];
			wrap->AttachChild(clone, true);
			wrap->local = BakedTuneLocal(a_i);
			KeepBakedFade(wrap);
			return wrap;
		}

		// 56.6.3: [Boat] BakedOarAttach=1 hangs the wrappers on the actor's NPC Root instead of the bones and places them
		// from the bone's world transform every frame.
		bool BakedOnRoot()
		{
			static int v = -1;
			if (v < 0) {
				v = static_cast<int>(GetPrivateProfileIntA("Boat", "BakedOarAttach", 0, BoatIniPath().c_str())) == 1 ? 1 : 0;
				SKSE::log::info("Boat ini: BakedOarAttach {} ({})", v, v ? "oars on NPC Root, placed from the bones" : "oars on the AnimObject bones");
			}
			return v == 1;
		}
		RE::NiNode* BakedParent(RE::NiAVObject* a_root, RE::NiNode* a_bone)
		{
			if (!BakedOnRoot()) return a_bone;
			auto* npcRoot = a_root ? a_root->GetObjectByName("NPC Root [Root]") : nullptr;
			if (auto* n = npcRoot ? npcRoot->AsNode() : nullptr) return n;
			return a_root ? a_root->AsNode() : nullptr;
		}

		void CollectGeometry(RE::NiAVObject* a_obj, std::vector<RE::BSGeometry*>& a_out, int& a_count, int a_depth)
		{
			if (!a_obj || a_depth > 12) return;
			if (auto* g = a_obj->AsGeometry()) {
				++a_count;
				if (a_out.size() < 2) a_out.push_back(g);
			}
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) CollectGeometry(child.get(), a_out, a_count, a_depth + 1);
			}
		}

		// 56.6.1: everything that decides whether one hung oar draws, once per row start.
		void LogBakedDraw(int a_i, const RE::NiAVObject* a_bone, RE::NiAVObject* a_wrap, const RE::NiAVObject* a_root)
		{
			const char* side = a_i == 0 ? "A" : "B";
			if (!a_wrap) { SKSE::log::info("Oar bake: {} draw: no wrapper", side); return; }
			const auto& w = a_wrap->world;
			SKSE::log::info("Oar bake: {} draw: bone scale {:.3f} | wrapper parent '{}' world ({:.0f},{:.0f},{:.0f}) row0 ({:.2f},{:.2f},{:.2f}) scale {:.3f}", side,
				a_bone ? a_bone->world.scale : 0.0f, a_wrap->parent && a_wrap->parent->name.c_str() ? a_wrap->parent->name.c_str() : "", w.translate.x, w.translate.y,
				w.translate.z, w.rotate.entry[0][0], w.rotate.entry[0][1], w.rotate.entry[0][2], w.scale);
			if (auto* wn = a_wrap->AsNode()) {
				for (auto& child : wn->GetChildren()) {
					if (auto* fade = child ? child->AsFadeNode() : nullptr)
						SKSE::log::info("Oar bake: {} draw: fade node '{}' currentFade {:.2f}", side, fade->name.c_str() ? fade->name.c_str() : "", fade->GetRuntimeData().currentFade);
				}
			}
			std::vector<RE::BSGeometry*> geos;
			int count = 0;
			CollectGeometry(a_wrap, geos, count);
			SKSE::log::info("Oar bake: {} draw: {} geometry shape(s)", side, count);
			for (auto* g : geos) {
				const auto& rd = g->GetGeometryRuntimeData();
				std::string chain;
				int hops = 0;
				for (const RE::NiAVObject* p = g; p && hops < 16; p = p->parent, ++hops) {
					chain += std::format("{}'{}'", chain.empty() ? "" : " < ", p->name.c_str() ? p->name.c_str() : "");
					if (p == a_root) break;
				}
				SKSE::log::info("Oar bake: {} draw: geometry '{}' bound ({:.0f},{:.0f},{:.0f}) r {:.0f} world ({:.0f},{:.0f},{:.0f}) culled={} shader={} alpha={} chain {}", side,
					g->name.c_str() ? g->name.c_str() : "", g->worldBound.center.x, g->worldBound.center.y, g->worldBound.center.z, g->worldBound.radius,
					g->world.translate.x, g->world.translate.y, g->world.translate.z, g->GetAppCulled(), static_cast<bool>(rd.shaderProperty),
					static_cast<bool>(rd.alphaProperty), chain);
				// 56.7.2: after the root reset the shape sits within about half the oar's length (121) of the wrapper.
				SKSE::log::info("Oar bake: {} draw: geometry world vs wrapper world {:.0f} (want under about 121)", side, (g->world.translate - w.translate).Length());
			}
		}

		// 56.16.3: after each attach, per side: where the clone came from, its shader and texture, its fade and root local.
		void LogBakedSource(int a_i, RE::NiAVObject* a_wrap)
		{
			const char* side = a_i == 0 ? "A" : "B";
			auto* wn = a_wrap ? a_wrap->AsNode() : nullptr;
			RE::NiAVObject* clone = wn && !wn->GetChildren().empty() ? wn->GetChildren()[0].get() : nullptr;
			if (!clone) { SKSE::log::info("Oar bake: {} source: no clone under the wrapper", side); return; }
			const auto& l = clone->local;
			auto* fade = clone->AsFadeNode();
			SKSE::log::info("Oar bake: {} source: {} | clone root local ({:.0f},{:.0f},{:.0f}) scale {:.3f}{} | fade {}", side, g_bakedSourceKind, l.translate.x, l.translate.y,
				l.translate.z, l.scale, l.translate.Length() < 0.01f ? " (identity)" : " (NOT identity)",
				fade ? std::format("{:.2f}", fade->GetRuntimeData().currentFade) : std::string("n/a (plain node)"));
			std::vector<RE::BSGeometry*> geos;
			int count = 0;
			CollectGeometry(clone, geos, count);
			for (auto* g : geos) {
				auto* prop = g->GetGeometryRuntimeData().shaderProperty.get();
				auto* shader = prop;  // already a BSShaderProperty
				const char* type = prop && prop->GetRTTI() ? prop->GetRTTI()->GetName() : "none";
				std::string diffuse = "n/a";
				bool texLoaded = false;
				if (auto* lit = prop ? netimmerse_cast<RE::BSLightingShaderProperty*>(prop) : nullptr) {
					auto* mat = static_cast<RE::BSLightingShaderMaterialBase*>(lit->material);
					const char* path = mat && mat->textureSet ? mat->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse) : nullptr;
					diffuse = path && *path ? std::format("'{}'", path) : std::string("EMPTY");
					texLoaded = mat && mat->diffuseTexture && mat->diffuseTexture->rendererTexture;
				}
				SKSE::log::info("Oar bake: {} source: shape '{}' shader {} flags {:016X} alpha {:.2f} | diffuse {} (texture {})", side, g->name.c_str() ? g->name.c_str() : "", type,
					shader ? shader->flags.underlying() : 0ull, shader ? shader->alpha : 0.0f, diffuse, texLoaded ? "loaded" : "NOT loaded");
			}
		}

		// 56.20.3: the nearest BSFadeNode at or above a_from: its fade and name ("none" if there is none).
		std::string NearestFade(const RE::NiAVObject* a_from)
		{
			int hops = 0;
			for (auto* p = a_from; p && hops < 32; p = p->parent, ++hops) {
				if (auto* f = const_cast<RE::NiAVObject*>(p)->AsFadeNode())
					return std::format("{:.2f} ('{}', {} up)", f->GetRuntimeData().currentFade, f->name.c_str() ? f->name.c_str() : "", hops);
			}
			return "none";
		}

		// The old oarlock: the prop tune's offset (g_oar), right/left 64.
		RE::NiPoint3 TuneOarlockWorld(int a_tune, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot)
		{
			const auto& t = g_oar[a_tune];
			const RE::NiPoint3 o = a_rot * RE::NiPoint3{ t.offset.x, t.offset.y, t.offset.z + kRollPivotDrop };
			return RE::NiPoint3{ a_vehicle.x + o.x, a_vehicle.y + o.y, a_vehicle.z - kRollPivotDrop + o.z };
		}
		// 56.22: the boat model's oarlock fitting for a_tune (0 = OarR = +X, 1 = OarL = -X), placed the way PlaceHullNode
		// places the hull: the model point scaled (and turned by g_yaw, if any) added to g_offset, then the boat frame.
		RE::NiPoint3 FittingOarlockWorld(int a_tune, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot)
		{
			const auto& m = BakedSettings().lockModel;
			RE::NiPoint3 p{ (a_tune == 0 ? 1.0f : -1.0f) * m.x, m.y, m.z };
			if (g_yaw != 0.0f) p = RzM(g_yaw) * p;
			p = p * g_scale;
			const RE::NiPoint3 o = a_rot * RE::NiPoint3{ g_offset.x + p.x, g_offset.y + p.y, g_offset.z + kRollPivotDrop + p.z };
			return RE::NiPoint3{ a_vehicle.x + o.x, a_vehicle.y + o.y, a_vehicle.z - kRollPivotDrop + o.z };
		}
		RE::NiPoint3 OarlockWorld(int a_tune, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot)
		{
			return BakedSettings().lockFromBoat ? FittingOarlockWorld(a_tune, a_vehicle, a_rot) : TuneOarlockWorld(a_tune, a_vehicle, a_rot);
		}

		// 56.23.2: once a second while armed. The node pointer shows whether OarR and OarL share one model node (a static
		// ref's 3D can be the model database's node), which would make one placement move the other oar.
		void TendOarGuard(float a_dt, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot)
		{
			if (g_oarGuardFor <= 0.0f) return;
			g_oarGuardFor -= a_dt;
			if ((g_oarGuardTick -= a_dt) > 0.0f) return;
			g_oarGuardTick = 1.0f;
			RE::TESObjectREFRPtr refs[2];
			RE::NiAVObject* r3d[2]{};
			for (int s2 = 0; s2 < 2; ++s2) {
				refs[s2] = g_oarProp[s2].cur.handle.get();
				r3d[s2] = refs[s2] ? refs[s2]->Get3D() : nullptr;
			}
			for (int s2 = 0; s2 < 2; ++s2) {
				const char* tag = g_oarProp[s2].tag;
				if (!refs[s2]) {
					SKSE::log::info("Oar guard: {} no ref (baked {})", tag, g_boatOarsBaked);
					continue;
				}
				const RE::NiPoint3 fit = FittingOarlockWorld(s2, a_vehicle, a_rot);
				const bool shared = r3d[s2] && r3d[s2] == r3d[1 - s2];
				SKSE::log::info("Oar guard: {} ref {:08X} 3D {} node {} culled {} | middle {} from the fitting | fade {} | baked {}{}", tag, refs[s2]->GetFormID(),
					r3d[s2] != nullptr, static_cast<const void*>(r3d[s2]), r3d[s2] ? r3d[s2]->GetAppCulled() : false,
					r3d[s2] ? std::format("{:.0f}", (r3d[s2]->world * g_oarCentre - fit).Length()) : std::string("-"), r3d[s2] ? NearestFade(r3d[s2]) : std::string("n/a"),
					g_boatOarsBaked, shared ? " | SAME 3D NODE AS THE OTHER OAR" : "");
				if (g_boatOarsBaked) continue;  // hidden on purpose while the clip carries the oars
				if (r3d[s2] && r3d[s2]->GetAppCulled()) {
					r3d[s2]->SetAppCulled(false);
					g_oarCulledByBake[s2] = false;
					SKSE::log::info("Oar guard: restored {} (was culled)", tag);
				}
				if (!r3d[s2]) {
					if (++g_oarGuardNo3D[s2] >= 2) {  // a fresh ref takes a few frames to load: two checks in a row
						g_oarGuardNo3D[s2] = 0;
						RespawnPropNow(g_oarProp[s2], "oar guard: no 3D");
						SKSE::log::info("Oar guard: restored {} (no 3D, re-placed)", tag);
					}
				} else {
					g_oarGuardNo3D[s2] = 0;
				}
			}
			if (g_oarGuardFor <= 0.0f) SKSE::log::info("Oar guard: done");
		}

		struct BakedOn
		{
			RE::FormID id;
			float age;
			bool logged;
			float clip = 0.0f;
			float nextBlade = 0.0f;
			bool flipLogged = false;
			// 56.13: the hung oars take over from our oar refs (two sets) only once the clip holds the bones, starting at
			// the refs' pose and blended onto the clip over OarBlendSeconds; the same way back when rowing stops.
			bool shown = false;       // the hung oars draw
			int shownFrames = 0;      // the first shown frame the refs still draw too (same spot), then they hide
			float wait = 0.0f;        // since the attach, while the graph brings the bones onto the clip
			int waitFrames = 0;
			float u = 0.0f;           // blend, linear: 0 = the rest oars' pose, 1 = the clip's
			bool out = false;         // blending back to the rest pose
			int outDone = 0;
			bool haveAnim = false;
			RE::NiTransform anim[2];  // the clip's last oar pose, in the boat frame (held while blending out)
			int frameLogs = 0;
			int shownLogs = 0;
			// 56.14: which of our oar refs (side) each hung oar takes over from / goes back to, paired in the boat frame when
			// the hung oars show. 56.15: no end-for-end turn of the rest pose (both sets are blade outboard already).
			int restOf[2] = { 1, 0 };
			int outFrames = 0;
			bool blendDoneLogged = false;
			bool lockLogged = false;  // 56.18: the hand lock line, once after the blend, then every 2 s
			// 56.19: the clip must really pose the bones (over 20 from NPC Root and moving against it frame to frame) before
			// the hung oars show; after the 1 s cap with them unposed: pending (rest oars kept), checked once a second.
			RE::NiPoint3 prevRel[2]{};
			bool havePrev = false;
			bool pending = false;
			float pendingCheck = 0.0f;
			float unposedFor = 0.0f;  // watchdog while shown
			const void* root3D = nullptr;
			float lockLogTimer = 0.0f;
			// 56.20: lake boats, ref mode: the same maths on two wrappers kept out of the scene, written onto the boat's refs.
			bool refMode = false;
			RE::NiPointer<RE::NiAVObject> detached[2];
			bool refDiagLogged = false;
			int palmSign[2] = { 0, 0 };
			float refGuardTick = 0.0f;  // 56.24: the ref guard, once a second while the refs are driven
			int refBad[2] = { 0, 0 };  // 56.23: palm side per hand (+1 / -1, 0 = not chosen yet)
		};

		// 56.13: rotation blend.
		struct BlendQuat { float w, x, y, z; };
		BlendQuat ToQuat(const RE::NiMatrix3& a_m)
		{
			const auto& m = a_m.entry;
			BlendQuat q{};
			const float tr = m[0][0] + m[1][1] + m[2][2];
			if (tr > 0.0f) {
				const float s = 0.5f / std::sqrt(tr + 1.0f);
				q = { 0.25f / s, (m[2][1] - m[1][2]) * s, (m[0][2] - m[2][0]) * s, (m[1][0] - m[0][1]) * s };
			} else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
				const float s = 2.0f * std::sqrt((std::max)(1e-8f, 1.0f + m[0][0] - m[1][1] - m[2][2]));
				q = { (m[2][1] - m[1][2]) / s, 0.25f * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s };
			} else if (m[1][1] > m[2][2]) {
				const float s = 2.0f * std::sqrt((std::max)(1e-8f, 1.0f + m[1][1] - m[0][0] - m[2][2]));
				q = { (m[0][2] - m[2][0]) / s, (m[0][1] + m[1][0]) / s, 0.25f * s, (m[1][2] + m[2][1]) / s };
			} else {
				const float s = 2.0f * std::sqrt((std::max)(1e-8f, 1.0f + m[2][2] - m[0][0] - m[1][1]));
				q = { (m[1][0] - m[0][1]) / s, (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25f * s };
			}
			return q;
		}
		RE::NiMatrix3 FromQuat(const BlendQuat& q)
		{
			const float w = q.w, x = q.x, y = q.y, z = q.z;
			return MakeMat(1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
				2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
				2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y));
		}
		BlendQuat Slerp(BlendQuat a, BlendQuat b, float t)
		{
			float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
			if (d < 0.0f) { b = { -b.w, -b.x, -b.y, -b.z }; d = -d; }
			float ka = 1.0f - t, kb = t;
			if (d < 0.9995f) {
				const float th = std::acos(std::clamp(d, -1.0f, 1.0f)), s = std::sin(th);
				ka = std::sin((1.0f - t) * th) / s;
				kb = std::sin(t * th) / s;
			}
			BlendQuat r{ ka * a.w + kb * b.w, ka * a.x + kb * b.x, ka * a.y + kb * b.y, ka * a.z + kb * b.z };
			const float l = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
			if (l > 1e-6f) r = { r.w / l, r.x / l, r.y / l, r.z / l };
			return r;
		}
		// a_w 0 = a, 1 = b: rotation slerped, position and size lerped.
		RE::NiTransform BlendXf(const RE::NiTransform& a, const RE::NiTransform& b, float a_w)
		{
			if (a_w <= 0.0f) return a;
			if (a_w >= 1.0f) return b;
			RE::NiTransform r;
			r.rotate = FromQuat(Slerp(ToQuat(a.rotate), ToQuat(b.rotate), a_w));
			r.translate = a.translate + (b.translate - a.translate) * a_w;
			r.scale = a.scale + (b.scale - a.scale) * a_w;
			return r;
		}
		float Smooth01(float u) { u = std::clamp(u, 0.0f, 1.0f); return u * u * (3.0f - 2.0f * u); }

		// The wrapper drawn at a world transform, whatever it hangs on.
		void PlaceBakedWorld(RE::NiAVObject* a_wrap, const RE::NiTransform& a_world)
		{
			a_wrap->local = a_wrap->parent ? a_wrap->parent->world.Invert() * a_world : a_world;
			KeepBakedFade(a_wrap);
			RE::NiUpdateData data{};
			data.flags.set(RE::NiUpdateData::Flag::kDirty);
			a_wrap->Update(data);
		}
		void ReparentBaked(RE::NiAVObject* a_wrap, RE::NiNode* a_to)
		{
			if (!a_wrap || !a_to || a_wrap->parent == a_to) return;
			RE::NiPointer<RE::NiAVObject> keep(a_wrap);
			if (a_wrap->parent) a_wrap->parent->DetachChild(a_wrap);
			a_to->AttachChild(a_wrap, true);
		}
		// Where our oar ref for side a_side is drawn this frame (TendOar / PlaceOarNode place it the same way).
		RE::NiTransform RestOarWorld(int a_side, float a_out, float a_phase, float a_amp, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot)
		{
			const OarPose op = ComputeOarPose(a_side, a_out, a_phase, a_amp, a_vehicle, a_rot);
			RE::NiTransform t;
			t.rotate = op.meshRot;
			t.translate = op.meshPos;
			t.scale = op.t.scale;
			return t;
		}

		// 56.14: the model's middle, and its shaft (handle -> blade), for an oar drawn at a_t.
		RE::NiPoint3 OarMidWorld(const RE::NiTransform& a_t) { return a_t * g_oarCentre; }
		RE::NiPoint3 OarShaftWorld(const RE::NiTransform& a_t)
		{
			const RE::NiPoint3 axis = g_oarAxisX ? RE::NiPoint3{ 1.0f, 0.0f, 0.0f } : RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
			return a_t * (g_oarCentre - axis * g_oarHalfLen) - a_t * (g_oarCentre + axis * g_oarHalfLen);
		}
		// 56.18: hand lock. The oar's handle end and unit shaft (handle -> blade) for an oar drawn at a_t, the same model
		// ends the flip check uses.
		void OarHandleDir(const RE::NiTransform& a_t, RE::NiPoint3& a_handle, RE::NiPoint3& a_dir)
		{
			const RE::NiPoint3 axis = g_oarAxisX ? RE::NiPoint3{ 1.0f, 0.0f, 0.0f } : RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
			a_handle = a_t * (g_oarCentre + axis * g_oarHalfLen);
			a_dir = a_t * (g_oarCentre - axis * g_oarHalfLen) - a_handle;
			const float l = a_dir.Length();
			a_dir = l > 1e-4f ? a_dir / l : RE::NiPoint3{ 1.0f, 0.0f, 0.0f };
		}
		float PointLineDist(const RE::NiPoint3& a_p, const RE::NiPoint3& a_o, const RE::NiPoint3& a_dir)
		{
			const RE::NiPoint3 d = a_p - a_o;
			const float along = d.x * a_dir.x + d.y * a_dir.y + d.z * a_dir.z;
			return (d - a_dir * along).Length();
		}
		// Shortest arc from unit a to unit b (Rodrigues); the roll about the shaft is left as it was.
		RE::NiMatrix3 ArcRotation(const RE::NiPoint3& a, const RE::NiPoint3& b)
		{
			const float c = a.x * b.x + a.y * b.y + a.z * b.z;
			RE::NiMatrix3 r;
			if (c < -0.9999f) {
				// Opposite: half a turn about any axis at right angles to a.
				RE::NiPoint3 k = std::abs(a.x) < 0.9f ? RE::NiPoint3{ 0.0f, -a.z, a.y } : RE::NiPoint3{ -a.z, 0.0f, a.x };
				k = k / k.Length();
				const float kk[3] = { k.x, k.y, k.z };
				for (int i = 0; i < 3; ++i)
					for (int j = 0; j < 3; ++j) r.entry[i][j] = 2.0f * kk[i] * kk[j] - (i == j ? 1.0f : 0.0f);
				return r;
			}
			const RE::NiPoint3 v{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
			const float K[3][3] = { { 0.0f, -v.z, v.y }, { v.z, 0.0f, -v.x }, { -v.y, v.x, 0.0f } };
			const float f = 1.0f / (1.0f + c);
			for (int i = 0; i < 3; ++i)
				for (int j = 0; j < 3; ++j) {
					float kk = 0.0f;
					for (int n = 0; n < 3; ++n) kk += K[i][n] * K[n][j];
					r.entry[i][j] = (i == j ? 1.0f : 0.0f) + K[i][j] + kk * f;
				}
			// NiMatrix3 * NiPoint3 is taken as row . vector; if a lands off b the other way round is meant.
			const RE::NiPoint3 ra = r * a;
			if (ra.x * b.x + ra.y * b.y + ra.z * b.z < 0.999f) r = r.Transpose();
			return r;
		}
		struct HandLockOut { RE::NiTransform xf; bool capped = false; };
		// The oar turned about its grip (a_gap in from the handle end, at its size; 56.21: HandGripGap, was 4) onto the
		// a_hand -> oarlock line, then moved so the grip is on a_hand (30 at the most). a_hand: the palm grip point.
		HandLockOut HandLockXf(const RE::NiTransform& a_t, const RE::NiPoint3& a_hand, const RE::NiPoint3& a_lock, float a_gap)
		{
			HandLockOut out{ a_t };
			RE::NiPoint3 handle, d;
			OarHandleDir(a_t, handle, d);
			const RE::NiPoint3 grip = handle + d * (a_gap * a_t.scale);
			RE::NiPoint3 d2 = a_lock - a_hand;
			const float l2 = d2.Length();
			if (l2 < 1.0f) return out;
			d2 = d2 / l2;
			const RE::NiMatrix3 r = ArcRotation(d, d2);
			out.xf.rotate = r * a_t.rotate;
			out.xf.translate = grip + r * (a_t.translate - grip);
			RE::NiPoint3 move = a_hand - grip;
			const float ml = move.Length();
			if (ml > 30.0f) {
				move = move * (30.0f / ml);
				out.capped = true;
			}
			out.xf.translate = out.xf.translate + move;
			return out;
		}

		// 56.21.2: the palm grip point G of side a_i's hand (0 = left, 1 = right), from the hand and finger nodes. a_ok false
		// (G = the hand node) if a finger node is missing.
		RE::NiAVObject* HandPart(RE::NiAVObject* a_root, int a_i, const char* a_part)
		{
			const char lr = a_i == 0 ? 'L' : 'R';
			return a_root->GetObjectByName(std::format("NPC {} Finger{} [{}F{}]", lr, a_part, lr, a_part));
		}
		// 56.23.A: a_sign picks the palm side (+1: Z = X0 x Y, the right hand's palm normal; -1: Y x X0, the left hand's).
		// The old fingertip-curl rule followed the pose and flipped.
		RE::NiPoint3 PalmGrip(RE::NiAVObject* a_root, int a_i, const RE::NiAVObject* a_hand, float a_gx, float a_gy, float a_gz, bool& a_ok, float a_sign)
		{
			const RE::NiPoint3 h = a_hand->world.translate;
			RE::NiAVObject* f[5] = { HandPart(a_root, a_i, "10"), HandPart(a_root, a_i, "20"), HandPart(a_root, a_i, "30"), HandPart(a_root, a_i, "40"),
				HandPart(a_root, a_i, "12") };
			a_ok = f[0] && f[1] && f[2] && f[3] && f[4];
			if (!a_ok) return h;
			auto unit = [](RE::NiPoint3 v) { const float l = v.Length(); return l > 1e-4f ? v / l : v; };
			auto dot = [](const RE::NiPoint3& a, const RE::NiPoint3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
			auto cross = [](const RE::NiPoint3& a, const RE::NiPoint3& b) { return RE::NiPoint3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; };
			const RE::NiPoint3 mean = (f[0]->world.translate + f[1]->world.translate + f[2]->world.translate + f[3]->world.translate) * 0.25f;
			const RE::NiPoint3 y = unit(mean - h);
			RE::NiPoint3 x = f[0]->world.translate - f[3]->world.translate;
			x = unit(x - y * dot(x, y));
			RE::NiPoint3 z = unit(cross(x, y));
			(void)dot;
			if (a_sign < 0.0f) z = z * -1.0f;  // X0 stays (the thumb side); only the palm normal turns
			return h + (x * a_gx + y * a_gy + z * a_gz) * a_hand->world.scale;
		}

		std::string LocalText(const RE::NiTransform& a_boatInv, const RE::NiTransform& a_t)
		{
			const RE::NiPoint3 p = a_boatInv * OarMidWorld(a_t);
			return std::format("({:.0f},{:.0f},{:.0f})", p.x, p.y, p.z);
		}

		// 56.9: the model's two ends in world space. The nif's blade is at its -X end (the handle at +X), Y if the
		// long axis is Y.
		void OarEnds(const RE::NiAVObject* a_wrap, RE::NiPoint3& a_blade, RE::NiPoint3& a_handle)
		{
			const RE::NiPoint3 axis = g_oarAxisX ? RE::NiPoint3{ 1.0f, 0.0f, 0.0f } : RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
			a_blade = a_wrap->world * (g_oarCentre - axis * g_oarHalfLen);
			a_handle = a_wrap->world * (g_oarCentre + axis * g_oarHalfLen);
		}
		std::vector<BakedOn> g_baked;
		RE::FormID g_boatRower = 0;

		void DetachBaked(RE::Actor* a_actor)
		{
			auto* root = a_actor ? a_actor->Get3D(false) : nullptr;
			for (int i = 0; i < 2; ++i) {
				// Wherever it hangs (the bone, or NPC Root with BakedOarAttach=1).
				auto* oar = root ? root->GetObjectByName(kBakedOarNode[i]) : nullptr;
				if (auto* parent = oar ? oar->parent : nullptr) parent->DetachChild(oar);
			}
		}
	}

	bool BakedOarsOn() { return BakedSettings().on; }
	float SecondsSinceRespawn()
	{
		if (!g_anyRespawnEnd) return 1e9f;
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - g_lastRespawnEnd).count();
	}
	BakedOarTune& BakedOar() { return BakedSettings().tune; }

	void LogBakedOar()
	{
		const auto& t = BakedSettings().tune;
		SKSE::log::info("Oar bake tuning: right={:.0f} forward={:.0f} up={:.0f} yaw={:.1f} pitch={:.1f} roll={:.1f} scale={:.2f}", t.offset.x, t.offset.y, t.offset.z,
			t.yaw * 57.2958f, t.pitch * 57.2958f, t.roll * 57.2958f, t.scale);
	}

	void SaveBakedOar()
	{
		const auto& t = BakedSettings().tune;
		const auto ini = BoatIniPath();
		const std::pair<const char*, float> keys[] = { { "BakedOarRight", t.offset.x }, { "BakedOarForward", t.offset.y }, { "BakedOarUp", t.offset.z },
			{ "BakedOarYaw", t.yaw * 57.2958f }, { "BakedOarPitch", t.pitch * 57.2958f }, { "BakedOarRoll", t.roll * 57.2958f }, { "BakedOarScale", t.scale } };
		for (const auto& [k, v] : keys) WritePrivateProfileStringA("Boat", k, std::format("{:g}", std::round(v * 100.0f) / 100.0f).c_str(), ini.c_str());
		// The oarlock line again with the new values (0.3 s, so the step has been drawn).
		for (auto& b : g_baked) { b.logged = false; b.age = 0.2f; }
	}

	namespace
	{
		void DropBaked(RE::Actor* a_actor, const char* a_why)
		{
			auto it = std::find_if(g_baked.begin(), g_baked.end(), [&](const BakedOn& b) { return b.id == a_actor->GetFormID(); });
			if (it == g_baked.end()) return;
			DetachBaked(a_actor);
			g_baked.erase(it);
			if (a_why && std::string_view(a_why) == "rowing -> resting swap") {
				SKSE::log::info("Oar bake: rowing -> resting swap on {} {:08X}", a_actor->GetName(), a_actor->GetFormID());
				if (a_actor->GetFormID() == g_boatRower) ArmOarGuard("rowing -> resting swap");  // 56.23.2: the player's boat only
			}
			else
				SKSE::log::info("Oar bake: detached from {} {:08X} ({})", a_actor->GetName(), a_actor->GetFormID(), a_why ? a_why : "");
		}

		std::string XfText(const RE::NiTransform& a_t) { return std::format("({:.0f},{:.0f},{:.0f})", a_t.translate.x, a_t.translate.y, a_t.translate.z); }
	}

	bool LakeOarRefMode() { return BakedSettings().lakeRef; }

	bool TendBakedOars(RE::Actor* a_actor, bool a_want, float a_dt, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot, float a_out, float a_phase, float a_amp,
		const char* a_why, RE::NiAVObject* const* a_refs)
	{
		if (!a_actor) return false;
		if (g_baked.size() > 32) {  // lake crews removed while rowing: their oars went with their 3D
			std::erase_if(g_baked, [](const BakedOn& b) {
				auto* a = RE::TESForm::LookupByID<RE::Actor>(b.id);
				return !a || a->IsDeleted() || !a->Is3DLoaded();
			});
		}
		auto it = std::find_if(g_baked.begin(), g_baked.end(), [&](const BakedOn& b) { return b.id == a_actor->GetFormID(); });
		auto* root = a_actor->Get3D(false);
		auto* rootNode = root ? root->AsNode() : nullptr;
		const auto& set = BakedSettings();
		LearnOarBounds();
		RE::NiTransform boat;
		boat.rotate = a_rot;
		boat.translate = a_vehicle;
		boat.scale = 1.0f;
		const RE::NiTransform boatInv = boat.Invert();
		RE::NiTransform restAll[2];  // by our oar side (g_oar[0] / g_oar[1])
		for (int s = 0; s < 2; ++s) restAll[s] = RestOarWorld(s, a_out, a_phase, a_amp, a_vehicle, a_rot);
		// 56.14: hung oar i's rest pose, by the pairing made when the hung oars showed.
		auto restFor = [&](const BakedOn& a_b, int a_i) -> const RE::NiTransform& { return restAll[a_b.restOf[a_i]]; };
		// 56.14.5: blend frames 1, 5, 10 and 20, boat-local oar middles (x right, y forward, z up).
		auto blendLog = [&](const BakedOn& a_b, int a_frame, const char* a_dir, float a_w, const RE::NiTransform (&a_drawn)[2], const RE::NiTransform (&a_anim)[2]) {
			if (a_frame != 1 && a_frame != 5 && a_frame != 10 && a_frame != 20) return;
			SKSE::log::info("Oar bake: blend {} frame {} w={:.2f}: A drawn {} (rest {} anim {}) | B drawn {} (rest {} anim {})", a_dir, a_frame, a_w,
				LocalText(boatInv, a_drawn[0]), LocalText(boatInv, restFor(a_b, 0)), LocalText(boatInv, a_anim[0]), LocalText(boatInv, a_drawn[1]),
				LocalText(boatInv, restFor(a_b, 1)), LocalText(boatInv, a_anim[1]));
		};

		// 56.13.3: rowing stopped: back from the clip's last pose to the rest oars, hung on the 3D root so the bones are
		// free to leave the clip; our refs show again once the hung oars are on them.
		if (!a_want || !set.on) {
			if (it == g_baked.end()) return false;
			if (!set.on || !it->shown || !it->haveAnim || !rootNode) {
				DropBaked(a_actor, a_why);
				return false;
			}
			RE::NiAVObject* hung[2]{};
			for (int i = 0; i < 2; ++i) {
				hung[i] = it->refMode ? it->detached[i].get() : root->GetObjectByName(kBakedOarNode[i]);
				if (!hung[i]) {
					DropBaked(a_actor, "3D reloaded");
					return false;
				}
			}
			if (!it->out) {
				it->out = true;
				it->outDone = 0;
				it->outFrames = 0;
				SKSE::log::info("Oar bake: {} {:08X} rowing stopped ({}): blending back to the rest oars from w={:.2f} over {:.2f} s (A -> ref {}, B -> ref {})",
					a_actor->GetName(), a_actor->GetFormID(), a_why ? a_why : "", Smooth01(it->u), set.blend, it->restOf[0], it->restOf[1]);
			}
			it->u = (std::max)(0.0f, it->u - a_dt / set.blend);
			if (it->u <= 0.0f && it->outDone++ >= 1) {
				DropBaked(a_actor, a_why);
				return false;
			}
			const float w = Smooth01(it->u);
			RE::NiTransform drawn[2], anim[2];
			for (int i = 0; i < 2; ++i) {
				if (!it->refMode) {
					ReparentBaked(hung[i], rootNode);
					hung[i]->SetAppCulled(false);
				}
				anim[i] = boat * it->anim[i];
				drawn[i] = BlendXf(restFor(*it, i), anim[i], w);  // 56.14.4: the same pairing back
				PlaceBakedWorld(hung[i], drawn[i]);
				// 56.20.1: ref mode: the boat's own ref takes it (placed after its stroke by the caller, so this wins).
				if (it->refMode && a_refs && a_refs[it->restOf[i]]) ApplyPlankNode(a_refs[it->restOf[i]], drawn[i].translate, drawn[i].rotate, drawn[i].scale);
			}
			blendLog(*it, ++it->outFrames, "out", w, drawn, anim);
			if (it->refMode) return it->u > 0.0f;  // ref mode: true = the refs are driven (never hidden)
			return it->u > 0.0f;  // at 0 the hung oars sit on the refs for one more frame while the refs show
		}
		if (!root) return false;
		RE::NiNode* bones[2]{};
		for (int i = 0; i < 2; ++i) {
			auto* bone = root->GetObjectByName(kBakedBone[i]);
			bones[i] = bone ? bone->AsNode() : nullptr;
			if (!bones[i]) {
				static int logs = 0;
				if (logs++ < 4) SKSE::log::warn("Oar bake: {} has no '{}' bone", a_actor->GetName(), kBakedBone[i]);
				return false;
			}
		}
		bool attached = false;
		RE::NiAVObject* hung[2]{};
		const bool refMode = a_refs && (set.lakeRef || g_forceRefMode);
		if (refMode) {
			// 56.20.1: no copy in the scene: two bare wrappers of ours outside it carry the transform maths (bone x tune,
			// hand lock, blend), and the result goes onto the boat's own refs, which the engine draws out there.
			if (it == g_baked.end()) it = g_baked.insert(g_baked.end(), BakedOn{ a_actor->GetFormID(), 0.0f, false });
			if (!it->detached[0] || !it->detached[1] || it->root3D != root) {
				for (int i = 0; i < 2; ++i) {
					auto* n = RE::NiNode::Create(0);
					n->name = kBakedOarNode[i];
					it->detached[i] = RE::NiPointer<RE::NiAVObject>(n);
				}
				attached = true;
			}
			it->refMode = true;
			for (int i = 0; i < 2; ++i) hung[i] = it->detached[i].get();
		} else {
			RE::NiPointer<RE::NiNode> source;
			for (int i = 0; i < 2; ++i) {
				auto* parent = BakedParent(root, bones[i]);
				if (!parent) return false;
				hung[i] = root->GetObjectByName(kBakedOarNode[i]);  // on the bone, or on the 3D root while blending out
				if (!hung[i]) {
					if (!source) source = BakedOarModel();  // 56.16: once per attach, for both sides, let go after
					hung[i] = MakeBakedOar(i, source.get());
					if (!hung[i]) return false;
					parent->AttachChild(hung[i], true);
					attached = true;
				} else {
					ReparentBaked(hung[i], parent);
				}
			}
			if (it == g_baked.end()) it = g_baked.insert(g_baked.end(), BakedOn{ a_actor->GetFormID(), 0.0f, false });
			it->refMode = false;
		}
		// 56.19.2: a new 3D (reloaded, or loaded at last): the clones went with the old one and were made again above.
		if (it->root3D && it->root3D != root)
			SKSE::log::info("Oar bake: {} {:08X} 3D changed, clones made again on the new 3D", a_actor->GetName(), a_actor->GetFormID());
		it->root3D = root;
		if (attached) {
			static bool setsLogged = false;
			if (!setsLogged) {
				setsLogged = true;
				// 56.13.1
				SKSE::log::info("Oar bake: two oar sets: at rest our two ShipOar01 refs placed in the boat frame; rowing, ShipOar01 clones hung on AnimObjectA/B "
								"(blend {:.2f} s, settle {:.2f} s)", set.blend, set.settle);
			}
			SKSE::log::info("Oar bake: attached A/B to {} {:08X}; bone locals A ({:.0f},{:.0f},{:.0f}) B ({:.0f},{:.0f},{:.0f}), parent '{}'", a_actor->GetName(),
				a_actor->GetFormID(), bones[0]->local.translate.x, bones[0]->local.translate.y, bones[0]->local.translate.z, bones[1]->local.translate.x,
				bones[1]->local.translate.y, bones[1]->local.translate.z, bones[0]->parent && bones[0]->parent->name.c_str() ? bones[0]->parent->name.c_str() : "");
			it->age = 0.0f;
			it->logged = false;
			it->clip = 0.0f;  // the clip starts with the attach (OarsRowStart was accepted this frame or the last)
			it->nextBlade = 0.0f;
			it->flipLogged = false;
			it->shown = false;
			it->shownFrames = 0;
			it->wait = 0.0f;
			it->waitFrames = 0;
			it->u = 0.0f;
			it->out = false;
			it->haveAnim = false;
			it->frameLogs = 0;
			it->shownLogs = 0;
			it->blendDoneLogged = false;
			it->lockLogged = false;
			it->lockLogTimer = 0.0f;
			it->havePrev = false;
			it->pending = false;
			it->pendingCheck = 0.0f;
			it->unposedFor = 0.0f;
			it->refDiagLogged = false;
			// 56.22.4: the fittings against the old tune lock, once per row start.
			for (int s2 = 0; s2 < 2; ++s2) {
				const auto& m = set.lockModel;
				const RE::NiPoint3 fit = FittingOarlockWorld(s2, a_vehicle, a_rot), old = TuneOarlockWorld(s2, a_vehicle, a_rot);
				const RE::NiPoint3 fl = boatInv * fit, ol = boatInv * old;
				SKSE::log::info("Oarlock: {} fitting model-frame ({:.1f},{:.1f},{:.1f}) boat ({:.1f},{:.1f},{:.1f}) | old tune lock boat ({:.1f},{:.1f},{:.1f}) | distance {:.1f} | boat scale "
								"{:.2f} yaw {:.1f} | {} used | actor {}",
					g_oarProp[s2].tag, (s2 == 0 ? 1.0f : -1.0f) * m.x, m.y, m.z, fl.x, fl.y, fl.z, ol.x, ol.y, ol.z, (fit - old).Length(), g_scale, g_yaw * 57.2958f,
					set.lockFromBoat ? "fitting" : "old tune lock", a_actor->GetName());
			}
			if (!refMode) {
				for (int i = 0; i < 2; ++i) LogBakedSource(i, hung[i]);  // 56.16.3
				// 56.20.3: lake rowers in clone mode: the fade above the copies.
				if (a_refs)
					for (int i = 0; i < 2; ++i) {
						auto* wn = hung[i]->AsNode();
						RE::NiAVObject* clone = wn && !wn->GetChildren().empty() ? wn->GetChildren()[0].get() : nullptr;
						SKSE::log::info("Oar bake: {} lake clone mode: {} clone fade {} | ancestors' fade {}", a_actor->GetName(), i == 0 ? "A" : "B",
							clone ? NearestFade(clone) : std::string("no clone"), NearestFade(hung[i]->parent));
					}
			}
		}
		if (it->out) {
			it->out = false;
			SKSE::log::info("Oar bake: {} {:08X} rowing again at w={:.2f}: blending back onto the clip", a_actor->GetName(), a_actor->GetFormID(), Smooth01(it->u));
		}
		auto* npcRoot = root->GetObjectByName("NPC Root [Root]");
		const RE::NiPoint3 npcAt = npcRoot ? npcRoot->world.translate : root->world.translate;
		// Where the clip puts each oar this frame (what drew at once before 56.13).
		RE::NiTransform clipW[2];
		for (int i = 0; i < 2; ++i) clipW[i] = bones[i]->world * BakedTuneLocal(i);
		const float awayA = (bones[0]->world.translate - npcAt).Length(), awayB = (bones[1]->world.translate - npcAt).Length();
		// 56.13.3: the hung oars stay hidden (our refs draw) until the clip holds the bones: at least OarClipSettle and two
		// frames after the attach, both bones away from NPC Root (not the bind pose); 1 s at the most.
		// 56.19.1: posed = both bones over 20 from NPC Root and moving against it since the last frame (the clip, not the
		// bind pose: a far rower's graph never posed them and the 1 s cap showed oars that were not there).
		// Measured in NPC Root's own frame (the boat turning moves nothing there), at a point 50 out along the bone's X so a
		// bone that only turns about its origin counts as moving too.
		bool moved = false;
		const RE::NiTransform npcInv = (npcRoot ? npcRoot->world : root->world).Invert();
		for (int i = 0; i < 2; ++i) {
			const RE::NiPoint3 rel = npcInv * (bones[i]->world * RE::NiPoint3{ 50.0f, 0.0f, 0.0f });
			if (it->havePrev && (rel - it->prevRel[i]).Length() > 0.05f) moved = true;
			it->prevRel[i] = rel;
		}
		it->havePrev = true;
		const bool posed = (std::min)(awayA, awayB) > 20.0f && moved;
		// 56.19.3: watchdog: shown, but the bones back under 20 for over 0.5 s: back to the rest oars, pending again.
		if (it->shown) {
			it->unposedFor = (std::min)(awayA, awayB) < 20.0f ? it->unposedFor + a_dt : 0.0f;
			if (it->unposedFor > 0.5f) {
				SKSE::log::info("Oar bake: {} bones back under 20 from NPC Root for {:.1f} s (A {:.0f} B {:.0f}), back to the rest oars, will retry", a_actor->GetName(),
					it->unposedFor, awayA, awayB);
				it->shown = false;
				it->shownFrames = 0;
				it->u = 0.0f;
				it->haveAnim = false;
				it->pending = true;
				it->pendingCheck = 1.0f;
				it->unposedFor = 0.0f;
			}
		}
		if (!it->shown) {
			it->wait += a_dt;
			++it->waitFrames;
			// 56.14: 50 (was 20): the 56.13 log had the bones still swinging out from the bind pose at 22-65.
			const bool settled = it->waitFrames >= 2 && it->wait >= set.settle && (std::min)(awayA, awayB) > 50.0f && posed;
			bool showNow = settled;
			if (!showNow && it->wait >= 1.0f) {
				if (!it->pending) {
					it->pending = true;
					it->pendingCheck = 1.0f;
					SKSE::log::info("Oar bake: {} bones unposed after cap, keeping rest oars, will retry (A {:.0f} B {:.0f} from NPC Root, {})", a_actor->GetName(), awayA,
						awayB, moved ? "moving" : "still");
				} else if ((it->pendingCheck -= a_dt) <= 0.0f) {
					it->pendingCheck = 1.0f;  // 56.19.2: once a second
					showNow = posed;
				}
			}
			if (showNow) {
				const bool wasPending = it->pending;
				it->pending = false;
				it->shown = true;
				it->u = 0.0f;
				SKSE::log::info("Oar bake: {} hung oars shown after {:.2f} s ({} frames){}, bones A {:.0f} B {:.0f} from NPC Root; blending from the rest oars over {:.2f} s",
					a_actor->GetName(), it->wait, it->waitFrames, wasPending ? " (pending retry: bones posed now)" : "", awayA, awayB, set.blend);
				// 56.14.1: pair by the boat-local side of the oar middles (port with port), not by name: the rower sits
				// facing aft, so AnimObjectA (his left) is on the boat's right. Sides disagreeing: the shorter total distance.
				RE::NiPoint3 rl[2], al[2];
				for (int k = 0; k < 2; ++k) {
					rl[k] = boatInv * OarMidWorld(restAll[k]);
					al[k] = boatInv * OarMidWorld(clipW[k]);
				}
				auto sidesAgree = [&](int a_sw) {
					for (int k = 0; k < 2; ++k)
						if ((al[k].x >= 0.0f) != (rl[k ^ a_sw].x >= 0.0f)) return false;
					return true;
				};
				auto total = [&](int a_sw) { return (al[0] - rl[a_sw]).Length() + (al[1] - rl[1 ^ a_sw]).Length(); };
				const bool s0 = sidesAgree(0), s1 = sidesAgree(1);
				const int sw = s0 != s1 ? (s1 ? 1 : 0) : (total(1) < total(0) ? 1 : 0);
				for (int k = 0; k < 2; ++k) {
					it->restOf[k] = k ^ sw;
					SKSE::log::info("Oar bake: oar pair: rest {} ref (x={:.0f}) -> anim {} (x={:.0f}) [{}; total {:.0f} vs {:.0f}]", g_oarProp[it->restOf[k]].tag,
						rl[it->restOf[k]].x, kBakedBone[k], al[k].x, s0 != s1 ? "by side" : "sides disagree, by distance", total(sw), total(1 ^ sw));
				}
				// 56.15.3: log only. Handle end -> blade end of the mesh (world points, the same model ends OarEnds uses) for the
				// rest ref and the hung oar's clip pose. At this point the clip is still coming in, so nothing acts on it.
				for (int k = 0; k < 2; ++k) {
					RE::NiPoint3 a = OarShaftWorld(restAll[it->restOf[k]]), b = OarShaftWorld(clipW[k]);
					const float la = a.Length(), lb = b.Length();
					const float dot = la > 1e-3f && lb > 1e-3f ? (a.x * b.x + a.y * b.y + a.z * b.z) / (la * lb) : 1.0f;
					SKSE::log::info("Oar bake: {} shaft dot rest/anim {:.2f} (mesh handle->blade, log only)", kBakedBone[k], dot);
				}
			}
		} else {
			it->u = (std::min)(1.0f, it->u + a_dt / set.blend);
		}
		const float w = it->shown ? Smooth01(it->u) : 0.0f;
		// 56.18.1: the hand lock. A is the left hand, B the right; each against its side's oarlock (the rest oar's pivot,
		// paired by side). Faded in with the blend weight: none at the start, all of it once the blend is done.
		RE::NiAVObject* hands[2] = { root->GetObjectByName("NPC L Hand [LHnd]"), root->GetObjectByName("NPC R Hand [RHnd]") };
		const bool lockOn = set.handLock && hands[0] && hands[1];
		const bool lockLogNow = lockOn && it->shown && w >= 1.0f && (!it->lockLogged || (it->lockLogTimer -= a_dt) <= 0.0f);
		if (lockLogNow) {
			it->lockLogged = true;
			it->lockLogTimer = 2.0f;
		}
		if (set.handLock && !lockOn) {
			static int noHandLogs = 0;
			if (noHandLogs++ < 4) SKSE::log::warn("Oar lock: {} has no hand bone, lock skipped", a_actor->GetName());
		}
		RE::NiTransform drawn[2];
		for (int i = 0; i < 2; ++i) {
			hung[i]->SetAppCulled(!it->shown);
			if (!it->shown) {
				drawn[i] = restFor(*it, i);
				PlaceBakedWorld(hung[i], drawn[i]);
				continue;
			}
			if (!lockOn) {
				it->anim[i] = boatInv * clipW[i];
				if (w >= 1.0f && !refMode) {
					// 56.6.2: every frame: the tune (live numpad), the fade, and the world transform + bound rebuilt from the parent.
					ApplyBakedLocal(hung[i], i, bones[i]);
					drawn[i] = hung[i]->world;
				} else if (w >= 1.0f) {
					drawn[i] = clipW[i];
					PlaceBakedWorld(hung[i], drawn[i]);
				} else {
					drawn[i] = BlendXf(restFor(*it, i), clipW[i], w);
					PlaceBakedWorld(hung[i], drawn[i]);
				}
				continue;
			}
			const RE::NiPoint3 hand = hands[i]->world.translate;
			// 56.21.3: the shaft runs from the palm grip point G (not the wrist) to the oarlock, G on the shaft.
			bool palmOk = false;
			// 56.23.A: the palm side comes from the hand (left Z = Y x X0, right Z = X0 x Y), not from the pose. Both sides are
			// still measured against the clip's shaft line for the log.
			const RE::NiPoint3 gPlus = PalmGrip(root, i, hands[i], set.gripX, set.gripY, set.gripZ, palmOk, 1.0f);
			const RE::NiPoint3 gMinus = PalmGrip(root, i, hands[i], set.gripX, set.gripY, set.gripZ, palmOk, -1.0f);
			RE::NiPoint3 ch, cd;
			OarHandleDir(clipW[i], ch, cd);
			const float dPlus = PointLineDist(gPlus, ch, cd), dMinus = PointLineDist(gMinus, ch, cd);
			int& sign = it->palmSign[i];
			sign = i == 0 ? -1 : 1;  // A = left hand, B = right hand
			const RE::NiPoint3 grip = sign > 0 ? gPlus : gMinus;
			if (!palmOk) {
				static int noFingerLogs = 0;
				if (noFingerLogs++ < 2) SKSE::log::warn("Oar lock: {} {} has a finger node missing, gripping at the hand node", a_actor->GetName(), i == 0 ? "A" : "B");
			}
			const RE::NiPoint3 lock = OarlockWorld(it->restOf[i], a_vehicle, a_rot);
			const RE::NiTransform blended = w >= 1.0f ? clipW[i] : BlendXf(restFor(*it, i), clipW[i], w);
			const HandLockOut lk = HandLockXf(blended, grip, lock, set.gripGap);
			drawn[i] = w >= 1.0f ? lk.xf : BlendXf(blended, lk.xf, w);
			// Held for the blend back: the clip with the full lock, in the boat frame.
			it->anim[i] = boatInv * (w >= 1.0f ? lk.xf : HandLockXf(clipW[i], grip, lock, set.gripGap).xf);
			PlaceBakedWorld(hung[i], drawn[i]);
			if (lockLogNow) {
				// 56.18.4, 56.21.4
				RE::NiPoint3 h0, d0, h1, d1;
				OarHandleDir(blended, h0, d0);
				OarHandleDir(hung[i]->world, h1, d1);
				const RE::NiPoint3 gl = hands[i]->world.Invert() * grip;
				std::string fingers;
				for (const char* part : { "12", "22", "32", "42", "02" }) {
					auto* fn = HandPart(root, i, part);
					fingers += fn ? std::format(" F{} {:.1f}", part, PointLineDist(fn->world.translate, h1, d1)) : std::format(" F{} -", part);
				}
				SKSE::log::info("Oar lock: {} hand->shaft before {:.1f} after {:.1f} | lock->shaft before {:.1f} after {:.1f} | scale {:.2f} | actor {}{} | palm G local to hand "
								"({:.1f},{:.1f},{:.1f}){} | G->shaft before {:.1f} after {:.1f} | finger nodes to shaft:{} | palm side {} by hand (clip shaft to +Z {:.1f}, -Z {:.1f})",
					i == 0 ? "A" : "B", PointLineDist(hand, h0, d0), PointLineDist(hand, h1, d1), PointLineDist(lock, h0, d0), PointLineDist(lock, h1, d1),
					hung[i]->world.scale, a_actor->GetName(), lk.capped ? " (move capped at 30)" : "", gl.x, gl.y, gl.z, palmOk ? "" : " (fingers missing: hand node)",
					PointLineDist(grip, h0, d0), PointLineDist(grip, h1, d1), fingers, sign > 0 ? "+Z" : "-Z", dPlus, dMinus);
			}
		}
		if (it->shown) it->haveAnim = true;
		// 56.20.1: ref mode, shown: the result onto the boat's own refs (paired by side). Until then their stroke stays.
		if (refMode && it->shown && a_refs) {
			for (int i = 0; i < 2; ++i)
				if (auto* ref = a_refs[it->restOf[i]]) ApplyPlankNode(ref, drawn[i].translate, drawn[i].rotate, drawn[i].scale);
			// 56.24: the ref guard: each driven oar against its AnimObject bone, once a second; missing 3D, culled or over 60
			// away on two ticks in a row: placed again (or, on the player's boat with no 3D, the prop re-placed).
			if ((it->refGuardTick -= a_dt) <= 0.0f) {
				it->refGuardTick = 1.0f;
				for (int i = 0; i < 2; ++i) {
					const int side = it->restOf[i];
					auto* ref = a_refs[side];
					const float dist = ref ? (ref->world * g_oarCentre - bones[i]->world.translate).Length() : -1.0f;
					const bool culled = ref && ref->GetAppCulled();
					const bool bad = !ref || culled || dist > 60.0f;
					SKSE::log::info("OarGuard: {} {} ref {} 3D {} culled {} dist from AnimObject {} visible {}", a_actor->GetName(), i == 0 ? "A" : "B",
						g_forceRefMode ? std::format("{:08X}", g_oarProp[side].cur.handle.get() ? g_oarProp[side].cur.handle.get()->GetFormID() : 0u) : std::format("side {}", side),
						ref ? "y" : "n", culled ? "y" : "n", ref ? std::format("{:.0f}", dist) : std::string("-"), !bad || (ref && !culled) ? "y" : "n");
					it->refBad[i] = bad ? it->refBad[i] + 1 : 0;
					if (it->refBad[i] < 2) continue;
					it->refBad[i] = 0;
					const char* why = !ref ? "no 3D" : culled ? "culled" : "too far from the AnimObject";
					if (ref) {
						ref->SetAppCulled(false);
						ApplyPlankNode(ref, drawn[i].translate, drawn[i].rotate, drawn[i].scale);
					} else if (g_forceRefMode) {
						RespawnPropNow(g_oarProp[side], "OarGuard: no 3D");
					}
					SKSE::log::info("OarGuard: re-hung {} ({})", i == 0 ? "A" : "B", why);
				}
			}
			if (!it->refDiagLogged) {
				it->refDiagLogged = true;
				for (int i = 0; i < 2; ++i) {
					auto* ref = a_refs[it->restOf[i]];
					const RE::NiPoint3 lp = ref ? boatInv * ref->world.translate : RE::NiPoint3{};
					SKSE::log::info("Oar bake: {} lake ref mode: ref {} (side {}) 3D loaded={} culled={} fade={} | ref world {} vs boat ({:.0f},{:.0f},{:.0f})", a_actor->GetName(),
						i == 0 ? "A" : "B", it->restOf[i], ref != nullptr, ref ? ref->GetAppCulled() : false, ref ? NearestFade(ref) : std::string("n/a"),
						ref ? XfText(ref->world) : std::string("n/a"), lp.x, lp.y, lp.z);
				}
			}
		}
		// 56.13.5: the first 5 frames after the attach: the clip's oar pose (drawn at once before 56.13) against what draws.
		if (it->frameLogs < 5) {
			++it->frameLogs;
			SKSE::log::info("Oar bake: frame t={:.3f} {}: A clip {} ({:.0f} from NPC Root, {:.0f} above it) drawn {} | B clip {} ({:.0f}, {:.0f}) drawn {} | rest refs 0 {} 1 {} | w={:.2f}",
				it->wait, it->shown ? "hung oars" : "rest refs (hung hidden)", XfText(clipW[0]), awayA, bones[0]->world.translate.z - npcAt.z, XfText(drawn[0]),
				XfText(clipW[1]), awayB, bones[1]->world.translate.z - npcAt.z, XfText(drawn[1]), XfText(restAll[0]), XfText(restAll[1]), w);
		}
		if (it->shown) {
			++it->shownFrames;
			blendLog(*it, it->shownFrames, "in", w, drawn, clipW);
		}
		// 56.9: the blade through the first 4 s of each stroke run, every 0.25 s per side: the blade normal (the nif's thin
		// axis, local Z) in world space, |z| near 1 = flat, near 0 = squared; and the blade tip's height against the oarlock.
		it->clip += a_dt;
		if (it->clip <= 4.0f && it->clip >= it->nextBlade) {
			it->nextBlade += 0.25f;
			for (int i = 0; i < 2; ++i) {
				RE::NiPoint3 n = hung[i]->world.rotate * RE::NiPoint3{ 0.0f, 0.0f, 1.0f };
				const float nl = n.Length();
				if (nl > 1e-4f) n = n / nl;
				RE::NiPoint3 blade, handle;
				OarEnds(hung[i], blade, handle);
				const RE::NiPoint3 lock = OarlockWorld(it->restOf[i], a_vehicle, a_rot);
				SKSE::log::info("Oar bake: t={:.2f} {} normal.z={:.2f} ({}) tipZ-lock={:.0f}", it->clip, i == 0 ? "A" : "B", n.z,
					std::abs(n.z) > 0.7f ? "flat" : std::abs(n.z) < 0.3f ? "squared" : "between", blade.z - lock.z);
			}
		}
		// 56.9.3: once per stroke run, does the 180 flip put the blade outboard? The blade end must be the farther one from NPC Root.
		if (!it->flipLogged && it->clip >= 0.5f) {
			it->flipLogged = true;
			auto* npcRoot = root->GetObjectByName("NPC Root [Root]");
			const RE::NiPoint3 c = npcRoot ? npcRoot->world.translate : root->world.translate;
			for (int i = 0; i < 2; ++i) {
				RE::NiPoint3 blade, handle;
				OarEnds(hung[i], blade, handle);
				const float db = (blade - c).Length(), dh = (handle - c).Length();
				SKSE::log::info("Oar bake: {} flip check (meshFlip={}): blade end {:.0f} from NPC Root, handle end {:.0f}: {}", i == 0 ? "A" : "B", g_oarMeshFlip, db, dh,
					db > dh ? "blade outboard (ok)" : "BLADE INBOARD (flip wrong)");
			}
		}
		// 56.15.6: once the blend has ended, the hung oars are the clip pose exactly: the same check on that.
		if (it->shown && w >= 1.0f && !it->blendDoneLogged) {
			it->blendDoneLogged = true;
			auto* npcRoot = root->GetObjectByName("NPC Root [Root]");
			const RE::NiPoint3 c = npcRoot ? npcRoot->world.translate : root->world.translate;
			for (int i = 0; i < 2; ++i) {
				RE::NiPoint3 blade, handle;
				OarEnds(hung[i], blade, handle);
				const float db = (blade - c).Length(), dh = (handle - c).Length();
				const RE::NiTransform tune = BakedTuneLocal(i);
				const bool exact = hung[i]->parent != bones[i] || (hung[i]->local.translate - tune.translate).Length() < 0.01f;
				SKSE::log::info("Oar bake: {} flip check after the blend (clip pose{}): blade end {:.0f} from NPC Root, handle end {:.0f}: {}", i == 0 ? "A" : "B",
					exact ? ", local = tune" : ", LOCAL NOT THE TUNE", db, dh, db > dh ? "blade outboard (ok)" : "BLADE INBOARD (flip wrong)");
			}
		}
		// Once on the first stroke (the clip has moved the bones by then): where each oar is against its oarlock.
		if (!it->logged && it->shown && (it->age += a_dt) >= 0.5f) {
			it->logged = true;
			for (int i = 0; i < 2; ++i) {
				const RE::NiPoint3 at = bones[i]->world.translate;
				const RE::NiPoint3 lock = OarlockWorld(it->restOf[i], a_vehicle, a_rot);
				RE::NiPoint3 dir = bones[i]->world.rotate * RE::NiPoint3{ 1.0f, 0.0f, 0.0f };
				const float dl = dir.Length();
				if (dl > 1e-4f) dir = dir / dl;
				const RE::NiPoint3 d = lock - at;
				const float along = d.x * dir.x + d.y * dir.y + d.z * dir.z;
				const float dist = (d - dir * along).Length();
				SKSE::log::info("Oar bake: {} node ({:.0f},{:.0f},{:.0f}) oarlock ({:.0f},{:.0f},{:.0f}) dist {:.1f} (oar axis to oarlock; {:.0f} along the shaft)",
					i == 0 ? "A" : "B", at.x, at.y, at.z, lock.x, lock.y, lock.z, dist, along);
				// Visibility: anything on the way from the 3D root to the oar that is culled or hidden keeps it from drawing.
				auto flags = [](const RE::NiAVObject* a_o) {
					return a_o ? std::format("culled={} hidden={}", a_o->GetAppCulled(), a_o->flags.any(RE::NiAVObject::Flag::kHidden)) : std::string("missing");
				};
				SKSE::log::info("Oar bake: {} visibility: oar {} | bone {} | 3D root {} | first person {}", i == 0 ? "A" : "B", flags(hung[i]), flags(bones[i]), flags(root),
					RE::PlayerCamera::GetSingleton() && RE::PlayerCamera::GetSingleton()->IsInFirstPerson());
			}
			if (!refMode) for (int i = 0; i < 2; ++i) LogBakedDraw(i, bones[i], hung[i], root);  // 56.6.1
			// 56.6.4: where the oars are against the camera.
			if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
				const RE::NiPoint3 cp = cam->cameraRoot->world.translate;
				const RE::NiPoint3 a = hung[0]->world.translate, b = hung[1]->world.translate;
				SKSE::log::info("Oar bake: row start {}: oar A ({:.0f},{:.0f},{:.0f}) dist {:.0f} | oar B ({:.0f},{:.0f},{:.0f}) dist {:.0f} | camera ({:.0f},{:.0f},{:.0f})",
					a_actor->GetName(), a.x, a.y, a.z, (a - cp).Length(), b.x, b.y, b.z, (b - cp).Length(), cp.x, cp.y, cp.z);
			}
			for (int s = 0; s < 2; ++s) {
				auto ref = g_oarProp[s].cur.handle.get();
				auto* r3d = ref ? ref->Get3D() : nullptr;
				if (r3d) SKSE::log::info("Oar bake: boat's own {} ref culled={} (hidden while baked)", g_oarProp[s].tag, r3d->GetAppCulled());
			}
		}
		// 56.13: our refs hide from the second shown frame (the first, both draw at the same spot). 56.20: never in ref mode;
		// there (56.24) true means the baked pose drives the refs.
		return refMode ? it->shown : it->shownFrames >= 2;
	}

	bool TendBoatBakedOars(RE::Actor* a_rower, bool a_want, float a_dt, const char* a_why)
	{
		// The rower changed (the player took over from a follower or back), the ride ended or a game loads: the old
		// one lets go at once (no blend, nobody tends it after this).
		if (g_boatRower && (!a_rower || a_rower->GetFormID() != g_boatRower)) {
			if (auto* old = RE::TESForm::LookupByID<RE::Actor>(g_boatRower)) DropBaked(old, a_rower ? "rower changed" : a_why);
			g_boatRower = 0;
		}
		// 56.24: the boat's own two oar refs carry the baked pose (one set, no clones).
		RE::NiAVObject* refs[2]{};
		for (int s2 = 0; s2 < 2; ++s2) {
			auto ref = g_oarProp[s2].cur.handle.get();
			refs[s2] = ref ? ref->Get3D() : nullptr;
		}
		g_forceRefMode = true;
		g_boatRefDriven = a_rower && TendBakedOars(a_rower, a_want, a_dt, g_frameVehicle, g_frameRot, g_oarOut, g_oarPhase, g_oarAmp, a_why, refs);
		g_forceRefMode = false;
		g_boatOarsBaked = false;  // nothing is hidden any more
		// Tended while waiting and blending too, not only while the hung oars draw.
		if (a_rower && std::any_of(g_baked.begin(), g_baked.end(), [&](const BakedOn& b) { return b.id == a_rower->GetFormID(); })) g_boatRower = a_rower->GetFormID();
		return g_boatRefDriven;
	}

	void ForgetBakedOars()
	{
		// A game is loading: every 3D is new, nothing of ours hangs on it any more.
		g_baked.clear();
		g_boatRower = 0;
		g_boatOarsBaked = false;
		g_boatRefDriven = false;
	}

	// A prop fixed in the boat frame: a_local is right / forward / up from the vehicle point, turned by a_yaw.
	RE::NiPoint3 PlaceRigidNode(RE::NiAVObject* a_root, const RE::NiPoint3& a_local, float a_yaw, float a_scale, const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot)
	{
		const RE::NiPoint3 o = a_rot * RE::NiPoint3{ a_local.x, a_local.y, a_local.z + kRollPivotDrop };
		const RE::NiPoint3 at{ a_vehicle.x + o.x, a_vehicle.y + o.y, a_vehicle.z - kRollPivotDrop + o.z };
		if (a_root) ApplyPlankNode(a_root, at, a_rot * RzM(a_yaw), a_scale);
		return at;
	}

	void StripNodePhysics(RE::NiAVObject* a_root) { if (a_root) StripPhysics(a_root); }

	void AddLakeRef(const RE::TESObjectREFR* a_ref, std::string_view a_type)
	{
		if (!a_ref) return;
		g_lakeRefs.push_back(a_ref->GetFormID());
		TrackSpawn(a_ref, a_type);  // co-save: purged after the next load like every other prop
	}
	void RemoveLakeRef(RE::FormID a_id) { std::erase(g_lakeRefs, a_id); }
	void DeleteActorLater(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) return;
		a_ref->Disable();
		g_pendingDeletes.push_back({ a_ref->GetHandle(), 2.0f });
	}
	void ForgetPendingDeletes() { g_pendingDeletes.clear(); }

	bool HullCellOk()
	{
		auto ref = g_cur.handle.get();
		auto* cell = ref ? ref->GetParentCell() : nullptr;
		return cell && cell->IsAttached();
	}

	void SetFlightHold(bool a_hold)
	{
		if (a_hold && !g_flightHold) g_flightPostponeLogged = false;
		g_flightHold = a_hold;
	}

	float HullLag(float a_vx, float a_vy)
	{
		if (!g_active || !g_goodValid) return -1.0f;
		auto ref = g_cur.handle.get();
		auto* root = ref ? ref->Get3D() : nullptr;
		if (!root) return -1.0f;
		const float ex = (root->world.translate.x - a_vx) - (g_goodRoot.x - g_goodVehicle.x);
		const float ey = (root->world.translate.y - a_vy) - (g_goodRoot.y - g_goodVehicle.y);
		return std::sqrt(ex * ex + ey * ey);
	}

	bool RespawnActive() { return g_respawnTag != nullptr; }

	void ForceReattach(float a_vx, float a_vy, float a_vz, const char* a_why)
	{
		const RE::NiPoint3 to{ a_vx, a_vy, a_vz };
		MoveSlot(g_cur, to, "boat", a_why);
		for (Prop* pr : { &g_plankProp, &g_rowBenchProp, &g_chestProp, &g_oarProp[0], &g_oarProp[1], &g_lampProp[0], &g_lampProp[1] }) {
			if (pr->cur.handle) MoveSlot(pr->cur, to, pr->tag, a_why);
		}
	}

	std::string ChestSummary()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!player || !tes || !g_chestProp.base) return "no chest form";
		const auto cur = g_chestProp.cur.handle.get();
		int n = 0;
		std::string others;
		tes->ForEachReferenceInRange(player, 9000.0f, [&](RE::TESObjectREFR* a_ref) {
			if (!a_ref || (a_ref->GetFormID() >> 24) != 0xFF || a_ref->GetBaseObject() != g_chestProp.base || a_ref->IsDeleted()) return RE::BSContainer::ForEachResult::kContinue;
			++n;
			if (a_ref != cur.get()) {
				const auto p = a_ref->GetPosition();
				others += std::format("{}{:08X} at ({:.0f},{:.0f},{:.0f}) items {}", others.empty() ? "" : ", ", a_ref->GetFormID(), p.x, p.y, p.z, CountItems(a_ref));
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return std::format("refs alive {} (current {}, others: {})", n, cur ? std::format("{:08X}", cur->GetFormID()) : std::string("none"), others.empty() ? "none" : others);
	}

	void SetRefreshHold(bool a_hold)
	{
		if (a_hold != g_refreshHold) SKSE::log::info("Respawn: {}", a_hold ? "held (fast, airborne, landing or crew busy)" : "allowed again");
		g_refreshHold = a_hold;
	}

	bool InRefreshWindow()
	{
		if (g_refreshQuiet > 0.0f) return true;
		const float swapAt = kWarmupTime + (g_refreshHold ? kRefreshMaxHold : 0.0f);
		auto soon = [&](const Slot& a_next) { return a_next.handle && a_next.loadedTime >= swapAt - kRefreshQuiet; };
		if (soon(g_next)) return true;
		for (const Prop* p : { &g_plankProp, &g_rowBenchProp, &g_chestProp, &g_oarProp[0], &g_oarProp[1], &g_lampProp[0], &g_lampProp[1] }) {
			if (soon(p->next)) return true;
		}
		return false;
	}

	void SetOarDemo(bool a_on)
	{
		g_oarDemo = a_on;
	}

	bool OarDemo() { return g_oarDemo; }

	bool SetPlankModel(const char* a_substr)
	{
		const std::string text = a_substr ? a_substr : "";
		auto* base = FindPlankBase(text, true);
		if (!base) return false;
		g_plankProp.text = text;
		g_plankProp.base = base;
		DespawnProp(g_plankProp);  // respawned with the new model on the next frame
		g_rowBenchProp.text = text;  // the rower's bench uses the same model
		g_rowBenchProp.base = base;
		DespawnProp(g_rowBenchProp);
		return true;
	}

	bool Attach()
	{
		if (g_active && g_cur.handle) {
			return true;
		}
		g_active = true;
		g_roll = g_pitch = g_impulse = g_pitchVel = g_airTime = 0.0f;
		g_respawnCooldown = 0.0f;
		g_spawnRetry = 0.0f;
		g_spawnFailures = 0;
		g_loggedLoaded = false;
		g_missingTime = 0.0f;
		SKSE::log::info("BoatBow build v8.8: parked boat adoption, subtle vibration");
		LogTuning();
		return Spawn(g_cur);
	}

	void Detach(bool a_handOff)
	{
		g_rigidValid = false;
		g_active = false;
		// 56.28: at an unboard that parks right here the boarded hull is not deleted at once (it popped out while the fresh
		// parked boat was still loading and fading in): it stays drawn where it is until the parked boat's 3D shows
		// (UpdateParked), and the props (benches, chest, lamps, oars) carry straight over to the parked boat.
		Despawn(g_leaving);
		if (a_handOff && g_cur.handle) {
			g_leaving = g_cur;
			g_cur = Slot{};
			g_leavingTime = 0.0f;
			g_leavingFrames = 0;
			if (auto r = g_leaving.handle.get()) SKSE::log::info("Handoff: boarded hull {:08X} kept drawn until the parked boat shows, props carried over", r->GetFormID());
		} else {
			Despawn(g_cur);
		}
		Despawn(g_next);
		if (!a_handOff) DespawnProps();
		g_respawnTag = nullptr;
		g_goodValid = false;
		g_rowExternal = false;
		g_rowActive = false;
		g_roll = g_pitch = g_impulse = g_pitchVel = g_airTime = 0.0f;
		SKSE::log::info("BoatBow: removed");
	}

	namespace
	{
		RE::ObjectRefHandle g_parked;
		float g_parkedX = 0.0f, g_parkedY = 0.0f, g_parkedZ = 0.0f, g_parkedYaw = 0.0f;

		// ---- v1.0.14: things the player can look at and activate on the parked boat ---------------------------------
		// OarsAndSails.esl holds four Activator records. The game shows a prompt (name + "Activate Text Override", with the
		// right button glyph) only for something its crosshair ray hits:
		//   Boat (0x800)  "[E] Board"  the parked boat itself is placed from this activator, so its hull is the target
		//   Helm (0x801)  "[E] Steer"  an invisible pick proxy on the helm bench
		//   Oars (0x802)  "[E] Row"    an invisible pick proxy on the rower's bench
		//   Chest (0x803) "[E] Open"   an invisible pick proxy on the chest (the chest prop itself is parked underground)
		// The benches and chest are drawn by their props (whose refs sit kParkDepth below and are placed by node), so the
		// crosshair cannot hit them. The proxies are real refs at the real spot, culled (invisible), and their collision is
		// moved to a layer the crosshair ray hits but the player's character controller walks through (chosen from the
		// game's own collision matrix, see PickLayer). They exist only while the boat is parked and the player is near.
		constexpr const char* kEslName = "OarsAndSails.esl";
		RE::TESObjectACTI* g_actiBoat = nullptr;
		RE::TESObjectACTI* HullActi() { return g_actiBoat; }
		enum PickKind { kPickHelm, kPickRow, kPickChest, kPickCount };
		struct PickProxy
		{
			const char* tag;
			RE::FormID localId;
			RE::TESObjectACTI* base = nullptr;
			RE::ObjectRefHandle handle;
			RE::NiPoint3 placedAt{};     // world spot it was placed at (moved when the prop's tuning moves it)
			bool set = false;            // culled + layer set once its 3D loaded
			int modelIdx = 0;            // v1.0.15: which model candidate (ProxyModels) this proxy uses; a model without collision moves it on
		};
		PickProxy g_pick[kPickCount] = { { "HelmBench", 0x801 }, { "RowBench", 0x802 }, { "Chest", 0x803 } };
		RE::COL_LAYER g_pickLayer = RE::COL_LAYER::kUnidentified;
		bool g_pickLayerChosen = false;
		bool g_pickAllowed = true;  // false while boarded

		bool HasCollision(RE::NiAVObject* a_obj, int a_depth = 0)
		{
			if (!a_obj || a_depth > 12) return false;
			if (a_obj->collisionObject) return true;
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) if (HasCollision(child.get(), a_depth + 1)) return true;
			}
			return false;
		}

		// A layer the crosshair ray (kItemPick) collides with and the character controller does not, from the game's
		// collision matrix. Preferably one the camera does not bump into either. Chosen once, logged.
		RE::COL_LAYER PickLayer()
		{
			if (g_pickLayerChosen) return g_pickLayer;
			g_pickLayerChosen = true;
			auto* filter = RE::bhkCollisionFilter::GetSingleton();
			if (!filter) { SKSE::log::warn("Pick: no collision filter, proxies keep their own layer"); return g_pickLayer; }
			auto hits = [&](RE::COL_LAYER a_a, RE::COL_LAYER a_b) {
				const auto a = static_cast<int>(a_a), b = static_cast<int>(a_b);
				return ((filter->layerBitfields[a] >> b) & 1) != 0 || ((filter->layerBitfields[b] >> a) & 1) != 0;
			};
			using L = RE::COL_LAYER;
			const L candidates[] = { L::kTransparentSmallAnim, L::kTransparentSmall, L::kDebrisSmall, L::kDebrisLarge, L::kWeapon,
				L::kClutter, L::kProps, L::kTransparent, L::kTrap, L::kAnimStatic, L::kStatic };
			std::string table;
			for (L c : candidates) {
				table += std::format(" {}:pick={} cc={} cam={}", static_cast<int>(c), hits(L::kItemPick, c), hits(L::kCharController, c), hits(L::kCameraSphere, c));
			}
			SKSE::log::info("Pick: collision matrix (layer: crosshair ray / character controller / camera):{}", table);
			for (int pass = 0; pass < 2 && g_pickLayer == L::kUnidentified; ++pass) {
				for (L c : candidates) {
					if (hits(L::kItemPick, c) && !hits(L::kCharController, c) && (pass == 1 || !hits(L::kCameraSphere, c))) { g_pickLayer = c; break; }
				}
			}
			// v1.0.15: none of the usual ones qualified in the v1.0.14 log, so look at every layer the crosshair ray hits.
			if (g_pickLayer == L::kUnidentified) {
				std::string all;
				for (int i = 1; i < 54; ++i) {
					const L c = static_cast<L>(i);
					if (c == L::kItemPick || c == L::kCharController || c == L::kNonCollidable) continue;
					if (!hits(L::kItemPick, c)) continue;
					all += std::format(" {}:cc={} cam={}", i, hits(L::kCharController, c), hits(L::kCameraSphere, c));
					if (g_pickLayer == L::kUnidentified && !hits(L::kCharController, c) && !hits(L::kCameraSphere, c)) g_pickLayer = c;
				}
				SKSE::log::info("Pick: every layer the crosshair ray hits:{}", all);
			}
			if (g_pickLayer == L::kUnidentified) SKSE::log::warn("Pick: no layer is hit by the crosshair and passed by the player; proxies keep their own layer (they may block)");
			else SKSE::log::info("Pick: proxies use collision layer {}", static_cast<int>(g_pickLayer));
			return g_pickLayer;
		}

		void DespawnPickProxy(PickProxy& a_p)
		{
			if (auto ref = a_p.handle.get()) {
				SKSE::log::info("Delete: {} pick proxy {:08X}", a_p.tag, ref->GetFormID());  // crash breadcrumb
				ref->Disable();
				ref->SetDelete(true);
			}
			a_p.handle.reset();
			a_p.set = false;
		}

		void DespawnPickProxies()
		{
			for (auto& p : g_pick) DespawnPickProxy(p);
		}

		// Models a proxy may use, in order. A bench: its own prop's model. The chest (B3): the chest prop's model has its
		// collision stripped in memory (StripPhysics on the prop also strips the cached model, so every later chest proxy came
		// out with no collision), so other chest models come first: containers whose model path has "chest" (shortest first),
		// then the bench model, which is never stripped.
		std::vector<const RE::TESBoundObject*> ProxyModels(const PickProxy& a_p, RE::TESBoundObject* a_own)
		{
			if (&a_p != &g_pick[kPickChest]) return { a_own };
			static std::vector<const RE::TESBoundObject*> chestModels;
			static bool scanned = false;
			if (!scanned && g_chestProp.base) {
				scanned = true;
				auto lower = [](std::string a_s) { for (auto& c : a_s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return a_s; };
				auto* own = skyrim_cast<RE::TESModel*>(g_chestProp.base);
				const std::string ownPath = own && own->GetModel() ? lower(own->GetModel()) : std::string{};
				std::vector<std::pair<std::string, const RE::TESBoundObject*>> found;
				if (auto* dh = RE::TESDataHandler::GetSingleton()) {
					for (auto* cont : dh->GetFormArray<RE::TESObjectCONT>()) {
						const char* mp = cont ? cont->GetModel() : nullptr;
						if (!mp || !*mp) continue;
						const std::string path = lower(mp);
						if (path == ownPath || path.find("chest") == std::string::npos) continue;
						bool dup = false;
						for (auto& f : found) if (f.first == path) dup = true;
						if (!dup) found.emplace_back(path, cont);
					}
				}
				std::stable_sort(found.begin(), found.end(), [](const auto& x, const auto& y) { return x.first.size() < y.first.size(); });
				for (std::size_t i = 0; i < found.size() && i < 8; ++i) chestModels.push_back(found[i].second);
				SKSE::log::info("Pick: {} chest model candidate(s) for the chest proxy{}", chestModels.size(), chestModels.empty() ? "" : std::format(", first '{}'", found[0].first));
			}
			std::vector<const RE::TESBoundObject*> out = chestModels;
			if (g_plankProp.base) out.push_back(g_plankProp.base);
			return out;
		}

		// Keeps one proxy at a_pos / a_rot (scale a_scale) with the model of a_model (the prop's base). Only while parked.
		void TendPickProxy(PickProxy& a_p, RE::TESBoundObject* a_model, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot, float a_scale)
		{
			if (!a_p.base || !a_model) return;
			auto ref = a_p.handle.get();
			if (ref && (ref->IsDeleted() || (ref->GetPosition() - a_pos).Length() > 2.0f)) { DespawnPickProxy(a_p); ref = nullptr; }  // tuned or re-parked: place again
			if (!ref) {
				a_p.handle.reset();
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player || !PlayerReadyForSpawn()) return;
				// The model is copied from a candidate each time (the bench model can be changed with oas plank).
				const auto models = ProxyModels(a_p, a_model);
				if (models.empty()) return;
				if (a_p.modelIdx >= static_cast<int>(models.size())) {
					if (a_p.modelIdx == static_cast<int>(models.size())) SKSE::log::warn("Pick: {} proxy: none of {} model(s) has collision; it cannot be picked", a_p.tag, models.size());
					a_p.modelIdx = static_cast<int>(models.size()) + 1;
					return;
				}
				const RE::TESBoundObject* src = models[a_p.modelIdx];
				auto* srcModel = skyrim_cast<RE::TESModel*>(const_cast<RE::TESBoundObject*>(src));
				if (!srcModel || !srcModel->GetModel() || !*srcModel->GetModel()) { ++a_p.modelIdx; return; }
				a_p.base->SetModel(srcModel->GetModel());
				a_p.base->boundData = src->boundData;
				SKSE::log::info("Spawn: {} pick proxy, placing (model '{}', candidate {} of {})", a_p.tag, srcModel->GetModel(), a_p.modelIdx + 1, models.size());  // crash breadcrumb
				auto placed = player->PlaceObjectAtMe(a_p.base, false);
				if (!placed) return;
				placed->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::clamp(a_scale, 0.01f, 10.0f) * 100.0f + 0.5f);
				placed->SetPosition(a_pos);
				placed->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, std::atan2(a_rot.entry[0][1], a_rot.entry[1][1]) });
				a_p.handle = placed->GetHandle();
				a_p.placedAt = a_pos;
				a_p.set = false;
				TrackSpawn(placed.get(), std::format("{} pick proxy", a_p.tag));  // 31.1
				return;
			}
			if (a_p.set) return;
			auto* root = ref->Get3D();
			if (!root) return;
			a_p.set = true;
			root->SetAppCulled(true);  // invisible: the prop draws the bench; the proxy is only something to look at
			if (!HasCollision(root)) {
				SKSE::log::warn("Pick: {} proxy model has no collision, the crosshair cannot hit it; trying the next model", a_p.tag);
				++a_p.modelIdx;
				DespawnPickProxy(a_p);
				return;
			}
			if (const auto layer = PickLayer(); layer != RE::COL_LAYER::kUnidentified) root->SetCollisionLayer(layer);
			SKSE::log::info("Pick: {} proxy {:08X} ready at ({:.0f},{:.0f},{:.0f}), layer {}", a_p.tag, ref->GetFormID(), a_pos.x, a_pos.y, a_pos.z, static_cast<int>(root->GetCollisionLayer()));
		}

		// The proxies on the parked boat, from its unrocked frame (the ref's own position and heading).
		void TendPickProxies(const RE::NiPoint3& a_vehicle, const RE::NiMatrix3& a_rot)
		{
			if (!g_pickAllowed) { DespawnPickProxies(); return; }
			RE::NiPoint3 pos;
			RE::NiMatrix3 rot;
			PropPlacement(g_plank, a_vehicle, a_rot, pos, rot);
			TendPickProxy(g_pick[kPickHelm], g_plankProp.base, pos, rot, g_plank.scale);
			PropPlacement(g_rowBench, a_vehicle, a_rot, pos, rot);
			TendPickProxy(g_pick[kPickRow], g_rowBenchProp.base, pos, rot, g_rowBench.scale);
			const PlankTune chest{ g_chest.offset, g_chest.yaw, g_chest.scale };
			PropPlacement(chest, a_vehicle, a_rot, pos, rot);
			TendPickProxy(g_pick[kPickChest], g_chestProp.base, pos, rot, g_chest.scale);
		}

		bool IsOurBoatBase(const RE::TESForm* a_base)
		{
			return a_base && (a_base->GetFormID() == kBoatStatic || (g_actiBoat && a_base == g_actiBoat));
		}
	}

	void PrepareVanillaHull()
	{
		const auto data = GameDataDir();
		const auto out = data / "meshes" / kVanillaHullModel;
		std::vector<std::uint8_t> bytes;
		std::string errs, from;
		for (const char* bsa : { "Skyrim - Meshes0.bsa", "Skyrim - Meshes1.bsa" }) {
			std::string err;
			if (ReadFromBsa(data / bsa, kHullArchiveDir, kHullArchiveFile, bytes, err)) {
				from = bsa;
				break;
			}
			errs += std::format("{}{}: {}", errs.empty() ? "" : "; ", bsa, err);
			bytes.clear();
		}
		std::error_code ec;
		if (from.empty()) {
			g_hullCopyReady = std::filesystem::exists(out, ec);  // a copy from an earlier run is still the vanilla hull
			SKSE::log::warn("Mesh: vanilla ShipRowBoat01.nif not extracted ({}){}", errs,
				g_hullCopyReady ? ", using the copy from an earlier run" : ", the boat uses the game path");
			return;
		}
		if (std::filesystem::exists(out, ec)) {
			std::ifstream in(out, std::ios::binary);
			const std::vector<std::uint8_t> have((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (have == bytes) {
				g_hullCopyReady = true;
				SKSE::log::info("Mesh: vanilla copy already present ({} bytes, same as in {})", bytes.size(), from);
				return;
			}
		}
		std::filesystem::create_directories(out.parent_path(), ec);
		std::ofstream o(out, std::ios::binary | std::ios::trunc);
		o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		o.close();
		if (!o) {
			g_hullCopyReady = false;
			SKSE::log::error("Mesh: could not write '{}', the boat uses the game path", out.string());
			return;
		}
		g_hullCopyReady = true;
		SKSE::log::info("Mesh: vanilla ShipRowBoat01.nif extracted ({} bytes, from {}) to '{}'", bytes.size(), from, out.string());
	}

	void CheckBoatTextures()
	{
		const auto data = GameDataDir();
		for (auto& t : g_boatTex) {
			for (int p = 0; p < kPaintCount; ++p) {
				if (!t.ours[p]) continue;
				std::error_code ec;
				t.found[p] = std::filesystem::exists(data / t.ours[p], ec);
				SKSE::log::info("Tex: shape {} diffuse -> {} ({}){}", t.shape, t.ours[p], t.found[p] ? "found" : "missing", t.found[p] ? "" : ", keeps the vanilla texture");
			}
		}
	}

	std::string BoatTextureStatus()
	{
		std::string line;
		for (const auto& t : g_boatTex) {
			const bool ours = t.ours[g_paint] && t.found[g_paint];
			line += std::format("{}{}={}", line.empty() ? "" : " ", t.slot, ours ? kPaintNames[g_paint] : "vanilla");
		}
		return line;
	}

	int Paint() { return g_paint; }
	const char* PaintName(int a_paint) { return a_paint >= 0 && a_paint < kPaintCount ? kPaintNames[a_paint] : "?"; }

	int ParsePaint(std::string_view a_name)
	{
		for (int p = 0; p < kPaintCount; ++p) {
			if (IEquals(a_name, kPaintNames[p])) return p;
		}
		return -1;
	}

	std::string PaintFiles(int a_paint)
	{
		if (a_paint == kPaintVanilla) return "no files, as the game ships";
		const auto& side = g_boatTex[1];
		const auto& hull = g_boatTex[2];
		return std::format("side {}, hull {}", side.found[a_paint] ? "found" : "missing", hull.found[a_paint] ? "found" : "missing");
	}

	void SetPaint(int a_paint, const char* a_why)
	{
		if (a_paint < 0 || a_paint >= kPaintCount) return;
		g_paint = a_paint;
		g_texDone.clear();  // 44.4.1: a new paint is applied again
		SKSE::log::info("Paint: {} ({}) ({})", kPaintNames[a_paint], PaintFiles(a_paint), a_why);
		for (int i = 1; i <= 2; ++i) {
			const auto& t = g_boatTex[i];
			if (t.ours[a_paint] && !t.found[a_paint]) SKSE::log::info("Paint: {} file {} missing, the {} keeps the vanilla texture", kPaintNames[a_paint], t.ours[a_paint], t.slot);
		}
		// The boat's 3D picks it up on its next update (ApplyBoatTextures runs every frame on the live and parked boat),
		// and right now when it is loaded.
		for (auto* handle : { &g_parked, &g_cur.handle, &g_next.handle }) {
			if (auto ref = handle->get(); ref && ref->Get3D()) ApplyBoatTextures(ref.get(), ref->Get3D());
		}
	}

	const char* HullMeshStatus()
	{
		return g_hullBilge ? "bilge floor" : g_hullUsingCopy ? "vanilla copy" : "game path";
	}

	bool InitPickForms()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh || !dh->LookupLoadedLightModByName(kEslName)) {
			SKSE::log::warn("Pick: {} is not loaded (not in the load order?): no Board / Steer / Row prompts, the boat is boarded with oas only", kEslName);
			return false;
		}
		g_actiBoat = dh->LookupForm<RE::TESObjectACTI>(0x800, kEslName);
		for (auto& p : g_pick) p.base = dh->LookupForm<RE::TESObjectACTI>(p.localId, kEslName);
		auto* stat = RE::TESForm::LookupByID<RE::TESObjectSTAT>(kBoatStatic);
		if (g_actiBoat && stat && stat->GetModel()) {
			ApplyHullModel(g_actiBoat, "data loaded");  // 29: the vanilla copy, or the boat static's model
			g_actiBoat->boundData = stat->boundData;
		}
		SKSE::log::info("Pick: {} loaded: Boat {:08X} (model '{}'), Helm {:08X}, Oars {:08X}, Chest {:08X}", kEslName,
			g_actiBoat ? g_actiBoat->GetFormID() : 0u, g_actiBoat && g_actiBoat->GetModel() ? g_actiBoat->GetModel() : "",
			g_pick[0].base ? g_pick[0].base->GetFormID() : 0u, g_pick[1].base ? g_pick[1].base->GetFormID() : 0u, g_pick[2].base ? g_pick[2].base->GetFormID() : 0u);
		return g_actiBoat != nullptr;
	}

	const char* PickTag(const RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) return nullptr;
		if (g_parked && g_parked.get().get() == a_ref) return "Boat";
		for (const auto& p : g_pick) if (p.handle && p.handle.get().get() == a_ref) return p.tag;
		if (g_chestProp.cur.handle && g_chestProp.cur.handle.get().get() == a_ref) return "Chest";
		return nullptr;
	}

	bool ParkedSolid()
	{
		auto ref = g_parked.get();
		auto* root = ref ? ref->Get3D() : nullptr;
		return root && HasCollision(root);
	}

	RE::TESObjectREFR* ChestRef()
	{
		return g_chestProp.cur.handle.get().get();
	}

	void SetPickAllowed(bool a_allowed)
	{
		g_pickAllowed = a_allowed;
		if (!a_allowed) DespawnPickProxies();
	}

	void CleanupPickProxies()
	{
		// Proxies left in a save (the game saved while parked): remove them, ours are placed again.
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!player || !tes) return;
		int removed = 0;
		tes->ForEachReferenceInRange(player, 8000.0f, [&](RE::TESObjectREFR* a_ref) {
			auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!base) return RE::BSContainer::ForEachResult::kContinue;
			for (auto& p : g_pick) {
				if (p.base && base == p.base && !(p.handle && p.handle.get().get() == a_ref)) {
					a_ref->Disable();
					a_ref->SetDelete(true);
					++removed;
				}
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		if (removed) SKSE::log::info("Pick: removed {} stray pick proxy ref(s)", removed);
	}

	void RemoveParkedImpl(bool a_keepProps)
	{
		if (g_parked) {
			if (auto ref = g_parked.get()) {
				if ((ref->GetFormID() >> 24) == 0xFF) {
					SKSE::log::info("Delete: parked boat ref {:08X}", ref->GetFormID());  // crash breadcrumb (v1.0.13)
					ref->Disable();
					ref->SetDelete(true);
				} else {
					// B9 (v1.0.15): a boat placed by the game (or another mod) is never deleted, that would change the save for good.
					SKSE::log::info("BoatBow: {:08X} is not one of ours, left alone", ref->GetFormID());
				}
			}
			g_parked.reset();
		}
		if (!a_keepProps) DespawnProps();  // 56.28: kept at an unboard hand-off
		DespawnPickProxies();
	}

	void RemoveParked() { RemoveParkedImpl(false); }

	void ForgetParked()
	{
		g_parked.reset();
		for (auto& p : g_pick) { p.handle.reset(); p.set = false; }
		ForgetProp(g_plankProp);
		ForgetProp(g_rowBenchProp);
		ForgetProp(g_chestProp);
		ForgetProp(g_oarProp[0]);
		ForgetProp(g_oarProp[1]);
		ForgetProp(g_lampProp[0]);
		ForgetProp(g_lampProp[1]);
		g_lampLight[0] = Slot{};
		g_lampLight[1] = Slot{};
		g_lampCleanup = true;
		g_lampState[0] = LampState{};
		g_lampState[1] = LampState{};
	}

	void Abandon()
	{
		g_rigidValid = false;
		g_active = false;
		g_oarOut = 0.0f;  // 50: a loaded parked boat shows its oars stowed at once
		g_oarStowDelay = 0.0f;
		g_cur = Slot{};
		g_next = Slot{};
		ForgetParked();
		ForgetProp(g_plankProp);
		ForgetProp(g_rowBenchProp);
		ForgetProp(g_chestProp);
		g_rowExternal = false;
		g_rowActive = false;
		g_roll = g_pitch = g_impulse = g_pitchVel = g_airTime = 0.0f;
		SKSE::log::info("BoatBow: abandoned (handles dropped, nothing touched)");
	}

	void CleanupStrays() { CleanupStrayLamps(); }

	bool GetParked(float& a_x, float& a_y, float& a_z, float& a_yaw)
	{
		if (!g_parked) {
			return false;
		}
		if (!g_parked.get()) {
			g_parked.reset();
			return false;
		}
		a_x = g_parkedX; a_y = g_parkedY; a_z = g_parkedZ; a_yaw = g_parkedYaw;
		return true;
	}

	void UpdateParked(float a_dt)
	{
		TendPendingDeletes(a_dt);  // 51.1
		if (g_active || a_dt <= 0.0f) return;  // while skimming, Update() looks after the props
		g_oarSpeed = 0.0f;
		auto ref = g_parked.get();
		// 56.28: the unboard hand-off: the old hull goes 2 frames after the parked boat's 3D is drawn (held opaque), or after 3 s.
		if (g_leaving.handle) {
			g_leavingTime += a_dt;
			auto* proot = ref ? ref->Get3D() : nullptr;
			if (proot) { KeepOpaque(proot, "Parked boat"); ++g_leavingFrames; }
			if (g_leavingFrames >= 2 || g_leavingTime > 3.0f) {
				SKSE::log::info("Handoff: old hull removed ({}, {:.2f} s)", g_leavingFrames >= 2 ? "parked boat drawn" : "timed out", g_leavingTime);
				Despawn(g_leaving);
				if (!ref) { DespawnProps(); SKSE::log::info("Handoff: no parked boat, props removed"); }
			}
		}
		if (!ref) return;
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !ref->Get3D()) return;
		KeepOpaque(ref->Get3D(), "Parked boat");  // 56.28
		ApplyFloorOffset(ref->Get3D());  // 56.35
		LogHullBoundsOnce(ref.get(), ref->Get3D());
		ApplyBoatTextures(ref.get(), ref->Get3D());  // 30
		const RE::NiPoint3 bp = ref->GetPosition();
		const RE::NiPoint3 pp = player->GetPosition();
		const float dx = pp.x - bp.x, dy = pp.y - bp.y;
		if (dx * dx + dy * dy > 2500.0f * 2500.0f) return;  // only while the player is near the boat

		// The boat sits at the vehicle point shifted by its offset (pivoting about the hips), so work back to it.
		const RE::NiMatrix3 rot = MakeMat(std::cos(ref->GetAngleZ()), std::sin(ref->GetAngleZ()), 0.0f,
			-std::sin(ref->GetAngleZ()), std::cos(ref->GetAngleZ()), 0.0f, 0.0f, 0.0f, 1.0f);
		const RE::NiPoint3 off = rot * RE::NiPoint3{ g_offset.x, g_offset.y, g_offset.z + kRollPivotDrop };
		RE::NiPoint3 vehicle{ bp.x - off.x, bp.y - off.y, bp.z + kRollPivotDrop - off.z };
		TendPickProxies(vehicle, rot);  // v1.0.14: from the unrocked frame (the proxies' collision cannot rock)

		// Moored boats rock too: the same slow idle motion as when riding, written to the boat's 3D root (the
		// reference itself stays exactly where it was parked, so boarding, saving and persistence are unaffected).
		g_time += a_dt;
		RE::NiMatrix3 worldRot = rot;
		if (g_idleBob > 0.0f) {
			float rl = 0.0f, pt = 0.0f, hv = 0.0f;
			IdleRock(g_time, 1.0f, rl, pt, hv);
			vehicle.z += hv;
			const float cp = std::cos(pt), sp = std::sin(pt);
			const float cr = std::cos(rl), sr = std::sin(rl);
			const RE::NiMatrix3 rx = MakeMat(1.0f, 0.0f, 0.0f, 0.0f, cp, -sp, 0.0f, sp, cp);
			const RE::NiMatrix3 ry = MakeMat(cr, 0.0f, sr, 0.0f, 1.0f, 0.0f, -sr, 0.0f, cr);
			worldRot = rot * (rx * ry);
			const RE::NiPoint3 ro = worldRot * RE::NiPoint3{ g_offset.x, g_offset.y, g_offset.z + kRollPivotDrop };
			const RE::NiPoint3 worldPos{ vehicle.x + ro.x, vehicle.y + ro.y, vehicle.z - kRollPivotDrop + ro.z };
			if (auto* root = ref->Get3D()) {
				RE::NiMatrix3 parentRotInv = MakeMat(1, 0, 0, 0, 1, 0, 0, 0, 1);
				RE::NiPoint3 parentPos{};
				float parentScale = 1.0f;
				if (auto* parent = root->parent) {
					parentRotInv = parent->world.rotate.Transpose();
					parentPos = parent->world.translate;
					parentScale = parent->world.scale > 0.0f ? parent->world.scale : 1.0f;
				}
				root->local.rotate = parentRotInv * worldRot;
				root->local.translate = (parentRotInv * (worldPos - parentPos)) / parentScale;
				root->local.scale = g_scale / parentScale;
				RE::NiUpdateData data{};
				root->Update(data);
			}
		}
		FollowProps(a_dt, vehicle, worldRot);
	}

	void BoatCenterFromVehicle(float a_vx, float a_vy, float a_yaw, float& a_bx, float& a_by)
	{
		const float s = std::sin(a_yaw), c = std::cos(a_yaw);
		a_bx = a_vx + c * g_offset.x + s * g_offset.y;
		a_by = a_vy - s * g_offset.x + c * g_offset.y;
	}

	void VehicleFromBoatCenter(float a_bx, float a_by, float a_yaw, float& a_vx, float& a_vy)
	{
		const float s = std::sin(a_yaw), c = std::cos(a_yaw);
		a_vx = a_bx - (c * g_offset.x + s * g_offset.y);
		a_vy = a_by - (-s * g_offset.x + c * g_offset.y);
	}

	bool AttachParked()
	{
		auto ref = g_parked.get();
		if (!ref) {
			return false;
		}
		g_active = true;
		g_roll = g_pitch = g_impulse = g_pitchVel = g_airTime = 0.0f;
		g_respawnCooldown = 0.0f;
		g_spawnRetry = 0.0f;
		g_spawnFailures = 0;
		g_loggedLoaded = false;
		g_missingTime = 0.0f;
		Despawn(g_next);
		Despawn(g_leaving);  // 56.28
		DespawnPickProxies();
		g_cur = Slot{};
		g_cur.handle = g_parked;
		g_cur.spawnPos = ref->GetPosition();
		g_parked.reset();
		ref->SetCollision(false);  // boarded: the boat no longer blocks anything
		// 56.27: SetCollision(false) only sets the ref's 'collisions disabled' flag; the Havok body already in the world stays
		// until the 3D reloads, and it shoved the player's and the crew's feet as the boat rowed away. Take it out now:
		// layer to NonCollidable first (nothing collides with it even if a body survives), then detach the Havok bodies.
		// Safe: unboarding spawns a fresh solid parked boat (SpawnParked), this ref never needs its collision back.
		if (auto* root = ref->Get3D()) {
			const bool hadBody = HasCollision(root);
			root->SetCollisionLayer(RE::COL_LAYER::kNonCollidable);
			const bool detached = ref->DetachHavok(root);
			SKSE::log::info("BoatBow: boarding collision off at once (body before {}, layer NonCollidable, DetachHavok {}, body after {})",
				hadBody ? 1 : 0, detached ? 1 : 0, HasCollision(root) ? 1 : 0);
		} else {
			SKSE::log::info("BoatBow: boarding collision off (no 3D yet, flag only)");
		}
		SKSE::log::info("BoatBow: boarded parked boat {:08X}", ref->GetFormID());
		LogTuning();
		return true;
	}

	bool AdoptNearby(const RE::NiPoint3& a_from, float a_range)
	{
		if (g_parked.get()) {
			return true;
		}
		auto* tes = RE::TES::GetSingleton();
		auto* cell = tes ? tes->GetCell(a_from) : nullptr;
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!cell || !cell->IsAttached() || !player) {
			return false;
		}
		RE::TESObjectREFR* best = nullptr;
		float bestD2 = a_range * a_range;
		// B9 (v1.0.15): every loaded cell in range, not only the player's own (our boat in the next cell was missed).
		tes->ForEachReferenceInRange(player, a_range,
			[&](RE::TESObjectREFR* a_ref) -> RE::BSContainer::ForEachResult {
				if (!a_ref || a_ref->IsDisabled() || a_ref->IsDeleted()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto* base = a_ref->GetBaseObject();
				if (!IsOurBoatBase(base) || (a_ref->GetFormID() >> 24) != 0xFF) {
					return RE::BSContainer::ForEachResult::kContinue;  // B9: only boats we placed (FF ids), never a vanilla placed boat
				}
				// Not the boat we are currently driving, and never a lake boat (51).
				if ((g_cur.handle && g_cur.handle.get().get() == a_ref) || IsLakeRef(a_ref)) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const RE::NiPoint3 d = a_ref->GetPosition() - a_from;
				const float d2 = d.x * d.x + d.y * d.y;
				if (d2 < bestD2) {
					bestD2 = d2;
					best = a_ref;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		if (!best) {
			return false;
		}
		const RE::NiPoint3 pos = best->GetPosition();
		g_parked = best->GetHandle();
		g_parkedX = pos.x; g_parkedY = pos.y; g_parkedZ = pos.z; g_parkedYaw = best->GetAngleZ();
		SKSE::log::info("BoatBow: adopted rowboat {:08X} at ({:.0f},{:.0f},{:.0f}) yaw={:.2f}", best->GetFormID(), pos.x, pos.y, pos.z, g_parkedYaw);
		CleanupPickProxies();
		// v1.0.14: one of our own boats from before the activator (a plain static, spawned by us: FF id) is swapped for
		// the activator boat at the same spot, so its hull gets the Board prompt. A boat that came with the game is kept.
		if (g_actiBoat && best->GetBaseObject() != g_actiBoat && (best->GetFormID() >> 24) == 0xFF) {
			SKSE::log::info("BoatBow: replacing the old static boat with the Board activator boat");
			const float yaw = g_parkedYaw;
			return SpawnParked(pos.x, pos.y, pos.z, yaw);
		}
		return true;
	}

	RE::FormID ParkedRefId()
	{
		auto ref = g_parked.get();
		return ref ? ref->GetFormID() : 0;
	}

	// B9: the parked boat from the co-save, by its ref id. False if it is gone, not ours, or not loaded yet.
	bool AdoptRef(RE::FormID a_id)
	{
		if (g_parked.get()) return true;
		auto* ref = a_id ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id) : nullptr;
		if (!ref || ref->IsDeleted() || ref->IsDisabled() || !IsOurBoatBase(ref->GetBaseObject()) || (a_id >> 24) != 0xFF) return false;
		auto* cell = ref->GetParentCell();
		if (!cell || !cell->IsAttached()) return false;
		const RE::NiPoint3 pos = ref->GetPosition();
		g_parked = ref->GetHandle();
		g_parkedX = pos.x; g_parkedY = pos.y; g_parkedZ = pos.z; g_parkedYaw = ref->GetAngleZ();
		SKSE::log::info("BoatBow: adopted our parked boat {:08X} from the co-save at ({:.0f},{:.0f},{:.0f})", a_id, pos.x, pos.y, pos.z);
		CleanupPickProxies();
		if (g_actiBoat && ref->GetBaseObject() != g_actiBoat) {
			SKSE::log::info("BoatBow: replacing the old static boat with the Board activator boat");
			return SpawnParked(pos.x, pos.y, pos.z, g_parkedYaw);
		}
		return true;
	}

	// B9 (3): after a load, our own leftover boats around the player (old parked boats, ride boats from an old save) except
	// the parked one. Vanilla boats are never touched.
	void CleanupOldBoats()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!player || !tes) return;
		auto parked = g_parked.get();
		std::vector<RE::TESObjectREFR*> old;
		tes->ForEachReferenceInRange(player, 9000.0f, [&](RE::TESObjectREFR* a_ref) {
			if (a_ref && (a_ref->GetFormID() >> 24) == 0xFF && a_ref != parked.get() && !a_ref->IsDeleted() && IsOurBoatBase(a_ref->GetBaseObject()) && !IsLakeRef(a_ref)
				&& !(g_cur.handle && g_cur.handle.get().get() == a_ref) && !(g_next.handle && g_next.handle.get().get() == a_ref)
				&& !(g_leaving.handle && g_leaving.handle.get().get() == a_ref)) old.push_back(a_ref);  // 56.53: not the hand-off hull either
			return RE::BSContainer::ForEachResult::kContinue;
		});
		for (auto* r : old) {
			SKSE::log::info("Delete: old boat ref {:08X} left by an earlier session", r->GetFormID());
			r->Disable();
			r->SetDelete(true);
		}
		CleanupPickProxies();
		CleanupStrayLamps();
		if (!old.empty()) SKSE::log::info("BoatBow: removed {} old boat ref(s)", old.size());
	}

	namespace
	{
		bool IsLivePropRef(const RE::TESObjectREFR* a_ref)
		{
			if (a_ref && IsLakeRef(a_ref)) return true;  // 51: a lake boat's hull, oars, lantern or crew
			auto is = [&](const Slot& a_slot) { return a_slot.handle && a_slot.handle.get().get() == a_ref; };
			for (const Prop* pr : { &g_plankProp, &g_rowBenchProp, &g_chestProp, &g_oarProp[0], &g_oarProp[1], &g_lampProp[0], &g_lampProp[1] }) {
				if (is(pr->cur) || is(pr->next)) return true;
			}
			for (const auto& s : g_lampLight) {
				if (is(s)) return true;
			}
			for (const auto& pk : g_pick) {
				if (pk.handle && pk.handle.get().get() == a_ref) return true;
			}
			if (g_parked && g_parked.get().get() == a_ref) return true;
			return is(g_cur) || is(g_next) || is(g_leaving);  // 56.28
		}

		// Disable first, then delete (as every other cleanup here). A container that still holds items is kept: the next
		// chest we spawn takes them (AdoptOldChests) and deletes it then.
		bool DeleteOldProp(RE::TESObjectREFR* a_ref, std::string_view a_type)
		{
			if (a_ref->As<RE::Actor>()) {  // 51: a lake boat's crew from the last session
				SKSE::log::info("Cleanup: old {} actor {:08X} disabled, deleted in 2 s", a_type, a_ref->GetFormID());
				DeleteActorLater(a_ref);
				return true;
			}
			const int items = CountItems(a_ref);
			const auto pos = a_ref->GetPosition();
			if (items > 0) {
				// 43.4.3: with a current chest the items go there and the old one is deleted; otherwise hidden and kept.
				if (auto cur = g_chestProp.cur.handle.get(); cur && cur.get() != a_ref && !cur->IsDeleted()) {
					const int n = MoveAllItems(a_ref, cur.get());
					SKSE::log::info("Cleanup: old {} ref {:08X}: {} item(s) moved into the current chest {:08X}, deleted", a_type, a_ref->GetFormID(), n, cur->GetFormID());
					a_ref->Disable();
					a_ref->SetDelete(true);
					return true;
				}
				HideRef(a_ref);
				SKSE::log::info("Cleanup: old {} ref {:08X} holds {} item(s), hidden until the next chest takes them", a_type, a_ref->GetFormID(), items);
				return false;
			}
			SKSE::log::info("Cleanup: deleted old {} ref {:08X} at ({:.0f},{:.0f},{:.0f})", a_type, a_ref->GetFormID(), pos.x, pos.y, pos.z);
			a_ref->Disable();
			a_ref->SetDelete(true);
			return true;
		}

		std::string LowerModel(const RE::TESForm* a_form)
		{
			const auto* model = a_form ? a_form->As<RE::TESModel>() : nullptr;
			std::string m = model && model->GetModel() ? model->GetModel() : "";
			for (auto& ch : m) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			return m;
		}
	}

	std::vector<std::pair<RE::FormID, std::string>> TrackedRefsForSave()
	{
		std::vector<std::pair<RE::FormID, std::string>> out;
		for (const auto* list : { &g_spawnedRefs, &g_staleRefs }) {
			for (const auto& t : *list) {
				auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(t.id);
				if (!ref || ref->IsDeleted()) continue;
				if (std::any_of(out.begin(), out.end(), [&](const auto& o) { return o.first == t.id; })) continue;
				out.push_back({ t.id, t.type });
			}
		}
		return out;
	}

	void ResetSpawnTracking()
	{
		g_spawnedRefs.clear();
		g_staleRefs.clear();
	}

	void SetStaleRefs(const std::vector<std::pair<RE::FormID, std::string>>& a_refs)
	{
		g_staleRefs.clear();
		for (const auto& [id, type] : a_refs) g_staleRefs.push_back({ id, type });
		SKSE::log::info("Cleanup: {} prop ref(s) from the last session read from the co-save", g_staleRefs.size());
	}

	int PurgeStaleRefs(const char* a_when)
	{
		int removed = 0;
		std::vector<TrackedRef> left;
		for (const auto& t : g_staleRefs) {
			auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(t.id);
			if (!ref || ref->IsDeleted() || IsLivePropRef(ref)) continue;
			if (!ref->GetParentCell()) { left.push_back(t); continue; }  // not in memory now: tried again later
			if (DeleteOldProp(ref, t.type)) ++removed;
			else left.push_back(t);
		}
		g_staleRefs = std::move(left);
		SKSE::log::info("Cleanup: removed {} stale prop ref(s) ({}){}", removed, a_when,
			g_staleRefs.empty() ? "" : std::format(", {} kept for later", g_staleRefs.size()));
		return removed;
	}

	int SweepOrphans(bool a_delete, const char* a_when)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		auto* console = RE::ConsoleLog::GetSingleton();
		if (!player || !tes) return 0;
		// What our props look like: the model of every prop form (also catches a plain static twin with the same model),
		// plus the lamp light and pick proxy forms by form.
		struct Kind { std::string model; const RE::TESForm* form; std::string type; };
		std::vector<Kind> kinds;
		auto addForm = [&](const RE::TESForm* a_form, std::string_view a_type, bool a_byModel) {
			if (!a_form) return;
			Kind k{ a_byModel ? LowerModel(a_form) : std::string{}, a_form, std::string(a_type) };
			kinds.push_back(std::move(k));
		};
		for (const Prop* pr : { &g_plankProp, &g_rowBenchProp, &g_chestProp, &g_oarProp[0], &g_oarProp[1], &g_lampProp[0], &g_lampProp[1] }) {
			addForm(pr->base, pr->tag, true);
			if (!pr->text.empty()) addForm(FindPlankBase(pr->text, false), pr->tag, true);
		}
		for (RE::FormID id : { 0x0002D847u, 0x00041EC7u, 0x00050E77u, 0x000F3923u }) addForm(RE::TESForm::LookupByID(id), "Prop", true);
		addForm(LampLightBase(), "LampLight", false);
		for (const auto& pk : g_pick) addForm(pk.base, std::format("{} pick proxy", pk.tag), false);
		const RE::NiPoint3 pp = player->GetPosition();
		std::vector<std::pair<RE::TESObjectREFR*, std::string>> found;
		tes->ForEachReferenceInRange(player, 16000.0f, [&](RE::TESObjectREFR* a_ref) {
			if (!a_ref || (a_ref->GetFormID() >> 24) != 0xFF || a_ref->IsDeleted() || IsLivePropRef(a_ref)) return RE::BSContainer::ForEachResult::kContinue;
			const auto* base = a_ref->GetBaseObject();
			if (!base) return RE::BSContainer::ForEachResult::kContinue;
			const std::string model = LowerModel(base);
			for (const auto& k : kinds) {
				if (base == k.form || (!k.model.empty() && model == k.model)) {
					found.push_back({ a_ref, k.type });
					break;
				}
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		int deleted = 0;
		for (auto& [ref, type] : found) {
			const auto pos = ref->GetPosition();
			const std::string line = std::format("{} {:08X} base {:08X} at ({:.0f},{:.0f},{:.0f}), {:.0f} from the player", type, ref->GetFormID(),
				ref->GetBaseObject()->GetFormID(), pos.x, pos.y, pos.z, (pos - pp).Length());
			SKSE::log::info("Sweep: {}{}", line, a_delete ? "" : " (list only)");
			if (console && std::string_view(a_when) == "oas sweep") console->Print("Oars and Sails sweep: %s", line.c_str());
			if (a_delete && DeleteOldProp(ref, type)) ++deleted;
		}
		const std::string total = std::format("Sweep ({}): {} orphan prop ref(s) found, {} deleted", a_when, found.size(), deleted);
		SKSE::log::info("{}", total);
		if (console && std::string_view(a_when) == "oas sweep") console->Print("Oars and Sails %s", total.c_str());
		return deleted;
	}

	bool SpawnParked(float a_x, float a_y, float a_z, float a_yaw, bool a_keepProps)
	{
		RemoveParkedImpl(a_keepProps);
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return false;
		}
		auto* stat = RE::TESForm::LookupByID<RE::TESObjectSTAT>(kBoatStatic);
		if (!stat) {
			stat = RE::TESForm::LookupByEditorID<RE::TESObjectSTAT>("ShipRowBoat01");
		}
		if (!stat) {
			SKSE::log::error("BoatBow: parked boat static not found");
			return false;
		}
		SKSE::log::info("Spawn: parked boat, placing");  // crash breadcrumb (v1.0.13)
		// v1.0.14: the Board activator (same model) when OarsAndSails.esl is loaded, so the hull shows "[E] Board".
		RE::TESBoundObject* base = g_actiBoat ? static_cast<RE::TESBoundObject*>(g_actiBoat) : stat;
		ApplyHullModel(g_actiBoat, "parked boat spawn");  // 29.3: falls back to the game path if the copy went missing
		auto ref = player->PlaceObjectAtMe(base, false);
		if (!ref) {
			SKSE::log::error("BoatBow: parked boat PlaceObjectAtMe failed");
			return false;
		}
		// Same size as the skim boat, set before its model loads so the collision matches.
		ref->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(g_scale * 100.0f + 0.5f);
		ref->SetPosition(a_x, a_y, a_z);
		ref->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, a_yaw });
		g_parked = ref->GetHandle();
		g_parkedX = a_x; g_parkedY = a_y; g_parkedZ = a_z; g_parkedYaw = a_yaw;
		SKSE::log::info("BoatBow: parked boat {:08X} at ({:.0f},{:.0f},{:.0f}) yaw={:.2f}", ref->GetFormID(), a_x, a_y, a_z, a_yaw);
		return true;
	}

	void NudgeParkedZ(float a_dz)
	{
		auto ref = g_parked.get();
		if (!ref || a_dz == 0.0f) return;
		const RE::NiPoint3 p = ref->GetPosition();
		ref->SetPosition(p.x, p.y, p.z + a_dz);
		g_parkedZ += a_dz;
	}

	void Impact(float a_strength)
	{
		// Small nose pop (rock launch, lip, beach touch). The stern stays put.
		g_pitchVel += std::clamp(a_strength, 0.0f, 1.0f) * 1.2f;
	}

	void Splash(float a_strength)
	{
		// Water landing: the bow slaps in, then kicks up and settles level.
		g_pitchVel += std::clamp(a_strength, 0.0f, 1.0f) * 3.0f;
	}

	RippleTune& Ripples() { return g_ripple; }
	RE::NiPoint3& WakeSideOffset(int a_side) { LoadWakeSideOffsets(); return g_wakeSideOff[a_side == 0 ? 0 : 1]; }
	void SaveWakeSideOffsets()
	{
		for (int i = 0; i < 2; ++i) {
			const auto& o = g_wakeSideOff[i];
			WritePrivateProfileStringA("Ripple", i == 0 ? "Side0" : "Side1", std::format("{:.0f},{:.0f},{:.0f}", o.x, o.y, o.z).c_str(), RippleIniPath().c_str());
		}
	}

	void RippleAt(const RE::NiPoint3& a_pos, float a_scale)
	{
		const bool was = g_rippleOn;
		g_rippleOn = true;
		g_rippleLogs = 0;
		AddRippleAt(a_pos, a_scale);
		g_rippleOn = was;
	}

	void SetRipplesOn(bool a_on) { g_rippleOn = a_on; }

	bool RipplesOn() { return g_rippleOn; }

	float FloorOffset() { LoadFloorOffset(); return g_floorOffset; }
	void SetFloorOffset(float a_offset)
	{
		LoadFloorOffset();
		g_floorOffset = std::clamp(std::round(a_offset * 2.0f) / 2.0f, -30.0f, 20.0f);
		WritePrivateProfileStringA("Boat", "BilgeFloorOffset", std::format("{:.1f}", g_floorOffset).c_str(), FloorIniPath().c_str());
		SKSE::log::info("Bilge: floor offset {:+.1f} (floor at model z {:.1f})", g_floorOffset, 28.0f + g_floorOffset);
	}

	void SetWaterZ(float a_z, bool a_valid)
	{
		g_waterZ = a_z;
		g_waterValid = a_valid;
	}

	void EmitWake(float a_dt, float a_speed01, float a_heading, float a_x, float a_y, float a_waterZ, bool a_onWater)
	{
		g_rippleStatTimer += a_dt;
		if (g_rippleStatTimer >= 3.0f) {
			g_rippleStatTimer = 0.0f;
			if (g_wakeCount + g_oarCount > 0) SKSE::log::info("Ripple stats (3 s): stern trail {}  oar rings {}", g_wakeCount, g_oarCount);
			g_wakeCount = g_oarCount = 0;
		}
		if (!g_rippleOn || !a_onWater) { g_wakeHave = false; return; }
		g_rippleWaterZ = a_waterZ;
		g_rippleWaterAge = 0.0f;
		const float speed = std::clamp(a_speed01, 0.0f, 1.0f);
		if (speed < 0.02f) { g_wakeHave = false; return; }
		// A continuous trail like the one a wading player leaves: a ripple every g_ripple.space units of travel,
		// laid along the path the stern took (so it stays one line however fast the boat goes).
		if (!g_wakeHave) {
			g_wakeHave = true;
			g_wakeLastX = a_x;
			g_wakeLastY = a_y;
			return;
		}
		const float spacing = (std::max)(8.0f, g_ripple.space);
		float dx = a_x - g_wakeLastX, dy = a_y - g_wakeLastY;
		float dist = std::sqrt(dx * dx + dy * dy);
		if (dist > 1500.0f) {  // teleport / cell swap: start the trail over
			g_wakeLastX = a_x;
			g_wakeLastY = a_y;
			return;
		}
		const float fx = std::sin(a_heading), fy = std::cos(a_heading);
		const float rx = fy, ry = -fx;
		int placed = 0;
		while (dist >= spacing && placed < 4) {
			const float inv = 1.0f / dist;
			g_wakeLastX += dx * inv * spacing;
			g_wakeLastY += dy * inv * spacing;
			dx = a_x - g_wakeLastX;
			dy = a_y - g_wakeLastY;
			dist = std::sqrt(dx * dx + dy * dy);
			++placed;
			// Slightly off the centre line, side to side, so neighbouring rings overlap into a trail.
			g_wakeSide = -g_wakeSide;
			// Offset in the boat's frame (right, forward) of the stern point, swung round the boat by the trail yaw.
			const float lat = g_wakeSide * g_ripple.lateral;
			const float cy = std::cos(g_ripple.yaw), sy = std::sin(g_ripple.yaw);
			LoadWakeSideOffsets();
			const RE::NiPoint3& so = g_wakeSideOff[g_wakeSide < 0.0f ? 0 : 1];  // 56.37: this side's own offset
			const float lx = lat * cy + g_ripple.stern * sy + so.x;
			const float ly = lat * sy - g_ripple.stern * cy + so.y;
			const RE::NiPoint3 p{
				g_wakeLastX + rx * lx + fx * ly,
				g_wakeLastY + ry * lx + fy * ly,
				a_waterZ + g_ripple.lift + so.z };
			AddRippleAt(p, g_ripple.wake * (0.7f + 0.6f * speed));
			--g_oarCount;
			++g_wakeCount;
		}
		if (placed == 4) { g_wakeLastX = a_x; g_wakeLastY = a_y; }  // far behind: do not build up a backlog
	}

	bool RigidRotation(RE::NiMatrix3& a_out)
	{
		if (!g_rigidValid) return false;
		a_out = g_rigidRot;
		return true;
	}

	bool RigidPoint(const RE::NiPoint3& a_local, RE::NiPoint3& a_world, RE::NiPoint3& a_forward, RE::NiPoint3& a_right)
	{
		if (!g_rigidValid) return false;
		const RE::NiPoint3 r = g_rigidRot * RE::NiPoint3{ a_local.x, a_local.y, a_local.z + kRollPivotDrop };
		a_world = RE::NiPoint3{ g_rigidPivot.x + r.x, g_rigidPivot.y + r.y, g_rigidPivot.z - kRollPivotDrop + r.z };
		a_forward = g_rigidRot * RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
		a_right = g_rigidRot * RE::NiPoint3{ 1.0f, 0.0f, 0.0f };
		return true;
	}

	void Update(float a_speed01, float a_turnRate, float a_dt,
		float a_vx, float a_vy, float a_vz, float a_heading,
		const Flight& a_flight)
	{
		if (!g_active || a_dt <= 0.0f) {
			return;
		}
		if (g_leaving.handle) Despawn(g_leaving);  // 56.28: re-boarded before the hand-off finished
		g_oarSpeed = std::clamp(a_speed01, 0.0f, 1.0f);

		g_respawnCooldown = (std::max)(0.0f, g_respawnCooldown - a_dt);
		g_spawnRetry = (std::max)(0.0f, g_spawnRetry - a_dt);
		g_refreshQuiet = (std::max)(0.0f, g_refreshQuiet - a_dt);
		if (g_respawnTag) g_respawnAge += a_dt;

		// Lost the reference entirely: bring it back (rate limited).
		if (!g_cur.handle) {
			if (g_spawnRetry > 0.5f) g_spawnRetry = 0.5f;  // 45.1.3: no long waits
			if (g_respawnCooldown <= 0.0f && g_spawnRetry <= 0.0f) {
				g_respawnCooldown = 0.25f;
				static int lostTries = 0;
				if (++lostTries % 5 == 0) SKSE::log::info("BoatBow: boat reference still lost, retry {}", lostTries);
				static bool lostLogged = false;
				if (!lostLogged) {
					lostLogged = true;
					SKSE::log::warn("BoatBow: boat reference lost, respawning (further retries are not logged until it returns)");
				}
				if (Spawn(g_cur)) {
					lostLogged = false;
				}
				g_missingTime = 0.0f;
			}
			return;
		}

		auto ref = g_cur.handle.get();
		if (!ref) {
			g_cur.handle.reset();
			g_respawnCooldown = 0.0f;
			return;
		}
		if (auto* cell = ref->GetParentCell(); ref->IsDeleted() || !cell || !cell->IsAttached()) {
			// 43.3: the boat's cell fell out of the loaded area: move the ref under the boat (it stayed behind before).
			if (!ref->IsDeleted() && g_respawnCooldown <= 0.0f) { g_respawnCooldown = 0.5f; CellHop(a_vx, a_vy, a_vz); }  // 48.1: move, not re-place
			return;
		}
		if (g_flightHold && !g_next.handle) {
			const float fx = a_vx - g_cur.spawnPos.x, fy = a_vy - g_cur.spawnPos.y;
			const float lim = kRefreshDistance * kRefreshForceFactor;
			if (fx * fx + fy * fy > lim * lim) MoveSlot(g_cur, RE::NiPoint3{ a_vx, a_vy, a_vz }, "boat", "flight");
		}

		// Spawn a fresh reference once we've travelled far from where this one was
		// created. It warms up (loads and fades in) on top of the old boat, and only
		// replaces it after kWarmupTime, so the swap is never visible.
		RE::NiAVObject* nextRoot = nullptr;
		if (!g_next.handle) {
			const float dx = a_vx - g_cur.spawnPos.x;
			const float dy = a_vy - g_cur.spawnPos.y;
			if (g_spawnRetry <= 0.0f && RefreshMayBegin(dx * dx + dy * dy, kRefreshDistance * kRefreshDistance, "boat", CellsFromPlayer(ref.get()))) {
				Spawn(g_next);
			}
		} else if (auto nextRef = g_next.handle.get()) {
			nextRoot = nextRef->Get3D();
			const bool forceEnd = g_respawnTag && std::string_view(g_respawnTag) == "boat" && g_respawnAge > kRespawnMaxAge;
			if (!nextRoot && forceEnd) {
				SKSE::log::info("Respawn: forced end boat (the fresh ref never loaded, dropped)");
				Despawn(g_next);
				g_next = Slot{};
				g_respawnTag = nullptr;
			}
			if (nextRoot) {
				ApplyBoatTextures(nextRef.get(), nextRoot);  // 30: the spare is ready before it is swapped in
				if (!g_next.collisionOff) {
					nextRef->SetCollision(false);
					KillHavok(nextRef.get(), "Boat");  // 56.27
					g_next.collisionOff = true;
				}
				g_next.loadedTime += a_dt;
				if (RefreshMaySwap(g_next.loadedTime) || forceEnd) {
					if (forceEnd) SKSE::log::info("Respawn: forced end boat");
					SKSE::log::info("BoatBow: swapped to fresh ref {:08X}", nextRef->GetFormID());
					Despawn(g_cur);
					g_cur = g_next;
					g_next = Slot{};
					g_refreshQuiet = kRefreshQuiet;
					SKSE::log::info("Respawn: end boat");
					g_respawnTag = nullptr;
					nextRoot = nullptr;
					ref = g_cur.handle.get();
					if (!ref) {
						return;
					}
				}
			}
		} else {
			g_next.handle.reset();
		}

		// The engine loads the model a few frames after spawning. If the 3D goes
		// missing for good, respawn at the player.
		RE::NiAVObject* root = ref->Get3D();
		if (root) {
			LogHullBoundsOnce(ref.get(), root);
			ApplyBoatTextures(ref.get(), root);  // 30
			KeepOpaque(root, "Boat");  // 56.28
		}
		if (auto nr = g_next.handle ? g_next.handle.get() : RE::NiPointer<RE::TESObjectREFR>{}) KeepOpaque(nr->Get3D(), "Boat (next)");  // 56.28
		if (!root) {
			g_missingTime += a_dt;
			if (g_missingTime > 0.75f && g_respawnCooldown <= 0.0f) {
				SKSE::log::warn("BoatBow: boat 3D missing for {:.1f}s, respawning", g_missingTime);
				g_respawnCooldown = 1.5f;
				Despawn(g_cur);
				Spawn(g_cur);
			}
			return;
		}
		g_missingTime = 0.0f;
		if (!g_cur.collisionOff) {
			ref->SetCollision(false);
			KillHavok(ref.get(), "Boat");  // 56.27
			g_cur.collisionOff = true;
		}
		if (!g_loggedLoaded) {
			g_loggedLoaded = true;
			SKSE::log::info("BoatBow: boat 3D is loaded, now following the vehicle");
		}

		const float speed = std::clamp(a_speed01, 0.0f, 1.0f);
		g_time += a_dt;

		auto smooth = [](float a_lo, float a_hi, float a_x) {
			const float t = std::clamp((a_x - a_lo) / (a_hi - a_lo), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		};

		// Lean into turns (turn right = lean right).
		// Lean into turns (56.41: back to the original lean, Miguel preferred it to the 56.30 cut).
		const float targetRoll = std::clamp(a_turnRate * 0.35f * speed, -0.25f, 0.25f);
		const float k = 1.0f - std::exp(-6.0f * a_dt);
		g_roll += (targetRoll - g_roll) * k;

		const float afloatNow = 1.0f - std::clamp(a_flight.groundBlend, 0.0f, 1.0f);  // 56.30: no stroke squat aground
		// Pitch is a spring pulled toward a target. The boat pivots about the
		// vehicle point (where the player sits), so only the bow moves.
		float target = 0.0f;
		float stiff = 45.0f;
		float damp = 7.0f;
		if (a_flight.airborne) {
			// Follow the flight path, and nose over further the longer we're up there
			// (a big waterfall drop ends head-first).
			g_airTime += a_dt;
			const float hs = (std::max)(a_flight.horizontalSpeed, 60.0f);
			const float gamma = std::atan2(a_flight.verticalSpeed, hs);
			target = std::clamp(gamma * 0.6f - 0.05f * (std::min)(g_airTime, 2.5f), -0.7f, 0.35f);
			stiff = 14.0f;
			damp = 5.5f;
		} else if (a_flight.onRamp) {
			g_airTime = 0.0f;
			target = std::clamp(a_flight.rampPitch * 0.6f, -0.6f, 0.4f);
			stiff = 30.0f;
			damp = 8.0f;
		} else {
			g_airTime = 0.0f;
			// 56.30: no planing trim (the bow rose at speed like a speedboat). Instead each oar dig squats the stern and lifts
			// the bow a touch (about 0.7 degrees at the peak of the drive), settling back on the recovery.
			target = 0.012f * (StrokeThrust() / 3.14159f) * afloatNow;
		}
		{
			const float h = 0.01f;
			float left = (std::min)(a_dt, 0.1f);
			while (left > 0.0f) {
				const float d = (std::min)(h, left);
				g_pitchVel += (stiff * (target - g_pitch) - damp * g_pitchVel) * d;
				g_pitch += g_pitchVel * d;
				left -= d;
			}
			g_pitch = std::clamp(g_pitch, -1.3f, 0.6f);
		}

		// 56.30: the old speedboat bow bob (a fast 6-9 Hz vibration at full speed) is gone: a rowboat does not chatter.
		const float bobPitch = 0.0f;
		// Idle bob: a floating boat never sits dead still. Slow, uneven rocking (roll about 1.1 degrees, pitch about
		// 0.6 degrees, a couple of units of heave) made from two sines each so it never repeats exactly. Strongest at
		// rest, fading to a quarter while rowing at speed. Not applied in the air or on a ramp.
		float idleRoll = 0.0f, idlePitch = 0.0f, idleHeave = 0.0f;
		if (g_idleBob > 0.0f && !a_flight.airborne && !a_flight.onRamp) {
			IdleRock(g_time, 1.0f - 0.35f * smooth(0.0f, 0.6f, speed), idleRoll, idlePitch, idleHeave);  // 56.30: keeps floating while rowing (was down to a quarter)
		}
		// Aground the boat sits still on the ground (no rocking or bob) and takes the ground's tilt.
		const float afloat = 1.0f - std::clamp(a_flight.groundBlend, 0.0f, 1.0f);
		idleRoll *= afloat;
		idlePitch *= afloat;
		idleHeave *= afloat;
		const float pitchOut = g_pitch + bobPitch * afloat + idlePitch + a_flight.groundPitch;
		const float rollOut = g_roll + idleRoll + a_flight.groundRoll;
		const float vzOut = a_vz + idleHeave;

		// Heading basis built explicitly (no Euler-order guesswork):
		// right = (cos h, -sin h), forward = (sin h, cos h), up = Z.
		const float hMesh = a_heading + g_yaw;
		const float sh = std::sin(hMesh);
		const float ch = std::cos(hMesh);
		const RE::NiMatrix3 base = MakeMat(
			ch, sh, 0.0f,
			-sh, ch, 0.0f,
			0.0f, 0.0f, 1.0f);

		const float cp = std::cos(pitchOut), sp = std::sin(pitchOut);
		const float cr = std::cos(rollOut), sr = std::sin(rollOut);
		const RE::NiMatrix3 rx = MakeMat(1.0f, 0.0f, 0.0f, 0.0f, cp, -sp, 0.0f, sp, cp);
		const RE::NiMatrix3 ry = MakeMat(cr, 0.0f, sr, 0.0f, 1.0f, 0.0f, -sr, 0.0f, cr);
		const RE::NiMatrix3 worldRot = base * (rx * ry);
		g_rigidRot = worldRot;
		g_rigidPivot = RE::NiPoint3{ a_vx, a_vy, vzOut };
		g_rigidValid = true;

		// The offset is rotated with the boat, so pitch/roll swing the bow about the player.
		const RE::NiPoint3 rotatedOffset = worldRot * RE::NiPoint3{ g_offset.x, g_offset.y, g_offset.z + kRollPivotDrop };
		const RE::NiPoint3 worldPos{
			a_vx + rotatedOffset.x,
			a_vy + rotatedOffset.y,
			vzOut - kRollPivotDrop + rotatedOffset.z
		};

		// Apply the world transform to a node (convert to its parent's space).
		auto applyTo = [&](RE::NiAVObject* a_node) {
			RE::NiMatrix3 parentRotInv = MakeMat(1, 0, 0, 0, 1, 0, 0, 0, 1);
			RE::NiPoint3 parentPos{};
			float parentScale = 1.0f;
			if (auto* parent = a_node->parent) {
				parentRotInv = parent->world.rotate.Transpose();
				parentPos = parent->world.translate;
				parentScale = parent->world.scale > 0.0f ? parent->world.scale : 1.0f;
			}
			a_node->local.rotate = parentRotInv * worldRot;
			a_node->local.translate = (parentRotInv * (worldPos - parentPos)) / parentScale;
			a_node->local.scale = g_scale / parentScale;
			a_node->SetAppCulled(false);
			RE::NiUpdateData data{};
			a_node->Update(data);
		};
		applyTo(root);
		if (nextRoot) {
			applyTo(nextRoot);  // warming up: sits exactly on the current boat
		}
		// 56.33: the bilge floor is locked to the hull (no longer moved with the water). 56.35: at its tuned offset.
		ApplyFloorOffset(root);
		if (nextRoot) ApplyFloorOffset(nextRoot);
		g_goodRoot = root->world.translate;
		g_goodVehicle = RE::NiPoint3{ a_vx, a_vy, vzOut };
		g_goodValid = true;
		FollowProps(a_dt, RE::NiPoint3{ a_vx, a_vy, vzOut }, worldRot);

		g_logTimer += a_dt;
		if (g_logTimer >= 3.0f) {
			g_logTimer = 0.0f;
			auto* cell = ref->GetParentCell();
			SKSE::log::info("BoatBow: vehicle=({:.0f},{:.0f},{:.0f}) boat=({:.0f},{:.0f},{:.0f}) culled={} cellAttached={} disabled={}",
				a_vx, a_vy, a_vz, worldPos.x, worldPos.y, worldPos.z, root->GetAppCulled(),
				cell ? cell->IsAttached() : false, ref->IsDisabled());
		}
	}

	// ---- live tuning via numpad -------------------------------------------------

	namespace
	{
		class TuningSink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events,
				RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!g_cur.handle || !a_events) {
					return RE::BSEventNotifyControl::kContinue;
				}

				bool changed = false;
				for (auto* e = *a_events; e; e = e->next) {
					auto* btn = e->AsButtonEvent();
					if (!btn || btn->device != RE::INPUT_DEVICE::kKeyboard || !btn->IsPressed()) {
						continue;
					}
					switch (btn->GetIDCode()) {
					case 0x48: g_offset.y += 2.0f; changed = true; break;   // Num8
					case 0x50: g_offset.y -= 2.0f; changed = true; break;   // Num2
					case 0x4B: g_offset.x -= 2.0f; changed = true; break;   // Num4
					case 0x4D: g_offset.x += 2.0f; changed = true; break;   // Num6
					case 0x49: g_offset.z += 2.0f; changed = true; break;   // Num9
					case 0x51: g_offset.z -= 2.0f; changed = true; break;   // Num3
					case 0x4E: g_scale += 0.01f; changed = true; break;     // Num+
					case 0x4A: g_scale = (std::max)(0.05f, g_scale - 0.01f); changed = true; break;  // Num-
					case 0x47: g_yaw += 0.0873f; changed = true; break;     // Num7 (+5 deg)
					case 0x4F: g_yaw -= 0.0873f; changed = true; break;     // Num1 (-5 deg)
					default: break;
					}
				}

				if (changed) {
					LogTuning();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		TuningSink g_sink;
	}

	void RegisterTuningHotkeys()
	{
		// The numpad now tunes the dog's slot (see Plugin.cpp). The boat keeps its tuned defaults.
		return;
	}
}
