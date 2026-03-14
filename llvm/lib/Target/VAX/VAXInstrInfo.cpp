//===-- VAXInstrInfo.cpp - VAX Instruction Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAX.h"
#include "VAXInstrInfo.h"
#include "VAXSubtarget.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "VAXGenInstrInfo.inc"

VAXInstrInfo::VAXInstrInfo(const VAXSubtarget &STI)
    : VAXGenInstrInfo(STI, RI, VAX::ADJCALLSTACKDOWN, VAX::ADJCALLSTACKUP), RI() {}

void VAXInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI,
                                const DebugLoc &DL, Register DstReg,
                                Register SrcReg, bool KillSrc,
                                bool RenamableDst, bool RenamableSrc) const {
  if (VAX::QPRRegClass.contains(DstReg, SrcReg)) {
    // 64-bit register pair copy via MOVQ (not MOVD — MOVD validates float
    // format and faults on reserved operand patterns in integer data).
    BuildMI(MBB, MI, DL, get(VAX::MOVQ_rr), DstReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  unsigned Opc;
  if (VAX::GPRBRegClass.contains(DstReg, SrcReg))
    Opc = VAX::MOVBrr;
  else if (VAX::GPRWRegClass.contains(DstReg, SrcReg))
    Opc = VAX::MOVWrr;
  else
    Opc = VAX::MOVL_rr;
  BuildMI(MBB, MI, DL, get(Opc), DstReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

/// Return the register and frame index of a load from a stack slot.
/// Recognizes the MOVL_rm / MOVQ_rm / MOVBload / MOVWload patterns
/// emitted by loadRegFromStackSlot().
Register VAXInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                           int &FrameIndex) const {
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  default:
    return Register();
  case VAX::MOVL_rm:
  case VAX::MOVQ_rm:
  case VAX::MOVBload:
  case VAX::MOVWload:
    break;
  }
  // Operand layout: $dst, $base, $disp, $index, $flags
  // A stack load has: FrameIndex as base, disp=0, index=$noreg, flags=Disp.
  const MachineOperand &Base = MI.getOperand(1);
  const MachineOperand &Disp = MI.getOperand(2);
  const MachineOperand &Index = MI.getOperand(3);
  if (Base.isFI() && Disp.isImm() && Disp.getImm() == 0 &&
      Index.isReg() && Index.getReg() == 0) {
    FrameIndex = Base.getIndex();
    return MI.getOperand(0).getReg();
  }
  return Register();
}

/// Return the register and frame index of a store to a stack slot.
Register VAXInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                          int &FrameIndex) const {
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  default:
    return Register();
  case VAX::MOVL_mr:
  case VAX::MOVQ_mr:
  case VAX::MOVBstore:
  case VAX::MOVWstore:
    break;
  }
  // Operand layout: $src, $base, $disp, $index, $flags
  // A stack store has: FrameIndex as base, disp=0, index=$noreg, flags=Disp.
  const MachineOperand &Base = MI.getOperand(1);
  const MachineOperand &Disp = MI.getOperand(2);
  const MachineOperand &Index = MI.getOperand(3);
  if (Base.isFI() && Disp.isImm() && Disp.getImm() == 0 &&
      Index.isReg() && Index.getReg() == 0) {
    FrameIndex = Base.getIndex();
    return MI.getOperand(0).getReg();
  }
  return Register();
}

/// Helper: append a stack-slot memory operand (FrameIndex, disp=0, no index,
/// Disp mode) to a MachineInstrBuilder.
static void addFrameMemOps(MachineInstrBuilder &MIB, int FrameIndex) {
  MIB.addFrameIndex(FrameIndex).addImm(0).addReg(0).addImm(VAXAM::Disp);
}

