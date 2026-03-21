/*
 * Chaos Fork: Game Mode Selection Scene Implementation
 */

#include "chaos/scene_gamemode_select.h"
#include "chaos/game_mode.h"
#include "input.h"
#include "player.h"
#include "game_system.h"
#include "game_clock.h"
#include "main_data.h"
#include "scene_logo.h"
#include "scene_title.h"
#include "scene.h"

namespace Chaos {

Scene_GameModeSelect::Scene_GameModeSelect() {
	type = Scene::Settings;
}

void Scene_GameModeSelect::Start() {
	CreateWindows();
	Game_Clock::ResetFrame(Game_Clock::now());
}

void Scene_GameModeSelect::CreateWindows() {
	help_window = std::make_unique<Window_Help>(0, 0, Player::screen_width, 32);

	std::vector<std::string> options;
	for (int i = 0; i < static_cast<int>(GameMode::Count); i++) {
		auto& info = GetGameModeInfo(static_cast<GameMode>(i));
		options.push_back(info.name);
	}

	command_window = std::make_unique<Window_Command>(options, Player::screen_width / 2);
	command_window->SetX(Player::screen_width / 4);
	command_window->SetY(Player::screen_height / 2 - command_window->GetHeight() / 2);

	UpdateHelp();
}

void Scene_GameModeSelect::UpdateHelp() {
	int idx = command_window->GetIndex();
	if (idx >= 0 && idx < static_cast<int>(GameMode::Count)) {
		auto& info = GetGameModeInfo(static_cast<GameMode>(idx));
		help_window->SetText(info.description);
	}
}

void Scene_GameModeSelect::vUpdate() {
	command_window->Update();

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		Scene::Pop();
		return;
	}

	// Update description when selection changes
	UpdateHelp();

	if (Input::IsTriggered(Input::DECISION)) {
		int idx = command_window->GetIndex();
		if (idx >= 0 && idx < static_cast<int>(GameMode::Count)) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));

			// Set the selected game mode
			SetCurrentGameMode(static_cast<GameMode>(idx));

			// Proceed to logo/title scene
			auto logos = Scene_Logo::LoadLogos();
			if (!logos.empty()) {
				Scene::Push(std::make_shared<Scene_Logo>(std::move(logos), 1), true);
			} else {
				Scene::Push(std::make_shared<Scene_Title>(), true);
			}
		}
	}
}

} // namespace Chaos
