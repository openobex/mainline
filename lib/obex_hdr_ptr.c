/**
 * @file obex_hdr_ptr.c
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

struct obex_hdr_ptr {
	enum obex_hdr_id id;
	enum obex_hdr_type type;
	size_t size;
	const void *value;
};

static
void obex_hdr_ptr_destroy(void *self)
{
	free(self);
}

static
enum obex_hdr_id obex_hdr_ptr_get_id(void *self)
{
	struct obex_hdr_ptr *ptr = self;
	return ptr->id;
}

static
enum obex_hdr_type obex_hdr_ptr_get_type(void *self)
{
	struct obex_hdr_ptr *ptr = self;
	return ptr->type;
}

static
size_t obex_hdr_ptr_get_data_size(void *self)
{
	struct obex_hdr_ptr *ptr = self;
	return ptr->size;
}

static
const void * obex_hdr_ptr_get_data_ptr(void *self)
{
	struct obex_hdr_ptr *ptr = self;
	return ptr->value;
}

static
struct obex_hdr_ops obex_hdr_ptr_ops = {
	&obex_hdr_ptr_destroy,
	&obex_hdr_ptr_get_id,
	&obex_hdr_ptr_get_type,
	&obex_hdr_ptr_get_data_size,
	&obex_hdr_ptr_get_data_ptr,
	NULL,
	NULL,
};

struct obex_hdr * obex_hdr_ptr_create(enum obex_hdr_id id,
				      enum obex_hdr_type type,
				      const void *data, size_t size)
{
	struct obex_hdr_ptr *ptr = malloc(sizeof(*ptr));

	if (!ptr)
		return NULL;

	ptr->id = id;
	ptr->type = type;
	ptr->size = size;
	ptr->value = data;

	return obex_hdr_new(&obex_hdr_ptr_ops, ptr);
}

struct obex_hdr * obex_hdr_ptr_parse(struct databuffer *buf, size_t offset)
{
	const uint8_t *msgdata = buf_get(buf) + offset;
	struct obex_hdr_ptr *ptr = malloc(sizeof(*ptr));
	uint16_t hsize;

	if (!ptr)
		return NULL;

	ptr->id = msgdata[0] & OBEX_HDR_ID_MASK;
	ptr->type = msgdata[0] & OBEX_HDR_TYPE_MASK;

	switch (ptr->type) {
	case OBEX_HDR_TYPE_UNICODE:
	case OBEX_HDR_TYPE_BYTES:
		memcpy(&hsize, &msgdata[1], 2);
		ptr->size = ntohs(hsize) - 3;
		ptr->value = &msgdata[3];
		break;

	case OBEX_HDR_TYPE_UINT8:
		ptr->size = 1;
		ptr->value = &msgdata[1];
		break;

	case OBEX_HDR_TYPE_UINT32:
		ptr->size = 4;
		ptr->value = &msgdata[1];
		break;

	default:
		free(ptr);
		ptr = NULL;
		break;
	}

	return obex_hdr_new(&obex_hdr_ptr_ops, ptr);
}
