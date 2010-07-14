#include <errno.h>

#include "obex_main.h"
#include "fdobex.h"

#include <unistd.h>
#if defined(_WIN32)
#include <io.h>
#endif

static int fdobex_connect_request(obex_t *self)
{
	/* no real connect on the file */
	if (self->fd != INVALID_SOCKET &&
	    self->writefd != INVALID_SOCKET)
		return 0;
	else {
		errno = EINVAL;
		return -1;
	}
}

static int fdobex_disconnect_request(obex_t *self)
{
	/* no real disconnect on a file */
	self->fd = self->writefd = INVALID_SOCKET;
	return 0;
}

static int fdobex_write(obex_t *self, buf_t *msg)
{
	int fd = self->writefd;
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

#ifdef _WIN32
		actual = _write(fd, msg->data, size);
#else
		actual = write(fd, msg->data, size);
#endif
		if (actual <= 0)
			return actual;

		/* Hide sent data */
		buf_remove_begin(msg, actual);
	}

	return actual;
}

static int fdobex_read (obex_t *self, void *buf, int buflen)
{
#ifdef _WIN32
	return  _read(self->fd, buf, buflen);
#else
	return read(self->fd, buf, buflen);
#endif
}

void fdobex_get_ops(struct obex_transport_ops* ops)
{
	ops->write = &fdobex_write;
	ops->read = &fdobex_read;
	ops->client.connect = &fdobex_connect_request;
	ops->client.disconnect = &fdobex_disconnect_request;
};
