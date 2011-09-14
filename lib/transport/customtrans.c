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
#include "customtrans.h"

#include <errno.h>
#include <stdlib.h>

static int custom_clone(obex_t *self, const obex_t *from)
{
	const obex_ctrans_t *old = &from->trans.data.custom;
	obex_ctrans_t *ctrans = &self->trans.data.custom;;

	*ctrans = *old;

	return 0;
}

void custom_set_data(obex_t *self, void *data)
{
	struct obex_transport *trans = &self->trans;
	obex_ctrans_t *ctrans = &trans->data.custom;

	ctrans->customdata = data;
}

void* custom_get_data(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	obex_ctrans_t *ctrans = &trans->data.custom;

	return ctrans->customdata;
}

static int custom_connect_request(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	obex_ctrans_t *ctrans = &trans->data.custom;

	return ctrans->connect(self, ctrans->customdata);
}

static int custom_disconnect_request(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	obex_ctrans_t *ctrans = &trans->data.custom;

	return ctrans->disconnect(self, ctrans->customdata);
}

static int custom_listen(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	obex_ctrans_t *ctrans = &trans->data.custom;

	return ctrans->listen(self, ctrans->customdata);
}

static int custom_handle_input(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	obex_ctrans_t *ctrans = &trans->data.custom;

	return ctrans->handleinput(self, ctrans->customdata, trans->timeout);
}

static int custom_write(obex_t *self, buf_t *msg)
{
	struct obex_transport *trans = &self->trans;
	obex_ctrans_t *ctrans = &trans->data.custom;

	return ctrans->write(self, ctrans->customdata, buf_get(msg),
			     buf_size(msg));
}

static int custom_read(obex_t *self, void *buf, int size)
{
	struct obex_transport *trans = &self->trans;
	obex_ctrans_t *ctrans = &trans->data.custom;

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
	struct obex_transport *trans = &self->trans;
	obex_ctrans_t *ctrans = &trans->data.custom;
	struct obex_transport_ops* ops = &self->trans.ops;

	if (!in->handleinput || !in->write)
		return -1;

	*ctrans = *in;
	ops->handle_input = &custom_handle_input;
	ops->write = &custom_write;
	ops->read = &custom_read;

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
	ops->clone = &custom_clone;
}
