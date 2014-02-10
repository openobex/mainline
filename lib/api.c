/**
	\file api.c
	OpenOBEX API definition.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 1999, 2000 Dag Brattli, All Rights Reserved.
	Copyright (c) 1999, 2000 Pontus Fuchs, All Rights Reserved.

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
#include <windows.h>
#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT WSAESOCKTNOSUPPORT
#endif

#else /* _WIN32 */
#include <fcntl.h>
#include <unistd.h>
#endif /* _WIN32 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "obex_main.h"
#include "obex_client.h"
#include "obex_object.h"
#include "obex_body.h"
#include "obex_msg.h"
#include "obex_connect.h"
#include "databuffer.h"

#ifdef HAVE_IRDA
#include "transport/irobex.h"
#endif /*HAVE_IRDA*/
#ifdef HAVE_BLUETOOTH
#include "transport/btobex.h"
#endif /*HAVE_BLUETOOTH*/
#ifdef HAVE_USB
#include "transport/usbobex.h"
#endif /*HAVE_USB*/
#include "transport/inobex.h"
#include "transport/customtrans.h"
#include "transport/fdobex.h"

#include "obex_incl.h"

/**
	Initialize OBEX.
	\param transport Which transport to use. The following transports are available :
			- #OBEX_TRANS_IRDA : Use regular IrDA socket (need an IrDA stack)
			- #OBEX_TRANS_INET : Use regular TCP/IP socket
			- #OBEX_TRANS_CUSTOM : Use user provided transport, you must
			register your own transport with #OBEX_RegisterCTransport()
			- #OBEX_TRANS_BLUETOOTH: Use regular Bluetooth RFCOMM socket
			- #OBEX_TRANS_USB: Use USB transport (libusb needed)
	\param eventcb Function pointer to your event callback.
			See obex.h for prototype of this callback.
	\param flags Bitmask of flags. The following flags are available :
			- #OBEX_FL_KEEPSERVER : Keep the server alive after incomming request
			- #OBEX_FL_FILTERHINT : Filter target devices based on Obex hint bit
			- #OBEX_FL_FILTERIAS  : Filter target devices based on IAS entry
			- #OBEX_FL_CLOEXEC    : Open all sockets with SO_CLOEXEC set
			- #OBEX_FL_NONBLOCK   : Open all sockets non-blocking
	\return an OBEX handle or NULL on error.
 */
LIB_SYMBOL
obex_t * CALLAPI OBEX_Init(int transport, obex_event_t eventcb,
							unsigned int flags)
{
	obex_t *self;

	obex_return_val_if_fail(eventcb != NULL, NULL);

	obex_library_init();
	self = obex_create(eventcb, flags);
	if (!obex_transport_init(self, transport)) {
		obex_destroy(self);
		self = NULL;
	}

	return self;
}

/**
	Register a custom transport.
	\param self OBEX handle
	\param ctrans Structure with callbacks to transport operations
		(see obex_const.h for details)
	\return -1 on error

	Call this function directly after #OBEX_Init if you are using
	a custom transport.
 */
LIB_SYMBOL
int CALLAPI OBEX_RegisterCTransport(obex_t *self, obex_ctrans_t *ctrans)
{
	obex_return_val_if_fail(self != NULL, -1);
	obex_return_val_if_fail(ctrans != NULL, -1);

	return custom_register(self, ctrans);
}

/**
	Close down an OBEX instance.
	\param self OBEX handle

	Disconnects the transport and frees the interface (see
	#OBEX_FreeInterfaces).
 */
LIB_SYMBOL
void CALLAPI OBEX_Cleanup(obex_t *self)
{
	obex_return_if_fail(self != NULL);

	obex_destroy(self);
}

/**
	Set userdata of an OBEX handle.
	\param self OBEX handle
	\param data It's all up to you!
 */
LIB_SYMBOL
void CALLAPI OBEX_SetUserData(obex_t *self, void *data)
{
	obex_return_if_fail(self != NULL);
	self->userdata=data;
}

/**
	Read the userdata from an OBEX handle.
	\param self OBEX handle
	\return the userdata

	Returns userdata set with #OBEX_SetUserData.
 */
LIB_SYMBOL
void * CALLAPI OBEX_GetUserData(obex_t *self)
{
	obex_return_val_if_fail(self != NULL, 0);
	return self->userdata;
}

