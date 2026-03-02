#pragma once

#include "extensions/extensions.h"
#include "mxtypes.h"

#include <map>
#include <string>

class LegoEntity;
class LegoWorld;

namespace Multiplayer
{
class NetworkManager;
class NetworkTransport;
} // namespace Multiplayer

namespace Extensions
{

class MultiplayerExt {
public:
	static void Initialize();
	static MxBool HandleWorldEnable(LegoWorld* p_world, MxBool p_enable);

	// Intercepts click notifications on plants/buildings for multiplayer routing.
	// Returns TRUE if the click should be suppressed locally (non-host).
	static MxBool HandleEntityNotify(LegoEntity* p_entity);

	static std::map<std::string, std::string> options;
	LEGO1_EXPORT static bool enabled;

	static std::string relayUrl;
	static std::string room;

	// Returns true if the multiplayer connection was rejected (e.g. room full).
	LEGO1_EXPORT static MxBool CheckRejected();

	static void SetNetworkManager(Multiplayer::NetworkManager* p_networkManager);
	static Multiplayer::NetworkManager* GetNetworkManager();

private:
	static Multiplayer::NetworkManager* s_networkManager;
	static Multiplayer::NetworkTransport* s_transport;
};

#ifdef EXTENSIONS
inline const auto HandleWorldEnable = &MultiplayerExt::HandleWorldEnable;
inline const auto HandleEntityNotify = &MultiplayerExt::HandleEntityNotify;
inline const auto CheckRejected = &MultiplayerExt::CheckRejected;
#else
inline const decltype(&MultiplayerExt::HandleWorldEnable) HandleWorldEnable = nullptr;
inline const decltype(&MultiplayerExt::HandleEntityNotify) HandleEntityNotify = nullptr;
inline const decltype(&MultiplayerExt::CheckRejected) CheckRejected = nullptr;
#endif

}; // namespace Extensions
