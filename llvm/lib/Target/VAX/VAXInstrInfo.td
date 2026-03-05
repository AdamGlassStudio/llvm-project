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
    [(set GPRnoPC:$dst, (load VAXMem:$src))]>;

// memory <- reg  (displacement/deferred store)
let HasMemOp = 1 in
def MOVL_mr : InstVAX<0xD0, (outs), (ins GPRnoPC:$src, VAXMemOp:$dst),
    "movl\t$src, $dst",
    [(store GPRnoPC:$src, VAXMem:$dst)]>;

// memory <- immediate  (store constant to memory without register)
let HasMemOp = 1 in
def MOVL_mi : InstVAX<0xD0, (outs), (ins i32imm:$imm, VAXMemOp:$dst),
    "movl\t$imm, $dst",
    [(store (i32 imm:$imm), VAXMem:$dst)]>;

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
def : Pat<(atomic_load_32 VAXMem:$src), (MOVL_rm VAXMemOp:$src)>;
def : Pat<(atomic_store_32 GPRnoPC:$src, VAXMem:$dst), (MOVL_mr GPRnoPC:$src, VAXMemOp:$dst)>;

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

// DIVL3 — signed divide: dst = dividend / divisor.
// VAX encoding: divl3 src1, src2, dst  →  dst = src2 / src1.
def DIVL3_rr : InstVAX<0xC7, (outs GPRnoPC:$dst), (ins GPRnoPC:$dv, GPRnoPC:$dd),
    "divl3\t$dv, $dd, $dst",
    [(set GPRnoPC:$dst, (sdiv GPRnoPC:$dd, GPRnoPC:$dv))]>;

// MNEGL — negate longword: dst = -src  (matches ISD ineg = sub 0, x)
def MNEGL : InstVAX<0xCE, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
    "mnegl\t$src, $dst",
    [(set GPRnoPC:$dst, (ineg GPRnoPC:$src))]>;

// MCOML — ones complement: dst = ~src  (matches not = xor x, -1)
def MCOML : InstVAX<0xD2, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
    "mcoml\t$src, $dst",
    [(set GPRnoPC:$dst, (not GPRnoPC:$src))]>;

// BICL3 — bit clear: dst = src & ~mask  (selected from VAXISD::BICL)
def BICL3_rr : InstVAX<0xCB, (outs GPRnoPC:$dst),
    (ins GPRnoPC:$mask, GPRnoPC:$src),
    "bicl3\t$mask, $src, $dst",
    [(set GPRnoPC:$dst, (vax_bicl GPRnoPC:$mask, GPRnoPC:$src))]>;
def BICL3_ri : InstVAX<0xCB, (outs GPRnoPC:$dst),
    (ins i32imm:$mask, GPRnoPC:$src),
    "bicl3\t$mask, $src, $dst",
    [(set GPRnoPC:$dst, (vax_bicl (i32 imm:$mask), GPRnoPC:$src))]>;

// MOVZBL — zero-extend byte to longword.
// Handles both zextloadi8 (explicit zero-ext) and extloadi8 (anyext, upper bits defined as 0).
let HasMemOp = 1 in {
def MOVZBL_rm : InstVAX<0x9A, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "movzbl\t$src, $dst",
    [(set GPRnoPC:$dst, (zextloadi8 VAXMem:$src))]>;
def MOVZBL_rm_e : InstVAX<0x9A, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "movzbl\t$src, $dst",
    [(set GPRnoPC:$dst, (extloadi8 VAXMem:$src))]>;
}
def MOVZBL_rg : InstVAX<0x9A, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "movzbl\t$sym, $dst",
    [(set GPRnoPC:$dst, (zextloadi8 (vax_pcrelwrapper tglobaladdr:$sym)))]>;
