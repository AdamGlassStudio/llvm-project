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
#include "llvm/IR/CallingConv.h"
#include "llvm/MC/MCDwarf.h"
#include <algorithm>

using namespace llvm;

bool VAXFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  // CALLS always establishes a frame pointer.
  // FastCC also uses FP for arg access (FP-relative).
  return true;
}

void VAXFrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t StackSize = MFI.getStackSize();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  CallingConv::ID CC = MF.getFunction().getCallingConv();

  if (CC == CallingConv::Fast) {
    // FastCC prologue: explicit frame setup (no CALLS hardware frame).
    //
    // Stack layout after prologue:
    //   FP+8+4*(N-1): argN-1
    //   ...
    //   FP+8:         arg0
    //   FP+4:         return address (pushed by JSB)
    //   FP+0:         saved old FP         <-- FP points here
    //   FP-4:         first saved CSR (lowest-numbered, via PUSHR)
    //   ...
    //   FP-4*K:       last saved CSR
    //   SP:           bottom of locals

    // pushl %fp — save caller's frame pointer.
    BuildMI(MBB, MBBI, DL, TII.get(VAX::PUSHL_r))
        .addReg(VAX::FP);

    // movl %sp, %fp — establish our frame pointer.
    BuildMI(MBB, MBBI, DL, TII.get(VAX::MOVL_rr), VAX::FP)
        .addReg(VAX::SP);

    // PUSHR $mask — save callee-saved registers.
    const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
    if (!CSI.empty()) {
      const auto &TRI = *MF.getSubtarget().getRegisterInfo();
      uint16_t Mask = 0;
      for (const auto &Info : CSI) {
        unsigned Enc = TRI.getEncodingValue(Info.getReg());
        if (Enc <= 11)
          Mask |= 1u << Enc;
      }
      if (Mask) {
        BuildMI(MBB, MBBI, DL, TII.get(VAX::PUSHR_imm))
            .addImm(Mask);
      }
    }

    // subl2 $stacksize, %sp — allocate locals.
    if (StackSize != 0) {
      BuildMI(MBB, MBBI, DL, TII.get(VAX::SUBL2_ri), VAX::SP)
          .addImm(StackSize)
          .addReg(VAX::SP);
    }

    // CFI: CFA = FP + 0.
    const auto &TRI = *MF.getSubtarget().getRegisterInfo();
    unsigned DwarfFP = TRI.getDwarfRegNum(VAX::FP, true);
    unsigned DwarfPC = TRI.getDwarfRegNum(VAX::PC, true);
    unsigned CFIIdx;

    CFIIdx = MF.addFrameInst(
        MCCFIInstruction::cfiDefCfa(nullptr, DwarfFP, /*Offset=*/0));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIdx);

    // Return address at FP+4 (pushed by JSB).
    CFIIdx = MF.addFrameInst(
        MCCFIInstruction::createOffset(nullptr, DwarfPC, /*Offset=*/4));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIdx);

    // Saved FP at FP+0 (pushed by pushl %fp).
    CFIIdx = MF.addFrameInst(
        MCCFIInstruction::createOffset(nullptr, DwarfFP, /*Offset=*/0));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIdx);

    // CFI for callee-saved registers (below FP, pushed by PUSHR).
    if (!CSI.empty()) {
      SmallVector<std::pair<unsigned, MCRegister>, 6> SavedRegs;
      for (const auto &Info : CSI) {
        MCRegister Reg = Info.getReg();
        unsigned HWNum = TRI.getEncodingValue(Reg);
        SavedRegs.push_back({HWNum, Reg});
      }
      llvm::sort(SavedRegs,
                 [](const auto &A, const auto &B) { return A.first < B.first; });

      // PUSHR pushes highest-numbered first. In memory (ascending address):
      // lowest-numbered is closest to FP (at FP-4*K), highest at FP-4.
      int NumCSR = SavedRegs.size();
      for (int i = 0; i < NumCSR; ++i) {
        unsigned DwarfReg = TRI.getDwarfRegNum(SavedRegs[i].second, true);
        int Offset = -4 * (NumCSR - i); // lowest-numbered at most negative offset
        CFIIdx = MF.addFrameInst(
            MCCFIInstruction::createOffset(nullptr, DwarfReg, Offset));
        BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIdx);
      }
    }

    return;
  }

  // Standard CALLS prologue below.

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
  // VAX CALLS pushes registers (R11 first, down to R0) BEFORE pushing the
  // frame header (PC, FP, AP, mask/PSW, handler). Since the stack grows
  // downward and FP is set to the handler address, saved registers end up
  // ABOVE FP+16 (above the saved PC). The lowest-numbered saved register
  // is closest to PC at FP+20, with higher-numbered registers at increasing
  // offsets above that.
  const MachineFrameInfo &CMFI = MF.getFrameInfo();
  const std::vector<CalleeSavedInfo> &CSI = CMFI.getCalleeSavedInfo();
  if (!CSI.empty()) {
    // Collect and sort ascending by hardware register number. CALLS pushes
    // R11 first (highest address) down to R0 last (lowest address = FP+20),
    // so ascending HW number order matches ascending memory address order.
    SmallVector<std::pair<unsigned, MCRegister>, 6> SavedRegs;
    for (const auto &Info : CSI) {
      MCRegister Reg = Info.getReg();
      unsigned HWNum = TRI.getEncodingValue(Reg);
      SavedRegs.push_back({HWNum, Reg});
    }
    llvm::sort(SavedRegs,
               [](const auto &A, const auto &B) { return A.first < B.first; });

    int Offset = 20; // First (lowest-numbered) saved reg at FP+20
    for (const auto &[HWNum, Reg] : SavedRegs) {
      unsigned DwarfReg = TRI.getDwarfRegNum(Reg, true);
      CFIIdx = MF.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, DwarfReg, Offset));
      BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIdx);
      Offset += 4;
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
  CallingConv::ID CC = MF.getFunction().getCallingConv();
  if (CC != CallingConv::Fast)
    return; // Standard CC: RET restores everything, no explicit epilogue.

  // FastCC epilogue (inserted before the RSB_RET terminator):
  //   movl  %fp, %sp       ; deallocate locals + CSRs in one shot
  //   movl  (%fp), %fp     ; restore old FP from saved location
  //   addl2 $4, %sp        ; advance SP past saved_old_FP
  //   ; RSB pops return address and jumps
  //
  // Note: POPR is not needed — movl %fp, %sp skips over saved CSRs,
  // and the RA has already ensured CSR values are in their registers
  // at this point (they were saved by PUSHR in prologue, the RA knows
  // they're callee-saved). Wait — that's wrong. The RA expects the
  // prologue/epilogue to actually save/restore CSRs. We must POPR.

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t StackSize = MFI.getStackSize();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();

  // Insert before the terminator (RSB_RET).
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  DebugLoc DL = MBBI->getDebugLoc();

  // Deallocate locals: addl2 $stacksize, %sp
  if (StackSize != 0) {
    BuildMI(MBB, MBBI, DL, TII.get(VAX::ADDL2_ri), VAX::SP)
        .addImm(StackSize)
        .addReg(VAX::SP);
  }

  // POPR $mask — restore callee-saved registers.
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  if (!CSI.empty()) {
    const auto &TRI = *MF.getSubtarget().getRegisterInfo();
    uint16_t Mask = 0;
    for (const auto &Info : CSI) {
      unsigned Enc = TRI.getEncodingValue(Info.getReg());
      if (Enc <= 11)
        Mask |= 1u << Enc;
    }
    if (Mask) {
      BuildMI(MBB, MBBI, DL, TII.get(VAX::POPR_imm))
          .addImm(Mask);
    }
  }

  // SP now points at saved_old_FP. FP still points there too.
  // Restore old FP: movl (%fp), %fp
  BuildMI(MBB, MBBI, DL, TII.get(VAX::MOVL_rm), VAX::FP)
      .addReg(VAX::FP)   // base
      .addImm(0)          // disp
      .addReg(0)          // index (none)
      .addImm(0);         // flags

  // Advance SP past saved_old_FP: addl2 $4, %sp
  BuildMI(MBB, MBBI, DL, TII.get(VAX::ADDL2_ri), VAX::SP)
      .addImm(4)
      .addReg(VAX::SP);
  // RSB_RET follows: pops return address from SP and jumps.
}

bool VAXFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return false;
}

bool VAXFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  // Standard CC: CALLS reads the entry mask and saves registers in hardware.
  // FastCC: PUSHR in emitPrologue handles CSR saves.
  // Either way, suppress the default individual spill instructions.
  return true;
}

bool VAXFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  // Standard CC: RET restores registers from the call frame in hardware.
  // FastCC: POPR in emitEpilogue handles CSR restores.
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
