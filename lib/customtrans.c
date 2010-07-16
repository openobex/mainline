#include "obex_main.h"
#include "customtrans.h"

#include <errno.h>
#include <stdlib.h>

static int custom_init (obex_t *self)
{
	obex_ctrans_t *ctrans = malloc(sizeof(*ctrans));

	if (!ctrans)
		return -1;

	memset(ctrans, 0, sizeof(*ctrans));
	self->trans.data = ctrans;
	return 0;
}

static int custom_clone(obex_t *self, const obex_t *from)
{
	obex_ctrans_t *ctrans = malloc(sizeof(*ctrans));

	if (!ctrans)
		return -1;

	memcpy(ctrans, from, sizeof(*ctrans));
	self->trans.data = ctrans;
	return 0;	
}

static void custom_cleanup (obex_t *self)
{
	obex_ctrans_t *ctrans = self->trans.data;

	if (ctrans) {
		free(ctrans);
		self->trans.data = NULL;
	}
}

void custom_set_data(obex_t *self, void *data)
{
	obex_ctrans_t *ctrans = self->trans.data;

	ctrans->customdata = data;
}

void* custom_get_data(obex_t *self)
{
	obex_ctrans_t *ctrans = self->trans.data;

	return ctrans->customdata;
}

static int custom_connect_request(obex_t *self)
{
	obex_ctrans_t *ctrans = self->trans.data;

	return ctrans->connect(self, ctrans->customdata);
}

static int custom_disconnect_request(obex_t *self)
{
	obex_ctrans_t *ctrans = self->trans.data;

	return ctrans->disconnect(self, ctrans->customdata);
}

static int custom_listen(obex_t *self)
{
	obex_ctrans_t *ctrans = self->trans.data;

	return ctrans->listen(self, ctrans->customdata);
}

static int custom_handle_input(obex_t *self, int timeout)
{
	obex_ctrans_t *ctrans = self->trans.data;

	return ctrans->handleinput(self, ctrans->customdata, timeout);
}

static int custom_write(obex_t *self, buf_t *msg)
{
	obex_ctrans_t *ctrans = self->trans.data;

	return ctrans->write(self, ctrans->customdata,
			     msg->data, msg->data_size);
}

int custom_register(obex_t *self, const obex_ctrans_t *in)
{
	obex_ctrans_t *ctrans = self->trans.data;
	struct obex_transport_ops* ops = &self->trans.ops;

	if (!ctrans->handleinput || !ctrans->write)
		return -1;

	memcpy(ctrans, in, sizeof(*ctrans));
	ops->handle_input = &custom_handle_input;
	ops->write = &custom_write;

	if (ctrans->listen)
		ops->server.listen = &custom_listen;
	if (ctrans->connect)
		ops->client.connect = &custom_connect_request;
	if (ctrans->disconnect)
		ops->client.disconnect = &custom_disconnect_request;
	return 0;
}

void custom_get_ops(struct obex_transport_ops* ops)
{
	ops->init = &custom_init;
	ops->clone = &custom_clone;
	ops->cleanup = &custom_cleanup;
};
