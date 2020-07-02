dnl Process this file with autoconf to produce a configure script.
AC_INIT([dbus-scripts], [1.0], [g+770@cobb.uk.net])
AM_INIT_AUTOMAKE([foreign])
AM_MAINTAINER_MODE

AC_PROG_CC
AC_CONFIG_HEADERS([config.h])

DEP_MODULES="glib-2.0 dbus-1 dbus-glib-1"
AC_PROG_LIBTOOL

PKG_CHECK_MODULES(DEPS, $DEP_MODULES)
AC_SUBST(DEPS_FLAGS)

AC_OUTPUT([
dbus-scripts
Makefile
src/Makefile
])
