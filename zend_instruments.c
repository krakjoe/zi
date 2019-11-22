/*
  +----------------------------------------------------------------------+
  | instruments                                                          |
  +----------------------------------------------------------------------+
  | Copyright (c) The PHP Group                                          |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: krakjoe                                                      |
  +----------------------------------------------------------------------+
 */

#ifndef ZEND_INSTRUMENTS
# define ZEND_INSTRUMENTS

#define ZEND_INSTRUMENTS_EXTNAME   "Zend Instruments"
#define ZEND_INSTRUMENTS_VERSION   "0.0.1-dev"
#define ZEND_INSTRUMENTS_AUTHOR    "krakjoe"
#define ZEND_INSTRUMENTS_URL       "https://github.com/krakjoe/instruments"
#define ZEND_INSTRUMENTS_COPYRIGHT "Copyright (c) The PHP Group"

#if defined(__GNUC__) && __GNUC__ >= 4
# define ZEND_INSTRUMENTS_EXTENSION_API __attribute__ ((visibility("default")))
#else
# define ZEND_INSTRUMENTS_EXTENSION_API
#endif

#include "zend_instruments.h"

#define ZEND_INSTRUMENTS_START            (0)
#define ZEND_INSTRUMENTS_EXIT             (1)

#define ZEND_INSTRUMENTS_VM_START         ZEND_VM_LAST_OPCODE + 1
#define ZEND_INSTRUMENTS_VM_EXIT          ZEND_VM_LAST_OPCODE + 2

typedef struct _zend_instruments_relay_t {
    zend_uchar opcode;
    const void *handler;
} zend_instruments_relay_t;

#define ZEND_INSTRUMENTS_RELAYS_LOAD(frame)             (frame)->func->op_array.reserved[zend_instruments_resource]
#define ZEND_INSTRUMENTS_RELAYS_STORE(function, relays) (function)->reserved[zend_instruments_resource] = (relays)

static int                     zend_instruments_resource = -1;
static zend_bool               zend_instruments_started = 0;

static int  zend_instruments_startup(zend_extension*);
static void zend_instruments_shutdown(zend_extension *);
static void zend_instruments_activate(void);
static void zend_instruments_setup(zend_op_array*);

static int zend_instruments_start(zend_execute_data *execute_data);
static int zend_instruments_exit(zend_execute_data *execute_data);

ZEND_INSTRUMENTS_EXTENSION_API zend_extension_version_info extension_version_info = {
    ZEND_EXTENSION_API_NO,
    ZEND_EXTENSION_BUILD_ID
};

ZEND_INSTRUMENTS_EXTENSION_API zend_extension zend_extension_entry = {
    ZEND_INSTRUMENTS_EXTNAME,
    ZEND_INSTRUMENTS_VERSION,
    ZEND_INSTRUMENTS_AUTHOR,
    ZEND_INSTRUMENTS_URL,
    ZEND_INSTRUMENTS_COPYRIGHT,
    zend_instruments_startup,
    zend_instruments_shutdown,
    zend_instruments_activate,
    NULL,
    NULL,
    zend_instruments_setup,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    STANDARD_ZEND_EXTENSION_PROPERTIES
};

static int zend_instruments_startup(zend_extension *ze) {
    if (zend_instruments_started) {
        return SUCCESS;
    }

    zend_instruments_started  = 1;
    zend_instruments_resource = zend_get_resource_handle(ze);

    zend_set_user_opcode_handler(ZEND_INSTRUMENTS_VM_START, zend_instruments_start);
    zend_set_user_opcode_handler(ZEND_INSTRUMENTS_VM_EXIT,  zend_instruments_exit);

    ze->handle = 0;

    return SUCCESS;
}

static void zend_instruments_shutdown(zend_extension *ze) {
    if (!zend_instruments_started) {
        return;
    }

    zend_instruments_started = 0;
}

static void zend_instruments_activate(void) {
#if defined(ZTS) && defined(COMPILE_DL_INSTRUMENTS)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif

    if (!zend_instruments_started) {
        return;
    }

    if (INI_INT("opcache.optimization_level")) {
        zend_string *optimizer = zend_string_init(
	        ZEND_STRL("opcache.optimization_level"), 1);
        zend_long level = INI_INT("opcache.optimization_level");
        zend_string *value;

        /* disable pass 1 (pre-evaluate constant function calls) */
        level &= ~(1<<0);

        /* disable pass 4 (optimize function calls) */
        level &= ~(1<<3);

        value = zend_strpprintf(0, "0x%08X", (unsigned int) level);

        zend_alter_ini_entry(optimizer, value,
	        ZEND_INI_SYSTEM, ZEND_INI_STAGE_ACTIVATE);

        zend_string_release(optimizer);
        zend_string_release(value);
    }
}