/**
	Change user callback on an OBEX handle.
	\param self OBEX handle
	\param eventcb Function pointer to your new event callback.
	\param data Pointer to the new user data to pass to the new
	callback (optional)
 */
LIB_SYMBOL
void CALLAPI OBEX_SetUserCallBack(obex_t *self, obex_event_t eventcb,
								void *data)
{
	obex_return_if_fail(self != NULL);
	/* The callback can't be NULL */
	if (eventcb != NULL) {
		self->eventcb = eventcb;
		/* Optionaly change the user data */
		if (data != NULL)
			self->userdata = data;
	}
}

/**
	Set MTU to be used for receive and transmit.
	\param self OBEX handle
	\param mtu_rx maximum receive transport packet size
	\param mtu_tx_max maximum transmit transport packet size negociated
	\return -1 or negative error code on error

	Changing those values can increase the performance of the underlying
	transport, but will increase memory consumption and latency (especially
	abort latency), and may trigger bugs in buggy transport.
	This need to be set *before* establishing the connection.
 */
LIB_SYMBOL
int CALLAPI OBEX_SetTransportMTU(obex_t *self, uint16_t mtu_rx,
							uint16_t mtu_tx_max)
{
	obex_return_val_if_fail(self != NULL, -EFAULT);
	if (self->object) {
		DEBUG(1, "We are busy.\n");
		return -EBUSY;
	}

	return obex_set_mtu(self, mtu_rx, mtu_tx_max);
}

/**
	Start listening for incoming connections.
	\param self OBEX handle
	\param saddr Local address to bind to or NULL
	\param addrlen Length of address
	\return -1 on error

	Bind a server socket to an Obex service. Common transport have
	specialised version of this function.
	If you want to call the listen callback of the custom transport,
	use NULL for saddr and 0 for addrlen.
 */
LIB_SYMBOL
int CALLAPI OBEX_ServerRegister(obex_t *self, struct sockaddr *saddr, int addrlen)
{
	DEBUG(3, "\n");

	obex_return_val_if_fail(self != NULL, -1);
	obex_return_val_if_fail((addrlen == 0) || (saddr != NULL), -1);

	if (addrlen != 0 && saddr != NULL &&
	    !obex_transport_set_local_addr(self, saddr, addrlen))
		return -1;

	return obex_transport_listen(self)? 1: -1;
}

/**
	Accept an incoming connection.
	\param server OBEX handle
	\param eventcb Event callback for client (use NULL for same as server)
	\param data Userdata for client (use NULL for same as server)
	\return the client instance or NULL on error

	Create a new OBEX instance to handle the incomming connection.
	The old OBEX instance will continue to listen for new connections.
	The two OBEX instances become totally independant from each other.

	This function should be called after the library generates
	an #OBEX_EV_ACCEPTHINT event to the user, but before the user
	start to pull data out of the incomming connection.

	Using this function also requires that the OBEX handle was created
	with the #OBEX_FL_KEEPSERVER flag set while calling #OBEX_Init().
 */
LIB_SYMBOL
obex_t *CALLAPI OBEX_ServerAccept(obex_t *server, obex_event_t eventcb,
								void *data)
{
	obex_t *self;

	DEBUG(3, "\n");

	obex_return_val_if_fail(server != NULL, NULL);

	/* If we have started receiving something, it's too late... */
	if (server->object != NULL)
		return NULL;

	/* Set callback and callback data as needed */
	if (eventcb == NULL)
		eventcb = server->eventcb;
	if (data == NULL)
		data = server->userdata;

	/* Allocate new instance */
	self = obex_create(eventcb, server->init_flags);
	if (self == NULL)
		return NULL;

	self->userdata = data;
	if (!obex_transport_accept(self, server))
		goto out_err;

	self->mtu_tx = server->mtu_tx;
	if (obex_set_mtu(self, server->mtu_rx, server->mtu_tx_max))
		goto out_err;

	self->mode = OBEX_MODE_SERVER;
        self->state = STATE_IDLE;
	self->rsp_mode = server->rsp_mode;

	return self;

out_err:
	obex_destroy(self);
	return NULL;
}

/**
	Set the timeout for read/write operations if supported by the underlying
	transport.
	\param self OBEX handle
	\param timeout Maximum time to wait in milliseconds (-1 for infinite)
 */
LIB_SYMBOL
void CALLAPI OBEX_SetTimeout(obex_t *self, int64_t timeout)
{
	obex_return_if_fail(self != NULL);
	obex_transport_set_timeout(self, timeout);	
}

