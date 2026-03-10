//===-- VAXMCAsmBackend.cpp - VAX MC Asm Backend ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXFixupKinds.h"
#include "VAXMCAsmInfo.h"
#include "VAXMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
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
        {"fixup_vax_got_32", 0, 32, 0},
        {"fixup_vax_plt_32", 0, 32, 0},
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
    case VAX::fixup_vax_got_32:
    case VAX::fixup_vax_plt_32:
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

    // Range check for 8-bit branch displacements. If this fires, the branch
    // should have been relaxed or the BranchRelaxation pass missed it.
    if ((unsigned)Fixup.getKind() == VAX::fixup_vax_pcrel_8 && IsResolved) {
      int64_t SVal = static_cast<int64_t>(Value);
      if (SVal < -128 || SVal > 127)
        getContext().reportError(Fixup.getLoc(),
                                "branch displacement out of range (-128..+127)");
    }

    for (unsigned i = 0; i < NumBytes; ++i) {
      Data[i] = static_cast<uint8_t>(Value & 0xFF);
      Value >>= 8;
    }
  }

  bool fixupNeedsRelaxation(const MCFixup &Fixup,
                            uint64_t Value) const override {
    // An 8-bit PC-relative branch displacement that doesn't fit needs
    // relaxation (BRB → BRW).
    if ((unsigned)Fixup.getKind() == VAX::fixup_vax_pcrel_8)
      return !isInt<8>(static_cast<int64_t>(Value) - 1);
    // A 16-bit PC-relative operand displacement that doesn't fit needs
    // relaxation (0xCF/0xDF word → 0xEF/0xFF longword).
    if ((unsigned)Fixup.getKind() == VAX::fixup_vax_pcrel_16)
      return !isInt<16>(static_cast<int64_t>(Value) - 2);
    return false;
  }

  bool mayNeedRelaxation(unsigned Opcode, ArrayRef<MCOperand> Operands,
                         const MCSubtargetInfo &STI) const override {
    // BRB can be relaxed to BRW.
    if (Opcode == VAX::BRB)
      return true;
    // Any instruction with an expression operand might need PC-relative
    // operand relaxation (word → longword).
    for (const auto &Op : Operands) {
      if (!Op.isExpr())
        continue;
      // Already marked longword — no further relaxation possible.
      if (auto *SRE = dyn_cast<MCSymbolRefExpr>(Op.getExpr()))
        if (SRE->getSpecifier() == VAX::S_PCREL32 ||
            SRE->getSpecifier() == VAX::S_PLT ||
            SRE->getSpecifier() == VAX::S_GOT)
          continue;
      return true;
    }
    return false;
  }

  /// Walk an MCExpr tree and replace bare MCSymbolRefExpr nodes (specifier == 0)
  /// with S_PCREL32 to force longword encoding on re-encode.
  static const MCExpr *addPCREL32(const MCExpr *Expr, MCContext &Ctx) {
    switch (Expr->getKind()) {
    case MCExpr::SymbolRef: {
      auto *SRE = cast<MCSymbolRefExpr>(Expr);
      if (SRE->getSpecifier() != 0)
        return nullptr; // Already has specifier
      return MCSymbolRefExpr::create(&SRE->getSymbol(), VAX::S_PCREL32, Ctx);
    }
    case MCExpr::Binary: {
      auto *BE = cast<MCBinaryExpr>(Expr);
      if (auto *New = addPCREL32(BE->getLHS(), Ctx))
        return MCBinaryExpr::create(BE->getOpcode(), New, BE->getRHS(), Ctx);
      if (auto *New = addPCREL32(BE->getRHS(), Ctx))
        return MCBinaryExpr::create(BE->getOpcode(), BE->getLHS(), New, Ctx);
      return nullptr;
    }
    case MCExpr::Unary: {
      auto *UE = cast<MCUnaryExpr>(Expr);
      if (auto *New = addPCREL32(UE->getSubExpr(), Ctx))
        return MCUnaryExpr::create(UE->getOpcode(), New, Ctx);
      return nullptr;
    }
    default:
      return nullptr;
    }
  }

  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override {
    if (Inst.getOpcode() == VAX::BRB) {
      Inst.setOpcode(VAX::BRW);
      return;
    }
    // PC-relative operand relaxation: mark expression operands with
    // S_PCREL32 so re-encoding uses longword displacement.
    // Handles both simple MCSymbolRefExpr and nested expressions like sym+4.
    for (unsigned i = 0; i < Inst.getNumOperands(); i++) {
      MCOperand &Op = Inst.getOperand(i);
      if (!Op.isExpr())
        continue;
      if (auto *NewExpr = addPCREL32(Op.getExpr(), getContext()))
        Op = MCOperand::createExpr(NewExpr);
    }
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
