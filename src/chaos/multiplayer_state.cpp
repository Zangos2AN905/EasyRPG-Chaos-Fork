/*
 * Chaos Fork: Multiplayer State Manager Implementation
 */

#include "chaos/multiplayer_state.h"
#include "chaos/net_manager.h"
#include "chaos/net_packet.h"
#include "game_actor.h"
#include "game_actors.h"
#include "game_event.h"
#include "game_map.h"
#include "game_party.h"
#include "game_player.h"
#include "game_switches.h"
#include "game_variables.h"
#include "game_system.h"
#include "input.h"
#include "main_data.h"
#include "output.h"
#include "player.h"
#include "rand.h"
#include "scene.h"
#include "scene_battle.h"
#include "spriteset_map.h"
#include "sprite_character.h"
#include "window_help.h"
#include <fmt/format.h>

namespace Chaos {

MultiplayerState& MultiplayerState::Instance() {
	static MultiplayerState instance;
	return instance;
}

void MultiplayerState::StartMultiplayer() {
	if (active) return;
	active = true;
	SetupCallbacks();
	Output::Debug("Multiplayer: State manager started");
}

void MultiplayerState::StopMultiplayer() {
	if (!active) return;
	active = false;
	host_lost = false;
	spectating = false;
	spectate_target_id = 0;
	spectator_window.reset();
	in_battle = false;
	pending_battle_invite = false;
	forced_battle = false;
	battle_invite_window.reset();
	remote_battle_actions.clear();
	remote_players.clear();
	last_switches.clear();
	last_variables.clear();
	current_spriteset = nullptr;
	Output::Debug("Multiplayer: State manager stopped");
}

void MultiplayerState::SetupCallbacks() {
	auto& net = NetManager::Instance();

	net.SetConnectCallback([this](uint16_t peer_id) {
		OnPlayerConnected(peer_id);
	});

	net.SetDisconnectCallback([this](uint16_t peer_id) {
		OnPlayerDisconnected(peer_id);
	});

	net.SetPacketCallback([this](uint16_t sender_id, const uint8_t* data, size_t len) {
		OnPacketReceived(sender_id, data, len);
	});
}

void MultiplayerState::Update() {
	if (!active) return;

	auto& net = NetManager::Instance();
	if (!net.IsConnected()) {
		// Check if host disconnected (client side)
		if (net.IsHostDisconnected()) {
			net.ClearHostDisconnected();
			host_lost = true;
			Output::Debug("Multiplayer: Host disconnected, returning to menu");
		}
		return;
	}

	// Process network events
	net.Update();

	// Only do gameplay sync when we're on a map (spriteset exists)
	if (!current_spriteset) return;

	// Send local player position periodically
	position_send_counter++;
	if (position_send_counter >= POSITION_SEND_INTERVAL) {
		position_send_counter = 0;
		SendLocalPlayerPosition();
	}

	// Update remote player characters (movement animation, stepping)
	for (auto& [id, rp] : remote_players) {
		if (rp->IsOnCurrentMap()) {
			rp->UpdateRemote();
		}
	}

	// Sync switches and variables based on mode
	auto& props = GetModeProperties(net.GetMode());
	if (props.sync_switches && net.IsHost()) {
		CheckAndSyncSwitches();
	}
	if (props.sync_variables && net.IsHost()) {
		CheckAndSyncVariables();
	}

	// Sync event positions from host to clients
	if (net.IsHost()) {
		event_sync_counter++;
		if (event_sync_counter >= EVENT_SYNC_INTERVAL) {
			event_sync_counter = 0;
			SyncEventPositions();
		}
	}

	// Update spectator mode
	if (spectating) {
		UpdateSpectator();
	}

	// Update battle invite prompt
	if (pending_battle_invite && !in_battle) {
		UpdateBattleInvite();
	}
}

void MultiplayerState::OnMapLoaded(Spriteset_Map* spriteset) {
	current_spriteset = spriteset;

	// Create sprites for all remote players that are on this map
	for (auto& [id, player] : remote_players) {
		if (player->IsOnCurrentMap()) {
			CreateRemotePlayerSprite(player.get());
		}
	}

	// Host: notify clients which map we loaded so they can join
	auto& net = NetManager::Instance();
	if (net.IsHost() && net.IsConnected()) {
		auto* hero = Main_Data::game_player.get();
		if (hero) {
			int map_id = Game_Map::GetMapId();
			int x = hero->GetX();
			int y = hero->GetY();

			PacketWriter pw(PacketType::HostMapReady);
			pw.write(static_cast<int32_t>(map_id));
			pw.write(static_cast<int32_t>(x));
			pw.write(static_cast<int32_t>(y));

			// Send party actor IDs and levels
			auto actors = Main_Data::game_party->GetActors();
			pw.write(static_cast<uint16_t>(actors.size()));
			for (auto* actor : actors) {
				pw.write(static_cast<uint16_t>(actor->GetId()));
				pw.write(static_cast<int32_t>(actor->GetLevel()));
			}

			net.Broadcast(pw, true);
			Output::Debug("Multiplayer: Host sent HostMapReady map={} x={} y={} actors={}",
				map_id, x, y, actors.size());

			// Send bulk switch data
			if (Main_Data::game_switches) {
				auto& sw = Main_Data::game_switches->GetData();
				PacketWriter sw_pw(PacketType::SwitchBulkSync);
				sw_pw.write(static_cast<uint16_t>(sw.size()));
				for (size_t i = 0; i < sw.size(); ++i) {
					sw_pw.write(static_cast<uint8_t>(sw[i] ? 1 : 0));
				}
				net.Broadcast(sw_pw, true);
			}

			// Send bulk variable data
			if (Main_Data::game_variables) {
				auto& vars = Main_Data::game_variables->GetData();
				PacketWriter var_pw(PacketType::VariableBulkSync);
				var_pw.write(static_cast<uint16_t>(vars.size()));
				for (size_t i = 0; i < vars.size(); ++i) {
					var_pw.write(static_cast<int32_t>(vars[i]));
				}
				net.Broadcast(var_pw, true);
			}
		}
	}
}

void MultiplayerState::OnMapUnloaded() {
	current_spriteset = nullptr;
}

void MultiplayerState::OnPlayerConnected(uint16_t peer_id) {
	auto& net = NetManager::Instance();

	// Skip if this is our own connection notification
	if (peer_id == net.GetLocalPeerId()) return;

	// Find the peer info to get name
	auto* pi = net.FindPeer(peer_id);
	std::string name = pi ? pi->player_name : "Player";

	// Create remote player
	auto rp = std::make_unique<Game_RemotePlayer>(peer_id, name);
	auto* ptr = rp.get();
	remote_players[peer_id] = std::move(rp);

	Output::Debug("Multiplayer: Created remote player for peer {}", peer_id);

	// If we're on a map, create sprite immediately
	if (current_spriteset) {
		CreateRemotePlayerSprite(ptr);
	}

	// If host and game already started, send game state to late joiner
	if (net.IsHost() && net.IsGameStarted() && current_spriteset) {
		// Send GameStart so the client leaves the lobby
		PacketWriter gs(PacketType::GameStart);
		net.SendTo(peer_id, gs, true);

		// Send HostMapReady with current map position
		auto* hero = Main_Data::game_player.get();
		if (hero) {
			int map_id = Game_Map::GetMapId();
			int x = hero->GetX();
			int y = hero->GetY();

			PacketWriter pw(PacketType::HostMapReady);
			pw.write(static_cast<int32_t>(map_id));
			pw.write(static_cast<int32_t>(x));
			pw.write(static_cast<int32_t>(y));

			auto actors = Main_Data::game_party->GetActors();
			pw.write(static_cast<uint16_t>(actors.size()));
			for (auto* actor : actors) {
				pw.write(static_cast<uint16_t>(actor->GetId()));
				pw.write(static_cast<int32_t>(actor->GetLevel()));
			}

			net.SendTo(peer_id, pw, true);

			// Send bulk switch data
			if (Main_Data::game_switches) {
				auto& sw = Main_Data::game_switches->GetData();
				PacketWriter sw_pw(PacketType::SwitchBulkSync);
				sw_pw.write(static_cast<uint16_t>(sw.size()));
				for (size_t i = 0; i < sw.size(); ++i) {
					sw_pw.write(static_cast<uint8_t>(sw[i] ? 1 : 0));
				}
				net.SendTo(peer_id, sw_pw, true);
			}

			// Send bulk variable data
			if (Main_Data::game_variables) {
				auto& vars = Main_Data::game_variables->GetData();
				PacketWriter var_pw(PacketType::VariableBulkSync);
				var_pw.write(static_cast<uint16_t>(vars.size()));
				for (size_t i = 0; i < vars.size(); ++i) {
					var_pw.write(static_cast<int32_t>(vars[i]));
				}
				net.SendTo(peer_id, var_pw, true);
			}

			Output::Debug("Multiplayer: Sent game state to late joiner peer {}", peer_id);
		}
	}
}

void MultiplayerState::OnPlayerDisconnected(uint16_t peer_id) {
	RemoveRemotePlayerSprite(peer_id);
	remote_players.erase(peer_id);
	Output::Debug("Multiplayer: Removed remote player for peer {}", peer_id);
}

void MultiplayerState::OnPacketReceived(uint16_t sender_id, const uint8_t* data, size_t len) {
	if (len < 1) return;

	PacketReader reader(data, len);
	PacketType ptype = reader.readType();

	switch (ptype) {
		case PacketType::PlayerPosition:
			HandlePlayerPosition(sender_id, data, len);
			break;
		case PacketType::SwitchSync:
			HandleSwitchSync(data, len);
			break;
		case PacketType::VariableSync:
			HandleVariableSync(data, len);
			break;
		case PacketType::GodCommand:
			HandleGodCommand(sender_id, data, len);
			break;
		case PacketType::BattleStart:
			HandleBattleStart(sender_id, data, len);
			break;
		case PacketType::BattleJoin:
			HandleBattleJoin(sender_id, data, len);
			break;
		case PacketType::BattleForce:
			HandleBattleForce(data, len);
			break;
		case PacketType::BattleAction:
			HandleBattleAction(data, len);
			break;
		case PacketType::BattleEnd:
			HandleBattleEnd(data, len);
			break;
		case PacketType::EventSync:
			HandleEventSync(data, len);
			break;
		case PacketType::BattleTurnSync:
			HandleBattleTurnSync(data, len);
			break;
		case PacketType::BattleEscapeVote:
			HandleBattleEscapeVote(sender_id, data, len);
			break;
		default:
			break;
	}
}

void MultiplayerState::HandlePlayerPosition(uint16_t sender_id, const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType(); // Skip type byte

	uint16_t peer_id = reader.readU16();
	int32_t map_id = reader.readI32();
	int32_t x = reader.readI32();
	int32_t y = reader.readI32();
	int32_t direction = reader.readI32();
	int32_t facing = reader.readI32();
	std::string sprite_name = reader.readString();
	int32_t sprite_index = reader.readI32();

	// Use sender_id if this is on the server, or peer_id from packet on client
	uint16_t actual_id = (sender_id != 0) ? sender_id : peer_id;

	auto* rp = GetRemotePlayer(actual_id);
	if (!rp) {
		// Player not yet registered, create them
		auto& net = NetManager::Instance();
		auto* pi = net.FindPeer(actual_id);
		std::string name = pi ? pi->player_name : "Player";
		auto new_rp = std::make_unique<Game_RemotePlayer>(actual_id, name);
		rp = new_rp.get();
		remote_players[actual_id] = std::move(new_rp);
	}

	bool was_on_map = rp->IsOnCurrentMap();
	rp->SetNetworkPosition(map_id, x, y, direction, facing);
	if (!sprite_name.empty()) {
		rp->SetCharacterSprite(sprite_name, sprite_index);
	}
	bool is_on_map = rp->IsOnCurrentMap();

	// Handle map transitions
	if (current_spriteset) {
		if (!was_on_map && is_on_map) {
			CreateRemotePlayerSprite(rp);
		}
		// Note: sprite removal on map change happens when we change maps
	}
}

void MultiplayerState::HandleSwitchSync(const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	int32_t switch_id = reader.readI32();
	uint8_t value = reader.readU8();

	if (Main_Data::game_switches) {
		Main_Data::game_switches->Set(switch_id, value != 0);
	}
}

void MultiplayerState::HandleVariableSync(const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	int32_t var_id = reader.readI32();
	int32_t value = reader.readI32();

	if (Main_Data::game_variables) {
		Main_Data::game_variables->Set(var_id, value);
	}
}

void MultiplayerState::HandleGodCommand(uint16_t sender_id, const uint8_t* data, size_t len) {
	auto& net = NetManager::Instance();
	if (net.GetMode() != MultiplayerMode::GodMode) return;

	// Only the god player (peer_id 1, the host) can send god commands
	auto* pi = net.FindPeer(sender_id);
	if (!pi || !pi->is_god) return;

	PacketReader reader(data, len);
	reader.readType();

	uint8_t cmd_type = reader.readU8();
	switch (cmd_type) {
		case 0: {
			// Set switch
			int32_t id = reader.readI32();
			uint8_t val = reader.readU8();
			if (Main_Data::game_switches) {
				Main_Data::game_switches->Set(id, val != 0);
			}
			break;
		}
		case 1: {
			// Set variable
			int32_t id = reader.readI32();
			int32_t val = reader.readI32();
			if (Main_Data::game_variables) {
				Main_Data::game_variables->Set(id, val);
			}
			break;
		}
		case 2: {
			// Teleport player
			int32_t map_id = reader.readI32();
			int32_t x = reader.readI32();
			int32_t y = reader.readI32();
			if (Main_Data::game_player) {
				Main_Data::game_player->ReserveTeleport(map_id, x, y, -1, TeleportTarget::eParallelTeleport);
			}
			break;
		}
		default:
			break;
	}
}

void MultiplayerState::SendLocalPlayerPosition() {
	auto& net = NetManager::Instance();
	if (!Main_Data::game_player) return;

	auto* player = Main_Data::game_player.get();

	PacketWriter packet(PacketType::PlayerPosition);
	packet.write(net.GetLocalPeerId());
	packet.write(static_cast<int32_t>(Game_Map::GetMapId()));
	packet.write(static_cast<int32_t>(player->GetX()));
	packet.write(static_cast<int32_t>(player->GetY()));
	packet.write(static_cast<int32_t>(player->GetDirection()));
	packet.write(static_cast<int32_t>(player->GetFacing()));
	packet.write(player->GetSpriteName().empty() ? std::string("") : std::string(player->GetSpriteName()));
	packet.write(static_cast<int32_t>(player->GetSpriteIndex()));

	if (net.IsHost()) {
		net.Broadcast(packet, false); // Unreliable for position updates
	} else {
		net.SendToServer(packet, false);
	}
}

void MultiplayerState::CheckAndSyncSwitches() {
	if (!Main_Data::game_switches) return;

	auto& sw_data = Main_Data::game_switches->GetData();
	auto& net = NetManager::Instance();

	// Initialize tracking on first call
	if (last_switches.empty()) {
		last_switches = sw_data;
		return;
	}

	// Detect changes
	size_t max_size = std::max(sw_data.size(), last_switches.size());
	for (size_t i = 0; i < max_size; ++i) {
		bool cur = (i < sw_data.size()) ? sw_data[i] : false;
		bool prev = (i < last_switches.size()) ? last_switches[i] : false;
		if (cur != prev) {
			PacketWriter packet(PacketType::SwitchSync);
			packet.write(static_cast<int32_t>(i + 1)); // 1-indexed
			packet.write(static_cast<uint8_t>(cur ? 1 : 0));
			net.Broadcast(packet, true);
		}
	}

	last_switches = sw_data;
}

void MultiplayerState::CheckAndSyncVariables() {
	if (!Main_Data::game_variables) return;

	auto& var_data = Main_Data::game_variables->GetData();
	auto& net = NetManager::Instance();

	// Initialize tracking on first call
	if (last_variables.empty()) {
		last_variables = var_data;
		return;
	}

	// Detect changes
	size_t max_size = std::max(var_data.size(), last_variables.size());
	for (size_t i = 0; i < max_size; ++i) {
		int32_t cur = (i < var_data.size()) ? var_data[i] : 0;
		int32_t prev = (i < last_variables.size()) ? last_variables[i] : 0;
		if (cur != prev) {
			PacketWriter packet(PacketType::VariableSync);
			packet.write(static_cast<int32_t>(i + 1)); // 1-indexed
			packet.write(cur);
			net.Broadcast(packet, true);
		}
	}

	last_variables = var_data;
}

void MultiplayerState::CreateRemotePlayerSprite(Game_RemotePlayer* player) {
	if (!current_spriteset || !player) return;
	current_spriteset->AddCharacterSprite(player);
	Output::Debug("Multiplayer: Created sprite for peer {}", player->GetPeerId());
}

void MultiplayerState::RemoveRemotePlayerSprite(uint16_t peer_id) {
	// Sprites will be cleaned up when the spriteset refreshes or map changes
	Output::Debug("Multiplayer: Sprite removal requested for peer {}", peer_id);
}

Game_RemotePlayer* MultiplayerState::GetRemotePlayer(uint16_t peer_id) {
	auto it = remote_players.find(peer_id);
	return (it != remote_players.end()) ? it->second.get() : nullptr;
}

bool MultiplayerState::ShouldInterceptGameOver() {
	if (!active) return false;
	EnterSpectatorMode();
	return true;
}

void MultiplayerState::EnterSpectatorMode() {
	if (spectating) return;

	// Find first remote player on current map as target
	spectate_target_id = 0;
	for (auto& [id, rp] : remote_players) {
		if (rp->IsOnCurrentMap()) {
			spectate_target_id = id;
			break;
		}
	}

	if (spectate_target_id == 0 && !remote_players.empty()) {
		// No remote player on current map, pick any
		spectate_target_id = remote_players.begin()->first;
	}

	if (spectate_target_id == 0) {
		Output::Debug("Multiplayer: No players to spectate");
		return;
	}

	spectating = true;

	// Hide local player
	if (Main_Data::game_player) {
		Main_Data::game_player->SetSpriteGraphic("", 0);
		Main_Data::game_player->SetTransparency(7);
	}

	// Create spectator overlay window
	auto* target = GetRemotePlayer(spectate_target_id);
	std::string target_name = target ? target->GetPlayerName() : "Unknown";
	spectator_window = std::make_unique<Window_Help>(
		0, 0, Player::screen_width, 32);
	spectator_window->SetText(fmt::format("Spectating: {} (Left/Right to switch)", target_name));

	Output::Debug("Multiplayer: Entered spectator mode, watching peer {}", spectate_target_id);
}

void MultiplayerState::ExitSpectatorMode() {
	if (!spectating) return;
	spectating = false;
	spectate_target_id = 0;
	spectator_window.reset();
	Output::Debug("Multiplayer: Exited spectator mode");
}

void MultiplayerState::CycleSpectateTarget(int direction) {
	if (remote_players.empty()) return;

	// Build list of valid targets (on current map preferred)
	std::vector<uint16_t> targets;
	for (auto& [id, rp] : remote_players) {
		if (rp->IsOnCurrentMap()) {
			targets.push_back(id);
		}
	}
	// If no one on current map, use all
	if (targets.empty()) {
		for (auto& [id, rp] : remote_players) {
			targets.push_back(id);
		}
	}
	if (targets.empty()) return;

	// Find current index
	int cur_idx = 0;
	for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
		if (targets[i] == spectate_target_id) {
			cur_idx = i;
			break;
		}
	}

