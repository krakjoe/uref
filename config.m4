dnl $Id$
dnl config.m4 for extension uref

PHP_ARG_ENABLE(uref, whether to enable uref support,
[  --enable-uref          Enable uref support], no)

if test "$PHP_UREF" != "no"; then
  AC_PATH_PROG(LLVM_CONFIG, llvm-config, no)
  PHP_REQUIRE_CXX()
  LLVM_CFLAGS=`$LLVM_CONFIG --cflags`
  LLVM_LIBDIR=`$LLVM_CONFIG --ldflags --link-shared --libs all --system-libs`
  LLVM_CFLAGS+=" -g"
  LLVM_LIBDIR+=" -g"
  PHP_EVAL_LIBLINE($LLVM_LIBDIR, UREF_SHARED_LIBADD)
  PHP_EVAL_INCLINE($LLVM_CFLAGS, UREF_CFLAGS)
  PHP_SUBST(UREF_SHARED_LIBADD)

  AC_DEFINE(HAVE_UREF, 1, [ Have uref support ])

  PHP_NEW_EXTENSION(uref, php_uref.cc, $ext_shared)

  PHP_ADD_MAKEFILE_FRAGMENT
fi
