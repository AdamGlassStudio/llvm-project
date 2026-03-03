//===-- VAXTargetMachine.cpp - Define TargetMachine for VAX -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXTargetMachine.h"
#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "TargetInfo/VAXTargetInfo.h"
#include "VAX.h"
#include "VAXMachineFunctionInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include <optional>

using namespace llvm;

// VAX data layout (little-endian, ELF, 32-bit pointers):
//   e        - little-endian
//   m:e      - ELF name mangling
//   p:32:32  - 32-bit pointers, 32-bit aligned
//   i1:8:32  - i1: 8-bit storage, 32-bit preferred align
//   i8:8:32  - i8: 8-bit storage, 32-bit preferred align
//   i16:16:32- i16: 16-bit storage, 32-bit preferred align
//   i64:32   - i64: 32-bit aligned (VAX has no 64-bit alignment requirement)
//   f64:32   - D_float: 32-bit aligned
//   a:0:32   - aggregates: 32-bit preferred align
//   n32      - native integer width: 32 bits
static const char *VAXDataLayout =
    "e-m:e-p:32:32-i1:8:32-i8:8:32-i16:16:32-i64:32-f64:32-a:0:32-n32";

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  // VAX uses PC-relative addressing for all globals, which is inherently
  // position-independent for local symbols. Accept PIC mode but note that
  // GOT-based addressing for external symbols is not yet implemented.
  return RM.value_or(Reloc::Static);
}

static CodeModel::Model
getVAXEffectiveCodeModel(std::optional<CodeModel::Model> CM) {
  if (CM && *CM != CodeModel::Small && *CM != CodeModel::Large)
    report_fatal_error("VAX only supports Small and Large code models");
  return CM.value_or(CodeModel::Small);
}

VAXTargetMachine::VAXTargetMachine(const Target &T, const Triple &TT,
                                   StringRef CPU, StringRef FS,
                                   const TargetOptions &Options,
                                   std::optional<Reloc::Model> RM,
                                   std::optional<CodeModel::Model> CM,
                                   CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, VAXDataLayout, TT, CPU, FS, Options,
                               getEffectiveRelocModel(RM),
                               getVAXEffectiveCodeModel(CM), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  // VAX has no user-accessible per-thread register (unlike x86 %fs/%gs or
  // ARM TPIDR_EL0), so native TLS models are impossible. Force emulated TLS,
  // which lowers __thread variables to __emutls_* runtime calls — matching
  // GCC's behavior on VAX (HAVE_AS_TLS is undefined, no TLS relocations).
  this->Options.EmulatedTLS = true;
  initAsmInfo();
}

VAXTargetMachine::~VAXTargetMachine() = default;

MachineFunctionInfo *VAXTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return VAXMachineFunctionInfo::create<VAXMachineFunctionInfo>(Allocator, F,
                                                                STI);
}

namespace {

class VAXPassConfig : public TargetPassConfig {
public:
  VAXPassConfig(VAXTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  VAXTargetMachine &getVAXTargetMachine() const {
    return getTM<VAXTargetMachine>();
  }

  bool addInstSelector() override;
  void addIRPasses() override;
  void addPreRegAlloc() override;
  void addPostRegAlloc() override;
  void addPreEmitPass() override;
};

} // end anonymous namespace

TargetPassConfig *VAXTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new VAXPassConfig(*this, PM);
}

void VAXPassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());
  TargetPassConfig::addIRPasses();
}

bool VAXPassConfig::addInstSelector() {
  addPass(createVAXISelDag(getVAXTargetMachine(), getOptLevel()));
  return false;
}

void VAXPassConfig::addPreRegAlloc() {
  addPass(createVAXFuseCmpBranchPass());
}

void VAXPassConfig::addPostRegAlloc() {
  addPass(createVAXExpandCmpBranchPass());
}

void VAXPassConfig::addPreEmitPass() {
  addPass(createVAXPeepholePass());
  addPass(&BranchRelaxationPassID);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVAXTarget() {
  RegisterTargetMachine<VAXTargetMachine> X(getTheVAXTarget());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeVAXDAGToDAGISelLegacyPass(PR);
  initializeVAXFuseCmpBranchPass(PR);
  initializeVAXExpandCmpBranchPass(PR);
  initializeVAXPeepholePass(PR);
}
