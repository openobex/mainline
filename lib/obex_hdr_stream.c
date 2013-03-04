/**
 * @file obex_hdr_stream.c
 *
 * OBEX header releated functions.
 * OpenOBEX library - Free implementation of the Object Exchange protocol.
 *
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

#include <obex_hdr.h>
#include <obex_main.h>
#include <obex_object.h>

struct obex_hdr_stream {
	struct obex *obex;

	/** Pointer to streaming data */
	struct obex_hdr *data;

	/** Current offset in buf */
	size_t s_offset;
	/** End of stream */
	bool s_stop;
};

static
void obex_hdr_stream_destroy(void *self)
{
	struct obex_hdr_stream *hdr = self;
	obex_hdr_destroy(hdr->data);
	free(hdr);
}

static
enum obex_hdr_id obex_hdr_stream_get_id(void *self)
{
	struct obex_hdr_stream *hdr = self;
	return obex_hdr_get_id(hdr->data);
}

static
enum obex_hdr_type obex_hdr_stream_get_type(void *self)
{
	struct obex_hdr_stream *hdr = self;
	return obex_hdr_get_type(hdr->data);
}

static
void obex_hdr_stream_refresh(struct obex_hdr_stream *hdr)
{
	if (!hdr->s_stop) {
		struct obex *obex = hdr->obex;
		struct obex_object *object = obex->object;
		enum obex_cmd cmd = obex_object_getcmd(object);

		/* Ask app for more data if no more */
		obex_hdr_set_data(hdr->data, NULL, 0);
		hdr->s_offset = 0;
		obex_deliver_event(obex, OBEX_EV_STREAMEMPTY, cmd, 0, FALSE);
		DEBUG(4, "s_size=%lu, s_stop=%d\n",
		      (unsigned long)obex_hdr_get_data_size(hdr->data),
		      hdr->s_stop);
	}
}

static
size_t obex_hdr_stream_get_data_size(void *self)
{
	struct obex_hdr_stream *hdr = self;
	size_t data_size = obex_hdr_get_data_size(hdr->data) - hdr->s_offset;

	if (data_size == 0) {
		obex_hdr_stream_refresh(hdr);
		data_size = obex_hdr_get_data_size(hdr->data) - hdr->s_offset;
	}
	return data_size;
}

static
const void * obex_hdr_stream_get_data_ptr(void *self)
{
	struct obex_hdr_stream *hdr = self;
	const void *ptr = obex_hdr_get_data_ptr(hdr->data);

	if (ptr == NULL)
		return NULL;
	else
		return (uint8_t *)ptr + hdr->s_offset;
}

static
bool obex_hdr_stream_set_data(void *self, const void *data, size_t size)
{
	struct obex_hdr_stream *hdr = self;
	return obex_hdr_set_data(hdr->data, data, size);
}

static
size_t obex_hdr_stream_append_data(void *self, struct databuffer *buf,
				   size_t size)
{
	struct obex_hdr_stream *hdr = self;
	size_t ret = 0;
	const void *ptr;
	size_t data_size = obex_hdr_stream_get_data_size(hdr);

	/* this function _must_ only be called _after_ get_data_size() */
	ptr = obex_hdr_stream_get_data_ptr(hdr);
	if (data_size == 0 || ptr == NULL)
		return 0;

	if (size < data_size) {
		DEBUG(4, "More data than tx_left. Buffer will not be empty\n");
		buf_append(buf, ptr, size);
		hdr->s_offset += size;
		ret += size;

	} else {
		DEBUG(4, "Less data than tx_left. Buffer will be empty\n");
		ptr = obex_hdr_stream_get_data_ptr(self);
		buf_append(buf, ptr, data_size);
		hdr->s_offset += data_size;
		ret += data_size;
	}

	return ret;
}

static
bool obex_hdr_stream_is_finished(void *self)
{
	struct obex_hdr_stream *hdr = self;
	return hdr->s_stop && (obex_hdr_stream_get_data_size(hdr) == 0);
}

static
struct obex_hdr_ops obex_hdr_stream_ops = {
	&obex_hdr_stream_destroy,
	&obex_hdr_stream_get_id,
	&obex_hdr_stream_get_type,
	&obex_hdr_stream_get_data_size,
	&obex_hdr_stream_get_data_ptr,
	&obex_hdr_stream_set_data,
	&obex_hdr_stream_append_data,
	&obex_hdr_stream_is_finished,
};

struct obex_hdr * obex_hdr_stream_create(struct obex *obex,
					 struct obex_hdr *data)
{
	struct obex_hdr_stream *hdr = calloc(1, sizeof(*hdr));

	if (!hdr)
		return NULL;
	hdr->obex = obex;
	hdr->data = data;

	return obex_hdr_new(&obex_hdr_stream_ops, hdr);
}

void obex_hdr_stream_finish(struct obex_hdr *hdr)
{
	struct obex_hdr_stream *s = hdr->data;
	s->s_stop = true;
}
