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

#include "obex_hdr.h"
#include "debug.h"

#ifndef _WIN32
#include <arpa/inet.h>
#endif
#include <string.h>

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
bool obex_hdr_ptr_set_data(void *self, const void *data, size_t size)
{
	struct obex_hdr_ptr *ptr = self;
	ptr->value = data;
	ptr->size = size;
	return true;
}

static
struct obex_hdr_ops obex_hdr_ptr_ops = {
	&obex_hdr_ptr_destroy,
	&obex_hdr_ptr_get_id,
	&obex_hdr_ptr_get_type,
	&obex_hdr_ptr_get_data_size,
	&obex_hdr_ptr_get_data_ptr,
	&obex_hdr_ptr_set_data,
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

struct obex_hdr * obex_hdr_ptr_parse(const void *msgdata, size_t size)
{
	struct obex_hdr_ptr *ptr;
	uint16_t hsize;

	if (size < 1)
		return NULL;

	ptr = malloc(sizeof(*ptr));
	if (!ptr)
		return NULL;

	ptr->id = ((uint8_t *)msgdata)[0] & OBEX_HDR_ID_MASK;
	ptr->type = ((uint8_t *)msgdata)[0] & OBEX_HDR_TYPE_MASK;

	switch (ptr->type) {
	case OBEX_HDR_TYPE_UNICODE:
	case OBEX_HDR_TYPE_BYTES:
		if (size < 3)
			goto err;
		memcpy(&hsize, (uint8_t *)msgdata + 1, 2);
		ptr->size = ntohs(hsize) - 3;
		if (size < (3 + ptr->size))
			goto err;
		ptr->value = (uint8_t *)msgdata + 3;
		break;

	case OBEX_HDR_TYPE_UINT8:
		if (size < 2)
			goto err;
		ptr->size = 1;
		ptr->value = (uint8_t *)msgdata + 1;
		break;

	case OBEX_HDR_TYPE_UINT32:
		if (size < 5)
			goto err;
		ptr->size = 4;
		ptr->value = (uint8_t *)msgdata + 1;
		break;

	default:
		goto err;
	}

	return obex_hdr_new(&obex_hdr_ptr_ops, ptr);

err:
	DEBUG(1, "Header too big.\n");
	free(ptr);
	return NULL;
}
