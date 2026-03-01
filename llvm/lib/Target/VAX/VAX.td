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
include "VAXInstrInfo.td"

defm : RemapAllTargetPseudoPointerOperands<GPR>;

def VAXInstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// VAX processors
//===----------------------------------------------------------------------===//

class Proc<string Name, list<SubtargetFeature> Features>
    : Processor<Name, NoItineraries, Features>;

def : Proc<"generic", []>;

//===----------------------------------------------------------------------===//
// Target
//===----------------------------------------------------------------------===//

def VAX : Target {
  let InstructionSet = VAXInstrInfo;
}
