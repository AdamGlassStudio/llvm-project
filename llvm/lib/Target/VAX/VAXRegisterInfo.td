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
  def R#i : VAXReg<!cast<bits<4>>(i), "%r"#i>, DwarfRegNum<[i]>;

// Special-purpose registers (ELF GAS requires % prefix)
def AP  : VAXReg<12, "%ap">,  DwarfRegNum<[12]>; // Argument Pointer  (R12)
def FP  : VAXReg<13, "%fp">,  DwarfRegNum<[13]>; // Frame Pointer     (R13)
def SP  : VAXReg<14, "%sp">,  DwarfRegNum<[14]>; // Stack Pointer     (R14)
def PC  : VAXReg<15, "%pc">,  DwarfRegNum<[15]>; // Program Counter   (R15)

// Processor Status Longword — not directly addressable as a GP register
// but needed for condition-code modeling
def PSW : Register<"psw"> { let Namespace = "VAX"; }

// Non-allocatable register class used to model implicit CC def/use on
// compare and branch instructions so the scheduler keeps them together.
let isAllocatable = 0 in
def PSWR : RegisterClass<"VAX", [i32], 32, (add PSW)>;

//===----------------------------------------------------------------------===//
// Register Classes
//===----------------------------------------------------------------------===//

// All GP registers including PC (used in addressing mode patterns)
def GPR : RegisterClass<"VAX", [i32], 32,
    (add R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, AP, FP, SP, PC)>;

// GP registers excluding PC (safe for ALU operations and most addressing)
def GPRnoPC : RegisterClass<"VAX", [i32], 32,
    (add R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, AP, FP, SP)>;

// Registers valid as index registers (R0-R11 only; no AP/FP/SP/PC).
// Also used for F_float (32-bit FP) values since VAX stores F_float in GPRs.
def GPRI : RegisterClass<"VAX", [i32, f32], 32,
    (add R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11)>;

// Word (i16) register class — same physical regs as GPRI, low 16 bits used.
// Allocation order: caller-saved first (R0-R5), then callee-saved (R6-R11).
def GPRW : RegisterClass<"VAX", [i16], 32,
    (add R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11)>;

// Byte (i8) register class — same physical regs as GPRI, low 8 bits used.
def GPRB : RegisterClass<"VAX", [i8], 32,
    (add R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11)>;

//===----------------------------------------------------------------------===//
// Quadword Register Pairs (for MOVQ, ASHQ, D_float, i64)
//===----------------------------------------------------------------------===//

// Sub-register indices for the low and high halves of a quad pair.
def sub_lo : SubRegIndex<32>;
def sub_hi : SubRegIndex<32, 32>;

// Consecutive register pairs: Rn:Rn+1 (low:high in little-endian).
// VAX allows ANY consecutive pair for quadword operations (not just even).
// R[n] holds bits 31:0, R[n+1] holds bits 63:32.
let Namespace = "VAX", CoveredBySubRegs = 1,
    SubRegIndices = [sub_lo, sub_hi] in {
  def R0_R1   : RegisterWithSubRegs<"%r0",  [R0,  R1]>;
  def R1_R2   : RegisterWithSubRegs<"%r1",  [R1,  R2]>;
  def R2_R3   : RegisterWithSubRegs<"%r2",  [R2,  R3]>;
  def R3_R4   : RegisterWithSubRegs<"%r3",  [R3,  R4]>;
  def R4_R5   : RegisterWithSubRegs<"%r4",  [R4,  R5]>;
  def R5_R6   : RegisterWithSubRegs<"%r5",  [R5,  R6]>;
  def R6_R7   : RegisterWithSubRegs<"%r6",  [R6,  R7]>;
  def R7_R8   : RegisterWithSubRegs<"%r7",  [R7,  R8]>;
  def R8_R9   : RegisterWithSubRegs<"%r8",  [R8,  R9]>;
  def R9_R10  : RegisterWithSubRegs<"%r9",  [R9,  R10]>;
  def R10_R11 : RegisterWithSubRegs<"%r10", [R10, R11]>;
}

// Quadword register class for 64-bit operations (MOVQ, ASHQ, D_float).
def QPR : RegisterClass<"VAX", [i64, f64], 32,
    (add R0_R1, R1_R2, R2_R3, R3_R4, R4_R5, R5_R6,
         R6_R7, R7_R8, R8_R9, R9_R10, R10_R11)>;
