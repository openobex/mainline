/**
	\file irobex.c
	IrOBEX, IrDA transport for OBEX.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 1999 Dag Brattli, All Rights Reserved.
	Copyright (c) 2000 Pontus Fuchs, All Rights Reserved.

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

#include "irda_wrap.h"
#define WSA_VER_MAJOR 2
#define WSA_VER_MINOR 2

#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL WSAEADDRNOTAVAIL
#endif

#else /* _WIN32 */
/* Linux case */

#include <string.h>
#include <unistd.h>
#include <stdio.h>		/* perror */
#include <errno.h>		/* errno and EADDRNOTAVAIL */
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>

#include "irda_wrap.h"

#ifndef AF_IRDA
#define AF_IRDA 23
#endif /* AF_IRDA */
#endif /* _WIN32 */


#include "obex_main.h"
#include "irobex.h"
#include "cloexec.h"
#include "nonblock.h"

struct irobex_data {
	struct sockaddr_irda self;
	struct sockaddr_irda peer;
};

static int irobex_init (obex_t *self)
{
#ifdef _WIN32
	WORD ver = MAKEWORD(WSA_VER_MAJOR,WSA_VER_MINOR);
	WSADATA WSAData;
	if (WSAStartup (ver, &WSAData) != 0) {
		DEBUG(4, "WSAStartup failed (%d)\n",WSAGetLastError());
		return -1;
	}
	if (LOBYTE(WSAData.wVersion) != WSA_VER_MAJOR ||
				HIBYTE(WSAData.wVersion) != WSA_VER_MINOR) {
		DEBUG(4, "WSA version mismatch\n");
		WSACleanup();
		return -1;
	}
#endif

	return 0;
}

static void irobex_cleanup (obex_t *self)
{
	struct irobex_data *data = self->trans.data;

	free(data);
#ifdef _WIN32
	WSACleanup();
#endif
}

/*
 * Function irobex_no_addr (addr)
 *
 *    Check if the address is not valid for connection
 *
 */
static int irobex_no_addr(struct sockaddr_irda *addr)
{
#ifndef _WIN32
	return ((addr->sir_addr == 0x0) || (addr->sir_addr == 0xFFFFFFFF));
#else
	return (((addr->irdaDeviceID[0] == 0x00) &&
		 (addr->irdaDeviceID[1] == 0x00) &&
		 (addr->irdaDeviceID[2] == 0x00) &&
		 (addr->irdaDeviceID[3] == 0x00)) ||
		((addr->irdaDeviceID[0] == 0xFF) &&
		 (addr->irdaDeviceID[1] == 0xFF) &&
		 (addr->irdaDeviceID[2] == 0xFF) &&
		 (addr->irdaDeviceID[3] == 0xFF)));
#endif /* _WIN32 */
}

static int irobex_query_ias (obex_t *self, uint32_t addr, const char* class_name)
{
	struct obex_transport *trans = &self->trans;
	int err;
	struct irda_ias_set ias_query;
	socklen_t len = sizeof(ias_query);

	/* Ask if the requested service exist on this device */
#ifndef _WIN32
	ias_query.daddr = addr;
	strncpy(ias_query.irda_class_name, class_name, IAS_MAX_CLASSNAME);
	strncpy(ias_query.irda_attrib_name, "IrDA:TinyTP:LsapSel", IAS_MAX_ATTRIBNAME);
#else
	ias_query.irdaDeviceID[0] = (addr >> 24) & 0xFF;
	ias_query.irdaDeviceID[1] = (addr >> 16) & 0xFF;
	ias_query.irdaDeviceID[2] = (addr >> 8) & 0xFF;
	ias_query.irdaDeviceID[3] = addr & 0xFF;
	strncpy(ias_query.irdaClassName, class_name, IAS_MAX_CLASSNAME);
	strncpy(ias_query.irdaAttribName, "IrDA:TinyTP:LsapSel", IAS_MAX_ATTRIBNAME);
#endif
	err = getsockopt(trans->fd, SOL_IRLMP, IRLMP_IAS_QUERY, (void*)&ias_query, &len);
	if (err == -1) {
		if (errno != EADDRNOTAVAIL) {
			DEBUG(1, " <can't query IAS>\n");
		} else {
			DEBUG(1, ", doesn't have %s\n", class_name);
		}
		return 0;
	}
	DEBUG(1, ", has service %s\n", class_name);

	return 1;
}

