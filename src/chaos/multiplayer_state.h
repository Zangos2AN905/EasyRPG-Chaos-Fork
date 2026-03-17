/*
 * Chaos Fork: Multiplayer State Manager
 * Central coordinator for multiplayer game state synchronization.
 */

#ifndef EP_CHAOS_MULTIPLAYER_STATE_H
#define EP_CHAOS_MULTIPLAYER_STATE_H

#include <map>
#include <memory>
#include <vector>
#include "chaos/multiplayer_mode.h"
#include "chaos/game_multiplayer.h"
#include "chaos/drawable_rubber_band.h"

class Game_Actor;
class Spriteset_Map;
class Window_Help;

namespace Chaos {

class MultiplayerState {
public:
	static MultiplayerState& Instance();

	/** Called each frame from the main loop when multiplayer is active */
	void Update();

	/** Called when entering a map scene */
	void OnMapLoaded(Spriteset_Map* spriteset);

	/** Called when leaving a map scene */
	void OnMapUnloaded();

	/** Enable multiplayer (called when game starts in MP mode) */
	void StartMultiplayer();

	/** Disable multiplayer */
	void StopMultiplayer();

	/** Whether multiplayer is currently active */
	bool IsActive() const { return active; }

	/** Whether the host has disconnected (for clients) */
	bool IsHostDisconnected() const { return host_lost; }
	void ClearHostDisconnected() { host_lost = false; }

	/** Get a remote player by peer ID */
	Game_RemotePlayer* GetRemotePlayer(uint16_t peer_id);

	/** Get all remote players */
	const std::map<uint16_t, std::unique_ptr<Game_RemotePlayer>>& GetRemotePlayers() const {
		return remote_players;
	}

	/** Spectator mode */
	bool IsSpectating() const { return spectating; }
	void EnterSpectatorMode();
	void ExitSpectatorMode();
	void CycleSpectateTarget(int direction);

	/** Called from game over triggers - returns true if multiplayer is active and spectator mode was entered */
	bool ShouldInterceptGameOver();

	/** Battle sync */
	void OnBattleStarted(int troop_id, int terrain_id, bool first_strike, bool allow_escape);
	void OnBattleEnded(int result);
	bool IsInMultiplayerBattle() const { return in_battle; }
	bool HasPendingBattleInvite() const { return pending_battle_invite; }
	bool HasForcedBattle() const { return forced_battle; }

	/** Chaotix map sync */
	bool HasForcedMapChange() const { return forced_map_change; }
	void ConsumeForcedMapChange();
	int GetBattleTroopId() const { return battle_troop_id; }
	int GetBattleTerrainId() const { return battle_terrain_id; }
	bool GetBattleFirstStrike() const { return battle_first_strike; }
	bool GetBattleAllowEscape() const { return battle_allow_escape; }
	void AcceptBattleInvite();
	void DeclineBattleInvite();
	void ConsumeForcedBattle();
	int32_t GetBattleSeed() const { return battle_seed; }

	/** Battle action sync */
	struct RemoteBattleAction {
		uint16_t actor_id = 0;
		uint8_t action_type = 0; // 0=none, 1=attack, 2=skill, 3=item, 4=defend
		int32_t target_id = 0;
		int32_t skill_or_item_id = 0;
	};
	void SendBattleAction(uint16_t actor_id, uint8_t action_type, int32_t target_id, int32_t skill_or_item_id);
	bool HasRemoteAction(uint16_t actor_id) const;
	RemoteBattleAction ConsumeRemoteAction(uint16_t actor_id);
	bool IsRemoteActor(Game_Actor* actor) const;
	int GetLocalActorIndex() const;

	/** Battle turn sync - ensures both clients enter execution phase together */
	void BroadcastTurnSync(int32_t seed);
	bool HasTurnSync() const { return turn_sync_received; }
	int32_t ConsumeTurnSync();

	/** Escape voting - both players must choose escape for it to proceed */
	void VoteEscape(bool wants_escape);
	bool HasEscapeVoteResult() const;
	bool IsEscapeApproved() const;
	void ResetEscapeVote();

	/** Remote battle end sync - force local battle to end when remote ends */
	bool HasRemoteBattleEnded() const { return remote_battle_ended; }
	int GetRemoteBattleResult() const { return remote_battle_result; }
	void ConsumeRemoteBattleEnd() { remote_battle_ended = false; }

	/** Chaotix rubber band - returns true if player movement should be blocked (too far from partner) */
	bool ShouldBlockMovement(int dir) const;

	/** Chaotix event trigger sync - called when local player triggers an event */
	void OnEventTriggered(int event_id, bool by_decision_key);

private:
	MultiplayerState() = default;

