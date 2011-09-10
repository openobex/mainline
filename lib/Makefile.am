
lib_LTLIBRARIES = libopenobex.la

libopenobex_la_SOURCES = \
	obex.c obex_incl.h \
	obex_main.c obex_main.h \
	obex_connect.c obex_connect.h \
	obex_header.c obex_header.h \
	obex_object.c obex_object.h \
	obex_transport.c obex_transport.h \
	obex_server.c obex_server.h \
	obex_client.c obex_client.h \
	databuffer.c databuffer.h \
	visibility.h defines.h debug.h cloexec.h nonblock.h \
	transport/irobex.c transport/irobex.h \
	transport/irda_wrap.h \
	transport/inobex.c transport/inobex.h \
	transport/btobex.c transport/btobex.h \
	transport/bluez_compat.h \
	transport/fdobex.c transport/fdobex.h \
	transport/customtrans.c transport/customtrans.h \
	transport/usbobex.c transport/usb1obex.c transport/usbobex.h \
	transport/usbutils.c transport/usbutils.h

libopenobex_la_CPPFLAGS = -I$(top_srcdir)/include -I$(srcdir)
libopenobex_la_CFLAGS = -D_GNU_SOURCE @CFLAG_VISIBILITY@ @USB1_CFLAGS@

libopenobex_la_LDFLAGS = \
	-no-undefined \
	-version-info 2:0:0 \
	-export-symbols $(top_srcdir)/lib/obex.sym

libopenobex_la_LIBADD = @USB_LIBS@ @USB1_LIBS@ @EXTRA_LIBS@

EXTRA_DIST = obex.sym win32compat.c CMakeLists.txt openobex.rc.in

MAINTAINERCLEANFILES = Makefile.in
