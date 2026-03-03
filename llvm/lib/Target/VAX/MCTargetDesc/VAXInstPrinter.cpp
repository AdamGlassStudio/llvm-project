//===-- VAXInstPrinter.cpp - Convert VAX MCInst to assembly -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXInstPrinter.h"
#include "VAX.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/raw_ostream.h"

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

// Memory operand: 4 MCOperands = (base, disp, index, flags).
// Prints VAX GAS syntax based on flags (VAXAM::*).
void VAXInstPrinter::printMemOperand(const MCInst *MI, unsigned OpNo,
                                     raw_ostream &O) {
  const MCOperand &Base = MI->getOperand(OpNo);
  const MCOperand &Disp = MI->getOperand(OpNo + 1);
  const MCOperand &Index = MI->getOperand(OpNo + 2);
  const MCOperand &Flags = MI->getOperand(OpNo + 3);

  unsigned Mode = Flags.getImm();

  // Helper: print [Rx] index suffix if present.
  auto printIndexSuffix = [&]() {
    if (Index.isReg() && Index.getReg()) {
      O << '[';
      printRegName(O, Index.getReg());
      O << ']';
    }
  };

  switch (Mode) {
  case VAXAM::RegDirect:
    // Bare register: %Rn
    printRegName(O, Base.getReg());
    printIndexSuffix();
    return;

  case VAXAM::RegDeferred:
    // (%Rn)
    O << '(';
    printRegName(O, Base.getReg());
    O << ')';
    printIndexSuffix();
    return;

  case VAXAM::AutoDec:
    // -(%Rn)
    O << "-(";
    printRegName(O, Base.getReg());
    O << ')';
    printIndexSuffix();
    return;

  case VAXAM::AutoInc:
    // (%Rn)+
    O << '(';
    printRegName(O, Base.getReg());
    O << ")+";
    printIndexSuffix();
    return;

  case VAXAM::AutoIncDef:
    // *(%Rn)+
    O << "*(";
    printRegName(O, Base.getReg());
    O << ")+";
    printIndexSuffix();
    return;

  case VAXAM::Imm:
    // $value (immediate)
    O << '$';
    if (Disp.isImm())
      O << Disp.getImm();
    else if (Disp.isExpr())
      MAI.printExpr(O, *Disp.getExpr());
    else
      O << '0';
    printIndexSuffix();
    return;

  case VAXAM::Absolute:
    // *$addr (absolute deferred) or *expr
    O << '*';
    if (Disp.isExpr())
      MAI.printExpr(O, *Disp.getExpr());
    else if (Disp.isImm())
      O << '$' << Disp.getImm();
    else
      O << "$0";
    printIndexSuffix();
    return;

  case VAXAM::DispDeferred:
    // *disp(%Rn) — displacement deferred
    O << '*';
    if (Disp.isImm() && Disp.getImm() != 0)
      O << Disp.getImm();
    else if (Disp.isExpr())
      MAI.printExpr(O, *Disp.getExpr());
    O << '(';
    printRegName(O, Base.getReg());
    O << ')';
    printIndexSuffix();
    return;

  case VAXAM::Disp:
  default:
    // disp(%Rn) or expr
    if (!Base.getReg()) {
      // No base: PC-relative expression or immediate
      if (Disp.isExpr())
        MAI.printExpr(O, *Disp.getExpr());
      else if (Disp.isImm())
        O << Disp.getImm();
      printIndexSuffix();
      return;
    }
    // Normal displacement
    if (Disp.isImm() && Disp.getImm() != 0)
      O << Disp.getImm();
    else if (Disp.isExpr())
      MAI.printExpr(O, *Disp.getExpr());
    O << '(';
    printRegName(O, Base.getReg());
    O << ')';
    printIndexSuffix();
    return;
  }
}