def MOVZBL_rg_e : InstVAX<0x9A, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "movzbl\t$sym, $dst",
    [(set GPRnoPC:$dst, (extloadi8 (vax_pcrelwrapper tglobaladdr:$sym)))]>;
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
// TSTL — Test Longword: equivalent to cmpl reg, $0 but one byte shorter.
def TSTL : InstVAX<0xD5, (outs), (ins GPRnoPC:$a),
    "tstl\t$a",
    [(vax_cmp GPRnoPC:$a, (i32 0))]>;
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
let HasMemOp = 1 in
def CVTBL_rm : InstVAX<0x98, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "cvtbl\t$src, $dst",
    [(set GPRnoPC:$dst, (sextloadi8 VAXMem:$src))]>;
def CVTBL_rg : InstVAX<0x98, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "cvtbl\t$sym, $dst",
    [(set GPRnoPC:$dst, (sextloadi8 (vax_pcrelwrapper tglobaladdr:$sym)))]>;
// Register source: interprets low byte of $src as signed byte, extends to long.
def CVTBL_rr : InstVAX<0x98, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
    "cvtbl\t$src, $dst",
    [(set GPRnoPC:$dst, (sext_inreg GPRnoPC:$src, i8))]>;

// MOVZWL (0x3C) — Move Zero-Extended Word to Longword.
// Covers zextloadi16 and extloadi16 (anyext is fine with zero-extension).
let HasMemOp = 1 in {
def MOVZWL_rm : InstVAX<0x3C, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "movzwl\t$src, $dst",
    [(set GPRnoPC:$dst, (zextloadi16 VAXMem:$src))]>;
def MOVZWL_rm_e : InstVAX<0x3C, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "movzwl\t$src, $dst",
    [(set GPRnoPC:$dst, (extloadi16 VAXMem:$src))]>;
}
def MOVZWL_rg : InstVAX<0x3C, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "movzwl\t$sym, $dst",
    [(set GPRnoPC:$dst, (zextloadi16 (vax_pcrelwrapper tglobaladdr:$sym)))]>;
def MOVZWL_rg_e : InstVAX<0x3C, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "movzwl\t$sym, $dst",
    [(set GPRnoPC:$dst, (extloadi16 (vax_pcrelwrapper tglobaladdr:$sym)))]>;

// CVTWL (0x32) — Convert Word to Longword: sign-extends low 16 bits.
let HasMemOp = 1 in
def CVTWL_rm : InstVAX<0x32, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "cvtwl\t$src, $dst",
    [(set GPRnoPC:$dst, (sextloadi16 VAXMem:$src))]>;
def CVTWL_rg : InstVAX<0x32, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "cvtwl\t$sym, $dst",
    [(set GPRnoPC:$dst, (sextloadi16 (vax_pcrelwrapper tglobaladdr:$sym)))]>;
def CVTWL_rr : InstVAX<0x32, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
    "cvtwl\t$src, $dst",
    [(set GPRnoPC:$dst, (sext_inreg GPRnoPC:$src, i16))]>;

// MOVB (0x90) — Move Byte: used for truncating stores of i32 to i8 memory.
let HasMemOp = 1 in
def MOVB_mr : InstVAX<0x90, (outs), (ins GPRnoPC:$src, VAXMemOp:$dst),
    "movb\t$src, $dst",
    [(truncstorei8 GPRnoPC:$src, VAXMem:$dst)]>;
def MOVB_mg : InstVAX<0x90, (outs), (ins GPRnoPC:$src, i32imm:$sym),
    "movb\t$src, $sym",
    [(truncstorei8 GPRnoPC:$src, (vax_pcrelwrapper tglobaladdr:$sym))]>;

// MOVW (0xB0) — Move Word: used for truncating stores of i32 to i16 memory.
let HasMemOp = 1 in
def MOVW_mr : InstVAX<0xB0, (outs), (ins GPRnoPC:$src, VAXMemOp:$dst),
    "movw\t$src, $dst",
    [(truncstorei16 GPRnoPC:$src, VAXMem:$dst)]>;