static int irobex_select_interface(obex_t *self, obex_interface_t *intf)
{
	struct irobex_data *data = self->trans.data;

	data->peer.sir_family = AF_IRDA;
	strncpy(data->peer.sir_name, intf->irda.service,
		sizeof(data->peer.sir_name));
	data->self.sir_family = AF_IRDA;
#ifndef _WIN32
	data->peer.sir_lsap_sel = LSAP_ANY;
	data->peer.sir_addr = intf->irda.remote;
	data->self.sir_addr = intf->irda.local;
#else
	data->peer.irdaDeviceID[0] = (intf->irda.remote >> 24) & 0xFF;
	data->peer.irdaDeviceID[1] = (intf->irda.remote >> 16) & 0xFF;
	data->peer.irdaDeviceID[2] = (intf->irda.remote >> 8) & 0xFF;
	data->peer.irdaDeviceID[3] = intf->irda.remote & 0xFF;
	memset(data->self.irdaDeviceID, 0, sizeof(data->self.irdaDeviceID));
#endif

	return 0;
}

static int irobex_set_local_addr(obex_t *self, struct sockaddr *addr, size_t len)
{
	struct irobex_data *data = self->trans.data;
	const struct sockaddr_irda *local = (struct sockaddr_irda *)addr;

	if (len == sizeof(*local) && local->sir_family == AF_IRDA) {
		data->self = *local;
		return 0;
	}

	return -1;
}

static int irobex_set_remote_addr(obex_t *self, struct sockaddr *addr, size_t len)
{
	struct irobex_data *data = self->trans.data;
	const struct sockaddr_irda *remote = (struct sockaddr_irda *)addr;

	if (len == sizeof(*remote) && remote->sir_family == AF_IRDA) {
		data->peer = *remote;
		return 0;
	}

	return -1;
}

/*
 * Function irobex_prepare_connect (self, service)
 *
 *    Prepare for IR-connect
 *
 */
void irobex_prepare_connect(obex_t *self, const char *service)
{
	int fd = obex_transport_sock_create(self, AF_IRDA, 0);
	int i = 0;

	obex_transport_enumerate(self);
	if (self->interfaces_number == 0) {
		DEBUG(1, "No devices in range\n");
		goto out_freesock;
	}

	if (service == NULL)
		service = "OBEX";

	/* Do we want to filter devices based on IAS ? */
	if (self->init_flags & OBEX_FL_FILTERIAS) {
		for (; i < self->interfaces_number; ++i) {
			obex_irda_intf_t *intf = &self->interfaces[i].irda;
			if (irobex_query_ias(self, intf->remote, service))
				break;
		}
		if (i >= self->interfaces_number)
			goto out_freesock;
	}
	self->interfaces[i].irda.service = service;
	irobex_select_interface(self, &self->interfaces[i]);
	self->interfaces[i].irda.service = NULL;

out_freesock:
	obex_transport_sock_delete(self, fd);
}

/*
 * Function irobex_prepare_listen (self, service)
 *
 *    Prepare for IR-listen
 *
 */
void irobex_prepare_listen(obex_t *self, const char *service)
{
	struct irobex_data *data = self->trans.data;

	/* Bind local service */
	data->self.sir_family = AF_IRDA;
#ifndef _WIN32
	data->self.sir_lsap_sel = LSAP_ANY;
#endif /* _WIN32 */

	if (service == NULL)
		service = "OBEX";
	strncpy(data->peer.sir_name, service,
		sizeof(data->peer.sir_name));
}

static void irobex_set_hint_bit(obex_t *self)
{
#ifndef _WIN32
	struct obex_transport *trans = &self->trans;

	/* Hint be we advertise */
	unsigned char hints[4] = {
		HINT_EXTENSION, HINT_OBEX, 0, 0,
	};

	/* Tell the stack about it.
	 * This command is not supported by older kernels,
	 * so ignore any errors!
	 */
	setsockopt(trans->serverfd, SOL_IRLMP, IRLMP_HINTS_SET,
							hints, sizeof(hints));
#else /* _WIN32 */
	/* The registry must be changed to set the hint bit. */
#endif /* _WIN32 */
}

/*
 * Function irobex_listen (self)
 *
 *    Listen for incoming connections.
 *
 */
static int irobex_listen(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	struct irobex_data *data = trans->data;

	DEBUG(3, "\n");

	trans->serverfd = obex_transport_sock_create(self, AF_IRDA, 0);
	if (trans->serverfd == INVALID_SOCKET) {
		DEBUG(0, "Error creating socket\n");
		return -1;
	}

	if (bind(trans->serverfd, (struct sockaddr*) &data->self,
		sizeof(data->self))) {
		DEBUG(0, "Error doing bind\n");
		goto out_freesock;
	}

	irobex_set_hint_bit(self);

	if (listen(trans->serverfd, 1)) {
		DEBUG(0, "Error doing listen\n");
		goto out_freesock;
	}

	DEBUG(4, "We are now listening for connections\n");
	return 1;

out_freesock:
	obex_transport_sock_delete(self, trans->serverfd);
	trans->serverfd = INVALID_SOCKET;
	return -1;
}

