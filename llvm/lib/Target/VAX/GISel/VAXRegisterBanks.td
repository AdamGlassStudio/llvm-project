//===-- VAXRegisterBanks.td - VAX Register Banks ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// VAX has a single register bank: everything lives in GPRs.
// F_float and D_float are stored in GPRs (R0 / R0:R1), not in a separate FPU
// register file. This is architecturally mandated — there is no FP register
// bank on VAX.
//
//===----------------------------------------------------------------------===//

/// General Purpose Registers: R0-R11 (allocatable), AP, FP, SP, PC (reserved).
def GPRBRegBank : RegisterBank<"GPRB", [GPRI]>;
