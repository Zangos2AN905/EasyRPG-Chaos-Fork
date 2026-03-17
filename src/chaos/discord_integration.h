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

#ifndef EP_CHAOS_DISCORD_INTEGRATION_H
#define EP_CHAOS_DISCORD_INTEGRATION_H

#include <string>
#include <functional>

namespace Chaos {

/**
 * Discord Rich Presence integration.
 * Shows the current game state in the user's Discord profile.
 * Supports join/invite via Discord and retrieving the Discord username.
 */
namespace DiscordIntegration {

	/** Initialize Discord RPC. Call once at startup. */
	void Initialize();

	/** Shut down Discord RPC. Call once at exit. */
	void Shutdown();

	/**
	 * Update the Rich Presence based on the current scene/game state.
	 * Internally rate-limited so it's safe to call every frame.
	 */
	void Update();

	/** @return whether Discord Rich Presence is enabled */
	bool IsEnabled();

	/** Enable or disable Discord Rich Presence at runtime */
	void SetEnabled(bool enabled);

	/** @return true if we received the local Discord user info */
	bool HasDiscordUser();

	/** @return the local Discord username (empty if not connected) */
	const std::string& GetDiscordUsername();

	/** @return the local Discord user ID (empty if not connected) */
	const std::string& GetDiscordUserId();

	/**
	 * Set the join secret for Discord RPC (the relay room code).
	 * When set, Discord will show a "Join" button on the user's profile.
	 * Pass empty string to clear.
	 */
	void SetJoinSecret(const std::string& secret);

	/**
	 * Set party info for Discord RPC display.
	 * @param party_id unique identifier for the party
	 * @param current_size number of players in the party
	 * @param max_size maximum number of players
	 */
	void SetPartyInfo(const std::string& party_id, int current_size, int max_size);

	/** Clear join/party info (call when leaving multiplayer) */
	void ClearMultiplayerPresence();

	/**
	 * Set a callback for when someone joins via Discord.
	 * The callback receives the join secret (room code).
	 */
	void SetJoinCallback(std::function<void(const std::string&)> callback);

} // namespace DiscordIntegration
} // namespace Chaos

#endif
