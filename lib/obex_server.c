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

static result_t obex_server_abort_tx(obex_t *self)
{
	enum obex_cmd cmd = OBEX_CMD_ABORT;

	if (self->object)
		cmd = self->object->cmd;

	obex_deliver_event(self, self->abort_event, cmd, 0, true);
	self->state = STATE_IDLE;

	return RESULT_SUCCESS;
}

static result_t obex_server_abort_tx_prepare(obex_t *self, enum obex_rsp opcode,
					     enum obex_event event)
{
	DEBUG(4, "STATE: ABORT/PREPARE_TX\n");

	self->abort_event = event;
	self->state = STATE_ABORT;
	self->substate = SUBSTATE_TX;

	if (!obex_data_request_init(self))
		return RESULT_ERROR;

	obex_data_request_prepare(self, opcode | OBEX_FINAL);
	return RESULT_SUCCESS;
}

/** Generate response to ABORT request from client */
static result_t obex_server_abort_by_client(obex_t *self)
{
	return obex_server_abort_tx_prepare(self, OBEX_RSP_SUCCESS,
					    OBEX_EV_ABORT);
}

/** Generate response when application has set object_t::abort to true */
static result_t obex_server_abort_by_application(obex_t *self)
{
	/* Use the error code provided by the application... */
	enum obex_rsp opcode = self->object->lastrsp;

	/** ...but do not send continue/success. */
	if (opcode == OBEX_RSP_CONTINUE || opcode == OBEX_RSP_SUCCESS)
		opcode = OBEX_RSP_INTERNAL_SERVER_ERROR;

	return obex_server_abort_tx_prepare(self, opcode, OBEX_EV_ABORT);
}

/** Generate response when we failed to parse the request packet */
static result_t obex_server_bad_request(obex_t *self)
{
	return obex_server_abort_tx_prepare(self, OBEX_RSP_BAD_REQUEST,
					    OBEX_EV_PARSEERR);
}

static result_t obex_server_response_tx_prepare(obex_t *self)
{
	DEBUG(4, "STATE: RESPONSE/PREPARE_TX\n");

	if (self->object->abort)
		return obex_server_abort_by_application(self);

	/* As a server, the final bit is always SET, and the "real final" packet
	 * is distinguished by being SUCCESS instead of CONTINUE.
	 * So, force the final bit here. */
	if (!obex_msg_prepare(self, self->object, TRUE))
		return RESULT_ERROR;

	self->substate = SUBSTATE_TX;
	return RESULT_SUCCESS;
}

static result_t obex_server_response_tx(obex_t *self)
{
	int cmd = self->object->cmd;

	/* Made some progress */
	obex_deliver_event(self, OBEX_EV_PROGRESS, cmd, 0, false);
	if (obex_object_finished(self->object, TRUE)) {
		self->state = STATE_IDLE;
		/* Response sent and object finished! */
		if (cmd == OBEX_CMD_DISCONNECT) {
			DEBUG(2, "CMD_DISCONNECT done. Resetting MTU!\n");
			self->mtu_tx = OBEX_MINIMUM_MTU;
			self->rsp_mode = OBEX_RSP_MODE_NORMAL;
			self->srm_flags = 0;
		}
		obex_deliver_event(self, OBEX_EV_REQDONE, cmd, 0, true);

	} else if (self->object->rsp_mode == OBEX_RSP_MODE_SINGLE &&
		   !(self->srm_flags & OBEX_SRM_FLAG_WAIT_LOCAL))
	{
		self->substate = SUBSTATE_TX_PREPARE;
		return obex_server_response_tx_prepare(self);

	} else {
		self->substate = SUBSTATE_RX;
	}

	return RESULT_SUCCESS;
}

