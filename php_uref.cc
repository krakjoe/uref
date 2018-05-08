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

#include <php.h>
#include <ext/standard/info.h>
#include <ext/spl/spl_exceptions.h>
#include <zend_exceptions.h>
#include <php_uref.h>

#include <unistd.h>
#include <stdlib.h>
#include <ucontext.h>
#include <sys/mman.h>

#include <iostream>
#include <string>

#include <llvm-c/Target.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/Triple.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCDisassembler/MCDisassembler.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/MC/MCInst.h>

#if defined(__APPLE__)
#if 	defined(__LP64__)
#		define UREF_IP_REF(mc) (mc).ss.rip
		typedef uint64_t uref_register_t;
#	else
#		define UREF_IP_REG(mc) (mc).ss.eip
		typedef unsigned int uref_register_t;
#	endif
#else
#	ifndef __x86_64__
#		define UREF_IP_REG(mc) (mc).gregs[REG_EIP]
#	else
#		define UREF_IP_REG(mc) (mc).gregs[REG_RIP]
#	endif
	typedef greg_t uref_register_t;
#endif

typedef struct _php_uref_t {
	zval referent;
	zend_object std;
} php_uref_t;

#define php_uref_from(o) ((php_uref_t*) (((char*) o) - XtOffsetOf(php_uref_t, std)))
#define php_uref_fetch(z) php_uref_from(Z_OBJ_P(z))

typedef enum _php_uref_state_t {
	UREF_RUNTIME,
	UREF_SHUTDOWN
} php_uref_state_t;

ZEND_BEGIN_MODULE_GLOBALS(uref)
	HashTable refs;
	php_uref_state_t state;
	zend_ulong object;
	uint8_t  trap;
	struct {
		struct {
			struct sigaction segv;
			struct sigaction trap;
		} uref;
		struct {
			struct sigaction segv;
			struct sigaction trap;
		} backup;
	} action;

	llvm::MCContext *CTX;
	llvm::MCDisassembler *Disassembler;

ZEND_END_MODULE_GLOBALS(uref)

ZEND_DECLARE_MODULE_GLOBALS(uref)

#ifdef ZTS
#	define UG(v) TSRMG(uref_globals_id, zend_uref_globals *, v)
#else
#	define UG(v) uref_globals.v
#endif

zend_class_entry *php_uref_ce;
zend_object_handlers php_uref_handlers;

static zend_always_inline int php_uref_add(zend_ulong idx, zval *ze);
static zend_always_inline void php_uref_update(zend_ulong idx);

static size_t php_uref_pagesize;

static zend_always_inline void *php_uref_pageof(void *addr) {
	return (void *) ((size_t) addr & ~(php_uref_pagesize - 1));
}

static zend_always_inline size_t php_uref_boundaryof(void *addr, size_t size) {
	return (size_t) php_uref_pageof((void *) ((size_t) addr + size - 1)) - (size_t) php_uref_pageof(addr) + php_uref_pagesize;
}

static zend_always_inline zend_ulong php_uref_bucketof(zend_object *object) {
	return reinterpret_cast<zend_ulong>(&(EG(objects_store).object_buckets[object->handle]));
}

uint64_t php_uref_lengthof(uint64_t address) {
	llvm::MCInst inst;
	uint64_t     size;

	llvm::raw_os_ostream os1(std::cerr);
	llvm::raw_os_ostream os2(std::cerr);

	/* silence streams, todo: something better */
	std::cerr.setstate(std::ios_base::badbit);

	switch(UG(Disassembler)->getInstruction(inst, size, 
		llvm::ArrayRef<uint8_t>((uint8_t*) address, 12),
		address, 
		os1, os2)) {
		case llvm::MCDisassembler::Success:
			return size;

		default: {/* nothing */}
	}

	return 0;
}

static zend_always_inline void php_uref_protect() {
	zend_objects_store *store = &EG(objects_store);
	
	mprotect(php_uref_pageof(store->object_buckets), 
		 php_uref_boundaryof(store->object_buckets, store->size * sizeof(zend_object*)), 
		 PROT_READ);
}

static zend_always_inline void php_uref_unprotect() {
	zend_objects_store *store = &EG(objects_store);

	mprotect(php_uref_pageof(store->object_buckets), 
		 php_uref_boundaryof(store->object_buckets, store->size * sizeof(zend_object*)), 
		 PROT_READ|PROT_WRITE);
}

static void php_uref_trap(int sig, siginfo_t *info, ucontext_t *context) {
	uint8_t *rip = reinterpret_cast<uint8_t*>(UREF_IP_REG(context->uc_mcontext)-1);

	mprotect(php_uref_pageof(rip), php_uref_pagesize, PROT_WRITE);
	
	rip[0] = UG(trap);

	UREF_IP_REG(context->uc_mcontext) = reinterpret_cast<uref_register_t>(rip);

	mprotect(php_uref_pageof(rip), php_uref_pagesize, PROT_EXEC);

	if (UG(state) == UREF_SHUTDOWN) {
		return;
	}

	php_uref_protect();
}

