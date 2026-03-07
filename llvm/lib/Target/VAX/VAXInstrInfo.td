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
def NOP : InstVAX<0x01, (outs), (ins), "nop", []>;

// RET — return from CALLS/CALLG frame (hardware restores saved regs, pops frame)
let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RET : InstVAX<0x04, (outs), (ins), "ret", [(VAXISD_ret_flag)]>;

// CLRL — zero a longword register; cheapest way to load the constant 0
// Nearly all VAX data-manipulation instructions set condition codes (N/Z/V/C
// in PSW). Each instruction or block below explicitly declares Defs = [PSW].
let Defs = [PSW] in
def CLRL : InstVAX<0xD4, (outs GPRnoPC:$dst), (ins),
    "clrl\t$dst", [(set GPRnoPC:$dst, (i32 0))]>;
// CLRL to memory: store zero without an intermediate register.
// Matches (store 0, addr) with higher specificity than MOVL_mi.
let Defs = [PSW], HasMemOp = 1 in
def CLRL_ms : InstVAX<0xD4, (outs), (ins VAXMemOp:$dst),
    "clrl\t$dst",
    [(store (i32 0), VAXMemLong:$dst)]>;

//===----------------------------------------------------------------------===//
// Phase 4 — MOV instructions + basic addressing
//===----------------------------------------------------------------------===//

// MOVL (opcode 0xD0) — longword (32-bit) move, various operand forms.
// All variants share the opcode; operand specifiers encode the addressing mode.
// All MOV/MOVAL instructions set PSW condition codes.
let Defs = [PSW] in {

// reg <- reg  (register copy; no DAG pattern — emitted by copyPhysReg)
def MOVL_rr : InstVAX<0xD0, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
    "movl\t$src, $dst", []>;

// reg <- immediate  (assembler emits $imm literal)
def MOVL_ri : InstVAX<0xD0, (outs GPRnoPC:$dst), (ins i32imm:$imm),
    "movl\t$imm, $dst",
    [(set GPRnoPC:$dst, (i32 imm:$imm))]>;

// reg <- memory  (displacement/deferred load)
let HasMemOp = 1 in
def MOVL_rm : InstVAX<0xD0, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "movl\t$src, $dst",
    [(set GPRnoPC:$dst, (load VAXMemLong:$src))]>;

// memory <- reg  (displacement/deferred store)
let HasMemOp = 1 in
def MOVL_mr : InstVAX<0xD0, (outs), (ins GPRnoPC:$src, VAXMemOp:$dst),
    "movl\t$src, $dst",
    [(store GPRnoPC:$src, VAXMemLong:$dst)]>;

// memory <- immediate  (store constant to memory without register)
let HasMemOp = 1 in
def MOVL_mi : InstVAX<0xD0, (outs), (ins i32imm:$imm, VAXMemOp:$dst),
    "movl\t$imm, $dst",
    [(store (i32 imm:$imm), VAXMemLong:$dst)]>;

// reg <- global  (PC-relative load from global/extern data)
def MOVL_rg : InstVAX<0xD0, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "movl\t$sym, $dst",
    [(set GPRnoPC:$dst, (load (vax_pcrelwrapper tglobaladdr:$sym)))]>;

// reg <- constant pool entry (PC-relative load)
def MOVL_rcp : InstVAX<0xD0, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "movl\t$sym, $dst",
    [(set GPRnoPC:$dst, (load (vax_pcrelwrapper tconstpool:$sym)))]>;

// global <- reg  (PC-relative store to global/extern data)
def MOVL_gr : InstVAX<0xD0, (outs), (ins GPRnoPC:$src, i32imm:$sym),
    "movl\t$src, $sym",
    [(store GPRnoPC:$src, (vax_pcrelwrapper tglobaladdr:$sym))]>;

// Materialize a jump table address into a register (PC-relative).
// Emits: moval JT_sym, $dst
def MOVAL_jt : InstVAX<0xDE, (outs GPRnoPC:$dst), (ins i32imm:$jt),
    "moval\t$jt, $dst",
    [(set GPRnoPC:$dst, (vax_pcrelwrapper tjumptable:$jt))]>;

// Address-of-global: moval global_sym, $dst
def MOVAL_ga : InstVAX<0xDE, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "moval\t$sym, $dst",
    [(set GPRnoPC:$dst, (vax_pcrelwrapper tglobaladdr:$sym))]>;

// Address-of-constant-pool entry: moval .LCPI_n, $dst
def MOVAL_cp : InstVAX<0xDE, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "moval\t$sym, $dst",
    [(set GPRnoPC:$dst, (vax_pcrelwrapper tconstpool:$sym))]>;
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
multiclass VAXAlu3_comm<bits<16> opc, string mn, SDNode SDOp> {
  def _rr : InstVAX<opc, (outs GPRnoPC:$dst), (ins GPRnoPC:$a, GPRnoPC:$b),
      mn # "\t$a, $b, $dst",
      [(set GPRnoPC:$dst, (SDOp GPRnoPC:$a, GPRnoPC:$b))]>;
  // Constant is RHS after LLVM's commutative canonicalisation.
  def _ri : InstVAX<opc, (outs GPRnoPC:$dst), (ins i32imm:$a, GPRnoPC:$b),
      mn # "\t$a, $b, $dst",
      [(set GPRnoPC:$dst, (SDOp GPRnoPC:$b, (i32 imm:$a)))]>;
  // Fold a load into one source operand: addl3 mem, reg, dst.
  // Commutative, so either operand can be the load.
  let HasMemOp = 1 in
  def _rm : InstVAX<opc, (outs GPRnoPC:$dst),
      (ins VAXMemOp:$a, GPRnoPC:$b),
      mn # "\t$a, $b, $dst",
      [(set GPRnoPC:$dst, (SDOp (load VAXMemLong:$a), GPRnoPC:$b))]>;
}

// Multiclass for 2-operand ALU: dst OP= src. Used by all xyzL2 instructions.
multiclass VAXAlu2<bits<16> opc, string mn> {
  def _rr : InstVAX<opc, (outs GPRnoPC:$dst), (ins GPRnoPC:$s, GPRnoPC:$src),
      mn # "\t$s, $dst", []> {
    let Constraints = "$src = $dst";
  }
  def _ri : InstVAX<opc, (outs GPRnoPC:$dst), (ins i32imm:$s, GPRnoPC:$src),
      mn # "\t$s, $dst", []> {
    let Constraints = "$src = $dst";
  }
}

