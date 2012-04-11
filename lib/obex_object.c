/**
	\file obex_object.c
	OBEX object related functions.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 1999, 2000 Pontus Fuchs, All Rights Reserved.
	Copyright (c) 1999, 2000 Dag Brattli, All Rights Reserved.

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "obex_main.h"
#include "obex_object.h"
#include "obex_header.h"
#include "obex_hdr.h"
#include "obex_connect.h"
#include "databuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Function obex_object_new ()
 *
 *    Create a new OBEX object
 *
 */
obex_object_t *obex_object_new(void)
{
	obex_object_t *object = calloc(1, sizeof(*object));

	if (object != NULL)
		obex_object_setrsp(object, OBEX_RSP_NOT_IMPLEMENTED,
						OBEX_RSP_NOT_IMPLEMENTED);

	return object;
}

/*
 * Function free_headerq(q)
 *
 *    Free all headers in a header queue.
 *
 */
static void free_headerq(slist_t *q)
{
	DEBUG(4, "\n");
	while (!slist_is_empty(q)) {
		struct obex_hdr *h = slist_get(q);
		q = slist_remove(q, h);
		obex_hdr_destroy(h);
	}
}

/*
 * Function obex_object_delete (object)
 *
 *    Delete OBEX object
 *
 */
int obex_object_delete(obex_object_t *object)
{
	DEBUG(4, "\n");
	obex_return_val_if_fail(object != NULL, -1);

	/* Free the headerqueues */
	free_headerq(object->tx_headerq);
	free_headerq(object->rx_headerq);

	/* Free tx and rx msgs */
	if (object->tx_nonhdr_data) {
		buf_free(object->tx_nonhdr_data);
		object->tx_nonhdr_data = NULL;
	}

	if (object->rx_nonhdr_data) {
		buf_free(object->rx_nonhdr_data);
		object->rx_nonhdr_data = NULL;
	}

	/* This one is already destroyed as part of the header queue */
	object->body = NULL;

	free(object);

	return 0;
}

/*
 * Function obex_object_setcmd ()
 *
 *    Set command of object
 *
 */
void obex_object_setcmd(obex_object_t *object, uint8_t cmd)
{
	DEBUG(4,"%02x\n", cmd);
	object->cmd = cmd;
	object->opcode = cmd;
	object->lastopcode = cmd | OBEX_FINAL;
}

/*
 * Function obex_object_setrsp ()
 *
 *    Set the response for an object
 *
 */
int obex_object_setrsp(obex_object_t *object, uint8_t rsp, uint8_t lastrsp)
{
	DEBUG(4,"\n");
	object->opcode = rsp;
	object->lastopcode = lastrsp;
	return 1;
}

int obex_object_getspace(obex_t *self, obex_object_t *object,
							unsigned int flags)
{
	size_t objlen = sizeof(struct obex_common_hdr);

	if (flags & OBEX_FL_FIT_ONE_PACKET) {
		struct obex_hdr_it *it = obex_hdr_it_create(object->tx_headerq);
		struct obex_hdr *hdr = obex_hdr_it_get_next(it);

		if (object->tx_nonhdr_data)
			objlen += buf_get_length(object->tx_nonhdr_data);
		while (hdr != NULL) {
			objlen += obex_hdr_get_size(hdr);
			hdr = obex_hdr_it_get_next(it);
		}
	}

	return self->mtu_tx - objlen;
}

/** Add a header to an objext TX queue
 * @deprecated
 */
