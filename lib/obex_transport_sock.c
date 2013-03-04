/**
 * @file obex_transport_sock.c
 *
 * OBEX header releated functions.
 * OpenOBEX library - Free implementation of the Object Exchange protocol.
 *
 * Copyright (c) 2012 Hendrik Sattler, All Rights Reserved.
 *
 * OpenOBEX is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with OpenOBEX. If not, see <http://www.gnu.org/>.
 */

#ifdef _WIN32
#include <winsock2.h>
#endif /* _WIN32 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "obex_main.h"
#include "obex_transport.h"
#include "obex_transport_sock.h"
#include "databuffer.h"
#include "cloexec.h"
#include "nonblock.h"

#ifdef _WIN32
#define WSA_VER_MAJOR 2
#define WSA_VER_MINOR 2
#endif
socket_t create_stream_socket(int domain, int proto, unsigned int flags)
{
	int type = SOCK_STREAM;
	socket_t fd;

	if (flags & OBEX_FL_CLOEXEC)
		fd = socket_cloexec(domain, type, proto);
	else
		fd = socket(domain, type, proto);

	if (flags & OBEX_FL_NONBLOCK)
		socket_set_nonblocking(fd);

	return fd;
}

bool close_socket(socket_t fd)
{
	if (fd != INVALID_SOCKET) {
#ifdef _WIN32
		return (closesocket(fd) == 0);
#else /* _WIN32 */
		return (close(fd) == 0);
#endif /* _WIN32 */
	}

	return false;
}

/** Initialize the socket interface */
bool obex_transport_sock_init(void)
{
#ifdef _WIN32
	WORD ver = MAKEWORD(WSA_VER_MAJOR,WSA_VER_MINOR);
	WSADATA WSAData;
	
	if (WSAStartup (ver, &WSAData) != 0) {
		DEBUG(4, "WSAStartup failed (%d)\n", WSAGetLastError());
		return false;
	}
	if (LOBYTE(WSAData.wVersion) != WSA_VER_MAJOR ||
	    HIBYTE(WSAData.wVersion) != WSA_VER_MINOR)
	{
		DEBUG(4, "WSA version mismatch\n");
		WSACleanup();
		return false;
	}
#endif

	return true;
}

/** Deinitialize the socket interface */
void obex_transport_sock_cleanup(void)
{
#ifdef _WIN32
	WSACleanup();
#endif
}

/** Create a stream socket with behavior as selected by the user.
 * Supported behaviours are close-on-exec and non-blocking.
 *
 * @param self the obex instance
 * @param domain the socket domain to use
 * @param proto the socket protocol within domain
 * @return a socket
 */
struct obex_sock * obex_transport_sock_create(int domain, int proto,
					      socklen_t addr_size,
					      unsigned int flags)
{
	struct obex_sock *sock = calloc(1, sizeof(*sock));
#define SAVE_FLAGS (OBEX_FL_CLOEXEC | OBEX_FL_NONBLOCK | OBEX_FL_KEEPSERVER)

	DEBUG(4, "\n");

	if (sock == NULL)
		return NULL;

	sock->domain = domain;
	sock->proto = proto;
	sock->addr_size = addr_size;
	sock->flags = flags & SAVE_FLAGS;
	sock->fd = INVALID_SOCKET;

	return sock;
}

/** Close socket. */
bool obex_transport_sock_disconnect(struct obex_sock *sock)
{
	bool res;

	DEBUG(4, "\n");

	res = close_socket(sock->fd);
	if (res)
		sock->fd = INVALID_SOCKET;

	return res;
}

/** Close socket. */
void obex_transport_sock_destroy(struct obex_sock *sock)
{
	DEBUG(4, "\n");

	obex_transport_sock_disconnect(sock);
	free(sock);
}

socket_t obex_transport_sock_get_fd(struct obex_sock *sock)
{
	return sock->fd;
}

/** Set the local address */
bool obex_transport_sock_set_local(struct obex_sock *sock,
				   const struct sockaddr *addr, socklen_t len)
{
	if (len != sock->addr_size || sock->domain != addr->sa_family)
		return false;

	memcpy(&sock->local, addr, len);
	return true;
}

/** Set the remote address */
bool obex_transport_sock_set_remote(struct obex_sock *sock,
				    const struct sockaddr *addr, socklen_t len)
{
	if (len != sock->addr_size || sock->domain != addr->sa_family)
		return false;

	memcpy(&sock->remote, addr, len);
	return true;
}

/** Send a buffer.
 * It may happen (especially with non-blocking mode) that the buffer is only
 * sent partially.
 *
 * @param sock the socket instance
 * @param msg the message to send
 * @param timeout give up after timeout (in milliseconds)
 * @return -1 on error, else number of sent bytes
 */
ssize_t obex_transport_sock_send(struct obex_sock *sock, struct databuffer *msg,
				 int64_t timeout)
{
	size_t size = buf_get_length(msg);
	socket_t fd = sock->fd;
	ssize_t status;
	fd_set fdset;

	if (size == 0)
		return 0;

	DEBUG(1, "sending %lu bytes\n", (unsigned long)size);

	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);
	if (timeout >= 0) {
		struct timeval time = {(long)(timeout / 1000), (long)(timeout % 1000)};
		status = select((int)fd + 1, NULL, &fdset, NULL, &time);
	} else {
		status = select((int)fd + 1, NULL, &fdset, NULL, NULL);
	}
	if (status == 0)
		return 0;

	/* call send() if no error */
	else if (status > 0)
		status = send(fd, buf_get(msg), size, 0);

	/* The following are not really transport errors. */
	else