// Multiclass for zero-extending loads: MOVZBL, MOVZWL.
multiclass VAXMovZext<bits<16> opc, string mn, PatFrag zload, PatFrag eload> {
  let HasMemOp = 1 in {
  def _rm : InstVAX<opc, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
      mn # "\t$src, $dst",
      [(set GPRnoPC:$dst, (zload VAXMem:$src))]>;
  def _rm_e : InstVAX<opc, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
      mn # "\t$src, $dst",
      [(set GPRnoPC:$dst, (eload VAXMem:$src))]>;
  }
  def _rg : InstVAX<opc, (outs GPRnoPC:$dst), (ins i32imm:$sym),
      mn # "\t$sym, $dst",
      [(set GPRnoPC:$dst, (zload (vax_pcrelwrapper tglobaladdr:$sym)))]>;
  def _rg_e : InstVAX<opc, (outs GPRnoPC:$dst), (ins i32imm:$sym),
      mn # "\t$sym, $dst",
      [(set GPRnoPC:$dst, (eload (vax_pcrelwrapper tglobaladdr:$sym)))]>;
}

// Multiclass for sign-extending conversions: CVTBL, CVTWL.
multiclass VAXSextConv<bits<16> opc, string mn, PatFrag sload, ValueType subvt> {
  let HasMemOp = 1 in
  def _rm : InstVAX<opc, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
      mn # "\t$src, $dst",
      [(set GPRnoPC:$dst, (sload VAXMem:$src))]>;
  def _rg : InstVAX<opc, (outs GPRnoPC:$dst), (ins i32imm:$sym),
      mn # "\t$sym, $dst",
      [(set GPRnoPC:$dst, (sload (vax_pcrelwrapper tglobaladdr:$sym)))]>;
  def _rr : InstVAX<opc, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
      mn # "\t$src, $dst",
      [(set GPRnoPC:$dst, (sext_inreg GPRnoPC:$src, subvt))]>;
}

// Multiclass for truncating stores: MOVB, MOVW.
multiclass VAXTruncStore<bits<16> opc, string mn, PatFrag tstore> {
  let HasMemOp = 1 in
  def _mr : InstVAX<opc, (outs), (ins GPRnoPC:$src, VAXMemOp:$dst),
      mn # "\t$src, $dst",
      [(tstore GPRnoPC:$src, VAXMem:$dst)]>;
  def _mg : InstVAX<opc, (outs), (ins GPRnoPC:$src, i32imm:$sym),
      mn # "\t$src, $sym",
      [(tstore GPRnoPC:$src, (vax_pcrelwrapper tglobaladdr:$sym))]>;
}

