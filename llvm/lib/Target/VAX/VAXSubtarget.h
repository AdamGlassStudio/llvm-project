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
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include <string>

#define GET_SUBTARGETINFO_HEADER
#include "VAXGenSubtargetInfo.inc"

namespace llvm {
class StringRef;
class VAXTargetMachine;

class VAXSubtarget : public VAXGenSubtargetInfo {
  VAXInstrInfo InstrInfo;
  VAXFrameLowering FrameLowering;
  VAXTargetLowering TLInfo;
  SelectionDAGTargetInfo TSInfo;

public:
  VAXSubtarget(const Triple &TT, StringRef CPU, StringRef FS,
               const VAXTargetMachine &TM);

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
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_VAX_VAXSUBTARGET_H
