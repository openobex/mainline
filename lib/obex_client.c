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
		return ntohs(((obex_common_hdr_t *)msg->data)->len);
	else
		return 0;
}

static int obex_client_recv(obex_t *self, buf_t *msg, int rsp)
{
	int ret;

	/* Receiving answer of request */
	DEBUG(4, "STATE_REC\n");

	switch (self->object->opcode) {
	case OBEX_CMD_CONNECT:
		/* Response of a CMD_CONNECT needs some special treatment.*/
		DEBUG(2, "We expect a connect-rsp\n");
		if (obex_parse_connect_header(self, msg) < 0) {
			self->mode = MODE_SRV;
			self->state = STATE_IDLE;
			obex_deliver_event(self, OBEX_EV_PARSEERR,
						self->object->opcode, 0, TRUE);
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

	/* Receive any headers */
	ret = obex_object_receive(self, msg);
	if (ret < 0) {
		self->mode = MODE_SRV;
		self->state = STATE_IDLE;
		obex_deliver_event(self, OBEX_EV_PARSEERR,
						self->object->opcode, 0, TRUE);
		return -1;
	}

	/* Are we done yet? */
	if (rsp == OBEX_RSP_CONTINUE) {
		DEBUG(3, "Continue...\n");

		self->object->continue_received = 1;

		if (self->object->abort) {
			DEBUG(3, "Ignoring CONTINUE because request was aborted\n");
			return 0;
		}

		if (self->object->suspend) {
			DEBUG(3, "Not sending new request because transfer is suspended\n");
			return 0;
		}

		if (self->object->rsp_mode == OBEX_RSP_MODE_NORMAL ||
		    (self->srm_flags & OBEX_SRM_FLAG_WAIT_REMOTE))
		{
			ret = obex_object_send(self, self->object, TRUE, FALSE);
			if (ret < 0) {
				obex_deliver_event(self, OBEX_EV_LINKERR,
						   self->object->opcode, rsp,
						   TRUE);
			}
		}
		if (ret >= 0)
			obex_deliver_event(self, OBEX_EV_PROGRESS,
					   self->object->opcode, rsp, FALSE);

		if (self->object)
			self->object->continue_received = 0;

	} else {
		/* Notify app that client-operation is done! */
		DEBUG(3, "Done! Rsp=%02x!\n", rsp);
		self->mode = MODE_SRV;
		self->state = STATE_IDLE;
		if (self->object->abort) {
			if (rsp == OBEX_RSP_SUCCESS)
				obex_deliver_event(self, OBEX_EV_ABORT,
							self->object->opcode,
							rsp, TRUE);
			else
				obex_deliver_event(self, OBEX_EV_LINKERR,
							self->object->opcode,
							rsp, TRUE);

		} else
			obex_deliver_event(self, OBEX_EV_REQDONE,
							self->object->opcode,
							rsp, TRUE);
	}

	return 0;
}

int obex_client_send(obex_t *self, buf_t *msg, int rsp)
{
	int ret;

	/* In progress of sending request */
	DEBUG(4, "STATE_SEND\n");

	if (self->object->first_packet_sent == 1) {
		/* Any errors from peer? Win2k will send RSP_SUCCESS after
		 * every fragment sent so we have to accept that too.*/
		switch (rsp) {
		case OBEX_RSP_SUCCESS:
		case OBEX_RSP_CONTINUE:
			break;

		default:
			DEBUG(0, "STATE_SEND. request not accepted.\n");
			obex_deliver_event(self, OBEX_EV_REQDONE,
					   self->object->opcode, rsp, TRUE);
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
			ret = -1;
			if (self->object->opcode == OBEX_CMD_CONNECT)
				ret = obex_object_receive(self, msg);
			if (ret < 0) {
				self->mode = MODE_SRV;
				self->state = STATE_IDLE;
				obex_deliver_event(self, OBEX_EV_PARSEERR,
						 self->object->opcode, 0, TRUE);
				return -1;
			}

			/* Note : we may want to get rid of received header,
			 * however they are mixed with legitimate headers,
			 * and the user may expect to consult them later.
			 * So, leave them here (== overhead). */
		}
	}

	ret = obex_object_send(self, self->object, TRUE, FALSE);
	if (ret < 0) {
		/* Error while sending */
		obex_deliver_event(self, OBEX_EV_LINKERR,
					self->object->opcode, 0, TRUE);
		self->mode = MODE_SRV;
		self->state = STATE_IDLE;
		return -1;

	}

	self->object->first_packet_sent = 1;
	obex_deliver_event(self, OBEX_EV_PROGRESS, self->object->opcode,
				   0, FALSE);
	if (ret > 0) {
		self->state = STATE_REC;
	}
	return 0;
}


/*
 * Function obex_client ()
 *
 *    Handle client operations
 *
 */
int obex_client(obex_t *self, buf_t *msg, int final)
{
	int rsp = msg_get_rsp(msg);

	DEBUG(4, "\n");

	switch (self->state) {
	case STATE_SEND:
		return obex_client_send(self, msg, rsp);

	case STATE_REC:
		return obex_client_recv(self, msg, rsp);

	default:
		DEBUG(0, "Unknown state\n");
		obex_deliver_event(self, OBEX_EV_PARSEERR, rsp, 0, TRUE);
		return -1;
	}
}
