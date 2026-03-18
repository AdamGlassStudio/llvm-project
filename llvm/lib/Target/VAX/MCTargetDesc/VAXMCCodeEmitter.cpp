//===-- VAXMCCodeEmitter.cpp - VAX MC Code Emitter --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Encodes VAX MCInst instructions into byte sequences. VAX instructions are
// variable-length: 1-2 opcode bytes followed by 0+ operand specifier bytes.
// Each operand specifier independently encodes its addressing mode.
//
//===----------------------------------------------------------------------===//

#include "VAXFixupKinds.h"
#include "VAXMCAsmInfo.h"
#include "VAXMCTargetDesc.h"
#include "VAX.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "vax-mccodeemitter"

namespace {

class VAXMCCodeEmitter : public MCCodeEmitter {
  const MCInstrInfo &MCII;
  MCContext &Ctx;

public:
  VAXMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx)
      : MCII(MCII), Ctx(Ctx) {}

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

private:
  /// Get the 4-bit hardware register number for an MCRegister.
  unsigned getRegEncoding(MCRegister Reg) const;

  /// Emit an operand specifier for a register operand.
  void emitRegOperand(MCRegister Reg, SmallVectorImpl<char> &CB) const;

  /// Emit an operand specifier for an immediate operand (literal or immediate
  /// mode depending on value). DataSize is the operand context in bytes:
  /// 1 for .rb, 2 for .rw, 4 for .rl (default).
  void emitImmOperand(int64_t Imm, unsigned DataSize,
                      SmallVectorImpl<char> &CB) const;

  /// Emit an operand specifier for an expression operand (PC-relative).
  void emitExprOperand(const MCExpr *Expr, SmallVectorImpl<char> &CB,
                       SmallVectorImpl<MCFixup> &Fixups,
                       unsigned StartByte) const;

  /// Emit an operand specifier for a 4-slot memory operand
  /// (base, disp, index, flags). Handles all VAX addressing modes.
  void emitMemOperand(const MCOperand &Base, const MCOperand &Disp,
                      const MCOperand &Index, const MCOperand &Flags,
                      SmallVectorImpl<char> &CB,
                      SmallVectorImpl<MCFixup> &Fixups,
                      unsigned StartByte, unsigned Opcode,
                      unsigned MemOpOrdinal) const;

  /// Emit a branch displacement with the appropriate fixup.
  void emitBranchDisp(const MCOperand &Target, unsigned DispSize,
                      SmallVectorImpl<char> &CB,
                      SmallVectorImpl<MCFixup> &Fixups,
                      unsigned StartByte) const;

  /// Return true if this opcode is a branch instruction with a direct
  /// displacement (not an operand-specifier branch like JMP).
  bool isBranch(unsigned Opcode) const;

  /// Expand a LongBcc pseudo into an inverted Bcc + BRW sequence.
  void expandLongCondBr(const MCInst &MI, SmallVectorImpl<char> &CB,
                        SmallVectorImpl<MCFixup> &Fixups) const;

  /// Return the displacement size for a branch opcode (1 or 2 bytes).
  unsigned getBranchDispSize(unsigned Opcode) const;

  /// Return the trailing branch displacement size for compound instructions
  /// (SOBxxx, AOBxxx, ACBx, BBxx, BLBx). Returns 0 if the instruction has
  /// no trailing branch displacement.
  unsigned getTrailingBranchDispSize(unsigned Opcode) const;
};

} // end anonymous namespace

unsigned VAXMCCodeEmitter::getRegEncoding(MCRegister Reg) const {
  const MCRegisterInfo &MRI = *Ctx.getRegisterInfo();

  // For QPR registers (register pairs), use the low register's encoding.
  if (unsigned SubReg = MRI.getSubReg(Reg, VAX::sub_lo))
    return MRI.getEncodingValue(SubReg);

  return MRI.getEncodingValue(Reg);
}

void VAXMCCodeEmitter::emitRegOperand(MCRegister Reg,
                                      SmallVectorImpl<char> &CB) const {
  // Register mode: specifier byte = 0x50 | regnum
  CB.push_back(0x50 | getRegEncoding(Reg));
}

