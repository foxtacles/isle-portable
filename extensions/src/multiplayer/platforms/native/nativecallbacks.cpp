#ifndef __EMSCRIPTEN__

#include "extensions/multiplayer/platforms/native/nativecallbacks.h"

#include <SDL3/SDL_log.h>

namespace Multiplayer
{

void NativeCallbacks::OnPlayerCountChanged(int p_count)
{
	if (p_count < 0) {
		SDL_Log("[Multiplayer] Left multiplayer world");
	}
	else {
		SDL_Log("[Multiplayer] Player count changed: %d", p_count);
	}
}

} // namespace Multiplayer

#endif // !__EMSCRIPTEN__
