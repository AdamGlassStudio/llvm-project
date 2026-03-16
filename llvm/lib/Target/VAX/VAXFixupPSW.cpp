//===-- VAXFixupPSW.cpp - Fix PSW clobbers via compare-branch fusion ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// On VAX, nearly all data-manipulation instructions (including MOVL used for
// register copies) set condition codes in PSW. The register allocator inserts
// COPY pseudos that are later expanded to MOVL_rr, but the RA doesn't know
// that COPY will clobber PSW. This can place a MOVL between a compare (CMPL)
// and a conditional branch (Bcc), producing incorrect code.
//
// Solution: two passes that bracket register allocation:
//
// 1. VAXFuseCmpBranch (pre-RA): Fuses adjacent CMP/TST + Bcc into a single
//    CMP_BRANCH pseudo-instruction. The RA sees this as one instruction and
//    cannot insert COPYs between compare and branch.
//
// 2. VAXExpandCmpBranch (post-RA): Expands the fused pseudo back into
//    separate CMP + Bcc instructions before any branch analysis passes run.
//
//===----------------------------------------------------------------------===//

#include "VAX.h"
#include "VAXInstrInfo.h"
#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

using namespace llvm;

#define FUSE_DEBUG_TYPE "vax-fuse-cmp-branch"
#define EXPAND_DEBUG_TYPE "vax-expand-cmp-branch"
#define DEBUG_TYPE "vax-fixup-psw"

// Map condition code integer to the corresponding Bcc opcode.
static unsigned ccToBranchOpcode(unsigned CC) {
  switch (CC) {
  case 0: return VAX::BEQL;
  case 1: return VAX::BNEQ;
  case 2: return VAX::BGTR;
  case 3: return VAX::BGEQ;
  case 4: return VAX::BLSS;
  case 5: return VAX::BLEQ;
  case 6: return VAX::BGTRU;
  case 7: return VAX::BGEQU;
  case 8: return VAX::BLSSU;
  case 9: return VAX::BLEQU;
  default: llvm_unreachable("invalid VAX condition code");
  }
}

// Map Bcc opcode to condition code integer.
static unsigned branchOpcodeToCC(unsigned Opc) {
  switch (Opc) {
  case VAX::BEQL:  return 0;
  case VAX::BNEQ:  return 1;
  case VAX::BGTR:  return 2;
  case VAX::BGEQ:  return 3;
  case VAX::BLSS:  return 4;
  case VAX::BLEQ:  return 5;
  case VAX::BGTRU: return 6;
  case VAX::BGEQU: return 7;
  case VAX::BLSSU: return 8;
  case VAX::BLEQU: return 9;
  default: return ~0U;
  }
}

// Get the compare opcode that corresponds to a fused pseudo.
static unsigned fusedToCmpOpcode(unsigned Opc) {
  switch (Opc) {
  case VAX::CMP_BRANCH_ri: return VAX::CMPL_ri;
  case VAX::CMP_BRANCH_rr: return VAX::CMPL_rr;
  case VAX::CMP_BRANCH_rm: return VAX::CMPL_rm;
  case VAX::CMP_BRANCH_mm: return VAX::CMPL_mm;
  case VAX::CMP_BRANCH_mi: return VAX::CMPL_mi;
  case VAX::TST_BRANCH:    return VAX::TSTL;
  case VAX::TST_BRANCH_m:  return VAX::TSTL_m;
  case VAX::CMPF_BRANCH:   return VAX::CMPF;
  case VAX::CMPD_BRANCH:   return VAX::CMPD;
  case VAX::TSTF_BRANCH:   return VAX::TSTF;
  case VAX::TSTD_BRANCH:   return VAX::TSTD;
  case VAX::CMPB_BRANCH:   return VAX::CMPB_rr;
  case VAX::CMPB_BRANCH_mi: return VAX::CMPB_mi;
  case VAX::TSTB_BRANCH:   return VAX::TSTB;
  case VAX::TSTB_BRANCH_m: return VAX::TSTB_m;
  case VAX::CMPW_BRANCH:   return VAX::CMPW_rr;
  case VAX::TSTW_BRANCH:   return VAX::TSTW;
  default: return 0;
  }
}

static bool isFusedCmpBranch(unsigned Opc) {
  return fusedToCmpOpcode(Opc) != 0;
}

// Return true if MI is a compare/test instruction.
static bool isCompareOrTest(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case VAX::CMPL_ri:
  case VAX::CMPL_rr:
  case VAX::CMPL_rm:
  case VAX::CMPL_mm:
  case VAX::CMPL_mi:
  case VAX::TSTL:
  case VAX::TSTL_m:
  case VAX::CMPF:
  case VAX::CMPD:
  case VAX::TSTF:
  case VAX::TSTD:
  case VAX::CMPB_rr:
  case VAX::CMPB_mi:
  case VAX::TSTB:
  case VAX::TSTB_m:
  case VAX::CMPW_rr:
  case VAX::TSTW:
    return true;
  default:
    return false;
  }
}

