//===-- ABISysV_vax.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABISysV_vax.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"
#include "lldb/ValueObject/ValueObjectMemory.h"
#include "lldb/ValueObject/ValueObjectRegister.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/TargetParser/Triple.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABISysV_vax, ABIVAX)

// VAX DWARF register numbers match architectural register numbers.
enum dwarf_regnums {
  dwarf_r0 = 0,
  dwarf_r1,
  dwarf_r2,
  dwarf_r3,
  dwarf_r4,
  dwarf_r5,
  dwarf_r6,
  dwarf_r7,
  dwarf_r8,
  dwarf_r9,
  dwarf_r10,
  dwarf_r11,
  dwarf_ap,   // R12 = Argument Pointer
  dwarf_fp,   // R13 = Frame Pointer
  dwarf_sp,   // R14 = Stack Pointer
  dwarf_pc,   // R15 = Program Counter
  dwarf_psw,  // Processor Status Word
};

static const RegisterInfo g_register_infos[] = {
    {"r0", nullptr, 4, 0, eEncodingUint, eFormatHex,
     {dwarf_r0, dwarf_r0, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r1", nullptr, 4, 4, eEncodingUint, eFormatHex,
     {dwarf_r1, dwarf_r1, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r2", nullptr, 4, 8, eEncodingUint, eFormatHex,
     {dwarf_r2, dwarf_r2, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r3", nullptr, 4, 12, eEncodingUint, eFormatHex,
     {dwarf_r3, dwarf_r3, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r4", nullptr, 4, 16, eEncodingUint, eFormatHex,
     {dwarf_r4, dwarf_r4, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r5", nullptr, 4, 20, eEncodingUint, eFormatHex,
     {dwarf_r5, dwarf_r5, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r6", nullptr, 4, 24, eEncodingUint, eFormatHex,
     {dwarf_r6, dwarf_r6, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r7", nullptr, 4, 28, eEncodingUint, eFormatHex,
     {dwarf_r7, dwarf_r7, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r8", nullptr, 4, 32, eEncodingUint, eFormatHex,
     {dwarf_r8, dwarf_r8, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r9", nullptr, 4, 36, eEncodingUint, eFormatHex,
     {dwarf_r9, dwarf_r9, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r10", nullptr, 4, 40, eEncodingUint, eFormatHex,
     {dwarf_r10, dwarf_r10, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r11", nullptr, 4, 44, eEncodingUint, eFormatHex,
     {dwarf_r11, dwarf_r11, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r12", "ap", 4, 48, eEncodingUint, eFormatHex,
     {dwarf_ap, dwarf_ap, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r13", "fp", 4, 52, eEncodingUint, eFormatHex,
     {dwarf_fp, dwarf_fp, LLDB_REGNUM_GENERIC_FP, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r14", "sp", 4, 56, eEncodingUint, eFormatHex,
     {dwarf_sp, dwarf_sp, LLDB_REGNUM_GENERIC_SP, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"r15", "pc", 4, 60, eEncodingUint, eFormatHex,
     {dwarf_pc, dwarf_pc, LLDB_REGNUM_GENERIC_PC, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
    {"psw", nullptr, 4, 64, eEncodingUint, eFormatHex,
     {dwarf_psw, dwarf_psw, LLDB_REGNUM_GENERIC_FLAGS, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr, nullptr, nullptr},
};

static const uint32_t k_num_register_infos =
    sizeof(g_register_infos) / sizeof(RegisterInfo);

const lldb_private::RegisterInfo *
ABISysV_vax::GetRegisterInfoArray(uint32_t &count) {
  count = k_num_register_infos;
  return g_register_infos;
}

size_t ABISysV_vax::GetRedZoneSize() const { return 0; }

ABISP ABISysV_vax::CreateInstance(lldb::ProcessSP process_sp,
                                  const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::vax) {
    return ABISP(
        new ABISysV_vax(std::move(process_sp), MakeMCRegisterInfo(arch)));
  }
  return ABISP();
}

bool ABISysV_vax::PrepareTrivialCall(Thread &thread, lldb::addr_t sp,
                                     lldb::addr_t pc, lldb::addr_t ra,
                                     llvm::ArrayRef<addr_t> args) const {
  return false; // JIT not supported
}

bool ABISysV_vax::GetArgumentValues(Thread &thread, ValueList &values) const {
  return false;
}

Status ABISysV_vax::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                                         lldb::ValueObjectSP &new_value_sp) {
  return Status();
}

ValueObjectSP ABISysV_vax::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  return ValueObjectSP();
}

// VAX CALLS instruction creates a frame:
//   [condition handler]  (optional, at (FP))
//   [saved register mask + PSW]
//   [saved AP]
//   [saved FP]           <- new FP points here
//   [saved PC (return address)]
//   [argument count]     <- SP after CALLS
UnwindPlanSP ABISysV_vax::CreateFunctionEntryUnwindPlan() {
  UnwindPlan::Row row;
  // After CALLS, FP points to saved FP; return address is at FP+16.
  // CFA is the value of SP before the call.
  row.GetCFAValue().SetIsRegisterPlusOffset(dwarf_fp, 0);
  row.SetRegisterLocationToAtCFAPlusOffset(dwarf_pc, 16, true);
  row.SetRegisterLocationToAtCFAPlusOffset(dwarf_fp, 12, true);
  row.SetRegisterLocationToAtCFAPlusOffset(dwarf_ap, 8, true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("vax at-func-entry default");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  return plan_sp;
}

UnwindPlanSP ABISysV_vax::CreateDefaultUnwindPlan() {
  UnwindPlan::Row row;
  // Standard VAX frame: FP-relative unwinding.
  row.GetCFAValue().SetIsRegisterPlusOffset(dwarf_fp, 0);
  row.SetRegisterLocationToAtCFAPlusOffset(dwarf_pc, 16, true);
  row.SetRegisterLocationToAtCFAPlusOffset(dwarf_fp, 12, true);
  row.SetRegisterLocationToAtCFAPlusOffset(dwarf_ap, 8, true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("vax default unwind plan");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return plan_sp;
}

bool ABISysV_vax::RegisterIsVolatile(const RegisterInfo *reg_info) {
  // VAX CALLS/RET convention: R0-R5 are scratch (volatile).
  // R6-R11 are callee-saved (specified by the entry mask).
  // AP, FP, SP, PC are handled by CALLS/RET.
  int reg = reg_info->byte_offset / 4;
  return reg <= 5; // R0-R5 are volatile
}

void ABISysV_vax::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "System V ABI for VAX targets", CreateInstance);
}

void ABISysV_vax::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}
