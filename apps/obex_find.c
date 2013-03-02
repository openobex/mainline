/** \file apps/obex_find.c
 * Find connectable OBEX devices (IrDA, USB).
 * OpenOBEX test applications and sample code.
 *
 * Copyright (c) 2010 Hendrik Sattler
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

#define _XOPEN_SOURCE 500
#include <openobex/obex.h>
#include <openobex/obex_const.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

static int verbose = 0;

static void obex_event_cb(obex_t *handle, obex_object_t *obj, int mode,
			  int event, int obex_cmd, int obex_rsp)
{
}

static void usb_print(obex_interface_t *intf)
{
	if (verbose > 0) {
		printf("\tPath: %d:%d:%d\n",
		       intf->usb.bus_number,
		       intf->usb.device_address,
		       intf->usb.interface_number);
		printf("\tManufacturer: %s (%04x)\n", intf->usb.manufacturer,
		       intf->usb.idVendor);
		printf("\tProduct: %s (%04x)\n", intf->usb.product,
		       intf->usb.idProduct);
	} else {
		printf("\tManufacturer: %s\n", intf->usb.manufacturer);
		printf("\tProduct: %s\n", intf->usb.product);
	}
	printf("\tSerial: %s\n", intf->usb.serial);
	printf("\tDescription: %s\n", intf->usb.control_interface);
}

static void irda_print(obex_interface_t *intf)
{
	const char* charsets[] = {
		"ASCII",
		"ISO-8859-1",
		"ISO-8859-2",
		"ISO-8859-3",
		"ISO-8859-4",
		"ISO-8859-5",
		"ISO-8859-6",
		"ISO-8859-7",
		"ISO-8859-8",
		"ISO-8859-9",
	};
	const char *charset = "";

	if (intf->irda.local)
		printf("\tLocal address: %08x\n", intf->irda.local);
	printf("\tRemote address: %08x\n", intf->irda.remote);

	if (intf->irda.charset == 0xFF) {
		charset = "Unicode";
	} else if (intf->irda.charset < sizeof(charsets)/sizeof(charsets[0])) {
		charset = charsets[intf->irda.charset];
	}
	printf("\tDescription character set: %s\n", charset);

	if (intf->irda.charset == 0x00)
		printf("\tDescription: %s\n", intf->irda.info);
}

static void find_devices(int trans, int flags)
{
	obex_t *handle;
	int found;
	int i = 0;
	const char *descr = NULL;
	void (*trans_print)(obex_interface_t *);

	switch (trans) {
	case OBEX_TRANS_IRDA:
		descr = "irda";
		trans_print = irda_print;
		break;

	case OBEX_TRANS_USB:
		descr = "usb";
		trans_print = usb_print;
		break;

	default:
		return;
	}
  
	handle = OBEX_Init(trans, obex_event_cb, flags);
	found = OBEX_EnumerateInterfaces(handle);
	printf("Found %d %s devices.\n", found, descr);
	for (; i < found; ++i) {
		obex_interface_t *intf = OBEX_GetInterfaceByIndex(handle, i);
		printf("Device %d:\n", i);
		trans_print(intf);
	}
	OBEX_Cleanup(handle);
}

int main (int argc, char **argv)
{
	int t = 1;
	char *default_argv[] = { argv[0], "-f", "irda", "usb", "bt", NULL };
	int flags = 0;

	if (argc < 2)
		argv = default_argv;

	for (; argv[t] != NULL; ++t) {
		if (0 == strcmp(argv[t], "-v"))
			++verbose;
		else if (0 == strcmp(argv[t], "-f"))
			flags |= OBEX_FL_FILTERHINT;
		else if (0 == strcasecmp(argv[t], "irda"))
			find_devices(OBEX_TRANS_IRDA, flags);
		else if (0 == strcasecmp(argv[t], "usb"))
			find_devices(OBEX_TRANS_USB, flags);
	}
	return 0;
}
