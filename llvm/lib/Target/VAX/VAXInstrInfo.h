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

class VAXSubtarget;

class VAXInstrInfo : public VAXGenInstrInfo {
  const VAXRegisterInfo RI;

public:
  explicit VAXInstrInfo(const VAXSubtarget &STI);

  const VAXRegisterInfo &getRegisterInfo() const { return RI; }

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, Register DstReg, Register SrcReg,
                   bool KillSrc, bool RenamableDst = false,
                   bool RenamableSrc = false) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_VAX_VAXINSTRINFO_H
