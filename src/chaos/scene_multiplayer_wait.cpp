/*
 * Chaos Fork: Multiplayer Wait Scene Implementation
 * Client waits here while the host selects a save file or starts a new game.
 * When host enters a map, HostMapReady is received and the client starts.
 */

#include "chaos/scene_multiplayer_wait.h"
#include "chaos/net_manager.h"
#include "chaos/multiplayer_state.h"
#include "chaos/multiplayer_mode.h"
#include "input.h"
#include "player.h"
#include "scene.h"
#include "scene_map.h"
#include "game_actor.h"
#include "game_actors.h"
#include "game_switches.h"
#include "game_system.h"
#include "game_map.h"
#include "game_party.h"
#include "game_player.h"
#include "game_variables.h"
#include "main_data.h"
#include "game_clock.h"
#include "output.h"
#include <fmt/format.h>

namespace Chaos {

Scene_MultiplayerWait::Scene_MultiplayerWait() {
	type = Scene::Settings;
}

void Scene_MultiplayerWait::Start() {
	status_window = std::make_unique<Window_Help>(
		0, Player::screen_height / 2 - 16,
		Player::screen_width, 32);
	status_window->SetText("Waiting for host to select a game...");
	Game_Clock::ResetFrame(Game_Clock::now());
}

void Scene_MultiplayerWait::vUpdate() {
	auto& net = NetManager::Instance();
	net.Update();

	timer++;

	if (Input::IsTriggered(Input::CANCEL)) {
		if (Main_Data::game_system) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		}
		MultiplayerState::Instance().StopMultiplayer();
		net.Disconnect();
		Scene::Pop();
		return;
	}

	// Check if host disconnected
	if (net.IsHostDisconnected() || !net.IsConnected()) {
		net.ClearHostDisconnected();
		status_window->SetText("Host disconnected!");
		// Wait a moment then pop
		if (timer > 120) {
			MultiplayerState::Instance().StopMultiplayer();
			net.Disconnect();
			Scene::Pop();
		}
		return;
	}

	// Check if host entered a map
	if (net.IsHostMapReady()) {
		int map_id = net.GetHostMapId();
		int map_x = net.GetHostMapX();
		int map_y = net.GetHostMapY();
		auto host_party = net.GetHostParty();
		net.ClearHostMapReady();

		Output::Debug("Multiplayer: Host is on map {} at ({}, {}), {} actors, starting client game",
			map_id, map_x, map_y, host_party.size());

		// Override spawn location to host's position
		Player::start_map_id = map_id;
		Player::party_x_position = map_x;
		Player::party_y_position = map_y;

		// Use the standard new game setup (handles all initialization properly)
		// This also pushes Scene_Map(0) on top of this scene
		Player::SetupNewGame();

		// Apply host's party composition and actor levels
		if (!host_party.empty()) {
			Main_Data::game_party->Clear();
			for (const auto& member : host_party) {
				Main_Data::game_party->AddActor(member.actor_id);
				auto* actor = Main_Data::game_actors->GetActor(member.actor_id);
				if (actor && actor->GetLevel() != member.level) {
					actor->ChangeLevel(member.level, nullptr);
				}
			}
		}

		// In team mode, set the player's sprite to their assigned actor
		// peer_id is 1-based (host=1, client1=2, ...) so subtract 1 for array index
		auto mode = net.GetMode();
		if (mode == MultiplayerMode::TeamParty || mode == MultiplayerMode::Chaotix) {
			int player_index = static_cast<int>(net.GetLocalPeerId()) - 1;
			if (player_index >= 0 && player_index < static_cast<int>(host_party.size())) {
				auto* actor = Main_Data::game_actors->GetActor(host_party[player_index].actor_id);
				if (actor) {
					Main_Data::game_player->SetSpriteGraphic(
						std::string(actor->GetSpriteName()), actor->GetSpriteIndex());
					Output::Debug("Multiplayer: Team mode - using actor {} ({})",
						actor->GetId(), std::string(actor->GetSpriteName()));
				}
			}
		} else {
			// Non-team mode: reset sprite to match the (possibly changed) party leader
			Main_Data::game_player->ResetGraphic();
		}

		// Apply switches/variables from host (for loaded saves)
		auto& host_switches = net.GetHostSwitches();
		if (!host_switches.empty() && Main_Data::game_switches) {
			for (size_t i = 0; i < host_switches.size(); ++i) {
				Main_Data::game_switches->Set(static_cast<int>(i + 1), host_switches[i]);
			}
			Output::Debug("Multiplayer: Applied {} switches from host", host_switches.size());
		}
		auto& host_variables = net.GetHostVariables();
		if (!host_variables.empty() && Main_Data::game_variables) {
			for (size_t i = 0; i < host_variables.size(); ++i) {
				Main_Data::game_variables->Set(static_cast<int>(i + 1), host_variables[i]);
			}
			Output::Debug("Multiplayer: Applied {} variables from host", host_variables.size());
		}

		return;
	}

	// Animate dots
	std::string dots;
	int n = (timer / 30) % 4;
	for (int i = 0; i < n; ++i) dots += '.';
	status_window->SetText(fmt::format("Waiting for host to select a game{}", dots));
}

} // namespace Chaos
