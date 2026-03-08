//===-- VAXDisassembler.cpp - Disassembler for VAX --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Decodes VAX variable-length byte stream into MCInst instructions.
//
// VAX instruction encoding:
//   - 1 or 2 opcode bytes (2-byte opcodes have 0xFD prefix)
//   - 0 or more operand specifiers, each independently encoded:
//     - Upper nibble of first byte = addressing mode
//     - Lower nibble = register number
//     - Followed by 0-4 bytes of displacement/immediate data
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "TargetInfo/VAXTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "vax-disassembler"

namespace {

/// Operand kind determines which addressing modes are valid during decode.
enum OpKind {
  OK_Reg,  // Register operand: only mode 5 (register direct) valid
  OK_Imm,  // Scalar immediate: modes 0-3 (short literal) or 0x8F (immediate)
  OK_Mem,  // Memory operand (4-slot): any addressing mode valid
};

class VAXDisassembler : public MCDisassembler {
  std::unique_ptr<MCInstrInfo> MCII;

public:
  VAXDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx,
                  std::unique_ptr<MCInstrInfo> MCII)
      : MCDisassembler(STI, Ctx), MCII(std::move(MCII)) {}

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;

  /// Handle VAX function entry masks. Every CALLS/CALLG function starts
  /// with a 2-byte register save mask (.word). When we see a STT_FUNC
  /// symbol, consume those 2 bytes as data so they don't cascade into
  /// misaligned instruction decodes.
  Expected<bool> onSymbolStart(SymbolInfoTy &Symbol, uint64_t &Size,
                               ArrayRef<uint8_t> Bytes,
                               uint64_t Address) const override;

  /// Decode a single operand specifier starting at Bytes[Offset].
  /// Returns the number of bytes consumed, or 0 on failure/mode mismatch.
  /// Adds operand(s) to Instr.
  unsigned decodeOperandSpecifier(ArrayRef<uint8_t> Bytes, unsigned Offset,
                                  MCInst &Instr, OpKind Kind,
                                  unsigned DataSize) const;
};

} // end anonymous namespace

// Map from 4-bit hardware register encoding to LLVM MCRegister.
static MCRegister GPRDecoderTable[] = {
    VAX::R0, VAX::R1, VAX::R2,  VAX::R3,  VAX::R4, VAX::R5,
    VAX::R6, VAX::R7, VAX::R8,  VAX::R9,  VAX::R10, VAX::R11,
    VAX::AP, VAX::FP, VAX::SP,  VAX::PC,
};

/// Return the operand data size in bytes for the given MCInstrDesc operand.
/// Defaults to 4 (longword).
static unsigned getOperandDataSize(const MCInstrDesc &Desc, unsigned OpIdx) {
  if (OpIdx < Desc.getNumOperands()) {
    uint8_t OpType = Desc.operands()[OpIdx].OperandType;
    if (OpType == VAXOp::OPERAND_BYTE_IMM)
      return 1;
    if (OpType == VAXOp::OPERAND_WORD_IMM)
      return 2;
  }
  return 4;
}

/// Check if OpIdx starts a 4-slot memory operand in the MCInstrDesc.
static bool isMemOpStart(const MCInstrDesc &Desc, unsigned OpIdx) {
  unsigned NumOps = Desc.getNumOperands();
  if (OpIdx + 3 >= NumOps)
    return false;
  return Desc.operands()[OpIdx].RegClass >= 0 &&
         Desc.operands()[OpIdx + 1].RegClass < 0 &&
         Desc.operands()[OpIdx + 2].RegClass >= 0 &&
         Desc.operands()[OpIdx + 3].RegClass < 0;
}

/// Count the number of memory (4-slot) operands in an MCInstrDesc.
static unsigned countMemOps(const MCInstrDesc &Desc) {
  unsigned Count = 0;
  unsigned NumOps = Desc.getNumOperands();
  for (unsigned i = 0; i < NumOps;) {
    if (isMemOpStart(Desc, i)) {
      ++Count;
      i += 4;
    } else {
      ++i;
    }
  }
  return Count;
}

