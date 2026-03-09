//===-- VAX.td - Describe the VAX Target Machine ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

include "VAXRegisterInfo.td"
include "VAXCallingConv.td"
include "VAXSchedule.td"
include "VAXInstrInfo.td"

defm : RemapAllTargetPseudoPointerOperands<GPR>;

def VAXInstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// VAX processors
//===----------------------------------------------------------------------===//

// CVAX (DC580) — MicroVAX 3800/3900 (KA655 module), simulated by SIMH.
// Timing data from DEC engineering note 166.1 (Mike Phipps, 24-JUL-1992).
def : ProcessorModel<"cvax", CVAXModel, []>;

// Default processor uses the CVAX model.
def : ProcessorModel<"generic", CVAXModel, []>;

def VAXAsmParser : AsmParser {
  let AllowDuplicateRegisterNames = 1;
  let ShouldEmitMatchRegisterAltName = 1;
}

//===----------------------------------------------------------------------===//
// Target
//===----------------------------------------------------------------------===//

def VAX : Target {
  let InstructionSet = VAXInstrInfo;
  let AssemblyParsers = [VAXAsmParser];
}
