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
  bool selectAnd(MachineInstr &MI) const;
  bool selectShr(MachineInstr &MI, bool IsArithmetic) const;
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
  bool selectSelect(MachineInstr &MI) const;
  bool selectTruncOrAnyExt(MachineInstr &MI) const;
  bool selectAddOSubO(MachineInstr &MI, bool IsSub) const;
  bool selectAddESubE(MachineInstr &MI, bool IsSub) const;
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
  case TargetOpcode::G_AND:
    return selectAnd(MI);
  case TargetOpcode::G_ASHR:
    return selectShr(MI, /*IsArithmetic=*/true);
  case TargetOpcode::G_LSHR:
    return selectShr(MI, /*IsArithmetic=*/false);
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
  case TargetOpcode::G_SELECT:
    return selectSelect(MI);
  case TargetOpcode::G_TRUNC:
  case TargetOpcode::G_ANYEXT:
    return selectTruncOrAnyExt(MI);
  case TargetOpcode::G_UADDO:
    return selectAddOSubO(MI, /*IsSub=*/false);
  case TargetOpcode::G_USUBO:
    return selectAddOSubO(MI, /*IsSub=*/true);
  case TargetOpcode::G_UADDE:
    return selectAddESubE(MI, /*IsSub=*/false);
  case TargetOpcode::G_USUBE:
    return selectAddESubE(MI, /*IsSub=*/true);
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
  if (Ty.getSizeInBits() == 8 || Ty.getSizeInBits() == 1)
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

/// Select G_UADDO / G_USUBO → ADDL3_rr_cc / SUBL3_rr_cc.
///
/// G_UADDO %dst, %carry_out = G_UADDO %a, %b
/// G_USUBO %dst, %borrow_out = G_USUBO %a, %b
///
/// Becomes:
///   %dst = ADDL3_rr_cc %a, %b      (defs PSW)
///   %carry_out = IMPLICIT_DEF      (dead — carry flows through PSW to ADWC)
///
/// SUBL3 is "subl3 $s, $m, $dst" → dst = m - s; we pass (src2, src1) to match
/// the "a - b" sense of G_USUBO(a, b).
bool VAXInstructionSelector::selectAddOSubO(MachineInstr &MI,
                                            bool IsSub) const {
  Register DstReg = MI.getOperand(0).getReg();
  Register CarryOutReg = MI.getOperand(1).getReg();
  Register Src1Reg = MI.getOperand(2).getReg();
  Register Src2Reg = MI.getOperand(3).getReg();

  LLT DstTy = MRI->getType(DstReg);
  if (DstTy.getSizeInBits() != 32)
    return false;

  RBI.constrainGenericRegister(DstReg, VAX::GPRIRegClass, *MRI);
  RBI.constrainGenericRegister(Src1Reg, VAX::GPRIRegClass, *MRI);
  RBI.constrainGenericRegister(Src2Reg, VAX::GPRIRegClass, *MRI);

  unsigned Opc;
  if (IsSub) {
    // SUBL3_rr_cc: (outs $dst), (ins $s, $m); dst = m - s.
    // For G_USUBO(%a, %b) we want dst = a - b, so s=b, m=a → operands (b, a).
    Opc = VAX::SUBL3_rr_cc;
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII.get(Opc), DstReg)
        .addReg(Src2Reg)
        .addReg(Src1Reg);
  } else {
    Opc = VAX::ADDL3_rr_cc;
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII.get(Opc), DstReg)
        .addReg(Src1Reg)
        .addReg(Src2Reg);
  }

  // Carry-out vreg is a bookkeeping artifact. Materialize it as dead.
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
          TII.get(TargetOpcode::IMPLICIT_DEF), CarryOutReg);
  RBI.constrainGenericRegister(CarryOutReg, VAX::GPRBRegClass, *MRI);

  MI.eraseFromParent();
  return true;
}

