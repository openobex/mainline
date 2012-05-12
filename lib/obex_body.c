/**
 * @file obex_body.c
 *
 * OBEX body reception releated functions.
 * OpenOBEX library - Free implementation of the Object Exchange protocol.
 *
 * Copyright (c) 1999, 2000 Pontus Fuchs, All Rights Reserved.
 * Copyright (c) 1999, 2000 Dag Brattli, All Rights Reserved.
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

#include <obex_body.h>
#include <obex_hdr.h>
#include <obex_main.h>
#include <obex_object.h>

int obex_body_rcv(struct obex_body *self, struct obex_hdr *hdr)
{
	if (self && self->ops && self->ops->rcv)
		return self->ops->rcv(self->data, hdr);
	else
		return -1;
}

const void * obex_body_read(struct obex_body *self, size_t *size)
{
	if (self && self->ops && self->ops->read)
		return self->ops->read(self->data, size);
	else
		return NULL;
}

static int obex_body_stream_rcv(void *self, struct obex_hdr *hdr)
{
	obex_t *obex = self;
	obex_object_t *object = obex->object;
	uint8_t cmd = obex_object_getcmd(object);
	enum obex_hdr_id id = obex_hdr_get_id(hdr);
	enum obex_hdr_type type = obex_hdr_get_type(hdr);
	size_t len = obex_hdr_get_data_size(hdr);

	DEBUG(4, "\n");

	/* Spare the app this empty nonlast body-hdr */
	if (obex_hdr_get_id(hdr) == OBEX_HDR_ID_BODY &&
	    obex_hdr_get_data_size(hdr) == 0)
		return 1;

	/* Notify app that data has arrived */
	object->body = hdr;
	obex_deliver_event(obex, OBEX_EV_STREAMAVAIL, cmd, 0, FALSE);
	object->body = NULL;

	/* If send send EOS to app */
	if (id == OBEX_HDR_ID_BODY_END && len != 0) {
		object->body = obex_hdr_ptr_create(id, type, NULL, 0);
		obex_deliver_event(obex, OBEX_EV_STREAMAVAIL, cmd, 0, FALSE);
		obex_hdr_destroy(object->body);
		object->body = NULL;
	}

	return 1;
}

static const void * obex_body_stream_read(void *self, size_t *size)
{
	obex_t *obex = self;
	obex_object_t *object = obex->object;
	const void *buf = obex_hdr_get_data_ptr(object->body);

	if (buf && size)
		*size = obex_hdr_get_data_size(object->body);

	return buf;
}

struct obex_body_ops obex_body_stream_ops = {
	&obex_body_stream_rcv,
	&obex_body_stream_read,
};

struct obex_body * obex_body_stream_create(obex_t *obex)
{
	struct obex_body *self = calloc(1, sizeof(*self));

	if (self) {
		self->ops = &obex_body_stream_ops;
		self->data = obex;
	}
	return self;
}

static int obex_body_buffered_rcv(void *self, struct obex_hdr *hdr)
{
	obex_object_t *object = self;
	const void *data = obex_hdr_get_data_ptr(hdr);
	size_t len = obex_hdr_get_data_size(hdr);

	DEBUG(4, "This is a body-header.\n");

	if (!object->body) {
		int alloclen = obex_hdr_get_data_size(hdr);

		if (object->hinted_body_len)
			alloclen = object->hinted_body_len;

		DEBUG(4, "Allocating new body-buffer. Len=%d\n", alloclen);
		object->body = obex_hdr_membuf_create(OBEX_HDR_ID_BODY,
						      OBEX_HDR_TYPE_BYTES,
						      data, len);
		if (!object->body)
			return -1;

	} else {
		struct databuffer *buf;
		buf = obex_hdr_membuf_get_databuffer(object->body);
		if (buf_append(buf, data, len) < 0)
			return -1;
	}

	if (obex_hdr_get_id(hdr) == OBEX_HDR_ID_BODY_END) {
		hdr = object->body;
		object->body = NULL;

		/* Add element to rx-list */
		object->rx_headerq = slist_append(object->rx_headerq, hdr);

		if (object->rx_it == NULL)
			object->rx_it = obex_hdr_it_create(object->rx_headerq);
	}

	return 1;
}

static const void * obex_body_buffered_read(void *self, size_t *size)
{
	obex_object_t *object = self;
	const void *buf = obex_hdr_get_data_ptr(object->body);

	if (buf && size)
		*size = obex_hdr_get_data_size(object->body);

	return buf;
}

struct obex_body_ops obex_body_buffered_ops = {
	&obex_body_buffered_rcv,
	&obex_body_buffered_read,
};

struct obex_body * obex_body_buffered_create(obex_object_t *object)
{
	struct obex_body *self = calloc(1, sizeof(*self));

	if (self) {
		self->ops = &obex_body_buffered_ops;
		self->data = object;
	}
	return self;
}
