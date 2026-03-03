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

#define GET_CALLINGCONV_IMPL
#include "VAXGenCallingConv.inc"

VAXTargetLowering::VAXTargetLowering(const VAXTargetMachine &TM,
                                     const VAXSubtarget &STI)
    : TargetLowering(TM, STI) {
  // Register classes by value type.
  addRegisterClass(MVT::i32, &VAX::GPRnoPCRegClass);
  addRegisterClass(MVT::f32, &VAX::GPRIRegClass);
  addRegisterClass(MVT::f64, &VAX::QPRRegClass);

  // Finalize register class / type legalization info.
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(VAX::SP);
  setSchedulingPreference(Sched::RegPressure);

  // Global addresses are lowered to PC-relative wrappers.
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i32, Custom);
  setOperationAction(ISD::ConstantPool,  MVT::i32, Custom);

  // Jump table addresses are lowered to PC-relative wrappers.
  setOperationAction(ISD::JumpTable, MVT::i32, Custom);

  // AND is lowered to BICL (bit-clear) since VAX has no direct AND instruction.
  setOperationAction(ISD::AND, MVT::i32, Custom);

  // Branches: lower ISD::BR_CC to VAXISD::CMP + VAXISD::BRCC.
  // Expanding BRCOND causes the DAG builder to produce BR_CC directly.
  setOperationAction(ISD::BR_CC,   MVT::i32,   Custom);
  setOperationAction(ISD::BRCOND,  MVT::Other,  Expand);

  // Switch/jump tables: custom-lower BR_JT to CASEL instruction.
  setOperationAction(ISD::BR_JT, MVT::Other, Custom);

  // Variadic function support.
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG,   MVT::Other, Expand);
  setOperationAction(ISD::VACOPY,  MVT::Other, Expand);
  setOperationAction(ISD::VAEND,   MVT::Other, Expand);

  // Conditional value selection: SELECT_CC is custom (needed by i64 expansion),
  // SELECT expands to SELECT_CC.
  setOperationAction(ISD::SELECT,    MVT::i32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Custom);

  // Extend SELECT_CC_Pseudo to also handle f32 results.
  setOperationAction(ISD::SELECT_CC, MVT::f32, Custom);

  // VAX DIVL is signed only; use EDIV for unsigned div/rem and signed rem.
  setOperationAction(ISD::UDIV, MVT::i32, Custom);
  setOperationAction(ISD::UREM, MVT::i32, Custom);
  setOperationAction(ISD::SREM, MVT::i32, Custom);

  // i64 mul: EMUL (32×32→64) handles SMUL_LOHI directly.
  setOperationAction(ISD::SMUL_LOHI, MVT::i32, Custom);
  // Remaining i64 mul/div expansions still use libcalls.
  setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::MULHU, MVT::i32, Expand);
  setOperationAction(ISD::MULHS, MVT::i32, Expand);

  // Carry-chained add/sub for i64 support: ADDL3 sets PSW.C, ADWC uses it.
  setOperationAction(ISD::ADDC, MVT::i32, Legal);
  setOperationAction(ISD::ADDE, MVT::i32, Legal);
  setOperationAction(ISD::SUBC, MVT::i32, Legal);
  setOperationAction(ISD::SUBE, MVT::i32, Legal);

  // Shifts: SHL is handled directly by ASHL. SRA and SRL need custom lowering
  // because VAX ASHL uses negative count for right shift (arithmetic), and
  // logical right shift has no dedicated instruction.
  setOperationAction(ISD::SRA, MVT::i32, Custom);
  setOperationAction(ISD::SRL, MVT::i32, Custom);

  // Frame intrinsics for exception handling and debugging.
  setOperationAction(ISD::FRAMEADDR,  MVT::i32, Custom);
  setOperationAction(ISD::RETURNADDR, MVT::i32, Custom);
  setOperationAction(ISD::EH_RETURN,  MVT::Other, Custom);

  // Extending loads: all byte/word variants now legal via CVT/MOVZ instructions.
  // i8 zero-extend: MOVZBL (Phase 5); i8 sign-extend: CVTBL (Phase 7).
  // i16 zero-extend: MOVZWL (Phase 7); i16 sign-extend: CVTWL (Phase 7).
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, MVT::i8,  Legal); // MOVZBL
  setLoadExtAction(ISD::EXTLOAD,  MVT::i32, MVT::i8,  Legal); // MOVZBL (anyext)
  setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i8,  Legal); // CVTBL
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, MVT::i16, Legal); // MOVZWL
  setLoadExtAction(ISD::EXTLOAD,  MVT::i32, MVT::i16, Legal); // MOVZWL (anyext)
  setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i16, Legal); // CVTWL
  // i1 loads: C _Bool is i1 in LLVM IR. Promote to byte load (MOVZBL).
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, MVT::i1, Promote);
  setLoadExtAction(ISD::EXTLOAD,  MVT::i32, MVT::i1, Promote);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i1, Promote);

  // SIGN_EXTEND_INREG i1: no VAX instruction; expand to shift pair.
  // i8 and i16 are handled by CVTBL/CVTWL patterns in TableGen.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  // Truncating stores: MOVB (i8) and MOVW (i16).
  setTruncStoreAction(MVT::i32, MVT::i8,  Legal);
  setTruncStoreAction(MVT::i32, MVT::i16, Legal);

  // Truncating f64→f32 store: custom-lower to CVTDF + MOVF.
  setTruncStoreAction(MVT::f64, MVT::f32, Custom);

  // Extending f32→f64 load: expand to MOVF + CVTFD.
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);

  // Scalar integer types are all legal at i32; narrower types will be
  // promoted/expanded in later phases as instructions are added.

  // Integer SETCC must be expanded — VAX sets condition codes but has no
  // instruction that directly produces a 0/1 result from a comparison.
  setOperationAction(ISD::SETCC,      MVT::i32, Expand);

  // UDIVREM/SDIVREM pairs: expand (individual udiv/urem/srem are Custom above).
  setOperationAction(ISD::UDIVREM,    MVT::i32, Expand);
  setOperationAction(ISD::SDIVREM,    MVT::i32, Expand);

  // Bit manipulation: VAX has no native CLZ, CTZ, bswap, or popcount.
  // CTZ could use FFS in the future; for now expand all to libcalls.
  setOperationAction(ISD::CTLZ,       MVT::i32, Expand);
  setOperationAction(ISD::CTTZ,       MVT::i32, Expand);
  setOperationAction(ISD::CTPOP,      MVT::i32, Expand);
  setOperationAction(ISD::BSWAP,      MVT::i32, Expand);
  setOperationAction(ISD::CTLZ_ZERO_UNDEF, MVT::i32, Expand);
  setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::i32, Expand);
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
  setOperationAction(ISD::UINT_TO_FP, MVT::i32, Expand);
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
  default:                   return nullptr;
  }
}