	// Cycle
	cur_idx += direction;
	if (cur_idx < 0) cur_idx = static_cast<int>(targets.size()) - 1;
	if (cur_idx >= static_cast<int>(targets.size())) cur_idx = 0;

	spectate_target_id = targets[cur_idx];

	// Update overlay text
	auto* target = GetRemotePlayer(spectate_target_id);
	if (spectator_window && target) {
		spectator_window->SetText(fmt::format("Spectating: {} (Left/Right to switch)", target->GetPlayerName()));
	}
}

void MultiplayerState::UpdateSpectator() {
	auto* target = GetRemotePlayer(spectate_target_id);
	if (!target) {
		CycleSpectateTarget(1);
		target = GetRemotePlayer(spectate_target_id);
		if (!target) {
			ExitSpectatorMode();
			return;
		}
	}

	// If target is on a different map, teleport spectator to follow them
	if (!target->IsOnCurrentMap()) {
		if (Main_Data::game_player && !Main_Data::game_player->IsPendingTeleport()) {
			int target_map = target->GetMapId();
			int target_x = target->GetX();
			int target_y = target->GetY();
			if (target_map > 0) {
				Main_Data::game_player->ReserveTeleport(
					target_map, target_x, target_y, -1,
					TeleportTarget::eParallelTeleport);
			}
		}
		return;
	}

	// Center camera on spectated player
	if (Main_Data::game_player) {
		auto* player = Main_Data::game_player.get();
		Game_Map::SetPositionX(target->GetSpriteX() - player->GetPanX(), false);
		Game_Map::SetPositionY(target->GetSpriteY() - player->GetPanY(), false);
	}

	// Handle cycling input
	if (Input::IsTriggered(Input::LEFT)) {
		CycleSpectateTarget(-1);
	}
	if (Input::IsTriggered(Input::RIGHT)) {
		CycleSpectateTarget(1);
	}
}