int obex_object_addheader(obex_t *self, obex_object_t *object, uint8_t hi,
				obex_headerdata_t hv, uint32_t hv_size,
				unsigned int flags)
{
	int ret;
	struct obex_hdr *hdr;
	const void *value;
	size_t size;
	enum obex_hdr_id id = hi & OBEX_HDR_ID_MASK;
	enum obex_hdr_type type = hi & OBEX_HDR_TYPE_MASK;
	unsigned int flags2 = OBEX_FL_COPY;

	DEBUG(4, "\n");

	if (object == NULL)
		object = self->object;
	if (object == NULL)
		return -1;

	if (id == OBEX_HDR_ID_BODY_END)
		id = OBEX_HDR_ID_BODY;

	if (id == OBEX_HDR_ID_BODY && (flags & OBEX_FL_STREAM_DATAEND)) {
		/* End of stream marker */
		obex_hdr_stream_set_data(object->body, hv.bs, hv_size);
		obex_hdr_stream_finish(object->body);
		return 1;

	} else if (id == OBEX_HDR_ID_BODY && (flags & OBEX_FL_STREAM_DATA)) {
		/* Stream data */
		obex_hdr_stream_set_data(object->body, hv.bs, hv_size);
		return 1;

	} else if (id == OBEX_HDR_ID_BODY && (flags & OBEX_FL_STREAM_START)) {
		/* Is this a stream? */
		DEBUG(3, "Adding stream\n");
		if (object->body)
			return -1;
		hdr = obex_hdr_stream_create(self);
		object->body = hdr;
		object->tx_headerq = slist_append(object->tx_headerq, hdr);
		return 1;
	}

	switch (type) {
	case OBEX_HDR_TYPE_UINT32:
		DEBUG(2, "4BQ header %d\n", hv.bq4);
		value = &hv.bq4;
		size = 4;
		break;

	case OBEX_HDR_TYPE_UINT8:
		DEBUG(2, "1BQ header %d\n", hv.bq1);
		value = &hv.bq1;
		size = 1;
		break;

	case OBEX_HDR_TYPE_BYTES:
		DEBUG(2, "BS  header size %d\n", hv_size);
		value = hv.bs;
		size = hv_size;
		break;

	case OBEX_HDR_TYPE_UNICODE:
		DEBUG(2, "Unicode header size %d\n", hv_size);
		value = hv.bs;
		size = hv_size;
		break;

	default:
		return -1;
	}

	if (hi == OBEX_HDR_EMPTY) {
		DEBUG(2, "Empty header\n");
		id = OBEX_HDR_ID_INVALID;
		type = OBEX_HDR_TYPE_INVALID;
		value = NULL;
		size = 0;
		flags2 = 0;
	}

	flags2 |= (flags & OBEX_FL_SUSPEND);
	hdr = obex_hdr_create(id, type, value, size, flags2);
	if (!hdr)
		return -1;

	ret = (int)obex_hdr_get_size(hdr);
	/* Check if you can send this header without violating MTU or
	 * OBEX_FIT_ONE_PACKET */
	if (!obex_hdr_is_splittable(hdr) && (flags & OBEX_FL_FIT_ONE_PACKET)) {
		int maxlen = obex_object_getspace(self, object, flags);
		if (maxlen < ret) {
			DEBUG(0, "Header to big\n");
			obex_hdr_destroy(hdr);
			return -1;
		}
	}

	object->tx_headerq = slist_append(object->tx_headerq, hdr);

	return ret;
}

uint8_t obex_object_getcmd(const obex_t *self, const obex_object_t *object)
{
	if (self->mode == OBEX_MODE_SERVER)
		return object->cmd;
	else
		return (object->opcode & ~OBEX_FINAL);
}

static unsigned int obex_object_send_srm_flags (uint8_t flag)
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

static int obex_object_get_real_opcode(obex_object_t *object,
				       int allowfinalcmd, int forcefinalbit)
{
	int real_opcode = object->opcode;

	/* Decide which command to use, and if to use final-bit */
	DEBUG(4, "allowfinalcmd: %d forcefinalbit:%d\n", allowfinalcmd,
	      forcefinalbit);

	/* Have more headers (or body) to send? */
	if (!slist_is_empty(object->tx_headerq)) {
		/* In server, final bit is always set.
		 * In client, final bit is set only when we finish sending. */
		if (forcefinalbit)
			real_opcode |= OBEX_FINAL;

	} else {
		/* Have no more headers to send */
		if (allowfinalcmd != FALSE) {
			/* Allowed to send final command (== end data we are
			 * sending) */
			real_opcode = object->lastopcode;
		}

		real_opcode |= OBEX_FINAL;
	}

	return real_opcode;
}

