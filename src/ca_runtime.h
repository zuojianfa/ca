/**
 * Copyright (c) 2023 Rusheng Xia <xrsh_2004@163.com>
 * CA Programming Language and CA Compiler are licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef __ca_runtime_h__
#define __ca_runtime_h__

#include "ca_types.h"

//#define TEST_RUNTIME // for test runtime functions in when print

#ifdef __cplusplus
BEGIN_EXTERN_C
#endif

#ifdef TEST_RUNTIME
int rt_add(int a, int b);
int rt_sub(int a, int b);
#endif

#ifdef __cplusplus
END_EXTERN_C
#endif
#endif