// ---- Battle sync ----

void MultiplayerState::OnBattleStarted(int troop_id, int terrain_id, bool first_strike, bool allow_escape) {
	in_battle = true;
	battle_troop_id = troop_id;
	battle_terrain_id = terrain_id;
	battle_first_strike = first_strike;
	battle_allow_escape = allow_escape;
	remote_battle_actions.clear();
	turn_sync_received = false;
	local_escape_voted = false;
	local_escape_wants = false;
	remote_escape_voted = false;
	remote_escape_wants = false;
	battle_seed = Rand::GetRandomNumber(0, 0x7FFFFFFF);
	Rand::SeedRandomNumberGenerator(battle_seed);

	auto& net = NetManager::Instance();
	PacketWriter pw(PacketType::BattleStart);
	pw.write(net.GetLocalPeerId());
	pw.write(static_cast<int32_t>(troop_id));
	pw.write(static_cast<int32_t>(terrain_id));
	pw.write(static_cast<uint8_t>(first_strike ? 1 : 0));
	pw.write(static_cast<uint8_t>(allow_escape ? 1 : 0));
	pw.write(battle_seed);

	if (net.IsHost()) {
		net.Broadcast(pw, true);

		// In Chaotix mode, also force others into battle
		auto mode = net.GetMode();
		if (mode == MultiplayerMode::Chaotix) {
			PacketWriter fpw(PacketType::BattleForce);
			fpw.write(static_cast<int32_t>(troop_id));
			fpw.write(static_cast<int32_t>(terrain_id));
			fpw.write(static_cast<uint8_t>(first_strike ? 1 : 0));
			fpw.write(static_cast<uint8_t>(allow_escape ? 1 : 0));
			fpw.write(battle_seed);
			net.Broadcast(fpw, true);
		}
	} else {
		net.SendToServer(pw, true);
	}

	Output::Debug("Multiplayer: Battle started, troop={}, seed={}", troop_id, battle_seed);
}