static result_t obex_server_response_rx(obex_t *self)
{
	enum obex_cmd cmd;

	DEBUG(4, "STATE: RESPONSE/RECEIVE_RX\n");

	if (!obex_msg_rx_status(self)) {
		if (self->object->rsp_mode == OBEX_RSP_MODE_SINGLE &&
		    !(self->srm_flags & OBEX_SRM_FLAG_WAIT_LOCAL))
		{
			self->substate = SUBSTATE_TX_PREPARE;
			return obex_server_response_tx_prepare(self);
		}

		return RESULT_SUCCESS;
	}

	/* Single response mode makes it possible for the client to send
	 * the next request (e.g. PUT) while still receiving the last
	 * multi-packet response. So we must not consume any request
	 * except ABORT. For Normal response mode, this other request is an
	 * error. */
	cmd = msg_get_cmd(self);
	if (cmd == OBEX_CMD_ABORT) {
		DEBUG(1, "Got OBEX_ABORT request!\n");
		obex_data_receive_finished(self);
		return obex_server_abort_by_client(self);

	} else if (cmd == obex_object_getcmd(self->object)) {
		int ret = obex_msg_receive(self, self->object);
		obex_data_receive_finished(self);
		if (ret < 0)
			return obex_server_bad_request(self);

		self->substate = SUBSTATE_TX_PREPARE;
		return obex_server_response_tx_prepare(self);

	} else {
		if (self->object->rsp_mode == OBEX_RSP_MODE_SINGLE)
		{
			self->substate = SUBSTATE_TX_PREPARE;
			return obex_server_response_tx_prepare(self);
		}

		obex_data_receive_finished(self);
		return obex_server_bad_request(self);
	}
}

static result_t obex_server_request_tx(obex_t *self)
{
	enum obex_cmd cmd = self->object->cmd;
	enum obex_rsp rsp = self->object->rsp;

	if (rsp == OBEX_RSP_CONTINUE) {
		obex_deliver_event(self, OBEX_EV_PROGRESS, cmd, rsp, false);
		self->substate = SUBSTATE_RX;

	} else {
		obex_deliver_event(self, OBEX_EV_REQDONE, cmd, rsp, true);
		self->state = STATE_IDLE;
	}

	return RESULT_SUCCESS;
}

static result_t obex_server_request_tx_prepare(obex_t *self)
{
	DEBUG(4, "STATE: REQUEST/PREPARE_TX\n");

	if (self->object->rsp_mode == OBEX_RSP_MODE_NORMAL ||
	    (self->object->rsp_mode == OBEX_RSP_MODE_SINGLE &&
	     self->srm_flags & OBEX_SRM_FLAG_WAIT_REMOTE))
	{
		if (self->object->abort)
			return obex_server_abort_by_application(self);

		if (!obex_msg_prepare(self, self->object, FALSE))
			return RESULT_ERROR;

		self->substate = SUBSTATE_TX;
		return RESULT_SUCCESS;

	} else {
		self->substate = SUBSTATE_RX;
		return RESULT_SUCCESS;
	}
}

static result_t obex_server_request_rx(obex_t *self, int first)
{
	int deny = 0;
	uint64_t filter;
	enum obex_cmd cmd;
	int final;

	DEBUG(4, "STATE: REQUEST/RECEIVE_RX\n");

	if (!obex_msg_rx_status(self))
		return RESULT_SUCCESS;
	cmd = msg_get_cmd(self);
	final = msg_get_final(self);

	/* Abort? */
	if (cmd == OBEX_CMD_ABORT) {
		DEBUG(1, "Got OBEX_ABORT request!\n");
		obex_data_receive_finished(self);
		return obex_server_abort_by_client(self);

	} else if (cmd != obex_object_getcmd(self->object)) {
		/* The cmd-field of this packet is not the
		 * same as int the first fragment. Bail out! */
		obex_data_receive_finished(self);
		return obex_server_bad_request(self);
	}

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

	if (obex_msg_receive_filtered(self, self->object, filter, true) < 0) {
		obex_data_receive_finished(self);
		return obex_server_bad_request(self);
	}

	/* Let the user decide whether to accept or deny a
	 * multi-packet request by examining all headers in
	 * the first packet */
	if (first)
		obex_deliver_event(self, OBEX_EV_REQCHECK, cmd, 0, false);

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
		{
			obex_data_receive_finished(self);
			return obex_server_bad_request(self);
		}
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
		self->substate = SUBSTATE_TX_PREPARE;
		return obex_server_request_tx_prepare(self);

	} else {
		/* Tell the app that a whole request has
		 * arrived. While this event is delivered the
		 * app should append the headers that should be
		 * in the response */
		if (!deny) {
			DEBUG(4, "We got a request!\n");
			obex_deliver_event(self, OBEX_EV_REQ, cmd, 0, false);
		}
		/* More connect-magic woodoo stuff */
		if (cmd == OBEX_CMD_CONNECT)
			obex_insert_connectframe(self, self->object);

		self->state = STATE_RESPONSE;
		self->substate = SUBSTATE_TX_PREPARE;
		return obex_server_response_tx_prepare(self);
	}
}

