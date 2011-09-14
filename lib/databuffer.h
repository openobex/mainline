/**
	\file databuffer.h
	Network buffer handling routines.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 2005 Herton Ronaldo Krzesinski, All Rights Reserved.

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

#ifndef DATABUFFER_H
#define DATABUFFER_H

#define __need_size_t
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * Implements a single linked list
 */
struct databuffer_list {
        void *data;
        struct databuffer_list *next;
};
typedef struct databuffer_list slist_t;

struct databuffer {
	uint8_t *buffer; // buffer containing the allocated space
	uint8_t *data; // pointer to the location of *buffer where there is valid data
	size_t head_avail; // number of allocated space available in front of buffer
	size_t data_avail; // allocated space available not specific for head or tail
	size_t tail_avail; // number of allocated space available at end of buffer
	size_t data_size; // number of allocated space used
};
typedef struct databuffer buf_t;

buf_t *buf_new(size_t default_size);
size_t buf_total_size(const buf_t *p);
int buf_empty(const buf_t *p);
void buf_resize(buf_t *p, size_t new_size);
buf_t *buf_reuse(buf_t *p);
void *buf_reserve_begin(buf_t *p, size_t data_size);
void *buf_reserve_end(buf_t *p, size_t data_size);
void buf_insert_begin(buf_t *p, const uint8_t *data, size_t data_size);
void buf_insert_end(buf_t *p, const uint8_t *data, size_t data_size);
void buf_remove_begin(buf_t *p, size_t data_size);
void buf_remove_end(buf_t *p, size_t data_size);
void buf_dump(buf_t *p, const char *label);
void buf_free(buf_t *p);

static __inline void* buf_get(const buf_t *p) {
	if (!p)
		return NULL;
	else
		return p->data;
}

static __inline size_t buf_size(const buf_t *p) {
	if (!p)
		return 0;
	else
		return p->data_size;
}

#define slist_is_empty(l) ((l) == NULL)
int slist_has_more(slist_t *list);
void *slist_get(slist_t *list);
slist_t *slist_append(slist_t *list, void *element);
slist_t *slist_remove(slist_t *list, void *element);

#endif
