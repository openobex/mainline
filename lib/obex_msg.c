/**
 * @file obex_msg.c
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

#include "obex_msg.h"

#include "obex_main.h"
#include "obex_object.h"
#include "obex_hdr.h"
#include "defines.h"

static unsigned int obex_srm_tx_flags_decode (uint8_t flag)
{
	switch (flag) {
	case 0x00:
		return OBEX_SRM_FLAG_WAIT_LOCAL;

	case 0x01:
		return OBEX_SRM_FLAG_WAIT_REMOTE;

	case 0x02:
		return (OBEX_SRM_FLAG_WAIT_LOCAL | OBEX_SRM_FLAG_WAIT_REMOTE);

	default:
		return 0;
	}
}

static unsigned int obex_srm_rx_flags_decode (uint8_t flag)
{
	switch (flag) {
	case 0x00:
		return OBEX_SRM_FLAG_WAIT_REMOTE;

	case 0x01:
		return OBEX_SRM_FLAG_WAIT_LOCAL;

	case 0x02:
		return (OBEX_SRM_FLAG_WAIT_LOCAL | OBEX_SRM_FLAG_WAIT_REMOTE);

	default:
		return 0;
	}
}

static bool obex_msg_post_prepare(obex_t *self, obex_object_t *object,
				  const struct obex_hdr_it *from,
				  const struct obex_hdr_it *to)
{
	struct obex_hdr_it it;
	struct obex_hdr *hdr;

	obex_hdr_it_init_from(&it, from);
	hdr = obex_hdr_it_get(&it);

	/* loop over all headers in that are non-NULL and finished... */
	while (hdr != NULL && obex_hdr_is_finished(hdr)) {
		if (self->rsp_mode == OBEX_RSP_MODE_SINGLE &&
		    obex_hdr_get_id(hdr) == OBEX_HDR_ID_SRM_FLAGS)
		{
			const uint8_t *data = obex_hdr_get_data_ptr(hdr);

			self->srm_flags &= ~OBEX_SRM_FLAG_WAIT_REMOTE;
			self->srm_flags |= obex_srm_tx_flags_decode(data[0]);
		}

		/* ...but only in the range [from..to]. The last entry
		 * must be included if it is finished. */
		if (obex_hdr_it_equals(&it, to))
			break;

		obex_hdr_it_next(&it);
		hdr = obex_hdr_it_get(&it);
	}

	return true;
}

bool obex_msg_prepare(obex_t *self, obex_object_t *object, bool allowfinal)
{
	buf_t *txmsg = self->tx_msg;
	uint16_t tx_left = self->mtu_tx - sizeof(struct obex_common_hdr);
	int real_opcode;
	struct obex_hdr_it it;

	obex_hdr_it_init_from(&it, object->tx_it);

	if (!obex_data_request_init(self))
		return false;

	if (!obex_object_append_data(object, txmsg, tx_left))
		return false;

	real_opcode = obex_object_get_opcode(self->object, allowfinal,
					     self->mode);
	DEBUG(4, "Generating packet with opcode %d\n", real_opcode);
	obex_data_request_prepare(self, real_opcode);

	return obex_msg_post_prepare(self, object, &it, object->tx_it);
}

int obex_msg_getspace(obex_t *self, obex_object_t *object, unsigned int flags)
{
	size_t objlen = sizeof(struct obex_common_hdr);

	if (flags & OBEX_FL_FIT_ONE_PACKET)
		objlen += obex_object_get_size(object);

	return self->mtu_tx - objlen;
}

/** Check if the RX message buffer contains at least one full message. */
bool obex_msg_rx_status(const obex_t *self)
{
	buf_t *msg = self->rx_msg;
	obex_common_hdr_t *hdr = buf_get(msg);

	return (buf_get_length(msg) >= sizeof(*hdr) &&
		buf_get_length(msg) >= ntohs(hdr->len));
}

/** Check if the TX message buffer was sent completely */
bool obex_msg_tx_status(const obex_t *self)
{
	buf_t *msg = self->tx_msg;

	return (buf_get_length(msg) == 0);
}

int obex_msg_get_opcode(const obex_t *self)
{
	buf_t *msg = self->rx_msg;
	obex_common_hdr_t *hdr = buf_get(msg);

	if (!obex_msg_rx_status(self))
		return -1;

	return hdr->opcode;
}

size_t obex_msg_get_len(const obex_t *self)
{
	buf_t *msg = self->rx_msg;
	obex_common_hdr_t *hdr;

	if (!obex_msg_rx_status(self))
		return -1;

	hdr = buf_get(msg);
	return (size_t)ntohs(hdr->len);
}

void obex_msg_pre_receive(obex_t *self)
{
	if (self->rsp_mode == OBEX_RSP_MODE_SINGLE)
		self->srm_flags &= ~OBEX_SRM_FLAG_WAIT_LOCAL;
}

int obex_msg_post_receive(obex_t *self)
{
	obex_object_t *object = self->object;
	struct obex_hdr *hdr;

	if (!object->rx_it)
		return 0;

	/* loop over all new headers */
	hdr = obex_hdr_it_get(object->rx_it);
	while (hdr != NULL) {
		if (self->rsp_mode == OBEX_RSP_MODE_SINGLE &&
		    obex_hdr_get_id(hdr) == OBEX_HDR_ID_SRM_FLAGS)
		{
			const uint8_t *data = obex_hdr_get_data_ptr(hdr);

			self->srm_flags |= obex_srm_rx_flags_decode(data[0]);
		}

		obex_hdr_it_next(object->rx_it);
		hdr = obex_hdr_it_get(object->rx_it);
	}

	return 0;
}

int obex_msg_receive_filtered(obex_t *self, obex_object_t *object,
			      uint64_t filter, bool first_run)
{
	buf_t *msg = self->rx_msg;
	size_t len;
	const uint8_t *data;
	int hlen;

	DEBUG(4, "\n");

	if (!obex_msg_rx_status(self))
		return 0;

	data = buf_get(msg);
	len = sizeof(struct obex_common_hdr);
	if (first_run)
		obex_msg_pre_receive(self);

	data += len;
	len = obex_msg_get_len(self) - len;
	if (first_run && len > 0)
		if (obex_object_receive_nonhdr_data(object, data, len) < 0)
			return -1;

	data += object->headeroffset;
	len -= object->headeroffset;
	if (len > 0) {
		hlen = obex_object_receive_headers(object, data, len, filter);
		if (hlen < 0)
			return hlen;
	}	

	return obex_msg_post_receive(self);
}

int obex_msg_receive(obex_t *self, obex_object_t *object)
{
	return obex_msg_receive_filtered(self, object, 0, TRUE);
}
