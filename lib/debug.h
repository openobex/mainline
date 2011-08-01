#ifndef OPENOBEX_DEBUG_H
#define OPENOBEX_DEBUG_H

#if defined(_MSC_VER) && _MSC_VER < 1400
void log_debug(char *format, ...);
#define log_debug_prefix ""

#elif defined(OBEX_SYSLOG) && !defined(_WIN32)
#include <syslog.h>
#define log_debug(format, ...) syslog(LOG_DEBUG, format, ## __VA_ARGS__)
#define log_debug_prefix "OpenOBEX: "

#else
#include <stdio.h>
#define log_debug(format, ...) fprintf(stderr, format, ## __VA_ARGS__)
#define log_debug_prefix ""
#endif

/* use integer:  0 for production
 *               1 for verification
 *              >2 for debug
 */
extern int obex_debug;

#if defined(_MSC_VER) && _MSC_VER < 1400
void DEBUG(int n, const char *format, ...);

#else
#  define DEBUG(n, format, ...) \
          if (obex_debug >= (n)) \
            log_debug("%s%s(): " format, log_debug_prefix, __FUNCTION__, ## __VA_ARGS__)
#endif


/* use bitmask: 0x1 for sendbuff
 *              0x2 for receivebuff
 */
extern int obex_dump;

#define DUMPBUFFER(n, label, msg) \
        if ((obex_dump & 0x3) & (n)) buf_dump(msg, label);

#endif
