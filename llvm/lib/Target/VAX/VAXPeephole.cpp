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
//   - Combine adjacent PUSHL register pairs into MOVQ reg, -(SP)
//   - Convert MOVL_ri + ADDL3_rm [+ PUSHL_r] to MOVL_rm + MOVAL/PUSHAL
//   - Convert ADDL3_ri to MOVAL (shorter encoding for imm outside 0-63)
//   - Convert ADDL3_ri + PUSHL_r to PUSHAL (one instruction instead of two)
//   - Shorten large immediates: MOVL_ri/PUSHL_i → ASHL_ii/MNEGL_i
//     when the constant decomposes as base<<shift or is a small negative
//
//===----------------------------------------------------------------------===//

#include "VAX.h"
#include "VAXInstrInfo.h"
#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "llvm/ADT/bit.h"
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
    {VAX::ADDL3_rm, VAX::ADDL2_rm, true},
    {VAX::SUBL3_rr, VAX::SUBL2_rr, false},
    {VAX::SUBL3_ri, VAX::SUBL2_ri, false},
    {VAX::MULL3_rr, VAX::MULL2_rr, true},
    {VAX::MULL3_ri, VAX::MULL2_ri, true},
    {VAX::MULL3_rm, VAX::MULL2_rm, true},
    {VAX::DIVL3_rr, VAX::DIVL2_rr, false},
    {VAX::BISL3_rr, VAX::BISL2_rr, true},
    {VAX::BISL3_ri, VAX::BISL2_ri, true},
    {VAX::BISL3_rm, VAX::BISL2_rm, true},
    {VAX::XORL3_rr, VAX::XORL2_rr, true},
    {VAX::XORL3_ri, VAX::XORL2_ri, true},
    {VAX::XORL3_rm, VAX::XORL2_rm, true},
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
  bool tryQuadCombineNonAdjacent(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator &II,
                                  const TargetInstrInfo *TII);
  bool tryEliminateRedundantTST(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator &II);
  bool tryFoldReload(MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator &II,
                     const TargetInstrInfo *TII);
  bool tryPushAddressCombine(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator &II,
                             const TargetInstrInfo *TII);
  bool tryPushPairCombine(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator &II,
                          const TargetInstrInfo *TII);
  bool tryConvertAddToMOVA(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator &II,
                           const TargetInstrInfo *TII);
  bool tryConvertImmLoadAddToMOVA(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator &II,
                                   const TargetInstrInfo *TII);
  bool tryShortenImmediate(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator &II,
                           const TargetInstrInfo *TII);
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

/// Return true if the given opcode is known to set PSW condition codes (N, Z,
/// V, C) even though its TableGen definition does not include Defs=[PSW].
/// On VAX, nearly all data-manipulation instructions set condition codes.
/// MOVL_rr, MOVL_rm, MOVL_mr, and MOVL_mi are intentionally defined without
/// Defs=[PSW] to avoid interfering with the register allocator's dead-def
/// elimination (see VAXInstrInfo.td for rationale).  The peephole uses this
/// helper to recognise the flag-setting behaviour for redundant-TST
/// elimination.
static bool inherentlySetsPSW(unsigned Opc) {
  switch (Opc) {
  // MOVL variants
  case VAX::MOVL_rr:
  case VAX::MOVL_rm:
  case VAX::MOVL_mr:
  case VAX::MOVL_mi:
  case VAX::MOVL_mm:
  // MOVB truncating store variants
  case VAX::MOVB_mm:
  case VAX::MOVB_mr:
  case VAX::MOVB_mg:
  case VAX::MOVBstore:
  case VAX::MOVBstore_i:
  case VAX::MOVBstore_ga:
  // MOVW truncating store variants
  case VAX::MOVW_mm:
  case VAX::MOVW_mr:
  case VAX::MOVW_mg:
  case VAX::MOVWstore:
  case VAX::MOVWstore_i:
  case VAX::MOVWstore_ga:
  case VAX::MOVQ_mm:
  // MOVZBL/MOVZWL (zero-extend loads)
  case VAX::MOVZBL_rm:
  case VAX::MOVZBL_rm_e:
  case VAX::MOVZBL_rg:
  case VAX::MOVZBL_rg_e:
  case VAX::MOVZBL_rr:
  case VAX::MOVZWL_rm:
  case VAX::MOVZWL_rm_e:
  case VAX::MOVZWL_rg:
  case VAX::MOVZWL_rg_e:
  case VAX::MOVZWL_rr:
  // CVTBL/CVTWL (sign-extend conversions)
  case VAX::CVTBL_rm:
  case VAX::CVTBL_rg:
  case VAX::CVTBL_rr:
  case VAX::CVTBL_rr_ext:
  case VAX::CVTWL_rm:
  case VAX::CVTWL_rg:
  case VAX::CVTWL_rr:
  case VAX::CVTWL_rr_ext:
  // CVTBW/MOVZBW (byte to word)
  case VAX::CVTBW_rr:
  case VAX::MOVZBW_rr:
  // Native byte/word loads and copies
  case VAX::MOVBload:
  case VAX::MOVBload_ga:
  case VAX::MOVBrr:
  case VAX::MOVBri:
  case VAX::MOVWload:
  case VAX::MOVWload_ga:
  case VAX::MOVWrr:
  case VAX::MOVWri:
    return true;
  default:
    return false;
  }
}

/// Check if MI defines the given physical register as an explicit def
/// (i.e. it writes a result to that register, setting N/Z flags on it).
/// We exclude CMP/TST instructions since they set PSW but don't write a GPR.
static bool definesRegWithFlags(const MachineInstr &MI, Register Reg) {
  // Must define PSW (set condition codes).
  if (!MI.modifiesRegister(VAX::PSW, /*TRI=*/nullptr) &&
      !inherentlySetsPSW(MI.getOpcode()))
    return false;

  // Check explicit defs — the result register(s) of the instruction.
  for (unsigned i = 0, e = MI.getNumExplicitDefs(); i < e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (MO.isReg() && MO.isDef() && MO.getReg() == Reg)
      return true;
  }
  return false;
}

/// Check if MI sets N/Z flags based on Reg's value (as a source, not
/// destination). This handles the spill pattern: MOVL %rX, disp(%fp) sets
/// N/Z from %rX without modifying %rX — a subsequent TST of %rX is redundant.
/// Only matches longword MOV/PUSH for TSTL; extend for TSTB/TSTW as needed.
static bool setsFlagsFromSource(const MachineInstr &MI, Register Reg,
                                unsigned TstOpc) {
  unsigned Opc = MI.getOpcode();

  if (!MI.modifiesRegister(VAX::PSW, /*TRI=*/nullptr) &&
      !inherentlySetsPSW(Opc))
    return false;

  Register SrcReg;
  switch (TstOpc) {
  case VAX::TSTL:
    if (Opc == VAX::MOVL_mr)
      SrcReg = MI.getOperand(0).getReg(); // (ins GPRnoPC:$src, VAXMemOp:$dst)
    else if (Opc == VAX::MOVL_rr)
      SrcReg = MI.getOperand(1).getReg(); // (outs $dst), (ins $src)
    else if (Opc == VAX::PUSHL_r)
      SrcReg = MI.getOperand(0).getReg(); // (ins GPRnoPC:$src)
    else
      return false;
    break;
  default:
    return false;
  }

  return SrcReg == Reg;
}

