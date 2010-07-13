
if APPS
noinst_LIBRARIES = libmisc.a

libmisc_a_SOURCES = \
	obex_io.c obex_io.h \
	obex_put_common.c obex_put_common.h

bin_PROGRAMS = irobex_palm3 irxfer obex_tcp

irobex_palm3_LDADD = libmisc.a $(top_builddir)/lib/libopenobex.la
irxfer_LDADD = libmisc.a $(top_builddir)/lib/libopenobex.la
obex_tcp_LDADD = libmisc.a $(top_builddir)/lib/libopenobex.la

AM_CFLAGS = @OPENOBEX_CFLAGS@

INCLUDES = @BLUETOOTH_CFLAGS@ -I$(top_srcdir)/include
endif

SUBDIRS = . obex_test

EXTRA_DIST = CMakeLists.txt

MAINTAINERCLEANFILES = Makefile.in