static unsigned int irobex_get_mtu(obex_t *self)
{
	struct obex_transport *trans = &self->trans;

#ifndef _WIN32
	int mtu;
	socklen_t len = sizeof(mtu);

	/* Check what the IrLAP data size is */
	if (getsockopt(trans->fd, SOL_IRLMP, IRTTP_MAX_SDU_SIZE,
							(void *) &mtu, &len))
		return 0;
#else
	DWORD mtu;
	int len = sizeof(mtu);

	if (getsockopt(trans->fd, SOL_IRLMP, IRLMP_SEND_PDU_LEN,
							(char *) &mtu, &len))
		return 0;
#endif /* _WIN32 */
	return (int) mtu;
}

/*
 * Function irobex_accept (self)
 *
 *    Accept an incoming connection.
 *
 * Note : don't close the server socket here, so apps may want to continue
 * using it...
 */
static int irobex_accept(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	struct irobex_data *data = self->trans.data;
	struct sockaddr *addr = (struct sockaddr *)&data->peer;
	socklen_t addrlen = sizeof(data->peer);

	// First accept the connection and get the new client socket.
	if (self->init_flags & OBEX_FL_CLOEXEC)
		trans->fd = accept_cloexec(trans->serverfd, addr, &addrlen);
	else
		trans->fd = accept(trans->serverfd, addr, &addrlen);

	if (trans->fd == INVALID_SOCKET)
		return -1;

	trans->mtu = irobex_get_mtu(self);
	DEBUG(3, "transport mtu=%d\n", trans->mtu);
	if (self->init_flags & OBEX_FL_NONBLOCK)
		socket_set_nonblocking(trans->fd);	

	return 1;
}

/*
 * Function irobex_find_interfaces()
 *
 *    Try to discover some remote device(s) that we can connect to
 *
 * Note : we optionally can do a first filtering on the Obex hint bit,
 * and then we can verify that the device does have the requested service...
 * Note : in this function, the memory allocation for the discovery log
 * is done "the right way", so that it's safe and we don't leak memory...
 * Jean II
 */
static int irobex_find_interfaces(obex_t *self, obex_interface_t **interfaces)
{
	struct irda_device_list *list;
	struct irda_device_info *dev;
	unsigned char buf[sizeof(*list) + ((MAX_DEVICES-1) * sizeof(*dev))];
	socklen_t len = sizeof(buf);
	int count = 0;
	socket_t fd = obex_transport_sock_create(self, AF_IRDA, 0);
	int i;
	uint32_t k = 0;

	if (fd == INVALID_SOCKET)
		goto out;

#ifndef _WIN32
	/* Hint bit filtering, if possible */
	if (self->init_flags & OBEX_FL_FILTERHINT) {
		unsigned char hints[4] = {
			HINT_EXTENSION, HINT_OBEX, 0, 0,
		};
		int err;

		/* Set the filter used for performing discovery */
		err = setsockopt(fd, SOL_IRLMP, IRLMP_HINT_MASK_SET,
							hints, sizeof(hints));
		if (err < 0) {
			perror("setsockopt");
			goto out;
		}
	}
#endif

	/* Perform a discovery and get device list */
	if (getsockopt(fd, SOL_IRLMP, IRLMP_ENUMDEVICES, (char *) buf, &len)) {
		DEBUG(1, "Didn't find any devices!\n");
		return 0;
	}

	list = (struct irda_device_list *) buf;
#ifndef _WIN32
	count = (int) list->len;
	dev = list->dev;
#else
	count = (int) list->numDevice;
	dev = list->Device;
#endif
	if (count <= 0)
		goto done;

	*interfaces = calloc(count, sizeof(**interfaces));

	DEBUG(1, "Discovered %u devices:\n", count);
	for (i = 0; i < count; ++i) {
		obex_irda_intf_t *intf = &((*interfaces)+k)->irda;

#ifndef _WIN32
		intf->local = dev[i].saddr;
		intf->remote = dev[i].daddr;
		intf->charset = dev[i].charset;
		/* allocate enough space to make sure the string is
		 * zero-terminated
		 */
		intf->info = calloc(sizeof(dev[i].info)+2, 1);
		if (intf->info)
			memcpy(intf->info, dev[i].info, sizeof(dev[i].info));

		intf->hints[0] = dev[i].hints[0];
		intf->hints[1] = dev[i].hints[1];
#else
		if ((self->init_flags & OBEX_FL_FILTERHINT) &&
				((dev[i].irdaDeviceHints1 & LM_HB_Extension) == 0))
			continue;

		if ((dev[i].irdaDeviceHints2 & 0x20) == 0)
			continue;

		intf->remote = dev[i].irdaDeviceID[3]
					| dev[i].irdaDeviceID[2] << 8
					| dev[i].irdaDeviceID[1] << 16
					| dev[i].irdaDeviceID[0] << 24;
		intf->charset = dev[i].irdaCharSet;
		/* allocate enough space to make sure the
		 * string is zero-terminated */
		intf->info = calloc(sizeof(dev[i].irdaDeviceName)+2, 1);
		if (intf->info)
			memcpy(intf->info, dev[i].irdaDeviceName,
						sizeof(dev[i].irdaDeviceName));
		intf->hints[0] = dev[i].irdaDeviceHints1;
		intf->hints[1] = dev[i].irdaDeviceHints2;
#endif
		++k;
		DEBUG(1, "  [%d] daddr: 0x%08x\n", i+1, intf->remote);
	}

	count = k;

done:
	if (count == 0)
		DEBUG(1, "didn't find any OBEX devices!\n");

out:
	obex_transport_sock_delete(self, fd);
	return count;
}

