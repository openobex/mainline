#include <errno.h>

#include "obex_main.h"
#include "fdobex.h"

#include <unistd.h>
#if defined(_WIN32)
#include <io.h>
#define ETIMEDOUT WSAETIMEDOUT
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
	int actual = 0;

	/* Send and fragment if necessary  */
	while (msg->data_size) {
		int status = 1;
		int size = msg->data_size;

		if (msg->data_size > trans->mtu)
			size = trans->mtu;
		DEBUG(1, "sending %d bytes\n", size);

		if (trans->timeout >= 0) {
			/* setup everything to check for blocking writes */
			fd_set fdset;
			struct timeval time = {trans->timeout, 0};

			FD_ZERO(&fdset);
			FD_SET(fd, &fdset);
			status = select((int)fd+1, NULL, &fdset, NULL, &time);
			if (status == 0) {
				errno = ETIMEDOUT;
				status = -1;
			}
		}

		/* call write() if no error */
		if (status > 0) {
#ifdef _WIN32
			status = _write(fd, msg->data, size);
#else
			status = write(fd, msg->data, size);
#endif
		}

		if (status < 0)
			return -1;

		/* Hide sent data */
		buf_remove_begin(msg, status);
		actual += status;
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
