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
  bool selectBr(MachineInstr &MI) const;
  bool selectBrCond(MachineInstr &MI) const;
  bool selectICmp(MachineInstr &MI) const;
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

  if (!isPreISelGenericOpcode(Opc) || Opc == TargetOpcode::G_PHI) {
    if (Opc == TargetOpcode::PHI || Opc == TargetOpcode::G_PHI) {
      Register DefReg = MI.getOperand(0).getReg();
      LLT DefTy = MRI->getType(DefReg);
      const TargetRegisterClass *DefRC = getRegClassForType(DefTy);
      if (!DefRC)
        return false;
      MI.setDesc(TII.get(TargetOpcode::PHI));
      if (!RBI.constrainGenericRegister(DefReg, *DefRC, *MRI))
        return false;
      // Constrain each incoming value register too so the PHI joins a single
      // class. Operands are (def, val0, bb0, val1, bb1, ...).
      for (unsigned I = 1, E = MI.getNumOperands(); I < E; I += 2) {
        Register InReg = MI.getOperand(I).getReg();
        if (!MRI->getRegClassOrNull(InReg))
          RBI.constrainGenericRegister(InReg, *DefRC, *MRI);
      }
      return true;
    }
    // Target-specific opcode — already selected, just constrain registers.
    if (Opc == TargetOpcode::COPY)
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
  case TargetOpcode::G_BR:
    return selectBr(MI);
  case TargetOpcode::G_BRCOND:
    return selectBrCond(MI);
  case TargetOpcode::G_ICMP:
    return selectICmp(MI);
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

// Map an LLVM ICmp predicate to the VAX conditional branch opcode to take
// when the branch condition evaluates to "true".
static unsigned getVAXBranchOpcodeForICmp(CmpInst::Predicate Pred) {
  switch (Pred) {
  case CmpInst::ICMP_EQ:  return VAX::BEQL;
  case CmpInst::ICMP_NE:  return VAX::BNEQ;
  case CmpInst::ICMP_SGT: return VAX::BGTR;
  case CmpInst::ICMP_SGE: return VAX::BGEQ;
  case CmpInst::ICMP_SLT: return VAX::BLSS;
  case CmpInst::ICMP_SLE: return VAX::BLEQ;
  case CmpInst::ICMP_UGT: return VAX::BGTRU;
  case CmpInst::ICMP_UGE: return VAX::BGEQU;
  case CmpInst::ICMP_ULT: return VAX::BLSSU;
  case CmpInst::ICMP_ULE: return VAX::BLEQU;
  default: return 0;
  }
}

bool VAXInstructionSelector::selectBr(MachineInstr &MI) const {
  // G_BR $bb → BRW $bb (16-bit PC-relative branch; far branches use JMP).
  MachineBasicBlock *Target = MI.getOperand(0).getMBB();
  MachineIRBuilder B(MI);
  B.buildInstr(VAX::BRW).addMBB(Target);
  MI.eraseFromParent();
  return true;
}

bool VAXInstructionSelector::selectBrCond(MachineInstr &MI) const {
  // G_BRCOND %cond, $bb
  // Fold with feeding G_ICMP if single-use: emit CMPL + Bxx.
  // Otherwise: emit TSTL %cond + BNEQ $bb (cond is the bool {0,1}).
  Register CondReg = MI.getOperand(0).getReg();
  MachineBasicBlock *Target = MI.getOperand(1).getMBB();

  MachineInstr *CondDef = MRI->getVRegDef(CondReg);
  MachineIRBuilder B(MI);

  if (CondDef && CondDef->getOpcode() == TargetOpcode::G_ICMP &&
      MRI->hasOneNonDBGUse(CondReg)) {
    CmpInst::Predicate Pred =
        static_cast<CmpInst::Predicate>(CondDef->getOperand(1).getPredicate());
    unsigned BrOpc = getVAXBranchOpcodeForICmp(Pred);
    if (!BrOpc)
      return false;

    Register LHS = CondDef->getOperand(2).getReg();
    Register RHS = CondDef->getOperand(3).getReg();

    // Determine compare opcode from LHS type width.
    LLT LHSTy = MRI->getType(LHS);
    unsigned CmpOpc;
    const TargetRegisterClass *OpRC;
    if (LHSTy.getSizeInBits() == 8) {
      CmpOpc = VAX::CMPB_rr;
      OpRC = &VAX::GPRBRegClass;
    } else if (LHSTy.getSizeInBits() == 16) {
      CmpOpc = VAX::CMPW_rr;
      OpRC = &VAX::GPRWRegClass;
    } else if (LHSTy.getSizeInBits() == 32) {
      CmpOpc = VAX::CMPL_rr;
      OpRC = &VAX::GPRIRegClass;
    } else {
      return false;
    }

    auto CmpMI = B.buildInstr(CmpOpc).addUse(LHS).addUse(RHS);
    RBI.constrainGenericRegister(LHS, *OpRC, *MRI);
    RBI.constrainGenericRegister(RHS, *OpRC, *MRI);
    (void)CmpMI;

    B.buildInstr(BrOpc).addMBB(Target);

    // Erase the G_ICMP (now dead) and the G_BRCOND.
    CondDef->eraseFromParent();
    MI.eraseFromParent();
    return true;
  }

  // Fallback: test the bool reg and BNEQ if non-zero.
  B.buildInstr(VAX::TSTL).addUse(CondReg);
  RBI.constrainGenericRegister(CondReg, VAX::GPRIRegClass, *MRI);
  B.buildInstr(VAX::BNEQ).addMBB(Target);
  MI.eraseFromParent();
  return true;
}

static unsigned getVAXCCForICmp(CmpInst::Predicate Pred) {
  switch (Pred) {
  case CmpInst::ICMP_EQ:  return 0;  // EQL
  case CmpInst::ICMP_NE:  return 1;  // NEQ
  case CmpInst::ICMP_SGT: return 2;  // GTR
  case CmpInst::ICMP_SGE: return 3;  // GEQ
  case CmpInst::ICMP_SLT: return 4;  // LSS
  case CmpInst::ICMP_SLE: return 5;  // LEQ
  case CmpInst::ICMP_UGT: return 6;  // GTRU
  case CmpInst::ICMP_UGE: return 7;  // GEQU
  case CmpInst::ICMP_ULT: return 8;  // LSSU
  case CmpInst::ICMP_ULE: return 9;  // LEQU
  default: return ~0U;
  }
}

bool VAXInstructionSelector::selectICmp(MachineInstr &MI) const {
  // Standalone G_ICMP (not folded into a branch).
  //
  // Emit:
  //   MOVL $1, TrueReg
  //   CLRL FalseReg
  //   CMPx LHS, RHS            ; sets PSW
  //   SELECT_CC_Pseudo dst, TrueReg, FalseReg, cc
  //
  // FinalizeISel expands SELECT_CC_Pseudo into a diamond via the target
  // lowering's EmitInstrWithCustomInserter, adding the conditional Bxx
  // and PHI. We reuse the SDAG path entirely.
  Register DstReg = MI.getOperand(0).getReg();
  CmpInst::Predicate Pred =
      static_cast<CmpInst::Predicate>(MI.getOperand(1).getPredicate());
  Register LHS = MI.getOperand(2).getReg();
  Register RHS = MI.getOperand(3).getReg();

  unsigned CC = getVAXCCForICmp(Pred);
  if (CC == ~0U)
    return false;

  LLT LHSTy = MRI->getType(LHS);
  unsigned CmpOpc;
  const TargetRegisterClass *OpRC;
  if (LHSTy.getSizeInBits() == 8) {
    CmpOpc = VAX::CMPB_rr;
    OpRC = &VAX::GPRBRegClass;
  } else if (LHSTy.getSizeInBits() == 16) {
    CmpOpc = VAX::CMPW_rr;
    OpRC = &VAX::GPRWRegClass;
  } else if (LHSTy.getSizeInBits() == 32) {
    CmpOpc = VAX::CMPL_rr;
    OpRC = &VAX::GPRIRegClass;
  } else {
    return false;
  }

  MachineIRBuilder B(MI);
  Register TrueReg = MRI->createVirtualRegister(&VAX::GPRIRegClass);
  Register FalseReg = MRI->createVirtualRegister(&VAX::GPRIRegClass);

  B.buildInstr(VAX::MOVL_ri, {TrueReg}, {}).addImm(1);
  B.buildInstr(VAX::CLRL, {FalseReg}, {});
  B.buildInstr(CmpOpc).addUse(LHS).addUse(RHS);
  RBI.constrainGenericRegister(LHS, *OpRC, *MRI);
  RBI.constrainGenericRegister(RHS, *OpRC, *MRI);

  B.buildInstr(VAX::SELECT_CC_Pseudo, {DstReg}, {TrueReg, FalseReg})
      .addImm(CC);
  RBI.constrainGenericRegister(DstReg, VAX::GPRIRegClass, *MRI);

  MI.eraseFromParent();
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
