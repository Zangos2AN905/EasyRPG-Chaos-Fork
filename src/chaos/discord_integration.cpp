/*
 * This file is part of EasyRPG Player (Chaos Fork).
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

#include "discord_integration.h"
#include "../output.h"
#include "../player.h"
#include "../scene.h"
#include "../game_map.h"
#include "../game_party.h"
#include "../game_actor.h"
#include "../main_data.h"

#include <discord_rpc.h>
#include <chrono>
#include <cstring>
#include <ctime>
#include <string>

namespace {

constexpr const char* APPLICATION_ID = "1483181814532149319";

// Rate-limit presence updates to every 5 seconds
using Clock = std::chrono::steady_clock;
Clock::time_point last_update_time{};
constexpr auto UPDATE_INTERVAL = std::chrono::seconds(5);

int64_t start_timestamp = 0;
bool initialized = false;
bool enabled = true;

// Buffers to hold strings while Discord references them
std::string details_buf;
std::string state_buf;

// ── Discord event handlers ─────────────────────────────────────────────────────

void OnReady(const DiscordUser* user) {
	if (user && user->username) {
		Output::Debug("Discord RPC: Connected as {}", user->username);
	} else {
		Output::Debug("Discord RPC: Connected");
	}
}

void OnDisconnected(int errorCode, const char* message) {
	Output::Debug("Discord RPC: Disconnected ({}: {})", errorCode, message ? message : "");
}

void OnErrored(int errorCode, const char* message) {
	Output::Debug("Discord RPC: Error ({}: {})", errorCode, message ? message : "");
}

// ── Helpers ────────────────────────────────────────────────────────────────────

const char* SceneTypeToString(Scene::SceneType type) {
	switch (type) {
		case Scene::Title:       return "Title Screen";
		case Scene::Map:         return "Exploring";
		case Scene::Battle:      return "In Battle";
		case Scene::Menu:        return "In Menu";
		case Scene::Item:        return "Browsing Items";
		case Scene::Skill:       return "Viewing Skills";
		case Scene::Equip:       return "Equipping";
		case Scene::Status:      return "Viewing Status";
		case Scene::Save:        return "Saving";
		case Scene::Load:        return "Loading";
		case Scene::Shop:        return "Shopping";
		case Scene::Name:        return "Naming Character";
		case Scene::Gameover:    return "Game Over";
		case Scene::End:         return "Ending";
		case Scene::Logo:        return "Starting Up";
		case Scene::GameBrowser: return "Browsing Games";
		case Scene::Settings:    return "In Settings";
		default:                 return "Playing";
	}
}

std::string GetPartyInfo() {
	if (!Main_Data::game_party) return {};

	auto actors = Main_Data::game_party->GetActors();
	if (actors.empty()) return {};

	auto* leader = actors[0];
	if (!leader) return {};

	std::string info;
	info += std::string(leader->GetName());
	info += " Lv" + std::to_string(leader->GetLevel());

	if (actors.size() > 1) {
		info += " (+" + std::to_string(actors.size() - 1) + " more)";
	}

	return info;
}

std::string GetMapDetails() {
	int map_id = Game_Map::GetMapId();
	if (map_id <= 0) return {};

	auto name = Game_Map::GetMapName(map_id);
	if (name.empty()) return {};

	return std::string(name);
}

} // anonymous namespace

// ── Public API ─────────────────────────────────────────────────────────────────

void Chaos::DiscordIntegration::Initialize() {
	if (initialized) return;

	DiscordEventHandlers handlers{};
	handlers.ready = OnReady;
	handlers.disconnected = OnDisconnected;
	handlers.errored = OnErrored;

	Discord_Initialize(APPLICATION_ID, &handlers, 1, nullptr);

	start_timestamp = std::time(nullptr);
	initialized = true;

	Output::Debug("Discord RPC: Initialized with App ID {}", APPLICATION_ID);
}

void Chaos::DiscordIntegration::Shutdown() {
	if (!initialized) return;

	Discord_ClearPresence();
	Discord_Shutdown();
	initialized = false;

	Output::Debug("Discord RPC: Shut down");
}

void Chaos::DiscordIntegration::Update() {
	if (!initialized || !enabled) return;

	Discord_RunCallbacks();

	// Rate-limit updates
	auto now = Clock::now();
	if (now - last_update_time < UPDATE_INTERVAL) return;
	last_update_time = now;

	DiscordRichPresence presence{};
	std::memset(&presence, 0, sizeof(presence));

	// Game title
	std::string game_title = Player::game_title;
	if (game_title.empty()) {
		game_title = "EasyRPG Player";
	}

	// Scene-specific details
	Scene::SceneType scene_type = Scene::Null;
	if (Scene::instance) {
		scene_type = Scene::instance->type;
	}

	details_buf = game_title;
	state_buf = SceneTypeToString(scene_type);

	// Add map/party info when on the map
	if (scene_type == Scene::Map || scene_type == Scene::Menu ||
	    scene_type == Scene::Item || scene_type == Scene::Skill ||
	    scene_type == Scene::Equip || scene_type == Scene::Status ||
	    scene_type == Scene::Save) {
		std::string map_name = GetMapDetails();
		if (!map_name.empty()) {
			state_buf = map_name;
		}

		std::string party_info = GetPartyInfo();
		if (!party_info.empty()) {
			state_buf += " | " + party_info;
		}
	} else if (scene_type == Scene::Battle) {
		std::string party_info = GetPartyInfo();
		if (!party_info.empty()) {
			state_buf = "In Battle | " + party_info;
		}
	}

	presence.details = details_buf.c_str();
	presence.state = state_buf.c_str();
	presence.startTimestamp = start_timestamp;
	presence.largeImageKey = "icon";
	presence.largeImageText = "EasyRPG Player (Chaos Fork)";

	Discord_UpdatePresence(&presence);
}

bool Chaos::DiscordIntegration::IsEnabled() {
	return enabled;
}

void Chaos::DiscordIntegration::SetEnabled(bool value) {
	enabled = value;
	if (enabled) {
		if (!initialized) {
			Initialize();
		}
		// Force an immediate update
		last_update_time = {};
	} else {
		if (initialized) {
			Discord_ClearPresence();
		}
	}
	Output::Debug("Discord RPC: {}", enabled ? "Enabled" : "Disabled");
}