void VAXMCCodeEmitter::emitImmOperand(int64_t Imm, unsigned DataSize,
                                      SmallVectorImpl<char> &CB) const {
  // Short literal mode: values 0-63 encode in a single specifier byte.
  if (Imm >= 0 && Imm <= 63) {
    CB.push_back(static_cast<char>(Imm));
    return;
  }

  // Immediate mode: 0x8F (autoincrement PC) + N bytes where N = DataSize.
  CB.push_back(static_cast<char>(0x8F));
  uint64_t Val = static_cast<uint64_t>(Imm);
  for (unsigned i = 0; i < DataSize; ++i)
    CB.push_back(static_cast<char>((Val >> (i * 8)) & 0xFF));
}

/// Check if any MCSymbolRefExpr in the expression tree has the given specifier.
static bool hasSpecifier(const MCExpr *Expr, unsigned Spec) {
  switch (Expr->getKind()) {
  case MCExpr::SymbolRef:
    return cast<MCSymbolRefExpr>(Expr)->getSpecifier() == Spec;
  case MCExpr::Binary:
    return hasSpecifier(cast<MCBinaryExpr>(Expr)->getLHS(), Spec) ||
           hasSpecifier(cast<MCBinaryExpr>(Expr)->getRHS(), Spec);
  case MCExpr::Unary:
    return hasSpecifier(cast<MCUnaryExpr>(Expr)->getSubExpr(), Spec);
  default:
    return false;
  }
}

/// Check if any MCSymbolRefExpr in the expression tree has one of the
/// "force longword" specifiers (S_PCREL32, S_PLT, S_GOT).
static bool hasLongwordSpecifier(const MCExpr *Expr) {
  return hasSpecifier(Expr, VAX::S_PCREL32) ||
         hasSpecifier(Expr, VAX::S_PLT) ||
         hasSpecifier(Expr, VAX::S_GOT);
}

/// Return the PLT/GOT specifier if present in the expression tree, else 0.
static unsigned getExprSpecifier(const MCExpr *Expr) {
  if (hasSpecifier(Expr, VAX::S_PLT)) return VAX::S_PLT;
  if (hasSpecifier(Expr, VAX::S_GOT)) return VAX::S_GOT;
  return 0;
}

void VAXMCCodeEmitter::emitExprOperand(const MCExpr *Expr,
                                       SmallVectorImpl<char> &CB,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       unsigned StartByte) const {
  // S_ABS: $symbol in assembly → immediate mode (0x8F + absolute relocation).
  // GAS treats $symbol as "the address value" (immediate), not "load from
  // address" (displacement). CodeGen never sets S_ABS; it only appears from
  // the asm parser for $-prefixed symbol operands.
  if (hasSpecifier(Expr, VAX::S_ABS)) {
    CB.push_back(static_cast<char>(0x8F));
    unsigned FixOff = CB.size() - StartByte;
    CB.push_back(0); CB.push_back(0); CB.push_back(0); CB.push_back(0);
    Fixups.push_back(MCFixup::create(FixOff, Expr, FK_Data_4,
                                     /*IsPCRel=*/false));
    return;
  }

  bool UseLongword = hasLongwordSpecifier(Expr);

  if (UseLongword) {
    // Longword PC-relative displacement: 0xEF + 4-byte displacement.
    CB.push_back(static_cast<char>(0xEF));
    unsigned FixOff = CB.size() - StartByte;
    unsigned Spec = getExprSpecifier(Expr);
    MCFixupKind Kind;
    if (Spec == VAX::S_PLT)
      Kind = MCFixupKind(VAX::fixup_vax_plt_32);
    else if (Spec == VAX::S_GOT)
      Kind = MCFixupKind(VAX::fixup_vax_got_32);
    else
      Kind = MCFixupKind(VAX::fixup_vax_pcrel_32);
    Fixups.push_back(MCFixup::create(FixOff, Expr, Kind, /*PCRel=*/true));
    CB.push_back(0); CB.push_back(0); CB.push_back(0); CB.push_back(0);
  } else {
    // Word PC-relative displacement: 0xCF + 2-byte displacement.
    // If the displacement doesn't fit in 16 bits, the relaxation framework
    // will grow this to longword (0xEF + 4 bytes) via S_PCREL32 specifier.
    CB.push_back(static_cast<char>(0xCF));
    unsigned FixOff = CB.size() - StartByte;
    Fixups.push_back(MCFixup::create(FixOff, Expr,
                                     MCFixupKind(VAX::fixup_vax_pcrel_16),
                                     /*PCRel=*/true));
    CB.push_back(0); CB.push_back(0);
  }
}

