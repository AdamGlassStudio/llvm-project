//===-- VAXISelLowering.cpp - VAX DAG Lowering Implementation ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXISelLowering.h"
#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "VAXSubtarget.h"
#include "VAXTargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_CALLINGCONV_IMPL
#include "VAXGenCallingConv.inc"

VAXTargetLowering::VAXTargetLowering(const VAXTargetMachine &TM,
                                     const VAXSubtarget &STI)
    : TargetLowering(TM, STI) {
  // Register classes by value type.
  // i8 and i16 are deferred until Phase 7 (extend/truncate instructions).
  addRegisterClass(MVT::i32, &VAX::GPRnoPCRegClass);

  // Finalize register class / type legalization info.
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(VAX::SP);
  setSchedulingPreference(Sched::RegPressure);

  // Global addresses are lowered to PC-relative wrappers.
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);

  // AND is lowered to BICL (bit-clear) since VAX has no direct AND instruction.
  setOperationAction(ISD::AND, MVT::i32, Custom);

  // Branches: lower ISD::BR_CC to VAXISD::CMP + VAXISD::BRCC.
  // Expanding BRCOND causes the DAG builder to produce BR_CC directly.
  setOperationAction(ISD::BR_CC,   MVT::i32,   Custom);
  setOperationAction(ISD::BRCOND,  MVT::Other,  Expand);

  // Conditional value selection: expand to branch sequence (Phase 6 deferred).
  setOperationAction(ISD::SELECT,    MVT::i32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Expand);

  // Extending loads from sub-i32 types: only i8 zero-extend is supported
  // (via MOVZBL); sext and word-sized extends deferred to Phase 7.
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, MVT::i8, Legal);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i8, Expand);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i16, Expand);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, MVT::i16, Expand);

  // Scalar integer types are all legal at i32; narrower types will be
  // promoted/expanded in later phases as instructions are added.
}

const char *VAXTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case VAXISD::RET_FLAG:     return "VAXISD::RET_FLAG";
  case VAXISD::PCRelWrapper: return "VAXISD::PCRelWrapper";
  case VAXISD::BICL:         return "VAXISD::BICL";
  case VAXISD::CMP:          return "VAXISD::CMP";
  case VAXISD::BRCC:         return "VAXISD::BRCC";
  default:                   return nullptr;
  }
}

SDValue VAXTargetLowering::LowerOperation(SDValue Op,
                                           SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress: return LowerGlobalAddress(Op, DAG);
  case ISD::AND:           return LowerAND(Op, DAG);
  case ISD::BR_CC:         return LowerBR_CC(Op, DAG);
  default:
    report_fatal_error(Twine("VAXTargetLowering::LowerOperation: unimplemented "
                             "opcode ") +
                       Twine(Op.getOpcode()));
  }
}

SDValue VAXTargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  // Map LLVM condition codes to VAX branch condition integers (see VAXCC enum
  // in VAXInstrInfo.td / branch PatLeaves).
  unsigned VAXCC;
  switch (CC) {
  default: llvm_unreachable("unsupported condition code for VAX BR_CC");
  case ISD::SETEQ:  VAXCC = 0; break; // BEQL
  case ISD::SETNE:  VAXCC = 1; break; // BNEQ
  case ISD::SETGT:  VAXCC = 2; break; // BGTR  (signed)
  case ISD::SETGE:  VAXCC = 3; break; // BGEQ  (signed)
  case ISD::SETLT:  VAXCC = 4; break; // BLSS  (signed)
  case ISD::SETLE:  VAXCC = 5; break; // BLEQ  (signed)
  case ISD::SETUGT: VAXCC = 6; break; // BGTRU (unsigned)
  case ISD::SETUGE: VAXCC = 7; break; // BGEQU (unsigned)
  case ISD::SETULT: VAXCC = 8; break; // BLSSU (unsigned)
  case ISD::SETULE: VAXCC = 9; break; // BLEQU (unsigned)
  }

  SDValue Cmp = DAG.getNode(VAXISD::CMP, DL, MVT::Glue, LHS, RHS);
  return DAG.getNode(VAXISD::BRCC, DL, MVT::Other,
                     Chain, Dest,
                     DAG.getConstant(VAXCC, DL, MVT::i32),
                     Cmp);
}

SDValue VAXTargetLowering::LowerAND(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();
  SDValue A = Op.getOperand(0);
  SDValue B = Op.getOperand(1);
  // VAX has no direct AND; use BICL(mask, src) = src & ~mask.
  // AND(A, B) = BICL(~B, A) since ~~B = B.
  if (auto *CN = dyn_cast<ConstantSDNode>(B)) {
    // Constant operand: fold the NOT at compile time (single instruction).
    return DAG.getNode(VAXISD::BICL, DL, VT,
                       DAG.getConstant(~CN->getAPIntValue(), DL, VT), A);
  }
  // Register operand: emit BICL(XOR(B, -1), A) → MCOML + BICL3 (two insns).
  SDValue NotB = DAG.getNode(ISD::XOR, DL, VT, B,
                             DAG.getAllOnesConstant(DL, VT));
  return DAG.getNode(VAXISD::BICL, DL, VT, NotB, A);
}

SDValue VAXTargetLowering::LowerGlobalAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  const GlobalAddressSDNode *GN = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = GN->getGlobal();
  SDLoc DL(GN);
  SDValue GA = DAG.getTargetGlobalAddress(GV, DL, MVT::i32, GN->getOffset());
  return DAG.getNode(VAXISD::PCRelWrapper, DL, MVT::i32, GA);
}

SDValue VAXTargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
    SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_VAX);

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps;
  RetOps.push_back(Chain);

  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "VAX: all return values must be in registers");
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVals[i], Flag);
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;
  if (Flag.getNode())
    RetOps.push_back(Flag);
  return DAG.getNode(VAXISD::RET_FLAG, DL, MVT::Other, RetOps);
}

SDValue VAXTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  // Phase 3: no argument lowering yet — will be implemented in Phase 8.
  if (!Ins.empty())
    report_fatal_error("VAX: function arguments not yet supported (Phase 8)");
  return Chain;
}

SDValue VAXTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  // TODO: implement in Phase 8
  report_fatal_error("VAXTargetLowering::LowerCall not yet implemented");
}
