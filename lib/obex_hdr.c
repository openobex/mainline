/**
 * @file obex_hdr.c
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
#include "defines.h"

#include <string.h>

struct obex_hdr * obex_hdr_create(enum obex_hdr_id id, enum obex_hdr_type type,
				  const void *value, size_t size,
				  unsigned int flags)
{
	struct obex_hdr *hdr;
	const unsigned int save_flags = OBEX_FL_SUSPEND;

	if (flags & OBEX_FL_COPY)
		hdr = obex_hdr_membuf_create(id, type, value, size);
	else
		hdr = obex_hdr_ptr_create(id, type, value, size);

	if (!hdr)
		return NULL;

	hdr->flags |= (flags & save_flags);
	return hdr;
}

struct obex_hdr * obex_hdr_new(struct obex_hdr_ops *ops, void *data)
{
	struct obex_hdr *hdr = calloc(1, sizeof(*hdr));
	if (!hdr) {
		if (ops && ops->destroy)
			ops->destroy(data);
		return NULL;
	}
	hdr->ops = ops;
	hdr->data = data;
	return hdr;
}

void obex_hdr_destroy(struct obex_hdr *hdr)
{
	if (hdr->ops && hdr->ops->destroy)
		hdr->ops->destroy(hdr->data);
	hdr->ops = NULL;
	hdr->data = NULL;
	free(hdr);
}

enum obex_hdr_id obex_hdr_get_id(struct obex_hdr *hdr)
{
	if (hdr->ops && hdr->ops->get_id)
		return hdr->ops->get_id(hdr->data);
	else
		return OBEX_HDR_ID_INVALID;
}

enum obex_hdr_type obex_hdr_get_type(struct obex_hdr *hdr)
{
	if (hdr->ops && hdr->ops->get_type)
		return hdr->ops->get_type(hdr->data);
	else
		return OBEX_HDR_TYPE_INVALID;
}

static size_t obex_hdr_get_hdr_size(struct obex_hdr *hdr)
{
	switch (obex_hdr_get_type(hdr)) {
	case OBEX_HDR_TYPE_UINT8:
	case OBEX_HDR_TYPE_UINT32:
		return 1;

	case OBEX_HDR_TYPE_BYTES:
	case OBEX_HDR_TYPE_UNICODE:
		return 3;

	default:
		return 0;
	}
}

size_t obex_hdr_get_data_size(struct obex_hdr *hdr)
{
	if (hdr->ops && hdr->ops->get_data_size)
		if (hdr->ops && hdr->ops->append_data)
			return hdr->ops->get_data_size(hdr->data);
		else
			return hdr->ops->get_data_size(hdr->data) - hdr->offset;
	else
		return (size_t)0;
}

size_t obex_hdr_get_size(struct obex_hdr *hdr)
{
	size_t hdr_size = obex_hdr_get_hdr_size(hdr);
	size_t data_size = obex_hdr_get_data_size(hdr);

	return hdr_size + data_size;
}

const void * obex_hdr_get_data_ptr(struct obex_hdr *hdr)
{
	if (hdr->ops && hdr->ops->get_data_ptr)
		if (hdr->ops && hdr->ops->append_data)
			return hdr->ops->get_data_ptr(hdr->data);
		else
			return (uint8_t *)hdr->ops->get_data_ptr(hdr->data) + hdr->offset;
	else
		return NULL;
}

bool obex_hdr_set_data(struct obex_hdr *hdr, const void *data, size_t size)
{
	if (hdr->ops && hdr->ops->set_data)
		return hdr->ops->set_data(hdr->data, data, size);
	else
		return false;
}

bool obex_hdr_is_splittable(struct obex_hdr *hdr)
{
	return (obex_hdr_get_id(hdr) == OBEX_HDR_ID_BODY &&
		obex_hdr_get_type(hdr) == OBEX_HDR_TYPE_BYTES);
}

static
size_t obex_hdr_append_data(struct obex_hdr *hdr, struct databuffer *buf,
			    size_t size)
{
	if (hdr->ops && hdr->ops->append_data)
		return hdr->ops->append_data(hdr->data, buf, size);

	buf_append(buf, obex_hdr_get_data_ptr(hdr), size);
	hdr->offset += size;

	return size;
}

#define MIN_DATA_SIZE 1

/** Append the header to a buffer
 * @param hdr the header instance
 * @param buf the buffer to append to
 * @param size maximum number of bytes to append to the buffer
 * @return the actual number of appended bytes
 */
