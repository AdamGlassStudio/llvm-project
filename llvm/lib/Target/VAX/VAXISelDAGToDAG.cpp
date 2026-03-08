//===-- VAXISelDAGToDAG.cpp - VAX DAG to DAG Instruction Selector -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAX.h"
#include "VAXISelLowering.h"
#include "VAXTargetMachine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "vax-isel"

namespace {

class VAXDAGToDAGISel : public SelectionDAGISel {
public:
  explicit VAXDAGToDAGISel(VAXTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

  void Select(SDNode *N) override;

  // ComplexPattern selector for base+displacement memory operands.
  // Returns true and sets Base/Offset/Index/Flags when Addr matches a known
  // form: FrameIndex, ADD(FrameIndex, Const), ADD(Reg, Const), bare Reg.
  // Index is always NoReg — use SelectVAXAddrLong for indexed addressing.
  bool SelectVAXAddr(SDValue Addr, SDValue &Base, SDValue &Offset,
                     SDValue &Index, SDValue &Flags);

  // Enhanced ComplexPattern selector for longword (4-byte) memory operands.
  // Matches everything SelectVAXAddr does, plus indexed addressing:
  //   ADD(base, SHL(idx, 2))  →  base[idx]  (scale=4, longword)
  // Only valid for instructions with 4-byte data size (MOVL, MOVF, etc.)
  // because VAX indexed mode implicitly scales by the instruction's data size.
  bool SelectVAXAddrLong(SDValue Addr, SDValue &Base, SDValue &Offset,
                         SDValue &Index, SDValue &Flags);

  // Handle 'm' constraint for inline assembly memory operands.
  bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                    InlineAsm::ConstraintCode ConstraintID,
                                    std::vector<SDValue> &OutOps) override;

// Include the pieces auto-generated from the target description.
#include "VAXGenDAGISel.inc"
};

} // end anonymous namespace

void VAXDAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;
  }

  if (N->getOpcode() == ISD::FrameIndex) {
    // Materialize a frame slot address as a register value (e.g., sret pointer).
    // Emit: MOVL_rm Rdst, 0(FI) — but this would be a load, not LEA.
    // Instead, use SUBL3_ir: subl3 FI, $0, Rdst → after eliminateFrameIndex
    // becomes subl3 %fp, $-offset, Rdst = FP + offset.
    // Simplest: emit ADDL3_ri $0, FI, Rdst with the FrameIndex in an
    // operand position that eliminateFrameIndex can resolve.
    //
    // We use the LEA_FI pseudo: dst = FI + 0, which eliminateFrameIndex
    // converts to ADDL3_ri $offset, %fp, $dst.
    FrameIndexSDNode *FIN = cast<FrameIndexSDNode>(N);
    SDValue FI = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i32);
    SDValue Zero = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i32);
    SDValue NoReg = CurDAG->getRegister(0, MVT::i32);
    SDValue Flags = CurDAG->getTargetConstant(VAXAM::Disp, SDLoc(N), MVT::i32);
    SmallVector<SDValue, 4> Ops = {FI, Zero, NoReg, Flags};
    SDNode *Lea = CurDAG->getMachineNode(
        VAX::LEA_FI, SDLoc(N), MVT::i32, Ops);
    ReplaceNode(N, Lea);
    return;
  }

  // ConstantFP f64: materialize as a constant pool load. isFPImmLegal(f64)
  // returns true to prevent DAGCombiner from splitting into IEEE i32 stores.
  // We must handle the surviving ConstantFP here by creating a MOVD from CP.
  if (N->getOpcode() == ISD::ConstantFP && N->getValueType(0) == MVT::f64) {
    SDLoc DL(N);
    auto *CFP = cast<ConstantFPSDNode>(N);
    const Constant *C = ConstantFP::get(*CurDAG->getContext(),
                                         CFP->getValueAPF());
    SDValue CPI = CurDAG->getTargetConstantPool(C, MVT::i32, Align(4));
    // MOVD_rcp takes a single i32imm operand (the constant pool symbol).
    SDNode *Load = CurDAG->getMachineNode(
        VAX::MOVD_rcp, DL, MVT::f64, {CPI});
    ReplaceNode(N, Load);
    return;
  }

  // ConstantFP f32: same treatment — materialize via constant pool to get
  // correct VAX F_float encoding (AsmPrinter converts IEEE to F_float).
  if (N->getOpcode() == ISD::ConstantFP && N->getValueType(0) == MVT::f32) {
    SDLoc DL(N);
    auto *CFP = cast<ConstantFPSDNode>(N);
    const Constant *C = ConstantFP::get(*CurDAG->getContext(),
                                         CFP->getValueAPF());
    SDValue CPI = CurDAG->getTargetConstantPool(C, MVT::i32, Align(4));
    SDNode *Load = CurDAG->getMachineNode(
        VAX::MOVF_rcp, DL, MVT::f32, {CPI});
    ReplaceNode(N, Load);
    return;
  }

  // PUSHL of a PCRelWrapper → PUSHAL (single instruction instead of
  // MOVAL + PUSHL). This folds address materialization into the push.
  if (N->getOpcode() == VAXISD::PUSHL) {
    SDValue Arg = N->getOperand(1);
    if (Arg.getOpcode() == VAXISD::PCRelWrapper) {
      SDValue Sym = Arg.getOperand(0);
      unsigned Opc;
      switch (Sym.getOpcode()) {
      case ISD::TargetGlobalAddress:  Opc = VAX::PUSHAL_ga; break;
      case ISD::TargetConstantPool:   Opc = VAX::PUSHAL_cp; break;
      case ISD::TargetBlockAddress:   Opc = VAX::PUSHAL_ba; break;
      case ISD::TargetExternalSymbol: Opc = VAX::PUSHAL_es; break;
      default: goto fallthrough_pushl;
      }
      SDValue Chain = N->getOperand(0);
      SDNode *Push = CurDAG->getMachineNode(
          Opc, SDLoc(N), N->getVTList(), {Sym, Chain});
      ReplaceNode(N, Push);
      return;
    }
  }
