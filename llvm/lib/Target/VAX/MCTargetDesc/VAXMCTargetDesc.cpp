//===-- VAXMCTargetDesc.cpp - VAX Target Descriptions -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXMCTargetDesc.h"
#include "VAXInstPrinter.h"
#include "VAXMCAsmInfo.h"
#include "TargetInfo/VAXTargetInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "VAXGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "VAXGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "VAXGenRegisterInfo.inc"

static MCInstrInfo *createVAXMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitVAXMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createVAXMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitVAXMCRegisterInfo(X, VAX::PC);
  return X;
}

static MCSubtargetInfo *createVAXMCSubtargetInfo(const Triple &TT,
                                                  StringRef CPU, StringRef FS) {
  return createVAXMCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCAsmInfo *createVAXMCAsmInfo(const MCRegisterInfo &MRI,
                                      const Triple &TT,
                                      const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new VAXMCAsmInfo(TT);
  // Initial frame state: CFA = SP+0.
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(nullptr, VAX::SP, 0);
  MAI->addInitialFrameState(Inst);
  return MAI;
}

static MCInstPrinter *createVAXMCInstPrinter(const Triple &T,
                                              unsigned SyntaxVariant,
                                              const MCAsmInfo &MAI,
                                              const MCInstrInfo &MII,
                                              const MCRegisterInfo &MRI) {
  return new VAXInstPrinter(MAI, MII, MRI);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVAXTargetMC() {
  RegisterMCAsmInfoFn X(getTheVAXTarget(), createVAXMCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(getTheVAXTarget(), createVAXMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(getTheVAXTarget(), createVAXMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(getTheVAXTarget(),
                                          createVAXMCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(getTheVAXTarget(),
                                        createVAXMCInstPrinter);
  TargetRegistry::RegisterMCCodeEmitter(getTheVAXTarget(),
                                        createVAXMCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(getTheVAXTarget(),
                                       createVAXAsmBackend);
}
