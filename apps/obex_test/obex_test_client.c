/**
	\file apps/obex_test_client.c
 	Client OBEX Commands.
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
#define _XOPEN_SOURCE 500

#include <openobex/obex.h>

#include "obex_io.h"
#include "obex_test_client.h"
#include "obex_test.h"

#if defined(_WIN32)
#include <io.h>
#define read _read
#define write _write
#define open _open
#if defined(_MSC_VER)
static char *basename(const char *s) {
	static char base[_MAX_FNAME+_MAX_EXT] = {0};

	_splitpath(s, NULL, NULL, base, NULL);
	_splitpath(s, NULL, NULL, NULL, base+strlen(base));
	return base;
}
#else /* MinGW compiler */
#include <libgen.h>
#endif

#else
#include <libgen.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define TRUE  1
#define FALSE 0

#define OBEX_STREAM_CHUNK       4096


uint8_t buffer[OBEX_STREAM_CHUNK];

int fileDesc;


//
// Wait for an obex command to finish.
//
static void syncwait(obex_t *handle)
{
	struct context *gt;
	int ret;

	gt = OBEX_GetUserData(handle);

	while(!gt->clientdone) {
		//printf("syncwait()\n");
		ret = OBEX_HandleInput(handle, 10);
		if(ret < 0) {
			printf("Error while doing OBEX_HandleInput()\n");
			break;
		}
	}

	gt->clientdone = FALSE;
}

//
//
//
void client_done(obex_t *handle, obex_object_t *object, int obex_cmd, int obex_rsp)
{
	struct context *gt;
	gt = OBEX_GetUserData(handle);

	switch(obex_cmd)	{
	case OBEX_CMD_CONNECT:
		connect_client_done(handle, object, obex_rsp);
		break;
	case OBEX_CMD_DISCONNECT:
		disconnect_client_done(handle, object, obex_rsp);
		break;
	case OBEX_CMD_PUT:
		put_client_done(handle, object, obex_rsp);
		break;
	case OBEX_CMD_GET:
		get_client_done(handle, object, obex_rsp, gt->get_name);
		break;
	case OBEX_CMD_SETPATH:
		setpath_client_done(handle, object, obex_rsp);
		break;
	}
	gt->clientdone = TRUE;
}

//
//
//
void connect_client(obex_t *handle)
{
	obex_object_t *object;
	obex_headerdata_t hd;
	int err;

	object = OBEX_ObjectNew(handle, OBEX_CMD_CONNECT);
	if(object == NULL) {
		printf("Error\n");
		return;
	}

	hd.bs = (uint8_t *) "Linux";
	if(OBEX_ObjectAddHeader(handle, object, OBEX_HDR_WHO, hd, 6,
				OBEX_FL_FIT_ONE_PACKET) < 0)
	{
		printf("Error adding header\n");
		OBEX_ObjectDelete(handle, object);
		return;
	}
	err = OBEX_Request(handle, object);
	if (err) {
		OBEX_ObjectDelete(handle, object);
		printf("Error: %s\n", strerror(err));
		return;
	}
	syncwait(handle);
}

//
//
//
void connect_client_done(obex_t *handle, obex_object_t *object, int obex_rsp)
{
	uint8_t *nonhdrdata;

	if(obex_rsp == OBEX_RSP_SUCCESS) {
		printf("Connect OK!\n");
		if(OBEX_ObjectGetNonHdrData(object, &nonhdrdata) == 4) {
			printf("Version: 0x%02x. Flags: 0x%02x\n", nonhdrdata[0], nonhdrdata[1]);
		}
	}
	else {
		printf("Connect failed 0x%02x!\n", obex_rsp);
	}
}


//
//
//
void disconnect_client(obex_t *handle)
{
	obex_object_t *object;
	int err;

	object = OBEX_ObjectNew(handle, OBEX_CMD_DISCONNECT);
	if(object == NULL) {
		printf("Error\n");
		return;
	}

	err = OBEX_Request(handle, object);
	if (err) {
		OBEX_ObjectDelete(handle, object);
		printf("Error: %s\n", strerror(err));
		return;
	}
	syncwait(handle);
}

//
//
//
void disconnect_client_done(obex_t *handle, obex_object_t *object, int obex_rsp)
{
	printf("Disconnect done!\n");
	OBEX_TransportDisconnect(handle);
}