void VAXMCCodeEmitter::emitMemOperand(const MCOperand &Base,
                                      const MCOperand &Disp,
                                      const MCOperand &Index,
                                      const MCOperand &Flags,
                                      SmallVectorImpl<char> &CB,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      unsigned StartByte, unsigned Opcode,
                                      unsigned MemOpOrdinal) const {
  assert(Base.isReg() && "Memory base must be a register");
  assert(Flags.isImm() && "Memory flags must be an immediate");
  unsigned BaseReg = Base.getReg() ? getRegEncoding(Base.getReg()) : 0;
  unsigned Mode = Flags.getImm();

  // When the base register is absent (NoReg) and the displacement is a
  // symbolic expression, this is a PC-relative operand (e.g., "symbol" or
  // "symbol[Rx]").  Use the PC register (0xF) as the base.
  // Numeric constants (MCConstantExpr) are absolute addresses and must NOT
  // use PC-relative encoding — they use absolute mode (0x9F) instead.
  if (!Base.getReg() && Disp.isExpr() &&
      !isa<MCConstantExpr>(Disp.getExpr()) &&
      (Mode == VAXAM::Disp || Mode == VAXAM::DispDeferred))
    BaseReg = 0xF;

  // Emit index prefix byte if present: 0x40 | index_reg.
  if (Index.isReg() && Index.getReg()) {
    CB.push_back(static_cast<char>(0x40 | getRegEncoding(Index.getReg())));
  }

  // Helper: emit displacement-mode operand specifier.
  // Deferred=false: 0x60 (reg deferred), 0xA0 (byte), 0xC0 (word), 0xE0 (long)
  // Deferred=true:  0xB0 (byte), 0xD0 (word), 0xF0 (long) — no zero-disp form
  auto emitDisplacement = [&](unsigned BaseRegNum, bool Deferred) {
    unsigned DeferBit = Deferred ? 0x10 : 0x00;
    if (Disp.isExpr()) {
      MCFixupKind Kind;
      bool IsPCRel;
      if (BaseRegNum == 0xF) {
        // PC-relative: check for GOT/PLT/PCREL32 specifier (may be nested).
        IsPCRel = true;
        bool UseLongword = hasLongwordSpecifier(Disp.getExpr());
        unsigned Spec = getExprSpecifier(Disp.getExpr());
        if (Spec == VAX::S_GOT)
          Kind = MCFixupKind(VAX::fixup_vax_got_32);
        else if (Spec == VAX::S_PLT)
          Kind = MCFixupKind(VAX::fixup_vax_plt_32);
        else if (UseLongword) {
          Kind = MCFixupKind(VAX::fixup_vax_pcrel_32);
        } else
          Kind = MCFixupKind(VAX::fixup_vax_pcrel_16);

        if (UseLongword) {
          // Longword: 0xE0/0xF0 | reg, 4 bytes
          CB.push_back(static_cast<char>((0xE0 | DeferBit) | BaseRegNum));
          unsigned FixOff = CB.size() - StartByte;
          CB.push_back(0); CB.push_back(0); CB.push_back(0); CB.push_back(0);
          Fixups.push_back(MCFixup::create(FixOff, Disp.getExpr(), Kind, IsPCRel));
        } else {
          // Word: 0xC0/0xD0 | reg, 2 bytes — relaxation grows to longword
          CB.push_back(static_cast<char>((0xC0 | DeferBit) | BaseRegNum));
          unsigned FixOff = CB.size() - StartByte;
          CB.push_back(0); CB.push_back(0);
          Fixups.push_back(MCFixup::create(FixOff, Disp.getExpr(), Kind, IsPCRel));
        }
      } else {
        IsPCRel = false;
        Kind = MCFixupKind(FK_Data_4);
        // Non-PC base: always longword displacement.
        CB.push_back(static_cast<char>((0xE0 | DeferBit) | BaseRegNum));
        unsigned FixOff = CB.size() - StartByte;
        CB.push_back(0); CB.push_back(0); CB.push_back(0); CB.push_back(0);
        Fixups.push_back(MCFixup::create(FixOff, Disp.getExpr(), Kind, IsPCRel));
      }
      return;
    }
    int64_t DispVal = Disp.isImm() ? Disp.getImm() : 0;
    if (DispVal == 0 && !Deferred) {
      // Zero displacement → register deferred (0x60|Rn).
      CB.push_back(static_cast<char>(0x60 | BaseRegNum));
      return;
    }
    // For deferred with DispVal==0, use byte displacement deferred (0xB0)
    // since there is no "register deferred deferred" mode.
    if (DispVal >= -128 && DispVal <= 127) {
      CB.push_back(static_cast<char>((0xA0 | DeferBit) | BaseRegNum));
      CB.push_back(static_cast<char>(DispVal & 0xFF));
      return;
    }
    if (DispVal >= -32768 && DispVal <= 32767) {
      CB.push_back(static_cast<char>((0xC0 | DeferBit) | BaseRegNum));
      uint16_t Val = static_cast<uint16_t>(DispVal);
      CB.push_back(static_cast<char>(Val & 0xFF));
      CB.push_back(static_cast<char>((Val >> 8) & 0xFF));
      return;
    }
    // Longword displacement.
    CB.push_back(static_cast<char>((0xE0 | DeferBit) | BaseRegNum));
    uint32_t Val = static_cast<uint32_t>(DispVal);
    CB.push_back(static_cast<char>(Val & 0xFF));
    CB.push_back(static_cast<char>((Val >> 8) & 0xFF));
    CB.push_back(static_cast<char>((Val >> 16) & 0xFF));
    CB.push_back(static_cast<char>((Val >> 24) & 0xFF));
  };

  switch (Mode) {
  case VAXAM::RegDirect:
    // Register direct: 0x50 | reg
    CB.push_back(static_cast<char>(0x50 | BaseReg));
    return;

  case VAXAM::RegDeferred:
    // Register deferred: 0x60 | reg
    CB.push_back(static_cast<char>(0x60 | BaseReg));
    return;

  case VAXAM::AutoDec:
    // Autodecrement: 0x70 | reg
    CB.push_back(static_cast<char>(0x70 | BaseReg));
    return;

  case VAXAM::AutoInc:
    // Autoincrement: 0x80 | reg
    CB.push_back(static_cast<char>(0x80 | BaseReg));
    return;

  case VAXAM::AutoIncDef:
    // Autoincrement deferred: 0x90 | reg
    CB.push_back(static_cast<char>(0x90 | BaseReg));
    return;

  case VAXAM::Imm: {
    // Immediate mode (no base register).
    // Determine data size from TSFlags MemOpWidth bits for this operand.
    // MemOpWidth encoding: 0=longword(4), 1=byte(1), 2=word(2), 3=quad(8).
    unsigned DataSize = 4;
    unsigned WidthBits = (MCII.get(Opcode).TSFlags >> (17 + MemOpOrdinal * 2)) & 0x3;
    if (WidthBits == 1) DataSize = 1;
    else if (WidthBits == 2) DataSize = 2;
    else if (WidthBits == 3) DataSize = 8;
    if (Disp.isImm()) {
      emitImmOperand(Disp.getImm(), DataSize, CB);
      return;
    }
    if (Disp.isExpr()) {
      // Immediate mode with expression: 0x8F + DataSize bytes.
      CB.push_back(static_cast<char>(0x8F));
      unsigned FixOff = CB.size() - StartByte;
      for (unsigned i = 0; i < DataSize; ++i)
        CB.push_back(0);
      MCFixupKind Kind;
      if (DataSize == 1)
        Kind = FK_Data_1;
      else if (DataSize == 2)
        Kind = FK_Data_2;
      else if (DataSize == 8)
        Kind = MCFixupKind(FK_Data_8);
      else
        Kind = MCFixupKind(FK_Data_4);
      Fixups.push_back(MCFixup::create(FixOff, Disp.getExpr(), Kind,
                                       /*IsPCRel=*/false));
      return;
    }
    emitImmOperand(0, DataSize, CB);
    return;
  }

  case VAXAM::Absolute:
    if (Disp.isExpr() && !isa<MCConstantExpr>(Disp.getExpr())) {
      // PC-relative displacement deferred for *symbol and *symbol[Rx].
      // Check specifier to decide word vs longword encoding (may be nested).
      bool UseLongword = hasLongwordSpecifier(Disp.getExpr());
      if (UseLongword) {
        // Longword deferred: 0xFF + 4-byte PC-relative displacement.
        CB.push_back(static_cast<char>(0xFF));
        unsigned FixOff = CB.size() - StartByte;
        CB.push_back(0); CB.push_back(0); CB.push_back(0); CB.push_back(0);
        Fixups.push_back(MCFixup::create(FixOff, Disp.getExpr(),
                                         MCFixupKind(VAX::fixup_vax_pcrel_32),
                                         /*IsPCRel=*/true));
      } else {
        // Word deferred: 0xDF + 2-byte PC-relative displacement.
        // Relaxation grows to longword (0xFF + 4 bytes) if needed.
        CB.push_back(static_cast<char>(0xDF));
        unsigned FixOff = CB.size() - StartByte;
        CB.push_back(0); CB.push_back(0);
        Fixups.push_back(MCFixup::create(FixOff, Disp.getExpr(),
                                         MCFixupKind(VAX::fixup_vax_pcrel_16),
                                         /*IsPCRel=*/true));
      }
    } else {
      // Absolute address: constant expr or immediate → 0x9F + addr.
      CB.push_back(static_cast<char>(0x9F));
      int64_t Addr = Disp.isImm() ? Disp.getImm() : 0;
      if (Disp.isExpr())
        Addr = cast<MCConstantExpr>(Disp.getExpr())->getValue();
      uint32_t Val = static_cast<uint32_t>(Addr);
      CB.push_back(static_cast<char>(Val & 0xFF));
      CB.push_back(static_cast<char>((Val >> 8) & 0xFF));
      CB.push_back(static_cast<char>((Val >> 16) & 0xFF));
      CB.push_back(static_cast<char>((Val >> 24) & 0xFF));
    }
    return;

  case VAXAM::DispDeferred:
    // Displacement deferred: 0xB0/D0/F0 modes.
    emitDisplacement(BaseReg, /*Deferred=*/true);
    return;

  case VAXAM::Disp:
  default:
    // Bare constant address with no register → absolute mode (0x9F + addr).
    // E.g., "tstl 0x80000100" accesses the absolute address directly.
    if (!Base.getReg() && Disp.isExpr() &&
        isa<MCConstantExpr>(Disp.getExpr())) {
      int64_t Addr = cast<MCConstantExpr>(Disp.getExpr())->getValue();
      CB.push_back(static_cast<char>(0x9F));
      uint32_t Val = static_cast<uint32_t>(Addr);
      CB.push_back(static_cast<char>(Val & 0xFF));
      CB.push_back(static_cast<char>((Val >> 8) & 0xFF));
      CB.push_back(static_cast<char>((Val >> 16) & 0xFF));
      CB.push_back(static_cast<char>((Val >> 24) & 0xFF));
      return;
    }
    // Normal displacement mode (or zero disp → register deferred).
    emitDisplacement(BaseReg, /*Deferred=*/false);
    return;
  }
}

