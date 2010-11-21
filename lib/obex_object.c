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
	obex_object_t *object = malloc(sizeof(*object));

	if (object != NULL) {
		memset(object, 0, sizeof(*object));
		obex_object_setrsp(object, OBEX_RSP_NOT_IMPLEMENTED,
						OBEX_RSP_NOT_IMPLEMENTED);
	}

	return object;
}

/*
 * Function free_headerq(q)
 *
 *    Free all headers in a header queue.
 *
 */
static void free_headerq(slist_t **q)
{
	struct obex_header_element *h;

	DEBUG(4, "\n");
	while (*q != NULL) {
		h = (*q)->data;
		*q = slist_remove(*q, h);
		buf_free(h->buf);
		free(h);
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
	free_headerq(&object->tx_headerq);
	free_headerq(&object->rx_headerq);
	free_headerq(&object->rx_headerq_rm);

	/* Free tx and rx msgs */
	buf_free(object->tx_nonhdr_data);
	object->tx_nonhdr_data = NULL;

	buf_free(object->rx_nonhdr_data);
	object->rx_nonhdr_data = NULL;

	buf_free(object->rx_body);
	object->rx_body = NULL;

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
	if (flags & OBEX_FL_FIT_ONE_PACKET)
		return self->mtu_tx - object->totallen -
					sizeof(struct obex_common_hdr);
	else
		return self->mtu_tx - sizeof(struct obex_common_hdr);
}

/*
 * Function int obex_object_addheader(obex_object_t *object, uint8_t hi,
 *					obex_headerdata_t hv, uint32_t hv_size,
 *					unsigned int flags)
 *
 *    Add a header to the TX-queue.
 *
 */
int obex_object_addheader(obex_t *self, obex_object_t *object, uint8_t hi,
				obex_headerdata_t hv, uint32_t hv_size,
				unsigned int flags)
{
	int ret = -1;
	struct obex_header_element *element;
	unsigned int maxlen;
	size_t hdr_size;

	DEBUG(4, "\n");

	/* End of stream marker */
	if (hi == OBEX_HDR_BODY &&
	    flags & OBEX_FL_STREAM_DATAEND)
	{
		if (self->object == NULL)
			return -1;
		self->object->s_stop = TRUE;
		self->object->s_buf = hv.bs;
		self->object->s_len = hv_size;
		return 1;
	}

	/* Stream data */
	if (hi == OBEX_HDR_BODY &&
	    flags & OBEX_FL_STREAM_DATA)
	{
		if (self->object == NULL)
			return -1;
		self->object->s_buf = hv.bs;
		self->object->s_len = hv_size;
		return 1;
	}

	maxlen = obex_object_getspace(self, object, flags);

	element = malloc(sizeof(*element));
	if (element == NULL)
		return -1;

	memset(element, 0, sizeof(*element));

	element->hi = hi;
	element->flags = flags;

	/* Is this a stream? */
	if (hi == OBEX_HDR_BODY &&
	    flags & OBEX_FL_STREAM_START)
	{
		DEBUG(3, "Adding stream\n");
		element->stream = TRUE;
		object->tx_headerq = slist_append(object->tx_headerq, element);
		return 1;
	}

	if (hi == OBEX_HDR_EMPTY) {
		DEBUG(2, "Empty header\n");
		object->tx_headerq = slist_append(object->tx_headerq, element);
		return 1;
	}

	switch (hi & OBEX_HDR_TYPE_MASK) {
	case OBEX_HDR_TYPE_UINT32:
		DEBUG(2, "4BQ header %d\n", hv.bq4);

		element->buf = buf_new(sizeof(struct obex_uint_hdr));
		if (element->buf) {
			element->length = sizeof(struct obex_uint_hdr);
			ret = insert_uint_header(element->buf, hi, hv.bq4);
		}
		break;

	case OBEX_HDR_TYPE_UINT8:
		DEBUG(2, "1BQ header %d\n", hv.bq1);

		element->buf = buf_new(sizeof(struct obex_ubyte_hdr));
		if (element->buf) {
			element->length = sizeof(struct obex_ubyte_hdr);
			ret = insert_ubyte_header(element->buf, hi, hv.bq1);
		}
		break;

	case OBEX_HDR_TYPE_BYTES:
		DEBUG(2, "BS  header size %d\n", hv_size);

		hdr_size = hv_size + sizeof(struct obex_byte_stream_hdr);

		element->buf = buf_new(hdr_size);

		if (element->buf) {
			element->length = hdr_size;
			ret = insert_byte_stream_header(element->buf, hi,
							hv.bs, hv_size);
		}
		break;

	case OBEX_HDR_TYPE_UNICODE:
		DEBUG(2, "Unicode header size %d\n", hv_size);

		hdr_size = hv_size + sizeof(struct obex_unicode_hdr);
		element->buf = buf_new(hdr_size);
		if (element->buf) {
			element->length = hdr_size;
			ret = insert_unicode_header(element->buf, hi, hv.bs,
								hv_size);
		}
		break;

	default:
		DEBUG(2, "Unsupported encoding %02x\n",
						hi & OBEX_HDR_TYPE_MASK);
		ret = -1;
		break;
	}

	/* Check if you can send this header without violating MTU or
	 * OBEX_FIT_ONE_PACKET */
	if (element->hi != OBEX_HDR_BODY || (flags & OBEX_FL_FIT_ONE_PACKET)) {
		if (maxlen < element->length) {
			DEBUG(0, "Header to big\n");
			ret = -1;
		}
	}

	if (ret > 0) {
		object->totallen += ret;
		object->tx_headerq = slist_append(object->tx_headerq, element);
		ret = 1;
	} else {
		buf_free(element->buf);
		free(element);
	}

	return ret;
}

static uint8_t obex_object_getcmd(const obex_t *self,
						const obex_object_t *object)
{
	if (self->mode == MODE_SRV)
		return object->cmd;
	else
		return (object->opcode & ~OBEX_FINAL);
}

/*
 * Function send_stream(object, header, txmsg, tx_left)
 *
 *  Send a streaming header.
 *
 */
static int send_stream(obex_t *self,
				struct obex_header_element *h,
				buf_t *txmsg, unsigned int tx_left)
{
	obex_object_t *object = self->object;
	struct obex_byte_stream_hdr *hdr;
	int actual;	/* Number of bytes sent in this fragment */
	uint8_t cmd = obex_object_getcmd(self, object);

	DEBUG(4, "\n");

	/* Fill in length and header type later, but reserve space for it */
	hdr = buf_reserve_end(txmsg, sizeof(*hdr));
	tx_left -= sizeof(*hdr);
	actual = sizeof(*hdr);

	do {
		if (object->s_len == 0) {
			/* Ask app for more data if no more */
			object->s_offset = 0;
			object->s_buf = NULL;
			obex_deliver_event(self, OBEX_EV_STREAMEMPTY, cmd,
								0, FALSE);
			DEBUG(4, "s_len=%d, s_stop = %d\n",
						object->s_len, object->s_stop);
			/* End of stream ?*/
			if (object->s_stop)
				break;

			/* User suspended and didn't provide any new data */
			if (object->suspend && object->s_buf == NULL)
				break;

			/* Error ?*/
			if (object->s_buf == NULL) {
				DEBUG(1, "Unexpected end-of-stream\n");
				return -1;
			}
		}

		if (tx_left < object->s_len) {
			/* There is more data left in buffer than tx_left */
			DEBUG(4, "More data than tx_left. Buffer will not be empty\n");

			buf_insert_end(txmsg,
				(uint8_t *) object->s_buf + object->s_offset,
				tx_left);
			object->s_len -= tx_left;
			object->s_offset += tx_left;
			actual += tx_left;
			tx_left = 0;
		} else {
			/* There less data in buffer than tx_left */
			DEBUG(4, "Less data that tx_left. Buffer will be empty\n");
			buf_insert_end(txmsg,
				(uint8_t *) object->s_buf + object->s_offset,
				object->s_len);
			tx_left -= object->s_len;
			object->s_offset += object->s_len;
			actual += object->s_len;
			object->s_len = 0;
			if (object->suspend)
				tx_left = 0;
		}
	} while (tx_left > 0);

	DEBUG(4, "txmsg full or no more stream-data. actual = %d\n", actual);
	hdr->hi = OBEX_HDR_BODY;

	if (object->s_stop && object->s_len == 0) {
		/* We are done. Remove header from tx-queue */
		object->tx_headerq = slist_remove(object->tx_headerq, h);
		hdr->hi = OBEX_HDR_BODY_END;
		buf_free(h->buf);
		free(h);
	}

	hdr->hl = htons((uint16_t)actual);

	return actual;
}


/*
 * Function send_body(object, header, txmsg, tx_left)
 *
 *  Fragment and send the body
 *
 */
static int send_body(obex_object_t *object,
				struct obex_header_element *h,
				buf_t *txmsg, unsigned int tx_left)
{
	struct obex_byte_stream_hdr *hdr;
	unsigned int actual;

	hdr = buf_reserve_end(txmsg, sizeof(*hdr));

	if (!h->body_touched) {
		/* This is the first time we try to send this header
		 * obex_object_addheaders has added a
		 * struct_byte_stream_hdr before the actual body-data.
		 * We shall send this in every fragment so we just
		 * remove it for now.*/

		buf_remove_begin(h->buf, sizeof(*hdr));
		h->body_touched = TRUE;
	}

	if (tx_left < (h->buf->data_size + sizeof(*hdr))) {
		DEBUG(4, "Add BODY header\n");
		hdr->hi = OBEX_HDR_BODY;
		hdr->hl = htons((uint16_t)tx_left);

		buf_insert_end(txmsg, h->buf->data, tx_left - sizeof(*hdr));

		buf_remove_begin(h->buf, tx_left - sizeof(*hdr));
		/* We have completely filled the tx-buffer */
		actual = tx_left;
	} else {
		DEBUG(4, "Add BODY_END header\n");

		hdr->hi = OBEX_HDR_BODY_END;
		hdr->hl = htons((uint16_t)(h->buf->data_size + sizeof(*hdr)));
		buf_insert_end(txmsg, h->buf->data, h->buf->data_size);
		actual = h->buf->data_size;

		object->tx_headerq = slist_remove(object->tx_headerq, h);
		buf_free(h->buf);
		free(h);
	}

	return actual;
}


/*
 * Function obex_object_send()
 *
 *    Send away all headers attached to an object. Returns:
 *       1 on sucessfully done
 *       0 on progress made
 *     < 0 on error
 */
int obex_object_send(obex_t *self, obex_object_t *object,
					int allowfinalcmd, int forcefinalbit)
{
	struct obex_header_element *h;
	buf_t *txmsg;
	int actual, finished = 0;
	uint16_t tx_left;
	int addmore = TRUE;
	int real_opcode;

	DEBUG(4, "allowfinalcmd: %d forcefinalbit:%d\n", allowfinalcmd,
			forcefinalbit);

	/* Return finished if aborted */
	if (object->abort)
		return 1;

	/* Don't do anything of object is suspended */
	if (object->suspend)
		return 0;

	/* Calc how many bytes of headers we can fit in this package */
	tx_left = self->mtu_tx - sizeof(struct obex_common_hdr);
#ifdef HAVE_IRDA
	if (self->trans.type == OBEX_TRANS_IRDA &&
	    0 < self->trans.mtu && self->trans.mtu < self->mtu_tx)
	{
		tx_left -= self->mtu_tx % self->trans.mtu;
	}
#endif /*HAVE_IRDA*/

	/* Reuse transmit buffer */
	txmsg = buf_reuse(self->tx_msg);

	/* Add nonheader-data first if any (SETPATH, CONNECT)*/
	if (object->tx_nonhdr_data) {
		DEBUG(4, "Adding %lu bytes of non-headerdata\n",
			(unsigned long) object->tx_nonhdr_data->data_size);
		buf_insert_end(txmsg, object->tx_nonhdr_data->data,
					object->tx_nonhdr_data->data_size);

		buf_free(object->tx_nonhdr_data);
		object->tx_nonhdr_data = NULL;
	}

	DEBUG(4, "4\n");

	/* Take headers from the tx queue and try to stuff as
	 * many as possible into the tx-msg */
	while (addmore == TRUE && object->tx_headerq != NULL) {

		h = object->tx_headerq->data;

		switch (h->hi) {
		case OBEX_HDR_BODY:
			if (h->flags & OBEX_FL_SUSPEND)
				object->suspend = 1;
			if (h->stream) {
				/* This is a streaming body */
				actual = send_stream(self, h, txmsg, tx_left);
			} else {
				/* The body may be fragmented over several
				 * packets. */
				actual = send_body(object, h, txmsg, tx_left);
			}
			if (actual < 0 )
				return -1;
			tx_left -= actual;
			break;

		case OBEX_HDR_EMPTY:
			if (h->flags & OBEX_FL_SUSPEND)
				object->suspend = 1;
			object->tx_headerq = slist_remove(object->tx_headerq,
									h);
			free(h);
			break;

		default:
			if (h->length <= tx_left) {
				/* There is room for more data in tx msg */
				DEBUG(4, "Adding non-body header\n");
				buf_insert_end(txmsg, h->buf->data, h->length);
				tx_left -= h->length;
				if (h->flags & OBEX_FL_SUSPEND)
					object->suspend = 1;

				/* Remove from tx-queue */
				object->tx_headerq =
					slist_remove(object->tx_headerq, h);
				object->totallen -= h->length;
				buf_free(h->buf);
				free(h);

			} else if (h->length > self->mtu_tx) {
				/* Header is bigger than MTU. This should not
				 * happen, because OBEX_ObjectAddHeader()
				 * rejects headers bigger than the MTU */

				DEBUG(0, "ERROR! header to big for MTU\n");
				return -1;

			} else {
				/* This header won't fit. */
				addmore = FALSE;
			}
			break;
		}

		if (object->suspend)
			addmore = FALSE;

		if (tx_left == 0)
			addmore = FALSE;
	};

	/* Decide which command to use, and if to use final-bit */
	if (object->tx_headerq) {
		/* Have more headers (or body) to send */
		real_opcode = object->opcode;

		/* In server, final bit is always set.
		 * In client, final bit is set only when we finish sending. */
		if (forcefinalbit)
			real_opcode |= OBEX_FINAL;
		finished = 0;

	} else {
		/* Have no more headers to send */
		if (allowfinalcmd == FALSE) {
			/* Not allowed to send final command (== server,
			 * receiving incomming request) */
			real_opcode = object->opcode;

		} else {
			/* Allowed to send final command (== end data we are
			 * sending) */
			real_opcode = object->lastopcode;
		}
		real_opcode |= OBEX_FINAL;
		finished = !!allowfinalcmd;
	}

	DEBUG(4, "Sending package with opcode %d\n", real_opcode);
	actual = obex_data_request(self, txmsg, real_opcode);

	if (actual < 0) {
		DEBUG(4, "Send error\n");
		return -1;
	} else
		return finished;
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
	uint32_t *bq4;
	struct obex_header_element *h;

	DEBUG(4, "\n");

	/* No more headers */
	if (object->rx_headerq == NULL)
		return 0;

	/* New headers are appended at the end of the list while
	 * receiving, so we pull them from the front.  Since we cannot
	 * free the mem used just yet just put the header in another
	 * list so we can free it when the object is deleted. */

	h = object->rx_headerq->data;
	object->rx_headerq = slist_remove(object->rx_headerq, h);
	object->rx_headerq_rm = slist_append(object->rx_headerq_rm, h);

	*hi = h->hi;
	*hv_size= h->length;

	switch (h->hi & OBEX_HDR_TYPE_MASK) {
		case OBEX_HDR_TYPE_BYTES:
			hv->bs = &h->buf->data[0];
			break;

		case OBEX_HDR_TYPE_UNICODE:
			hv->bs = &h->buf->data[0];
			break;

		case OBEX_HDR_TYPE_UINT32:
			bq4 = (uint32_t*) h->buf->data;
			hv->bq4 = ntohl(*bq4);
			break;

		case OBEX_HDR_TYPE_UINT8:
			hv->bq1 = h->buf->data[0];
			break;
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
	if (object->rx_headerq != NULL)
		return 0;

	/* Put the old headers back in the active list */
	object->rx_headerq = object->rx_headerq_rm;
	object->rx_headerq_rm = NULL;

	/* Success */
	return 1;
}

/*
 * Function obex_object_receive_stream()
 *
 *    Handle receiving of body-stream
 *
 */
static void obex_object_receive_stream(obex_t *self, uint8_t hi,
						const uint8_t *source,
						unsigned int len)
{
	obex_object_t *object = self->object;
	uint8_t cmd = obex_object_getcmd(self, object);

	DEBUG(4, "\n");

	if (object->abort) {
		DEBUG(3, "Ignoring incoming data because request was aborted\n");
		return;
	}

	/* Spare the app this empty nonlast body-hdr */
	if (hi == OBEX_HDR_BODY && len == 0)
		return;

	/* Notify app that data has arrived */
	object->s_buf = source;
	object->s_len = len;
	obex_deliver_event(self, OBEX_EV_STREAMAVAIL, cmd, 0, FALSE);

	/* If send send EOS to app */
	if (hi == OBEX_HDR_BODY_END && len != 0) {
		object->s_buf = source;
		object->s_len = 0;
		obex_deliver_event(self, OBEX_EV_STREAMAVAIL, cmd, 0, FALSE);
	}

	object->s_buf = NULL;
	object->s_len = 0;
}

/*
 * Function obex_object_receive_body()
 *
 *    Handle receiving of body
 *
 */
static int obex_object_receive_buffered(obex_t *self, uint8_t hi,
							const uint8_t *source,
							unsigned int len)
{
	obex_object_t *object = self->object;

	DEBUG(4, "This is a body-header. Len=%d\n", len);

	if (!object->rx_body) {
		int alloclen = OBEX_OBJECT_ALLOCATIONTRESHOLD + len;

		if (object->hinted_body_len)
			alloclen = object->hinted_body_len;

		DEBUG(4, "Allocating new body-buffer. Len=%d\n", alloclen);
		object->rx_body = buf_new(alloclen);
		if (!object->rx_body)
			return -1;
	}

	/* Reallocate body buffer if needed */
	if (object->rx_body->data_avail + object->rx_body->tail_avail < (unsigned int) len) {
		int t;
		size_t needed;
		DEBUG(4, "Buffer too small. Go realloc\n");
		t = buf_total_size(object->rx_body);
		needed = t + OBEX_OBJECT_ALLOCATIONTRESHOLD + len;
		buf_resize(object->rx_body, needed);
		if (buf_total_size(object->rx_body) != needed) {
			DEBUG(1, "Can't realloc rx_body\n");
			return -1;
			/* FIXME: Handle this in a nice way... */
		}
	}

	buf_insert_end(object->rx_body, source, len);

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

	DEBUG(4, "\n");

	if (object->headeroffset == 0)
		return 0;

	/* Copy any non-header data (like in CONNECT and SETPATH) */

	object->rx_nonhdr_data = buf_new(object->headeroffset);
	if (!object->rx_nonhdr_data)
		return -1;
	buf_insert_end(object->rx_nonhdr_data, msg->data,
						object->headeroffset);
	DEBUG(4, "Command has %lu bytes non-headerdata\n",
			(unsigned long) object->rx_nonhdr_data->data_size);

	return 0;
}

static int obex_object_get_hdrdata(buf_t *msg, uint16_t offset,
					uint8_t **source, unsigned int *len,
					unsigned int *hlen)
{
	union {
		struct obex_unicode_hdr     *unicode;
		struct obex_byte_stream_hdr *bstream;
	} h;
	int err = 0;
	uint8_t *msgdata = msg->data + offset;
	uint8_t hi = msgdata[0];

	switch (hi & OBEX_HDR_TYPE_MASK) {
	case OBEX_HDR_TYPE_UNICODE:
		h.unicode = (struct obex_unicode_hdr *) msgdata;
		*source = &msgdata[3];
		*hlen = ntohs(h.unicode->hl);
		*len = *hlen - 3;
		break;

	case OBEX_HDR_TYPE_BYTES:
		h.bstream = (struct obex_byte_stream_hdr *) msgdata;
		*source = &msgdata[3];
		*hlen = ntohs(h.bstream->hl);
		*len = *hlen - 3;
		break;

	case OBEX_HDR_TYPE_UINT8:
		*source = &msgdata[1];
		*hlen = 2;
		*len = 1;
		break;

	case OBEX_HDR_TYPE_UINT32:
		*source = &msgdata[1];
		*hlen = 5;
		*len = 4;
		break;

	default:
		DEBUG(1, "Badly formed header received\n");
		err = -1;
		break;
	}

	return err;
}

static int obex_object_receive_body(obex_t *self, uint8_t hi,
							const uint8_t *source,
							unsigned int len)
{
	obex_object_t *object = self->object;

	DEBUG(4, "\n");

	if (object->s_srv) {
		if (hi == OBEX_HDR_BODY || hi == OBEX_HDR_BODY_END) {
			/* The body-header need special treatment */
			obex_object_receive_stream(self, hi, source, len);
			/* We have already handled this data! */
			return 1;
		}

	} else {
		if (hi == OBEX_HDR_BODY || hi == OBEX_HDR_BODY_END) {
			if (obex_object_receive_buffered(self, hi,
							source, len) < 0)
				return -1;

			if (hi == OBEX_HDR_BODY) {
				DEBUG(4, "Normal body fragment...\n");
				/* We have already handled this data! */
				return 1;
			}

		} else if (hi == OBEX_HDR_LENGTH && !object->rx_body) {
			/* The length MAY be useful when receiving body. */
			uint32_t value;
			memcpy(&value, source, sizeof(value));
			object->hinted_body_len = ntohl(value);
			DEBUG(4, "Hinted body len is %d\n",
						object->hinted_body_len);
		}
	}
	return 0;
}

static int obex_object_rcv_one_header(obex_t *self, uint8_t hi,
							const uint8_t *source,
							unsigned int len)
{
	struct obex_header_element *element;
	obex_object_t *object = self->object;
	int err = 0;

	DEBUG(4, "\n");

	element = malloc(sizeof(*element));
	if (element == NULL) {
		DEBUG(1, "Cannot allocate memory\n");
		if (hi == OBEX_HDR_BODY_END && object->rx_body) {
			buf_free(object->rx_body);
			object->rx_body = NULL;
		}
		err = -1;

	} else {
		memset(element, 0, sizeof(*element));

		if (hi == OBEX_HDR_BODY_END)
			hi = OBEX_HDR_BODY;
		element->hi = hi;

		if (hi == OBEX_HDR_BODY && object->rx_body) {
			DEBUG(4, "Body receive done\n");
			element->length = object->rx_body->data_size;
			element->buf = object->rx_body;
			object->rx_body = NULL;

		} else if (len == 0) {
			/* If we get an emtpy we have to deal with it...
			 * This might not be an optimal way, but it works. */
			DEBUG(4, "Got empty header. Allocating dummy buffer anyway\n");
			element->buf = buf_new(1);

		} else {
			element->length = len;
			element->buf = buf_new(len);
			if (element->buf) {
				DEBUG(4, "Copying %d bytes\n", len);
				buf_insert_end(element->buf, source, len);
			}
		}

		if (!element->buf) {
			DEBUG(1, "Cannot allocate memory\n");
			free(element);
			element = NULL;
			err = -1;
		}
	}

	if (element) {
		/* Add element to rx-list */
		object->rx_headerq = slist_append(object->rx_headerq, element);
	}

	return err;
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

/*
 * Function obex_object_receive_headers()
 *
 *    Add any incoming headers to headerqueue but does not remove them from
 *    the message buffer.
 *    Returns the total number of bytes of the added headers or -1;
 */
int obex_object_receive_headers(obex_t *self, buf_t *msg, uint64_t filter)
{
	struct obex_common_hdr *hdr = (obex_common_hdr_t *)msg->data;
	uint16_t offset = sizeof(*hdr) + self->object->headeroffset;
	int consumed = 0;
	const uint64_t body_filter = (1 << OBEX_HDR_ID_BODY |
						1 << OBEX_HDR_ID_BODY_END);
	uint16_t msglen = ntohs(hdr->len);

	DEBUG(4, "\n");

	while (offset < msglen) {
		uint8_t hi = msg->data[offset];
		uint8_t *source = NULL;
		unsigned int len = 0;
		unsigned int hlen = 0;
		int err = 0;
		uint64_t header_bit;

		DEBUG(4, "Header: %02x\n", hi);

		err = obex_object_get_hdrdata(msg, offset, &source,
								&len, &hlen);

		/* Make sure that the msg is big enough for header */
		if (len > (unsigned int)(msglen - offset)) {
			DEBUG(1, "Header %d too big. HSize=%d Buffer=%lu\n",
				hi, len,
				(unsigned long) msglen - offset);
			source = NULL;
			err = -1;
		}

		/* Push the body header data either to the application
		 * or to an internal receive buffer.
		 * This also uses the length header in the latter case
		 * but the filter is not checked for it as it's only an
		 * optimisation (currently only works if BODY header is
		 * part of first message).
		 */
		if (source && (filter & body_filter) == 0) {
			int used = obex_object_receive_body(self, hi,
								source, len);
			if (used != 0)
				source = NULL;
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
		header_bit = (uint64_t) 1 << (hi & OBEX_HDR_ID_MASK);
		if (source && (filter & header_bit) == 0) {
			err = obex_object_rcv_one_header(self, hi, source,
									len);
			consumed += hlen;
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

	DEBUG(4, "s_len = %d\n", object->s_len);
	*buf = object->s_buf;

	return object->s_len;
}

int obex_object_suspend(obex_object_t *object)
{
	object->suspend = 1;
	return 0;
}

int obex_object_resume(obex_t *self, obex_object_t *object)
{
	int ret;
	uint8_t cmd = obex_object_getcmd(self, object);
	int allowfinalcmd = TRUE, forcefinalbit = FALSE;

	if (!object->suspend)
		return 0;

	object->suspend = 0;

	if (object->first_packet_sent && !object->continue_received)
		return 0;

	if (self->mode == MODE_SRV) {
		forcefinalbit = TRUE;
		if (self->state == STATE_REC)
			allowfinalcmd = FALSE;
	}

	ret = obex_object_send(self, object, allowfinalcmd, forcefinalbit);

	if (ret < 0) {
		obex_deliver_event(self, OBEX_EV_LINKERR, cmd, 0, TRUE);
		return -1;
	} else if (ret == 0) {
		obex_deliver_event(self, OBEX_EV_PROGRESS, cmd, 0, FALSE);
		object->first_packet_sent = 1;
		object->continue_received = 0;
	} else {
		if (self->mode == MODE_SRV) {
			obex_deliver_event(self, OBEX_EV_REQDONE,
							cmd, 0, TRUE);
			self->state = STATE_IDLE;
			return 0;
		}
	}

	self->state = STATE_REC;

	return 0;
}
