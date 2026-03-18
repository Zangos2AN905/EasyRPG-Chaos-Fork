/*
 * Chaos Fork: Multiplayer Mode Definitions
 */

#ifndef EP_CHAOS_MULTIPLAYER_MODE_H
#define EP_CHAOS_MULTIPLAYER_MODE_H

#include <string>

namespace Chaos {

enum class MultiplayerMode {
	Normal = 0,    // Non-team mode, unlimited players
	TeamParty,     // Team up, switches/variables sync, max 4 players
	Chaotix,       // Like TeamParty but players must stay close
	Pandora,       // Like Normal but switches/variables sync
	GodMode,       // One controller, rest play normally
	Horror,        // Dark survival mode with flashlight + battery
	Count
};

struct ModeProperties {
	const char* name;
	const char* description;
	int max_players;       // 0 = unlimited
	bool sync_switches;
	bool sync_variables;
	bool sync_actor_states; // sync HP/SP/conditions between players
	bool proximity_required;
	bool has_god_player;
};

inline const ModeProperties& GetModeProperties(MultiplayerMode mode) {
	static const ModeProperties props[] = {
		{ "Normal",     "Normal non-team mode. Unlimited players.",                      0, false, false, false, false, false },
		{ "Team Party", "Team up! Switches and variables sync. Max 4 players.",          4, true,  true,  false, false, false },
		{ "Chaotix",    "Team mode, but players must stay close together. Max 4.",       4, true,  true,  true,  true,  false },
		{ "Pandora",    "Normal mode, but switches and variables sync.",                 0, true,  true,  false, false, false },
		{ "God Mode",   "One player controls the game. Others play normally.",           0, false, false, false, false, true  },
		{ "Horror",     "SURVIVE.",                                                    0, false, false, false, false, false },
	};
	return props[static_cast<int>(mode)];
}

inline int GetModeCount() {
	return static_cast<int>(MultiplayerMode::Count);
}

} // namespace Chaos

#endif
