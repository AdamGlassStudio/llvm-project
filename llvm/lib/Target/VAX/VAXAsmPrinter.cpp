//===-- VAXAsmPrinter.cpp - VAX LLVM assembly writer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAX.h"
#include "VAXSubtarget.h"
#include "VAXTargetMachine.h"
#include "TargetInfo/VAXTargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {

class VAXAsmPrinter : public AsmPrinter {
public:
  explicit VAXAsmPrinter(TargetMachine &TM,
                          std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)) {}

  StringRef getPassName() const override { return "VAX Assembly Printer"; }

  void emitFunctionBodyStart() override;
  void emitInstruction(const MachineInstr *MI) override;
};

} // end anonymous namespace

void VAXAsmPrinter::emitFunctionBodyStart() {
  // Emit the 2-byte entry mask immediately after the function label.
  // The CALLS/CALLG instruction reads this word on every call to decide
  // which of R0–R11 to save; only callee-saved regs (R6–R11) should appear.
  const MCRegisterInfo *MRI = TM.getMCRegisterInfo();
  const MachineFrameInfo &MFI = MF->getFrameInfo();
  uint16_t Mask = 0;
  for (const CalleeSavedInfo &CSI : MFI.getCalleeSavedInfo()) {
    unsigned Enc = MRI->getEncodingValue(CSI.getReg());
    if (Enc <= 11) // only R0–R11 appear in the entry mask
      Mask |= 1u << Enc;
  }
  OutStreamer->emitIntValue(Mask, 2);
}

void VAXAsmPrinter::emitInstruction(const MachineInstr *MI) {
  // Expand LEA_FI pseudo: addl3 $offset, %fp, $dst
  if (MI->getOpcode() == VAX::LEA_FI) {
    MCInst Inst;
    Inst.setOpcode(VAX::ADDL3_ri);
    // Operand layout: ADDL3_ri (outs $dst), (ins i32imm:$a, GPRnoPC:$b)
    // LEA_FI: op0=$dst, op1=base(FP), op2=disp
    Inst.addOperand(MCOperand::createReg(MI->getOperand(0).getReg())); // dst
    Inst.addOperand(MCOperand::createImm(MI->getOperand(2).getImm())); // offset
    Inst.addOperand(MCOperand::createReg(MI->getOperand(1).getReg())); // FP
    EmitToStreamer(*OutStreamer, Inst);
    return;
  }

  MCInst Inst;
  Inst.setOpcode(MI->getOpcode());
  for (const MachineOperand &MO : MI->explicit_operands()) {
    switch (MO.getType()) {
    case MachineOperand::MO_Register:
      Inst.addOperand(MCOperand::createReg(MO.getReg()));
      break;
    case MachineOperand::MO_Immediate:
      Inst.addOperand(MCOperand::createImm(MO.getImm()));
      break;
    case MachineOperand::MO_GlobalAddress: {
      const MCSymbol *Sym = getSymbol(MO.getGlobal());
      const MCExpr *Expr = MCSymbolRefExpr::create(Sym, OutContext);
      if (MO.getOffset())
        Expr = MCBinaryExpr::createAdd(
            Expr, MCConstantExpr::create(MO.getOffset(), OutContext),
            OutContext);
      Inst.addOperand(MCOperand::createExpr(Expr));
      break;
    }
    case MachineOperand::MO_ExternalSymbol: {
      const MCSymbol *Sym = GetExternalSymbolSymbol(MO.getSymbolName());
      Inst.addOperand(
          MCOperand::createExpr(MCSymbolRefExpr::create(Sym, OutContext)));
      break;
    }
    case MachineOperand::MO_MachineBasicBlock: {
      // Branch target — emit the basic block label as a symbol reference.
      const MCSymbol *Sym = MO.getMBB()->getSymbol();
      Inst.addOperand(
          MCOperand::createExpr(MCSymbolRefExpr::create(Sym, OutContext)));
      break;
    }
    case MachineOperand::MO_RegisterMask:
      // Register mask is register-allocator metadata; not emitted to assembly.
      break;
    default:
      report_fatal_error("VAXAsmPrinter: unsupported MachineOperand type");
    }
  }
  EmitToStreamer(*OutStreamer, Inst);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVAXAsmPrinter() {
  RegisterAsmPrinter<VAXAsmPrinter> X(getTheVAXTarget());
}
