//===-- VAXELFObjectWriter.cpp - VAX ELF Object Writer ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXFixupKinds.h"
#include "VAXMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {

class VAXELFObjectWriter : public MCELFObjectTargetWriter {
public:
  VAXELFObjectWriter()
      : MCELFObjectTargetWriter(/*Is64Bit=*/false, /*OSABI=*/ELF::ELFOSABI_NONE,
                                /*EMachine=*/ELF::EM_VAX,
                                /*HasRelocationAddend=*/true) {}

  /// Check PIC lazily via the MCAssembler (set after construction).
  bool isPositionIndependent() const {
    if (!Asm)
      return false;
    if (auto *MOFI = getContext().getObjectFileInfo())
      return MOFI->isPositionIndependent();
    return false;
  }

  unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                        bool IsPCRel) const override {
    unsigned Kind = Fixup.getKind();

    if (IsPCRel) {
      switch (Kind) {
      default:
        return ELF::R_VAX_PC32;
      case VAX::fixup_vax_pcrel_8:
        return ELF::R_VAX_PC8;
      case VAX::fixup_vax_pcrel_16:
        return ELF::R_VAX_PC16;
      case VAX::fixup_vax_pcrel_32: {
        // PIC promotion: in PIC mode, promote bare PC-relative references to
        // non-local symbols to PLT32, matching GAS -k behavior. This covers
        // both calls (e.g., inline asm `calls $1,_mcount`) and jumps (e.g.,
        // `jmp __setjmp14+2` in sigsetjmp.S). The BFD linker resolves PLT32
        // to direct references for symbols defined in the same shared object.
        // Local symbols (STB_LOCAL) are excluded: the BFD linker requires a
        // global symbol hash entry for PLT32 (asserts h != NULL).
        if (isPositionIndependent()) {
          const MCSymbol *Sym = Target.getAddSym();
          if (Sym && !Sym->isTemporary()) {
            auto &ElfSym = static_cast<const MCSymbolELF &>(*Sym);
            if (ElfSym.getBinding() != ELF::STB_LOCAL)
              return ELF::R_VAX_PLT32;
          }
        }
        return ELF::R_VAX_PC32;
      }
      case VAX::fixup_vax_got_32:
        return ELF::R_VAX_GOT32;
      case VAX::fixup_vax_plt_32:
        return ELF::R_VAX_PLT32;
      }
    }

    switch (Kind) {
    default:
      llvm_unreachable("unsupported relocation type");
      return ELF::R_VAX_NONE;
    case FK_Data_1:
      return ELF::R_VAX_8;
    case FK_Data_2:
      return ELF::R_VAX_16;
    case FK_Data_4:
      return ELF::R_VAX_32;
    }
  }
};

} // end anonymous namespace

std::unique_ptr<MCObjectTargetWriter> llvm::createVAXELFObjectWriter() {
  return std::make_unique<VAXELFObjectWriter>();
}
