/**
	\file obex_server.c
	Handle server operations.
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
#include "obex_server.h"
#include "obex_msg.h"
#include "databuffer.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

static __inline enum obex_cmd msg_get_cmd(const obex_t *self)
{
	int opcode = obex_msg_get_opcode(self);

	if (opcode < 0)
		return OBEX_CMD_ABORT;
	else
		return (enum obex_cmd)(opcode & ~OBEX_FINAL);
}

static __inline int msg_get_final(const obex_t *self)
{
	int opcode = obex_msg_get_opcode(self);

	if (opcode < 0)
		return 0;
	else
		return opcode & OBEX_FINAL;
}

static void obex_response_request_prepare(obex_t *self, uint8_t opcode)
{
	obex_data_request_init(self);
	obex_data_request_prepare(self, opcode | OBEX_FINAL);
}

static void obex_response_request(obex_t *self, uint8_t opcode)
{
	obex_return_if_fail(self != NULL);

	obex_response_request_prepare(self, opcode);
	do {
		int status = obex_data_request(self);
		if (status < 0)
			break;
	} while (!buf_get_length(self->tx_msg));
}

static int obex_server_bad_request(obex_t *self)
{
	enum obex_cmd cmd = obex_object_getcmd(self->object);

	obex_response_request(self, OBEX_RSP_BAD_REQUEST);
	self->state = STATE_IDLE;
	obex_deliver_event(self, OBEX_EV_PARSEERR, cmd, 0, TRUE);
	return -1;
}

static int obex_server_abort_transmit(obex_t *self)
{
	int ret = 0;
	enum obex_rsp rsp = OBEX_RSP_CONTINUE;
	enum obex_cmd cmd = OBEX_CMD_ABORT;

	DEBUG(4, "STATE: ABORT/TRANSMIT_TX\n");

	ret = obex_msg_transmit(self);
	if (self->object)
		cmd = obex_object_getcmd(self->object);
	if (ret == -1)
		obex_deliver_event(self, OBEX_EV_LINKERR, cmd, rsp, TRUE);
	else if (ret == 1)
		obex_deliver_event(self, OBEX_EV_ABORT, cmd, rsp, FALSE);

	self->state = STATE_IDLE;
	return ret;
}

static int obex_server_abort_response(obex_t *self)
{
	obex_response_request_prepare(self, OBEX_RSP_SUCCESS);
	self->state = STATE_ABORT;
	self->substate = SUBSTATE_TRANSMIT_TX;
	return obex_server_abort_transmit(self);
}

static int obex_server_abort_prepare(obex_t *self)
{
	int opcode = OBEX_RSP_INTERNAL_SERVER_ERROR;

	DEBUG(4, "STATE: ABORT/PREPARE_TX\n");

	/* Do not send continue */
	if (self->object->rsp != OBEX_RSP_CONTINUE)
		opcode = self->object->lastrsp;
	obex_response_request_prepare(self, opcode);
	self->substate = SUBSTATE_TRANSMIT_TX;
	return obex_server_abort_transmit(self);
}

static int obex_server_send_transmit_tx(obex_t *self)
{
	int ret;
	int cmd = self->object->cmd;

	DEBUG(4, "STATE: SEND/TRANSMIT_TX\n");

	ret = obex_msg_transmit(self);
	if (ret == -1) {
		/* Error sending response */
		obex_deliver_event(self, OBEX_EV_LINKERR, cmd, 0, TRUE);
		self->state = STATE_IDLE;

	} else if (ret == 1) {
		/* Made some progress */
		obex_deliver_event(self, OBEX_EV_PROGRESS, cmd, 0, FALSE);
		if (obex_object_finished(self->object, TRUE)) {
			self->state = STATE_IDLE;
			/* Response sent and object finished! */
			if (cmd == OBEX_CMD_DISCONNECT) {
				DEBUG(2, "CMD_DISCONNECT done. Resetting MTU!\n");
				self->mtu_tx = OBEX_MINIMUM_MTU;
				self->rsp_mode = OBEX_RSP_MODE_NORMAL;
				self->srm_flags = 0;
			}
			obex_deliver_event(self, OBEX_EV_REQDONE, cmd, 0, TRUE);

		} else if (self->object->rsp_mode == OBEX_RSP_MODE_SINGLE &&
			   !(self->srm_flags & OBEX_SRM_FLAG_WAIT_LOCAL)) {
			self->substate = SUBSTATE_PREPARE_TX;

		} else
			self->substate = SUBSTATE_RECEIVE_RX;
	}

	return ret;
}

static int obex_server_send_prepare_tx(obex_t *self)
{
	int ret;

	DEBUG(4, "STATE: SEND/PREPARE_TX\n");

	if (self->object->abort == 1) {
		self->state = STATE_ABORT;
		return obex_server_abort_prepare(self);
	}

	/* As a server, the final bit is always SET, and the "real final" packet
	 * is distinguished by being SUCCESS instead of CONTINUE.
	 * So, force the final bit here. */
	ret = obex_msg_prepare(self, self->object, TRUE);
	if (ret == 1) {
		self->substate = SUBSTATE_TRANSMIT_TX;
		return obex_server_send_transmit_tx(self);

	} else
		return ret;
}

