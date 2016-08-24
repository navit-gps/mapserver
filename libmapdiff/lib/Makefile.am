## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" \
	-I../include

#if WIN32
#AM_CPPFLAGS + = -I../../include
#endif

AM_CFLAGS =\
	 -Wall\
	 -g

lib_LTLIBRARIES = libmapdiff.la


libmapdiff_la_SOURCES = \
	poccore.c

libmapdiff_la_LDFLAGS = 

libmapdiff_la_LIBADD = 

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmapdiff.pc

EXTRA_DIST = \
	libmapdiff.pc.in