	void SetupCallbacks();

	// Network event handlers
	void OnPlayerConnected(uint16_t peer_id);
	void OnPlayerDisconnected(uint16_t peer_id);
	void OnPacketReceived(uint16_t sender_id, const uint8_t* data, size_t len);

	// Packet handlers
	void HandlePlayerPosition(uint16_t sender_id, const uint8_t* data, size_t len);
	void HandleSwitchSync(const uint8_t* data, size_t len);
	void HandleVariableSync(const uint8_t* data, size_t len);
	void HandleGodCommand(uint16_t sender_id, const uint8_t* data, size_t len);
	void HandleBattleStart(uint16_t sender_id, const uint8_t* data, size_t len);
	void HandleBattleJoin(uint16_t sender_id, const uint8_t* data, size_t len);
	void HandleBattleForce(const uint8_t* data, size_t len);
	void HandleBattleAction(const uint8_t* data, size_t len);
	void HandleBattleEnd(const uint8_t* data, size_t len);
	void HandleEventSync(const uint8_t* data, size_t len);
	void HandleBattleTurnSync(const uint8_t* data, size_t len);
	void HandleBattleEscapeVote(uint16_t sender_id, const uint8_t* data, size_t len);
	void HandleMapChangeForce(const uint8_t* data, size_t len);
	void HandleActorStateSync(const uint8_t* data, size_t len);
	void HandleEventTriggerSync(const uint8_t* data, size_t len);

	// Sync functions
	void SendLocalPlayerPosition();
	void CheckAndSyncSwitches();
	void CheckAndSyncVariables();
	void SyncEventPositions();
	void CheckAndSyncActorStates();

	// Sprite management
	void CreateRemotePlayerSprite(Game_RemotePlayer* player);
	void RemoveRemotePlayerSprite(uint16_t peer_id);

	// Spectator
	void UpdateSpectator();
	void UpdateBattleInvite();

	// Chaotix rubber band
	void UpdateRubberBand();
	void CreateRubberBandDrawable();
	void DestroyRubberBandDrawable();
	void ApplyRubberBandPull();

	bool active = false;
	bool host_lost = false;
	Spriteset_Map* current_spriteset = nullptr;

	// Remote players indexed by peer_id
	std::map<uint16_t, std::unique_ptr<Game_RemotePlayer>> remote_players;

	// Spectator state
	bool spectating = false;
	uint16_t spectate_target_id = 0;
	std::unique_ptr<Window_Help> spectator_window;

	// Battle state
	bool in_battle = false;
	bool pending_battle_invite = false;
	bool forced_battle = false;
	int battle_troop_id = 0;
	int battle_terrain_id = 0;
	bool battle_first_strike = false;
	bool battle_allow_escape = true;
	uint16_t battle_initiator_id = 0;
	int32_t battle_seed = 0;
	std::unique_ptr<Window_Help> battle_invite_window;
	std::map<uint16_t, RemoteBattleAction> remote_battle_actions;

	// Turn sync state
	bool turn_sync_received = false;
	int32_t turn_sync_seed = 0;

	// Escape vote state
	bool local_escape_voted = false;
	bool local_escape_wants = false;
	bool remote_escape_voted = false;
	bool remote_escape_wants = false;

	// Remote battle end sync
	bool remote_battle_ended = false;
	int remote_battle_result = 0; // maps to BattleResult enum

	// Chaotix forced map change
	bool forced_map_change = false;
	int forced_map_id = 0;
	int forced_map_x = 0;
	int forced_map_y = 0;

	// For detecting switch/variable changes
	std::vector<bool> last_switches;
	std::vector<int32_t> last_variables;

	// For detecting actor state changes (chaotix sync)
	struct ActorSnapshot {
		int hp = 0;
		int sp = 0;
		std::vector<int16_t> states;
	};
	std::map<int, ActorSnapshot> last_actor_states;
	int actor_state_sync_counter = 0;
	static constexpr int ACTOR_STATE_SYNC_INTERVAL = 10; // Send every 10 frames

	// Rate limiting for position updates
	int position_send_counter = 0;
	static constexpr int POSITION_SEND_INTERVAL = 3; // Send every 3 frames
	int event_sync_counter = 0;
	static constexpr int EVENT_SYNC_INTERVAL = 6; // Send every 6 frames

	// Chaotix rubber band
	std::unique_ptr<Drawable_RubberBand> rubber_band;
	static constexpr int RUBBER_BAND_RANGE = 5;  // tiles before band starts stretching
	static constexpr int RUBBER_BAND_MAX = 8;    // tiles max distance (blocks movement)
};

} // namespace Chaos

#endif
