//===-- vax/floatundidf.c - Implement __floatundidf for VAX ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// VAX-specific implementation of __floatundidf (unsigned i64 → D_float double).
//
//===----------------------------------------------------------------------===//

#include "../int_lib.h"

COMPILER_RT_ABI double __floatundidf(du_int a) {
  if (a == 0)
    return 0.0;

  su_int high = (su_int)(a >> 32);
  su_int low = (su_int)a;

  static const double twop32 = 4294967296.0;
  return (double)high * twop32 + (double)low;
}
