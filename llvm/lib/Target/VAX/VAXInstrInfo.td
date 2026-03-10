//===-- VAXInstrInfo.td - VAX Instruction Definitions ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "VAXInstrFormats.td"

// Instruction definitions are added incrementally by phase.

// Pseudo-instruction base class.
class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstVAX<0, outs, ins, asmstr, pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

//===----------------------------------------------------------------------===//
// Custom DAG node declarations
//===----------------------------------------------------------------------===//

// Return from a CALLS/CALLG-protocol function.  Variadic: [chain, (reg...), glue?]
def SDT_VAXRet : SDTypeProfile<0, -1, []>;
def VAXISD_ret_flag : SDNode<"VAXISD::RET_FLAG", SDT_VAXRet,
    [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

// PC-relative wrapper for global/extern symbols (produces the symbol address).
def SDT_VAXPCRel : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisInt<0>]>;
def vax_pcrelwrapper : SDNode<"VAXISD::PCRelWrapper", SDT_VAXPCRel>;

//===----------------------------------------------------------------------===//
// Memory operand: base register + signed displacement
//   Printed as: disp(base), or (base) when disp is 0.
//===----------------------------------------------------------------------===//

def VAXMemAsmOperand : AsmOperandClass {
  let Name = "Mem";
  let ParserMethod = "parseMemOperand";
}

def VAXMemOp : Operand<iPTR> {
  let PrintMethod   = "printMemOperand";
  let MIOperandInfo = (ops GPR, i32imm, GPR, i32imm);
  let ParserMatchClass = VAXMemAsmOperand;
}

// Byte-width immediate operand (.rb context). The MC encoder emits only 1 byte
// for immediate mode instead of the default 4. Used by ASHL cnt, FFS/FFC size, etc.
def ByteImm : Operand<i32> {
  let OperandType = "OPERAND_BYTE_IMM";
  let OperandNamespace = "VAXOp";
}

// Word-width immediate operand (.rw context). The MC encoder emits 2 bytes.
def WordImm : Operand<i32> {
  let OperandType = "OPERAND_WORD_IMM";
  let OperandNamespace = "VAXOp";
}

// ComplexPattern that recognizes base + displacement + index + flags addressing.
// Handles: FrameIndex, ADD(FrameIndex, Const), ADD(Reg, Const), bare Reg.
// Index is always NoReg and flags is always 0 (Disp) from SelectionDAG.
def VAXMem : ComplexPattern<iPTR, 4, "SelectVAXAddr", [frameindex, add], []>;

// Enhanced ComplexPattern for longword (4-byte) memory operands.
// Matches everything VAXMem does plus indexed: ADD(base, SHL(idx, 2)).
// Only use for instructions with 4-byte data size (MOVL, MOVF, etc.)
// because VAX indexed mode implicitly scales by the instruction's data size.
def VAXMemLong : ComplexPattern<iPTR, 4, "SelectVAXAddrLong",
                                [frameindex, add], []>;

//===----------------------------------------------------------------------===//
// Real instructions (Phase 3 — enough for "int f() { return 0; }")
//===----------------------------------------------------------------------===//

// NOP — kept as a known-good no-op; also satisfies old TableGen min-instruction req
def NOP : InstVAX<0x01, (outs), (ins), "nop", []>, Sched<[WriteNOP]>;

// RET — return from CALLS/CALLG frame (hardware restores saved regs, pops frame)
let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RET : InstVAX<0x04, (outs), (ins), "ret", [(VAXISD_ret_flag)]>,
    Sched<[WriteRET]>;

// CLRL — zero a longword register; cheapest way to load the constant 0
// Nearly all VAX data-manipulation instructions set condition codes (N/Z/V/C
// in PSW). Each instruction or block below explicitly declares Defs = [PSW].
let Defs = [PSW] in
def CLRL : InstVAX<0xD4, (outs GPRnoPC:$dst), (ins),
    "clrl\t$dst", [(set GPRnoPC:$dst, (i32 0))]>,
    Sched<[WriteALU]>;
// CLRL to memory: store zero without an intermediate register.
// Matches (store 0, addr) with higher specificity than MOVL_mi.
let Defs = [PSW], HasMemOp = 1 in
def CLRL_ms : InstVAX<0xD4, (outs), (ins VAXMemOp:$dst),
    "clrl\t$dst",
    [(store (i32 0), VAXMemLong:$dst)]>,
    Sched<[WriteALU]>;

//===----------------------------------------------------------------------===//
// Phase 4 — MOV instructions + basic addressing
//===----------------------------------------------------------------------===//

// MOVL (opcode 0xD0) — longword (32-bit) move, various operand forms.
// All variants share the opcode; operand specifiers encode the addressing mode.
// All MOV/MOVAL instructions set PSW condition codes.
let Defs = [PSW] in {

// reg <- reg  (register copy; no DAG pattern — emitted by copyPhysReg)
def MOVL_rr : InstVAX<0xD0, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
    "movl\t$src, $dst", []>, Sched<[WriteMOV]>;

// reg <- immediate  (assembler emits $imm literal)
def MOVL_ri : InstVAX<0xD0, (outs GPRnoPC:$dst), (ins i32imm:$imm),
    "movl\t$imm, $dst",
    [(set GPRnoPC:$dst, (i32 imm:$imm))]>, Sched<[WriteMOV]>;

// reg <- memory  (displacement/deferred load)
let HasMemOp = 1 in
def MOVL_rm : InstVAX<0xD0, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "movl\t$src, $dst",
    [(set GPRnoPC:$dst, (load VAXMemLong:$src))]>, Sched<[WriteMOV]>;

// memory <- reg  (displacement/deferred store)
let HasMemOp = 1 in
def MOVL_mr : InstVAX<0xD0, (outs), (ins GPRnoPC:$src, VAXMemOp:$dst),
    "movl\t$src, $dst",
    [(store GPRnoPC:$src, VAXMemLong:$dst)]>, Sched<[WriteMOV]>;

// memory <- immediate  (store constant to memory without register)
let HasMemOp = 1 in
def MOVL_mi : InstVAX<0xD0, (outs), (ins i32imm:$imm, VAXMemOp:$dst),
    "movl\t$imm, $dst",
    [(store (i32 imm:$imm), VAXMemLong:$dst)]>, Sched<[WriteMOV]>;

// reg <- global  (PC-relative load from global/extern data)
def MOVL_rg : InstVAX<0xD0, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "movl\t$sym, $dst",
    [(set GPRnoPC:$dst, (load (vax_pcrelwrapper tglobaladdr:$sym)))]>,
    Sched<[WriteMOV]>;

// reg <- constant pool entry (PC-relative load)
def MOVL_rcp : InstVAX<0xD0, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "movl\t$sym, $dst",
    [(set GPRnoPC:$dst, (load (vax_pcrelwrapper tconstpool:$sym)))]>,
    Sched<[WriteMOV]>;

// global <- reg  (PC-relative store to global/extern data)
def MOVL_gr : InstVAX<0xD0, (outs), (ins GPRnoPC:$src, i32imm:$sym),
    "movl\t$src, $sym",
    [(store GPRnoPC:$src, (vax_pcrelwrapper tglobaladdr:$sym))]>,
    Sched<[WriteMOV]>;

// Materialize a jump table address into a register (PC-relative).
// Emits: moval JT_sym, $dst
def MOVAL_jt : InstVAX<0xDE, (outs GPRnoPC:$dst), (ins i32imm:$jt),
    "moval\t$jt, $dst",
    [(set GPRnoPC:$dst, (vax_pcrelwrapper tjumptable:$jt))]>,
    Sched<[WriteMOV]>;

// Address-of-global: moval global_sym, $dst
def MOVAL_ga : InstVAX<0xDE, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "moval\t$sym, $dst",
    [(set GPRnoPC:$dst, (vax_pcrelwrapper tglobaladdr:$sym))]>,
    Sched<[WriteMOV]>;

// Address-of-constant-pool entry: moval .LCPI_n, $dst
def MOVAL_cp : InstVAX<0xDE, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "moval\t$sym, $dst",
    [(set GPRnoPC:$dst, (vax_pcrelwrapper tconstpool:$sym))]>,
    Sched<[WriteMOV]>;

// Address-of-block (computed goto): moval .Ltmp_n, $dst
def MOVAL_ba : InstVAX<0xDE, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "moval\t$sym, $dst",
    [(set GPRnoPC:$dst, (vax_pcrelwrapper tblockaddress:$sym))]>,
    Sched<[WriteMOV]>;
} // let Defs = [PSW] — end of MOV/MOVAL block

// Atomic load/store: VAX aligned loads/stores are naturally atomic.
def : Pat<(atomic_load_32 VAXMemLong:$src), (MOVL_rm VAXMemOp:$src)>;
def : Pat<(atomic_store_32 GPRnoPC:$src, VAXMemLong:$dst), (MOVL_mr GPRnoPC:$src, VAXMemOp:$dst)>;

//===----------------------------------------------------------------------===//
// Phase 5 — Integer Arithmetic (longword i32)
//===----------------------------------------------------------------------===//

// Custom node: VAXISD::BICL(mask, src) = src & ~mask.
// ISD::AND is lowered to this to avoid needing a separate AND instruction.
def SDT_VAXBICL : SDTypeProfile<1, 2, [SDTCisSameAs<0,1>, SDTCisSameAs<1,2>,
                                        SDTCisInt<0>]>;
def vax_bicl : SDNode<"VAXISD::BICL", SDT_VAXBICL>;

// Multiclass for 3-operand commutative ALU operations.
// LLVM canonicalises commutative ops so constants appear as the RHS operand.
multiclass VAXAlu3_comm<bits<16> opc, string mn, SDNode SDOp,
                        SchedWrite sched = WriteALU> {
  def _rr : InstVAX<opc, (outs GPRnoPC:$dst), (ins GPRnoPC:$a, GPRnoPC:$b),
      mn # "\t$a, $b, $dst",
      [(set GPRnoPC:$dst, (SDOp GPRnoPC:$a, GPRnoPC:$b))]>,
    Sched<[sched]>;
  // Constant is RHS after LLVM's commutative canonicalisation.
  def _ri : InstVAX<opc, (outs GPRnoPC:$dst), (ins i32imm:$a, GPRnoPC:$b),
      mn # "\t$a, $b, $dst",
      [(set GPRnoPC:$dst, (SDOp GPRnoPC:$b, (i32 imm:$a)))]>,
    Sched<[sched]>;
  // Fold a load into one source operand: addl3 mem, reg, dst.
  // Commutative, so either operand can be the load.
  let HasMemOp = 1 in
  def _rm : InstVAX<opc, (outs GPRnoPC:$dst),
      (ins VAXMemOp:$a, GPRnoPC:$b),
      mn # "\t$a, $b, $dst",
      [(set GPRnoPC:$dst, (SDOp (load VAXMemLong:$a), GPRnoPC:$b))]>,
    Sched<[sched]>;
}

// Multiclass for 2-operand ALU: dst OP= src. Used by all xyzL2 instructions.
multiclass VAXAlu2<bits<16> opc, string mn, SchedWrite sched = WriteALU> {
  def _rr : InstVAX<opc, (outs GPRnoPC:$dst), (ins GPRnoPC:$s, GPRnoPC:$src),
      mn # "\t$s, $dst", []>, Sched<[sched]> {
    let Constraints = "$src = $dst";
  }
  def _ri : InstVAX<opc, (outs GPRnoPC:$dst), (ins i32imm:$s, GPRnoPC:$src),
      mn # "\t$s, $dst", []>, Sched<[sched]> {
    let Constraints = "$src = $dst";
  }
  // Memory source: addl2 mem, dst. Used by peephole (3op→2op with mem source).
  let HasMemOp = 1 in
  def _rm : InstVAX<opc, (outs GPRnoPC:$dst),
      (ins VAXMemOp:$s, GPRnoPC:$src),
      mn # "\t$s, $dst", []>, Sched<[sched]> {
    let Constraints = "$src = $dst";
  }
}

// 2-operand byte ALU: dst OP= src (byte). Used by peephole.
multiclass VAXAlu2B<bits<16> opc, string mn> {
  def _rr : InstVAX<opc, (outs GPRB:$dst), (ins GPRB:$s, GPRB:$src),
      mn # "\t$s, $dst", []>, Sched<[WriteALU]> {
    let Constraints = "$src = $dst";
  }
}

// 2-operand word ALU: dst OP= src (word). Used by peephole.
multiclass VAXAlu2W<bits<16> opc, string mn> {
  def _rr : InstVAX<opc, (outs GPRW:$dst), (ins GPRW:$s, GPRW:$src),
      mn # "\t$s, $dst", []>, Sched<[WriteALU]> {
    let Constraints = "$src = $dst";
  }
}

// 2-operand F_float ALU: dst OP= src. Used by peephole.
multiclass VAXAlu2F<bits<16> opc, string mn> {
  def _rr : InstVAX<opc, (outs GPRI:$dst), (ins GPRI:$s, GPRI:$src),
      mn # "\t$s, $dst", []>, Sched<[WriteFPALU]> {
    let Constraints = "$src = $dst";
  }
}

// 2-operand D_float ALU: dst OP= src. Used by peephole.
multiclass VAXAlu2D<bits<16> opc, string mn> {
  def _rr : InstVAX<opc, (outs QPR:$dst), (ins QPR:$s, QPR:$src),
      mn # "\t$s, $dst", []>, Sched<[WriteFPDALU]> {
    let Constraints = "$src = $dst";
  }
}

// Multiclass for zero-extending loads: MOVZBL, MOVZWL.
multiclass VAXMovZext<bits<16> opc, string mn, PatFrag zload, PatFrag eload> {
  let HasMemOp = 1 in {
  def _rm : InstVAX<opc, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
      mn # "\t$src, $dst",
      [(set GPRnoPC:$dst, (zload VAXMem:$src))]>, Sched<[WriteCVT]>;
  def _rm_e : InstVAX<opc, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
      mn # "\t$src, $dst",
      [(set GPRnoPC:$dst, (eload VAXMem:$src))]>, Sched<[WriteCVT]>;
  }
  def _rg : InstVAX<opc, (outs GPRnoPC:$dst), (ins i32imm:$sym),
      mn # "\t$sym, $dst",
      [(set GPRnoPC:$dst, (zload (vax_pcrelwrapper tglobaladdr:$sym)))]>,
    Sched<[WriteCVT]>;
  def _rg_e : InstVAX<opc, (outs GPRnoPC:$dst), (ins i32imm:$sym),
      mn # "\t$sym, $dst",
      [(set GPRnoPC:$dst, (eload (vax_pcrelwrapper tglobaladdr:$sym)))]>,
    Sched<[WriteCVT]>;
}

// Multiclass for sign-extending conversions: CVTBL, CVTWL.
multiclass VAXSextConv<bits<16> opc, string mn, PatFrag sload, ValueType subvt> {
  let HasMemOp = 1 in
  def _rm : InstVAX<opc, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
      mn # "\t$src, $dst",
      [(set GPRnoPC:$dst, (sload VAXMem:$src))]>, Sched<[WriteCVT]>;
  def _rg : InstVAX<opc, (outs GPRnoPC:$dst), (ins i32imm:$sym),
      mn # "\t$sym, $dst",
      [(set GPRnoPC:$dst, (sload (vax_pcrelwrapper tglobaladdr:$sym)))]>,
    Sched<[WriteCVT]>;
  def _rr : InstVAX<opc, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
      mn # "\t$src, $dst",
      [(set GPRnoPC:$dst, (sext_inreg GPRnoPC:$src, subvt))]>,
    Sched<[WriteCVT]>;
}

// Multiclass for truncating stores: MOVB, MOVW.
multiclass VAXTruncStore<bits<16> opc, string mn, PatFrag tstore> {
  let HasMemOp = 1 in
  def _mr : InstVAX<opc, (outs), (ins GPRnoPC:$src, VAXMemOp:$dst),
      mn # "\t$src, $dst",
      [(tstore GPRnoPC:$src, VAXMem:$dst)]>, Sched<[WriteMOV]>;
  def _mg : InstVAX<opc, (outs), (ins GPRnoPC:$src, i32imm:$sym),
      mn # "\t$src, $sym",
      [(tstore GPRnoPC:$src, (vax_pcrelwrapper tglobaladdr:$sym))]>,
    Sched<[WriteMOV]>;
}

// Multiclass for FP data moves (load/store/copy variants).
multiclass VAXFpMov<bits<16> opc, string mn, RegisterClass RC,
                    ValueType VT, Operand MemOp> {
  let HasMemOp = 1 in
  def _rm : InstVAX<opc, (outs RC:$dst), (ins MemOp:$src),
      mn # "\t$src, $dst",
      [(set RC:$dst, (VT (load VAXMem:$src)))]>, Sched<[WriteFPMOV]>;
  def _rcp : InstVAX<opc, (outs RC:$dst), (ins i32imm:$sym),
      mn # "\t$sym, $dst",
      [(set RC:$dst, (VT (load (vax_pcrelwrapper tconstpool:$sym))))]>,
    Sched<[WriteFPMOV]>;
  def _rga : InstVAX<opc, (outs RC:$dst), (ins i32imm:$sym),
      mn # "\t$sym, $dst",
      [(set RC:$dst, (VT (load (vax_pcrelwrapper tglobaladdr:$sym))))]>,
    Sched<[WriteFPMOV]>;
  let HasMemOp = 1 in
  def _mr : InstVAX<opc, (outs), (ins RC:$src, MemOp:$dst),
      mn # "\t$src, $dst",
      [(store (VT RC:$src), VAXMem:$dst)]>, Sched<[WriteFPMOV]>;
  def _mga : InstVAX<opc, (outs), (ins RC:$src, i32imm:$sym),
      mn # "\t$src, $sym",
      [(store (VT RC:$src), (vax_pcrelwrapper tglobaladdr:$sym))]>,
    Sched<[WriteFPMOV]>;
  def _rr : InstVAX<opc, (outs RC:$dst), (ins RC:$src),
      mn # "\t$src, $dst", []>, Sched<[WriteFPMOV]>;
}