/// Try to eliminate a redundant TSTL/TSTB/TSTW/TSTF/TSTD by checking if the
/// immediately preceding (non-debug) instruction already set N/Z on the same
/// register. Within a basic block, no other path can enter between instructions.
/// Two cases:
///  1. Instruction defines TstReg and sets PSW (e.g., ADDL2 → %rX; TSTL %rX)
///  2. Instruction uses TstReg as source and sets PSW from it
///     (e.g., MOVL %rX, disp(%fp) [spill]; TSTL %rX)
///
/// NOTE: We intentionally do NOT scan past real instructions. On VAX, nearly
/// all instructions set PSW, but many instruction definitions are missing
/// Defs=[PSW] in TableGen. Scanning past such instructions would incorrectly
/// assume flags are preserved. Until all instruction definitions are audited
/// for PSW, we limit elimination to the immediately preceding instruction.
bool VAXPeephole::tryEliminateRedundantTST(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator &II) {
  MachineInstr &MI = *II;
  Register TstReg = getTSTReg(MI);
  if (!TstReg.isValid())
    return false;

  unsigned TstOpc = MI.getOpcode();

  // Walk backward skipping debug/CFI to find the preceding real instruction.
  auto Prev = II;
  while (Prev != MBB.begin()) {
    --Prev;
    if (Prev->isDebugInstr() || Prev->isCFIInstruction())
      continue;

    // Case 1: Instruction defines TstReg AND sets PSW → TST is redundant.
    if (definesRegWithFlags(*Prev, TstReg)) {
      LLVM_DEBUG(dbgs() << "VAXPeephole: eliminating redundant " << MI
                        << "  (flags set by def: " << *Prev << ")\n");
      auto EraseIt = II++;
      EraseIt->eraseFromParent();
      return true;
    }

    // Case 2: Instruction uses TstReg as source and sets N/Z from it
    // (e.g., MOVL %rX, mem [spill]) → TST is redundant.
    // MOVL sets N/Z identically to TSTL, V←0 (same). Only C differs
    // (MOVL preserves C, TSTL clears C) — benign since no sane code reads C
    // after TST.
    if (setsFlagsFromSource(*Prev, TstReg, TstOpc)) {
      LLVM_DEBUG(dbgs() << "VAXPeephole: eliminating redundant " << MI
                        << "  (flags set by src: " << *Prev << ")\n");
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

/// Copy the 4-slot VAXMemOp (base, disp, index, flags) from a source
/// instruction starting at operand MemStart into a MachineInstrBuilder.
static void addMemOps(MachineInstrBuilder &MIB, const MachineInstr &Src,
                      unsigned MemStart) {
  MIB.addReg(Src.getOperand(MemStart).getReg());
  MIB.add(Src.getOperand(MemStart + 1)); // disp: may be imm or global
  MIB.addReg(Src.getOperand(MemStart + 2).getReg());
  MIB.addImm(Src.getOperand(MemStart + 3).getImm());
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
    // MOVQ reads the quadword pair: mark high register as implicit use.
    MIB.addReg(SrcHi, RegState::Implicit);

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
    // MOVQ writes the quadword pair: mark high register as implicit def.
    MIB->addRegisterDefined(DstHi);

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

/// Try to fold a MOVL_rm (reload from stack) into the next instruction that
/// uses the loaded register. VAX instructions natively support memory source
/// operands, so we can eliminate the reload and use memory directly.
///
/// Examples:
///   MOVL_rm + PUSHL_r → PUSHL_m
///   MOVL_rm + MOVL_rr → MOVL_rm (direct load into different register)
///   MOVL_rm + ADDL2_rr → ADDL2_rm
///   MOVL_rm + ADDL3_rr → ADDL3_rm
bool VAXPeephole::tryFoldReload(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator &II,
                                const TargetInstrInfo *TII) {
  MachineInstr &Reload = *II;
  if (Reload.getOpcode() != VAX::MOVL_rm)
    return false;

  // MOVL_rm: [0]=dst(def), [1]=base, [2]=disp, [3]=idx, [4]=flags
  Register RR = Reload.getOperand(0).getReg();
  // Memory operand starts at index 1.
  constexpr unsigned MemStart = 1;

  // Find the next real instruction.
  auto Next = std::next(II);
  while (Next != MBB.end() && (Next->isDebugInstr() || Next->isCFIInstruction()))
    ++Next;
  if (Next == MBB.end())
    return false;

  MachineInstr &Use = *Next;
  unsigned UseOpc = Use.getOpcode();

  // --- Pattern 1: PUSHL_r → PUSHL_m ---
  if (UseOpc == VAX::PUSHL_r &&
      Use.getOperand(0).getReg() == RR &&
      Use.getOperand(0).isKill()) {
    MachineInstrBuilder MIB =
        BuildMI(MBB, II, Use.getDebugLoc(), TII->get(VAX::PUSHL_m));
    addMemOps(MIB, Reload, MemStart);
    MIB.addReg(VAX::SP, RegState::ImplicitDefine);
    MIB.cloneMemRefs(Reload);
    LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL_rm+PUSHL_r→PUSHL_m "
                      << Reload << "  + " << Use);
    auto Erase1 = II;
    II = std::next(Next);
    Erase1->eraseFromParent();
    Next->eraseFromParent();
    return true;
  }

  // --- Pattern 2: MOVL_rr → MOVL_rm (direct load into different register) ---
  // MOVL_rr: [0]=dst(def), [1]=src
  if (UseOpc == VAX::MOVL_rr &&
      Use.getOperand(1).getReg() == RR &&
      Use.getOperand(1).isKill()) {
    Register NewDst = Use.getOperand(0).getReg();
    MachineInstrBuilder MIB =
        BuildMI(MBB, II, Use.getDebugLoc(), TII->get(VAX::MOVL_rm), NewDst);
    addMemOps(MIB, Reload, MemStart);
    MIB.cloneMemRefs(Reload);
    LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL_rm+MOVL_rr→MOVL_rm "
                      << Reload << "  + " << Use);
    auto Erase1 = II;
    II = std::next(Next);
    Erase1->eraseFromParent();
    Next->eraseFromParent();
    return true;
  }

  // --- Pattern 3: ALU2_rr → ALU2_rm ---
  // ALU2_rr: [0]=dst(def), [1]=src, [2]=src2(tied to dst)
  // ALU2_rm: [0]=dst(def), [1..4]=VAXMemOp, [5]=src2(tied to dst)
  // Guard: dst != RR (tied constraint means dst is also read; if dst == RR,
  // the fold changes semantics because the tied read sees the old value).
  struct Alu2Fold {
    unsigned FromOpc, ToOpc;
  };
  static const Alu2Fold Alu2Folds[] = {
    {VAX::ADDL2_rr, VAX::ADDL2_rm},
    {VAX::SUBL2_rr, VAX::SUBL2_rm},
    {VAX::MULL2_rr, VAX::MULL2_rm},
    {VAX::DIVL2_rr, VAX::DIVL2_rm},
    {VAX::BISL2_rr, VAX::BISL2_rm},
    {VAX::BICL2_rr, VAX::BICL2_rm},
    {VAX::XORL2_rr, VAX::XORL2_rm},
  };
  for (const auto &F : Alu2Folds) {
    if (UseOpc == F.FromOpc &&
        Use.getOperand(1).getReg() == RR &&
        Use.getOperand(1).isKill() &&
        Use.getOperand(0).getReg() != RR) {
      Register DstReg = Use.getOperand(0).getReg();
      MachineInstrBuilder MIB =
          BuildMI(MBB, II, Use.getDebugLoc(), TII->get(F.ToOpc));
      MIB.addDef(DstReg);
      addMemOps(MIB, Reload, MemStart);
      MIB.addReg(DstReg); // tied src2
      MIB.cloneMemRefs(Reload);
      LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL_rm+ALU2_rr→ALU2_rm "
                        << Reload << "  + " << Use);
      auto Erase1 = II;
      II = std::next(Next);
      Erase1->eraseFromParent();
      Next->eraseFromParent();
      return true;
    }
  }

  // --- Pattern 4: ALU3_rr → ALU3_rm ---
  // ALU3_rr: [0]=dst, [1]=src1, [2]=src2
  // ALU3_rm: [0]=dst, [1..4]=VAXMemOp(src1), [5]=src2
  // NOTE: Only instructions whose _rm form has VAXMemOp as the FIRST input.
  // BICL3_rm has (ins GPRnoPC:$mask, VAXMemOp:$src) — different layout — excluded.
  struct Alu3Fold {
    unsigned FromOpc, ToOpc;
    bool Commutative;
  };
  static const Alu3Fold Alu3Folds[] = {
    {VAX::ADDL3_rr, VAX::ADDL3_rm, true},
    {VAX::SUBL3_rr, VAX::SUBL3_rm, false},
  };
  for (const auto &F : Alu3Folds) {
    if (UseOpc != F.FromOpc)
      continue;
    Register Dst = Use.getOperand(0).getReg();
    Register Src1 = Use.getOperand(1).getReg();
    Register Src2 = Use.getOperand(2).getReg();

    // Fold when src1 is the reloaded register.
    // Skip if src2 also uses the same register.
    if (Src1 == RR && Use.getOperand(1).isKill() && Src2 != RR) {
      MachineInstrBuilder MIB =
          BuildMI(MBB, II, Use.getDebugLoc(), TII->get(F.ToOpc));
      MIB.addDef(Dst);
      addMemOps(MIB, Reload, MemStart);
      MIB.addReg(Src2);
      MIB.cloneMemRefs(Reload);
      LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL_rm+ALU3_rr→ALU3_rm "
                        << Reload << "  + " << Use);
      auto Erase1 = II;
      II = std::next(Next);
      Erase1->eraseFromParent();
      Next->eraseFromParent();
      return true;
    }

    // For commutative ops: fold when src2 is the reloaded register.
    // Skip if src1 also uses the same register.
    if (F.Commutative && Src2 == RR && Use.getOperand(2).isKill() &&
        Src1 != RR) {
      MachineInstrBuilder MIB =
          BuildMI(MBB, II, Use.getDebugLoc(), TII->get(F.ToOpc));
      MIB.addDef(Dst);
      addMemOps(MIB, Reload, MemStart);
      MIB.addReg(Src1); // swapped: was src1, now occupies src2 position
      MIB.cloneMemRefs(Reload);
      LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL_rm+ALU3_rr→ALU3_rm(swap) "
                        << Reload << "  + " << Use);
      auto Erase1 = II;
      II = std::next(Next);
      Erase1->eraseFromParent();
      Next->eraseFromParent();
      return true;
    }
  }

  // --- Pattern 5: CMPL_rr → CMPL_rm ---
  // CMPL_rr: [0]=$a, [1]=$b; CMPL_rm: [0..3]=VAXMemOp_a, [4]=$b
  // Only fold when the reload feeds the first operand ($a).
  // Skip if both operands are the same register (self-compare) — folding
  // would eliminate the only def of that register.
  if (UseOpc == VAX::CMPL_rr &&
      Use.getOperand(0).getReg() == RR &&
      Use.getOperand(0).isKill()) {
    Register OtherReg = Use.getOperand(1).getReg();
    if (OtherReg == RR)
      return false;
    MachineInstrBuilder MIB =
        BuildMI(MBB, II, Use.getDebugLoc(), TII->get(VAX::CMPL_rm));
    addMemOps(MIB, Reload, MemStart);
    MIB.addReg(OtherReg);
    MIB.cloneMemRefs(Reload);
    LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL_rm+CMPL_rr→CMPL_rm "
                      << Reload << "  + " << Use);
    auto Erase1 = II;
    II = std::next(Next);
    Erase1->eraseFromParent();
    Next->eraseFromParent();
    return true;
  }

  // --- Pattern 6: CMP_BRANCH_rr → CMP_BRANCH_rm ---
  // CMP_BRANCH_rr: [0]=$lhs, [1]=$rhs, [2]=$cc, [3]=$dst
  // CMP_BRANCH_rm: [0..3]=VAXMemOp_lhs, [4]=$rhs, [5]=$cc, [6]=$dst
  // Only fold when the reload feeds $lhs (first operand).
  // Skip if both operands are the same register (self-compare).
  if (UseOpc == VAX::CMP_BRANCH_rr &&
      Use.getOperand(0).getReg() == RR &&
      Use.getOperand(0).isKill()) {
    Register RhsReg = Use.getOperand(1).getReg();
    if (RhsReg == RR)
      return false;
    int64_t CC = Use.getOperand(2).getImm();
    MachineBasicBlock *Target = Use.getOperand(3).getMBB();
    MachineInstrBuilder MIB =
        BuildMI(MBB, II, Use.getDebugLoc(), TII->get(VAX::CMP_BRANCH_rm));
    addMemOps(MIB, Reload, MemStart);
    MIB.addReg(RhsReg).addImm(CC).addMBB(Target);
    MIB.cloneMemRefs(Reload);
    LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL_rm+CMP_BRANCH_rr→CMP_BRANCH_rm "
                      << Reload << "  + " << Use);
    auto Erase1 = II;
    II = std::next(Next);
    Erase1->eraseFromParent();
    Next->eraseFromParent();
    return true;
  }

  return false;
}

