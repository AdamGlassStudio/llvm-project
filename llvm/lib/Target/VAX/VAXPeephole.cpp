//===-- VAXPeephole.cpp - VAX post-RA peephole optimizations --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Post-RA peephole pass for VAX:
//   - Convert 3-operand ALU to 2-operand when dst == one source
//     (addl3 %rX, %rY, %rY → addl2 %rX, %rY)
//   - Eliminate redundant TSTL/TSTB/TSTW after flag-setting instructions
//   - Combine adjacent CLRL pairs into CLRQ (quadword clear)
//   - Combine adjacent MOVL load/store pairs into MOVQ (quadword move)
//
//===----------------------------------------------------------------------===//

#include "VAX.h"
#include "VAXInstrInfo.h"
#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "vax-peephole"

namespace {

// Map 3-op opcode → (2-op opcode, commutative?)
struct Alu3To2 {
  unsigned Opc3;
  unsigned Opc2;
  bool Commutative;
};

// VAX 2-op instructions: opl2 src, dst  →  dst = dst OP src
// VAX 3-op instructions: opl3 src1, src2, dst  →  dst = src2 OP src1
//   For add: addl3 s1, s2, dst → dst = s2 + s1  (commutative)
//   For sub: subl3 s1, s2, dst → dst = s2 - s1  (NOT commutative)
static const Alu3To2 Alu3To2Table[] = {
    // Longword integer
    {VAX::ADDL3_rr, VAX::ADDL2_rr, true},
    {VAX::ADDL3_ri, VAX::ADDL2_ri, true},
    {VAX::SUBL3_rr, VAX::SUBL2_rr, false},
    {VAX::SUBL3_ri, VAX::SUBL2_ri, false},
    {VAX::MULL3_rr, VAX::MULL2_rr, true},
    {VAX::MULL3_ri, VAX::MULL2_ri, true},
    {VAX::DIVL3_rr, VAX::DIVL2_rr, false},
    {VAX::BISL3_rr, VAX::BISL2_rr, true},
    {VAX::BISL3_ri, VAX::BISL2_ri, true},
    {VAX::XORL3_rr, VAX::XORL2_rr, true},
    {VAX::XORL3_ri, VAX::XORL2_ri, true},
    {VAX::BICL3_rr, VAX::BICL2_rr, false},
    {VAX::BICL3_ri, VAX::BICL2_ri, false},
    // Byte
    {VAX::ADDB3_rr, VAX::ADDB2_rr, true},
    {VAX::SUBB3_rr, VAX::SUBB2_rr, false},
    // Word
    {VAX::ADDW3_rr, VAX::ADDW2_rr, true},
    {VAX::SUBW3_rr, VAX::SUBW2_rr, false},
    // F_float
    {VAX::ADDF3_rr, VAX::ADDF2_rr, true},
    {VAX::SUBF3_rr, VAX::SUBF2_rr, false},
    {VAX::MULF3_rr, VAX::MULF2_rr, true},
    {VAX::DIVF3_rr, VAX::DIVF2_rr, false},
    // D_float
    {VAX::ADDD3_rr, VAX::ADDD2_rr, true},
    {VAX::SUBD3_rr, VAX::SUBD2_rr, false},
    {VAX::MULD3_rr, VAX::MULD2_rr, true},
    {VAX::DIVD3_rr, VAX::DIVD2_rr, false},
};

class VAXPeephole : public MachineFunctionPass {
public:
  static char ID;
  VAXPeephole() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "VAX Peephole"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  bool tryQuadCombine(MachineBasicBlock &MBB,
                      MachineBasicBlock::iterator &II,
                      const TargetInstrInfo *TII);
  bool tryEliminateRedundantTST(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator &II);
};

char VAXPeephole::ID = 0;

/// Return the register tested by a TST instruction, or 0 if not a TST.
static Register getTSTReg(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case VAX::TSTL:
  case VAX::TSTB:
  case VAX::TSTW:
  case VAX::TSTF:
  case VAX::TSTD:
    return MI.getOperand(0).getReg();
  default:
    return Register();
  }
}

/// Check if MI defines the given physical register as an explicit def
/// (i.e. it writes a result to that register, setting N/Z flags on it).
/// We exclude CMP/TST instructions since they set PSW but don't write a GPR.
static bool definesRegWithFlags(const MachineInstr &MI, Register Reg) {
  // Must define PSW (set condition codes).
  if (!MI.modifiesRegister(VAX::PSW, /*TRI=*/nullptr))
    return false;

  // Check explicit defs — the result register(s) of the instruction.
  for (unsigned i = 0, e = MI.getNumExplicitDefs(); i < e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (MO.isReg() && MO.isDef() && MO.getReg() == Reg)
      return true;
  }
  return false;
}

