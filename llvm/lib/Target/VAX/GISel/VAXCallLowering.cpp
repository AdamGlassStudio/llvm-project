//===-- VAXCallLowering.cpp - Call lowering for VAX ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Stub implementation of GlobalISel call lowering for VAX.
//
// VAX CALLS convention: all arguments pushed on stack right-to-left,
// return value in R0 (i32/f32) or R0:R1 (i64/f64).
//
//===----------------------------------------------------------------------===//

#include "VAXCallLowering.h"
#include "VAXISelLowering.h"
#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"

#define DEBUG_TYPE "vax-call-lowering"

using namespace llvm;

VAXCallLowering::VAXCallLowering(const VAXTargetLowering &TLI)
    : CallLowering(&TLI) {}

bool VAXCallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                  const Value *Val, ArrayRef<Register> VRegs,
                                  FunctionLoweringInfo &FLI) const {
  // TODO: Implement return value lowering (R0 / R0:R1).
  if (Val)
    return false; // Can't handle return values yet — fall back to SDAG.
  // Void return — emit RET.
  MIRBuilder.buildInstr(VAX::RET);
  return true;
}

bool VAXCallLowering::canLowerReturn(MachineFunction &MF,
                                     CallingConv::ID CallConv,
                                     SmallVectorImpl<BaseArgInfo> &Outs,
                                     bool IsVarArg) const {
  // For now, only void returns.
  return Outs.empty();
}

bool VAXCallLowering::lowerFormalArguments(MachineIRBuilder &MIRBuilder,
                                           const Function &F,
                                           ArrayRef<ArrayRef<Register>> VRegs,
                                           FunctionLoweringInfo &FLI) const {
  // TODO: Implement argument lowering from the VAX stack frame.
  // All arguments are on the stack at AP+4, AP+8, etc.
  if (!VRegs.empty())
    return false; // Can't handle arguments yet — fall back to SDAG.
  return true;
}

bool VAXCallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                CallLoweringInfo &Info) const {
  // TODO: Implement CALLS instruction emission.
  return false; // Fall back to SDAG for all calls.
}
