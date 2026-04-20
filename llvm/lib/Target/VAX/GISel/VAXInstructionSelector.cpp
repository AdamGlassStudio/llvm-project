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

#include "VAX.h"
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
    this->MF = &MF;
  }

  static const char *getName() { return DEBUG_TYPE; }

private:
  bool selectImpl(MachineInstr &MI, CodeGenCoverage &CoverageInfo) const;
  bool selectCopy(MachineInstr &MI) const;
  bool selectALU(MachineInstr &MI, unsigned MachOpc) const;
  bool selectLoad(MachineInstr &MI) const;
  bool selectStore(MachineInstr &MI) const;
  bool selectConstant(MachineInstr &MI) const;
  bool selectFrameIndex(MachineInstr &MI) const;
  const TargetRegisterClass *getRegClassForType(LLT Ty) const;

  const VAXSubtarget &STI;
  const VAXInstrInfo &TII;
  const VAXRegisterInfo &TRI;
  const VAXRegisterBankInfo &RBI;
  MachineRegisterInfo *MRI = nullptr;
  MachineFunction *MF = nullptr;

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

  // Manual selection for common operations.
  switch (Opc) {
  case TargetOpcode::G_ADD:
    return selectALU(MI, VAX::ADDL3_rr_cc);
  case TargetOpcode::G_SUB:
    return selectALU(MI, VAX::SUBL3_rr_cc);
  case TargetOpcode::G_LOAD:
    return selectLoad(MI);
  case TargetOpcode::G_STORE:
    return selectStore(MI);
  case TargetOpcode::G_CONSTANT:
    return selectConstant(MI);
  case TargetOpcode::G_FRAME_INDEX:
    return selectFrameIndex(MI);
  default:
    break;
  }

  LLVM_DEBUG(dbgs() << "VAX GISel: Cannot select: " << MI);
  return false;
}

bool VAXInstructionSelector::selectCopy(MachineInstr &MI) const {
  Register DstReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(1).getReg();

  // Constrain source vreg if it lacks a register class.
  if (SrcReg.isVirtual() && !MRI->getRegClassOrNull(SrcReg)) {
    LLT SrcTy = MRI->getType(SrcReg);
    const TargetRegisterClass *SrcRC = nullptr;
    if (SrcTy.isPointer() || SrcTy.getSizeInBits() == 32)
      SrcRC = &VAX::GPRIRegClass;
    else if (SrcTy.getSizeInBits() == 16)
      SrcRC = &VAX::GPRWRegClass;
    else if (SrcTy.getSizeInBits() == 8)
      SrcRC = &VAX::GPRBRegClass;
    else if (SrcTy.getSizeInBits() == 64)
      SrcRC = &VAX::QPRRegClass;
    if (SrcRC)
      RBI.constrainGenericRegister(SrcReg, *SrcRC, *MRI);
  }

  if (DstReg.isPhysical())
    return true;

  const TargetRegisterClass *DstRC = MRI->getRegClassOrNull(DstReg);
  if (!DstRC) {
    LLT DstTy = MRI->getType(DstReg);
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
      return false;
    }
  }
  return true;
}

/// Constrain a vreg to a register class based on its type.
const TargetRegisterClass *
VAXInstructionSelector::getRegClassForType(LLT Ty) const {
  if (Ty.isPointer() || Ty.getSizeInBits() == 32)
    return &VAX::GPRIRegClass;
  if (Ty.getSizeInBits() == 16)
    return &VAX::GPRWRegClass;
  if (Ty.getSizeInBits() == 8)
    return &VAX::GPRBRegClass;
  if (Ty.getSizeInBits() == 64)
    return &VAX::QPRRegClass;
  return nullptr;
}

/// Select G_ADD/G_SUB → three-operand ALU instruction (e.g., ADDL3_rr_cc).
bool VAXInstructionSelector::selectALU(MachineInstr &MI,
                                       unsigned MachOpc) const {
  Register DstReg = MI.getOperand(0).getReg();
  Register Src1Reg = MI.getOperand(1).getReg();
  Register Src2Reg = MI.getOperand(2).getReg();

  const TargetRegisterClass *RC = &VAX::GPRIRegClass;
  RBI.constrainGenericRegister(DstReg, *RC, *MRI);
  RBI.constrainGenericRegister(Src1Reg, *RC, *MRI);
  RBI.constrainGenericRegister(Src2Reg, *RC, *MRI);

  MachineInstr *NewMI = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
                                TII.get(MachOpc), DstReg)
                            .addReg(Src1Reg)
                            .addReg(Src2Reg);
  (void)NewMI;
  MI.eraseFromParent();
  return true;
}

