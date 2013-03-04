/**
	\file databuffer.c
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

#include "databuffer.h"
#include "obex_main.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int slist_has_more(slist_t *list)
{
	return (!slist_is_empty(list) && !slist_is_empty(list->next));
}

void *slist_get(slist_t *list)
{
	if (slist_is_empty(list))
		return NULL;
	else
		return list->data;
}

slist_t *slist_append(slist_t *list, void *element)
{
	slist_t *node, *p;

	node = malloc(sizeof(*node));
	assert(node != NULL);
	node->data = element;
	node->next = NULL;
	if (!list)
		return node;
	p = list;
	while (p->next)
		p = p->next;
	p->next = node;
	return list;
}

slist_t *slist_remove(slist_t *list, void *element)
{
	slist_t *prev, *next;

	if (!list)
		return NULL;
	prev = list;
	next = list;
	while (next != NULL) {
		if (next->data == element) {
			/* if first element, update list pointer */
			if (next == list) {
				list = list->next;
				prev = list;
				free(next);
				next = prev;
			} else {
				prev->next = next->next;
				free(next);
				next = prev->next;
			}
			continue;
		}
		prev = next;
		next = next->next;
	}
	return list;
}

struct databuffer *buf_create(size_t default_size, struct databuffer_ops *ops) {
	struct databuffer *self = malloc(sizeof(*self));

	if (!self)
		return NULL;

	self->ops = ops;
	self->ops_data = self->ops->new(default_size);
	if (!self->ops_data) {
		free(self);
		return NULL;
	}

	return self;
}

void buf_delete(struct databuffer *self) {
	if (self->ops->delete)
		self->ops->delete(self->ops_data);
	free(self);
}

size_t buf_get_offset(struct databuffer *self) {
	if (self->ops->get_offset)
		return self->ops->get_offset(self->ops_data);
	else
		return 0;
}

void buf_set_offset(struct databuffer *self, size_t offset) {
	if (self->ops->set_offset)
		self->ops->set_offset(self->ops_data, offset);
}

size_t buf_get_size(struct databuffer *self) {
	if (self->ops->get_size)
		return self->ops->get_size(self->ops_data);
	else
		return 0;
}

int buf_set_size(struct databuffer *self, size_t new_size) {
	if (self->ops->set_size)
		return self->ops->set_size(self->ops_data, new_size);
	else
		return 0;
}

size_t buf_get_length(const struct databuffer *self) {
	if (self->ops->get_length)
		return self->ops->get_length(self->ops_data);
	else
		return 0;
}

void *buf_get(const struct databuffer *self) {
	if (self->ops->get)
		return self->ops->get(self->ops_data);
	else
		return NULL;
}

void buf_clear(struct databuffer *self, size_t len) {
	if (self->ops->clear)
		self->ops->clear(self->ops_data, len);
}

int buf_append(struct databuffer *self, const void *data, size_t len) {
	if (self->ops->append)
		return self->ops->append(self->ops_data, data, len);
	else
		return -EINVAL;
}

void buf_dump(buf_t *p, const char *label)
{
	unsigned int i, n;

	if (!p || !label)
		return;

	n = 0;
	for (i = 0; i < buf_get_length(p); ++i) {
		if (n == 0)
			log_debug("%s%s(%04x):", log_debug_prefix, label, i);
		log_debug(" %02X", *((uint8_t *)buf_get(p) + i));
		if (n >= 0xF || i == buf_get_length(p) - 1) {
			log_debug("\n");
			n = 0;
		} else
			n++;
	}
}
