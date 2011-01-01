/** \file apps/obex_find.c
 * Help udev to find connectable OBEX USB devices.
 *
 * Copyright (c) 2011 Hendrik Sattler
 *
 * OpenOBEX is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with OpenOBEX. If not, see <http://www.gnu.org/>.
 */

#include <openobex/obex.h>
#include <openobex/obex_const.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void obex_event_cb(obex_t *handle, obex_object_t *obj, int mode,
			  int event, int obex_cmd, int obex_rsp)
{
}

static int match_usb_interface(obex_usb_intf_t *intf, uint16_t vendor,
			       uint16_t product)
{
	return (intf->idVendor == vendor && intf->idProduct == product);
}

static int find_usb_device(uint16_t vendor, uint16_t product)
{
	obex_t *handle;
	int found;
	int matched = 0;
	int i = 0;

	handle = OBEX_Init(OBEX_TRANS_USB, obex_event_cb, 0);
	if (!handle)
		return 0;

	found = OBEX_EnumerateInterfaces(handle);
	for (; !matched && i < found; ++i) {
		obex_interface_t *intf = OBEX_GetInterfaceByIndex(handle, i);

		matched = match_usb_interface(&intf->usb, vendor, product);
	}
	OBEX_Cleanup(handle);

	return matched;
}

int main (int argc, char **argv)
{
	unsigned long vendor;
	unsigned long product;

	if (argc < 2)
		return 0;

	vendor = strtoul(argv[1], NULL, 16);
	product = strtoul(argv[2], NULL, 16);
	if (vendor <= 0xFFFF && product <= 0xFFFF &&
	    find_usb_device((uint16_t)vendor, (uint16_t)product))
		return EXIT_SUCCESS;

	return EXIT_FAILURE;
}
