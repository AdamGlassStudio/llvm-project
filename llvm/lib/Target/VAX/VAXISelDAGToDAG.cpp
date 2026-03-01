//===-- VAXISelDAGToDAG.cpp - VAX DAG to DAG Instruction Selector -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAX.h"
#include "VAXISelLowering.h"
#include "VAXTargetMachine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "vax-isel"

namespace {

class VAXDAGToDAGISel : public SelectionDAGISel {
public:
  explicit VAXDAGToDAGISel(VAXTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

  void Select(SDNode *N) override;

  // ComplexPattern selector for base+displacement memory operands.
  // Returns true and sets Base/Offset when Addr matches a known form:
  //   FrameIndex, ADD(FrameIndex, Const), ADD(Reg, Const), bare Reg.
  bool SelectVAXAddr(SDValue Addr, SDValue &Base, SDValue &Offset);

// Include the pieces auto-generated from the target description.
#include "VAXGenDAGISel.inc"
};

} // end anonymous namespace

void VAXDAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;
  }

  if (N->getOpcode() == VAXISD::CALL) {
    SDValue Chain   = N->getOperand(0);
    SDValue NumArgs = N->getOperand(1);
    SDValue Callee  = N->getOperand(2);
    SDValue RegMask = N->getOperand(3);

    bool isDirect = (Callee.getOpcode() == ISD::TargetGlobalAddress ||
                     Callee.getOpcode() == ISD::TargetExternalSymbol);
    unsigned Opc = isDirect ? VAX::CALLS_direct : VAX::CALLS_indir;
    // Convert arg count to a target immediate for the machine instruction.
    auto *CN = cast<ConstantSDNode>(NumArgs);
    SDValue Count = CurDAG->getTargetConstant(
        CN->getZExtValue(), SDLoc(N), MVT::i32);
    SmallVector<SDValue, 4> Ops = { Count, Callee, RegMask, Chain };

    SDNode *Call = CurDAG->getMachineNode(Opc, SDLoc(N), N->getVTList(), Ops);
    ReplaceNode(N, Call);
    return;
  }

  SelectCode(N);
}

bool VAXDAGToDAGISel::SelectVAXAddr(SDValue Addr, SDValue &Base,
                                     SDValue &Offset) {
  SDLoc DL(Addr);
  MVT PtrTy = MVT::i32;

  // FrameIndex — stack slot, zero displacement.
  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), PtrTy);
    Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
    return true;
  }

  if (Addr.getOpcode() == ISD::ADD) {
    // ADD(FrameIndex, Constant) — stack slot with displacement.
    if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr.getOperand(0)))
      if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Addr.getOperand(1))) {
        Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), PtrTy);
        Offset = CurDAG->getTargetConstant(CN->getSExtValue(), DL, MVT::i32);
        return true;
      }
    // ADD(Reg, Constant) — register + displacement.
    if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Addr.getOperand(1))) {
      Base = Addr.getOperand(0);
      Offset = CurDAG->getTargetConstant(CN->getSExtValue(), DL, MVT::i32);
      return true;
    }
  }

  // Bare register — zero displacement.
  // Exclude nodes that have their own dedicated patterns (PCRelWrapper, etc.)
  if (Addr.getOpcode() == VAXISD::PCRelWrapper)
    return false;
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
  return true;
}

namespace {

class VAXDAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  explicit VAXDAGToDAGISelLegacy(VAXTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<VAXDAGToDAGISel>(TM, OptLevel)) {}
};

} // end anonymous namespace

char VAXDAGToDAGISelLegacy::ID = 0;

INITIALIZE_PASS(VAXDAGToDAGISelLegacy, "vax-isel",
                "VAX DAG->DAG Pattern Instruction Selection", false, false)

FunctionPass *llvm::createVAXISelDag(VAXTargetMachine &TM,
                                      CodeGenOptLevel OptLevel) {
  return new VAXDAGToDAGISelLegacy(TM, OptLevel);
}
