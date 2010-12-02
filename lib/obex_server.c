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
#include "obex_header.h"
#include "obex_connect.h"
#include "obex_server.h"
#include "databuffer.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

static __inline int msg_get_cmd(const buf_t *msg)
{
	return ((obex_common_hdr_t *)msg->data)->opcode & ~OBEX_FINAL;
}

static __inline uint16_t msg_get_len(const buf_t *msg)
{
	return ntohs(((obex_common_hdr_t *)msg->data)->len);
}

int obex_server_send(obex_t *self, buf_t *msg, int cmd, uint16_t len)
{
	int ret;

	/* Send back response */
	DEBUG(4, "STATE_SEND\n");

	/* Abort? */
	if (cmd == OBEX_CMD_ABORT) {
		DEBUG(1, "Got OBEX_ABORT request!\n");
		obex_response_request(self, OBEX_RSP_SUCCESS);
		self->state = STATE_IDLE;
		obex_deliver_event(self, OBEX_EV_ABORT, self->object->opcode, cmd, TRUE);
		/* This is not an Obex error, it is just that the peer
		 * aborted the request, so return 0 - Jean II */
		return 0;
	}

	if (len > 3) {
		/* For Single Response Mode, this is actually not unexpected. */
		if (self->object->rsp_mode == OBEX_RSP_MODE_NORMAL) {
			DEBUG(1, "STATE_SEND Didn't expect data from peer"
			      "(%u)\n", len);
			DUMPBUFFER(4, "unexpected data", msg);
			obex_deliver_event(self, OBEX_EV_UNEXPECTED,
					   self->object->opcode, 0, FALSE);
		}

		/* At this point, we are in the middle of sending our response
		 * to the client, and it is still sending us some data!
		 * This break the whole Request/Response model of HTTP!
		 * Most often, the client is sending some out of band progress
		 * information for a GET. This is the way we will handle that:
		 * Save this header in our Rx header list. We can have
		 * duplicated header, so no problem. The user has already parsed
		 * headers, so will most likely ignore those new headers. User
		 * can check the header in the next EV_PROGRESS, doing so will
		 * hide the header (until reparse). If not, header can be parsed
		 * at EV_REQDONE. Don't send any additional event to the app to
		 * not break compatibility and because app can just check this
		 * condition itself.
		 * No headeroffset needed because 'connect' is single packet (or
		 * we deny it). */
		ret = -1;
		if (cmd != OBEX_CMD_CONNECT)
			ret = obex_object_receive(self, msg);
		if (ret < 0) {
			obex_response_request(self, OBEX_RSP_BAD_REQUEST);
			self->state = STATE_IDLE;
			obex_deliver_event(self, OBEX_EV_PARSEERR,
						self->object->opcode, 0, TRUE);
			return -1;
		}

		/* Note: we may want to get rid of received header, however they
		 * are mixed with legitimate headers, and the user may expect to
		 * consult them later. So, leave them here (== overhead). */
	}

	/* As a server, the final bit is always SET, and the "real final" packet
	 * is distinguish by beeing SUCCESS instead of CONTINUE.
	 * So, force the final bit here. */
	self->object->continue_received = 1;

	if (self->object->suspend)
		return 0;

	ret = obex_object_send(self, self->object, TRUE, TRUE);
	if (ret == 0) {
		/* Made some progress */
		obex_deliver_event(self, OBEX_EV_PROGRESS, cmd, 0, FALSE);
		self->object->first_packet_sent = 1;
		self->object->continue_received = 0;

	} else if (ret < 0) {
		/* Error sending response */
		obex_deliver_event(self, OBEX_EV_LINKERR, cmd, 0, TRUE);
		return -1;

	} else {
		/* Response sent! */
		if (cmd == OBEX_CMD_DISCONNECT) {
			DEBUG(2, "CMD_DISCONNECT done. Resetting MTU!\n");
			self->mtu_tx = OBEX_MINIMUM_MTU;
			self->rsp_mode = OBEX_RSP_MODE_NORMAL;
			self->srm_flags = 0;
		}
		self->state = STATE_IDLE;
		obex_deliver_event(self, OBEX_EV_REQDONE, cmd, 0, TRUE);
	}
	return 0;
}

