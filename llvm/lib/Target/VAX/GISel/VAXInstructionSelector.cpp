//===-- VAXInstructionSelector.cpp - VAX GISel Instruction Selector ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Stub instruction selector for VAX GlobalISel.
//
// The interesting question for VAX is whether GISel's G_LOAD/G_STORE with
// complex addressing can be folded into VAX's rich memory operand modes
// (register deferred, displacement, indexed, autoincrement, etc.) as
// naturally as SelectionDAG's ComplexPattern matching does.
//
//===----------------------------------------------------------------------===//

#include "VAXInstrInfo.h"
#include "VAXRegisterBankInfo.h"
#include "VAXSubtarget.h"
#include "VAXTargetMachine.h"
#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutorImpl.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "vax-gisel"

using namespace llvm;

namespace {

#define GET_GLOBALISEL_PREDICATE_BITSET
#include "VAXGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATE_BITSET

class VAXInstructionSelector : public InstructionSelector {
public:
  VAXInstructionSelector(const VAXTargetMachine &TM, const VAXSubtarget &STI,
                         const VAXRegisterBankInfo &RBI);

  bool select(MachineInstr &MI) override;

  void setupMF(MachineFunction &MF, GISelValueTracking *VT,
               CodeGenCoverage *CoverageInfo, ProfileSummaryInfo *PSI,
               BlockFrequencyInfo *BFI) override {
    InstructionSelector::setupMF(MF, VT, CoverageInfo, PSI, BFI);
    MRI = &MF.getRegInfo();
  }

  static const char *getName() { return DEBUG_TYPE; }

private:
  bool selectImpl(MachineInstr &MI, CodeGenCoverage &CoverageInfo) const;
  bool selectCopy(MachineInstr &MI) const;

  const VAXSubtarget &STI;
  const VAXInstrInfo &TII;
  const VAXRegisterInfo &TRI;
  const VAXRegisterBankInfo &RBI;
  MachineRegisterInfo *MRI = nullptr;

#define GET_GLOBALISEL_PREDICATES_DECL
#include "VAXGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATES_DECL

#define GET_GLOBALISEL_TEMPORARIES_DECL
#include "VAXGenGlobalISel.inc"
#undef GET_GLOBALISEL_TEMPORARIES_DECL
};

} // end anonymous namespace

#define GET_GLOBALISEL_IMPL
#include "VAXGenGlobalISel.inc"
#undef GET_GLOBALISEL_IMPL

VAXInstructionSelector::VAXInstructionSelector(const VAXTargetMachine &TM,
                                               const VAXSubtarget &STI,
                                               const VAXRegisterBankInfo &RBI)
    : STI(STI), TII(*STI.getInstrInfo()), TRI(*STI.getRegisterInfo()),
      RBI(RBI),
#define GET_GLOBALISEL_PREDICATES_INIT
#include "VAXGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATES_INIT
#define GET_GLOBALISEL_TEMPORARIES_INIT
#include "VAXGenGlobalISel.inc"
#undef GET_GLOBALISEL_TEMPORARIES_INIT
{
}

bool VAXInstructionSelector::select(MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();

  if (!isPreISelGenericOpcode(Opc)) {
    // Target-specific opcode — already selected, just constrain registers.
    if (Opc == TargetOpcode::PHI || Opc == TargetOpcode::COPY)
      return selectCopy(MI);
    return true;
  }

  // Try TableGen-generated patterns first.
  if (selectImpl(MI, *CoverageInfo))
    return true;

  // Manual selection for anything TableGen can't handle.
  LLVM_DEBUG(dbgs() << "VAX GISel: Cannot select: " << MI);
  return false;
}

bool VAXInstructionSelector::selectCopy(MachineInstr &MI) const {
  Register DstReg = MI.getOperand(0).getReg();
  if (DstReg.isPhysical())
    return true;

  const TargetRegisterClass *DstRC = MRI->getRegClassOrNull(DstReg);
  if (!DstRC) {
    LLT DstTy = MRI->getType(DstReg);
    // Map everything to GPRI (R0-R11, allocatable GPRs).
    if (DstTy.isPointer() || DstTy.getSizeInBits() == 32)
      DstRC = &VAX::GPRIRegClass;
    else if (DstTy.getSizeInBits() == 16)
      DstRC = &VAX::GPRWRegClass;
    else if (DstTy.getSizeInBits() == 8)
      DstRC = &VAX::GPRBRegClass;
    else if (DstTy.getSizeInBits() == 64)
      DstRC = &VAX::QPRRegClass;
    else
      return false;

    if (!RBI.constrainGenericRegister(DstReg, *DstRC, *MRI)) {
      LLVM_DEBUG(dbgs() << "VAX GISel: Failed to constrain " << MI);
      return false;
    }
  }
  return true;
}

namespace llvm {
InstructionSelector *
createVAXInstructionSelector(const VAXTargetMachine &TM,
                             const VAXSubtarget &Subtarget,
                             const VAXRegisterBankInfo &RBI) {
  return new VAXInstructionSelector(TM, Subtarget, RBI);
}
} // namespace llvm
