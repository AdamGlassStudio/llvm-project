//===-- VAXCallLowering.cpp - Call lowering for VAX ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// GlobalISel call lowering for VAX.
//
// VAX CALLS convention: all arguments pushed on stack right-to-left,
// return value in R0 (i32/f32) or R0:R1 (i64/f64). AP points to the
// argument area: AP+0 = arg count, AP+4 = first arg, AP+8 = second, etc.
//
//===----------------------------------------------------------------------===//

#include "VAXCallLowering.h"
#include "VAXISelLowering.h"
#include "VAXMachineFunctionInfo.h"
#include "VAXSubtarget.h"
#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "vax-call-lowering"

using namespace llvm;

// Duplicate of CC_VAX_RetI64 from VAXISelLowering.cpp — needed by the
// generated CallingConv.inc. Our canLowerReturn rejects i64 returns,
// so this is never actually called from GISel.
static bool CC_VAX_RetI64(unsigned ValNo, MVT ValVT, MVT LocVT,
                          CCValAssign::LocInfo LocInfo,
                          ISD::ArgFlagsTy ArgFlags, CCState &State) {
  if (!State.AllocateReg(VAX::R0))
    return false;
  State.addLoc(
      CCValAssign::getCustomReg(ValNo, ValVT, VAX::R0, MVT::i32, LocInfo));
  if (!State.AllocateReg(VAX::R1))
    return false;
  State.addLoc(
      CCValAssign::getCustomReg(ValNo, ValVT, VAX::R1, MVT::i32, LocInfo));
  return true;
}

#include "VAXGenCallingConv.inc"

VAXCallLowering::VAXCallLowering(const VAXTargetLowering &TLI)
    : CallLowering(&TLI) {}

namespace {

/// Handler for incoming formal arguments (loaded from the stack via AP).
struct VAXIncomingArgHandler : public CallLowering::IncomingValueHandler {
  VAXIncomingArgHandler(MachineIRBuilder &MIRBuilder,
                        MachineRegisterInfo &MRI)
      : IncomingValueHandler(MIRBuilder, MRI) {}

  Register getStackAddress(uint64_t MemSize, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    // VAX: AP+0 = arg count word, AP+4 = first arg.
    // CC_VAX assigns MemOffset starting at 0 for the first arg,
    // so we add +4 to skip the arg count.
    int64_t APOffset = Offset + 4;
    MachineFrameInfo &MFI = MIRBuilder.getMF().getFrameInfo();
    int FI = MFI.CreateFixedObject(MemSize, APOffset, /*IsImmutable=*/true);
    MPO = MachinePointerInfo::getFixedStack(MIRBuilder.getMF(), FI);
    return MIRBuilder.buildFrameIndex(LLT::pointer(0, 32), FI).getReg(0);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    MachineFunction &MF = MIRBuilder.getMF();
    auto *MMO = MF.getMachineMemOperand(MPO, MachineMemOperand::MOLoad, MemTy,
                                        inferAlignFromPtrInfo(MF, MPO));
    MIRBuilder.buildLoad(ValVReg, Addr, *MMO);
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA,
                        ISD::ArgFlagsTy Flags) override {
    // VAX has no register arguments, but this is needed for the interface.
    markPhysRegUsed(PhysReg);
    IncomingValueHandler::assignValueToReg(ValVReg, PhysReg, VA, Flags);
  }

  void markPhysRegUsed(MCRegister PhysReg) {
    MIRBuilder.getMRI()->addLiveIn(PhysReg);
    MIRBuilder.getMBB().addLiveIn(PhysReg);
  }
};

/// Handler for outgoing return values (copied to physical registers).
struct VAXOutgoingRetHandler : public CallLowering::OutgoingValueHandler {
  MachineInstrBuilder &MIB;

  VAXOutgoingRetHandler(MachineIRBuilder &MIRBuilder,
                        MachineRegisterInfo &MRI, MachineInstrBuilder &MIB)
      : OutgoingValueHandler(MIRBuilder, MRI), MIB(MIB) {}

  Register getStackAddress(uint64_t MemSize, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    llvm_unreachable("VAX return values are always in registers");
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    llvm_unreachable("VAX return values are always in registers");
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA,
                        ISD::ArgFlagsTy Flags) override {
    Register ExtReg = extendRegister(ValVReg, VA);
    MIRBuilder.buildCopy(PhysReg, ExtReg);
    MIB.addUse(PhysReg, RegState::Implicit);
  }
};

} // end anonymous namespace

