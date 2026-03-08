#ifndef __EMSCRIPTEN__

#include "extensions/multiplayer/platforms/native/lwstransport.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <libwebsockets.h>

namespace Multiplayer
{

static int LwsCallback(struct lws* p_wsi, enum lws_callback_reasons p_reason, void* p_user, void* p_in, size_t p_len)
{
	LwsTransport* transport = static_cast<LwsTransport*>(lws_get_opaque_user_data(p_wsi));
	if (transport) {
		return transport->HandleLwsEvent(p_wsi, static_cast<int>(p_reason), p_in, p_len);
	}
	return 0;
}

// clang-format off
static const struct lws_protocols s_protocols[] = {
	{"lws-multiplayer", LwsCallback, 0, 8192},
	LWS_PROTOCOL_LIST_TERM
};
// clang-format on

LwsTransport::LwsTransport(const std::string& p_relayBaseUrl)
	: m_relayBaseUrl(p_relayBaseUrl), m_context(nullptr), m_wsi(nullptr), m_connected(false), m_rejected(false)
{
}

LwsTransport::~LwsTransport()
{
	Disconnect();
}

void LwsTransport::Connect(const char* p_roomId)
{
	if (m_connected) {
		Disconnect();
	}

	m_rejected = false;

	// lws_parse_uri modifies the string in place, so we need a mutable copy
	std::string fullUrl = m_relayBaseUrl + "/room/" + p_roomId;
	std::vector<char> urlBuf(fullUrl.begin(), fullUrl.end());
	urlBuf.push_back('\0');

	const char* protocol = nullptr;
	const char* address = nullptr;
	const char* path = nullptr;
	int port = 0;

	if (lws_parse_uri(&urlBuf[0], &protocol, &address, &port, &path)) {
		SDL_Log("[Multiplayer] Failed to parse relay URL: %s", fullUrl.c_str());
		m_rejected = true;
		return;
	}

	struct lws_context_creation_info ctxInfo;
	SDL_memset(&ctxInfo, 0, sizeof(ctxInfo));
	ctxInfo.port = CONTEXT_PORT_NO_LISTEN;
	ctxInfo.protocols = s_protocols;

	m_context = lws_create_context(&ctxInfo);
	if (!m_context) {
		SDL_Log("[Multiplayer] Failed to create lws context");
		m_rejected = true;
		return;
	}

	// path from lws_parse_uri does not include the leading '/', so prepend it
	std::string fullPath = std::string("/") + path;

	struct lws_client_connect_info connInfo;
	SDL_memset(&connInfo, 0, sizeof(connInfo));
	connInfo.context = m_context;
	connInfo.address = address;
	connInfo.port = port;
	connInfo.path = fullPath.c_str();
	connInfo.host = address;
	connInfo.origin = address;
	connInfo.ssl_connection = 0;
	connInfo.protocol = s_protocols[0].name;
	connInfo.local_protocol_name = s_protocols[0].name;
	connInfo.opaque_user_data = this;

	m_wsi = lws_client_connect_via_info(&connInfo);
	if (!m_wsi) {
		SDL_Log("[Multiplayer] Failed to initiate WebSocket connection to %s:%d%s", address, port, fullPath.c_str());
		lws_context_destroy(m_context);
		m_context = nullptr;
		m_rejected = true;
		return;
	}
}

void LwsTransport::Disconnect()
{
	if (m_context) {
		lws_context_destroy(m_context);
		m_context = nullptr;
	}
	m_wsi = nullptr;
	m_connected = false;
	m_sendQueue.clear();
	m_recvQueue.clear();
	m_fragment.clear();
}

bool LwsTransport::IsConnected() const
{
	return m_connected;
}

bool LwsTransport::WasRejected() const
{
	return m_rejected;
}

void LwsTransport::Send(const uint8_t* p_data, size_t p_length)
{
	if (!m_connected || !m_wsi) {
		return;
	}

	std::vector<uint8_t> buf(LWS_PRE + p_length);
	SDL_memcpy(&buf[LWS_PRE], p_data, p_length);
	m_sendQueue.push_back(std::move(buf));

	lws_callback_on_writable(m_wsi);
}

size_t LwsTransport::Receive(std::function<void(const uint8_t*, size_t)> p_callback)
{
	if (!m_context) {
		return 0;
	}

	// Non-blocking service: processes pending network I/O and fires lws callbacks
	// which populate m_recvQueue and drain m_sendQueue
	lws_service(m_context, 0);

	size_t count = m_recvQueue.size();
	while (!m_recvQueue.empty()) {
		const std::vector<uint8_t>& msg = m_recvQueue.front();
		p_callback(msg.data(), msg.size());
		m_recvQueue.pop_front();
	}

	return count;
}

int LwsTransport::HandleLwsEvent(struct lws* p_wsi, int p_reason, void* p_in, size_t p_len)
{
	switch (p_reason) {
	case LWS_CALLBACK_CLIENT_ESTABLISHED:
		m_connected = true;
		break;

	case LWS_CALLBACK_CLIENT_RECEIVE:
		m_fragment.insert(m_fragment.end(), static_cast<uint8_t*>(p_in), static_cast<uint8_t*>(p_in) + p_len);
		if (lws_is_final_fragment(p_wsi)) {
			m_recvQueue.push_back(std::move(m_fragment));
			m_fragment.clear();
		}
		break;

	case LWS_CALLBACK_CLIENT_WRITEABLE:
		if (!m_sendQueue.empty()) {
			std::vector<uint8_t>& front = m_sendQueue.front();
			size_t payloadLen = front.size() - LWS_PRE;
			lws_write(p_wsi, &front[LWS_PRE], payloadLen, LWS_WRITE_BINARY);
			m_sendQueue.pop_front();

			if (!m_sendQueue.empty()) {
				lws_callback_on_writable(p_wsi);
			}
		}
		break;

	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		if (!m_connected) {
			m_rejected = true;
		}
		m_connected = false;
		m_wsi = nullptr;
		break;

	case LWS_CALLBACK_CLIENT_CLOSED:
		if (!m_connected) {
			m_rejected = true;
		}
		m_connected = false;
		m_wsi = nullptr;
		break;

	default:
		break;
	}

	return 0;
}

} // namespace Multiplayer

#endif // !__EMSCRIPTEN__