// Multiclass for FP data moves (load/store/copy variants).
multiclass VAXFpMov<bits<16> opc, string mn, RegisterClass RC,
                    ValueType VT, Operand MemOp> {
  let HasMemOp = 1 in
  def _rm : InstVAX<opc, (outs RC:$dst), (ins MemOp:$src),
      mn # "\t$src, $dst",
      [(set RC:$dst, (VT (load VAXMem:$src)))]>;
  def _rcp : InstVAX<opc, (outs RC:$dst), (ins i32imm:$sym),
      mn # "\t$sym, $dst",
      [(set RC:$dst, (VT (load (vax_pcrelwrapper tconstpool:$sym))))]>;
  def _rga : InstVAX<opc, (outs RC:$dst), (ins i32imm:$sym),
      mn # "\t$sym, $dst",
      [(set RC:$dst, (VT (load (vax_pcrelwrapper tglobaladdr:$sym))))]>;
  let HasMemOp = 1 in
  def _mr : InstVAX<opc, (outs), (ins RC:$src, MemOp:$dst),
      mn # "\t$src, $dst",
      [(store (VT RC:$src), VAXMem:$dst)]>;
  def _mga : InstVAX<opc, (outs), (ins RC:$src, i32imm:$sym),
      mn # "\t$src, $sym",
      [(store (VT RC:$src), (vax_pcrelwrapper tglobaladdr:$sym))]>;
  def _rr : InstVAX<opc, (outs RC:$dst), (ins RC:$src),
      mn # "\t$src, $dst", []>;
}

// All integer ALU instructions set PSW condition codes.
let Defs = [PSW] in {
defm ADDL3 : VAXAlu3_comm<0xC1, "addl3", add>;
defm MULL3 : VAXAlu3_comm<0xC5, "mull3", mul>;
defm BISL3 : VAXAlu3_comm<0xC9, "bisl3", or>;
defm XORL3 : VAXAlu3_comm<0xCD, "xorl3", xor>;

// SUBL3 — dst = minuend - subtrahend.
// VAX encoding: subl3 src1, src2, dst  →  dst = src2 - src1.
//   $s = subtrahend (src1), $m = minuend (src2).
def SUBL3_rr : InstVAX<0xC3, (outs GPRnoPC:$dst), (ins GPRnoPC:$s, GPRnoPC:$m),
    "subl3\t$s, $m, $dst",
    [(set GPRnoPC:$dst, (sub GPRnoPC:$m, GPRnoPC:$s))]>;
// reg - imm  (subtrahend is immediate)
def SUBL3_ri : InstVAX<0xC3, (outs GPRnoPC:$dst), (ins i32imm:$s, GPRnoPC:$m),
    "subl3\t$s, $m, $dst",
    [(set GPRnoPC:$dst, (sub GPRnoPC:$m, (i32 imm:$s)))]>;
// imm - reg  (minuend is immediate)
def SUBL3_ir : InstVAX<0xC3, (outs GPRnoPC:$dst), (ins GPRnoPC:$s, i32imm:$m),
    "subl3\t$s, $m, $dst",
    [(set GPRnoPC:$dst, (sub (i32 imm:$m), GPRnoPC:$s))]>;
// reg - mem  (subtrahend loaded from memory): dst = reg - (load addr)
let HasMemOp = 1 in
def SUBL3_rm : InstVAX<0xC3, (outs GPRnoPC:$dst),
    (ins VAXMemOp:$s, GPRnoPC:$m),
    "subl3\t$s, $m, $dst",
    [(set GPRnoPC:$dst, (sub GPRnoPC:$m, (load VAXMemLong:$s)))]>;
// mem - reg  (minuend loaded from memory): dst = (load addr) - reg
let HasMemOp = 1 in
def SUBL3_mr : InstVAX<0xC3, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$s, VAXMemOp:$m),
    "subl3\t$s, $m, $dst",
    [(set GPRnoPC:$dst, (sub (load VAXMemLong:$m), GPRnoPC:$s))]>;

// DIVL3 — signed divide: dst = dividend / divisor.
// VAX encoding: divl3 src1, src2, dst  →  dst = src2 / src1.
def DIVL3_rr : InstVAX<0xC7, (outs GPRnoPC:$dst), (ins GPRnoPC:$dv, GPRnoPC:$dd),
    "divl3\t$dv, $dd, $dst",
    [(set GPRnoPC:$dst, (sdiv GPRnoPC:$dd, GPRnoPC:$dv))]>;

// MNEGL — negate longword: dst = -src  (matches ISD ineg = sub 0, x)
def MNEGL : InstVAX<0xCE, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
    "mnegl\t$src, $dst",
    [(set GPRnoPC:$dst, (ineg GPRnoPC:$src))]>;
