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
#include "obex_transport_sock.h"

struct irobex_data {
	struct obex_sock *sock;
};

static void * irobex_create(void)
{
	return calloc(1, sizeof(struct irobex_data));
}

static bool irobex_init (obex_t *self)
{
	struct irobex_data *data = self->trans->data;
	socklen_t len = sizeof(struct sockaddr_irda);

	if (data == NULL)
		return false;

	data->sock = obex_transport_sock_create(AF_IRDA, 0,
						len, self->init_flags);
	if (data->sock == NULL) {
		free(data);
		return false;
	}

	return true;
}

static void irobex_cleanup (obex_t *self)
{
	struct irobex_data *data = self->trans->data;

	if (data->sock)
		obex_transport_sock_destroy(data->sock);
	free(data);	
}

/*
 * Function irobex_no_addr (addr)
 *
 *    Check if the address is not valid for connection
 *
 */
static bool irobex_no_addr(struct sockaddr_irda *addr)
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

static bool irobex_query_ias(obex_t *self, uint32_t addr, const char* class_name)
{
	int err;
	struct irda_ias_set ias_query;
	socklen_t len = sizeof(ias_query);
	socket_t fd;

	memset(&ias_query, 0, sizeof(ias_query));

	/* Ask if the requested service exist on this device */
#ifndef _WIN32
	ias_query.daddr = addr;
	strncpy(ias_query.irda_class_name, class_name, IAS_MAX_CLASSNAME-1);
	strncpy(ias_query.irda_attrib_name, "IrDA:TinyTP:LsapSel", IAS_MAX_ATTRIBNAME-1);
#else
	ias_query.irdaDeviceID[0] = (addr >> 24) & 0xFF;
	ias_query.irdaDeviceID[1] = (addr >> 16) & 0xFF;
	ias_query.irdaDeviceID[2] = (addr >> 8) & 0xFF;
	ias_query.irdaDeviceID[3] = addr & 0xFF;
	strncpy(ias_query.irdaClassName, class_name, IAS_MAX_CLASSNAME-1);
	strncpy(ias_query.irdaAttribName, "IrDA:TinyTP:LsapSel", IAS_MAX_ATTRIBNAME-1);
#endif

	fd = create_stream_socket(AF_IRDA, 0, OBEX_FL_CLOEXEC);
	if (fd == INVALID_SOCKET)
		return false;
	err = getsockopt(fd, SOL_IRLMP, IRLMP_IAS_QUERY, (void*)&ias_query, &len);
	close_socket(fd);

#if defined(_WIN32)
	if (err == SOCKET_ERROR) {
		if (WSAGetLastError() == WSAECONNREFUSED) {
			DEBUG(1, ", doesn't have %s\n", class_name);
		} else {
			DEBUG(1, " <can't query IAS>\n");
		}
		return false;
	}
#else
	if (err == -1) {
		if (errno == EADDRNOTAVAIL) {
			DEBUG(1, ", doesn't have %s\n", class_name);
		} else {
			DEBUG(1, " <can't query IAS>\n");
		}
		return false;
	}
#endif

	DEBUG(1, ", has service %s\n", class_name);
	return true;
}

static bool irobex_set_local_addr(obex_t *self, struct sockaddr *addr, size_t len)
{
	struct irobex_data *data = self->trans->data;

	return obex_transport_sock_set_local(data->sock, addr, len);
}

static bool irobex_set_remote_addr(obex_t *self, struct sockaddr *addr, size_t len)
{
	struct irobex_data *data = self->trans->data;

	return obex_transport_sock_set_remote(data->sock, addr, len);
}

static bool irobex_select_interface(obex_t *self, obex_interface_t *intf)
{
	struct sockaddr_irda addr;

	/* local address */
	memset(&addr, 0, sizeof(addr));
	addr.sir_family = AF_IRDA;
	addr.sir_lsap_sel = LSAP_ANY;
#ifndef _WIN32
	addr.sir_addr = intf->irda.local;
#else
	memset(addr.irdaDeviceID, 0, sizeof(addr.irdaDeviceID));
#endif
	if (!irobex_set_local_addr(self, (struct sockaddr *)&addr, sizeof(addr)))
		return false;

	/* remote address */
	memset(&addr, 0, sizeof(addr));
	addr.sir_family = AF_IRDA;
	strncpy(addr.sir_name, intf->irda.service, sizeof(addr.sir_name)-1);
#ifndef _WIN32
	addr.sir_lsap_sel = LSAP_ANY;
	addr.sir_addr = intf->irda.remote;
#else
	addr.irdaDeviceID[0] = (intf->irda.remote >> 24) & 0xFF;
	addr.irdaDeviceID[1] = (intf->irda.remote >> 16) & 0xFF;
	addr.irdaDeviceID[2] = (intf->irda.remote >> 8) & 0xFF;
	addr.irdaDeviceID[3] = intf->irda.remote & 0xFF;
#endif
	if (!irobex_set_remote_addr(self, (struct sockaddr *)&addr, sizeof(addr)))
		return false;

	return true;
}