int fillstream(obex_t *handle, obex_object_t *object)
{
	int actual;
	obex_headerdata_t hv;

	printf("Filling stream!\n");

	actual = read(fileDesc, buffer, OBEX_STREAM_CHUNK);
	if(actual > 0) {
		/* Read was ok! */
		hv.bs = buffer;
		OBEX_ObjectAddHeader(handle, object, OBEX_HDR_BODY,
				hv, actual, OBEX_FL_STREAM_DATA);
	}
	else if(actual == 0) {
		/* EOF */
		hv.bs = buffer;
		close(fileDesc); fileDesc = -1;
		OBEX_ObjectAddHeader(handle, object, OBEX_HDR_BODY,
				hv, 0, OBEX_FL_STREAM_DATAEND);
	}
	else {
		/* Error */
		hv.bs = NULL;
		close(fileDesc); fileDesc = -1;
		OBEX_ObjectAddHeader(handle, object, OBEX_HDR_BODY,
				hv, 0, OBEX_FL_STREAM_DATA);
	}

	return actual;
}

void push_client(obex_t *handle)
{
	obex_object_t *object;

	char fname[200];
	int uname_size;
	char *bfname;
	uint8_t *uname;

	obex_headerdata_t hd;

	uint8_t *buf;
	int file_size;
	int err;

	if (read_input(fname, sizeof(fname), "PUSH file> ") <= 0) {
		perror("Error reading file name");
		return;
	}

	bfname = strdup(basename(fname));
	if (bfname == NULL) {
		perror("Error");
		return;
	}

	buf = easy_readfile(fname, &file_size);
	if (buf == NULL) {
		printf("file not found: %s\n", fname);
		free(bfname);
		printf("Error\n");
		return;
	}
	fileDesc = open(fname, O_RDONLY, 0);

	if (fileDesc < 0) {
		free(buf);
		free(bfname);
		printf("Error\n");
		return;
	}

	printf("Going to send %s(%s), %d bytes\n",fname,bfname, file_size);

	/* Build object */
	object = OBEX_ObjectNew(handle, OBEX_CMD_PUT);
	if (object == NULL) {
		free(buf);
		free(bfname);
		printf("Error\n");
		return;
	}

	uname_size = (strlen(bfname) + 1) << 1;
	uname = malloc(uname_size);
	uname_size = OBEX_CharToUnicode(uname, (uint8_t *) bfname, uname_size);
	if (uname_size < 0) {
		free(buf);
		free(bfname);
		OBEX_ObjectDelete(handle, object);
		printf("Error\n");
		return;
	}

	hd.bs = uname;
	OBEX_ObjectAddHeader(handle, object, OBEX_HDR_NAME, hd, uname_size, 0);

	hd.bq4 = file_size;
	OBEX_ObjectAddHeader(handle, object, OBEX_HDR_LENGTH, hd, sizeof(uint32_t), 0);

	hd.bs = NULL;
	OBEX_ObjectAddHeader(handle, object, OBEX_HDR_BODY, hd, 0, OBEX_FL_STREAM_START);

	free(buf);
	free(uname);
	free(bfname);

	err = OBEX_Request(handle, object);
	if (err) {
		OBEX_ObjectDelete(handle, object);
		printf("Error: %s\n", strerror(err));
		return;
	}
	syncwait(handle);
}

//
//
//
void put_client(obex_t *handle)
{
	obex_object_t *object;

	char lname[200];
	char rname[200];
	int rname_size;
	obex_headerdata_t hd;

	uint8_t *buf;
	int file_size;
	int err;

	if (read_input(lname, sizeof(lname), "PUT file (local)> ") <= 0) {
		perror("Error reading file name");
		return;
	}


	buf = easy_readfile(lname, &file_size);
	if(buf == NULL) {
		printf("Error: file not found: %s\n", lname);
		return;
	}

	if (read_input(rname, sizeof(rname),
		       "PUT remote filename (default: %s)> ", lname) < 0) {
		free(buf);
		perror("Error reading file name");
		return;
	}
	if (strlen(rname) == 0)
		strcpy(rname, lname);
	printf("Going to send %d bytes\n", file_size);

	/* Build object */
	object = OBEX_ObjectNew(handle, OBEX_CMD_PUT);
	if (object == NULL) {
		free(buf);
		printf("Error\n");
		return;
	}

	rname_size = OBEX_CharToUnicode((uint8_t *) rname, (uint8_t *) rname, sizeof(rname));
	if (rname_size < 0) {
		free(buf);
		OBEX_ObjectDelete(handle, object);
		printf("Error\n");
		return;
	}

	hd.bq4 = file_size;
	OBEX_ObjectAddHeader(handle, object, OBEX_HDR_LENGTH, hd, 4, 0);

	hd.bs = (uint8_t *) rname;
	OBEX_ObjectAddHeader(handle, object, OBEX_HDR_NAME, hd, rname_size, 0);

	hd.bs = buf;
	OBEX_ObjectAddHeader(handle, object, OBEX_HDR_BODY, hd, file_size, 0);

	free(buf);

	err = OBEX_Request(handle, object);
	if (err) {
		OBEX_ObjectDelete(handle, object);
		printf("Error: %s\n", strerror(err));
		return;
	}
	syncwait(handle);
}

