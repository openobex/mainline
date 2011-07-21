#ifndef OBEX_INCL_H
#define OBEX_INCL_H

#include "bluez_compat.h"
#include "visibility.h"

/* This overides the define in openobex/obex.h */
#define OPENOBEX_SYMBOL(retval) LIB_SYMBOL retval CALLAPI

/* Visual Studio C++ Compiler 7.1 does not know about Bluetooth */
#if defined(_MSC_VER) && _MSC_VER < 1400
#define bt_addr_t void
#endif

#include <openobex/obex.h>

#endif
