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
#include <sys/socket.h>
#endif /* _WIN32 */

#include "obex_main.h"
#include "btobex.h"

#ifdef _WIN32
bdaddr_t bluez_compat_bdaddr_any = { BTH_ADDR_NULL };
#endif

#include "obex_transport_sock.h"
#include "cloexec.h"
#include "nonblock.h"

#include <stdlib.h>

struct btobex_rfcomm_data {
	struct obex_sock *sock;
};

static void * btobex_create(void)
{
	return calloc(1, sizeof(struct btobex_rfcomm_data));
}

static bool btobex_init(obex_t *self)
{
	struct btobex_rfcomm_data *data = self->trans->data;
	socklen_t len = sizeof(struct sockaddr_rc);

	if (data == NULL)
		return false;

	data->sock = obex_transport_sock_create(AF_BLUETOOTH, BTPROTO_RFCOMM,
						len, self->init_flags);
	if (data->sock == NULL) {
		free(data);
		return false;
	}

	return true;
}

static void btobex_cleanup (obex_t *self)
{
	struct btobex_rfcomm_data *data = self->trans->data;

	if (data->sock)
		obex_transport_sock_destroy(data->sock);
	free(data);	
}

static bool btobex_set_local_addr(obex_t *self, struct sockaddr *addr,
				 size_t len)
{
	struct btobex_rfcomm_data *data = self->trans->data;

	return obex_transport_sock_set_local(data->sock, addr, len);
}

static bool btobex_set_remote_addr(obex_t *self, struct sockaddr *addr, size_t len)
{
	struct btobex_rfcomm_data *data = self->trans->data;

	return obex_transport_sock_set_remote(data->sock, addr, len);
}

static void btobex_addr2sock(const bdaddr_t *addr, uint8_t channel,
			     struct sockaddr_rc *sock)
{
	memset(sock, 0, sizeof(*sock));
	sock->rc_family = AF_BLUETOOTH;
	bacpy(&sock->rc_bdaddr, addr);
	sock->rc_channel = (uint8_t)(channel & 0xFF);
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
	struct btobex_rfcomm_data *data = self->trans->data;
	struct sockaddr_rc sock;

	btobex_prepare_listen(self, src, 0);
	btobex_addr2sock(dst, channel, &sock);
	obex_transport_sock_set_remote(data->sock, (struct sockaddr *) &sock,
				       sizeof(sock));
}

/*
 * Function btobex_prepare_listen (self, service)
 *
 *    Prepare for Bluetooth RFCOMM listen
 *
 */
void btobex_prepare_listen(obex_t *self, const bdaddr_t *src, uint8_t channel)
{
	struct btobex_rfcomm_data *data = self->trans->data;
	struct sockaddr_rc sock;

	btobex_addr2sock(src, channel, &sock);
	obex_transport_sock_set_local(data->sock, (struct sockaddr *) &sock,
				      sizeof(sock));
}

/*
 * Function btobex_listen (self)
 *
 *    Listen for incoming connections.
 *
 */
static bool btobex_listen(obex_t *self)
{
	struct btobex_rfcomm_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_listen(data->sock);
}

/*
 * Function btobex_accept (self)
 *
 *    Accept an incoming connection.
 *
 * Note : don't close the server socket here, so apps may want to continue
 * using it...
 */
static bool btobex_accept(obex_t *self, const obex_t *server)
{
	struct btobex_rfcomm_data *server_data = server->trans->data;
	struct btobex_rfcomm_data *data = self->trans->data;

	if (data == NULL)
		return false;

	data->sock = obex_transport_sock_accept(server_data->sock);
	if (data->sock == NULL)
		return false;

	return true;
}

/*
 * Function btobex_connect_request (self)
 *
 *    Open the RFCOMM connection
 *
 */
static bool btobex_connect_request(obex_t *self)
{
	struct btobex_rfcomm_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_connect(data->sock);
}

/*
 * Function btobex_disconnect (self)
 *
 *    Shutdown the RFCOMM link
 *
 */
static bool btobex_disconnect(obex_t *self)
{
	struct btobex_rfcomm_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_disconnect(data->sock);
}

static result_t btobex_handle_input(obex_t *self)
{
	struct btobex_rfcomm_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_wait(data->sock, self->trans->timeout);
}

static ssize_t btobex_write(obex_t *self, struct databuffer *msg)
{
	struct obex_transport *trans = self->trans;
	struct btobex_rfcomm_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_send(data->sock, msg, trans->timeout);
}

static ssize_t btobex_read(obex_t *self, void *buf, int buflen)
{
	struct btobex_rfcomm_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_recv(data->sock, buf, buflen);
}

static int btobex_get_fd(obex_t *self)
{
	struct btobex_rfcomm_data *data = self->trans->data;

	return (int)obex_transport_sock_get_fd(data->sock);
}

static struct obex_transport_ops btobex_transport_ops = {
	&btobex_create,
	&btobex_init,
	&btobex_cleanup,

	&btobex_handle_input,
	&btobex_write,
	&btobex_read,
	&btobex_disconnect,

	&btobex_get_fd,
	&btobex_set_local_addr,
	&btobex_set_remote_addr,

	{
		&btobex_listen,
		&btobex_accept,
	},

	{
		&btobex_connect_request,
		NULL,
		NULL,
		NULL,
	},
};

struct obex_transport * btobex_transport_create(void)
{
	return obex_transport_create(&btobex_transport_ops);
}

#endif /* HAVE_BLUETOOTH */
