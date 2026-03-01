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
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCStreamer.h"
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

  void emitInstruction(const MachineInstr *MI) override;
};

} // end anonymous namespace

void VAXAsmPrinter::emitInstruction(const MachineInstr *MI) {
  // TODO: implement MCInstLowering in Phase 3
  report_fatal_error("VAXAsmPrinter: instruction emission not yet implemented");
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVAXAsmPrinter() {
  RegisterAsmPrinter<VAXAsmPrinter> X(getTheVAXTarget());
}
