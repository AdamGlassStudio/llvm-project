//===-- VAXFixupKinds.h - VAX-specific fixup kinds ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_MCTARGETDESC_VAXFIXUPKINDS_H
#define LLVM_LIB_TARGET_VAX_MCTARGETDESC_VAXFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm::VAX {

enum Fixups {
  // 8-bit PC-relative branch displacement (BRB, Bcc).
  fixup_vax_pcrel_8 = FirstTargetFixupKind,

  // 16-bit PC-relative branch displacement (BRW).
  fixup_vax_pcrel_16,

  // 32-bit PC-relative operand displacement (longword displacement mode with
  // PC as base — the standard operand-level PC-relative addressing).
  fixup_vax_pcrel_32,

  // 32-bit PC-relative GOT entry reference. The linker resolves this to a
  // displacement to the symbol's GOT entry and sets the deferred bit in the
  // operand specifier byte, turning it into an indirect load through the GOT.
  fixup_vax_got_32,

  // 32-bit PC-relative PLT entry reference. Used for calls to external
  // functions in PIC mode. The linker resolves this to a displacement to the
  // PLT stub.
  fixup_vax_plt_32,

  // Marker for the end of target-specific fixups.
  fixup_vax_invalid,
  NumTargetFixupKinds = fixup_vax_invalid - FirstTargetFixupKind
};

} // namespace llvm::VAX

#endif
