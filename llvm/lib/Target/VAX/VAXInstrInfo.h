//===-- VAXInstrInfo.h - VAX Instruction Information ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_VAXINSTRINFO_H
#define LLVM_LIB_TARGET_VAX_VAXINSTRINFO_H

#include "VAXRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "VAXGenInstrInfo.inc"

namespace llvm {

class VAXInstrInfo : public VAXGenInstrInfo {
  const VAXRegisterInfo RI;

public:
  VAXInstrInfo();

  const VAXRegisterInfo &getRegisterInfo() const { return RI; }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_VAX_VAXINSTRINFO_H
