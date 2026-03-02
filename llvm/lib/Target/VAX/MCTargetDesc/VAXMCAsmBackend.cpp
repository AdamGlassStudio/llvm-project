//===-- VAXMCAsmBackend.cpp - VAX MC Asm Backend ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXFixupKinds.h"
#include "VAXMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {

class VAXAsmBackend : public MCAsmBackend {
public:
  VAXAsmBackend() : MCAsmBackend(llvm::endianness::little) {}

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    const static MCFixupKindInfo Infos[VAX::NumTargetFixupKinds] = {
        // name              offset bits flags
        {"fixup_vax_pcrel_8", 0, 8, 0},
        {"fixup_vax_pcrel_16", 0, 16, 0},
        {"fixup_vax_pcrel_32", 0, 32, 0},
    };
    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);

    assert(unsigned(Kind - FirstTargetFixupKind) < VAX::NumTargetFixupKinds &&
           "Invalid kind!");
    return Infos[Kind - FirstTargetFixupKind];
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data,
                  uint64_t Value, bool IsResolved) override {
    maybeAddReloc(F, Fixup, Target, Value, IsResolved);

    unsigned NumBytes;

    switch ((unsigned)Fixup.getKind()) {
    default:
      NumBytes = getFixupKindInfo(Fixup.getKind()).TargetSize / 8;
      break;
    case VAX::fixup_vax_pcrel_8:
      NumBytes = 1;
      break;
    case VAX::fixup_vax_pcrel_16:
      NumBytes = 2;
      break;
    case VAX::fixup_vax_pcrel_32:
    case FK_Data_4:
      NumBytes = 4;
      break;
    case FK_Data_1:
      NumBytes = 1;
      break;
    case FK_Data_2:
      NumBytes = 2;
      break;
    }

    // VAX PC-relative displacements are measured from the byte after the
    // displacement field, but LLVM computes the value relative to the start
    // of the field. Adjust by subtracting the field size for resolved fixups.
    // For unresolved fixups (relocations), the linker handles this adjustment.
    if (Fixup.isPCRel() && IsResolved)
      Value -= NumBytes;

    for (unsigned i = 0; i < NumBytes; ++i) {
      Data[i] = static_cast<uint8_t>(Value & 0xFF);
      Value >>= 8;
    }
  }

  bool fixupNeedsRelaxation(const MCFixup &Fixup,
                            uint64_t Value) const override {
    return false;
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override {
    // VAX NOP is 0x01.
    for (uint64_t i = 0; i < Count; ++i)
      OS.write('\x01');
    return true;
  }

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createVAXELFObjectWriter();
  }
};

} // end anonymous namespace

MCAsmBackend *llvm::createVAXAsmBackend(const Target &T,
                                        const MCSubtargetInfo &STI,
                                        const MCRegisterInfo &MRI,
                                        const MCTargetOptions &Options) {
  return new VAXAsmBackend();
}