/// Combine consecutive PUSHL_r of adjacent registers into MOVQ reg, -(SP).
/// Pattern: PUSHL_r $rN+1   (pushed first → higher address)
///          PUSHL_r $rN     (pushed second → lower address)
/// Result:  MOVQ $rN, -(SP)  (decrements SP by 8, stores rN:rN+1)
bool VAXPeephole::tryPushPairCombine(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator &II,
                                     const TargetInstrInfo *TII) {
  MachineInstr &MI = *II;
  if (MI.getOpcode() != VAX::PUSHL_r)
    return false;

  // Look ahead for a second PUSHL_r.
  auto Next = std::next(II);
  while (Next != MBB.end() && (Next->isDebugInstr() || Next->isCFIInstruction()))
    ++Next;
  if (Next == MBB.end() || Next->getOpcode() != VAX::PUSHL_r)
    return false;

  Register HiReg = MI.getOperand(0).getReg();   // first push = higher address
  Register LoReg = Next->getOperand(0).getReg(); // second push = lower address

  // Must be consecutive: HiReg == LoReg + 1 (i.e., rN+1 then rN).
  if (HiReg != LoReg + 1)
    return false;

  // Only combine R0-R11 pairs (not AP/FP/SP).
  if (LoReg < VAX::R0 || LoReg > VAX::R10)
    return false;

  // Build MOVQ $rN, -(%sp)
  // MOVQ: (outs), (ins VAXMemOp:$src, VAXMemOp:$dst)
  // Source: register-direct (LoReg)
  // Destination: autodecrement (SP)
  MachineInstrBuilder MIB =
      BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::MOVQ));
  MIB.addReg(LoReg).addImm(0).addReg(0).addImm(VAXAM::RegDirect);
  MIB.addReg(VAX::SP).addImm(0).addReg(0).addImm(VAXAM::AutoDec);

  // MOVQ reads both LoReg and HiReg (quadword), and autodecrement modifies SP.
  // MOVQ also sets condition codes (N/Z/V/C) on VAX.
  MIB.addReg(HiReg, RegState::Implicit);
  MIB->addRegisterDefined(VAX::SP);
  MIB->addRegisterDefined(VAX::PSW);

  LLVM_DEBUG(dbgs() << "VAXPeephole: PUSHL+PUSHL→MOVQ " << MI << "  + "
                    << *Next);
  auto Erase1 = II;
  II = std::next(Next);
  Erase1->eraseFromParent();
  Next->eraseFromParent();
  return true;
}

