/**
	\file inobex.c
	InOBEX, Inet transport for OBEX.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 1999 Dag Brattli, All Rights Reserved.

	OpenOBEX is free software; you can redistribute it and/or modify
	it under the terms of the GNU Lesser General Public License as
	published by the Free Software Foundation; either version 2.1 of
	the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with OpenOBEX. If not, see <http://www.gnu.org/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define WSA_VER_MAJOR 2
#define WSA_VER_MINOR 2
#else

#include <sys/types.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#endif /*_WIN32*/

#include "obex_main.h"
#include "inobex.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "cloexec.h"
#include "nonblock.h"

#define OBEX_PORT 650

static void map_ip4to6(struct sockaddr_in *in, struct sockaddr_in6 *out)
{
	out->sin6_family = AF_INET6;
	if (in->sin_port == 0)
		out->sin6_port = htons(OBEX_PORT);
	else
		out->sin6_port = in->sin_port;
	out->sin6_flowinfo = 0;
	out->sin6_scope_id = 0;

	/* default, matches IN6ADDR_ANY */
	memset(out->sin6_addr.s6_addr, 0, sizeof(out->sin6_addr.s6_addr));
	switch (in->sin_addr.s_addr) {
	case INADDR_ANY:
		/* does not work, so use IN6ADDR_ANY
		 * which includes INADDR_ANY
		 */
		break;
	default:
		/* map the IPv4 address to [::FFFF:<ipv4>]
		 * see RFC2373 and RFC2553 for details
		 */
		out->sin6_addr.s6_addr[10] = 0xFF;
		out->sin6_addr.s6_addr[11] = 0xFF;
		out->sin6_addr.s6_addr[12] = (unsigned char)((in->sin_addr.s_addr >>  0) & 0xFF);
		out->sin6_addr.s6_addr[13] = (unsigned char)((in->sin_addr.s_addr >>  8) & 0xFF);
		out->sin6_addr.s6_addr[14] = (unsigned char)((in->sin_addr.s_addr >> 16) & 0xFF);
		out->sin6_addr.s6_addr[15] = (unsigned char)((in->sin_addr.s_addr >> 24) & 0xFF);
		break;
	}
}

static int inobex_init (obex_t *self)
{
#ifdef _WIN32
	WORD ver = MAKEWORD(WSA_VER_MAJOR,WSA_VER_MINOR);
	WSADATA WSAData;
	if (WSAStartup (ver, &WSAData) != 0) {
		DEBUG(4, "WSAStartup failed (%d)\n",WSAGetLastError());
		return -1;
	}
	if (LOBYTE(WSAData.wVersion) != WSA_VER_MAJOR ||
				HIBYTE(WSAData.wVersion) != WSA_VER_MINOR) {
		DEBUG(4, "WSA version mismatch\n");
		WSACleanup();
		return -1;
	}
#endif

	return 0;
}

static void inobex_cleanup (obex_t *self)
{
#ifdef _WIN32
	WSACleanup();
#endif
}

static int inobex_set_remote_addr(obex_t *self, struct sockaddr *addr, size_t len)
{
	size_t expected_len;

	if (addr->sa_family == AF_INET)
		expected_len = sizeof(struct sockaddr_in);
	else if (addr->sa_family == AF_INET6)
		expected_len = sizeof(struct sockaddr_in6);
	else
		return -1;

	if (expected_len != len)
		return -1;

	inobex_prepare_connect(self, addr, len);

	return 0;
}

static int inobex_set_local_addr(obex_t *self, struct sockaddr *addr, size_t len)
{
	size_t expected_len;

	if (addr->sa_family == AF_INET)
		expected_len = sizeof(struct sockaddr_in);
	else if (addr->sa_family == AF_INET6)
		expected_len = sizeof(struct sockaddr_in6);
	else
		return -1;

	if (expected_len != len)
		return -1;

	inobex_prepare_listen(self, addr, len);

	return 0;
}

/*
 * Function inobex_prepare_connect (self, service)
 *
 *    Prepare for INET-connect
 *
 */
