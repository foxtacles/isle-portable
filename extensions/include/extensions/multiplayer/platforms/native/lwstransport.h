#pragma once

#ifndef __EMSCRIPTEN__

#include "extensions/multiplayer/networktransport.h"

#include <deque>
#include <string>
#include <vector>

struct lws_context;
struct lws;

namespace Multiplayer
{

class LwsTransport : public NetworkTransport {
public:
	LwsTransport(const std::string& p_relayBaseUrl);
	~LwsTransport() override;

	void Connect(const char* p_roomId) override;
	void Disconnect() override;
	bool IsConnected() const override;
	bool WasDisconnected() const override;
	bool WasRejected() const override;
	void Send(const uint8_t* p_data, size_t p_length) override;
	size_t Receive(std::function<void(const uint8_t*, size_t)> p_callback) override;

	// Called from static lws callback trampoline
	int HandleLwsEvent(struct lws* p_wsi, int p_reason, void* p_in, size_t p_len);

private:
	std::string m_relayBaseUrl;
	struct lws_context* m_context;
	struct lws* m_wsi;
	bool m_connected;
	bool m_disconnected;
	bool m_wasEverConnected;

	std::deque<std::vector<uint8_t>> m_sendQueue;
	std::deque<std::vector<uint8_t>> m_recvQueue;
	std::vector<uint8_t> m_fragment;
};

} // namespace Multiplayer

#endif // !__EMSCRIPTEN__
