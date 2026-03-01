//===-- VAXTargetMachine.h - Define TargetMachine for VAX -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_VAXTARGETMACHINE_H
#define LLVM_LIB_TARGET_VAX_VAXTARGETMACHINE_H

#include "VAXSubtarget.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include <memory>
#include <optional>

namespace llvm {

class VAXTargetMachine : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  VAXSubtarget Subtarget;

public:
  VAXTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                   StringRef FS, const TargetOptions &Options,
                   std::optional<Reloc::Model> RM,
                   std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                   bool JIT);
  ~VAXTargetMachine() override;

  const VAXSubtarget *getSubtargetImpl(const Function &) const override {
    return &Subtarget;
  }

  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_VAX_VAXTARGETMACHINE_H
