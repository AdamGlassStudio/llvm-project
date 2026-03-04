//===-- vax/floatdidf.c - Implement __floatdidf for VAX -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// VAX-specific implementation of __floatdidf (signed i64 → D_float double).
//
// The generic implementation uses IEEE 754 union-punning which produces
// incorrect results on VAX D_float. This version uses pure arithmetic:
// split into high/low 32-bit halves, convert each with hardware CVTLD,
// and combine with FP multiply/add.
//
//===----------------------------------------------------------------------===//

#include "../int_lib.h"

COMPILER_RT_ABI double __floatdidf(di_int a) {
  if (a == 0)
    return 0.0;

  int negative = 0;
  du_int ua;
  if (a < 0) {
    negative = 1;
    ua = (du_int)(-(a + 1)) + 1u;
  } else {
    ua = (du_int)a;
  }

  su_int high = (su_int)(ua >> 32);
  su_int low = (su_int)ua;

  // VAX CVTLD handles int32→D_float natively.
  static const double twop32 = 4294967296.0;
  double result = (double)high * twop32 + (double)low;

  return negative ? -result : result;
}
