//===-- VAXSubtarget.h - Define Subtarget for VAX ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_VAXSUBTARGET_H
#define LLVM_LIB_TARGET_VAX_VAXSUBTARGET_H

#include "VAXFrameLowering.h"
#include "VAXISelLowering.h"
#include "VAXInstrInfo.h"
#include "VAXRegisterInfo.h"
#include "VAXSelectionDAGInfo.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include <memory>
#include <string>

#define GET_SUBTARGETINFO_HEADER
#include "VAXGenSubtargetInfo.inc"

namespace llvm {
class StringRef;
class VAXTargetMachine;
class VAXRegisterBankInfo;

class VAXSubtarget : public VAXGenSubtargetInfo {
  VAXInstrInfo InstrInfo;
  VAXFrameLowering FrameLowering;
  VAXTargetLowering TLInfo;
  VAXSelectionDAGInfo TSInfo;

  // GlobalISel members — lazily initialized.
  // Note: destructors need complete types, so these are destroyed in the .cpp.
  mutable std::unique_ptr<CallLowering> CallLoweringInfo;
  mutable std::unique_ptr<InstructionSelector> InstSelector;
  mutable std::unique_ptr<LegalizerInfo> Legalizer;
  mutable std::unique_ptr<RegisterBankInfo> RegBankInfo;

public:
  VAXSubtarget(const Triple &TT, StringRef CPU, StringRef FS,
               const VAXTargetMachine &TM);
  ~VAXSubtarget() override;

  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  const VAXInstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const VAXFrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const VAXTargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const VAXRegisterInfo *getRegisterInfo() const override {
    return &InstrInfo.getRegisterInfo();
  }
  const VAXSelectionDAGInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }

  // GlobalISel support.
  const CallLowering *getCallLowering() const override;
  InstructionSelector *getInstructionSelector() const override;
  const LegalizerInfo *getLegalizerInfo() const override;
  const RegisterBankInfo *getRegBankInfo() const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_VAX_VAXSUBTARGET_H
