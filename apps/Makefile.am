
if APPS
bin_PROGRAMS = irobex_palm3 irxfer obex_tcp

irobex_palm3_LDADD = $(builddir)/lib/libmisc.a $(top_builddir)/lib/libopenobex.la
irxfer_LDADD = $(builddir)/lib/libmisc.a $(top_builddir)/lib/libopenobex.la
obex_tcp_LDADD = $(builddir)/lib/libmisc.a $(top_builddir)/lib/libopenobex.la

AM_CFLAGS = @OPENOBEX_CFLAGS@

INCLUDES = -I$(srcdir)/lib -I$(top_srcdir)/include @BLUETOOTH_CFLAGS@
endif

SUBDIRS = lib obex_test ircp

EXTRA_DIST = CMakeLists.txt

MAINTAINERCLEANFILES = Makefile.in