/**
	Let the OBEX parser do some work.
	\param self OBEX handle
	\return -1 on error, 0 on timeout, positive on success

	Let the OBEX parser read and process incoming data, or prepare and send
	outgoing data.

	When a request has been sent (client) or you are waiting for an incoming
	request (server) you should call this function until the request has
	finished.
 */
LIB_SYMBOL
int CALLAPI OBEX_Work(obex_t *self)
{
	DEBUG(4, "\n");
	obex_return_val_if_fail(self != NULL, -1);
	return obex_work(self);
}

/**
	Determine data direction of OBEX_Work()
	\param self OBEX handle

	Use this to achieve better integration of OBEX_Work() into mainloops.
	You should call this after each call of OBEX_Work().
	If #OBEX_DATA_NONE is returned, it depends on your event callback when
	to re-enable mainloop events.
 */
LIB_SYMBOL
enum obex_data_direction CALLAPI OBEX_GetDataDirection(obex_t *self)
{
	DEBUG(4, "\n");
	return obex_get_data_direction(self);
}

/**
	Let the OBEX parser do some work.
	\param self OBEX handle
	\param timeout Maximum time to wait in seconds (-1 for infinite)
	\return -1 on error, 0 on timeout, positive on success

	Deprecated.

	Let the OBEX parser read and process incoming data and send the response.

	The timeout parameter is only for the reading part, preparing and sending
	can take any amount of time.
 */
LIB_SYMBOL
int CALLAPI OBEX_HandleInput(obex_t *self, int timeout)
{
	int result;
	int64_t oldTimeout;
	enum obex_data_direction dir;

	obex_return_val_if_fail(self != NULL, -1);

	DEBUG(4, "\n");

	oldTimeout = obex_transport_get_timeout(self);
	dir = obex_get_data_direction(self);
	obex_transport_set_timeout(self, timeout*1000);

	if (dir == OBEX_DATA_IN) {
		result = obex_work(self);
		if (result <= 0) /* timeout or error */
			goto timeout_or_error;
		dir = obex_get_data_direction(self);
	}

	/* make the following loop more efficient */
	obex_transport_set_timeout(self, -1);

	while (dir == OBEX_DATA_NONE || dir == OBEX_DATA_OUT) {
		result = obex_work(self);
		if (result < 0) /* error */
			goto timeout_or_error;
		dir = obex_get_data_direction(self);
	}

	result = 1;

timeout_or_error:
	obex_transport_set_timeout(self, oldTimeout);
	return result;
}

/**
	Feed OBEX with data when using a custom transport.
	\param self OBEX handle
	\param inputbuf Pointer to custom data
	\param actual Length of buffer
	\return -1 on error
 */
LIB_SYMBOL
int CALLAPI OBEX_CustomDataFeed(obex_t *self, uint8_t *inputbuf, int actual)
{
	DEBUG(3, "\n");

	obex_return_val_if_fail(self != NULL, -1);

	if (inputbuf && actual > 0)
		buf_append(self->rx_msg, inputbuf, (size_t)actual);

	return obex_data_indication(self);
}

/**
	Try to connect to peer.
	\param self OBEX handle
	\param saddr Address to connect to
	\param addrlen Length of address
	\return -1 on error
 */
LIB_SYMBOL
int CALLAPI OBEX_TransportConnect(obex_t *self, struct sockaddr *saddr, int addrlen)
{
	DEBUG(4, "\n");

	obex_return_val_if_fail(self != NULL, -1);
	obex_return_val_if_fail((addrlen == 0) || (saddr != NULL), -1);

	if (addrlen != 0 && saddr != NULL &&
	    !obex_transport_set_remote_addr(self, saddr, addrlen))
		return -1;

	return obex_transport_connect_request(self)? 1: -1;
}

/**
	Disconnect transport.
	\param self OBEX handle
	\return -1 on error
 */
LIB_SYMBOL
int CALLAPI OBEX_TransportDisconnect(obex_t *self)
{
	DEBUG(4, "\n");
	obex_return_val_if_fail(self != NULL, -1);

	obex_transport_disconnect(self);

	return 0;
}