fallthrough_pushl:

  if (N->getOpcode() == VAXISD::CALL) {
    SDValue Chain   = N->getOperand(0);
    SDValue NumArgs = N->getOperand(1);
    SDValue Callee  = N->getOperand(2);
    SDValue RegMask = N->getOperand(3);

    bool isDirect = (Callee.getOpcode() == ISD::TargetGlobalAddress ||
                     Callee.getOpcode() == ISD::TargetExternalSymbol);
    unsigned Opc = isDirect ? VAX::CALLS_direct : VAX::CALLS_indir;
    // Convert arg count to a target immediate for the machine instruction.
    auto *CN = cast<ConstantSDNode>(NumArgs);
    SDValue Count = CurDAG->getTargetConstant(
        CN->getZExtValue(), SDLoc(N), MVT::i32);
    SmallVector<SDValue, 4> Ops = { Count, Callee, RegMask, Chain };

    SDNode *Call = CurDAG->getMachineNode(Opc, SDLoc(N), N->getVTList(), Ops);
    ReplaceNode(N, Call);
    return;
  }

  // ASHQ — quadword shift. The VAXISD::ASHQ node has i32 operands (cnt, lo,
  // hi) and i32 results (lo, hi), but the hardware ASHQ reads/writes
  // consecutive register pairs. Build REG_SEQUENCE to form QPR, emit ASHQ,
  // then EXTRACT_SUBREG to get the i32 halves back.
  if (N->getOpcode() == VAXISD::ASHQ) {
    SDLoc DL(N);
    SDValue Cnt   = N->getOperand(0);
    SDValue SrcLo = N->getOperand(1);
    SDValue SrcHi = N->getOperand(2);

    // Build QPR from i32 halves: REG_SEQUENCE QPRRegClass, SrcLo, VAX::sub_lo, SrcHi, VAX::sub_hi.
    SDValue QprRC = CurDAG->getTargetConstant(VAX::QPRRegClassID, DL, MVT::i32);
    SDValue SubLoIdx = CurDAG->getTargetConstant(VAX::sub_lo, DL, MVT::i32);
    SDValue SubHiIdx = CurDAG->getTargetConstant(VAX::sub_hi, DL, MVT::i32);
    SDNode *SrcPair = CurDAG->getMachineNode(
        TargetOpcode::REG_SEQUENCE, DL, MVT::i64,
        {QprRC, SrcLo, SubLoIdx, SrcHi, SubHiIdx});

    // Choose register or immediate count variant.
    unsigned Opc = VAX::ASHQ;
    if (ConstantSDNode *CntC = dyn_cast<ConstantSDNode>(Cnt)) {
      Opc = VAX::ASHQ_i;
      Cnt = CurDAG->getTargetConstant(CntC->getSExtValue(), DL, MVT::i32);
    }

    // Emit ASHQ: (QPR:$dst) = ashq cnt, QPR:$src.
    SDNode *Ashq = CurDAG->getMachineNode(
        Opc, DL, MVT::i64, {Cnt, SDValue(SrcPair, 0)});

    // Extract i32 halves from QPR result.
    SDNode *Lo = CurDAG->getMachineNode(
        TargetOpcode::EXTRACT_SUBREG, DL, MVT::i32,
        SDValue(Ashq, 0), SubLoIdx);
    SDNode *Hi = CurDAG->getMachineNode(
        TargetOpcode::EXTRACT_SUBREG, DL, MVT::i32,
        SDValue(Ashq, 0), SubHiIdx);

    ReplaceUses(SDValue(N, 0), SDValue(Lo, 0));
    ReplaceUses(SDValue(N, 1), SDValue(Hi, 0));
    CurDAG->RemoveDeadNode(N);
    return;
  }

  // EMUL — extended multiply. Inputs are i32, output is quadword (QPR).
  if (N->getOpcode() == VAXISD::EMUL) {
    SDLoc DL(N);
    SDValue Mulr   = N->getOperand(0);
    SDValue Muld   = N->getOperand(1);
    SDValue Addend = N->getOperand(2);

    // Addend must be an immediate (we always pass 0 from LowerSMUL_LOHI).
    SDValue Add = CurDAG->getTargetConstant(
        cast<ConstantSDNode>(Addend)->getSExtValue(), DL, MVT::i32);

    // Emit EMUL: (QPR:$dst) = emul mulr, muld, add.
    SDNode *Emul = CurDAG->getMachineNode(
        VAX::EMUL, DL, MVT::i64, {Mulr, Muld, Add});

    // Extract i32 halves.
    SDValue SubLoIdx = CurDAG->getTargetConstant(VAX::sub_lo, DL, MVT::i32);
    SDValue SubHiIdx = CurDAG->getTargetConstant(VAX::sub_hi, DL, MVT::i32);
    SDNode *Lo = CurDAG->getMachineNode(
        TargetOpcode::EXTRACT_SUBREG, DL, MVT::i32,
        SDValue(Emul, 0), SubLoIdx);
    SDNode *Hi = CurDAG->getMachineNode(
        TargetOpcode::EXTRACT_SUBREG, DL, MVT::i32,
        SDValue(Emul, 0), SubHiIdx);

    ReplaceUses(SDValue(N, 0), SDValue(Lo, 0));
    ReplaceUses(SDValue(N, 1), SDValue(Hi, 0));
    CurDAG->RemoveDeadNode(N);
    return;
  }

  // EDIV — extended divide. The VAXISD::EDIV node has i32 operands
  // (divisor, dividend_lo, dividend_hi) and i32 results (quotient, remainder).
  // The hardware EDIV reads a quadword register pair for the dividend.
  if (N->getOpcode() == VAXISD::EDIV) {
    SDLoc DL(N);
    SDValue Divisor = N->getOperand(0);
    SDValue DvdLo   = N->getOperand(1);
    SDValue DvdHi   = N->getOperand(2);

    // Build QPR from i32 halves via REG_SEQUENCE.
    SDValue QprRC = CurDAG->getTargetConstant(VAX::QPRRegClassID, DL, MVT::i32);
    SDValue SubLoIdx = CurDAG->getTargetConstant(VAX::sub_lo, DL, MVT::i32);
    SDValue SubHiIdx = CurDAG->getTargetConstant(VAX::sub_hi, DL, MVT::i32);
    SDNode *DvdPair = CurDAG->getMachineNode(
        TargetOpcode::REG_SEQUENCE, DL, MVT::i64,
        {QprRC, DvdLo, SubLoIdx, DvdHi, SubHiIdx});

    // Emit EDIV: (GPRnoPC:$quo, GPRnoPC:$rem) = ediv divisor, QPR:$dividend.
    SDNode *Ediv = CurDAG->getMachineNode(
        VAX::EDIV, DL, MVT::i32, MVT::i32,
        {Divisor, SDValue(DvdPair, 0)});

    ReplaceUses(SDValue(N, 0), SDValue(Ediv, 0));
    ReplaceUses(SDValue(N, 1), SDValue(Ediv, 1));
    CurDAG->RemoveDeadNode(N);
    return;
  }

  SelectCode(N);
}

