#pragma once
#pragma once

#include "game/game.hpp"

namespace demonware {
	namespace io {

	hostent* WINAPI gethostbyname_stub(const char* name);

	int WINAPI connect_stub(const SOCKET s, const sockaddr* addr, const int len);

	int WINAPI closesocket_stub(const SOCKET s);

	int WINAPI send_stub(const SOCKET s, const char* buf, const int len, const int flags);

	int WINAPI recv_stub(const SOCKET s, char* buf, const int len, const int flags);

	int WINAPI sendto_stub(const SOCKET s, const char* buf, const int len, const int flags, const sockaddr* to,	const int tolen);

	int WINAPI recvfrom_stub(const SOCKET s, char* buf, const int len, const int flags, sockaddr* from, int* fromlen);

	int WINAPI select_stub(const int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, timeval* timeout);

	int WINAPI ioctlsocket_stub(const SOCKET s, const long cmd, u_long* argp);

	BOOL internet_get_connected_state_stub(LPDWORD, DWORD);
	}

}