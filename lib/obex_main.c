/**
	\file obex_main.c
	Implementation of the Object Exchange Protocol OBEX.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 2000 Pontus Fuchs, All Rights Reserved.
	Copyright (c) 1998, 1999 Dag Brattli, All Rights Reserved.

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

#ifdef _WIN32
#include <winsock2.h>
#else /* _WIN32 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdio.h>

#endif /* _WIN32 */

#include "obex_main.h"
#include "obex_transport.h"
#include "obex_object.h"
#include "obex_server.h"
#include "obex_client.h"
#include "databuffer.h"

#include <openobex/obex_const.h>

int obex_debug;
int obex_dump;

/*
 * Function obex_response_to_string(rsp)
 *
 *    Return a string of an OBEX-response
 *
 */
char *obex_response_to_string(int rsp)
{
	switch (rsp) {
	case OBEX_RSP_CONTINUE:
		return "Continue";
	case OBEX_RSP_SWITCH_PRO:
		return "Switching protocols";
	case OBEX_RSP_SUCCESS:
		return "OK, Success";
	case OBEX_RSP_CREATED:
		return "Created";
	case OBEX_RSP_ACCEPTED:
		return "Accepted";
	case OBEX_RSP_NO_CONTENT:
		return "No Content";
	case OBEX_RSP_BAD_REQUEST:
		return "Bad Request";
	case OBEX_RSP_UNAUTHORIZED:
		return "Unauthorized";
	case OBEX_RSP_PAYMENT_REQUIRED:
		return "Payment required";
	case OBEX_RSP_FORBIDDEN:
		return "Forbidden";
	case OBEX_RSP_NOT_FOUND:
		return "Not found";
	case OBEX_RSP_METHOD_NOT_ALLOWED:
		return "Method not allowed";
	case OBEX_RSP_CONFLICT:
		return "Conflict";
	case OBEX_RSP_INTERNAL_SERVER_ERROR:
		return "Internal server error";
	case OBEX_RSP_NOT_IMPLEMENTED:
		return "Not implemented!";
	case OBEX_RSP_DATABASE_FULL:
		return "Database full";
	case OBEX_RSP_DATABASE_LOCKED:
		return "Database locked";
	default:
		return "Unknown response";
	}
}

/*
 * Function obex_deliver_event ()
 *
 *    Deliver an event to app.
 *
 */
void obex_deliver_event(obex_t *self, enum obex_event event, enum obex_cmd cmd,
			enum obex_rsp rsp, int del)
{
	obex_object_t *object = self->object;

	if (del == TRUE)
		self->object = NULL;

	self->eventcb(self, object, self->mode, event, cmd, rsp);

	if (del == TRUE)
		obex_object_delete(object);
}

void obex_data_request_init(obex_t *self)
{
	buf_t *msg = self->tx_msg;

	buf_clear(msg, buf_get_length(msg));
	buf_set_size(msg, self->mtu_tx);
	buf_append(msg, NULL, sizeof(struct obex_common_hdr));	
}

/** Prepare response or command code along with optional headers/data to send.
 *
 * The caller is supposed to reserve the size of struct obex_common_hdr at the
 * begin of the message buffer, e.g. using obex_data_request_init()
 *
 * @param self the obex instance
 * @param msg the message buffer
 * @opcode the message opcode
 */
void obex_data_request_prepare(obex_t *self, int opcode)
{
	buf_t *msg = self->tx_msg;
	obex_common_hdr_t *hdr = buf_get(msg);

	/* alignment is assured here */
	hdr->opcode = opcode;
	hdr->len = htons((uint16_t)buf_get_length(msg));

	DUMPBUFFER(1, "Tx", msg);
}

int obex_msg_prepare(obex_t *self, obex_object_t *object, int allowfinal)
{
	buf_t *txmsg = self->tx_msg;
	uint16_t tx_left = self->mtu_tx - sizeof(struct obex_common_hdr);
	int real_opcode;
	unsigned int srm_flags = 0;

#ifdef HAVE_IRDA
	if (self->trans.type == OBEX_TRANS_IRDA &&
			self->trans.mtu > 0 && self->trans.mtu < self->mtu_tx)
		tx_left -= self->mtu_tx % self->trans.mtu;
#endif /*HAVE_IRDA*/

	obex_data_request_init(self);

	if (!obex_object_append_data(object, txmsg, tx_left, &srm_flags))
		return 0;

	real_opcode = obex_object_get_real_opcode(self->object, allowfinal,
						  self->mode);
	DEBUG(4, "Generating packet with opcode %d\n", real_opcode);
	obex_data_request_prepare(self, real_opcode);

	self->srm_flags &= ~OBEX_SRM_FLAG_WAIT_REMOTE;
	self->srm_flags |= srm_flags;

	return 1;
}

/*
 * Function obex_data_request (self, opcode)
 *
 *    Send message.
 *
 */
int obex_data_request(obex_t *self)
{
	buf_t *msg = self->tx_msg;
	int status;

	obex_return_val_if_fail(self != NULL, -1);

	DEBUG(1, "len = %lu bytes\n", (unsigned long)buf_get_length(msg));

	status = obex_transport_write(self, msg);
	if (status > 0)
		buf_clear(msg, status);

	return status;
}