static void php_uref_segv(int sig, siginfo_t *info, ucontext_t *context) {
	uint8_t *rip = 
		reinterpret_cast<uint8_t*>(UREF_IP_REG(context->uc_mcontext));
	uint8_t *trap = rip + php_uref_lengthof(UREF_IP_REG(context->uc_mcontext));

	if (info->si_code != SEGV_ACCERR) {
		/* segfault for some other reason, breaks everything */
		sigaction(SIGSEGV, &UG(action).backup.segv, NULL);
		return;
	}

	if (UG(state) == UREF_SHUTDOWN) {
		php_uref_unprotect();
		return;
	}

	if (zend_hash_num_elements(&UG(refs)) == 0) {
		php_uref_unprotect();
		return;
	}

	mprotect(php_uref_pageof(rip), php_uref_pagesize, PROT_READ|PROT_WRITE);

	UG(trap) = trap[0];

	trap[0] = 0xCC;

	mprotect(php_uref_pageof(rip), php_uref_pagesize, PROT_READ|PROT_EXEC);

	php_uref_unprotect();

	php_uref_update(reinterpret_cast<zend_ulong>(info->si_addr));
}

static zend_object* php_uref_create(zend_class_entry *ce) {
	php_uref_t *u = 
		(php_uref_t*) 
			ecalloc(1, sizeof(php_uref_t));

	zend_object_std_init(&u->std, ce);

	u->std.handlers = &php_uref_handlers;

	return &u->std;
}

static zend_object* php_uref_clone(zval *zv) {
	php_uref_t *u = php_uref_fetch(zv);
	php_uref_t *c;	
	zend_object *co = php_uref_create(Z_OBJCE_P(zv));
	zval rv;

	ZVAL_OBJ(&rv, co);

	c = php_uref_from(co);

	ZVAL_COPY_VALUE(&c->referent, &u->referent);

	if (php_uref_add(
		php_uref_bucketof(Z_OBJ(c->referent)), &rv) != SUCCESS) {
		/* throw ? */
	}

	php_uref_protect();

	return co;
}

#define php_uref_unsupported(thing) \
	zend_throw_exception_ex( \
		spl_ce_RuntimeException, 0, "uref objects do not support " thing);

static void php_uref_write(zval *object, zval *member, zval *value, void **rtc) {
	php_uref_unsupported("properties");
}

static zval* php_uref_read(zval *object, zval *member, int type, void **rtc, zval *rv) {
	if (!EG(exception)) {
		php_uref_unsupported("properties");
	}

	return &EG(uninitialized_zval);
}

static zval *php_uref_read_ptr(zval *object, zval *member, int type, void **rtc) {
	php_uref_unsupported("references");
	return NULL;
}

static int php_uref_isset(zval *object, zval *member, int hse, void **rtc) {
	if (hse != 2) {
		php_uref_unsupported("properties");
	}
	return 0;
}

static void php_uref_unset(zval *object, zval *member, void **rtc) {
	php_uref_unsupported("properties");
}

static zend_always_inline int php_uref_add(zend_ulong idx, zval *ze) {
	HashTable *refs = static_cast<HashTable*>(zend_hash_index_find_ptr(&UG(refs), idx));

	if (!refs) {
		refs = static_cast<HashTable*>(emalloc(sizeof(HashTable)));

		zend_hash_init(refs, 4, NULL, ZVAL_PTR_DTOR, 0);

		zend_hash_index_update_ptr(&UG(refs), idx, refs);
	}

	if (zend_hash_index_update(refs, Z_OBJ_P(ze)->handle, ze)) {
		Z_ADDREF_P(ze);

		return SUCCESS;
	}

	return FAILURE;
}

static zend_always_inline void php_uref_update(zend_ulong idx) {
	HashTable *refs = static_cast<HashTable*>(zend_hash_index_find_ptr(&UG(refs), idx));
	zval *ze;
	bool deleting = false;

	if (!refs) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(refs, ze) {
		php_uref_t *u = php_uref_fetch(ze);

		if (deleting ||
		    (Z_TYPE(u->referent) != IS_UNDEF && Z_REFCOUNT(u->referent) == 0)) {
			ZVAL_UNDEF(&u->referent);
			deleting = true;
		} else break;
	} ZEND_HASH_FOREACH_END();

	if (deleting) {
		zend_hash_index_del(&UG(refs), idx);
	}
}


ZEND_BEGIN_ARG_INFO_EX(php_uref_construct_arginfo, 0, 0, 1)
	ZEND_ARG_INFO(0, object)
