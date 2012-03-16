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

/** This implements an abstracted data buffer. */
struct databuffer_ops {
	void* (*new)(size_t default_size);
	void (*delete)(void *self);
	size_t (*get_offset)(void *self);
	void (*set_offset)(void *self, size_t offset);
	size_t (*get_size)(void *self);
	int (*set_size)(void *self, size_t new_size);
	size_t (*get_length)(const void *self);
	void *(*get)(const void *self);
	void (*clear)(void *self, size_t len);
	int (*append)(void *self, const void *data, size_t len);
};

struct databuffer {
	struct databuffer_ops *ops;
	void *ops_data;
};
typedef struct databuffer buf_t;

#include <membuf.h>
#define buf_new(x)  membuf_create(x)

struct databuffer *buf_create(size_t default_size, struct databuffer_ops *ops);
void buf_delete(struct databuffer *self);
size_t buf_get_offset(struct databuffer *self);
void buf_set_offset(struct databuffer *self, size_t offset);
size_t buf_get_size(struct databuffer *self);
int buf_set_size(struct databuffer *self, size_t new_size);
size_t buf_get_length(const struct databuffer *self);
void *buf_get(const struct databuffer *self);
void buf_clear(struct databuffer *self, size_t len);
int buf_append(struct databuffer *self, const void *data, size_t len);
void buf_dump(buf_t *p, const char *label);

/* Old API */
int buf_empty(const buf_t *p);
buf_t *buf_reuse(buf_t *p);
void *buf_reserve_begin(buf_t *p, size_t data_size);
void *buf_reserve_end(buf_t *p, size_t data_size);
void buf_insert_begin(buf_t *p, const void *data, size_t data_size);
#define buf_size(p) buf_get_length(p)
#define buf_resize(p,new_size) buf_set_size(p, new_size)
#define buf_insert_end(p,data,data_size) buf_append(p, data, data_size)
#define buf_remove_begin(p,data_size) buf_clear(p, data_size)
#define buf_remove_end(p,data_size) buf_set_size(p, buf_size(p) - (data_size))
#define buf_free(p) buf_delete(p)

#define slist_is_empty(l) ((l) == NULL)
int slist_has_more(slist_t *list);
void *slist_get(slist_t *list);
slist_t *slist_append(slist_t *list, void *element);
slist_t *slist_remove(slist_t *list, void *element);

#endif
