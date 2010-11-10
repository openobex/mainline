#ifndef OPENOBEX_DEFINES_H
#define OPENOBEX_DEFINES_H

#ifdef TRUE
#undef TRUE
#endif
#define	TRUE		1

#ifdef FALSE
#undef FALSE
#endif
#define FALSE		0

#define obex_return_if_fail(test) \
        do { if (!(test)) return; } while(0)
#define obex_return_val_if_fail(test, val) \
        do { if (!(test)) return val; } while(0)

#define OBEX_VERSION	0x10      /* OBEX Protocol Version 1.1 */

enum obex_mode {
	MODE_SRV,
	MODE_CLI,
};

enum obex_state {
	STATE_IDLE,
	STATE_SEND,
	STATE_REC,
};

#endif
