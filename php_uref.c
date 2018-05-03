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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "php.h"
#include "ext/standard/info.h"
#include "php_uref.h"

#include <unistd.h>
#include <ucontext.h>
#include <sys/mman.h>

ZEND_BEGIN_MODULE_GLOBALS(uref)
	HashTable refs;
	long long int trap;
	struct {
		struct sigaction segv;
		struct sigaction trap;
	} action;
ZEND_END_MODULE_GLOBALS(uref)

ZEND_DECLARE_MODULE_GLOBALS(uref);

#ifdef ZTS
#	define UG(v) TSRMG(uref_globals_id, zend_uref_globals *, v)
#else
#	define UG(v) uref_globals.v
#endif

zend_class_entry *php_uref_ce;
zend_object_handlers php_uref_handlers;

typedef struct _php_uref_t {
	zval referent;
	zend_object std;
} php_uref_t;

#define php_uref_from(o) ((php_uref_t*) (((char*) o) - XtOffsetOf(php_uref_t, std)))
#define php_uref_fetch(z) php_uref_from(Z_OBJ_P(z))

static size_t php_uref_pagesize;

static zend_always_inline void *php_uref_pageof(void *addr) {
	return (void *) ((size_t) addr & ~(php_uref_pagesize - 1));
}

static inline void php_uref_trap(int sig, siginfo_t *info, ucontext_t *context) {
	unsigned char *rip = (unsigned char*) context->uc_mcontext.gregs[REG_RIP];

	mprotect(php_uref_pageof(rip), php_uref_pagesize, PROT_READ|PROT_WRITE);

	asm volatile ("decq (%0)"
		: /* nothing */
		: "r" (rip));

	asm volatile ("mov %0, (%1)"
		: /* nothing */
		: "r" (UG(trap)),
		  "r" (rip));

	mprotect(php_uref_pageof(rip), php_uref_pagesize, PROT_READ|PROT_EXEC);

	mprotect(php_uref_pageof(info->si_addr), php_uref_pagesize, PROT_READ);
}

static inline void php_uref_segv(int sig, siginfo_t *info, ucontext_t *context) {
	void *addr = info->si_addr;
	unsigned char *rip = (unsigned char*) context->uc_mcontext.gregs[REG_RIP];
	unsigned char *nip = rip + 1;

	zval *ze = 
		zend_hash_index_find(&UG(refs), (zend_long) addr);

	mprotect(
		php_uref_pageof(info->si_addr),
		php_uref_pagesize,
		PROT_READ|PROT_WRITE|PROT_EXEC);

	if (ze && Z_TYPE_P(ze) == IS_OBJECT) {
		php_uref_t *u = 
			php_uref_fetch(ze);

		ZVAL_UNDEF(&u->referent);
	}

	mprotect(php_uref_pageof(nip), php_uref_pagesize, PROT_READ|PROT_WRITE);

	asm volatile ("movq %0, %1"
		: /* nothing */
		: "r" (nip),
		  "r" (UG(trap)));

	asm volatile ("movw $0xCC, (%0)"
		: /* nothing */
		: "r" (nip));
	
	mprotect(php_uref_pageof(nip), php_uref_pagesize, PROT_READ|PROT_EXEC);
}

static inline void php_uref_init() {
	struct sigaction segv;
	struct sigaction trap;

	segv.sa_sigaction = php_uref_segv;
	segv.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigemptyset(&segv.sa_mask);

	sigaction(SIGSEGV, &segv, &UG(action).segv);

	trap.sa_sigaction = php_uref_trap;
	trap.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigemptyset(&trap.sa_mask);

	sigaction(SIGTRAP, &trap, &UG(action).trap);
}

static inline void php_uref_shutdown() {
	sigaction(SIGSEGV, &UG(action).segv, NULL);
	sigaction(SIGTRAP, &UG(action).trap, NULL);
}

static inline zend_object* php_uref_create(zend_class_entry *ce) {
	php_uref_t *u = 
		(php_uref_t*) 
			ecalloc(1, sizeof(php_uref_t));

	zend_object_std_init(&u->std, ce);

	u->std.handlers = &php_uref_handlers;

	return &u->std;
}

