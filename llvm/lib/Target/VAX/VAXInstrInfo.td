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

def VAXMemOp : Operand<iPTR> {
  let PrintMethod   = "printMemOperand";
  let MIOperandInfo = (ops GPR, i32imm);
}

// ComplexPattern that recognizes base + displacement addressing.
// Handles: FrameIndex, ADD(FrameIndex, Const), ADD(Reg, Const), bare Reg.
def VAXMem : ComplexPattern<iPTR, 2, "SelectVAXAddr", [frameindex, add], []>;

//===----------------------------------------------------------------------===//
// Real instructions (Phase 3 — enough for "int f() { return 0; }")
//===----------------------------------------------------------------------===//

// NOP — kept as a known-good no-op; also satisfies old TableGen min-instruction req
def NOP : InstVAX<0x01, (outs), (ins), "nop", []>;

// RET — return from CALLS/CALLG frame (hardware restores saved regs, pops frame)
let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RET : InstVAX<0x04, (outs), (ins), "ret", [(VAXISD_ret_flag)]>;

// CLRL — zero a longword register; cheapest way to load the constant 0
def CLRL : InstVAX<0xD4, (outs GPRnoPC:$dst), (ins),
    "clrl\t$dst", [(set GPRnoPC:$dst, (i32 0))]>;

//===----------------------------------------------------------------------===//
// Phase 4 — MOV instructions + basic addressing
//===----------------------------------------------------------------------===//

// MOVL (opcode 0xD0) — longword (32-bit) move, various operand forms.
// All variants share the opcode; operand specifiers encode the addressing mode.

// reg <- reg  (register copy; no DAG pattern — emitted by copyPhysReg)
def MOVL_rr : InstVAX<0xD0, (outs GPRnoPC:$dst), (ins GPRnoPC:$src),
    "movl\t$src, $dst", []>;

// reg <- immediate  (assembler emits $imm literal)
def MOVL_ri : InstVAX<0xD0, (outs GPRnoPC:$dst), (ins i32imm:$imm),
    "movl\t$imm, $dst",
    [(set GPRnoPC:$dst, (i32 imm:$imm))]>;

// reg <- memory  (displacement/deferred load)
def MOVL_rm : InstVAX<0xD0, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "movl\t$src, $dst",
    [(set GPRnoPC:$dst, (load VAXMem:$src))]>;

// memory <- reg  (displacement/deferred store)
def MOVL_mr : InstVAX<0xD0, (outs), (ins GPRnoPC:$src, VAXMemOp:$dst),
    "movl\t$src, $dst",
    [(store GPRnoPC:$src, VAXMem:$dst)]>;

// reg <- global  (PC-relative load from global/extern data)
def MOVL_rg : InstVAX<0xD0, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "movl\t$sym, $dst",
    [(set GPRnoPC:$dst, (load (vax_pcrelwrapper tglobaladdr:$sym)))]>;

// global <- reg  (PC-relative store to global/extern data)
def MOVL_gr : InstVAX<0xD0, (outs), (ins GPRnoPC:$src, i32imm:$sym),
    "movl\t$src, $sym",
    [(store GPRnoPC:$src, (vax_pcrelwrapper tglobaladdr:$sym))]>;

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
def MOVZBL_rm : InstVAX<0x9A, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "movzbl\t$src, $dst",
    [(set GPRnoPC:$dst, (zextloadi8 VAXMem:$src))]>;
def MOVZBL_rm_e : InstVAX<0x9A, (outs GPRnoPC:$dst), (ins VAXMemOp:$src),
    "movzbl\t$src, $dst",
    [(set GPRnoPC:$dst, (extloadi8 VAXMem:$src))]>;
def MOVZBL_rg : InstVAX<0x9A, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "movzbl\t$sym, $dst",
    [(set GPRnoPC:$dst, (zextloadi8 (vax_pcrelwrapper tglobaladdr:$sym)))]>;
def MOVZBL_rg_e : InstVAX<0x9A, (outs GPRnoPC:$dst), (ins i32imm:$sym),
    "movzbl\t$sym, $dst",
    [(set GPRnoPC:$dst, (extloadi8 (vax_pcrelwrapper tglobaladdr:$sym)))]>;
