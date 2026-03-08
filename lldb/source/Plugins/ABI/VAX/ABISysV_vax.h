//===-- ABISysV_vax.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ABI_VAX_ABISYSV_VAX_H
#define LLDB_SOURCE_PLUGINS_ABI_VAX_ABISYSV_VAX_H

#include "lldb/Target/ABI.h"
#include "lldb/lldb-private.h"

class ABISysV_vax : public lldb_private::RegInfoBasedABI {
public:
  ~ABISysV_vax() override = default;

  size_t GetRedZoneSize() const override;

  bool PrepareTrivialCall(lldb_private::Thread &thread, lldb::addr_t sp,
                          lldb::addr_t functionAddress,
                          lldb::addr_t returnAddress,
                          llvm::ArrayRef<lldb::addr_t> args) const override;

  bool GetArgumentValues(lldb_private::Thread &thread,
                         lldb_private::ValueList &values) const override;

  lldb_private::Status
  SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                       lldb::ValueObjectSP &new_value) override;

  lldb::ValueObjectSP
  GetReturnValueObjectImpl(lldb_private::Thread &thread,
                           lldb_private::CompilerType &type) const override;

  lldb::UnwindPlanSP CreateFunctionEntryUnwindPlan() override;

  lldb::UnwindPlanSP CreateDefaultUnwindPlan() override;

  bool RegisterIsVolatile(const lldb_private::RegisterInfo *reg_info) override;

  bool CallFrameAddressIsValid(lldb::addr_t cfa) override {
    // Stack frames must be 4-byte aligned and non-zero.
    return (cfa & 0x03) == 0 && cfa != 0;
  }

  bool CodeAddressIsValid(lldb::addr_t pc) override { return true; }

  const lldb_private::RegisterInfo *
  GetRegisterInfoArray(uint32_t &count) override;

  uint64_t GetStackFrameSize() override { return 512; }

  static void Initialize();
  static void Terminate();
  static lldb::ABISP CreateInstance(lldb::ProcessSP process_sp,
                                    const lldb_private::ArchSpec &arch);
  static llvm::StringRef GetPluginNameStatic() { return "sysv-vax"; }
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

private:
  using lldb_private::RegInfoBasedABI::RegInfoBasedABI;
};

#endif // LLDB_SOURCE_PLUGINS_ABI_VAX_ABISYSV_VAX_H
