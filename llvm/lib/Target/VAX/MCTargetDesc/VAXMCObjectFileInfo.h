//===-- VAXMCObjectFileInfo.h - VAX object file info ----------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_MCTARGETDESC_VAXMCOBJECTFILEINFO_H
#define LLVM_LIB_TARGET_VAX_MCTARGETDESC_VAXMCOBJECTFILEINFO_H

#include "llvm/MC/MCObjectFileInfo.h"

namespace llvm {

class VAXMCObjectFileInfo : public MCObjectFileInfo {
public:
  // VAX instructions are byte-aligned (variable-length, no alignment
  // constraints). GAS uses alignment 1 for .text; match that to avoid
  // unnecessary NOP padding between object files at link time.
  unsigned getTextSectionAlignment() const override { return 1; }
};

} // namespace llvm

#endif