bool VAXDAGToDAGISel::SelectVAXAddr(SDValue Addr, SDValue &Base,
                                     SDValue &Offset, SDValue &Index,
                                     SDValue &Flags) {
  SDLoc DL(Addr);
  MVT PtrTy = MVT::i32;

  // Index is always NoReg from SelectionDAG (indexed mode is AsmParser-only).
  Index = CurDAG->getRegister(0, PtrTy);
  Flags = CurDAG->getTargetConstant(0, DL, MVT::i32); // VAXAM::Disp

  // FrameIndex — stack slot, zero displacement.
  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), PtrTy);
    Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
    return true;
  }

  if (Addr.getOpcode() == ISD::ADD) {
    // ADD(FrameIndex, Constant) — stack slot with displacement.
    if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr.getOperand(0)))
      if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Addr.getOperand(1))) {
        Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), PtrTy);
        Offset = CurDAG->getTargetConstant(CN->getSExtValue(), DL, MVT::i32);
        return true;
      }
    // ADD(Reg, Constant) — register + displacement.
    if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Addr.getOperand(1))) {
      Base = Addr.getOperand(0);
      Offset = CurDAG->getTargetConstant(
          APInt(32, CN->getSExtValue(), /*isSigned=*/true), DL, MVT::i32);
      return true;
    }
  }

  // Bare register — zero displacement.
  // Exclude nodes that have their own dedicated patterns (PCRelWrapper, etc.)
  if (Addr.getOpcode() == VAXISD::PCRelWrapper)
    return false;
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
  return true;
}

