//===-- VAXRegisterBankInfo.h - VAX Register Bank Info -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// VAX has a single register bank (GPR). FP values live in GPRs too.
// This makes register bank selection trivially simple.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_VAXREGISTERBANKINFO_H
#define LLVM_LIB_TARGET_VAX_VAXREGISTERBANKINFO_H

#include "llvm/CodeGen/RegisterBankInfo.h"

#define GET_REGBANK_DECLARATIONS
#include "VAXGenRegisterBank.inc"

namespace llvm {

class TargetRegisterInfo;

class VAXGenRegisterBankInfo : public RegisterBankInfo {
protected:
#define GET_TARGET_REGBANK_CLASS
#include "VAXGenRegisterBank.inc"
};

class VAXRegisterBankInfo final : public VAXGenRegisterBankInfo {
public:
  VAXRegisterBankInfo(const TargetRegisterInfo &TRI);

  const InstructionMapping &
  getInstrMapping(const MachineInstr &MI) const override;

  const RegisterBank &getRegBankFromRegClass(const TargetRegisterClass &RC,
                                             LLT Ty) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_VAX_VAXREGISTERBANKINFO_H
