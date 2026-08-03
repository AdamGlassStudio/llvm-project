//===-- VAXSelectionDAGInfo.cpp - VAX SelectionDAG Info ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the VAXSelectionDAGInfo class, which provides
// target-specific lowering of memcpy/memset to VAX MOVC3/MOVC5 instructions.
//
//===----------------------------------------------------------------------===//

#include "VAXSelectionDAGInfo.h"
#include "VAXISelLowering.h"
#include "llvm/CodeGen/SelectionDAG.h"

using namespace llvm;

#define DEBUG_TYPE "vax-selectiondag-info"

// GCC uses MOVE_RATIO=6 (6 longwords = 24 bytes). Below this threshold,
// LLVM's generic expansion to inline loads/stores is preferred.
static constexpr unsigned MovcThreshold = 24;

// MOVC3 length operand is .rw (read word): 16-bit unsigned, max 65535 bytes.
static constexpr uint64_t MovcMaxSize = 65535;

SDValue VAXSelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst,
    SDValue Src, SDValue Size, Align DstAlign, Align SrcAlign, bool IsVolatile,
    bool AlwaysInline, MachinePointerInfo DstPtrInfo,
    MachinePointerInfo SrcPtrInfo) const {

  // Volatile copies must preserve ordering guarantees that MOVC3 may not.
  if (IsVolatile)
    return SDValue();

  // Only handle constant sizes: we can't prove a dynamic size fits in 16 bits.
  auto *SizeConst = dyn_cast<ConstantSDNode>(Size);
  if (!SizeConst)
    return SDValue();

  uint64_t SizeVal = SizeConst->getZExtValue();

  // Below threshold: let LLVM inline as individual loads/stores.
  if (SizeVal < MovcThreshold)
    return SDValue();

  // Above MOVC3 max: let LLVM emit a memcpy libcall.
  if (SizeVal > MovcMaxSize)
    return SDValue();

  // Truncate size to i32 if needed (memcpy intrinsic may use i64 size).
  // We've already verified the constant fits in 16 bits above.
  if (Size.getValueType() != MVT::i32)
    Size = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Size);

  // Emit VAXISD::MOVC3 (chain, dst, src, size) → chain.
  // ISel custom-selects this into the real MOVC3 instruction.
  return DAG.getNode(VAXISD::MOVC3, DL, MVT::Other,
                     {Chain, Dst, Src, Size});
}

SDValue VAXSelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst,
    SDValue Byte, SDValue Size, Align Alignment, bool IsVolatile,
    bool AlwaysInline, MachinePointerInfo DstPtrInfo) const {
  // MOVC5 memset lowering — future work.
  return SDValue();
}
