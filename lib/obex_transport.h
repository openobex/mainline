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
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#endif

/* forward declaration for all transport includes */
struct obex_transport_ops;
struct obex;
struct databuffer;

struct obex_transport_ops {
	bool (*init)(obex_t*);
	void (*cleanup)(obex_t*);

	result_t (*handle_input)(obex_t*);
	ssize_t (*write)(obex_t*, struct databuffer*);
	ssize_t (*read)(obex_t*, void*, int);
	bool (*disconnect)(obex_t*);

	int (*get_fd)(obex_t*);
	bool (*set_local_addr)(obex_t*, struct sockaddr*, size_t);
	bool (*set_remote_addr)(obex_t*, struct sockaddr*, size_t);

	struct {
		bool (*listen)(obex_t*);
		bool (*accept)(obex_t*, const obex_t*);
	} server;

	struct {
		bool (*connect)(obex_t*);
		int (*find_interfaces)(obex_t*, obex_interface_t**);
		void (*free_interface)(obex_interface_t*);
		bool (*select_interface)(obex_t*, obex_interface_t*);
	} client;
};

struct obex_transport * obex_transport_create(struct obex_transport_ops *ops,
					      void *data);

typedef struct obex_transport {
	struct obex_transport_ops *ops;
	void *data;		/* Private data for the transport */

	int timeout;		/* set timeout */
	bool connected;		/* Link connection state */
	unsigned int mtu;	/* Tx MTU of the link */

} obex_transport_t;

bool obex_transport_init(obex_t *self, int transport);
void obex_transport_cleanup(obex_t *self);

bool obex_transport_accept(obex_t *self, const obex_t *server);
result_t obex_transport_handle_input(struct obex *self, int timeout);
bool obex_transport_connect_request(struct obex *self);
void obex_transport_disconnect(struct obex *self);
bool obex_transport_listen(struct obex *self);
ssize_t obex_transport_write(struct obex *self, struct databuffer *msg);
ssize_t obex_transport_read(struct obex *self, int count);
void obex_transport_enumerate(struct obex *self);
int obex_transport_get_fd(struct obex *self);

bool obex_transport_set_local_addr(obex_t *self, struct sockaddr *addr, size_t len);
bool obex_transport_set_remote_addr(obex_t *self, struct sockaddr *addr, size_t len);

#endif /* OBEX_TRANSPORT_H */
