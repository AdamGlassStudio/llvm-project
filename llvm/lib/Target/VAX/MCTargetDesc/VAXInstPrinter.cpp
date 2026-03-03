//===-- VAXInstPrinter.cpp - Convert VAX MCInst to assembly -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXInstPrinter.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/raw_ostream.h"
#include <climits>

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#define PRINT_ALIAS_INSTR
#include "VAXGenAsmWriter.inc"

void VAXInstPrinter::printRegName(raw_ostream &O, MCRegister Reg) {
  // VAX GAS uses bare register names: r0, r1, ..., ap, fp, sp, pc
  O << getRegisterName(Reg);
}

void VAXInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                               StringRef Annot, const MCSubtargetInfo &STI,
                               raw_ostream &O) {
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

void VAXInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                  raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    printRegName(O, Op.getReg());
    return;
  }
  if (Op.isImm()) {
    O << '$' << Op.getImm();
    return;
  }
  assert(Op.isExpr() && "Unknown operand type");
  MAI.printExpr(O, *Op.getExpr());
}

// Memory operand: OpNo+0 = base register, OpNo+1 = signed displacement.
// Prints as: disp(base), or (base) when displacement is 0.
void VAXInstPrinter::printMemOperand(const MCInst *MI, unsigned OpNo,
                                     raw_ostream &O) {
  const MCOperand &Base = MI->getOperand(OpNo);
  const MCOperand &Disp = MI->getOperand(OpNo + 1);

  // No base register: immediate mode ($value).
  if (Base.isReg() && Base.getReg() == 0) {
    O << '$';
    if (Disp.isImm())
      O << Disp.getImm();
    else if (Disp.isExpr())
      MAI.printExpr(O, *Disp.getExpr());
    else
      O << '0';
    return;
  }

  // Register direct (sentinel INT32_MIN): bare register.
  if (Disp.isImm() && Disp.getImm() == INT32_MIN) {
    assert(Base.isReg() && "Register direct must have a register base");
    printRegName(O, Base.getReg());
    return;
  }

  // Autodecrement (sentinel INT64_MIN): -(Rn).
  if (Disp.isImm() && Disp.getImm() == INT64_MIN) {
    assert(Base.isReg() && "Autodecrement must have a register base");
    O << "-(";
    printRegName(O, Base.getReg());
    O << ')';
    return;
  }

  // Displacement: may be immediate or an expression (e.g. frame index fixup).
  if (Disp.isImm() && Disp.getImm() != 0)
    O << Disp.getImm();
  else if (Disp.isExpr())
    MAI.printExpr(O, *Disp.getExpr());

  O << '(';
  assert(Base.isReg() && "Memory base must be a register");
  printRegName(O, Base.getReg());
  O << ')';
}