/// Try to combine LEA_FI + PUSHL_r into PUSHAL.
/// Pattern: $rN = LEA_FI $base, disp, $noreg, flags
///          PUSHL_r killed $rN
/// Result:  PUSHAL $base, disp, $noreg, flags
bool VAXPeephole::tryPushAddressCombine(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator &II,
                                        const TargetInstrInfo *TII) {
  MachineInstr &MI = *II;
  if (MI.getOpcode() != VAX::LEA_FI)
    return false;

  // LEA_FI operands: (outs GPRnoPC:$dst), (ins VAXMemOp:$addr)
  // op0 = dst reg, op1 = base, op2 = disp, op3 = index, op4 = flags
  Register DstReg = MI.getOperand(0).getReg();

  // Look ahead for PUSHL_r of the same register.
  auto Next = std::next(II);
  while (Next != MBB.end() &&
         (Next->isDebugInstr() || Next->isCFIInstruction()))
    ++Next;
  if (Next == MBB.end())
    return false;

  if (Next->getOpcode() != VAX::PUSHL_r)
    return false;
  if (Next->getOperand(0).getReg() != DstReg)
    return false;

  // The LEA_FI defines DstReg. If DstReg is used after the PUSHL (i.e., it's
  // still live), we can't eliminate the LEA_FI — the PUSHAL won't produce the
  // register value. Check that DstReg is killed by the PUSHL.
  if (!Next->getOperand(0).isKill())
    return false;

  // Build PUSHAL with the same memory operand as LEA_FI.
  Register BaseReg = MI.getOperand(1).getReg();
  int64_t Disp = MI.getOperand(2).getImm();
  Register IdxReg = MI.getOperand(3).getReg();
  int64_t Flags = MI.getOperand(4).getImm();

  MachineInstrBuilder MIB =
      BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::PUSHAL));
  MIB.addReg(BaseReg).addImm(Disp).addReg(IdxReg).addImm(Flags);
  // PUSHAL implicitly defs SP and PSW.
  MIB.addReg(VAX::SP, RegState::ImplicitDefine);

  LLVM_DEBUG(dbgs() << "VAXPeephole: LEA_FI+PUSHL→PUSHAL " << MI << "  + "
                    << *Next);
  auto Erase1 = II;
  II = std::next(Next);
  Erase1->eraseFromParent();
  Next->eraseFromParent();
  return true;
}

/// Try to convert MOVL_ri + ADDL3_rm [+ PUSHL_r] into MOVL_rm + MOVAL/PUSHAL.
///
/// ISel often produces: $rT = MOVL_ri $imm; $rD = ADDL3_rm mem, $rT
/// when computing base+offset where the base comes from memory.
/// This can be converted to: $rD = MOVL_rm mem; MOVAL imm($rD), $rD
/// saving bytes (large imm) or: $rD = MOVL_rm mem; PUSHAL imm($rD) saving
/// an entire instruction when followed by PUSHL.
bool VAXPeephole::tryConvertImmLoadAddToMOVA(MachineBasicBlock &MBB,
                                              MachineBasicBlock::iterator &II,
                                              const TargetInstrInfo *TII) {
  MachineInstr &MI = *II;
  if (MI.getOpcode() != VAX::MOVL_ri)
    return false;

  Register ImmReg = MI.getOperand(0).getReg();
  int64_t Imm = MI.getOperand(1).getImm();

  // Find next real instruction — must be ADDL3_rm using ImmReg.
  auto Next = std::next(II);
  while (Next != MBB.end() &&
         (Next->isDebugInstr() || Next->isCFIInstruction()))
    ++Next;
  if (Next == MBB.end() || Next->getOpcode() != VAX::ADDL3_rm)
    return false;

  // ADDL3_rm layout: [0]=dst(def), [1..4]=memop, [5]=reg_src
  Register AddDst = Next->getOperand(0).getReg();
  constexpr unsigned Add3MemStart = 1;
  Register MemBase = Next->getOperand(1).getReg();
  Register MemIdx = Next->getOperand(3).getReg();
  MachineOperand &RegSrc = Next->getOperand(5);

  if (RegSrc.getReg() != ImmReg)
    return false;

  // ImmReg must be killed by ADDL3 (dead after), so removing MOVL_ri is safe.
  if (!RegSrc.isKill())
    return false;

  // The memory operand must not reference AddDst, because we'll load into
  // AddDst before the MOVAL reads it as displacement base. (If mem uses AddDst,
  // the load would clobber the base before address computation.)
  // Actually, MOVL_rm evaluates the source EA before writing dst, so mem
  // referencing AddDst is safe — it reads the old AddDst value.
  // But mem must NOT reference ImmReg, since we're removing the MOVL_ri that
  // defines it.
  if (MemBase == ImmReg || (MemIdx.isValid() && MemIdx == ImmReg))
    return false;

  // --- Check for trailing PUSHL_r (killed) → fold into PUSHAL ---
  auto AfterAdd = std::next(Next);
  while (AfterAdd != MBB.end() &&
         (AfterAdd->isDebugInstr() || AfterAdd->isCFIInstruction()))
    ++AfterAdd;

  bool HasPUSHL = (AfterAdd != MBB.end() &&
                   AfterAdd->getOpcode() == VAX::PUSHL_r &&
                   AfterAdd->getOperand(0).getReg() == AddDst &&
                   AfterAdd->getOperand(0).isKill());

  if (HasPUSHL) {
    // MOVL_ri + ADDL3_rm + PUSHL → MOVL_rm + PUSHAL (saves 1 instruction)
    MachineInstrBuilder Load =
        BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::MOVL_rm));
    Load.addDef(AddDst);
    addMemOps(Load, *Next, Add3MemStart);
    Load.cloneMemRefs(*Next);

    MachineInstrBuilder Push =
        BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::PUSHAL));
    Push.addReg(AddDst, RegState::Kill).addImm(Imm).addReg(0).addImm(VAXAM::Disp);
    Push.addReg(VAX::SP, RegState::ImplicitDefine);

    LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL_ri+ADDL3_rm+PUSHL→MOVL_rm+PUSHAL "
                      << MI << "  + " << *Next << "  + " << *AfterAdd);
    auto Erase1 = II;
    II = std::next(AfterAdd);
    Erase1->eraseFromParent();
    Next->eraseFromParent();
    AfterAdd->eraseFromParent();
    return true;
  }

  // Non-push case: only worthwhile when imm is outside short-literal range.
  // For 0–63, MOVL_ri uses a 1-byte literal (total MOVL_ri = 3 bytes) which
  // combined with ADDL3_rm is no worse than MOVL_rm + MOVAL.
  if (Imm >= 0 && Imm <= 63)
    return false;

  // MOVL_ri + ADDL3_rm → MOVL_rm + MOVAL
  MachineInstrBuilder Load =
      BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::MOVL_rm));
  Load.addDef(AddDst);
  addMemOps(Load, *Next, Add3MemStart);
  Load.cloneMemRefs(*Next);

  MachineInstrBuilder Mova =
      BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::MOVAL));
  // src: disp(AddDst)
  Mova.addReg(AddDst).addImm(Imm).addReg(0).addImm(VAXAM::Disp);
  // dst: register direct
  Mova.addReg(AddDst).addImm(0).addReg(0).addImm(VAXAM::RegDirect);

  LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL_ri+ADDL3_rm→MOVL_rm+MOVAL " << MI
                    << "  + " << *Next);
  auto Erase1 = II;
  II = std::next(Next);
  Erase1->eraseFromParent();
  Next->eraseFromParent();
  return true;
}

