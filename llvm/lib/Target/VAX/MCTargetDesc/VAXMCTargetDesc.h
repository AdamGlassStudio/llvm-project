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

MCCodeEmitter *createVAXMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx);

MCAsmBackend *createVAXAsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                  const MCRegisterInfo &MRI,
                                  const MCTargetOptions &Options);

std::unique_ptr<MCObjectTargetWriter> createVAXELFObjectWriter();

} // namespace llvm

// Custom operand types for VAX. These encode the data context width of
// operand specifiers (byte, word, longword) so the MC encoder emits the
// correct number of bytes for immediate mode (0x8F + N bytes).
#include "llvm/MC/MCInstrDesc.h"
namespace llvm {
namespace VAXOp {
enum OperandType : unsigned {
  OPERAND_BYTE_IMM = MCOI::OPERAND_FIRST_TARGET,
  OPERAND_WORD_IMM,
  OPERAND_QUAD_IMM,
};
} // namespace VAXOp
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
