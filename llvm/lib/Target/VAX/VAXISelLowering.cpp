//===-- VAXISelLowering.cpp - VAX DAG Lowering Implementation ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAXISelLowering.h"
#include "MCTargetDesc/VAXBaseInfo.h"
#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "VAXMachineFunctionInfo.h"
#include "VAXSubtarget.h"
#include "VAXTargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "vax-lower"

// IEEE 754 single → VAX F_float conversion (same as in VAXAsmPrinter.cpp).
static uint32_t convertIEEEToVAXF(uint32_t IEEE) {
  uint32_t Sign = (IEEE >> 31) & 1;
  uint32_t Exp = (IEEE >> 23) & 0xFF;
  uint32_t Frac = IEEE & 0x7FFFFF;
  if (Exp == 0 || Exp == 0xFF) return 0;
  uint32_t VaxExp = Exp + 2;
  if (VaxExp > 255) return 0;
  uint16_t W0 = (Sign << 15) | (VaxExp << 7) | ((Frac >> 16) & 0x7F);
  uint16_t W1 = Frac & 0xFFFF;
  return (uint32_t(W1) << 16) | W0;
}

// IEEE 754 double → VAX D_float conversion (same as in VAXAsmPrinter.cpp).
static uint64_t convertIEEEToVAXD(uint64_t IEEE) {
  uint64_t Sign = (IEEE >> 63) & 1;
  uint64_t Exp = (IEEE >> 52) & 0x7FF;
  uint64_t Frac = IEEE & 0xFFFFFFFFFFFFFULL;
  if (Exp == 0 || Exp == 0x7FF) return 0;
  int VaxExp = (int)Exp - 894;
  if (VaxExp <= 0 || VaxExp > 255) return 0;
  uint64_t VaxFrac = Frac << 3;
  uint16_t W0 = (Sign << 15) | (VaxExp << 7) | ((VaxFrac >> 48) & 0x7F);
  uint16_t W1 = (VaxFrac >> 32) & 0xFFFF;
  uint16_t W2 = (VaxFrac >> 16) & 0xFFFF;
  uint16_t W3 = VaxFrac & 0xFFFF;
  return (uint64_t(W3) << 48) | (uint64_t(W2) << 32) |
         (uint64_t(W1) << 16) | W0;
}

// Custom calling convention handler: split i64 return into R0 (lo) + R1 (hi).
static bool CC_VAX_RetI64(unsigned ValNo, MVT ValVT, MVT LocVT,
                           CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                           CCState &State) {
  // Allocate R0 for the low half and R1 for the high half.
  if (!State.AllocateReg(VAX::R0))
    return false;
  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, VAX::R0,
                                          MVT::i32, LocInfo));
  if (!State.AllocateReg(VAX::R1))
    return false;
  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, VAX::R1,
                                          MVT::i32, LocInfo));
  return true;
}

#define GET_CALLINGCONV_IMPL
#include "VAXGenCallingConv.inc"

