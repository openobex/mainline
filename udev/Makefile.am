
if UDEV_SUPPORT
sbin_PROGRAMS = obex-check-device
obex_check_device_LDADD = $(top_builddir)/lib/libopenobex.la
AM_CFLAGS = @OPENOBEX_CFLAGS@
INCLUDES = -I$(top_srcdir)/include
endif

EXTRA_DIST = CMakeLists.txt openobex.rules.in

MAINTAINERCLEANFILES = Makefile.in

dnl FIXME: automake is not flexible enought to install to /lib/udev/?