def MOVW_mg : InstVAX<0xB0, (outs), (ins GPRnoPC:$src, i32imm:$sym),
    "movw\t$src, $sym",
    [(truncstorei16 GPRnoPC:$src, (vax_pcrelwrapper tglobaladdr:$sym))]>;
} // let Defs = [PSW] — end of Phase 7 conversion/move block

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

let usesCustomInserter = 1 in
def SELECT_CC_Pseudo : Pseudo<(outs GPRnoPC:$dst),
    (ins GPRnoPC:$truev, GPRnoPC:$falsev, i32imm:$cc),
    "#SELECT_CC $truev, $falsev, $cc",
    [(set GPRnoPC:$dst, (vax_select_cc GPRnoPC:$truev, GPRnoPC:$falsev,
                                        (i32 imm:$cc)))]>;

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

// SUBL2_ri — subl2 $imm, $dst  (dst -= imm)  opcode 0xC2
let Defs = [PSW] in {
def SUBL2_ri : InstVAX<0xC2, (outs GPRnoPC:$dst), (ins i32imm:$imm, GPRnoPC:$src),
    "subl2\t$imm, $dst", []> {
  let Constraints = "$src = $dst";
}

// ADDL2_ri — addl2 $imm, $dst  (dst += imm)  opcode 0xC0
def ADDL2_ri : InstVAX<0xC0, (outs GPRnoPC:$dst), (ins i32imm:$imm, GPRnoPC:$src),
    "addl2\t$imm, $dst", []> {
  let Constraints = "$src = $dst";
}

// 2-operand register-register forms: opl2 src, dst → dst OP= src
def ADDL2_rr : InstVAX<0xC0, (outs GPRnoPC:$dst), (ins GPRnoPC:$s, GPRnoPC:$src),
    "addl2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
def SUBL2_rr : InstVAX<0xC2, (outs GPRnoPC:$dst), (ins GPRnoPC:$s, GPRnoPC:$src),
    "subl2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
def MULL2_rr : InstVAX<0xC4, (outs GPRnoPC:$dst), (ins GPRnoPC:$s, GPRnoPC:$src),
    "mull2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
def MULL2_ri : InstVAX<0xC4, (outs GPRnoPC:$dst), (ins i32imm:$s, GPRnoPC:$src),
    "mull2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
def BISL2_rr : InstVAX<0xC8, (outs GPRnoPC:$dst), (ins GPRnoPC:$s, GPRnoPC:$src),
    "bisl2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
def BISL2_ri : InstVAX<0xC8, (outs GPRnoPC:$dst), (ins i32imm:$s, GPRnoPC:$src),
    "bisl2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
def XORL2_rr : InstVAX<0xCC, (outs GPRnoPC:$dst), (ins GPRnoPC:$s, GPRnoPC:$src),
    "xorl2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
def XORL2_ri : InstVAX<0xCC, (outs GPRnoPC:$dst), (ins i32imm:$s, GPRnoPC:$src),
    "xorl2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
def BICL2_rr : InstVAX<0xCA, (outs GPRnoPC:$dst), (ins GPRnoPC:$s, GPRnoPC:$src),
    "bicl2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
def BICL2_ri : InstVAX<0xCA, (outs GPRnoPC:$dst), (ins i32imm:$s, GPRnoPC:$src),
    "bicl2\t$s, $dst", []> {
  let Constraints = "$src = $dst";
}
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
// MOVL for f32: load/store (reuse i32 move opcode, different type).
let HasMemOp = 1 in
def MOVF_rm : InstVAX<0x50, (outs GPRI:$dst), (ins VAXFMemOp:$src),
    "movf\t$src, $dst",
    [(set GPRI:$dst, (f32 (load VAXMem:$src)))]>;
// f32 load from constant pool
def MOVF_rcp : InstVAX<0x50, (outs GPRI:$dst), (ins i32imm:$sym),
    "movf\t$sym, $dst",
    [(set GPRI:$dst, (f32 (load (vax_pcrelwrapper tconstpool:$sym))))]>;
// f32 load from global address
def MOVF_rga : InstVAX<0x50, (outs GPRI:$dst), (ins i32imm:$sym),
    "movf\t$sym, $dst",
    [(set GPRI:$dst, (f32 (load (vax_pcrelwrapper tglobaladdr:$sym))))]>;
let HasMemOp = 1 in
def MOVF_mr : InstVAX<0x50, (outs), (ins GPRI:$src, VAXFMemOp:$dst),
    "movf\t$src, $dst",
    [(store (f32 GPRI:$src), VAXMem:$dst)]>;
// f32 store to global address
def MOVF_mga : InstVAX<0x50, (outs), (ins GPRI:$src, i32imm:$sym),
    "movf\t$src, $sym",
    [(store (f32 GPRI:$src), (vax_pcrelwrapper tglobaladdr:$sym))]>;
def MOVF_rr : InstVAX<0x50, (outs GPRI:$dst), (ins GPRI:$src),
    "movf\t$src, $dst", []>;

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

let Defs = [PSW] in {
def CMPF : InstVAX<0x51, (outs), (ins GPRI:$a, GPRI:$b),
    "cmpf\t$a, $b",
    [(vax_fcmp GPRI:$a, GPRI:$b)]>;
def TSTF : InstVAX<0x53, (outs), (ins GPRI:$a),
    "tstf\t$a", []>;
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
// MOVD (0x70) — Move D_float (64-bit) using register pairs.
let HasMemOp = 1 in
def MOVD_rm : InstVAX<0x70, (outs QPR:$dst), (ins VAXDMemOp:$src),
    "movd\t$src, $dst",
    [(set QPR:$dst, (f64 (load VAXMem:$src)))]>;
// f64 load from constant pool
def MOVD_rcp : InstVAX<0x70, (outs QPR:$dst), (ins i32imm:$sym),
    "movd\t$sym, $dst",
    [(set QPR:$dst, (f64 (load (vax_pcrelwrapper tconstpool:$sym))))]>;
// f64 load from global address
def MOVD_rga : InstVAX<0x70, (outs QPR:$dst), (ins i32imm:$sym),
    "movd\t$sym, $dst",
    [(set QPR:$dst, (f64 (load (vax_pcrelwrapper tglobaladdr:$sym))))]>;
let HasMemOp = 1 in
def MOVD_mr : InstVAX<0x70, (outs), (ins QPR:$src, VAXDMemOp:$dst),
    "movd\t$src, $dst",
    [(store (f64 QPR:$src), VAXMem:$dst)]>;
// f64 store to global address
def MOVD_mga : InstVAX<0x70, (outs), (ins QPR:$src, i32imm:$sym),
    "movd\t$src, $sym",
    [(store (f64 QPR:$src), (vax_pcrelwrapper tglobaladdr:$sym))]>;
def MOVD_rr : InstVAX<0x70, (outs QPR:$dst), (ins QPR:$src),
    "movd\t$src, $dst", []>;

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
def TSTD : InstVAX<0x73, (outs), (ins QPR:$a),
    "tstd\t$a", []>;
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

// MOVQ (0x7D): move quadword. src.rq, dst.wq
let HasMemOp = 1 in
def MOVQ : InstVAX<0x7D, (outs),
    (ins VAXMemOp:$src, VAXMemOp:$dst),
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

// Atomic load/store for i8/i16 (after all instruction defs).
def : Pat<(atomic_load_8  VAXMem:$src), (MOVZBL_rm VAXMemOp:$src)>;
def : Pat<(atomic_load_16 VAXMem:$src), (MOVZWL_rm VAXMemOp:$src)>;
def : Pat<(atomic_store_8  GPRnoPC:$src, VAXMem:$dst), (MOVB_mr GPRnoPC:$src, VAXMemOp:$dst)>;
def : Pat<(atomic_store_16 GPRnoPC:$src, VAXMem:$dst), (MOVW_mr GPRnoPC:$src, VAXMemOp:$dst)>;
