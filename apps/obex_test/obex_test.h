/**
	\file apps/obex_test.h
	Test IrOBEX, TCPOBEX and OBEX over cable to R320s.
	OpenOBEX test applications and sample code.

 */

#ifndef OBEX_TEST_H
#define OBEX_TEST_H

struct context
{
	int serverdone;
	int clientdone;
	char *get_name;	/* Name of last get-request */
};
int read_input(char *answer, size_t size, const char *question, ...);

#endif