VAXTargetLowering::VAXTargetLowering(const VAXTargetMachine &TM,
                                     const VAXSubtarget &STI)
    : TargetLowering(TM, STI) {
  // Register classes by value type.
  // i8/i16/i32 are all legal integer types. VAX has native byte (ADDB3, CMPB,
  // BICB3, etc.) and word (ADDW3, CMPW, BICW3, etc.) instructions.
  // Upper register bits are NOT zeroed by byte/word operations — LLVM handles
  // this via sub-register tracking (same approach as x86).
  addRegisterClass(MVT::i8,  &VAX::GPRBRegClass);
  addRegisterClass(MVT::i16, &VAX::GPRWRegClass);
  addRegisterClass(MVT::i32, &VAX::GPRnoPCRegClass);
  addRegisterClass(MVT::f32, &VAX::GPRIRegClass);
  addRegisterClass(MVT::f64, &VAX::QPRRegClass);

  // Finalize register class / type legalization info.
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(VAX::SP);
  setSchedulingPreference(Sched::RegPressure);

  //===------------------------------------------------------------------===//
  // Operation actions for i32 (longword)
  //===------------------------------------------------------------------===//

  // Global addresses are lowered to PC-relative wrappers.
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i32, Custom);
  setOperationAction(ISD::BlockAddress,  MVT::i32, Custom);
  setOperationAction(ISD::ConstantPool,  MVT::i32, Custom);

  // Jump table addresses are lowered to PC-relative wrappers.
  setOperationAction(ISD::JumpTable, MVT::i32, Custom);

  // AND is lowered to BICL (bit-clear) since VAX has no direct AND instruction.
  for (auto VT : {MVT::i8, MVT::i16, MVT::i32})
    setOperationAction(ISD::AND, VT, Custom);

  // Branches: lower ISD::BR_CC to VAXISD::CMP + VAXISD::BRCC.
  // Expanding BRCOND causes the DAG builder to produce BR_CC directly.
  for (auto VT : {MVT::i8, MVT::i16, MVT::i32})
    setOperationAction(ISD::BR_CC, VT, Custom);
  setOperationAction(ISD::BRCOND,  MVT::Other,  Expand);

  // Switch/jump tables: custom-lower BR_JT to CASEL instruction.
  setOperationAction(ISD::BR_JT, MVT::Other, Custom);

  // Variadic function support.
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG,   MVT::Other, Expand);
  setOperationAction(ISD::VACOPY,  MVT::Other, Expand);
  setOperationAction(ISD::VAEND,   MVT::Other, Expand);

  // Conditional value selection: SELECT_CC is custom, SELECT expands.
  for (auto VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::SELECT,    VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
  }

  // Extend SELECT_CC_Pseudo to also handle f32 results.
  setOperationAction(ISD::SELECT_CC, MVT::f32, Custom);

  // VAX DIVL is signed only; use EDIV for unsigned div/rem and signed rem.
  // No byte/word div instructions exist — promote to i32.
  setOperationAction(ISD::UDIV, MVT::i32, Custom);
  setOperationAction(ISD::UREM, MVT::i32, Custom);
  setOperationAction(ISD::SREM, MVT::i32, Custom);
  for (auto VT : {MVT::i8, MVT::i16}) {
    setOperationAction(ISD::SDIV, VT, Promote);
    setOperationAction(ISD::UDIV, VT, Promote);
    setOperationAction(ISD::SREM, VT, Promote);
    setOperationAction(ISD::UREM, VT, Promote);
  }

  // i64 mul: EMUL (32×32→64) handles both SMUL_LOHI and UMUL_LOHI.
  setOperationAction(ISD::SMUL_LOHI, MVT::i32, Custom);
  setOperationAction(ISD::UMUL_LOHI, MVT::i32, Custom);
  setOperationAction(ISD::MULHU, MVT::i32, Expand);
  setOperationAction(ISD::MULHS, MVT::i32, Expand);

  // i8/i16 multiply: no byte/word MUL instructions — promote to i32.
  for (auto VT : {MVT::i8, MVT::i16}) {
    setOperationAction(ISD::MUL,       VT, Promote);
    setOperationAction(ISD::SMUL_LOHI, VT, Promote);
    setOperationAction(ISD::UMUL_LOHI, VT, Promote);
    setOperationAction(ISD::MULHU,     VT, Promote);
    setOperationAction(ISD::MULHS,     VT, Promote);
  }

  // Carry-chained add/sub for i64 support: ADDL3 sets PSW.C, ADWC uses it.
  setOperationAction(ISD::ADDC, MVT::i32, Legal);
  setOperationAction(ISD::ADDE, MVT::i32, Legal);
  setOperationAction(ISD::SUBC, MVT::i32, Legal);
  setOperationAction(ISD::SUBE, MVT::i32, Legal);

  // Enable target DAG combine to catch the i64 comparison expansion pattern.
  setTargetDAGCombine(ISD::SELECT_CC);

  // Shifts: SHL/SRA/SRL all need custom lowering (ASHL/EXTZV).
  // VAX has no byte/word shift instructions, so i8/i16 shifts are custom
  // lowered by extending to i32, shifting, and truncating back.
  // Note: Promote doesn't work here because LLVM's operation legalizer
  // cannot promote shifts when the value type is legal.
  for (auto VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::SHL, VT, Custom);
    setOperationAction(ISD::SRA, VT, Custom);
    setOperationAction(ISD::SRL, VT, Custom);
  }
  for (auto VT : {MVT::i8, MVT::i16}) {
    setOperationAction(ISD::ROTL, VT, Expand);
    setOperationAction(ISD::ROTR, VT, Expand);
  }

  // Frame intrinsics for exception handling and debugging.
  setOperationAction(ISD::FRAMEADDR,  MVT::i32, Custom);
  setOperationAction(ISD::RETURNADDR, MVT::i32, Custom);
  setOperationAction(ISD::EH_RETURN,  MVT::Other, Custom);

  // Extending loads: all byte/word variants legal via CVT/MOVZ instructions.
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, MVT::i8,  Legal); // MOVZBL
  setLoadExtAction(ISD::EXTLOAD,  MVT::i32, MVT::i8,  Legal); // MOVZBL (anyext)
  setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i8,  Legal); // CVTBL
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, MVT::i16, Legal); // MOVZWL
  setLoadExtAction(ISD::EXTLOAD,  MVT::i32, MVT::i16, Legal); // MOVZWL (anyext)
  setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i16, Legal); // CVTWL
  // i16 extending load from i8.
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i16, MVT::i8,  Legal); // MOVZBW
  setLoadExtAction(ISD::EXTLOAD,  MVT::i16, MVT::i8,  Legal); // MOVZBW
  setLoadExtAction(ISD::SEXTLOAD, MVT::i16, MVT::i8,  Legal); // CVTBW
  // i1 loads: C _Bool is i1 in LLVM IR. Promote to byte load.
  for (auto VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::EXTLOAD,  VT, MVT::i1, Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
  }

  // SIGN_EXTEND_INREG: i1 must expand; i8/i16 handled by CVTBL/CVTWL.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  // Truncating stores: MOVB (i8) and MOVW (i16).
  setTruncStoreAction(MVT::i32, MVT::i8,  Legal);
  setTruncStoreAction(MVT::i32, MVT::i16, Legal);
  setTruncStoreAction(MVT::i16, MVT::i8,  Legal);

  // Truncating f64→f32 store: custom-lower to CVTDF + MOVF.
  setTruncStoreAction(MVT::f64, MVT::f32, Custom);

  // Extending f32→f64 load: expand to MOVF + CVTFD.
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);

  // Scalar integer types are all legal at i32; narrower types will be
  // promoted/expanded in later phases as instructions are added.

  // Integer SETCC must be expanded — VAX sets condition codes but has no
  // instruction that directly produces a 0/1 result from a comparison.
  for (auto VT : {MVT::i8, MVT::i16, MVT::i32})
    setOperationAction(ISD::SETCC, VT, Expand);

  // UDIVREM/SDIVREM pairs: expand.
  for (auto VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::UDIVREM, VT, Expand);
    setOperationAction(ISD::SDIVREM, VT, Expand);
  }

  // Bit manipulation: promote i8/i16 to i32; expand for i32.
  for (auto VT : {MVT::i8, MVT::i16}) {
    setOperationAction(ISD::CTLZ,       VT, Promote);
    setOperationAction(ISD::CTTZ,       VT, Promote);
    setOperationAction(ISD::CTPOP,      VT, Promote);
    setOperationAction(ISD::BSWAP,      VT, Promote);
    setOperationAction(ISD::CTLZ_ZERO_UNDEF, VT, Promote);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, VT, Promote);
  }
  setOperationAction(ISD::CTLZ,       MVT::i32, Expand);
  setOperationAction(ISD::CTTZ,       MVT::i32, Expand);
  setOperationAction(ISD::CTPOP,      MVT::i32, Expand);
  setOperationAction(ISD::BSWAP,      MVT::i32, Expand);
  setOperationAction(ISD::CTLZ_ZERO_UNDEF, MVT::i32, Expand);
  setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::i32, Legal);
  // VAX has ROTL but not ROTR; expand ROTR to ROTL with negated shift.
  setOperationAction(ISD::ROTR,       MVT::i32, Expand);

  // 64-bit shift parts: ASHQ handles SHL and SRA directly (single instruction).
  // SRL_PARTS has no direct quadword equivalent — ASHQ is arithmetic only.
  setOperationAction(ISD::SHL_PARTS,  MVT::i32, Custom);
  setOperationAction(ISD::SRA_PARTS,  MVT::i32, Custom);
  setOperationAction(ISD::SRL_PARTS,  MVT::i32, Expand);

  // Dynamic stack allocation (VLAs): expand to SP adjustment.
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Expand);
  setOperationAction(ISD::STACKSAVE,          MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE,       MVT::Other, Expand);

  // Atomic fence: VAX has no explicit memory barrier instruction.
  // The architecture has strict memory ordering (in-order, no store buffer),
  // so fences expand to compiler barriers. MP synchronization uses
  // interlocked instructions (BBSSI, BBCCI, ADAWI) rather than fences.
  setOperationAction(ISD::ATOMIC_FENCE,       MVT::Other, Expand);

  // Atomic load/store: VAX is single-core with strict ordering, so
  // monotonic/acquire/release atomic loads/stores are just regular loads/stores.
  setMaxAtomicSizeInBitsSupported(32);
  setMinCmpXchgSizeInBits(32);

  // VAX instructions are variable-length and byte-aligned — no function
  // alignment needed. Align(1) prevents .p2align directives that would
  // inflate .text section alignment from 1 (GAS default) to 4.
  setMinFunctionAlignment(Align(1));
  setPrefFunctionAlignment(Align(1));

  // FP bitcast: VAX D_float is not IEEE754, so bitcast between FP and int
  // must go through memory (store as one type, load as another).
  setOperationAction(ISD::BITCAST,    MVT::f32, Expand);
  setOperationAction(ISD::BITCAST,    MVT::i32, Expand);
  setOperationAction(ISD::BITCAST,    MVT::f64, Expand);

  // F_float (f32) support: VAX has native F_float arithmetic.
  setOperationAction(ISD::BR_CC,      MVT::f32, Custom);
  setOperationAction(ISD::SELECT_CC,  MVT::f32, Custom);
  setOperationAction(ISD::SELECT,     MVT::f32, Expand);
  setOperationAction(ISD::SETCC,      MVT::f32, Expand);
  // FP conversions.
  setOperationAction(ISD::FP_TO_SINT, MVT::i32, Legal);  // CVTFL
  setOperationAction(ISD::SINT_TO_FP, MVT::i32, Legal);  // CVTLF
  setOperationAction(ISD::FP_TO_UINT, MVT::i32, Expand);
  setOperationAction(ISD::UINT_TO_FP, MVT::i32, Custom);
  // FP operations VAX doesn't have natively.
  setOperationAction(ISD::FNEG,       MVT::f32, Legal);   // MNEGF
  setOperationAction(ISD::FABS,       MVT::f32, Expand);
  setOperationAction(ISD::FSQRT,      MVT::f32, Expand);
  setOperationAction(ISD::FREM,       MVT::f32, LibCall);  // fmodf
  setOperationAction(ISD::FCOPYSIGN,  MVT::f32, Expand);
  setOperationAction(ISD::FSIN,       MVT::f32, Expand);
  setOperationAction(ISD::FCOS,       MVT::f32, Expand);
  setOperationAction(ISD::FPOW,       MVT::f32, Expand);
  setOperationAction(ISD::FMINNUM,    MVT::f32, Expand);
  setOperationAction(ISD::FMAXNUM,    MVT::f32, Expand);
  setOperationAction(ISD::FMA,        MVT::f32, Expand);

  // D_float (f64) support: VAX has native D_float arithmetic using QPR pairs.
  setOperationAction(ISD::BR_CC,      MVT::f64, Custom);
  setOperationAction(ISD::SELECT_CC,  MVT::f64, Custom);
  setOperationAction(ISD::SELECT,     MVT::f64, Expand);
  setOperationAction(ISD::SETCC,      MVT::f64, Expand);
  setOperationAction(ISD::FP_ROUND,   MVT::f32, Legal);   // CVTDF
  setOperationAction(ISD::FP_EXTEND,  MVT::f64, Legal);   // CVTFD
  setOperationAction(ISD::FNEG,       MVT::f64, Legal);   // MNEGD
  setOperationAction(ISD::FABS,       MVT::f64, Expand);
  setOperationAction(ISD::FSQRT,      MVT::f64, Expand);
  setOperationAction(ISD::FREM,       MVT::f64, LibCall);  // fmod
  setOperationAction(ISD::FCOPYSIGN,  MVT::f64, Expand);
  setOperationAction(ISD::FSIN,       MVT::f64, Expand);
  setOperationAction(ISD::FCOS,       MVT::f64, Expand);
  setOperationAction(ISD::FPOW,       MVT::f64, Expand);
  setOperationAction(ISD::FMINNUM,    MVT::f64, Expand);
  setOperationAction(ISD::FMAXNUM,    MVT::f64, Expand);
  setOperationAction(ISD::FMA,        MVT::f64, Expand);
  // D_float ↔ int conversions.
  setOperationAction(ISD::FP_TO_SINT, MVT::i32, Legal);
  setOperationAction(ISD::SINT_TO_FP, MVT::i32, Legal);

  // Extending FP loads: there's no hardware "load f32, extend to f64" —
  // require separate load + CVTFD.
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);
}

bool VAXTargetLowering::isFPImmLegal(const APFloat &Imm, EVT VT,
                                      bool ForCodeSize) const {
  // Return true to prevent DAGCombiner from replacing ConstantFP stores
  // with integer stores using IEEE bit patterns. VAX uses non-IEEE
  // F_float/D_float formats — IEEE bits in memory are wrong. The ConstantFP
  // will be handled by ISel as a constant pool load (VAX format via AsmPrinter).
  return VT == MVT::f64 || VT == MVT::f32;
}

const char *VAXTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case VAXISD::RET_FLAG:     return "VAXISD::RET_FLAG";
  case VAXISD::PCRelWrapper: return "VAXISD::PCRelWrapper";
  case VAXISD::BICL:         return "VAXISD::BICL";
  case VAXISD::CMP:          return "VAXISD::CMP";
  case VAXISD::BRCC:         return "VAXISD::BRCC";
  case VAXISD::CALL:         return "VAXISD::CALL";
  case VAXISD::ASHL:         return "VAXISD::ASHL";
  case VAXISD::SELECT_CC:    return "VAXISD::SELECT_CC";
  case VAXISD::PUSHL:        return "VAXISD::PUSHL";
  case VAXISD::FCMP:         return "VAXISD::FCMP";
  case VAXISD::CASEL:        return "VAXISD::CASEL";
  case VAXISD::ASHQ:         return "VAXISD::ASHQ";
  case VAXISD::EMUL:         return "VAXISD::EMUL";
    case VAXISD::EDIV:         return "VAXISD::EDIV";
  case VAXISD::EXTZV:        return "VAXISD::EXTZV";
  case VAXISD::SELECT_CC_I64: return "VAXISD::SELECT_CC_I64";
  default:                   return nullptr;
  }
}