bool VAXCallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                  const Value *Val, ArrayRef<Register> VRegs,
                                  FunctionLoweringInfo &FLI) const {
  assert(!Val == VRegs.empty() && "Return value without a vreg");

  MachineInstrBuilder Ret = MIRBuilder.buildInstrNoInsert(VAX::RET);

  if (!VRegs.empty()) {
    MachineFunction &MF = MIRBuilder.getMF();
    const Function &F = MF.getFunction();
    const DataLayout &DL = MF.getDataLayout();

    // Build the return value argument info.
    ArgInfo RetInfo(VRegs, Val->getType(), 0);
    setArgFlags(RetInfo, AttributeList::ReturnIndex, DL, F);

    SmallVector<ArgInfo, 4> SplitRetInfos;
    splitToValueTypes(RetInfo, SplitRetInfos, DL, F.getCallingConv());

    // Use RetCC_VAX to assign return values to R0 (or R0+R1 for i64).
    OutgoingValueAssigner Assigner(RetCC_VAX);
    VAXOutgoingRetHandler Handler(MIRBuilder, MF.getRegInfo(), Ret);
    if (!determineAndHandleAssignments(Handler, Assigner, SplitRetInfos,
                                       MIRBuilder, F.getCallingConv(),
                                       F.isVarArg()))
      return false;
  }

  MIRBuilder.insertInstr(Ret);
  return true;
}

bool VAXCallLowering::canLowerReturn(MachineFunction &MF,
                                     CallingConv::ID CallConv,
                                     SmallVectorImpl<BaseArgInfo> &Outs,
                                     bool IsVarArg) const {
  // Run the return CC to check if all outs can be assigned.
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs,
                 MF.getFunction().getContext());
  for (unsigned I = 0, E = Outs.size(); I < E; ++I) {
    MVT VT = MVT::getVT(Outs[I].Ty);
    // i64 returns need custom splitting we don't handle in GISel yet.
    if (VT == MVT::i64 || VT == MVT::f64)
      return false;
    if (RetCC_VAX(I, VT, VT, CCValAssign::Full, Outs[I].Flags[0],
                  Outs[I].Ty, CCInfo))
      return false;
  }
  return true;
}

bool VAXCallLowering::lowerFormalArguments(MachineIRBuilder &MIRBuilder,
                                           const Function &F,
                                           ArrayRef<ArrayRef<Register>> VRegs,
                                           FunctionLoweringInfo &FLI) const {
  MachineFunction &MF = MIRBuilder.getMF();

  // Mark AP as live-in: CALLS establishes AP pointing to the argument area.
  MF.getRegInfo().addLiveIn(VAX::AP);
  MIRBuilder.getMBB().addLiveIn(VAX::AP);

  if (VRegs.empty())
    return true; // No arguments — nothing to lower.

  const DataLayout &DL = MF.getDataLayout();
  CallingConv::ID CC = F.getCallingConv();

  SmallVector<ArgInfo, 8> SplitArgInfos;
  unsigned Index = 0;
  for (auto &Arg : F.args()) {
    // i64/f64 args need splitting — defer for now.
    if (Arg.getType()->isIntegerTy(64) || Arg.getType()->isDoubleTy()) {
      LLVM_DEBUG(dbgs() << "VAXCallLowering: rejecting i64/f64 arg\n");
      return false;
    }

    ArgInfo AInfo(VRegs[Index], Arg.getType(), Index);
    setArgFlags(AInfo, Index + AttributeList::FirstArgIndex, DL, F);
    splitToValueTypes(AInfo, SplitArgInfos, DL, CC);
    ++Index;
  }

  LLVM_DEBUG(dbgs() << "VAXCallLowering: " << SplitArgInfos.size()
                    << " split args\n");

  // CC_VAX assigns all arguments to the stack.
  IncomingValueAssigner Assigner(CC_VAX);
  VAXIncomingArgHandler Handler(MIRBuilder, MF.getRegInfo());

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CC, F.isVarArg(), MF, ArgLocs, F.getContext());
  if (!determineAssignments(Assigner, SplitArgInfos, CCInfo)) {
    LLVM_DEBUG(dbgs() << "VAXCallLowering: determineAssignments failed\n");
    return false;
  }
  if (!handleAssignments(Handler, SplitArgInfos, CCInfo, ArgLocs, MIRBuilder)) {
    LLVM_DEBUG(dbgs() << "VAXCallLowering: handleAssignments failed\n");
    return false;
  }

  // For variadic functions, record where variadic args start.
  if (F.isVarArg()) {
    VAXMachineFunctionInfo *FuncInfo = MF.getInfo<VAXMachineFunctionInfo>();
    FuncInfo->setVarArgsOffset(4 + CCInfo.getStackSize());
  }

  return true;
}

bool VAXCallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                CallLoweringInfo &Info) const {
  // TODO: Implement CALLS instruction emission.
  return false; // Fall back to SDAG for all calls.
}