// All integer ALU instructions set PSW condition codes.
let Defs = [PSW] in {
defm ADDL3 : VAXAlu3_comm<0xC1, "addl3", add>;
defm MULL3 : VAXAlu3_comm<0xC5, "mull3", mul, WriteMULL>;
defm BISL3 : VAXAlu3_comm<0xC9, "bisl3", or>;
defm XORL3 : VAXAlu3_comm<0xCD, "xorl3", xor>;

// SUBL3 — dst = minuend - subtrahend.
// VAX encoding: subl3 src1, src2, dst  →  dst = src2 - src1.
//   $s = subtrahend (src1), $m = minuend (src2).
def SUBL3_rr : InstVAX<0xC3, (outs GPRnoPC:$dst), (ins GPRnoPC:$s, GPRnoPC:$m),
    "subl3\t$s, $m, $dst",
    [(set GPRnoPC:$dst, (sub GPRnoPC:$m, GPRnoPC:$s))]>,
    Sched<[WriteALU]>;
// reg - imm  (subtrahend is immediate)
def SUBL3_ri : InstVAX<0xC3, (outs GPRnoPC:$dst), (ins i32imm:$s, GPRnoPC:$m),
    "subl3\t$s, $m, $dst",
    [(set GPRnoPC:$dst, (sub GPRnoPC:$m, (i32 imm:$s)))]>,
    Sched<[WriteALU]>;
// imm - reg  (minuend is immediate)
def SUBL3_ir : InstVAX<0xC3, (outs GPRnoPC:$dst), (ins GPRnoPC:$s, i32imm:$m),
    "subl3\t$s, $m, $dst",
    [(set GPRnoPC:$dst, (sub (i32 imm:$m), GPRnoPC:$s))]>,
    Sched<[WriteALU]>;
// reg - mem  (subtrahend loaded from memory): dst = reg - (load addr)
let HasMemOp = 1 in
def SUBL3_rm : InstVAX<0xC3, (outs GPRnoPC:$dst),
    (ins VAXMemOp:$s, GPRnoPC:$m),
    "subl3\t$s, $m, $dst",
    [(set GPRnoPC:$dst, (sub GPRnoPC:$m, (load VAXMemLong:$s)))]>,
    Sched<[WriteALU]>;
// mem - reg  (minuend loaded from memory): dst = (load addr) - reg
let HasMemOp = 1 in
def SUBL3_mr : InstVAX<0xC3, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$s, VAXMemOp:$m),
    "subl3\t$s, $m, $dst",
    [(set GPRnoPC:$dst, (sub (load VAXMemLong:$m), GPRnoPC:$s))]>,
    Sched<[WriteALU]>;

// DIVL3 — signed divide: dst = dividend / divisor.
// VAX encoding: divl3 src1, src2, dst  →  dst = src2 / src1.
def DIVL3_rr : InstVAX<0xC7, (outs GPRnoPC:$dst), (ins GPRnoPC:$dv, GPRnoPC:$dd),
    "divl3\t$dv, $dd, $dst",
    [(set GPRnoPC:$dst, (sdiv GPRnoPC:$dd, GPRnoPC:$dv))]>,
    Sched<[WriteDIVL]>;

// MNEGL — negate longword: dst = -src  (matches ISD ineg = sub 0, x)
def MNEGL : InstVAX<0xCE, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
    "mnegl\t$src, $dst",
    [(set GPRnoPC:$dst, (ineg GPRnoPC:$src))]>,
    Sched<[WriteMCOM]>;
let HasMemOp = 1 in
def MNEGL_m : InstVAX<0xCE, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "mnegl\t$src, $dst",
    [(set GPRnoPC:$dst, (ineg (load VAXMemLong:$src)))]>,
    Sched<[WriteMCOM]>;

// MCOML — ones complement: dst = ~src  (matches not = xor x, -1)
def MCOML : InstVAX<0xD2, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
    "mcoml\t$src, $dst",
    [(set GPRnoPC:$dst, (not GPRnoPC:$src))]>,
    Sched<[WriteMCOM]>;
let HasMemOp = 1 in
def MCOML_m : InstVAX<0xD2, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "mcoml\t$src, $dst",
    [(set GPRnoPC:$dst, (not (load VAXMemLong:$src)))]>,
    Sched<[WriteMCOM]>;

// BICL3 — bit clear: dst = src & ~mask  (selected from VAXISD::BICL)
def BICL3_rr : InstVAX<0xCB, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$mask, GPRnoPC:$src),
    "bicl3\t$mask, $src, $dst",
    [(set GPRnoPC:$dst, (vax_bicl GPRnoPC:$mask, GPRnoPC:$src))]>,
    Sched<[WriteALU]>;
def BICL3_ri : InstVAX<0xCB, (outs GPRnoPC:$dst),
    (ins i32imm:$mask, GPRnoPC:$src),
    "bicl3\t$mask, $src, $dst",
    [(set GPRnoPC:$dst, (vax_bicl (i32 imm:$mask), GPRnoPC:$src))]>,
    Sched<[WriteALU]>;
// Fold load into source operand: dst = (load addr) & ~mask
let HasMemOp = 1 in
def BICL3_rm : InstVAX<0xCB, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$mask, VAXMemOp:$src),
    "bicl3\t$mask, $src, $dst",
    [(set GPRnoPC:$dst, (vax_bicl GPRnoPC:$mask, (load VAXMemLong:$src)))]>,
    Sched<[WriteALU]>;

// MOVZBL — zero-extend byte to longword.
defm MOVZBL : VAXMovZext<0x9A, "movzbl", zextloadi8, extloadi8>;
} // let Defs = [PSW] — end of integer ALU + MOVZBL block

//===----------------------------------------------------------------------===//
// Phase 6 — Comparisons + Branches
//===----------------------------------------------------------------------===//

// Branch target operand — holds a BasicBlock/label reference.
def brtarget : Operand<OtherVT>;

// SDNode for VAXISD::CMP: compare two i32 values, produce a glue (PSW).
def SDT_VAXCMP : SDTypeProfile<0, 2, [SDTCisSameAs<0,1>, SDTCisInt<0>]>;
def vax_cmp : SDNode<"VAXISD::CMP", SDT_VAXCMP, [SDNPOutGlue]>;

// SDNode for VAXISD::BRCC: conditional branch consuming CC glue.
// Explicit operands: dest:OtherVT, cc:i32.  Chain + Glue are implicit.
def SDT_VAXBRCC : SDTypeProfile<0, 2,
    [SDTCisVT<0, OtherVT>, SDTCisVT<1, i32>]>;
def vax_brcc : SDNode<"VAXISD::BRCC", SDT_VAXBRCC,
    [SDNPHasChain, SDNPInGlue]>;

// SDNode for VAXISD::CASEL: hardware switch dispatch.
// Operands: index:i32, limit:i32, jti:i32. Chain is implicit.
def SDT_VAXCASEL : SDTypeProfile<0, 3,
    [SDTCisVT<0, i32>, SDTCisVT<1, i32>, SDTCisVT<2, i32>]>;
def VAXISD_CASEL : SDNode<"VAXISD::CASEL", SDT_VAXCASEL,
    [SDNPHasChain]>;

// Condition-code integer constants used as pattern leaves in branch defs.
// Must match the VAXCC values assigned in LowerBR_CC().
def VAXCC_EQL  : PatLeaf<(i32 0)>;   // equal
def VAXCC_NEQ  : PatLeaf<(i32 1)>;   // not equal
def VAXCC_GTR  : PatLeaf<(i32 2)>;   // greater than (signed)
def VAXCC_GEQ  : PatLeaf<(i32 3)>;   // greater or equal (signed)
def VAXCC_LSS  : PatLeaf<(i32 4)>;   // less than (signed)
def VAXCC_LEQ  : PatLeaf<(i32 5)>;   // less or equal (signed)
def VAXCC_GTRU : PatLeaf<(i32 6)>;   // greater than (unsigned)
def VAXCC_GEQU : PatLeaf<(i32 7)>;   // greater or equal (unsigned)
def VAXCC_LSSU : PatLeaf<(i32 8)>;   // less than (unsigned)
def VAXCC_LEQU : PatLeaf<(i32 9)>;   // less or equal (unsigned)

// CMPL — Compare Longword: sets PSW condition codes based on op1 - op2.
let Defs = [PSW] in {
def CMPL_rr : InstVAX<0xD1, (outs), (ins GPRnoPC:$a, GPRnoPC:$b),
    "cmpl\t$a, $b",
    [(vax_cmp GPRnoPC:$a, GPRnoPC:$b)]>,
    Sched<[WriteALU]>;
def CMPL_ri : InstVAX<0xD1, (outs), (ins GPRnoPC:$a, i32imm:$b),
    "cmpl\t$a, $b",
    [(vax_cmp GPRnoPC:$a, (i32 imm:$b))]>,
    Sched<[WriteALU]>;
// Compare register against memory operand.
let HasMemOp = 1 in
def CMPL_rm : InstVAX<0xD1, (outs), (ins VAXMemOp:$a, GPRnoPC:$b),
    "cmpl\t$a, $b",
    [(vax_cmp (load VAXMemLong:$a), GPRnoPC:$b)]>,
    Sched<[WriteALU]>;
// TSTL — Test Longword: equivalent to cmpl reg, $0 but one byte shorter.
def TSTL : InstVAX<0xD5, (outs), (ins GPRnoPC:$a),
    "tstl\t$a",
    [(vax_cmp GPRnoPC:$a, (i32 0))]>,
    Sched<[WriteALU]>;
let HasMemOp = 1 in
def TSTL_m : InstVAX<0xD5, (outs), (ins VAXMemOp:$a),
    "tstl\t$a",
    [(vax_cmp (load VAXMemLong:$a), (i32 0))]>,
    Sched<[WriteALU]>;
}

// Unconditional branch — use BRW (16-bit displacement, ±32 KB) as the
// default form; the assembler will shorten to BRB when possible.
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def BRW : InstVAX<0x31, (outs), (ins brtarget:$dst), "brw\t$dst",
    [(br bb:$dst)]>,
    Sched<[WriteBR]>;

// 8-bit unconditional branch (used when assembler / relaxation pass shortens).
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def BRB : InstVAX<0x11, (outs), (ins brtarget:$dst), "brb\t$dst", []>,
    Sched<[WriteBR]>;

// JMP — indirect branch through register (for jump tables / computed goto).
// VAX JMP opcode 0x17; operand is an address specifier.
let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1 in
def JMP : InstVAX<0x17, (outs), (ins GPRnoPC:$target), "jmp\t(${target})",
    [(brind GPRnoPC:$target)]>,
    Sched<[WriteJMP]>;

// JMP_mem — for AsmPrinter expansion: JMP with memory operand encoding.
let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1,
    HasMemOp = 1 in
def JMP_mem : InstVAX<0x17, (outs), (ins VAXMemOp:$target), "jmp\t$target",
    []>,
    Sched<[WriteJMP]>;

// CASEL — hardware switch dispatch via inline word-displacement table.
// Pseudo: lowered in AsmPrinter to CASEL_L MCInst + .word entries.
let isPseudo = 1, isBranch = 1, isTerminator = 1, isBarrier = 1,
    isIndirectBranch = 1 in
def CASEL : Pseudo<(outs), (ins GPRnoPC:$index, i32imm:$limit, i32imm:$jti),
    "", [(VAXISD_CASEL GPRnoPC:$index, (i32 imm:$limit), (i32 tjumptable:$jti))]>;

// Real CASEL_L instruction (opcode 0xCF): casel selector, base, limit.
// Used by AsmPrinter expansion of the CASEL pseudo and parseable by AsmParser.
def CASEL_L : InstVAX<0xCF, (outs),
    (ins GPRnoPC:$selector, i32imm:$base, i32imm:$limit),
    "casel\t$selector, $base, $limit", []>,
    Sched<[WriteCASE]>;

// Conditional branches (8-bit signed displacement ±127 B; all use PSW).
// Each is a separate def so the opcode byte is fixed at instruction level.
let Uses = [PSW], isBranch = 1, isTerminator = 1 in {
def BEQL  : InstVAX<0x13, (outs), (ins brtarget:$dst), "beql\t$dst",
    [(vax_brcc bb:$dst, VAXCC_EQL)]>,
    Sched<[WriteBRcc]>;
def BNEQ  : InstVAX<0x12, (outs), (ins brtarget:$dst), "bneq\t$dst",
    [(vax_brcc bb:$dst, VAXCC_NEQ)]>,
    Sched<[WriteBRcc]>;
def BGTR  : InstVAX<0x14, (outs), (ins brtarget:$dst), "bgtr\t$dst",
    [(vax_brcc bb:$dst, VAXCC_GTR)]>,
    Sched<[WriteBRcc]>;
def BGEQ  : InstVAX<0x18, (outs), (ins brtarget:$dst), "bgeq\t$dst",
    [(vax_brcc bb:$dst, VAXCC_GEQ)]>,
    Sched<[WriteBRcc]>;
def BLSS  : InstVAX<0x19, (outs), (ins brtarget:$dst), "blss\t$dst",
    [(vax_brcc bb:$dst, VAXCC_LSS)]>,
    Sched<[WriteBRcc]>;
def BLEQ  : InstVAX<0x15, (outs), (ins brtarget:$dst), "bleq\t$dst",
    [(vax_brcc bb:$dst, VAXCC_LEQ)]>,
    Sched<[WriteBRcc]>;
def BGTRU : InstVAX<0x1A, (outs), (ins brtarget:$dst), "bgtru\t$dst",
    [(vax_brcc bb:$dst, VAXCC_GTRU)]>,
    Sched<[WriteBRcc]>;
def BGEQU : InstVAX<0x1E, (outs), (ins brtarget:$dst), "bgequ\t$dst",
    [(vax_brcc bb:$dst, VAXCC_GEQU)]>,
    Sched<[WriteBRcc]>;
def BLSSU : InstVAX<0x1F, (outs), (ins brtarget:$dst), "blssu\t$dst",
    [(vax_brcc bb:$dst, VAXCC_LSSU)]>,
    Sched<[WriteBRcc]>;
def BLEQU : InstVAX<0x1B, (outs), (ins brtarget:$dst), "blequ\t$dst",
    [(vax_brcc bb:$dst, VAXCC_LEQU)]>,
    Sched<[WriteBRcc]>;
}

// Fused compare-branch pseudos: prevent the RA from inserting PSW-clobbering
// COPYs between compare and conditional branch.  These are created by a
// pre-RA pass and expanded back into CMP + Bcc after RA.
let isPseudo = 1, isBranch = 1, isTerminator = 1, Defs = [PSW] in {
def CMP_BRANCH_ri : Pseudo<(outs),
    (ins GPRnoPC:$lhs, i32imm:$rhs, i32imm:$cc, brtarget:$dst), "", []>;
def CMP_BRANCH_rr : Pseudo<(outs),
    (ins GPRnoPC:$lhs, GPRnoPC:$rhs, i32imm:$cc, brtarget:$dst), "", []>;
def TST_BRANCH : Pseudo<(outs),
    (ins GPRnoPC:$src, i32imm:$cc, brtarget:$dst), "", []>;
def CMPF_BRANCH : Pseudo<(outs),
    (ins GPRI:$lhs, GPRI:$rhs, i32imm:$cc, brtarget:$dst), "", []>;
def CMPD_BRANCH : Pseudo<(outs),
    (ins QPR:$lhs, QPR:$rhs, i32imm:$cc, brtarget:$dst), "", []>;
def TSTF_BRANCH : Pseudo<(outs),
    (ins GPRI:$src, i32imm:$cc, brtarget:$dst), "", []>;
def TSTD_BRANCH : Pseudo<(outs),
    (ins QPR:$src, i32imm:$cc, brtarget:$dst), "", []>;
// Memory-operand compare/test fused pseudos:
def CMP_BRANCH_rm : Pseudo<(outs),
    (ins VAXMemOp:$lhs, GPRnoPC:$rhs, i32imm:$cc, brtarget:$dst), "", []>;
def TST_BRANCH_m : Pseudo<(outs),
    (ins VAXMemOp:$src, i32imm:$cc, brtarget:$dst), "", []>;
// Byte/word compare/test fused pseudos:
def CMPB_BRANCH : Pseudo<(outs),
    (ins GPRB:$lhs, GPRB:$rhs, i32imm:$cc, brtarget:$dst), "", []>;
def TSTB_BRANCH : Pseudo<(outs),
    (ins GPRB:$src, i32imm:$cc, brtarget:$dst), "", []>;
def CMPW_BRANCH : Pseudo<(outs),
    (ins GPRW:$lhs, GPRW:$rhs, i32imm:$cc, brtarget:$dst), "", []>;
def TSTW_BRANCH : Pseudo<(outs),
    (ins GPRW:$src, i32imm:$cc, brtarget:$dst), "", []>;
}

// INCL / DECL — increment / decrement longword (sets PSW).
let Defs = [PSW] in {
def INCL : InstVAX<0xD6, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
    "incl\t$dst",
    [(set GPRnoPC:$dst, (add GPRnoPC:$src, (i32 1)))]> {
  let Constraints = "$src = $dst";
}
def DECL : InstVAX<0xD7, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
    "decl\t$dst",
    [(set GPRnoPC:$dst, (add GPRnoPC:$src, (i32 -1)))]> {
  let Constraints = "$src = $dst";
}
}

//===----------------------------------------------------------------------===//
// Phase 7 — Sign/Zero-Extend Loads + Truncating Stores
//===----------------------------------------------------------------------===//

