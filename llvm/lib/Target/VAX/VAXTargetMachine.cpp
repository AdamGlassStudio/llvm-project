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
#include "VAXTargetTransformInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include <optional>

using namespace llvm;

namespace {
// VAX instructions are variable-length and byte-aligned — no .text alignment
// requirement. Override the default alignment (4) to match GAS (1), preventing
// unnecessary NOP padding between object files at link time.
//
// Also compensates for VAX R_VAX_PC32 semantics (S+A-P-4 vs standard S+A-P)
// in TType entries used for C++ exception type matching in PIC code.
class VAXELFTargetObjectFile : public TargetLoweringObjectFileELF {
public:
  unsigned getTextSectionAlignment() const override { return 1; }

  const MCExpr *getTTypeGlobalReference(const GlobalValue *GV,
                                        unsigned Encoding,
                                        const TargetMachine &TM,
                                        MachineModuleInfo *MMI,
                                        MCStreamer &Streamer) const override;
};
} // namespace

// VAX R_VAX_PC32 computes S+A-P-4 (VAX displacement convention), but DWARF
// pcrel encoding expects S+A-P. For TType entries in the LSDA (used by the
// personality routine for catch type matching), we compensate by adding +4
// to the pcrel expression, just as we do for FDE/personality in VAXMCAsmInfo.
const MCExpr *VAXELFTargetObjectFile::getTTypeGlobalReference(
    const GlobalValue *GV, unsigned Encoding, const TargetMachine &TM,
    MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  // For non-pcrel encodings, use the default.
  if (!(Encoding & dwarf::DW_EH_PE_pcrel))
    return TargetLoweringObjectFileELF::getTTypeGlobalReference(GV, Encoding,
                                                                TM, MMI,
                                                                Streamer);

  // For indirect pcrel, let the parent create the GOT stub, then adjust.
  if (Encoding & dwarf::DW_EH_PE_indirect) {
    // Parent creates .DW.stub GOT entry and returns Stub - PCSym.
    const MCExpr *Expr =
        TargetLoweringObjectFileELF::getTTypeGlobalReference(GV, Encoding, TM,
                                                             MMI, Streamer);
    // Add +4 to compensate for R_VAX_PC32 semantics.
    return MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(4, getContext()), getContext());
  }

  // Direct pcrel (no indirect): emit Sym - PCSym + 4.
  MCSymbol *Sym = TM.getSymbol(GV);
  MCSymbol *PCSym = getContext().createTempSymbol();
  Streamer.emitLabel(PCSym);
  const MCExpr *SymRef = MCSymbolRefExpr::create(Sym, getContext());
  const MCExpr *PCRef = MCSymbolRefExpr::create(PCSym, getContext());
  const MCExpr *Diff = MCBinaryExpr::createSub(SymRef, PCRef, getContext());
  return MCBinaryExpr::createAdd(Diff, MCConstantExpr::create(4, getContext()),
                                 getContext());
}

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
    "e-m:e-p:32:32-i1:8:32-i8:8:32-i16:16:32-i64:32-f64:32-a:0:32-n32-nif";

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  // VAX ELF always defaults to PIC, matching GCC's forced -fPIC for this
  // target (gcc/config/vax/elf.h VAX_CC1_AND_CC1PLUS_SPEC).
  //
  // Without PIC, external symbol references use R_VAX_PC32 relocations.
  // The VAX BFD linker does not create COPY relocations for PC-relative
  // references to undefined symbols, so data from shared libraries (e.g.
  // libc's __sF/stdout) resolves to its link-time address (~0) rather than
  // being copied into the executable's BSS — causing SIGSEGV at runtime.
  //
  // PIC mode routes external symbols through the GOT (R_VAX_GOT32) and
  // external calls through the PLT (R_VAX_PLT32), which the dynamic linker
  // resolves correctly.  Local symbols continue to use efficient PC-relative
  // addressing since VAX displacement modes are inherently position-independent.
  return RM.value_or(Reloc::PIC_);
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
      TLOF(std::make_unique<VAXELFTargetObjectFile>()),
      Subtarget(TT, CPU.empty() ? "generic" : std::string(CPU),
                std::string(FS), *this) {
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

TargetTransformInfo
VAXTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<VAXTTIImpl>(this, F));
}

//===----------------------------------------------------------------------===//
// VAX Inline Threshold
//
// Approximate GCC's inlining behavior on VAX by default. LLVM's default
// threshold (225) is ~3x more aggressive than GCC's effective threshold,
// causing +6.2% kernel .text bloat that is entirely due to inlining
// policy — at threshold=0, Clang codegen is actually 2% smaller than GCC.
//
// The "function-inline-threshold" function attribute overrides the default
// threshold per-function. We stamp it on every function that doesn't
// already have one, giving an effective default of 75 — the empirical
// crossover point where Clang's kernel .text matches GCC's.
//
// LLVM's TTI hooks (adjustInliningThreshold, getInliningThresholdMultiplier)
// can only increase the threshold, not decrease it, so this attribute-based
// approach is necessary.
//===----------------------------------------------------------------------===//

namespace {

// Empirically determined: threshold=75 produces kernel .text within 0.1%
// of GCC. The user can still override globally via -mllvm -inline-threshold=N.
static constexpr int VAXInlineThreshold = 75;

struct VAXSetInlineThresholdPass
    : public PassInfoMixin<VAXSetInlineThresholdPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    for (Function &F : M) {
      if (F.isDeclaration())
        continue;
      if (F.hasFnAttribute("function-inline-threshold"))
        continue;
      F.addFnAttr("function-inline-threshold",
                   std::to_string(VAXInlineThreshold));
    }
    return PreservedAnalyses::all();
  }
};

} // end anonymous namespace

void VAXTargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {
  PB.registerPipelineStartEPCallback(
      [](ModulePassManager &MPM, OptimizationLevel Level) {
        if (Level == OptimizationLevel::O0)
          return;
        MPM.addPass(VAXSetInlineThresholdPass());
      });
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
  bool addILPOpts() override;
  void addPostRegAlloc() override;
  void addPreEmitPass() override;
  void addPreEmitPass2() override;
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

bool VAXPassConfig::addILPOpts() {
  // Fuse CMP/TST + Bcc into CMP_BRANCH/TST_BRANCH pseudos BEFORE Machine CSE.
  // On VAX, MOVL (used by COPY for PHI resolution) clobbers PSW. If Machine
  // CSE removes a "redundant" CMPL across basic blocks, a later PHI-resolution
  // COPY can clobber the condition codes, producing wrong branches. Fusing
  // prevents Machine CSE from seeing a separable CMPL to eliminate.
  addPass(createVAXFuseCmpBranchPass());
  return true;
}

void VAXPassConfig::addPostRegAlloc() {
  addPass(createVAXExpandCmpBranchPass());
}

void VAXPassConfig::addPreEmitPass() {
  addPass(createVAXPeepholePass());
  addPass(&BranchRelaxationPassID);
}

void VAXPassConfig::addPreEmitPass2() {
  // SOB/AOB loop combine runs after branch relaxation. Conditional branches
  // that survived relaxation fit in byte displacement. SOB/AOB replace a
  // larger sequence, so displacement can only get shorter.
  addPass(createVAXSobAobCombinePass());
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVAXTarget() {
  RegisterTargetMachine<VAXTargetMachine> X(getTheVAXTarget());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeVAXDAGToDAGISelLegacyPass(PR);
  initializeVAXFuseCmpBranchPass(PR);
  initializeVAXExpandCmpBranchPass(PR);
  initializeVAXPeepholePass(PR);
  initializeVAXSobAobCombinePass(PR);
}
