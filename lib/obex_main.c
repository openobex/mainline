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
#include <errno.h>

#include "obex_main.h"
#include "obex_transport.h"
#include "obex_object.h"
#include "obex_server.h"
#include "obex_client.h"
#include "obex_hdr.h"
#include "obex_msg.h"
#include "databuffer.h"

#include <openobex/obex_const.h>

int obex_debug;
int obex_dump;

void obex_library_init(void)
{
	char *env;

#if OBEX_DEBUG
	obex_debug = OBEX_DEBUG;
#else
	obex_debug = -1;
#endif
	env = getenv("OBEX_DEBUG");
	if (env)
		obex_debug = atoi(env);

#if OBEX_DUMP
	obex_dump = OBEX_DUMP;
#else
	obex_dump = 0;
#endif
	env = getenv("OBEX_DUMP");
	if (env)
		obex_dump = atoi(env);
}

obex_t * obex_create(obex_event_t eventcb, unsigned int flags)
{
	obex_t *self;

	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return NULL;

	self->eventcb = eventcb;
	self->init_flags = flags;
	self->mode = OBEX_MODE_SERVER;
	self->state = STATE_IDLE;
	self->rsp_mode = OBEX_RSP_MODE_NORMAL;

	/* Safe values.
	 * Both self->mtu_rx and self->mtu_tx_max can be increased by app
	 * self->mtu_tx will be whatever the other end sends us - Jean II */
	self->mtu_tx = OBEX_MINIMUM_MTU;
	if (obex_set_mtu(self, OBEX_DEFAULT_MTU, OBEX_DEFAULT_MTU)) {
		obex_destroy(self);
		self = NULL;
	}

	return self;
}

void obex_destroy(obex_t *self)
{
	if (self->trans)
		obex_transport_cleanup(self);

	if (self->tx_msg)
		buf_delete(self->tx_msg);

	if (self->rx_msg)
		buf_delete(self->rx_msg);

	free(self);
}

int obex_set_mtu(obex_t *self, uint16_t mtu_rx, uint16_t mtu_tx_max)
{
	if (mtu_rx < OBEX_MINIMUM_MTU /*|| mtu_rx > OBEX_MAXIMUM_MTU*/)
		return -E2BIG;

	if (mtu_tx_max < OBEX_MINIMUM_MTU /*|| mtu_tx_max > OBEX_MAXIMUM_MTU*/)
		return -E2BIG;

	/* Change MTUs */
	self->mtu_rx = mtu_rx;
	self->mtu_tx_max = mtu_tx_max;

	/* (Re)Allocate transport buffers */
	if (self->rx_msg)
		buf_set_size(self->rx_msg, self->mtu_rx);
	else
		self->rx_msg = membuf_create(self->mtu_rx);		
	if (self->rx_msg == NULL)
		return -ENOMEM;

	if (self->tx_msg)
		buf_set_size(self->tx_msg, self->mtu_tx_max);
	else
		self->tx_msg = membuf_create(self->mtu_tx_max);
	if (self->tx_msg == NULL)
		return -ENOMEM;

	return 0;
}

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
			enum obex_rsp rsp, bool delete_object)
{
	obex_object_t *object = self->object;

	if (delete_object)
		self->object = NULL;

	self->eventcb(self, object, self->mode, event, cmd, rsp);

	if (delete_object)
		obex_object_delete(object);
}

