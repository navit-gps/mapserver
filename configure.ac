dnl Process this file with autoconf to produce a configure script.

AC_INIT(navit_netwd, 0.0.1)

AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.11])

AM_SILENT_RULES([yes])

AC_PROG_CC

LT_INIT

dnl  *****************
dnl ***** depends *****
dnl  *****************

dnl **NOTE:**
dnl postgres will be used as a stogae backend first
dnl later sqlite will be added ...

dnl ************************
dnl *libpq Postgres C API***
dnl *CT_CHECK_POSTGRES_DB()*
dnl ************************

AC_MSG_CHECKING(for PostgreSQL headers)
for d in /usr /usr/local /usr/local/pgsql; do
    test -f $d/include/libpq-fe.h && {
         AC_SUBST(PGSQL_INC)
         PGSQL_INC="-I$d/include"
         PG_DIR="$d"
         AC_MSG_RESULT($d/include)
    }
    test -f $d/include/postgresql/libpq-fe.h && {
         AC_SUBST(PGSQL_INC)
         PGSQL_INC="-I$d/include/postgresql"
         PG_DIR="$d"
         AC_MSG_RESULT($d/include/postgresql)
    }
    test -f $d/include/postgresql/8.0/libpq-fe.h && {
         AC_SUBST(PGSQL_INC)
         PGSQL_INC="-I$d/include/postgresql/8.0"
         PG_DIR="$d"
         AC_MSG_RESULT($d/include/postgresql/8.0)
    }
    test -f $d/include/pgsql/libpq-fe.h && {
         AC_SUBST(PGSQL_INC)
         PGSQL_INC="-I$d/include/pgsql"
         PG_DIR="$d"
         AC_MSG_RESULT($d/include/pgsql)
    }
done
old_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS -L$PG_DIR/lib"
AC_CHECK_LIB(pq, PQexecParams,
[AC_SUBST(PGSQL_LIB)
PGSQL_LIB="-L$PG_DIR/lib"],
[AC_MSG_ERROR(could not determine PostgreSQL library location)])
LDFLAGS="$old_LDFLAGS"
AC_SUBST(PGSQL_LIB)

PKG_CHECK_MODULES(OPENSSL, [openssl])
dnl from 0.5.0 tdata and sexpr will goes with sxmp, don't check it
PKG_CHECK_MODULES(LIBSXMP, [libsxmp >= 0.5.0])
dnl libydaemon is a part sxmp library bundle, btw sxmp itself doesn't require it i.e. will not linked
dnl with libsxmp
PKG_CHECK_MODULES(LIBYDAEMON, [libydaemon])

AC_OUTPUT([
Makefile
ntsprund/Makefile
modules/Makefile
modules/modsxmp/Makefile
])
