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

#ifdef HAVE_IRDA

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
	struct obex_transport *trans = &self->trans;

	trans->peer.irda.sir_family = AF_IRDA;
	strncpy(trans->peer.irda.sir_name, intf->irda.service,
		sizeof(trans->peer.irda.sir_name));
	trans->self.irda.sir_family = AF_IRDA;
#ifndef _WIN32
	trans->peer.irda.sir_lsap_sel = LSAP_ANY;
	trans->peer.irda.sir_addr = intf->irda.remote;
	trans->self.irda.sir_addr = intf->irda.local;
#else
	trans->peer.irda.irdaDeviceID[0] = (intf->irda.remote >> 24) & 0xFF;
	trans->peer.irda.irdaDeviceID[1] = (intf->irda.remote >> 16) & 0xFF;
	trans->peer.irda.irdaDeviceID[2] = (intf->irda.remote >> 8) & 0xFF;
	trans->peer.irda.irdaDeviceID[3] = intf->irda.remote & 0xFF;
	memset(trans->self.irda.irdaDeviceID, 0, sizeof(trans->self.irda.irdaDeviceID));
#endif

	return 0;
}

/*
 * Function irobex_prepare_connect (self, service)
 *
 *    Prepare for IR-connect
 *
 */
void irobex_prepare_connect(obex_t *self, const char *service)
{
	int fd = obex_create_socket(self, AF_IRDA);
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
	obex_delete_socket(self, fd);
}

/*
 * Function irobex_prepare_listen (self, service)
 *
 *    Prepare for IR-listen
 *
 */
void irobex_prepare_listen(obex_t *self, const char *service)
{
	struct obex_transport *trans = &self->trans;

	/* Bind local service */
	trans->self.irda.sir_family = AF_IRDA;
#ifndef _WIN32
	trans->self.irda.sir_lsap_sel = LSAP_ANY;
#endif /* _WIN32 */

	if (service == NULL)
		service = "OBEX";
	strncpy(trans->peer.irda.sir_name, service,
		sizeof(trans->peer.irda.sir_name));
}

static void irobex_set_hint_bit(obex_t *self)
{
	struct obex_transport *trans = &self->trans;

#ifndef _WIN32
	/* Hint be we advertise */
	unsigned char hints[4] = { 
		HINT_EXTENSION, HINT_OBEX, 0, 0,
	};

	/* Tell the stack about it.
	 * This command is not supported by older kernels,
	 * so ignore any errors!
	 */
	(void)setsockopt(trans->serverfd, SOL_IRLMP, IRLMP_HINTS_SET,
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

	DEBUG(3, "\n");

	trans->serverfd = obex_create_socket(self, AF_IRDA);
	if (trans->serverfd == INVALID_SOCKET) {
		DEBUG(0, "Error creating socket\n");
		return -1;
	}

	if (bind(trans->serverfd, (struct sockaddr*) &trans->self.irda,
		 sizeof(struct sockaddr_irda))) {
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
	obex_delete_socket(self, trans->serverfd);
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

	if (getsockopt(self->fd, SOL_IRLMP, IRLMP_SEND_PDU_LEN,
		       (char *) &mtu, &len))
		return 0;
#endif /* _WIN32 */
	return (int)mtu;
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
	struct sockaddr *addr = (struct sockaddr *)&self->trans.peer.irda;
	socklen_t addrlen = sizeof(struct sockaddr_irda);

	// First accept the connection and get the new client socket.
	if (self->init_flags & OBEX_FL_CLOEXEC)
		trans->fd = accept_cloexec(trans->serverfd, addr, &addrlen);
	else
		trans->fd = accept(trans->serverfd, addr, &addrlen);

	if (trans->fd == INVALID_SOCKET)
		return -1;

	trans->mtu = irobex_get_mtu(self);
	DEBUG(3, "transport mtu=%d\n", trans->mtu);

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
	int fd = obex_create_socket(self, AF_IRDA);

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
		if (err == -1) {
			perror("setsockopt");
			goto out;
		}
	}
#endif

	/* Perform a discovery and get device list */
	if (getsockopt(fd, SOL_IRLMP, IRLMP_ENUMDEVICES, (char *)buf, &len)) {
		DEBUG(1, "Didn't find any devices!\n");
		return 0;
	}

	list = (struct irda_device_list *)buf;
#ifndef _WIN32
	count = (int)list->len;
	dev = list->dev;
#else
	count = (int)list->numDevice;
	dev = list->Device;
#endif
	if (count > 0) {
		int i = 0;
		uint32_t k = 0;
		*interfaces = calloc(count, sizeof(**interfaces));

		DEBUG(1, "Discovered %u devices:\n", count);
		for (; i < count; ++i) {
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
			if (self->init_flags & OBEX_FL_FILTERHINT &&
			    ((dev[i].irdaDeviceHints1 & LM_HB_Extension) == 0 ||
			     (dev[i].irdaDeviceHints2 & 0x20) == 0))
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
	}

	if (count == 0) {
		DEBUG(1, "didn't find any OBEX devices!\n");
	}

out:
	obex_delete_socket(self, fd);
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
	int ret = -1;

	DEBUG(4, "\n");

	/* Check if the application did supply a valid address. */
	if (irobex_no_addr(&trans->peer.irda))
		return -1;

	if (trans->fd  == INVALID_SOCKET) {
		trans->fd = obex_create_socket(self, AF_IRDA);
		if (trans->fd == INVALID_SOCKET)
			return -1;
	}

	ret = connect(trans->fd, (struct sockaddr*) &trans->peer.irda,
		      sizeof(struct sockaddr_irda));
	if (ret < 0) {
		DEBUG(4, "ret=%d\n", ret);
		goto out_freesock;
	}

	trans->mtu = irobex_get_mtu(self);
	DEBUG(3, "transport mtu=%d\n", trans->mtu);

	return 1;

out_freesock:
	obex_delete_socket(self, trans->fd);
	trans->fd = INVALID_SOCKET;
	return ret;
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

	ret = obex_delete_socket(self, trans->fd);
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

	ret = obex_delete_socket(self, trans->serverfd);
	trans->serverfd = INVALID_SOCKET;

	return ret;
}

void irobex_get_ops(struct obex_transport_ops* ops)
{
	ops->init = &irobex_init;
	ops->cleanup = &irobex_cleanup;
	ops->write = &obex_transport_do_send;
	ops->read = &obex_transport_do_recv;
	ops->server.listen = &irobex_listen;
	ops->server.accept = &irobex_accept;
	ops->server.disconnect = &irobex_disconnect_server;
	ops->client.connect = &irobex_connect_request;
	ops->client.disconnect = &irobex_disconnect_request;
	ops->client.find_interfaces = &irobex_find_interfaces;
	ops->client.free_interface = &irobex_free_interface;
	ops->client.select_interface = &irobex_select_interface;
};

#endif /* HAVE_IRDA */