void inobex_prepare_connect(obex_t *self, struct sockaddr *saddr, int addrlen)
{
	struct inobex_data *data = &self->trans.data.inet;
	struct sockaddr_in6 addr;

	addr.sin6_family   = AF_INET6;
	addr.sin6_port     = htons(OBEX_PORT);
	addr.sin6_flowinfo = 0;
	memcpy(&addr.sin6_addr, &in6addr_loopback, sizeof(addr.sin6_addr));
	addr.sin6_scope_id = 0;

	if (saddr == NULL)
		saddr = (struct sockaddr*)(&addr);
	else
		switch (saddr->sa_family){
		case AF_INET6:
			break;
		case AF_INET:
			map_ip4to6((struct sockaddr_in*)saddr,&addr);
			/* no break */
		default:
			saddr = (struct sockaddr*)(&addr);
			break;
	}
	data->peer = *(struct sockaddr_in6 *)saddr;
}

/*
 * Function inobex_prepare_listen (self)
 *
 *    Prepare for INET-listen
 *
 */
void inobex_prepare_listen(obex_t *self, struct sockaddr *saddr, int addrlen)
{
	struct inobex_data *data = &self->trans.data.inet;
	struct sockaddr_in6 addr;

	addr.sin6_family   = AF_INET6;
	addr.sin6_port     = htons(OBEX_PORT);
	addr.sin6_flowinfo = 0;
	memcpy(&addr.sin6_addr, &in6addr_any, sizeof(addr.sin6_addr));
	addr.sin6_scope_id = 0;

	/* Bind local service */
	if (saddr == NULL)
		saddr = (struct sockaddr *) &addr;
	else
		switch (saddr->sa_family) {
		case AF_INET6:
			break;
		case AF_INET:
			map_ip4to6((struct sockaddr_in *) saddr, &addr);
			/* no break */
		default:
			saddr = (struct sockaddr *) &addr;
			break;
		}
	data->self = *(struct sockaddr_in6 *)saddr;
}

/*
 * Function inobex_listen (self)
 *
 *    Wait for incomming connections
 *
 */
static int inobex_listen(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	struct inobex_data *data = &self->trans.data.inet;

	DEBUG(4, "\n");

	/* needed as compat for apps that call OBEX_TransportConnect
	 * instead of InOBEX_TransportConnect (e.g. obexftp)
	 */
	if (data->self.sin6_family == AF_INET)
		inobex_prepare_listen(self, (struct sockaddr *) &data->self,
							sizeof(data->self));

	trans->serverfd = obex_transport_sock_create(self, AF_INET6, 0);
	if (trans->serverfd == INVALID_SOCKET) {
		DEBUG(0, "Cannot create server-socket\n");
		return -1;
	}
#ifdef IPV6_V6ONLY
	else {
		/* Needed for some system that set this IPv6 socket option to
		 * 1 by default (Windows Vista, maybe some BSDs).
		 * Do not check the return code as it may not matter.
		 * You will certainly notice later if it failed.
		 */
		int v6only = 0;
		setsockopt(trans->serverfd, IPPROTO_IPV6, IPV6_V6ONLY,
					(void *) &v6only, sizeof(v6only));
	}
#endif

	//printf("TCP/IP listen %d %X\n", trans->self.inet.sin_port,
	//       trans->self.inet.sin_addr.s_addr);
	if (bind(trans->serverfd, (struct sockaddr *) &data->self,
							sizeof(data->self))) {
		DEBUG(0, "bind() Failed\n");
		return -1;
	}

	if (listen(trans->serverfd, 2)) {
		DEBUG(0, "listen() Failed\n");
		return -1;
	}

	DEBUG(4, "Now listening for incomming connections. serverfd = %d\n",
							trans->serverfd);

	return 1;
}

/*
 * Function inobex_accept (self)
 *
 *    Accept incoming connection.
 *
 * Note : don't close the server socket here, so apps may want to continue
 * using it...
 */
static int inobex_accept(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	struct inobex_data *data = &self->trans.data.inet;
	struct sockaddr *addr = (struct sockaddr *)&data->peer;
	socklen_t addrlen = sizeof(data->peer);

	if (self->init_flags & OBEX_FL_CLOEXEC)
		trans->fd = accept_cloexec(trans->serverfd, addr, &addrlen);
	else
		trans->fd = accept(trans->serverfd, addr, &addrlen);

	if (trans->fd == INVALID_SOCKET)
		return -1;

	/* Just use the default MTU for now */
	trans->mtu = OBEX_DEFAULT_MTU;
	if (self->init_flags & OBEX_FL_NONBLOCK)
		socket_set_nonblocking(trans->fd);	

	return 1;
}