bool VAXDAGToDAGISel::SelectVAXAddrLong(SDValue Addr, SDValue &Base,
                                         SDValue &Offset, SDValue &Index,
                                         SDValue &Flags) {
  SDLoc DL(Addr);
  MVT PtrTy = MVT::i32;

  // Try indexed addressing: ADD(base, SHL(idx, 2)) or ADD(SHL(idx, 2), base).
  // VAX indexed mode computes EA = EA(base_specifier) + Rx * data_size.
  // For longword instructions (4 bytes), shift by 2 matches the implicit scale.
  if (Addr.getOpcode() == ISD::ADD) {
    for (int Swap = 0; Swap < 2; ++Swap) {
      SDValue MaybeShift = Addr.getOperand(Swap);
      SDValue MaybeBase = Addr.getOperand(1 - Swap);

      if (MaybeShift.getOpcode() != ISD::SHL)
        continue;
      auto *ShAmt = dyn_cast<ConstantSDNode>(MaybeShift.getOperand(1));
      if (!ShAmt || ShAmt->getZExtValue() != 2)
        continue;

      // Matched (add base, (shl idx, 2)).
      SDValue IdxReg = MaybeShift.getOperand(0);
      Index = IdxReg;
      Flags = CurDAG->getTargetConstant(VAXAM::Disp, DL, MVT::i32);

      // Decompose base: FrameIndex, ADD(base, const), or bare register.
      if (auto *FIN = dyn_cast<FrameIndexSDNode>(MaybeBase)) {
        Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), PtrTy);
        Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
        return true;
      }
      if (MaybeBase.getOpcode() == ISD::ADD) {
        if (auto *CN = dyn_cast<ConstantSDNode>(MaybeBase.getOperand(1))) {
          if (auto *FIN = dyn_cast<FrameIndexSDNode>(MaybeBase.getOperand(0))) {
            Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), PtrTy);
          } else {
            Base = MaybeBase.getOperand(0);
          }
          Offset = CurDAG->getTargetConstant(CN->getSExtValue(), DL, MVT::i32);
          return true;
        }
      }
      Base = MaybeBase;
      Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
      return true;
    }
  }

  // No indexed match — fall back to standard address selection.
  return SelectVAXAddr(Addr, Base, Offset, Index, Flags);
}

bool VAXDAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, InlineAsm::ConstraintCode ConstraintID,
    std::vector<SDValue> &OutOps) {
  switch (ConstraintID) {
  default:
    return true;
  case InlineAsm::ConstraintCode::m:
  case InlineAsm::ConstraintCode::o:
  case InlineAsm::ConstraintCode::p: {
    SDValue Base, Offset, Index, Flags;
    if (!SelectVAXAddr(Op, Base, Offset, Index, Flags)) {
      // SelectVAXAddr rejects PCRelWrapper to let TableGen patterns match
      // globals directly. For inline asm, accept it as base+0.
      SDLoc DL(Op);
      Base = Op;
      Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
      Index = CurDAG->getRegister(0, MVT::i32);
      Flags = CurDAG->getTargetConstant(0, DL, MVT::i32);
    }
    OutOps.push_back(Base);
    OutOps.push_back(Offset);
    OutOps.push_back(Index);
    OutOps.push_back(Flags);
    return false;
  }
  }
}

namespace {

class VAXDAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  explicit VAXDAGToDAGISelLegacy(VAXTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<VAXDAGToDAGISel>(TM, OptLevel)) {}
};

} // end anonymous namespace

char VAXDAGToDAGISelLegacy::ID = 0;

INITIALIZE_PASS(VAXDAGToDAGISelLegacy, "vax-isel",
                "VAX DAG->DAG Pattern Instruction Selection", false, false)

FunctionPass *llvm::createVAXISelDag(VAXTargetMachine &TM,
                                      CodeGenOptLevel OptLevel) {
  return new VAXDAGToDAGISelLegacy(TM, OptLevel);
}
