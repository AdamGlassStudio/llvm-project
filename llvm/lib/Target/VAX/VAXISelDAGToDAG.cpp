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

  // Try to narrow VAXISD::CMP to a byte compare (CMPB_mi/TSTB_m) when
  // the LHS is a zero-extending byte load and the RHS is a small constant.
  bool tryNarrowCmpToByte(SDNode *N);

  // ComplexPattern selector for base+displacement memory operands.
  // Returns true and sets Base/Offset/Index/Flags when Addr matches a known
  // form: FrameIndex, ADD(FrameIndex, Const), ADD(Reg, Const), bare Reg.
  // Index is always NoReg — use SelectVAXAddrLong for indexed addressing.
  bool SelectVAXAddr(SDValue Addr, SDValue &Base, SDValue &Offset,
                     SDValue &Index, SDValue &Flags);

  // Like SelectVAXAddr but does NOT match bare registers. Only matches when
  // there is an actual address computation: FrameIndex, Reg+Const, PCRel.
  // Used by PUSHAL to avoid turning "pushl reg" into "pushal (reg)".
  bool SelectVAXAddrNonTrivial(SDValue Addr, SDValue &Base, SDValue &Offset,
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

  // JSB_CALL — fast call via JSB (no arg count, no entry mask).
  if (N->getOpcode() == VAXISD::JSB_CALL) {
    SDValue Chain   = N->getOperand(0);
    SDValue Callee  = N->getOperand(1);
    SDValue RegMask = N->getOperand(2);

    bool isDirect = (Callee.getOpcode() == ISD::TargetGlobalAddress ||
                     Callee.getOpcode() == ISD::TargetExternalSymbol);
    unsigned Opc = isDirect ? VAX::JSB_direct : VAX::JSB_indir;
    SmallVector<SDValue, 3> Ops = { Callee, RegMask, Chain };

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
      Cnt = CurDAG->getTargetConstant(
          APInt(32, CntC->getSExtValue(), /*isSigned=*/true), DL, MVT::i32);
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
        APInt(32, cast<ConstantSDNode>(Addend)->getSExtValue(),
              /*isSigned=*/true),
        DL, MVT::i32);

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

  // Try to narrow VAXISD::CMP to CMPB/TSTB when comparing a zero-extended
  // byte load against a small constant.
  if (N->getOpcode() == VAXISD::CMP) {
    if (tryNarrowCmpToByte(N))
      return;
  }

  // VAXISD::MOVC3 → VAX::MOVC3 with proper VAXMemOp addressing modes.
  // len: immediate for constants, register direct for dynamic values.
  // src/dst: standard address decomposition (.ab access type).
  if (N->getOpcode() == VAXISD::MOVC3) {
    SDLoc DL(N);
    SDValue Chain = N->getOperand(0);
    SDValue Dst   = N->getOperand(1);
    SDValue Src   = N->getOperand(2);
    SDValue Size  = N->getOperand(3);

    SDValue NoReg = CurDAG->getRegister(0, MVT::i32);

    SmallVector<SDValue, 13> Ops;

    // len: use immediate mode for constants, register direct for dynamic.
    if (auto *SizeConst = dyn_cast<ConstantSDNode>(Size)) {
      SDValue ImmVal = CurDAG->getTargetConstant(
          SizeConst->getZExtValue(), DL, MVT::i32);
      SDValue ImmFlags = CurDAG->getTargetConstant(VAXAM::Imm, DL, MVT::i32);
      Ops.append({NoReg, ImmVal, NoReg, ImmFlags});
    } else {
      SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i32);
      SDValue DirectFlags = CurDAG->getTargetConstant(
          VAXAM::RegDirect, DL, MVT::i32);
      Ops.append({Size, Zero, NoReg, DirectFlags});
    }

    // src: decompose address for .ab access (address = effective address).
    SDValue SrcBase, SrcDisp, SrcIdx, SrcFlags;
    SelectVAXAddr(Src, SrcBase, SrcDisp, SrcIdx, SrcFlags);
    Ops.append({SrcBase, SrcDisp, SrcIdx, SrcFlags});

    // dst: decompose address for .ab access.
    SDValue DstBase, DstDisp, DstIdx, DstFlags;
    SelectVAXAddr(Dst, DstBase, DstDisp, DstIdx, DstFlags);
    Ops.append({DstBase, DstDisp, DstIdx, DstFlags});

    // Chain input (last operand).
    Ops.push_back(Chain);

    SDNode *Movc = CurDAG->getMachineNode(VAX::MOVC3, DL, MVT::Other, Ops);
    ReplaceNode(N, Movc);
    return;
  }

  SelectCode(N);
}

