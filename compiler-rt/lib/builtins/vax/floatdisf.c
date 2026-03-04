//===-- vax/floatdisf.c - Implement __floatdisf for VAX -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// VAX-specific implementation of __floatdisf (signed i64 → F_float).
//
// The generic implementation uses int_to_fp_impl.inc which constructs
// IEEE 754 bit patterns directly. This version uses pure arithmetic
// that works with VAX F_float.
//
//===----------------------------------------------------------------------===//

#include "../int_lib.h"

// Implemented in floatdidf.c.
COMPILER_RT_ABI double __floatdidf(di_int a);

COMPILER_RT_ABI float __floatdisf(di_int a) {
  // Use the double conversion and let hardware CVTDF truncate.
  // D_float (56-bit mantissa) has enough precision for both the i64
  // value and the F_float (24-bit) result — no double rounding issue.
  return (float)__floatdidf(a);
}
