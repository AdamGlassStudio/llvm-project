//===-- VAXLegalizerInfo.cpp - VAX Legalizer Info -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the legalization rules for the VAX GlobalISel pipeline.
//
// VAX natively supports i8, i16, i32 ALU operations — one of the few LLVM
// targets where sub-32-bit types can genuinely be legal. This is the key
// experiment: can GISel actually use this, or does the framework fight it?
//
//===----------------------------------------------------------------------===//

#include "VAXLegalizerInfo.h"
#include "VAXSubtarget.h"
#include "llvm/CodeGen/GlobalISel/LegalizerHelper.h"
#include "llvm/CodeGen/TargetOpcodes.h"

#define DEBUG_TYPE "vax-legalizer-info"

using namespace llvm;
using namespace LegalityPredicates;
using namespace LegalizeMutations;

VAXLegalizerInfo::VAXLegalizerInfo(const VAXSubtarget &ST) {
  using namespace TargetOpcode;

  const LLT s1 = LLT::scalar(1);
  const LLT s8 = LLT::scalar(8);
  const LLT s16 = LLT::scalar(16);
  const LLT s32 = LLT::scalar(32);
  const LLT p0 = LLT::pointer(0, 32);

  // VAX has native i8/i16/i32 add, sub, and, or, xor.
  // This is the whole point — let's see if GISel can keep them narrow.
  getActionDefinitionsBuilder({G_ADD, G_SUB, G_AND, G_OR, G_XOR})
      .legalFor({s8, s16, s32})
      .widenScalarToNextPow2(0)
      .clampScalar(0, s8, s32);

  // Multiply: VAX has MULB2/MULW2/MULL2 (native i8/i16/i32).
  getActionDefinitionsBuilder(G_MUL)
      .legalFor({s8, s16, s32})
      .widenScalarToNextPow2(0)
      .clampScalar(0, s8, s32);

  // Division: VAX has DIVB/DIVW/DIVL.
  getActionDefinitionsBuilder({G_SDIV, G_UDIV, G_SREM, G_UREM})
      .legalFor({s8, s16, s32})
      .widenScalarToNextPow2(0)
      .clampScalar(0, s8, s32);

  // Shifts: VAX ASHL/ASHR take an i8 shift count and i32 operand/result.
  // The shift count is always i8 (cnt field), but GISel models both operands.
  getActionDefinitionsBuilder({G_SHL, G_LSHR, G_ASHR})
      .legalFor({{s32, s32}})
      .widenScalarToNextPow2(0)
      .clampScalar(0, s32, s32)
      .clampScalar(1, s32, s32);

  // Comparisons. Allow pointer compares too (memmove, iterators, etc.).
  getActionDefinitionsBuilder(G_ICMP)
      .legalFor({{s32, s8}, {s32, s16}, {s32, s32}, {s32, p0}})
      .widenScalarToNextPow2(1)
      .clampScalar(0, s32, s32)
      .clampScalar(1, s8, s32);

  // Select: dst = cond ? tval : fval.  Handled by the instruction selector
  // via SELECT_CC_Pseudo (same mechanism SDAG uses).
  getActionDefinitionsBuilder(G_SELECT)
      .legalFor({{s8, s1}, {s16, s1}, {s32, s1}, {p0, s1}})
      .widenScalarToNextPow2(0)
      .clampScalar(0, s8, s32);

  // Pointer operations.
  getActionDefinitionsBuilder(G_PTR_ADD).legalFor({{p0, s32}});

  getActionDefinitionsBuilder(G_GLOBAL_VALUE).legalFor({p0});

  getActionDefinitionsBuilder(G_INTTOPTR).legalFor({{p0, s32}});
  getActionDefinitionsBuilder(G_PTRTOINT).legalFor({{s32, p0}});

  // Constants.
  getActionDefinitionsBuilder(G_CONSTANT)
      .legalFor({s8, s16, s32, p0})
      .widenScalarToNextPow2(0)
      .clampScalar(0, s8, s32);

  // Frame index.
  getActionDefinitionsBuilder(G_FRAME_INDEX).legalFor({p0});

  // Loads and stores: VAX has MOVB/MOVW/MOVL for native 8/16/32.
  getActionDefinitionsBuilder({G_LOAD, G_STORE})
      .legalFor({{s8, p0}, {s16, p0}, {s32, p0}, {p0, p0}})
      .widenScalarToNextPow2(0)
      .clampScalar(0, s8, s32);

  // Sign/zero extend: VAX has CVTBL/CVTWL (sign-extend) and MOVZBL/MOVZWL.
  getActionDefinitionsBuilder(G_SEXT)
      .legalFor({{s16, s8}, {s32, s8}, {s32, s16}})
      .clampScalar(0, s16, s32);
  getActionDefinitionsBuilder(G_ZEXT)
      .legalFor({{s16, s8}, {s32, s8}, {s32, s16}})
      .clampScalar(0, s16, s32);
  // ANYEXT behaves like ZEXT on VAX (upper bits don't matter; MOVZxL is fine
  // and often what the IR translator produces around narrow returns/args).
  getActionDefinitionsBuilder(G_ANYEXT)
      .legalFor({{s16, s8}, {s32, s8}, {s32, s16}})
      .clampScalar(0, s16, s32);

  // Truncate. s1 is legal as a destination (materializes as a 1-bit boolean
  // in a GPRB); downstream users (G_SELECT cond, G_BRCOND cond) take s1.
  getActionDefinitionsBuilder(G_TRUNC)
      .legalFor({{s8, s16}, {s8, s32}, {s16, s32},
                 {s1, s8}, {s1, s16}, {s1, s32}});

  // Sign/zero extend from memory (CVTBL + load, etc.)
  getActionDefinitionsBuilder({G_SEXTLOAD, G_ZEXTLOAD})
      .legalFor({{s32, p0}})
      .clampScalar(0, s32, s32);

  // Branches.
  getActionDefinitionsBuilder(G_BRCOND)
      .legalFor({s32})
      .widenScalarToNextPow2(0, 8)
      .clampScalar(0, s32, s32);

  // PHIs: VAX has no native vector; scalar s32 / pointer are the only legal
  // types. Narrower scalars get widened to s32.
  getActionDefinitionsBuilder(G_PHI)
      .legalFor({p0, s32})
      .widenScalarToNextPow2(0, 8)
      .clampScalar(0, s32, s32);
  getActionDefinitionsBuilder(G_BR).legalIf([](const LegalityQuery &) {
    return true;
  });

  // Absolute value — lowered to sub + icmp + select via custom hook.
  getActionDefinitionsBuilder(G_ABS)
      .customFor({s32})
      .widenScalarToNextPow2(0)
      .clampScalar(0, s32, s32);

  // i64 status:
  //   - G_AND / G_OR / G_XOR / G_LOAD / G_STORE on s64 are narrowed to s32
  //     pairs by the clampScalar(0, s8, s32) rules above. These are fully
  //     selectable and run under GISel today.
  //   - G_ADD / G_SUB / G_MUL / G_SDIV / G_UDIV / G_SREM / G_UREM /
  //     G_SHL / G_LSHR / G_ASHR on s64 fall back to SDAG.
  //
  // Why ADD/SUB fall back: VAX has native ADWC/SBWC, but the carry lives
  // in PSW (no explicit carry register), which doesn't fit the GISel
  // G_UADDO/G_UADDE explicit-carry-output model. The framework's generic
  // .lower() expands them to G_ADD + G_ICMP + select sequences (~3x the
  // code size of the SDAG ADWC chain), so we prefer SDAG fallback over
  // worse codegen. A future change can add a custom selector that emits
  // ADDL3_cc + ADWC bundled with PSW glue, or a 64-bit pseudo expanded
  // post-RA, to get parity. Until then, RegBankSelect rejects the s64
  // ADD/SUB (>32 bit operand mapping invalid) and triggers fallback.
  //
  // Why MUL/DIV/REM/shifts fall back: SDAG uses EMUL / EDIV / ASHQ which
  // are 64-bit-result instructions on QPR pairs. Reaching those from GISel
  // requires either a QPR-anchored register bank or per-op custom
  // selection — out of scope for the current pass.

  // Floating point: fall back to SDAG for ALL FP ops.
  // F_float (f32) and D_float (f64) are NOT IEEE 754 — calling the
  // standard compiler-rt libcalls (__addsf3, __adddf3, etc.) would
  // produce wrong results. SDAG handles VAX FP natively (F_float in
  // GPRs via ADDF2 etc.; D_float in QPR pairs via ADDD2 etc.) and
  // includes IEEE↔VAX conversion at constant-load sites. Replicating
  // that in GISel would require a new register bank anchored on QPR
  // plus custom legalization — substantial work for no correctness
  // gain. The RegBankSelect rejection of >32-bit operands is what
  // funnels f64 back to SDAG; f32 ops just stay unlegalized here.
  getActionDefinitionsBuilder({G_FADD, G_FSUB, G_FMUL, G_FDIV})
      .legalFor({s32})
      .lower();

  // Everything else: fall back.
  getLegacyLegalizerInfo().computeTables();
  verify(*ST.getInstrInfo());
}

bool VAXLegalizerInfo::legalizeCustom(LegalizerHelper &Helper, MachineInstr &MI,
                                      LostDebugLocObserver &) const {
  switch (MI.getOpcode()) {
  case TargetOpcode::G_ABS:
    return Helper.lowerAbsToCNeg(MI) == LegalizerHelper::Legalized;
  default:
    return false;
  }
}