/*
 * Function obex_object_prepare_send()
 *
 *    Prepare to send away all headers attached to an object.
 *    Returns:
 *       1 when a message was created
 *       0 when no message was created
 *      -1 on error
 */
int obex_object_prepare_send(obex_t *self, obex_object_t *object,
			     int allowfinalcmd, int forcefinalbit)
{
	buf_t *txmsg;
	int real_opcode;
	uint16_t tx_left;
	unsigned int srm_flags = 0;

	/* Don't do anything if object is suspended */
	if (object->suspend)
		return 0;

	/* Calc how many bytes of headers we can fit in this package */
	tx_left = self->mtu_tx - sizeof(struct obex_common_hdr);
#ifdef HAVE_IRDA
	if (self->trans.type == OBEX_TRANS_IRDA &&
			self->trans.mtu > 0 && self->trans.mtu < self->mtu_tx)
		tx_left -= self->mtu_tx % self->trans.mtu;
#endif /*HAVE_IRDA*/

	/* Reuse transmit buffer */
	txmsg = buf_reuse(self->tx_msg);

	/* Add nonheader-data first if any (SETPATH, CONNECT)*/
	if (object->tx_nonhdr_data) {
		DEBUG(4, "Adding %lu bytes of non-headerdata\n",
		      (unsigned long)buf_size(object->tx_nonhdr_data));
		buf_append(txmsg, buf_get(object->tx_nonhdr_data),
			   buf_size(object->tx_nonhdr_data));

		buf_delete(object->tx_nonhdr_data);
		object->tx_nonhdr_data = NULL;
	}

	DEBUG(4, "4\n");

	/* Take headers from the tx queue and try to stuff as
	 * many as possible into the tx-msg */
	while (!slist_is_empty(object->tx_headerq) && !object->suspend
	       && tx_left > 0)
	{
		struct obex_hdr *h = slist_get(object->tx_headerq);

		if (obex_hdr_get_id(h) != OBEX_HDR_ID_INVALID) {
			size_t ret = obex_hdr_append(h, txmsg, tx_left);
			if (ret == 0)
				break;
			tx_left -= ret;
		}

		if (obex_hdr_get_id(h) == OBEX_HDR_ID_SRM_FLAGS) {
			const uint8_t *hdrdata = obex_hdr_get_data_ptr(h);
			srm_flags = obex_object_send_srm_flags(hdrdata[0]);
		}


		/* Remove from TX queue */
		if (obex_hdr_is_finished(h)) {
			struct databuffer_list *q = object->tx_headerq;
			if (h->flags & OBEX_FL_SUSPEND)
				object->suspend = 1;

			object->tx_headerq = slist_remove(q, h);
			obex_hdr_destroy(h);
		}
	}

	real_opcode = obex_object_get_real_opcode(self->object, allowfinalcmd,
						  forcefinalbit);
	DEBUG(4, "Generating packet with opcode %d\n", real_opcode);
	obex_data_request_prepare(self, self->tx_msg, real_opcode);

	self->srm_flags &= ~OBEX_SRM_FLAG_WAIT_REMOTE;
	self->srm_flags |= srm_flags;

	return 1;
}

int obex_object_finished(obex_t *self, obex_object_t *object, int allowfinalcmd)
{
	int finished = 0;

	if (object->suspend)
		return 0;

	if (slist_is_empty(object->tx_headerq))
		finished = !!allowfinalcmd;

	return finished;
}