/// Try to eliminate a redundant TSTL/TSTB/TSTW/TSTF/TSTD by checking if the
/// immediately preceding (non-debug) instruction already set N/Z on the same
/// register. Within a basic block, no other path can enter between instructions.
bool VAXPeephole::tryEliminateRedundantTST(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator &II) {
  MachineInstr &MI = *II;
  Register TstReg = getTSTReg(MI);
  if (!TstReg.isValid())
    return false;

  // Walk backward skipping debug/CFI.
  auto Prev = II;
  while (Prev != MBB.begin()) {
    --Prev;
    if (Prev->isDebugInstr() || Prev->isCFIInstruction())
      continue;

    // If the previous real instruction defines TstReg and sets PSW, the
    // TST is redundant — N/Z flags already reflect TstReg's value.
    if (definesRegWithFlags(*Prev, TstReg)) {
      LLVM_DEBUG(dbgs() << "VAXPeephole: eliminating redundant " << MI
                        << "  (flags set by " << *Prev << ")\n");
      auto EraseIt = II++;
      EraseIt->eraseFromParent();
      return true;
    }
    // Hit a non-matching real instruction — stop.
    break;
  }
  return false;
}

/// Get the memory operand base from a CLRL_ms, MOVL_mr, or MOVL_rm instruction.
/// Returns the starting operand index of the VAXMemOp (4-slot: base, disp,
/// index, flags), or -1 if not applicable.
static int getMemOpIdx(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  if (Opc == VAX::CLRL_ms)
    return 0; // (outs), (ins VAXMemOp:$dst) → operand 0
  if (Opc == VAX::MOVL_mr)
    return 1; // (outs), (ins GPRnoPC:$src, VAXMemOp:$dst) → operand 1
  if (Opc == VAX::MOVL_rm)
    return 1; // (outs GPRnoPC:$dst), (ins VAXMemOp:$src) → operand 1
  return -1;
}

/// Check if two memory operands refer to the same base address with
/// displacements that differ by exactly 4 bytes. If so, return the lower
/// displacement value (i.e., the MOVQ/CLRQ target address).
/// MemIdx is the operand index where the 4-slot VAXMemOp starts.
static bool isAdjacentQuadword(const MachineInstr &A, int MemIdxA,
                               const MachineInstr &B, int MemIdxB,
                               int64_t &LowDisp) {
  // VAXMemOp: [base_reg, disp, index_reg, flags]
  const MachineOperand &BaseA = A.getOperand(MemIdxA);
  const MachineOperand &DispA = A.getOperand(MemIdxA + 1);
  const MachineOperand &IdxA  = A.getOperand(MemIdxA + 2);
  const MachineOperand &FlagA = A.getOperand(MemIdxA + 3);

  const MachineOperand &BaseB = B.getOperand(MemIdxB);
  const MachineOperand &DispB = B.getOperand(MemIdxB + 1);
  const MachineOperand &IdxB  = B.getOperand(MemIdxB + 2);
  const MachineOperand &FlagB = B.getOperand(MemIdxB + 3);

  // Both must be register-based with immediate displacement.
  if (!BaseA.isReg() || !BaseB.isReg())
    return false;
  if (!DispA.isImm() || !DispB.isImm())
    return false;

  // Same base register, same index register, same flags.
  if (BaseA.getReg() != BaseB.getReg())
    return false;
  if (IdxA.getReg() != IdxB.getReg())
    return false;
  if (FlagA.getImm() != FlagB.getImm())
    return false;

  // No index register — MOVQ with indexed mode is unusual and risky.
  if (IdxA.getReg() != 0)
    return false;

  int64_t DA = DispA.getImm();
  int64_t DB = DispB.getImm();

  if (DA + 4 == DB) {
    LowDisp = DA;
    return true;
  }
  if (DB + 4 == DA) {
    LowDisp = DB;
    return true;
  }
  return false;
}

/// Check that the base register of a memory operand is not modified between
/// instructions First and Second (exclusive).
static bool baseNotModified(const MachineInstr &First, int MemIdx,
                            const MachineInstr &Second) {
  Register BaseReg = First.getOperand(MemIdx).getReg();
  auto It = std::next(MachineBasicBlock::const_iterator(First));
  auto End = MachineBasicBlock::const_iterator(Second);
  for (; It != End; ++It) {
    if (It->modifiesRegister(BaseReg, /*TRI=*/nullptr))
      return false;
  }
  return true;
}

