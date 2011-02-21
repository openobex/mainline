/**
	\file btobex.c
	Bluetooth OBEX, Bluetooth transport for OBEX.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 2002 Marcel Holtmann, All Rights Reserved.

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

#ifdef HAVE_BLUETOOTH

#ifdef _WIN32
#include <winsock2.h>

#else /* _WIN32 */
/* Linux/FreeBSD/NetBSD case */

#include <string.h>
#include <unistd.h>
#include <stdio.h>		/* perror */
#include <errno.h>		/* errno and EADDRNOTAVAIL */
#include <netinet/in.h>
#include <sys/socket.h>
#endif /* _WIN32 */

#include "obex_main.h"
#include "btobex.h"

#ifdef _WIN32
bdaddr_t bluez_compat_bdaddr_any = { BTH_ADDR_NULL };
#define WSA_VER_MAJOR 2
#define WSA_VER_MINOR 2
#endif

#include "cloexec.h"

static int btobex_init(obex_t *self)
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

static void btobex_cleanup (obex_t *self)
{
#ifdef _WIN32
	WSACleanup();
#endif
}

static int btobex_set_local_addr(obex_t *self, struct sockaddr *addr,
								size_t len)
{
	struct btobex_data *data = &self->trans.data.rfcomm;
	const struct sockaddr_rc *local = (struct sockaddr_rc *) addr;

	if (len == sizeof(*local) && local->rc_family == AF_BLUETOOTH) {
		data->self = *local;
		return 0;
	}

	return -1;
}

static int btobex_set_remote_addr(obex_t *self, struct sockaddr *addr, size_t len)
{
	struct btobex_data *data = &self->trans.data.rfcomm;
	const struct sockaddr_rc *remote = (struct sockaddr_rc *)addr;

	if (len == sizeof(*remote) && remote->rc_family == AF_BLUETOOTH) {
		data->peer = *remote;
		return 0;
	}

	return -1;
}

/*
 * Function btobex_prepare_connect (self, service)
 *
 *    Prepare for Bluetooth RFCOMM connect
 *
 */
void btobex_prepare_connect(obex_t *self, const bdaddr_t *src,
					const bdaddr_t *dst, uint8_t channel)
{
	struct sockaddr_rc addr;

	btobex_prepare_listen(self, src, 0);

	addr.rc_family = AF_BLUETOOTH;
	bacpy(&addr.rc_bdaddr, dst);
	addr.rc_channel = channel;
	btobex_set_remote_addr(self, (struct sockaddr *) &addr, sizeof(addr));
}

/*
 * Function btobex_prepare_listen (self, service)
 *
 *    Prepare for Bluetooth RFCOMM listen
 *
 */
void btobex_prepare_listen(obex_t *self, const bdaddr_t *src, uint8_t channel)
{
	struct sockaddr_rc addr;

	addr.rc_family = AF_BLUETOOTH;
	bacpy(&addr.rc_bdaddr, src);
	addr.rc_channel = channel;
	btobex_set_local_addr(self, (struct sockaddr *) &addr, sizeof(addr));
}

/*
 * Function btobex_listen (self)
 *
 *    Listen for incoming connections.
 *
 */
static int btobex_listen(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	struct btobex_data *data = &self->trans.data.rfcomm;

	DEBUG(3, "\n");

	trans->serverfd = obex_create_socket(self, AF_BLUETOOTH);
	if (trans->serverfd == INVALID_SOCKET) {
		DEBUG(0, "Error creating socket\n");
		return -1;
	}

	if (bind(trans->serverfd, (struct sockaddr *) &data->self,
							sizeof(data->self))) {
		DEBUG(0, "Error doing bind\n");
		goto out_freesock;
	}


	if (listen(trans->serverfd, 1)) {
		DEBUG(0, "Error doing listen\n");
		goto out_freesock;
	}

	DEBUG(4, "We are now listening for connections\n");
	return 1;

out_freesock:
	obex_delete_socket(self, trans->serverfd);
	trans->serverfd = INVALID_SOCKET;
	return -1;
}

/*
 * Function btobex_accept (self)
 *
 *    Accept an incoming connection.
 *
 * Note : don't close the server socket here, so apps may want to continue
 * using it...
 */
static int btobex_accept(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	struct btobex_data *data = &self->trans.data.rfcomm;
	struct sockaddr *addr = (struct sockaddr *) &data->peer;
	socklen_t addrlen = sizeof(data->peer);

	// First accept the connection and get the new client socket.
	if (self->init_flags & OBEX_FL_CLOEXEC)
		trans->fd = accept_cloexec(trans->serverfd, addr, &addrlen);
	else
		trans->fd = accept(trans->serverfd, addr, &addrlen);

	if (trans->fd == INVALID_SOCKET)
		return -1;

	trans->mtu = OBEX_DEFAULT_MTU;

	return 0;
}

/*
 * Function btobex_connect_request (self)
 *
 *    Open the RFCOMM connection
 *
 */
static int btobex_connect_request(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	struct btobex_data *data = &self->trans.data.rfcomm;
	int ret;
	int mtu = 0;

	DEBUG(4, "\n");

	if (trans->fd == INVALID_SOCKET) {
		trans->fd = obex_create_socket(self, AF_BLUETOOTH);
		if (trans->fd == INVALID_SOCKET)
			return -1;
	}

	ret = bind(trans->fd, (struct sockaddr*) &data->self,
							sizeof(data->self));

	if (ret < 0) {
		DEBUG(4, "bind(): error %d\n", errno);
		goto out_freesock;
	}

	ret = connect(trans->fd, (struct sockaddr*) &data->peer,
							sizeof(data->peer));
	if (ret < 0) {
		DEBUG(4, "connect(): error %d\n", errno);
		goto out_freesock;
	}

	mtu = OBEX_DEFAULT_MTU;
	trans->mtu = mtu;

	DEBUG(2, "transport mtu=%d\n", mtu);

	return 1;

out_freesock:
	obex_delete_socket(self, trans->fd);
	trans->fd = INVALID_SOCKET;
	return ret;
}

/*
 * Function btobex_disconnect_request (self)
 *
 *    Shutdown the RFCOMM link
 *
 */
static int btobex_disconnect_request(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	int ret;

	DEBUG(4, "\n");

	ret = obex_delete_socket(self, trans->fd);
	if (ret < 0)
		return ret;

	trans->fd = INVALID_SOCKET;

	return ret;
}

/*
 * Function btobex_disconnect_server (self)
 *
 *    Close the server socket
 *
 * Used when we start handling a incomming request, or when the
 * client just want to quit...
 */
static int btobex_disconnect_server(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	int ret;

	DEBUG(4, "\n");

	ret = obex_delete_socket(self, trans->serverfd);
	trans->serverfd = INVALID_SOCKET;

	return ret;
}

void btobex_get_ops(struct obex_transport_ops *ops)
{
	ops->init = &btobex_init;
	ops->cleanup = &btobex_cleanup;
	ops->write = &obex_transport_do_send;
	ops->read = &obex_transport_do_recv;
	ops->set_local_addr = &btobex_set_local_addr;
	ops->set_remote_addr = &btobex_set_remote_addr;
	ops->server.listen = &btobex_listen;
	ops->server.accept = &btobex_accept;
	ops->server.disconnect = &btobex_disconnect_server;
	ops->client.connect = &btobex_connect_request;
	ops->client.disconnect = &btobex_disconnect_request;
}

#endif /* HAVE_BLUETOOTH */