// Return the fused pseudo opcode for a given compare opcode.
static unsigned cmpToFusedOpcode(unsigned CmpOpc) {
  switch (CmpOpc) {
  case VAX::CMPL_ri: return VAX::CMP_BRANCH_ri;
  case VAX::CMPL_rr: return VAX::CMP_BRANCH_rr;
  case VAX::CMPL_rm: return VAX::CMP_BRANCH_rm;
  case VAX::CMPL_mm: return VAX::CMP_BRANCH_mm;
  case VAX::CMPL_mi: return VAX::CMP_BRANCH_mi;
  case VAX::TSTL:    return VAX::TST_BRANCH;
  case VAX::TSTL_m:  return VAX::TST_BRANCH_m;
  case VAX::CMPF:    return VAX::CMPF_BRANCH;
  case VAX::CMPD:    return VAX::CMPD_BRANCH;
  case VAX::TSTF:    return VAX::TSTF_BRANCH;
  case VAX::TSTD:    return VAX::TSTD_BRANCH;
  case VAX::CMPB_rr: return VAX::CMPB_BRANCH;
  case VAX::CMPB_mi: return VAX::CMPB_BRANCH_mi;
  case VAX::TSTB:    return VAX::TSTB_BRANCH;
  case VAX::TSTB_m:  return VAX::TSTB_BRANCH_m;
  case VAX::CMPW_rr: return VAX::CMPW_BRANCH;
  case VAX::TSTW:    return VAX::TSTW_BRANCH;
  default: return 0;
  }
}

//===----------------------------------------------------------------------===//
// VAXFuseCmpBranch — Pre-RA: fuse CMP + Bcc into CMP_BRANCH pseudo
//===----------------------------------------------------------------------===//

namespace {

class VAXFuseCmpBranch : public MachineFunctionPass {
public:
  static char ID;
  VAXFuseCmpBranch() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "VAX Fuse Compare-Branch";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

char VAXFuseCmpBranch::ID = 0;

} // end anonymous namespace

bool VAXFuseCmpBranch::runOnMachineFunction(MachineFunction &MF) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (auto II = MBB.begin(), IE = MBB.end(); II != IE; /*advanced below*/) {
      MachineInstr &CmpMI = *II;
      if (!isCompareOrTest(CmpMI)) {
        ++II;
        continue;
      }

      // Check if the next non-debug instruction is a conditional branch.
      auto NextIt = std::next(II);
      while (NextIt != IE && NextIt->isDebugInstr())
        ++NextIt;

      if (NextIt == IE) {
        ++II;
        continue;
      }

      unsigned CC = branchOpcodeToCC(NextIt->getOpcode());
      if (CC == ~0U) {
        ++II;
        continue;
      }

      unsigned FusedOpc = cmpToFusedOpcode(CmpMI.getOpcode());
      if (!FusedOpc) {
        ++II;
        continue;
      }

      // Get branch target.
      MachineBasicBlock *TargetBB = NextIt->getOperand(0).getMBB();

      // Build the fused pseudo.
      MachineInstrBuilder MIB =
          BuildMI(MBB, II, CmpMI.getDebugLoc(), TII->get(FusedOpc));

      // Copy compare explicit operands only (skip implicit PSW defs).
      for (unsigned i = 0, e = CmpMI.getNumExplicitOperands(); i < e; ++i) {
        MIB.add(CmpMI.getOperand(i));
      }

      // Add condition code and branch target.
      MIB.addImm(CC);
      MIB.addMBB(TargetBB);

      LLVM_DEBUG(dbgs() << "VAXFuseCmpBranch: fusing " << CmpMI << "  + "
                        << *NextIt << "  -> " << *MIB << "\n");

      // Remove the original CMP and Bcc.
      auto EraseIt1 = II;
      auto EraseIt2 = NextIt;
      II = std::next(EraseIt2);
      EraseIt1->eraseFromParent();
      EraseIt2->eraseFromParent();
      Changed = true;
    }
  }
  return Changed;
}

//===----------------------------------------------------------------------===//
// VAXExpandCmpBranch — Post-RA: expand CMP_BRANCH back into CMP + Bcc,
//                      and expand SELECT_CC pseudos into branch diamonds.
//===----------------------------------------------------------------------===//

namespace {

class VAXExpandCmpBranch : public MachineFunctionPass {
public:
  static char ID;
  VAXExpandCmpBranch() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "VAX Expand Compare-Branch and Select";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  bool expandSelectCC(MachineInstr &MI, const TargetInstrInfo *TII);
};

char VAXExpandCmpBranch::ID = 0;

} // end anonymous namespace