// All these conversion/move instructions set PSW condition codes.
let Defs = [PSW] in {

// CVTBL (0x98) — Convert Byte to Longword: sign-extends the low byte of the
// source operand to 32 bits.  Covers sextloadi8 and sext_inreg i8.
defm CVTBL : VAXSextConv<0x98, "cvtbl", sextloadi8, i8>;

// MOVZWL (0x3C) — Move Zero-Extended Word to Longword.
defm MOVZWL : VAXMovZext<0x3C, "movzwl", zextloadi16, extloadi16>;

// CVTWL (0x32) — Convert Word to Longword: sign-extends low 16 bits.
defm CVTWL : VAXSextConv<0x32, "cvtwl", sextloadi16, i16>;

// MOVB (0x90) — Move Byte: used for truncating stores of i32 to i8 memory.
defm MOVB : VAXTruncStore<0x90, "movb", truncstorei8>;

// MOVW (0xB0) — Move Word: used for truncating stores of i32 to i16 memory.
defm MOVW : VAXTruncStore<0xB0, "movw", truncstorei16>;
} // let Defs = [PSW] — end of Phase 7 conversion/move block

//===----------------------------------------------------------------------===//
// Phase 33B — Native byte/word loads, stores, extends, truncates
//===----------------------------------------------------------------------===//

let Defs = [PSW] in {

// MOVB (0x90) — native i8 load/store/copy (byte register class).
// reg <- mem (byte load)
let HasMemOp = 1 in
def MOVBload : InstVAX<0x90, (outs GPRB:$dst), (ins VAXMemOp:$src),
    "movb\t$src, $dst",
    [(set GPRB:$dst, (load VAXMem:$src))]>;
// reg <- globaladdr (byte load via PC-relative)
def MOVBload_ga : InstVAX<0x90, (outs GPRB:$dst), (ins i32imm:$sym),
    "movb\t$sym, $dst",
    [(set GPRB:$dst, (load (vax_pcrelwrapper tglobaladdr:$sym)))]>;
// mem <- reg (byte store)
let HasMemOp = 1 in
def MOVBstore : InstVAX<0x90, (outs), (ins GPRB:$src, VAXMemOp:$dst),
    "movb\t$src, $dst",
    [(store GPRB:$src, VAXMem:$dst)]>;
// mem <- imm (byte store immediate)
let HasMemOp = 1 in
def MOVBstore_i : InstVAX<0x90, (outs), (ins i8imm:$src, VAXMemOp:$dst),
    "movb\t$src, $dst",
    [(store (i8 imm:$src), VAXMem:$dst)]>;
// globaladdr <- reg (byte store via PC-relative)
def MOVBstore_ga : InstVAX<0x90, (outs), (ins GPRB:$src, i32imm:$sym),
    "movb\t$src, $sym",
    [(store GPRB:$src, (vax_pcrelwrapper tglobaladdr:$sym))]>;
// reg <- reg (byte copy; no DAG pattern — emitted by copyPhysReg)
def MOVBrr : InstVAX<0x90, (outs GPRB:$dst), (ins GPRB:$src),
    "movb\t$src, $dst", []>;
// reg <- imm (byte immediate load)
def MOVBri : InstVAX<0x90, (outs GPRB:$dst), (ins i8imm:$src),
    "movb\t$src, $dst",
    [(set GPRB:$dst, (i8 imm:$src))]>;

// MOVW (0xB0) — native i16 load/store/copy (word register class).
// reg <- mem (word load)
let HasMemOp = 1 in
def MOVWload : InstVAX<0xB0, (outs GPRW:$dst), (ins VAXMemOp:$src),
    "movw\t$src, $dst",
    [(set GPRW:$dst, (load VAXMem:$src))]>;
// reg <- globaladdr (word load via PC-relative)
def MOVWload_ga : InstVAX<0xB0, (outs GPRW:$dst), (ins i32imm:$sym),
    "movw\t$sym, $dst",
    [(set GPRW:$dst, (load (vax_pcrelwrapper tglobaladdr:$sym)))]>;
// mem <- reg (word store)
let HasMemOp = 1 in
def MOVWstore : InstVAX<0xB0, (outs), (ins GPRW:$src, VAXMemOp:$dst),
    "movw\t$src, $dst",
    [(store GPRW:$src, VAXMem:$dst)]>;
// mem <- imm (word store immediate)
let HasMemOp = 1 in
def MOVWstore_i : InstVAX<0xB0, (outs), (ins i16imm:$src, VAXMemOp:$dst),
    "movw\t$src, $dst",
    [(store (i16 imm:$src), VAXMem:$dst)]>;
// globaladdr <- reg (word store via PC-relative)
def MOVWstore_ga : InstVAX<0xB0, (outs), (ins GPRW:$src, i32imm:$sym),
    "movw\t$src, $sym",
    [(store GPRW:$src, (vax_pcrelwrapper tglobaladdr:$sym))]>;
// reg <- reg (word copy; no DAG pattern — emitted by copyPhysReg)
def MOVWrr : InstVAX<0xB0, (outs GPRW:$dst), (ins GPRW:$src),
    "movw\t$src, $dst", []>;
// reg <- imm (word immediate load)
def MOVWri : InstVAX<0xB0, (outs GPRW:$dst), (ins i16imm:$src),
    "movw\t$src, $dst",
    [(set GPRW:$dst, (i16 imm:$src))]>;

// MOVZBL (0x9A) reg <- reg: zero-extend i8 to i32.
def MOVZBL_rr : InstVAX<0x9A, (outs GPRnoPC:$dst), (ins GPRB:$src),
    "movzbl\t$src, $dst",
    [(set GPRnoPC:$dst, (zext GPRB:$src))]>;

// CVTBL (0x98) reg <- reg: sign-extend i8 to i32.
def CVTBL_rr_ext : InstVAX<0x98, (outs GPRnoPC:$dst), (ins GPRB:$src),
    "cvtbl\t$src, $dst",
    [(set GPRnoPC:$dst, (sext GPRB:$src))]>;

// MOVZWL (0x3C) reg <- reg: zero-extend i16 to i32.
def MOVZWL_rr : InstVAX<0x3C, (outs GPRnoPC:$dst), (ins GPRW:$src),
    "movzwl\t$src, $dst",
    [(set GPRnoPC:$dst, (zext GPRW:$src))]>;

// CVTWL (0x32) reg <- reg: sign-extend i16 to i32.
def CVTWL_rr_ext : InstVAX<0x32, (outs GPRnoPC:$dst), (ins GPRW:$src),
    "cvtwl\t$src, $dst",
    [(set GPRnoPC:$dst, (sext GPRW:$src))]>;

} // let Defs = [PSW]

// anyext i8/i16→i32: use MOVZBL/MOVZWL (zero-fill is cheapest).
def : Pat<(i32 (anyext GPRB:$src)), (MOVZBL_rr GPRB:$src)>;
def : Pat<(i32 (anyext GPRW:$src)), (MOVZWL_rr GPRW:$src)>;

// CVTBW (0x99) reg <- reg: sign-extend i8 to i16.
let Defs = [PSW] in
def CVTBW_rr : InstVAX<0x99, (outs GPRW:$dst), (ins GPRB:$src),
    "cvtbw\t$src, $dst",
    [(set GPRW:$dst, (sext GPRB:$src))]>;

// MOVZBW (0x9B) reg <- reg: zero-extend i8 to i16.
let Defs = [PSW] in
def MOVZBW_rr : InstVAX<0x9B, (outs GPRW:$dst), (ins GPRB:$src),
    "movzbw\t$src, $dst",
    [(set GPRW:$dst, (zext GPRB:$src))]>;

// i8→i16 any-extend: use zero-extend (cheapest).
def : Pat<(i16 (anyext GPRB:$src)), (MOVZBW_rr GPRB:$src)>;

// Truncate patterns: extract sub-register (free — no instruction emitted).
def : Pat<(i8  (trunc GPRnoPC:$src)), (EXTRACT_SUBREG GPRnoPC:$src, sub_8lo)>;
def : Pat<(i16 (trunc GPRnoPC:$src)), (EXTRACT_SUBREG GPRnoPC:$src, sub_16lo)>;
def : Pat<(i8  (trunc GPRW:$src)),    (EXTRACT_SUBREG GPRW:$src, sub_8lo)>;

//===----------------------------------------------------------------------===//
// Phase 33B+ — Native byte/word arithmetic
//===----------------------------------------------------------------------===//

let Defs = [PSW] in {

// ADDB3 (0x81) — byte add: dst = src1 + src2
def ADDB3_rr : InstVAX<0x81, (outs GPRB:$dst), (ins GPRB:$a, GPRB:$b),
    "addb3\t$a, $b, $dst",
    [(set GPRB:$dst, (add GPRB:$a, GPRB:$b))]> {
  let isCommutable = 1;
}
// SUBB3 (0x83) — byte subtract: dst = src2 - src1
def SUBB3_rr : InstVAX<0x83, (outs GPRB:$dst), (ins GPRB:$s, GPRB:$m),
    "subb3\t$s, $m, $dst",
    [(set GPRB:$dst, (sub GPRB:$m, GPRB:$s))]>;

// MNEGB (0x8E) — byte negate: dst = -src
def MNEGB : InstVAX<0x8E, (outs GPRB:$dst), (ins GPRB:$src),
    "mnegb\t$src, $dst",
    [(set GPRB:$dst, (ineg GPRB:$src))]>;

// ADDW3 (0xA1) — word add: dst = src1 + src2
def ADDW3_rr : InstVAX<0xA1, (outs GPRW:$dst), (ins GPRW:$a, GPRW:$b),
    "addw3\t$a, $b, $dst",
    [(set GPRW:$dst, (add GPRW:$a, GPRW:$b))]> {
  let isCommutable = 1;
}
// SUBW3 (0xA3) — word subtract: dst = src2 - src1
def SUBW3_rr : InstVAX<0xA3, (outs GPRW:$dst), (ins GPRW:$s, GPRW:$m),
    "subw3\t$s, $m, $dst",
    [(set GPRW:$dst, (sub GPRW:$m, GPRW:$s))]>;

// MNEGW (0xAE) — word negate: dst = -src
def MNEGW : InstVAX<0xAE, (outs GPRW:$dst), (ins GPRW:$src),
    "mnegw\t$src, $dst",
    [(set GPRW:$dst, (ineg GPRW:$src))]>;

//===----------------------------------------------------------------------===//
// Phase 33C — Byte/word compare, logic, complement, clear
//===----------------------------------------------------------------------===//

// CMPB (0x91) — compare byte: sets PSW from op1 - op2
def CMPB_rr : InstVAX<0x91, (outs), (ins GPRB:$a, GPRB:$b),
    "cmpb\t$a, $b",
    [(vax_cmp GPRB:$a, GPRB:$b)]>;
// TSTB (0x95) — test byte: sets PSW from op - 0
def TSTB : InstVAX<0x95, (outs), (ins GPRB:$a),
    "tstb\t$a",
    [(vax_cmp GPRB:$a, (i8 0))]>;

// CMPW (0xB1) — compare word: sets PSW from op1 - op2
def CMPW_rr : InstVAX<0xB1, (outs), (ins GPRW:$a, GPRW:$b),
    "cmpw\t$a, $b",
    [(vax_cmp GPRW:$a, GPRW:$b)]>;
// TSTW (0xB5) — test word: sets PSW from op - 0
def TSTW : InstVAX<0xB5, (outs), (ins GPRW:$a),
    "tstw\t$a",
    [(vax_cmp GPRW:$a, (i16 0))]>;

// MCOMB (0x92) — ones complement byte: dst = ~src
def MCOMB : InstVAX<0x92, (outs GPRB:$dst), (ins GPRB:$src),
    "mcomb\t$src, $dst",
    [(set GPRB:$dst, (not GPRB:$src))]>;

// MCOMW (0xB2) — ones complement word: dst = ~src
def MCOMW : InstVAX<0xB2, (outs GPRW:$dst), (ins GPRW:$src),
    "mcomw\t$src, $dst",
    [(set GPRW:$dst, (not GPRW:$src))]>;

// BISB3 (0x89) — bit set byte (OR): dst = src1 | src2
def BISB3_rr : InstVAX<0x89, (outs GPRB:$dst), (ins GPRB:$a, GPRB:$b),
    "bisb3\t$a, $b, $dst",
    [(set GPRB:$dst, (or GPRB:$a, GPRB:$b))]> {
  let isCommutable = 1;
}

// BISW3 (0xA9) — bit set word (OR): dst = src1 | src2
def BISW3_rr : InstVAX<0xA9, (outs GPRW:$dst), (ins GPRW:$a, GPRW:$b),
    "bisw3\t$a, $b, $dst",
    [(set GPRW:$dst, (or GPRW:$a, GPRW:$b))]> {
  let isCommutable = 1;
}

// XORB3 (0x8D) — exclusive-or byte: dst = src1 ^ src2
def XORB3_rr : InstVAX<0x8D, (outs GPRB:$dst), (ins GPRB:$a, GPRB:$b),
    "xorb3\t$a, $b, $dst",
    [(set GPRB:$dst, (xor GPRB:$a, GPRB:$b))]> {
  let isCommutable = 1;
}

// XORW3 (0xAD) — exclusive-or word: dst = src1 ^ src2
def XORW3_rr : InstVAX<0xAD, (outs GPRW:$dst), (ins GPRW:$a, GPRW:$b),
    "xorw3\t$a, $b, $dst",
    [(set GPRW:$dst, (xor GPRW:$a, GPRW:$b))]> {
  let isCommutable = 1;
}

// BICB3 (0x8B) — bit clear byte: dst = src & ~mask
def BICB3_rr : InstVAX<0x8B, (outs GPRB:$dst),
    (ins GPRB:$mask, GPRB:$src),
    "bicb3\t$mask, $src, $dst",
    [(set GPRB:$dst, (vax_bicl GPRB:$mask, GPRB:$src))]>;
def BICB3_ri : InstVAX<0x8B, (outs GPRB:$dst),
    (ins i8imm:$mask, GPRB:$src),
    "bicb3\t$mask, $src, $dst",
    [(set GPRB:$dst, (vax_bicl (i8 imm:$mask), GPRB:$src))]>;

// BICW3 (0xAB) — bit clear word: dst = src & ~mask
def BICW3_rr : InstVAX<0xAB, (outs GPRW:$dst),
    (ins GPRW:$mask, GPRW:$src),
    "bicw3\t$mask, $src, $dst",
    [(set GPRW:$dst, (vax_bicl GPRW:$mask, GPRW:$src))]>;
def BICW3_ri : InstVAX<0xAB, (outs GPRW:$dst),
    (ins i16imm:$mask, GPRW:$src),
    "bicw3\t$mask, $src, $dst",
    [(set GPRW:$dst, (vax_bicl (i16 imm:$mask), GPRW:$src))]>;

} // let Defs = [PSW]

// LEA_FI — materialize frame slot address as a register value.
// Pseudo: expanded by eliminateFrameIndex to addl3 $offset, %fp, $dst.
// Operand layout matches VAXMemOp (base, disp) for eliminateFrameIndex.
def LEA_FI : Pseudo<(outs GPRnoPC:$dst), (ins VAXMemOp:$addr),
    "#LEA_FI $addr, $dst", []>;

//===----------------------------------------------------------------------===//
// SELECT_CC pseudo — expanded to branch diamond by EmitInstrWithCustomInserter
//===----------------------------------------------------------------------===//

def SDT_VAXSelectCC : SDTypeProfile<1, 3, [SDTCisSameAs<0, 1>,
                                            SDTCisSameAs<1, 2>,
                                            SDTCisVT<3, i32>]>;
def vax_select_cc : SDNode<"VAXISD::SELECT_CC", SDT_VAXSelectCC,
    [SDNPInGlue]>;

let usesCustomInserter = 1 in {
def SELECT_CC_Pseudo : Pseudo<(outs GPRnoPC:$dst),
    (ins GPRnoPC:$truev, GPRnoPC:$falsev, i32imm:$cc),
    "#SELECT_CC $truev, $falsev, $cc",
    [(set GPRnoPC:$dst, (vax_select_cc GPRnoPC:$truev, GPRnoPC:$falsev,
                                        (i32 imm:$cc)))]>;
def SELECT_CC_B_Pseudo : Pseudo<(outs GPRB:$dst),
    (ins GPRB:$truev, GPRB:$falsev, i32imm:$cc),
    "#SELECT_CC_B $truev, $falsev, $cc",
    [(set GPRB:$dst, (vax_select_cc GPRB:$truev, GPRB:$falsev,
                                      (i32 imm:$cc)))]>;
def SELECT_CC_W_Pseudo : Pseudo<(outs GPRW:$dst),
    (ins GPRW:$truev, GPRW:$falsev, i32imm:$cc),
    "#SELECT_CC_W $truev, $falsev, $cc",
    [(set GPRW:$dst, (vax_select_cc GPRW:$truev, GPRW:$falsev,
                                      (i32 imm:$cc)))]>;
}

//===----------------------------------------------------------------------===//
// Phase 10 — Shift + Rotate
//===----------------------------------------------------------------------===//

// All shift/rotate instructions set PSW condition codes.

// Custom node for VAX arithmetic shift (handles negative cnt for right shift).
def SDT_VAXASHL : SDTypeProfile<1, 2, [SDTCisSameAs<0, 2>, SDTCisInt<0>,
                                        SDTCisInt<1>]>;
def vax_ashl : SDNode<"VAXISD::ASHL", SDT_VAXASHL>;

let Defs = [PSW] in {
// ASHL (0x78) — Arithmetic Shift Longword: dst = src << cnt (positive cnt),
// or dst = src >> |cnt| (arithmetic, negative cnt).
// VAX encoding: ashl cnt, src, dst.
// shl: positive shift count.
def ASHL_rr : InstVAX<0x78, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$cnt, GPRnoPC:$src),
    "ashl\t$cnt, $src, $dst",
    [(set GPRnoPC:$dst, (shl GPRnoPC:$src, GPRnoPC:$cnt))]>,
    Sched<[WriteShift]>;
def ASHL_ir : InstVAX<0x78, (outs GPRnoPC:$dst),
    (ins ByteImm:$cnt, GPRnoPC:$src),
    "ashl\t$cnt, $src, $dst",
    [(set GPRnoPC:$dst, (shl GPRnoPC:$src, (i32 imm:$cnt)))]>,
    Sched<[WriteShift]>;