size_t obex_hdr_append(struct obex_hdr *hdr, struct databuffer *buf,
		       size_t max_size)
{
	size_t actual = 0;
	uint8_t *h;
	size_t buflen = buf_get_length(buf);
	size_t hdr_size = obex_hdr_get_hdr_size(hdr);
	size_t data_size = obex_hdr_get_data_size(hdr);

	if (((hdr_size + data_size) > max_size && !obex_hdr_is_splittable(hdr))
	    || hdr_size + MIN_DATA_SIZE > max_size)
		return 0;

	buf_append(buf, NULL, hdr_size);
	h = (uint8_t *)buf_get(buf) + buflen;
	actual += hdr_size;
	while (max_size > actual && data_size != 0) {
		size_t ret;

		if (data_size > (max_size - actual)) {
			if (obex_hdr_is_splittable(hdr))
				data_size = max_size - actual;
			else
				return 0;
		}

		if (obex_hdr_get_type(hdr) == OBEX_HDR_TYPE_UINT8
		    && data_size != 1)
		{
			if (data_size < 1)
			{
				uint8_t dummy = 0;
				buf_append(buf, &dummy, 1);
				ret = 1;

			} else
				ret = obex_hdr_append_data(hdr, buf, 1);

		} else if (obex_hdr_get_type(hdr) == OBEX_HDR_TYPE_UINT32
			   && data_size != 4)
		{
			if (data_size < 4)
			{
				uint32_t dummy = 0;
				buf_append(buf, &dummy, 4);
				ret = 1;

			} else
				ret = obex_hdr_append_data(hdr, buf, 4);

		} else
			ret = obex_hdr_append_data(hdr, buf, data_size);


		actual += ret;
		if (ret == 0 )
			break;

		data_size = obex_hdr_get_data_size(hdr);
	} ;

	h[0] = obex_hdr_get_id(hdr) | obex_hdr_get_type(hdr);
	if (hdr_size > 1) {
		h[1] = (actual >> 8) & 0xFF;
		h[2] = actual & 0xFF;
	}

	return actual;
}

bool obex_hdr_is_finished(struct obex_hdr *hdr)
{
	if (hdr->ops && hdr->ops->is_finished)
		return hdr->ops->is_finished(hdr->data);
	else
		return (obex_hdr_get_data_size(hdr) == 0);
}

void obex_hdr_it_init_from(struct obex_hdr_it *it,
			   const struct obex_hdr_it *from)
{
	if (from) {
		it->list = from->list;
		it->is_valid = from->is_valid;
	} else {
		it->list = NULL;
		it->is_valid = FALSE;
	}
}

struct obex_hdr_it * obex_hdr_it_create(struct databuffer_list *list)
{
	struct obex_hdr_it *it = malloc(sizeof(*it));

	if (it) {
		it->list = list;
		it->is_valid = TRUE;
	}

	return it;
}

void obex_hdr_it_destroy(struct obex_hdr_it *it)
{
	if (it == NULL)
		return;

	it->list = NULL;
	free(it);
}

struct obex_hdr * obex_hdr_it_get(const struct obex_hdr_it *it)
{
	if (it->is_valid)
		return slist_get(it->list);
	else
		return NULL;
}

void obex_hdr_it_next(struct obex_hdr_it *it)
{
	if (it == NULL)
		return;

	it->is_valid = slist_has_more(it->list);

	if (it->is_valid)
		it->list = it->list->next;
}

int obex_hdr_it_equals(const struct obex_hdr_it *a, const struct obex_hdr_it *b)
{
	return a && b && (memcmp(a, b, sizeof(*a)) == 0);
}
