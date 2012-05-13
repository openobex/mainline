/**
	\file obex_transport.c
	Handle different types of transports.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 1999, 2000 Pontus Fuchs, All Rights Reserved.
	Copyright (c) 1999, 2000 Dag Brattli, All Rights Reserved.

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

#include "obex_main.h"
#include "databuffer.h"
#include "obex_transport.h"
#include "obex_msg.h"

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#if defined(_WIN32)
#include <io.h>
#endif

struct obex_transport * obex_transport_create(struct obex_transport_ops *ops,
					      void *data)
{
	struct obex_transport *trans = calloc(1, sizeof(*trans));

	if (!trans)
		return NULL;

	trans->ops = ops;
	trans->data = data;

	trans->fd = INVALID_SOCKET;
	trans->serverfd = INVALID_SOCKET;
	trans->timeout = -1; /* no time-out */
	trans->connected = FALSE;

	return trans;
}

int obex_transport_init(obex_t *self, int transport)
{
	struct obex_transport *trans = &self->trans;

	switch (transport) {
#ifdef HAVE_IRDA
	case OBEX_TRANS_IRDA:
		irobex_transport_create();
		break;
#endif /*HAVE_IRDA*/

	case OBEX_TRANS_INET:
		inobex_transport_create();
		break;

	case OBEX_TRANS_CUSTOM:
		custom_transport_create();
		break;

#ifdef HAVE_BLUETOOTH
	case OBEX_TRANS_BLUETOOTH:
		btobex_transport_create();
		break;
#endif /*HAVE_BLUETOOTH*/

	case OBEX_TRANS_FD:
		fdobex_transport_create();
		break;

#ifdef HAVE_USB
	case OBEX_TRANS_USB:
		usbobex_transport_create();
		/* Set MTU to the maximum, if using USB transport - Alex Kanavin */
		self->mtu_rx = OBEX_MAXIMUM_MTU;
		self->mtu_tx = OBEX_MINIMUM_MTU;
		self->mtu_tx_max = OBEX_MAXIMUM_MTU;
		break;
#endif /*HAVE_USB*/

	default:
		return -1;
	}

	trans->type = transport;
	if (trans->ops->init)
		return trans->ops->init(self);
	else
		return 0;
}

void obex_transport_cleanup(obex_t *self)
{
	obex_transport_disconnect_request(self);
	obex_transport_disconnect_server(self);
	if (self->trans.ops->cleanup)
		self->trans.ops->cleanup(self);
}

void obex_transport_clone(obex_t *self, obex_t *old)
{
	struct obex_transport *trans = &self->trans;

	/* Copy transport data:
	 * This includes the transport operations and either the
	 * transport data can be cloned (means: implements the clone()
	 * function) or they must be initialized.
	 */
	*trans = old->trans;
	memset(&trans->data, 0, sizeof(trans->data));
	if (trans->ops->clone)
		trans->ops->clone(self, old);
	else if (trans->ops->init)
		trans->ops->init(self);
}

void obex_transport_split(obex_t *self, obex_t *server)
{
	/* Now, that's the interesting bit !
	 * We split the sockets apart, one for each instance */
	self->trans.fd = server->trans.fd;
	server->trans.fd = INVALID_SOCKET;

	self->trans.serverfd = INVALID_SOCKET;
}

/*
 * Function obex_transport_accept(self)
 *
 *    Accept an incoming connection.
 *
 */
static int obex_transport_accept(obex_t *self)
{
	DEBUG(4, "\n");

	if (self->trans.ops->server.accept) {
		if (self->trans.ops->server.accept(self) < 0)
			return -1;
		else
			return 1;
	}

	errno = EINVAL;
	return -1;
}

int obex_transport_standard_handle_input(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	fd_set fdset;
	socket_t highestfd = 0;
	int ret;

	DEBUG(4, "\n");
	obex_return_val_if_fail(self != NULL, -1);

	/* Check of we have any fd's to do select on. */
	if (trans->fd == INVALID_SOCKET &&
				trans->serverfd == INVALID_SOCKET) {
		DEBUG(0, "No valid socket is open\n");
		return -1;
	}

	/* Add the fd's to the set. */
	FD_ZERO(&fdset);
	if (trans->fd != INVALID_SOCKET) {
		FD_SET(trans->fd, &fdset);
		if (trans->fd > highestfd)
			highestfd = trans->fd;
	}

	if (trans->serverfd != INVALID_SOCKET) {
		FD_SET(trans->serverfd, &fdset);
		if (trans->serverfd > highestfd)
			highestfd = trans->serverfd;
	}

	/* Wait for input */
	if (trans->timeout >= 0) {
		struct timeval time = { trans->timeout, 0 };
		ret = select((int) highestfd + 1, &fdset, NULL, NULL, &time);
	} else {
		ret = select((int) highestfd + 1, &fdset, NULL, NULL, NULL);
	}

	/* Check if this is a timeout (0) or error (-1) */
	if (ret < 1)
		return ret;

	if (trans->fd != INVALID_SOCKET && FD_ISSET(trans->fd, &fdset)) {
		DEBUG(4, "Data available on client socket\n");
		ret = obex_data_indication(self);
	} else if (trans->serverfd != INVALID_SOCKET &&
					FD_ISSET(trans->serverfd, &fdset)) {
		DEBUG(4, "Data available on server socket\n");
		/* Accept : create the connected socket */
		ret = obex_transport_accept(self);

		/* Tell the app to perform the OBEX_Accept() */
		if (self->init_flags & OBEX_FL_KEEPSERVER)
			obex_deliver_event(self, OBEX_EV_ACCEPTHINT, 0, 0,
									FALSE);
		/* Otherwise, just disconnect the server */
		else if (ret >= 0)
			obex_transport_disconnect_server(self);
	} else
		ret = -1;

	return ret;
}