static int obex_server_send(obex_t *self)
{
	enum obex_cmd cmd = msg_get_cmd(self);

	DEBUG(4, "STATE: SEND/RECEIVE_RX\n");

	/* Single response mode makes it possible for the client to send
	 * the next request (e.g. PUT) while still receiving the last
	 * multi-packet response. So we must not consume any request
	 * except ABORT. */
	if (self->object &&
	    self->object->rsp_mode == OBEX_RSP_MODE_SINGLE &&
	    !(cmd == OBEX_CMD_ABORT || cmd == self->object->cmd)) {
		self->substate = SUBSTATE_PREPARE_TX;
		return obex_server_send_prepare_tx(self);
	}

	/* Abort? */
	if (cmd == OBEX_CMD_ABORT) {
		DEBUG(1, "Got OBEX_ABORT request!\n");
		return obex_server_abort_response(self);
	}

	if (!self->object->abort) {
		int ret = obex_msg_receive(self, self->object);
		if (ret < 0)
			return obex_server_bad_request(self);
	}

	obex_data_receive_finished(self);
	self->substate = SUBSTATE_PREPARE_TX;
	return obex_server_send_prepare_tx(self);
}

static int obex_server_recv_transmit_tx(obex_t *self)
{
	int ret = 0;
	int cmd = self->object->cmd;

	DEBUG(4, "STATE: RECV/TRANSMIT_TX\n");

	ret = obex_msg_transmit(self);
	if (ret == -1) {
		obex_deliver_event(self, OBEX_EV_LINKERR, cmd, 0, TRUE);
		self->state = STATE_IDLE;

	} else if (ret == 1) {
		obex_deliver_event(self, OBEX_EV_PROGRESS, cmd, 0, FALSE);
		self->substate = SUBSTATE_RECEIVE_RX;
	}

	return ret;
}

static int obex_server_recv_prepare_tx(obex_t *self)
{
	DEBUG(4, "STATE: RECV/PREPARE_TX\n");

	if (self->object->rsp_mode == OBEX_RSP_MODE_NORMAL ||
	    (self->object->rsp_mode == OBEX_RSP_MODE_SINGLE &&
	     self->srm_flags & OBEX_SRM_FLAG_WAIT_REMOTE))
	{
		int ret;

		if (self->object->abort == 1) {
			self->state = STATE_ABORT;
			return obex_server_abort_prepare(self);
		}

		ret = obex_msg_prepare(self, self->object, FALSE);
		if (ret == 1) {
			self->substate = SUBSTATE_TRANSMIT_TX;
			return obex_server_recv_transmit_tx(self);

		} else
			return ret;

	} else {
		self->substate = SUBSTATE_RECEIVE_RX;
		return 0;
	}
}

static int obex_server_recv(obex_t *self, int first)
{
	int deny = 0;
	uint64_t filter;
	enum obex_cmd cmd;
	int final;

	DEBUG(4, "STATE: RECV/RECEIVE_RX\n");

	if (!obex_msg_rx_status(self))
		return 0;
	cmd = msg_get_cmd(self);
	final = msg_get_final(self);

	/* Abort? */
	if (cmd == OBEX_CMD_ABORT) {
		DEBUG(1, "Got OBEX_ABORT request!\n");
		return obex_server_abort_response(self);
	}

	/* Sanity check */
	if (cmd != self->object->cmd)
		/* The cmd-field of this packet is not the
		 * same as int the first fragment. Bail out! */
		return obex_server_bad_request(self);

	/* Get the non-header data and look at all non-body headers.
	 * Leaving the body headers out here has advantages:
	 * - we don't need to assign a data buffer if the user rejects
	 *   the request
	 * - the user can inspect all first-packet headers before
	 *   deciding about stream mode
	 * - the user application actually received the REQCHECK when
	 *   always using stream mode
	 */
	filter = (1 << OBEX_HDR_ID_BODY | 1 << OBEX_HDR_ID_BODY_END);
	
	/* Some commands needs special treatment (data outside headers) */
	switch (cmd) {
	case OBEX_CMD_CONNECT:
		self->object->headeroffset = 4;
		break;

	case OBEX_CMD_SETPATH:
		self->object->headeroffset = 2;
		break;

	default:
		break;
	}

	if (obex_msg_receive_filtered(self, self->object, filter, TRUE) < 0)
		return obex_server_bad_request(self);

	/* Let the user decide whether to accept or deny a
	 * multi-packet request by examining all headers in
	 * the first packet */
	if (first)
		obex_deliver_event(self, OBEX_EV_REQCHECK, cmd, 0, FALSE);

	/* Everything except 0x1X and 0x2X means that the user
	 * callback denied the request. In the denied cases
	 * treat the last packet as a final one but don't
	 * bother about body headers and don't signal
	 * OBEX_EV_REQ. */
	switch ((self->object->rsp & ~OBEX_FINAL) & 0xF0) {
	case OBEX_RSP_CONTINUE:
	case OBEX_RSP_SUCCESS:
		if (obex_msg_receive_filtered(self, self->object, ~filter,
					      FALSE) < 0)
			return obex_server_bad_request(self);
		break;

	default:
		final = 1;
		deny = 1;
		break;
	}

	obex_data_receive_finished(self);

	/* Connect needs some extra special treatment */
	if (cmd == OBEX_CMD_CONNECT) {
		DEBUG(4, "Got CMD_CONNECT\n");
		if (!final || obex_parse_connectframe(self, self->object) < 0)
			return obex_server_bad_request(self);
	}

	if (!final) {
		self->substate = SUBSTATE_PREPARE_TX;
		return obex_server_recv_prepare_tx(self);

	} else {
		/* Tell the app that a whole request has
		 * arrived. While this event is delivered the
		 * app should append the headers that should be
		 * in the response */
		if (!deny) {
			DEBUG(4, "We got a request!\n");
			obex_deliver_event(self, OBEX_EV_REQ, cmd, 0, FALSE);
		}
		/* More connect-magic woodoo stuff */
		if (cmd == OBEX_CMD_CONNECT)
			obex_insert_connectframe(self, self->object);

		self->state = STATE_SEND;
		self->substate = SUBSTATE_PREPARE_TX;
		return obex_server_send_prepare_tx(self);
	}
}

