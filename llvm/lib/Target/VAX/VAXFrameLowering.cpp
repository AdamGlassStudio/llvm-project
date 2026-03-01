//===-- VAXFrameLowering.cpp - VAX Frame Lowering ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXFrameLowering.h"
#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "VAXInstrInfo.h"
#include "VAXSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

using namespace llvm;

bool VAXFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  // VAX CALLS always establishes a frame pointer.
  return true;
}

void VAXFrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t StackSize = MFI.getStackSize();
  if (StackSize == 0)
    return;

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // Allocate local frame: subl2 $stacksize, %sp
  BuildMI(MBB, MBBI, DL, TII.get(VAX::SUBL2_ri), VAX::SP)
      .addImm(StackSize)
      .addReg(VAX::SP);
}

void VAXFrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  // TODO: implement in Phase 3 — emit RET
}

bool VAXFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  // Always emit explicit SP adjustments so emitCallFramePseudoInstr fires.
  return false;
}

MachineBasicBlock::iterator VAXFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  MachineInstr &Old = *I;
  DebugLoc DL = Old.getDebugLoc();
  unsigned Opc = Old.getOpcode();
  int64_t Amount = Old.getOperand(0).getImm();

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  if (Opc == TII.getCallFrameSetupOpcode()) {
    // CALLSEQ_START: decrement SP to reserve space for call arguments.
    if (Amount != 0)
      BuildMI(MBB, I, DL, TII.get(VAX::SUBL2_ri), VAX::SP)
          .addImm(Amount).addReg(VAX::SP);
  } else {
    // CALLSEQ_END: subtract callee-pop amount. On VAX, CALLS/RET pops
    // all args, so Amount - CalleePop is typically 0 (no-op).
    int64_t CalleePop = Old.getOperand(1).getImm();
    Amount -= CalleePop;
    if (Amount != 0)
      BuildMI(MBB, I, DL, TII.get(VAX::ADDL2_ri), VAX::SP)
          .addImm(Amount).addReg(VAX::SP);
  }
  return MBB.erase(I);
}