/// Collect all LLVM opcodes matching a given hardware opcode.
/// Sorted so that specific variants (fewer memory operands) are tried first,
/// and all-memory variants (most permissive) serve as fallbacks.
static SmallVector<unsigned, 8> lookupOpcodes(const MCInstrInfo &MCII,
                                              uint16_t HWOpcode) {
  SmallVector<unsigned, 8> Result;
  unsigned NumOpcodes = MCII.getNumOpcodes();
  for (unsigned i = 0; i < NumOpcodes; ++i) {
    const MCInstrDesc &Desc = MCII.get(i);
    if (Desc.isPseudo())
      continue;
    uint64_t TSFlags = Desc.TSFlags;
    uint16_t OpcVal = (TSFlags >> 1) & 0xFFFF;
    if (OpcVal == HWOpcode)
      Result.push_back(i);
  }
  // Sort: fewer memory operands first (specific matches before fallbacks).
  llvm::sort(Result, [&](unsigned A, unsigned B) {
    return countMemOps(MCII.get(A)) < countMemOps(MCII.get(B));
  });
  return Result;
}

/// Check if this LLVM opcode is a branch with direct displacement.
static bool isBranch(unsigned Opcode) {
  switch (Opcode) {
  case VAX::BRB: case VAX::BRW:
  case VAX::BEQL: case VAX::BNEQ: case VAX::BGTR: case VAX::BGEQ:
  case VAX::BLSS: case VAX::BLEQ: case VAX::BGTRU: case VAX::BGEQU:
  case VAX::BLSSU: case VAX::BLEQU: case VAX::BVC: case VAX::BVS:
    return true;
  default:
    return false;
  }
}

/// Return the branch displacement size (1 or 2 bytes).
static unsigned getBranchDispSize(unsigned Opcode) {
  return (Opcode == VAX::BRW) ? 2 : 1;
}

