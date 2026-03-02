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

  // Marker for the end of target-specific fixups.
  fixup_vax_invalid,
  NumTargetFixupKinds = fixup_vax_invalid - FirstTargetFixupKind
};

} // namespace llvm::VAX

#endif