void MultiplayerState::OnBattleEnded() {
	in_battle = false;
	remote_battle_actions.clear();
	turn_sync_received = false;
	local_escape_voted = false;
	local_escape_wants = false;
	remote_escape_voted = false;
	remote_escape_wants = false;

	auto& net = NetManager::Instance();
	PacketWriter pw(PacketType::BattleEnd);
	pw.write(net.GetLocalPeerId());

	if (net.IsHost()) {
		net.Broadcast(pw, true);
	} else {
		net.SendToServer(pw, true);
	}

	Output::Debug("Multiplayer: Battle ended");
}

void MultiplayerState::HandleBattleStart(uint16_t sender_id, const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	uint16_t peer_id = reader.readU16();
	int32_t troop_id = reader.readI32();
	int32_t terrain_id = reader.readI32();
	uint8_t first_strike = reader.readU8();
	uint8_t allow_escape = reader.readU8();
	int32_t seed = reader.readI32();

	auto& net = NetManager::Instance();

	// If host, relay to other clients (with seed)
	if (net.IsHost()) {
		PacketWriter rpw(PacketType::BattleStart);
		rpw.write(peer_id);
		rpw.write(troop_id);
		rpw.write(terrain_id);
		rpw.write(first_strike);
		rpw.write(allow_escape);
		rpw.write(seed);
		net.Broadcast(rpw, true);

		// In Chaotix mode, force all into battle
		auto mode = net.GetMode();
		if (mode == MultiplayerMode::Chaotix) {
			PacketWriter fpw(PacketType::BattleForce);
			fpw.write(troop_id);
			fpw.write(terrain_id);
			fpw.write(first_strike);
			fpw.write(allow_escape);
			fpw.write(seed);
			net.Broadcast(fpw, true);
		}
	}

	battle_troop_id = troop_id;
	battle_terrain_id = terrain_id;
	battle_first_strike = first_strike != 0;
	battle_allow_escape = allow_escape != 0;
	battle_initiator_id = peer_id;
	battle_seed = seed;
	Rand::SeedRandomNumberGenerator(battle_seed);

	auto mode = net.GetMode();
	if (mode == MultiplayerMode::TeamParty) {
		// Team mode: show invite prompt
		if (!in_battle) {
			pending_battle_invite = true;
			battle_invite_window = std::make_unique<Window_Help>(
				0, Player::screen_height / 2 - 16,
				Player::screen_width, 32);
			battle_invite_window->SetText("A battle started! Press [Enter] to join, [Esc] to skip");
			Output::Debug("Multiplayer: Battle invite received from peer {}, troop={}", peer_id, troop_id);
		}
	}
	// Chaotix force is handled via BattleForce packet
}