int obex_object_send_transmit(obex_t *self, obex_object_t *object)
{
	if (!buf_empty(self->tx_msg)) {
		int ret = obex_data_request(self, self->tx_msg);
		if (ret < 0) {
			DEBUG(4, "Send error\n");
			return -1;
		}
	}

	return buf_empty(self->tx_msg);
}

/*
 * Function obex_object_getnextheader()
 *
 * Return the next header in the rx-queue
 *
 */
int obex_object_getnextheader(obex_t *self, obex_object_t *object, uint8_t *hi,
				obex_headerdata_t *hv, uint32_t *hv_size)
{
	const uint8_t *bq1;
	const uint32_t *bq4;
	struct obex_hdr *h;

	DEBUG(4, "\n");

	/* No more headers */
	if (slist_is_empty(object->rx_headerq))
		return 0;

	if (!object->it)
		object->it = obex_hdr_it_create(object->rx_headerq);

	if (!object->it)
		return -1;

	h = obex_hdr_it_get_next(object->it);
	if (h == NULL)
	    return 0;

	*hi = obex_hdr_get_id(h) | obex_hdr_get_type(h);
	*hv_size= (uint32_t)obex_hdr_get_data_size(h);

	switch (obex_hdr_get_type(h)) {
	case OBEX_HDR_TYPE_BYTES:
	case OBEX_HDR_TYPE_UNICODE:
		hv->bs = obex_hdr_get_data_ptr(h);
		break;

	case OBEX_HDR_TYPE_UINT32:
		bq4 = obex_hdr_get_data_ptr(h);
		hv->bq4 = ntohl(*bq4);
		break;

	case OBEX_HDR_TYPE_UINT8:
		bq1 = obex_hdr_get_data_ptr(h);
		hv->bq1 = bq1[0];
		break;

	default:
		return -1;
	}

	return 1;
}

/*
 * Function obex_object_reparseheader()
 *
 * Allow the user to re-parse the headers in the rx-queue
 *
 */
int obex_object_reparseheaders(obex_t *self, obex_object_t *object)
{

	DEBUG(4, "\n");

	/* Check that there is no more active headers */
	if (object->it) {
		obex_hdr_it_destroy(object->it);
		object->it = NULL;
	}

	/* Success */
	return 1;
}

/*
 * Function obex_object_receive_stream()
 *
 *    Handle receiving of body-stream
 *
 */
static void obex_object_receive_stream(obex_t *self, struct obex_hdr *hdr)
{
	obex_object_t *object = self->object;
	uint8_t cmd = obex_object_getcmd(self, object);
	enum obex_hdr_id id = obex_hdr_get_id(hdr);
	enum obex_hdr_type type = obex_hdr_get_type(hdr);
	size_t len = obex_hdr_get_data_size(hdr);

	DEBUG(4, "\n");

	/* Spare the app this empty nonlast body-hdr */
	if (obex_hdr_get_id(hdr) == OBEX_HDR_ID_BODY &&
	    obex_hdr_get_data_size(hdr) == 0)
		return;

	/* Notify app that data has arrived */
	object->body = hdr;
	obex_deliver_event(self, OBEX_EV_STREAMAVAIL, cmd, 0, FALSE);
	object->body = NULL;

	/* If send send EOS to app */
	if (id == OBEX_HDR_ID_BODY_END && len != 0) {
		object->body = obex_hdr_ptr_create(id, type, NULL, 0);
		obex_deliver_event(self, OBEX_EV_STREAMAVAIL, cmd, 0, FALSE);
		obex_hdr_destroy(object->body);
		object->body = NULL;
	}
}

/*
 * Function obex_object_receive_buffered()
 *
 *    Handle receiving of buffered body
 *
 */
