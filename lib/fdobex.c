#include <errno.h>

#include "obex_main.h"
#include "fdobex.h"

#include <unistd.h>
#if defined(_WIN32)
#include <io.h>
#endif

static int fdobex_connect_request(obex_t *self)
{
	struct obex_transport *trans = &self->trans;

	/* no real connect on the file */
	if (trans->fd != INVALID_SOCKET &&
	    trans->writefd != INVALID_SOCKET)
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
	trans->fd = trans->writefd = INVALID_SOCKET;
	return 0;
}

static int fdobex_write(obex_t *self, buf_t *msg)
{
	struct obex_transport *trans = &self->trans;
	int fd = trans->writefd;
	unsigned int mtu = trans->mtu;
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
	struct obex_transport *trans = &self->trans;

#ifdef _WIN32
	return  _read(trans->fd, buf, buflen);
#else
	return read(trans->fd, buf, buflen);
#endif
}

void fdobex_get_ops(struct obex_transport_ops* ops)
{
	ops->write = &fdobex_write;
	ops->read = &fdobex_read;
	ops->client.connect = &fdobex_connect_request;
	ops->client.disconnect = &fdobex_disconnect_request;
};