// ASHL for custom SRA lowering (cnt is already negated by LowerSRA).
def ASHL_rr_sra : InstVAX<0x78, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$cnt, GPRnoPC:$src),
    "ashl\t$cnt, $src, $dst",
    [(set GPRnoPC:$dst, (vax_ashl GPRnoPC:$cnt, GPRnoPC:$src))]>,
    Sched<[WriteShift]>;
def ASHL_ir_sra : InstVAX<0x78, (outs GPRnoPC:$dst),
    (ins ByteImm:$cnt, GPRnoPC:$src),
    "ashl\t$cnt, $src, $dst",
    [(set GPRnoPC:$dst, (vax_ashl (i32 imm:$cnt), GPRnoPC:$src))]>,
    Sched<[WriteShift]>;

// ROTL (0x9C) — Rotate Longword: dst = rotl(src, cnt).
def ROTL_rr : InstVAX<0x9C, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$cnt, GPRnoPC:$src),
    "rotl\t$cnt, $src, $dst",
    [(set GPRnoPC:$dst, (rotl GPRnoPC:$src, GPRnoPC:$cnt))]>,
    Sched<[WriteROTL]>;
def ROTL_ir : InstVAX<0x9C, (outs GPRnoPC:$dst),
    (ins ByteImm:$cnt, GPRnoPC:$src),
    "rotl\t$cnt, $src, $dst",
    [(set GPRnoPC:$dst, (rotl GPRnoPC:$src, (i32 imm:$cnt)))]>,
    Sched<[WriteROTL]>;
} // let Defs = [PSW] — end of shift/rotate block

// ROTL — assembly-only form with all MemOp operands (for hand-written .S)
let Defs = [PSW], HasMemOp = 1 in
def ROTL : InstVAX<0x9C, (outs),
    (ins VAXMemOp:$cnt, VAXMemOp:$src, VAXMemOp:$dst),
    "rotl\t$cnt, $src, $dst", []>;

//===----------------------------------------------------------------------===//
// EXTZV — Extract Zero-extended Field (ISel forms)
//===----------------------------------------------------------------------===//

// Custom SDNode for VAXISD::EXTZV: (pos, size, base) → result
def SDT_VAXEXTZV : SDTypeProfile<1, 3, [SDTCisInt<0>, SDTCisInt<1>,
                                          SDTCisInt<2>, SDTCisSameAs<0, 3>]>;
def vax_extzv : SDNode<"VAXISD::EXTZV", SDT_VAXEXTZV>;

// EXTZV with immediate pos/size and register base — constant lshr.
let Defs = [PSW] in
def EXTZV_iir : InstVAX<0xEF, (outs GPRnoPC:$dst),
    (ins ByteImm:$pos, ByteImm:$size, GPRnoPC:$base),
    "extzv\t$pos, $size, $base, $dst",
    [(set GPRnoPC:$dst, (vax_extzv (i32 imm:$pos), (i32 imm:$size),
                                     GPRnoPC:$base))]>,
    Sched<[WriteEXTZV]>;

// EXTZV with register pos/size — variable lshr.
let Defs = [PSW] in
// EXTZV with register pos, register size — variable lshr.
// The size operand is .rb but accepts a register operand specifier.
let Defs = [PSW] in
def EXTZV_rrr : InstVAX<0xEF, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$pos, GPRnoPC:$size, GPRnoPC:$base),
    "extzv\t$pos, $size, $base, $dst",
    [(set GPRnoPC:$dst, (vax_extzv GPRnoPC:$pos, GPRnoPC:$size,
                                     GPRnoPC:$base))]>,
    Sched<[WriteEXTZV]>;

// EXTZV with memory base — load + shift folded.
let Defs = [PSW], HasMemOp = 1 in
def EXTZV_iim : InstVAX<0xEF, (outs GPRnoPC:$dst),
    (ins ByteImm:$pos, ByteImm:$size, VAXMemOp:$base),
    "extzv\t$pos, $size, $base, $dst",
    [(set GPRnoPC:$dst, (vax_extzv (i32 imm:$pos), (i32 imm:$size),
                                     (load VAXMemLong:$base)))]>,
    Sched<[WriteEXTZV]>;

//===----------------------------------------------------------------------===//
// FFS — Find First Set bit (ISel form for cttz)
//===----------------------------------------------------------------------===//

// FFS $0, $32, src, dst — find lowest set bit (count trailing zeros).
let Defs = [PSW] in {
def FFS_rr : InstVAX<0xEA, (outs GPRnoPC:$dst),
    (ins ByteImm:$pos, ByteImm:$size, GPRnoPC:$base),
    "ffs\t$pos, $size, $base, $dst", []>,
    Sched<[WriteFFS]>;
let HasMemOp = 1 in
def FFS_rm : InstVAX<0xEA, (outs GPRnoPC:$dst),
    (ins ByteImm:$pos, ByteImm:$size, VAXMemOp:$base),
    "ffs\t$pos, $size, $base, $dst", []>,
    Sched<[WriteFFS]>;
}
def : Pat<(cttz_zero_undef i32:$src),
          (FFS_rr (i32 0), (i32 32), GPRnoPC:$src)>;
def : Pat<(cttz_zero_undef (i32 (load VAXMemLong:$src))),
          (FFS_rm (i32 0), (i32 32), VAXMemOp:$src)>;

//===----------------------------------------------------------------------===//
// Phase 8 — Function calls
//===----------------------------------------------------------------------===//

// CALLSEQ_START / CALLSEQ_END SDNodes (needed by SelectionDAG call lowering).
def SDT_VAXCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def SDT_VAXCallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_VAXCallSeqStart,
                            [SDNPHasChain, SDNPOutGlue]>;
def callseq_end   : SDNode<"ISD::CALLSEQ_END",   SDT_VAXCallSeqEnd,
                            [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

// Call sequence pseudo-instructions: matched from ISD::CALLSEQ_START/END,
// then eliminated by VAXFrameLowering::eliminateCallFramePseudoInstr.
let Defs = [SP], Uses = [SP] in {
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
    "#ADJCALLSTACKDOWN", [(callseq_start timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
    "#ADJCALLSTACKUP", [(callseq_end timm:$amt1, timm:$amt2)]>;
}

// 2-operand ALU instructions: dst OP= src. Used by frame lowering and peephole.
let Defs = [PSW] in {
defm ADDL2 : VAXAlu2<0xC0, "addl2">;
defm SUBL2 : VAXAlu2<0xC2, "subl2">;
defm MULL2 : VAXAlu2<0xC4, "mull2", WriteMULL>;
defm DIVL2 : VAXAlu2<0xC6, "divl2", WriteDIVL>;
defm BISL2 : VAXAlu2<0xC8, "bisl2">;
defm BICL2 : VAXAlu2<0xCA, "bicl2">;
defm XORL2 : VAXAlu2<0xCC, "xorl2">;
// Byte 2-operand
defm ADDB2 : VAXAlu2B<0x80, "addb2">;
defm SUBB2 : VAXAlu2B<0x82, "subb2">;
// Word 2-operand
defm ADDW2 : VAXAlu2W<0xA0, "addw2">;
defm SUBW2 : VAXAlu2W<0xA2, "subw2">;
} // let Defs = [PSW]

// PUSHL — push longword onto stack (SP autodecrement)  opcode 0xDD
def SDT_VAXPushL : SDTypeProfile<0, 1, [SDTCisInt<0>]>;
def vax_pushl : SDNode<"VAXISD::PUSHL", SDT_VAXPushL,
    [SDNPHasChain, SDNPOutGlue]>;

let Defs = [SP, PSW], Uses = [SP] in {
def PUSHL_r : InstVAX<0xDD, (outs), (ins GPRnoPC:$src),
    "pushl\t$src", [(vax_pushl GPRnoPC:$src)]>,
    Sched<[WritePUSH]>;
def PUSHL_i : InstVAX<0xDD, (outs), (ins i32imm:$imm),
    "pushl\t$imm", [(vax_pushl (i32 imm:$imm))]>,
    Sched<[WritePUSH]>;
}

// PUSHAx — push address onto stack (SP autodecrement)
// These are assembly-only (used in hand-written .S files like crtbegin.S)
// MOVAx/PUSHAx set N/Z based on the address value and clear V; C unchanged.
let Defs = [SP, PSW], Uses = [SP], HasMemOp = 1 in {
def PUSHAB : InstVAX<0x9F, (outs), (ins VAXMemOp:$src),
    "pushab\t$src", []>,
    Sched<[WritePUSHA]>;
def PUSHAW : InstVAX<0x3F, (outs), (ins VAXMemOp:$src),
    "pushaw\t$src", []>;
def PUSHAL : InstVAX<0xDF, (outs), (ins VAXMemOp:$src),
    "pushal\t$src", []>,
    Sched<[WritePUSHA]>;
def PUSHAQ : InstVAX<0x7F, (outs), (ins VAXMemOp:$src),
    "pushaq\t$src", []>;
}

// PUSHAL with PC-relative symbol operand — ISel patterns for pushing addresses.
// Parallel to MOVAL_ga/cp/ba but push instead of move-to-register.
let Defs = [SP, PSW], Uses = [SP] in {
def PUSHAL_ga : InstVAX<0xDF, (outs), (ins i32imm:$sym),
    "pushal\t$sym",
    [(vax_pushl (vax_pcrelwrapper tglobaladdr:$sym))]>,
    Sched<[WritePUSHA]>;
def PUSHAL_cp : InstVAX<0xDF, (outs), (ins i32imm:$sym),
    "pushal\t$sym",
    [(vax_pushl (vax_pcrelwrapper tconstpool:$sym))]>,
    Sched<[WritePUSHA]>;
def PUSHAL_ba : InstVAX<0xDF, (outs), (ins i32imm:$sym),
    "pushal\t$sym",
    [(vax_pushl (vax_pcrelwrapper tblockaddress:$sym))]>,
    Sched<[WritePUSHA]>;
def PUSHAL_es : InstVAX<0xDF, (outs), (ins i32imm:$sym),
    "pushal\t$sym",
    [(vax_pushl (vax_pcrelwrapper texternalsym:$sym))]>,
    Sched<[WritePUSHA]>;
}

// CALLS — call subroutine with stack arguments  opcode 0xFB
// CALLS clears all condition codes and clobbers R0-R5, SP, AP, FP.
let isCall = 1, Uses = [SP],
    Defs = [SP, AP, FP, R0, R1, R2, R3, R4, R5, PSW] in {
// Direct call: callee is a global address or external symbol
def CALLS_direct : InstVAX<0xFB, (outs),
    (ins i32imm:$count, i32imm:$callee),
    "calls\t$count, $callee", []>,
    Sched<[WriteCALL]>;
// Indirect call: callee address is in a register (register deferred addressing)
let isCodeGenOnly = 1 in
def CALLS_indir : InstVAX<0xFB, (outs),
    (ins i32imm:$count, GPRnoPC:$callee),
    "calls\t$count, (${callee})", []>,
    Sched<[WriteCALL]>;
// Indirect call (assembler form): callee is a memory operand (%reg deferred)
let HasMemOp = 1 in
def CALLS_mem : InstVAX<0xFB, (outs),
    (ins i32imm:$count, VAXMemOp:$callee),
    "calls\t$count, $callee", []>,
    Sched<[WriteCALL]>;
}

//===----------------------------------------------------------------------===//
// Phase 14A — Add/Subtract with Carry (i64 support)
//===----------------------------------------------------------------------===//

// ADWC (0xD8) — Add With Carry: dst = src + dst + C
// Used by ISD::ADDE for high half of i64 add.
def SDT_VAXADWC : SDTypeProfile<1, 2, [SDTCisSameAs<0,1>, SDTCisSameAs<1,2>,
                                        SDTCisInt<0>]>;

// ADWC — add with carry: dst = src + dst + PSW.C
let Defs = [PSW], Uses = [PSW] in {
def ADWC_rr : InstVAX<0xD8, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$src, GPRnoPC:$dstin),
    "adwc\t$src, $dst",
    [(set GPRnoPC:$dst, (adde GPRnoPC:$src, GPRnoPC:$dstin))]> {
  let Constraints = "$dstin = $dst";
}
def ADWC_ir : InstVAX<0xD8, (outs GPRnoPC:$dst),
    (ins i32imm:$src, GPRnoPC:$dstin),
    "adwc\t$src, $dst",
    [(set GPRnoPC:$dst, (adde (i32 imm:$src), GPRnoPC:$dstin))]> {
  let Constraints = "$dstin = $dst";
}
// SBWC — subtract with carry: dst = dst - src - PSW.C
def SBWC_rr : InstVAX<0xD9, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$src, GPRnoPC:$dstin),
    "sbwc\t$src, $dst",
    [(set GPRnoPC:$dst, (sube GPRnoPC:$dstin, GPRnoPC:$src))]> {
  let Constraints = "$dstin = $dst";
}
def SBWC_ir : InstVAX<0xD9, (outs GPRnoPC:$dst),
    (ins i32imm:$src, GPRnoPC:$dstin),
    "sbwc\t$src, $dst",
    [(set GPRnoPC:$dst, (sube GPRnoPC:$dstin, (i32 imm:$src)))]> {
  let Constraints = "$dstin = $dst";
}
}

// ADDL3 variant that defines carry (for low half of i64 add).
// ISD::ADDC produces (result, glue) — glue carries the carry flag.
let Defs = [PSW] in {
def ADDL3_rr_cc : InstVAX<0xC1, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$a, GPRnoPC:$b),
    "addl3\t$a, $b, $dst",
    [(set GPRnoPC:$dst, (addc GPRnoPC:$a, GPRnoPC:$b))]>;
def ADDL3_ri_cc : InstVAX<0xC1, (outs GPRnoPC:$dst),
    (ins i32imm:$a, GPRnoPC:$b),
    "addl3\t$a, $b, $dst",
    [(set GPRnoPC:$dst, (addc GPRnoPC:$b, (i32 imm:$a)))]>;
def SUBL3_rr_cc : InstVAX<0xC3, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$s, GPRnoPC:$m),
    "subl3\t$s, $m, $dst",
    [(set GPRnoPC:$dst, (subc GPRnoPC:$m, GPRnoPC:$s))]>;
def SUBL3_ri_cc : InstVAX<0xC3, (outs GPRnoPC:$dst),
    (ins i32imm:$s, GPRnoPC:$m),
    "subl3\t$s, $m, $dst",
    [(set GPRnoPC:$dst, (subc GPRnoPC:$m, (i32 imm:$s)))]>;
}

//===----------------------------------------------------------------------===//
// Quadword Operations (ASHQ, EMUL, EDIV)
//===----------------------------------------------------------------------===//

// ASHQ (0x79) — Arithmetic Shift Quadword.
// cnt > 0: left shift; cnt < 0: arithmetic right shift.
// Operands: cnt (byte), src (quadword), dst (quadword).
// Quadword operands use QPR (consecutive register pair Rn:Rn+1).
// Selected via custom ISel in VAXISelDAGToDAG (builds REG_SEQUENCE for QPR).
def SDT_VAXASHQ : SDTypeProfile<2, 3,
    [SDTCisVT<0, i32>, SDTCisVT<1, i32>,   // results: lo, hi
     SDTCisVT<2, i32>,                       // cnt
     SDTCisVT<3, i32>, SDTCisVT<4, i32>]>;  // src: lo, hi
def vax_ashq : SDNode<"VAXISD::ASHQ", SDT_VAXASHQ>;

let Defs = [PSW] in {
let Constraints = "@earlyclobber $dst" in {
def ASHQ : InstVAX<0x79, (outs QPR:$dst),
    (ins GPRnoPC:$cnt, QPR:$src),
    "ashq\t$cnt, $src, $dst", []>,
    Sched<[WriteShiftQ]>;
def ASHQ_i : InstVAX<0x79, (outs QPR:$dst),
    (ins i32imm:$cnt, QPR:$src),
    "ashq\t$cnt, $src, $dst", []>,
    Sched<[WriteShiftQ]>;
}
}

// EMUL (0x7A) — Extended Multiply: result = mulr * muld + sign_extend(add)
// Produces 64-bit result in dst (quadword).
// Selected via custom ISel in VAXISelDAGToDAG (extracts QPR subregs).
def SDT_VAXEMUL : SDTypeProfile<2, 3,
    [SDTCisVT<0, i32>, SDTCisVT<1, i32>,   // results: lo, hi
     SDTCisVT<2, i32>, SDTCisVT<3, i32>,   // mulr, muld
     SDTCisVT<4, i32>]>;                    // addend
def vax_emul : SDNode<"VAXISD::EMUL", SDT_VAXEMUL>;

let Defs = [PSW], Constraints = "@earlyclobber $dst" in
def EMUL : InstVAX<0x7A, (outs QPR:$dst),
    (ins GPRnoPC:$mulr, GPRnoPC:$muld, i32imm:$add),
    "emul\t$mulr, $muld, $add, $dst", []>,
    Sched<[WriteEMUL]>;

// EDIV (0x7B) — Extended Divide: dividend_quad / divisor → quotient, remainder
// Selected via custom ISel in VAXISelDAGToDAG (builds QPR from i32 halves).
def SDT_VAXEDIV : SDTypeProfile<2, 3,
    [SDTCisVT<0, i32>, SDTCisVT<1, i32>,   // results: quotient, remainder
     SDTCisVT<2, i32>,                       // divisor
     SDTCisVT<3, i32>, SDTCisVT<4, i32>]>;  // dividend: lo, hi
def vax_ediv : SDNode<"VAXISD::EDIV", SDT_VAXEDIV>;

let Defs = [PSW], Constraints = "@earlyclobber $quo,@earlyclobber $rem" in
def EDIV : InstVAX<0x7B, (outs GPRnoPC:$quo, GPRnoPC:$rem),
    (ins GPRnoPC:$divisor, QPR:$dividend),
    "ediv\t$divisor, $dividend, $quo, $rem", []>,
    Sched<[WriteEDIV]>;

