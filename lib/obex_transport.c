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

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#if defined(_WIN32)
#include <io.h>
#endif

/*
 * Function obex_transport_accept(self)
 *
 *    Accept an incoming connection.
 *
 */
static int obex_transport_accept(obex_t *self)
{
	DEBUG(4, "\n");

	if (self->trans.ops.server.accept)
		return self->trans.ops.server.accept(self);

	
	errno = EINVAL;
	return -1;
}

int obex_transport_standard_handle_input(obex_t *self, int timeout)
{
	struct timeval time = {timeout, 0};
	fd_set fdset;
	socket_t highestfd = 0;
	int ret;

	DEBUG(4, "\n");
	obex_return_val_if_fail(self != NULL, -1);

	/* Check of we have any fd's to do select on. */
	if (self->fd == INVALID_SOCKET
	    && self->serverfd == INVALID_SOCKET) {
		DEBUG(0, "No valid socket is open\n");
		return -1;
	}

	/* Add the fd's to the set. */
	FD_ZERO(&fdset);
	if (self->fd != INVALID_SOCKET) {
		FD_SET(self->fd, &fdset);
		if (self->fd > highestfd)
			highestfd = self->fd;
	}

	if (self->serverfd != INVALID_SOCKET) {
		FD_SET(self->serverfd, &fdset);
		if (self->serverfd > highestfd)
			highestfd = self->serverfd;
	}

	/* Wait for input */
	if (timeout >= 0) {
		ret = select((int)highestfd+1, &fdset, NULL, NULL, &time);
	} else {
		ret = select((int)highestfd+1, &fdset, NULL, NULL, NULL);
	}

	/* Check if this is a timeout (0) or error (-1) */
	if (ret < 1)
		return ret;

	if (self->fd != INVALID_SOCKET && FD_ISSET(self->fd, &fdset)) {
		DEBUG(4, "Data available on client socket\n");
		ret = obex_data_indication(self, NULL, 0);

	} else if (self->serverfd != INVALID_SOCKET && FD_ISSET(self->serverfd, &fdset)) {
		DEBUG(4, "Data available on server socket\n");
		/* Accept : create the connected socket */
		ret = obex_transport_accept(self);

		/* Tell the app to perform the OBEX_Accept() */
		if (self->keepserver)
			obex_deliver_event(self, OBEX_EV_ACCEPTHINT,
					   0, 0, FALSE);
		/* Otherwise, just disconnect the server */
		if (ret >= 0 && !self->keepserver)
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
	if (self->trans.ops.handle_input)
		return self->trans.ops.handle_input(self, timeout);
	else
		return obex_transport_standard_handle_input(self, timeout);
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

	if (self->trans.ops.client.connect) {
		ret = self->trans.ops.client.connect(self);
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
	if (self->trans.ops.client.disconnect)
		self->trans.ops.client.disconnect(self);
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
	if (self->trans.ops.server.listen)
		return self->trans.ops.server.listen(self);
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
	if (self->trans.ops.server.disconnect)
		self->trans.ops.server.disconnect(self);
}

/*
 * does fragmented write
 */
int obex_transport_do_send (obex_t *self, buf_t *msg)
{
	int fd = self->fd;
	unsigned int mtu = self->trans.mtu;

	int actual = -1;
	int size;

	/* Send and fragment if necessary  */
	while (msg->data_size) {
		if (msg->data_size > mtu)
			size = mtu;
		else
			size = msg->data_size;
		DEBUG(1, "sending %d bytes\n", size);

		actual = send(fd, msg->data, size, 0);
		if (actual <= 0)
			return actual;

		/* Hide sent data */
		buf_remove_begin(msg, actual);
	}

	return actual;
}

/*
 * Function obex_transport_write ()
 *
 *    Do the writing
 *
 */
int obex_transport_write(obex_t *self, buf_t *msg)
{
	if (self->trans.ops.write)
		return self->trans.ops.write(self, msg);
	else
		return -1;
}

int obex_transport_do_recv (obex_t *self, void *buf, int buflen)
{
	return recv(self->fd, buf, buflen, 0);
}

/*
 * Function obex_transport_read ()
 *
 *    Do the reading
 *
 */
int obex_transport_read(obex_t *self, int max, uint8_t *in, int len)
{
	int actual = -1;
	void * buf = buf_reserve_end(self->rx_msg, max);

	DEBUG(4, "Request to read max %d bytes\n", max);

	if (self->trans.ops.read)
		actual = self->trans.ops.read(self, buf, max);
	else if (len) {
		actual = len;
		if (actual > max)
			actual = max;
		memcpy(buf, in, actual);
	}

	if (actual <= 0)
		buf_remove_end(self->rx_msg, max);
	else if (0 < actual && actual < max)
		buf_remove_end(self->rx_msg, max - actual);

	return actual;
}