static int obex_server_idle(obex_t *self)
{
	enum obex_cmd cmd;

	/* Nothing has been recieved yet, so this is probably a new request */
	DEBUG(4, "STATE: IDLE\n");
	
	if (!obex_msg_rx_status(self))
		return 0;
	cmd = msg_get_cmd(self);

	if (self->object) {
		/* What shall we do here? I don't know!*/
		DEBUG(0, "Got a new server-request while already having one!\n");
		obex_response_request(self, OBEX_RSP_INTERNAL_SERVER_ERROR);
		return -1;
	}

	/* If ABORT command is done while we are not handling another command,
	 * we don't need to send a request hint to the application */
	if (cmd == OBEX_CMD_ABORT) {
		DEBUG(1, "Got OBEX_ABORT request!\n");
		obex_data_receive_finished(self);
		return obex_server_abort_response(self);
	}

	self->object = obex_object_new();
	if (self->object == NULL) {
		DEBUG(1, "Allocation of object failed!\n");
		obex_response_request(self, OBEX_RSP_INTERNAL_SERVER_ERROR);
		return -1;
	}
	/* Remember the initial command of the request.*/
	obex_object_setcmd(self->object, cmd);
	self->object->rsp_mode = self->rsp_mode;

	/* Hint app that something is about to come so that
	 * the app can deny a PUT-like request early, or
	 * set the header-offset */
	obex_deliver_event(self, OBEX_EV_REQHINT, cmd, 0, FALSE);

	/* Check the response from the REQHINT event */
	switch ((self->object->rsp & ~OBEX_FINAL) & 0xF0) {
	case OBEX_RSP_CONTINUE:
	case OBEX_RSP_SUCCESS:
		self->state = STATE_REC;
		self->substate = SUBSTATE_RECEIVE_RX;
		return obex_server_recv(self, 1);

	default:
		obex_data_receive_finished(self);
		self->state = STATE_SEND;
		self->substate = SUBSTATE_PREPARE_TX;
		return obex_server_send_prepare_tx(self);
	}
}

/*
 * Function obex_server ()
 *
 *    Handle server-operations
 *
 */
int obex_server(obex_t *self)
{
	DEBUG(4, "\n");

	switch (self->state) {
	case STATE_IDLE:
		return obex_server_idle(self);

	case STATE_REC:
		switch (self->substate) {
		case SUBSTATE_RECEIVE_RX:
			return obex_server_recv(self, 0);

		case SUBSTATE_PREPARE_TX:
			return obex_server_recv_prepare_tx(self);

		case SUBSTATE_TRANSMIT_TX:
			return obex_server_recv_transmit_tx(self);
		}
		break;

	case STATE_SEND:
		switch (self->substate) {
		case SUBSTATE_RECEIVE_RX:
			return obex_server_send(self);

		case SUBSTATE_PREPARE_TX:
			return obex_server_send_prepare_tx(self);

		case SUBSTATE_TRANSMIT_TX:
			return obex_server_send_transmit_tx(self);
		}
		break;

	case STATE_ABORT:
		switch (self->substate) {
		case SUBSTATE_PREPARE_TX:
			return obex_server_abort_prepare(self);

		case SUBSTATE_TRANSMIT_TX:
			return obex_server_abort_transmit(self);

		default:
			break;
		}
		break;

	default:
		DEBUG(0, "Unknown state\n");
		break;
	}

	return -1;
}