/**
	Get transport file descriptor.
	\param self OBEX handle
	\return file descriptor of the transport, -1 on error

	Returns the file descriptor of the transport or -1 on error.
	Note that not all transports have a file descriptor, especially
	USB and custom transports do not.

	The returned filehandle can be used to do select() on, before
	calling OBEX_HandleInput()

	There is one subtelty about this function. When the OBEX connection is
	established, it returns the connection filedescriptor, while for
	an unconnected server it will return the listening filedescriptor.
	This mean that after receiving an incomming connection, you need to
	call this function again.
 */
LIB_SYMBOL
int CALLAPI OBEX_GetFD(obex_t *self)
{
	obex_return_val_if_fail(self != NULL, -1);

	return obex_transport_get_fd(self);
}

/**
	Schedule a request (as client).
	\param self OBEX handle
	\param object Object containing request
	\return 0 on success or a negative error code on failure (-EBUSY, -EINVAL, -EIO)
 */
LIB_SYMBOL
int CALLAPI OBEX_Request(obex_t *self, obex_object_t *object)
{
	result_t result;

	DEBUG(4, "\n");

	obex_return_val_if_fail(self != NULL, -EINVAL);
	obex_return_val_if_fail(object != NULL, -EINVAL);

	if (self->object) {
		DEBUG(1, "We are busy.\n");
		return -EBUSY;
	}

	object->rsp_mode = self->rsp_mode;
	self->object = object;
	self->mode = OBEX_MODE_CLIENT;
        self->state = STATE_REQUEST;
	self->substate = SUBSTATE_TX_PREPARE;

	/* Prepare the packet but do not send it */
	result = obex_client(self);
	if (result < 0) {
		self->object = NULL;
		self->mode = OBEX_MODE_SERVER;
		self->state = STATE_IDLE;
		return -EIO;
	}

	return 0;
}

/**
	Cancel an ongoing operation.
	\param self OBEX handle
	\param nice If true an OBEX Abort will be sent if beeing client
	or respond with an error if beeing server.
	\return -1 on error
 */
LIB_SYMBOL
int CALLAPI OBEX_CancelRequest(obex_t *self, int nice)
{
	obex_return_val_if_fail(self != NULL, -1);
	return obex_cancelrequest(self, nice);
}

/**
	Suspend transfer of an object.
	\param self OBEX handle
	\param object object to suspend (NULL to suspend currently transfered object)
	\return -1 on error
 */
LIB_SYMBOL
int CALLAPI OBEX_SuspendRequest(obex_t *self, obex_object_t *object)
{
	obex_return_val_if_fail(object != NULL || self->object != NULL, -1);
	return obex_object_suspend(object ? object : self->object);
}

/**
	Resume transfer of an object.
	\param self OBEX handle
	\return -1 on error
 */
LIB_SYMBOL
int CALLAPI OBEX_ResumeRequest(obex_t *self)
{
	obex_return_val_if_fail(self->object != NULL, -1);
	return obex_object_resume(self->object);
}

/**
	Set the OBEX response mode.
	\param self OBEX context
	\param rsp_mode see OBEX_RSP_MODE_*

	If you want any mode other than default (#OBEX_RSP_MODE_NORMAL), you
	have to call this function. If you want it to affect the current object,
	you need to call it at the first #OBEX_EV_PROGRESS (as client) or
	at the #OBEX_EV_REQHINT or #OBEX_EV_REQCHECK events (as server).
 */
LIB_SYMBOL
void CALLAPI OBEX_SetReponseMode(obex_t *self, enum obex_rsp_mode rsp_mode)
{
	switch (rsp_mode) {
	case OBEX_RSP_MODE_NORMAL:
	case OBEX_RSP_MODE_SINGLE:
		self->rsp_mode = rsp_mode;
		if (self->object)
			self->object->rsp_mode = rsp_mode;
		break;

	default:
		break;
	}
}

/**
	Create a new OBEX Object.
	\param self OBEX handle
	\param cmd command of object
	\return pointer to a new OBEX Object, NULL on error
 */
LIB_SYMBOL
obex_object_t * CALLAPI OBEX_ObjectNew(obex_t *self, uint8_t cmd)
{
	obex_object_t *object;

	obex_return_val_if_fail(self != NULL, NULL);

	object = obex_object_new();
	if (object == NULL)
		return NULL;

	obex_object_setcmd(object, cmd);
	/* Need some special woodoo magic on connect-frame */
	if (cmd == OBEX_CMD_CONNECT) {
		if (obex_insert_connectframe(self, object) < 0) {
			obex_object_delete(object);
			object = NULL;
		}
	}

	return object;
}

