//===-- VAXMCAsmInfo.h - VAX asm properties -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VAX_MCTARGETDESC_VAXMCASMINFO_H
#define LLVM_LIB_TARGET_VAX_MCTARGETDESC_VAXMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

namespace VAX {
/// Specifiers for MCSymbolRefExpr, used to encode PIC relocation types.
enum Specifier {
  S_None,
  S_GOT,     /// @GOT — GOT-relative reference for data
  S_PLT,     /// @PLT — PLT-relative reference for calls
  S_PCREL32, /// Internal: force longword PC-relative encoding (from relaxation)
  S_ABS,     /// Internal: force immediate/absolute encoding ($symbol in asm)
};
} // namespace VAX

class VAXMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit VAXMCAsmInfo(const Triple &TT);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_VAX_MCTARGETDESC_VAXMCASMINFO_H
