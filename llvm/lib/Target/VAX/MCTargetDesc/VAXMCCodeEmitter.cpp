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
#include "VAXMCTargetDesc.h"
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
#include <climits>

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
  /// mode depending on value).
  void emitImmOperand(int64_t Imm, SmallVectorImpl<char> &CB) const;

  /// Emit an operand specifier for an expression operand (PC-relative).
  void emitExprOperand(const MCExpr *Expr, SmallVectorImpl<char> &CB,
                       SmallVectorImpl<MCFixup> &Fixups,
                       unsigned StartByte) const;

  /// Emit an operand specifier for a memory operand (base + displacement).
  void emitMemOperand(const MCOperand &Base, const MCOperand &Disp,
                      SmallVectorImpl<char> &CB,
                      SmallVectorImpl<MCFixup> &Fixups,
                      unsigned StartByte) const;

  /// Emit a branch displacement with the appropriate fixup.
  void emitBranchDisp(const MCOperand &Target, unsigned DispSize,
                      SmallVectorImpl<char> &CB,
                      SmallVectorImpl<MCFixup> &Fixups,
                      unsigned StartByte) const;

  /// Return true if this opcode is a branch instruction with a direct
  /// displacement (not an operand-specifier branch like JMP).
  bool isBranch(unsigned Opcode) const;

  /// Return the displacement size for a branch opcode (1 or 2 bytes).
  unsigned getBranchDispSize(unsigned Opcode) const;
};

} // end anonymous namespace

unsigned VAXMCCodeEmitter::getRegEncoding(MCRegister Reg) const {
  const MCRegisterInfo &MRI = *Ctx.getRegisterInfo();

  // For QPR registers (register pairs), use the low register's encoding.
  if (unsigned SubReg = MRI.getSubReg(Reg, sub_lo))
    return MRI.getEncodingValue(SubReg);

  return MRI.getEncodingValue(Reg);
}

void VAXMCCodeEmitter::emitRegOperand(MCRegister Reg,
                                      SmallVectorImpl<char> &CB) const {
  // Register mode: specifier byte = 0x50 | regnum
  CB.push_back(0x50 | getRegEncoding(Reg));
}

void VAXMCCodeEmitter::emitImmOperand(int64_t Imm,
                                      SmallVectorImpl<char> &CB) const {
  // Short literal mode: values 0-63 encode in a single specifier byte.
  if (Imm >= 0 && Imm <= 63) {
    CB.push_back(static_cast<char>(Imm));
    return;
  }

  // Longword immediate mode: 0x8F (autoincrement PC) + 4-byte LE value.
  CB.push_back(static_cast<char>(0x8F));
  uint32_t Val = static_cast<uint32_t>(Imm);
  CB.push_back(static_cast<char>(Val & 0xFF));
  CB.push_back(static_cast<char>((Val >> 8) & 0xFF));
  CB.push_back(static_cast<char>((Val >> 16) & 0xFF));
  CB.push_back(static_cast<char>((Val >> 24) & 0xFF));
}

void VAXMCCodeEmitter::emitExprOperand(const MCExpr *Expr,
                                       SmallVectorImpl<char> &CB,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       unsigned StartByte) const {
  // PC-relative longword displacement: 0xEF + 4-byte displacement.
  CB.push_back(static_cast<char>(0xEF));
  unsigned FixOff = CB.size() - StartByte;
  Fixups.push_back(
      MCFixup::create(FixOff, Expr, MCFixupKind(VAX::fixup_vax_pcrel_32),
                      /*PCRel=*/true));
  // Emit 4 placeholder bytes for the fixup.
  CB.push_back(0);
  CB.push_back(0);
  CB.push_back(0);
  CB.push_back(0);
}

