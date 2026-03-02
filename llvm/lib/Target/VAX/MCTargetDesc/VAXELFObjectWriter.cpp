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
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
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
      case VAX::fixup_vax_pcrel_32:
        return ELF::R_VAX_PC32;
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
