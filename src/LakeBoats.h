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
// Part 51/52: NPC rowboats on the lakes. Unnamed hunters, fishermen and farmers row around a lake, stop to fish, row on;
// a lantern at night. Each lake keeps its boats for at least a game day; only those in the loaded area are drawn.
// Part 54: as many boats as the water is big, never placed where the player can see; the sea has a few near its harbours.

#include <functional>
#include <string>

namespace LakeBoats
{
	// Every frame from the player update (on foot or boating).
	void Update(float a_dt);
	// A game is loading: forget every handle without touching the refs (the co-save purge removes them).
	void OnGameLoading();
	// oas lakehere / lakepoint / lakes / lakedel / lakeboats ...: the console text, or empty when the command is not ours.
	std::string Command(const std::string& a_lower);
	// Part 52: the lakes and their boat populations, kept in the co-save (text). a_resolve maps saved form IDs to this load order.
	std::string SaveState();
	void LoadState(const std::string& a_text, const std::function<RE::FormID(RE::FormID)>& a_resolve);
	// Part 54: the numpad moves every NPC boat as one piece (hull, oars, benches, lantern and crew); kept in the ini.
	bool AnyDrawn();
	bool IsRigKey(std::uint32_t a_key);
	void TuneRig(std::uint32_t a_key, bool a_title);
}