let HasMemOp = 1 in
def MNEGL_m : InstVAX<0xCE, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "mnegl\t$src, $dst",
    [(set GPRnoPC:$dst, (ineg (load VAXMemLong:$src)))]>;

// MCOML — ones complement: dst = ~src  (matches not = xor x, -1)
def MCOML : InstVAX<0xD2, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
    "mcoml\t$src, $dst",
    [(set GPRnoPC:$dst, (not GPRnoPC:$src))]>;
let HasMemOp = 1 in
def MCOML_m : InstVAX<0xD2, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "mcoml\t$src, $dst",
    [(set GPRnoPC:$dst, (not (load VAXMemLong:$src)))]>;

// BICL3 — bit clear: dst = src & ~mask  (selected from VAXISD::BICL)
def BICL3_rr : InstVAX<0xCB, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$mask, GPRnoPC:$src),
    "bicl3\t$mask, $src, $dst",
    [(set GPRnoPC:$dst, (vax_bicl GPRnoPC:$mask, GPRnoPC:$src))]>;
def BICL3_ri : InstVAX<0xCB, (outs GPRnoPC:$dst),
    (ins i32imm:$mask, GPRnoPC:$src),
    "bicl3\t$mask, $src, $dst",
    [(set GPRnoPC:$dst, (vax_bicl (i32 imm:$mask), GPRnoPC:$src))]>;
// Fold load into source operand: dst = (load addr) & ~mask
let HasMemOp = 1 in
def BICL3_rm : InstVAX<0xCB, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$mask, VAXMemOp:$src),
    "bicl3\t$mask, $src, $dst",
    [(set GPRnoPC:$dst, (vax_bicl GPRnoPC:$mask, (load VAXMemLong:$src)))]>;

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
    [(vax_cmp GPRnoPC:$a, GPRnoPC:$b)]>;
def CMPL_ri : InstVAX<0xD1, (outs), (ins GPRnoPC:$a, i32imm:$b),
    "cmpl\t$a, $b",
    [(vax_cmp GPRnoPC:$a, (i32 imm:$b))]>;
// Compare register against memory operand.
let HasMemOp = 1 in
def CMPL_rm : InstVAX<0xD1, (outs), (ins VAXMemOp:$a, GPRnoPC:$b),
    "cmpl\t$a, $b",
    [(vax_cmp (load VAXMemLong:$a), GPRnoPC:$b)]>;
// TSTL — Test Longword: equivalent to cmpl reg, $0 but one byte shorter.
def TSTL : InstVAX<0xD5, (outs), (ins GPRnoPC:$a),
    "tstl\t$a",
    [(vax_cmp GPRnoPC:$a, (i32 0))]>;
let HasMemOp = 1 in
def TSTL_m : InstVAX<0xD5, (outs), (ins VAXMemOp:$a),
    "tstl\t$a",
    [(vax_cmp (load VAXMemLong:$a), (i32 0))]>;
}

// Unconditional branch — use BRW (16-bit displacement, ±32 KB) as the
// default form; the assembler will shorten to BRB when possible.
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def BRW : InstVAX<0x31, (outs), (ins brtarget:$dst), "brw\t$dst",
    [(br bb:$dst)]>;

// 8-bit unconditional branch (used when assembler / relaxation pass shortens).
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def BRB : InstVAX<0x11, (outs), (ins brtarget:$dst), "brb\t$dst", []>;

// JMP — indirect branch through register (for jump tables / computed goto).
// VAX JMP opcode 0x17; operand is an address specifier.
let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1 in
def JMP : InstVAX<0x17, (outs), (ins GPRnoPC:$target), "jmp\t(${target})",
    [(brind GPRnoPC:$target)]>;

// JMP_mem — for AsmPrinter expansion: JMP with memory operand encoding.
let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1,
    HasMemOp = 1 in
def JMP_mem : InstVAX<0x17, (outs), (ins VAXMemOp:$target), "jmp\t$target",
    []>;

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
    "casel\t$selector, $base, $limit", []>;

