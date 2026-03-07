//===-- VAXISelLowering.h - VAX DAG Lowering Interface --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_VAXISELLOWERING_H
#define LLVM_LIB_TARGET_VAX_VAXISELLOWERING_H

#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {
class VAXSubtarget;
class VAXTargetMachine;

namespace VAXISD {
enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  // Return from a VAX function via RET
  RET_FLAG,
  // Wrap a TargetGlobalAddress for PC-relative data access
  PCRelWrapper,
  // Bit clear: BICL(mask, src) = src & ~mask  (lowered from ISD::AND)
  BICL,
  // Compare two values and set condition codes (produces a glue output).
  CMP,
  // Conditional branch using condition codes from a preceding CMP.
  // Operands: (chain, dest:BB, cc:i32, glue)
  BRCC,
  // CALLS instruction node (chain, count, callee, regmask) → (chain, glue)
  CALL,
  // Arithmetic shift with pre-negated count (for SRA lowering).
  ASHL,
  // Conditional select: (trueval, falseval, cc, cmp_glue) → result
  SELECT_CC,
  // Push longword onto stack (SP autodecrement): (chain, val) → (chain, glue)
  PUSHL,
  // FP compare (CMPF): sets PSW from FP operands.
  FCMP,
  // CASEL switch dispatch: (chain, index, limit, JTI) → jumps to table entry
  CASEL,
  // ASHQ: arithmetic shift quadword. (cnt:i32, src_lo:i32, src_hi:i32) →
  // (dst_lo:i32, dst_hi:i32). Positive cnt = left shift, negative = right.
  ASHQ,
  // EMUL: extended multiply. (a:i32, b:i32, add:i32) →
  // (lo:i32, hi:i32). Result = a*b + sign_extend(add).
  EMUL,
  // EDIV: extended divide. (divisor:i32, dividend_lo:i32, dividend_hi:i32) →
  // (quotient:i32, remainder:i32). dividend_quad / divisor.
  EDIV,
  // EXTZV: extract zero-extended bit field. (pos:i32, size:i32, base:i32) →
  // result:i32. Used for logical right shift (srl).
  EXTZV,
};
} // namespace VAXISD

class VAXTargetLowering : public TargetLowering {
public:
  explicit VAXTargetLowering(const VAXTargetMachine &TM,
                              const VAXSubtarget &STI);

  const char *getTargetNodeName(unsigned Opcode) const override;

  /// Prevent DAGCombiner from folding FP constant stores into integer stores.
  /// VAX uses non-IEEE FP format, so IEEE bit patterns in memory are wrong.
  bool isFPImmLegal(const APFloat &Imm, EVT VT,
                    bool ForCodeSize) const override;

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
                      SelectionDAG &DAG) const override;

  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &dl, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

private:
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerAND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSRA(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSRL(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSHL_PARTS(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSRA_PARTS(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSMUL_LOHI(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerUDIV(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerUREM(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSREM(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerUINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSTORE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_JT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerEH_RETURN(SDValue Op, SelectionDAG &DAG) const;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *BB) const override;

  Register getExceptionPointerRegister(const Constant *PersonalityFn) const override;
  Register getExceptionSelectorRegister(const Constant *PersonalityFn) const override;

  unsigned getJumpTableEncoding() const override;

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

  TargetLoweringBase::AtomicExpansionKind
  shouldExpandAtomicRMWInIR(const AtomicRMWInst *AI) const override;
  TargetLoweringBase::AtomicExpansionKind
  shouldExpandAtomicCmpXchgInIR(const AtomicCmpXchgInst *AI) const override;
  TargetLoweringBase::AtomicExpansionKind
  shouldExpandAtomicLoadInIR(LoadInst *LI) const override;
  TargetLoweringBase::AtomicExpansionKind
  shouldExpandAtomicStoreInIR(StoreInst *SI) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_VAX_VAXISELLOWERING_H
