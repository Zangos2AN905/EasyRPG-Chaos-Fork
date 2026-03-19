/*
 * Chaos Fork: Multiplayer Wait Scene Implementation
 * Client waits here while the host selects a save file or starts a new game.
 * When host enters a map, HostMapReady is received.
 * If the host's game isn't available locally, downloads it first.
 */

#include "chaos/scene_multiplayer_wait.h"
#include "chaos/net_manager.h"
#include "chaos/multiplayer_state.h"
#include "chaos/multiplayer_mode.h"
#include "chaos/game_file_transfer.h"
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
#include "filefinder.h"
#include "output.h"
#include <fmt/format.h>
#include <filesystem>

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

bool Scene_MultiplayerWait::IsHostGameAvailable() {
	auto& net = NetManager::Instance();
	std::string host_game = net.GetHostGameName();

	if (host_game.empty() || host_game == "Unknown Game") {
		return true; // Can't determine, assume available
	}

	// Check if the local game title matches
	if (!Player::game_title.empty() && Player::game_title == host_game) {
		return true;
	}

	// Check if a directory with the host's game name exists near the executable
	std::string games_dir;
#ifdef _WIN32
	wchar_t exe_path[MAX_PATH];
	GetModuleFileNameW(NULL, exe_path, MAX_PATH);
	std::wstring wpath(exe_path);
	size_t pos = wpath.find_last_of(L"\\/");
	if (pos != std::wstring::npos) {
		wpath = wpath.substr(0, pos);
	}
	int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, NULL, 0, NULL, NULL);
	games_dir.resize(len - 1);
	WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &games_dir[0], len, NULL, NULL);
#else
	games_dir = ".";
#endif

	// Sanitize game name for folder lookup
	std::string safe_name = host_game;
	for (char& c : safe_name) {
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
			c = '_';
		}
	}

	std::string game_path = games_dir + "/" + safe_name;
	std::error_code ec;
	if (std::filesystem::is_directory(game_path, ec)) {
		// Found the game directory - switch to it
		SwitchToHostGame(game_path);
		return true;
	}

	return false;
}

void Scene_MultiplayerWait::SwitchToHostGame(const std::string& game_path) {
	Output::Debug("Multiplayer: Switching to host's game at {}", game_path);

	auto fs = FileFinder::Root().Create(game_path);
	if (fs) {
		FileFinder::SetGameFilesystem(fs);
		Player::CreateGameObjects();
		Output::Debug("Multiplayer: Game objects created for host's game");
	} else {
		Output::Warning("Multiplayer: Failed to create filesystem for {}", game_path);
	}
}

void Scene_MultiplayerWait::StartClientGame() {
	auto& net = NetManager::Instance();

	Output::Debug("Multiplayer: Host is on map {} at ({}, {}), starting client game",
		pending_map_id, pending_map_x, pending_map_y);

	// Override spawn location to host's position
	Player::start_map_id = pending_map_id;
	Player::party_x_position = pending_map_x;
	Player::party_y_position = pending_map_y;

	// Use the standard new game setup
	Player::SetupNewGame();

	// Apply host's party composition and actor levels
	auto host_party = net.GetHostParty();
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
		Main_Data::game_player->ResetGraphic();
	}

	// Apply switches/variables from host
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
		if (timer > 120) {
			MultiplayerState::Instance().StopMultiplayer();
			net.Disconnect();
			Scene::Pop();
		}
		return;
	}

	switch (wait_state) {
		case WaitState::WaitingForHost: {
			// Check if host entered a map
			if (net.IsHostMapReady()) {
				pending_map_id = net.GetHostMapId();
				pending_map_x = net.GetHostMapX();
				pending_map_y = net.GetHostMapY();
				net.ClearHostMapReady();
				has_pending_host_data = true;

				// Check if host's game is available locally
				if (IsHostGameAvailable()) {
					// Game is available, start immediately
					StartClientGame();
					return;
				}

				// Game not available - start downloading
				Output::Debug("Multiplayer: Host game '{}' not found locally, requesting download",
					net.GetHostGameName());
				status_window->SetText(fmt::format("Downloading {}...", net.GetHostGameName()));
				net.RequestGameFiles();
				wait_state = WaitState::Downloading;
				return;
			}

			// Animate dots
			std::string dots;
			int n = (timer / 30) % 4;
			for (int i = 0; i < n; ++i) dots += '.';
			status_window->SetText(fmt::format("Waiting for host to select a game{}", dots));
			break;
		}

		case WaitState::Downloading: {
			auto* transfer = net.GetClientTransfer();
			if (!transfer) {
				status_window->SetText("Download error: transfer lost");
				wait_state = WaitState::WaitingForHost;
				break;
			}

			if (transfer->HasError()) {
				status_window->SetText(fmt::format("Download failed: {}", transfer->GetError()));
				// Wait a bit then fall back
				if (timer > 300) {
					wait_state = WaitState::WaitingForHost;
				}
				break;
			}

			if (transfer->IsComplete() && !transfer->HasError()) {
				// Writer thread may still be flushing; check if it's done
				// IsComplete means OnTransferDone was called. Wait for writer to finish.
				// The writer sets writer_running to false when done.
				// Give it a moment to flush remaining queue
				wait_state = WaitState::DownloadDone;
				timer = 0;
				status_window->SetText("Download complete! Loading game...");
				break;
			}

			// Show progress
			int pct = transfer->GetProgressPercent();
			uint16_t files_done = transfer->GetReceivedFiles();
			uint16_t files_total = transfer->GetTotalFiles();
			if (pct > 0) {
				status_window->SetText(fmt::format("Downloading {}... {}% ({}/{})",
					net.GetHostGameName(), pct, files_done, files_total));
			} else if (transfer->IsInfoReceived()) {
				status_window->SetText(fmt::format("Downloading {}... preparing",
					net.GetHostGameName()));
			} else {
				std::string dots;
				int n = (timer / 30) % 4;
				for (int i = 0; i < n; ++i) dots += '.';
				status_window->SetText(fmt::format("Requesting game files{}", dots));
			}
			break;
		}

		case WaitState::DownloadDone: {
			// Wait a few frames for the writer thread to fully flush
			if (timer > 30) {
				// Determine the downloaded game path
				std::string safe_name = net.GetHostGameName();
				for (char& c : safe_name) {
					if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
						c = '_';
					}
				}
				if (safe_name.empty()) safe_name = "DownloadedGame";

				std::string games_dir;
#ifdef _WIN32
				wchar_t exe_path[MAX_PATH];
				GetModuleFileNameW(NULL, exe_path, MAX_PATH);
				std::wstring wpath(exe_path);
				size_t pos = wpath.find_last_of(L"\\/");
				if (pos != std::wstring::npos) {
					wpath = wpath.substr(0, pos);
				}
				int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, NULL, 0, NULL, NULL);
				games_dir.resize(len - 1);
				WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &games_dir[0], len, NULL, NULL);
#else
				games_dir = ".";
#endif
				std::string game_path = games_dir + "/" + safe_name;

				SwitchToHostGame(game_path);
				StartClientGame();
				wait_state = WaitState::Ready;
			}
			break;
		}

		case WaitState::Ready:
			// Already started
			break;
	}
}

} // namespace Chaos