unsigned VAXDisassembler::decodeOperandSpecifier(ArrayRef<uint8_t> Bytes,
                                                 unsigned Offset,
                                                 MCInst &Instr, OpKind Kind,
                                                 unsigned DataSize) const {
  if (Offset >= Bytes.size())
    return 0;

  uint8_t Specifier = Bytes[Offset];
  uint8_t Mode = (Specifier >> 4) & 0xF;
  uint8_t RegNum = Specifier & 0xF;

  // Index mode prefix: mode 4 = 0x4R, followed by another operand specifier.
  if (Mode == 0x4) {
    if (Kind != OK_Mem)
      return 0; // Indexed addressing only valid for memory operands
    MCRegister IndexReg = GPRDecoderTable[RegNum];
    // Decode the base operand recursively.
    MCInst TempInst;
    unsigned BaseConsumed =
        decodeOperandSpecifier(Bytes, Offset + 1, TempInst, Kind, DataSize);
    if (BaseConsumed == 0)
      return 0;
    // Copy base operand slots but set the index register (slot 2).
    if (TempInst.getNumOperands() >= 4) {
      Instr.addOperand(TempInst.getOperand(0)); // base reg
      Instr.addOperand(TempInst.getOperand(1)); // displacement
      Instr.addOperand(MCOperand::createReg(IndexReg)); // index reg
      Instr.addOperand(TempInst.getOperand(3)); // flags
    }
    return 1 + BaseConsumed;
  }

  auto readWord = [&](unsigned Off) -> int {
    if (Off + 1 >= Bytes.size())
      return -1;
    return Bytes[Off] | (Bytes[Off + 1] << 8);
  };

  auto readLong = [&](unsigned Off) -> int64_t {
    if (Off + 3 >= Bytes.size())
      return -1;
    return Bytes[Off] | (Bytes[Off + 1] << 8) | (Bytes[Off + 2] << 16) |
           (static_cast<uint32_t>(Bytes[Off + 3]) << 24);
  };

  switch (Mode) {
  case 0x0: case 0x1: case 0x2: case 0x3:
    // Short literal: value is the specifier byte itself (0-63).
    if (Kind == OK_Reg)
      return 0; // Short literal can't be a register operand
    if (Kind == OK_Mem) {
      Instr.addOperand(MCOperand::createReg(0));
      Instr.addOperand(MCOperand::createImm(Specifier));
      Instr.addOperand(MCOperand::createReg(0));
      Instr.addOperand(MCOperand::createImm(7)); // VAXAM::Imm
    } else {
      Instr.addOperand(MCOperand::createImm(Specifier));
    }
    return 1;

  case 0x5:
    // Register direct: %Rn
    if (RegNum > 15)
      return 0;
    if (Kind == OK_Imm)
      return 0; // Register can't be a scalar immediate
    if (Kind == OK_Mem) {
      Instr.addOperand(MCOperand::createReg(GPRDecoderTable[RegNum]));
      Instr.addOperand(MCOperand::createImm(0));
      Instr.addOperand(MCOperand::createReg(0));
      Instr.addOperand(MCOperand::createImm(1)); // VAXAM::RegDirect
    } else {
      Instr.addOperand(MCOperand::createReg(GPRDecoderTable[RegNum]));
    }
    return 1;

  case 0x6:
    // Register deferred: (%Rn) — memory-only addressing mode
    if (Kind != OK_Mem)
      return 0;
    Instr.addOperand(MCOperand::createReg(GPRDecoderTable[RegNum]));
    Instr.addOperand(MCOperand::createImm(0));
    Instr.addOperand(MCOperand::createReg(0));
    Instr.addOperand(MCOperand::createImm(2)); // VAXAM::RegDeferred
    return 1;

  case 0x7:
    // Autodecrement: -(%Rn) — memory-only
    if (Kind != OK_Mem)
      return 0;
    Instr.addOperand(MCOperand::createReg(GPRDecoderTable[RegNum]));
    Instr.addOperand(MCOperand::createImm(0));
    Instr.addOperand(MCOperand::createReg(0));
    Instr.addOperand(MCOperand::createImm(3)); // VAXAM::AutoDec
    return 1;

  case 0x8:
    if (RegNum == 0xF) {
      // Immediate mode: 0x8F + N bytes of data.
      if (Kind == OK_Reg)
        return 0; // Immediate can't be a register operand
      if (Offset + 1 + DataSize > Bytes.size())
        return 0;
      int64_t Val = 0;
      for (unsigned i = 0; i < DataSize; ++i)
        Val |= static_cast<int64_t>(Bytes[Offset + 1 + i]) << (i * 8);
      if (DataSize == 1)
        Val = static_cast<int8_t>(Val);
      else if (DataSize == 2)
        Val = static_cast<int16_t>(Val);
      else if (DataSize == 4)
        Val = static_cast<int32_t>(Val);

      if (Kind == OK_Mem) {
        Instr.addOperand(MCOperand::createReg(0));
        Instr.addOperand(MCOperand::createImm(Val));
        Instr.addOperand(MCOperand::createReg(0));
        Instr.addOperand(MCOperand::createImm(7)); // VAXAM::Imm
      } else {
        Instr.addOperand(MCOperand::createImm(Val));
      }
      return 1 + DataSize;
    }
    // Autoincrement: (%Rn)+ — memory-only
    if (Kind != OK_Mem)
      return 0;
    Instr.addOperand(MCOperand::createReg(GPRDecoderTable[RegNum]));
    Instr.addOperand(MCOperand::createImm(0));
    Instr.addOperand(MCOperand::createReg(0));
    Instr.addOperand(MCOperand::createImm(4)); // VAXAM::AutoInc
    return 1;

  case 0x9:
    if (RegNum == 0xF) {
      // Absolute: 0x9F + 4-byte address — memory-only
      if (Kind != OK_Mem)
        return 0;
      if (Offset + 5 > Bytes.size())
        return 0;
      int64_t Addr = readLong(Offset + 1);
      Instr.addOperand(MCOperand::createReg(0));
      Instr.addOperand(MCOperand::createImm(Addr));
      Instr.addOperand(MCOperand::createReg(0));
      Instr.addOperand(MCOperand::createImm(8)); // VAXAM::Absolute
      return 5;
    }
    // Autoincrement deferred: *(%Rn)+ — memory-only
    if (Kind != OK_Mem)
      return 0;
    Instr.addOperand(MCOperand::createReg(GPRDecoderTable[RegNum]));
    Instr.addOperand(MCOperand::createImm(0));
    Instr.addOperand(MCOperand::createReg(0));
    Instr.addOperand(MCOperand::createImm(6)); // VAXAM::AutoIncDef
    return 1;

  case 0xA: {
    // Byte displacement: 0xA0|Rn + 1 byte — memory-only
    if (Kind != OK_Mem)
      return 0;
    if (Offset + 2 > Bytes.size())
      return 0;
    int8_t Disp = static_cast<int8_t>(Bytes[Offset + 1]);
    Instr.addOperand(MCOperand::createReg(GPRDecoderTable[RegNum]));
    Instr.addOperand(MCOperand::createImm(Disp));
    Instr.addOperand(MCOperand::createReg(0));
    Instr.addOperand(MCOperand::createImm(0)); // VAXAM::Disp
    return 2;
  }

  case 0xB: {
    // Byte displacement deferred — memory-only
    if (Kind != OK_Mem)
      return 0;
    if (Offset + 2 > Bytes.size())
      return 0;
    int8_t Disp = static_cast<int8_t>(Bytes[Offset + 1]);
    Instr.addOperand(MCOperand::createReg(GPRDecoderTable[RegNum]));
    Instr.addOperand(MCOperand::createImm(Disp));
    Instr.addOperand(MCOperand::createReg(0));
    Instr.addOperand(MCOperand::createImm(5)); // VAXAM::DispDeferred
    return 2;
  }

  case 0xC: {
    // Word displacement — memory-only
    if (Kind != OK_Mem)
      return 0;
    if (Offset + 3 > Bytes.size())
      return 0;
    int16_t Disp = static_cast<int16_t>(readWord(Offset + 1));
    Instr.addOperand(MCOperand::createReg(GPRDecoderTable[RegNum]));
    Instr.addOperand(MCOperand::createImm(Disp));
    Instr.addOperand(MCOperand::createReg(0));
    Instr.addOperand(MCOperand::createImm(0)); // VAXAM::Disp
    return 3;
  }

  case 0xD: {
    // Word displacement deferred — memory-only
    if (Kind != OK_Mem)
      return 0;
    if (Offset + 3 > Bytes.size())
      return 0;
    int16_t Disp = static_cast<int16_t>(readWord(Offset + 1));
    Instr.addOperand(MCOperand::createReg(GPRDecoderTable[RegNum]));
    Instr.addOperand(MCOperand::createImm(Disp));
    Instr.addOperand(MCOperand::createReg(0));
    Instr.addOperand(MCOperand::createImm(5)); // VAXAM::DispDeferred
    return 3;
  }

  case 0xE: {
    // Longword displacement — memory-only
    if (Kind != OK_Mem)
      return 0;
    if (Offset + 5 > Bytes.size())
      return 0;
    int32_t Disp = static_cast<int32_t>(readLong(Offset + 1));
    Instr.addOperand(MCOperand::createReg(GPRDecoderTable[RegNum]));
    Instr.addOperand(MCOperand::createImm(Disp));
    Instr.addOperand(MCOperand::createReg(0));
    Instr.addOperand(MCOperand::createImm(0)); // VAXAM::Disp
    return 5;
  }

  case 0xF: {
    // Longword displacement deferred — memory-only
    if (Kind != OK_Mem)
      return 0;
    if (Offset + 5 > Bytes.size())
      return 0;
    int32_t Disp = static_cast<int32_t>(readLong(Offset + 1));
    Instr.addOperand(MCOperand::createReg(GPRDecoderTable[RegNum]));
    Instr.addOperand(MCOperand::createImm(Disp));
    Instr.addOperand(MCOperand::createReg(0));
    Instr.addOperand(MCOperand::createImm(5)); // VAXAM::DispDeferred
    return 5;
  }

  default:
    return 0;
  }
}

