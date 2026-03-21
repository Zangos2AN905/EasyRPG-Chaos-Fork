/*
 * Chaos Fork: Undertale Mode Implementation
 * Pixel-based movement system and dance glitch.
 */

#include "chaos/undertale_mode.h"
#include "chaos/game_mode.h"
#include "chaos/multiplayer_mode.h"
#include "chaos/net_manager.h"
#include "game_player.h"
#include "game_map.h"
#include "game_message.h"
#include "game_character.h"
#include "input.h"
#include "output.h"
#include "options.h"
#include "utils.h"

namespace Chaos {

UndertaleMode& UndertaleMode::Instance() {
	static UndertaleMode instance;
	return instance;
}

bool UndertaleMode::IsActive() const {
	// Active in singleplayer Undertale mode or multiplayer Undertale mode
	if (IsUndertaleMode()) {
		return true;
	}
	auto& net = NetManager::Instance();
	if (net.IsConnected() && net.GetMode() == MultiplayerMode::Undertale) {
		return true;
	}
	return false;
}

void UndertaleMode::InitFromPlayer(Game_Player& player) {
	abs_x = player.GetX() * SCREEN_TILE_SIZE;
	abs_y = player.GetY() * SCREEN_TILE_SIZE;
	initialized = true;
	dance_active = false;
	opposing_keys_held = false;
	step_accumulator_x = 0;
	step_accumulator_y = 0;
}

void UndertaleMode::Reset() {
	initialized = false;
	abs_x = 0;
	abs_y = 0;
	dance_active = false;
	opposing_keys_held = false;
	step_accumulator_x = 0;
	step_accumulator_y = 0;
}

void UndertaleMode::SetMoveSpeed(int pixels_per_frame) {
	move_speed_pixels = pixels_per_frame;
}

bool UndertaleMode::CanMoveTo(Game_Player& player, int from_tile_x, int from_tile_y, int to_tile_x, int to_tile_y) const {
	if (from_tile_x == to_tile_x && from_tile_y == to_tile_y) {
		return true;
	}
	return player.MakeWay(from_tile_x, from_tile_y, to_tile_x, to_tile_y);
}

void UndertaleMode::UpdateDanceGlitch(Game_Player& player, bool opposing_held) {
	if (opposing_held && last_was_blocked) {
		// Frisk dance: opposing keys held and blocked by wall
		dance_active = true;
		player.SetAnimFrame((player.GetAnimFrame() + 1) % 4);
	} else {
		dance_active = false;
	}
}

void UndertaleMode::SyncTilePosition(Game_Player& player) {
	int tile_x = abs_x / SCREEN_TILE_SIZE;
	int tile_y = abs_y / SCREEN_TILE_SIZE;

	// Handle wrapping for looping maps
	tile_x = Game_Map::RoundX(tile_x);
	tile_y = Game_Map::RoundY(tile_y);

	if (player.GetX() != tile_x || player.GetY() != tile_y) {
		player.SetX(tile_x);
		player.SetY(tile_y);
	}
}

void UndertaleMode::UpdateScroll(Game_Player& player) {
	if (player.IsPanLocked()) {
		return;
	}

	// Center the camera on the player's pixel position
	int target_x = abs_x - player.GetPanX();
	int target_y = abs_y - player.GetPanY();

	int scroll_dx = target_x - Game_Map::GetPositionX();
	int scroll_dy = target_y - Game_Map::GetPositionY();

	// Handle looping maps
	if (Game_Map::LoopHorizontal()) {
		int w = Game_Map::GetTilesX() * SCREEN_TILE_SIZE;
		scroll_dx = Utils::PositiveModulo(scroll_dx + w / 2, w) - w / 2;
	}
	if (Game_Map::LoopVertical()) {
		int h = Game_Map::GetTilesY() * SCREEN_TILE_SIZE;
		scroll_dy = Utils::PositiveModulo(scroll_dy + h / 2, h) - h / 2;
	}

	if (scroll_dx != 0 || scroll_dy != 0) {
		Game_Map::Scroll(scroll_dx, scroll_dy);
	}
}

bool UndertaleMode::UpdatePixelMovement(Game_Player& player) {
	if (!initialized) {
		InitFromPlayer(player);
	}

	// Reset per-frame tracking
	last_was_blocked = false;
	last_entered_new_tile = false;

	// Read input
	int move_dir = -1;
	int dx = 0;
	int dy = 0;

	// Detect opposing direction holds for Frisk Dance:
	// Up+Down → move Up, Left+Right → move Left. Dance activates on wall contact.
	bool up_held = Input::IsPressed(Input::UP);
	bool down_held = Input::IsPressed(Input::DOWN);
	bool left_held = Input::IsPressed(Input::LEFT);
	bool right_held = Input::IsPressed(Input::RIGHT);

	bool opposing_vertical = up_held && down_held;
	bool opposing_horizontal = left_held && right_held;
	opposing_keys_held = opposing_vertical || opposing_horizontal;

	if (opposing_vertical) {
		// Up+Down held: always move Up
		move_dir = Game_Character::Up;
		dy = -move_speed_pixels * UNITS_PER_PIXEL;
	} else if (opposing_horizontal) {
		// Left+Right held: always move Left
		move_dir = Game_Character::Left;
		dx = -move_speed_pixels * UNITS_PER_PIXEL;
	} else {
		// Normal input: 8-directional
		int dir_input = Input::dir8;
		switch (dir_input) {
			case 2: // Down
				move_dir = Game_Character::Down;
				dy = move_speed_pixels * UNITS_PER_PIXEL;
				break;
			case 4: // Left
				move_dir = Game_Character::Left;
				dx = -move_speed_pixels * UNITS_PER_PIXEL;
				break;
			case 6: // Right
				move_dir = Game_Character::Right;
				dx = move_speed_pixels * UNITS_PER_PIXEL;
				break;
			case 8: // Up
				move_dir = Game_Character::Up;
				dy = -move_speed_pixels * UNITS_PER_PIXEL;
				break;
			case 1: // Down-Left
				move_dir = Game_Character::DownLeft;
				dx = -move_speed_pixels * UNITS_PER_PIXEL;
				dy = move_speed_pixels * UNITS_PER_PIXEL;
				break;
			case 3: // Down-Right
				move_dir = Game_Character::DownRight;
				dx = move_speed_pixels * UNITS_PER_PIXEL;
				dy = move_speed_pixels * UNITS_PER_PIXEL;
				break;
			case 7: // Up-Left
				move_dir = Game_Character::UpLeft;
				dx = -move_speed_pixels * UNITS_PER_PIXEL;
				dy = -move_speed_pixels * UNITS_PER_PIXEL;
				break;
			case 9: // Up-Right
				move_dir = Game_Character::UpRight;
				dx = move_speed_pixels * UNITS_PER_PIXEL;
				dy = -move_speed_pixels * UNITS_PER_PIXEL;
				break;
		}
	}

	// Direction and facing must be set before dance check
	// so that blocked detection works for the correct direction

	if (move_dir < 0) {
		// No movement input — still update camera
		UpdateScroll(player);
		return false;
	}

	// Set direction and facing
	player.SetDirection(move_dir);
	player.UpdateFacing();

	// Calculate new position
	int new_abs_x = abs_x + dx;
	int new_abs_y = abs_y + dy;

	// Compute tile positions
	int old_tile_x = abs_x / SCREEN_TILE_SIZE;
	int old_tile_y = abs_y / SCREEN_TILE_SIZE;

	// For negative coordinates, adjust tile calculation
	int new_tile_x = (new_abs_x >= 0) ? new_abs_x / SCREEN_TILE_SIZE : (new_abs_x - SCREEN_TILE_SIZE + 1) / SCREEN_TILE_SIZE;
	int new_tile_y = (new_abs_y >= 0) ? new_abs_y / SCREEN_TILE_SIZE : (new_abs_y - SCREEN_TILE_SIZE + 1) / SCREEN_TILE_SIZE;

	// Check collision when crossing tile boundary
	bool can_move_x = true;
	bool can_move_y = true;

	if (new_tile_x != old_tile_x) {
		can_move_x = CanMoveTo(player, Game_Map::RoundX(old_tile_x), Game_Map::RoundY(old_tile_y),
		                       Game_Map::RoundX(new_tile_x), Game_Map::RoundY(old_tile_y));
	}

	if (new_tile_y != old_tile_y) {
		can_move_y = CanMoveTo(player, Game_Map::RoundX(old_tile_x), Game_Map::RoundY(old_tile_y),
		                       Game_Map::RoundX(old_tile_x), Game_Map::RoundY(new_tile_y));
	}

	// Save original position to compute actual movement delta
	int orig_abs_x = abs_x;
	int orig_abs_y = abs_y;

	// Apply X movement
	if (can_move_x) {
		abs_x = new_abs_x;
	} else if (dx != 0) {
		// Tile crossing blocked: snap to tile-aligned position
		abs_x = old_tile_x * SCREEN_TILE_SIZE;
	}

	// Ahead-of-time clamping for X: if the next tile in the movement direction
	// is impassable, snap to tile-aligned position so the player sprite never
	// visually overlaps with blocked tiles or events.
	if (dx != 0) {
		int cur_tx = (abs_x >= 0) ? abs_x / SCREEN_TILE_SIZE
		                          : (abs_x - SCREEN_TILE_SIZE + 1) / SCREEN_TILE_SIZE;
		int ahead_tx = Game_Map::RoundX(cur_tx + (dx > 0 ? 1 : -1));
		if (!CanMoveTo(player, Game_Map::RoundX(cur_tx), Game_Map::RoundY(old_tile_y),
		               ahead_tx, Game_Map::RoundY(old_tile_y))) {
			abs_x = cur_tx * SCREEN_TILE_SIZE;
		}
	}

	// Apply Y movement
	if (can_move_y) {
		abs_y = new_abs_y;
	} else if (dy != 0) {
		abs_y = old_tile_y * SCREEN_TILE_SIZE;
	}

	// Ahead-of-time clamping for Y (uses updated X tile position)
	if (dy != 0) {
		int cur_ty = (abs_y >= 0) ? abs_y / SCREEN_TILE_SIZE
		                          : (abs_y - SCREEN_TILE_SIZE + 1) / SCREEN_TILE_SIZE;
		int cur_tx = (abs_x >= 0) ? abs_x / SCREEN_TILE_SIZE
		                          : (abs_x - SCREEN_TILE_SIZE + 1) / SCREEN_TILE_SIZE;
		int ahead_ty = Game_Map::RoundY(cur_ty + (dy > 0 ? 1 : -1));
		if (!CanMoveTo(player, Game_Map::RoundX(cur_tx), Game_Map::RoundY(cur_ty),
		               Game_Map::RoundX(cur_tx), ahead_ty)) {
			abs_y = cur_ty * SCREEN_TILE_SIZE;
		}
	}

	int actual_dx = abs_x - orig_abs_x;
	int actual_dy = abs_y - orig_abs_y;
	bool moved = (actual_dx != 0 || actual_dy != 0);

	// Blocked = movement input given but no progress made
	if (!moved && (dx != 0 || dy != 0)) {
		last_was_blocked = true;
	}

	// Update dance glitch after movement/blocked detection
	UpdateDanceGlitch(player, opposing_keys_held);

	if (moved) {
		// Sync tile position and detect tile change
		int prev_tile_x = player.GetX();
		int prev_tile_y = player.GetY();
		SyncTilePosition(player);
		last_entered_new_tile = (player.GetX() != prev_tile_x || player.GetY() != prev_tile_y);

		// Update walking animation
		if (!dance_active) {
			step_accumulator_x += std::abs(actual_dx);
			step_accumulator_y += std::abs(actual_dy);

			int step_threshold = 8 * UNITS_PER_PIXEL;
			int total_step = step_accumulator_x + step_accumulator_y;
			if (total_step >= step_threshold) {
				player.SetAnimFrame((player.GetAnimFrame() + 1) % 4);
				step_accumulator_x = 0;
				step_accumulator_y = 0;
			}
		}
	}

	// Always update camera to track player pixel position
	UpdateScroll(player);

	return moved;
}

} // namespace Chaos
