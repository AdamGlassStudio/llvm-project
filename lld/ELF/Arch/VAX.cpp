//===- VAX.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The VAX is a 32-bit CISC architecture with a rich instruction set and
// orthogonal addressing modes. This implements ELF linking for the
// vax-unknown-netbsdelf target, supporting static linking with basic
// absolute and PC-relative relocations.
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class VAX final : public TargetInfo {
public:
  VAX(Ctx &);
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

VAX::VAX(Ctx &ctx) : TargetInfo(ctx) {
  // HALT instruction (opcode 0x00) used as trap fill.
  trapInstr = {0x00, 0x00, 0x00, 0x00};
  defaultImageBase = 0;
}

RelExpr VAX::getRelExpr(RelType type, const Symbol &s,
                        const uint8_t *loc) const {
  switch (type) {
  case R_VAX_PC32:
  case R_VAX_PC16:
  case R_VAX_PC8:
    return R_PC;
  case R_VAX_PLT32:
    return R_PLT_PC;
  case R_VAX_GOT32:
    return R_GOT_OFF;
  case R_VAX_32:
  case R_VAX_16:
  case R_VAX_8:
  case R_VAX_NONE:
    return R_ABS;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unknown relocation (" << type
             << ") against symbol " << &s;
    return R_NONE;
  }
}

void VAX::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_VAX_8:
    checkIntUInt(ctx, loc, val, 8, rel);
    *loc = val;
    break;
  case R_VAX_PC8:
    // VAX PC-relative: displacement is from the byte after the field.
    val -= 1;
    checkIntUInt(ctx, loc, val, 8, rel);
    *loc = val;
    break;
  case R_VAX_16:
    checkIntUInt(ctx, loc, val, 16, rel);
    write16le(loc, val);
    break;
  case R_VAX_PC16:
    val -= 2;
    checkIntUInt(ctx, loc, val, 16, rel);
    write16le(loc, val);
    break;
  case R_VAX_32:
    write32le(loc, val);
    break;
  case R_VAX_PC32:
  case R_VAX_PLT32:
    // VAX PC-relative: displacement is from the byte after the 4-byte field.
    val -= 4;
    write32le(loc, val);
    break;
  case R_VAX_GOT32:
    write32le(loc, val);
    break;
  case R_VAX_NONE:
    break;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << rel.type;
  }
}

void elf::setVAXTargetInfo(Ctx &ctx) { ctx.target.reset(new VAX(ctx)); }
