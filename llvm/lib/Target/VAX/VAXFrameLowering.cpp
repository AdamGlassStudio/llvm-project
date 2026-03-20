//===-- VAXFrameLowering.cpp - VAX Frame Lowering ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXFrameLowering.h"
#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "VAXInstrInfo.h"
#include "VAXSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/MC/MCDwarf.h"
#include <algorithm>

using namespace llvm;

bool VAXFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  // VAX CALLS always establishes a frame pointer.
  return true;
}

void VAXFrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t StackSize = MFI.getStackSize();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // Emit CFI for the CALLS frame. After CALLS/entry, FP points to the
  // condition handler longword. The fixed layout is:
  //   FP+0:  condition handler
  //   FP+4:  entry mask | PSW | SPA | S
  //   FP+8:  saved AP
  //   FP+12: saved FP (caller's frame pointer)
  //   FP+16: saved PC (return address)
  const auto &TRI = *MF.getSubtarget().getRegisterInfo();
  unsigned DwarfFP = TRI.getDwarfRegNum(VAX::FP, true);
  unsigned DwarfPC = TRI.getDwarfRegNum(VAX::PC, true);
  unsigned DwarfAP = TRI.getDwarfRegNum(VAX::AP, true);
  unsigned CFIIdx;

  // CFA = FP + 0 (FP is the frame base after CALLS).
  CFIIdx = MF.addFrameInst(
      MCCFIInstruction::cfiDefCfa(nullptr, DwarfFP, /*Offset=*/0));
  BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIdx);

  // Return address at FP+16.
  CFIIdx = MF.addFrameInst(
      MCCFIInstruction::createOffset(nullptr, DwarfPC, /*Offset=*/16));
  BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIdx);

  // Saved FP at FP+12.
  CFIIdx = MF.addFrameInst(
      MCCFIInstruction::createOffset(nullptr, DwarfFP, /*Offset=*/12));
  BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIdx);

  // Saved AP at FP+8.
  CFIIdx = MF.addFrameInst(
      MCCFIInstruction::createOffset(nullptr, DwarfAP, /*Offset=*/8));
  BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIdx);

  // Emit CFI for callee-saved registers saved by the entry mask.
  // VAX CALLS reads the entry mask and pushes registers in descending order
  // (R11 first, then R10, ..., R0) below FP. Only registers with their bit
  // set in the mask are saved. So the highest-numbered saved register is at
  // FP-4, the next at FP-8, etc.
  const MachineFrameInfo &CMFI = MF.getFrameInfo();
  const std::vector<CalleeSavedInfo> &CSI = CMFI.getCalleeSavedInfo();
  if (!CSI.empty()) {
    // Collect the hardware register numbers for callee-saved registers and
    // sort descending (matching the hardware push order).
    SmallVector<std::pair<unsigned, MCRegister>, 6> SavedRegs;
    for (const auto &Info : CSI) {
      MCRegister Reg = Info.getReg();
      unsigned HWNum = TRI.getEncodingValue(Reg);
      SavedRegs.push_back({HWNum, Reg});
    }
    llvm::sort(SavedRegs,
               [](const auto &A, const auto &B) { return A.first > B.first; });

    int Offset = -4; // First saved reg at CFA-4 (FP-4)
    for (const auto &[HWNum, Reg] : SavedRegs) {
      unsigned DwarfReg = TRI.getDwarfRegNum(Reg, true);
      CFIIdx = MF.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, DwarfReg, Offset));
      BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIdx);
      Offset -= 4;
    }
  }

  if (StackSize == 0)
    return;

  // Allocate local frame: subl2 $stacksize, %sp
  BuildMI(MBB, MBBI, DL, TII.get(VAX::SUBL2_ri), VAX::SP)
      .addImm(StackSize)
      .addReg(VAX::SP);
}

void VAXFrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  // RET restores SP from the call frame — no explicit epilogue needed.
}

bool VAXFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return false;
}

bool VAXFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  // VAX CALLS reads the entry mask and saves registers in hardware.
  // No explicit spill instructions needed — return true to suppress default.
  return true;
}

bool VAXFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  // VAX RET restores registers from the call frame in hardware.
  return true;
}

MachineBasicBlock::iterator VAXFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  MachineInstr &Old = *I;
  DebugLoc DL = Old.getDebugLoc();
  unsigned Opc = Old.getOpcode();
  int64_t Amount = Old.getOperand(0).getImm();

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  if (Opc == TII.getCallFrameSetupOpcode()) {
    // CALLSEQ_START: decrement SP to reserve space for call arguments.
    if (Amount != 0)
      BuildMI(MBB, I, DL, TII.get(VAX::SUBL2_ri), VAX::SP)
          .addImm(Amount).addReg(VAX::SP);
  } else {
    // CALLSEQ_END: subtract callee-pop amount. On VAX, CALLS/RET pops
    // all args, so Amount - CalleePop is typically 0 (no-op).
    int64_t CalleePop = Old.getOperand(1).getImm();
    Amount -= CalleePop;
    if (Amount != 0)
      BuildMI(MBB, I, DL, TII.get(VAX::ADDL2_ri), VAX::SP)
          .addImm(Amount).addReg(VAX::SP);
  }
  return MBB.erase(I);
}
