/*
  +----------------------------------------------------------------------+
  | uref                                                                 |
  +----------------------------------------------------------------------+
  | Copyright (c) Joe Watkins 2018                                       |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: krakjoe <krakjoe@php.net>                                    |
  +----------------------------------------------------------------------+
 */

#ifndef PHP_UREF_H
# define PHP_UREF_H

extern zend_module_entry uref_module_entry;
# define phpext_uref_ptr &uref_module_entry

# define PHP_UREF_VERSION "0.0.1"

# if defined(ZTS) && defined(COMPILE_DL_UREF)
ZEND_TSRMLS_CACHE_EXTERN()
# endif

#endif	/* PHP_UREF_H */