MachineInstr *VAXInstrInfo::foldMemoryOperandImpl(
    MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
    MachineBasicBlock::iterator InsertPt, int FrameIndex, LiveIntervals *LIS,
    VirtRegMap *VRM) const {
  // We only fold a single operand at a time.
  if (Ops.size() != 1)
    return nullptr;

  unsigned OpIdx = Ops[0];
  unsigned Opc = MI.getOpcode();

  // Don't try to fold COPYs — the base class handles those.
  if (MI.isCopy())
    return nullptr;

  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();

  // --- Fold loads: register source → memory source ---
  // The RA wants to replace a register use with a load from FrameIndex.

  // PUSHL_r $reg → PUSHL_m (stack-slot)
  if (Opc == VAX::PUSHL_r && OpIdx == 0) {
    auto MIB = BuildMI(MBB, InsertPt, DL, get(VAX::PUSHL_m));
    addFrameMemOps(MIB, FrameIndex);
    MIB.addReg(VAX::SP, RegState::ImplicitDefine);
    return MIB;
  }

  // CMPL_rr: fold operand 0 ($a) → CMPL_rm
  if (Opc == VAX::CMPL_rr && OpIdx == 0) {
    Register RhsReg = MI.getOperand(1).getReg();
    auto MIB = BuildMI(MBB, InsertPt, DL, get(VAX::CMPL_rm));
    addFrameMemOps(MIB, FrameIndex);
    MIB.addReg(RhsReg);
    return MIB;
  }

  // CMP_BRANCH_rr: fold operand 0 ($lhs) → CMP_BRANCH_rm
  if (Opc == VAX::CMP_BRANCH_rr && OpIdx == 0) {
    Register RhsReg = MI.getOperand(1).getReg();
    int64_t CC = MI.getOperand(2).getImm();
    MachineBasicBlock *Target = MI.getOperand(3).getMBB();
    auto MIB = BuildMI(MBB, InsertPt, DL, get(VAX::CMP_BRANCH_rm));
    addFrameMemOps(MIB, FrameIndex);
    MIB.addReg(RhsReg).addImm(CC).addMBB(Target);
    return MIB;
  }

  // CMP_BRANCH_ri: fold operand 0 ($lhs) → CMP_BRANCH_mi
  if (Opc == VAX::CMP_BRANCH_ri && OpIdx == 0) {
    int64_t RhsImm = MI.getOperand(1).getImm();
    int64_t CC = MI.getOperand(2).getImm();
    MachineBasicBlock *Target = MI.getOperand(3).getMBB();
    auto MIB = BuildMI(MBB, InsertPt, DL, get(VAX::CMP_BRANCH_mi));
    addFrameMemOps(MIB, FrameIndex);
    MIB.addImm(RhsImm).addImm(CC).addMBB(Target);
    return MIB;
  }

  // TST_BRANCH: fold operand 0 ($src) → TST_BRANCH_m
  if (Opc == VAX::TST_BRANCH && OpIdx == 0) {
    int64_t CC = MI.getOperand(1).getImm();
    MachineBasicBlock *Target = MI.getOperand(2).getMBB();
    auto MIB = BuildMI(MBB, InsertPt, DL, get(VAX::TST_BRANCH_m));
    addFrameMemOps(MIB, FrameIndex);
    MIB.addImm(CC).addMBB(Target);
    return MIB;
  }

  // --- ALU 3-operand: fold src1 (operand 1) into memory ---
  // ALU3_rr: [0]=dst, [1]=src1, [2]=src2
  // ALU3_rm: [0]=dst, [1..4]=VAXMemOp(src1), [5]=src2
  struct Alu3Fold {
    unsigned FromOpc, ToOpc;
    bool Commutative;
  };
  static const Alu3Fold Alu3Folds[] = {
      {VAX::ADDL3_rr, VAX::ADDL3_rm, true},
      {VAX::SUBL3_rr, VAX::SUBL3_rm, false},
  };
  for (const auto &F : Alu3Folds) {
    if (Opc != F.FromOpc)
      continue;
    Register Dst = MI.getOperand(0).getReg();
    // Fold operand 1 (src1) directly.
    if (OpIdx == 1) {
      Register Src2 = MI.getOperand(2).getReg();
      auto MIB = BuildMI(MBB, InsertPt, DL, get(F.ToOpc), Dst);
      addFrameMemOps(MIB, FrameIndex);
      MIB.addReg(Src2);
      return MIB;
    }
    // Fold operand 2 (src2) for commutative ops by swapping.
    if (OpIdx == 2 && F.Commutative) {
      Register Src1 = MI.getOperand(1).getReg();
      auto MIB = BuildMI(MBB, InsertPt, DL, get(F.ToOpc), Dst);
      addFrameMemOps(MIB, FrameIndex);
      MIB.addReg(Src1);
      return MIB;
    }
    return nullptr;
  }

  // --- ALU 2-operand: fold src (operand 1) into memory ---
  // ALU2_rr: [0]=dst(def,tied), [1]=src, [2]=src2(tied to dst)
  // ALU2_rm: [0]=dst(def,tied), [1..4]=VAXMemOp(src), [5]=src2(tied to dst)
  // Only fold operand 1 (the non-tied source).
  struct Alu2Fold {
    unsigned FromOpc, ToOpc;
  };
  static const Alu2Fold Alu2Folds[] = {
      {VAX::ADDL2_rr, VAX::ADDL2_rm}, {VAX::SUBL2_rr, VAX::SUBL2_rm},
      {VAX::MULL2_rr, VAX::MULL2_rm}, {VAX::DIVL2_rr, VAX::DIVL2_rm},
      {VAX::BISL2_rr, VAX::BISL2_rm}, {VAX::BICL2_rr, VAX::BICL2_rm},
      {VAX::XORL2_rr, VAX::XORL2_rm},
  };
  for (const auto &F : Alu2Folds) {
    if (Opc != F.FromOpc)
      continue;
    if (OpIdx != 1)
      return nullptr;
    Register DstReg = MI.getOperand(0).getReg();
    auto MIB = BuildMI(MBB, InsertPt, DL, get(F.ToOpc));
    MIB.addDef(DstReg);
    addFrameMemOps(MIB, FrameIndex);
    MIB.addReg(DstReg); // tied src2
    return MIB;
  }

  return nullptr;
}

void VAXInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MI,
                                        Register SrcReg, bool isKill,
                                        int FrameIndex,
                                        const TargetRegisterClass *RC,
                                        Register VReg,
                                        MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  unsigned Opc;
  if (RC == &VAX::QPRRegClass)
    Opc = VAX::MOVQ_mr;
  else if (RC == &VAX::GPRBRegClass)
    Opc = VAX::MOVBstore;
  else if (RC == &VAX::GPRWRegClass)
    Opc = VAX::MOVWstore;
  else
    Opc = VAX::MOVL_mr;
  BuildMI(MBB, MI, DL, get(Opc))
      .addReg(SrcReg, getKillRegState(isKill))
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addReg(0) // index (none)
      .addImm(VAXAM::Disp); // flags
}

void VAXInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         Register DstReg, int FrameIndex,
                                         const TargetRegisterClass *RC,
                                         Register VReg, unsigned SubReg,
                                         MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  unsigned Opc;
  if (RC == &VAX::QPRRegClass)
    Opc = VAX::MOVQ_rm;
  else if (RC == &VAX::GPRBRegClass)
    Opc = VAX::MOVBload;
  else if (RC == &VAX::GPRWRegClass)
    Opc = VAX::MOVWload;
  else
    Opc = VAX::MOVL_rm;
  BuildMI(MBB, MI, DL, get(Opc), DstReg)
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addReg(0) // index (none)
      .addImm(VAXAM::Disp); // flags
}

static bool isCondBranch(unsigned Opc) {
  switch (Opc) {
  case VAX::BEQL: case VAX::BNEQ:
  case VAX::BGTR: case VAX::BGEQ: case VAX::BLSS: case VAX::BLEQ:
  case VAX::BGTRU: case VAX::BGEQU: case VAX::BLSSU: case VAX::BLEQU:
    return true;
  default:
    return false;
  }
}

static bool isUncondBranch(unsigned Opc) {
  return Opc == VAX::BRW || Opc == VAX::BRB;
}

static unsigned getOppositeBranch(unsigned Opc) {
  switch (Opc) {
  case VAX::BEQL:  return VAX::BNEQ;
  case VAX::BNEQ:  return VAX::BEQL;
  case VAX::BGTR:  return VAX::BLEQ;
  case VAX::BLEQ:  return VAX::BGTR;
  case VAX::BGEQ:  return VAX::BLSS;
  case VAX::BLSS:  return VAX::BGEQ;
  case VAX::BGTRU: return VAX::BLEQU;
  case VAX::BLEQU: return VAX::BGTRU;
  case VAX::BGEQU: return VAX::BLSSU;
  case VAX::BLSSU: return VAX::BGEQU;
  default: llvm_unreachable("Unknown branch opcode");
  }
}