/**
	Delete an OBEX object.
	\param self OBEX handle
	\param object object to delete.
	\return -1 on error

	Note that as soon as you have passed an object to the lib using
	OBEX_Request(), you shall not delete it yourself.
 */
LIB_SYMBOL
int CALLAPI OBEX_ObjectDelete(obex_t *self, obex_object_t *object)
{
	obex_return_val_if_fail(object != NULL, -1);
	return obex_object_delete(object);
}

/**
	Get available space in object.
	\param self OBEX handle
	\param object OBEX object to query
	\param flags OBEX_FL_FIT_ONE_PACKET or 0
	\return -1 on error

	Returns the available space in a given obex object.

	This can be useful e.g. if the caller wants to check the size of the
	biggest body header that can be added to the current packet.

 */
LIB_SYMBOL
int CALLAPI OBEX_ObjectGetSpace(obex_t *self, obex_object_t *object,
				unsigned int flags)
{
	obex_return_val_if_fail(self != NULL, -1);
	obex_return_val_if_fail(object != NULL, -1);
	return obex_msg_getspace(self, object, flags);
}

/**
	Attach a header to an object.
	\param self OBEX handle
	\param object OBEX object
	\param hi Header identifier
	\param hv Header value
	\param hv_size Header size
	\param flags See obex.h for possible values
	\return -1 on error

	Add a new header to an object.

	If you want all headers to fit in one packet, use the flag
	#OBEX_FL_FIT_ONE_PACKET on all headers you add to an object.

	To stream a body add a body header with hv.bs = NULL and set the flag
	#OBEX_FL_STREAM_START. You will now get #OBEX_EV_STREAMEMPTY events as
	soon as the the parser wants you to feed it with more data.

	When you get an #OBEX_EV_STREAMEMPTY event give the parser some data by
	adding a body-header and set the flag #OBEX_FL_STREAM_DATA. When you
	have no more data to send set the flag #OBEX_FL_STREAM_DATAEND instead.

	After adding a header you are free to do whatever you want with
	the buffer if you are NOT streaming. If you are streaming you
	may not touch the buffer until you get another
	#OBEX_EV_STREAMEMPTY or until the request finishes.

	The headers will be sent in the order you add them.
 */
LIB_SYMBOL
int CALLAPI OBEX_ObjectAddHeader(obex_t *self, obex_object_t *object, uint8_t hi,
				obex_headerdata_t hv, uint32_t hv_size,
				unsigned int flags)
{
	obex_return_val_if_fail(self != NULL, -1);
	obex_return_val_if_fail(object != NULL, -1);
	return obex_object_addheader(self, object, hi, hv, hv_size, flags);
}

/**
	Get next available header from an object.
	\param self OBEX handle (ignored)
	\param object OBEX object
	\param hi Pointer to header identifier
	\param hv Pointer to hv
	\param hv_size Pointer to hv_size
	\return 0 when no more headers are available, -1 on error

	Returns 0 when no more headers are available.

	All headers are read-only.

	You will get the headers in the received order.
 */
LIB_SYMBOL
int CALLAPI OBEX_ObjectGetNextHeader(obex_t *self, obex_object_t *object, uint8_t *hi,
					obex_headerdata_t *hv,
					uint32_t *hv_size)
{
	obex_return_val_if_fail(object != NULL, -1);
	return obex_object_getnextheader(object, hi, hv, hv_size);
}

/**
	Allow the user to parse again the rx headers.
	\param self OBEX handle (ignored)
	\param object OBEX object
	\return 1 on success, 0 if previous parsing not completed, -1 on error

	Next call to #OBEX_ObjectGetNextHeader() will return the first received
	header.
 */
LIB_SYMBOL
int CALLAPI OBEX_ObjectReParseHeaders(obex_t *self, obex_object_t *object)
{
	obex_return_val_if_fail(object != NULL, -1);
	return obex_object_reparseheaders(object);
}

/**
	Read data from body stream.
	\param self OBEX handle
	\param object OBEX object (ignored)
	\param buf A pointer to a pointer which this function will set
	to a buffer which shall be read (and ONLY read) after this
	function returns.
	\return number of bytes in buffer, or 0 for end-of-stream, -1 on error

	To recieve the body as a stream call this function with buf = NULL
	as soon as you get an #OBEX_EV_REQHINT event.

	You will now recieve #OBEX_EV_STREAMAVAIL events when data is
	available for you. Call this function to get the data.

	Note! When receiving a stream data is not buffered so if you
	don't call this function when you get an #OBEX_EV_STREAMAVAIL
	event data will be lost.
 */
