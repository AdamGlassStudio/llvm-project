//===-- VAXRegisterBanks.td - VAX Register Banks ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// VAX has two register banks:
//   GPRB — single 32-bit GPRs (R0..R11 etc). Holds i1/i8/i16/i32/p0 and F_float.
//   QPRB — consecutive-pair pseudo-registers (R0_R1, R1_R2, ...). Holds i64
//          (for EMUL / EDIV / ASHQ) and D_float (for ADDD2 / MULD2 / etc.).
//
// F_float and i32 share GPRB because VAX has no separate FP register file —
// F_float lives in a single GPR. D_float and i64 share QPRB for the same
// reason: 64-bit values live in consecutive GPR pairs, not in a separate FPU.
//
//===----------------------------------------------------------------------===//

def GPRBRegBank : RegisterBank<"GPRB", [GPRI]>;
def QPRBRegBank : RegisterBank<"QPRB", [QPR]>;
