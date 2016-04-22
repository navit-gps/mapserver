## AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" \
	-DPREFIX=\""$(prefix)"\" \
	$(OPENSSL_CFLAGS) \
	$(LIBSXMP_CFLAGS) $(LIBYDAEMON_CFLAGS) \
	-Iinclude -I../include

AM_CFLAGS = -Wall -g

lib_LTLIBRARIES = libmodpsqlbe.la

libmodpsqlbe_la_SOURCES = modpsqlbe.c
libmodpsqlbe_la_LDFLAGS = -Wl,--export-dynamic
libmodpsqlbe_la_LIBADD = \
	$(OPENSSL_LIBS) -lpthread \
	$(LIBSXMP_LIBS) $(LIBYDAEMON_LIBS) $(PGSQL_LIB)

