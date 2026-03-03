//===-- VAXBaseInfo.h - Top-level definitions for VAX MC --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone enum definitions for the VAX target
// useful for the compiler back-end and the MC libraries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_MCTARGETDESC_VAXBASEINFO_H
#define LLVM_LIB_TARGET_VAX_MCTARGETDESC_VAXBASEINFO_H

#include "llvm/MC/MCExpr.h"

namespace llvm {

/// VAXII - This namespace holds target operand flags for MachineOperand
/// TargetFlags. These flags are used to indicate PIC-related relocation
/// requirements on global address references.
namespace VAXII {

/// Target operand flags. These are stored in
/// MachineOperand::TargetFlags.
enum TOF {
  MO_NO_FLAG = 0,

  /// MO_GOT - On a symbol reference, indicates that the symbol should
  /// be accessed through the GOT. Produces R_VAX_GOT32 relocation.
  /// The linker will set the deferred bit in the operand specifier.
  MO_GOT = 1,

  /// MO_PLT - On a symbol reference, indicates that the symbol should
  /// be called through the PLT. Produces R_VAX_PLT32 relocation.
  MO_PLT = 2,
};

} // namespace VAXII
} // namespace llvm

#endif
