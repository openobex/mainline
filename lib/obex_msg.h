/**
 * @file obex_msg.h
 *
 * Bridge between obex object and raw message buffer.
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

#include "obex_incl.h"
#include "defines.h"

bool obex_msg_prepare(obex_t *self, obex_object_t *object, bool allowfinal);
int obex_msg_getspace(obex_t *self, obex_object_t *object, unsigned int flags);

bool obex_msg_rx_status(const obex_t *self);
bool obex_msg_tx_status(const obex_t *self);
int obex_msg_get_opcode(const obex_t *self);
size_t obex_msg_get_len(const obex_t *self);

void obex_msg_pre_receive(obex_t *self);
int obex_msg_receive_filtered(obex_t *self, obex_object_t *object,
			      uint64_t filter, bool first_run);
int obex_msg_receive(obex_t *self, obex_object_t *object);
int obex_msg_post_receive(obex_t *self);
