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
    // 64-bit register pair copy via MOVD.
    BuildMI(MBB, MI, DL, get(VAX::MOVD_rr), DstReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  // MOVL covers all 32-bit register copies (both i32 and f32 — same hardware).
  BuildMI(MBB, MI, DL, get(VAX::MOVL_rr), DstReg)
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
  unsigned Opc = (RC == &VAX::QPRRegClass) ? VAX::MOVD_mr : VAX::MOVL_mr;
  BuildMI(MBB, MI, DL, get(Opc))
      .addReg(SrcReg, getKillRegState(isKill))
      .addFrameIndex(FrameIndex)
      .addImm(0);
}

void VAXInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         Register DstReg, int FrameIndex,
                                         const TargetRegisterClass *RC,
                                         Register VReg, unsigned SubReg,
                                         MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  unsigned Opc = (RC == &VAX::QPRRegClass) ? VAX::MOVD_rm : VAX::MOVL_rm;
  BuildMI(MBB, MI, DL, get(Opc), DstReg)
      .addFrameIndex(FrameIndex)
      .addImm(0);
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
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return false;

  // Walk backwards past non-branch terminators.
  while (I != MBB.end() && !I->isTerminator())
    --I;
  if (I == MBB.end())
    return false;

  // Skip non-analyzable terminators (indirect branches, CASEL, etc.).
  unsigned Opc = I->getOpcode();
  if (!isCondBranch(Opc) && !isUncondBranch(Opc))
    return true;

  // Last instruction is an unconditional branch.
  if (isUncondBranch(Opc)) {
    TBB = I->getOperand(0).getMBB();

    // Check for preceding conditional branch.
    if (I != MBB.begin()) {
      --I;
      if (I->isTerminator() && isCondBranch(I->getOpcode())) {
        FBB = TBB;
        TBB = I->getOperand(0).getMBB();
        Cond.push_back(MachineOperand::CreateImm(I->getOpcode()));
        return false;
      }
    }

    // Just an unconditional branch.
    return false;
  }

  // Last instruction is a conditional branch.
  TBB = I->getOperand(0).getMBB();
  Cond.push_back(MachineOperand::CreateImm(Opc));

  // Check for preceding branch.
  if (I != MBB.begin()) {
    --I;
    if (I->isTerminator()) {
      if (isUncondBranch(I->getOpcode())) {
        // cond + uncond before it? That's unusual (normally cond then uncond).
        return true;
      }
      if (isCondBranch(I->getOpcode())) {
        // Two conditional branches? Can't analyze.
        return true;
      }
    }
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

  I = MBB.getLastNonDebugInstr();
  if (I != MBB.end()) {
    Opc = I->getOpcode();
    if (isCondBranch(Opc) || isUncondBranch(Opc)) {
      I->eraseFromParent();
      ++Count;
    }
  }

  if (BytesRemoved)
    *BytesRemoved = 0; // We don't track exact byte counts.
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
      *BytesAdded = 0;
    return 1;
  }

  // Conditional branch.
  unsigned CC = Cond[0].getImm();
  BuildMI(&MBB, DL, get(CC)).addMBB(TBB);

  if (!FBB) {
    if (BytesAdded)
      *BytesAdded = 0;
    return 1;
  }

  // Conditional + fallthrough unconditional.
  BuildMI(&MBB, DL, get(VAX::BRW)).addMBB(FBB);
  if (BytesAdded)
    *BytesAdded = 0;
  return 2;
}

bool VAXInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 1 && "Expected single condition operand");
  unsigned Opc = Cond[0].getImm();
  Cond[0].setImm(getOppositeBranch(Opc));
  return false;
}
