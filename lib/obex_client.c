/**
	\file obex_client.c
	Handle client operations.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 1999-2000 Pontus Fuchs, All Rights Reserved.

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
#include "obex_client.h"
#include "databuffer.h"

#include <stdlib.h>
#include <stdio.h>

static __inline int msg_get_rsp(const buf_t *msg)
{
	if (!msg)
		return OBEX_RSP_BAD_REQUEST;
	else
		return ((obex_common_hdr_t *)msg->data)->opcode & ~OBEX_FINAL;
}

static __inline uint16_t msg_get_len(const buf_t *msg)
{
	if (msg)
		return ntohs(((obex_common_hdr_t *) msg->data)->len);
	else
		return 0;
}

static int obex_client_abort_transmit(obex_t *self)
{
	int ret = 0;

	DEBUG(4, "STATE: ABORT/TRANSMIT_TX\n");

	ret = obex_object_send_transmit(self, NULL);
	if (ret == -1) {
		int rsp = OBEX_RSP_CONTINUE;

		obex_deliver_event(self, OBEX_EV_LINKERR,
				   self->object->opcode, rsp, TRUE);
		self->state = STATE_IDLE;

	} else if (ret == 1) {
		self->substate = SUBSTATE_RECEIVE_RX;
	}

	return ret;
}

static int obex_client_abort_prepare(obex_t *self)
{
	buf_t *msg = buf_reuse(self->tx_msg);

	DEBUG(4, "STATE: ABORT/PREPARE_TX\n");

	obex_data_request_prepare(self, msg, OBEX_CMD_ABORT);
	self->substate = SUBSTATE_TRANSMIT_TX;
	return obex_client_abort_transmit(self);
}

static int obex_client_abort(obex_t *self)
{
	int ret = 0;
	buf_t *msg = obex_data_receive(self);
	int rsp;
	int event = OBEX_EV_LINKERR;

	DEBUG(4, "STATE: ABORT/RECEIVE_RX\n");

	if (msg == NULL)
		return 0;
	rsp = msg_get_rsp(msg);

	if (rsp == OBEX_RSP_SUCCESS)
		event = OBEX_EV_ABORT;
	obex_deliver_event(self, event, self->object->opcode, rsp, TRUE);
	if (event == OBEX_EV_LINKERR)
		ret = -1;

	self->mode = OBEX_MODE_SERVER;
	self->state = STATE_IDLE;
	return ret;
}

static int obex_client_recv_transmit_tx(obex_t *self)
{
	int ret = 0;
	int rsp = OBEX_RSP_CONTINUE;

	DEBUG(4, "STATE: RECV/TRANSMIT_TX\n");

	ret = obex_object_send_transmit(self, self->object);
	if (ret == -1) {
		obex_deliver_event(self, OBEX_EV_LINKERR,
				   self->object->opcode, rsp, TRUE);
		self->state = STATE_IDLE;

	} else if (ret == 1) {
		obex_deliver_event(self, OBEX_EV_PROGRESS,
				   self->object->opcode, rsp, FALSE);
		self->substate = SUBSTATE_RECEIVE_RX;
	}

	return ret;
}

static int obex_client_recv_prepare_tx(obex_t *self)
{
	DEBUG(4, "STATE: RECV/PREPARE_TX\n");

	/* Sending ABORT is allowed even during SRM */
	if (self->object->abort == 1) {
		self->state = STATE_ABORT;
		return obex_client_abort_prepare(self);
	}

	if (self->object->rsp_mode == OBEX_RSP_MODE_NORMAL ||
	    (self->object->rsp_mode == OBEX_RSP_MODE_SINGLE &&
	     self->srm_flags & OBEX_SRM_FLAG_WAIT_REMOTE))
	{
		int ret;

		ret = obex_object_prepare_send(self, self->object, TRUE, FALSE);
		if (ret == 1) {
			self->substate = SUBSTATE_TRANSMIT_TX;
			return obex_client_recv_transmit_tx(self);

		} else
			return ret;

	} else {
		self->substate = SUBSTATE_RECEIVE_RX;
	}

	return 0;
}