static bool cleanStaleSuccessors(MachineFunction &MF,
                                 const TargetInstrInfo *TII);

static bool isSelectCCPseudo(unsigned Opc) {
  switch (Opc) {
  case VAX::SELECT_CC_Pseudo:
  case VAX::SELECT_CC_B_Pseudo:
  case VAX::SELECT_CC_W_Pseudo:
  case VAX::SELECT_CC_F_Pseudo:
  case VAX::SELECT_CC_D_Pseudo:
    return true;
  default:
    return false;
  }
}

// Map VAXCC condition code to branch opcode.
static unsigned selectCCToBranchOpc(unsigned VAXCC) {
  static const unsigned BrOpcodes[] = {
    VAX::BEQL, VAX::BNEQ, VAX::BGTR, VAX::BGEQ,
    VAX::BLSS, VAX::BLEQ, VAX::BGTRU, VAX::BGEQU,
    VAX::BLSSU, VAX::BLEQU
  };
  assert(VAXCC < std::size(BrOpcodes) && "Invalid VAXCC");
  return BrOpcodes[VAXCC];
}

/// Expand a SELECT_CC pseudo into a three-block diamond.
///
/// This runs post-RA to avoid PSW corruption.  On VAX, every register copy
/// (MOVB/MOVW/MOVL) clobbers PSW.  Pre-RA expansion (usesCustomInserter +
/// PHI nodes) causes PHI elimination to insert copies in the CMP block —
/// between the compare and the branch — corrupting condition flags.
///
/// Expansion:
///   BB:        CMP ...  ; implicit-def $psw
///              Bcc TrueMBB, implicit $psw
///   FalseMBB:  [MOVx $dst, $falseReg]  BRW SinkMBB
///   TrueMBB:   [MOVx $dst, $trueReg]   (fall through)
///   SinkMBB:   ... (original continuation)
///
/// Copies are omitted when $dst already equals the source register.
bool VAXExpandCmpBranch::expandSelectCC(MachineInstr &MI,
                                        const TargetInstrInfo *TII) {
  MachineBasicBlock *BB = MI.getParent();
  MachineFunction *MF = BB->getParent();
  DebugLoc DL = MI.getDebugLoc();

  Register DstReg = MI.getOperand(0).getReg();
  Register TrueReg = MI.getOperand(1).getReg();
  Register FalseReg = MI.getOperand(2).getReg();
  unsigned VAXCC = MI.getOperand(3).getImm();
  unsigned BrOpc = selectCCToBranchOpc(VAXCC);

  // Pick the right copy opcode based on the pseudo variant.
  unsigned CopyOpc;
  switch (MI.getOpcode()) {
  case VAX::SELECT_CC_B_Pseudo: CopyOpc = VAX::MOVBrr; break;
  case VAX::SELECT_CC_W_Pseudo: CopyOpc = VAX::MOVWrr; break;
  case VAX::SELECT_CC_F_Pseudo: CopyOpc = VAX::MOVF_rr; break;
  case VAX::SELECT_CC_D_Pseudo: CopyOpc = VAX::MOVQ_rr; break;
  default:                      CopyOpc = VAX::MOVL_rr; break;
  }

  const BasicBlock *LLVMBB = BB->getBasicBlock();
  MachineFunction::iterator InsertPt = ++BB->getIterator();

  MachineBasicBlock *FalseMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *TrueMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *SinkMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MF->insert(InsertPt, FalseMBB);
  MF->insert(InsertPt, TrueMBB);
  MF->insert(InsertPt, SinkMBB);

  // Move everything after the SELECT_CC pseudo into SinkMBB.
  SinkMBB->splice(SinkMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(BB);

  // BB: conditional branch to TrueMBB, fall through to FalseMBB.
  BB->addSuccessor(FalseMBB);
  BB->addSuccessor(TrueMBB);
  BuildMI(BB, DL, TII->get(BrOpc)).addMBB(TrueMBB);

  // FalseMBB: copy false value into dst, jump to SinkMBB.
  FalseMBB->addSuccessor(SinkMBB);
  if (FalseReg != DstReg)
    BuildMI(FalseMBB, DL, TII->get(CopyOpc), DstReg).addReg(FalseReg);
  BuildMI(FalseMBB, DL, TII->get(VAX::BRW)).addMBB(SinkMBB);

  // TrueMBB: copy true value into dst, fall through to SinkMBB.
  TrueMBB->addSuccessor(SinkMBB);
  if (TrueReg != DstReg)
    BuildMI(TrueMBB, DL, TII->get(CopyOpc), DstReg).addReg(TrueReg);

  SinkMBB->addLiveIn(DstReg);

  MI.eraseFromParent();
  return true;
}

bool VAXExpandCmpBranch::runOnMachineFunction(MachineFunction &MF) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (auto II = MBB.begin(), IE = MBB.end(); II != IE; /*advanced below*/) {
      MachineInstr &MI = *II;

      // SELECT_CC expansion creates new blocks and splices instructions out
      // of MBB, invalidating the inner iterator.  Break to the outer loop
      // which will visit the new blocks.
      if (isSelectCCPseudo(MI.getOpcode())) {
        expandSelectCC(MI, TII);
        Changed = true;
        break; // MBB is now truncated; new blocks follow in function order.
      }

      if (!isFusedCmpBranch(MI.getOpcode())) {
        ++II;
        continue;
      }

      unsigned CmpOpc = fusedToCmpOpcode(MI.getOpcode());
      DebugLoc DL = MI.getDebugLoc();

      // Operand layout:
      //   CMP_BRANCH_ri: lhs(reg), rhs(imm), cc(imm), dst(MBB)
      //   CMP_BRANCH_rr: lhs(reg), rhs(reg), cc(imm), dst(MBB)
      //   TST_BRANCH:    src(reg), cc(imm), dst(MBB)
      //   CMPF_BRANCH:   lhs(reg), rhs(reg), cc(imm), dst(MBB)
      //   CMPD_BRANCH:   lhs(reg), rhs(reg), cc(imm), dst(MBB)

      unsigned NumExplicit = MI.getNumExplicitOperands();
      // Last two explicit operands are always cc(imm) and dst(MBB).
      unsigned CC = MI.getOperand(NumExplicit - 2).getImm();
      MachineBasicBlock *TargetBB = MI.getOperand(NumExplicit - 1).getMBB();

      // Emit the compare (all explicit operands except cc and dst).
      MachineInstrBuilder CmpMIB =
          BuildMI(MBB, II, DL, TII->get(CmpOpc));
      for (unsigned i = 0, e = NumExplicit - 2; i < e; ++i)
        CmpMIB.add(MI.getOperand(i));

      // Emit the branch.
      unsigned BrOpc = ccToBranchOpcode(CC);
      BuildMI(MBB, II, DL, TII->get(BrOpc)).addMBB(TargetBB);

      LLVM_DEBUG(dbgs() << "VAXExpandCmpBranch: expanding " << MI << "\n");

      auto EraseIt = II;
      ++II;
      EraseIt->eraseFromParent();
      Changed = true;
    }
  }

  // Clean up stale successor edges. SelectionDAG can produce blocks with
  // unconditional branches but extra successors left over from folded
  // conditional branches (e.g., fcmp uno on VAX always folds to false,
  // leaving a phantom edge to the "true" block). Doing this here — before
  // BranchFolding — avoids iterator-invalidation issues that occur if the
  // cleanup is done inside analyzeBranch during TailMergeBlocks.
  Changed |= cleanStaleSuccessors(MF, TII);

  return Changed;
}

