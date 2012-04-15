/**
 * @file obex_transport_sock.c
 *
 * OBEX header releated functions.
 * OpenOBEX library - Free implementation of the Object Exchange protocol.
 *
 * Copyright (c) 2012 Hendrik Sattler, All Rights Reserved.
 *
 * OpenOBEX is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with OpenOBEX. If not, see <http://www.gnu.org/>.
 */

#ifdef _WIN32
#include <winsock2.h>
#endif /* _WIN32 */

#include <unistd.h>
#include <errno.h>

#include <obex_main.h>
#include <obex_transport.h>

#include "cloexec.h"
#include "nonblock.h"

/** Create a stream socket with behavior as selected by the user.
 * Supported behaviours are close-on-exec and non-blocking.
 *
 * @param self the obex instance
 * @param domain the socket domain to use
 * @param proto the socket protocol within domain
 * @return a socket
 */
socket_t obex_transport_sock_create(obex_t *self, int domain, int proto)
{
	socket_t fd;
	int type = SOCK_STREAM;

	DEBUG(4, "\n");

	if (self->init_flags & OBEX_FL_CLOEXEC)
		fd = socket_cloexec(domain, type, proto);
	else
		fd = socket(domain, type, proto);

	if (self->init_flags & OBEX_FL_NONBLOCK)
		socket_set_nonblocking(fd);

	return fd;
}

/** Close socket.
 *
 * @param self the obex instance
 * @param fd a previously created socket
 * @return -1 on error, else 0
 */
int obex_transport_sock_delete(obex_t *self, socket_t fd)
{
	int ret;

	DEBUG(4, "\n");

	if (fd == INVALID_SOCKET)
		return -1;

#ifdef _WIN32
	ret = closesocket(fd);
#else /* _WIN32 */
	ret = close(fd);
#endif /* _WIN32 */
	return ret;
}

/** Send a buffer.
 * It may happen (especially with non-blocking mode) that the buffer is only
 * sent partially.
 *
 * @param self the obex instance
 * @param msg the message to send
 * @return -1 on error, else number of sent bytes
 */
int obex_transport_sock_send(obex_t *self, buf_t *msg)
{
	struct obex_transport *trans = &self->trans;
	socket_t fd = trans->fd;
	size_t size = buf_get_length(msg);
	int status;
	fd_set fdset;
	struct timeval *time_ptr = NULL;
	struct timeval timeout = {trans->timeout, 0};

	if (size == 0)
		return 0;

	if (size > trans->mtu)
		size = trans->mtu;
	DEBUG(1, "sending %lu bytes\n", (unsigned long)size);

	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);
	if (trans->timeout >= 0)
		time_ptr = &timeout;
	status = select((int)fd+1, NULL, &fdset, NULL, time_ptr);
	if (status == 0)
		return 0;

	/* call send() if no error */
	status = send(fd, buf_get(msg), size, 0);

	/* The following are not really transport errors. */
#if defined(_WIN32)
	if (status == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
		status = 0;
#else
	if (status == -1 &&
	    (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
		status = 0;
#endif

	return status;
}

/** Receive into a buffer
 * It may happen that less than requested bytes are received.
 *
 * @param self the obex instance
 * @param buf the buffer to receive into
 * @param buflen the maximum size to receive
 * @return -1 on error, else number of received bytes
 */
int obex_transport_sock_recv(obex_t *self, void *buf, int buflen)
{
	struct obex_transport *trans = &self->trans;
	int status = recv(trans->fd, buf, buflen, 0);

	/* The following are not really transport errors. */
#if defined(_WIN32)
	if (status == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
		status = 0;
#else
	if (status == -1 &&
	    (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
		status = 0;
#endif

	return status;
}