SDValue VAXTargetLowering::LowerOperation(SDValue Op,
                                           SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress: return LowerGlobalAddress(Op, DAG);
  case ISD::GlobalTLSAddress: return LowerGlobalTLSAddress(Op, DAG);
  case ISD::BlockAddress:  return LowerBlockAddress(Op, DAG);
  case ISD::ConstantPool:  return LowerConstantPool(Op, DAG);
  case ISD::JumpTable:     return LowerJumpTable(Op, DAG);
  case ISD::AND:           return LowerAND(Op, DAG);
  case ISD::SHL:           return LowerSHL(Op, DAG);
  case ISD::SRA:           return LowerSRA(Op, DAG);
  case ISD::SRL:           return LowerSRL(Op, DAG);
  case ISD::SHL_PARTS:     return LowerSHL_PARTS(Op, DAG);
  case ISD::SRA_PARTS:     return LowerSRA_PARTS(Op, DAG);
  case ISD::SMUL_LOHI:     return LowerSMUL_LOHI(Op, DAG);
  case ISD::UMUL_LOHI:     return LowerUMUL_LOHI(Op, DAG);
  case ISD::UDIV:          return LowerUDIV(Op, DAG);
  case ISD::UREM:          return LowerUREM(Op, DAG);
  case ISD::SREM:          return LowerSREM(Op, DAG);
  case ISD::BR_CC:         return LowerBR_CC(Op, DAG);
  case ISD::BR_JT:         return LowerBR_JT(Op, DAG);
  case ISD::SELECT_CC:     return LowerSELECT_CC(Op, DAG);
  case ISD::VASTART:       return LowerVASTART(Op, DAG);
  case ISD::UINT_TO_FP:    return LowerUINT_TO_FP(Op, DAG);
  case ISD::STORE:       return LowerSTORE(Op, DAG);
  case ISD::FRAMEADDR:     return LowerFRAMEADDR(Op, DAG);
  case ISD::RETURNADDR:    return LowerRETURNADDR(Op, DAG);
  case ISD::EH_RETURN:     return LowerEH_RETURN(Op, DAG);
  default:
    report_fatal_error(Twine("VAXTargetLowering::LowerOperation: unimplemented "
                             "opcode ") +
                       Twine(Op.getOpcode()));
  }
}

// Custom-lower truncating f64→f32 store: FP_ROUND + store.
// Lower unsigned int → float/double.
// VAX has CVTLF/CVTLD for signed int, but no unsigned variant.
// Strategy: do signed conversion, then add 2^32 if the input was negative
// (i.e., bit 31 was set, meaning the unsigned value was >= 2^31).
SDValue VAXTargetLowering::LowerUINT_TO_FP(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Src = Op.getOperand(0);
  EVT DstVT = Op.getValueType();

  // signed_result = (sint_to_fp src)
  SDValue Signed = DAG.getNode(ISD::SINT_TO_FP, DL, DstVT, Src);

  // correction = (DstVT) 4294967296.0  (2^32)
  APFloat Correction(DstVT == MVT::f32 ? APFloat::IEEEsingle()
                                       : APFloat::IEEEdouble());
  Correction.convertFromAPInt(APInt(64, 1ULL << 32), /*isSigned=*/false,
                              APFloat::rmNearestTiesToEven);
  SDValue CorrVal = DAG.getConstantFP(Correction, DL, DstVT);

  // add = signed_result + correction
  SDValue Adjusted = DAG.getNode(ISD::FADD, DL, DstVT, Signed, CorrVal);

  // select: if src < 0 (as signed), use adjusted, else use signed result
  SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
  SDValue Result = DAG.getSelectCC(DL, Src, Zero, Adjusted, Signed,
                                   ISD::SETLT);
  return Result;
}

SDValue VAXTargetLowering::LowerSTORE(SDValue Op, SelectionDAG &DAG) const {
  StoreSDNode *ST = cast<StoreSDNode>(Op.getNode());
  SDLoc DL(Op);

  LLVM_DEBUG(dbgs() << "VAX LowerSTORE called, isTrunc="
             << ST->isTruncatingStore() << "\n");

  // Only handle truncating FP stores.
  if (!ST->isTruncatingStore())
    return Op;

  SDValue Value = ST->getValue();
  EVT StVT = ST->getMemoryVT();

  // f64 → f32 truncating store: emit FP_ROUND + normal store.
  if (Value.getValueType() == MVT::f64 && StVT == MVT::f32) {
    SDValue Rounded = DAG.getNode(ISD::FP_ROUND, DL, MVT::f32, Value,
                                  DAG.getTargetConstant(0, DL, MVT::i32));
    return DAG.getStore(ST->getChain(), DL, Rounded, ST->getBasePtr(),
                        ST->getPointerInfo(), ST->getBaseAlign(),
                        ST->getMemOperand()->getFlags());
  }

  return Op;
}

SDValue VAXTargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  // Map LLVM condition codes to VAX branch condition integers (see VAXCC enum
  // in VAXInstrInfo.td / branch PatLeaves).
  // Note: SETUGT/etc. mean "unsigned" for integer and "unordered" for FP.
  // VAX F_float/D_float have no NaN, so unordered FP compares → ordered.
  bool IsFP = LHS.getValueType().isFloatingPoint();
  unsigned VAXCC;
  switch (CC) {
  default: llvm_unreachable("unsupported condition code for VAX BR_CC");
  case ISD::SETEQ:  case ISD::SETOEQ: VAXCC = 0; break; // BEQL
  case ISD::SETNE:  case ISD::SETONE: VAXCC = 1; break; // BNEQ
  case ISD::SETGT:  case ISD::SETOGT: VAXCC = 2; break; // BGTR  (signed)
  case ISD::SETGE:  case ISD::SETOGE: VAXCC = 3; break; // BGEQ  (signed)
  case ISD::SETLT:  case ISD::SETOLT: VAXCC = 4; break; // BLSS  (signed)
  case ISD::SETLE:  case ISD::SETOLE: VAXCC = 5; break; // BLEQ  (signed)
  // FP-only unordered: no NaN on VAX, so map to ordered equivalents.
  case ISD::SETUEQ: VAXCC = 0; break; // BEQL
  case ISD::SETUNE: VAXCC = 1; break; // BNEQ
  // SETUGT/SETUGE/SETULT/SETULE: unsigned for integer, unordered for FP.
  case ISD::SETUGT: VAXCC = IsFP ? 2 : 6; break; // BGTR or BGTRU
  case ISD::SETUGE: VAXCC = IsFP ? 3 : 7; break; // BGEQ or BGEQU
  case ISD::SETULT: VAXCC = IsFP ? 4 : 8; break; // BLSS or BLSSU
  case ISD::SETULE: VAXCC = IsFP ? 5 : 9; break; // BLEQ or BLEQU
  // SETUO (unordered) → never true on VAX (no NaN).
  case ISD::SETUO:
    return Chain;
  // SETO (ordered) → always true on VAX — unconditional branch.
  case ISD::SETO:
    return DAG.getNode(ISD::BR, DL, MVT::Other, Chain, Dest);
  }

  // Use FCMP for floating-point, CMP for integer.
  unsigned CmpOpc = IsFP ? VAXISD::FCMP : VAXISD::CMP;
  SDValue Cmp = DAG.getNode(CmpOpc, DL, MVT::Glue, LHS, RHS);
  return DAG.getNode(VAXISD::BRCC, DL, MVT::Other,
                     Chain, Dest,
                     DAG.getConstant(VAXCC, DL, MVT::i32),
                     Cmp);
}

static unsigned mapISDCCToVAXCC(ISD::CondCode CC, bool IsFP) {
  switch (CC) {
  default: llvm_unreachable("unsupported condition code for VAX SELECT_CC");
  case ISD::SETEQ:  case ISD::SETOEQ: case ISD::SETUEQ: return 0;
  case ISD::SETNE:  case ISD::SETONE: case ISD::SETUNE: return 1;
  case ISD::SETGT:  case ISD::SETOGT: return 2;
  case ISD::SETGE:  case ISD::SETOGE: return 3;
  case ISD::SETLT:  case ISD::SETOLT: return 4;
  case ISD::SETLE:  case ISD::SETOLE: return 5;
  case ISD::SETUGT: return IsFP ? 2 : 6;
  case ISD::SETUGE: return IsFP ? 3 : 7;
  case ISD::SETULT: return IsFP ? 4 : 8;
  case ISD::SETULE: return IsFP ? 5 : 9;
  }
}

SDValue VAXTargetLowering::LowerSELECT_CC(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue TrueV = Op.getOperand(2);
  SDValue FalseV = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();

  // VAX has no NaN — SETUO always false, SETO always true.
  if (CC == ISD::SETUO)
    return FalseV;
  if (CC == ISD::SETO)
    return TrueV;

  bool IsFP = LHS.getValueType().isFloatingPoint();
  unsigned VAXCC = mapISDCCToVAXCC(CC, IsFP);
  unsigned CmpOpc = IsFP ? VAXISD::FCMP : VAXISD::CMP;
  SDValue Cmp = DAG.getNode(CmpOpc, DL, MVT::Glue, LHS, RHS);
  return DAG.getNode(VAXISD::SELECT_CC, DL, Op.getValueType(),
                     TrueV, FalseV,
                     DAG.getConstant(VAXCC, DL, MVT::i32),
                     Cmp);
}

SDValue VAXTargetLowering::LowerAND(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();
  SDValue A = Op.getOperand(0);
  SDValue B = Op.getOperand(1);
  // VAX has no direct AND; use BICL(mask, src) = src & ~mask.
  // AND(A, B) = BICL(~B, A) since ~~B = B.
  if (auto *CN = dyn_cast<ConstantSDNode>(B)) {
    // Constant operand: fold the NOT at compile time (single instruction).
    return DAG.getNode(VAXISD::BICL, DL, VT,
                       DAG.getConstant(~CN->getAPIntValue(), DL, VT), A);
  }
  // Register operand: emit BICL(XOR(B, -1), A) → MCOML + BICL3 (two insns).
  SDValue NotB = DAG.getNode(ISD::XOR, DL, VT, B,
                             DAG.getAllOnesConstant(DL, VT));
  return DAG.getNode(VAXISD::BICL, DL, VT, NotB, A);
}

