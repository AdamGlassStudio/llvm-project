//===-- VAXFrameLowering.h - Define Frame Lowering for VAX ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_VAXFRAMELOWERING_H
#define LLVM_LIB_TARGET_VAX_VAXFRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {
class VAXSubtarget;

class VAXFrameLowering : public TargetFrameLowering {
public:
  explicit VAXFrameLowering()
      : TargetFrameLowering(TargetFrameLowering::StackGrowsDown,
                            Align(4), 0) {}

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  bool hasFP(const MachineFunction &MF) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_VAX_VAXFRAMELOWERING_H