//
//
//
void put_client_done(obex_t *handle, obex_object_t *object, int obex_rsp)
{
	if(obex_rsp == OBEX_RSP_SUCCESS) {
		printf("PUT successful!\n");
	}
	else {
		printf("PUT failed 0x%02x!\n", obex_rsp);
	}
}

//
//
//
void get_client(obex_t *handle, struct context *gt)
{
	obex_object_t *object;
	uint8_t rname[200];
	char req_name[200];
	int rname_size;
	obex_headerdata_t hd;
	int err;

	if (read_input(req_name, sizeof(req_name), "GET file> ") <= 0) {
		perror("Error reading file name");
		return;
	}

	object = OBEX_ObjectNew(handle, OBEX_CMD_GET);
	if(object == NULL) {
		printf("Error\n");
		return;
	}

	rname_size = OBEX_CharToUnicode(rname, (uint8_t *) req_name, sizeof(rname));
	if (rname_size < 0) {
		OBEX_ObjectDelete(handle, object);
		printf("Error\n");
		return;
	}

	hd.bs = rname;
	if (OBEX_ObjectAddHeader(handle, object, OBEX_HDR_NAME, hd,
				 rname_size, OBEX_FL_FIT_ONE_PACKET) < 0)
	{
		printf("Error adding header\n");
		OBEX_ObjectDelete(handle, object);
		return;
	}

	/* Remember the name of the file we are getting so we can save
	   it when we get the response */
	gt->get_name = req_name;
	err = OBEX_Request(handle, object);
	if (err) {
		OBEX_ObjectDelete(handle, object);
		printf("Error: %s\n", strerror(err));
		return;
	}
	syncwait(handle);
}

//
//
//
void get_client_done(obex_t *handle, obex_object_t *object, int obex_rsp, char *name)
{
	obex_headerdata_t hv;
	uint8_t hi;
	uint32_t hlen;

	const uint8_t *body = NULL;
	int body_len = 0;

	if(obex_rsp != OBEX_RSP_SUCCESS) {
		printf("GET failed 0x%02x!\n", obex_rsp);
		return;
	}

	while (OBEX_ObjectGetNextHeader(handle, object, &hi, &hv, &hlen))	{
		if(hi == OBEX_HDR_BODY)	{
		printf("%s() Found body\n", __FUNCTION__);
			body = hv.bs;
			body_len = hlen;
			break;
		}
		else	{
			printf("%s() Skipped header %02x\n", __FUNCTION__, hi);
		}
	}


	if(!body) {
		printf("No body found in answer!\n");
		return;
	}
	printf("GET successful!\n");
	safe_save_file(name, body, body_len);
}

//
//
//
void setpath_client(obex_t *handle)
{
	uint8_t setpath_data[2] = { 0, 0 };
	obex_object_t *object;
	char path[200];
	int path_size;
	obex_headerdata_t hd;
	int err;

	if (read_input(path, sizeof(path), "SETPATH> ") <= 0) {
		perror("Error reading path");
		return;
	}

	object = OBEX_ObjectNew(handle, OBEX_CMD_SETPATH);
	if(object == NULL) {
		printf("Error\n");
		return;
	}

	path_size = OBEX_CharToUnicode((uint8_t *) path, (uint8_t *) path, sizeof(path));
	if (path_size < 0) {
		OBEX_ObjectDelete(handle, object);
		printf("Error\n");
		return;
	}

	hd.bs = (uint8_t *) path;
	if (OBEX_ObjectAddHeader(handle, object, OBEX_HDR_NAME, hd,
				 path_size, OBEX_FL_FIT_ONE_PACKET) < 0)
	{
		printf("Error adding header\n");
		OBEX_ObjectDelete(handle, object);
		return;
	}

	OBEX_ObjectSetNonHdrData(object, setpath_data, 2);
	err = OBEX_Request(handle, object);
	if (err) {
		OBEX_ObjectDelete(handle, object);
		printf("Error: %s\n", strerror(err));
		return;
	}
	syncwait(handle);
}

//
//
//
void setpath_client_done(obex_t *handle, obex_object_t *object, int obex_rsp)
{
	if(obex_rsp == OBEX_RSP_SUCCESS) {
		printf("SETPATH successful!\n");
	}
	else {
		printf("SETPATH failed 0x%02x!\n", obex_rsp);
	}
}
