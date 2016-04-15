## AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" \
	-DPREFIX=\""$(prefix)"\" \
	$(OPENSSL_CFLAGS) \
	$(LIBSXMP_CFLAGS) $(LIBYDAEMON_CFLAGS) \
	$(PGSQL_INC) -Iinclude -I../include

AM_CFLAGS = -Wall -g

bin_PROGRAMS = ntsprund
ntsprund_SOURCES = daemon.c

ntsprund_LDADD = \
	$(OPENSSL_LIBS) $(PQSQL_LIB) -lpq -lpthread -ldl \
	$(LIBSXMP_LIBS) $(LIBYDAEMON_LIBS)