/// Try to convert ADDL3_ri to MOVAL (shorter encoding), and
/// ADDL3_ri + PUSHL_r to PUSHAL (eliminates one instruction).
///
/// MOVAL disp(%rSrc), %rDst encodes the displacement in the addressing-mode
/// specifier (1–4 bytes via byte/word/long displacement), while ADDL3 encodes
/// the immediate as a separate operand (1 byte for 0–63 literal, else 5 bytes
/// for a longword immediate). For values outside 0–63, MOVAL is shorter.
///
/// Additionally, ADDL3_ri + PUSHL_r (killed) can collapse to a single PUSHAL.
bool VAXPeephole::tryConvertAddToMOVA(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator &II,
                                      const TargetInstrInfo *TII) {
  MachineInstr &MI = *II;
  if (MI.getOpcode() != VAX::ADDL3_ri)
    return false;

  // ADDL3_ri layout: [0]=dst(reg), [1]=imm, [2]=src(reg)
  Register DstReg = MI.getOperand(0).getReg();
  int64_t Imm = MI.getOperand(1).getImm();
  Register SrcReg = MI.getOperand(2).getReg();

  // MOVAL only saves bytes when the immediate is outside VAX short-literal
  // range (0–63). For 0–63, ADDL3 uses a 1-byte literal specifier which is
  // the same size as or smaller than byte-displacement mode (2 bytes).
  // However, if we can fold into PUSHAL below, it's always a win (saves an
  // entire instruction), so check PUSHAL first.

  // --- Pattern 2: ADDL3_ri + PUSHL_r (killed) → PUSHAL ---
  auto Next = std::next(II);
  while (Next != MBB.end() &&
         (Next->isDebugInstr() || Next->isCFIInstruction()))
    ++Next;

  if (Next != MBB.end() && Next->getOpcode() == VAX::PUSHL_r &&
      Next->getOperand(0).getReg() == DstReg &&
      Next->getOperand(0).isKill()) {
    // DstReg is produced by ADDL3 and consumed+killed by PUSHL — safe to fold.
    MachineInstrBuilder MIB =
        BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::PUSHAL));
    MIB.addReg(SrcReg).addImm(Imm).addReg(0).addImm(VAXAM::Disp);
    MIB.addReg(VAX::SP, RegState::ImplicitDefine);

    LLVM_DEBUG(dbgs() << "VAXPeephole: ADDL3+PUSHL→PUSHAL " << MI << "  + "
                      << *Next);
    auto Erase1 = II;
    II = std::next(Next);
    Erase1->eraseFromParent();
    Next->eraseFromParent();
    return true;
  }

  // --- Pattern 1: ADDL3_ri → MOVAL (encoding size win) ---
  // Only convert when immediate is outside short-literal range.
  if (Imm >= 0 && Imm <= 63)
    return false;

  // Build: MOVAL Imm(SrcReg), DstReg
  // MOVAL takes two VAXMemOp operands (src, dst), each with 4 sub-operands:
  //   (base, disp, index, flags)
  // src = disp(SrcReg): base=SrcReg, disp=Imm, index=0, flags=Disp
  // dst = register mode: base=DstReg, disp=0, index=0, flags=RegDirect
  MachineInstrBuilder MIB =
      BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::MOVAL));
  // Source operand: displacement addressing
  MIB.addReg(SrcReg).addImm(Imm).addReg(0).addImm(VAXAM::Disp);
  // Destination operand: register direct
  MIB.addReg(DstReg).addImm(0).addReg(0).addImm(VAXAM::RegDirect);

  LLVM_DEBUG(dbgs() << "VAXPeephole: ADDL3_ri→MOVAL " << MI);
  auto EraseIt = II++;
  EraseIt->eraseFromParent();
  return true;
}

