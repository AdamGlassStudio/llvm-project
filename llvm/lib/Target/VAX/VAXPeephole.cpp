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
//
//===----------------------------------------------------------------------===//

#include "VAX.h"
#include "VAXInstrInfo.h"
#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
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
};

char VAXPeephole::ID = 0;

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
        ++II;
    }
  }
  return Changed;
}

} // end anonymous namespace

INITIALIZE_PASS(VAXPeephole, DEBUG_TYPE, "VAX Peephole", false, false)

FunctionPass *llvm::createVAXPeepholePass() { return new VAXPeephole(); }