//===----------------------------------------------------------------------===//
// Phase 14B — F_float (32-bit floating point)
//===----------------------------------------------------------------------===//

// F_float memory operand (same layout as integer VAXMemOp).
def VAXFMemOp : Operand<iPTR> {
  let PrintMethod   = "printMemOperand";
  let MIOperandInfo = (ops GPR, i32imm, GPR, i32imm);
  let ParserMatchClass = VAXMemAsmOperand;
}

// All F_float data instructions set PSW condition codes.
let Defs = [PSW] in {
// MOVF (0x50) — F_float data moves (load/store/copy).
defm MOVF : VAXFpMov<0x50, "movf", GPRI, f32, VAXFMemOp>;

// CLRF (0xD4) — Clear F_float: dst = 0.0.
def CLRF : InstVAX<0xD4, (outs GPRI:$dst), (ins),
    "clrf\t$dst", []>;

// ADDF2 (0x40) — Add F_float 2-op: dst += src.
def ADDF2_rr : InstVAX<0x40, (outs GPRI:$dst), (ins GPRI:$s, GPRI:$src),
    "addf2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
// SUBF2 (0x42) — Subtract F_float 2-op: dst -= src.
def SUBF2_rr : InstVAX<0x42, (outs GPRI:$dst), (ins GPRI:$s, GPRI:$src),
    "subf2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
// MULF2 (0x44) — Multiply F_float 2-op: dst *= src.
def MULF2_rr : InstVAX<0x44, (outs GPRI:$dst), (ins GPRI:$s, GPRI:$src),
    "mulf2\t$s, $dst", []>, Sched<[WriteFPALU]> {
  let Constraints = "$src = $dst";
}
// DIVF2 (0x46) — Divide F_float 2-op: dst /= src.
def DIVF2_rr : InstVAX<0x46, (outs GPRI:$dst), (ins GPRI:$s, GPRI:$src),
    "divf2\t$s, $dst", []>, Sched<[WriteFPDIV]> {
  let Constraints = "$src = $dst";
}

// ADDF3 (0x41) — Add F_float: dst = src1 + src2.
def ADDF3_rr : InstVAX<0x41, (outs GPRI:$dst), (ins GPRI:$a, GPRI:$b),
    "addf3\t$a, $b, $dst",
    [(set GPRI:$dst, (fadd GPRI:$a, GPRI:$b))]>;
// SUBF3 (0x43) — Subtract F_float: dst = src2 - src1.
def SUBF3_rr : InstVAX<0x43, (outs GPRI:$dst), (ins GPRI:$s, GPRI:$m),
    "subf3\t$s, $m, $dst",
    [(set GPRI:$dst, (fsub GPRI:$m, GPRI:$s))]>;
// MULF3 (0x45) — Multiply F_float: dst = src1 * src2.
def MULF3_rr : InstVAX<0x45, (outs GPRI:$dst), (ins GPRI:$a, GPRI:$b),
    "mulf3\t$a, $b, $dst",
    [(set GPRI:$dst, (fmul GPRI:$a, GPRI:$b))]>, Sched<[WriteFPALU]>;
// DIVF3 (0x47) — Divide F_float: dst = src2 / src1.
def DIVF3_rr : InstVAX<0x47, (outs GPRI:$dst), (ins GPRI:$dv, GPRI:$dd),
    "divf3\t$dv, $dd, $dst",
    [(set GPRI:$dst, (fdiv GPRI:$dd, GPRI:$dv))]>, Sched<[WriteFPDIV]>;

// MNEGF (0x52) — Negate F_float: dst = -src.
def MNEGF : InstVAX<0x52, (outs GPRI:$dst), (ins GPRI:$src),
    "mnegf\t$src, $dst",
    [(set GPRI:$dst, (fneg GPRI:$src))]>;

// CVTFL (0x4A) — Convert F_float to Longword (float→int truncation).
def CVTFL : InstVAX<0x4A, (outs GPRnoPC:$dst), (ins GPRI:$src),
    "cvtfl\t$src, $dst",
    [(set GPRnoPC:$dst, (fp_to_sint GPRI:$src))]>;
// CVTLF (0x4E) — Convert Longword to F_float (int→float).
def CVTLF : InstVAX<0x4E, (outs GPRI:$dst), (ins GPRnoPC:$src),
    "cvtlf\t$src, $dst",
    [(set GPRI:$dst, (sint_to_fp GPRnoPC:$src))]>;
} // let Defs = [PSW] — end of F_float data block

// FP compare: CMPF sets PSW, used by FP branch/select via VAXISD::FCMP.
def SDT_VAXFCMP : SDTypeProfile<0, 2, [SDTCisSameAs<0,1>, SDTCisFP<0>]>;
def vax_fcmp : SDNode<"VAXISD::FCMP", SDT_VAXFCMP, [SDNPOutGlue]>;

// FP zero immediate — used for TSTF/TSTD compare-to-zero folding.
def fpimm_zero : PatLeaf<(fpimm), [{return N->isExactlyValue(0.0);}]>;

let Defs = [PSW] in {
def CMPF : InstVAX<0x51, (outs), (ins GPRI:$a, GPRI:$b),
    "cmpf\t$a, $b",
    [(vax_fcmp GPRI:$a, GPRI:$b)]>;
// TSTF (0x53) — Test F_float: sets PSW from src vs 0.0.
def TSTF : InstVAX<0x53, (outs), (ins GPRI:$a),
    "tstf\t$a",
    [(vax_fcmp GPRI:$a, (f32 fpimm_zero))]>;
}

// SELECT_CC for f32 results (same diamond expansion as i32).
let usesCustomInserter = 1 in
def SELECT_CC_F_Pseudo : Pseudo<(outs GPRI:$dst),
    (ins GPRI:$truev, GPRI:$falsev, i32imm:$cc),
    "#SELECT_CC_F $truev, $falsev, $cc",
    [(set (f32 GPRI:$dst), (vax_select_cc (f32 GPRI:$truev),
                                           (f32 GPRI:$falsev),
                                           (i32 imm:$cc)))]>;

//===----------------------------------------------------------------------===//
// Phase 14C — D_float (64-bit floating point)
//===----------------------------------------------------------------------===//

// D_float memory operand (same layout as integer, but 8-byte load).
def VAXDMemOp : Operand<iPTR> {
  let PrintMethod   = "printMemOperand";
  let MIOperandInfo = (ops GPR, i32imm, GPR, i32imm);
  let ParserMatchClass = VAXMemAsmOperand;
}

// All D_float data instructions set PSW condition codes.
let Defs = [PSW] in {
// MOVD (0x70) — D_float data moves (load/store/copy) using register pairs.
defm MOVD : VAXFpMov<0x70, "movd", QPR, f64, VAXDMemOp>;

// CLRD (0x7C) — Clear D_float: dst = 0.0.
def CLRD : InstVAX<0x7C, (outs QPR:$dst), (ins),
    "clrd\t$dst", []>;

// ADDD2 (0x60) — Add D_float 2-op: dst += src.
def ADDD2_rr : InstVAX<0x60, (outs QPR:$dst), (ins QPR:$s, QPR:$src),
    "addd2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
// SUBD2 (0x62) — Subtract D_float 2-op: dst -= src.
def SUBD2_rr : InstVAX<0x62, (outs QPR:$dst), (ins QPR:$s, QPR:$src),
    "subd2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
// MULD2 (0x64) — Multiply D_float 2-op: dst *= src.
def MULD2_rr : InstVAX<0x64, (outs QPR:$dst), (ins QPR:$s, QPR:$src),
    "muld2\t$s, $dst", []>, Sched<[WriteFPDALU]> {
  let Constraints = "$src = $dst";
}
// DIVD2 (0x66) — Divide D_float 2-op: dst /= src.
def DIVD2_rr : InstVAX<0x66, (outs QPR:$dst), (ins QPR:$s, QPR:$src),
    "divd2\t$s, $dst", []>, Sched<[WriteFPDDIV]> {
  let Constraints = "$src = $dst";
}

// ADDD3 (0x61) — Add D_float: dst = src1 + src2.
def ADDD3_rr : InstVAX<0x61, (outs QPR:$dst), (ins QPR:$a, QPR:$b),
    "addd3\t$a, $b, $dst",
    [(set QPR:$dst, (fadd QPR:$a, QPR:$b))]>;
// SUBD3 (0x63) — Subtract D_float: dst = src2 - src1.
def SUBD3_rr : InstVAX<0x63, (outs QPR:$dst), (ins QPR:$s, QPR:$m),
    "subd3\t$s, $m, $dst",
    [(set QPR:$dst, (fsub QPR:$m, QPR:$s))]>;
// MULD3 (0x65) — Multiply D_float: dst = src1 * src2.
def MULD3_rr : InstVAX<0x65, (outs QPR:$dst), (ins QPR:$a, QPR:$b),
    "muld3\t$a, $b, $dst",
    [(set QPR:$dst, (fmul QPR:$a, QPR:$b))]>, Sched<[WriteFPDALU]>;
// DIVD3 (0x67) — Divide D_float: dst = src2 / src1.
def DIVD3_rr : InstVAX<0x67, (outs QPR:$dst), (ins QPR:$dv, QPR:$dd),
    "divd3\t$dv, $dd, $dst",
    [(set QPR:$dst, (fdiv QPR:$dd, QPR:$dv))]>, Sched<[WriteFPDDIV]>;

// MNEGD (0x72) — Negate D_float: dst = -src.
def MNEGD : InstVAX<0x72, (outs QPR:$dst), (ins QPR:$src),
    "mnegd\t$src, $dst",
    [(set QPR:$dst, (fneg QPR:$src))]>;

// CVTDL (0x6A) — Convert D_float to Longword (double→int).
def CVTDL : InstVAX<0x6A, (outs GPRnoPC:$dst), (ins QPR:$src),
    "cvtdl\t$src, $dst",
    [(set GPRnoPC:$dst, (fp_to_sint QPR:$src))]>;
// CVTLD (0x6E) — Convert Longword to D_float (int→double).
def CVTLD : InstVAX<0x6E, (outs QPR:$dst), (ins GPRnoPC:$src),
    "cvtld\t$src, $dst",
    [(set QPR:$dst, (sint_to_fp GPRnoPC:$src))]>;
// CVTDF (0x76) — Convert D_float to F_float (double→float, FP_ROUND).
def CVTDF : InstVAX<0x76, (outs GPRI:$dst), (ins QPR:$src),
    "cvtdf\t$src, $dst",
    [(set GPRI:$dst, (fpround QPR:$src))]>;
// CVTFD (0x56) — Convert F_float to D_float (float→double, FP_EXTEND).
def CVTFD : InstVAX<0x56, (outs QPR:$dst), (ins GPRI:$src),
    "cvtfd\t$src, $dst",
    [(set QPR:$dst, (fpextend GPRI:$src))]>;
} // let Defs = [PSW] — end of D_float data block

// D_float compare: CMPD sets PSW.
let Defs = [PSW] in {
def CMPD : InstVAX<0x71, (outs), (ins QPR:$a, QPR:$b),
    "cmpd\t$a, $b",
    [(vax_fcmp QPR:$a, QPR:$b)]>;
// TSTD (0x73) — Test D_float: sets PSW from src vs 0.0.
def TSTD : InstVAX<0x73, (outs), (ins QPR:$a),
    "tstd\t$a",
    [(vax_fcmp QPR:$a, (f64 fpimm_zero))]>;
}

// SELECT_CC for f64 results.
let usesCustomInserter = 1 in
def SELECT_CC_D_Pseudo : Pseudo<(outs QPR:$dst),
    (ins QPR:$truev, QPR:$falsev, i32imm:$cc),
    "#SELECT_CC_D $truev, $falsev, $cc",
    [(set (f64 QPR:$dst), (vax_select_cc (f64 QPR:$truev),
                                          (f64 QPR:$falsev),
                                          (i32 imm:$cc)))]>;

//===----------------------------------------------------------------------===//
// MC-only instructions — used exclusively in inline assembly (kernel headers).
// No ISel patterns; only need AsmParser + MCCodeEmitter support.
//===----------------------------------------------------------------------===//

// HALT (0x00): stop processor execution.
let isBarrier = 1, isTerminator = 1 in
def HALT : InstVAX<0x00, (outs), (ins), "halt", []>;

let HasMemOp = 1 in {

// MOVPSL (0xDC): move processor status longword to destination.
def MOVPSL : InstVAX<0xDC, (outs), (ins VAXMemOp:$dst),
    "movpsl\t$dst", []>;

// MTPR (0xDA): move to processor register. src.rl, procreg.rl
def MTPR : InstVAX<0xDA, (outs), (ins VAXMemOp:$src, VAXMemOp:$procreg),
    "mtpr\t$src, $procreg", []>;

// MFPR (0xDB): move from processor register. procreg.rl, dst.wl
def MFPR : InstVAX<0xDB, (outs), (ins VAXMemOp:$procreg, VAXMemOp:$dst),
    "mfpr\t$procreg, $dst", []>;

// FFS (0xEA): find first set bit. startpos.rl, size.rb, base.rl, dst.wl
let Defs = [PSW] in
def FFS : InstVAX<0xEA, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$size, VAXMemOp:$base, VAXMemOp:$dst),
    "ffs\t$pos, $size, $base, $dst", []>;

// FFC (0xEB): find first clear bit. startpos.rl, size.rb, base.rl, dst.wl
let Defs = [PSW] in
def FFC : InstVAX<0xEB, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$size, VAXMemOp:$base, VAXMemOp:$dst),
    "ffc\t$pos, $size, $base, $dst", []>;

// INSQUE (0x0E): insert entry into queue. entry.ab, pred.ab
let Defs = [PSW] in
def INSQUE : InstVAX<0x0E, (outs), (ins VAXMemOp:$entry, VAXMemOp:$pred),
    "insque\t$entry, $pred", []>;

// REMQUE (0x0F): remove entry from queue. entry.ab, addr.wl
let Defs = [PSW] in
def REMQUE : InstVAX<0x0F, (outs), (ins VAXMemOp:$entry, VAXMemOp:$addr),
    "remque\t$entry, $addr", []>;

// MOVC3 (0x28): move character 3-operand. len.rw, srcaddr.ab, dstaddr.ab
let Defs = [R0, R1, R2, R3, R4, R5, PSW], MemOpWidth0 = 2 in
def MOVC3 : InstVAX<0x28, (outs),
    (ins VAXMemOp:$len, VAXMemOp:$src, VAXMemOp:$dst),
    "movc3\t$len, $src, $dst", []>;

// MOVC5 (0x2C): move character 5-operand.
// srclen.rw, srcaddr.ab, fill.rb, dstlen.rw, dstaddr.ab
let Defs = [R0, R1, R2, R3, R4, R5, PSW], MemOpWidth0 = 2, MemOpWidth2 = 1, MemOpWidth3 = 2 in
def MOVC5 : InstVAX<0x2C, (outs),
    (ins VAXMemOp:$srclen, VAXMemOp:$srcaddr, VAXMemOp:$fill,
         VAXMemOp:$dstlen, VAXMemOp:$dstaddr),
    "movc5\t$srclen, $srcaddr, $fill, $dstlen, $dstaddr", []>;

// SCANC (0x2A): scan characters. len.rw, addr.ab, tbladdr.ab, mask.rb
let Defs = [R0, R1, R2, R3, PSW], MemOpWidth0 = 2, MemOpWidth3 = 1 in
def SCANC : InstVAX<0x2A, (outs),
    (ins VAXMemOp:$len, VAXMemOp:$addr, VAXMemOp:$tbl, VAXMemOp:$mask),
    "scanc\t$len, $addr, $tbl, $mask", []>;

// LOCC (0x3A): locate character. char.rb, len.rw, addr.ab
let Defs = [R0, R1, PSW], MemOpWidth0 = 1, MemOpWidth1 = 2 in
def LOCC : InstVAX<0x3A, (outs),
    (ins VAXMemOp:$char, VAXMemOp:$len, VAXMemOp:$addr),
    "locc\t$char, $len, $addr", []>;

// SKPC (0x3B): skip character. char.rb, len.rw, addr.ab
let Defs = [R0, R1, PSW], MemOpWidth0 = 1, MemOpWidth1 = 2 in
def SKPC : InstVAX<0x3B, (outs),
    (ins VAXMemOp:$char, VAXMemOp:$len, VAXMemOp:$addr),
    "skpc\t$char, $len, $addr", []>;

} // let HasMemOp = 1

// REI (0x02): return from exception/interrupt. No operands.
let isReturn = 1, isBarrier = 1, isTerminator = 1 in
def REI : InstVAX<0x02, (outs), (ins), "rei", []>;

let HasMemOp = 1 in {

// ASHL (0x78): arithmetic shift long. cnt.rb, src.rl, dst.wl
let Defs = [PSW], MemOpWidth0 = 1 in
def ASHL : InstVAX<0x78, (outs),
    (ins VAXMemOp:$cnt, VAXMemOp:$src, VAXMemOp:$dst),
    "ashl\t$cnt, $src, $dst", []>;

// MOVAB (0x9E): move address byte. src.ab, dst.wl
// MOVAx instructions set N/Z based on address value and clear V; C unchanged.
let Defs = [PSW], HasMemOp = 1 in
def MOVAB : InstVAX<0x9E, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movab\t$src, $dst", []>;

// MOVABL (0x9E): move address byte — alias for MOVAB
// (GAS accepts both movab and movabl)

// MOVAW (0x3E): move address word. src.aw, dst.wl
let Defs = [PSW], HasMemOp = 1 in
def MOVAW : InstVAX<0x3E, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movaw\t$src, $dst", []>;

// MOVAL (0xDE): move address long. src.al, dst.wl
let Defs = [PSW], HasMemOp = 1 in
def MOVAL : InstVAX<0xDE, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dst),
    "moval\t$src, $dst", []>;

