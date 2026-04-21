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
#include "MCTargetDesc/VAXBaseInfo.h"
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
  bool selectALU(MachineInstr &MI, unsigned ByteOpc, unsigned WordOpc,
                 unsigned LongOpc) const;
  bool selectLoad(MachineInstr &MI) const;
  bool selectStore(MachineInstr &MI) const;
  bool selectConstant(MachineInstr &MI) const;
  bool selectFrameIndex(MachineInstr &MI) const;
  bool selectGlobalValue(MachineInstr &MI) const;
  bool selectPtrAdd(MachineInstr &MI) const;

  /// Decompose a pointer vreg into VAX memory-operand fields
  /// (Base, Disp, Index, Flags). Mirrors VAXDAGToDAGISel::SelectVAXAddr.
  /// If AllowIndexScale != 0, also matches G_PTR_ADD(base, G_SHL(idx, log2scale))
  /// for indexed addressing of size-matched loads/stores.
  struct MemAddr {
    // Exactly one of BaseReg / BaseFI / BaseGlobal is set. BaseReg==0 + no
    // FI/Global means "no base register" (used with an absolute symbol).
    Register BaseReg;       // 0 = NoReg
    bool HasBaseFI = false;
    int BaseFI = 0;
    const GlobalValue *BaseGV = nullptr;
    int64_t BaseGVOffset = 0;
    int64_t Disp = 0;
    Register IndexReg;      // 0 = NoReg
    unsigned Flags = VAXAM::Disp;
  };
  bool selectAddr(Register Ptr, MemAddr &Out,
                  unsigned IndexScaleLog2 = 0) const;
  // Append the 4 memory-operand fields to MIB.
  void addMemOperands(MachineInstrBuilder &MIB, const MemAddr &A) const;
  bool selectBr(MachineInstr &MI) const;
  bool selectBrCond(MachineInstr &MI) const;
  bool selectICmp(MachineInstr &MI) const;
  bool selectTruncOrAnyExt(MachineInstr &MI) const;
  const TargetRegisterClass *getRegClassForType(LLT Ty) const;

  const VAXTargetMachine &TM;
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
    : TM(TM), STI(STI), TII(*STI.getInstrInfo()), TRI(*STI.getRegisterInfo()),
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
  // Snapshot def vregs so we can ensure they end up with a RegClass even when
  // the tablegen pattern emits `origDef = COPY newvreg` and leaves origDef
  // with only a RegBank.  The main InstructionSelect loop does not revisit
  // instructions inserted during selectImpl, so we have to clean up here.
  SmallVector<Register, 2> DefVRegs;
  for (const MachineOperand &MO : MI.defs())
    if (MO.isReg() && MO.getReg().isVirtual())
      DefVRegs.push_back(MO.getReg());
  if (selectImpl(MI, *CoverageInfo)) {
    for (Register R : DefVRegs) {
      if (!MRI->getRegClassOrNull(R)) {
        if (const TargetRegisterClass *RC =
                getRegClassForType(MRI->getType(R)))
          RBI.constrainGenericRegister(R, *RC, *MRI);
      }
    }
    return true;
  }

  // Manual selection for common operations.
  switch (Opc) {
  case TargetOpcode::G_ADD:
    return selectALU(MI, VAX::ADDB3_rr, VAX::ADDW3_rr, VAX::ADDL3_rr_cc);
  case TargetOpcode::G_SUB:
    return selectALU(MI, VAX::SUBB3_rr, VAX::SUBW3_rr, VAX::SUBL3_rr_cc);
  case TargetOpcode::G_LOAD:
    return selectLoad(MI);
  case TargetOpcode::G_STORE:
    return selectStore(MI);
  case TargetOpcode::G_CONSTANT:
    return selectConstant(MI);
  case TargetOpcode::G_FRAME_INDEX:
    return selectFrameIndex(MI);
  case TargetOpcode::G_GLOBAL_VALUE:
    return selectGlobalValue(MI);
  case TargetOpcode::G_PTR_ADD:
    return selectPtrAdd(MI);
  case TargetOpcode::G_BR:
    return selectBr(MI);
  case TargetOpcode::G_BRCOND:
    return selectBrCond(MI);
  case TargetOpcode::G_ICMP:
    return selectICmp(MI);
  case TargetOpcode::G_TRUNC:
  case TargetOpcode::G_ANYEXT:
    return selectTruncOrAnyExt(MI);
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
bool VAXInstructionSelector::selectALU(MachineInstr &MI, unsigned ByteOpc,
                                       unsigned WordOpc,
                                       unsigned LongOpc) const {
  Register DstReg = MI.getOperand(0).getReg();
  Register Src1Reg = MI.getOperand(1).getReg();
  Register Src2Reg = MI.getOperand(2).getReg();

  LLT DstTy = MRI->getType(DstReg);
  unsigned MachOpc;
  const TargetRegisterClass *RC;
  switch (DstTy.getSizeInBits()) {
  case 8:  MachOpc = ByteOpc; RC = &VAX::GPRBRegClass; break;
  case 16: MachOpc = WordOpc; RC = &VAX::GPRWRegClass; break;
  case 32: MachOpc = LongOpc; RC = &VAX::GPRIRegClass; break;
  default: return false;
  }

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

/// Decompose a pointer vreg into (Base, Disp, Index, Flags) VAX memory-operand
/// fields. Mirrors the SDAG-side SelectVAXAddr closely.
bool VAXInstructionSelector::selectAddr(Register Ptr, MemAddr &Out,
                                        unsigned IndexScaleLog2) const {
  Out = MemAddr{};

  // Walk through no-op COPYs inserted by IRTranslator/legalizer.
  auto skipCopies = [&](Register R) -> Register {
    while (R.isVirtual()) {
      MachineInstr *D = MRI->getVRegDef(R);
      if (!D || D->getOpcode() != TargetOpcode::COPY) break;
      Register Src = D->getOperand(1).getReg();
      if (!Src.isVirtual()) break;
      R = Src;
    }
    return R;
  };
  auto defOf = [&](Register R) -> MachineInstr * {
    return MRI->getVRegDef(skipCopies(R));
  };

  Ptr = skipCopies(Ptr);
  MachineInstr *Def = MRI->getVRegDef(Ptr);

  // G_FRAME_INDEX: stack slot, zero displacement.
  if (Def && Def->getOpcode() == TargetOpcode::G_FRAME_INDEX) {
    Out.HasBaseFI = true;
    Out.BaseFI = Def->getOperand(1).getIndex();
    Out.Flags = VAXAM::Disp;
    return true;
  }

  // G_GLOBAL_VALUE: base=NoReg, disp=symbol. PC-rel encoding in MC layer.
  if (Def && Def->getOpcode() == TargetOpcode::G_GLOBAL_VALUE) {
    Out.BaseGV = Def->getOperand(1).getGlobal();
    Out.BaseGVOffset = Def->getOperand(1).getOffset();
    Out.Flags = VAXAM::Disp;
    return true;
  }

  // Helper: match "scaled index" producer — either G_SHL(idx, log2scale) or
  // G_MUL(idx, scale). Returns the index vreg on success.
  auto matchScaledIndex = [&](Register R) -> Register {
    if (IndexScaleLog2 == 0) return Register();
    MachineInstr *SD = defOf(R);
    if (!SD) return Register();
    if (SD->getOpcode() == TargetOpcode::G_SHL) {
      MachineInstr *A = defOf(SD->getOperand(2).getReg());
      if (A && A->getOpcode() == TargetOpcode::G_CONSTANT &&
          A->getOperand(1).getCImm()->getZExtValue() == IndexScaleLog2)
        return SD->getOperand(1).getReg();
    } else if (SD->getOpcode() == TargetOpcode::G_MUL) {
      MachineInstr *A = defOf(SD->getOperand(2).getReg());
      if (A && A->getOpcode() == TargetOpcode::G_CONSTANT &&
          A->getOperand(1).getCImm()->getZExtValue() ==
              (1ull << IndexScaleLog2))
        return SD->getOperand(1).getReg();
    }
    return Register();
  };

  // G_PTR_ADD — try indexed mode first (when a scale is supplied),
  // then fall back to base+disp.
  if (Def && Def->getOpcode() == TargetOpcode::G_PTR_ADD) {
    Register LHS = Def->getOperand(1).getReg();
    Register RHS = Def->getOperand(2).getReg();

    // Indexed: G_PTR_ADD(base, G_SHL(idx,k)) or G_PTR_ADD(base, G_MUL(idx,s)).
    for (int Swap = 0; Swap < 2 && IndexScaleLog2 != 0; ++Swap) {
      Register MaybeIdxChain = Swap ? LHS : RHS;
      Register MaybeBaseReg  = Swap ? RHS : LHS;
      Register IdxReg = matchScaledIndex(MaybeIdxChain);
      if (!IdxReg) continue;
      Out.IndexReg = IdxReg;
      // Decompose base: FI, G_GLOBAL_VALUE, G_PTR_ADD(..., const), bare reg.
      MachineInstr *BaseDef = defOf(MaybeBaseReg);
      if (BaseDef && BaseDef->getOpcode() == TargetOpcode::G_FRAME_INDEX) {
        Out.HasBaseFI = true;
        Out.BaseFI = BaseDef->getOperand(1).getIndex();
      } else if (BaseDef &&
                 BaseDef->getOpcode() == TargetOpcode::G_GLOBAL_VALUE) {
        Out.BaseGV = BaseDef->getOperand(1).getGlobal();
        Out.BaseGVOffset = BaseDef->getOperand(1).getOffset();
      } else if (BaseDef &&
                 BaseDef->getOpcode() == TargetOpcode::G_PTR_ADD) {
        Register BLHS = BaseDef->getOperand(1).getReg();
        Register BRHS = BaseDef->getOperand(2).getReg();
        MachineInstr *CDef = defOf(BRHS);
        MachineInstr *FIDef = defOf(BLHS);
        if (CDef && CDef->getOpcode() == TargetOpcode::G_CONSTANT) {
          Out.Disp = CDef->getOperand(1).getCImm()->getSExtValue();
          if (FIDef && FIDef->getOpcode() == TargetOpcode::G_FRAME_INDEX) {
            Out.HasBaseFI = true;
            Out.BaseFI = FIDef->getOperand(1).getIndex();
          } else {
            Out.BaseReg = BLHS;
          }
        } else {
          Out.BaseReg = MaybeBaseReg;
        }
      } else {
        Out.BaseReg = MaybeBaseReg;
      }
      Out.Flags = VAXAM::Disp;
      return true;
    }

    // Base + constant displacement.
    MachineInstr *RHSDef = defOf(RHS);
    if (RHSDef && RHSDef->getOpcode() == TargetOpcode::G_CONSTANT) {
      int64_t C = RHSDef->getOperand(1).getCImm()->getSExtValue();
      MachineInstr *LHSDef = defOf(LHS);
      if (LHSDef && LHSDef->getOpcode() == TargetOpcode::G_FRAME_INDEX) {
        Out.HasBaseFI = true;
        Out.BaseFI = LHSDef->getOperand(1).getIndex();
        Out.Disp = C;
        Out.Flags = VAXAM::Disp;
        return true;
      }
      if (LHSDef && LHSDef->getOpcode() == TargetOpcode::G_GLOBAL_VALUE) {
        Out.BaseGV = LHSDef->getOperand(1).getGlobal();
        Out.BaseGVOffset = LHSDef->getOperand(1).getOffset() + C;
        Out.Flags = VAXAM::Disp;
        return true;
      }
      // Nested: G_PTR_ADD(G_PTR_ADD(base, scaled_idx), const) — fold the
      // outer const onto an indexed addressing mode.
      if (LHSDef && LHSDef->getOpcode() == TargetOpcode::G_PTR_ADD &&
          IndexScaleLog2 != 0) {
        Register ILHS = LHSDef->getOperand(1).getReg();
        Register IRHS = LHSDef->getOperand(2).getReg();
        for (int Swap = 0; Swap < 2; ++Swap) {
          Register MaybeIdx  = Swap ? ILHS : IRHS;
          Register MaybeBase = Swap ? IRHS : ILHS;
          Register IdxReg = matchScaledIndex(MaybeIdx);
          if (!IdxReg) continue;
          Out.IndexReg = IdxReg;
          Out.Disp = C;
          MachineInstr *BD = defOf(MaybeBase);
          if (BD && BD->getOpcode() == TargetOpcode::G_FRAME_INDEX) {
            Out.HasBaseFI = true;
            Out.BaseFI = BD->getOperand(1).getIndex();
          } else if (BD && BD->getOpcode() == TargetOpcode::G_GLOBAL_VALUE) {
            Out.BaseGV = BD->getOperand(1).getGlobal();
            Out.BaseGVOffset = BD->getOperand(1).getOffset();
          } else {
            Out.BaseReg = MaybeBase;
          }
          Out.Flags = VAXAM::Disp;
          return true;
        }
      }
      Out.BaseReg = LHS;
      Out.Disp = C;
      Out.Flags = VAXAM::Disp;
      return true;
    }
  }

  // Bare register: register-deferred mode.
  Out.BaseReg = Ptr;
  Out.Flags = VAXAM::RegDeferred;
  return true;
}

void VAXInstructionSelector::addMemOperands(MachineInstrBuilder &MIB,
                                            const MemAddr &A) const {
  // Base operand.
  if (A.HasBaseFI)
    MIB.addFrameIndex(A.BaseFI);
  else if (A.BaseGV)
    MIB.addReg(0); // base=NoReg when using a global as the displacement
  else
    MIB.addReg(A.BaseReg);

  // Displacement operand (either an integer immediate or a global address).
  if (A.BaseGV) {
    unsigned TF = VAXII::MO_NO_FLAG;
    if (TM.isPositionIndependent() && !A.BaseGV->isDSOLocal())
      TF = VAXII::MO_GOT;
    MIB.addGlobalAddress(A.BaseGV, A.BaseGVOffset, TF);
  } else
    MIB.addImm(A.Disp);

  // Index + flags.
  MIB.addReg(A.IndexReg);
  MIB.addImm(A.Flags);
}

/// Select G_LOAD → MOVB/MOVW/MOVL load with full addressing-mode folding.
bool VAXInstructionSelector::selectLoad(MachineInstr &MI) const {
  Register DstReg = MI.getOperand(0).getReg();
  Register PtrReg = MI.getOperand(1).getReg();
  MachineMemOperand *MMO = *MI.memoperands_begin();

  LLT DstTy = MRI->getType(DstReg);
  unsigned MovOpc;
  const TargetRegisterClass *DstRC;
  unsigned IndexScaleLog2;
  unsigned Size = DstTy.isPointer() ? 32 : DstTy.getSizeInBits();
  switch (Size) {
  case 8:  MovOpc = VAX::MOVBload; DstRC = &VAX::GPRBRegClass; IndexScaleLog2 = 0; break;
  case 16: MovOpc = VAX::MOVWload; DstRC = &VAX::GPRWRegClass; IndexScaleLog2 = 1; break;
  case 32: MovOpc = VAX::MOVL_rm;  DstRC = &VAX::GPRIRegClass; IndexScaleLog2 = 2; break;
  default:
    LLVM_DEBUG(dbgs() << "VAX GISel: unsupported load type " << DstTy << "\n");
    return false;
  }
  RBI.constrainGenericRegister(DstReg, *DstRC, *MRI);

  MemAddr A;
  if (!selectAddr(PtrReg, A, IndexScaleLog2))
    return false;
  if (A.BaseReg)
    RBI.constrainGenericRegister(A.BaseReg, VAX::GPRIRegClass, *MRI);
  if (A.IndexReg)
    RBI.constrainGenericRegister(A.IndexReg, VAX::GPRIRegClass, *MRI);

  auto MIB = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
                     TII.get(MovOpc), DstReg);
  addMemOperands(MIB, A);
  MIB.addMemOperand(MMO);
  MI.eraseFromParent();
  return true;
}

/// Select G_STORE → MOVB/MOVW/MOVL store with full addressing-mode folding.
bool VAXInstructionSelector::selectStore(MachineInstr &MI) const {
  Register ValReg = MI.getOperand(0).getReg();
  Register PtrReg = MI.getOperand(1).getReg();
  MachineMemOperand *MMO = *MI.memoperands_begin();

  LLT ValTy = MRI->getType(ValReg);
  unsigned MovOpc;
  const TargetRegisterClass *ValRC;
  unsigned IndexScaleLog2;
  unsigned Size = ValTy.isPointer() ? 32 : ValTy.getSizeInBits();
  switch (Size) {
  case 8:  MovOpc = VAX::MOVBstore; ValRC = &VAX::GPRBRegClass; IndexScaleLog2 = 0; break;
  case 16: MovOpc = VAX::MOVWstore; ValRC = &VAX::GPRWRegClass; IndexScaleLog2 = 1; break;
  case 32: MovOpc = VAX::MOVL_mr;   ValRC = &VAX::GPRIRegClass; IndexScaleLog2 = 2; break;
  default:
    LLVM_DEBUG(dbgs() << "VAX GISel: unsupported store type " << ValTy << "\n");
    return false;
  }
  RBI.constrainGenericRegister(ValReg, *ValRC, *MRI);

  MemAddr A;
  if (!selectAddr(PtrReg, A, IndexScaleLog2))
    return false;
  if (A.BaseReg)
    RBI.constrainGenericRegister(A.BaseReg, VAX::GPRIRegClass, *MRI);
  if (A.IndexReg)
    RBI.constrainGenericRegister(A.IndexReg, VAX::GPRIRegClass, *MRI);

  // MOVx_mr operand order: value, base, disp, index, flags.
  auto MIB = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII.get(MovOpc))
                 .addReg(ValReg);
  addMemOperands(MIB, A);
  MIB.addMemOperand(MMO);
  MI.eraseFromParent();
  return true;
}