static int obex_object_receive_buffered(obex_t *self, struct obex_hdr *hdr)
{
	obex_object_t *object = self->object;
	const void *data = obex_hdr_get_data_ptr(hdr);
	size_t len = obex_hdr_get_data_size(hdr);

	DEBUG(4, "This is a body-header.\n");

	if (!object->body) {
		int alloclen = obex_hdr_get_data_size(hdr);

		if (object->hinted_body_len)
			alloclen = object->hinted_body_len;

		DEBUG(4, "Allocating new body-buffer. Len=%d\n", alloclen);
		object->body = obex_hdr_membuf_create(OBEX_HDR_ID_BODY,
						      OBEX_HDR_TYPE_BYTES,
						      data, len);
		if (!object->body)
			return -1;

	} else {
		struct databuffer *buf;
		buf = obex_hdr_membuf_get_databuffer(object->body);;
		if (buf_append(buf, data, len) < 0)
			return -1;
	}

	return 1;
}

/*
 * Function obex_object_receive()
 *
 *    Process non-header data from an incoming message.
 */
int obex_object_receive_nonhdr_data(obex_t *self, buf_t *msg)
{
	obex_object_t *object = self->object;
	uint8_t *msgdata = buf_get(msg) + sizeof(struct obex_common_hdr);

	DEBUG(4, "\n");

	if (object->headeroffset == 0)
		return 0;

	/* Copy any non-header data (like in CONNECT and SETPATH) */
	object->rx_nonhdr_data = buf_new(object->headeroffset);
	if (!object->rx_nonhdr_data)
		return -1;
	buf_insert_end(object->rx_nonhdr_data, msgdata,	object->headeroffset);
	DEBUG(4, "Command has %lu bytes non-headerdata\n",
	      (unsigned long)buf_size(object->rx_nonhdr_data));

	return 0;
}

static int obex_object_receive_body(obex_t *self, struct obex_hdr *hdr)
{
	obex_object_t *object = self->object;
	enum obex_hdr_id id = obex_hdr_get_id(hdr);

	DEBUG(4, "\n");

	if (object->s_srv) {
		if (id == OBEX_HDR_ID_BODY || id == OBEX_HDR_ID_BODY_END) {
			/* The body-header need special treatment */
			obex_object_receive_stream(self, hdr);
			/* We have already handled this data! */
			return 1;
		}

		return 0;
	}

	if (id == OBEX_HDR_ID_BODY || id == OBEX_HDR_ID_BODY_END) {
		if (obex_object_receive_buffered(self, hdr) < 0)
			return -1;

		if (id == OBEX_HDR_ID_BODY) {
			DEBUG(4, "Normal body fragment...\n");
			/* We have already handled this data! */
			return 1;
		}
	} else if (obex_hdr_get_id(hdr) == OBEX_HDR_ID_LENGTH
		   && !object->body)
	{
		/* The length MAY be useful when receiving body. */
		uint32_t value;
		memcpy(&value, obex_hdr_get_data_ptr(hdr), sizeof(value));
		object->hinted_body_len = ntohl(value);
		DEBUG(4, "Hinted body len is %d\n", object->hinted_body_len);
	}

	return 0;
}

static int obex_object_rcv_one_header(obex_t *self, struct obex_hdr *hdr)
{
	obex_object_t *object = self->object;
	enum obex_hdr_id id = obex_hdr_get_id(hdr);

	DEBUG(4, "\n");

	if (id == OBEX_HDR_ID_BODY_END) {
		hdr = object->body;
		object->body = NULL;

	} else {
		enum obex_hdr_type type = obex_hdr_get_type(hdr);
		const void *data = obex_hdr_get_data_ptr(hdr);
		size_t len = obex_hdr_get_data_size(hdr);

		hdr = obex_hdr_membuf_create(id, type, data, len);
		if (hdr == NULL)
			return -1;
	}

	/* Add element to rx-list */
	object->rx_headerq = slist_append(object->rx_headerq, hdr);

	return 0;
}

/*
 * Function obex_object_receive()
 *
 *    Process all data from an incoming message, including non-header data
 *    and headers, and remove them from the message buffer.
 */