#define php_uref_bucket(o) EG(objects_store).object_buckets[(o)->handle]

static inline void php_uref_attach(zval *ze, zval *referent) {
	php_uref_t *u = php_uref_fetch(ze);

	if (mprotect(
		php_uref_pageof(
			&php_uref_bucket(Z_OBJ_P(referent))
		), php_uref_pagesize, PROT_READ) != SUCCESS) {
		return;
	}

	ZVAL_COPY_VALUE(&u->referent, referent);

	if (zend_hash_index_update(&UG(refs), (zend_long) &php_uref_bucket(Z_OBJ_P(referent)), ze)) {
		Z_ADDREF_P(ze);
	}
}
#undef php_uref_bucket

ZEND_BEGIN_ARG_INFO_EX(php_uref_construct_arginfo, 0, 0, 1)
	ZEND_ARG_INFO(0, object)
ZEND_END_ARG_INFO()

PHP_METHOD(uref, __construct) 
{
	zval *object = NULL;

	if (zend_parse_parameters_throw(ZEND_NUM_ARGS(), "o", &object) != SUCCESS) {
		return;
	}

	php_uref_attach(getThis(), object);
}

PHP_METHOD(uref, valid)
{
	php_uref_t *u =
		php_uref_fetch(getThis());

	RETURN_BOOL(Z_TYPE(u->referent) != IS_UNDEF);
}

PHP_METHOD(uref, get) 
{
	php_uref_t *u = 
		php_uref_fetch(getThis());

	if (!Z_ISUNDEF(u->referent)) {
		ZVAL_COPY(return_value, &u->referent);
	}
}

static zend_function_entry php_uref_methods[] = {
	PHP_ME(uref, __construct, php_uref_construct_arginfo, ZEND_ACC_PUBLIC)
	PHP_ME(uref, valid, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(uref, get, NULL, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

static inline void php_uref_init_globals(zend_uref_globals *ug) {
	memset(ug, 0, sizeof(zend_uref_globals));
}

PHP_MINIT_FUNCTION(uref)
{
	zend_class_entry ce;

	ZEND_INIT_MODULE_GLOBALS(uref, php_uref_init_globals, NULL);

#if _SC_PAGE_SIZE
	php_uref_pagesize = sysconf(_SC_PAGE_SIZE);
#elif _SC_PAGESIZE
	php_uref_pagesize = sysconf(_SC_PAGESIZE);
#elif _SC_NUTC_OS_PAGESIZE
	php_uref_pagesize = sysconf(_SC_NUTC_OS_PAGESIZE);
#else
	php_uref_pagesize = 4096; /* common pagesize */
#endif

	INIT_CLASS_ENTRY(ce, "uref", php_uref_methods);

	php_uref_ce = zend_register_internal_class(&ce);
	php_uref_ce->create_object = php_uref_create;

	memcpy(&php_uref_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));

	php_uref_handlers.offset = XtOffsetOf(php_uref_t, std);

	php_uref_init();

	return SUCCESS;	
}

PHP_MSHUTDOWN_FUNCTION(uref)
{
	php_uref_shutdown();

	return SUCCESS;
}

/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(uref)
{
#if defined(ZTS) && defined(COMPILE_DL_UREF)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif

	zend_hash_init(&UG(refs), 32, NULL, ZVAL_PTR_DTOR, 0);

	return SUCCESS;
}
/* }}} */

PHP_RSHUTDOWN_FUNCTION(uref)
{
	zend_hash_destroy(&UG(refs));

	return SUCCESS;
}

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(uref)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "uref support", "enabled");
	php_info_print_table_end();
}
/* }}} */

/* {{{ uref_module_entry
 */
zend_module_entry uref_module_entry = {
	STANDARD_MODULE_HEADER,
	"uref",
	NULL,
	PHP_MINIT(uref),
	PHP_MSHUTDOWN(uref),
	PHP_RINIT(uref),
	PHP_RSHUTDOWN(uref),
	PHP_MINFO(uref),
	PHP_UREF_VERSION,
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_UREF
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(uref)
#endif
