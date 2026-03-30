//===-- VAXCallingConv.td - VAX Calling Conventions ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// VAX calling convention (CALLS/RET):
//   - All arguments passed on the stack; no register arguments.
//   - Return value: i32/pointer in R0; i64 in R0+R1.
//   - Callee-saved: R6-R11 (saved/restored via CALLS entry mask).
//   - R0-R5 are caller-saved (scratch).
//   - AP, FP, SP, PC are reserved.
//
// The entry mask mechanism (hardware-managed save/restore via CALLS/RET)
// is handled in VAXFrameLowering, not here.
//
//===----------------------------------------------------------------------===//

// Return value convention: i32/ptr in R0; i64 splits into two i32 → R0, R1.
// f32 returns in R0 (same register as i32).
// f64 (D_float) returns in R0_R1 (quadword register pair).
def RetCC_VAX : CallingConv<[
  // Promote i8/i16 returns to i32 (CALLS convention: R0 is always longword).
  CCIfType<[i8, i16], CCPromoteToType<i32>>,
  CCIfType<[f32], CCAssignToReg<[R0]>>,
  CCIfType<[f64], CCAssignToReg<[R0_R1]>>,
  CCIfType<[i64], CCCustom<"CC_VAX_RetI64">>,
  CCIfType<[i32], CCAssignToReg<[R0, R1]>>
]>;

// Argument convention: all on stack (CALLS pushes args right-to-left).
// i8/i16 promoted to i32 — VAX CALLS always pushes longwords.
def CC_VAX : CallingConv<[
  CCIfType<[i8, i16], CCPromoteToType<i32>>,
  CCIfType<[f64], CCAssignToStack<8, 4>>,
  CCIfType<[f32], CCAssignToStack<4, 4>>,
  CCAssignToStack<4, 4>
]>;

def CSR_VAX : CalleeSavedRegs<(add R6, R7, R8, R9, R10, R11)>;

//===----------------------------------------------------------------------===//
// Fast calling convention (JSB/RSB):
//   - Used for internal/static functions promoted to fastcc.
//   - First 4 integer/pointer args in R0-R3; remainder on stack.
//   - f32/f64 always on stack (VAX float format needs special handling).
//   - Return value: same as standard (i32 in R0; i64 in R0+R1).
//   - Callee-saved: R6-R11 (saved/restored via explicit PUSHR/POPR).
//   - AP is allocatable (not used by JSB).
//   - No hardware entry mask; prologue/epilogue are explicit.
//===----------------------------------------------------------------------===//

def CC_VAX_Fast : CallingConv<[
  CCIfType<[i8, i16], CCPromoteToType<i32>>,
  // Float/double always on stack (VAX format needs special push handling).
  CCIfType<[f64], CCAssignToStack<8, 4>>,
  CCIfType<[f32], CCAssignToStack<4, 4>>,
  // First 4 i32/ptr args in registers.
  CCIfType<[i32], CCAssignToReg<[R0, R1, R2, R3]>>,
  // Remaining args on stack.
  CCAssignToStack<4, 4>
]>;

def RetCC_VAX_Fast : CallingConv<[
  CCIfType<[i8, i16], CCPromoteToType<i32>>,
  CCIfType<[f32], CCAssignToReg<[R0]>>,
  CCIfType<[f64], CCAssignToReg<[R0_R1]>>,
  CCIfType<[i64], CCCustom<"CC_VAX_RetI64">>,
  CCIfType<[i32], CCAssignToReg<[R0, R1]>>
]>;

def CSR_VAX_Fast : CalleeSavedRegs<(add R6, R7, R8, R9, R10, R11)>;
