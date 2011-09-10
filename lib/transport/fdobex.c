/**
	\file fdobex.c
	FD OBEX, file descriptor transport for OBEX.
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

#include <errno.h>

#include "obex_main.h"
#include "fdobex.h"

#include <unistd.h>
#if defined(_WIN32)
#include <io.h>
#endif

static int fdobex_init(obex_t *self)
{
	self->trans.data.fd.writefd = INVALID_SOCKET;

	return 0;
}

static int fdobex_connect_request(obex_t *self)
{
	struct obex_transport *trans = &self->trans;

	/* no real connect on the file */
	if (trans->fd != INVALID_SOCKET &&
				trans->data.fd.writefd != INVALID_SOCKET)
		return 0;
	else {
		errno = EINVAL;
		return -1;
	}
}

static int fdobex_disconnect_request(obex_t *self)
{
	struct obex_transport *trans = &self->trans;

	/* no real disconnect on a file */
	trans->fd = trans->data.fd.writefd = INVALID_SOCKET;
	return 0;
}

static int fdobex_write(obex_t *self, buf_t *msg)
{
	struct obex_transport *trans = &self->trans;
	int fd = trans->data.fd.writefd;
	size_t size = msg->data_size;
	int status;
	fd_set fdset;
	struct timeval time = {trans->timeout, 0};

	if (size == 0)
		return 0;

	if (size > trans->mtu)
		size = trans->mtu;
	DEBUG(1, "sending %lu bytes\n", (unsigned long)size);

	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);
	if (trans->timeout >= 0)
		status = select((int)fd+1, NULL, &fdset, NULL, &time);
	else
		status = select((int)fd+1, NULL, &fdset, NULL, NULL);

	if (status == 0)
		return 0;

#if defined(_WIN32)
	status = _write(fd, msg->data, size);
#else
	status = write(fd, msg->data, size);
	/* The following are not really transport errors. */
	if (status == -1 &&
	    (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
		status = 0;
#endif

	return status;
}

static int fdobex_read(obex_t *self, void *buf, int buflen)
{
	struct obex_transport *trans = &self->trans;
	int status;
	int fd = trans->fd;
	fd_set fdset;
	struct timeval time = {trans->timeout, 0};

	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);
	if (trans->timeout >= 0)
		status = select((int)fd+1, NULL, &fdset, NULL, &time);
	else
		status = select((int)fd+1, NULL, &fdset, NULL, NULL);

	if (status == 0)
		return 0;

#ifdef _WIN32
	status = _read(fd, buf, buflen);
#else
	status = read(fd, buf, buflen);
	/* The following are not really transport errors */
	if (status == -1 &&
	    (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
		status = 0;
#endif

	return status;
}

void fdobex_get_ops(struct obex_transport_ops* ops)
{
	ops->init = &fdobex_init;
	ops->write = &fdobex_write;
	ops->read = &fdobex_read;
	ops->client.connect = &fdobex_connect_request;
	ops->client.disconnect = &fdobex_disconnect_request;
}
