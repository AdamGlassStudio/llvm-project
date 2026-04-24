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
#include "VAXRegisterInfo.h"
#include "VAXSubtarget.h"
#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
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

/// Handler for outgoing call arguments: pushes each via PUSHL_r.
/// CC_VAX assigns MemOffsets in natural (left-to-right) order, but VAX
/// pushes args right-to-left. We record the ordered vregs here and the
/// caller emits PUSHL in reverse after assignments are determined.
struct VAXOutgoingArgHandler : public CallLowering::OutgoingValueHandler {
  VAXOutgoingArgHandler(MachineIRBuilder &MIRBuilder,
                        MachineRegisterInfo &MRI)
      : OutgoingValueHandler(MIRBuilder, MRI) {}

  // Collected (offset, vreg) pairs from CC_VAX assignments.
  SmallVector<std::pair<int64_t, Register>, 8> StackArgs;

  Register getStackAddress(uint64_t MemSize, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    // Return an undef pointer; we don't actually emit stores here, we just
    // record the vreg so the caller can emit PUSHL_r in reverse.
    MPO = MachinePointerInfo();
    return MIRBuilder.buildUndef(LLT::pointer(0, 32)).getReg(0);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    StackArgs.push_back({VA.getLocMemOffset(), ValVReg});
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA,
                        ISD::ArgFlagsTy Flags) override {
    // VAX has no register arguments — this shouldn't be called for calls.
    llvm_unreachable("VAX calls don't use register arguments");
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

/// Handler for incoming call return values (copied from R0/R1 to a vreg).
struct VAXCallReturnHandler : public CallLowering::IncomingValueHandler {
  MachineInstrBuilder &MIB;

  VAXCallReturnHandler(MachineIRBuilder &MIRBuilder,
                       MachineRegisterInfo &MRI, MachineInstrBuilder &MIB)
      : IncomingValueHandler(MIRBuilder, MRI), MIB(MIB) {}

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
    // Mark the physreg as an implicit def of the CALLS instruction so RA
    // knows it's live.
    MIB.addDef(PhysReg, RegState::Implicit);
    IncomingValueHandler::assignValueToReg(ValVReg, PhysReg, VA, Flags);
    // The base handler inserted `ValVReg = COPY PhysReg`.  ValVReg currently
    // has only a RegBank — constrain it to a RegClass so post-isel verification
    // succeeds even when ValVReg's only uses are other not-yet-selected MIs.
    MachineRegisterInfo &MRI = MIRBuilder.getMF().getRegInfo();
    if (MRI.getRegClassOrRegBank(ValVReg).is<const RegisterBank *>())
      MRI.setRegClass(ValVReg, &VAX::GPRIRegClass);
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
    // f64 under GISel would risk a compiler-rt libcall (__adddf3 etc.)
    // which is IEEE and would corrupt D_float. No native FP selectors
    // yet; reject to force SDAG fallback.
    if (VT == MVT::f64)
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
    // No native FP selectors in GISel yet; libcall fallback would hit
    // IEEE compiler-rt and corrupt D_float. Reject so SDAG handles it.
    if (Arg.getType()->isDoubleTy()) {
      LLVM_DEBUG(dbgs() << "VAXCallLowering: rejecting f64 arg\n");
      return false;
    }

    ArgInfo AInfo(VRegs[Index], Arg.getType(), Index);
    setArgFlags(AInfo, Index + AttributeList::FirstArgIndex, DL, F);

    // VAX passes byval/sret aggregates by *reference* (pointer in the arglist),
    // but the GISel framework would otherwise treat the byval slot as if the
    // payload were inlined.  Bail to SDAG, which already gets this right.
    if (AInfo.Flags[0].isByVal() || AInfo.Flags[0].isSRet()) {
      LLVM_DEBUG(dbgs() << "VAXCallLowering: byval/sret formal arg unsupported\n");
      return false;
    }

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
  LLVM_DEBUG(dbgs() << "VAXCallLowering::lowerCall: entry\n");
  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const Function &F = MF.getFunction();
  const DataLayout &DL = MF.getDataLayout();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  // VAX CALLS/RET prevents tail calls — clear the hint and fall through to
  // a normal call.  Returning false here would make IRTranslator abort.
  Info.IsTailCall = false;

  // Reject varargs calls for now.
  if (Info.IsVarArg) {
    LLVM_DEBUG(dbgs() << "VAXCallLowering::lowerCall: varargs not supported\n");
    return false;
  }

  // Reject f64 return: no native FP selectors in GISel yet, and the
  // libcall fallback would hit IEEE compiler-rt (wrong for D_float).
  if (!Info.OrigRet.Ty->isVoidTy()) {
    if (Info.OrigRet.Ty->isDoubleTy()) {
      LLVM_DEBUG(dbgs() << "VAXCallLowering::lowerCall: f64 return not supported\n");
      return false;
    }
  }

  // Reject f64 arguments (same reason).
  for (const ArgInfo &A : Info.OrigArgs) {
    if (A.Ty->isDoubleTy()) {
      LLVM_DEBUG(dbgs() << "VAXCallLowering::lowerCall: f64 arg not supported\n");
      return false;
    }
    if (A.Flags[0].isByVal() || A.Flags[0].isSRet()) {
      LLVM_DEBUG(dbgs() << "VAXCallLowering::lowerCall: byval/sret not supported\n");
      return false;
    }
  }

  // Split original arg infos into legal-typed pieces.
  SmallVector<ArgInfo, 32> SplitArgInfos;
  for (const ArgInfo &A : Info.OrigArgs) {
    splitToValueTypes(A, SplitArgInfos, DL, Info.CallConv);
  }

  // Assign outgoing args via CC_VAX (all go to stack).
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(Info.CallConv, Info.IsVarArg, MF, ArgLocs, F.getContext());
  OutgoingValueAssigner ArgAssigner(CC_VAX);
  VAXOutgoingArgHandler ArgHandler(MIRBuilder, MRI);
  if (!determineAssignments(ArgAssigner, SplitArgInfos, CCInfo)) {
    LLVM_DEBUG(dbgs() << "VAXCallLowering::lowerCall: arg determineAssignments failed\n");
    return false;
  }
  if (!handleAssignments(ArgHandler, SplitArgInfos, CCInfo, ArgLocs,
                         MIRBuilder)) {
    LLVM_DEBUG(dbgs() << "VAXCallLowering::lowerCall: arg handleAssignments failed\n");
    return false;
  }

  unsigned StackBytes = CCInfo.getStackSize();

  // Bracket with CALLSEQ_START/END.  We pass 0 to START because PUSHLs
  // adjust SP themselves; we pass StackBytes to END because CALLS/RET
  // pops the arg area (tell frame lowering SP moves back).
  MIRBuilder.buildInstr(VAX::ADJCALLSTACKDOWN)
      .addImm(0)
      .addImm(0);

  // Emit PUSHL_r for each stack arg, in reverse order (right-to-left).
  // ArgHandler collected (offset, vreg) pairs; sort by offset descending
  // so arg N pushes first and arg 0 last (arg 0 ends up at lowest SP).
  llvm::sort(ArgHandler.StackArgs,
             [](const std::pair<int64_t, Register> &A,
                const std::pair<int64_t, Register> &B) {
               return A.first > B.first;
             });
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const RegisterBankInfo &RBI = *MF.getSubtarget().getRegBankInfo();
  for (auto &P : ArgHandler.StackArgs) {
    auto PushMI = MIRBuilder.buildInstr(VAX::PUSHL_r).addUse(P.second);
    // Constrain the use operand to PUSHL_r's expected class (GPRnoPC).
    constrainOperandRegClass(MF, *TRI, MRI, TII, RBI, *PushMI,
                             PushMI->getDesc(), PushMI->getOperand(0), 0);
  }

  // Emit the CALLS instruction.
  // CALLS_direct takes (i32imm count, i32imm callee) where callee is a
  // TargetGlobalAddress or TargetExternalSymbol.
  // CALLS_indir takes (i32imm count, GPRnoPC callee) for register callees.
  MachineInstrBuilder CallMI;
  if (Info.Callee.isReg()) {
    CallMI = MIRBuilder.buildInstr(VAX::CALLS_indir)
                 .addImm(StackBytes / 4)
                 .addUse(Info.Callee.getReg());
  } else if (Info.Callee.isGlobal()) {
    CallMI = MIRBuilder.buildInstr(VAX::CALLS_direct)
                 .addImm(StackBytes / 4)
                 .addGlobalAddress(Info.Callee.getGlobal(),
                                   Info.Callee.getOffset(),
                                   Info.Callee.getTargetFlags());
  } else if (Info.Callee.isSymbol()) {
    CallMI = MIRBuilder.buildInstr(VAX::CALLS_direct)
                 .addImm(StackBytes / 4)
                 .addExternalSymbol(Info.Callee.getSymbolName(),
                                    Info.Callee.getTargetFlags());
  } else {
    LLVM_DEBUG(dbgs() << "VAXCallLowering::lowerCall: unsupported callee kind\n");
    return false;
  }
  CallMI.addRegMask(TRI->getCallPreservedMask(MF, Info.CallConv));

  // Constrain indirect-call register callee to its expected class.
  if (Info.Callee.isReg()) {
    constrainOperandRegClass(MF, *TRI, MRI, TII, RBI, *CallMI,
                             CallMI->getDesc(), CallMI->getOperand(1), 1);
  }

  // Copy return value(s) from R0 (and R1 if i64 — not yet supported).
  if (!Info.OrigRet.Ty->isVoidTy()) {
    SmallVector<ArgInfo, 4> SplitRetInfos;
    splitToValueTypes(Info.OrigRet, SplitRetInfos, DL, Info.CallConv);

    SmallVector<CCValAssign, 4> RetLocs;
    CCState RetCCInfo(Info.CallConv, Info.IsVarArg, MF, RetLocs,
                      F.getContext());
    OutgoingValueAssigner RetAssigner(RetCC_VAX);
    VAXCallReturnHandler RetHandler(MIRBuilder, MRI, CallMI);
    if (!determineAssignments(RetAssigner, SplitRetInfos, RetCCInfo)) {
      LLVM_DEBUG(dbgs() << "VAXCallLowering::lowerCall: ret determineAssignments failed\n");
      return false;
    }
    if (!handleAssignments(RetHandler, SplitRetInfos, RetCCInfo, RetLocs,
                           MIRBuilder)) {
      LLVM_DEBUG(dbgs() << "VAXCallLowering::lowerCall: ret handleAssignments failed\n");
      return false;
    }
  }

  // Close the call sequence: RET will pop StackBytes for us.
  MIRBuilder.buildInstr(VAX::ADJCALLSTACKUP)
      .addImm(StackBytes)
      .addImm(StackBytes);

  return true;
}