void VAXMCCodeEmitter::emitBranchDisp(const MCOperand &Target,
                                      unsigned DispSize,
                                      SmallVectorImpl<char> &CB,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      unsigned StartByte) const {
  if (Target.isExpr()) {
    MCFixupKind Kind = (DispSize == 1) ? MCFixupKind(VAX::fixup_vax_pcrel_8)
                                       : MCFixupKind(VAX::fixup_vax_pcrel_16);
    Fixups.push_back(
        MCFixup::create(CB.size() - StartByte, Target.getExpr(), Kind,
                        /*PCRel=*/true));
  }
  // Emit placeholder bytes.
  for (unsigned i = 0; i < DispSize; ++i)
    CB.push_back(0);
}

/// Return the hardware opcode byte for the inverted condition of a LongBcc.
static uint8_t getInvertedCondBranchHWOpcode(unsigned Opcode) {
  switch (Opcode) {
  case VAX::LongBEQL:  return 0x12; // BNEQ
  case VAX::LongBNEQ:  return 0x13; // BEQL
  case VAX::LongBGTR:  return 0x15; // BLEQ
  case VAX::LongBGEQ:  return 0x19; // BLSS
  case VAX::LongBLSS:  return 0x18; // BGEQ
  case VAX::LongBLEQ:  return 0x14; // BGTR
  case VAX::LongBGTRU: return 0x1B; // BLEQU
  case VAX::LongBGEQU: return 0x1F; // BLSSU
  case VAX::LongBLSSU: return 0x1E; // BGEQU
  case VAX::LongBLEQU: return 0x1A; // BGTRU
  default:
    llvm_unreachable("Not a long conditional branch pseudo");
  }
}

