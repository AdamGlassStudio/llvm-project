//===-- VAXRegisterInfo.td - VAX Register Definitions ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class VAXReg<bits<4> Enc, string n> : Register<n> {
  let HWEncoding{3-0} = Enc;
  let Namespace = "VAX";
}

// General-purpose registers R0-R11
// Calling convention: R0-R1 return values; R0-R5 caller-saved; R6-R11 callee-saved
foreach i = 0-11 in
  def R#i : VAXReg<!cast<bits<4>>(i), "r"#i>, DwarfRegNum<[i]>;

// Special-purpose registers
def AP  : VAXReg<12, "ap">,  DwarfRegNum<[12]>; // Argument Pointer  (R12)
def FP  : VAXReg<13, "fp">,  DwarfRegNum<[13]>; // Frame Pointer     (R13)
def SP  : VAXReg<14, "sp">,  DwarfRegNum<[14]>; // Stack Pointer     (R14)
def PC  : VAXReg<15, "pc">,  DwarfRegNum<[15]>; // Program Counter   (R15)

// Processor Status Longword — not directly addressable as a GP register
// but needed for condition-code modeling
def PSW : Register<"psw"> { let Namespace = "VAX"; }

//===----------------------------------------------------------------------===//
// Register Classes
//===----------------------------------------------------------------------===//

// All GP registers including PC (used in addressing mode patterns)
def GPR : RegisterClass<"VAX", [i32], 32,
    (add R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, AP, FP, SP, PC)>;

// GP registers excluding PC (safe for ALU operations and most addressing)
def GPRnoPC : RegisterClass<"VAX", [i32], 32,
    (add R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, AP, FP, SP)>;

// Registers valid as index registers (R0-R11 only; no AP/FP/SP/PC)
def GPRI : RegisterClass<"VAX", [i32], 32,
    (add R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11)>;

// Word (i16) register class — same physical regs as GPRI, low 16 bits used.
// Allocation order: caller-saved first (R0-R5), then callee-saved (R6-R11).
def GPRW : RegisterClass<"VAX", [i16], 32,
    (add R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11)>;

// Byte (i8) register class — same physical regs as GPRI, low 8 bits used.
def GPRB : RegisterClass<"VAX", [i8], 32,
    (add R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11)>;
