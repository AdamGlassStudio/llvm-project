//===--- VAX.cpp - Implement VAX target feature support -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements VAX TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "VAX.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

void VAXTargetInfo::getTargetDefines(const LangOptions &Opts,
                                     MacroBuilder &Builder) const {
  Builder.defineMacro("__vax__");
  Builder.defineMacro("__vax");
  Builder.defineMacro("vax");
  // GCC compatibility.
  Builder.defineMacro("__VAX__");
}

const char *const VAXTargetInfo::GCCRegNames[] = {
    "r0",  "r1",  "r2",  "r3", "r4",  "r5",  "r6",  "r7",
    "r8",  "r9",  "r10", "r11", "ap",  "fp",  "sp",  "pc"};

ArrayRef<const char *> VAXTargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

ArrayRef<TargetInfo::GCCRegAlias> VAXTargetInfo::getGCCRegAliases() const {
  // No aliases — register names match GCC directly.
  return {};
}
