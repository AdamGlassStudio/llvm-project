//===-- VAX.h - Top-level interface for VAX representation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_VAX_H
#define LLVM_LIB_TARGET_VAX_VAX_H

#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "llvm/PassRegistry.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class FunctionPass;
class PassRegistry;
class VAXTargetMachine;

FunctionPass *createVAXISelDag(VAXTargetMachine &TM,
                                CodeGenOptLevel OptLevel);
FunctionPass *createVAXFixupPSWPass();
FunctionPass *createVAXFuseCmpBranchPass();
FunctionPass *createVAXExpandCmpBranchPass();
FunctionPass *createVAXPeepholePass();
void initializeVAXDAGToDAGISelLegacyPass(PassRegistry &);
void initializeVAXFixupPSWPass(PassRegistry &);
void initializeVAXFuseCmpBranchPass(PassRegistry &);
void initializeVAXExpandCmpBranchPass(PassRegistry &);
void initializeVAXPeepholePass(PassRegistry &);

} // namespace llvm

#endif // LLVM_LIB_TARGET_VAX_VAX_H
