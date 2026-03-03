//===--- VAX.cpp - Implement VAX target feature support -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements VAX TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "VAX.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

/// Redefine a preprocessor macro (undef + define).
static void RedefineMacro(MacroBuilder &Builder, const Twine &Name,
                          const Twine &Value) {
  Builder.undefineMacro(Name);
  Builder.defineMacro(Name, Value);
}

void VAXTargetInfo::getTargetDefines(const LangOptions &Opts,
                                     MacroBuilder &Builder) const {
  Builder.defineMacro("__vax__");
  Builder.defineMacro("__vax");
  Builder.defineMacro("vax");
  // GCC compatibility.
  Builder.defineMacro("__VAX__");

  // VAX uses non-IEEE floating point (F_float/D_float).  The default
  // macros are computed from IEEEsingle/IEEEdouble semantics and have
  // wrong ranges.  Override with correct VAX values matching GCC.

  // F_float (32-bit): 1 sign, 8 exp (excess-128), 23+1 mantissa bits.
  // Range: ~2.94e-39 to ~1.70e+38.  No infinity, NaN, or denormals.
  RedefineMacro(Builder, "__FLT_MAX__", "1.70141173319264430e+38F");
  RedefineMacro(Builder, "__FLT_MIN__", "2.93873587705571877e-39F");
  RedefineMacro(Builder, "__FLT_EPSILON__", "1.19209289550781250e-7F");
  RedefineMacro(Builder, "__FLT_DENORM_MIN__", "2.93873587705571877e-39F");
  RedefineMacro(Builder, "__FLT_NORM_MAX__", "1.70141173319264430e+38F");
  RedefineMacro(Builder, "__FLT_HAS_DENORM__", "0");
  RedefineMacro(Builder, "__FLT_HAS_INFINITY__", "0");
  RedefineMacro(Builder, "__FLT_HAS_QUIET_NAN__", "0");
  RedefineMacro(Builder, "__FLT_MAX_EXP__", "127");
  RedefineMacro(Builder, "__FLT_MIN_EXP__", "(-127)");
  RedefineMacro(Builder, "__FLT_MAX_10_EXP__", "38");
  RedefineMacro(Builder, "__FLT_MIN_10_EXP__", "(-38)");

  // D_float (64-bit): 1 sign, 8 exp (excess-128), 55+1 mantissa bits.
  // Same exponent range as F_float, more precision.
  RedefineMacro(Builder, "__DBL_MANT_DIG__", "56");
  RedefineMacro(Builder, "__DBL_DIG__", "16");
  RedefineMacro(Builder, "__DBL_DECIMAL_DIG__", "18");
  RedefineMacro(Builder, "__DBL_MAX__", "1.70141183460469229e+38");
  RedefineMacro(Builder, "__DBL_MIN__", "2.93873587705571877e-39");
  RedefineMacro(Builder, "__DBL_EPSILON__", "2.77555756156289135e-17");
  RedefineMacro(Builder, "__DBL_DENORM_MIN__", "2.93873587705571877e-39");
  RedefineMacro(Builder, "__DBL_NORM_MAX__", "1.70141183460469229e+38");
  RedefineMacro(Builder, "__DBL_HAS_DENORM__", "0");
  RedefineMacro(Builder, "__DBL_HAS_INFINITY__", "0");
  RedefineMacro(Builder, "__DBL_HAS_QUIET_NAN__", "0");
  RedefineMacro(Builder, "__DBL_MAX_EXP__", "127");
  RedefineMacro(Builder, "__DBL_MIN_EXP__", "(-127)");
  RedefineMacro(Builder, "__DBL_MAX_10_EXP__", "38");
  RedefineMacro(Builder, "__DBL_MIN_10_EXP__", "(-38)");

  // long double = D_float on VAX (same as double).
  RedefineMacro(Builder, "__LDBL_MANT_DIG__", "56");
  RedefineMacro(Builder, "__LDBL_DIG__", "16");
  RedefineMacro(Builder, "__LDBL_DECIMAL_DIG__", "18");
  RedefineMacro(Builder, "__LDBL_MAX__", "1.70141183460469229e+38L");
  RedefineMacro(Builder, "__LDBL_MIN__", "2.93873587705571877e-39L");
  RedefineMacro(Builder, "__LDBL_EPSILON__", "2.77555756156289135e-17L");
  RedefineMacro(Builder, "__LDBL_DENORM_MIN__", "2.93873587705571877e-39L");
  RedefineMacro(Builder, "__LDBL_NORM_MAX__", "1.70141183460469229e+38L");
  RedefineMacro(Builder, "__LDBL_HAS_DENORM__", "0");
  RedefineMacro(Builder, "__LDBL_HAS_INFINITY__", "0");
  RedefineMacro(Builder, "__LDBL_HAS_QUIET_NAN__", "0");
  RedefineMacro(Builder, "__LDBL_MAX_EXP__", "127");
  RedefineMacro(Builder, "__LDBL_MIN_EXP__", "(-127)");
  RedefineMacro(Builder, "__LDBL_MAX_10_EXP__", "38");
  RedefineMacro(Builder, "__LDBL_MIN_10_EXP__", "(-38)");
}

const char *const VAXTargetInfo::GCCRegNames[] = {
    "r0",  "r1",  "r2",  "r3", "r4",  "r5",  "r6",  "r7",
    "r8",  "r9",  "r10", "r11", "ap",  "fp",  "sp",  "pc"};

ArrayRef<const char *> VAXTargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

ArrayRef<TargetInfo::GCCRegAlias> VAXTargetInfo::getGCCRegAliases() const {
  // No aliases — register names match GCC directly.
  return {};
}
