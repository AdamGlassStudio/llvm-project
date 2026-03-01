//===-- VAXRegisterInfo.cpp - VAX Register Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXRegisterInfo.h"
#include "VAXSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

// Include enum values so GET_REGINFO_TARGET_DESC can reference VAX::GPRRegClassID etc.
#define GET_REGINFO_ENUM
#include "VAXGenRegisterInfo.inc"

#define GET_REGINFO_TARGET_DESC
#include "VAXGenRegisterInfo.inc"

VAXRegisterInfo::VAXRegisterInfo() : VAXGenRegisterInfo(VAX::PC) {}

const MCPhysReg *
VAXRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_VAX_SaveList;
}

const uint32_t *
VAXRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                      CallingConv::ID CC) const {
  return CSR_VAX_RegMask;
}

BitVector VAXRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  markSuperRegs(Reserved, VAX::AP); // Argument Pointer
  markSuperRegs(Reserved, VAX::FP); // Frame Pointer
  markSuperRegs(Reserved, VAX::SP); // Stack Pointer
  markSuperRegs(Reserved, VAX::PC); // Program Counter
  return Reserved;
}

const TargetRegisterClass *
VAXRegisterInfo::getPointerRegClass(unsigned Kind) const {
  return &VAX::GPRRegClass;
}

bool VAXRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                          int SPAdj, unsigned FIOperandNum,
                                          RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  int FI = MI.getOperand(FIOperandNum).getIndex();

  // Object offset is negative (locals are below FP on stack-grows-down).
  // The displacement operand is at FIOperandNum+1 (VAXMemOp = base, disp).
  int64_t Offset = MFI.getObjectOffset(FI) +
                   MI.getOperand(FIOperandNum + 1).getImm();

  MI.getOperand(FIOperandNum).ChangeToRegister(VAX::FP, false);
  MI.getOperand(FIOperandNum + 1).setImm(Offset);
  return false;
}

Register VAXRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return VAX::FP;
}
