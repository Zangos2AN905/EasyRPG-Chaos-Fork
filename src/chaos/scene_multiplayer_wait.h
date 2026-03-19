/*
 * Chaos Fork: Multiplayer Wait Scene
 * Shown to clients while the host picks a save/new game.
 * Also handles game file download when host's game isn't available locally.
 */

#ifndef EP_CHAOS_SCENE_MULTIPLAYER_WAIT_H
#define EP_CHAOS_SCENE_MULTIPLAYER_WAIT_H

#include "scene.h"
#include "window_help.h"
#include <memory>
#include <vector>

namespace Chaos {

class Scene_MultiplayerWait : public Scene {
public:
	Scene_MultiplayerWait();

	void Start() override;
	void vUpdate() override;

private:
	/** Check whether the host's game exists locally. */
	bool IsHostGameAvailable();

	/** Switch the game filesystem to the downloaded/found game and load it. */
	void SwitchToHostGame(const std::string& game_path);

	/** Start the actual game after download or match. */
	void StartClientGame();

	enum class WaitState {
		WaitingForHost,  // Waiting for HostMapReady
		Downloading,     // Downloading game files from host
		DownloadDone,    // Download finished, loading game
		Ready,           // Game is loaded, starting
	};

	WaitState wait_state = WaitState::WaitingForHost;

	std::unique_ptr<Window_Help> status_window;
	int timer = 0;

	// Stored host data from HostMapReady (used after download completes)
	int pending_map_id = 0;
	int pending_map_x = 0;
	int pending_map_y = 0;
	bool has_pending_host_data = false;
};

} // namespace Chaos

#endif
