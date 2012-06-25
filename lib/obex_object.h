/**
	\file obex_object.h
	OBEX object related functions.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 1999, 2000 Dag Brattli, All Rights Reserved.
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

#ifndef OBEX_OBJECT_H
#define OBEX_OBJECT_H

#include "obex_incl.h"
#include "defines.h"

#if ! defined(_WIN32)
#  include <sys/time.h>
#endif
#include <time.h>


struct databuffer;
struct databuffer_list;

struct obex_object {
	struct databuffer *tx_nonhdr_data;	/* Data before of headers (like CONNECT and SETPATH) */
	struct databuffer_list *tx_headerq;	/* List of headers to transmit*/
	struct obex_hdr_it *tx_it;

	struct databuffer *rx_nonhdr_data;	/* Data before of headers (like CONNECT and SETPATH) */
	struct databuffer_list *rx_headerq;	/* List of received headers */
	struct obex_hdr_it *rx_it;
	struct obex_hdr_it *it;

	enum obex_cmd cmd;		/* command */
	enum obex_rsp rsp;		/* response */
	enum obex_rsp lastrsp;		/* response for last packet */

	uint16_t headeroffset;		/* Where to start parsing headers */
	uint32_t hinted_body_len;	/* Hinted body-length or 0 */
	bool abort;			/* Request shall be aborted */

	enum obex_rsp_mode rsp_mode;	/* OBEX_RSP_MODE_* */

	bool suspended;			/* Temporarily stop transfering object */

	struct obex_hdr *body;		/* The body header need some extra help */
	struct obex_body *body_rcv;	/* Deliver body */
};

struct obex_object *obex_object_new(void);
int obex_object_delete(struct obex_object *object);
size_t obex_object_get_size(obex_object_t *object);
int obex_object_addheader(struct obex *self, struct obex_object *object, uint8_t hi,
			  obex_headerdata_t hv, uint32_t hv_size,
			  unsigned int flags);
int obex_object_getnextheader(struct obex_object *object, uint8_t *hi,
			      obex_headerdata_t *hv, uint32_t *hv_size);
int obex_object_reparseheaders(struct obex_object *object);
void obex_object_setcmd(struct obex_object *object, enum obex_cmd cmd);
enum obex_cmd obex_object_getcmd(const obex_object_t *object);
int obex_object_setrsp(struct obex_object *object, enum obex_rsp rsp,
		       enum obex_rsp lastrsp);
int obex_object_get_opcode(obex_object_t *object, bool allowfinalcmd,
				enum obex_mode mode);
bool obex_object_append_data(obex_object_t *object, struct databuffer *txmsg,
			     size_t tx_left);
int obex_object_finished(obex_object_t *object, bool allowfinal);

int obex_object_receive_nonhdr_data(obex_object_t *object, const void *msgdata,
				    size_t rx_left);
int obex_object_receive_headers(struct obex_object *object, const void *msgdata,
				size_t tx_left, uint64_t filter);

int obex_object_set_body_receiver(obex_object_t *object, struct obex_body *b);
const void * obex_object_read_body(obex_object_t *object, size_t *size);

int obex_object_suspend(struct obex_object *object);
int obex_object_resume(struct obex_object *object);

#endif
