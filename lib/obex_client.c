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
#include "obex_connect.h"
#include "obex_client.h"
#include "obex_msg.h"
#include "databuffer.h"

#include <stdlib.h>
#include <stdio.h>

static __inline enum obex_rsp msg_get_rsp(obex_t *self)
{
	int opcode = obex_msg_get_opcode(self);

	if (opcode < 0)
		return OBEX_RSP_BAD_REQUEST;
	else
		return opcode & ~OBEX_FINAL;
}

static __inline uint16_t msg_get_len(const buf_t *msg)
{
	if (msg) {
		obex_common_hdr_t *hdr = buf_get(msg);
		return ntohs(hdr->len);
	} else
		return 0;
}

static int obex_client_abort_transmit(obex_t *self)
{
	int ret = 0;

	DEBUG(4, "STATE: ABORT/TRANSMIT_TX\n");

	ret = obex_msg_transmit(self);
	if (ret == -1) {
		int rsp = OBEX_RSP_CONTINUE;

		obex_deliver_event(self, OBEX_EV_LINKERR,
				   self->object->cmd, rsp, TRUE);
		self->state = STATE_IDLE;

	} else if (ret == 1) {
		self->substate = SUBSTATE_RECEIVE_RX;
	}

	return ret;
}

static int obex_client_abort_prepare(obex_t *self)
{
	DEBUG(4, "STATE: ABORT/PREPARE_TX\n");

	obex_data_request_init(self);
	obex_data_request_prepare(self, OBEX_CMD_ABORT);
	self->substate = SUBSTATE_TRANSMIT_TX;
	return obex_client_abort_transmit(self);
}

static int obex_client_abort(obex_t *self)
{
	int ret = 0;
	enum obex_rsp rsp;
	int event = OBEX_EV_LINKERR;

	DEBUG(4, "STATE: ABORT/RECEIVE_RX\n");

	if (!obex_msg_rx_status(self))
		return 0;
	rsp = msg_get_rsp(self);

	if (rsp == OBEX_RSP_SUCCESS)
		event = OBEX_EV_ABORT;
	obex_deliver_event(self, event, self->object->cmd, rsp, TRUE);
	if (event == OBEX_EV_LINKERR)
		ret = -1;

	self->mode = OBEX_MODE_SERVER;
	self->state = STATE_IDLE;
	return ret;
}

static int obex_client_recv_transmit_tx(obex_t *self)
{
	int ret = 0;
	enum obex_rsp rsp = OBEX_RSP_CONTINUE;

	DEBUG(4, "STATE: RECV/TRANSMIT_TX\n");

	ret = obex_msg_transmit(self);
	if (ret == -1) {
		obex_deliver_event(self, OBEX_EV_LINKERR,
				   self->object->cmd, rsp, TRUE);
		self->state = STATE_IDLE;

	} else if (ret == 1) {
		obex_deliver_event(self, OBEX_EV_PROGRESS,
				   self->object->cmd, rsp, FALSE);
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

		ret = obex_msg_prepare(self, self->object, TRUE);
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
	enum obex_rsp rsp;

	DEBUG(4, "STATE: RECV/RECEIVE_RX\n");

	if (!obex_msg_rx_status(self))
		return 0;
	rsp = msg_get_rsp(self);

	switch (self->object->cmd) {
	case OBEX_CMD_CONNECT:
		DEBUG(2, "We expect a connect-rsp\n");
		self->object->headeroffset=4;
		break;

	case OBEX_CMD_DISCONNECT:
		/* Response of a CMD_DISCONNECT needs some special treatment.*/
		DEBUG(2, "CMD_DISCONNECT done. Resetting MTU!\n");
		self->mtu_tx = OBEX_MINIMUM_MTU;
		self->rsp_mode = OBEX_RSP_MODE_NORMAL;
		self->srm_flags = 0;
		break;

	default:
		break;
	}

	if (self->object->abort == 0) {
		/* Receive any headers */
		ret = obex_msg_receive(self, self->object);
		if (ret < 0) {
			obex_deliver_event(self, OBEX_EV_PARSEERR,
					   self->object->cmd, 0, TRUE);
			self->mode = OBEX_MODE_SERVER;
			self->state = STATE_IDLE;
			obex_data_receive_finished(self);
			return -1;
		}
	}
	obex_data_receive_finished(self);

	/* Response of a CMD_CONNECT needs some special treatment.*/
	if (self->object->cmd == OBEX_CMD_CONNECT) {
		DEBUG(2, "We expect a connect-rsp\n");
		if (rsp != OBEX_RSP_SUCCESS ||
		    obex_parse_connectframe(self, self->object) < 0)
		{
			obex_deliver_event(self, OBEX_EV_PARSEERR,
					   self->object->cmd, 0, TRUE);
			self->mode = OBEX_MODE_SERVER;
			self->state = STATE_IDLE;
			return -1;
		}
	}

	/* Are we done yet? */
	if (rsp == OBEX_RSP_CONTINUE) {
		DEBUG(3, "Continue...\n");
		self->substate = SUBSTATE_PREPARE_TX;
		return obex_client_recv_prepare_tx(self);

	} else {
		/* Notify app that client-operation is done! */
		DEBUG(3, "Done! Rsp=%02x!\n", rsp);
		obex_deliver_event(self, OBEX_EV_REQDONE, self->object->cmd,
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

	ret = obex_msg_transmit(self);
	if (ret < 0) {
		/* Error while sending */
		obex_deliver_event(self, OBEX_EV_LINKERR,
				   self->object->cmd, 0, TRUE);
		self->mode = OBEX_MODE_SERVER;
		self->state = STATE_IDLE;

	} else if (ret == 1) {
		obex_deliver_event(self, OBEX_EV_PROGRESS, self->object->cmd,
								      0, FALSE);
		if (obex_object_finished(self->object, TRUE)) {
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


	ret = obex_msg_prepare(self, self->object, TRUE);
	if (ret == 1) {
		self->substate = SUBSTATE_TRANSMIT_TX;
		return obex_client_send_transmit_tx(self);

	} else
		return ret;
}

static int obex_client_send(obex_t *self)
{
	enum obex_rsp rsp;

	DEBUG(4, "STATE: SEND/RECEIVE_RX\n");

	if (!obex_msg_rx_status(self))
		return 0;
	rsp = msg_get_rsp(self);

	/* Any errors from peer? Win2k will send RSP_SUCCESS after
	 * every fragment sent so we have to accept that too.*/
	switch (rsp) {
	case OBEX_RSP_SUCCESS:
	case OBEX_RSP_CONTINUE:
		break;

	default:
		DEBUG(0, "STATE_SEND. request not accepted.\n");
		obex_deliver_event(self, OBEX_EV_REQDONE, self->object->cmd,
								     rsp, TRUE);
		/* This is not an Obex error, it is just that the peer
		 * doesn't accept the request */
		obex_data_receive_finished(self);
		return 0;
	}

	if (!self->object->abort) {
		int ret = obex_msg_receive(self, self->object);
		if (ret < 0) {
			obex_deliver_event(self, OBEX_EV_PARSEERR,
					   self->object->cmd, 0, TRUE);
			self->mode = OBEX_MODE_SERVER;
			self->state = STATE_IDLE;
			obex_data_receive_finished(self);
			return -1;
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
