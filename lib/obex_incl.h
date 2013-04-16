#ifndef OBEX_INCL_H
#define OBEX_INCL_H

#include "transport/bluez_compat.h"

#if defined(_WIN32)
#  include <winsock2.h> /* make sure to use version 2 of Windows socket API */
#  include <windows.h>
#  define CALLAPI WINAPI
#endif

#include "openobex_export.h"

#ifndef LIB_SYMBOL
#define LIB_SYMBOL OPENOBEX_EXPORT
#endif

#ifndef CALLAPI
#define CALLAPI
#endif

/* This overides the define in openobex/obex.h */
#define OPENOBEX_SYMBOL(retval) LIB_SYMBOL retval CALLAPI

/* Visual Studio C++ Compiler 7.1 does not know about Bluetooth */
#if ( defined(_MSC_VER) && _MSC_VER < 1400 ) || !defined(HAVE_BLUETOOTH)
#define bt_addr_t void
#endif

#include <openobex/obex.h>

#endif
