
#ifndef _WIN32
#include <fcntl.h>
static __inline void fcntl_cloexec(socket_t fd)
{
	if (fd != INVALID_SOCKET)
		(void)fcntl(fd, F_SETFD, FD_CLOEXEC);
}
#else
#define fcntl_cloexec
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
