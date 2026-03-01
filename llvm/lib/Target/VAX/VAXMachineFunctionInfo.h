//===-- VAXMachineFunctionInfo.h - VAX Machine Function Info ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_VAXMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_VAX_VAXMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class VAXMachineFunctionInfo : public MachineFunctionInfo {
  // Offset from AP to the first variadic argument (AP + VarArgsOffset).
  unsigned VarArgsOffset = 0;

public:
  VAXMachineFunctionInfo() = default;
  explicit VAXMachineFunctionInfo(const Function &F,
                                   const TargetSubtargetInfo *STI) {}

  unsigned getVarArgsOffset() const { return VarArgsOffset; }
  void setVarArgsOffset(unsigned Offset) { VarArgsOffset = Offset; }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_VAX_VAXMACHINEFUNCTIONINFO_H