/*
 * Function inobex_connect_request (self)
 */
static int inobex_connect_request(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	struct inobex_data *data = &self->trans.data.inet;
	int ret;
#ifndef _WIN32
	char addr[INET6_ADDRSTRLEN];
#endif

	/* needed as compat for apps that call OBEX_TransportConnect
	 * instead of InOBEX_TransportConnect (e.g. obexftp)
	 */
	if (data->peer.sin6_family == AF_INET)
		inobex_prepare_connect(self, (struct sockaddr *) &data->peer,
							sizeof(data->peer));

	trans->fd = obex_transport_sock_create(self, AF_INET6, 0);
	if (trans->fd == INVALID_SOCKET)
		return -1;
#ifdef IPV6_V6ONLY
	else {
		/* Needed for some system that set this IPv6 socket option to
		 * 1 by default (Windows Vista, maybe some BSDs).
		 * Do not check the return code as it may not matter.
		 * You will certainly notice later if it failed.
		 */
		int v6only = 0;
		setsockopt(trans->fd, IPPROTO_IPV6, IPV6_V6ONLY,
					(void *) &v6only, sizeof(v6only));
	}
#endif

	/* Set these just in case */
	if (data->peer.sin6_port == 0)
		data->peer.sin6_port = htons(OBEX_PORT);

#ifndef _WIN32
	if (!inet_ntop(AF_INET6, &data->peer.sin6_addr, addr,sizeof(addr))) {
		DEBUG(4, "Adress problem\n");
		obex_transport_sock_delete(self, trans->fd);
		trans->fd = INVALID_SOCKET;
		return -1;
	}
	DEBUG(2, "peer addr = [%s]:%u\n", addr, ntohs(data->peer.sin6_port));
#endif

	ret = connect(trans->fd, (struct sockaddr *) &data->peer,
							sizeof(data->peer));
#if defined(_WIN32)
	if (ret == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
		ret = 0;
#else
	if (ret == -1 && errno == EINPROGRESS)
		ret = 0;
#endif
	if (ret == -1) {
		DEBUG(4, "Connect failed\n");
		obex_transport_sock_delete(self, trans->fd);
		trans->fd = INVALID_SOCKET;
		return ret;
	}

	trans->mtu = OBEX_DEFAULT_MTU;
	DEBUG(3, "transport mtu=%d\n", trans->mtu);

	return ret;
}

/*
 * Function inobex_transport_disconnect_request (self)
 *
 *    Shutdown the TCP/IP link
 *
 */
static int inobex_disconnect_request(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	int ret;

	DEBUG(4, "\n");
	ret = obex_transport_sock_delete(self, trans->fd);
	if (ret < 0)
		return ret;
	trans->fd = INVALID_SOCKET;
	return ret;
}

/*
 * Function inobex_transport_disconnect_server (self)
 *
 *    Close the server socket
 *
 * Used when we start handling a incomming request, or when the
 * client just want to quit...
 */
static int inobex_disconnect_server(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	int ret;

	DEBUG(4, "\n");
	ret = obex_transport_sock_delete(self, trans->serverfd);
	trans->serverfd = INVALID_SOCKET;
	return ret;
}

void inobex_get_ops(struct obex_transport_ops* ops)
{
	ops->init = &inobex_init;
	ops->cleanup = &inobex_cleanup;
	ops->write = &obex_transport_sock_send;
	ops->read = &obex_transport_sock_recv;
	ops->set_local_addr = &inobex_set_local_addr;
	ops->set_remote_addr = &inobex_set_remote_addr;
	ops->server.listen = &inobex_listen;
	ops->server.accept = &inobex_accept;
	ops->server.disconnect = &inobex_disconnect_server;
	ops->client.connect = &inobex_connect_request;
	ops->client.disconnect = &inobex_disconnect_request;
}
