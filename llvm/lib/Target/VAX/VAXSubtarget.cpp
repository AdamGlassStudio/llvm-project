//===-- VAXSubtarget.cpp - VAX Subtarget Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXSubtarget.h"
#include "VAXTargetMachine.h"
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
