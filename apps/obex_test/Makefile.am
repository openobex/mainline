
if APPS
bin_PROGRAMS = obex_test

obex_test_SOURCES = \
	obex_test.c obex_test.h \
	obex_test_client.c obex_test_client.h \
	obex_test_server.c obex_test_server.h \
	obex_test_cable.c obex_test_cable.h

obex_test_LDADD = $(builddir)/../libmisc.a $(top_builddir)/lib/libopenobex.la @BLUETOOTH_LIBS@

AM_CFLAGS = @OPENOBEX_CFLAGS@

INCLUDES = -I$(srcdir)/.. -I$(top_srcdir)/include @BLUETOOTH_CFLAGS@
endif

EXTRA_DIST = CMakeLists.txt

MAINTAINERCLEANFILES = Makefile.in
