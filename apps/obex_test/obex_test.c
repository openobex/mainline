/**
	\file apps/obex_test.c
	Test IrOBEX, TCPOBEX and OBEX over cable to R320s.
	OpenOBEX test applications and sample code.

	Copyright (c) 2000 Pontus Fuchs, All Rights Reserved.

	OpenOBEX is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as
	published by the Free Software Foundation; either version 2 of
	the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public
	License along with OpenOBEX. If not, see <http://www.gnu.org/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_BLUETOOTH
#include "../lib/transport/bluez_compat.h"
#ifdef _WIN32
bdaddr_t bluez_compat_bdaddr_any = { BTH_ADDR_NULL };
static int str2ba(const char *str, bdaddr_t *ba) {
	//TODO
	*ba = *BDADDR_ANY;
	return 0;
}
#endif
#endif

#include <openobex/obex.h>

#include "obex_test.h"
#include "obex_test_client.h"
#include "obex_test_server.h"

#if defined(_WIN32)
#define in_addr_t unsigned long

#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define TRUE  1
#define FALSE 0

#define IR_SERVICE "OBEX"
#define BT_CHANNEL 4

//
// Called by the obex-layer when some event occurs.
//
static void obex_event(obex_t *handle, obex_object_t *object, int mode,
					int event, int obex_cmd, int obex_rsp)
{
	switch (event)	{
	case OBEX_EV_PROGRESS:
		printf("Made some progress...\n");
		break;

	case OBEX_EV_ABORT:
		printf("Request aborted!\n");
		break;

	case OBEX_EV_REQDONE:
		if (mode == OBEX_MODE_CLIENT) {
			client_done(handle, object, obex_cmd, obex_rsp);
		}
		else	{
			server_done(handle, object, obex_cmd, obex_rsp);
		}
		break;
	case OBEX_EV_REQHINT:
		/* Accept any command. Not rellay good, but this is a test-program :) */
		OBEX_ObjectSetRsp(object, OBEX_RSP_CONTINUE, OBEX_RSP_SUCCESS);
		break;

	case OBEX_EV_REQ:
		server_request(handle, object, event, obex_cmd);
		break;

	case OBEX_EV_LINKERR:
		OBEX_TransportDisconnect(handle);
		printf("Link broken!\n");
		break;

	case OBEX_EV_STREAMEMPTY:
		fillstream(handle, object);
		break;

	default:
		printf("Unknown event %02x!\n", event);
		break;
	}
}

int read_input(char *answer, size_t size, const char *question, ...)
{
	va_list ap;
	va_start(ap, question);
	vfprintf(stdout, question, ap);
	va_end(ap);

	fflush(stdout);
	if (fgets(answer, size, stdin) == NULL)
		return -1;

	answer[strlen(answer)-1] = '\0'; /* remove trailing newline */
	return strlen(answer);
}	

#if 0
/*
 * Function get_peer_addr (name, peer)
 *
 *    
 *
 */
static int get_peer_addr(char *name, struct sockaddr_in *peer) 
{
	struct hostent *host;
	in_addr_t inaddr;
        
	/* Is the address in dotted decimal? */
	if ((inaddr = inet_addr(name)) != INADDR_NONE) {
		memcpy((char *) &peer->sin_addr, (char *) &inaddr,
		      sizeof(inaddr));  
	}
	else {
		if ((host = gethostbyname(name)) == NULL) {
			printf( "Bad host name: ");
			exit(-1);
                }
		memcpy((char *) &peer->sin_addr, host->h_addr,
				host->h_length);
        }
	return 0;
}

//
//
//
static int inet_connect(obex_t *handle)
{
	struct sockaddr_in peer;

	get_peer_addr("localhost", &peer);
	return OBEX_TransportConnect(handle, (struct sockaddr *) &peer,
				  sizeof(struct sockaddr_in));
}
#endif
	
