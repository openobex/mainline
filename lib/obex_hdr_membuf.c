/**
 * @file obex_hdr_membuf.c
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

#include <membuf.h>
#include <obex_hdr.h>

#include <string.h>

struct obex_hdr_membuf {
	enum obex_hdr_id id;
	enum obex_hdr_type type;
	struct databuffer *buf;
};

static
void * obex_hdr_membuf_new(enum obex_hdr_id id, enum obex_hdr_type type,
			   const void *value, size_t size)
{
	struct obex_hdr_membuf *hdr = malloc(sizeof(*hdr));

	if (!hdr)
		return NULL;

	hdr->id = id;
	hdr->type = type;
	hdr->buf = membuf_create(size);
	if (hdr->buf == NULL) {
		free(hdr);
		return NULL;
	}

	buf_append(hdr->buf, value, size);
	return hdr;
}

static
void obex_hdr_membuf_destroy(void *self)
{
	struct obex_hdr_membuf *hdr = self;
	buf_delete(hdr->buf);
	free(hdr);
}

static
enum obex_hdr_id obex_hdr_membuf_get_id(void *self)
{
	struct obex_hdr_membuf *hdr = self;
	return hdr->id;
}

static
enum obex_hdr_type obex_hdr_membuf_get_type(void *self)
{
	struct obex_hdr_membuf *hdr = self;
	return hdr->type;
}

static
size_t obex_hdr_membuf_get_data_size(void *self)
{
	struct obex_hdr_membuf *hdr = self;
	return buf_get_length(hdr->buf);
}

static
const void * obex_hdr_membuf_get_data_ptr(void *self)
{
	struct obex_hdr_membuf *hdr = self;
	return buf_get(hdr->buf);
}

static
bool obex_hdr_membuf_set_data(void *self, const void *data, size_t size)
{
	struct obex_hdr_membuf *hdr = self;
	buf_clear(hdr->buf, buf_get_length(hdr->buf));
	if (buf_set_size(hdr->buf, size) != 0)
		return false;
	memcpy(buf_get(hdr->buf), data, size);
	return true;
}

static
struct obex_hdr_ops obex_hdr_membuf_ops = {
	&obex_hdr_membuf_destroy,
	&obex_hdr_membuf_get_id,
	&obex_hdr_membuf_get_type,
	&obex_hdr_membuf_get_data_size,
	&obex_hdr_membuf_get_data_ptr,
	&obex_hdr_membuf_set_data,
	NULL,
	NULL,
};

struct obex_hdr * obex_hdr_membuf_create(enum obex_hdr_id id,
					 enum obex_hdr_type type,
					 const void *data, size_t size)
{
	void *buf = obex_hdr_membuf_new(id, type, data, size);

	if (!buf)
		return NULL;

	return obex_hdr_new(&obex_hdr_membuf_ops, buf);
}

struct databuffer * obex_hdr_membuf_get_databuffer(struct obex_hdr *hdr)
{
	struct obex_hdr_membuf *memhdr = hdr->data;
	return memhdr->buf;
}