bool VAXPeephole::tryQuadCombine(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator &II,
                                 const TargetInstrInfo *TII) {
  MachineInstr &MI = *II;
  unsigned Opc = MI.getOpcode();
  int MemIdx = getMemOpIdx(MI);
  if (MemIdx < 0)
    return false;

  // Look ahead for a matching instruction.
  auto Next = std::next(II);
  // Skip debug values / CFI between the pair.
  while (Next != MBB.end() && (Next->isDebugInstr() || Next->isCFIInstruction()))
    ++Next;
  if (Next == MBB.end())
    return false;

  MachineInstr &MI2 = *Next;

  // --- CLRL_ms + CLRL_ms → CLRQ ---
  if (Opc == VAX::CLRL_ms && MI2.getOpcode() == VAX::CLRL_ms) {
    int64_t LowDisp;
    if (!isAdjacentQuadword(MI, 0, MI2, 0, LowDisp))
      return false;
    if (!baseNotModified(MI, 0, MI2))
      return false;

    Register BaseReg = MI.getOperand(0).getReg();
    Register IdxReg  = MI.getOperand(2).getReg();
    int64_t Flags    = MI.getOperand(3).getImm();

    MachineInstrBuilder MIB =
        BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::CLRQ));
    MIB.addReg(BaseReg).addImm(LowDisp).addReg(IdxReg).addImm(Flags);
    MIB->setMemRefs(*MBB.getParent(), {});

    LLVM_DEBUG(dbgs() << "VAXPeephole: CLRL+CLRL→CLRQ " << MI << "  + "
                      << MI2);
    auto Erase1 = II;
    II = std::next(Next);
    Erase1->eraseFromParent();
    Next->eraseFromParent();
    return true;
  }

  // --- MOVL_mr + MOVL_mr → MOVQ (store pair) ---
  // Two stores to adjacent locations from consecutive registers.
  if (Opc == VAX::MOVL_mr && MI2.getOpcode() == VAX::MOVL_mr) {
    int64_t LowDisp;
    if (!isAdjacentQuadword(MI, 1, MI2, 1, LowDisp))
      return false;
    if (!baseNotModified(MI, 1, MI2))
      return false;

    // Determine which store targets the low address.
    int64_t DispA = MI.getOperand(2).getImm();
    const MachineInstr &LowMI  = (DispA == LowDisp) ? MI : MI2;
    const MachineInstr &HighMI = (DispA == LowDisp) ? MI2 : MI;

    Register SrcLo = LowMI.getOperand(0).getReg();
    Register SrcHi = HighMI.getOperand(0).getReg();

    // The source registers must be a consecutive pair (Rn, Rn+1).
    if (SrcHi != SrcLo + 1)
      return false;

    Register BaseReg = MI.getOperand(1).getReg();
    Register IdxReg  = MI.getOperand(3).getReg();
    int64_t Flags    = MI.getOperand(4).getImm();

    // Emit MOVQ as memory-to-memory form: src is register-direct, dst is mem.
    MachineInstrBuilder MIB =
        BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::MOVQ));
    // Source: register-direct via VAXMemOp (base=SrcLo, disp=0, idx=noreg,
    //         flags=RegDirect)
    MIB.addReg(SrcLo).addImm(0).addReg(0).addImm(VAXAM::RegDirect);
    // Destination: memory
    MIB.addReg(BaseReg).addImm(LowDisp).addReg(IdxReg).addImm(Flags);

    LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL+MOVL→MOVQ(store) " << MI << "  + "
                      << MI2);
    auto Erase1 = II;
    II = std::next(Next);
    Erase1->eraseFromParent();
    Next->eraseFromParent();
    return true;
  }

  // --- MOVL_rm + MOVL_rm → MOVQ (load pair) ---
  // Two loads from adjacent locations into consecutive registers.
  if (Opc == VAX::MOVL_rm && MI2.getOpcode() == VAX::MOVL_rm) {
    int64_t LowDisp;
    if (!isAdjacentQuadword(MI, 1, MI2, 1, LowDisp))
      return false;
    if (!baseNotModified(MI, 1, MI2))
      return false;

    // Determine which load targets the low address.
    int64_t DispA = MI.getOperand(2).getImm();
    const MachineInstr &LowMI  = (DispA == LowDisp) ? MI : MI2;
    const MachineInstr &HighMI = (DispA == LowDisp) ? MI2 : MI;

    Register DstLo = LowMI.getOperand(0).getReg();
    Register DstHi = HighMI.getOperand(0).getReg();

    // The destination registers must be a consecutive pair (Rn, Rn+1).
    if (DstHi != DstLo + 1)
      return false;

    // Make sure the first load doesn't clobber the base register of the second.
    Register BaseReg = MI.getOperand(1).getReg();
    if (MI.getOperand(0).getReg() == BaseReg)
      return false;

    Register IdxReg  = MI.getOperand(3).getReg();
    int64_t Flags    = MI.getOperand(4).getImm();

    // Emit MOVQ: src is memory, dst is register-direct.
    MachineInstrBuilder MIB =
        BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::MOVQ));
    // Source: memory
    MIB.addReg(BaseReg).addImm(LowDisp).addReg(IdxReg).addImm(Flags);
    // Destination: register-direct via VAXMemOp
    MIB.addReg(DstLo).addImm(0).addReg(0).addImm(VAXAM::RegDirect);

    LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL+MOVL→MOVQ(load) " << MI << "  + "
                      << MI2);
    auto Erase1 = II;
    II = std::next(Next);
    Erase1->eraseFromParent();
    Next->eraseFromParent();
    return true;
  }

  return false;
}

