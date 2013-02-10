/**
	\file customtrans.c
	Custom OBEX, custom transport for OBEX.
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

#include "obex_main.h"
#include "databuffer.h"
#include "customtrans.h"

#include <errno.h>
#include <stdlib.h>

static void * custom_create (void)
{
	return calloc(1, sizeof(obex_ctrans_t));
}

static void custom_cleanup (obex_t *self)
{
	struct obex_transport_ops *ops = self->trans->ops;
	obex_ctrans_t *data = self->trans->data;

	free(ops);
	free(data);
}

void custom_set_data(obex_t *self, void *data)
{
	struct obex_transport *trans = self->trans;
	obex_ctrans_t *ctrans = trans->data;

	ctrans->customdata = data;
}

void* custom_get_data(obex_t *self)
{
	struct obex_transport *trans = self->trans;
	obex_ctrans_t *ctrans = trans->data;

	return ctrans->customdata;
}

static bool custom_connect_request(obex_t *self)
{
	struct obex_transport *trans = self->trans;
	obex_ctrans_t *ctrans = trans->data;

	return (ctrans->connect(self, ctrans->customdata) >= 0);
}

static bool custom_disconnect(obex_t *self)
{
	struct obex_transport *trans = self->trans;
	obex_ctrans_t *ctrans = trans->data;

	return (ctrans->disconnect(self, ctrans->customdata) >= 0);
}

static bool custom_listen(obex_t *self)
{
	struct obex_transport *trans = self->trans;
	obex_ctrans_t *ctrans = trans->data;

	return (ctrans->listen(self, ctrans->customdata) >= 0);
}

static bool custom_accept(obex_t *self, const obex_t *from)
{
	const obex_ctrans_t *old = from->trans->data;
	obex_ctrans_t *ctrans = self->trans->data;

	*ctrans = *old;

	return true;
}

static result_t custom_handle_input(obex_t *self)
{
	struct obex_transport *trans = self->trans;
	obex_ctrans_t *ctrans = trans->data;
	int res = ctrans->handleinput(self, ctrans->customdata, (int)((trans->timeout+999)/1000));

	if (res < 0)
		return RESULT_ERROR;
	else if (res == 0)
		return RESULT_TIMEOUT;
	else
		return RESULT_SUCCESS;
}

static ssize_t custom_write(obex_t *self, buf_t *msg)
{
	struct obex_transport *trans = self->trans;
	obex_ctrans_t *ctrans = trans->data;

	return ctrans->write(self, ctrans->customdata, buf_get(msg),
			     buf_get_length(msg));
}

static ssize_t custom_read(obex_t *self, void *buf, int size)
{
	struct obex_transport *trans = self->trans;
	obex_ctrans_t *ctrans = trans->data;

	if (ctrans->read) {
		return ctrans->read(self, ctrans->customdata, buf, size);

	} else {
		/* This is not an error as it may happen that
		 * OBEX_CustomDataFeed() was not given enough data
		 * for one OBEX packet. This would result in calling
		 * this function.
		 */
		return 0;
	}
}

int custom_register(obex_t *self, const obex_ctrans_t *in)
{
	struct obex_transport *trans = self->trans;
	obex_ctrans_t *ctrans = trans->data;
	struct obex_transport_ops *ops = self->trans->ops;

	if (!in->handleinput || !in->write)
		return -1;

	*ctrans = *in;
	ops->handle_input = &custom_handle_input;
	ops->write = &custom_write;
	ops->read = &custom_read;

	ops->server.accept = &custom_accept;
	if (ctrans->listen)
		ops->server.listen = &custom_listen;
	if (ctrans->connect)
		ops->client.connect = &custom_connect_request;
	if (ctrans->disconnect)
		ops->disconnect = &custom_disconnect;
	return 0;
}

struct obex_transport * custom_transport_create(void)
{
	struct obex_transport_ops *ops = calloc(1, sizeof(*ops));
	struct obex_transport *trans;
	
	if (!ops)
		return NULL;

	ops->create = &custom_create;
	ops->cleanup = &custom_cleanup;

	trans = obex_transport_create(ops);
	if (!trans)
		free(ops);

	return trans;
}