int obex_msg_transmit(obex_t *self)
{
	buf_t *msg = self->tx_msg;

	if (!buf_get_length(msg)) {
		int ret = obex_data_request(self);
		if (ret < 0) {
			DEBUG(4, "Send error\n");
			return -1;
		}
	}

	return !!buf_get_length(msg);
}


static int obex_mode(obex_t *self)
{
	switch (self->mode) {
	case OBEX_MODE_SERVER:
		return obex_server(self);

	case OBEX_MODE_CLIENT:
		return obex_client(self);

	default:
		return -1;
	}
}

/*
 * Function obex_work (self, timeout)
 *
 *    Do some work on the current transferred object.
 *
 */
int obex_work(obex_t *self, int timeout)
{
	if (self->state == STATE_IDLE ||
	    self->substate == SUBSTATE_RECEIVE_RX)
	{
		int ret = obex_transport_handle_input(self, timeout);
		if (ret <= 0)
			return ret;
	}

	return obex_mode(self);
}

/*
 * Check if a message buffer contains at least one full message.
 */
int obex_get_buffer_status(buf_t *msg) {
	obex_common_hdr_t *hdr = buf_get(msg);

	return (buf_get_length(msg) >= sizeof(*hdr) &&
		buf_get_length(msg) >= ntohs(hdr->len));
}

/*
 * Function obex_data_indication (self)
 *
 *    Read some input from device and find out which packet it is
 *
 */
int obex_data_indication(obex_t *self)
{
	obex_common_hdr_t *hdr;
	buf_t *msg;
	int actual;
	unsigned int size;

	DEBUG(4, "\n");

	obex_return_val_if_fail(self != NULL, -1);

	msg = self->rx_msg;

	/* First we need 3 bytes to be able to know how much data to read */
	if (buf_get_length(msg) < sizeof(*hdr))  {
		size_t readsize = sizeof(*hdr) - buf_get_length(msg);
		actual = obex_transport_read(self, readsize);

		DEBUG(4, "Got %d bytes\n", actual);

		/* Check if we are still connected */
		/* do not error if the data is from non-empty but
		 * partial buffer (custom transport) */
		if (actual < 0) {
			obex_deliver_event(self, OBEX_EV_LINKERR, 0, 0, TRUE);
			return -1;
		}
	}

	/* If we have 3 bytes data we can decide how big the packet is */
	if (buf_get_length(msg) >= sizeof(*hdr)) {
		hdr = buf_get(msg);
		size = ntohs(hdr->len);

		actual = 0;
		if (buf_get_length(msg) < size) {
			size_t readsize = size - buf_get_length(msg);
			actual = obex_transport_read(self, readsize);

			/* hdr might not be valid anymore if the _read
			 * did a realloc */
			hdr = buf_get(msg);

			/* Check if we are still connected */
			/* do not error if the data is from non-empty
			 * but partial buffer (custom transport) */
			if (actual < 0) {
				obex_deliver_event(self, OBEX_EV_LINKERR,
								0, 0, TRUE);
				return -1;
			}
		}
	} else {
		/* Wait until we have at least 3 bytes data */
		DEBUG(3, "Need at least 3 bytes got only %lu!\n",
		      (unsigned long)buf_get_length(msg));
		return 0;
        }

	/* New data has been inserted at the end of message */
	DEBUG(1, "Got %d bytes msg len=%lu\n", actual,
	      (unsigned long)buf_get_length(msg));

	/*
	 * Make sure that the buffer we have, actually has the specified
	 * number of bytes. If not the frame may have been fragmented, and
	 * we will then need to read more from the socket.
	 */

	/* Make sure we have a whole packet */
	if (size > buf_get_length(msg)) {
		DEBUG(3, "Need more data, size=%d, len=%lu!\n",
		      size, (unsigned long)buf_get_length(msg));

		/* I'll be back! */
		return 0;
	}

	DUMPBUFFER(2, "Rx", msg);

	return 1;
}

buf_t* obex_data_receive(obex_t *self)
{
	buf_t *msg = self->rx_msg;

	if (!obex_get_buffer_status(msg))
		return NULL;

	self->srm_flags &= ~OBEX_SRM_FLAG_WAIT_LOCAL;
	return msg;
}

void obex_data_receive_finished(obex_t *self)
{
	buf_t *msg = self->rx_msg;
	obex_common_hdr_t *hdr = buf_get(msg);
	unsigned int size = ntohs(hdr->len);

	DEBUG(4, "Pulling %u bytes\n", size);
	buf_clear(msg, size);
}

/*
 * Function obex_cancel_request ()
 *
 *    Cancel an ongoing request
 *
 */
int obex_cancelrequest(obex_t *self, int nice)
{
	/* If we have no ongoing request do nothing */
	if (self->object == NULL)
		return 0;

	/* Abort request without sending abort */
	if (!nice) {
		/* Deliver event will delete the object */
		obex_deliver_event(self, OBEX_EV_ABORT, 0, 0, TRUE);
		buf_clear(self->tx_msg, buf_get_length(self->tx_msg));
		buf_clear(self->rx_msg, buf_get_length(self->rx_msg));
		/* Since we didn't send ABORT to peer we are out of sync
		 * and need to disconnect transport immediately, so we
		 * signal link error to app */
		obex_deliver_event(self, OBEX_EV_LINKERR, 0, 0, FALSE);
		return 1;

	} else {
		/* The client or server code will take action at the
		 * right time. */
		self->object->abort = TRUE;

		return 1;
	}
}
