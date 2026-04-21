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

  // Comparisons.
  getActionDefinitionsBuilder(G_ICMP)
      .legalFor({{s32, s8}, {s32, s16}, {s32, s32}})
      .widenScalarToNextPow2(1)
      .clampScalar(0, s32, s32)
      .clampScalar(1, s8, s32);

  // Pointer operations.
  getActionDefinitionsBuilder(G_PTR_ADD).legalFor({{p0, s32}});

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

  // Truncate.
  getActionDefinitionsBuilder(G_TRUNC)
      .legalFor({{s8, s16}, {s8, s32}, {s16, s32}});

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

  // i64 — lower to pairs (no native i64 ALU, only MOVQ/ASHQ).
  // For now, just widen/clamp above rules handle i64 by forcing to s32.
  // TODO: Custom i64 lowering using register pairs.

  // Floating point — mark as needing custom lowering eventually.
  // VAX F_float (f32) in GPRs, D_float (f64) in register pairs.
  // For now, fall back to SDAG for any FP.
  getActionDefinitionsBuilder({G_FADD, G_FSUB, G_FMUL, G_FDIV})
      .legalFor({s32})
      .lower();

  // Everything else: fall back.
  getLegacyLegalizerInfo().computeTables();
  verify(*ST.getInstrInfo());
}