bool VAXInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                  MachineBasicBlock *&TBB,
                                  MachineBasicBlock *&FBB,
                                  SmallVectorImpl<MachineOperand> &Cond,
                                  bool AllowModify) const {
  // Start from the bottom and work up, examining terminator instructions.
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;

    // Stop at the first non-terminator.
    if (!I->isTerminator())
      break;

    unsigned Opc = I->getOpcode();

    // Non-analyzable terminators (indirect branches, CASEL, etc.).
    if (!isCondBranch(Opc) && !isUncondBranch(Opc))
      return true;

    // Handle unconditional branches.
    if (isUncondBranch(Opc)) {
      if (!AllowModify) {
        // If we already saw an unconditional branch (walking bottom-up),
        // there are two consecutive unconditional branches — bail out.
        if (TBB && Cond.empty())
          return true;
        TBB = I->getOperand(0).getMBB();
        continue;
      }

      // If the block has any instructions after an unconditional branch,
      // delete them (they are dead code) and remove stale successors.
      if (std::next(I) != MBB.end()) {
        // Collect targets of dead branches being removed.
        MachineBasicBlock *KeepSucc = I->getOperand(0).getMBB();
        for (auto J = std::next(I); J != MBB.end();) {
          if (J->isBranch() && !J->isIndirectBranch()) {
            for (const MachineOperand &MO : J->operands()) {
              if (MO.isMBB() && MO.getMBB() != KeepSucc)
                MBB.removeSuccessor(MO.getMBB());
            }
          }
          J = MBB.erase(J);
        }
      }

      Cond.clear();
      FBB = nullptr;

      // TBB is used to indicate the unconditional destination.
      TBB = I->getOperand(0).getMBB();
      continue;
    }

    // Handle conditional branches.
    assert(isCondBranch(Opc));

    // Working from the bottom, handle the first conditional branch.
    if (Cond.empty()) {
      FBB = TBB;
      TBB = I->getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(Opc));
      continue;
    }

    // Multiple conditional branches? Can't handle.
    return true;
  }

  return false;
}

unsigned VAXInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                    int *BytesRemoved) const {
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return 0;

  unsigned Opc = I->getOpcode();
  if (!isCondBranch(Opc) && !isUncondBranch(Opc))
    return 0;

  I->eraseFromParent();
  unsigned Count = 1;
  int Removed = (Opc == VAX::BRW) ? 3 : 2; // BRW=3, BRB/Bcc=2

  I = MBB.getLastNonDebugInstr();
  if (I != MBB.end()) {
    Opc = I->getOpcode();
    if (isCondBranch(Opc) || isUncondBranch(Opc)) {
      Removed += (Opc == VAX::BRW) ? 3 : 2;
      I->eraseFromParent();
      ++Count;
    }
  }

  if (BytesRemoved)
    *BytesRemoved = Removed;
  return Count;
}

unsigned VAXInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                    MachineBasicBlock *TBB,
                                    MachineBasicBlock *FBB,
                                    ArrayRef<MachineOperand> Cond,
                                    const DebugLoc &DL,
                                    int *BytesAdded) const {
  assert(TBB && "insertBranch must not be told to insert a fallthrough");

  if (Cond.empty()) {
    // Unconditional branch — emit BRW (3 bytes) instead of BRB.
    // BRB is shorter but the MC layer may relax it to BRW, shifting code
    // and pushing nearby conditional branches (which only have 8-bit
    // displacement) out of range. Using BRW here makes sizes predictable
    // for BranchRelaxation. The MC layer's BRB→BRW relaxation still
    // handles hand-written .S files where BRB is used explicitly.
    assert(!FBB && "Unconditional branch with false block?");
    BuildMI(&MBB, DL, get(VAX::BRW)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded = 3;
    return 1;
  }

  // Conditional branch.
  unsigned CC = Cond[0].getImm();
  BuildMI(&MBB, DL, get(CC)).addMBB(TBB);

  if (!FBB) {
    if (BytesAdded)
      *BytesAdded = 2;
    return 1;
  }

  // Conditional + fallthrough unconditional.
  BuildMI(&MBB, DL, get(VAX::BRW)).addMBB(FBB);
  if (BytesAdded)
    *BytesAdded = 5; // 2 (Bcc) + 3 (BRW)
  return 2;
}

bool VAXInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 1 && "Expected single condition operand");
  unsigned Opc = Cond[0].getImm();
  Cond[0].setImm(getOppositeBranch(Opc));
  return false;
}