/// Select G_UADDE / G_USUBE → ADWC_rr / SBWC_rr.
///
/// G_UADDE %dst, %carry_out = G_UADDE %a, %b, %carry_in
/// G_USUBE %dst, %borrow_out = G_USUBE %a, %b, %borrow_in
///
/// Becomes:
///   %dst = ADWC_rr %b, %a          (uses PSW, constrained $dstin = $dst)
///   %carry_out = IMPLICIT_DEF      (dead)
///
/// The s1 %carry_in operand is ignored — carry flows through PSW from the
/// preceding ADDL3_rr_cc. Defs/Uses = [PSW] on both instructions prevent
/// scheduling reorder.
///
/// ADWC_rr: "adwc $src, $dst" → dst = dst + src + C  (dstin tied to dst)
/// For G_UADDE(%a, %b, %c): dst = a + b + C. We pick src=b, dstin=a.
///
/// SBWC_rr: "sbwc $src, $dst" → dst = dst - src - C
/// For G_USUBE(%a, %b, %c): dst = a - b - C. We pick src=b, dstin=a.
bool VAXInstructionSelector::selectAddESubE(MachineInstr &MI,
                                            bool IsSub) const {
  Register DstReg = MI.getOperand(0).getReg();
  Register CarryOutReg = MI.getOperand(1).getReg();
  Register Src1Reg = MI.getOperand(2).getReg();
  Register Src2Reg = MI.getOperand(3).getReg();
  // Operand 4 is the s1 carry-in vreg; unused here (flows via PSW).

  LLT DstTy = MRI->getType(DstReg);
  if (DstTy.getSizeInBits() != 32)
    return false;

  RBI.constrainGenericRegister(DstReg, VAX::GPRIRegClass, *MRI);
  RBI.constrainGenericRegister(Src1Reg, VAX::GPRIRegClass, *MRI);
  RBI.constrainGenericRegister(Src2Reg, VAX::GPRIRegClass, *MRI);

  unsigned Opc = IsSub ? VAX::SBWC_rr : VAX::ADWC_rr;
  // (outs $dst), (ins $src, $dstin); Constraints: $dstin = $dst.
  // src = Src2Reg (the "b" in a +/- b +/- C), dstin = Src1Reg (the "a").
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII.get(Opc), DstReg)
      .addReg(Src2Reg)
      .addReg(Src1Reg);

  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
          TII.get(TargetOpcode::IMPLICIT_DEF), CarryOutReg);
  RBI.constrainGenericRegister(CarryOutReg, VAX::GPRBRegClass, *MRI);

  MI.eraseFromParent();
  return true;
}

/// Select G_AND → MCOM + BICL3. VAX has no direct AND; we use BIC (bit clear)
/// after complementing one operand: a & b = BICL(~b, a).
bool VAXInstructionSelector::selectAnd(MachineInstr &MI) const {
  Register DstReg = MI.getOperand(0).getReg();
  Register Src1Reg = MI.getOperand(1).getReg();
  Register Src2Reg = MI.getOperand(2).getReg();

  LLT DstTy = MRI->getType(DstReg);
  unsigned McomOpc, BicRR, BicRI;
  const TargetRegisterClass *RC;
  switch (DstTy.getSizeInBits()) {
  case 8:  McomOpc = VAX::MCOMB;  BicRR = VAX::BICB3_rr; BicRI = VAX::BICB3_ri; RC = &VAX::GPRBRegClass; break;
  case 16: McomOpc = VAX::MCOMW;  BicRR = VAX::BICW3_rr; BicRI = VAX::BICW3_ri; RC = &VAX::GPRWRegClass; break;
  case 32: McomOpc = VAX::MCOML;  BicRR = VAX::BICL3_rr; BicRI = VAX::BICL3_ri; RC = &VAX::GPRIRegClass; break;
  default: return false;
  }

  RBI.constrainGenericRegister(DstReg, *RC, *MRI);

  // If either source is a constant, fold `a & K` to `BICL3 ~K, a, dst` in one
  // instruction. Check Src2 first (LLVM's commutative canonicalization puts
  // constants on the RHS), then Src1.
  auto TryFoldConst = [&](Register RegC, Register RegA) -> bool {
    auto Cst = getIConstantVRegValWithLookThrough(RegC, *MRI);
    if (!Cst)
      return false;
    RBI.constrainGenericRegister(RegA, *RC, *MRI);
    // For s32 AND with 0xFF or 0xFFFF, emit MOVZBL/MOVZWL via a subreg
    // extract — one instruction instead of BICL3 $-256 (two, because the
    // sign-extended long immediate can't share a 2-op form).
    if (DstTy.getSizeInBits() == 32) {
      uint64_t K = Cst->Value.getZExtValue();
      if (K == 0xFFu || K == 0xFFFFu) {
        unsigned SubIdx = (K == 0xFFu) ? VAX::sub_8lo : VAX::sub_16lo;
        const TargetRegisterClass *SubRC =
            (K == 0xFFu) ? &VAX::GPRBRegClass : &VAX::GPRWRegClass;
        unsigned MovzOpc = (K == 0xFFu) ? VAX::MOVZBL_rr : VAX::MOVZWL_rr;
        Register SubReg = MRI->createVirtualRegister(SubRC);
        BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
                TII.get(TargetOpcode::COPY), SubReg)
            .addReg(RegA, RegState::NoFlags, SubIdx);
        BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII.get(MovzOpc),
                DstReg)
            .addReg(SubReg);
        MI.eraseFromParent();
        return true;
      }
    }
    int64_t Mask = ~Cst->Value.getSExtValue();
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII.get(BicRI), DstReg)
        .addImm(Mask)
        .addReg(RegA);
    MI.eraseFromParent();
    return true;
  };
  if (TryFoldConst(Src2Reg, Src1Reg) || TryFoldConst(Src1Reg, Src2Reg))
    return true;

  RBI.constrainGenericRegister(Src1Reg, *RC, *MRI);
  RBI.constrainGenericRegister(Src2Reg, *RC, *MRI);

  // General case: `tmp = ~X; Dst = BIC(tmp, Y)`  (= Y & ~~X = X & Y).
  //
  // For peephole friendliness we want the load-fed operand to sit on the
  // BIC3 source side AND be immediately adjacent to BIC3, so
  // MOVL_rm + BICL3_rr folds to BICL3_rm. The MCOM of the other side is
  // inserted before the load so that it doesn't split the load/BIC3 pair.
  //
  // Detect "load side" as the operand whose def is a load-bearing machine
  // instruction immediately preceding the G_AND MI (where the peephole
  // would see MOVL_rm + BIC3 as a fold candidate).
  MachineBasicBlock &MBB = *MI.getParent();
  auto isAdjacentLoadDef = [&](Register R) -> MachineInstr * {
    MachineInstr *Def = MRI->getVRegDef(R);
    if (!Def || Def->getParent() != &MBB || !Def->mayLoad())
      return nullptr;
    auto It = MachineBasicBlock::iterator(Def);
    auto MIIt = MachineBasicBlock::iterator(&MI);
    // Def must be the immediately preceding non-debug instr.
    if (std::next(It) == MIIt)
      return Def;
    return nullptr;
  };
  MachineInstr *LoadDef = isAdjacentLoadDef(Src1Reg);
  Register BicSrc = Src1Reg;
  Register McomSrc = Src2Reg;
  if (!LoadDef) {
    LoadDef = isAdjacentLoadDef(Src2Reg);
    if (LoadDef) {
      BicSrc = Src2Reg;
      McomSrc = Src1Reg;
    }
  }

  Register TmpReg = MRI->createVirtualRegister(RC);
  MachineBasicBlock::iterator InsertPt =
      LoadDef ? MachineBasicBlock::iterator(LoadDef)
              : MachineBasicBlock::iterator(&MI);
  BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII.get(McomOpc), TmpReg)
      .addReg(McomSrc);
  BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(BicRR), DstReg)
      .addReg(TmpReg)
      .addReg(BicSrc);
  MI.eraseFromParent();
  return true;
}

