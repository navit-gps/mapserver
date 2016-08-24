## Process this file with automake to produce Makefile.in

SUBDIRS = include lib

libmapdiffdocdir = ${prefix}/doc/libmapdiff
libmapdiffdoc_DATA = \
	README\
	COPYING\
	AUTHORS\
	ChangeLog\
	INSTALL\
	NEWS

EXTRA_DIST = $(libmapdiffdoc_DATA)

clean-local:


# Remove doc directory on uninstall
uninstall-local:
	-rm -r $(libmapdiffdocdir)