void MultiplayerState::HandleBattleJoin(uint16_t sender_id, const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();
	uint16_t peer_id = reader.readU16();
	Output::Debug("Multiplayer: Peer {} joined the battle", peer_id);
}

void MultiplayerState::HandleBattleForce(const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	int32_t troop_id = reader.readI32();
	int32_t terrain_id = reader.readI32();
	uint8_t first_strike = reader.readU8();
	uint8_t allow_escape = reader.readU8();
	int32_t seed = reader.readI32();

	// Don't force if we're already in battle
	if (in_battle) return;

	battle_troop_id = troop_id;
	battle_terrain_id = terrain_id;
	battle_first_strike = first_strike != 0;
	battle_allow_escape = allow_escape != 0;
	battle_seed = seed;
	Rand::SeedRandomNumberGenerator(battle_seed);
	forced_battle = true;
	Output::Debug("Multiplayer: Forced into battle, troop={}, seed={}", troop_id, seed);
}

void MultiplayerState::HandleBattleAction(const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	uint16_t actor_id = reader.readU16();
	uint8_t action_type = reader.readU8();
	int32_t target_id = reader.readI32();
	int32_t skill_or_item_id = reader.readI32();

	RemoteBattleAction action;
	action.actor_id = actor_id;
	action.action_type = action_type;
	action.target_id = target_id;
	action.skill_or_item_id = skill_or_item_id;

	remote_battle_actions[actor_id] = action;
	Output::Debug("Multiplayer: Received battle action for actor {}: type={}", actor_id, action_type);
}

