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

#ifdef HAVE_BLUETOOTH
#include "bluez_compat.h"
#endif /*HAVE_BLUETOOTH*/

#include "obex_main.h"
#include "obex_object.h"
#include "obex_header.h"
#include "obex_server.h"
#include "obex_client.h"
#include "databuffer.h"

#include <openobex/obex_const.h>

int obex_debug;
int obex_dump;

#include "cloexec.h"

/*
 * Function obex_create_socket()
 *
 *    Create socket if needed.
 *
 */
socket_t obex_create_socket(obex_t *self, int domain)
{
	socket_t fd;
	int type = SOCK_STREAM, proto = 0;

	DEBUG(4, "\n");

#ifdef HAVE_BLUETOOTH
	if (domain == AF_BLUETOOTH)
		proto = BTPROTO_RFCOMM;
#endif /*HAVE_BLUETOOTH*/

	if (self->init_flags & OBEX_FL_CLOEXEC)
		fd = socket_cloexec(domain, type, proto);
	else
		fd = socket(domain, type, proto);

	return fd;
}

/*
 * Function obex_delete_socket()
 *
 *    Close socket if opened.
 *
 */
int obex_delete_socket(obex_t *self, socket_t fd)
{
	int ret;

	DEBUG(4, "\n");

	if (fd == INVALID_SOCKET)
		return fd;

#ifdef _WIN32
	ret = closesocket(fd);
#else /* _WIN32 */
	ret = close(fd);
#endif /* _WIN32 */
	return ret;
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
void obex_deliver_event(obex_t *self, int event, int cmd, int rsp, int del)
{
	obex_object_t *object = self->object;

	if (del == TRUE)
		self->object = NULL;

	if (self->mode == MODE_SRV)
		self->eventcb(self, object, OBEX_MODE_SERVER, event, cmd, rsp);
	else
		self->eventcb(self, object, OBEX_MODE_CLIENT, event, cmd, rsp);

	if (del == TRUE)
		obex_object_delete(object);
}

/*
 * Function obex_response_request (self, opcode)
 *
 *    Send a response to peer device
 *
 */
void obex_response_request(obex_t *self, uint8_t opcode)
{
	buf_t *msg;

	obex_return_if_fail(self != NULL);

	msg = buf_reuse(self->tx_msg);
	obex_data_request_prepare(self, msg, opcode | OBEX_FINAL);
	obex_data_request(self, msg);
}

/*
 * Function obex_data_request_prepare (self, opcode, cmd)
 *
 *    Prepare response or command code along with optional headers/data
 *    to send.
 *
 */
void obex_data_request_prepare(obex_t *self, buf_t *msg, int opcode)
{
	obex_common_hdr_t *hdr;

	/* Insert common header */
	hdr = buf_reserve_begin(msg, sizeof(*hdr));

	hdr->opcode = opcode;
	hdr->len = htons((uint16_t) msg->data_size);

	DUMPBUFFER(1, "Tx", msg);
}

/*
 * Function obex_data_request (self, opcode)
 *
 *    Send message.
 *
 */
int obex_data_request(obex_t *self, buf_t *msg)
{
	int status;

	obex_return_val_if_fail(self != NULL, -1);
	obex_return_val_if_fail(msg != NULL, -1);

	DEBUG(1, "len = %lu bytes\n", (unsigned long) msg->data_size);

	do {
		status = obex_transport_write(self, msg);
		if (status > 0)
			buf_remove_begin(msg, status);
	} while (status >= 0 && !buf_empty(msg));
	return status;
}

static int obex_mode(obex_t *self)
{
	switch (self->mode) {
	case MODE_SRV:
		return obex_server(self);

	case MODE_CLI:
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
	int ret;

	/* Waiting for an incoming packet will not work for single response mode
	 * as the client is not supposed to send any when we (as server) are
	 * sending the response.
	 * For request reception, this is handled above */
	if (self->mode == MODE_SRV &&
			self->object &&
			self->object->rsp_mode != OBEX_RSP_MODE_NORMAL &&
			self->state == STATE_SEND &&
			!(self->srm_flags & OBEX_SRM_FLAG_WAIT_LOCAL)) {
		/* Still, we need to do a zero-wait check for an ABORT
		 * and for connection errors. */
		ret = obex_transport_handle_input(self, 0);
		if (ret == 0) /* timeout: no error, no input */
			ret = obex_server_send(self, NULL, self->object->cmd, 0);
		if (ret < 0)
			return -1;
		else
			return sizeof(obex_common_hdr_t);

		ret = obex_transport_handle_input(self, timeout);
		if (ret <= 0)
			return ret;

	} else if (self->mode == MODE_CLI) {
		if (self->substate == SUBSTATE_RECEIVE_RX) {
			ret = obex_transport_handle_input(self, timeout);
			if (ret <= 0)
				return ret;
		}

	}

	return obex_mode(self);
}

/*
 * Check if a message buffer contains at least one full message.
 */
int obex_get_buffer_status(buf_t *msg) {
	obex_common_hdr_t *hdr = (obex_common_hdr_t *)msg->data;

	return (msg->data_size >= sizeof(*hdr) &&
					msg->data_size >= ntohs(hdr->len));
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
	if (msg->data_size < sizeof(*hdr))  {
		actual = obex_transport_read(self, sizeof(*hdr)-msg->data_size);

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
	if (msg->data_size >= sizeof(*hdr)) {
		hdr = (obex_common_hdr_t *) msg->data;
		size = ntohs(hdr->len);

		actual = 0;
		if (msg->data_size < size) {

			actual = obex_transport_read(self,
							size - msg->data_size);
			/* hdr might not be valid anymore if the _read
			 * did a realloc */
			hdr = (obex_common_hdr_t *) msg->data;

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
					(unsigned long) msg->data_size);
		return 0;
        }

	/* New data has been inserted at the end of message */
	DEBUG(1, "Got %d bytes msg len=%lu\n", actual,
					(unsigned long) msg->data_size);

	/*
	 * Make sure that the buffer we have, actually has the specified
	 * number of bytes. If not the frame may have been fragmented, and
	 * we will then need to read more from the socket.
	 */

	/* Make sure we have a whole packet */
	if (size > msg->data_size) {
		DEBUG(3, "Need more data, size=%d, len=%lu!\n",
				size, (unsigned long)msg->data_size);

		/* I'll be back! */
		return 0;
	}

	DUMPBUFFER(2, "Rx", msg);

	return 1;
}

buf_t* obex_data_receive(obex_t *self)
{
	buf_t *msg = self->rx_msg;
	obex_common_hdr_t *hdr = (obex_common_hdr_t *)msg->data;
	uint8_t opcode = hdr->opcode & ~OBEX_FINAL;

	if (self->mode == MODE_SRV) {
		/* Single response mode makes it possible for the client to send
		 * the next request (e.g. PUT) while still receiving the last
		 * multi-packet response. So we must not consume any request
		 * except ABORT. */
		if (self->object &&
				self->object->rsp_mode != OBEX_RSP_MODE_NORMAL &&
				self->state == STATE_SEND &&
				!(opcode == OBEX_CMD_ABORT || opcode == self->object->cmd))
			return NULL;
	}

	if (!obex_get_buffer_status(msg))
		return NULL;

	self->srm_flags &= ~OBEX_SRM_FLAG_WAIT_LOCAL;
	return msg;
}

void obex_data_receive_finished(obex_t *self)
{
	buf_t *msg = self->rx_msg;
	obex_common_hdr_t *hdr = (obex_common_hdr_t *)msg->data;
	unsigned int size = ntohs(hdr->len);

	DEBUG(4, "Pulling %u bytes\n", size);
	buf_remove_begin(msg, size);
	if (msg->data_size == 0)
		buf_reuse(msg);
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
		buf_reuse(self->tx_msg);
		buf_reuse(self->rx_msg);
		/* Since we didn't send ABORT to peer we are out of sync
		 * and need to disconnect transport immediately, so we
		 * signal link error to app */
		obex_deliver_event(self, OBEX_EV_LINKERR, 0, 0, FALSE);
		return 1;
	} else {
		obex_object_t *object;

		object = obex_object_new();
		if (object == NULL)
			return -1;

		if (self->mode == MODE_SRV) {
			/* Do not send continue */
			if (self->object->opcode != OBEX_RSP_CONTINUE)
				obex_object_setrsp(object, self->object->opcode,
							self->object->lastopcode);
			else
				obex_object_setrsp(object,
						OBEX_RSP_INTERNAL_SERVER_ERROR,
						OBEX_RSP_INTERNAL_SERVER_ERROR);
		} else
			obex_object_setcmd(object, OBEX_CMD_ABORT);

		if (obex_object_send(self, object, TRUE, TRUE) < 0) {
			obex_object_delete(object);
			return -1;
		}

		obex_object_delete(object);

		self->object->abort = TRUE;
		self->state = self->mode == MODE_SRV ? STATE_IDLE : STATE_REC;

		if (self->state == STATE_IDLE)
			/* Deliver event will delete the object */
			obex_deliver_event(self, OBEX_EV_ABORT, 0, 0, TRUE);

		return 0;
	}
}
