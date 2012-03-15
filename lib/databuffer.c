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

size_t buf_total_size(const buf_t *p)
{
	if (!p)
		return 0;
	return p->ops->get_size(p->ops_data);
}

void* buf_get(const buf_t *p)
{
	if (!p)
		return 0;
	return p->ops->get(p->ops_data);
}

size_t buf_size(const buf_t *p)
{
	if (!p)
		return 0;
	return p->ops->get_length(p->ops_data);
}

int buf_empty(const buf_t *p)
{
	return (buf_size(p) == 0);
}

void buf_resize(buf_t *p, size_t new_size)
{
	if (!p)
		return;
	p->ops->set_size(p->ops_data, new_size);
}

buf_t *buf_reuse(buf_t *p)
{
	if (!p)
		return NULL;
	p->ops->clear(p->ops_data, p->ops->get_length(p->ops_data));
	return p;
}

void *buf_reserve_begin(buf_t *p, size_t data_size)
{
	void *old;
	size_t old_offset;

	if (!p)
		return NULL;

	old = p->ops_data;
	old_offset = p->ops->get_offset(old);
	p->ops->set_offset(old, 0);

	p->ops_data = p->ops->new(buf_total_size(p)+data_size);
	p->ops->append(p->ops_data, p->ops->get(old), old_offset);
	p->ops->set_offset(p->ops_data, old_offset);
	p->ops->set_offset(old, old_offset);

	p->ops->append(p->ops_data, NULL, data_size);
	p->ops->append(p->ops_data, p->ops->get(old), p->ops->get_length(old));
	p->ops->delete(old);
	return buf_get(p);
}

void *buf_reserve_end(buf_t *p, size_t data_size)
{
	size_t len;

	if (!p)
		return NULL;

	len = buf_size(p);
	if (p->ops->append(p, NULL, data_size) < 0)
		return NULL;

	return buf_get(p)+len;
}

void buf_insert_begin(buf_t *p, const void *data, size_t data_size)
{
	uint8_t *dest;

	if (!p)
		return;

	dest = buf_reserve_begin(p, data_size);
	assert(dest != NULL);
	memcpy(dest, data, data_size);
}

int buf_insert_end(buf_t *p, const void *data, size_t data_size)
{
	if (!p)
		return -EINVAL;
	return p->ops->append(p->ops_data, data, data_size);
}

void buf_remove_begin(buf_t *p, size_t data_size)
{
	if (!p)
		return;
	p->ops->clear(p->ops_data, data_size);
}

void buf_remove_end(buf_t *p, size_t data_size)
{
	if (!p)
		return;
	p->ops->set_size(p->ops_data, buf_size(p) - data_size);
}

void buf_dump(buf_t *p, const char *label)
{
	unsigned int i, n;

	if (!p || !label)
		return;

	n = 0;
	for (i = 0; i < buf_size(p); ++i) {
		if (n == 0)
			log_debug("%s%s(%04x):", log_debug_prefix, label, i);
		log_debug(" %02X", ((uint8_t *)buf_get(p))[i]);
		if (n >= 0xF || i == buf_size(p) - 1) {
			log_debug("\n");
			n = 0;
		} else
			n++;
	}
}

void buf_free(buf_t *p)
{
	if (!p)
		return;
	p->ops->delete(p->ops_data);
	free(p);
}