bool VAXInstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                         int64_t BrOffset) const {
  switch (BranchOpc) {
  // Conditional branches have 8-bit signed displacement: ±127 bytes.
  // Use a conservative range (±117) to absorb getInstSizeInBytes() estimation
  // errors. Conditional branches can only be relaxed at the MIR level (by
  // inverting condition + BRW); the MC layer cannot relax them because
  // relaxInstruction can't emit two instructions. Any conditional branch that
  // escapes BranchRelaxation out-of-range is a hard error.
  case VAX::BEQL: case VAX::BNEQ:
  case VAX::BGTR: case VAX::BGEQ: case VAX::BLSS: case VAX::BLEQ:
  case VAX::BGTRU: case VAX::BGEQU: case VAX::BLSSU: case VAX::BLEQU:
    return BrOffset >= -117 && BrOffset <= 117;
  // BRB has 8-bit signed displacement.
  case VAX::BRB:
    return isInt<8>(BrOffset);
  // BRW has 16-bit signed displacement: ±32 KB.
  case VAX::BRW:
    return isInt<16>(BrOffset);
  default:
    llvm_unreachable("Unknown branch opcode");
  }
}

MachineBasicBlock *
VAXInstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case VAX::BEQL: case VAX::BNEQ:
  case VAX::BGTR: case VAX::BGEQ: case VAX::BLSS: case VAX::BLEQ:
  case VAX::BGTRU: case VAX::BGEQU: case VAX::BLSSU: case VAX::BLEQU:
  case VAX::BRB: case VAX::BRW:
    return MI.getOperand(0).getMBB();
  default:
    llvm_unreachable("Unexpected branch opcode");
  }
}

void VAXInstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                        MachineBasicBlock &NewDestBB,
                                        MachineBasicBlock &RestoreBB,
                                        const DebugLoc &DL, int64_t BrOffset,
                                        RegScavenger *RS) const {
  // For branches beyond BRW range we'd need JMP, but ±32 KB should suffice
  // for any reasonable function. Just emit BRW.
  BuildMI(&MBB, DL, get(VAX::BRW)).addMBB(&NewDestBB);
}

/// Estimate the size of a single operand specifier in bytes.
/// For VAXMemOp (base, disp, index, flags), this estimates one operand's
/// encoding. Called per-operand to sum up the instruction size.
static unsigned estimateOperandSize(const MachineInstr &MI, unsigned OpIdx) {
  const MachineOperand &MO = MI.getOperand(OpIdx);

  // Register operand (not part of a VAXMemOp): register direct = 1 byte.
  if (MO.isReg())
    return 1;

  // Immediate operand: literal (0-63) = 1 byte, else longword immediate = 5.
  if (MO.isImm()) {
    int64_t Val = MO.getImm();
    return (Val >= 0 && Val <= 63) ? 1 : 5;
  }

  // Global/symbol reference: longword immediate = 5 bytes.
  return 5;
}

/// Estimate the size of a VAXMemOp 4-tuple (base, disp, index, flags) in bytes.
/// Returns the number of bytes for this operand specifier, including the
/// optional index prefix byte (0x4n) when an index register is present.
static unsigned estimateMemOpSize(const MachineInstr &MI, unsigned StartIdx) {
  const MachineOperand &Flags = MI.getOperand(StartIdx + 3);
  if (!Flags.isImm())
    return 6; // conservative: index(1) + longword displacement(5)

  // Indexed mode prefix: if Index operand is a non-zero register, the encoder
  // emits 0x40|Rx before the base specifier — add 1 byte.
  unsigned IndexExtra = 0;
  const MachineOperand &Index = MI.getOperand(StartIdx + 2);
  if (Index.isReg() && Index.getReg())
    IndexExtra = 1;

  unsigned Mode = Flags.getImm();
  switch (Mode) {
  case VAXAM::RegDirect:   // 0x5n → 1 byte
  case VAXAM::RegDeferred: // 0x6n → 1 byte
  case VAXAM::AutoDec:     // 0x7n → 1 byte
  case VAXAM::AutoInc:     // 0x8n → 1 byte
  case VAXAM::AutoIncDef:  // 0x9n → 1 byte
    return 1 + IndexExtra;

  case VAXAM::Imm: {
    // Literal (0-63) → 1 byte, else immediate (0x8F + 4 bytes) → 5 bytes.
    const MachineOperand &Disp = MI.getOperand(StartIdx + 1);
    if (Disp.isImm()) {
      int64_t Val = Disp.getImm();
      return ((Val >= 0 && Val <= 63) ? 1 : 5) + IndexExtra;
    }
    return 5 + IndexExtra; // expression → longword
  }

  case VAXAM::Absolute: // 0x9F + 4-byte address → 5 bytes
    return 5 + IndexExtra;

  case VAXAM::Disp:
  case VAXAM::DispDeferred: {
    // Displacement: specifier + byte(1)/word(2)/long(4) displacement.
    const MachineOperand &Disp = MI.getOperand(StartIdx + 1);
    if (Disp.isImm()) {
      int64_t Val = Disp.getImm();
      if (Val >= -128 && Val <= 127) return 2 + IndexExtra;
      if (Val >= -32768 && Val <= 32767) return 3 + IndexExtra;
      return 5 + IndexExtra; // longword displacement
    }
    // Expression (global, etc.) → longword displacement.
    return 5 + IndexExtra;
  }

  default:
    return 5 + IndexExtra; // conservative
  }
}

