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
#include "obex_hdr.h"
#include "obex_body.h"
#include "obex_msg.h"
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
static void free_headerq(slist_t *q, const struct obex_hdr *no_delete)
{
	DEBUG(4, "\n");
	while (!slist_is_empty(q)) {
		struct obex_hdr *h = slist_get(q);
		q = slist_remove(q, h);
		if (h != no_delete)
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
	obex_hdr_it_destroy(object->tx_it);
	free_headerq(object->tx_headerq, object->body);
	/* Free tx non-header data */
	if (object->tx_nonhdr_data) {
		buf_delete(object->tx_nonhdr_data);
		object->tx_nonhdr_data = NULL;
	}


	/* Free the headerqueues */
	obex_hdr_it_destroy(object->it);
	obex_hdr_it_destroy(object->rx_it);
	free_headerq(object->rx_headerq, object->body);
	/* Free rx non-header data */
	if (object->rx_nonhdr_data) {
		buf_delete(object->rx_nonhdr_data);
		object->rx_nonhdr_data = NULL;
	}

	if (object->body != NULL) {
		obex_hdr_destroy(object->body);
		object->body = NULL;
	}

	free(object);

	return 0;
}

/*
 * Function obex_object_setcmd ()
 *
 *    Set command of object
 *
 */
void obex_object_setcmd(obex_object_t *object, enum obex_cmd cmd)
{
	DEBUG(4,"%02x\n", cmd);
	object->cmd = cmd & ~OBEX_FINAL;
}

/*
 * Function obex_object_setrsp ()
 *
 *    Set the response for an object
 *
 */
int obex_object_setrsp(obex_object_t *object, enum obex_rsp rsp,
		       enum obex_rsp lastrsp)
{
	DEBUG(4,"\n");
	object->rsp = rsp;
	object->lastrsp = lastrsp;
	return 1;
}

size_t obex_object_get_size(obex_object_t *object)
{
	size_t objlen = 0;

	if (object->tx_nonhdr_data)
		objlen += buf_get_length(object->tx_nonhdr_data);

	if (object->tx_it) {
		struct obex_hdr_it it;
		struct obex_hdr *hdr;

		obex_hdr_it_init_from(&it, object->tx_it);
		hdr = obex_hdr_it_get(&it);

		while (hdr != NULL) {
			objlen += obex_hdr_get_size(hdr);

			obex_hdr_it_next(&it);
			hdr = obex_hdr_it_get(&it);
		}
	}

	return objlen;
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
	uint32_t bq4;
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
	{
		id = OBEX_HDR_ID_BODY;
		if (object->body)
			flags &= OBEX_FL_STREAM_DATAEND;
	}

	if (id == OBEX_HDR_ID_BODY) {
		if (flags & OBEX_FL_STREAM_DATAEND) {
			/* End of stream marker */
			if (object->body == NULL) {
				/* A body with a single chunk. */
				hdr = obex_hdr_ptr_create(OBEX_HDR_ID_BODY_END, OBEX_HDR_TYPE_BYTES, hv.bs, hv_size);
				hdr = obex_hdr_stream_create(self, hdr);
				obex_hdr_stream_finish(hdr);
			} else {
				/* Set the remaining data on the BODY header... */
				obex_hdr_set_data(object->body, hv.bs, hv_size);
				obex_hdr_stream_finish(object->body);
				object->body = NULL;
				/* ...and add the BODY_END header to the end */
				hdr = obex_hdr_ptr_create(OBEX_HDR_ID_BODY_END,
							  OBEX_HDR_TYPE_BYTES, NULL, 0);
			}
			ret = 1;
			goto out;

		} else if (flags & OBEX_FL_STREAM_CONTINUE) {
			/* Continue stream after all other headers */
			if (object->body == NULL)
				return -1;
			obex_hdr_stream_finish(object->body);
			hdr = obex_hdr_ptr_create(id, OBEX_HDR_TYPE_BYTES, hv.bs, hv_size);
			hdr = obex_hdr_stream_create(self, hdr);
			object->body = hdr;
			ret = 1;
			goto out;

		} else if (flags & OBEX_FL_STREAM_DATA) {
			/* Stream data */
			if (object->body == NULL)
				return -1;
			obex_hdr_set_data(object->body, hv.bs, hv_size);
			return 1;

		} else if (flags & OBEX_FL_STREAM_START) {
			/* Is this a stream? */
			DEBUG(3, "Adding stream\n");
			if (object->body)
				return -1;
			hdr = obex_hdr_ptr_create(id, OBEX_HDR_TYPE_BYTES, hv.bs, hv_size);
			hdr = obex_hdr_stream_create(self, hdr);
			object->body = hdr;
			ret = 1;
			goto out;
		}
	}

	switch (type) {
	case OBEX_HDR_TYPE_UINT32:
		DEBUG(2, "4BQ header %d\n", hv.bq4);
		bq4 = htonl(hv.bq4);
		value = &bq4;
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
		int maxlen = obex_msg_getspace(self, object, flags);
		if (maxlen < ret) {
			DEBUG(0, "Header to big\n");
			obex_hdr_destroy(hdr);
			return -1;
		}
	}

out:
	object->tx_headerq = slist_append(object->tx_headerq, hdr);

	if (object->tx_it == NULL)
		object->tx_it = obex_hdr_it_create(object->tx_headerq);

	return ret;
}

enum obex_cmd obex_object_getcmd(const obex_object_t *object)
{
	return object->cmd;
}

int obex_object_get_opcode(obex_object_t *object, bool allowfinal,
			   enum obex_mode mode)
{
	int opcode = -1;

	/* Decide which command to use, and if to use final-bit */
	DEBUG(4, "allowfinalcmd: %d mode:%d\n", allowfinal, mode);

	switch (mode) {
	case OBEX_MODE_SERVER:
		if (obex_object_finished(object, allowfinal))
			opcode = object->lastrsp;
		else
			opcode = object->rsp;
		opcode |= OBEX_FINAL;
		break;

	case OBEX_MODE_CLIENT:
		opcode = object->cmd;
		/* Have more headers (or body) to send? */
		if (obex_object_finished(object, allowfinal))
			opcode |= OBEX_FINAL;
		break;

	default:
		break;
	}

	return opcode;
}

bool obex_object_append_data(obex_object_t *object, buf_t *txmsg, size_t tx_left)
{
	/* Don't do anything if object is suspended */
	if (object->suspended)
		return false;

	/* Add nonheader-data first if any (SETPATH, CONNECT)*/
	if (object->tx_nonhdr_data) {
		DEBUG(4, "Adding %lu bytes of non-headerdata\n",
		      (unsigned long)buf_get_length(object->tx_nonhdr_data));
		buf_append(txmsg, buf_get(object->tx_nonhdr_data),
			   buf_get_length(object->tx_nonhdr_data));

		buf_delete(object->tx_nonhdr_data);
		object->tx_nonhdr_data = NULL;
	}

	DEBUG(4, "4\n");

	/* Take headers from the tx queue and try to stuff as
	 * many as possible into the tx-msg */
	if (object->tx_it) {
		bool has_body_header = false;
		struct obex_hdr *h = obex_hdr_it_get(object->tx_it);
		while (h != NULL && !object->suspended && tx_left > 0) {
			enum obex_hdr_id id = obex_hdr_get_id(h);

			if (id == OBEX_HDR_ID_BODY || id == OBEX_HDR_ID_BODY_END)
			{
				if (has_body_header)
					break;
				has_body_header = true;
			}

			if (id != OBEX_HDR_ID_INVALID) {
				size_t ret = obex_hdr_append(h, txmsg, tx_left);
				if (ret == 0)
					break;
				tx_left -= ret;
			}
		
			if (obex_hdr_is_finished(h)) {
				if (h->flags & OBEX_FL_SUSPEND)
					object->suspended = true;

				obex_hdr_it_next(object->tx_it);
				h = obex_hdr_it_get(object->tx_it);
			}
		}
	}

	return true;
}

int obex_object_finished(obex_object_t *object, bool allowfinal)
{
	return (!object->suspended &&
		(!object->tx_it || !obex_hdr_it_get(object->tx_it)) &&
		allowfinal);
}

/*
 * Function obex_object_getnextheader()
 *
 * Return the next header in the rx-queue
 *
 */
int obex_object_getnextheader(obex_object_t *object, uint8_t *hi,
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

	h = obex_hdr_it_get(object->it);
	if (h == NULL)
	    return 0;

	obex_hdr_it_next(object->it);

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
int obex_object_reparseheaders(obex_object_t *object)
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
 * Function obex_object_receive()
 *
 *    Process non-header data from an incoming message.
 */
int obex_object_receive_nonhdr_data(obex_object_t *object, const void *msgdata,
				    size_t rx_left)
{
	DEBUG(4, "\n");

	if (object->headeroffset == 0)
		return 0;

	if (object->headeroffset > rx_left)
		return -1;

	/* Copy any non-header data (like in CONNECT and SETPATH) */
	object->rx_nonhdr_data = membuf_create(object->headeroffset);
	if (!object->rx_nonhdr_data)
		return -1;
	buf_append(object->rx_nonhdr_data, msgdata, object->headeroffset);
	DEBUG(4, "Command has %lu bytes non-headerdata\n",
	      (unsigned long)buf_get_length(object->rx_nonhdr_data));

	return 0;
}

static
int obex_object_receive_body(obex_object_t *object, struct obex_hdr *hdr)
{
	enum obex_hdr_id id = obex_hdr_get_id(hdr);

	DEBUG(4, "\n");

	if (id == OBEX_HDR_ID_BODY || id == OBEX_HDR_ID_BODY_END) {
		if (!object->body_rcv)
			object->body_rcv = obex_body_buffered_create(object);
		if (!object->body_rcv)
			return -1;

		if (obex_body_rcv(object->body_rcv, hdr) < 0)
			return -1;

		if (id == OBEX_HDR_ID_BODY) {
			DEBUG(4, "Normal body fragment...\n");
			/* We have already handled this data! */
		}

		return 1;

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

static
int obex_object_rcv_one_header(obex_object_t *object, struct obex_hdr *hdr)
{
	enum obex_hdr_id id = obex_hdr_get_id(hdr);
	enum obex_hdr_type type = obex_hdr_get_type(hdr);
	const void *data = obex_hdr_get_data_ptr(hdr);
	size_t len = obex_hdr_get_data_size(hdr);

	DEBUG(4, "\n");

	hdr = obex_hdr_membuf_create(id, type, data, len);
	if (hdr == NULL)
		return -1;

	/* Add element to rx-list */
	object->rx_headerq = slist_append(object->rx_headerq, hdr);

	if (object->rx_it == NULL)
		object->rx_it = obex_hdr_it_create(object->rx_headerq);

	return 0;
}

/*
 * Function obex_object_receive_headers()
 *
 *    Add any incoming headers to headerqueue but does not remove them from
 *    the message buffer.
 *    Returns the total number of bytes of the added headers or -1;
 */
int obex_object_receive_headers(struct obex_object *object, const void *msgdata,
				size_t tx_left, uint64_t filter)
{
	size_t offset = 0;
	int consumed = 0;
	const uint64_t body_filter = (1 << OBEX_HDR_ID_BODY |
						1 << OBEX_HDR_ID_BODY_END);

	DEBUG(4, "\n");

	while (offset < tx_left) {
		struct obex_hdr *hdr = obex_hdr_ptr_parse((uint8_t *)msgdata + offset,
							  tx_left - offset);
		size_t hlen;
		int err = 0;
		uint64_t header_bit;

		if (hdr == NULL)
			break;

		hlen = obex_hdr_get_size(hdr);
		DEBUG(4, "Header: type=%02x, id=%02x, size=%ld\n",
		      obex_hdr_get_type(hdr), obex_hdr_get_id(hdr),
		      (unsigned long)hlen);

		/* Push the body header data either to the application
		 * or to an internal receive buffer.
		 * This also uses the length header in the latter case
		 * but the filter is not checked for it as it's only an
		 * optimisation (currently only works if BODY header is
		 * part of first message).
		 */
		if (hdr && (filter & body_filter) == 0) {
			int used = obex_object_receive_body(object, hdr);
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

			header_bit = (uint64_t) 1 << id;
			if (!(filter & header_bit)) {
				err = obex_object_rcv_one_header(object, hdr);
				consumed += hlen;
			}
			obex_hdr_destroy(hdr);
		}

		if (err)
			return err;

		offset += hlen;
	}

	return consumed;
}

int obex_object_set_body_receiver(obex_object_t *object, struct obex_body *b)
{
	if (!object->body_rcv)
		object->body_rcv = b;

	return (object->body_rcv == b);
}

const void * obex_object_read_body(obex_object_t *object, size_t *size)
{
	return obex_body_read(object->body_rcv, size);
}

int obex_object_suspend(obex_object_t *object)
{
	if (object->suspended)
		return -1;

	object->suspended = true;
	return 0;
}

int obex_object_resume(obex_object_t *object)
{
	if (!object->suspended)
		return -1;

	object->suspended = false;
	return 0;
}
