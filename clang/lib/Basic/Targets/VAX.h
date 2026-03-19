//===--- VAX.h - Declare VAX target feature support -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares VAX TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_VAX_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_VAX_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY VAXTargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];

public:
  VAXTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    // VAX is 32-bit, little-endian.
    IntWidth = IntAlign = 32;
    LongWidth = LongAlign = 32;
    LongLongWidth = 64;
    LongLongAlign = 32;
    PointerWidth = PointerAlign = 32;
    SizeType = UnsignedLong;
    PtrDiffType = SignedLong;
    IntPtrType = SignedLong;
    DoubleAlign = 32;
    LongDoubleWidth = 64;
    LongDoubleAlign = 32;

    // Data layout matches the LLVM backend.
    resetDataLayout(
        "e-m:e-p:32:32-i1:8:32-i8:8:32-i16:16:32-i64:32-f64:32-a:0:32-n32-nif");

    // VAX BIGGEST_ALIGNMENT is 32 bits (GCC: BIGGEST_ALIGNMENT = 32).
    // __attribute__((aligned)) without a value uses this.
    SuitableAlign = 32;
    DefaultAlignForAttributeAligned = 32;

    // VAX aligned longword (32-bit) accesses are atomic by architecture.
    // The bus interlock protocol guarantees atomicity even on SMP systems.
    MaxAtomicPromoteWidth = 32;
    MaxAtomicInlineWidth = 32;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  bool isValidCPUName(StringRef Name) const override { return true; }
  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override {}

  bool hasFeature(StringRef Feature) const override { return Feature == "vax"; }

  // VAX uses 4-byte alignment for all types including long long and double.
  // Don't promote preferred alignment to natural size (8 bytes).
  bool allowsLargerPreferedTypeAlignment() const override { return false; }

  ArrayRef<const char *> getGCCRegNames() const override;
  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    return false;
  }

  std::string_view getClobbers() const override { return ""; }

  // EH data registers match GCC VAX: R2 and R3 (DWARF register numbers).
  int getEHDataRegisterNumber(unsigned RegNo) const override {
    if (RegNo == 0)
      return 2; // R2 — exception pointer
    if (RegNo == 1)
      return 3; // R3 — exception selector
    return -1;
  }

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::CharPtrBuiltinVaList;
  }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_VAX_H