// Conditional branches (8-bit signed displacement ±127 B; all use PSW).
// Each is a separate def so the opcode byte is fixed at instruction level.
let Uses = [PSW], isBranch = 1, isTerminator = 1 in {
def BEQL  : InstVAX<0x13, (outs), (ins brtarget:$dst), "beql\t$dst",
    [(vax_brcc bb:$dst, VAXCC_EQL)]>;
def BNEQ  : InstVAX<0x12, (outs), (ins brtarget:$dst), "bneq\t$dst",
    [(vax_brcc bb:$dst, VAXCC_NEQ)]>;
def BGTR  : InstVAX<0x14, (outs), (ins brtarget:$dst), "bgtr\t$dst",
    [(vax_brcc bb:$dst, VAXCC_GTR)]>;
def BGEQ  : InstVAX<0x18, (outs), (ins brtarget:$dst), "bgeq\t$dst",
    [(vax_brcc bb:$dst, VAXCC_GEQ)]>;
def BLSS  : InstVAX<0x19, (outs), (ins brtarget:$dst), "blss\t$dst",
    [(vax_brcc bb:$dst, VAXCC_LSS)]>;
def BLEQ  : InstVAX<0x15, (outs), (ins brtarget:$dst), "bleq\t$dst",
    [(vax_brcc bb:$dst, VAXCC_LEQ)]>;
def BGTRU : InstVAX<0x1A, (outs), (ins brtarget:$dst), "bgtru\t$dst",
    [(vax_brcc bb:$dst, VAXCC_GTRU)]>;
def BGEQU : InstVAX<0x1E, (outs), (ins brtarget:$dst), "bgequ\t$dst",
    [(vax_brcc bb:$dst, VAXCC_GEQU)]>;
def BLSSU : InstVAX<0x1F, (outs), (ins brtarget:$dst), "blssu\t$dst",
    [(vax_brcc bb:$dst, VAXCC_LSSU)]>;
def BLEQU : InstVAX<0x1B, (outs), (ins brtarget:$dst), "blequ\t$dst",
    [(vax_brcc bb:$dst, VAXCC_LEQU)]>;
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
def CVTBW_rr : InstVAX<0x99, (outs GPRW:$dst), (ins GPRB:$src),
    "cvtbw\t$src, $dst",
    [(set GPRW:$dst, (sext GPRB:$src))]>;

// MOVZBW (0x9B) reg <- reg: zero-extend i8 to i16.
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
    [(set GPRnoPC:$dst, (shl GPRnoPC:$src, GPRnoPC:$cnt))]>;
def ASHL_ir : InstVAX<0x78, (outs GPRnoPC:$dst),
    (ins ByteImm:$cnt, GPRnoPC:$src),
    "ashl\t$cnt, $src, $dst",
    [(set GPRnoPC:$dst, (shl GPRnoPC:$src, (i32 imm:$cnt)))]>;

// ASHL for custom SRA lowering (cnt is already negated by LowerSRA).
def ASHL_rr_sra : InstVAX<0x78, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$cnt, GPRnoPC:$src),
    "ashl\t$cnt, $src, $dst",
    [(set GPRnoPC:$dst, (vax_ashl GPRnoPC:$cnt, GPRnoPC:$src))]>;
def ASHL_ir_sra : InstVAX<0x78, (outs GPRnoPC:$dst),
    (ins ByteImm:$cnt, GPRnoPC:$src),
    "ashl\t$cnt, $src, $dst",
    [(set GPRnoPC:$dst, (vax_ashl (i32 imm:$cnt), GPRnoPC:$src))]>;

// ROTL (0x9C) — Rotate Longword: dst = rotl(src, cnt).
def ROTL_rr : InstVAX<0x9C, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$cnt, GPRnoPC:$src),
    "rotl\t$cnt, $src, $dst",
    [(set GPRnoPC:$dst, (rotl GPRnoPC:$src, GPRnoPC:$cnt))]>;
def ROTL_ir : InstVAX<0x9C, (outs GPRnoPC:$dst),
    (ins ByteImm:$cnt, GPRnoPC:$src),
    "rotl\t$cnt, $src, $dst",
    [(set GPRnoPC:$dst, (rotl GPRnoPC:$src, (i32 imm:$cnt)))]>;
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
                                     GPRnoPC:$base))]>;

// EXTZV with register pos/size — variable lshr.
let Defs = [PSW] in
// EXTZV with register pos, register size — variable lshr.
// The size operand is .rb but accepts a register operand specifier.
let Defs = [PSW] in
def EXTZV_rrr : InstVAX<0xEF, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$pos, GPRnoPC:$size, GPRnoPC:$base),
    "extzv\t$pos, $size, $base, $dst",
    [(set GPRnoPC:$dst, (vax_extzv GPRnoPC:$pos, GPRnoPC:$size,
                                     GPRnoPC:$base))]>;

// EXTZV with memory base — load + shift folded.
let Defs = [PSW], HasMemOp = 1 in
def EXTZV_iim : InstVAX<0xEF, (outs GPRnoPC:$dst),
    (ins ByteImm:$pos, ByteImm:$size, VAXMemOp:$base),
    "extzv\t$pos, $size, $base, $dst",
    [(set GPRnoPC:$dst, (vax_extzv (i32 imm:$pos), (i32 imm:$size),
                                     (load VAXMemLong:$base)))]>;

