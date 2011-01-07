html:
	doxygen $(top_builddir)/doc/Doxyfile

clean-local:
	-rm -rf $(top_builddir)/doc/html

if APPS_DOC
man_MANS = ircp.1 irobex_palm3.1 irxfer.1 obex_find.1 obex_tcp.1 obex_test.1

$(man_MANS): $(top_srcdir)/doc/openobex-apps.xml
	$(XMLTO) -o $(top_builddir)/doc --stringparam "use.id.as.filename=1" man $<

CLEANFILES = $(man_MANS)
endif

EXTRA_DIST = openobex.coverpage CMakeLists.txt Doxyfile.in openobex-apps.xml
MAINTAINERCLEANFILES = Makefile.in
