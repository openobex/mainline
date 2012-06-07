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
#include "databuffer.h"

#include <unistd.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <io.h>
#define fd_t unsigned int
#else
#define fd_t int
#endif

struct fdobex_data {
	fd_t readfd;  /* read descriptor */
	fd_t writefd; /* write descriptor */
};

static int fdobex_init(obex_t *self)
{
	struct fdobex_data *data = self->trans->data;

	data->readfd = (fd_t)-1;
	data->writefd = (fd_t)-1;

	return 0;
}

void fdobex_set_fd(obex_t *self, int in, int out)
{
	struct fdobex_data *data = self->trans->data;

	data->readfd = (fd_t)in;
	data->writefd = (fd_t)out;  
}

static void fdobex_cleanup (obex_t *self)
{
	struct fdobex_data *data = self->trans->data;

	free(data);
}

static int fdobex_connect_request(obex_t *self)
{
	struct fdobex_data *data = self->trans->data;

	/* no real connect on the file */
	if (data->readfd != (fd_t)-1 &&
	    data->writefd != (fd_t)-1)
		return 0;
	else {
		errno = EINVAL;
		return -1;
	}
}

static int fdobex_disconnect(obex_t *self)
{
	/* no real disconnect on a file */
	return fdobex_init(self);
}

static int fdobex_write(obex_t *self, buf_t *msg)
{
	struct obex_transport *trans = self->trans;
	struct fdobex_data *data = self->trans->data;
	fd_t fd = data->writefd;
	size_t size = buf_get_length(msg);
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
		status = select(fd+1, NULL, &fdset, NULL, &time);
	else
		status = select(fd+1, NULL, &fdset, NULL, NULL);

	if (status == 0)
		return 0;

#if defined(_WIN32)
	status = _write(fd, buf_get(msg), size);
#else
	status = write(fd, buf_get(msg), size);
	/* The following are not really transport errors. */
	if (status == -1 &&
	    (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
		status = 0;
#endif

	return status;
}

static int fdobex_handle_input(obex_t *self)
{
	struct obex_transport *trans = self->trans;
	struct fdobex_data *data = self->trans->data;
	fd_t fd = data->readfd;
	struct timeval time = {trans->timeout, 0};
	fd_set fdset;
	int status;

	DEBUG(4, "\n");

	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);
	if (trans->timeout >= 0)
		status = select(fd+1, NULL, &fdset, NULL, &time);
	else
		status = select(fd+1, NULL, &fdset, NULL, NULL);

	return status;
}

static int fdobex_read(obex_t *self, void *buf, int buflen)
{
	struct fdobex_data *data = self->trans->data;
	fd_t fd = data->readfd;
	int status;

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

static int fdobex_get_fd(obex_t *self)
{
	struct fdobex_data *data = self->trans->data;

	return data->readfd;
}

static struct obex_transport_ops fdobex_transport_ops = {
	&fdobex_init,
	&fdobex_cleanup,

	&fdobex_handle_input,
	&fdobex_write,
	&fdobex_read,
	&fdobex_disconnect,

	&fdobex_get_fd,
	NULL,
	NULL,

	{
		NULL,
		NULL,
	},

	{
		&fdobex_connect_request,
		NULL,
		NULL,
		NULL,
	},
};

struct obex_transport * fdobex_transport_create(void) {
	struct fdobex_data *data = calloc(1, sizeof(*data));
	struct obex_transport *trans;

	if (!data)
		return NULL;

	trans = obex_transport_create(&fdobex_transport_ops, data);
	if (!trans)
		free(data);

	return trans;
}
