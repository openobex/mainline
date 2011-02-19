/**
	\file obex_transport.h
	Handle different types of transports.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

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

#ifndef OBEX_TRANSPORT_H
#define OBEX_TRANSPORT_H

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define socket_t SOCKET
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#define socket_t int
#define INVALID_SOCKET -1
#endif

/* forward declaration for all transport includes */
struct obex_transport_ops;

#ifdef HAVE_IRDA
#include "irobex.h"
#endif /*HAVE_IRDA*/
#ifdef HAVE_BLUETOOTH
#include "btobex.h"
#endif /*HAVE_BLUETOOTH*/
#ifdef HAVE_USB
#include "usbobex.h"
#endif /*HAVE_USB*/
#include "inobex.h"
#include "customtrans.h"
#include "fdobex.h"

#include <inttypes.h>

struct obex;
struct databuffer;
#include "databuffer.h"

struct obex_transport_ops {
	int (*init)(obex_t*);
	int (*clone)(obex_t*, const obex_t*);
	void (*cleanup)(obex_t*);

	int (*handle_input)(obex_t*);
	int (*write)(obex_t*, buf_t*);
	int (*read)(obex_t*, void*, int);

	int (*set_local_addr)(obex_t*, struct sockaddr*, size_t);
	int (*set_remote_addr)(obex_t*, struct sockaddr*, size_t);

	struct {
		int (*listen)(obex_t*);
		int (*accept)(obex_t*); /* optional */
		int (*disconnect)(obex_t*);
	} server;

	struct {
		int (*connect)(obex_t*);
		int (*disconnect)(obex_t*);
		int (*find_interfaces)(obex_t*, obex_interface_t**);
		void (*free_interface)(obex_interface_t*);
		int (*select_interface)(obex_t*, obex_interface_t*);
	} client;
};
int obex_transport_standard_handle_input(obex_t *self);
int obex_transport_do_send(obex_t *self, buf_t *msg);
int obex_transport_do_recv(obex_t *self, void *buf, int buflen);

union obex_transport_data {
#ifdef HAVE_IRDA
	struct irobex_data irda;
#endif /*HAVE_IRDA*/
	struct inobex_data inet;
#ifdef HAVE_BLUETOOTH
	struct btobex_data rfcomm;
#endif /*HAVE_BLUETOOTH*/
	obex_ctrans_t custom;
#ifdef HAVE_USB
	struct usbobex_data usb;
#endif /*HAVE_USB*/
	struct fdobex_data fd;
};

typedef struct obex_transport {
	int type;
	struct obex_transport_ops ops;
	union obex_transport_data data;	/* Private data for the transport */

	socket_t fd;		/* Socket descriptor */
	socket_t serverfd;

	int timeout;		/* set timeout */

	int connected;		/* Link connection state */
	unsigned int mtu;	/* Tx MTU of the link */

} obex_transport_t;

int obex_transport_init(obex_t *self, int transport);
void obex_transport_cleanup(obex_t *self);
void obex_transport_clone(obex_t *self, obex_t *old);
void obex_transport_split(obex_t *self, obex_t *server);

int obex_transport_handle_input(struct obex *self, int timeout);
int obex_transport_connect_request(struct obex *self);
void obex_transport_disconnect_request(struct obex *self);
int obex_transport_listen(struct obex *self);
void obex_transport_disconnect_server(struct obex *self);
int obex_transport_write(struct obex *self, struct databuffer *msg);
int obex_transport_read(struct obex *self, int count);
void obex_transport_enumerate(struct obex *self);

int obex_transport_set_local_addr(obex_t *self, struct sockaddr *addr, size_t len);
int obex_transport_set_remote_addr(obex_t *self, struct sockaddr *addr, size_t len);

#endif /* OBEX_TRANSPORT_H */
