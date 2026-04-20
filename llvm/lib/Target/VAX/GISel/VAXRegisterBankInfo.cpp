//===-- VAXRegisterBankInfo.cpp - VAX Register Bank Info ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Trivial register bank selection for VAX: everything goes to GPRB.
// VAX has no FP register file — F_float and D_float live in GPRs.
//
//===----------------------------------------------------------------------===//

#include "VAXRegisterBankInfo.h"
#include "VAXRegisterInfo.h"
#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterBank.h"

#define GET_TARGET_REGBANK_IMPL
#include "VAXGenRegisterBank.inc"

#define DEBUG_TYPE "vax-regbankinfo"

using namespace llvm;

VAXRegisterBankInfo::VAXRegisterBankInfo(const TargetRegisterInfo &TRI)
    : VAXGenRegisterBankInfo() {}

const RegisterBank &
VAXRegisterBankInfo::getRegBankFromRegClass(const TargetRegisterClass &RC,
                                            LLT Ty) const {
  // Everything maps to the GPR bank. VAX has no separate FP registers.
  return getRegBank(VAX::GPRBRegBankID);
}

const RegisterBankInfo::InstructionMapping &
VAXRegisterBankInfo::getInstrMapping(const MachineInstr &MI) const {
  const MachineFunction &MF = *MI.getParent()->getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  unsigned Opc = MI.getOpcode();

  // Handle copies and target-specific instructions.
  if (!isPreISelGenericOpcode(Opc) || Opc == TargetOpcode::G_PHI) {
    const InstructionMapping &Mapping = getInstrMappingImpl(MI);
    if (Mapping.isValid())
      return Mapping;
  }

  // Everything goes to GPRB. Build a mapping with one GPRB operand per
  // register operand.
  unsigned NumOperands = MI.getNumOperands();
  SmallVector<const ValueMapping *, 4> OpdsMapping(NumOperands);

  for (unsigned I = 0; I < NumOperands; ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    if (!MO.isReg() || !MO.getReg())
      continue;

    Register Reg = MO.getReg();
    LLT Ty = MRI.getType(Reg);
    if (!Ty.isValid())
      continue;

    unsigned Size = Ty.getSizeInBits();
    // Map everything to GPRB with appropriate size.
    OpdsMapping[I] = &getValueMapping(0, Size, getRegBank(VAX::GPRBRegBankID));
  }

  return getInstructionMapping(DefaultMappingID, /*Cost=*/1,
                               getOperandsMapping(OpdsMapping), NumOperands);
}
