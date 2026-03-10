//===-- VAXInstrFormats.td - VAX Instruction Formats -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// VAX instructions are variable-length: one or two opcode bytes followed by
// zero or more operand specifier bytes. The base class captures the opcode;
// operand specifiers are handled by the operand infrastructure.
//
//===----------------------------------------------------------------------===//

class InstVAX<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
    : Instruction {
  field bits<16> Inst;
  let Inst{15-0} = op;

  let Namespace = "VAX";
  let OutOperandList = outs;
  let InOperandList  = ins;
  let AsmString      = asmstr;
  let Pattern        = pattern;

  // VAX instructions are variable-length; set a large size and let the
  // assembler/encoder determine the real encoding size.
  let Size = 0;

  // TSFlags layout for the MCCodeEmitter:
  //   Bit  0:     HasMemOp — MCInst operands [1,2] form a 2-slot memory pair.
  //   Bits 1-16:  HWOpcode — the 1-2 byte hardware opcode value.
  //   Bits 17-28: Per-MemOp data widths (6 positions × 2 bits each).
  //               00 = longword (4 bytes), 01 = byte (1), 10 = word (2),
  //               11 = quadword (8). Position 0 is the first MemOp operand.
  bit HasMemOp = 0;
  bits<16> HWOpcode = op;
  bits<2> MemOpWidth0 = 0;
  bits<2> MemOpWidth1 = 0;
  bits<2> MemOpWidth2 = 0;
  bits<2> MemOpWidth3 = 0;
  bits<2> MemOpWidth4 = 0;
  bits<2> MemOpWidth5 = 0;
  let TSFlags{0} = HasMemOp;
  let TSFlags{16-1} = HWOpcode;
  let TSFlags{18-17} = MemOpWidth0;
  let TSFlags{20-19} = MemOpWidth1;
  let TSFlags{22-21} = MemOpWidth2;
  let TSFlags{24-23} = MemOpWidth3;
  let TSFlags{26-25} = MemOpWidth4;
  let TSFlags{28-27} = MemOpWidth5;
}