/*
 * Function irobex_prepare_connect (self, service)
 *
 *    Prepare for IR-connect
 *
 */
void irobex_prepare_connect(obex_t *self, const char *service)
{
	int i = 0;

	obex_transport_enumerate(self);
	if (self->interfaces_number == 0) {
		DEBUG(1, "No devices in range\n");
		return;
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
			return;
	}
	self->interfaces[i].irda.service = service;
	irobex_select_interface(self, &self->interfaces[i]);
	self->interfaces[i].irda.service = NULL;
}

/*
 * Function irobex_prepare_listen (self, service)
 *
 *    Prepare for IR-listen
 *
 */
void irobex_prepare_listen(obex_t *self, const char *service)
{
	struct sockaddr_irda addr;

	/* Bind local service */
	addr.sir_family = AF_IRDA;
#ifndef _WIN32
	addr.sir_lsap_sel = LSAP_ANY;
#endif /* _WIN32 */

	if (service == NULL)
		service = "OBEX";
	strncpy(addr.sir_name, service,	sizeof(addr.sir_name));
	irobex_set_local_addr(self, (struct sockaddr *)&addr, sizeof(addr));
}

static bool set_listen_sock_opts(socket_t fd)
{
#ifndef _WIN32
	/* Hint be we advertise */
	unsigned char hints[4] = {
		HINT_EXTENSION, HINT_OBEX, 0, 0,
	};

	/* Tell the stack about it.
	 * This command is not supported by older kernels,
	 * so ignore any errors!
	 */
	(void)setsockopt(fd, SOL_IRLMP, IRLMP_HINTS_SET, hints, sizeof(hints));

#else /* _WIN32 */
	/* The registry must be changed to set the hint bit. */
#endif /* _WIN32 */

	return true;
}

/*
 * Function irobex_listen (self)
 *
 *    Listen for incoming connections.
 *
 */
static bool irobex_listen(obex_t *self)
{
	struct irobex_data *data = self->trans->data;

	DEBUG(4, "\n");

	data->sock->set_sock_opts = &set_listen_sock_opts;

	return obex_transport_sock_listen(data->sock);
}

/*
 * Function irobex_accept (self)
 *
 *    Accept an incoming connection.
 *
 * Note : don't close the server socket here, so apps may want to continue
 * using it...
 */
static bool irobex_accept(obex_t *self, const obex_t *server)
{
	struct irobex_data *server_data = server->trans->data;
	struct irobex_data *data = self->trans->data;

	if (data == NULL)
		return false;

	data->sock = obex_transport_sock_accept(server_data->sock);
	if (data->sock == NULL)
		return false;

	return true;
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
	socket_t fd = create_stream_socket(AF_IRDA, 0, OBEX_FL_CLOEXEC);
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
	if (getsockopt(fd, SOL_IRLMP, IRLMP_ENUMDEVICES, (char *) buf, &len))
		goto done;

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
	close_socket(fd);
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
static bool irobex_connect_request(obex_t *self)
{
	struct irobex_data *data = self->trans->data;

	DEBUG(4, "\n");

	/* Check if the application did supply a valid address. */
	if (irobex_no_addr((struct sockaddr_irda *)&data->sock->remote))
		return false;

	return obex_transport_sock_connect(data->sock);
}

/*
 * Function irobex_disconnect (self)
 *
 *    Shutdown the IrTTP link
 *
 */
static bool irobex_disconnect(obex_t *self)
{
	struct irobex_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_disconnect(data->sock);
}

static result_t irobex_handle_input(obex_t *self)
{
	struct irobex_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_wait(data->sock, self->trans->timeout);
}

static ssize_t irobex_write(obex_t *self, struct databuffer *msg)
{
	struct obex_transport *trans = self->trans;
	struct irobex_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_send(data->sock, msg, trans->timeout);
}

static ssize_t irobex_read(obex_t *self, void *buf, int buflen)
{
	struct irobex_data *data = self->trans->data;

	DEBUG(4, "\n");

	return obex_transport_sock_recv(data->sock, buf, buflen);
}

static int irobex_get_fd(obex_t *self)
{
	struct irobex_data *data = self->trans->data;

	return (int)obex_transport_sock_get_fd(data->sock);
}

static struct obex_transport_ops irobex_transport_ops = {
	&irobex_create,
	&irobex_init,
	&irobex_cleanup,

	&irobex_handle_input,
	&irobex_write,
	&irobex_read,
	&irobex_disconnect,

	&irobex_get_fd,
	&irobex_set_local_addr,
	&irobex_set_remote_addr,
	{
		&irobex_listen,
		&irobex_accept,
	},
	{
		&irobex_connect_request,
		&irobex_find_interfaces,
		&irobex_free_interface,
		&irobex_select_interface,
	},
};

struct obex_transport * irobex_transport_create(void)
{
	return obex_transport_create(&irobex_transport_ops);
}
