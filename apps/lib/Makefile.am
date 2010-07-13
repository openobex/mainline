
if APPS
noinst_LIBRARIES = libmisc.a

libmisc_a_SOURCES = \
	obex_io.c obex_io.h \
	obex_put_common.c obex_put_common.h

AM_CFLAGS = @OPENOBEX_CFLAGS@

INCLUDES = -I$(top_srcdir)/include @BLUETOOTH_CFLAGS@
endif

EXTRA_DIST = CMakeLists.txt

MAINTAINERCLEANFILES = Makefile.in