/// Try to combine non-adjacent MOVL pairs into MOVQ.
/// Scans forward from MI up to MaxScanDist real instructions, skipping safe
/// instructions including other MOVL spills/reloads through the same base
/// register with non-overlapping displacements.
bool VAXPeephole::tryQuadCombineNonAdjacent(MachineBasicBlock &MBB,
                                             MachineBasicBlock::iterator &II,
                                             const TargetInstrInfo *TII) {
  MachineInstr &MI = *II;
  unsigned Opc = MI.getOpcode();

  if (Opc != VAX::MOVL_mr && Opc != VAX::MOVL_rm)
    return false;

  int MemIdx = getMemOpIdx(MI);
  if (MemIdx < 0)
    return false;

  Register BaseReg = MI.getOperand(MemIdx).getReg();

  static constexpr unsigned MaxScanDist = 4;
  SmallVector<MachineInstr *, 4> Intervening;
  unsigned Dist = 0;

  for (auto Scan = std::next(II); Scan != MBB.end(); ++Scan) {
    if (Scan->isDebugInstr() || Scan->isCFIInstruction())
      continue;

    MachineInstr &MI2 = *Scan;

    // Try to match MI2 as a quad partner (same opcode, adjacent quadword).
    if (MI2.getOpcode() == Opc) {
      int MI2MemIdx = getMemOpIdx(MI2);
      if (MI2MemIdx >= 0) {
        int64_t LowDisp;
        if (isAdjacentQuadword(MI, MemIdx, MI2, MI2MemIdx, LowDisp)) {
          int64_t DispA = MI.getOperand(MemIdx + 1).getImm();
          const MachineInstr &LowMI = (DispA == LowDisp) ? MI : MI2;
          const MachineInstr &HighMI = (DispA == LowDisp) ? MI2 : MI;

          // Verify all intervening instructions are safe w.r.t. the quadword
          // memory range [LowDisp, LowDisp+8) and partner registers.
          bool AllSafe = true;
          for (const MachineInstr *I : Intervening) {
            // Check memory overlap for intervening memory instructions.
            if (I->mayLoad() || I->mayStore()) {
              int IMemIdx = getMemOpIdx(*I);
              if (IMemIdx < 0) { AllSafe = false; break; }
              int64_t IDisp = I->getOperand(IMemIdx + 1).getImm();
              // 4-byte access at IDisp must not overlap [LowDisp, LowDisp+8)
              if (IDisp + 4 > LowDisp && IDisp < LowDisp + 8) {
                AllSafe = false; break;
              }
            }
            // Partner register safety checks.
            if (Opc == VAX::MOVL_mr) {
              if (I->modifiesRegister(MI2.getOperand(0).getReg(),
                                      /*TRI=*/nullptr)) {
                AllSafe = false; break;
              }
            }
            if (Opc == VAX::MOVL_rm) {
              Register PDst = MI2.getOperand(0).getReg();
              if (I->modifiesRegister(PDst, /*TRI=*/nullptr) ||
                  I->readsRegister(PDst, /*TRI=*/nullptr)) {
                AllSafe = false; break;
              }
            }
          }
          if (!AllSafe)
            break;

          if (Opc == VAX::MOVL_mr) {
            Register SrcLo = LowMI.getOperand(0).getReg();
            Register SrcHi = HighMI.getOperand(0).getReg();
            if (SrcHi == SrcLo + 1) {
              Register IdxReg = MI.getOperand(MemIdx + 2).getReg();
              int64_t Flags = MI.getOperand(MemIdx + 3).getImm();
              MachineInstrBuilder MIB =
                  BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::MOVQ));
              MIB.addReg(SrcLo).addImm(0).addReg(0).addImm(VAXAM::RegDirect);
              MIB.addReg(BaseReg).addImm(LowDisp).addReg(IdxReg).addImm(Flags);
              MIB.addReg(SrcHi, RegState::Implicit);
              LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL+MOVL→MOVQ(store,nonadj) "
                                << MI << "  + " << MI2);
              auto Erase1 = II;
              II = std::next(MachineBasicBlock::iterator(Scan));
              Erase1->eraseFromParent();
              Scan->eraseFromParent();
              return true;
            }
          }

          if (Opc == VAX::MOVL_rm) {
            Register DstLo = LowMI.getOperand(0).getReg();
            Register DstHi = HighMI.getOperand(0).getReg();
            if (DstHi == DstLo + 1 &&
                MI.getOperand(0).getReg() != BaseReg) {
              Register IdxReg = MI.getOperand(MemIdx + 2).getReg();
              int64_t Flags = MI.getOperand(MemIdx + 3).getImm();
              MachineInstrBuilder MIB =
                  BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::MOVQ));
              MIB.addReg(BaseReg).addImm(LowDisp).addReg(IdxReg).addImm(Flags);
              MIB.addReg(DstLo).addImm(0).addReg(0).addImm(VAXAM::RegDirect);
              MIB->addRegisterDefined(DstHi);
              LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL+MOVL→MOVQ(load,nonadj) "
                                << MI << "  + " << MI2);
              auto Erase1 = II;
              II = std::next(MachineBasicBlock::iterator(Scan));
              Erase1->eraseFromParent();
              Scan->eraseFromParent();
              return true;
            }
          }
        }
      }
      // MI2 is a MOVL that didn't form a pair — it accesses memory, stop.
      break;
    }

    // Not a matching opcode — check if safe to skip.
    if (Dist >= MaxScanDist)
      break;
    if (MI2.isCall() || MI2.isBranch() || MI2.isTerminator())
      break;
    if (MI2.hasUnmodeledSideEffects())
      break;
    if (MI2.modifiesRegister(BaseReg, /*TRI=*/nullptr))
      break;

    // Allow memory instructions through the same base register (displacement
    // overlap checked when partner is found). Reject unknown memory formats.
    if (MI2.mayLoad() || MI2.mayStore()) {
      int MI2MemIdx = getMemOpIdx(MI2);
      if (MI2MemIdx < 0)
        break; // Unknown memory instruction format
      const MachineOperand &MI2Base = MI2.getOperand(MI2MemIdx);
      const MachineOperand &MI2Disp = MI2.getOperand(MI2MemIdx + 1);
      const MachineOperand &MI2Idx = MI2.getOperand(MI2MemIdx + 2);
      if (!MI2Base.isReg() || MI2Base.getReg() != BaseReg)
        break; // Different base — can't prove non-aliasing
      if (!MI2Disp.isImm())
        break; // Non-immediate displacement
      if (MI2Idx.getReg() != 0)
        break; // Indexed mode
    }

    Intervening.push_back(&MI2);
    ++Dist;
  }

  return false;
}