/*
 * Function obex_transport_handle_input(self, timeout)
 *
 *    Used when working in synchronous mode.
 *
 */
int obex_transport_handle_input(obex_t *self, int timeout)
{
	DEBUG(4, "\n");
	self->trans.timeout = timeout;
	if (obex_msg_rx_status(self)) {
		DEBUG(4, "full message already in buffer\n");
		return 1;
	}

	if (self->trans.ops->handle_input)
		return self->trans.ops->handle_input(self);
	else
		return obex_transport_standard_handle_input(self);
}

/*
 * Function obex_transport_set_local_addr(self, addr, len)
 *
 *    Set the local server address to bind and listen to.
 *
 */
int obex_transport_set_local_addr(obex_t *self, struct sockaddr *addr, size_t len)
{
	if (self->trans.ops->set_local_addr)
		return self->trans.ops->set_local_addr(self, addr, len);
	else
		return -1;
}

/*
 * Function obex_transport_set_local_addr(self, addr, len)
 *
 *    Set the remote server address to connect to.
 *
 */
int obex_transport_set_remote_addr(obex_t *self, struct sockaddr *addr, size_t len)
{
	if (self->trans.ops->set_remote_addr)
		return self->trans.ops->set_remote_addr(self, addr, len);
	else
		return -1;
}

/*
 * Function obex_transport_connect_request (self, service)
 *
 *    Try to connect transport
 *
 */
int obex_transport_connect_request(obex_t *self)
{
	int ret = -1;

	if (self->trans.connected)
		return 1;

	if (self->trans.ops->client.connect) {
		ret = self->trans.ops->client.connect(self);
		if (ret >= 0)
			self->trans.connected = TRUE;
	} else
		errno = EINVAL;

	return ret;
}

/*
 * Function obex_transport_disconnect_request (self)
 *
 *    Disconnect transport
 *
 */
void obex_transport_disconnect_request(obex_t *self)
{
	if (self->trans.ops->client.disconnect)
		self->trans.ops->client.disconnect(self);
	else
		errno = EINVAL;

	self->trans.connected = FALSE;
}

/*
 * Function obex_transport_listen (self)
 *
 *    Prepare for incomming connections
 *
 */
int obex_transport_listen(obex_t *self)
{
	if (self->trans.ops->server.listen)
		return self->trans.ops->server.listen(self);
	else {
		errno = EINVAL;
		return -1;
	}
}

/*
 * Function obex_transport_disconnect_server (self)
 *
 *    Disconnect the listening server
 *
 * Used either after an accept, or directly at client request (app. exits)
 * Note : obex_delete_socket() will catch the case when the socket
 * doesn't exist (-1)...
 */
void obex_transport_disconnect_server(obex_t *self)
{
	if (self->trans.ops->server.disconnect)
		self->trans.ops->server.disconnect(self);
}

/*
 * Function obex_transport_write ()
 *
 *    Do the writing
 *
 */
int obex_transport_write(obex_t *self, buf_t *msg)
{
	if (self->trans.ops->write)
		return self->trans.ops->write(self, msg);
	else
		return -1;
}

/*
 * Function obex_transport_read ()
 *
 *    Do the reading
 *
 */
int obex_transport_read(obex_t *self, int max)
{
	struct databuffer *msg = self->rx_msg;
	size_t msglen = buf_get_length(msg);
	void *buf;
	int err = buf_set_size(msg, msglen + self->mtu_rx);

	if (err)
		return -1;

	buf = buf_get(msg) + msglen;

	if (self->trans.ops->read) {
		int ret = self->trans.ops->read(self, buf, max);
		if (ret > 0)
			buf_append(msg, NULL, ret);
		return ret;
	} else
		return 0;
}

void obex_transport_enumerate(struct obex *self)
{
	struct obex_transport_ops *ops = self->trans.ops;
	int i;

	if (self->interfaces)
		return;

	if (ops->client.find_interfaces)
		i = ops->client.find_interfaces(self, &self->interfaces);
	else
		i = 0;

	self->interfaces_number = i;
}
