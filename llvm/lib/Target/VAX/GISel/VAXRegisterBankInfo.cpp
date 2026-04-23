//===-- VAXRegisterBankInfo.cpp - VAX Register Bank Info ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Register bank selection for VAX.
//   - GPRB (32-bit) holds i1/i8/i16/i32/p0 and F_float.
//   - QPRB (64-bit, consecutive-pair pseudo) holds i64 and D_float.
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
  switch (RC.getID()) {
  case VAX::QPRRegClassID:
    return getRegBank(VAX::QPRBRegBankID);
  default:
    return getRegBank(VAX::GPRBRegBankID);
  }
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
    unsigned BankID;
    if (Size <= 32)
      BankID = VAX::GPRBRegBankID;
    else if (Size == 64)
      BankID = VAX::QPRBRegBankID;
    else
      return getInvalidInstructionMapping();
    OpdsMapping[I] = &getValueMapping(0, Size, getRegBank(BankID));
  }

  return getInstructionMapping(DefaultMappingID, /*Cost=*/1,
                               getOperandsMapping(OpdsMapping), NumOperands);
}
