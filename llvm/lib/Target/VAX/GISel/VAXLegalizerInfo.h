//===-- VAXLegalizerInfo.h - VAX Legalizer Info ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// VAX is unusual among LLVM targets: it has native i8, i16, i32 operations
// (ADDB, ADDW, ADDL etc.) so sub-32-bit types can be legal rather than
// requiring widening. This is one area where GISel might shine — SelectionDAG
// forces type legalization to widen everything to i32.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_VAXLEGALIZERINFO_H
#define LLVM_LIB_TARGET_VAX_VAXLEGALIZERINFO_H

#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"

namespace llvm {

class VAXSubtarget;

class VAXLegalizerInfo : public LegalizerInfo {
public:
  VAXLegalizerInfo(const VAXSubtarget &ST);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_VAX_VAXLEGALIZERINFO_H
