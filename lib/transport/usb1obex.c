/**
	\file usbobex.c
	USB OBEX, USB transport for OBEX, libusb 1.x support.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 2009 Alex Kanavin, All Rights Reserved.

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

#ifdef HAVE_USB1

#include <string.h>
#include <unistd.h>
#include <stdio.h>		/* perror */
#include <errno.h>		/* errno and EADDRNOTAVAIL */
#include <stdlib.h>
#include <poll.h>		/* POLLIN */
#include <limits.h>

#include <libusb.h>

#include "obex_main.h"
#include "usbobex.h"
#include "usbutils.h"
#include "databuffer.h"

static void usbobex_set_fd(int fd, short events, void *user_data)
{
	struct usbobex_data *data = user_data;

	data->fd = fd;
}

static void usbobex_clear_fd(int fd, void *user_data)
{
	struct usbobex_data *data = user_data;

	if (fd == data->fd)
		data->fd = -1;
}

static int usbobex_get_fd(obex_t *self)
{
	struct usbobex_data *data = self->trans->data;

	return data->fd;
}

static void * usbobex_create (void)
{
	return calloc(1, sizeof(struct usbobex_data));
}

static bool usbobex_init (obex_t *self)
{
	struct usbobex_data *data = self->trans->data;

	if (data == NULL)
		return false;

	if (data->ctx == NULL) {
		int err = libusb_init(&data->ctx);
		if (err)
			return false;
	}

	data->fd = -1;
	libusb_set_pollfd_notifiers(data->ctx, &usbobex_set_fd,
						&usbobex_clear_fd, data);

	return true;
}