static int obex_client_recv(obex_t *self)
{
	int ret;
	buf_t *msg = obex_data_receive(self);
	int rsp;

	DEBUG(4, "STATE: RECV/RECEIVE_RX\n");

	if (msg == NULL)
		return 0;
	rsp = msg_get_rsp(msg);

	switch (self->object->opcode) {
	case OBEX_CMD_CONNECT:
		/* Response of a CMD_CONNECT needs some special treatment.*/
		DEBUG(2, "We expect a connect-rsp\n");
		if (obex_parse_connect_header(self, msg) < 0) {
			obex_deliver_event(self, OBEX_EV_PARSEERR,
						self->object->opcode, 0, TRUE);
			self->mode = OBEX_MODE_SERVER;
			self->state = STATE_IDLE;
			return -1;
		}
		self->object->headeroffset=4;
		break;

	case OBEX_CMD_DISCONNECT:
		/* So does CMD_DISCONNECT */
		DEBUG(2, "CMD_DISCONNECT done. Resetting MTU!\n");
		self->mtu_tx = OBEX_MINIMUM_MTU;
		self->rsp_mode = OBEX_RSP_MODE_NORMAL;
		self->srm_flags = 0;
		break;
	}

	if (self->object->abort == 0) {
		/* Receive any headers */
		ret = obex_object_receive(self, msg);
		if (ret < 0) {
			obex_deliver_event(self, OBEX_EV_PARSEERR,
						self->object->opcode, 0, TRUE);
			self->mode = OBEX_MODE_SERVER;
			self->state = STATE_IDLE;
			return -1;
		}
	}
	obex_data_receive_finished(self);

	/* Are we done yet? */
	if (rsp == OBEX_RSP_CONTINUE) {
		DEBUG(3, "Continue...\n");
		self->substate = SUBSTATE_PREPARE_TX;
		return obex_client_recv_prepare_tx(self);

	} else {
		/* Notify app that client-operation is done! */
		DEBUG(3, "Done! Rsp=%02x!\n", rsp);
		obex_deliver_event(self, OBEX_EV_REQDONE, self->object->opcode,
								     rsp, TRUE);
		self->mode = OBEX_MODE_SERVER;
		self->state = STATE_IDLE;
		return 0;
	}

}

static int obex_client_send_transmit_tx(obex_t *self)
{
	int ret;

	DEBUG(4, "STATE: SEND/TRANSMIT_TX\n");

	ret = obex_object_send_transmit(self, self->object);
	if (ret < 0) {
		/* Error while sending */
		obex_deliver_event(self, OBEX_EV_LINKERR,
					self->object->opcode, 0, TRUE);
		self->mode = OBEX_MODE_SERVER;
		self->state = STATE_IDLE;

	} else if (ret == 1) {
		obex_deliver_event(self, OBEX_EV_PROGRESS, self->object->opcode,
								      0, FALSE);
		if (obex_object_finished(self, self->object, TRUE)) {
			self->state = STATE_REC;
			self->substate = SUBSTATE_RECEIVE_RX;

		} else if (self->object->rsp_mode == OBEX_RSP_MODE_SINGLE &&
			   !(self->srm_flags & OBEX_SRM_FLAG_WAIT_LOCAL)) {
			/* Still, we need to do a zero-wait check for an
			 * negative response or for connection errors. */
			int check = obex_transport_handle_input(self, 0);
			if (check <= 0) /* no input */
				self->substate = SUBSTATE_RECEIVE_RX;
			else
				self->substate = SUBSTATE_PREPARE_TX;

		} else {
			self->substate = SUBSTATE_RECEIVE_RX;
		}
	}

	return ret;
}

static int obex_client_send_prepare_tx(obex_t *self)
{
	int ret;

	DEBUG(4, "STATE: SEND/PREPARE_TX\n");

	if (self->object->abort == 1) {
		self->state = STATE_ABORT;
		return obex_client_abort_prepare(self);
	}


	ret = obex_object_prepare_send(self, self->object, TRUE, FALSE);
	if (ret == 1) {
		self->substate = SUBSTATE_TRANSMIT_TX;
		return obex_client_send_transmit_tx(self);

	} else
		return ret;
}