LIB_SYMBOL
int CALLAPI OBEX_ObjectReadStream(obex_t *self, obex_object_t *object,
							const uint8_t **buf)
{
	size_t size = 0;

	obex_return_val_if_fail(self != NULL, -1);

	object = self->object;
	obex_return_val_if_fail(object != NULL, -1);

	/* Enable streaming */
	if (buf == NULL) {
		struct obex_body *b = obex_body_stream_create(self);
		int result = obex_object_set_body_receiver(object, b);

		if (!result)
			return -1;
		DEBUG(4, "Streaming is enabled!\n");
		return 0;
	}

	if (buf)
		*buf = obex_object_read_body(object, &size);

	return (int)size;
}

/**
	Sets the response to a received request.
	\param object OBEX object
	\param rsp Respose code in non-last packets
	\param lastrsp Response code in last packet
	\return -1 on error
 */
LIB_SYMBOL
int CALLAPI OBEX_ObjectSetRsp(obex_object_t *object, uint8_t rsp,
							uint8_t lastrsp)
{
	obex_return_val_if_fail(object != NULL, -1);
	return obex_object_setrsp(object, rsp, lastrsp);
}

/**
	Get any data which was before headers.
	\param object OBEX object
	\param buffer Pointer to a pointer which will point to a
	read-only buffer
	\return size of the buffer or -1 for error
 */
LIB_SYMBOL
int CALLAPI OBEX_ObjectGetNonHdrData(obex_object_t *object, uint8_t **buffer)
{
	obex_return_val_if_fail(object != NULL, -1);
	if (!object->rx_nonhdr_data)
		return 0;

	*buffer = buf_get(object->rx_nonhdr_data);
	return buf_get_length(object->rx_nonhdr_data);
}

/**
	Set data to send before headers.
	\param object OBEX object
	\param buffer Data to send
	\param len Length to data
	\return 1 on success, -1 on error

	Some commands (notably SetPath) send data before headers. Use this
	function to set this data.
 */
LIB_SYMBOL
int CALLAPI OBEX_ObjectSetNonHdrData(obex_object_t *object,
						const uint8_t *buffer,
						unsigned int len)
{
	/* TODO: Check that we actually can send len bytes without
	 * violating MTU */

	obex_return_val_if_fail(object != NULL, -1);
	obex_return_val_if_fail(buffer != NULL, -1);

	if (object->tx_nonhdr_data)
		return -1;

	object->tx_nonhdr_data = membuf_create(len);
	if (object->tx_nonhdr_data == NULL)
		return -1;

	buf_append(object->tx_nonhdr_data, buffer, len);

	return 1;
}

/**
	Set headeroffset.
	\param object OBEX object
	\param offset Desired offset
	\return 1 on success, -1 on error

	Call this function when you get a OBEX_EV_REQHINT and you know that the
	command has data before the headers comes. You do NOT need to use this
	function on Connect and SetPath, they are handled automatically.
 */
LIB_SYMBOL
int CALLAPI OBEX_ObjectSetHdrOffset(obex_object_t *object, unsigned int offset)
{
	obex_return_val_if_fail(object != NULL, -1);
	object->headeroffset = offset;
	return 1;
}

/**
	Get the OBEX commmand of an object.
	\param self OBEX context
	\param object OBEX object (or NULL to access the current object)
	\return -1 on error

	Call this function to get the OBEX command of an object.
 */
LIB_SYMBOL
int CALLAPI OBEX_ObjectGetCommand(obex_t *self, obex_object_t *object)
{
	obex_return_val_if_fail(object != NULL || self->object != NULL, -1);

	if (object)
		return object->cmd;

	return self->object->cmd;
}

/**
	Return a human understandable string from a response-code.
	\param rsp Response code.
	\return static response code string

	The returned string must not be freed.
	If the response code is unknown, the string "Unknown response" will
	be returned.
 */
LIB_SYMBOL
char * CALLAPI OBEX_ResponseToString(int rsp)
{
	DEBUG(4, "\n");

	return obex_response_to_string(rsp);
}