/// Select G_CONSTANT → MOVB/MOVW/MOVL immediate (sized).
bool VAXInstructionSelector::selectConstant(MachineInstr &MI) const {
  Register DstReg = MI.getOperand(0).getReg();
  int64_t Val = MI.getOperand(1).getCImm()->getSExtValue();

  LLT DstTy = MRI->getType(DstReg);
  unsigned MovOpc;
  const TargetRegisterClass *RC;
  switch (DstTy.getSizeInBits()) {
  case 8:  MovOpc = VAX::MOVBri; RC = &VAX::GPRBRegClass; break;
  case 16: MovOpc = VAX::MOVWri; RC = &VAX::GPRWRegClass; break;
  case 32: MovOpc = VAX::MOVL_ri; RC = &VAX::GPRIRegClass; break;
  default: return false;
  }
  RBI.constrainGenericRegister(DstReg, *RC, *MRI);
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
          TII.get(MovOpc), DstReg)
      .addImm(Val);
  MI.eraseFromParent();
  return true;
}

/// Select G_TRUNC and G_ANYEXT.
///
/// On VAX, GPRB/GPRW/GPRI share the same physical registers — narrowing or
/// any-extending is a pure register-class change with no runtime cost. We
/// lower both to COPYs; the final-pass copy coalescer in InstructionSelect
/// collapses them away.
bool VAXInstructionSelector::selectTruncOrAnyExt(MachineInstr &MI) const {
  Register DstReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(1).getReg();
  LLT DstTy = MRI->getType(DstReg);
  LLT SrcTy = MRI->getType(SrcReg);

  const TargetRegisterClass *DstRC = getRegClassForType(DstTy);
  const TargetRegisterClass *SrcRC = getRegClassForType(SrcTy);
  if (!DstRC || !SrcRC)
    return false;

  RBI.constrainGenericRegister(DstReg, *DstRC, *MRI);
  RBI.constrainGenericRegister(SrcReg, *SrcRC, *MRI);

  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII.get(TargetOpcode::COPY),
          DstReg)
      .addReg(SrcReg);
  MI.eraseFromParent();
  return true;
}