SDValue VAXTargetLowering::LowerSHL(SDValue Op, SelectionDAG &DAG) const {
  // VAX ASHL is i32-only. For i8/i16, extend to i32, shift, truncate.
  SDLoc DL(Op);
  EVT VT = Op.getSimpleValueType();
  SDValue Src = Op.getOperand(0);
  SDValue Cnt = Op.getOperand(1);

  if (VT != MVT::i32) {
    SDValue Ext = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i32, Src);
    SDValue Shift = DAG.getNode(ISD::SHL, DL, MVT::i32, Ext, Cnt);
    return DAG.getNode(ISD::TRUNCATE, DL, VT, Shift);
  }

  // i32: lower directly to ASHL.
  return DAG.getNode(VAXISD::ASHL, DL, MVT::i32, Cnt, Src);
}

SDValue VAXTargetLowering::LowerSRA(SDValue Op, SelectionDAG &DAG) const {
  // VAX ASHL with negative count does arithmetic right shift.
  // For i8/i16, sign-extend to i32, shift, truncate.
  SDLoc DL(Op);
  EVT VT = Op.getSimpleValueType();
  SDValue Src = Op.getOperand(0);
  SDValue Cnt = Op.getOperand(1);

  if (VT != MVT::i32) {
    SDValue Ext = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i32, Src);
    SDValue Shift = DAG.getNode(ISD::SRA, DL, MVT::i32, Ext, Cnt);
    return DAG.getNode(ISD::TRUNCATE, DL, VT, Shift);
  }

  if (auto *CN = dyn_cast<ConstantSDNode>(Cnt)) {
    int64_t NegAmt = -CN->getSExtValue();
    return DAG.getNode(VAXISD::ASHL, DL, MVT::i32,
                       DAG.getSignedConstant(NegAmt, DL, MVT::i32), Src);
  }
  SDValue NegCnt = DAG.getNode(ISD::SUB, DL, MVT::i32,
                               DAG.getConstant(0, DL, MVT::i32), Cnt);
  return DAG.getNode(VAXISD::ASHL, DL, MVT::i32, NegCnt, Src);
}

SDValue VAXTargetLowering::LowerSRL(SDValue Op, SelectionDAG &DAG) const {
  // VAX has no logical right shift instruction. Use EXTZV (extract field).
  // For i8/i16, zero-extend to i32, shift, truncate.
  SDLoc DL(Op);
  EVT VT = Op.getSimpleValueType();
  SDValue Src = Op.getOperand(0);
  SDValue Cnt = Op.getOperand(1);

  if (VT != MVT::i32) {
    SDValue Ext = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, Src);
    SDValue Shift = DAG.getNode(ISD::SRL, DL, MVT::i32, Ext, Cnt);
    return DAG.getNode(ISD::TRUNCATE, DL, VT, Shift);
  }

  if (auto *CN = dyn_cast<ConstantSDNode>(Cnt)) {
    unsigned N = CN->getZExtValue() & 31;
    if (N == 0) return Src;
    return DAG.getNode(VAXISD::EXTZV, DL, MVT::i32,
                       DAG.getConstant(N, DL, MVT::i32),
                       DAG.getConstant(32 - N, DL, MVT::i32), Src);
  }
  SDValue Size = DAG.getNode(ISD::SUB, DL, MVT::i32,
                             DAG.getConstant(32, DL, MVT::i32), Cnt);
  return DAG.getNode(VAXISD::EXTZV, DL, MVT::i32, Cnt, Size, Src);
}

SDValue VAXTargetLowering::LowerSHL_PARTS(SDValue Op,
                                           SelectionDAG &DAG) const {
  // shl_parts(lo, hi, amt) → ASHQ(amt, lo, hi) → (dst_lo, dst_hi)
  SDLoc DL(Op);
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Amt = Op.getOperand(2);
  SDValue ASHQ = DAG.getNode(VAXISD::ASHQ, DL,
                              DAG.getVTList(MVT::i32, MVT::i32), Amt, Lo, Hi);
  return DAG.getMergeValues({ASHQ.getValue(0), ASHQ.getValue(1)}, DL);
}

SDValue VAXTargetLowering::LowerSRA_PARTS(SDValue Op,
                                           SelectionDAG &DAG) const {
  // sra_parts(lo, hi, amt) → ASHQ(-amt, lo, hi) → (dst_lo, dst_hi)
  SDLoc DL(Op);
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Amt = Op.getOperand(2);

  SDValue NegAmt;
  if (auto *CN = dyn_cast<ConstantSDNode>(Amt)) {
    NegAmt = DAG.getSignedConstant(-CN->getSExtValue(), DL, MVT::i32);
  } else {
    NegAmt = DAG.getNode(ISD::SUB, DL, MVT::i32,
                          DAG.getConstant(0, DL, MVT::i32), Amt);
  }
  SDValue ASHQ = DAG.getNode(VAXISD::ASHQ, DL,
                              DAG.getVTList(MVT::i32, MVT::i32), NegAmt, Lo, Hi);
  return DAG.getMergeValues({ASHQ.getValue(0), ASHQ.getValue(1)}, DL);
}

SDValue VAXTargetLowering::LowerSMUL_LOHI(SDValue Op,
                                            SelectionDAG &DAG) const {
  // smul_lohi(a, b) → EMUL(a, b, 0) → (lo, hi)
  // EMUL computes a*b + sign_extend(addend); we pass addend=0.
  SDLoc DL(Op);
  SDValue A = Op.getOperand(0);
  SDValue B = Op.getOperand(1);
  SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
  SDValue EMUL = DAG.getNode(VAXISD::EMUL, DL,
                              DAG.getVTList(MVT::i32, MVT::i32), A, B, Zero);
  return DAG.getMergeValues({EMUL.getValue(0), EMUL.getValue(1)}, DL);
}

SDValue VAXTargetLowering::LowerUMUL_LOHI(SDValue Op,
                                            SelectionDAG &DAG) const {
  // umul_lohi(a, b) → unsigned 32×32→64 using signed EMUL + fixup.
  //
  // EMUL computes signed(a)*signed(b). For unsigned interpretation:
  //   unsigned_product = signed_product
  //     + (a < 0 ? b : 0) << 32
  //     + (b < 0 ? a : 0) << 32
  //
  // The fixup uses arithmetic right shift by 31 to create a mask:
  //   ashl $-31, a → 0xFFFFFFFF if bit 31 set, else 0
  //   AND with b → b if a was "negative", else 0
  SDLoc DL(Op);
  SDValue A = Op.getOperand(0);
  SDValue B = Op.getOperand(1);
  SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
  SDValue Shift31 = DAG.getConstant(31, DL, MVT::i32);

  // Signed 32×32→64 product.
  SDValue Prod = DAG.getNode(VAXISD::EMUL, DL,
                              DAG.getVTList(MVT::i32, MVT::i32), A, B, Zero);
  SDValue Lo = Prod.getValue(0);
  SDValue Hi = Prod.getValue(1);

  // Unsigned fixup: add b when a has bit 31 set, and vice versa.
  SDValue AMask = DAG.getNode(ISD::SRA, DL, MVT::i32, A, Shift31);
  SDValue BMask = DAG.getNode(ISD::SRA, DL, MVT::i32, B, Shift31);
  SDValue Fix1 = DAG.getNode(ISD::AND, DL, MVT::i32, AMask, B);
  SDValue Fix2 = DAG.getNode(ISD::AND, DL, MVT::i32, BMask, A);
  Hi = DAG.getNode(ISD::ADD, DL, MVT::i32, Hi, Fix1);
  Hi = DAG.getNode(ISD::ADD, DL, MVT::i32, Hi, Fix2);

  return DAG.getMergeValues({Lo, Hi}, DL);
}

SDValue VAXTargetLowering::LowerUDIV(SDValue Op, SelectionDAG &DAG) const {
  // VAX EDIV is signed — it interprets the divisor as signed i32.
  // Two cases for unsigned division:
  //   1. divisor >= 2^31 (MSB set, signed negative): quotient is 0 or 1
  //   2. divisor < 2^31 (positive): EDIV with zero-extended 64-bit dividend
  //      Works because divisor>0 makes the 64-bit dividend positive regardless
  //      of the 32-bit dividend's MSB. EDIV overflow stores truncated quotient
  //      on all known VAX implementations, which is the correct unsigned answer.
  // NOTE: We must NOT emit ISD::SDIV here — the DAGCombiner will convert
  // sdiv-by-positive-constant back to udiv, creating an infinite loop.
  SDLoc DL(Op);
  SDValue Dividend = Op.getOperand(0);
  SDValue Divisor = Op.getOperand(1);
  SDValue Zero = DAG.getConstant(0, DL, MVT::i32);

  // Case 1: divisor >= 2^31 → result is (dividend >=u divisor) ? 1 : 0
  SDValue One = DAG.getConstant(1, DL, MVT::i32);
  SDValue BigDivisorResult =
      DAG.getSelectCC(DL, Dividend, Divisor, One, Zero, ISD::SETUGE);

  // Case 2: divisor < 2^31 → EDIV({dividend, 0}, divisor)
  SDValue EdivResult = DAG.getNode(VAXISD::EDIV, DL,
                                   DAG.getVTList(MVT::i32, MVT::i32),
                                   Divisor, Dividend, Zero);

  // Select based on divisor MSB (signed < 0 means MSB set)
  return DAG.getSelectCC(DL, Divisor, Zero,
                         BigDivisorResult, EdivResult.getValue(0), ISD::SETLT);
}

