//===-- VAXInstrInfo.td - VAX Instruction Definitions ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "VAXInstrFormats.td"

// Instruction definitions are added incrementally by phase.

// Pseudo-instruction base class.
class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstVAX<0, outs, ins, asmstr, pattern> {
  let isPseudo = 1;
}

//===----------------------------------------------------------------------===//
// Custom DAG node declarations
//===----------------------------------------------------------------------===//

// Return from a CALLS/CALLG-protocol function.  Variadic: [chain, (reg...), glue?]
def SDT_VAXRet : SDTypeProfile<0, -1, []>;
def VAXISD_ret_flag : SDNode<"VAXISD::RET_FLAG", SDT_VAXRet,
    [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

//===----------------------------------------------------------------------===//
// Real instructions (Phase 3 — enough for "int f() { return 0; }")
//===----------------------------------------------------------------------===//

// NOP — kept as a known-good no-op; also satisfies old TableGen min-instruction req
def NOP : InstVAX<0x01, (outs), (ins), "nop", []>;

// RET — return from CALLS/CALLG frame (hardware restores saved regs, pops frame)
let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RET : InstVAX<0x04, (outs), (ins), "ret", [(VAXISD_ret_flag)]>;

// CLRL — zero a longword register; cheapest way to load the constant 0
def CLRL : InstVAX<0xD4, (outs GPRnoPC:$dst), (ins),
    "clrl\t$dst", [(set GPRnoPC:$dst, (i32 0))]>;
