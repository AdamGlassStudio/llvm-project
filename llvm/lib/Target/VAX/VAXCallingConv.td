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
  CCIfType<[f32], CCAssignToReg<[R0]>>,
  CCIfType<[f64], CCAssignToReg<[R0_R1]>>,
  CCIfType<[i64], CCCustom<"CC_VAX_RetI64">>,
  CCIfType<[i32], CCAssignToReg<[R0, R1]>>
]>;

// Argument convention: all on stack (CALLS pushes args right-to-left)
def CC_VAX : CallingConv<[
  CCIfType<[f64], CCAssignToStack<8, 4>>,
  CCIfType<[f32], CCAssignToStack<4, 4>>,
  CCAssignToStack<4, 4>
]>;

def CSR_VAX : CalleeSavedRegs<(add R6, R7, R8, R9, R10, R11)>;