// MOVAQ (0x7E): move address quad. src.aq, dst.wl
let Defs = [PSW], HasMemOp = 1 in
def MOVAQ : InstVAX<0x7E, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movaq\t$src, $dst", []>;

// MOVQ (0x7D): move quadword (64-bit integer, no float validation).
// Unlike MOVD, MOVQ does NOT check for reserved floating-point operands.
// Use MOVQ for i64 data and register spills; MOVD only for actual f64 values.
let Defs = [PSW], HasMemOp = 1 in
def MOVQ : InstVAX<0x7D, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movq\t$src, $dst", []>;
// MOVQ register-register (QPR copy for i64 / register allocation).
let Defs = [PSW] in
def MOVQ_rr : InstVAX<0x7D, (outs QPR:$dst), (ins QPR:$src),
    "movq\t$src, $dst", []>,
    Sched<[WriteMOV]>;
// MOVQ register-from-memory (QPR reload from stack slot).
let Defs = [PSW], HasMemOp = 1 in
def MOVQ_rm : InstVAX<0x7D, (outs QPR:$dst), (ins VAXDMemOp:$src),
    "movq\t$src, $dst", []>,
    Sched<[WriteMOV]>;
// MOVQ memory-from-register (QPR spill to stack slot).
let Defs = [PSW], HasMemOp = 1 in
def MOVQ_mr : InstVAX<0x7D, (outs), (ins QPR:$src, VAXDMemOp:$dst),
    "movq\t$src, $dst", []>,
    Sched<[WriteMOV]>;

// EXTZV (0xEF): extract zero-extended field. pos.rl, size.rb, base.rl, dst.wl
let Defs = [PSW], MemOpWidth1 = 1 in
def EXTZV : InstVAX<0xEF, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$size, VAXMemOp:$base, VAXMemOp:$dst),
    "extzv\t$pos, $size, $base, $dst", []>;

// INSV (0xF0): insert field. src.rl, pos.rl, size.rb, base.rl
let Defs = [PSW], MemOpWidth2 = 1 in
def INSV : InstVAX<0xF0, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$pos, VAXMemOp:$size, VAXMemOp:$base),
    "insv\t$src, $pos, $size, $base", []>;

// SOBGTR (0xF5): subtract one and branch if greater. index.ml, disp.bb
// The index operand is modified (read-write). disp is a branch displacement.
let Defs = [PSW] in
def SOBGTR : InstVAX<0xF5, (outs),
    (ins VAXMemOp:$index, VAXMemOp:$disp),
    "sobgtr\t$index, $disp", []>;

} // let HasMemOp = 1

// Assembly-only MemOp forms of 2-operand instructions (for hand-written .S).
// These allow memory operands in positions that codegen uses registers.
let HasMemOp = 1 in {

// SUBL2 with memory destination (e.g., subl2 %r0, 12(%ap) in memcpy.S)
let Defs = [PSW] in
def SUBL2 : InstVAX<0xC2, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dst),
    "subl2\t$src, $dst", []>;

// ADDL2 with memory operands
let Defs = [PSW] in
def ADDL2 : InstVAX<0xC0, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dst),
    "addl2\t$src, $dst", []>;

// DECW (0xB7): decrement word
let Defs = [PSW], MemOpWidth0 = 2 in
def DECW : InstVAX<0xB7, (outs),
    (ins VAXMemOp:$dst),
    "decw\t$dst", []>;

// DECB (0x97): decrement byte
let Defs = [PSW], MemOpWidth0 = 1 in
def DECB : InstVAX<0x97, (outs),
    (ins VAXMemOp:$dst),
    "decb\t$dst", []>;

// INCW (0xB6): increment word
let Defs = [PSW], MemOpWidth0 = 2 in
def INCW : InstVAX<0xB6, (outs),
    (ins VAXMemOp:$dst),
    "incw\t$dst", []>;

// INCB (0x96): increment byte
let Defs = [PSW], MemOpWidth0 = 1 in
def INCB : InstVAX<0x96, (outs),
    (ins VAXMemOp:$dst),
    "incb\t$dst", []>;

// DIVL2 (0xC6): divide longword 2-operand. divisor.rl, dividend.ml
let Defs = [PSW] in
def DIVL2 : InstVAX<0xC6, (outs),
    (ins VAXMemOp:$divisor, VAXMemOp:$dividend),
    "divl2\t$divisor, $dividend", []>, Sched<[WriteDIVL]>;

// MULL2 (0xC4): multiply longword 2-operand. mulr.rl, prod.ml
let Defs = [PSW] in
def MULL2 : InstVAX<0xC4, (outs),
    (ins VAXMemOp:$mulr, VAXMemOp:$prod),
    "mull2\t$mulr, $prod", []>, Sched<[WriteMULL]>;

// SUBL3 with MemOp operands (e.g., subl3 %r2, 4(%ap), %r0)
let Defs = [PSW] in
def SUBL3 : InstVAX<0xC3, (outs),
    (ins VAXMemOp:$s, VAXMemOp:$m, VAXMemOp:$dst),
    "subl3\t$s, $m, $dst", []>;

// ADDL3 with MemOp operands
let Defs = [PSW] in
def ADDL3 : InstVAX<0xC1, (outs),
    (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "addl3\t$a, $b, $dst", []>;

// CMPL with memory operands (e.g., cmpl 4(%ap), %r2 in udiv.S)
let Defs = [PSW] in
def CMPL : InstVAX<0xD1, (outs),
    (ins VAXMemOp:$a, VAXMemOp:$b),
    "cmpl\t$a, $b", []>;

// CLRL with memory operand (e.g., clrl (%r3) in udiv.S)
let Defs = [PSW] in
def CLRL_m : InstVAX<0xD4, (outs),
    (ins VAXMemOp:$dst),
    "clrl\t$dst", []>;

// CLRB (0x94): clear byte
let Defs = [PSW], MemOpWidth0 = 1 in
def CLRB : InstVAX<0x94, (outs),
    (ins VAXMemOp:$dst),
    "clrb\t$dst", []>;

// CLRW (0xB4): clear word
let Defs = [PSW], MemOpWidth0 = 2 in
def CLRW : InstVAX<0xB4, (outs),
    (ins VAXMemOp:$dst),
    "clrw\t$dst", []>;

// CLRQ (0x7C): clear quadword
let Defs = [PSW] in
def CLRQ : InstVAX<0x7C, (outs),
    (ins VAXMemOp:$dst),
    "clrq\t$dst", []>;

// EMUL (0x7A): extended multiply with MemOp (used by GMP inline asm)
let Defs = [PSW] in
def EMUL_m : InstVAX<0x7A, (outs),
    (ins VAXMemOp:$mulr, VAXMemOp:$muld, VAXMemOp:$add, VAXMemOp:$dst),
    "emul\t$mulr, $muld, $add, $dst", []>;

// EDIV (0x7B): extended divide with MemOp
let Defs = [PSW] in
def EDIV_m : InstVAX<0x7B, (outs),
    (ins VAXMemOp:$divisor, VAXMemOp:$dividend, VAXMemOp:$quo, VAXMemOp:$rem),
    "ediv\t$divisor, $dividend, $quo, $rem", []>;

//===----------------------------------------------------------------------===//
// Phase 35 — MC-layer completeness: branch-on-bit, loops, case, subroutine,
// stack, string, privileged, bit-test, and miscellaneous instructions.
//===----------------------------------------------------------------------===//

// --- Branch-on-bit instructions ---

// BBS (0xE0): branch on bit set. pos.rl, base.rl, disp.bb
let Defs = [PSW] in
def BBS : InstVAX<0xE0, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$base, VAXMemOp:$disp),
    "bbs\t$pos, $base, $disp", []>;

// BBC (0xE1): branch on bit clear. pos.rl, base.rl, disp.bb
let Defs = [PSW] in
def BBC : InstVAX<0xE1, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$base, VAXMemOp:$disp),
    "bbc\t$pos, $base, $disp", []>;

// BBSS (0xE2): branch on bit set and set. pos.rl, base.rl, disp.bb
let Defs = [PSW] in
def BBSS : InstVAX<0xE2, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$base, VAXMemOp:$disp),
    "bbss\t$pos, $base, $disp", []>;

// BBCS (0xE3): branch on bit clear and set. pos.rl, base.rl, disp.bb
let Defs = [PSW] in
def BBCS : InstVAX<0xE3, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$base, VAXMemOp:$disp),
    "bbcs\t$pos, $base, $disp", []>;

// BBSC (0xE4): branch on bit set and clear. pos.rl, base.rl, disp.bb
let Defs = [PSW] in
def BBSC : InstVAX<0xE4, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$base, VAXMemOp:$disp),
    "bbsc\t$pos, $base, $disp", []>;

// BBCC (0xE5): branch on bit clear and clear. pos.rl, base.rl, disp.bb
let Defs = [PSW] in
def BBCC : InstVAX<0xE5, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$base, VAXMemOp:$disp),
    "bbcc\t$pos, $base, $disp", []>;

// BBSSI (0xE6): branch on bit set and set interlocked. pos.rl, base.rl, disp.bb
let Defs = [PSW] in
def BBSSI : InstVAX<0xE6, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$base, VAXMemOp:$disp),
    "bbssi\t$pos, $base, $disp", []>;

// BBCCI (0xE7): branch on bit clear and clear interlocked. pos.rl, base.rl, disp.bb
let Defs = [PSW] in
def BBCCI : InstVAX<0xE7, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$base, VAXMemOp:$disp),
    "bbcci\t$pos, $base, $disp", []>;

// BLBS (0xE8): branch on low bit set. src.rl, disp.bb
def BLBS : InstVAX<0xE8, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$disp),
    "blbs\t$src, $disp", []>;

// BLBC (0xE9): branch on low bit clear. src.rl, disp.bb
def BLBC : InstVAX<0xE9, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$disp),
    "blbc\t$src, $disp", []>;

// --- Bit test instructions (test bits, set PSW, no branch) ---

// BITL (0xD3): bit test longword. mask.rl, src.rl — sets NZV from AND
let Defs = [PSW] in
def BITL : InstVAX<0xD3, (outs),
    (ins VAXMemOp:$mask, VAXMemOp:$src),
    "bitl\t$mask, $src", []>;

// BITW (0xB3): bit test word. mask.rw, src.rw
let Defs = [PSW], MemOpWidth0 = 2, MemOpWidth1 = 2 in
def BITW : InstVAX<0xB3, (outs),
    (ins VAXMemOp:$mask, VAXMemOp:$src),
    "bitw\t$mask, $src", []>;

// BITB (0x93): bit test byte. mask.rb, src.rb
let Defs = [PSW], MemOpWidth0 = 1, MemOpWidth1 = 1 in
def BITB : InstVAX<0x93, (outs),
    (ins VAXMemOp:$mask, VAXMemOp:$src),
    "bitb\t$mask, $src", []>;

// --- Loop instructions ---

// SOBGEQ (0xF4): subtract one and branch if >= 0. index.ml, disp.bb
let Defs = [PSW] in
def SOBGEQ : InstVAX<0xF4, (outs),
    (ins VAXMemOp:$index, VAXMemOp:$disp),
    "sobgeq\t$index, $disp", []>;

// AOBLEQ (0xF3): add one and branch if <= limit. limit.rl, index.ml, disp.bb
let Defs = [PSW] in
def AOBLEQ : InstVAX<0xF3, (outs),
    (ins VAXMemOp:$limit, VAXMemOp:$index, VAXMemOp:$disp),
    "aobleq\t$limit, $index, $disp", []>;

// AOBLSS (0xF2): add one and branch if < limit. limit.rl, index.ml, disp.bb
let Defs = [PSW] in
def AOBLSS : InstVAX<0xF2, (outs),
    (ins VAXMemOp:$limit, VAXMemOp:$index, VAXMemOp:$disp),
    "aoblss\t$limit, $index, $disp", []>;

// ACBL (0xF1): add compare and branch longword.
// limit.rl, add.rl, index.ml, disp.bw
let Defs = [PSW] in
def ACBL : InstVAX<0xF1, (outs),
    (ins VAXMemOp:$limit, VAXMemOp:$add, VAXMemOp:$index, VAXMemOp:$disp),
    "acbl\t$limit, $add, $index, $disp", []>;

// ACBW (0x3D): add compare and branch word.
// limit.rw, add.rw, index.mw, disp.bw
let Defs = [PSW], MemOpWidth0 = 2, MemOpWidth1 = 2, MemOpWidth2 = 2 in
def ACBW : InstVAX<0x3D, (outs),
    (ins VAXMemOp:$limit, VAXMemOp:$add, VAXMemOp:$index, VAXMemOp:$disp),
    "acbw\t$limit, $add, $index, $disp", []>;

// ACBB (0x9D): add compare and branch byte.
// limit.rb, add.rb, index.mb, disp.bw
let Defs = [PSW], MemOpWidth0 = 1, MemOpWidth1 = 1, MemOpWidth2 = 1 in
def ACBB : InstVAX<0x9D, (outs),
    (ins VAXMemOp:$limit, VAXMemOp:$add, VAXMemOp:$index, VAXMemOp:$disp),
    "acbb\t$limit, $add, $index, $disp", []>;

// ACBF (0x4F): add compare and branch F_float.
// limit.rf, add.rf, index.mf, disp.bw
let Defs = [PSW] in
def ACBF : InstVAX<0x4F, (outs),
    (ins VAXMemOp:$limit, VAXMemOp:$add, VAXMemOp:$index, VAXMemOp:$disp),
    "acbf\t$limit, $add, $index, $disp", []>;

// ACBD (0x6F): add compare and branch D_float.
// limit.rd, add.rd, index.md, disp.bw
let Defs = [PSW] in
def ACBD : InstVAX<0x6F, (outs),
    (ins VAXMemOp:$limit, VAXMemOp:$add, VAXMemOp:$index, VAXMemOp:$disp),
    "acbd\t$limit, $add, $index, $disp", []>;

// --- Case instructions ---

// CASEB (0x8F): case byte. selector.rb, base.rb, limit.rb
let Defs = [PSW], MemOpWidth0 = 1, MemOpWidth1 = 1, MemOpWidth2 = 1 in
def CASEB : InstVAX<0x8F, (outs),
    (ins VAXMemOp:$sel, VAXMemOp:$base, VAXMemOp:$limit),
    "caseb\t$sel, $base, $limit", []>;

// CASEW (0xAF): case word. selector.rw, base.rw, limit.rw
let Defs = [PSW], MemOpWidth0 = 2, MemOpWidth1 = 2, MemOpWidth2 = 2 in
def CASEW : InstVAX<0xAF, (outs),
    (ins VAXMemOp:$sel, VAXMemOp:$base, VAXMemOp:$limit),
    "casew\t$sel, $base, $limit", []>;

// --- Subroutine instructions ---

// CALLG (0xFA): call with general argument list. arglist.ab, dst.ab
def CALLG : InstVAX<0xFA, (outs),
    (ins VAXMemOp:$arglist, VAXMemOp:$dst),
    "callg\t$arglist, $dst", []>;

// --- Stack save/restore ---

// PUSHR (0xBB): push registers. mask.rw
let MemOpWidth0 = 2 in
def PUSHR : InstVAX<0xBB, (outs),
    (ins VAXMemOp:$mask),
    "pushr\t$mask", []>;

// POPR (0xBA): pop registers. mask.rw
let MemOpWidth0 = 2 in
def POPR : InstVAX<0xBA, (outs),
    (ins VAXMemOp:$mask),
    "popr\t$mask", []>;

// --- String instructions ---

// CMPC3 (0x29): compare characters 3-operand. len.rw, addr1.ab, addr2.ab
let Defs = [R0, R1, R2, R3, PSW], MemOpWidth0 = 2 in
def CMPC3 : InstVAX<0x29, (outs),
    (ins VAXMemOp:$len, VAXMemOp:$addr1, VAXMemOp:$addr2),
    "cmpc3\t$len, $addr1, $addr2", []>;

// CMPC5 (0x2D): compare characters 5-operand.
// len1.rw, addr1.ab, fill.rb, len2.rw, addr2.ab
let Defs = [R0, R1, R2, R3, PSW], MemOpWidth0 = 2, MemOpWidth2 = 1, MemOpWidth3 = 2 in
def CMPC5 : InstVAX<0x2D, (outs),
    (ins VAXMemOp:$len1, VAXMemOp:$addr1, VAXMemOp:$fill,
         VAXMemOp:$len2, VAXMemOp:$addr2),
    "cmpc5\t$len1, $addr1, $fill, $len2, $addr2", []>;

// MOVTC (0x2E): move translated characters.
// srclen.rw, srcaddr.ab, fill.rb, tbladdr.ab, dstlen.rw, dstaddr.ab
let Defs = [R0, R1, R2, R3, R4, R5, PSW], MemOpWidth0 = 2, MemOpWidth2 = 1, MemOpWidth4 = 2 in
def MOVTC : InstVAX<0x2E, (outs),
    (ins VAXMemOp:$srclen, VAXMemOp:$srcaddr, VAXMemOp:$fill,
         VAXMemOp:$tbl, VAXMemOp:$dstlen, VAXMemOp:$dstaddr),
    "movtc\t$srclen, $srcaddr, $fill, $tbl, $dstlen, $dstaddr", []>;

// MOVTUC (0x2F): move translated until character.
// srclen.rw, srcaddr.ab, esc.rb, tbladdr.ab, dstlen.rw, dstaddr.ab
let Defs = [R0, R1, R2, R3, R4, R5, PSW], MemOpWidth0 = 2, MemOpWidth2 = 1, MemOpWidth4 = 2 in
def MOVTUC : InstVAX<0x2F, (outs),
    (ins VAXMemOp:$srclen, VAXMemOp:$srcaddr, VAXMemOp:$esc,
         VAXMemOp:$tbl, VAXMemOp:$dstlen, VAXMemOp:$dstaddr),
    "movtuc\t$srclen, $srcaddr, $esc, $tbl, $dstlen, $dstaddr", []>;

