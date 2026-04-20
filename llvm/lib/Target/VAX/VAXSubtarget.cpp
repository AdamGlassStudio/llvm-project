//===-- VAXSubtarget.cpp - VAX Subtarget Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXSubtarget.h"
#include "GISel/VAXCallLowering.h"
#include "GISel/VAXLegalizerInfo.h"
#include "GISel/VAXRegisterBankInfo.h"
#include "VAX.h"
#include "VAXTargetMachine.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "vax-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "VAXGenSubtargetInfo.inc"

VAXSubtarget::VAXSubtarget(const Triple &TT, StringRef CPU, StringRef FS,
                           const VAXTargetMachine &TM)
    : VAXGenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS), InstrInfo(*this),
      FrameLowering(), TLInfo(TM, *this) {
  ParseSubtargetFeatures(CPU, /*TuneCPU=*/CPU, FS);
}

// Out-of-line destructor so unique_ptr members see complete types.
VAXSubtarget::~VAXSubtarget() = default;

const CallLowering *VAXSubtarget::getCallLowering() const {
  if (!CallLoweringInfo)
    CallLoweringInfo.reset(new VAXCallLowering(*getTargetLowering()));
  return CallLoweringInfo.get();
}

InstructionSelector *VAXSubtarget::getInstructionSelector() const {
  if (!InstSelector) {
    InstSelector.reset(createVAXInstructionSelector(
        *static_cast<const VAXTargetMachine *>(&TLInfo.getTargetMachine()),
        *this, *static_cast<const VAXRegisterBankInfo *>(getRegBankInfo())));
  }
  return InstSelector.get();
}

const LegalizerInfo *VAXSubtarget::getLegalizerInfo() const {
  if (!Legalizer)
    Legalizer.reset(new VAXLegalizerInfo(*this));
  return Legalizer.get();
}

const RegisterBankInfo *VAXSubtarget::getRegBankInfo() const {
  if (!RegBankInfo)
    RegBankInfo.reset(new VAXRegisterBankInfo(*getRegisterInfo()));
  return RegBankInfo.get();
}