static int zend_instruments_start(zend_execute_data *execute_data) {
    zend_instruments_relay_t *relays = ZEND_INSTRUMENTS_RELAYS_LOAD(execute_data);
    uint32_t offset = EX(opline) - EX(func)->op_array.opcodes;

    php_printf("started %s %s @ %p#%d\n",
        ZSTR_VAL(EX(func)->common.function_name),
        zend_get_opcode_name(relays[offset].opcode),
        EX(opline),
        EX(opline) - EX(func)->op_array.opcodes);

    return ZEND_USER_OPCODE_DISPATCH_TO | relays[offset].opcode;
}

static int zend_instruments_exit(zend_execute_data *execute_data) {
    zend_instruments_relay_t *relays = ZEND_INSTRUMENTS_RELAYS_LOAD(execute_data);
    uint32_t offset = EX(opline) - EX(func)->op_array.opcodes;

    php_printf("exit %s %s @ %p#%d\n",
        ZSTR_VAL(EX(func)->common.function_name),
        zend_get_opcode_name(relays[offset].opcode),
        EX(opline),
        EX(opline) - EX(func)->op_array.opcodes);

    return ZEND_USER_OPCODE_DISPATCH_TO | relays[offset].opcode;
}

static void zend_instruments_setup(zend_op_array *ops) {
    zend_cfg cfg;
	zend_basic_block *block;
    zend_arena *arena;
    zend_op *opline, *end, *limit = ops->opcodes + ops->last;
    zend_instruments_relay_t *relays;
	int i = 0;

    if (!ops->function_name) {
        return;
    }

    relays =
        (zend_instruments_relay_t*) // leaks, needs to be mapped
            calloc(ops->last, sizeof(zend_instruments_relay_t));

    if (!relays) {
        return;
    }

    ZEND_INSTRUMENTS_RELAYS_STORE(ops, relays);

    arena =
        zend_arena_create(1024 * 1024);

    memset(&cfg, 0, sizeof(cfg));

    zend_build_cfg(
        &arena, ops,
        (ops->fn_flags & ZEND_ACC_DONE_PASS_TWO) ?
            ZEND_RT_CONSTANTS : 0,
        &cfg);

	for (block = cfg.blocks, i = 0; i < cfg.blocks_count; i++, block++) {
		opline = ops->opcodes + block->start;
		end = opline + block->len;

		if (!(block->flags & ZEND_BB_REACHABLE)) {
			continue;
		}

        if ((block->flags & ZEND_BB_START)) {
            while (opline->opcode == ZEND_RECV ||
                   opline->opcode == ZEND_RECV_INIT) {
                opline++;
            }
            end--;

            relays[opline - ops->opcodes].opcode = opline->opcode;
            relays[opline - ops->opcodes].handler = opline->handler;
            opline->opcode = ZEND_INSTRUMENTS_VM_START;

            relays[end - ops->opcodes].opcode = end->opcode;
            relays[end - ops->opcodes].handler = end->handler;
            end->opcode = ZEND_INSTRUMENTS_VM_EXIT;
            continue;
        }

        if ((block->flags & ZEND_BB_EXIT)) {
            if (opline->opcode == ZEND_INSTRUMENTS_VM_EXIT) {
                continue;
            }

            while (opline->opcode != ZEND_RETURN &&
                   opline->opcode != ZEND_THROW) {
                opline++;
            }

            relays[opline - ops->opcodes].opcode = opline->opcode;
            relays[opline - ops->opcodes].handler = opline->handler;
            opline->opcode = ZEND_INSTRUMENTS_VM_EXIT;
            continue;
        }
    }

    zend_arena_destroy(arena);
}

#if defined(ZTS) && defined(COMPILE_DL_INSTRUMENTS)
    ZEND_TSRMLS_CACHE_DEFINE();
#endif

#endif /* ZEND_INSTRUMENTS */