void MultiplayerState::HandleBattleEnd(const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();
	// Other player's battle ended
	uint16_t peer_id = reader.readU16();
	Output::Debug("Multiplayer: Peer {} ended their battle", peer_id);
}

void MultiplayerState::AcceptBattleInvite() {
	pending_battle_invite = false;
	battle_invite_window.reset();

	auto& net = NetManager::Instance();
	PacketWriter pw(PacketType::BattleJoin);
	pw.write(net.GetLocalPeerId());
	if (net.IsHost()) {
		net.Broadcast(pw, true);
	} else {
		net.SendToServer(pw, true);
	}

	// Enter battle locally
	in_battle = true;
	remote_battle_actions.clear();

	BattleArgs args;
	args.troop_id = battle_troop_id;
	args.terrain_id = battle_terrain_id;
	args.first_strike = battle_first_strike;
	args.allow_escape = battle_allow_escape;
	args.formation = lcf::rpg::System::BattleFormation_terrain;
	args.condition = lcf::rpg::System::BattleCondition_none;

	Game_Map::SetupBattle(args);
	args.on_battle_end = [](BattleResult) {
		MultiplayerState::Instance().OnBattleEnded();
	};

	Scene::instance->SetRequestedScene(Scene_Battle::Create(std::move(args)));
	Output::Debug("Multiplayer: Joining battle, troop={}", battle_troop_id);
}

