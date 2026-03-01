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
};
} // namespace VAXISD

class VAXTargetLowering : public TargetLowering {
public:
  explicit VAXTargetLowering(const VAXTargetMachine &TM,
                              const VAXSubtarget &STI);

  const char *getTargetNodeName(unsigned Opcode) const override;

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
  SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerAND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSRA(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSRL(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
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
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_VAX_VAXISELLOWERING_H
