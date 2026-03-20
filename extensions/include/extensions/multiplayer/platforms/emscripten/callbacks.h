#pragma once

#ifdef __EMSCRIPTEN__

#include "extensions/multiplayer/platformcallbacks.h"

namespace Multiplayer
{

class EmscriptenCallbacks : public PlatformCallbacks {
public:
	void OnPlayerCountChanged(int p_count) override;
	void OnThirdPersonChanged(bool p_enabled) override;
	void OnNameBubblesChanged(bool p_enabled) override;
	void OnAllowCustomizeChanged(bool p_enabled) override;
	void OnConnectionStatusChanged(int p_status) override;
	void OnNearestLocationChanged(int16_t p_location, uint16_t p_animCount) override;
};

} // namespace Multiplayer

#endif // __EMSCRIPTEN__
