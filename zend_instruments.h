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

#ifndef ZEND_INSTRUMENTS_H
# define ZEND_INSTRUMENTS_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "zend.h"
#include "zend_arena.h"
#include "zend_API.h"
#include "zend_cfg.h"
#include "zend_extensions.h"
#include "zend_ini.h"
#include "zend_vm.h"

# if defined(ZTS) && defined(COMPILE_DL_INSTRUMENTS)
ZEND_TSRMLS_CACHE_EXTERN()
# endif

#endif	/* ZEND_INSTRUMENTS_H */
