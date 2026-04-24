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
  const LLT s64 = LLT::scalar(64);
  const LLT p0 = LLT::pointer(0, 32);

  // VAX has native i8/i16/i32 add, sub, and, or, xor.
  // This is the whole point — let's see if GISel can keep them narrow.
  getActionDefinitionsBuilder({G_ADD, G_SUB, G_AND, G_OR, G_XOR})
      .legalFor({s8, s16, s32})
      .widenScalarToNextPow2(0)
      .clampScalar(0, s8, s32);

  // Multiply: VAX has MULB2/MULW2/MULL2 (native i8/i16/i32). i64 MUL
  // goes through a libcall (__muldi3) like DIV/MOD for now — inline EMUL
  // + cross-products is possible but deferred.
  getActionDefinitionsBuilder(G_MUL)
      .legalFor({s8, s16, s32})
      .libcallFor({s64})
      .widenScalarToNextPow2(0)
      .clampScalar(0, s8, s64);

  // Division: VAX has DIVB/DIVW/DIVL (native i8/i16/i32). i64 DIV/REM have
  // no hardware support (EDIV is only 64÷32→32), so route i64 through
  // libcalls (__[u]divdi3 / __[u]moddi3) matching what SDAG does.
  getActionDefinitionsBuilder({G_SDIV, G_UDIV, G_SREM, G_UREM})
      .legalFor({s8, s16, s32})
      .libcallFor({s64})
      .widenScalarToNextPow2(0)
      .clampScalar(0, s8, s64);

  // Shifts: VAX ASHL/ASHR take an i8 shift count and i32 operand/result.
  // ASHQ (quadword) handles i64 SHL (positive count) and ASHR (negated
  // count), but NOT LSHR — ASHQ is arithmetic, so unsigned right shift
  // gets a libcall (__lshrdi3) on i64 just like SDAG.
  getActionDefinitionsBuilder({G_SHL, G_ASHR})
      .legalFor({{s32, s32}, {s64, s32}})
      .widenScalarToNextPow2(0)
      .clampScalar(0, s32, s64)
      .clampScalar(1, s32, s32);
  // LSHR s64: LLVM GlobalISel has no shift-libcall dispatch in
  // LegalizerHelper::getRTLibDesc (unlike SDAG), and narrowScalarShift
  // produces ~30 inline instructions. Leave s64 unsupported here so the
  // function falls back to SDAG, which emits the expected `__lshrdi3`
  // libcall (~5 insns), matching GCC.
  getActionDefinitionsBuilder(G_LSHR)
      .legalFor({{s32, s32}})
      .clampScalar(1, s32, s32)
      .widenScalarIf(
          [=](const LegalityQuery &Q) { return Q.Types[0].getSizeInBits() < 32; },
          LegalizeMutations::widenScalarOrEltToNextPow2(0, 32))
      .unsupportedIf([=](const LegalityQuery &Q) {
        return Q.Types[0].getSizeInBits() > 32;
      });

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

  // Loads and stores: VAX has MOVB/MOVW/MOVL for native 8/16/32. i64 loads
  // are narrowed to two i32 loads + G_MERGE_VALUES by narrowScalar.
  getActionDefinitionsBuilder({G_LOAD, G_STORE})
      .legalFor({{s8, p0}, {s16, p0}, {s32, p0}, {p0, p0}})
      .widenScalarToNextPow2(0)
      .clampScalar(0, s8, s32);

  // Merge/unmerge between s64 and s32 pairs (lo, hi). QPR is backed by two
  // consecutive GPRs; the selector emits REG_SEQUENCE / COPY with sub_lo /
  // sub_hi subregister indices.
  getActionDefinitionsBuilder(G_MERGE_VALUES)
      .legalFor({{s64, s32}});
  getActionDefinitionsBuilder(G_UNMERGE_VALUES)
      .legalFor({{s32, s64}});

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

  // i64 ADD/SUB lowers to {G_UADDO, G_UADDE} / {G_USUBO, G_USUBE} pairs
  // via narrowScalar. Declaring those legal here (instead of lowering to
  // generic G_ADD+G_ICMP sequences) lets the selector pick up native
  // ADDL3_cc+ADWC / SUBL3_cc+SBWC. The s1 carry-out vreg is a bookkeeping
  // artifact; actual carry flows through PSW between the two selected
  // instructions.
  getActionDefinitionsBuilder({G_UADDO, G_USUBO})
      .legalFor({{s32, s1}});
  getActionDefinitionsBuilder({G_UADDE, G_USUBE})
      .legalFor({{s32, s1}});

  // i64 status (updated 2026-04-22):
  //   - ADD/SUB: native via ADDL3_cc+ADWC / SUBL3_cc+SBWC (see above and
  //     VAXInstructionSelector::selectAddE / selectAddO).
  //   - AND/OR/XOR/LOAD/STORE: narrowed to s32 pairs by clampScalar above.
  //   - MUL/DIV/REM/shifts: still SDAG fallback. EMUL/EDIV/ASHQ on QPR
  //     pairs is future work on the QPRB register bank.

  // Floating point: fall back to SDAG for ALL FP ops.
  // VAX hardware has native FP instructions (ADDF/ADDD etc.) and SDAG
  // selects them directly — there's nothing fundamentally wrong with
  // FP under GISel. The hazard is the *libcall* fallback: compiler-rt's
  // __addsf3/__adddf3 are IEEE 754 and would silently corrupt F_float
  // / D_float values. No native FP selectors exist in the GISel
  // pipeline yet, so rather than risk that path, we keep FP on SDAG
  // until selectors (and a QPRB-anchored bank for f64) land. The
  // RegBankSelect rejection of >32-bit operands funnels f64 back to
  // SDAG; f32 ops just stay unlegalized here.
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