SDValue VAXTargetLowering::LowerUREM(SDValue Op, SelectionDAG &DAG) const {
  // Unsigned remainder — same 2-case split as LowerUDIV.
  //   1. divisor >= 2^31: remainder is dividend (if < divisor) or dividend-divisor
  //   2. divisor < 2^31: EDIV with zero-extended dividend, take remainder result
  // NOTE: We must NOT emit ISD::SDIV here — see LowerUDIV comment.
  SDLoc DL(Op);
  SDValue Dividend = Op.getOperand(0);
  SDValue Divisor = Op.getOperand(1);
  SDValue Zero = DAG.getConstant(0, DL, MVT::i32);

  // Case 1: divisor >= 2^31 → remainder is dividend if dividend < divisor,
  // else dividend - divisor.
  SDValue Diff = DAG.getNode(ISD::SUB, DL, MVT::i32, Dividend, Divisor);
  SDValue BigDivisorResult =
      DAG.getSelectCC(DL, Dividend, Divisor, Diff, Dividend, ISD::SETUGE);

  // Case 2: divisor < 2^31 → EDIV remainder (second result)
  SDValue EdivResult = DAG.getNode(VAXISD::EDIV, DL,
                                   DAG.getVTList(MVT::i32, MVT::i32),
                                   Divisor, Dividend, Zero);

  // Select based on divisor MSB
  return DAG.getSelectCC(DL, Divisor, Zero,
                         BigDivisorResult, EdivResult.getValue(1), ISD::SETLT);
}

SDValue VAXTargetLowering::LowerSREM(SDValue Op, SelectionDAG &DAG) const {
  // srem(a, b) → EDIV(b, sign_extend(a)).remainder
  // Sign-extend dividend to 64-bit: hi = ashr(a, 31).
  SDLoc DL(Op);
  SDValue Dividend = Op.getOperand(0);
  SDValue Divisor = Op.getOperand(1);
  SDValue Hi = DAG.getNode(ISD::SRA, DL, MVT::i32, Dividend,
                           DAG.getConstant(31, DL, MVT::i32));
  SDValue EDIV = DAG.getNode(VAXISD::EDIV, DL,
                              DAG.getVTList(MVT::i32, MVT::i32),
                              Divisor, Dividend, Hi);
  return EDIV.getValue(1);
}

SDValue VAXTargetLowering::LowerGlobalAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  const GlobalAddressSDNode *GN = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = GN->getGlobal();
  SDLoc DL(GN);

  // In PIC mode, external/interposable symbols must be accessed through the
  // GOT so the dynamic linker can resolve them.  This produces R_VAX_GOT32
  // relocations.  DSO-local symbols can use plain PC-relative addressing.
  unsigned TF = VAXII::MO_NO_FLAG;
  if (isPositionIndependent() && !GV->isDSOLocal())
    TF = VAXII::MO_GOT;

  SDValue GA = DAG.getTargetGlobalAddress(GV, DL, MVT::i32, GN->getOffset(), TF);
  SDValue Addr = DAG.getNode(VAXISD::PCRelWrapper, DL, MVT::i32, GA);

  return Addr;
}

SDValue VAXTargetLowering::LowerBlockAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  const BlockAddressSDNode *BAN = cast<BlockAddressSDNode>(Op);
  const BlockAddress *BA = BAN->getBlockAddress();
  SDLoc DL(Op);

  SDValue Result = DAG.getTargetBlockAddress(BA, MVT::i32, BAN->getOffset());
  return DAG.getNode(VAXISD::PCRelWrapper, DL, MVT::i32, Result);
}

SDValue VAXTargetLowering::LowerGlobalTLSAddress(SDValue Op,
                                                  SelectionDAG &DAG) const {
  // VAX has no hardware TLS register — lower all TLS models to emulated TLS,
  // which calls __emutls_get_address() at runtime.
  return LowerToTLSEmulatedModel(cast<GlobalAddressSDNode>(Op), DAG);
}

SDValue VAXTargetLowering::LowerJumpTable(SDValue Op,
                                           SelectionDAG &DAG) const {
  JumpTableSDNode *JT = cast<JumpTableSDNode>(Op);
  SDLoc DL(JT);
  SDValue Table = DAG.getTargetJumpTable(JT->getIndex(), MVT::i32);
  return DAG.getNode(VAXISD::PCRelWrapper, DL, MVT::i32, Table);
}

SDValue VAXTargetLowering::LowerBR_JT(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDValue Table = Op.getOperand(1);
  SDValue Index = Op.getOperand(2);
  SDLoc DL(Op);

  JumpTableSDNode *JT = cast<JumpTableSDNode>(Table);
  unsigned JTI = JT->getIndex();

  // Get jump table size to determine the limit operand for CASEL.
  const MachineJumpTableInfo *MJTI =
      DAG.getMachineFunction().getOrCreateJumpTableInfo(
          MachineJumpTableInfo::EK_Inline);
  unsigned NumEntries = MJTI->getJumpTables()[JTI].MBBs.size();
  SDValue Limit = DAG.getConstant(NumEntries - 1, DL, MVT::i32);
  SDValue JTIVal = DAG.getTargetJumpTable(JTI, MVT::i32);

  return DAG.getNode(VAXISD::CASEL, DL, MVT::Other, Chain, Index, Limit,
                     JTIVal);
}

unsigned VAXTargetLowering::getJumpTableEncoding() const {
  return MachineJumpTableInfo::EK_Inline;
}

SDValue VAXTargetLowering::LowerConstantPool(SDValue Op,
                                              SelectionDAG &DAG) const {
  ConstantPoolSDNode *CP = cast<ConstantPoolSDNode>(Op);
  SDLoc DL(CP);
  SDValue Res;
  if (CP->isMachineConstantPoolEntry())
    Res = DAG.getTargetConstantPool(CP->getMachineCPVal(), MVT::i32,
                                    CP->getAlign(), CP->getOffset());
  else
    Res = DAG.getTargetConstantPool(CP->getConstVal(), MVT::i32,
                                    CP->getAlign(), CP->getOffset());
  return DAG.getNode(VAXISD::PCRelWrapper, DL, MVT::i32, Res);
}

SDValue VAXTargetLowering::LowerVASTART(SDValue Op,
                                         SelectionDAG &DAG) const {
  // va_start stores the address of the first variadic arg into the va_list ptr.
  // On VAX: AP + VarArgsOffset (AP+0 = argcount, AP+4 = first arg, ...).
  MachineFunction &MF = DAG.getMachineFunction();
  VAXMachineFunctionInfo *FuncInfo = MF.getInfo<VAXMachineFunctionInfo>();
  SDLoc DL(Op);

  SDValue AP = DAG.getRegister(VAX::AP, MVT::i32);
  SDValue Offset = DAG.getConstant(FuncInfo->getVarArgsOffset(), DL, MVT::i32);
  SDValue VAListAddr = Op.getOperand(1); // pointer to va_list
  SDValue Src = DAG.getNode(ISD::ADD, DL, MVT::i32, AP, Offset);
  return DAG.getStore(Op.getOperand(0), DL, Src, VAListAddr,
                      MachinePointerInfo());
}

SDValue VAXTargetLowering::LowerFRAMEADDR(SDValue Op,
                                            SelectionDAG &DAG) const {
  // VAX CALLS always sets FP. Frame depth > 0 requires chasing FP links.
  unsigned Depth = Op.getConstantOperandVal(0);
  SDLoc DL(Op);
  SDValue FrameAddr = DAG.getCopyFromReg(DAG.getEntryNode(), DL, VAX::FP,
                                          MVT::i32);
  // Chase saved FP chain for nested frames. Saved FP is at FP+12 in the
  // CALLS frame (handler[0], mask[4], AP[8], FP[12], PC[16]).
  for (unsigned i = 0; i < Depth; ++i)
    FrameAddr = DAG.getLoad(MVT::i32, DL, DAG.getEntryNode(),
                            DAG.getNode(ISD::ADD, DL, MVT::i32, FrameAddr,
                                        DAG.getConstant(12, DL, MVT::i32)),
                            MachinePointerInfo());
  return FrameAddr;
}

SDValue VAXTargetLowering::LowerRETURNADDR(SDValue Op,
                                             SelectionDAG &DAG) const {
  unsigned Depth = Op.getConstantOperandVal(0);
  SDLoc DL(Op);

  if (Depth > 0) {
    // For depth > 0: get the frame pointer for that depth, then load PC at +16.
    SDValue FrameAddr = LowerFRAMEADDR(
        DAG.getNode(ISD::FRAMEADDR, DL, MVT::i32,
                    DAG.getConstant(Depth - 1, DL, MVT::i32)),
        DAG);
    return DAG.getLoad(MVT::i32, DL, DAG.getEntryNode(),
                       DAG.getNode(ISD::ADD, DL, MVT::i32, FrameAddr,
                                   DAG.getConstant(16, DL, MVT::i32)),
                       MachinePointerInfo());
  }
  // Depth 0: return address is at FP+16 in the CALLS frame.
  SDValue FP = DAG.getCopyFromReg(DAG.getEntryNode(), DL, VAX::FP, MVT::i32);
  return DAG.getLoad(MVT::i32, DL, DAG.getEntryNode(),
                     DAG.getNode(ISD::ADD, DL, MVT::i32, FP,
                                 DAG.getConstant(16, DL, MVT::i32)),
                     MachinePointerInfo());
}