/// Select G_FRAME_INDEX — usually folded into load/store.
/// If standalone (address-taken), materialize via the LEA_FI pseudo which
/// the AsmPrinter expands into "moval disp(%fp), $dst".
bool VAXInstructionSelector::selectFrameIndex(MachineInstr &MI) const {
  Register DstReg = MI.getOperand(0).getReg();

  if (MRI->use_nodbg_empty(DstReg)) {
    MI.eraseFromParent();
    return true;
  }

  RBI.constrainGenericRegister(DstReg, VAX::GPRIRegClass, *MRI);
  int FI = MI.getOperand(1).getIndex();
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
          TII.get(VAX::LEA_FI), DstReg)
      .addFrameIndex(FI)
      .addImm(0)
      .addReg(0)
      .addImm(VAXAM::Disp);
  MI.eraseFromParent();
  return true;
}

/// Select G_GLOBAL_VALUE — usually folded into load/store.
/// If standalone (address-of), materialize via MOVAL_ga.
bool VAXInstructionSelector::selectGlobalValue(MachineInstr &MI) const {
  Register DstReg = MI.getOperand(0).getReg();

  if (MRI->use_nodbg_empty(DstReg)) {
    MI.eraseFromParent();
    return true;
  }

  RBI.constrainGenericRegister(DstReg, VAX::GPRIRegClass, *MRI);
  const GlobalValue *GV = MI.getOperand(1).getGlobal();
  int64_t Offset = MI.getOperand(1).getOffset();
  unsigned TF = VAXII::MO_NO_FLAG;
  if (TM.isPositionIndependent() && !GV->isDSOLocal())
    TF = VAXII::MO_GOT;
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII.get(VAX::MOVAL_ga), DstReg)
      .addGlobalAddress(GV, Offset, TF);
  MI.eraseFromParent();
  return true;
}