/// Determine the OpKind for a given operand in the MCInstrDesc.
static OpKind getOpKind(const MCInstrDesc &Desc, unsigned OpIdx) {
  if (isMemOpStart(Desc, OpIdx))
    return OK_Mem;
  if (OpIdx < Desc.getNumOperands() && Desc.operands()[OpIdx].RegClass >= 0)
    return OK_Reg;
  return OK_Imm;
}

/// Try to decode one instruction candidate. Returns true on success.
/// On success, populates Instr and sets BytesConsumed.
static bool tryDecodeCandidate(const VAXDisassembler &Dis,
                               const MCInstrInfo &MCII, unsigned Opcode,
                               ArrayRef<uint8_t> Bytes, unsigned OpcodeSize,
                               uint64_t Address, MCInst &Instr,
                               unsigned &BytesConsumed) {
  const MCInstrDesc &Desc = MCII.get(Opcode);
  unsigned Offset = OpcodeSize;
  unsigned NumDefs = Desc.getNumDefs();
  unsigned NumDescOps = Desc.getNumOperands();

  // Build operand descriptor list in hardware encoding order (ins, then outs).
  struct OpDesc {
    unsigned DescIdx;
    OpKind Kind;
    unsigned DataSize;
    unsigned NumSlots; // 4 for mem, 1 for scalar
  };
  SmallVector<OpDesc, 8> HWOrder;

  // Ins first (starting from NumDefs, skipping tied).
  for (unsigned i = NumDefs; i < NumDescOps;) {
    if (Desc.getOperandConstraint(i, MCOI::TIED_TO) >= 0) {
      ++i;
      continue;
    }
    OpKind Kind = getOpKind(Desc, i);
    unsigned DS = getOperandDataSize(Desc, i);
    unsigned Slots = (Kind == OK_Mem) ? 4 : 1;
    HWOrder.push_back({i, Kind, DS, Slots});
    i += Slots;
  }
  // Then outs (0 to NumDefs-1).
  for (unsigned i = 0; i < NumDefs;) {
    OpKind Kind = getOpKind(Desc, i);
    unsigned DS = getOperandDataSize(Desc, i);
    unsigned Slots = (Kind == OK_Mem) ? 4 : 1;
    HWOrder.push_back({i, Kind, DS, Slots});
    i += Slots;
  }

  // Decode each operand from the byte stream.
  SmallVector<SmallVector<MCOperand, 4>, 8> DecodedOps(HWOrder.size());

  for (unsigned i = 0; i < HWOrder.size(); ++i) {
    MCInst TempInst;
    unsigned Consumed = Dis.decodeOperandSpecifier(
        Bytes, Offset, TempInst, HWOrder[i].Kind, HWOrder[i].DataSize);
    if (Consumed == 0)
      return false; // This candidate doesn't match the byte stream
    Offset += Consumed;

    for (unsigned j = 0; j < TempInst.getNumOperands(); ++j)
      DecodedOps[i].push_back(TempInst.getOperand(j));
  }

  // Success — assemble the MCInst in the correct order (outs then ins).
  Instr.setOpcode(Opcode);

  // Count ins for reordering.
  unsigned NumIns = 0;
  for (unsigned i = NumDefs; i < NumDescOps;) {
    if (Desc.getOperandConstraint(i, MCOI::TIED_TO) >= 0) {
      ++i;
      continue;
    }
    OpKind Kind = getOpKind(Desc, i);
    ++NumIns;
    i += (Kind == OK_Mem) ? 4 : 1;
  }

  // Outs first (at end of HWOrder, starting at NumIns).
  for (unsigned i = NumIns; i < HWOrder.size(); ++i)
    for (auto &Op : DecodedOps[i])
      Instr.addOperand(Op);

  // Ins with tied operand handling.
  unsigned InsIdx = 0;
  for (unsigned i = NumDefs; i < NumDescOps;) {
    if (Desc.getOperandConstraint(i, MCOI::TIED_TO) >= 0) {
      int TiedTo = Desc.getOperandConstraint(i, MCOI::TIED_TO);
      if (TiedTo >= 0 && static_cast<unsigned>(TiedTo) < Instr.getNumOperands())
        Instr.addOperand(Instr.getOperand(TiedTo));
      ++i;
      continue;
    }
    for (auto &Op : DecodedOps[InsIdx])
      Instr.addOperand(Op);
    ++InsIdx;
    OpKind Kind = getOpKind(Desc, i);
    i += (Kind == OK_Mem) ? 4 : 1;
  }

  BytesConsumed = Offset;
  return true;
}

