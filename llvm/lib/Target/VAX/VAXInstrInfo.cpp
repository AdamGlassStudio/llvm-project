//===-- VAXInstrInfo.cpp - VAX Instruction Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAX.h"
#include "VAXInstrInfo.h"
#include "VAXSubtarget.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "VAXGenInstrInfo.inc"

VAXInstrInfo::VAXInstrInfo(const VAXSubtarget &STI)
    : VAXGenInstrInfo(STI, RI, VAX::ADJCALLSTACKDOWN, VAX::ADJCALLSTACKUP), RI() {}

void VAXInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI,
                                const DebugLoc &DL, Register DstReg,
                                Register SrcReg, bool KillSrc,
                                bool RenamableDst, bool RenamableSrc) const {
  // All GPR registers are i32; MOVL covers all GPR→GPR copies.
  BuildMI(MBB, MI, DL, get(VAX::MOVL_rr), DstReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}
