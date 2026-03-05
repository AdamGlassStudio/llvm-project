//===-- VAXMCAsmInfo.cpp - VAX asm properties -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXMCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

static const MCAsmInfo::AtSpecifier atSpecifiers[] = {
    {VAX::S_GOT, "GOT"},
    {VAX::S_PLT, "PLT"},
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

  initializeAtSpecifiers(atSpecifiers);
}
