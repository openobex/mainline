#ifndef OPENOBEX_DEFINES_H
#define OPENOBEX_DEFINES_H

#include <inttypes.h>
#include <stdbool.h>

#ifdef TRUE
#undef TRUE
#endif
#define TRUE true

#ifdef FALSE
#undef FALSE
#endif
#define FALSE false

enum result_type {
	RESULT_ERROR = -1,
	RESULT_TIMEOUT = 0,
	RESULT_SUCCESS = 1,
};
typedef enum result_type result_t;

#define obex_return_if_fail(test) \
        do { if (!(test)) return; } while(0)
#define obex_return_val_if_fail(test, val) \
        do { if (!(test)) return val; } while(0)

#define OBEX_VERSION	0x10      /* OBEX Protocol Version 1.1 */

enum obex_state {
	STATE_IDLE,
	STATE_REQUEST,
	STATE_RESPONSE,
	STATE_ABORT,
};

enum obex_substate {
	SUBSTATE_RX,
	SUBSTATE_TX_PREPARE,
	SUBSTATE_TX,
};

#define OBEX_SRM_FLAG_WAIT_LOCAL  (1 << 0)
#define OBEX_SRM_FLAG_WAIT_REMOTE (1 << 1)

#endif