/// Try to shorten immediate loads and pushes using shorter instruction forms.
///
/// Patterns:
///  1. MOVL_ri $C, $rN  →  ASHL_ii $shift, $base, $rN
///     when C = base << shift, base in [1,63], shift > 0, and C > 63.
///     Saves 3 bytes (7 → 4).
///
///  2. MOVL_ri $C, $rN  →  MNEGL_i $(-C), $rN
///     when C is negative (as i32) and -C in [1,63].
///     Saves 4 bytes (7 → 3).
///
///  3. PUSHL_i $C  →  ASHL_iip $shift, $base
///     when C = base << shift, base in [1,63], shift > 0, and C > 63.
///     Saves 2 bytes (6 → 4).
///
///  4. PUSHL_i $C  →  MNEGL_ip $(-C)
///     when C is negative (as i32) and -C in [1,63].
///     Saves 3 bytes (6 → 3).
bool VAXPeephole::tryShortenImmediate(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator &II,
                                      const TargetInstrInfo *TII) {
  MachineInstr &MI = *II;
  unsigned Opc = MI.getOpcode();

  bool IsMov = (Opc == VAX::MOVL_ri);
  bool IsPush = (Opc == VAX::PUSHL_i);
  if (!IsMov && !IsPush)
    return false;

  unsigned ImmOpIdx = IsMov ? 1 : 0;
  if (!MI.getOperand(ImmOpIdx).isImm())
    return false;

  int64_t Imm = MI.getOperand(ImmOpIdx).getImm();

  // Truncate to 32 bits (VAX is 32-bit).
  int32_t Imm32 = static_cast<int32_t>(Imm);
  uint32_t UImm = static_cast<uint32_t>(Imm32);

  // Values 0–63 already use VAX literal mode (1-byte operand) — no savings.
  if (UImm <= 63)
    return false;

  // Try MNEGL: if the value is negative and its absolute value fits in [1,63].
  // -C must be positive and ≤ 63 for literal mode savings.
  // Note: INT32_MIN (-2147483648) cannot be negated in i32, skip it.
  if (Imm32 < 0 && Imm32 != INT32_MIN) {
    int32_t NegImm = -Imm32;
    if (NegImm >= 1 && NegImm <= 63) {
      if (IsMov) {
        // MOVL_ri $(-N), $rN → MNEGL_i $N, $rN  (saves 4 bytes: 7 → 3)
        Register DstReg = MI.getOperand(0).getReg();
        BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::MNEGL_i))
            .addDef(DstReg)
            .addImm(NegImm);
        LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL_ri→MNEGL_i " << MI);
      } else {
        // PUSHL_i $(-N) → MNEGL_ip $N  (saves 3 bytes: 6 → 3)
        MachineInstrBuilder MIB =
            BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::MNEGL_ip))
                .addImm(NegImm);
        MIB->addRegisterDefined(VAX::SP);
        LLVM_DEBUG(dbgs() << "VAXPeephole: PUSHL_i→MNEGL_ip " << MI);
      }
      auto Erase = II++;
      Erase->eraseFromParent();
      return true;
    }
  }

  // Try ASHL: if UImm = base << shift, base in [1,63], shift > 0.
  unsigned Shift = llvm::countr_zero(UImm);
  if (Shift == 0)
    return false;

  uint32_t Base = UImm >> Shift;
  if (Base < 1 || Base > 63)
    return false;

  if (IsMov) {
    // MOVL_ri $C, $rN → ASHL_ii $shift, $base, $rN  (saves 3 bytes: 7 → 4)
    Register DstReg = MI.getOperand(0).getReg();
    BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::ASHL_ii))
        .addDef(DstReg)
        .addImm(Shift)
        .addImm(Base);
    LLVM_DEBUG(dbgs() << "VAXPeephole: MOVL_ri→ASHL_ii " << MI);
  } else {
    // PUSHL_i $C → ASHL_iip $shift, $base  (saves 2 bytes: 6 → 4)
    MachineInstrBuilder MIB =
        BuildMI(MBB, II, MI.getDebugLoc(), TII->get(VAX::ASHL_iip))
            .addImm(Shift)
            .addImm(Base);
    MIB->addRegisterDefined(VAX::SP);
    LLVM_DEBUG(dbgs() << "VAXPeephole: PUSHL_i→ASHL_iip " << MI);
  }
  auto Erase = II++;
  Erase->eraseFromParent();
  return true;
}

/// Combine DECL+Bcc → SOBxxx or INCL+CMPL+Bcc → AOBxxx.
///
/// Pattern 1 (SOB): DECL $reg + BGTR $target → SOBGTR $reg, $target
///                   DECL $reg + BGEQ $target → SOBGEQ $reg, $target
///
/// Pattern 2 (AOB): INCL $reg + CMPL $reg, $limit + BLSS $target
///                                            → AOBLSS $limit, $reg, $target
///                   INCL $reg + CMPL $reg, $limit + BLEQ $target
///                                            → AOBLEQ $limit, $reg, $target
///
/// These patterns appear at loop backedges after register allocation.
/// The SOB/AOB instructions atomically modify the index and branch,
/// saving 1–2 instructions per loop iteration.
///
/// This runs AFTER branch relaxation. At this point, any Bcc that survived
/// relaxation (wasn't converted to inverted+BRW) fits in a byte displacement.
/// SOB/AOB also use byte displacement (.bb) but replace a LARGER instruction
/// sequence, so the backward displacement can only get shorter.
static bool trySobAobCombine(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator &II,
                             const TargetInstrInfo *TII) {
  MachineInstr &BranchMI = *II;
  unsigned BrOpc = BranchMI.getOpcode();

  // Only handle conditional branches that SOB/AOB can replace.
  bool IsSOBCandidate = (BrOpc == VAX::BGTR || BrOpc == VAX::BGEQ);
  bool IsAOBCandidate = (BrOpc == VAX::BLSS || BrOpc == VAX::BLEQ);
  if (!IsSOBCandidate && !IsAOBCandidate)
    return false;

  // The branch must target a MBB operand.
  if (BranchMI.getNumOperands() < 1 || !BranchMI.getOperand(0).isMBB())
    return false;
  MachineBasicBlock *Target = BranchMI.getOperand(0).getMBB();

  // Walk backward past debug values to find the preceding instruction.
  auto PrevIt = II;
  if (PrevIt == MBB.begin())
    return false;
  --PrevIt;
  while (PrevIt != MBB.begin() && PrevIt->isDebugValue())
    --PrevIt;
  if (PrevIt->isDebugValue())
    return false;

  // --- SOB patterns: DECL $reg + BGTR/BGEQ → SOBGTR/SOBGEQ ---
  if (IsSOBCandidate) {
    MachineInstr &PrevMI = *PrevIt;
    if (PrevMI.getOpcode() != VAX::DECL)
      return false;

    // DECL sets PSW based on the result. BGTR/BGEQ tests PSW.
    // The branch must not have any intervening PSW-setting instruction.
    Register IndexReg = PrevMI.getOperand(0).getReg();

    unsigned NewOpc = (BrOpc == VAX::BGTR) ? VAX::SOBGTR : VAX::SOBGEQ;

    // Build: SOBGTR/SOBGEQ (index as VAXMemOp reg-direct), target
    MachineInstrBuilder MIB =
        BuildMI(MBB, II, BranchMI.getDebugLoc(), TII->get(NewOpc));
    // index operand: VAXMemOp in register-direct mode
    MIB.addReg(IndexReg).addImm(0).addReg(0).addImm(VAXAM::RegDirect);
    MIB.addMBB(Target);

    LLVM_DEBUG(dbgs() << "VAXPeephole: DECL+B"
                      << ((BrOpc == VAX::BGTR) ? "GTR" : "GEQ")
                      << "→SOB " << PrevMI << "  " << BranchMI);

    // Erase both DECL and branch. Advance II past the branch first.
    auto BrErase = II++;
    BrErase->eraseFromParent();
    PrevIt->eraseFromParent();
    return true;
  }

  // --- AOB patterns: INCL $reg + CMPL $reg, $limit + BLSS/BLEQ ---
  // Need two preceding instructions: CMPL then INCL.
  MachineInstr &CmpMI = *PrevIt;

  // The CMPL can be _rr (reg, reg) or _ri (reg, imm).
  unsigned CmpOpc = CmpMI.getOpcode();
  if (CmpOpc != VAX::CMPL_rr && CmpOpc != VAX::CMPL_ri)
    return false;

  // Walk backward past the CMP to find INCL.
  auto IncIt = PrevIt;
  if (IncIt == MBB.begin())
    return false;
  --IncIt;
  while (IncIt != MBB.begin() && IncIt->isDebugValue())
    --IncIt;
  if (IncIt->isDebugValue())
    return false;

  MachineInstr &IncMI = *IncIt;
  if (IncMI.getOpcode() != VAX::INCL)
    return false;

  Register IndexReg = IncMI.getOperand(0).getReg();

  // CMPL first operand must be the same register as INCL's result.
  if (!CmpMI.getOperand(0).isReg() || CmpMI.getOperand(0).getReg() != IndexReg)
    return false;

  // The limit is the second operand of CMPL.
  MachineOperand &LimitOp = CmpMI.getOperand(1);

  unsigned NewOpc = (BrOpc == VAX::BLSS) ? VAX::AOBLSS : VAX::AOBLEQ;

  // Build: AOBLSS/AOBLEQ (limit as VAXMemOp), (index as VAXMemOp), target
  MachineInstrBuilder MIB =
      BuildMI(MBB, II, BranchMI.getDebugLoc(), TII->get(NewOpc));

  // limit operand: VAXMemOp encoding
  if (LimitOp.isReg()) {
    MIB.addReg(LimitOp.getReg()).addImm(0).addReg(0).addImm(VAXAM::RegDirect);
  } else if (LimitOp.isImm()) {
    MIB.addReg(0).addImm(LimitOp.getImm()).addReg(0).addImm(VAXAM::Imm);
  } else {
    return false; // Unexpected operand type
  }

  // index operand: VAXMemOp in register-direct mode
  MIB.addReg(IndexReg).addImm(0).addReg(0).addImm(VAXAM::RegDirect);
  MIB.addMBB(Target);

  LLVM_DEBUG(dbgs() << "VAXPeephole: INCL+CMPL+B"
                    << ((BrOpc == VAX::BLSS) ? "LSS" : "LEQ")
                    << "→AOB " << IncMI << "  " << CmpMI
                    << "  " << BranchMI);

  // Erase all three instructions.
  auto BrErase = II++;
  BrErase->eraseFromParent();
  PrevIt->eraseFromParent();
  IncIt->eraseFromParent();
  return true;
}