static bool cleanStaleSuccessors(MachineFunction &MF,
                                 const TargetInstrInfo *TII) {
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
    SmallVector<MachineOperand, 4> Cond;
    if (TII->analyzeBranch(MBB, TBB, FBB, Cond, /*AllowModify=*/false))
      continue;

    // Only clean up unconditional-only blocks (no conditional branch).
    if (!TBB || !Cond.empty())
      continue;

    SmallVector<MachineBasicBlock *, 4> Stale;
    for (MachineBasicBlock *Succ : MBB.successors())
      if (Succ != TBB)
        Stale.push_back(Succ);

    for (MachineBasicBlock *S : Stale) {
      LLVM_DEBUG(dbgs() << "VAXExpandCmpBranch: removing stale successor "
                        << printMBBReference(*S) << " from "
                        << printMBBReference(MBB) << "\n");
      MBB.removeSuccessor(S);
      Changed = true;
    }
  }
  return Changed;
}

//===----------------------------------------------------------------------===//
// Pass initialization and creation
//===----------------------------------------------------------------------===//

INITIALIZE_PASS(VAXFuseCmpBranch, FUSE_DEBUG_TYPE,
                "VAX Fuse Compare-Branch", false, false)
INITIALIZE_PASS(VAXExpandCmpBranch, EXPAND_DEBUG_TYPE,
                "VAX Expand Compare-Branch", false, false)

FunctionPass *llvm::createVAXFuseCmpBranchPass() {
  return new VAXFuseCmpBranch();
}

FunctionPass *llvm::createVAXExpandCmpBranchPass() {
  return new VAXExpandCmpBranch();
}

// Keep the old entry point for compatibility (now a no-op that can be removed).
FunctionPass *llvm::createVAXFixupPSWPass() {
  return new VAXExpandCmpBranch();
}
