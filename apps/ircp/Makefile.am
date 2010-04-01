
if APPS

bin_PROGRAMS = ircp

ircp_SOURCES = \
	ircp.c ircp.h \
	ircp_client.c ircp_client.h \
	ircp_server.c ircp_server.h \
	ircp_io.c ircp_io.h \
	dirtraverse.c dirtraverse.h \
	debug.h

AM_CFLAGS = @OPENOBEX_CFLAGS@

LDADD = $(top_builddir)/lib/libopenobex.la

INCLUDES = -I$(top_srcdir)/include
endif

EXTRA_DIST = CMakeLists.txt

MAINTAINERCLEANFILES = Makefile.in