#if defined(_WIN32)
	if (status == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
		status = 0;
#else
	if (status == -1 &&
	    (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
		status = 0;
#endif

	return status;
}

/** Connect to a remote server */
bool obex_transport_sock_connect(struct obex_sock *sock)
{
	socket_t fd = sock->fd;
	int ret;

	DEBUG(4, "\n");

	if (fd == INVALID_SOCKET) {
		fd = sock->fd = create_stream_socket(sock->domain, sock->proto,
						     sock->flags);
		if (fd == INVALID_SOCKET) {
			DEBUG(4, "No valid socket: %d\n", errno);
			goto err;
		}
	}

 	if (sock->set_sock_opts)
		if (!sock->set_sock_opts(fd)) {
			DEBUG(4, "Failed to set socket options\n");
			goto err;
		}

	/* Only bind if a local address was specified */
	if (sock->local.ss_family != AF_UNSPEC) {
		ret = bind(fd, (struct sockaddr*)&sock->local, sock->addr_size);
		if (ret < 0) {
			DEBUG(4, "Cannot bind to local address: %d\n", errno);
			goto err;
		}
	}

	ret = connect(fd, (struct sockaddr*) &sock->remote, sock->addr_size);
#if defined(_WIN32)
	if (ret == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
		ret = 0;
#else
	if (ret == -1 && errno == EINPROGRESS)
		ret = 0;
#endif
	if (ret == -1) {
		DEBUG(4, "Connect failed: %d\n", errno);
		goto err;
	}

	return true;

err:
	obex_transport_sock_disconnect(sock);
	return false;
}

bool obex_transport_sock_listen(struct obex_sock *sock)
{
	socket_t fd = sock->fd;
	
	if (fd == INVALID_SOCKET) {
		fd = sock->fd = create_stream_socket(sock->domain, sock->proto,
						     sock->flags);
		if (fd == INVALID_SOCKET) {
			DEBUG(4, "No valid socket: %d\n", errno);
			goto err;
		}
	}

 	if (sock->set_sock_opts)
		if (!sock->set_sock_opts(fd)) {
			DEBUG(4, "Failed to set socket options\n");
			goto err;
		}

	if (bind(fd, (struct sockaddr *)&sock->local, sock->addr_size) == -1) {
		DEBUG(0, "Error doing bind\n");
		goto err;
	}

	if (listen(fd, 1) == -1) {
		DEBUG(0, "Error doing listen\n");
		goto err;
	}

	DEBUG(4, "We are now listening for connections\n");
	return true;

err:
	obex_transport_sock_disconnect(sock);
	return false;
}

/** Accept an incoming client connection
 *
 * @param sock the server socket instance
 * @return the client socket instance
 */
struct obex_sock * obex_transport_sock_accept(struct obex_sock *sock)
{
	socket_t serverfd = sock->fd;
	unsigned int flags = sock->flags;
	socklen_t socklen = sock->addr_size;
	struct obex_sock *client;
	struct sockaddr *addr;

	if (flags & OBEX_FL_KEEPSERVER)
		client = calloc(1, sizeof(*sock));
	else
		client = sock;

	if (client == NULL)
		return NULL;

	client->fd = INVALID_SOCKET;
	client->addr_size = sock->addr_size;
	client->flags = sock->flags;
	addr = (struct sockaddr *) &client->remote;

	// Accept the connection: get socket for the new client
	if (flags & OBEX_FL_CLOEXEC)
		client->fd = accept_cloexec(serverfd, addr, &socklen);
	else
		client->fd = accept(serverfd, addr, &socklen);

	if (client->fd == INVALID_SOCKET)
		goto err_out;

	if (getsockname(client->fd, (struct sockaddr *)&client->local, &socklen)
	    == -1)
	{
		obex_transport_sock_disconnect(client);
		goto err_out;
	}

	if (flags & OBEX_FL_NONBLOCK)
		socket_set_nonblocking(client->fd);

	if (client == sock)
		(void)close_socket(serverfd);

	return client;

err_out:
	if (client != sock)
		free(client);
	return NULL;
}

/** Wait for incoming data/events
 *
 * @param sock the socket instance
 * @param timeout give up after timeout (in milliseconds)
 * @return -1 on failure, 0 on timeout, >0 on success
 */
result_t obex_transport_sock_wait(struct obex_sock *sock, int64_t timeout)
{
	socket_t fd = sock->fd;
	fd_set fdset;
	int ret;

	DEBUG(4, "\n");

	/* Check of we have any fd's to do select on. */
	if (fd == INVALID_SOCKET) {
		DEBUG(0, "No valid socket is open\n");
		return RESULT_ERROR;
	}

	/* Add the fd's to the set. */
	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);

	/* Wait for input */
	if (timeout >= 0) {
		struct timeval time = {(long)(timeout / 1000), (long)(timeout % 1000)};
		ret = select((int)fd + 1, &fdset, NULL, NULL, &time);
	} else {
		ret = select((int)fd + 1, &fdset, NULL, NULL, NULL);
	}

	/* Check if this is a timeout (0) or error (-1) */
	if (ret < 0)
		return RESULT_ERROR;
	else if (ret == 0)
		return RESULT_TIMEOUT;
	else
		return RESULT_SUCCESS;
}

/** Receive into a buffer
 * It may happen that less than requested bytes are received.
 *
 * @param sock the socket instance
 * @param buf the buffer to receive into
 * @param buflen the maximum size to receive
 * @return -1 on error, else number of received bytes
 */
ssize_t obex_transport_sock_recv(struct obex_sock *sock, void *buf, int buflen)
{
	socket_t fd = sock->fd;
	ssize_t status = recv(fd, buf, buflen, 0);

	/* The following are not really transport errors. */
#if defined(_WIN32)
	if (status == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
		status = 0;
#else
	if (status == -1 &&
	    (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
		status = 0;
#endif

	return status;
}