//===----------------------------------------------------------------------===//
// FFS — Find First Set bit (ISel form for cttz)
//===----------------------------------------------------------------------===//

// FFS $0, $32, src, dst — find lowest set bit (count trailing zeros).
let Defs = [PSW] in {
def FFS_rr : InstVAX<0xEA, (outs GPRnoPC:$dst),
    (ins ByteImm:$pos, ByteImm:$size, GPRnoPC:$base),
    "ffs\t$pos, $size, $base, $dst", []>;
let HasMemOp = 1 in
def FFS_rm : InstVAX<0xEA, (outs GPRnoPC:$dst),
    (ins ByteImm:$pos, ByteImm:$size, VAXMemOp:$base),
    "ffs\t$pos, $size, $base, $dst", []>;
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
defm MULL2 : VAXAlu2<0xC4, "mull2">;
defm BISL2 : VAXAlu2<0xC8, "bisl2">;
defm BICL2 : VAXAlu2<0xCA, "bicl2">;
defm XORL2 : VAXAlu2<0xCC, "xorl2">;
} // let Defs = [PSW]

// PUSHL — push longword onto stack (SP autodecrement)  opcode 0xDD
def SDT_VAXPushL : SDTypeProfile<0, 1, [SDTCisInt<0>]>;
def vax_pushl : SDNode<"VAXISD::PUSHL", SDT_VAXPushL,
    [SDNPHasChain, SDNPOutGlue]>;

let Defs = [SP, PSW], Uses = [SP] in {
def PUSHL_r : InstVAX<0xDD, (outs), (ins GPRnoPC:$src),
    "pushl\t$src", [(vax_pushl GPRnoPC:$src)]>;
def PUSHL_i : InstVAX<0xDD, (outs), (ins i32imm:$imm),
    "pushl\t$imm", [(vax_pushl (i32 imm:$imm))]>;
}

// PUSHAx — push address onto stack (SP autodecrement)
// These are assembly-only (used in hand-written .S files like crtbegin.S)
let Defs = [SP], Uses = [SP], HasMemOp = 1 in {
def PUSHAB : InstVAX<0x9F, (outs), (ins VAXMemOp:$src),
    "pushab\t$src", []>;
def PUSHAW : InstVAX<0x3F, (outs), (ins VAXMemOp:$src),
    "pushaw\t$src", []>;
def PUSHAL : InstVAX<0xDF, (outs), (ins VAXMemOp:$src),
    "pushal\t$src", []>;
def PUSHAQ : InstVAX<0x7F, (outs), (ins VAXMemOp:$src),
    "pushaq\t$src", []>;
}