bool VAXPeephole::runOnMachineFunction(MachineFunction &MF) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (auto II = MBB.begin(), IE = MBB.end(); II != IE; /*below*/) {
      MachineInstr &MI = *II;

      // Try to fold a MOVL_rm (reload) into the next instruction.
      // Must run first: MOVL_rm only matches this pattern, and folding
      // may enable further optimizations (e.g., TST elimination on the result).
      bool Converted = tryFoldReload(MBB, II, TII);

      // Try MOVL_ri + ADDL3_rm [+ PUSHL] → MOVL_rm + MOVAL/PUSHAL
      // Must run before 3→2 conversion to see the original ADDL3_rm.
      if (!Converted)
        Converted = tryConvertImmLoadAddToMOVA(MBB, II, TII);

      // Try 3-op → 2-op conversion
      if (!Converted)
      for (const auto &Entry : Alu3To2Table) {
        if (MI.getOpcode() != Entry.Opc3)
          continue;

        MachineOperand &Dst = MI.getOperand(0);
        if (!Dst.isReg())
          break;

        Register DstReg = Dst.getReg();

        // Detect _rm forms: operand[1] is the start of a VAXMemOp (4 sub-ops),
        // operand[5] is the register source. For _rr/_ri: operand layout is
        // [0]=dst, [1]=src1, [2]=src2.
        bool IsRM = MI.getNumOperands() > 5 && MI.getOperand(1).isReg() &&
                    MI.getOperand(5).isReg();

        if (IsRM) {
          // _rm layout: [0]=dst, [1..4]=memop, [5]=reg_src
          // Convert to _rm 2-op: [0]=dst, [1..4]=memop, [5]=tied_src
          MachineOperand &RegSrc = MI.getOperand(5);

          if (RegSrc.getReg() == DstReg) {
            // dst == reg_src: opl3 mem, reg, reg → opl2 mem, reg
            MachineInstrBuilder MIB =
                BuildMI(MBB, II, MI.getDebugLoc(), TII->get(Entry.Opc2));
            MIB.addDef(DstReg);
            // Copy the 4 memop sub-operands.
            for (unsigned i = 1; i <= 4; ++i)
              MIB.add(MI.getOperand(i));
            MIB.addReg(DstReg);
            // Preserve memory operand info.
            MIB.cloneMemRefs(MI);
            LLVM_DEBUG(dbgs() << "VAXPeephole: 3op→2op(rm) " << MI);
            auto EraseIt = II++;
            EraseIt->eraseFromParent();
            Changed = true;
            Converted = true;
            break;
          }
          // For commutative ops, dst == mem_src doesn't apply (dst is a reg,
          // mem_src is an address — they can't match).
        } else {
          // _rr/_ri layout: [0]=dst, [1]=src1, [2]=src2
          MachineOperand &Src1 = MI.getOperand(1);
          MachineOperand &Src2 = MI.getOperand(2);

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
        Converted = tryConvertAddToMOVA(MBB, II, TII);

      if (!Converted)
        Converted = tryPushAddressCombine(MBB, II, TII);

      if (!Converted)
        Converted = tryEliminateRedundantTST(MBB, II);

      if (!Converted)
        Converted = tryQuadCombine(MBB, II, TII);

      if (!Converted)
        Converted = tryQuadCombineNonAdjacent(MBB, II, TII);

      if (!Converted)
        Converted = tryPushPairCombine(MBB, II, TII);

      if (!Converted)
        Converted = tryShortenImmediate(MBB, II, TII);

      if (!Converted)
        ++II;
    }
  }
  return Changed;
}

} // end anonymous namespace

INITIALIZE_PASS(VAXPeephole, DEBUG_TYPE, "VAX Peephole", false, false)

FunctionPass *llvm::createVAXPeepholePass() { return new VAXPeephole(); }

//===----------------------------------------------------------------------===//
// VAXSobAobCombine — post-branch-relaxation SOB/AOB loop combine
//
// Runs AFTER branch relaxation so that:
// 1. Conditional branches that survived relaxation are known to fit in .bb
// 2. SOB/AOB (which also use .bb) replace a larger instruction sequence,
//    so the displacement can only get shorter — no risk of overflow
// 3. Branch relaxation doesn't need to know about SOB/AOB instructions
//===----------------------------------------------------------------------===//

#define SOB_AOB_DEBUG_TYPE "vax-sob-aob"

namespace {

class VAXSobAobCombine : public MachineFunctionPass {
public:
  static char ID;
  VAXSobAobCombine() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override { return "VAX SOB/AOB Loop Combine"; }
  bool runOnMachineFunction(MachineFunction &MF) override;
};

char VAXSobAobCombine::ID = 0;

bool VAXSobAobCombine::runOnMachineFunction(MachineFunction &MF) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (auto II = MBB.begin(), IE = MBB.end(); II != IE; /*below*/) {
      if (trySobAobCombine(MBB, II, TII))
        Changed = true;
      else
        ++II;
    }
  }
  return Changed;
}

} // end anonymous namespace

INITIALIZE_PASS(VAXSobAobCombine, SOB_AOB_DEBUG_TYPE,
                "VAX SOB/AOB Loop Combine", false, false)

FunctionPass *llvm::createVAXSobAobCombinePass() {
  return new VAXSobAobCombine();
}