// Try to narrow (VAXISD::CMP (zextload i8 addr), const<256>) → CMPB_mi/TSTB_m.
//
// After type legalization, an i8 comparison becomes:
//   %1 = (zextload i8 addr)   ;; MOVZBL — extends byte to i32
//   (vax_cmp %1, imm)         ;; CMPL — compares full longwords
//
// Since the loaded value is in [0,255] and the constant is < 256, comparing
// the byte at memory directly against the byte immediate gives the same PSW
// result for equality and unsigned conditions. This eliminates the MOVZBL
// and shrinks CMPL→CMPB (or TSTB if 0).
//
// IMPORTANT: CMPB treats bytes as signed (-128..127), so narrowing is UNSAFE
// for signed comparisons when the byte value could be >= 128. A zext'd byte
// of 200 is i32 +200, but cmpb sees it as -56. We restrict to:
//   - Equality (BEQL=0, BNEQ=1)
//   - Unsigned (BGTRU=6, BGEQU=7, BLSSU=8, BLEQU=9)
//
// We only fold when the zextload has a single use (the CMP) so we don't keep
// the MOVZBL alive for other consumers.
bool VAXDAGToDAGISel::tryNarrowCmpToByte(SDNode *N) {
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  // LHS must be a zero-extending byte load with a single use.
  if (LHS.getOpcode() != ISD::LOAD)
    return false;
  LoadSDNode *Load = cast<LoadSDNode>(LHS);
  if (Load->getExtensionType() != ISD::ZEXTLOAD &&
      Load->getExtensionType() != ISD::EXTLOAD)
    return false;
  if (Load->getMemoryVT() != MVT::i8)
    return false;
  // Don't fold volatile loads — the chain output from the compare node would
  // flow through lifetime markers into the TokenFactor consumed by the branch,
  // creating a scheduling cycle (the branch is glue-connected to the compare).
  if (Load->isVolatile())
    return false;
  if (!LHS.hasOneUse())
    return false;

  // Don't fold when the load's chain output has users — redirecting them to
  // the compare node's chain creates scheduling cycles.  The compare is
  // glue-connected to the branch, which reads the BB's final TokenFactor.
  // If any of the load's chain users (lifetime markers, stores, etc.) feed
  // into that TokenFactor, the cycle is:
  //   CMPB_mi:ch → users → TokenFactor → BNEQ (same SUnit via glue).
  if (!LHS.getValue(1).use_empty())
    return false;

  // RHS must be a constant that fits in a byte (0..255).
  auto *RHSC = dyn_cast<ConstantSDNode>(RHS);
  if (!RHSC)
    return false;
  uint64_t Imm = RHSC->getZExtValue();
  if (Imm > 255)
    return false;

  // Check that all users of the CMP's glue use only equality or unsigned
  // condition codes. Signed comparisons (BGTR=2..BLEQ=5) are unsafe because
  // cmpb treats bytes as signed but the zext'd i32 value is unsigned.
  for (SDNode *User : N->users()) {
    unsigned UOpc = User->isMachineOpcode() ? User->getMachineOpcode()
                                            : User->getOpcode();
    // After ISel pattern matching, BRCC+CC becomes specific branch opcodes.
    // Check for signed branch opcodes — these are unsafe to narrow.
    if (UOpc == VAX::BGTR || UOpc == VAX::BGEQ ||
        UOpc == VAX::BLSS || UOpc == VAX::BLEQ ||
        UOpc == VAX::LongBGTR || UOpc == VAX::LongBGEQ ||
        UOpc == VAX::LongBLSS || UOpc == VAX::LongBLEQ)
      return false; // Signed condition — cannot narrow.
    // Also check unmatched BRCC/SELECT_CC nodes (in case ISel order varies).
    if (UOpc == VAXISD::BRCC || UOpc == VAXISD::SELECT_CC) {
      unsigned CC = User->getConstantOperandVal(2);
      if (CC >= 2 && CC <= 5)
        return false;
    }
  }

  // Decompose the load address.
  SDValue Base, Offset, Index, Flags;
  if (!SelectVAXAddr(Load->getBasePtr(), Base, Offset, Index, Flags))
    return false;

  SDLoc DL(N);
  SmallVector<SDValue, 6> Ops;

  SDNode *CmpNode;
  if (Imm == 0) {
    // TSTB_m: test byte at memory (equivalent to cmpb mem, $0).
    Ops = {Base, Offset, Index, Flags, Load->getChain()};
    CmpNode = CurDAG->getMachineNode(VAX::TSTB_m, DL, MVT::Other, MVT::Glue,
                                     Ops);
  } else {
    // CMPB_mi: compare byte at memory against byte immediate.
    SDValue ImmOp = CurDAG->getTargetConstant(Imm, DL, MVT::i32);
    Ops = {Base, Offset, Index, Flags, ImmOp, Load->getChain()};
    CmpNode = CurDAG->getMachineNode(VAX::CMPB_mi, DL, MVT::Other, MVT::Glue,
                                     Ops);
  }

  // Replace the glue output (condition codes) and propagate the chain.
  // Result 0 = chain (MVT::Other), Result 1 = glue (MVT::Glue).
  ReplaceUses(SDValue(N, 0), SDValue(CmpNode, 1));
  // The load's chain is consumed by the CMPB/TSTB; update chain users.
  ReplaceUses(LHS.getValue(1), SDValue(CmpNode, 0));
  CurDAG->RemoveDeadNode(N);
  return true;
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

  if (Addr.getOpcode() == ISD::ADD ||
      (Addr.getOpcode() == ISD::OR &&
       Addr->getFlags().hasDisjoint())) {
    // ADD(FrameIndex, Constant) — stack slot with displacement.
    // OR-disjoint is equivalent to ADD when low bits are known zero.
    if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr.getOperand(0)))
      if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Addr.getOperand(1))) {
        Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), PtrTy);
        Offset = CurDAG->getTargetConstant(
            APInt(32, CN->getSExtValue(), /*isSigned=*/true), DL, MVT::i32);
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
  // PCRelWrapper(tglobaladdr) → Base=NoReg, Offset=symbol.
  // The MC encoder and printer handle Base=NoReg + expr as PC-relative.
  if (Addr.getOpcode() == VAXISD::PCRelWrapper) {
    Base = CurDAG->getRegister(0, PtrTy);
    Offset = Addr.getOperand(0);
    return true;
  }
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
  return true;
}