SDValue VAXTargetLowering::LowerEH_RETURN(SDValue Op,
                                            SelectionDAG &DAG) const {
  // EH_RETURN(chain, offset, handler)
  // Store the handler address at FP+16 (overwrite saved PC in CALLS frame),
  // then RET will "return" to the handler.
  SDValue Chain = Op.getOperand(0);
  SDValue Offset = Op.getOperand(1);
  SDValue Handler = Op.getOperand(2);
  SDLoc DL(Op);

  SDValue FP = DAG.getCopyFromReg(Chain, DL, VAX::FP, MVT::i32);
  SDValue RetAddr = DAG.getNode(ISD::ADD, DL, MVT::i32, FP,
                                DAG.getConstant(16, DL, MVT::i32));
  Chain = DAG.getStore(FP.getValue(1), DL, Handler, RetAddr,
                       MachinePointerInfo());
  return Chain;
}

Register
VAXTargetLowering::getExceptionPointerRegister(
    const Constant *PersonalityFn) const {
  // GCC VAX uses R2 (EH_RETURN_DATA_REGNO(0)).
  return VAX::R2;
}

Register
VAXTargetLowering::getExceptionSelectorRegister(
    const Constant *PersonalityFn) const {
  // GCC VAX uses R3 (EH_RETURN_DATA_REGNO(1)).
  return VAX::R3;
}

SDValue VAXTargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
    SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_VAX);

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps;
  RetOps.push_back(Chain);

  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "VAX: all return values must be in registers");

    if (VA.needsCustom()) {
      // i64 return: split into lo (R0) and hi (R1).
      assert(i + 1 < e && "i64 return needs two CCValAssigns");
      CCValAssign &HiVA = RVLocs[i + 1];
      SDValue Val = OutVals[VA.getValNo()];
      SDValue Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i32, Val,
                               DAG.getConstant(0, DL, MVT::i32));
      SDValue Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i32, Val,
                               DAG.getConstant(1, DL, MVT::i32));
      Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Lo, Flag);
      Flag = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(VA.getLocReg(), MVT::i32));
      Chain = DAG.getCopyToReg(Chain, DL, HiVA.getLocReg(), Hi, Flag);
      Flag = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(HiVA.getLocReg(), MVT::i32));
      ++i; // skip the hi half
      continue;
    }

    SDValue RetVal = OutVals[i];
    // Apply CC promotion: extend i8/i16 to i32 for register return.
    if (VA.getLocInfo() == CCValAssign::SExt)
      RetVal = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), RetVal);
    else if (VA.getLocInfo() == CCValAssign::ZExt)
      RetVal = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), RetVal);
    else if (VA.getLocInfo() == CCValAssign::AExt)
      RetVal = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), RetVal);

    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), RetVal, Flag);
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;
  if (Flag.getNode())
    RetOps.push_back(Flag);
  return DAG.getNode(VAXISD::RET_FLAG, DL, MVT::Other, RetOps);
}

SDValue VAXTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();

  // Mark AP as live-in: CALLS establishes AP pointing to the argument area.
  MF.getRegInfo().addLiveIn(VAX::AP);
  MF.front().addLiveIn(VAX::AP);

  SmallVector<CCValAssign, 8> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_VAX);

  MachineFrameInfo &MFI = MF.getFrameInfo();
  for (auto &VA : ArgLocs) {
    // AP+0 is the argument count word written by CALLS.
    // AP+4 is the first argument, AP+8 the second, etc.
    // Create a fixed stack object for each arg so the RA knows it can
    // rematerialize loads from the arg area instead of spilling to a new
    // stack slot. We use positive offsets to distinguish arg-area objects
    // (AP-relative) from locals (FP-relative, negative offsets).
    // eliminateFrameIndex resolves these to AP+offset.
    int APOffset = VA.getLocMemOffset() + 4;
    int FI = MFI.CreateFixedObject(VA.getLocVT().getSizeInBits() / 8,
                                   APOffset, /*IsImmutable=*/true);
    SDValue FIN = DAG.getFrameIndex(FI, MVT::i32);
    SDValue Load = DAG.getLoad(VA.getLocVT(), DL, Chain, FIN,
                               MachinePointerInfo::getFixedStack(MF, FI));

    // Handle CC promotion: if the arg was promoted (e.g., i8→i32),
    // truncate back to the expected value type.
    SDValue Val = Load;
    if (VA.getValVT() != VA.getLocVT()) {
      switch (VA.getLocInfo()) {
      case CCValAssign::SExt:
        Val = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), Val,
                          DAG.getValueType(VA.getValVT()));
        Val = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Val);
        break;
      case CCValAssign::ZExt:
        Val = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), Val,
                          DAG.getValueType(VA.getValVT()));
        Val = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Val);
        break;
      default:
        Val = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Val);
        break;
      }
    }
    InVals.push_back(Val);
  }

  // For variadic functions, record where the first variadic arg starts.
  // AP+0 = arg count, AP+4.. = fixed args. CCState tracks the total stack
  // bytes consumed by fixed args, accounting for their actual sizes (e.g.
  // doubles use 8 bytes, not 4).
  if (isVarArg) {
    VAXMachineFunctionInfo *FuncInfo =
        MF.getInfo<VAXMachineFunctionInfo>();
    FuncInfo->setVarArgsOffset(4 + CCInfo.getStackSize());
  }

  return Chain;
}

SDValue VAXTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG   = CLI.DAG;
  SDLoc         DL    = CLI.DL;
  SDValue       Chain = CLI.Chain;
  SDValue       Callee = CLI.Callee;
  MachineFunction &MF = DAG.getMachineFunction();
  bool isVarArg        = CLI.IsVarArg;

  // VAX CALLS/RET builds a frame linkage that prevents tail call optimization.
  CLI.IsTailCall = false;

  // Assign outgoing args: all go to stack via CC_VAX.
  SmallVector<CCValAssign, 8> ArgLocs;
  CCState CCInfo(CLI.CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(CLI.Outs, CC_VAX);
  unsigned NumArgs   = CLI.OutVals.size();
  unsigned StackBytes = CCInfo.getStackSize();

  // CALLSEQ_START with 0: PUSHLs will adjust SP incrementally.
  Chain = DAG.getCALLSEQ_START(Chain, 0, 0, DL);

  // Push args in reverse order (right-to-left) using PUSHL.
  // Each PUSHL decrements SP by 4 and stores the value.
  // f64 args need two PUSHLs (store to stack temp, load as two i32 halves).
  for (int i = NumArgs - 1; i >= 0; --i) {
    SDVTList VTs = DAG.getVTList(MVT::Other, MVT::Glue);
    SDValue Arg = CLI.OutVals[i];

    // Apply CC promotion: extend i8/i16 args to i32 before pushing.
    CCValAssign &VA = ArgLocs[i];
    if (VA.getLocInfo() == CCValAssign::SExt)
      Arg = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Arg);
    else if (VA.getLocInfo() == CCValAssign::ZExt)
      Arg = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Arg);
    else if (VA.getLocInfo() == CCValAssign::AExt)
      Arg = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Arg);

    if (Arg.getValueType() == MVT::f64) {
      // f64 (D_float): convert to VAX format and push as two i32 words.
      // For constants, convert IEEE→VAX D_float at compile time to avoid
      // the DAG optimizer folding store-load into IEEE integer immediates.
      if (auto *CFP = dyn_cast<ConstantFPSDNode>(Arg)) {
        uint64_t VaxBits = convertIEEEToVAXD(
            CFP->getValueAPF().bitcastToAPInt().getZExtValue());
        uint32_t Lo32 = VaxBits & 0xFFFFFFFF;
        uint32_t Hi32 = (VaxBits >> 32) & 0xFFFFFFFF;
        SDValue Hi = DAG.getConstant(Hi32, DL, MVT::i32);
        SDValue Lo = DAG.getConstant(Lo32, DL, MVT::i32);
        Chain = DAG.getNode(VAXISD::PUSHL, DL, VTs, Chain, Hi);
        Chain = DAG.getNode(VAXISD::PUSHL, DL, VTs, Chain, Lo);
      } else {
        // Non-constant: store to temp stack slot, load as two i32, push both.
        // The MOVD store writes VAX D_float bytes (hardware format), so the
        // i32 loads read back correct VAX-format words.
        SDValue StackSlot = DAG.CreateStackTemporary(MVT::f64);
        Chain = DAG.getStore(Chain, DL, Arg, StackSlot,
                             MachinePointerInfo());
        SDValue HiPtr = DAG.getNode(ISD::ADD, DL, MVT::i32, StackSlot,
                                    DAG.getConstant(4, DL, MVT::i32));
        SDValue Hi = DAG.getLoad(MVT::i32, DL, Chain, HiPtr,
                                 MachinePointerInfo());
        SDValue Lo = DAG.getLoad(MVT::i32, DL, Chain, StackSlot,
                                 MachinePointerInfo());
        Chain = DAG.getNode(VAXISD::PUSHL, DL, VTs, Hi.getValue(1), Hi);
        Chain = DAG.getNode(VAXISD::PUSHL, DL, VTs, Chain, Lo);
      }
    } else if (Arg.getValueType() == MVT::f32) {
      // f32 (F_float): convert to VAX format and push as i32.
      if (auto *CFP = dyn_cast<ConstantFPSDNode>(Arg)) {
        uint32_t VaxBits = convertIEEEToVAXF(
            CFP->getValueAPF().bitcastToAPInt().getZExtValue());
        SDValue AsInt = DAG.getConstant(VaxBits, DL, MVT::i32);
        Chain = DAG.getNode(VAXISD::PUSHL, DL, VTs, Chain, AsInt);
      } else {
        // Non-constant: store to temp, load as i32, push.
        SDValue StackSlot = DAG.CreateStackTemporary(MVT::f32);
        Chain = DAG.getStore(Chain, DL, Arg, StackSlot,
                             MachinePointerInfo());
        SDValue AsInt = DAG.getLoad(MVT::i32, DL, Chain, StackSlot,
                                    MachinePointerInfo());
        Chain = DAG.getNode(VAXISD::PUSHL, DL, VTs, AsInt.getValue(1), AsInt);
      }
    } else {
      Chain = DAG.getNode(VAXISD::PUSHL, DL, VTs, Chain, Arg);
    }
  }

  // Wrap callee for direct calls.
  // In PIC mode, external/interposable calls must go through the PLT so that
  // the dynamic linker can resolve them.  This produces R_VAX_PLT32
  // relocations (GAS achieves the same effect via its -k flag).
  bool IsPIC = isPositionIndependent();
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    const GlobalValue *GV = G->getGlobal();
    unsigned TF = VAXII::MO_NO_FLAG;
    if (IsPIC && !GV->isDSOLocal())
      TF = VAXII::MO_PLT;
    Callee = DAG.getTargetGlobalAddress(GV, DL, MVT::i32, 0, TF);
  } else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    unsigned TF = IsPIC ? VAXII::MO_PLT : VAXII::MO_NO_FLAG;
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i32, TF);
  }

  // Build VAXISD::CALL node.
  const uint32_t *Mask =
      MF.getSubtarget().getRegisterInfo()->getCallPreservedMask(MF, CLI.CallConv);
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 6> Ops = {
      Chain,
      DAG.getConstant(StackBytes / 4, DL, MVT::i32),
      Callee,
      DAG.getRegisterMask(Mask),
  };
  Chain = DAG.getNode(VAXISD::CALL, DL, NodeTys, Ops);
  SDValue InFlag = Chain.getValue(1);

  // VAX RET pops the arg area (CALLS S-bit), so callee pops all bytes.
  Chain = DAG.getCALLSEQ_END(Chain, StackBytes, StackBytes, InFlag, DL);
  InFlag = Chain.getValue(1);

  // Copy return value(s) from registers.
  SmallVector<CCValAssign, 4> RVLocs;
  CCState RetInfo(CLI.CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  RetInfo.AnalyzeCallResult(CLI.Ins, RetCC_VAX);
  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    CCValAssign &VA = RVLocs[i];

    if (VA.needsCustom()) {
      // i64 return: reassemble from R0 (lo) and R1 (hi).
      assert(i + 1 < e && "i64 return needs two CCValAssigns");
      CCValAssign &HiVA = RVLocs[i + 1];
      SDValue Lo = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), MVT::i32,
                                      InFlag);
      Chain  = Lo.getValue(1);
      InFlag = Lo.getValue(2);
      SDValue Hi = DAG.getCopyFromReg(Chain, DL, HiVA.getLocReg(), MVT::i32,
                                      InFlag);
      Chain  = Hi.getValue(1);
      InFlag = Hi.getValue(2);
      SDValue Val = DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64,
                                Lo.getValue(0), Hi.getValue(0));
      InVals.push_back(Val);
      ++i; // skip the hi half
      continue;
    }

    SDValue RV = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(),
                                    InFlag);
    Chain  = RV.getValue(1);
    InFlag = RV.getValue(2);

    // Handle CC promotion: if the return value was promoted (e.g., i8→i32),
    // truncate or assert back to the expected value type.
    SDValue Val = RV.getValue(0);
    if (VA.getValVT() != VA.getLocVT()) {
      switch (VA.getLocInfo()) {
      case CCValAssign::SExt:
        Val = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), Val,
                          DAG.getValueType(VA.getValVT()));
        Val = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Val);
        break;
      case CCValAssign::ZExt:
        Val = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), Val,
                          DAG.getValueType(VA.getValVT()));
        Val = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Val);
        break;
      default:
        Val = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Val);
        break;
      }
    }
    InVals.push_back(Val);
  }
  return Chain;
}