/// Select G_PTR_ADD — usually folded into a load/store.
/// If standalone, emit ADDL3 (reg+imm or reg+reg).
bool VAXInstructionSelector::selectPtrAdd(MachineInstr &MI) const {
  Register DstReg = MI.getOperand(0).getReg();

  if (MRI->use_nodbg_empty(DstReg)) {
    MI.eraseFromParent();
    return true;
  }

  Register LHS = MI.getOperand(1).getReg();
  Register RHS = MI.getOperand(2).getReg();
  RBI.constrainGenericRegister(DstReg, VAX::GPRIRegClass, *MRI);
  RBI.constrainGenericRegister(LHS, VAX::GPRIRegClass, *MRI);

  MachineInstr *RHSDef = MRI->getVRegDef(RHS);
  if (RHSDef && RHSDef->getOpcode() == TargetOpcode::G_CONSTANT) {
    int64_t C = RHSDef->getOperand(1).getCImm()->getSExtValue();
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
            TII.get(VAX::ADDL3_ri_cc), DstReg)
        .addImm(C)
        .addReg(LHS);
    MI.eraseFromParent();
    return true;
  }

  RBI.constrainGenericRegister(RHS, VAX::GPRIRegClass, *MRI);
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
          TII.get(VAX::ADDL3_rr_cc), DstReg)
      .addReg(LHS)
      .addReg(RHS);
  MI.eraseFromParent();
  return true;
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