// CRC (0x0B): compute cyclic redundancy check.
// tbl.ab, inicrc.rl, strlen.rw, stream.ab
let Defs = [R0, R1, R2, R3, PSW], MemOpWidth2 = 2 in
def CRC : InstVAX<0x0B, (outs),
    (ins VAXMemOp:$tbl, VAXMemOp:$inicrc, VAXMemOp:$strlen, VAXMemOp:$stream),
    "crc\t$tbl, $inicrc, $strlen, $stream", []>;

// MATCHC (0x39): match characters. objlen.rw, objaddr.ab, srclen.rw, srcaddr.ab
let Defs = [R0, R1, R2, R3, PSW], MemOpWidth0 = 2, MemOpWidth2 = 2 in
def MATCHC : InstVAX<0x39, (outs),
    (ins VAXMemOp:$objlen, VAXMemOp:$objaddr, VAXMemOp:$srclen, VAXMemOp:$srcaddr),
    "matchc\t$objlen, $objaddr, $srclen, $srcaddr", []>;

// SPANC (0x2B): span characters. len.rw, addr.ab, tbladdr.ab, mask.rb
let Defs = [R0, R1, R2, R3, PSW], MemOpWidth0 = 2, MemOpWidth3 = 1 in
def SPANC : InstVAX<0x2B, (outs),
    (ins VAXMemOp:$len, VAXMemOp:$addr, VAXMemOp:$tbl, VAXMemOp:$mask),
    "spanc\t$len, $addr, $tbl, $mask", []>;

// --- Privileged / system instructions ---

// CHMK (0xBC): change mode to kernel. code.rw
let MemOpWidth0 = 2 in
def CHMK : InstVAX<0xBC, (outs),
    (ins VAXMemOp:$code),
    "chmk\t$code", []>;

// CHME (0xBD): change mode to executive. code.rw
let MemOpWidth0 = 2 in
def CHME : InstVAX<0xBD, (outs),
    (ins VAXMemOp:$code),
    "chme\t$code", []>;

// CHMS (0xBE): change mode to supervisor. code.rw
let MemOpWidth0 = 2 in
def CHMS : InstVAX<0xBE, (outs),
    (ins VAXMemOp:$code),
    "chms\t$code", []>;

// CHMU (0xBF): change mode to user. code.rw
let MemOpWidth0 = 2 in
def CHMU : InstVAX<0xBF, (outs),
    (ins VAXMemOp:$code),
    "chmu\t$code", []>;

// PROBER (0x0C): probe read access. mode.rb, len.rw, base.ab
let Defs = [PSW], MemOpWidth0 = 1, MemOpWidth1 = 2 in
def PROBER : InstVAX<0x0C, (outs),
    (ins VAXMemOp:$mode, VAXMemOp:$len, VAXMemOp:$base),
    "prober\t$mode, $len, $base", []>;

// PROBEW (0x0D): probe write access. mode.rb, len.rw, base.ab
let Defs = [PSW], MemOpWidth0 = 1, MemOpWidth1 = 2 in
def PROBEW : InstVAX<0x0D, (outs),
    (ins VAXMemOp:$mode, VAXMemOp:$len, VAXMemOp:$base),
    "probew\t$mode, $len, $base", []>;

// INDEX (0x0A): compute index. subscript.rl, low.rl, high.rl, size.rl,
// indexin.rl, indexout.wl
let Defs = [PSW] in
def INDEX : InstVAX<0x0A, (outs),
    (ins VAXMemOp:$sub, VAXMemOp:$low, VAXMemOp:$high,
         VAXMemOp:$size, VAXMemOp:$idxin, VAXMemOp:$idxout),
    "index\t$sub, $low, $high, $size, $idxin, $idxout", []>;

// ADAWI (0x58): add aligned word interlocked. src.rw, dst.mw
let Defs = [PSW], MemOpWidth0 = 2, MemOpWidth1 = 2 in
def ADAWI : InstVAX<0x58, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dst),
    "adawi\t$src, $dst", []>;

// INSQHI (0x5C): insert into queue head interlocked. entry.ab, header.aq
let Defs = [PSW] in
def INSQHI : InstVAX<0x5C, (outs),
    (ins VAXMemOp:$entry, VAXMemOp:$header),
    "insqhi\t$entry, $header", []>;

// INSQTI (0x5D): insert into queue tail interlocked. entry.ab, header.aq
let Defs = [PSW] in
def INSQTI : InstVAX<0x5D, (outs),
    (ins VAXMemOp:$entry, VAXMemOp:$header),
    "insqti\t$entry, $header", []>;

// REMQHI (0x5E): remove from queue head interlocked. header.aq, addr.wl
let Defs = [PSW] in
def REMQHI : InstVAX<0x5E, (outs),
    (ins VAXMemOp:$header, VAXMemOp:$addr),
    "remqhi\t$header, $addr", []>;

// REMQTI (0x5F): remove from queue tail interlocked. header.aq, addr.wl
let Defs = [PSW] in
def REMQTI : InstVAX<0x5F, (outs),
    (ins VAXMemOp:$header, VAXMemOp:$addr),
    "remqti\t$header, $addr", []>;

} // let HasMemOp = 1

// BPT (0x03): breakpoint trap. No operands.
def BPT : InstVAX<0x03, (outs), (ins), "bpt", []>;

// LDPCTX (0x06): load process context. No operands (privileged).
def LDPCTX : InstVAX<0x06, (outs), (ins), "ldpctx", []>;

// SVPCTX (0x07): save process context. No operands (privileged).
def SVPCTX : InstVAX<0x07, (outs), (ins), "svpctx", []>;

// XFC (0xFC): extended function call. No operands.
def XFC : InstVAX<0xFC, (outs), (ins), "xfc", []>;

// BSBB (0x10): branch to subroutine byte displacement. disp.bb
def BSBB : InstVAX<0x10, (outs), (ins brtarget:$dst),
    "bsbb\t$dst", []>;

// BSBW (0x30): branch to subroutine word displacement. disp.bw
def BSBW : InstVAX<0x30, (outs), (ins brtarget:$dst),
    "bsbw\t$dst", []>;

// RSB (0x05): return from subroutine. No operands.
let isReturn = 1, isBarrier = 1, isTerminator = 1 in
def RSB : InstVAX<0x05, (outs), (ins), "rsb", []>;

// Atomic load/store for i8/i16 (after all instruction defs).
def : Pat<(atomic_load_8  VAXMem:$src), (MOVZBL_rm VAXMemOp:$src)>;
def : Pat<(atomic_load_16 VAXMem:$src), (MOVZWL_rm VAXMemOp:$src)>;
def : Pat<(atomic_store_8  GPRnoPC:$src, VAXMem:$dst), (MOVB_mr GPRnoPC:$src, VAXMemOp:$dst)>;
def : Pat<(atomic_store_16 GPRnoPC:$src, VAXMem:$dst), (MOVW_mr GPRnoPC:$src, VAXMemOp:$dst)>;

// Store-zero patterns: CLRB/CLRW for truncating store of zero.
def : Pat<(truncstorei8 (i32 0), VAXMem:$dst), (CLRB VAXMemOp:$dst)>;
def : Pat<(truncstorei16 (i32 0), VAXMem:$dst), (CLRW VAXMemOp:$dst)>;

//===----------------------------------------------------------------------===//
// All-memory instruction variants for disassembly support.
//
// The VAX ISA allows any addressing mode for any operand. The compiler only
// needs a subset of variants (register, immediate, specific memory patterns),
// but the disassembler must handle any valid encoding. These definitions
// provide all-memory variants (every general operand uses VAXMemOp) so the
// disassembler can decode any addressing mode combination.
//
// No ISel patterns — these are never generated by the compiler.
//===----------------------------------------------------------------------===//

let isCodeGenOnly = 0, HasMemOp = 1 in {

// --- Single-operand instructions (1 mem operand) ---

let Defs = [PSW] in {

// PUSHL: push longword onto stack. Only had _r and _i.
def PUSHL_m : InstVAX<0xDD, (outs), (ins VAXMemOp:$src),
    "pushl\t$src", []>;

// TSTB/TSTW/TSTL: test (set condition codes). TSTL_m exists; add byte/word.
def TSTB_m : InstVAX<0x95, (outs), (ins VAXMemOp:$a),
    "tstb\t$a", []>;
def TSTW_m : InstVAX<0xB5, (outs), (ins VAXMemOp:$a),
    "tstw\t$a", []>;

// INCL/DECL/INCB/DECB/INCW/DECW: increment/decrement.
def INCL_m : InstVAX<0xD6, (outs), (ins VAXMemOp:$dst),
    "incl\t$dst", []>;
def DECL_m : InstVAX<0xD7, (outs), (ins VAXMemOp:$dst),
    "decl\t$dst", []>;
def INCB_m : InstVAX<0x96, (outs), (ins VAXMemOp:$dst),
    "incb\t$dst", []>;
def DECB_m : InstVAX<0x97, (outs), (ins VAXMemOp:$dst),
    "decb\t$dst", []>;
def INCW_m : InstVAX<0xB6, (outs), (ins VAXMemOp:$dst),
    "incw\t$dst", []>;
def DECW_m : InstVAX<0xB7, (outs), (ins VAXMemOp:$dst),
    "decw\t$dst", []>;

// CLRB/CLRW: clear byte/word. (CLRL_m already exists.)
def CLRB_m : InstVAX<0x94, (outs), (ins VAXMemOp:$dst),
    "clrb\t$dst", []>;
def CLRW_m : InstVAX<0xB4, (outs), (ins VAXMemOp:$dst),
    "clrw\t$dst", []>;
def CLRD_m : InstVAX<0x7C, (outs), (ins VAXMemOp:$dst),
    "clrd\t$dst", []>;

// TSTF/TSTD: test floating-point.
def TSTF_m : InstVAX<0x53, (outs), (ins VAXMemOp:$a),
    "tstf\t$a", []>;
def TSTD_m : InstVAX<0x73, (outs), (ins VAXMemOp:$a),
    "tstd\t$a", []>;

} // Defs = [PSW]

// MOVPSL: move PSL to destination.
def MOVPSL_m : InstVAX<0xDC, (outs), (ins VAXMemOp:$dst),
    "movpsl\t$dst", []>;

// JMP: already has JMP_mem.

// --- Two-operand instructions (2 mem operands) ---

let Defs = [PSW] in {

// MOVL: move longword. Add all-memory variant.
def MOVL_mm : InstVAX<0xD0, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movl\t$src, $dst", []>;

// MOVB/MOVW: move byte/word. All-memory.
def MOVB_mm : InstVAX<0x90, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movb\t$src, $dst", []>;
def MOVW_mm : InstVAX<0xB0, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movw\t$src, $dst", []>;

// MOVQ: move quadword. All-memory.
def MOVQ_mm : InstVAX<0x7D, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movq\t$src, $dst", []>;

// CMPL/CMPB/CMPW: compare. All-memory.
def CMPL_mm : InstVAX<0xD1, (outs), (ins VAXMemOp:$a, VAXMemOp:$b),
    "cmpl\t$a, $b", []>;
def CMPB_mm : InstVAX<0x91, (outs), (ins VAXMemOp:$a, VAXMemOp:$b),
    "cmpb\t$a, $b", []>;
def CMPW_mm : InstVAX<0xB1, (outs), (ins VAXMemOp:$a, VAXMemOp:$b),
    "cmpw\t$a, $b", []>;
def CMPF_mm : InstVAX<0x51, (outs), (ins VAXMemOp:$a, VAXMemOp:$b),
    "cmpf\t$a, $b", []>;
def CMPD_mm : InstVAX<0x71, (outs), (ins VAXMemOp:$a, VAXMemOp:$b),
    "cmpd\t$a, $b", []>;

// MCOML/MCOMB/MCOMW: move complement.
def MCOML_mm : InstVAX<0xD2, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "mcoml\t$src, $dst", []>;
def MCOMB_mm : InstVAX<0x92, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "mcomb\t$src, $dst", []>;
def MCOMW_mm : InstVAX<0xB2, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "mcomw\t$src, $dst", []>;

// MNEGL/MNEGB/MNEGW/MNEGF/MNEGD: move negated. (MNEGL_m exists as single-out.)
def MNEGB_mm : InstVAX<0x8E, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "mnegb\t$src, $dst", []>;
def MNEGW_mm : InstVAX<0xAE, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "mnegw\t$src, $dst", []>;
def MNEGL_mm : InstVAX<0xCE, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "mnegl\t$src, $dst", []>;
def MNEGF_mm : InstVAX<0x52, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "mnegf\t$src, $dst", []>;
def MNEGD_mm : InstVAX<0x72, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "mnegd\t$src, $dst", []>;

// BITL/BITB/BITW: bit test.
def BITL_mm : InstVAX<0xD3, (outs), (ins VAXMemOp:$a, VAXMemOp:$b),
    "bitl\t$a, $b", []>;
def BITB_mm : InstVAX<0x93, (outs), (ins VAXMemOp:$a, VAXMemOp:$b),
    "bitb\t$a, $b", []>;
def BITW_mm : InstVAX<0xB3, (outs), (ins VAXMemOp:$a, VAXMemOp:$b),
    "bitw\t$a, $b", []>;

// MOVZBL/MOVZBW/MOVZWL: move zero-extended. All-memory.
def MOVZBL_mm : InstVAX<0x9A, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movzbl\t$src, $dst", []>;
def MOVZBW_mm : InstVAX<0x9B, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movzbw\t$src, $dst", []>;
def MOVZWL_mm : InstVAX<0x3C, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movzwl\t$src, $dst", []>;

// CVTBL/CVTBW/CVTWL: convert (sign-extend). All-memory.
def CVTBL_mm : InstVAX<0x98, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtbl\t$src, $dst", []>;
def CVTBW_mm : InstVAX<0x99, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtbw\t$src, $dst", []>;
def CVTWL_mm : InstVAX<0x32, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtwl\t$src, $dst", []>;

// CVTFL/CVTLF/CVTFD/CVTDF/CVTDL/CVTLD: float conversions.
def CVTFL_mm : InstVAX<0x4A, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtfl\t$src, $dst", []>;
def CVTLF_mm : InstVAX<0x4E, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtlf\t$src, $dst", []>;
def CVTFD_mm : InstVAX<0x56, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtfd\t$src, $dst", []>;
def CVTDF_mm : InstVAX<0x76, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtdf\t$src, $dst", []>;
def CVTDL_mm : InstVAX<0x6A, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtdl\t$src, $dst", []>;
def CVTLD_mm : InstVAX<0x6E, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtld\t$src, $dst", []>;

// MOVF/MOVD: move float. All-memory.
def MOVF_mm : InstVAX<0x50, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movf\t$src, $dst", []>;
def MOVD_mm : InstVAX<0x70, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movd\t$src, $dst", []>;

// ADWC/SBWC: add/subtract with carry. All-memory.
def ADWC_mm : InstVAX<0xD8, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "adwc\t$src, $dst", []>;
def SBWC_mm : InstVAX<0xD9, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "sbwc\t$src, $dst", []>;

} // Defs = [PSW]

// MTPR/MFPR: move to/from processor register. Do not set condition codes.
def MTPR_mm : InstVAX<0xDA, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "mtpr\t$src, $dst", []>;
def MFPR_mm : InstVAX<0xDB, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "mfpr\t$src, $dst", []>;

let Defs = [PSW] in {

// 2-operand ALU: all-memory variants.
def ADDL2_mm : InstVAX<0xC0, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "addl2\t$src, $dst", []>;
def SUBL2_mm : InstVAX<0xC2, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "subl2\t$src, $dst", []>;
def MULL2_mm : InstVAX<0xC4, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "mull2\t$src, $dst", []>, Sched<[WriteMULL]>;
def DIVL2_mm : InstVAX<0xC6, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "divl2\t$src, $dst", []>, Sched<[WriteDIVL]>;
def BISL2_mm : InstVAX<0xC8, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "bisl2\t$src, $dst", []>;
def BICL2_mm : InstVAX<0xCA, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "bicl2\t$src, $dst", []>;
def XORL2_mm : InstVAX<0xCC, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "xorl2\t$src, $dst", []>;

// 2-operand byte/word ALU: all-memory.
def ADDB2_mm : InstVAX<0x80, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "addb2\t$src, $dst", []>;
def SUBB2_mm : InstVAX<0x82, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "subb2\t$src, $dst", []>;
def BISB2_mm : InstVAX<0x88, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "bisb2\t$src, $dst", []>;
def BICB2_mm : InstVAX<0x8A, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "bicb2\t$src, $dst", []>;
def XORB2_mm : InstVAX<0x8C, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "xorb2\t$src, $dst", []>;
def ADDW2_mm : InstVAX<0xA0, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "addw2\t$src, $dst", []>;
def SUBW2_mm : InstVAX<0xA2, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "subw2\t$src, $dst", []>;
def BISW2_mm : InstVAX<0xA8, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "bisw2\t$src, $dst", []>;
def BICW2_mm : InstVAX<0xAA, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "bicw2\t$src, $dst", []>;
def XORW2_mm : InstVAX<0xAC, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "xorw2\t$src, $dst", []>;

// 2-operand F_float ALU: all-memory.
def ADDF2_mm : InstVAX<0x40, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "addf2\t$src, $dst", []>;
def SUBF2_mm : InstVAX<0x42, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "subf2\t$src, $dst", []>;
def MULF2_mm : InstVAX<0x44, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "mulf2\t$src, $dst", []>, Sched<[WriteFPALU]>;
def DIVF2_mm : InstVAX<0x46, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "divf2\t$src, $dst", []>, Sched<[WriteFPDIV]>;

// 2-operand D_float ALU: all-memory.
def ADDD2_mm : InstVAX<0x60, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "addd2\t$src, $dst", []>;
def SUBD2_mm : InstVAX<0x62, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "subd2\t$src, $dst", []>;
def MULD2_mm : InstVAX<0x64, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "muld2\t$src, $dst", []>, Sched<[WriteFPDALU]>;
def DIVD2_mm : InstVAX<0x66, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "divd2\t$src, $dst", []>, Sched<[WriteFPDDIV]>;

// ROTL/ASHL: shift. 3-operand, all-memory.
def ASHL_mm : InstVAX<0x78, (outs), (ins VAXMemOp:$cnt, VAXMemOp:$src, VAXMemOp:$dst),
    "ashl\t$cnt, $src, $dst", []>;
def ASHQ_mm : InstVAX<0x79, (outs), (ins VAXMemOp:$cnt, VAXMemOp:$src, VAXMemOp:$dst),
    "ashq\t$cnt, $src, $dst", []>;
def ROTL_mm : InstVAX<0x9C, (outs), (ins VAXMemOp:$cnt, VAXMemOp:$src, VAXMemOp:$dst),
    "rotl\t$cnt, $src, $dst", []>;

// --- Three-operand instructions (3 mem operands) ---

// 3-operand longword ALU: all-memory.
def ADDL3_mm : InstVAX<0xC1, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "addl3\t$a, $b, $dst", []>;
def SUBL3_mm : InstVAX<0xC3, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "subl3\t$a, $b, $dst", []>;
def MULL3_mm : InstVAX<0xC5, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "mull3\t$a, $b, $dst", []>, Sched<[WriteMULL]>;
def DIVL3_mm : InstVAX<0xC7, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "divl3\t$a, $b, $dst", []>, Sched<[WriteDIVL]>;
def BISL3_mm : InstVAX<0xC9, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "bisl3\t$a, $b, $dst", []>;
def BICL3_mm : InstVAX<0xCB, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "bicl3\t$a, $b, $dst", []>;
def XORL3_mm : InstVAX<0xCD, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "xorl3\t$a, $b, $dst", []>;

// 3-operand byte ALU: all-memory.
def ADDB3_mm : InstVAX<0x81, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "addb3\t$a, $b, $dst", []>;
def SUBB3_mm : InstVAX<0x83, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "subb3\t$a, $b, $dst", []>;
def BISB3_mm : InstVAX<0x89, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "bisb3\t$a, $b, $dst", []>;
def BICB3_mm : InstVAX<0x8B, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "bicb3\t$a, $b, $dst", []>;
def XORB3_mm : InstVAX<0x8D, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "xorb3\t$a, $b, $dst", []>;

// 3-operand word ALU: all-memory.
def ADDW3_mm : InstVAX<0xA1, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "addw3\t$a, $b, $dst", []>;
def SUBW3_mm : InstVAX<0xA3, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "subw3\t$a, $b, $dst", []>;
def BISW3_mm : InstVAX<0xA9, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "bisw3\t$a, $b, $dst", []>;
def BICW3_mm : InstVAX<0xAB, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "bicw3\t$a, $b, $dst", []>;
def XORW3_mm : InstVAX<0xAD, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "xorw3\t$a, $b, $dst", []>;

// 3-operand F_float ALU: all-memory.
def ADDF3_mm : InstVAX<0x41, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "addf3\t$a, $b, $dst", []>;
def SUBF3_mm : InstVAX<0x43, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "subf3\t$a, $b, $dst", []>;
def MULF3_mm : InstVAX<0x45, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "mulf3\t$a, $b, $dst", []>, Sched<[WriteFPALU]>;
def DIVF3_mm : InstVAX<0x47, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "divf3\t$a, $b, $dst", []>, Sched<[WriteFPDIV]>;

// 3-operand D_float ALU: all-memory.
def ADDD3_mm : InstVAX<0x61, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "addd3\t$a, $b, $dst", []>;
def SUBD3_mm : InstVAX<0x63, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "subd3\t$a, $b, $dst", []>;
def MULD3_mm : InstVAX<0x65, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "muld3\t$a, $b, $dst", []>, Sched<[WriteFPDALU]>;
def DIVD3_mm : InstVAX<0x67, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "divd3\t$a, $b, $dst", []>, Sched<[WriteFPDDIV]>;

} // Defs = [PSW]

// CALLS: all-memory variant (call with args count + memory target).
// Does not set condition codes (CALLS saves/restores PSW via call frame).
def CALLS_mm : InstVAX<0xFB, (outs), (ins VAXMemOp:$args, VAXMemOp:$target),
    "calls\t$args, $target", []>;

// JSB: jump to subroutine — memory operand. Does not set condition codes.
def JSB_m : InstVAX<0x16, (outs), (ins VAXMemOp:$target),
    "jsb\t$target", []>;

} // end let isCodeGenOnly, HasMemOp