/// Select G_LOAD → MOVL_rm.
/// If the pointer is a G_FRAME_INDEX, fold it into a displacement load.
bool VAXInstructionSelector::selectLoad(MachineInstr &MI) const {
  Register DstReg = MI.getOperand(0).getReg();
  Register PtrReg = MI.getOperand(1).getReg();
  MachineMemOperand *MMO = *MI.memoperands_begin();

  LLT DstTy = MRI->getType(DstReg);
  unsigned MovOpc;
  if (DstTy.getSizeInBits() == 32 || DstTy.isPointer())
    MovOpc = VAX::MOVL_rm;
  else {
    LLVM_DEBUG(dbgs() << "VAX GISel: unsupported load type " << DstTy << "\n");
    return false;
  }

  RBI.constrainGenericRegister(DstReg, VAX::GPRIRegClass, *MRI);

  // Check if pointer comes from G_FRAME_INDEX — fold into displacement mode.
  MachineInstr *PtrDef = MRI->getVRegDef(PtrReg);
  if (PtrDef && PtrDef->getOpcode() == TargetOpcode::G_FRAME_INDEX) {
    int FI = PtrDef->getOperand(1).getIndex();
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
            TII.get(MovOpc), DstReg)
        .addFrameIndex(FI)        // base (frame index, resolved by PEI)
        .addImm(0)                // displacement (PEI adds FP offset)
        .addReg(0)                // index (noreg)
        .addImm(VAXAM::Disp)
        .addMemOperand(MMO);
  } else {
    RBI.constrainGenericRegister(PtrReg, VAX::GPRIRegClass, *MRI);
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
            TII.get(MovOpc), DstReg)
        .addReg(PtrReg)           // base
        .addImm(0)                // displacement
        .addReg(0)                // index (noreg)
        .addImm(VAXAM::RegDeferred)
        .addMemOperand(MMO);
  }
  MI.eraseFromParent();
  return true;
}

/// Select G_STORE → MOVL_mr with register-deferred addressing.
bool VAXInstructionSelector::selectStore(MachineInstr &MI) const {
  Register ValReg = MI.getOperand(0).getReg();
  Register PtrReg = MI.getOperand(1).getReg();
  MachineMemOperand *MMO = *MI.memoperands_begin();

  LLT ValTy = MRI->getType(ValReg);
  unsigned MovOpc;
  if (ValTy.getSizeInBits() == 32 || ValTy.isPointer())
    MovOpc = VAX::MOVL_mr;
  else {
    LLVM_DEBUG(dbgs() << "VAX GISel: unsupported store type " << ValTy << "\n");
    return false;
  }

  // Constrain registers.
  RBI.constrainGenericRegister(ValReg, VAX::GPRIRegClass, *MRI);
  RBI.constrainGenericRegister(PtrReg, VAX::GPRIRegClass, *MRI);

  // Build: MOVL_mr $val, $ptr, 0, $noreg, RegDeferred
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
          TII.get(MovOpc))
      .addReg(ValReg)           // source
      .addReg(PtrReg)           // base
      .addImm(0)                // displacement
      .addReg(0)                // index (noreg)
      .addImm(VAXAM::RegDeferred)
      .addMemOperand(MMO);
  MI.eraseFromParent();
  return true;
}

/// Select G_CONSTANT → MOVL_ri (immediate to register).
bool VAXInstructionSelector::selectConstant(MachineInstr &MI) const {
  Register DstReg = MI.getOperand(0).getReg();
  int64_t Val = MI.getOperand(1).getCImm()->getSExtValue();

  RBI.constrainGenericRegister(DstReg, VAX::GPRIRegClass, *MRI);
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
          TII.get(VAX::MOVL_ri), DstReg)
      .addImm(Val);
  MI.eraseFromParent();
  return true;
}

/// Select G_FRAME_INDEX — usually folded into load/store.
/// If standalone (e.g., address taken), materialize via ADDL3 of base + offset.
/// If dead (folded into load/store), just erase.
bool VAXInstructionSelector::selectFrameIndex(MachineInstr &MI) const {
  Register DstReg = MI.getOperand(0).getReg();

  // If the frame index was folded into a load/store, it has no remaining uses.
  if (MRI->use_nodbg_empty(DstReg)) {
    MI.eraseFromParent();
    return true;
  }

  // TODO: Standalone frame-index materialization for address-taken locals.
  LLVM_DEBUG(dbgs() << "VAX GISel: standalone G_FRAME_INDEX not supported: "
                    << MI);
  return false;
}

namespace llvm {
InstructionSelector *
createVAXInstructionSelector(const VAXTargetMachine &TM,
                             const VAXSubtarget &Subtarget,
                             const VAXRegisterBankInfo &RBI) {
  return new VAXInstructionSelector(TM, Subtarget, RBI);
}
} // namespace llvm
