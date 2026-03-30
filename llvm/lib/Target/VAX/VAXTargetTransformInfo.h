//===-- VAXTargetTransformInfo.h - VAX specific TTI -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TargetTransformInfo implementation for the VAX target. Provides
// target-specific cost model information used by LLVM's optimization passes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_VAXTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_VAX_VAXTARGETTRANSFORMINFO_H

#include "VAXSubtarget.h"
#include "VAXTargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"

namespace llvm {

class VAXTTIImpl final : public BasicTTIImplBase<VAXTTIImpl> {
  using BaseT = BasicTTIImplBase<VAXTTIImpl>;
  using TTI = TargetTransformInfo;
  friend BaseT;

  const VAXSubtarget *ST;
  const VAXTargetLowering *TLI;

  const VAXSubtarget *getST() const { return ST; }
  const VAXTargetLowering *getTLI() const { return TLI; }

public:
  explicit VAXTTIImpl(const VAXTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  // VAX has no jump tables in hardware — switch lowering should use
  // if-else chains or binary search, not lookup tables.
  bool shouldBuildLookupTables() const override { return false; }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_VAX_VAXTARGETTRANSFORMINFO_H