// PerformDAGCombine — target-specific DAG optimizations.
//
// Catches the i64 comparison expansion pattern produced by the type legalizer:
//   SELECT_CC(SELECT(SETCC_EQ, SETCC_LO, SETCC_HI), 0, T, F, SETNE)
// and replaces it with a single VAXISD::SELECT_CC_I64 node that expands to
// an efficient multi-block branch sequence in EmitInstrWithCustomInserter.
// This avoids materializing 3 intermediate booleans and a nested select.
SDValue VAXTargetLowering::PerformDAGCombine(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  if (N->getOpcode() != ISD::SELECT_CC)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  SDValue TrueVal = N->getOperand(2);
  SDValue FalseVal = N->getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(4))->get();

  // Only match the pattern: SELECT_CC(x, 0, T, F, SETNE) where x is either:
  //   (a) SELECT(SETCC_EQ, SETCC_LO, SETCC_HI) — pre-combine form, or
  //   (b) SELECT_CC(hi_a, hi_b, SETCC_LO, SETCC_HI, SETEQ) — combined form.
  // Both represent the standard i64 comparison expansion from type legalization.
  if (CC != ISD::SETNE || !isNullConstant(RHS))
    return SDValue();

  SDValue HiLHS, HiRHS, LoLHS, LoRHS;
  ISD::CondCode HiCC;

  if (LHS.getOpcode() == ISD::SELECT_CC) {
    // Combined form: SELECT_CC(hi_a, hi_b, setcc_lo, setcc_hi, SETEQ).
    ISD::CondCode InnerCC =
        cast<CondCodeSDNode>(LHS.getOperand(4))->get();
    if (InnerCC != ISD::SETEQ)
      return SDValue();

    SDValue InnerTrue = LHS.getOperand(2);   // SETCC(lo_a, lo_b, unsigned_cc)
    SDValue InnerFalse = LHS.getOperand(3);  // SETCC(hi_a, hi_b, signed_cc)
    if (InnerTrue.getOpcode() != ISD::SETCC ||
        InnerFalse.getOpcode() != ISD::SETCC)
      return SDValue();

    HiLHS = LHS.getOperand(0);
    HiRHS = LHS.getOperand(1);
    LoLHS = InnerTrue.getOperand(0);
    LoRHS = InnerTrue.getOperand(1);
    HiCC = cast<CondCodeSDNode>(InnerFalse.getOperand(2))->get();

    // Verify the hi-comparison setcc uses the same hi operands.
    if (InnerFalse.getOperand(0) != HiLHS ||
        InnerFalse.getOperand(1) != HiRHS)
      return SDValue();
  } else if (LHS.getOpcode() == ISD::SELECT) {
    // Pre-combine form: SELECT(SETCC_EQ, SETCC_LO, SETCC_HI).
    SDValue SelCond = LHS.getOperand(0);
    SDValue SelTrue = LHS.getOperand(1);
    SDValue SelFalse = LHS.getOperand(2);

    if (SelCond.getOpcode() != ISD::SETCC ||
        SelTrue.getOpcode() != ISD::SETCC ||
        SelFalse.getOpcode() != ISD::SETCC)
      return SDValue();

    ISD::CondCode EqCC =
        cast<CondCodeSDNode>(SelCond.getOperand(2))->get();
    if (EqCC != ISD::SETEQ)
      return SDValue();

    HiLHS = SelCond.getOperand(0);
    HiRHS = SelCond.getOperand(1);
    LoLHS = SelTrue.getOperand(0);
    LoRHS = SelTrue.getOperand(1);
    HiCC = cast<CondCodeSDNode>(SelFalse.getOperand(2))->get();

    if (SelFalse.getOperand(0) != HiLHS ||
        SelFalse.getOperand(1) != HiRHS)
      return SDValue();
  } else {
    return SDValue();
  }

  // Map the original i64 condition code to an encoding for the pseudo.
  unsigned I64CC;
  switch (HiCC) {
  case ISD::SETLT:  I64CC = 0; break;
  case ISD::SETLE:  I64CC = 1; break;
  case ISD::SETGT:  I64CC = 2; break;
  case ISD::SETGE:  I64CC = 3; break;
  case ISD::SETULT: I64CC = 4; break;
  case ISD::SETULE: I64CC = 5; break;
  case ISD::SETUGT: I64CC = 6; break;
  case ISD::SETUGE: I64CC = 7; break;
  default: return SDValue(); // EQ/NE use XOR+OR path, not this pattern.
  }

  SDLoc DL(N);
  return DAG.getNode(VAXISD::SELECT_CC_I64, DL, N->getValueType(0),
                     {HiLHS, HiRHS, LoLHS, LoRHS, TrueVal, FalseVal,
                      DAG.getConstant(I64CC, DL, MVT::i32)});
}

// Expand SELECT_CC_Pseudo into a branch diamond:
//   ThisMBB:
//     (CMP already set flags)
//     bXX  SinkMBB          (branch on TRUE condition to sink)
//   FalseMBB:
//     (fallthrough: false value)
//   SinkMBB:
//     %dst = PHI(%truev, ThisMBB, %falsev, FalseMBB)
MachineBasicBlock *
VAXTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                MachineBasicBlock *BB) const {
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  if (MI.getOpcode() == VAX::SELECT_CC_I64_Pseudo)
    return EmitSELECT_CC_I64(MI, BB);

  assert((MI.getOpcode() == VAX::SELECT_CC_Pseudo ||
          MI.getOpcode() == VAX::SELECT_CC_B_Pseudo ||
          MI.getOpcode() == VAX::SELECT_CC_W_Pseudo ||
          MI.getOpcode() == VAX::SELECT_CC_F_Pseudo ||
          MI.getOpcode() == VAX::SELECT_CC_D_Pseudo) &&
         "Unexpected custom inserter opcode");

  Register DstReg = MI.getOperand(0).getReg();
  Register TrueReg = MI.getOperand(1).getReg();
  Register FalseReg = MI.getOperand(2).getReg();
  unsigned VAXCC = MI.getOperand(3).getImm();

  // Map VAXCC integer to the branch opcode.
  static const unsigned BrOpcodes[] = {
    VAX::BEQL, VAX::BNEQ, VAX::BGTR, VAX::BGEQ,
    VAX::BLSS, VAX::BLEQ, VAX::BGTRU, VAX::BGEQU,
    VAX::BLSSU, VAX::BLEQU
  };
  assert(VAXCC < std::size(BrOpcodes) && "Invalid VAXCC");
  unsigned BrOpc = BrOpcodes[VAXCC];

  MachineFunction *MF = BB->getParent();
  const BasicBlock *LLVMBB = BB->getBasicBlock();
  MachineFunction::iterator I = ++BB->getIterator();

  MachineBasicBlock *FalseMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *SinkMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MF->insert(I, FalseMBB);
  MF->insert(I, SinkMBB);

  SinkMBB->splice(SinkMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(BB);

  BB->addSuccessor(FalseMBB);
  BB->addSuccessor(SinkMBB);
  BuildMI(BB, DL, TII.get(BrOpc)).addMBB(SinkMBB);

  FalseMBB->addSuccessor(SinkMBB);

  BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(TargetOpcode::PHI), DstReg)
      .addReg(TrueReg)
      .addMBB(BB)
      .addReg(FalseReg)
      .addMBB(FalseMBB);

  MI.eraseFromParent();
  return SinkMBB;
}