SDValue VAXTargetLowering::LowerOperation(SDValue Op,
                                           SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress: return LowerGlobalAddress(Op, DAG);
  case ISD::GlobalTLSAddress: return LowerGlobalTLSAddress(Op, DAG);
  case ISD::ConstantPool:  return LowerConstantPool(Op, DAG);
  case ISD::JumpTable:     return LowerJumpTable(Op, DAG);
  case ISD::AND:           return LowerAND(Op, DAG);
  case ISD::SRA:           return LowerSRA(Op, DAG);
  case ISD::SRL:           return LowerSRL(Op, DAG);
  case ISD::SHL_PARTS:     return LowerSHL_PARTS(Op, DAG);
  case ISD::SRA_PARTS:     return LowerSRA_PARTS(Op, DAG);
  case ISD::SMUL_LOHI:     return LowerSMUL_LOHI(Op, DAG);
  case ISD::UDIV:          return LowerUDIV(Op, DAG);
  case ISD::UREM:          return LowerUREM(Op, DAG);
  case ISD::SREM:          return LowerSREM(Op, DAG);
  case ISD::BR_CC:         return LowerBR_CC(Op, DAG);
  case ISD::BR_JT:         return LowerBR_JT(Op, DAG);
  case ISD::SELECT_CC:     return LowerSELECT_CC(Op, DAG);
  case ISD::VASTART:       return LowerVASTART(Op, DAG);
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

SDValue VAXTargetLowering::LowerSRA(SDValue Op, SelectionDAG &DAG) const {
  // VAX ASHL with negative count does arithmetic right shift.
  // Lower sra(x, n) → VAXISD::ASHL(-n, x).
  SDLoc DL(Op);
  SDValue Src = Op.getOperand(0);
  SDValue Cnt = Op.getOperand(1);

  if (auto *CN = dyn_cast<ConstantSDNode>(Cnt)) {
    // Constant shift: negate at compile time.
    int64_t NegAmt = -CN->getSExtValue();
    return DAG.getNode(VAXISD::ASHL, DL, MVT::i32,
                       DAG.getSignedConstant(NegAmt, DL, MVT::i32), Src);
  }
  // Variable shift: emit MNEGL + ASHL.
  SDValue NegCnt = DAG.getNode(ISD::SUB, DL, MVT::i32,
                               DAG.getConstant(0, DL, MVT::i32), Cnt);
  return DAG.getNode(VAXISD::ASHL, DL, MVT::i32, NegCnt, Src);
}

SDValue VAXTargetLowering::LowerSRL(SDValue Op, SelectionDAG &DAG) const {
  // VAX has no logical right shift. Use: srl(x, n) = rotl(x, 32-n) & mask.
  // For constant n, mask = (1 << (32-n)) - 1 = ~0u >> n.
  // For variable n, compute mask dynamically via ASHL.
  SDLoc DL(Op);
  SDValue Src = Op.getOperand(0);
  SDValue Cnt = Op.getOperand(1);

  if (auto *CN = dyn_cast<ConstantSDNode>(Cnt)) {
    unsigned N = CN->getZExtValue() & 31;
    if (N == 0) return Src;
    // rotl(src, 32-N) then AND with mask
    SDValue Rot = DAG.getNode(ISD::ROTL, DL, MVT::i32, Src,
                              DAG.getConstant(32 - N, DL, MVT::i32));
    uint32_t Mask = 0xFFFFFFFFu >> N;
    return DAG.getNode(ISD::AND, DL, MVT::i32, Rot,
                       DAG.getConstant(Mask, DL, MVT::i32));
  }
  // Variable logical right shift: rotl(src, 32-cnt) & ((1 << (32-cnt)) - 1)
  // mask = ASHL(1, 32-cnt) - 1 = ~0 srl cnt, but that's circular.
  // Alternative: ASHL(-cnt, src) gives arithmetic right shift, then
  // clear sign-extended bits: srl(x, n) = ashl(-n, x) & ((1u << (32-n)) - 1).
  // Mask = ~((-1) << (32 - n)) = ~ashl(32-n, -1).
  // Emit: neg_cnt = -cnt; shifted = ASHL(neg_cnt, src); 
  //       mask_bits = ASHL(neg_cnt, -1); mask = NOT(mask_bits);
  //       result = AND(shifted, mask)
  SDValue NegCnt = DAG.getNode(ISD::SUB, DL, MVT::i32,
                               DAG.getConstant(0, DL, MVT::i32), Cnt);
  SDValue Shifted = DAG.getNode(VAXISD::ASHL, DL, MVT::i32, NegCnt, Src);
  SDValue SignBits = DAG.getNode(VAXISD::ASHL, DL, MVT::i32, NegCnt,
                                 DAG.getAllOnesConstant(DL, MVT::i32));
  SDValue Mask = DAG.getNOT(DL, SignBits, MVT::i32);
  return DAG.getNode(ISD::AND, DL, MVT::i32, Shifted, Mask);
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

SDValue VAXTargetLowering::LowerUDIV(SDValue Op, SelectionDAG &DAG) const {
  // udiv(a, b) → EDIV(b, a, 0).quotient
  // Zero-extend dividend to 64-bit quadword for unsigned division.
  SDLoc DL(Op);
  SDValue Dividend = Op.getOperand(0);
  SDValue Divisor = Op.getOperand(1);
  SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
  SDValue EDIV = DAG.getNode(VAXISD::EDIV, DL,
                              DAG.getVTList(MVT::i32, MVT::i32),
                              Divisor, Dividend, Zero);
  return EDIV.getValue(0);
}

SDValue VAXTargetLowering::LowerUREM(SDValue Op, SelectionDAG &DAG) const {
  // urem(a, b) → EDIV(b, a, 0).remainder
  SDLoc DL(Op);
  SDValue Dividend = Op.getOperand(0);
  SDValue Divisor = Op.getOperand(1);
  SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
  SDValue EDIV = DAG.getNode(VAXISD::EDIV, DL,
                              DAG.getVTList(MVT::i32, MVT::i32),
                              Divisor, Dividend, Zero);
  return EDIV.getValue(1);
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

  unsigned TF = VAXII::MO_NO_FLAG;
  if (isPositionIndependent() && !GV->isDSOLocal())
    TF = VAXII::MO_GOT;

  SDValue GA = DAG.getTargetGlobalAddress(GV, DL, MVT::i32, GN->getOffset(), TF);
  SDValue Addr = DAG.getNode(VAXISD::PCRelWrapper, DL, MVT::i32, GA);

  // GOT references are indirect: the PC-relative displacement points to a
  // GOT entry containing the symbol's actual address. Load through it.
  if (TF == VAXII::MO_GOT)
    Addr = DAG.getLoad(MVT::i32, DL, DAG.getEntryNode(), Addr,
                       MachinePointerInfo::getGOT(DAG.getMachineFunction()));

  return Addr;
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
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVals[i], Flag);
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

  SDValue AP = DAG.getRegister(VAX::AP, MVT::i32);
  for (auto &VA : ArgLocs) {
    // AP+0 is the argument count word written by CALLS.
    // AP+4 is the first argument, AP+8 the second, etc.
    SDValue Off = DAG.getConstant(VA.getLocMemOffset() + 4, DL, MVT::i32);
    SDValue Ptr = DAG.getNode(ISD::ADD, DL, MVT::i32, AP, Off);
    SDValue Load = DAG.getLoad(VA.getLocVT(), DL, Chain, Ptr,
                               MachinePointerInfo());
    InVals.push_back(Load);
  }

  // For variadic functions, record where the first variadic arg starts.
  // AP+0 = arg count, AP+4 = first arg. Fixed args occupy ArgLocs.size() slots.
  if (isVarArg) {
    VAXMachineFunctionInfo *FuncInfo =
        MF.getInfo<VAXMachineFunctionInfo>();
    FuncInfo->setVarArgsOffset(4 + ArgLocs.size() * 4);
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
  unsigned NumArgs   = ArgLocs.size();
  unsigned StackBytes = CCInfo.getStackSize();

  // CALLSEQ_START with 0: PUSHLs will adjust SP incrementally.
  Chain = DAG.getCALLSEQ_START(Chain, 0, 0, DL);

  // Push args in reverse order (right-to-left) using PUSHL.
  // Each PUSHL decrements SP by 4 and stores the value.
  // f64 args need two PUSHLs (store to stack temp, load as two i32 halves).
  for (int i = NumArgs - 1; i >= 0; --i) {
    SDVTList VTs = DAG.getVTList(MVT::Other, MVT::Glue);
    SDValue Arg = CLI.OutVals[i];
    if (Arg.getValueType() == MVT::f64) {
      // f64 (D_float): store to temp stack slot, load as two i32, push both.
      SDValue StackSlot = DAG.CreateStackTemporary(MVT::f64);
      Chain = DAG.getStore(Chain, DL, Arg, StackSlot,
                           MachinePointerInfo());
      SDValue HiPtr = DAG.getNode(ISD::ADD, DL, MVT::i32, StackSlot,
                                  DAG.getConstant(4, DL, MVT::i32));
      SDValue Hi = DAG.getLoad(MVT::i32, DL, Chain, HiPtr,
                               MachinePointerInfo());
      SDValue Lo = DAG.getLoad(MVT::i32, DL, Chain, StackSlot,
                               MachinePointerInfo());
      // Push high word first (higher address on stack), then low word.
      Chain = DAG.getNode(VAXISD::PUSHL, DL, VTs, Hi.getValue(1), Hi);
      Chain = DAG.getNode(VAXISD::PUSHL, DL, VTs, Chain, Lo);
    } else if (Arg.getValueType() == MVT::f32) {
      // f32 (F_float): bitcast to i32 via stack temp, then push.
      SDValue StackSlot = DAG.CreateStackTemporary(MVT::f32);
      Chain = DAG.getStore(Chain, DL, Arg, StackSlot,
                           MachinePointerInfo());
      SDValue AsInt = DAG.getLoad(MVT::i32, DL, Chain, StackSlot,
                                  MachinePointerInfo());
      Chain = DAG.getNode(VAXISD::PUSHL, DL, VTs, AsInt.getValue(1), AsInt);
    } else {
      Chain = DAG.getNode(VAXISD::PUSHL, DL, VTs, Chain, Arg);
    }
  }

  // Wrap callee for direct calls.
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    unsigned TF = VAXII::MO_NO_FLAG;
    if (isPositionIndependent() && !G->getGlobal()->isDSOLocal())
      TF = VAXII::MO_PLT;
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i32, 0, TF);
  } else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    unsigned TF = isPositionIndependent() ? VAXII::MO_PLT : VAXII::MO_NO_FLAG;
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i32, TF);
  }

  // Build VAXISD::CALL node.
  const uint32_t *Mask =
      MF.getSubtarget().getRegisterInfo()->getCallPreservedMask(MF, CLI.CallConv);
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 6> Ops = {
      Chain,
      DAG.getConstant(NumArgs, DL, MVT::i32),
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
  for (auto &VA : RVLocs) {
    SDValue RV = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(),
                                    InFlag);
    Chain  = RV.getValue(1);
    InFlag = RV.getValue(2);
    InVals.push_back(RV.getValue(0));
  }
  return Chain;
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
  assert((MI.getOpcode() == VAX::SELECT_CC_Pseudo ||
          MI.getOpcode() == VAX::SELECT_CC_F_Pseudo ||
          MI.getOpcode() == VAX::SELECT_CC_D_Pseudo) &&
         "Unexpected custom inserter opcode");

  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

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
