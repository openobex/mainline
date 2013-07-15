/**
	\file usbobex.c
	USB OBEX, USB transport for OBEX.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 2005 Alex Kanavin, All Rights Reserved.

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
#include <errno.h>		/* errno and EADDRNOTAVAIL */

#if defined HAVE_USB && !defined HAVE_USB1

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#ifndef ETIMEDOUT
#define ETIMEDOUT WSAETIMEDOUT
#endif

#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <string.h>
#include <unistd.h>
#include <stdio.h>		/* perror */
#include <limits.h>
#include <usb.h>

#include "obex_main.h"
#include "usbobex.h"
#include "usbutils.h"
#include "databuffer.h"

static void * usbobex_create (void)
{
	return calloc(1, sizeof(struct usbobex_data));
}

static void usbobex_cleanup (obex_t *self)
{
	struct usbobex_data *data = self->trans->data;

	free(data);
}

/*
 * Function usbobex_select_interface (self, interface)
 *
 *    Prepare for USB OBEX connect
 *
 */
static bool usbobex_select_interface(obex_t *self, obex_interface_t *intf)
{
	struct usbobex_data *data = self->trans->data;

	obex_return_val_if_fail(intf->usb.intf != NULL, false);
	data->self = *intf->usb.intf;
	return true;
}

/*
 * Helper function to usbobex_find_interfaces
 */