//===----------------------------------------------------------------------===//
// Missing ISA instructions for disassembly completeness.
//
// These instructions exist in the VAX ISA and appear in GCC-generated code
// or the NetBSD kernel, but are not needed for our compiler's code generation.
// Defined here so the disassembler can decode them.
//===----------------------------------------------------------------------===//

// BVC/BVS: branch on overflow clear/set (0x1C/0x1D).
def BVC : InstVAX<0x1C, (outs), (ins brtarget:$dst), "bvc\t$dst", []>,
    Sched<[WriteBRcc]>;
def BVS : InstVAX<0x1D, (outs), (ins brtarget:$dst), "bvs\t$dst", []>,
    Sched<[WriteBRcc]>;

let HasMemOp = 1 in {

let Defs = [PSW] in {

// CVTLB (0xF6): convert long to byte. src.rl, dst.wb
def CVTLB : InstVAX<0xF6, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtlb\t$src, $dst", []>;

// CVTLW (0xF7): convert long to word. src.rl, dst.ww
def CVTLW : InstVAX<0xF7, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtlw\t$src, $dst", []>;

// CVTWB (0x33): convert word to byte. src.rw, dst.wb
def CVTWB : InstVAX<0x33, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtwb\t$src, $dst", []>;

// CVTFB (0x48): convert F_float to byte. src.rf, dst.wb
def CVTFB : InstVAX<0x48, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtfb\t$src, $dst", []>;

// CVTFW (0x49): convert F_float to word. src.rf, dst.ww
def CVTFW : InstVAX<0x49, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtfw\t$src, $dst", []>;

// CVTRFL (0x4B): convert rounded F_float to long. src.rf, dst.wl
def CVTRFL : InstVAX<0x4B, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtrfl\t$src, $dst", []>;

// CVTBF (0x4C): convert byte to F_float. src.rb, dst.wf
def CVTBF : InstVAX<0x4C, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtbf\t$src, $dst", []>;

// CVTWF (0x4D): convert word to F_float. src.rw, dst.wf
def CVTWF : InstVAX<0x4D, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtwf\t$src, $dst", []>;

// CVTDB (0x68): convert D_float to byte. src.rd, dst.wb
def CVTDB : InstVAX<0x68, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtdb\t$src, $dst", []>;

// CVTDW (0x69): convert D_float to word. src.rd, dst.ww
def CVTDW : InstVAX<0x69, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtdw\t$src, $dst", []>;

// CVTRDL (0x6B): convert rounded D_float to long. src.rd, dst.wl
def CVTRDL : InstVAX<0x6B, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtrdl\t$src, $dst", []>;

// CVTBD (0x6C): convert byte to D_float. src.rb, dst.wd
def CVTBD : InstVAX<0x6C, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtbd\t$src, $dst", []>;

// CVTWD (0x6D): convert word to D_float. src.rw, dst.wd
def CVTWD : InstVAX<0x6D, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "cvtwd\t$src, $dst", []>;

// Byte/word multiply/divide (0x84-0x87, 0xA4-0xA7).
def MULB2 : InstVAX<0x84, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "mulb2\t$src, $dst", []>;
def MULB3 : InstVAX<0x85, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "mulb3\t$a, $b, $dst", []>;
def DIVB2 : InstVAX<0x86, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "divb2\t$src, $dst", []>;
def DIVB3 : InstVAX<0x87, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "divb3\t$a, $b, $dst", []>;
def MULW2 : InstVAX<0xA4, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "mulw2\t$src, $dst", []>;
def MULW3 : InstVAX<0xA5, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "mulw3\t$a, $b, $dst", []>;
def DIVW2 : InstVAX<0xA6, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "divw2\t$src, $dst", []>;
def DIVW3 : InstVAX<0xA7, (outs), (ins VAXMemOp:$a, VAXMemOp:$b, VAXMemOp:$dst),
    "divw3\t$a, $b, $dst", []>;

// EMODF (0x54): extended modulus F_float. mulr.rf, mulrx.rb, muld.rf, int.wl, fract.wf
def EMODF_m : InstVAX<0x54, (outs),
    (ins VAXMemOp:$mulr, VAXMemOp:$mulrx, VAXMemOp:$muld,
         VAXMemOp:$int, VAXMemOp:$fract),
    "emodf\t$mulr, $mulrx, $muld, $int, $fract", []>;

// EMODD (0x74): extended modulus D_float.
def EMODD_m : InstVAX<0x74, (outs),
    (ins VAXMemOp:$mulr, VAXMemOp:$mulrx, VAXMemOp:$muld,
         VAXMemOp:$int, VAXMemOp:$fract),
    "emodd\t$mulr, $mulrx, $muld, $int, $fract", []>;

// POLYF (0x55): evaluate polynomial F_float. arg.rf, degree.rw, tbladdr.ab
def POLYF_m : InstVAX<0x55, (outs),
    (ins VAXMemOp:$arg, VAXMemOp:$degree, VAXMemOp:$tbladdr),
    "polyf\t$arg, $degree, $tbladdr", []>;

// POLYD (0x75): evaluate polynomial D_float. arg.rd, degree.rw, tbladdr.ab
def POLYD_m : InstVAX<0x75, (outs),
    (ins VAXMemOp:$arg, VAXMemOp:$degree, VAXMemOp:$tbladdr),
    "polyd\t$arg, $degree, $tbladdr", []>;

// BISPSW (0xB8): bit set PSW. mask.rw
def BISPSW : InstVAX<0xB8, (outs), (ins VAXMemOp:$mask),
    "bispsw\t$mask", []>;

// BICPSW (0xB9): bit clear PSW. mask.rw
def BICPSW : InstVAX<0xB9, (outs), (ins VAXMemOp:$mask),
    "bicpsw\t$mask", []>;

// CMPV (0xEC): compare field. pos.rl, size.rb, base.vb, src.rl
def CMPV : InstVAX<0xEC, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$size, VAXMemOp:$base, VAXMemOp:$src),
    "cmpv\t$pos, $size, $base, $src", []>;

// CMPZV (0xED): compare zero-extended field. pos.rl, size.rb, base.vb, src.rl
def CMPZV : InstVAX<0xED, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$size, VAXMemOp:$base, VAXMemOp:$src),
    "cmpzv\t$pos, $size, $base, $src", []>;

// EXTV (0xEE): extract field. pos.rl, size.rb, base.vb, dst.wl
def EXTV : InstVAX<0xEE, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$size, VAXMemOp:$base, VAXMemOp:$dst),
    "extv\t$pos, $size, $base, $dst", []>;

// Decimal string instructions (packed decimal).
// CVTPS (0x08): convert packed to string. srclen.rw, srcaddr.ab, dstlen.rw, dstaddr.ab
def CVTPS_m : InstVAX<0x08, (outs),
    (ins VAXMemOp:$srclen, VAXMemOp:$srcaddr, VAXMemOp:$dstlen, VAXMemOp:$dstaddr),
    "cvtps\t$srclen, $srcaddr, $dstlen, $dstaddr", []>;

// CVTSP (0x09): convert string to packed.
def CVTSP_m : InstVAX<0x09, (outs),
    (ins VAXMemOp:$srclen, VAXMemOp:$srcaddr, VAXMemOp:$dstlen, VAXMemOp:$dstaddr),
    "cvtsp\t$srclen, $srcaddr, $dstlen, $dstaddr", []>;

// ADDP4 (0x20): add packed 4-operand.
def ADDP4 : InstVAX<0x20, (outs),
    (ins VAXMemOp:$addlen, VAXMemOp:$addaddr, VAXMemOp:$sumlen, VAXMemOp:$sumaddr),
    "addp4\t$addlen, $addaddr, $sumlen, $sumaddr", []>;

// ADDP6 (0x21): add packed 6-operand.
def ADDP6 : InstVAX<0x21, (outs),
    (ins VAXMemOp:$add1len, VAXMemOp:$add1addr, VAXMemOp:$add2len,
         VAXMemOp:$add2addr, VAXMemOp:$dstlen, VAXMemOp:$dstaddr),
    "addp6\t$add1len, $add1addr, $add2len, $add2addr, $dstlen, $dstaddr", []>;

// SUBP4 (0x22): subtract packed 4-operand.
def SUBP4 : InstVAX<0x22, (outs),
    (ins VAXMemOp:$sublen, VAXMemOp:$subaddr, VAXMemOp:$diflen, VAXMemOp:$difaddr),
    "subp4\t$sublen, $subaddr, $diflen, $difaddr", []>;

// SUBP6 (0x23): subtract packed 6-operand.
def SUBP6 : InstVAX<0x23, (outs),
    (ins VAXMemOp:$sublen, VAXMemOp:$subaddr, VAXMemOp:$minlen,
         VAXMemOp:$minaddr, VAXMemOp:$diflen, VAXMemOp:$difaddr),
    "subp6\t$sublen, $subaddr, $minlen, $minaddr, $diflen, $difaddr", []>;

// CVTPT (0x24): convert packed to trailing.
def CVTPT : InstVAX<0x24, (outs),
    (ins VAXMemOp:$srclen, VAXMemOp:$srcaddr, VAXMemOp:$tbladdr,
         VAXMemOp:$dstlen, VAXMemOp:$dstaddr),
    "cvtpt\t$srclen, $srcaddr, $tbladdr, $dstlen, $dstaddr", []>;

// MULP (0x25): multiply packed.
def MULP : InstVAX<0x25, (outs),
    (ins VAXMemOp:$mulrlen, VAXMemOp:$mulraddr, VAXMemOp:$muldlen,
         VAXMemOp:$muldaddr, VAXMemOp:$dstlen, VAXMemOp:$dstaddr),
    "mulp\t$mulrlen, $mulraddr, $muldlen, $muldaddr, $dstlen, $dstaddr", []>;

// CVTTP (0x26): convert trailing to packed.
def CVTTP : InstVAX<0x26, (outs),
    (ins VAXMemOp:$srclen, VAXMemOp:$srcaddr, VAXMemOp:$tbladdr,
         VAXMemOp:$dstlen, VAXMemOp:$dstaddr),
    "cvttp\t$srclen, $srcaddr, $tbladdr, $dstlen, $dstaddr", []>;

// DIVP (0x27): divide packed.
def DIVP : InstVAX<0x27, (outs),
    (ins VAXMemOp:$divrlen, VAXMemOp:$divraddr, VAXMemOp:$divdlen,
         VAXMemOp:$divdaddr, VAXMemOp:$dstlen, VAXMemOp:$dstaddr),
    "divp\t$divrlen, $divraddr, $divdlen, $divdaddr, $dstlen, $dstaddr", []>;

// MOVP (0x34): move packed. len.rw, srcaddr.ab, dstaddr.ab
def MOVP : InstVAX<0x34, (outs),
    (ins VAXMemOp:$len, VAXMemOp:$srcaddr, VAXMemOp:$dstaddr),
    "movp\t$len, $srcaddr, $dstaddr", []>;

// CMPP3 (0x35): compare packed 3-operand. len.rw, src1addr.ab, src2addr.ab
def CMPP3 : InstVAX<0x35, (outs),
    (ins VAXMemOp:$len, VAXMemOp:$src1addr, VAXMemOp:$src2addr),
    "cmpp3\t$len, $src1addr, $src2addr", []>;

// CVTPL (0x36): convert packed to long. srclen.rw, srcaddr.ab, dst.wl
def CVTPL : InstVAX<0x36, (outs),
    (ins VAXMemOp:$srclen, VAXMemOp:$srcaddr, VAXMemOp:$dst),
    "cvtpl\t$srclen, $srcaddr, $dst", []>;

// CMPP4 (0x37): compare packed 4-operand.
def CMPP4 : InstVAX<0x37, (outs),
    (ins VAXMemOp:$src1len, VAXMemOp:$src1addr, VAXMemOp:$src2len, VAXMemOp:$src2addr),
    "cmpp4\t$src1len, $src1addr, $src2len, $src2addr", []>;

// EDITPC (0x38): edit packed to character.
def EDITPC : InstVAX<0x38, (outs),
    (ins VAXMemOp:$srclen, VAXMemOp:$srcaddr, VAXMemOp:$pattern, VAXMemOp:$dstaddr),
    "editpc\t$srclen, $srcaddr, $pattern, $dstaddr", []>;

// ASHP (0xF8): arithmetic shift and round packed.
def ASHP : InstVAX<0xF8, (outs),
    (ins VAXMemOp:$cnt, VAXMemOp:$srclen, VAXMemOp:$srcaddr,
         VAXMemOp:$round, VAXMemOp:$dstlen, VAXMemOp:$dstaddr),
    "ashp\t$cnt, $srclen, $srcaddr, $round, $dstlen, $dstaddr", []>;

// CVTLP (0xF9): convert long to packed. src.rl, dstlen.rw, dstaddr.ab
def CVTLP : InstVAX<0xF9, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dstlen, VAXMemOp:$dstaddr),
    "cvtlp\t$src, $dstlen, $dstaddr", []>;

} // Defs = [PSW]

// CASEL_mm (0xCF): all-memory variant for disassembly. Does not set CC.
def CASEL_mm : InstVAX<0xCF, (outs),
    (ins VAXMemOp:$selector, VAXMemOp:$base, VAXMemOp:$limit),
    "casel\t$selector, $base, $limit", []>;

// ADAWI (0x58): add aligned word interlocked. All-memory.
let Defs = [PSW] in
def ADAWI_mm : InstVAX<0x58, (outs), (ins VAXMemOp:$src, VAXMemOp:$dst),
    "adawi\t$src, $dst", []>;

} // end let HasMemOp
