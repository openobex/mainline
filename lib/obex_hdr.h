/**
 * @file obex_hdr.h
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

#include <databuffer.h>
#include <obex_incl.h>
#include <defines.h>

struct obex_hdr {
	unsigned int flags;
	size_t offset;
	struct obex_hdr_ops *ops;
	void *data;
};

/** Copy the data to the header instance */
#define OBEX_FL_COPY		(1 <<  0)

struct obex_hdr * obex_hdr_create(enum obex_hdr_id id, enum obex_hdr_type type,
				  const void *data, size_t size,
				  unsigned int flags);


struct obex_hdr * obex_hdr_membuf_create(enum obex_hdr_id id,
					 enum obex_hdr_type type,
					 const void *data, size_t size);
struct databuffer * obex_hdr_membuf_get_databuffer(struct obex_hdr *hdr);


struct obex_hdr * obex_hdr_ptr_create(enum obex_hdr_id id,
				      enum obex_hdr_type type,
				      const void *data, size_t size);
struct obex_hdr * obex_hdr_ptr_parse(const void *msgdata, size_t size);


struct obex_hdr * obex_hdr_stream_create(struct obex *obex,
					 struct obex_hdr *data);
void obex_hdr_stream_finish(struct obex_hdr *hdr);


struct obex_hdr_ops {
	void (*destroy)(void *self);
	enum obex_hdr_id (*get_id)(void *self);
	enum obex_hdr_type (*get_type)(void *self);
	size_t (*get_data_size)(void *self);
	const void * (*get_data_ptr)(void *self);
	bool (*set_data)(void *self, const void *data, size_t size);
	size_t (*append_data)(void *self, struct databuffer *buf, size_t size);
	bool (*is_finished)(void *self);
};

struct obex_hdr * obex_hdr_new(struct obex_hdr_ops *ops, void *data);
void obex_hdr_destroy(struct obex_hdr *hdr);
enum obex_hdr_id obex_hdr_get_id(struct obex_hdr *hdr);
enum obex_hdr_type obex_hdr_get_type(struct obex_hdr *hdr);
size_t obex_hdr_get_size(struct obex_hdr *hdr);
size_t obex_hdr_get_data_size(struct obex_hdr *hdr);
const void * obex_hdr_get_data_ptr(struct obex_hdr *hdr);
bool obex_hdr_set_data(struct obex_hdr *hdr, const void *data, size_t size);
size_t obex_hdr_append(struct obex_hdr *hdr, struct databuffer *buf,
		       size_t max_size);
bool obex_hdr_is_splittable(struct obex_hdr *hdr);
bool obex_hdr_is_finished(struct obex_hdr *hdr);

struct obex_hdr_it {
	struct databuffer_list *list;
	int is_valid;
};

void obex_hdr_it_init_from(struct obex_hdr_it *it,
			   const struct obex_hdr_it *from);
struct obex_hdr_it * obex_hdr_it_create(struct databuffer_list *list);
void obex_hdr_it_destroy(struct obex_hdr_it *it);
struct obex_hdr * obex_hdr_it_get(const struct obex_hdr_it *it);
void obex_hdr_it_next(struct obex_hdr_it *it);
int obex_hdr_it_equals(const struct obex_hdr_it *a, const struct obex_hdr_it *b);
