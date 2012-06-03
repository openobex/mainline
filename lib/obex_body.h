/**
 * @file obex_body.h
 *
 * OBEX body reception releated functions.
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

#ifndef OBEX_BODY_H
#define OBEX_BODY_H

#include <stdlib.h>
#include "defines.h"

struct obex;
struct obex_hdr;
struct obex_object;

struct obex_body_ops {
	int (*rcv)(void *data, struct obex_hdr *hdr);
	const void * (*read)(void *data, size_t *size);
};

struct obex_body {
	struct obex_body_ops *ops;
	void *data;
};


int obex_body_rcv(struct obex_body *self, struct obex_hdr *hdr);
const void * obex_body_read(struct obex_body *self, size_t *size);

struct obex_body * obex_body_stream_create(struct obex *obex);
struct obex_body * obex_body_buffered_create(struct obex_object *object);

#endif /* OBEX_BODY_H */