// CALLS — call subroutine with stack arguments  opcode 0xFB
// CALLS clears all condition codes and clobbers R0-R5, SP, AP, FP.
let isCall = 1, Uses = [SP],
    Defs = [SP, AP, FP, R0, R1, R2, R3, R4, R5, PSW] in {
// Direct call: callee is a global address or external symbol
def CALLS_direct : InstVAX<0xFB, (outs),
    (ins i32imm:$count, i32imm:$callee),
    "calls\t$count, $callee", []>;
// Indirect call: callee address is in a register (register deferred addressing)
let isCodeGenOnly = 1 in
def CALLS_indir : InstVAX<0xFB, (outs),
    (ins i32imm:$count, GPRnoPC:$callee),
    "calls\t$count, (${callee})", []>;
// Indirect call (assembler form): callee is a memory operand (%reg deferred)
let HasMemOp = 1 in
def CALLS_mem : InstVAX<0xFB, (outs),
    (ins i32imm:$count, VAXMemOp:$callee),
    "calls\t$count, $callee", []>;
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
    "ashq\t$cnt, $src, $dst", []>;
def ASHQ_i : InstVAX<0x79, (outs QPR:$dst),
    (ins i32imm:$cnt, QPR:$src),
    "ashq\t$cnt, $src, $dst", []>;
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
    "emul\t$mulr, $muld, $add, $dst", []>;

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
    "ediv\t$divisor, $dividend, $quo, $rem", []>;

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
    "mulf2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
// DIVF2 (0x46) — Divide F_float 2-op: dst /= src.
def DIVF2_rr : InstVAX<0x46, (outs GPRI:$dst), (ins GPRI:$s, GPRI:$src),
    "divf2\t$s, $dst", []> {
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
    [(set GPRI:$dst, (fmul GPRI:$a, GPRI:$b))]>;
// DIVF3 (0x47) — Divide F_float: dst = src2 / src1.
def DIVF3_rr : InstVAX<0x47, (outs GPRI:$dst), (ins GPRI:$dv, GPRI:$dd),
    "divf3\t$dv, $dd, $dst",
    [(set GPRI:$dst, (fdiv GPRI:$dd, GPRI:$dv))]>;

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
    "muld2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
// DIVD2 (0x66) — Divide D_float 2-op: dst /= src.
def DIVD2_rr : InstVAX<0x66, (outs QPR:$dst), (ins QPR:$s, QPR:$src),
    "divd2\t$s, $dst", []> {
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
    [(set QPR:$dst, (fmul QPR:$a, QPR:$b))]>;
// DIVD3 (0x67) — Divide D_float: dst = src2 / src1.
def DIVD3_rr : InstVAX<0x67, (outs QPR:$dst), (ins QPR:$dv, QPR:$dd),
    "divd3\t$dv, $dd, $dst",
    [(set QPR:$dst, (fdiv QPR:$dd, QPR:$dv))]>;

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
def INSQUE : InstVAX<0x0E, (outs), (ins VAXMemOp:$entry, VAXMemOp:$pred),
    "insque\t$entry, $pred", []>;

// REMQUE (0x0F): remove entry from queue. entry.ab, addr.wl
let Defs = [PSW] in
def REMQUE : InstVAX<0x0F, (outs), (ins VAXMemOp:$entry, VAXMemOp:$addr),
    "remque\t$entry, $addr", []>;

// MOVC3 (0x28): move character 3-operand. len.rw, srcaddr.ab, dstaddr.ab
let Defs = [R0, R1, R2, R3, R4, R5] in
def MOVC3 : InstVAX<0x28, (outs),
    (ins VAXMemOp:$len, VAXMemOp:$src, VAXMemOp:$dst),
    "movc3\t$len, $src, $dst", []>;

// MOVC5 (0x2C): move character 5-operand.
// srclen.rw, srcaddr.ab, fill.rb, dstlen.rw, dstaddr.ab
let Defs = [R0, R1, R2, R3, R4, R5] in
def MOVC5 : InstVAX<0x2C, (outs),
    (ins VAXMemOp:$srclen, VAXMemOp:$srcaddr, VAXMemOp:$fill,
         VAXMemOp:$dstlen, VAXMemOp:$dstaddr),
    "movc5\t$srclen, $srcaddr, $fill, $dstlen, $dstaddr", []>;

// SCANC (0x2A): scan characters. len.rw, addr.ab, tbladdr.ab, mask.rb
let Defs = [R0, R1, R2, R3] in
def SCANC : InstVAX<0x2A, (outs),
    (ins VAXMemOp:$len, VAXMemOp:$addr, VAXMemOp:$tbl, VAXMemOp:$mask),
    "scanc\t$len, $addr, $tbl, $mask", []>;

// LOCC (0x3A): locate character. char.rb, len.rw, addr.ab
let Defs = [R0, R1] in
def LOCC : InstVAX<0x3A, (outs),
    (ins VAXMemOp:$char, VAXMemOp:$len, VAXMemOp:$addr),
    "locc\t$char, $len, $addr", []>;

// SKPC (0x3B): skip character. char.rb, len.rw, addr.ab
let Defs = [R0, R1] in
def SKPC : InstVAX<0x3B, (outs),
    (ins VAXMemOp:$char, VAXMemOp:$len, VAXMemOp:$addr),
    "skpc\t$char, $len, $addr", []>;

} // let HasMemOp = 1

// REI (0x02): return from exception/interrupt. No operands.
let isReturn = 1, isBarrier = 1, isTerminator = 1 in
def REI : InstVAX<0x02, (outs), (ins), "rei", []>;

let HasMemOp = 1 in {

// ASHL (0x78): arithmetic shift long. cnt.rb, src.rl, dst.wl
def ASHL : InstVAX<0x78, (outs),
    (ins VAXMemOp:$cnt, VAXMemOp:$src, VAXMemOp:$dst),
    "ashl\t$cnt, $src, $dst", []>;

// MOVAB (0x9E): move address byte. src.ab, dst.wl
let HasMemOp = 1 in
def MOVAB : InstVAX<0x9E, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movab\t$src, $dst", []>;

// MOVABL (0x9E): move address byte — alias for MOVAB
// (GAS accepts both movab and movabl)

// MOVAW (0x3E): move address word. src.aw, dst.wl
let HasMemOp = 1 in
def MOVAW : InstVAX<0x3E, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movaw\t$src, $dst", []>;

// MOVAL (0xDE): move address long. src.al, dst.wl
let HasMemOp = 1 in
def MOVAL : InstVAX<0xDE, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dst),
    "moval\t$src, $dst", []>;

// MOVAQ (0x7E): move address quad. src.aq, dst.wl
let HasMemOp = 1 in
def MOVAQ : InstVAX<0x7E, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movaq\t$src, $dst", []>;

// MOVQ (0x7D): move quadword (64-bit integer, no float validation).
// Unlike MOVD, MOVQ does NOT check for reserved floating-point operands.
// Use MOVQ for i64 data and register spills; MOVD only for actual f64 values.
let HasMemOp = 1 in
def MOVQ : InstVAX<0x7D, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dst),
    "movq\t$src, $dst", []>;
// MOVQ register-register (QPR copy for i64 / register allocation).
def MOVQ_rr : InstVAX<0x7D, (outs QPR:$dst), (ins QPR:$src),
    "movq\t$src, $dst", []>;
// MOVQ register-from-memory (QPR reload from stack slot).
let HasMemOp = 1 in
def MOVQ_rm : InstVAX<0x7D, (outs QPR:$dst), (ins VAXDMemOp:$src),
    "movq\t$src, $dst", []>;
// MOVQ memory-from-register (QPR spill to stack slot).
let HasMemOp = 1 in
def MOVQ_mr : InstVAX<0x7D, (outs), (ins QPR:$src, VAXDMemOp:$dst),
    "movq\t$src, $dst", []>;

// EXTZV (0xEF): extract zero-extended field. pos.rl, size.rb, base.rl, dst.wl
def EXTZV : InstVAX<0xEF, (outs),
    (ins VAXMemOp:$pos, VAXMemOp:$size, VAXMemOp:$base, VAXMemOp:$dst),
    "extzv\t$pos, $size, $base, $dst", []>;

// INSV (0xF0): insert field. src.rl, pos.rl, size.rb, base.rl
def INSV : InstVAX<0xF0, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$pos, VAXMemOp:$size, VAXMemOp:$base),
    "insv\t$src, $pos, $size, $base", []>;