// VAX has only ASHL (arithmetic shift long). Right shifts are synthesized:
//   SRA = ASHL with negated count (sign-propagating on VAX).
//   SRL = EXTZV (zero-extract bitfield from pos=cnt, size=32-cnt).
// Legalizer forces both operands to s32, so no byte/word dispatch is needed.
bool VAXInstructionSelector::selectShr(MachineInstr &MI,
                                       bool IsArithmetic) const {
  Register DstReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(1).getReg();
  Register CntReg = MI.getOperand(2).getReg();

  const TargetRegisterClass *RC = &VAX::GPRIRegClass;
  RBI.constrainGenericRegister(DstReg, *RC, *MRI);
  RBI.constrainGenericRegister(SrcReg, *RC, *MRI);

  auto CntCst = getIConstantVRegValWithLookThrough(CntReg, *MRI);

  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();

  if (IsArithmetic) {
    if (CntCst) {
      int64_t N = CntCst->Value.getSExtValue() & 31;
      if (N == 0) {
        BuildMI(MBB, MI, DL, TII.get(TargetOpcode::COPY), DstReg)
            .addReg(SrcReg);
      } else {
        // ASHL with negated imm count → arithmetic right shift.
        BuildMI(MBB, MI, DL, TII.get(VAX::ASHL_ir_sra), DstReg)
            .addImm(-N)
            .addReg(SrcReg);
      }
      MI.eraseFromParent();
      return true;
    }
    RBI.constrainGenericRegister(CntReg, *RC, *MRI);
    Register NegCnt = MRI->createVirtualRegister(RC);
    BuildMI(MBB, MI, DL, TII.get(VAX::MNEGL), NegCnt).addReg(CntReg);
    BuildMI(MBB, MI, DL, TII.get(VAX::ASHL_rr_sra), DstReg)
        .addReg(NegCnt)
        .addReg(SrcReg);
    MI.eraseFromParent();
    return true;
  }

  // Logical right shift (SRL) via EXTZV.
  if (CntCst) {
    int64_t N = CntCst->Value.getZExtValue() & 31;
    if (N == 0) {
      BuildMI(MBB, MI, DL, TII.get(TargetOpcode::COPY), DstReg).addReg(SrcReg);
    } else {
      BuildMI(MBB, MI, DL, TII.get(VAX::EXTZV_iir), DstReg)
          .addImm(N)
          .addImm(32 - N)
          .addReg(SrcReg);
    }
    MI.eraseFromParent();
    return true;
  }
  RBI.constrainGenericRegister(CntReg, *RC, *MRI);
  // size = 32 - cnt
  Register SizeReg = MRI->createVirtualRegister(RC);
  BuildMI(MBB, MI, DL, TII.get(VAX::SUBL3_ir), SizeReg)
      .addReg(CntReg)
      .addImm(32);
  BuildMI(MBB, MI, DL, TII.get(VAX::EXTZV_rrr), DstReg)
      .addReg(CntReg)
      .addReg(SizeReg)
      .addReg(SrcReg);
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

bool VAXInstructionSelector::selectSelect(MachineInstr &MI) const {
  // G_SELECT %dst, %cond(s1), %tval, %fval
  // Lower via SELECT_CC_Pseudo (diamond expander). Fold an adjacent G_ICMP
  // condition if single-use; otherwise TSTL the bool and branch on NEQ.
  Register DstReg = MI.getOperand(0).getReg();
  Register CondReg = MI.getOperand(1).getReg();
  Register TVal = MI.getOperand(2).getReg();
  Register FVal = MI.getOperand(3).getReg();

  LLT DstTy = MRI->getType(DstReg);
  const TargetRegisterClass *DstRC = nullptr;
  if (DstTy.isPointer()) {
    DstRC = &VAX::GPRIRegClass;
  } else {
    switch (DstTy.getSizeInBits()) {
    case 8:  DstRC = &VAX::GPRBRegClass; break;
    case 16: DstRC = &VAX::GPRWRegClass; break;
    case 32: DstRC = &VAX::GPRIRegClass; break;
    default: return false;
    }
  }

  MachineIRBuilder B(MI);
  unsigned CC = ~0U;

  MachineInstr *CondDef = MRI->getVRegDef(CondReg);
  if (CondDef && CondDef->getOpcode() == TargetOpcode::G_ICMP &&
      MRI->hasOneNonDBGUse(CondReg)) {
    CmpInst::Predicate Pred =
        static_cast<CmpInst::Predicate>(CondDef->getOperand(1).getPredicate());
    CC = getVAXCCForICmp(Pred);
    if (CC == ~0U)
      return false;
    Register LHS = CondDef->getOperand(2).getReg();
    Register RHS = CondDef->getOperand(3).getReg();
    LLT LHSTy = MRI->getType(LHS);
    unsigned CmpOpc;
    const TargetRegisterClass *OpRC;
    if (LHSTy.getSizeInBits() == 8) {
      CmpOpc = VAX::CMPB_rr; OpRC = &VAX::GPRBRegClass;
    } else if (LHSTy.getSizeInBits() == 16) {
      CmpOpc = VAX::CMPW_rr; OpRC = &VAX::GPRWRegClass;
    } else if (LHSTy.getSizeInBits() == 32) {
      CmpOpc = VAX::CMPL_rr; OpRC = &VAX::GPRIRegClass;
    } else {
      return false;
    }
    B.buildInstr(CmpOpc).addUse(LHS).addUse(RHS);
    RBI.constrainGenericRegister(LHS, *OpRC, *MRI);
    RBI.constrainGenericRegister(RHS, *OpRC, *MRI);
    CondDef->eraseFromParent();
  } else {
    // Bool materialized some other way: compare against zero; NEQ = true.
    LLT CondTy = MRI->getType(CondReg);
    unsigned TstOpc;
    const TargetRegisterClass *CondRC;
    unsigned CondBits = CondTy.getSizeInBits();
    if (CondBits == 1 || CondBits == 8) {
      TstOpc = VAX::TSTB; CondRC = &VAX::GPRBRegClass;
    } else if (CondBits == 16) {
      TstOpc = VAX::TSTW; CondRC = &VAX::GPRWRegClass;
    } else {
      TstOpc = VAX::TSTL; CondRC = &VAX::GPRIRegClass;
    }
    B.buildInstr(TstOpc).addUse(CondReg);
    RBI.constrainGenericRegister(CondReg, *CondRC, *MRI);
    CC = getVAXCCForICmp(CmpInst::ICMP_NE);
  }

  B.buildInstr(VAX::SELECT_CC_Pseudo, {DstReg}, {TVal, FVal}).addImm(CC);
  RBI.constrainGenericRegister(DstReg, *DstRC, *MRI);
  RBI.constrainGenericRegister(TVal, *DstRC, *MRI);
  RBI.constrainGenericRegister(FVal, *DstRC, *MRI);

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