static int obex_client_send(obex_t *self)
{
	int ret;
	buf_t *msg = obex_data_receive(self);
	int rsp;

	DEBUG(4, "STATE: SEND/RECEIVE_RX\n");

	if (msg == NULL)
		return 0;
	rsp = msg_get_rsp(msg);

	/* Any errors from peer? Win2k will send RSP_SUCCESS after
	 * every fragment sent so we have to accept that too.*/
	switch (rsp) {
	case OBEX_RSP_SUCCESS:
	case OBEX_RSP_CONTINUE:
		break;

	default:
		DEBUG(0, "STATE_SEND. request not accepted.\n");
		obex_deliver_event(self, OBEX_EV_REQDONE, self->object->opcode,
								     rsp, TRUE);
		/* This is not an Obex error, it is just that the peer
		 * doesn't accept the request, so return 0 - Jean II */
		return 0;
	}

	if (msg_get_len(msg) > 3) {
		/* For Single Response Mode, this is actually not
		 * unexpected. */
		if (self->object->rsp_mode == OBEX_RSP_MODE_NORMAL) {
			DEBUG(1, "STATE_SEND. Didn't excpect data from "
			      "peer (%u)\n", msg_get_len(msg));
			DUMPBUFFER(4, "unexpected data", msg);
			obex_deliver_event(self, OBEX_EV_UNEXPECTED,
						self->object->opcode, 0, FALSE);
		}

		/* At this point, we are in the middle of sending our
		 * request to the server, and it is already sending us
		 * some data! This breaks the whole Request/Response
		 * model! Most often, the server is sending some out of
		 * band progress information for a PUT.
		 * This is the way we will handle that:
		 * Save this header in our Rx header list. We can have
		 * duplicated headers, so no problem. User can check the
		 * header in the next EV_PROGRESS, doing so will hide
		 * the header (until reparse). If not, header will be
		 * parsed at 'final', or just ignored (common case for
		 * PUT).
		 * No headeroffset needed because 'connect' is single
		 * packet (or we deny it). */
		if (!self->object->abort) {
			ret = -1;
			if (self->object->opcode != OBEX_CMD_CONNECT)
				ret = obex_object_receive(self, msg);
			if (ret < 0) {
				obex_deliver_event(self, OBEX_EV_PARSEERR,
						 self->object->opcode, 0, TRUE);
				self->mode = OBEX_MODE_SERVER;
				self->state = STATE_IDLE;
				return -1;
			}
			/* Note : we may want to get rid of received header,
			 * however they are mixed with legitimate headers,
			 * and the user may expect to consult them later.
			 * So, leave them here (== overhead). */
		}
	}

	obex_data_receive_finished(self);
	self->substate = SUBSTATE_PREPARE_TX;
	return obex_client_send_prepare_tx(self);
}


/*
 * Function obex_client ()
 *
 *    Handle client operations
 *
 */
int obex_client(obex_t *self)
{
	DEBUG(4, "\n");

	switch (self->state) {
	case STATE_SEND:
		switch (self->substate) {
		case SUBSTATE_RECEIVE_RX:
			return obex_client_send(self);

		case SUBSTATE_PREPARE_TX:
			return obex_client_send_prepare_tx(self);

		case SUBSTATE_TRANSMIT_TX:
			return obex_client_send_transmit_tx(self);
		}
		break;

	case STATE_REC:
		switch (self->substate) {
		case SUBSTATE_RECEIVE_RX:
			return obex_client_recv(self);

		case SUBSTATE_PREPARE_TX:
			return obex_client_recv_prepare_tx(self);

		case SUBSTATE_TRANSMIT_TX:
			return obex_client_recv_transmit_tx(self);
		}
		break;

	case STATE_ABORT:
		switch (self->substate) {
		case SUBSTATE_RECEIVE_RX:
			return obex_client_abort(self);

		case SUBSTATE_PREPARE_TX:
			return obex_client_abort_prepare(self);

		case SUBSTATE_TRANSMIT_TX:
			return obex_client_abort_transmit(self);
		}
		break;

	default:
		DEBUG(0, "Unknown state\n");
		break;
	}

	return -1;
}