static int obex_server_recv(obex_t *self, buf_t *msg, int final,
							int cmd, uint16_t len)
{
	int deny = 0;
	uint64_t filter;

	DEBUG(4, "STATE_REC\n");
	/* In progress of receiving a request */

	/* Abort? */
	if (cmd == OBEX_CMD_ABORT) {
		DEBUG(1, "Got OBEX_ABORT request!\n");
		obex_response_request(self, OBEX_RSP_SUCCESS);
		self->state = STATE_IDLE;
		obex_deliver_event(self, OBEX_EV_ABORT, self->object->opcode,
								cmd, TRUE);
		/* This is not an Obex error, it is just that the peer
		 * aborted the request, so return 0 - Jean II */
		return 0;
	}

	/* Sanity check */
	if (cmd != self->object->cmd) {
		/* The cmd-field of this packet is not the
		 * same as int the first fragment. Bail out! */
		obex_response_request(self, OBEX_RSP_BAD_REQUEST);
		self->state = STATE_IDLE;
		obex_deliver_event(self, OBEX_EV_PARSEERR,
					self->object->opcode, cmd, TRUE);
		return -1;
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
	if (obex_object_receive_nonhdr_data(self, msg) < 0 ||
			obex_object_receive_headers(self, msg, filter) < 0) {
		obex_response_request(self, OBEX_RSP_BAD_REQUEST);
		self->state = STATE_IDLE;
		obex_deliver_event(self, OBEX_EV_PARSEERR,
						self->object->opcode, 0, TRUE);
		return -1;
	}

	if (!final) {
		/* The REQCHECK event is rather optional. The decision
		 * about the actual support for a command is at
		 * REQHINT. So let's assume that the application wants
		 * that request. It can still deny it but it doesn't
		 * have to ack it. */
		obex_object_setrsp(self->object, OBEX_RSP_CONTINUE,
							OBEX_RSP_SUCCESS);

		/* Let the user decide whether to accept or deny a
		 * multi-packet request by examining all headers in
		 * the first packet */
		if (!self->object->checked) {
			obex_deliver_event(self, OBEX_EV_REQCHECK, cmd,
								0, FALSE);
			self->object->checked = 1;
		}

		/* Everything except 0x1X and 0x2X means that the user
		 * callback denied the request. In the denied cases
		 * treat the last packet as a final one but don't
		 * bother about body headers and don't signal
		 * OBEX_EV_REQ. */
		switch ((self->object->opcode & ~OBEX_FINAL) & 0xF0) {
		case OBEX_RSP_CONTINUE:
		case OBEX_RSP_SUCCESS:
			break;

		default:
			final = 1;
			deny = 1;
			break;
		}
	}

	if (!deny) {
		if (obex_object_receive_headers(self, msg, ~filter) < 0) {
			obex_response_request(self, OBEX_RSP_BAD_REQUEST);
			self->state = STATE_IDLE;
			obex_deliver_event(self, OBEX_EV_PARSEERR,
						self->object->opcode, 0, TRUE);
			return -1;
		}
	}

	if (!final) {
		obex_deliver_event(self, OBEX_EV_PROGRESS, cmd, 0, FALSE);
		if (self->object->rsp_mode == OBEX_RSP_MODE_NORMAL ||
		    (self->srm_flags & OBEX_SRM_FLAG_WAIT_REMOTE))
		{
			int ret = obex_object_send(self, self->object, FALSE,
						   TRUE);
			if (ret < 0) {
				obex_deliver_event(self, OBEX_EV_LINKERR, cmd,
						   0, TRUE);
				return -1;
			}
		}
		return 0;

	} else  {
		if (!self->object->first_packet_sent) {
			/* Tell the app that a whole request has
			 * arrived. While this event is delivered the
			 * app should append the headers that should be
			 * in the response */
			if (!deny) {
				DEBUG(4, "We got a request!\n");
				obex_deliver_event(self, OBEX_EV_REQ, cmd,
								0, FALSE);
			}
			/* More connect-magic woodoo stuff */
			if (cmd == OBEX_CMD_CONNECT)
				obex_insert_connectframe(self, self->object);
			/* Otherwise sanitycheck later will fail */
			len = 3;
		}
		self->state = STATE_SEND;		
		return obex_server_send(self, msg, cmd, len);
	}
}

static int obex_server_idle(obex_t *self, buf_t *msg, int final,
							int cmd, uint16_t len)
{
	/* Nothing has been recieved yet, so this is probably a new request */
	DEBUG(4, "STATE_IDLE\n");

	if (self->object) {
		/* What shall we do here? I don't know!*/
		DEBUG(0, "Got a new server-request while already having one!\n");
		obex_response_request(self, OBEX_RSP_INTERNAL_SERVER_ERROR);
		return -1;
	}

	self->object = obex_object_new();
	if (self->object == NULL) {
		DEBUG(1, "Allocation of object failed!\n");
		obex_response_request(self, OBEX_RSP_INTERNAL_SERVER_ERROR);
		return -1;
	}
	/* Remember the initial command of the request.*/
	self->object->cmd = cmd;
	self->object->rsp_mode = self->rsp_mode;

	/* If ABORT command is done while we are not handling another command,
	 * we don't need to send a request hint to the application */
	if (cmd != OBEX_CMD_ABORT) {
		/* Hint app that something is about to come so that
		 * the app can deny a PUT-like request early, or
		 * set the header-offset */
		obex_deliver_event(self, OBEX_EV_REQHINT, cmd, 0, FALSE);
	}

	/* Some commands needs special treatment (data outside headers) */
	switch (cmd) {
	case OBEX_CMD_CONNECT:
		DEBUG(4, "Got CMD_CONNECT\n");
		/* Connect needs some extra special treatment */
		if (obex_parse_connect_header(self, msg) < 0) {
			obex_response_request(self, OBEX_RSP_BAD_REQUEST);
			obex_deliver_event(self, OBEX_EV_PARSEERR,
						self->object->opcode, 0, TRUE);
			return -1;
		}
		self->object->headeroffset = 4;
		break;

	case OBEX_CMD_SETPATH:
		self->object->headeroffset = 2;
		break;
	}

	self->state = STATE_REC;
	return obex_server_recv(self, msg, final, cmd, len);
}

/*
 * Function obex_server ()
 *
 *    Handle server-operations
 *
 */
int obex_server(obex_t *self, buf_t *msg, int final)
{
	int cmd = msg_get_cmd(msg);
	uint16_t len = msg_get_len(msg);

	DEBUG(4, "\n");

	switch (self->state) {
	case STATE_IDLE:
		return obex_server_idle(self, msg, final, cmd, len);

	case STATE_REC:
		return obex_server_recv(self, msg, final, cmd, len);

	case STATE_SEND:
		return obex_server_send(self, msg, cmd, len);

	default:
		DEBUG(0, "Unknown state\n");
		obex_response_request(self, OBEX_RSP_BAD_REQUEST);
		obex_deliver_event(self, OBEX_EV_PARSEERR, cmd, 0, TRUE);
		return -1;
	}
}