/**
	Set customdata of an OBEX handle.
	\param self OBEX handle
	\param data Custom Transport data
	\return 0 on success, -1 on error

	Note : this call is *reserved* to the Custom Transport and
	should not be use by the user/client. It allow to update the
	Custom Transport data originally set via
	OBEX_RegisterCTransport().
	The Custom Transport data (or instance handle) is used to store
	data relative to the specific instance (i.e. connection), such
	as file descriptors, offsets and others, so that the Custom
	Transport can manage multiple connections transparently (i.e.
	without a lookup table).
	- Jean II
 */
LIB_SYMBOL
int CALLAPI OBEX_SetCustomData(obex_t *self, void *data)
{
	obex_return_val_if_fail(self == NULL, -1);

	custom_set_data(self, data);
	return 0;
}

/**
	Read the customdata from an OBEX handle.
	\param self OBEX handle
	\return custom transport data, NULL on error
 */
LIB_SYMBOL
void * CALLAPI OBEX_GetCustomData(obex_t *self)
{
	obex_return_val_if_fail(self == NULL, NULL);

	return custom_get_data(self);
}

/**
	Start listening for incoming TCP connections.
	\param self OBEX handle
	\param addr Address to bind to (*:650 if NULL)
	\param addrlen Length of address structure
	\return -1 on error

	An easier server function to use for TCP/IP (TcpOBEX) only.
	It supports IPv4 (AF_INET) and IPv6 (AF_INET6).
	Note: INADDR_ANY will get mapped to IN6ADDR_ANY and using port 0
	will select the default OBEX port.
 */
LIB_SYMBOL
int CALLAPI TcpOBEX_ServerRegister(obex_t *self, struct sockaddr *addr,
								int addrlen)
{
	DEBUG(3, "\n");

	errno = EINVAL;
	obex_return_val_if_fail(self != NULL, -1);

	inobex_prepare_listen(self, addr, addrlen);

	return obex_transport_listen(self)? 1: -1;
}

/**
	Connect TCP transport.
	\param self OBEX handle
	\param addr Address to connect to ([::1]:650 if NULL)
	\param addrlen Length of address structure
	\return -1 on error

	An easier connect function to use for TCP/IP (TcpOBEX) only.
	It supports IPv4 (AF_INET) and IPv6 (AF_INET6).
 */
LIB_SYMBOL
int CALLAPI TcpOBEX_TransportConnect(obex_t *self, struct sockaddr *addr,
								int addrlen)
{
	DEBUG(4, "\n");

	errno = EINVAL;
	obex_return_val_if_fail(self != NULL, -1);

	if (self->object) {
		DEBUG(1, "We are busy.\n");
		errno = EBUSY;
		return -1;
	}

	inobex_prepare_connect(self, addr, addrlen);

	return obex_transport_connect_request(self)? 1: -1;
}

/**
	Start listening for incoming connections.
	\param self OBEX handle
	\param service Service to bind to.
	\return -1 or negative error code on error

	An easier server function to use for IrDA (IrOBEX) only.
 */
LIB_SYMBOL
int CALLAPI IrOBEX_ServerRegister(obex_t *self, const char *service)
{
	DEBUG(3, "\n");

	obex_return_val_if_fail(self != NULL, -1);
	obex_return_val_if_fail(service != NULL, -1);

#ifdef HAVE_IRDA
	irobex_prepare_listen(self, service);
	return obex_transport_listen(self)? 1: -1;
#else
	return -ESOCKTNOSUPPORT;
#endif /* HAVE_IRDA */
}

/**
	Connect Irda transport.
	\param self OBEX handle
	\param service IrIAS service name to connect to
	\return -1 or negative error code on error

	An easier connect function to use for IrDA (IrOBEX) only.
 */
LIB_SYMBOL
int CALLAPI IrOBEX_TransportConnect(obex_t *self, const char *service)
{
	obex_interface_t *intf;
	int err;

	DEBUG(4, "\n");

	err = OBEX_EnumerateInterfaces(self);
	if (err <= 0)
		return -1;

	intf = OBEX_GetInterfaceByIndex(self, 0);
	intf->irda.service = service;

	return OBEX_InterfaceConnect(self, intf);
}

/**
	Start listening for incoming connections.
	\param self OBEX handle
	\param src source address to listen on
	\param channel source channel to listen on
	\return -1 or negative error code on error

	An easier server function to use for Bluetooth (Bluetooth OBEX) only.
 */
