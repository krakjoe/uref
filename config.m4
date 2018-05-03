dnl $Id$
dnl config.m4 for extension uref

PHP_ARG_ENABLE(uref, whether to enable uref support,
[  --enable-uref          Enable uref support], no)

if test "$PHP_UREF" != "no"; then
  dnl # In case of no dependencies
  AC_DEFINE(HAVE_UREF, 1, [ Have uref support ])

  PHP_NEW_EXTENSION(uref, php_uref.c, $ext_shared)
fi
