#include "obex_main.h"
#include "customtrans.h"

#include <errno.h>

static int custom_connect_request(obex_t *self)
{
	if (self->ctrans.connect)
		return self->ctrans.connect(self, self->ctrans.customdata);
	else {
		DEBUG(4, "No connect-callback exist!\n");
		errno = EINVAL;
		return -1;		
	}
}

static int custom_disconnect_request(obex_t *self)
{
	if (self->ctrans.disconnect)
		self->ctrans.disconnect(self, self->ctrans.customdata);
	else {
		DEBUG(4, "No disconnect-callback exist!\n");
	}
	return 0;
}

static int custom_listen(obex_t *self)
{
	if (self->ctrans.listen)
		return self->ctrans.listen(self, self->ctrans.customdata);
	else {
		DEBUG(4, "No listen-callback exist!\n");
		return -1;
	}
}
static int custom_handle_input(obex_t *self, int timeout)
{
	if (self->ctrans.handleinput)
		return self->ctrans.handleinput(self, self->ctrans.customdata, timeout);
	else {
		DEBUG(4, "No handleinput-callback exist!\n");
		return -1;
	}
}

static int custom_write(obex_t *self, buf_t *msg)
{
	if (self->ctrans.write)
		return self->ctrans.write(self, self->ctrans.customdata, msg->data, msg->data_size);
	else {
		DEBUG(4, "No write-callback exist!\n");
		return -1;
	}
}

void custom_get_ops(struct obex_transport_ops* ops)
{
	ops->handle_input = &custom_handle_input;
	ops->write = &custom_write;
	ops->server.listen = &custom_listen;
	ops->client.connect = &custom_connect_request;
	ops->client.disconnect = &custom_disconnect_request;
};