LIB_SYMBOL
int CALLAPI BtOBEX_ServerRegister(obex_t *self, const bt_addr_t *src, uint8_t channel)
{
	DEBUG(3, "\n");

	obex_return_val_if_fail(self != NULL, -1);

#ifdef HAVE_BLUETOOTH
	if (src == NULL)
		src = BDADDR_ANY;
	btobex_prepare_listen(self, src, channel);
	return obex_transport_listen(self)? 1: -1;
#else
	return -ESOCKTNOSUPPORT;
#endif /* HAVE_BLUETOOTH */
}

/**
	Connect Bluetooth transport.
	\param self OBEX handle
	\param src source address to connect from
	\param dst destination address to connect to
	\param channel destination channel to connect to
	\return -1 or negative error code on error

	An easier connect function to use for Bluetooth (Bluetooth OBEX) only.
 */
LIB_SYMBOL
int CALLAPI BtOBEX_TransportConnect(obex_t *self, const bt_addr_t *src,
					const bt_addr_t *dst, uint8_t channel)
{
	DEBUG(4, "\n");

	obex_return_val_if_fail(self != NULL, -1);

	if (self->object) {
		DEBUG(1, "We are busy.\n");
		return -EBUSY;
	}

	obex_return_val_if_fail(dst != NULL, -1);

#ifdef HAVE_BLUETOOTH
	if (src == NULL)
		src = BDADDR_ANY;
	btobex_prepare_connect(self, src, dst, channel);
	return obex_transport_connect_request(self)? 1: -1;
#else
	return -ESOCKTNOSUPPORT;
#endif /* HAVE_BLUETOOTH */
}

/*
	FdOBEX_TransportSetup - setup descriptors for OBEX_TRANS_FD transport.

	\param self OBEX handle
	\param rfd descriptor to read
	\param wfd descriptor to write
	\param mtu (ignored)
	\return -1 or negative error code on error
 */
LIB_SYMBOL
int CALLAPI FdOBEX_TransportSetup(obex_t *self, int rfd, int wfd, int mtu)
{
	DEBUG(4, "\n");

	obex_return_val_if_fail(self != NULL, -1);

	if (self->object) {
		DEBUG(1, "We are busy.\n");
		return -EBUSY;
	}
	fdobex_set_fd(self, rfd, wfd);
	return obex_transport_connect_request(self)? 1: -1;
}

/**
	Connect USB interface.
	\param self OBEX handle
	\param intf USB interface to connect to
	\return -1 or negative error code on error

	An easier connect function to connect to a discovered interface
	(currently USB OBEX only).
 */
LIB_SYMBOL
int CALLAPI OBEX_InterfaceConnect(obex_t *self, obex_interface_t *intf)
{
	DEBUG(4, "\n");

	obex_return_val_if_fail(self != NULL, -1);

	if (self->object) {
		DEBUG(1, "We are busy.\n");
		return -EBUSY;
	}

	obex_return_val_if_fail(intf != NULL, -1);
	if (self->trans->ops->client.select_interface) {
		if (!self->trans->ops->client.select_interface(self, intf))
			return -1;
		return obex_transport_connect_request(self)? 1: -1;
	} else
		return -ESOCKTNOSUPPORT;
}

/**
	Find OBEX interfaces on the system.
	\param self OBEX handle
	\return the number of OBEX interfaces.
 */
LIB_SYMBOL
int CALLAPI OBEX_EnumerateInterfaces(obex_t *self)
{
	DEBUG(4, "\n");

	obex_return_val_if_fail(self != NULL, -1);

	OBEX_FreeInterfaces(self);
	obex_transport_enumerate(self);

	return self->interfaces_number;
}

/**
	Get OBEX interface information.
	\param self OBEX handle
	\param i interface number
	\return OBEX interface information.
 */
LIB_SYMBOL
obex_interface_t * CALLAPI OBEX_GetInterfaceByIndex(obex_t *self, int i)
{
	DEBUG(4, "\n");

	obex_return_val_if_fail(self != NULL, NULL);

	if (i >= self->interfaces_number || i < 0)
		return NULL;
	return &self->interfaces[i];
}

/**
	Free memory allocated to OBEX interface structures.
	\param self OBEX handle

	Frees memory allocated to OBEX interface structures after it has been
	allocated by OBEX_EnumerateInterfaces.
 */
LIB_SYMBOL
void CALLAPI OBEX_FreeInterfaces(obex_t *self)
{
	obex_return_if_fail(self != NULL);

	obex_transport_free_interfaces(self);
}