static result_t obex_server_idle(obex_t *self)
{
	enum obex_cmd cmd;

	/* Nothing has been recieved yet, so this is probably a new request */
	DEBUG(4, "STATE: IDLE\n");
	
	if (!obex_msg_rx_status(self))
		return RESULT_SUCCESS;
	cmd = msg_get_cmd(self);

	if (self->object) {
		/* What shall we do here? I don't know!*/
		DEBUG(0, "Got a new server-request while already having one!\n");
		return RESULT_ERROR;
	}

	/* If ABORT command is done while we are not handling another command,
	 * we don't need to send a request hint to the application */
	if (cmd == OBEX_CMD_ABORT) {
		DEBUG(1, "Got OBEX_ABORT request!\n");
		obex_data_receive_finished(self);
		return obex_server_abort_by_client(self);
	}

	self->object = obex_object_new();
	if (self->object == NULL) {
		DEBUG(1, "Allocation of object failed!\n");
		return RESULT_ERROR;
	}
	/* Remember the initial command of the request.*/
	obex_object_setcmd(self->object, cmd);
	self->object->rsp_mode = self->rsp_mode;

	/* Hint app that something is about to come so that
	 * the app can deny a PUT-like request early, or
	 * set the header-offset */
	obex_deliver_event(self, OBEX_EV_REQHINT, cmd, 0, false);

	/* Check the response from the REQHINT event */
	switch ((self->object->rsp & ~OBEX_FINAL) & 0xF0) {
	case OBEX_RSP_CONTINUE:
	case OBEX_RSP_SUCCESS:
		self->state = STATE_REQUEST;
		self->substate = SUBSTATE_RX;
		return obex_server_request_rx(self, 1);

	default:
		obex_data_receive_finished(self);
		self->state = STATE_RESPONSE;
		self->substate = SUBSTATE_TX_PREPARE;
		return obex_server_response_tx_prepare(self);
	}
}

/*
 * Function obex_server ()
 *
 *    Handle server-operations
 *
 */
result_t obex_server(obex_t *self)
{
	DEBUG(4, "\n");

	switch (self->state) {
	case STATE_IDLE:
		return obex_server_idle(self);

	case STATE_REQUEST:
		switch (self->substate) {
		case SUBSTATE_RX:
			return obex_server_request_rx(self, 0);

		case SUBSTATE_TX_PREPARE:
			return obex_server_request_tx_prepare(self);

		case SUBSTATE_TX:
			return obex_server_request_tx(self);

		default:
			break;
		}
		break;

	case STATE_RESPONSE:
		switch (self->substate) {
		case SUBSTATE_RX:
			return obex_server_response_rx(self);

		case SUBSTATE_TX_PREPARE:
			return obex_server_response_tx_prepare(self);

		case SUBSTATE_TX:
			return obex_server_response_tx(self);

		default:
			break;
		}
		break;

	case STATE_ABORT:
		switch (self->substate) {
		case SUBSTATE_TX:
			return obex_server_abort_tx(self);

		default:
			break;
		}
		break;

	default:
		DEBUG(0, "Unknown state\n");
		break;
	}

	return RESULT_ERROR;
}