void VAXMCCodeEmitter::expandLongCondBr(const MCInst &MI,
                                        SmallVectorImpl<char> &CB,
                                        SmallVectorImpl<MCFixup> &Fixups) const {
  unsigned StartByte = CB.size();

  // Emit the inverted conditional branch with displacement +3.
  // This skips over the 3-byte BRW that follows.
  //   [inverted Bcc opcode] [+3]     ← 2 bytes
  //   [BRW opcode=0x31] [disp16]     ← 3 bytes
  // The Bcc displacement is PC-relative from the byte after the disp field,
  // i.e., from offset +2.  We need to reach offset +5, so disp = +3.
  CB.push_back(static_cast<char>(getInvertedCondBranchHWOpcode(MI.getOpcode())));
  CB.push_back(static_cast<char>(3));

  // Emit BRW (opcode 0x31) with the original target.
  CB.push_back(static_cast<char>(0x31));
  const MCOperand &Target = MI.getOperand(0);
  if (Target.isExpr()) {
    Fixups.push_back(
        MCFixup::create(CB.size() - StartByte, Target.getExpr(),
                        MCFixupKind(VAX::fixup_vax_pcrel_16),
                        /*PCRel=*/true));
  }
  CB.push_back(0);
  CB.push_back(0);
}

bool VAXMCCodeEmitter::isBranch(unsigned Opcode) const {
  switch (Opcode) {
  case VAX::BRB: case VAX::BRW:
  case VAX::BSBB: case VAX::BSBW:
  case VAX::BEQL: case VAX::BNEQ: case VAX::BGTR: case VAX::BGEQ:
  case VAX::BLSS: case VAX::BLEQ: case VAX::BGTRU: case VAX::BGEQU:
  case VAX::BLSSU: case VAX::BLEQU:
  case VAX::BVC: case VAX::BVS:
    return true;
  default:
    return false;
  }
}

