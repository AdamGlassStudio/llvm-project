//===-- VAXMCAsmInfo.cpp - VAX asm properties -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXMCAsmInfo.h"
#include "llvm/ADT/Enum.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

constexpr EnumStringDef<MCAsmInfo::AtSpecifierKind> AtSpecifierDefs[] = {
    {{"ABS"}, VAX::S_ABS},
    {{"GOT"}, VAX::S_GOT},
    {{"PLT"}, VAX::S_PLT},
    {{"PCREL32"}, VAX::S_PCREL32},
};
constexpr auto atSpecifiers = BUILD_ENUM_STRINGS(AtSpecifierDefs);

void VAXMCAsmInfo::anchor() {}

VAXMCAsmInfo::VAXMCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
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

  // R_VAX_PC32 computes S + A - P - 4, while DWARF pcrel expects S + A - P.
  DwarfFDERelSymbolAddend = 4;

  initializeAtSpecifiers(atSpecifiers);
}
