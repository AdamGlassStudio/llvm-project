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
class InstructionSelector;
class PassRegistry;
class VAXRegisterBankInfo;
class VAXSubtarget;
class VAXTargetMachine;

/// VAX addressing mode flags for memory operands.
/// Stored in the 4th MCOperand of a VAXMemOp (base, disp, index, flags).
namespace VAXAM {
enum : unsigned {
  Disp = 0,        // disp(Rn)   — byte/word/longword displacement
  RegDirect = 1,   // %Rn        — register direct (0x50|Rn)
  RegDeferred = 2, // (%Rn)      — register deferred (0x60|Rn)
  AutoDec = 3,     // -(%Rn)     — autodecrement (0x70|Rn)
  AutoInc = 4,     // (%Rn)+     — autoincrement (0x80|Rn)
  DispDeferred = 5,// *disp(Rn)  — displacement deferred (0xB0/D0/F0|Rn)
  AutoIncDef = 6,  // *(%Rn)+    — autoincrement deferred (0x90|Rn)
  Imm = 7,         // $val       — literal (0-63) or immediate (0x8F)
  Absolute = 8,    // *$addr     — absolute deferred (0x9F + addr32)
};
} // namespace VAXAM

FunctionPass *createVAXISelDag(VAXTargetMachine &TM,
                                CodeGenOptLevel OptLevel);
FunctionPass *createVAXFixupPSWPass();
FunctionPass *createVAXFuseCmpBranchPass();
FunctionPass *createVAXExpandCmpBranchPass();
FunctionPass *createVAXPeepholePass();
FunctionPass *createVAXSobAobCombinePass();
void initializeVAXDAGToDAGISelLegacyPass(PassRegistry &);
void initializeVAXFixupPSWPass(PassRegistry &);
void initializeVAXFuseCmpBranchPass(PassRegistry &);
void initializeVAXExpandCmpBranchPass(PassRegistry &);
void initializeVAXPeepholePass(PassRegistry &);
void initializeVAXSobAobCombinePass(PassRegistry &);

InstructionSelector *
createVAXInstructionSelector(const VAXTargetMachine &TM,
                             const VAXSubtarget &Subtarget,
                             const VAXRegisterBankInfo &RBI);

} // namespace llvm

#endif // LLVM_LIB_TARGET_VAX_VAX_H
