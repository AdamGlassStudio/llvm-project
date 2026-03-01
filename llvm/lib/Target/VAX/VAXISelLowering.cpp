//===-- VAXISelLowering.cpp - VAX DAG Lowering Implementation ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXISelLowering.h"
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
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_CALLINGCONV_IMPL
#include "VAXGenCallingConv.inc"

VAXTargetLowering::VAXTargetLowering(const VAXTargetMachine &TM,
                                     const VAXSubtarget &STI)
    : TargetLowering(TM) {
  addRegisterClass(MVT::i32, &VAX::GPRnoPCRegClass);
  computeRegisterProperties(STI.getRegisterInfo());
  setStackPointerRegisterToSaveRestore(VAX::SP);
  setSchedulingPreference(Sched::RegPressure);
}

const char *VAXTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case VAXISD::RET_FLAG: return "VAXISD::RET_FLAG";
  default:               return nullptr;
  }
}

SDValue VAXTargetLowering::LowerOperation(SDValue Op,
                                           SelectionDAG &DAG) const {
  report_fatal_error(Twine("VAXTargetLowering::LowerOperation: unimplemented "
                           "opcode ") +
                     Twine(Op.getOpcode()));
}

SDValue VAXTargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
    SelectionDAG &DAG) const {
  // TODO: implement in Phase 3
  report_fatal_error("VAXTargetLowering::LowerReturn not yet implemented");
}

SDValue VAXTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  // TODO: implement in Phase 3
  report_fatal_error(
      "VAXTargetLowering::LowerFormalArguments not yet implemented");
}

SDValue VAXTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  // TODO: implement in Phase 8
  report_fatal_error("VAXTargetLowering::LowerCall not yet implemented");
}
