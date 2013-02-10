/**
	\file obex_main.h
	Implementation of the Object Exchange Protocol OBEX.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 1998, 1999, 2000 Dag Brattli, All Rights Reserved.
	Copyright (c) 1999, 2000 Pontus Fuchs, All Rights Reserved.

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

#ifndef OBEX_MAIN_H
#define OBEX_MAIN_H

#include "obex_incl.h"
#include "defines.h"

#include <time.h>

struct databuffer;
struct obex_object;

#include "obex_transport.h"
#include "defines.h"
#include "debug.h"

void obex_library_init(void);

struct obex {
	uint16_t mtu_tx;		/* Maximum OBEX TX packet size */
	uint16_t mtu_rx;		/* Maximum OBEX RX packet size */
	uint16_t mtu_tx_max;		/* Maximum TX we can accept */

	enum obex_state state;
	enum obex_substate substate;
	enum obex_mode mode;
	enum obex_rsp_mode rsp_mode;	/* OBEX_RSP_MODE_* */

	unsigned int init_flags;
	unsigned int srm_flags;		/* Flags for single response mode */

	struct databuffer *tx_msg;	/* Reusable transmit message */
	struct databuffer *rx_msg;	/* Reusable receive message */

	struct obex_object *object;	/* Current object being transfered */
	obex_event_t eventcb;		/* Event-callback */
	enum obex_event abort_event;	/**< event for application when server aborts */

	obex_transport_t *trans;	/* Transport being used */

	obex_interface_t *interfaces;	/* Array of discovered interfaces */
	int interfaces_number;		/* Number of discovered interfaces */

	void * userdata;		/* For user */
};

obex_t * obex_create(obex_event_t eventcb, unsigned int flags);
void obex_destroy(obex_t *self);

/* Common header used by all frames */
#pragma pack(1)
struct obex_common_hdr {
	uint8_t  opcode;
	uint16_t len;
};
#pragma pack()
typedef struct obex_common_hdr obex_common_hdr_t;

void obex_deliver_event(obex_t *self, enum obex_event event, enum obex_cmd cmd,
			enum obex_rsp rsp, bool delete_object);

result_t obex_handle_input(obex_t *self);
result_t obex_work(struct obex *self);
enum obex_data_direction obex_get_data_direction(obex_t *self);
int obex_get_buffer_status(struct databuffer *msg);
int obex_data_indication(struct obex *self);
void obex_data_receive_finished(obex_t *self);

int obex_set_mtu(obex_t *self, uint16_t mtu_rx, uint16_t mtu_tx_max);
bool obex_data_request_init(struct obex *self);
void obex_data_request_prepare(struct obex *self, int opcode);
int obex_cancelrequest(struct obex *self, int nice);

char *obex_response_to_string(int rsp);

#endif
