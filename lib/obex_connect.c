/**
	\file obex_connect.c
	Parse and create connect-command.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 2000 Pontus Fuchs, All Rights Reserved.
	Copyright (c) 1999 Dag Brattli, All Rights Reserved.

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
#include "databuffer.h"

#include "obex_connect.h"

#include <stdio.h>
#include <string.h>

/* Connect header */
#pragma pack(1)
struct obex_connect_hdr {
	uint8_t  version;
	uint8_t  flags;
	uint16_t mtu;
};
#pragma pack()

/*
 * Function obex_insert_connectframe ()
 *
 *    Add the data needed to send/reply to a connect
 *
 */
int obex_insert_connectframe(obex_t *self, obex_object_t *object)
{
	struct databuffer *buf = object->tx_nonhdr_data;
	struct obex_connect_hdr *hdr;

	DEBUG(4, "\n");

	if (!buf) {
		buf = object->tx_nonhdr_data = membuf_create(sizeof(*hdr));
		if (!buf)
			return -1;
	} else
		buf_clear(buf, buf_get_length(buf));

	buf_append(buf, NULL, sizeof(*hdr));
	hdr = buf_get(buf);
	hdr->version = OBEX_VERSION;
	hdr->flags = 0x00;              /* Flags */
	hdr->mtu = htons(self->mtu_rx); /* Max packet size */
	return 0;
}

/*
 * Function obex_parse_connect_header ()
 *
 *    Parse a Connect
 *
 */
int obex_parse_connect_header(obex_t *self, buf_t *msg)
{
	obex_common_hdr_t *common_hdr = buf_get(msg);
	struct obex_connect_hdr *conn_hdr =(void *)(common_hdr + 1);

	/* Remember opcode and size for later */
	uint8_t opcode = common_hdr->opcode;
	/* uint16_t length = ntohs(common_hdr->len); */

	DEBUG(4, "\n");

	/* Parse beyond 3 bytes only if response is success */
	if (opcode != (OBEX_RSP_SUCCESS | OBEX_FINAL) &&
				opcode != (OBEX_CMD_CONNECT | OBEX_FINAL))
		return 1;

	DEBUG(4, "Len: %lu\n", (unsigned long)buf_get_length(msg));
	if (buf_get_length(msg) >= sizeof(*common_hdr)+sizeof(*conn_hdr)) {
		/* Get what we need */
		uint16_t mtu = ntohs(conn_hdr->mtu);

		DEBUG(1, "version=%02x\n", conn_hdr->version);

		/* Limit to some reasonable value (usually OBEX_DEFAULT_MTU) */
		if (mtu < self->mtu_tx_max)
			self->mtu_tx = mtu;
		else
			self->mtu_tx = self->mtu_tx_max;

		DEBUG(1, "requested MTU=%u, used MTU=%u\n", mtu, self->mtu_tx);
		return 1;
	}

	DEBUG(1, "Malformed connect-header received\n");
	return -1;
}