static void find_eps(struct obex_usb_intf_transport_t *intf,
			struct usb_interface_descriptor data_intf,
			int *found_active, int *found_idle)
{
	if (data_intf.bNumEndpoints == 2) {
		struct usb_endpoint_descriptor *ep0, *ep1;

		ep0 = data_intf.endpoint;
		ep1 = data_intf.endpoint + 1;

		if ((ep0->bEndpointAddress & USB_ENDPOINT_IN) &&
		    ((ep0->bmAttributes & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_BULK) &&
		    !(ep1->bEndpointAddress & USB_ENDPOINT_IN) &&
		    ((ep1->bmAttributes & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_BULK)) {
			*found_active = 1;
			intf->data_active_setting = data_intf.bAlternateSetting;
			intf->data_interface_active_description = data_intf.iInterface;
			intf->data_endpoint_read = ep0->bEndpointAddress;
			intf->data_endpoint_write = ep1->bEndpointAddress;
		}
		if (!(ep0->bEndpointAddress & USB_ENDPOINT_IN) &&
		    ((ep0->bmAttributes & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_BULK) &&
		    (ep1->bEndpointAddress & USB_ENDPOINT_IN) &&
		    ((ep1->bmAttributes & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_BULK)) {
			*found_active = 1;
			intf->data_active_setting = data_intf.bAlternateSetting;
			intf->data_interface_active_description = data_intf.iInterface;
			intf->data_endpoint_read = ep1->bEndpointAddress;
			intf->data_endpoint_write = ep0->bEndpointAddress;
		}
	}

	if (data_intf.bNumEndpoints == 0) {
		*found_idle = 1;
		intf->data_idle_setting = data_intf.bAlternateSetting;
		intf->data_interface_idle_description = data_intf.iInterface;
	}
}

/*
 * Helper function to usbobex_find_interfaces
 */
static int find_obex_data_interface(unsigned char *buffer, int buflen,
					struct usb_config_descriptor config,
					struct obex_usb_intf_transport_t *intf)
{
	struct cdc_union_desc *union_header = NULL;
	int i, a;
	int found_active = 0;
	int found_idle = 0;

	if (!buffer) {
		DEBUG(2,"Weird descriptor references");
		return -EINVAL;
	}

	while (buflen > 0) {
		if (buffer [1] != USB_DT_CS_INTERFACE) {
			DEBUG(2,"skipping garbage");
			goto next_desc;
		}
		switch (buffer [2]) {
		case CDC_UNION_TYPE: /* we've found it */
			if (union_header) {
				DEBUG(2,"More than one union descriptor, skiping ...");
				goto next_desc;
			}
			union_header = (struct cdc_union_desc *)buffer;
			break;
		case CDC_OBEX_TYPE: /* maybe check version */
		case CDC_OBEX_SERVICE_ID_TYPE: /* This one is handled later */
		case CDC_HEADER_TYPE:
			break; /* for now we ignore it */
		default:
			DEBUG(2, "Ignoring extra header, type %d, length %d", buffer[2], buffer[0]);
			break;
		}
next_desc:
		buflen -= buffer[0];
		buffer += buffer[0];
	}

	if (!union_header) {
		DEBUG(2,"No union descriptor, giving up\n");
		return -ENODEV;
	}
	/* Found the slave interface, now find active/idle settings and endpoints */
	intf->data_interface = union_header->bSlaveInterface0;
	/* Loop through all of the interfaces */
	for (i = 0; i < config.bNumInterfaces; i++) {
		/* Loop through all of the alternate settings */
		for (a = 0; a < config.interface[i].num_altsetting; a++) {
			/* Check if this interface is OBEX data interface*/
			/* and find endpoints */
			if (config.interface[i].altsetting[a].bInterfaceNumber == intf->data_interface)
				find_eps(intf, config.interface[i].altsetting[a], &found_active, &found_idle);
		}
	}
	if (!found_idle) {
		DEBUG(2,"No idle setting\n");
		return -ENODEV;
	}
	if (!found_active) {
		DEBUG(2,"No active setting\n");
		return -ENODEV;
	}

	return 0;
}

/*
 * Helper function to usbobex_find_interfaces
 */
static int get_intf_string(struct usb_dev_handle *usb_handle,
							char **string, int id)
{
	if (id) {
		*string = malloc(USB_MAX_STRING_SIZE);
		if (*string == NULL)
			return -errno;
		*string[0] = '\0';
		return usb_get_string_simple(usb_handle, id, *string, USB_MAX_STRING_SIZE);
	}

	return 0;
}

/*
 * Helper function to usbobex_find_interfaces
 */
static struct obex_usb_intf_transport_t *check_intf(struct usb_device *dev,
						    int c, int i, int a)
{
	struct usb_interface_descriptor *alt;

	alt = &dev->config[c].interface[i].altsetting[a];

	if ((alt->bInterfaceClass == USB_CDC_CLASS) &&
			(alt->bInterfaceSubClass == USB_CDC_OBEX_SUBCLASS)) {
		int err;
		unsigned char *buffer = alt->extra;
		int buflen = alt->extralen;
		struct obex_usb_intf_transport_t *next;

		next = calloc(1, sizeof(*next));
		if (next == NULL)
			return NULL;

		next->device = dev;
		next->configuration = dev->config[c].bConfigurationValue;
		next->configuration_description = dev->config[c].iConfiguration;
		next->control_interface = alt->bInterfaceNumber;
		next->control_interface_description = alt->iInterface;
		next->control_setting = alt->bAlternateSetting;
		next->extra_descriptors = malloc(buflen);
		if (next->extra_descriptors)
			memcpy(next->extra_descriptors, buffer, buflen);
		next->extra_descriptors_len = buflen;

		err = find_obex_data_interface(buffer, buflen, dev->config[c], next);
		if (err) {
			free(next);
			return NULL;
		}

		return next;
	}

	return NULL;
}

/*
 * Function usbobex_find_interfaces ()
 *
 *    Find available USBOBEX interfaces on the system
 */
static int usbobex_find_interfaces(obex_t *self, obex_interface_t **interfaces)
{
	struct usb_bus *busses;
	struct usb_bus *bus;
	struct usb_device *dev;
	int c, i, a;
	int num = 0;
	struct obex_usb_intf_transport_t *first = NULL;
	struct obex_usb_intf_transport_t *current = NULL;
	obex_interface_t *intf_array = NULL;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	busses = usb_get_busses();

	for (bus = busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			/* Loop through all of the configurations */
			for (c = 0; c < dev->descriptor.bNumConfigurations; c++) {
				/* Loop through all of the interfaces */
				for (i = 0; i < dev->config[c].bNumInterfaces; i++) {
					/* Loop through all of the alternate settings */
					for (a = 0; a < dev->config[c].interface[i].num_altsetting; a++) {
						/* Check if this interface is OBEX */
						/* and find data interface */
						struct obex_usb_intf_transport_t *next;

						next = check_intf(dev, c, i, a);
						if (!next)
							continue;

						++num;
						if (current)
							current->next = next;
						current = next;

						if (first == NULL)
							first = current;
					}
				}
			}
		}
	}

	intf_array = calloc(num, sizeof(*intf_array));
	if (intf_array == NULL) {
		while (current) {
			struct obex_usb_intf_transport_t *tmp = current->next;
			free(current);
			current = tmp;
		}
		return 0;
	}

	num = 0;
	current = first;
	while (current) {
		struct usb_dev_handle *usb_handle;

		intf_array[num].usb.intf = current;
		usb_handle = usb_open(current->device);
		get_intf_string(usb_handle, &intf_array[num].usb.manufacturer,
				current->device->descriptor.iManufacturer);
		get_intf_string(usb_handle, &intf_array[num].usb.product,
				current->device->descriptor.iProduct);
		get_intf_string(usb_handle, &intf_array[num].usb.serial,
				current->device->descriptor.iSerialNumber);
		get_intf_string(usb_handle, &intf_array[num].usb.configuration,
				current->configuration_description);
		get_intf_string(usb_handle, &intf_array[num].usb.control_interface,
				current->control_interface_description);
		get_intf_string(usb_handle, &intf_array[num].usb.data_interface_idle,
				current->data_interface_idle_description);
		get_intf_string(usb_handle, &intf_array[num].usb.data_interface_active,
				current->data_interface_active_description);
		intf_array[num].usb.idVendor = current->device->descriptor.idVendor;
		intf_array[num].usb.idProduct = current->device->descriptor.idProduct;
		intf_array[num].usb.bus_number = atoi(current->device->bus->dirname);
		intf_array[num].usb.device_address = atoi(current->device->filename);
		intf_array[num].usb.interface_number = current->control_interface;
		find_obex_service_descriptor(current->extra_descriptors,
					current->extra_descriptors_len,
					&intf_array[num].usb.service);
		usb_close(usb_handle);

		current = current->next;
		++num;
	}

	*interfaces = intf_array;
	return num;
}

/*
 * Function usbobex_free_interface ()
 *
 *    Free a discovered USBOBEX interface on the system
 */
static void usbobex_free_interface(obex_interface_t *intf)
{
	if (intf) {
		free(intf->usb.manufacturer);
		free(intf->usb.product);
		free(intf->usb.serial);
		free(intf->usb.configuration);
		free(intf->usb.control_interface);
		free(intf->usb.data_interface_idle);
		free(intf->usb.data_interface_active);
		free(intf->usb.service);
		free(intf->usb.intf->extra_descriptors);
		free(intf->usb.intf);
	}
}

/*
 * Function usbobex_connect_request (self)
 *
 *    Open the USB connection
 *
 */
static bool usbobex_connect_request(obex_t *self)
{
	struct usbobex_data *data = self->trans->data;
	int ret;

	DEBUG(4, "\n");

	data->self.dev = usb_open(data->self.device);

	ret = usb_set_configuration(data->self.dev, data->self.configuration);
	if (ret < 0) {
		DEBUG(4, "Can't set configuration %d", ret);
	}

	ret = usb_claim_interface(data->self.dev, data->self.control_interface);
	if (ret < 0) {
		DEBUG(4, "Can't claim control interface %d", ret);
		goto err1;
	}

	ret = usb_set_altinterface(data->self.dev, data->self.control_setting);
	if (ret < 0) {
		DEBUG(4, "Can't set control setting %d", ret);
		goto err2;
	}

	ret = usb_claim_interface(data->self.dev, data->self.data_interface);
	if (ret < 0) {
		DEBUG(4, "Can't claim data interface %d", ret);
		goto err2;
	}

	ret = usb_set_altinterface(data->self.dev, data->self.data_active_setting);
	if (ret < 0) {
		DEBUG(4, "Can't set data active setting %d", ret);
		goto err3;
	}

	return true;

err3:
	usb_release_interface(data->self.dev, data->self.data_interface);
err2:
	usb_release_interface(data->self.dev, data->self.control_interface);
err1:
	usb_close(data->self.dev);
	return false;
}

/*
 * Function usbobex_disconnect (self)
 *
 *    Shutdown the USB link
 *
 */
static bool usbobex_disconnect(obex_t *self)
{
	struct usbobex_data *data = self->trans->data;
	int ret;

	DEBUG(4, "\n");

	usb_clear_halt(data->self.dev, data->self.data_endpoint_read);
	usb_clear_halt(data->self.dev, data->self.data_endpoint_write);

	ret = usb_set_altinterface(data->self.dev, data->self.data_idle_setting);
	if (ret < 0) {
		DEBUG(4, "Can't set data idle setting %d", ret);
	}

	ret = usb_release_interface(data->self.dev, data->self.data_interface);
	if (ret < 0) {
		DEBUG(4, "Can't release data interface %d", ret);
	}

	ret = usb_release_interface(data->self.dev, data->self.control_interface);
	if (ret < 0) {
		DEBUG(4, "Can't release control interface %d", ret);
	}

	ret = usb_close(data->self.dev);
	if (ret < 0) {
		DEBUG(4, "Can't close interface %d", ret);
		return false;
	}

	return true;
}

static int usbobex_get_timeout(int64_t timeout)
{
	if (timeout < 0 || timeout > INT_MAX) {
		/* libusb-0.x doesn't know about waiting infinitely
		 * so we try with the largest value possible
		 */
		return INT_MAX;
	}
	return (int)timeout;
}

static ssize_t usbobex_write(obex_t *self, struct databuffer *msg)
{
	struct obex_transport *trans = self->trans;
	struct usbobex_data *data = self->trans->data;
	int status;

	DEBUG(4, "Endpoint %d\n", data->self.data_endpoint_write);
	status = usb_bulk_write(data->self.dev,
				data->self.data_endpoint_write,
				buf_get(msg), buf_get_length(msg),
				usbobex_get_timeout(trans->timeout));

	if (status < 0) {
		if (status == -ETIMEDOUT)
			return 0;
		errno = -status;
		return -1;
	}

	return status;
}

static ssize_t usbobex_read(obex_t *self, void *buf, int buflen)
{
	struct obex_transport *trans = self->trans;
	struct usbobex_data *data = self->trans->data;
	int status;

	/* USB can only read 0xFFFF bytes at once (equals mtu_rx) */
	DEBUG(4, "Endpoint %d\n", data->self.data_endpoint_read);
	status = usb_bulk_read(data->self.dev,
				data->self.data_endpoint_read,
				buf, self->mtu_rx,
				usbobex_get_timeout(trans->timeout));

	if (status < 0) {
		if (status == -ETIMEDOUT)
			return 0;
		errno = -status;
		return -1;
	}

	return status;
}

static result_t usbobex_handle_input(obex_t *self)
{
	ssize_t err = obex_transport_read(self, 0);
	if (err > 0)
		return RESULT_SUCCESS;
	else if (err == 0)
		return RESULT_TIMEOUT;
	else
		return RESULT_ERROR;
}

static struct obex_transport_ops usbobex_transport_ops = {
	&usbobex_create,
	NULL,
	&usbobex_cleanup,

	usbobex_handle_input,
	&usbobex_write,
	&usbobex_read,
	&usbobex_disconnect,

	NULL,
	NULL,
	NULL,

	{
		NULL,
		NULL,
	},

	{
		&usbobex_connect_request,
		&usbobex_find_interfaces,
		&usbobex_free_interface,
		&usbobex_select_interface,
	},
};

struct obex_transport * usbobex_transport_create(void)
{
	return obex_transport_create(&usbobex_transport_ops);
}
#endif /* HAVE_USB */