unsigned VAXMCCodeEmitter::getBranchDispSize(unsigned Opcode) const {
  return (Opcode == VAX::BRW || Opcode == VAX::BSBW) ? 2 : 1;
}

unsigned VAXMCCodeEmitter::getTrailingBranchDispSize(unsigned Opcode) const {
  switch (Opcode) {
  // Byte displacement (.bb)
  case VAX::SOBGTR: case VAX::SOBGEQ:
  case VAX::AOBLEQ: case VAX::AOBLSS:
  case VAX::BBS: case VAX::BBC:
  case VAX::BBSS: case VAX::BBCS: case VAX::BBSC: case VAX::BBCC:
  case VAX::BBSSI: case VAX::BBCCI:
  case VAX::BLBS: case VAX::BLBC:
    return 1;
  // Word displacement (.bw)
  case VAX::ACBL: case VAX::ACBW: case VAX::ACBB:
  case VAX::ACBF: case VAX::ACBD:
    return 2;
  default:
    return 0;
  }
}

void VAXMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                         SmallVectorImpl<char> &CB,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const {
  unsigned Opcode = MI.getOpcode();
  const MCInstrDesc &Desc = MCII.get(Opcode);

  // Handle long conditional branch pseudos before the general isPseudo check.
  // These expand to an inverted Bcc + BRW two-instruction sequence.
  switch (Opcode) {
  case VAX::LongBEQL: case VAX::LongBNEQ:
  case VAX::LongBGTR: case VAX::LongBGEQ:
  case VAX::LongBLSS: case VAX::LongBLEQ:
  case VAX::LongBGTRU: case VAX::LongBGEQU:
  case VAX::LongBLSSU: case VAX::LongBLEQU:
    expandLongCondBr(MI, CB, Fixups);
    return;
  default:
    break;
  }

  // Extract hardware opcode and flags from TSFlags.
  // Bit 0: HasMemOp, Bits 1-16: HWOpcode.
  uint64_t TSFlags = Desc.TSFlags;
  uint16_t OpcodeVal = (TSFlags >> 1) & 0xFFFF;

  // Build a set of MCInst operand indices that start memory quads.
  // A VAXMemOp in TableGen expands to 4 MCInst slots: (GPR, i32imm, GPR,
  // i32imm) = (base, disp, index, flags). We detect memory quads by looking
  // for the pattern: reg, imm, reg, imm in the MCInstrDesc operand list.
  bool HasMemOp = TSFlags & 1;
  SmallVector<unsigned, 6> MemOpIndices;
  if (HasMemOp) {
    unsigned NumDescOps = Desc.getNumOperands();
    for (unsigned i = 0; i + 3 < NumDescOps; ++i) {
      // Memory quad: (RegClass>=0, RegClass<0, RegClass>=0, RegClass<0)
      if (Desc.operands()[i].RegClass >= 0 &&
          Desc.operands()[i + 1].RegClass < 0 &&
          Desc.operands()[i + 2].RegClass >= 0 &&
          Desc.operands()[i + 3].RegClass < 0) {
        MemOpIndices.push_back(i);
        i += 3; // skip the remaining 3 sub-operands
      }
    }
  }
  auto isMemOpStart = [&](unsigned Idx) {
    return llvm::is_contained(MemOpIndices, Idx);
  };

  LLVM_DEBUG(dbgs() << "VAXMCCodeEmitter: encoding opcode=" << Opcode
                    << " HWOpcode=0x" << Twine::utohexstr(OpcodeVal)
                    << " isPseudo=" << Desc.isPseudo()
                    << " numOps=" << MI.getNumOperands() << "\n");

  // Skip pseudo-instructions (they should have been lowered by now).
  if (Desc.isPseudo())
    return;

  unsigned StartByte = CB.size();

  // Emit opcode byte(s).
  // For single-byte opcodes (< 0x100), emit one byte.
  // For FD-prefix opcodes (>= 0xFD00), emit 0xFD then the second byte.
  if (OpcodeVal > 0xFF) {
    CB.push_back(static_cast<char>(OpcodeVal >> 8)); // FD prefix
    CB.push_back(static_cast<char>(OpcodeVal & 0xFF));
  } else {
    CB.push_back(static_cast<char>(OpcodeVal));
  }

  // Branch instructions have a direct displacement after the opcode (not
  // an operand specifier).
  if (isBranch(Opcode)) {
    assert(MI.getNumOperands() == 1 && "Branch should have exactly 1 operand");
    emitBranchDisp(MI.getOperand(0), getBranchDispSize(Opcode), CB, Fixups,
                   StartByte);
    return;
  }

  // For non-branch instructions, emit operand specifiers in hardware order.
  // Hardware order = ins operands first, then outs operands.
  // MCInst layout: [outs..., ins...] (defs first, then uses).
  unsigned NumDefs = Desc.getNumDefs();
  unsigned NumOps = MI.getNumOperands();

  // Determine if this instruction has a trailing branch displacement
  // (e.g., SOBxxx, AOBxxx, ACBx, BBxx, BLBx).
  unsigned TrailingBranchSize = getTrailingBranchDispSize(Opcode);

  // Helper: emit one MCInst operand. Returns the number of MCInst operands
  // consumed (4 for memory, 1 otherwise).
  unsigned MemOpCounter = 0; // tracks ordinal of current memory operand
  auto emitOperand = [&](unsigned OpIdx) -> unsigned {
    // Check if this operand is a branch displacement (OPERAND_PCREL).
    if (TrailingBranchSize && OpIdx < Desc.getNumOperands() &&
        Desc.operands()[OpIdx].OperandType == MCOI::OPERAND_PCREL) {
      emitBranchDisp(MI.getOperand(OpIdx), TrailingBranchSize, CB, Fixups,
                     StartByte);
      return 1;
    }

    // Check if this is the start of a 4-slot memory operand.
    if (isMemOpStart(OpIdx)) {
      emitMemOperand(MI.getOperand(OpIdx), MI.getOperand(OpIdx + 1),
                     MI.getOperand(OpIdx + 2), MI.getOperand(OpIdx + 3),
                     CB, Fixups, StartByte, Opcode, MemOpCounter);
      ++MemOpCounter;
      return 4;
    }

    const MCOperand &Op = MI.getOperand(OpIdx);

    LLVM_DEBUG(dbgs() << "  operand[" << OpIdx << "]: "
                      << (Op.isReg() ? "reg" : Op.isImm() ? "imm"
                                             : Op.isExpr() ? "expr"
                                                           : "other")
                      << "\n");

    if (Op.isReg()) {
      emitRegOperand(Op.getReg(), CB);
      return 1;
    }

    if (Op.isImm()) {
      // Determine operand data size from MCInstrDesc OperandType.
      unsigned DataSize = 4; // Default: longword
      if (OpIdx < Desc.getNumOperands()) {
        uint8_t OpType = Desc.operands()[OpIdx].OperandType;
        if (OpType == VAXOp::OPERAND_BYTE_IMM)
          DataSize = 1;
        else if (OpType == VAXOp::OPERAND_WORD_IMM)
          DataSize = 2;
        else if (OpType == VAXOp::OPERAND_QUAD_IMM)
          DataSize = 8;
      }
      emitImmOperand(Op.getImm(), DataSize, CB);
      return 1;
    }

    if (Op.isExpr()) {
      emitExprOperand(Op.getExpr(), CB, Fixups, StartByte);
      return 1;
    }

    llvm_unreachable("Unexpected operand type");
  };

  // Emit ins operands (sources) first, then outs (destinations).
  // Ins start at MCInst operand index NumDefs.
  // Skip tied operands — they repeat a def register and should not produce
  // a separate operand specifier.
  unsigned OpIdx = NumDefs;
  while (OpIdx < NumOps) {
    if (OpIdx < Desc.getNumOperands() &&
        Desc.getOperandConstraint(OpIdx, MCOI::TIED_TO) >= 0) {
      ++OpIdx;
      continue;
    }
    OpIdx += emitOperand(OpIdx);
  }

  // Then emit outs operands (destinations).
  OpIdx = 0;
  while (OpIdx < NumDefs)
    OpIdx += emitOperand(OpIdx);

  // ASHL_iip / MNEGL_ip have a hardcoded -(SP) destination that is not an
  // explicit MCInst operand (it only appears in the asm string).  Emit the
  // autodecrement-SP operand specifier byte that the hardware expects.
  if (Opcode == VAX::ASHL_iip || Opcode == VAX::MNEGL_ip)
    CB.push_back(0x7E); // mode 7 (autodecrement), register 14 (SP)

  LLVM_DEBUG(dbgs() << "  encoded " << (CB.size() - StartByte) << " bytes, "
                    << Fixups.size() << " fixups\n");
}

MCCodeEmitter *llvm::createVAXMCCodeEmitter(const MCInstrInfo &MCII,
                                             MCContext &Ctx) {
  return new VAXMCCodeEmitter(MCII, Ctx);
}
