//===-- VAXMCTargetDesc.h - VAX Target Descriptions -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_MCTARGETDESC_VAXMCTARGETDESC_H
#define LLVM_LIB_TARGET_VAX_MCTARGETDESC_VAXMCTARGETDESC_H

#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {
class Target;
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCTargetOptions;
class StringRef;
class Triple;

} // namespace llvm

// Defines symbolic names for VAX registers.
#define GET_REGINFO_ENUM
#include "VAXGenRegisterInfo.inc"

// Defines symbolic names for VAX instructions.
#define GET_INSTRINFO_ENUM
#include "VAXGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "VAXGenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_VAX_MCTARGETDESC_VAXMCTARGETDESC_H
