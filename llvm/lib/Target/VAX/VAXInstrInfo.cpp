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
  int Removed = isUncondBranch(Opc) ? 3 : 2; // BRW=3, Bcc=2

  I = MBB.getLastNonDebugInstr();
  if (I != MBB.end()) {
    Opc = I->getOpcode();
    if (isCondBranch(Opc) || isUncondBranch(Opc)) {
      Removed += isUncondBranch(Opc) ? 3 : 2;
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
    // Unconditional branch.
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
  case VAX::BEQL: case VAX::BNEQ:
  case VAX::BGTR: case VAX::BGEQ: case VAX::BLSS: case VAX::BLEQ:
  case VAX::BGTRU: case VAX::BGEQU: case VAX::BLSSU: case VAX::BLEQU:
    return isInt<8>(BrOffset);
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

unsigned VAXInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();

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
  default:
    break;
  }

  // For non-branch instructions, use a conservative estimate.
  // VAX instructions are variable-length; most are 2–10 bytes.
  // Use the MCInst encoding size if available, otherwise estimate.
  const MCInstrDesc &Desc = MI.getDesc();
  unsigned Size = Desc.getSize();
  if (Size)
    return Size;

  // Conservative fallback: count operands and estimate.
  // VAX instructions are variable-length (1–30+ bytes).
  // Overestimate to ensure BranchRelaxation is safe.
  // Opcode(1-2) + each operand can be up to 6 bytes (displacement mode).
  unsigned NumOps = Desc.getNumOperands();
  return 2 + NumOps * 6;
}