//
//
//
int main (int argc, char *argv[])
{
	char cmd[3];
	int end = 0;
	int tcpobex = FALSE;
	obex_t *handle = NULL;
	struct context global_context = {0};
	unsigned int flags = 0;
	int i = 1;

#ifdef HAVE_BLUETOOTH
	int btobex = FALSE;
	bdaddr_t bdaddr;
	uint8_t channel = 0;
#endif
#ifdef HAVE_USB
	int usbobex = FALSE;
	obex_interface_t *obex_intf = NULL;
#endif

	if (argc == 2 && strcmp(argv[1], "-h") == 0) {
		printf(
			"Usage: obex_test [options]\n"
			"\n"
			"Options:\n"
			"    -f [flags]        Set some flags: n=non-blocking\n"
#ifdef HAVE_BLUETOOTH
			"    -b [addr] [chan]  Use bluetooth RFCOMM transport\n"
#endif
#ifdef HAVE_USB
			"    -u [interface]    Use USB transport\n"
#endif
			"    -i                Use IP/TCP transport\n"
			"    -h                Print this help message\n"
			"\n"
			"If no transport is selected, IrDA is used.\n"
		);
		return 0;
	}

	/* Read flags for OBEX_Init() */
	if (argc >= i+1 && strcmp(argv[i], "-f") == 0) {
		++i;
		if (argc >= i+1 && argv[i][0] != '-') {
			char *flag = argv[i++];

			for (; *flag != 0; ++flag)
				switch(*flag) {
				case 'n':
					fprintf(stderr, "Using non-blocking mode\n");
					flags |= OBEX_FL_NONBLOCK;
					break;

				default:
					fprintf(stderr, "Unknown flag %c\n", *flag);
					break;
				};
		}
	}

#ifdef HAVE_BLUETOOTH
	if (argc >= i+1 && strcmp(argv[i], "-b") == 0)
		btobex = 1;
#endif
#ifdef HAVE_USB
	if (argc >= i+1 && strcmp(argv[i], "-u") == 0)
		usbobex = TRUE;
#endif
	if (argc == i+1 && strcmp(argv[i], "-i") == 0)
		tcpobex = TRUE;

#ifdef HAVE_BLUETOOTH
	if (btobex) {
		const char *channel_arg = NULL;
		switch (argc-i) {
		case 3:
			str2ba(argv[i+1], &bdaddr);
			channel_arg = argv[i+2];
			break;
		case 2:
			str2ba(argv[i+1], &bdaddr);
			if (bacmp(&bdaddr, BDADDR_ANY) == 0)
				channel_arg = argv[i+1];
			break;
		case 1:
			bacpy(&bdaddr, BDADDR_ANY);
			break;
		default:
			printf("Wrong number of arguments\n");
			exit(0);
		}

		switch (btobex) {
		case TRUE:
			printf("Using Bluetooth RFCOMM transport\n");
			handle = OBEX_Init(OBEX_TRANS_BLUETOOTH, obex_event,
					  flags);
			if (channel_arg)
				channel = (atoi(channel_arg) & 0xFF);
			else
				channel = BT_CHANNEL;
			break;
		}

		if (!handle) {
			perror( "OBEX_Init failed");
			exit(0);
		}
	} else
#endif
#ifdef HAVE_USB
	if (usbobex) {
		int k, interfaces_number, intf_num;
		switch (argc-i) {
		case 1:
			printf("Using USB transport, "
			       "querying available interfaces\n");
			handle = OBEX_Init(OBEX_TRANS_USB, obex_event, flags);
			if (!handle) {
				perror( "OBEX_Init failed");
				exit(0);
			}
			interfaces_number = OBEX_EnumerateInterfaces(handle);
			for (k = 0; k < interfaces_number; k++) {
				obex_intf = OBEX_GetInterfaceByIndex(handle, k);
				printf("Interface %d: idVendor: %#x, "
				       "idProduct: %#x, bus %d, dev %d, "
				       "intf %d, %s %s %s\n", k,
					obex_intf->usb.idVendor,
					obex_intf->usb.idProduct,
					obex_intf->usb.bus_number,
					obex_intf->usb.device_address,
					obex_intf->usb.interface_number,
					obex_intf->usb.manufacturer,
					obex_intf->usb.product,
					obex_intf->usb.control_interface);
			}
			printf("Use '%s -u interface_number' to run "
			       "interactive OBEX test client\n", argv[0]);
			OBEX_Cleanup(handle);
			exit(0);
			break;
		case 2:
			intf_num = atoi(argv[i+1]);
			printf("Using USB transport \n");
			handle = OBEX_Init(OBEX_TRANS_USB, obex_event, flags);
			if (!handle) {
				perror( "OBEX_Init failed");
				exit(0);
			}

			interfaces_number = OBEX_EnumerateInterfaces(handle);
			if (intf_num >= interfaces_number) {
				printf( "Invalid interface number\n");
				exit(0);
			}
			obex_intf = OBEX_GetInterfaceByIndex(handle, intf_num);

			break;
		default:
			printf("Wrong number of arguments\n");
			exit(0);
		}
	} else
#endif

	if (tcpobex) {
		printf("Using TCP transport\n");
		handle = OBEX_Init(OBEX_TRANS_INET, obex_event, flags);
		if (!handle) {
			perror( "OBEX_Init failed");
			exit(0);
		}

	} else {
		printf("Using IrDA transport\n");
		handle = OBEX_Init(OBEX_TRANS_IRDA, obex_event, flags);
		if (!handle) {
			perror( "OBEX_Init failed");
			exit(0);
		}
	}

	OBEX_SetUserData(handle, &global_context);
	
	printf( "OBEX Interactive test client/server.\n");

	while (!end) {
		if (read_input(cmd, sizeof(cmd), "> ") < 0)
			break;
		switch(cmd[0]) {
			case 'h':
				printf("Commands:\n"
				       " c - connect\n"
				       " d - disconnect\n"
				       " g - get\n"
				       " p - put\n"
				       " t - set path\n"
				       " s - server\n"
				       " x - push\n"
				       " h - help\n"
				       " q - quit\n"
				);
			break;
			case 'q':
				end=1;
			break;
			case 'g':
				get_client(handle, &global_context);
			break;
			case 't':
				setpath_client(handle);
			break;
			case 'p':
				put_client(handle);
			break;
			case 'x':
				push_client(handle);
			break;
			case 'c':
				/* First connect transport */
#ifdef HAVE_BLUETOOTH
				if (btobex) {
					if (bacmp(&bdaddr, BDADDR_ANY) == 0) {
						printf("Device address error! (Bluetooth)\n");
						break;
					}
					if (BtOBEX_TransportConnect(handle, BDADDR_ANY, &bdaddr, channel) <0) {
						printf("Transport connect error! (Bluetooth)\n");
						break;
					}
				} else
#endif
#ifdef HAVE_USB
				if (usbobex) {
					if (OBEX_InterfaceConnect(handle, obex_intf) < 0) {
						printf("Transport connect error! (USB)\n");
						break;
					}
				} else
#endif
				if (tcpobex) {
					if (TcpOBEX_TransportConnect(handle, NULL, 0) < 0) {
						printf("Transport connect error! (TCP)\n");
						break;
					}
				} else {
					if (IrOBEX_TransportConnect(handle, IR_SERVICE) < 0) {
						printf("Transport connect error! (IrDA)\n");
						break;
					}
				}
				// Now send OBEX-connect.	
				connect_client(handle);
			break;
			case 'd':
				disconnect_client(handle);
			break;
			case 's':
				/* First register server */
#ifdef HAVE_BLUETOOTH
				if (btobex) {
					if (BtOBEX_ServerRegister(handle, BDADDR_ANY, channel) < 0) {
						printf("Server register error! (Bluetooth)\n");
						break;
					}
				} else
#endif
#ifdef HAVE_USB
				if (usbobex) {
					printf("Transport not found! (USB)\n");
				} else
#endif
				if (tcpobex) {
					if (TcpOBEX_ServerRegister(handle, NULL, 0) < 0) {
						printf("Server register error! (TCP)\n");
						break;
					}

				} else {
					if (IrOBEX_ServerRegister(handle, IR_SERVICE) < 0) {
						printf("Server register error! (IrDA)\n");
						break;
					}
				}
				/* No process server events */
				server_do(handle);
				OBEX_TransportDisconnect(handle);
			break;
			default:
				printf("Unknown command %s\n", cmd);
		}
	}

	return 0;
}
