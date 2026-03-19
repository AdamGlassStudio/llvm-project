//===-- VAXMCAsmInfo.cpp - VAX asm properties -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXMCAsmInfo.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

static const MCAsmInfo::AtSpecifier atSpecifiers[] = {
    {VAX::S_ABS, "ABS"},
    {VAX::S_GOT, "GOT"},
    {VAX::S_PLT, "PLT"},
    {VAX::S_PCREL32, "PCREL32"},
};

void VAXMCAsmInfo::anchor() {}

VAXMCAsmInfo::VAXMCAsmInfo(const Triple &TT) {
  // GAS syntax for NetBSD/vax:
  //   Registers: %r0-%r11, %ap, %fp, %sp, %pc
  //   Immediates: $5, $0 (dollar prefix)
  //   Operand order: source before destination
  CommentString = "#";
  Data16bitsDirective = "\t.word\t";
  Data32bitsDirective = "\t.long\t";
  Data64bitsDirective = nullptr; // VAX has no native 64-bit data directive
  ZeroDirective = "\t.space\t";
  AscizDirective = "\t.asciz\t";
  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;
  // Use external GAS assembler — the integrated assembler does not yet
  // support VAX instruction encoding.
  UseIntegratedAssembler = true;
  // VAX GAS does not support the `.bss` shorthand; use `.section .bss`.
  UsesELFSectionDirectiveForBSS = true;
  // GAS on VAX interprets .align N as 2^N byte alignment (power-of-two).
  AlignmentIsInBytes = false;
  // VAX instructions are variable-length (1–37 bytes). Typical instructions
  // are 2–10 bytes. Used by getInlineAsmLength to estimate inline asm size.
  MaxInstLength = 10;

  initializeAtSpecifiers(atSpecifiers);
}

const MCExpr *
VAXMCAsmInfo::getExprForFDESymbol(const MCSymbol *Sym, unsigned Encoding,
                                  MCStreamer &Streamer) const {
  if (!(Encoding & dwarf::DW_EH_PE_pcrel))
    return MCAsmInfo::getExprForFDESymbol(Sym, Encoding, Streamer);

  // R_VAX_PC32 computes S + A - P - 4 (VAX displacement convention: the
  // displacement is relative to the byte after the 4-byte field).  DWARF's
  // pcrel encoding expects S + A - P.  Add +4 to the expression so the
  // extra -4 from the linker cancels out.
  MCContext &Ctx = Streamer.getContext();
  const MCExpr *Res = MCSymbolRefExpr::create(Sym, Ctx);
  MCSymbol *PCSym = Ctx.createTempSymbol();
  Streamer.emitLabel(PCSym);
  const MCExpr *PC = MCSymbolRefExpr::create(PCSym, Ctx);
  const MCExpr *Four = MCConstantExpr::create(4, Ctx);
  return MCBinaryExpr::createAdd(MCBinaryExpr::createSub(Res, PC, Ctx), Four,
                                 Ctx);
}