bool obex_data_request_init(obex_t *self)
{
	buf_t *msg = self->tx_msg;
	int err;

	buf_clear(msg, buf_get_length(msg));
	err = buf_set_size(msg, self->mtu_tx);
	if (err)
		return false;

	buf_append(msg, NULL, sizeof(struct obex_common_hdr));
	return true;
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

/** Transmit some data from the TX message buffer. */
static bool obex_data_request_transmit(obex_t *self)
{
	buf_t *msg = self->tx_msg;

	if (buf_get_length(msg)) {
		ssize_t status = obex_transport_write(self, msg);
		if (status > 0)
			buf_clear(msg, status);
		else if (status < 0) {
			DEBUG(4, "Send error\n");
			return false;
		}
	}
	return true;
}


static result_t obex_mode(obex_t *self)
{
	switch (self->mode) {
	case OBEX_MODE_SERVER:
		return obex_server(self);

	case OBEX_MODE_CLIENT:
		return obex_client(self);

	default:
		return RESULT_ERROR;
	}
}

enum obex_data_direction obex_get_data_direction(obex_t *self)
{
	if (self->state == STATE_IDLE)
		return OBEX_DATA_IN;

	else if (self->substate == SUBSTATE_RX)
		return OBEX_DATA_IN;

	else if (self->substate == SUBSTATE_TX)
		return OBEX_DATA_OUT;

	else
		return OBEX_DATA_NONE;
}

result_t obex_handle_input(obex_t *self)
{
	result_t ret = obex_transport_handle_input(self);

	if (ret != RESULT_SUCCESS)
		return ret;

	if (obex_transport_is_server(self)) {
		DEBUG(4, "Data available on server socket\n");
		if (self->init_flags & OBEX_FL_KEEPSERVER)
			/* Tell the app to perform the OBEX_Accept() */
			obex_deliver_event(self, OBEX_EV_ACCEPTHINT, 0, 0, FALSE);

		else
			obex_transport_accept(self, self);

		return RESULT_SUCCESS;

	} else {
		DEBUG(4, "Data available on client socket\n");
		return obex_data_indication(self);
	}
}

static bool obex_check_srm_input(obex_t *self)
{
	if (self->object->rsp_mode == OBEX_RSP_MODE_SINGLE &&
	    !(self->srm_flags & OBEX_SRM_FLAG_WAIT_LOCAL) &&
	    ((self->mode == OBEX_MODE_CLIENT && self->state == STATE_REQUEST) ||
	     (self->mode == OBEX_MODE_SERVER && self->state == STATE_RESPONSE)))
	{
		result_t ret = obex_handle_input(self);
		if (ret == RESULT_TIMEOUT) {
			self->substate = SUBSTATE_TX_PREPARE;
			return false;
		}
	}
	return true;
}

/*
 * Function obex_work (self)
 *
 *    Do some work on the current transferred object.
 *
 */
result_t obex_work(obex_t *self)
{
	result_t ret;

	if (self->state == STATE_IDLE) {
		ret = obex_handle_input(self);
		if (ret != RESULT_SUCCESS)
			return ret;

	} else if (self->substate == SUBSTATE_RX) {
		if (obex_check_srm_input(self)) {
			ret = obex_handle_input(self);
			if (ret != RESULT_SUCCESS)
				return ret;
		}

	} else if (self->substate == SUBSTATE_TX) {
		if (!obex_msg_tx_status(self)) {
			if (!obex_data_request_transmit(self)) {
				enum obex_cmd cmd = OBEX_CMD_ABORT;

				if (self->object)
					cmd = obex_object_getcmd(self->object);
		
				obex_deliver_event(self, OBEX_EV_LINKERR, cmd,
						   0, TRUE);
				self->mode = OBEX_MODE_SERVER;
				self->state = STATE_IDLE;
				return RESULT_ERROR;
			}

			if (!obex_msg_tx_status(self))
				return RESULT_TIMEOUT;
		}
	}

	return obex_mode(self);
}

/** Read a message from transport into the RX message buffer. */
result_t obex_data_indication(obex_t *self)
{
	obex_common_hdr_t *hdr;
	buf_t *msg;
	int actual;
	unsigned int size;

	DEBUG(4, "\n");

	obex_return_val_if_fail(self != NULL, RESULT_ERROR);

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
			return RESULT_ERROR;
		}
		if (actual == 0)
			return RESULT_TIMEOUT;
	}

	/* If we have 3 bytes data we can decide how big the packet is */
	if (buf_get_length(msg) >= sizeof(*hdr)) {
		hdr = buf_get(msg);
		size = ntohs(hdr->len);

		actual = 0;
		if (buf_get_length(msg) < size) {
			size_t readsize = size - buf_get_length(msg);
			actual = obex_transport_read(self, readsize);

			/* Check if we are still connected */
			/* do not error if the data is from non-empty
			 * but partial buffer (custom transport) */
			if (actual < 0) {
				obex_deliver_event(self, OBEX_EV_LINKERR,
								0, 0, TRUE);
				return RESULT_ERROR;
			}
			if (actual == 0)
				return RESULT_TIMEOUT;
		}
	} else {
		/* Wait until we have at least 3 bytes data */
		DEBUG(3, "Need at least 3 bytes got only %lu!\n",
		      (unsigned long)buf_get_length(msg));
		return RESULT_SUCCESS;
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
		return RESULT_SUCCESS;
	}

	DUMPBUFFER(2, "Rx", msg);

	return RESULT_SUCCESS;
}

/** Remove message from RX message buffer after evaluation */
void obex_data_receive_finished(obex_t *self)
{
	buf_t *msg = self->rx_msg;
	unsigned int size = obex_msg_get_len(self);

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
		self->object->abort = true;

		return 1;
	}
}