Expected<bool>
VAXDisassembler::onSymbolStart(SymbolInfoTy &Symbol, uint64_t &Size,
                               ArrayRef<uint8_t> Bytes,
                               uint64_t Address) const {
  // VAX functions using CALLS/CALLG convention start with a 2-byte entry mask
  // (.word) that encodes which registers to save. This is data, not an
  // instruction. If we don't skip it, the disassembler will try to decode
  // it as an instruction, often consuming subsequent real instruction bytes
  // and causing a cascade of decode failures.
  //
  // We skip the entry mask for STT_FUNC symbols. The mask uses bits 0-11
  // for R0-R11; bits 12-13 must be 0 (AP/FP saved implicitly by CALLS);
  // bit 14 is IV (integer overflow trap enable); bit 15 is DV (decimal
  // overflow trap enable).
  if (Symbol.Type == ELF::STT_FUNC && Bytes.size() >= 2) {
    Size = 2;
    return true;
  }
  return false;
}

MCDisassembler::DecodeStatus
VAXDisassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                ArrayRef<uint8_t> Bytes, uint64_t Address,
                                raw_ostream &CStream) const {
  Size = 0;
  if (Bytes.empty())
    return Fail;

  // Decode opcode: single byte or 0xFD prefix + second byte.
  uint16_t HWOpcode;
  unsigned OpcodeSize;
  if (Bytes[0] == 0xFD) {
    if (Bytes.size() < 2)
      return Fail;
    HWOpcode = 0xFD00 | Bytes[1];
    OpcodeSize = 2;
  } else {
    HWOpcode = Bytes[0];
    OpcodeSize = 1;
  }

  // Find all LLVM instruction definitions matching this hardware opcode.
  SmallVector<unsigned, 8> Candidates = lookupOpcodes(*MCII, HWOpcode);
  if (Candidates.empty()) {
    Size = OpcodeSize;
    return Fail;
  }

  // Branch instructions: direct displacement after opcode (no operand specifiers).
  for (unsigned Opcode : Candidates) {
    if (isBranch(Opcode)) {
      unsigned DispSize = getBranchDispSize(Opcode);
      if (OpcodeSize + DispSize > Bytes.size())
        continue;
      int64_t Disp;
      if (DispSize == 1)
        Disp = static_cast<int8_t>(Bytes[OpcodeSize]);
      else
        Disp = static_cast<int16_t>(Bytes[OpcodeSize] |
                                     (Bytes[OpcodeSize + 1] << 8));
      int64_t Target = Address + OpcodeSize + DispSize + Disp;
      Instr.setOpcode(Opcode);
      Instr.addOperand(MCOperand::createImm(Target));
      Size = OpcodeSize + DispSize;
      return Success;
    }
  }

  // Try each candidate — pick the first one where all operand specifiers
  // match the expected types (register, immediate, or memory).
  for (unsigned Opcode : Candidates) {
    MCInst TryInst;
    unsigned BytesConsumed = 0;
    if (tryDecodeCandidate(*this, *MCII, Opcode, Bytes, OpcodeSize, Address,
                           TryInst, BytesConsumed)) {
      Instr = TryInst;
      Size = BytesConsumed;
      return Success;
    }
  }

  // No candidate matched.
  Size = OpcodeSize;
  return Fail;
}

static MCDisassembler *createVAXDisassembler(const Target &T,
                                             const MCSubtargetInfo &STI,
                                             MCContext &Ctx) {
  std::unique_ptr<MCInstrInfo> MCII(T.createMCInstrInfo());
  return new VAXDisassembler(STI, Ctx, std::move(MCII));
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeVAXDisassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheVAXTarget(),
                                         createVAXDisassembler);
}
