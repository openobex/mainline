/**
	\file membuf.c
	Memory buffer handling routines.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 2012 Hendrik Sattler, All Rights Reserved.

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

#include "membuf.h"
#include "databuffer.h"
#include "debug.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

/* If an object has no expected length we have to reallocate every
 * ALLOCATION_TRESHOLD bytes */
#if defined _SC_PAGESIZE
#define ALLOCATION_TRESHOLD (size_t)sysconf(_SC_PAGESIZE)
#else
#define ALLOCATION_TRESHOLD (size_t)4096
#endif

struct membuf_data {
	uint8_t *buffer;
	size_t buffer_size;

	size_t offset;
	size_t data_len;
};

static int membuf_set_size(void *self, size_t new_size) {
	struct membuf_data *p = self;
	uint8_t *tmp;

	new_size += p->offset;
	if (new_size == 0 && p->buffer == NULL)
		return 0;

	if (new_size == 0) {
		free(p->buffer);
		p->buffer = NULL;
		p->data_len = 0;
		p->buffer_size = 0;
		return 0;
	}

	tmp = realloc(p->buffer, new_size);
	if (!tmp)
		return -errno;

	p->buffer = tmp;
	p->buffer_size = new_size;

	if (p->buffer_size < (p->offset + p->data_len)) {
		p->data_len = p->buffer_size - p->offset;

	} else if (p->buffer_size > (p->offset + p->data_len)) {		
		memset(p->buffer + (p->offset + p->data_len), 0,
		       p->buffer_size - (p->offset + p->data_len));
	}
	return 0;
}

static void *membuf_new(size_t default_size) {
	struct membuf_data *p;

	p = malloc(sizeof(*p));
	if (!p)
		return NULL;

	p->buffer = NULL;
	p->buffer_size = 0;
	p->offset = 0;
	p->data_len = 0;

	if (membuf_set_size(p, default_size) < 0) {
		free(p);
		p = NULL;
	}

	return (void*)p;
}

static void membuf_delete(void *self) {
	struct membuf_data *p = self;	

	if (!p)
		return;
	if (p->buffer) {
		free(p->buffer);
	}
	free(p);
}

static size_t membuf_get_offset(void *self) {
	struct membuf_data *p = self;

	if (!p)
		return 0;
	else
		return p->offset;
}

static void membuf_set_offset(void *self, size_t offset) {
	struct membuf_data *p = self;

	if (!p)
		return;
	p->data_len = (p->offset + p->data_len) - offset;
	p->offset = offset;
}

static size_t membuf_get_size(void *self) {
	struct membuf_data *p = self;

	if (!p)
		return 0;
	else
		return p->buffer_size - p->offset;
}

static size_t membuf_get_length(const void *self) {
	const struct membuf_data *p = self;

	if (!p)
		return 0;
	else
		return p->data_len;
}

static void* membuf_get(const void *self) {
	const struct membuf_data *p = self;

	if (!p)
		return NULL;
	else
		return p->buffer + p->offset;
}

static void membuf_clear(void *self, size_t len) {
	struct membuf_data *p = self;

	if (!p || !p->data_len)
		return;

	if (len < p->data_len)
		memmove(membuf_get(self), (uint8_t *)membuf_get(self)+len,
			p->data_len - len);

	p->data_len -= len;
	memset((uint8_t *)membuf_get(self) + p->data_len, 0, len);
}

static int membuf_append(void *self, const void *data, size_t len) {
	struct membuf_data *p = self;

	if (!p)
		return -EINVAL;

	DEBUG(4, "Request to append %lu bytes\n", (unsigned long)len);
	if (len > (p->buffer_size - (p->offset + p->data_len))) {
		size_t alloc_size = ALLOCATION_TRESHOLD;
		int ret;
		if (len > alloc_size)
			alloc_size = len;
		DEBUG(4, "Resizing buffer to %lu\n",
		      (unsigned long)p->buffer_size + alloc_size);
		ret = membuf_set_size(self, p->buffer_size + alloc_size);
		if (ret < 0) {
			DEBUG(4, "Resizing failed\n");
			return ret;
		}
	}

	if (data) {
		void *dest = ((uint8_t *)p->buffer) + (p->offset + p->data_len);
		memcpy(dest, data, len);
	}
	p->data_len += len;
	return 0;
}

static struct databuffer_ops membuf_ops = {
	&membuf_new,
	&membuf_delete,
	&membuf_get_offset,
	&membuf_set_offset,
	&membuf_get_size,
	&membuf_set_size,
	&membuf_get_length,
	&membuf_get,
	&membuf_clear,
	&membuf_append,
};

struct databuffer *membuf_create(size_t default_size) {
	return buf_create(default_size, &membuf_ops);
}
