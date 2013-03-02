/**
	\file inobex.c
	InOBEX, Inet transport for OBEX.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else

#include <sys/types.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#endif /*_WIN32*/

#include "obex_main.h"
#include "inobex.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "obex_transport_sock.h"
#include "cloexec.h"
#include "nonblock.h"

struct inobex_data {
	struct obex_sock *sock;
};

#if 0
static void print_sock_addr(const char *prefix, const struct sockaddr *addr)
{
#ifndef _WIN32
	char atxt[INET6_ADDRSTRLEN];
	const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)addr;

	if (!inet_ntop(AF_INET6, &addr6->sin6_addr, atxt, sizeof(atxt)))
		return;

	DEBUG(2, "%s [%s]:%u\n", prefix, atxt, ntohs(addr6->sin6_port));
#endif
}
#endif

static bool set_sock_opts(socket_t fd)
{
#ifdef IPV6_V6ONLY
	/* Needed for some system that set this IPv6 socket option to
	 * 1 by default (Windows Vista, maybe some BSDs).
	 * Do not check the return code as it may not matter.
	 * You will certainly notice later if it failed.
	 */
	int v6only = 0;
	(void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
			 (void *) &v6only, sizeof(v6only));
#endif
	return true;
}

static void map_ip4to6(struct sockaddr_in *in, struct sockaddr_in6 *out)
{
	out->sin6_family = AF_INET6;
	out->sin6_port = in->sin_port;
	out->sin6_flowinfo = 0;
	out->sin6_scope_id = 0;

	/* default, matches IN6ADDR_ANY */
	memset(out->sin6_addr.s6_addr, 0, sizeof(out->sin6_addr.s6_addr));
	switch (in->sin_addr.s_addr) {
	case INADDR_ANY:
		/* does not work, so use IN6ADDR_ANY
		 * which includes INADDR_ANY
		 */
		break;
	default:
		/* map the IPv4 address to [::FFFF:<ipv4>]
		 * see RFC2373 and RFC2553 for details
		 */
		out->sin6_addr.s6_addr[10] = 0xFF;
		out->sin6_addr.s6_addr[11] = 0xFF;
		out->sin6_addr.s6_addr[12] = (unsigned char)((in->sin_addr.s_addr >>  0) & 0xFF);
		out->sin6_addr.s6_addr[13] = (unsigned char)((in->sin_addr.s_addr >>  8) & 0xFF);
		out->sin6_addr.s6_addr[14] = (unsigned char)((in->sin_addr.s_addr >> 16) & 0xFF);
		out->sin6_addr.s6_addr[15] = (unsigned char)((in->sin_addr.s_addr >> 24) & 0xFF);
		break;
	}
}

static void * inobex_create(void)
{
	return calloc(1, sizeof(struct inobex_data));
}

static bool inobex_init (obex_t *self)
{
	struct inobex_data *data = self->trans->data;
	socklen_t len = sizeof(struct sockaddr_in6);

	if (data == NULL)
		return false;

	data->sock = obex_transport_sock_create(AF_INET6, 0,
						len, self->init_flags);
	if (data->sock == NULL) {
		free(data);
		return false;
	}

	data->sock->set_sock_opts = &set_sock_opts;

	return true;
}

static void inobex_cleanup (obex_t *self)
{
	struct inobex_data *data = self->trans->data;

	if (data->sock)
		obex_transport_sock_destroy(data->sock);
	free(data);	
}

static bool inobex_set_remote_addr(obex_t *self, struct sockaddr *addr, size_t len)
{
	size_t expected_len;

	if (addr->sa_family == AF_INET)
		expected_len = sizeof(struct sockaddr_in);
	else if (addr->sa_family == AF_INET6)
		expected_len = sizeof(struct sockaddr_in6);
	else
		return false;

	if (expected_len != len)
		return false;

	inobex_prepare_connect(self, addr, len);

	return true;
}

static bool inobex_set_local_addr(obex_t *self, struct sockaddr *addr, size_t len)
{
	size_t expected_len;

	if (addr->sa_family == AF_INET)
		expected_len = sizeof(struct sockaddr_in);
	else if (addr->sa_family == AF_INET6)
		expected_len = sizeof(struct sockaddr_in6);
	else
		return false;

	if (expected_len != len)
		return false;

	inobex_prepare_listen(self, addr, len);

	return true;
}

#define OBEX_DEFAULT_PORT 650
static void check_default_port(struct sockaddr_in6 *saddr)
{
	if (saddr->sin6_port == 0)
		saddr->sin6_port = htons(OBEX_DEFAULT_PORT);
}