// SelectVAXAddrNonTrivial — like SelectVAXAddr but rejects bare registers.
// Used for PUSHAL patterns where we only want to match real address
// computations (FrameIndex, Reg+Disp, PCRel), not plain register values.
bool VAXDAGToDAGISel::SelectVAXAddrNonTrivial(SDValue Addr, SDValue &Base,
                                               SDValue &Offset, SDValue &Index,
                                               SDValue &Flags) {
  // Reject bare registers — these should use PUSHL, not PUSHAL.
  if (Addr.getOpcode() != ISD::FrameIndex &&
      Addr.getOpcode() != ISD::ADD &&
      !(Addr.getOpcode() == ISD::OR && Addr->getFlags().hasDisjoint()) &&
      Addr.getOpcode() != VAXISD::PCRelWrapper)
    return false;
  return SelectVAXAddr(Addr, Base, Offset, Index, Flags);
}

bool VAXDAGToDAGISel::SelectVAXAddrLong(SDValue Addr, SDValue &Base,
                                         SDValue &Offset, SDValue &Index,
                                         SDValue &Flags) {
  SDLoc DL(Addr);
  MVT PtrTy = MVT::i32;

  // Try indexed addressing: ADD(base, SHL(idx, 2)) or ADD(SHL(idx, 2), base).
  // Also matches VAXISD::ASHL($2, idx) which is what custom SHL lowering
  // produces for i32 shifts.
  // VAX indexed mode computes EA = EA(base_specifier) + Rx * data_size.
  // For longword instructions (4 bytes), shift by 2 matches the implicit scale.
  if (Addr.getOpcode() == ISD::ADD) {
    for (int Swap = 0; Swap < 2; ++Swap) {
      SDValue MaybeShift = Addr.getOperand(Swap);
      SDValue MaybeBase = Addr.getOperand(1 - Swap);

      // Match ISD::SHL(idx, 2) or VAXISD::ASHL(2, idx).
      SDValue IdxReg;
      if (MaybeShift.getOpcode() == ISD::SHL) {
        auto *ShAmt = dyn_cast<ConstantSDNode>(MaybeShift.getOperand(1));
        if (!ShAmt || ShAmt->getZExtValue() != 2)
          continue;
        IdxReg = MaybeShift.getOperand(0);
      } else if (MaybeShift.getOpcode() == VAXISD::ASHL) {
        // ASHL operands: (count, src)
        auto *ShAmt = dyn_cast<ConstantSDNode>(MaybeShift.getOperand(0));
        if (!ShAmt || ShAmt->getZExtValue() != 2)
          continue;
        IdxReg = MaybeShift.getOperand(1);
      } else {
        continue;
      }
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
          Offset = CurDAG->getTargetConstant(
              APInt(32, CN->getSExtValue(), /*isSigned=*/true), DL, MVT::i32);
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
    if (!SelectVAXAddr(Op, Base, Offset, Index, Flags))
      return true; // cannot represent as memory operand
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
