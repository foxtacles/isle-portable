#pragma once

#ifndef __EMSCRIPTEN__

#include "extensions/multiplayer/platformcallbacks.h"

namespace Multiplayer
{

class NativeCallbacks : public PlatformCallbacks {
public:
	void OnPlayerCountChanged(int p_count) override;
};

} // namespace Multiplayer

#endif // !__EMSCRIPTEN__