/*
 * Function inobex_prepare_connect (self, service)
 *
 *    Prepare for INET-connect
 *
 */
void inobex_prepare_connect(obex_t *self, struct sockaddr *saddr, int addrlen)
{
	struct inobex_data *data = self->trans->data;
	struct sockaddr_in6 addr;

	addr.sin6_family   = AF_INET6;
	addr.sin6_port     = 0;
	addr.sin6_flowinfo = 0;
	memcpy(&addr.sin6_addr, &in6addr_loopback, sizeof(addr.sin6_addr));
	addr.sin6_scope_id = 0;

	if (saddr == NULL)
		saddr = (struct sockaddr*)(&addr);
	else
		switch (saddr->sa_family){
		case AF_INET6:
			break;
		case AF_INET:
			map_ip4to6((struct sockaddr_in*)saddr,&addr);
			/* no break */
		default:
			saddr = (struct sockaddr*)(&addr);
			break;
		}

	check_default_port((struct sockaddr_in6 *)saddr);
	obex_transport_sock_set_remote(data->sock, saddr, sizeof(addr));
}

/*
 * Function inobex_prepare_listen (self)
 *
 *    Prepare for INET-listen
 *
 */
void inobex_prepare_listen(obex_t *self, struct sockaddr *saddr, int addrlen)
{
	struct inobex_data *data = self->trans->data;
	struct sockaddr_in6 addr;

	addr.sin6_family   = AF_INET6;
	addr.sin6_port     = 0;
	addr.sin6_flowinfo = 0;
	memcpy(&addr.sin6_addr, &in6addr_any, sizeof(addr.sin6_addr));
	addr.sin6_scope_id = 0;

	/* Bind local service */
	if (saddr == NULL)
		saddr = (struct sockaddr *) &addr;
	else
		switch (saddr->sa_family) {
		case AF_INET6:
			break;
		case AF_INET:
			map_ip4to6((struct sockaddr_in *) saddr, &addr);
			/* no break */
		default:
			saddr = (struct sockaddr *) &addr;
			break;
		}

	check_default_port((struct sockaddr_in6 *)saddr);
	obex_transport_sock_set_local(data->sock, saddr, sizeof(addr));
}

/*
 * Function inobex_listen (self)
 *
 *    Wait for incomming connections
 *
 */
static bool inobex_listen(obex_t *self)
{
	struct inobex_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_listen(data->sock);
}

/*
 * Function inobex_accept (self)
 *
 *    Accept incoming connection.
 *
 * Note : don't close the server socket here, so apps may want to continue
 * using it...
 */
static bool inobex_accept(obex_t *self, const obex_t *server)
{
	struct inobex_data *server_data = server->trans->data;
	struct inobex_data *data = self->trans->data;

	if (data == NULL)
		return false;

	data->sock = obex_transport_sock_accept(server_data->sock);
	if (data->sock == NULL)
		return false;

	return true;
}

/*
 * Function inobex_connect_request (self)
 */
static bool inobex_connect_request(obex_t *self)
{
	struct inobex_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_connect(data->sock);
}

/*
 * Function inobex_transport_disconnect (self)
 *
 *    Shutdown the TCP/IP link
 *
 */
static bool inobex_disconnect(obex_t *self)
{
	struct inobex_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_disconnect(data->sock);
}

static result_t inobex_handle_input(obex_t *self)
{
	struct inobex_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_wait(data->sock, self->trans->timeout);
}

static ssize_t inobex_write(obex_t *self, struct databuffer *msg)
{
	struct obex_transport *trans = self->trans;
	struct inobex_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_send(data->sock, msg, trans->timeout);
}

static ssize_t inobex_read(obex_t *self, void *buf, int buflen)
{
	struct inobex_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_recv(data->sock, buf, buflen);
}

static int inobex_get_fd(obex_t *self)
{
	struct inobex_data *data = self->trans->data;

	return (int)obex_transport_sock_get_fd(data->sock);
}

static struct obex_transport_ops inobex_transport_ops = {
	&inobex_create,
	&inobex_init,
	&inobex_cleanup,

	&inobex_handle_input,
	&inobex_write,
	&inobex_read,
	&inobex_disconnect,

	&inobex_get_fd,
	&inobex_set_local_addr,
	&inobex_set_remote_addr,

	{
		&inobex_listen,
		&inobex_accept,
	},

	{
		&inobex_connect_request,
		NULL,
		NULL,
		NULL,
	},
};

struct obex_transport * inobex_transport_create(void)
{
	return obex_transport_create(&inobex_transport_ops);
}
