/**
	\file nonblock.h
	wrapper functions to enable/disable non-blocking
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

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <fcntl.h>
#endif

static __inline void socket_set_nonblocking(socket_t fd)
{
#if defined(_WIN32)
	unsigned long val = 1;

	(void)ioctlsocket(fd, FIONBIO, &val);

#else
	long status = fcntl(fd, F_GETFL);

	if (status == -1)
		status = 0;

	(void)fcntl(fd, F_SETFL, status | O_NONBLOCK);
#endif
}
