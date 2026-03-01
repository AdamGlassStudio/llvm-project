//===-- VAXTargetInfo.cpp - VAX Target Implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/VAXTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

Target &llvm::getTheVAXTarget() {
  static Target TheVAXTarget;
  return TheVAXTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVAXTargetInfo() {
  RegisterTarget<Triple::vax> X(getTheVAXTarget(), "vax", "VAX",
                                 "VAX");
}
