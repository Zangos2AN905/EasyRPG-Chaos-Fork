/*
 * Chaos Fork: Multiplayer Wait Scene
 * Shown to clients while the host picks a save/new game.
 */

#ifndef EP_CHAOS_SCENE_MULTIPLAYER_WAIT_H
#define EP_CHAOS_SCENE_MULTIPLAYER_WAIT_H

#include "scene.h"
#include "window_help.h"
#include <memory>

namespace Chaos {

class Scene_MultiplayerWait : public Scene {
public:
	Scene_MultiplayerWait();

	void Start() override;
	void vUpdate() override;

private:
	std::unique_ptr<Window_Help> status_window;
	int timer = 0;
};

} // namespace Chaos

#endif