static void usbobex_cleanup (obex_t *self)
{
	struct usbobex_data *data = self->trans->data;

	if (data->ctx) {
		libusb_exit(data->ctx);
		data->ctx = NULL;
	}
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
static void find_eps(struct obex_usb_intf_transport_t *intf, struct libusb_interface_descriptor data_intf, int *found_active, int *found_idle)
{
	struct libusb_endpoint_descriptor ep0, ep1;

	if (data_intf.bNumEndpoints == 2) {
		ep0 = data_intf.endpoint[0];
		ep1 = data_intf.endpoint[1];
		if ((ep0.bEndpointAddress & LIBUSB_ENDPOINT_IN) &&
		    ((ep0.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK) &&
		    !(ep1.bEndpointAddress & LIBUSB_ENDPOINT_IN) &&
		    ((ep1.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK)) {
			*found_active = 1;
			intf->data_active_setting = data_intf.bAlternateSetting;
			intf->data_interface_active_description = data_intf.iInterface;
			intf->data_endpoint_read = ep0.bEndpointAddress;
			intf->data_endpoint_write = ep1.bEndpointAddress;
		}
		if (!(ep0.bEndpointAddress & LIBUSB_ENDPOINT_IN) &&
		    ((ep0.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK) &&
		    (ep1.bEndpointAddress & LIBUSB_ENDPOINT_IN) &&
		    ((ep1.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK)) {
			*found_active = 1;
			intf->data_active_setting = data_intf.bAlternateSetting;
			intf->data_interface_active_description = data_intf.iInterface;
			intf->data_endpoint_read = ep1.bEndpointAddress;
			intf->data_endpoint_write = ep0.bEndpointAddress;
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
static int find_obex_data_interface(const unsigned char *buffer, int buflen, struct libusb_config_descriptor *config, struct obex_usb_intf_transport_t *intf)
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
	for (i = 0; i < config->bNumInterfaces; i++) {
		/* Loop through all of the alternate settings */
		for (a = 0; a < config->interface[i].num_altsetting; a++) {
			/* Check if this interface is OBEX data interface*/
			/* and find endpoints */
			if (config->interface[i].altsetting[a].bInterfaceNumber == intf->data_interface)
				find_eps(intf, config->interface[i].altsetting[a], &found_active, &found_idle);
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
static int get_intf_string(struct libusb_device_handle *usb_handle, char **string, int id)
{
	if (id) {
		*string = malloc(USB_MAX_STRING_SIZE);
		if (*string == NULL)
			return -errno;
		*string[0] = '\0';
		return libusb_get_string_descriptor_ascii(usb_handle, id, (unsigned char*)*string, USB_MAX_STRING_SIZE);
	}

	return 0;
}

/*
 * Helper function to usbobex_find_interfaces
 */
static struct obex_usb_intf_transport_t *check_intf(struct libusb_device *dev,
						    struct libusb_config_descriptor *conf_desc, int i, int a)
{
	const struct libusb_interface_descriptor *alt;

	alt = &conf_desc->interface[i].altsetting[a];

	if (alt->bInterfaceClass == USB_CDC_CLASS &&
			alt->bInterfaceSubClass == USB_CDC_OBEX_SUBCLASS) {
		int err;
		const unsigned char *buffer = alt->extra;
		int buflen = alt->extra_length;
		struct obex_usb_intf_transport_t *next;

		next = calloc(1, sizeof(*next));
		if (next == NULL)
			return NULL;

		next->device = dev;
		libusb_ref_device(dev);
		next->configuration = conf_desc->bConfigurationValue;
		next->configuration_description = conf_desc->iConfiguration;
		next->control_interface = alt->bInterfaceNumber;
		next->control_interface_description = alt->iInterface;
		next->control_setting = alt->bAlternateSetting;
		next->extra_descriptors = malloc(buflen);
		if (next->extra_descriptors)
			memcpy(next->extra_descriptors, buffer, buflen);
		next->extra_descriptors_len = buflen;

		err = find_obex_data_interface(buffer, buflen, conf_desc, next);
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
	struct usbobex_data *data = self->trans->data;
	struct libusb_context *libusb_ctx = data->ctx;
	struct obex_usb_intf_transport_t *first = NULL;
	struct obex_usb_intf_transport_t *current = NULL;
	int i, a;
	int num = 0;
	obex_interface_t *intf_array = NULL;

	if (libusb_ctx) {
		libusb_device **list;
		size_t cnt_dev = libusb_get_device_list(libusb_ctx, &list);
		size_t d = 0;
		for (d = 0; d < cnt_dev; d++) {
			struct libusb_config_descriptor *conf_desc;
			if (libusb_get_active_config_descriptor(list[d], &conf_desc) == 0) {
				for (i = 0; i < conf_desc->bNumInterfaces; i++) {
					for (a = 0; a < conf_desc->interface[i].num_altsetting; a++) {
						/* and find data interface */
						struct obex_usb_intf_transport_t *next;

						next = check_intf(list[d], conf_desc, i, a);
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
				libusb_free_config_descriptor(conf_desc);
			}
		}
		libusb_free_device_list(list, 1);
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
		struct libusb_device_handle *usb_handle;

		intf_array[num].usb.intf = current;
		if (libusb_open(current->device, &usb_handle) == 0) {
			struct libusb_device_descriptor dev_desc;
			if (libusb_get_device_descriptor(current->device, &dev_desc) == 0) {
				get_intf_string(usb_handle, &intf_array[num].usb.manufacturer,
					dev_desc.iManufacturer);
				get_intf_string(usb_handle, &intf_array[num].usb.product,
					dev_desc.iProduct);
				get_intf_string(usb_handle, &intf_array[num].usb.serial,
					dev_desc.iSerialNumber);
				get_intf_string(usb_handle, &intf_array[num].usb.configuration,
					current->configuration_description);
				get_intf_string(usb_handle, &intf_array[num].usb.control_interface,
					current->control_interface_description);
				get_intf_string(usb_handle, &intf_array[num].usb.data_interface_idle,
					current->data_interface_idle_description);
				get_intf_string(usb_handle, &intf_array[num].usb.data_interface_active,
					current->data_interface_active_description);
				intf_array[num].usb.idVendor = dev_desc.idVendor;
				intf_array[num].usb.idProduct = dev_desc.idProduct;
				intf_array[num].usb.bus_number = libusb_get_bus_number(current->device);
				intf_array[num].usb.device_address = libusb_get_device_address(current->device);
				intf_array[num].usb.interface_number = current->control_interface;
			}
			find_obex_service_descriptor(current->extra_descriptors,
					current->extra_descriptors_len,
					&intf_array[num].usb.service);
			libusb_close(usb_handle);
		}

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
		libusb_unref_device(intf->usb.intf->device);
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

	ret = libusb_open(data->self.device, &data->self.dev);
	if (ret != 0)
		return false;

	ret = libusb_claim_interface(data->self.dev,
				     data->self.control_interface);
	if (ret < 0) {
		DEBUG(4, "Can't claim control interface %d", ret);
		goto err1;
	}

	ret = libusb_set_interface_alt_setting(data->self.dev,
					       data->self.control_interface,
					       data->self.control_setting);
	if (ret < 0) {
		DEBUG(4, "Can't set control setting %d", ret);
		goto err2;
	}

	ret = libusb_claim_interface(data->self.dev, data->self.data_interface);
	if (ret < 0) {
		DEBUG(4, "Can't claim data interface %d", ret);
		goto err2;
	}

	ret = libusb_set_interface_alt_setting(data->self.dev,
					       data->self.data_interface,
					       data->self.data_active_setting);
	if (ret < 0) {
		DEBUG(4, "Can't set data active setting %d", ret);
		goto err3;
	}

	return true;

err3:
	libusb_release_interface(data->self.dev, data->self.data_interface);
err2:
	libusb_release_interface(data->self.dev, data->self.control_interface);
err1:
	libusb_close(data->self.dev);
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

	libusb_clear_halt(data->self.dev, data->self.data_endpoint_read);
	libusb_clear_halt(data->self.dev, data->self.data_endpoint_write);

	ret = libusb_set_interface_alt_setting(data->self.dev,
					       data->self.data_interface,
					       data->self.data_idle_setting);
	if (ret < 0) {
		DEBUG(4, "Can't set data idle setting %d", ret);
	}
	ret = libusb_release_interface(data->self.dev,
				       data->self.data_interface);
	if (ret < 0) {
		DEBUG(4, "Can't release data interface %d", ret);
	}
	ret = libusb_release_interface(data->self.dev,
				       data->self.control_interface);
	if (ret < 0) {
		DEBUG(4, "Can't release control interface %d", ret);
	}
	libusb_close(data->self.dev);
	return true;
}

static unsigned int usbobex_get_timeout(int64_t timeout)
{
	/* uses closest to zero, 0 itself means infinite */
	if (timeout == 0) {
		return 1;

	} else if (timeout > 0) {
		if ((uint64_t)timeout > UINT_MAX)
			return UINT_MAX;
		else
			return (unsigned int)timeout;

	}
	return 0;
}

static ssize_t usbobex_write(obex_t *self, struct databuffer *msg)
{
	struct obex_transport *trans = self->trans;
	struct usbobex_data *data = self->trans->data;
	int actual = 0;
	int usberror;

	DEBUG(4, "Endpoint %d\n", data->self.data_endpoint_write);
	usberror = libusb_bulk_transfer(data->self.dev,
					data->self.data_endpoint_write,
					buf_get(msg),
					buf_get_length(msg), &actual,
					usbobex_get_timeout(trans->timeout));
	switch (usberror) {
	case 0:
		buf_clear(msg, actual);
		return actual;

	case LIBUSB_ERROR_TIMEOUT:
		buf_clear(msg, actual);
		return 0;

	default:
		return -1;
	}
}

static ssize_t usbobex_read(obex_t *self, void *buf, int buflen)
{
	struct obex_transport *trans = self->trans;
	struct usbobex_data *data = self->trans->data;
	int usberror;
	int actual = 0;

	/* USB can only read 0xFFFF bytes at once (equals mtu_rx) */
	DEBUG(4, "Endpoint %d\n", data->self.data_endpoint_read);
	usberror = libusb_bulk_transfer(data->self.dev,
					data->self.data_endpoint_read,
					buf, self->mtu_rx, &actual,
					usbobex_get_timeout(trans->timeout));
	switch (usberror) {
	case 0:
		if (actual > buflen)
			buflen = actual;
		break;

	case LIBUSB_ERROR_TIMEOUT:
		actual = 0;
		break;

	default:
		actual = -1;
		break;
	}

	return actual;
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
	&usbobex_init,
	&usbobex_cleanup,

	&usbobex_handle_input,
	&usbobex_write,
	&usbobex_read,
	&usbobex_disconnect,

	&usbobex_get_fd,
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
#endif /* HAVE_USB1 */