void MultiplayerState::DeclineBattleInvite() {
	pending_battle_invite = false;
	battle_invite_window.reset();
	Output::Debug("Multiplayer: Declined battle invite");
}

void MultiplayerState::ConsumeForcedBattle() {
	forced_battle = false;

	in_battle = true;
	remote_battle_actions.clear();

	BattleArgs args;
	args.troop_id = battle_troop_id;
	args.terrain_id = battle_terrain_id;
	args.first_strike = battle_first_strike;
	args.allow_escape = false; // Can't escape forced battle
	args.formation = lcf::rpg::System::BattleFormation_terrain;
	args.condition = lcf::rpg::System::BattleCondition_none;

	Game_Map::SetupBattle(args);
	args.on_battle_end = [](BattleResult) {
		MultiplayerState::Instance().OnBattleEnded();
	};

	Scene::instance->SetRequestedScene(Scene_Battle::Create(std::move(args)));
	Output::Debug("Multiplayer: Forced into battle, troop={}", battle_troop_id);
}

void MultiplayerState::UpdateBattleInvite() {
	if (!pending_battle_invite) return;

	if (Input::IsTriggered(Input::DECISION)) {
		if (Main_Data::game_system) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		}
		AcceptBattleInvite();
		return;
	}

	if (Input::IsTriggered(Input::CANCEL)) {
		if (Main_Data::game_system) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		}
		DeclineBattleInvite();
		return;
	}
}

void MultiplayerState::SendBattleAction(uint16_t actor_id, uint8_t action_type, int32_t target_id, int32_t skill_or_item_id) {
	auto& net = NetManager::Instance();
	PacketWriter pw(PacketType::BattleAction);
	pw.write(actor_id);
	pw.write(action_type);
	pw.write(target_id);
	pw.write(skill_or_item_id);

	if (net.IsHost()) {
		net.Broadcast(pw, true);
	} else {
		net.SendToServer(pw, true);
	}
}

bool MultiplayerState::HasRemoteAction(uint16_t actor_id) const {
	return remote_battle_actions.count(actor_id) > 0;
}

MultiplayerState::RemoteBattleAction MultiplayerState::ConsumeRemoteAction(uint16_t actor_id) {
	auto it = remote_battle_actions.find(actor_id);
	if (it != remote_battle_actions.end()) {
		RemoteBattleAction action = it->second;
		remote_battle_actions.erase(it);
		return action;
	}
	return {};
}

bool MultiplayerState::IsRemoteActor(Game_Actor* actor) const {
	if (!active) return false;
	auto& net = NetManager::Instance();
	auto mode = net.GetMode();
	if (mode != MultiplayerMode::TeamParty && mode != MultiplayerMode::Chaotix) return false;

	int local_index = static_cast<int>(net.GetLocalPeerId()) - 1;
	auto actors = Main_Data::game_party->GetActors();
	for (int i = 0; i < static_cast<int>(actors.size()); ++i) {
		if (actors[i] == actor) {
			return i != local_index;
		}
	}
	return false;
}

int MultiplayerState::GetLocalActorIndex() const {
	auto& net = NetManager::Instance();
	return static_cast<int>(net.GetLocalPeerId()) - 1;
}

