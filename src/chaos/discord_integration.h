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

namespace Chaos {

/**
 * Discord Rich Presence integration.
 * Shows the current game state in the user's Discord profile.
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

} // namespace DiscordIntegration
} // namespace Chaos

#endif