void VAXMCCodeEmitter::emitMemOperand(const MCOperand &Base,
                                      const MCOperand &Disp,
                                      SmallVectorImpl<char> &CB,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      unsigned StartByte) const {
  assert(Base.isReg() && "Memory base must be a register");
  unsigned BaseReg = Base.getReg() ? getRegEncoding(Base.getReg()) : 0;

  // No base register: this was a bare immediate ($value) morphed to Mem.
  // Encode as immediate mode (literal or immediate operand specifier).
  if (!Base.getReg()) {
    if (Disp.isImm()) {
      emitImmOperand(Disp.getImm(), CB);
      return;
    }
    if (Disp.isExpr()) {
      // PC-relative or absolute expression.
      emitExprOperand(Disp.getExpr(), CB, Fixups, StartByte);
      return;
    }
    // Null displacement (bare $0 case).
    emitImmOperand(0, CB);
    return;
  }

  if (Disp.isExpr()) {
    // Expression displacement — emit as longword displacement + fixup.
    // If base is PC (0xF), this is PC-relative.
    CB.push_back(static_cast<char>(0xE0 | BaseReg));
    unsigned FixOff = CB.size() - StartByte;
    CB.push_back(0);
    CB.push_back(0);
    CB.push_back(0);
    CB.push_back(0);
    MCFixupKind Kind = (BaseReg == 0xF)
                           ? MCFixupKind(VAX::fixup_vax_pcrel_32)
                           : MCFixupKind(FK_Data_4);
    bool IsPCRel = (BaseReg == 0xF);
    Fixups.push_back(MCFixup::create(FixOff, Disp.getExpr(), Kind, IsPCRel));
    return;
  }

  assert(Disp.isImm() && "Memory displacement must be immediate or expression");
  int64_t DispVal = Disp.getImm();

  // Sentinel value INT32_MIN means "register direct mode" — a bare register
  // that was morphed to a Mem pair by the AsmParser.
  if (DispVal == INT32_MIN) {
    CB.push_back(static_cast<char>(0x50 | BaseReg));
    return;
  }

  if (DispVal == 0) {
    // Zero displacement → register deferred mode (0x60 | reg).
    // But if the base is FP/AP/SP, keep displacement form for clarity and to
    // match GAS output. Actually for encoding correctness, register deferred
    // IS valid and shorter.
    CB.push_back(static_cast<char>(0x60 | BaseReg));
    return;
  }

  if (DispVal >= -128 && DispVal <= 127) {
    // Byte displacement: 0xA0 | reg + 1-byte signed.
    CB.push_back(static_cast<char>(0xA0 | BaseReg));
    CB.push_back(static_cast<char>(DispVal & 0xFF));
    return;
  }

  if (DispVal >= -32768 && DispVal <= 32767) {
    // Word displacement: 0xC0 | reg + 2-byte signed LE.
    CB.push_back(static_cast<char>(0xC0 | BaseReg));
    uint16_t Val = static_cast<uint16_t>(DispVal);
    CB.push_back(static_cast<char>(Val & 0xFF));
    CB.push_back(static_cast<char>((Val >> 8) & 0xFF));
    return;
  }

  // Longword displacement: 0xE0 | reg + 4-byte signed LE.
  CB.push_back(static_cast<char>(0xE0 | BaseReg));
  uint32_t Val = static_cast<uint32_t>(DispVal);
  CB.push_back(static_cast<char>(Val & 0xFF));
  CB.push_back(static_cast<char>((Val >> 8) & 0xFF));
  CB.push_back(static_cast<char>((Val >> 16) & 0xFF));
  CB.push_back(static_cast<char>((Val >> 24) & 0xFF));
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

bool VAXMCCodeEmitter::isBranch(unsigned Opcode) const {
  switch (Opcode) {
  case VAX::BRB: case VAX::BRW:
  case VAX::BEQL: case VAX::BNEQ: case VAX::BGTR: case VAX::BGEQ:
  case VAX::BLSS: case VAX::BLEQ: case VAX::BGTRU: case VAX::BGEQU:
  case VAX::BLSSU: case VAX::BLEQU:
    return true;
  default:
    return false;
  }
}

unsigned VAXMCCodeEmitter::getBranchDispSize(unsigned Opcode) const {
  return (Opcode == VAX::BRW) ? 2 : 1;
}

void VAXMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                         SmallVectorImpl<char> &CB,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const {
  unsigned Opcode = MI.getOpcode();
  const MCInstrDesc &Desc = MCII.get(Opcode);

  // Extract hardware opcode and flags from TSFlags.
  // Bit 0: HasMemOp, Bits 1-16: HWOpcode.
  uint64_t TSFlags = Desc.TSFlags;
  uint16_t OpcodeVal = (TSFlags >> 1) & 0xFFFF;

  // Build a set of MCInst operand indices that start memory pairs.
  // A VAXMemOp in TableGen expands to 2 MCInst slots: (GPR, i32imm).
  // We detect memory pairs by looking for a register operand immediately
  // followed by an immediate operand in the MCInstrDesc.
  bool HasMemOp = TSFlags & 1;
  SmallVector<unsigned, 6> MemOpIndices;
  if (HasMemOp) {
    unsigned NumDescOps = Desc.getNumOperands();
    for (unsigned i = 0; i + 1 < NumDescOps; ++i) {
      // Memory pair: register sub-operand (has RegClass) followed by
      // immediate sub-operand (no RegClass, rc == -1).
      if (Desc.operands()[i].RegClass >= 0 &&
          Desc.operands()[i + 1].RegClass < 0) {
        MemOpIndices.push_back(i);
        ++i; // skip the immediate sub-operand
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

  // Helper: emit one MCInst operand. Returns the number of MCInst operands
  // consumed (2 for memory, 1 otherwise).
  auto emitOperand = [&](unsigned OpIdx) -> unsigned {
    // Check if this is the start of a memory operand pair.
    if (isMemOpStart(OpIdx)) {
      emitMemOperand(MI.getOperand(OpIdx), MI.getOperand(OpIdx + 1), CB,
                     Fixups, StartByte);
      return 2;
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
      emitImmOperand(Op.getImm(), CB);
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

  LLVM_DEBUG(dbgs() << "  encoded " << (CB.size() - StartByte) << " bytes, "
                    << Fixups.size() << " fixups\n");
}

MCCodeEmitter *llvm::createVAXMCCodeEmitter(const MCInstrInfo &MCII,
                                             MCContext &Ctx) {
  return new VAXMCCodeEmitter(MCII, Ctx);
}