bool VAXPeephole::runOnMachineFunction(MachineFunction &MF) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (auto II = MBB.begin(), IE = MBB.end(); II != IE; /*below*/) {
      MachineInstr &MI = *II;

      // Try 3-op → 2-op conversion
      bool Converted = false;
      for (const auto &Entry : Alu3To2Table) {
        if (MI.getOpcode() != Entry.Opc3)
          continue;

        // 3-op layout: dst = op(src1, src2)
        //   Operand 0: dst (def)
        //   Operand 1: src1
        //   Operand 2: src2
        // 2-op layout: dst = op(src, dst)  i.e. dst OP= src
        //   Operand 0: dst (def, tied to src_tied)
        //   Operand 1: src
        //   Operand 2: src_tied (tied to dst)
        MachineOperand &Dst = MI.getOperand(0);
        MachineOperand &Src1 = MI.getOperand(1);
        MachineOperand &Src2 = MI.getOperand(2);

        if (!Dst.isReg())
          break;

        Register DstReg = Dst.getReg();

        // Check if dst == src2 (the natural case: dst OP= src1)
        if (Src2.isReg() && Src2.getReg() == DstReg) {
          MachineInstrBuilder MIB =
              BuildMI(MBB, II, MI.getDebugLoc(), TII->get(Entry.Opc2));
          MIB.addDef(DstReg);
          MIB.add(Src1);
          MIB.addReg(DstReg);
          LLVM_DEBUG(dbgs() << "VAXPeephole: 3op→2op " << MI);
          auto EraseIt = II++;
          EraseIt->eraseFromParent();
          Changed = true;
          Converted = true;
          break;
        }

        // Check if dst == src1 AND commutative (swap operands)
        if (Entry.Commutative && Src1.isReg() && Src1.getReg() == DstReg) {
          MachineInstrBuilder MIB =
              BuildMI(MBB, II, MI.getDebugLoc(), TII->get(Entry.Opc2));
          MIB.addDef(DstReg);
          MIB.add(Src2);
          MIB.addReg(DstReg);
          LLVM_DEBUG(dbgs() << "VAXPeephole: 3op→2op(swap) " << MI);
          auto EraseIt = II++;
          EraseIt->eraseFromParent();
          Changed = true;
          Converted = true;
          break;
        }

        break; // Found matching entry but can't convert
      }

      // Try ADDL2_ri $1 → INCL, ADDL2_ri $-1 → DECL
      if (!Converted) {
        unsigned Opc = MI.getOpcode();
        if (Opc == VAX::ADDL2_ri && MI.getOperand(1).isImm()) {
          int64_t Imm = MI.getOperand(1).getImm();
          Register DstReg = MI.getOperand(0).getReg();
          unsigned NewOpc = 0;
          if (Imm == 1)
            NewOpc = VAX::INCL;
          else if (Imm == -1)
            NewOpc = VAX::DECL;
          if (NewOpc) {
            MachineInstrBuilder MIB =
                BuildMI(MBB, II, MI.getDebugLoc(), TII->get(NewOpc));
            MIB.addDef(DstReg);
            MIB.addReg(DstReg);
            LLVM_DEBUG(dbgs() << "VAXPeephole: addl2→inc/dec " << MI);
            auto EraseIt = II++;
            EraseIt->eraseFromParent();
            Changed = true;
            Converted = true;
          }
        }
      }

      if (!Converted)
        Converted = tryEliminateRedundantTST(MBB, II);

      if (!Converted)
        Converted = tryQuadCombine(MBB, II, TII);

      if (!Converted)
        ++II;
    }
  }
  return Changed;
}

} // end anonymous namespace

INITIALIZE_PASS(VAXPeephole, DEBUG_TYPE, "VAX Peephole", false, false)

FunctionPass *llvm::createVAXPeepholePass() { return new VAXPeephole(); }