int obex_object_receive(obex_t *self, buf_t *msg)
{
	int hlen;

	DEBUG(4, "\n");

	if (obex_object_receive_nonhdr_data(self, msg) < 0)
		return -1;

	hlen = obex_object_receive_headers(self, msg, 0);
	if (hlen < 0)
		return hlen;

	return 0;
}

static unsigned int obex_object_rcv_srm_flags(uint8_t flag)
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

/*
 * Function obex_object_receive_headers()
 *
 *    Add any incoming headers to headerqueue but does not remove them from
 *    the message buffer.
 *    Returns the total number of bytes of the added headers or -1;
 */
int obex_object_receive_headers(obex_t *self, buf_t *msg, uint64_t filter)
{
	struct obex_common_hdr *hdr = buf_get(msg);
	uint16_t offset = sizeof(*hdr) + self->object->headeroffset;
	int consumed = 0;
	const uint64_t body_filter = (1 << OBEX_HDR_ID_BODY |
						1 << OBEX_HDR_ID_BODY_END);
	uint16_t msglen = ntohs(hdr->len);

	DEBUG(4, "\n");

	while (offset < msglen) {
		struct obex_hdr *hdr = obex_hdr_ptr_parse(msg, offset);
		size_t hlen = obex_hdr_get_size(hdr);
		int err = 0;
		uint64_t header_bit;

		DEBUG(4, "Header: type=%02x, id=%02x, size=%ld\n",
		      obex_hdr_get_type(hdr), obex_hdr_get_id(hdr),
		      (unsigned long)hlen);

		/* Make sure that the msg is big enough for header */
		if (hlen > (unsigned int)(msglen - offset)) {
			DEBUG(1, "Header too big.\n");
			obex_hdr_destroy(hdr);
			hdr = NULL;
			err = -1;
		}

		/* Push the body header data either to the application
		 * or to an internal receive buffer.
		 * This also uses the length header in the latter case
		 * but the filter is not checked for it as it's only an
		 * optimisation (currently only works if BODY header is
		 * part of first message).
		 */
		if (hdr && (filter & body_filter) == 0) {
			int used = obex_object_receive_body(self, hdr);
			if (used != 0) {
				obex_hdr_destroy(hdr);
				hdr = NULL;
			}
			if (used < 0)
				err = -1;
			if (used > 0)
				consumed += hlen;
		}

		/* This adds all headers to an internal list of headers that
		 * the application can walk easily. Body headers are only added
		 * if not in streaming mode and only after end-of-body was
		 * received.
		 */
		if (hdr) {
			enum obex_hdr_id id = obex_hdr_get_id(hdr);
			const uint8_t *data = obex_hdr_get_data_ptr(hdr);
			header_bit = (uint64_t) 1 << id;
			if (!(filter & header_bit)) {
				if (id == OBEX_HDR_ID_SRM_FLAGS)
					self->srm_flags |=
					     obex_object_rcv_srm_flags(data[0]);

				err = obex_object_rcv_one_header(self, hdr);
				consumed += hlen;
			}
		}

		if (err)
			return err;

		offset += hlen;
	}

	return consumed;
}

/*
 * Function obex_object_readstream()
 *
 *    App wants to read stream fragment.
 *
 */
int obex_object_readstream(obex_t *self, obex_object_t *object,
							const uint8_t **buf)
{
	DEBUG(4, "\n");
	/* Enable streaming */
	if (buf == NULL) {
		DEBUG(4, "Streaming is enabled!\n");
		object->s_srv = TRUE;
		return 0;
	}

	*buf = obex_hdr_get_data_ptr(object->body);

	return obex_hdr_get_data_size(object->body);
}

int obex_object_suspend(obex_object_t *object)
{
	if (object->suspend == 1)
		return -1;

	object->suspend = 1;
	return 0;
}

int obex_object_resume(obex_object_t *object)
{
	if (object->suspend == 0)
		return -1;

	object->suspend = 0;
	return 0;
}
