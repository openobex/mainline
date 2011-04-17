/**
	\file cloexec.h
	close-on-exec wrapper functions.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 2010 Hendrik Sattler, All Rights Reserved.

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

#ifndef _WIN32
#include <fcntl.h>
static __inline void fcntl_cloexec(socket_t fd)
{
	if (fd != INVALID_SOCKET)
		(void)fcntl(fd, F_SETFD, FD_CLOEXEC);
}
#else
static __inline void fcntl_cloexec(socket_t fd) { fd = fd; }
#endif

static __inline socket_t socket_cloexec(int domain, int type, int proto)
{
#ifdef SOCK_CLOEXEC
	return socket(domain, type | SOCK_CLOEXEC, proto);
#else
	socket_t fd = socket(domain, type, proto);
	fcntl_cloexec(fd);
	return fd;
#endif
}

static __inline socket_t accept_cloexec(socket_t sockfd, struct sockaddr *addr,
				      socklen_t *addrlen)
{
#ifdef SOCK_CLOEXEC
	return accept4(sockfd, addr, addrlen, SOCK_CLOEXEC);
#else
	socket_t fd = accept(sockfd, addr, addrlen);
	fcntl_cloexec(fd);
	return fd;
#endif
}