// SOBGTR (0xF5): subtract one and branch if greater. index.ml, disp.bb
// The index operand is modified (read-write). disp is a branch displacement.
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
let Defs = [PSW] in
def DECW : InstVAX<0xB7, (outs),
    (ins VAXMemOp:$dst),
    "decw\t$dst", []>;

// DECB (0x97): decrement byte
let Defs = [PSW] in
def DECB : InstVAX<0x97, (outs),
    (ins VAXMemOp:$dst),
    "decb\t$dst", []>;

// INCW (0xB6): increment word
let Defs = [PSW] in
def INCW : InstVAX<0xB6, (outs),
    (ins VAXMemOp:$dst),
    "incw\t$dst", []>;

// INCB (0x96): increment byte
let Defs = [PSW] in
def INCB : InstVAX<0x96, (outs),
    (ins VAXMemOp:$dst),
    "incb\t$dst", []>;

// DIVL2 (0xC6): divide longword 2-operand. divisor.rl, dividend.ml
let Defs = [PSW] in
def DIVL2 : InstVAX<0xC6, (outs),
    (ins VAXMemOp:$divisor, VAXMemOp:$dividend),
    "divl2\t$divisor, $dividend", []>;

// MULL2 (0xC4): multiply longword 2-operand. mulr.rl, prod.ml
let Defs = [PSW] in
def MULL2 : InstVAX<0xC4, (outs),
    (ins VAXMemOp:$mulr, VAXMemOp:$prod),
    "mull2\t$mulr, $prod", []>;

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
let Defs = [PSW] in
def CLRB : InstVAX<0x94, (outs),
    (ins VAXMemOp:$dst),
    "clrb\t$dst", []>;

// CLRW (0xB4): clear word
let Defs = [PSW] in
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

} // let HasMemOp = 1

// Atomic load/store for i8/i16 (after all instruction defs).
def : Pat<(atomic_load_8  VAXMem:$src), (MOVZBL_rm VAXMemOp:$src)>;
def : Pat<(atomic_load_16 VAXMem:$src), (MOVZWL_rm VAXMemOp:$src)>;
def : Pat<(atomic_store_8  GPRnoPC:$src, VAXMem:$dst), (MOVB_mr GPRnoPC:$src, VAXMemOp:$dst)>;
def : Pat<(atomic_store_16 GPRnoPC:$src, VAXMem:$dst), (MOVW_mr GPRnoPC:$src, VAXMemOp:$dst)>;

// Store-zero patterns: CLRB/CLRW for truncating store of zero.
def : Pat<(truncstorei8 (i32 0), VAXMem:$dst), (CLRB VAXMemOp:$dst)>;
def : Pat<(truncstorei16 (i32 0), VAXMem:$dst), (CLRW VAXMemOp:$dst)>;