void MultiplayerState::SyncEventPositions() {
	auto& net = NetManager::Instance();
	if (!net.IsHost()) return;

	auto& events = Game_Map::GetEvents();
	if (events.empty()) return;

	int32_t current_map_id = Game_Map::GetMapId();

	// Pack event positions: [map_id:i32][count:u16] then per event [id:u16][x:i32][y:i32][dir:u8][facing:u8][remaining:i32]
	// Split into chunks to avoid oversized packets (max ~50 events per packet)
	static constexpr int MAX_EVENTS_PER_PACKET = 50;

	for (size_t start = 0; start < events.size(); start += MAX_EVENTS_PER_PACKET) {
		size_t end = std::min(start + MAX_EVENTS_PER_PACKET, events.size());
		uint16_t count = 0;

		// Count active events in this chunk
		for (size_t i = start; i < end; ++i) {
			auto& ev = events[i];
			if (ev.GetActivePage() != nullptr) {
				count++;
			}
		}

		if (count == 0) continue;

		PacketWriter pw(PacketType::EventSync);
		pw.write(current_map_id);
		pw.write(count);

		for (size_t i = start; i < end; ++i) {
			auto& ev = events[i];
			if (ev.GetActivePage() == nullptr) continue;

			pw.write(static_cast<uint16_t>(ev.GetId()));
			pw.write(static_cast<int32_t>(ev.GetX()));
			pw.write(static_cast<int32_t>(ev.GetY()));
			pw.write(static_cast<uint8_t>(ev.GetDirection()));
			pw.write(static_cast<uint8_t>(ev.GetFacing()));
			pw.write(static_cast<int32_t>(ev.GetRemainingStep()));
		}

		net.Broadcast(pw, false); // unreliable for performance
	}
}

void MultiplayerState::HandleEventSync(const uint8_t* data, size_t len) {
	auto& net = NetManager::Instance();
	// Only clients apply event sync from host
	if (net.IsHost()) return;

	PacketReader reader(data, len);
	reader.readType();

	int32_t map_id = reader.readI32();

	// Ignore event sync for a different map
	if (map_id != Game_Map::GetMapId()) return;

	uint16_t count = reader.readU16();

	for (uint16_t i = 0; i < count; ++i) {
		uint16_t event_id = reader.readU16();
		int32_t x = reader.readI32();
		int32_t y = reader.readI32();
		uint8_t direction = reader.readU8();
		uint8_t facing = reader.readU8();
		int32_t remaining_step = reader.readI32();

		Game_Event* ev = Game_Map::GetEvent(event_id);
		if (!ev) continue;

		ev->SetX(x);
		ev->SetY(y);
		ev->SetDirection(direction);
		ev->SetFacing(facing);
		ev->SetRemainingStep(remaining_step);
	}
}

void MultiplayerState::BroadcastTurnSync(int32_t seed) {
	auto& net = NetManager::Instance();
	PacketWriter pw(PacketType::BattleTurnSync);
	pw.write(seed);

	if (net.IsHost()) {
		net.Broadcast(pw, true);
	} else {
		net.SendToServer(pw, true);
	}

	// Host also applies the sync locally
	turn_sync_seed = seed;
	turn_sync_received = true;
	Output::Debug("Multiplayer: Broadcast turn sync, seed={}", seed);
}

int32_t MultiplayerState::ConsumeTurnSync() {
	turn_sync_received = false;
	return turn_sync_seed;
}

void MultiplayerState::HandleBattleTurnSync(const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	int32_t seed = reader.readI32();

	auto& net = NetManager::Instance();
	// If host, relay to other clients
	if (net.IsHost()) {
		PacketWriter rpw(PacketType::BattleTurnSync);
		rpw.write(seed);
		net.Broadcast(rpw, true);
	}

	turn_sync_seed = seed;
	turn_sync_received = true;
	Output::Debug("Multiplayer: Received turn sync, seed={}", seed);
}

void MultiplayerState::VoteEscape(bool wants_escape) {
	local_escape_voted = true;
	local_escape_wants = wants_escape;

	auto& net = NetManager::Instance();
	PacketWriter pw(PacketType::BattleEscapeVote);
	pw.write(static_cast<uint8_t>(wants_escape ? 1 : 0));

	if (net.IsHost()) {
		net.Broadcast(pw, true);
	} else {
		net.SendToServer(pw, true);
	}

	Output::Debug("Multiplayer: Voted escape={}", wants_escape);
}

bool MultiplayerState::HasEscapeVoteResult() const {
	return local_escape_voted && remote_escape_voted;
}

bool MultiplayerState::IsEscapeApproved() const {
	return local_escape_wants && remote_escape_wants;
}

void MultiplayerState::ResetEscapeVote() {
	local_escape_voted = false;
	local_escape_wants = false;
	remote_escape_voted = false;
	remote_escape_wants = false;
}

void MultiplayerState::HandleBattleEscapeVote(uint16_t sender_id, const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	uint8_t wants = reader.readU8();

	auto& net = NetManager::Instance();
	// If host, relay to other clients
	if (net.IsHost()) {
		PacketWriter rpw(PacketType::BattleEscapeVote);
		rpw.write(wants);
		net.Broadcast(rpw, true);
	}

	remote_escape_voted = true;
	remote_escape_wants = (wants != 0);
	Output::Debug("Multiplayer: Received escape vote={}", wants != 0);
}

} // namespace Chaos