unsigned VAXInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();

  // Inline assembly: delegate to getInlineAsmLength which counts instruction
  // lines and multiplies by MaxInstLength from MCAsmInfo.
  if (Opc == TargetOpcode::INLINEASM || Opc == TargetOpcode::INLINEASM_BR) {
    const MachineFunction *MF = MI.getParent()->getParent();
    const auto *MAI = MF->getTarget().getMCAsmInfo();
    return getInlineAsmLength(MI.getOperand(0).getSymbolName(), *MAI);
  }

  // Pseudos that expand later.
  if (MI.isMetaInstruction())
    return 0;

  switch (Opc) {
  // 2-byte branches: opcode + 8-bit displacement.
  case VAX::BEQL: case VAX::BNEQ:
  case VAX::BGTR: case VAX::BGEQ: case VAX::BLSS: case VAX::BLEQ:
  case VAX::BGTRU: case VAX::BGEQU: case VAX::BLSSU: case VAX::BLEQU:
  case VAX::BRB:
    return 2;
  // 3-byte branch: opcode + 16-bit displacement.
  case VAX::BRW:
    return 3;
  // CASEL pseudo expands to: casel instr (4B) + (limit+1)*2B table + brw (3B).
  case VAX::CASEL: {
    unsigned Limit = MI.getOperand(1).getImm();
    return 4 + (Limit + 1) * 2 + 3;
  }
  // Stack adjustment pseudos expand to nothing (frame setup).
  case VAX::ADJCALLSTACKDOWN:
  case VAX::ADJCALLSTACKUP:
    return 0;
  default:
    break;
  }

  const MCInstrDesc &Desc = MI.getDesc();

  // Determine opcode size from TSFlags.
  uint64_t TSFlags = Desc.TSFlags;
  unsigned HWOpcode = (TSFlags >> 1) & 0xFFFF;
  unsigned OpcodeSize = (HWOpcode > 0xFF) ? 2 : 1; // FD-prefix → 2 bytes
  bool HasMemOp = TSFlags & 0x1;

  unsigned Size = OpcodeSize;

  // VAX encodes ALL operands (sources AND destinations) as specifier bytes.
  // Unlike RISC targets, the def register is NOT part of the opcode—it has
  // its own specifier in the byte stream. We must count def operands too,
  // but skip tied defs (the tied use already accounts for the specifier).
  unsigned NumOps = Desc.getNumOperands();
  unsigned NumDefs = Desc.getNumDefs();
  unsigned i = 0;

  // Count untied def operands (their specifiers are encoded in the stream).
  for (; i < NumDefs && i < NumOps; ++i) {
    int TiedTo = Desc.getOperandConstraint(i, MCOI::TIED_TO);
    if (TiedTo >= 0)
      continue; // tied def—the tied use will account for this specifier
    if (HasMemOp && i + 3 < NumOps && MI.getOperand(i).isReg() &&
        MI.getOperand(i + 3).isImm()) {
      Size += estimateMemOpSize(MI, i);
      i += 3; // will be incremented to i+4 by the for-loop
    } else {
      Size += estimateOperandSize(MI, i);
    }
  }

  // Count source operands.
  while (i < NumOps) {
    if (HasMemOp && i + 3 < NumOps && MI.getOperand(i).isReg() &&
        MI.getOperand(i + 3).isImm()) {
      Size += estimateMemOpSize(MI, i);
      i += 4;
    } else {
      if (i < MI.getNumOperands())
        Size += estimateOperandSize(MI, i);
      i++;
    }
  }

  return Size;
}