ZEND_END_ARG_INFO()

PHP_METHOD(uref, __construct) 
{
	php_uref_t *u = php_uref_fetch(getThis());
	zval *object = NULL;

	if (zend_parse_parameters_throw(ZEND_NUM_ARGS(), "o", &object) != SUCCESS) {
		return;
	}

	ZVAL_COPY_VALUE(&u->referent, object);

	if (php_uref_add(
		php_uref_bucketof(Z_OBJ_P(object)),
		getThis()) != SUCCESS) {
		/* throw ? */
	}

	php_uref_protect();
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

	LLVMInitializeNativeTarget();
	LLVMInitializeNativeDisassembler();

	ZEND_INIT_MODULE_GLOBALS(uref, php_uref_init_globals, NULL);

	INIT_CLASS_ENTRY(ce, "uref", php_uref_methods);

	php_uref_ce = zend_register_internal_class(&ce);
	php_uref_ce->create_object = php_uref_create;
	php_uref_ce->ce_flags |= ZEND_ACC_FINAL;

	memcpy(&php_uref_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	php_uref_handlers.clone_obj = php_uref_clone;
	php_uref_handlers.write_property = php_uref_write;
	php_uref_handlers.read_property = php_uref_read;
	php_uref_handlers.has_property = php_uref_isset;
	php_uref_handlers.unset_property = php_uref_unset;
	php_uref_handlers.get_property_ptr_ptr = php_uref_read_ptr;

	php_uref_handlers.offset = XtOffsetOf(php_uref_t, std);

	UG(action).uref.segv.sa_sigaction = (void (*)(int, siginfo_t*, void*)) php_uref_segv;
	UG(action).uref.segv.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigemptyset(&UG(action).uref.segv.sa_mask);

	sigaction(SIGSEGV, &UG(action).uref.segv, &UG(action).backup.segv);

	UG(action).uref.trap.sa_sigaction = (void (*)(int, siginfo_t*, void*)) php_uref_trap;
	UG(action).uref.trap.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigemptyset(&UG(action).uref.trap.sa_mask);

	sigaction(SIGTRAP, &UG(action).uref.trap, &UG(action).backup.trap);

#if _SC_PAGE_SIZE
	php_uref_pagesize = sysconf(_SC_PAGE_SIZE);
#elif _SC_PAGESIZE
	php_uref_pagesize = sysconf(_SC_PAGESIZE);
#elif _SC_NUTC_OS_PAGESIZE
	php_uref_pagesize = sysconf(_SC_NUTC_OS_PAGESIZE);
#else
	php_uref_pagesize = 4096; /* common pagesize */
#endif

	return SUCCESS;	
}

PHP_MSHUTDOWN_FUNCTION(uref)
{
	sigaction(SIGSEGV, &UG(action).backup.segv, NULL);
	sigaction(SIGTRAP, &UG(action).backup.trap, NULL);

	return SUCCESS;
}

static inline void php_uref_dtor(zval *zv) {
	zend_hash_destroy(static_cast<HashTable*>(Z_PTR_P(zv)));
	efree(Z_PTR_P(zv));
}

/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(uref)
{
#if defined(ZTS) && defined(COMPILE_DL_UREF)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif

	UG(state) = UREF_RUNTIME;

	zend_hash_init(&UG(refs), 32, NULL, php_uref_dtor, 0);
	{
		std::string Error;
		std::string Triple = llvm::sys::getProcessTriple();
		std::string CPU    = llvm::sys::getHostCPUName();
		
		const llvm::Target* target = llvm::TargetRegistry::lookupTarget(Triple, Error);
		if (!target) {
			return FAILURE;
		}

		const llvm::MCRegisterInfo *MRI = target->createMCRegInfo(Triple);
		if (!MRI) {
			return FAILURE;
		}

		const llvm::MCAsmInfo *MAI = target->createMCAsmInfo(*MRI, Triple);
		if (!MAI) {
			return FAILURE;
		}

		const llvm::MCSubtargetInfo *STI = target->createMCSubtargetInfo(Triple, CPU, "");
		if (!STI) {
			return FAILURE;
		}

	 	UG(CTX) = new llvm::MCContext(MAI, MRI, nullptr);
		if (!UG(CTX)) {
			return FAILURE;
		}

	 	UG(Disassembler) = target->createMCDisassembler(*STI, *UG(CTX));
		if (!UG(Disassembler)) {
			delete UG(CTX);

			return FAILURE;
		}
	}

	return SUCCESS;
}
/* }}} */

PHP_RSHUTDOWN_FUNCTION(uref)
{
	UG(state) = UREF_SHUTDOWN;

	zend_hash_destroy(&UG(refs));

	if (UG(Disassembler)) {
		delete UG(CTX);
	}

	php_uref_unprotect();

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