static void irobex_free_interface(obex_interface_t *intf)
{
	if (intf) {
		if (intf->irda.info) {
			free(intf->irda.info);
			intf->irda.info = NULL;
		}
	}
}

/*
 * Function irobex_irda_connect_request (self)
 *
 *    Open the TTP connection
 *
 */
static int irobex_connect_request(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	struct irobex_data *data = trans->data;
	int ret = -1;

	DEBUG(4, "\n");

	/* Check if the application did supply a valid address. */
	if (irobex_no_addr(&data->peer))
		return -1;

	if (trans->fd  == INVALID_SOCKET) {
		trans->fd = obex_transport_sock_create(self, AF_IRDA, 0);
		if (trans->fd == INVALID_SOCKET)
			return -1;
	}

	ret = connect(trans->fd, (struct sockaddr*) &data->peer,
							sizeof(data->peer));
#if defined(_WIN32)
	if (ret == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
		ret = 0;
#else
	if (ret == -1 && errno == EINPROGRESS)
		ret = 0;
#endif
	if (ret == -1) {
		DEBUG(4, "ret=%d\n", ret);
		obex_transport_sock_delete(self, trans->fd);
		trans->fd = INVALID_SOCKET;
		return ret;
	}

	trans->mtu = irobex_get_mtu(self);
	DEBUG(3, "transport mtu=%d\n", trans->mtu);

	return 1;
}

/*
 * Function irobex_disconnect_request (self)
 *
 *    Shutdown the IrTTP link
 *
 */
static int irobex_disconnect_request(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	int ret;

	DEBUG(4, "\n");

	ret = obex_transport_sock_delete(self, trans->fd);
	if (ret < 0)
		return ret;

	trans->fd = INVALID_SOCKET;

	return ret;
}

/*
 * Function irobex_disconnect_server (self)
 *
 *    Close the server socket
 *
 * Used when we start handling a incomming request, or when the
 * client just want to quit...
 */
static int irobex_disconnect_server(obex_t *self)
{
	struct obex_transport *trans = &self->trans;
	int ret;

	DEBUG(4, "\n");

	ret = obex_transport_sock_delete(self, trans->serverfd);
	trans->serverfd = INVALID_SOCKET;

	return ret;
}

static struct obex_transport_ops irobex_transport_ops = {
	&irobex_init,
	NULL,
	&irobex_cleanup,
	NULL,
	&obex_transport_sock_send,
	&obex_transport_sock_recv,
	&irobex_set_local_addr,
	&irobex_set_remote_addr,
	{
		&irobex_listen,
		&irobex_accept,
		&irobex_disconnect_server,
	},
	{
		&irobex_connect_request,
		&irobex_disconnect_request,
		&irobex_find_interfaces,
		&irobex_free_interface,
		&irobex_select_interface,
	},
};

struct obex_transport * irobex_transport_create(void) {
	struct irobex_data *data = calloc(1, sizeof(*data));
	struct obex_transport *trans;

	if (!data)
		return NULL;

	trans = obex_transport_create(&irobex_transport_ops, data);
	if (!trans)
		free(data);

	return trans;
}
