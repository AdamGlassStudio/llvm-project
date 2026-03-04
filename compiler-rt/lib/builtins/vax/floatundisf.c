//===-- vax/floatundisf.c - Implement __floatundisf for VAX ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// VAX-specific implementation of __floatundisf (unsigned i64 → F_float).
//
//===----------------------------------------------------------------------===//

#include "../int_lib.h"

// Implemented in floatundidf.c.
COMPILER_RT_ABI double __floatundidf(du_int a);

COMPILER_RT_ABI float __floatundisf(du_int a) {
  return (float)__floatundidf(a);
}
