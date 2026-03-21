/*
 * Chaos Fork: Singleplayer Game Mode Implementation
 */

#include "chaos/game_mode.h"

namespace Chaos {

static GameMode current_game_mode = GameMode::Normal;

GameMode GetCurrentGameMode() {
	return current_game_mode;
}

void SetCurrentGameMode(GameMode mode) {
	current_game_mode = mode;
}

bool IsUndertaleMode() {
	return current_game_mode == GameMode::Undertale;
}

bool IsHorrorSingleplayerMode() {
	return current_game_mode == GameMode::Horror;
}

} // namespace Chaos