// Expand SELECT_CC_I64_Pseudo into an efficient multi-block branch sequence
// for i64 ordered comparisons.  Avoids materializing intermediate booleans:
//
//   BB0:
//     CMPL hi_lhs, hi_rhs
//     BNEQ HiDecideMBB          ; hi words differ — skip lo compare
//   LoCmpMBB:                   ; hi words equal — compare lo (unsigned)
//     CMPL lo_lhs, lo_rhs
//     Bcc_lo TrueMBB
//   FalseMBB:                   ; fallthrough = false
//     BRW SinkMBB
//   HiDecideMBB:
//     CMPL hi_lhs, hi_rhs       ; redundant CMP (safe: avoids cross-MBB flags)
//     Bcc_hi TrueMBB
//     BRW FalseMBB
//   TrueMBB:
//   SinkMBB:
//     PHI dst = [TrueReg:TrueMBB, FalseReg:FalseMBB]
MachineBasicBlock *
VAXTargetLowering::EmitSELECT_CC_I64(MachineInstr &MI,
                                       MachineBasicBlock *BB) const {
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  Register DstReg  = MI.getOperand(0).getReg();
  Register HiLHS   = MI.getOperand(1).getReg();
  Register HiRHS   = MI.getOperand(2).getReg();
  Register LoLHS   = MI.getOperand(3).getReg();
  Register LoRHS   = MI.getOperand(4).getReg();
  Register TrueReg = MI.getOperand(5).getReg();
  Register FalseReg = MI.getOperand(6).getReg();
  unsigned I64CC   = MI.getOperand(7).getImm();

  // Decode I64CC to branch opcodes:
  //   HiBrTrue:  branch taken when hi comparison strictly satisfies CC
  //   LoBrTrue:  branch taken when lo comparison satisfies CC (unsigned)
  // The "hi strictly false" case is handled by BNEQ falling through from BB0
  // to LoCmpMBB (equal) or jumping to HiDecideMBB (not equal), then a single
  // conditional branch in HiDecideMBB.
  unsigned HiBrTrue, LoBrTrue;
  switch (I64CC) {
  case 0: HiBrTrue = VAX::BLSS;  LoBrTrue = VAX::BLSSU; break; // SETLT
  case 1: HiBrTrue = VAX::BLSS;  LoBrTrue = VAX::BLEQU; break; // SETLE
  case 2: HiBrTrue = VAX::BGTR;  LoBrTrue = VAX::BGTRU; break; // SETGT
  case 3: HiBrTrue = VAX::BGTR;  LoBrTrue = VAX::BGEQU; break; // SETGE
  case 4: HiBrTrue = VAX::BLSSU; LoBrTrue = VAX::BLSSU; break; // SETULT
  case 5: HiBrTrue = VAX::BLSSU; LoBrTrue = VAX::BLEQU; break; // SETULE
  case 6: HiBrTrue = VAX::BGTRU; LoBrTrue = VAX::BGTRU; break; // SETUGT
  case 7: HiBrTrue = VAX::BGTRU; LoBrTrue = VAX::BGEQU; break; // SETUGE
  default: llvm_unreachable("Invalid I64CC");
  }

  MachineFunction *MF = BB->getParent();
  const BasicBlock *LLVMBB = BB->getBasicBlock();
  MachineFunction::iterator InsertPt = ++BB->getIterator();

  // Create blocks. Layout: BB0, LoCmpMBB, FalseMBB, SinkMBB, HiDecideMBB,
  // TrueMBB.  This puts the common (hi-equal) path as fallthrough.
  auto *LoCmpMBB    = MF->CreateMachineBasicBlock(LLVMBB);
  auto *FalseMBB    = MF->CreateMachineBasicBlock(LLVMBB);
  auto *SinkMBB     = MF->CreateMachineBasicBlock(LLVMBB);
  auto *HiDecideMBB = MF->CreateMachineBasicBlock(LLVMBB);
  auto *TrueMBB     = MF->CreateMachineBasicBlock(LLVMBB);
  MF->insert(InsertPt, LoCmpMBB);
  MF->insert(InsertPt, FalseMBB);
  MF->insert(InsertPt, SinkMBB);
  MF->insert(InsertPt, HiDecideMBB);
  MF->insert(InsertPt, TrueMBB);

  // Move the rest of the original block (after the pseudo) into SinkMBB.
  SinkMBB->splice(SinkMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(BB);

  // BB0: CMPL hi + BNEQ HiDecideMBB, fallthrough to LoCmpMBB.
  BB->addSuccessor(HiDecideMBB);
  BB->addSuccessor(LoCmpMBB);
  BuildMI(BB, DL, TII.get(VAX::CMPL_rr)).addReg(HiLHS).addReg(HiRHS);
  BuildMI(BB, DL, TII.get(VAX::BNEQ)).addMBB(HiDecideMBB);

  // LoCmpMBB: CMPL lo + Bcc_lo TrueMBB, fallthrough to FalseMBB.
  LoCmpMBB->addSuccessor(TrueMBB);
  LoCmpMBB->addSuccessor(FalseMBB);
  BuildMI(LoCmpMBB, DL, TII.get(VAX::CMPL_rr)).addReg(LoLHS).addReg(LoRHS);
  BuildMI(LoCmpMBB, DL, TII.get(LoBrTrue)).addMBB(TrueMBB);

  // FalseMBB: jump to SinkMBB (not adjacent in layout).
  FalseMBB->addSuccessor(SinkMBB);
  BuildMI(FalseMBB, DL, TII.get(VAX::BRW)).addMBB(SinkMBB);

  // HiDecideMBB: redundant CMPL hi + Bcc_hi TrueMBB, else jump to FalseMBB.
  // The redundant CMPL avoids relying on PSW surviving across MBB boundaries
  // (register allocator can insert PSW-clobbering copies at block boundaries).
  HiDecideMBB->addSuccessor(TrueMBB);
  HiDecideMBB->addSuccessor(FalseMBB);
  BuildMI(HiDecideMBB, DL, TII.get(VAX::CMPL_rr)).addReg(HiLHS).addReg(HiRHS);
  BuildMI(HiDecideMBB, DL, TII.get(HiBrTrue)).addMBB(TrueMBB);
  BuildMI(HiDecideMBB, DL, TII.get(VAX::BRW)).addMBB(FalseMBB);

  // TrueMBB: jump to SinkMBB.
  TrueMBB->addSuccessor(SinkMBB);
  BuildMI(TrueMBB, DL, TII.get(VAX::BRW)).addMBB(SinkMBB);

  // SinkMBB: PHI to merge true/false values.
  BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(TargetOpcode::PHI), DstReg)
      .addReg(TrueReg)
      .addMBB(TrueMBB)
      .addReg(FalseReg)
      .addMBB(FalseMBB);

  MI.eraseFromParent();
  return SinkMBB;
}

std::pair<unsigned, const TargetRegisterClass *>
VAXTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                StringRef Constraint,
                                                MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      return std::make_pair(0U, &VAX::GPRRegClass);
    default:
      break;
    }
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

TargetLoweringBase::AtomicExpansionKind
VAXTargetLowering::shouldExpandAtomicRMWInIR(const AtomicRMWInst *AI) const {
  // VAX is single-core with strict memory ordering. Atomic RMW can be
  // lowered to non-atomic load/op/store (no concurrent writers).
  return AtomicExpansionKind::NotAtomic;
}

TargetLoweringBase::AtomicExpansionKind
VAXTargetLowering::shouldExpandAtomicCmpXchgInIR(const AtomicCmpXchgInst *AI) const {
  // VAX is single-core — CAS expands to a simple compare-and-store.
  return AtomicExpansionKind::NotAtomic;
}

TargetLoweringBase::AtomicExpansionKind
VAXTargetLowering::shouldExpandAtomicLoadInIR(LoadInst *LI) const {
  // VAX is single-core with strict memory ordering.  All loads are atomic.
  // Expand to non-atomic to avoid ISel issues with sub-32-bit atomic loads.
  return AtomicExpansionKind::NotAtomic;
}

TargetLoweringBase::AtomicExpansionKind
VAXTargetLowering::shouldExpandAtomicStoreInIR(StoreInst *SI) const {
  return AtomicExpansionKind::NotAtomic;
}

bool VAXTargetLowering::allowsMisalignedMemoryAccesses(
    EVT VT, unsigned AddrSpace, Align Alignment,
    MachineMemOperand::Flags Flags, unsigned *Fast) const {
  // VAX handles all unaligned memory accesses in hardware with no penalty.
  if (Fast)
    *Fast = 1;
  return true;
}
