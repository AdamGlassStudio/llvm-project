//===-- VAXAsmPrinter.cpp - VAX LLVM assembly writer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VAX.h"
#include "VAXSubtarget.h"
#include "VAXTargetMachine.h"
#include "MCTargetDesc/VAXBaseInfo.h"
#include "MCTargetDesc/VAXInstPrinter.h"
#include "MCTargetDesc/VAXMCAsmInfo.h"
#include "TargetInfo/VAXTargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLoweringObjectFile.h"

using namespace llvm;

// IEEE 754 single → VAX F_float (32-bit) conversion.
// Both formats: sign(1) + exp(8) + frac(23). Differences:
//   IEEE bias=127, VAX bias=128; VAX stores with 16-bit word swap.
//   VAX value = 0.1{frac} × 2^(exp-128), IEEE = 1.{frac} × 2^(exp-127)
//   So VAX_exp = IEEE_exp + 2, then swap the two 16-bit halves.
static uint32_t convertIEEEToVAXF(uint32_t IEEE) {
  uint32_t Sign = (IEEE >> 31) & 1;
  uint32_t Exp = (IEEE >> 23) & 0xFF;
  uint32_t Frac = IEEE & 0x7FFFFF;

  if (Exp == 0) return 0;    // zero or denorm → VAX zero
  if (Exp == 0xFF) return 0; // inf/nan → no VAX equivalent

  uint32_t VaxExp = Exp + 2;
  if (VaxExp > 255) return 0;

  uint16_t W0 = (Sign << 15) | (VaxExp << 7) | ((Frac >> 16) & 0x7F);
  uint16_t W1 = Frac & 0xFFFF;
  return (uint32_t(W1) << 16) | W0;
}

// IEEE 754 double → VAX D_float (64-bit) conversion.
// IEEE: sign(1) + exp(11, bias 1023) + frac(52).
// D_float: sign(1) + exp(8, bias 128) + frac(55), 16-bit word-swapped.
static uint64_t convertIEEEToVAXD(uint64_t IEEE) {
  uint64_t Sign = (IEEE >> 63) & 1;
  uint64_t Exp = (IEEE >> 52) & 0x7FF;
  uint64_t Frac = IEEE & 0xFFFFFFFFFFFFFULL;

  if (Exp == 0) return 0;
  if (Exp == 0x7FF) return 0;

  int VaxExp = (int)Exp - 894; // IEEE bias 1023 → VAX bias 128, +1 for 0.1 form
  if (VaxExp <= 0 || VaxExp > 255) return 0;

  uint64_t VaxFrac = Frac << 3; // 52 → 55 bits, zero-fill bottom 3

  uint16_t W0 = (Sign << 15) | (VaxExp << 7) | ((VaxFrac >> 48) & 0x7F);
  uint16_t W1 = (VaxFrac >> 32) & 0xFFFF;
  uint16_t W2 = (VaxFrac >> 16) & 0xFFFF;
  uint16_t W3 = VaxFrac & 0xFFFF;

  return (uint64_t(W3) << 48) | (uint64_t(W2) << 32) |
         (uint64_t(W1) << 16) | W0;
}

#define DEBUG_TYPE "asm-printer"

namespace {

class VAXAsmPrinter : public AsmPrinter {
public:
  explicit VAXAsmPrinter(TargetMachine &TM,
                          std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)) {}

  StringRef getPassName() const override { return "VAX Assembly Printer"; }

  void emitFunctionBodyStart() override;
  void emitInstruction(const MachineInstr *MI) override;
  void emitGlobalVariable(const GlobalVariable *GV) override;
  void emitConstantPool() override;

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override;

private:
  void printOperand(const MachineInstr *MI, unsigned OpNo, raw_ostream &OS);
  void emitVAXGlobalConstant(const DataLayout &DL, const Constant *CV);
};

} // end anonymous namespace

void VAXAsmPrinter::emitFunctionBodyStart() {
  // Emit the 2-byte entry mask immediately after the function label.
  // The CALLS/CALLG instruction reads this word on every call to decide
  // which of R0–R11 to save; only callee-saved regs (R6–R11) should appear.
  const MCRegisterInfo *MRI = TM.getMCRegisterInfo();
  const MachineFrameInfo &MFI = MF->getFrameInfo();
  uint16_t Mask = 0;
  for (const CalleeSavedInfo &CSI : MFI.getCalleeSavedInfo()) {
    unsigned Enc = MRI->getEncodingValue(CSI.getReg());
    if (Enc <= 11) // only R0–R11 appear in the entry mask
      Mask |= 1u << Enc;
  }
  OutStreamer->emitIntValue(Mask, 2);
}

void VAXAsmPrinter::emitInstruction(const MachineInstr *MI) {
  // Expand LEA_FI pseudo: addl3 $offset, %fp, $dst
  if (MI->getOpcode() == VAX::LEA_FI) {
    MCInst Inst;
    Inst.setOpcode(VAX::ADDL3_ri);
    // Operand layout: ADDL3_ri (outs $dst), (ins i32imm:$a, GPRnoPC:$b)
    // LEA_FI: op0=$dst, op1=base(FP), op2=disp
    Inst.addOperand(MCOperand::createReg(MI->getOperand(0).getReg())); // dst
    Inst.addOperand(MCOperand::createImm(MI->getOperand(2).getImm())); // offset
    Inst.addOperand(MCOperand::createReg(MI->getOperand(1).getReg())); // FP
    EmitToStreamer(*OutStreamer, Inst);
    return;
  }

  // Expand CASEL pseudo: emit CASEL_L MCInst + inline word displacement table.
  if (MI->getOpcode() == VAX::CASEL) {
    Register IndexReg = MI->getOperand(0).getReg();
    int64_t Limit = MI->getOperand(1).getImm();
    unsigned JTI = MI->getOperand(2).getIndex();

    // Emit: casel %rN, $0, $limit as a proper MCInst.
    MCInst CaselInst;
    CaselInst.setOpcode(VAX::CASEL_L);
    CaselInst.addOperand(MCOperand::createReg(IndexReg));
    CaselInst.addOperand(MCOperand::createImm(0));
    CaselInst.addOperand(MCOperand::createImm(Limit));
    OutStreamer->emitInstruction(CaselInst, getSubtargetInfo());

    // Emit the word displacement table: .word target - tablebase
    // CASEL sets PC to the start of the table, then adds sign-extended
    // word displacement. All entries are relative to the table base.
    const MachineJumpTableInfo *MJTI = MF->getJumpTableInfo();
    const std::vector<MachineJumpTableEntry> &JT = MJTI->getJumpTables();
    const std::vector<MachineBasicBlock *> &JTBBs = JT[JTI].MBBs;

    // Label at the start of the displacement table.
    MCSymbol *TableBase = OutContext.createTempSymbol();
    OutStreamer->emitLabel(TableBase);

    for (MachineBasicBlock *MBB : JTBBs) {
      // Emit .word (target - tablebase) as a 16-bit value.
      const MCExpr *Expr = MCBinaryExpr::createSub(
          MCSymbolRefExpr::create(MBB->getSymbol(), OutContext),
          MCSymbolRefExpr::create(TableBase, OutContext), OutContext);
      OutStreamer->emitValue(Expr, 2);
    }
    return;
  }

  MCInst Inst;
  Inst.setOpcode(MI->getOpcode());
  for (const MachineOperand &MO : MI->explicit_operands()) {
    switch (MO.getType()) {
    case MachineOperand::MO_Register:
      Inst.addOperand(MCOperand::createReg(MO.getReg()));
      break;
    case MachineOperand::MO_Immediate:
      Inst.addOperand(MCOperand::createImm(MO.getImm()));
      break;
    case MachineOperand::MO_GlobalAddress: {
      const MCSymbol *Sym = getSymbol(MO.getGlobal());
      unsigned TF = MO.getTargetFlags();
      unsigned Spec = VAX::S_None;
      if (TF == VAXII::MO_GOT)
        Spec = VAX::S_GOT;
      else if (TF == VAXII::MO_PLT)
        Spec = VAX::S_PLT;
      const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Spec, OutContext);
      if (MO.getOffset())
        Expr = MCBinaryExpr::createAdd(
            Expr, MCConstantExpr::create(MO.getOffset(), OutContext),
            OutContext);
      Inst.addOperand(MCOperand::createExpr(Expr));
      break;
    }
    case MachineOperand::MO_ExternalSymbol: {
      const MCSymbol *Sym = GetExternalSymbolSymbol(MO.getSymbolName());
      unsigned TF = MO.getTargetFlags();
      unsigned Spec = VAX::S_None;
      if (TF == VAXII::MO_PLT)
        Spec = VAX::S_PLT;
      else if (TF == VAXII::MO_GOT)
        Spec = VAX::S_GOT;
      Inst.addOperand(
          MCOperand::createExpr(MCSymbolRefExpr::create(Sym, Spec, OutContext)));
      break;
    }
    case MachineOperand::MO_MachineBasicBlock: {
      // Branch target — emit the basic block label as a symbol reference.
      const MCSymbol *Sym = MO.getMBB()->getSymbol();
      Inst.addOperand(
          MCOperand::createExpr(MCSymbolRefExpr::create(Sym, OutContext)));
      break;
    }
    case MachineOperand::MO_RegisterMask:
      // Register mask is register-allocator metadata; not emitted to assembly.
      break;
    case MachineOperand::MO_JumpTableIndex: {
      SmallString<64> Name;
      raw_svector_ostream(Name) << MAI->getPrivateGlobalPrefix()
                                << "JTI" << getFunctionNumber()
                                << '_' << MO.getIndex();
      const MCSymbol *Sym = OutContext.getOrCreateSymbol(Name);
      Inst.addOperand(
          MCOperand::createExpr(MCSymbolRefExpr::create(Sym, OutContext)));
      break;
    }
    case MachineOperand::MO_ConstantPoolIndex: {
      const MCSymbol *Sym = GetCPISymbol(MO.getIndex());
      const MCExpr *Expr = MCSymbolRefExpr::create(Sym, OutContext);
      if (MO.getOffset())
        Expr = MCBinaryExpr::createAdd(
            Expr, MCConstantExpr::create(MO.getOffset(), OutContext),
            OutContext);
      Inst.addOperand(MCOperand::createExpr(Expr));
      break;
    }
    default:
      report_fatal_error("VAXAsmPrinter: unsupported MachineOperand type");
    }
  }
  EmitToStreamer(*OutStreamer, Inst);
}

// Emit a Constant, converting any FP values from IEEE to VAX format.
// For non-FP constants, delegates to the standard emitGlobalConstant.
void VAXAsmPrinter::emitVAXGlobalConstant(const DataLayout &DL,
                                           const Constant *CV) {
  if (const auto *CFP = dyn_cast<ConstantFP>(CV)) {
    APFloat APF = CFP->getValueAPF();
    APInt API = APF.bitcastToAPInt();

    if (isVerbose()) {
      SmallString<8> StrVal;
      APF.toString(StrVal);
      OutStreamer->getCommentOS() << (API.getBitWidth() == 32 ? "F_float "
                                                              : "D_float ")
                                  << StrVal << '\n';
    }

    if (API.getBitWidth() == 32) {
      uint32_t VaxBits = convertIEEEToVAXF(API.getZExtValue());
      OutStreamer->emitIntValue(VaxBits, 4);
    } else if (API.getBitWidth() == 64) {
      uint64_t VaxBits = convertIEEEToVAXD(API.getZExtValue());
      OutStreamer->emitIntValue(VaxBits & 0xFFFFFFFF, 4);
      OutStreamer->emitIntValue(VaxBits >> 32, 4);
    } else {
      // Unsupported FP width — fall back to generic emission.
      emitGlobalConstant(DL, CV);
    }
    return;
  }

  // For aggregates (arrays, structs), recurse into elements.
  if (const auto *CA = dyn_cast<ConstantAggregate>(CV)) {
    for (unsigned I = 0, E = CA->getNumOperands(); I != E; ++I)
      emitVAXGlobalConstant(DL, CA->getOperand(I));
    // Emit tail padding if needed.
    uint64_t Size = DL.getTypeAllocSize(CV->getType());
    uint64_t EmittedSize = 0;
    for (unsigned I = 0, E = CA->getNumOperands(); I != E; ++I)
      EmittedSize += DL.getTypeAllocSize(CA->getOperand(I)->getType());
    if (Size > EmittedSize)
      OutStreamer->emitZeros(Size - EmittedSize);
    return;
  }

  if (const auto *CDS = dyn_cast<ConstantDataSequential>(CV)) {
    // ConstantDataArray/ConstantDataVector of float/double elements.
    Type *EltTy = CDS->getElementType();
    if (EltTy->isFloatTy()) {
      for (unsigned I = 0, E = CDS->getNumElements(); I != E; ++I) {
        APFloat APF = CDS->getElementAsAPFloat(I);
        uint32_t VaxBits = convertIEEEToVAXF(
            APF.bitcastToAPInt().getZExtValue());
        OutStreamer->emitIntValue(VaxBits, 4);
      }
      return;
    }
    if (EltTy->isDoubleTy()) {
      for (unsigned I = 0, E = CDS->getNumElements(); I != E; ++I) {
        APFloat APF = CDS->getElementAsAPFloat(I);
        uint64_t VaxBits = convertIEEEToVAXD(
            APF.bitcastToAPInt().getZExtValue());
        OutStreamer->emitIntValue(VaxBits & 0xFFFFFFFF, 4);
        OutStreamer->emitIntValue(VaxBits >> 32, 4);
      }
      return;
    }
    // Non-FP data sequences: fall through to generic.
  }

  // Non-FP constant: use default emission.
  emitGlobalConstant(DL, CV);
}

void VAXAsmPrinter::emitGlobalVariable(const GlobalVariable *GV) {
  if (!GV->hasInitializer()) {
    AsmPrinter::emitGlobalVariable(GV);
    return;
  }

  // Check if the initializer contains any FP constants.
  Type *Ty = GV->getValueType();
  bool HasFP = Ty->isFloatingPointTy();
  if (!HasFP) {
    // Check aggregate types for FP elements.
    if (auto *AT = dyn_cast<ArrayType>(Ty))
      HasFP = AT->getElementType()->isFloatingPointTy();
    else if (auto *ST = dyn_cast<StructType>(Ty)) {
      for (Type *EltTy : ST->elements())
        if (EltTy->isFloatingPointTy()) { HasFP = true; break; }
    }
  }

  if (!HasFP) {
    AsmPrinter::emitGlobalVariable(GV);
    return;
  }

  // FP global: emit everything the base does except the constant data,
  // then emit the data ourselves with VAX FP conversion.
  // We replicate the essential parts of AsmPrinter::emitGlobalVariable.
  MCSymbol *GVSym = getSymbol(GV);
  emitVisibility(GVSym, GV->getVisibility(), !GV->isDeclaration());

  GVSym->redefineIfPossible();

  const DataLayout &DL = GV->getDataLayout();
  uint64_t Size = DL.getTypeAllocSize(GV->getValueType());
  Align Alignment = GV->getAlign().value_or(DL.getPreferredAlign(GV));

  MCSection *TheSection =
      getObjFileLowering().SectionForGlobal(GV, TM);
  OutStreamer->switchSection(TheSection);

  emitAlignment(Alignment, GV);
  OutStreamer->emitLabel(GVSym);

  if (GV->hasLocalLinkage())
    OutStreamer->emitSymbolAttribute(GVSym, MCSA_Local);
  if (GV->getLinkage() == GlobalValue::ExternalLinkage ||
      GV->getLinkage() == GlobalValue::WeakAnyLinkage ||
      GV->getLinkage() == GlobalValue::WeakODRLinkage)
    OutStreamer->emitSymbolAttribute(GVSym, MCSA_Global);

  emitVAXGlobalConstant(DL, GV->getInitializer());

  OutStreamer->emitELFSize(GVSym,
                           MCConstantExpr::create(Size, OutContext));
}

void VAXAsmPrinter::emitConstantPool() {
  const MachineConstantPool *MCP = MF->getConstantPool();
  const std::vector<MachineConstantPoolEntry> &CP = MCP->getConstants();
  if (CP.empty()) return;

  const DataLayout &DL = getDataLayout();

  for (unsigned i = 0, e = CP.size(); i != e; ++i) {
    const MachineConstantPoolEntry &CPE = CP[i];
    MCSymbol *Sym = GetCPISymbol(i);
    if (!Sym->isUndefined())
      continue;

    Align Alignment = CPE.getAlign();
    SectionKind Kind = CPE.getSectionKind(&DL);

    const Constant *C = nullptr;
    if (!CPE.isMachineConstantPoolEntry())
      C = CPE.Val.ConstVal;

    MCSection *S = getObjFileLowering().getSectionForConstant(
        DL, Kind, C, Alignment);
    OutStreamer->switchSection(S);
    emitAlignment(Alignment);
    OutStreamer->emitLabel(Sym);

    if (CPE.isMachineConstantPoolEntry()) {
      emitMachineConstantPoolValue(CPE.Val.MachineCPVal);
    } else {
      emitVAXGlobalConstant(DL, CPE.Val.ConstVal);
    }
  }
}

void VAXAsmPrinter::printOperand(const MachineInstr *MI, unsigned OpNo,
                                 raw_ostream &OS) {
  const MachineOperand &MO = MI->getOperand(OpNo);
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    // getRegisterName already includes the '%' prefix.
    OS << VAXInstPrinter::getRegisterName(MO.getReg());
    break;
  case MachineOperand::MO_Immediate:
    OS << '$' << MO.getImm();
    break;
  case MachineOperand::MO_GlobalAddress:
    PrintSymbolOperand(MO, OS);
    break;
  case MachineOperand::MO_ExternalSymbol:
    OS << *GetExternalSymbolSymbol(MO.getSymbolName());
    break;
  case MachineOperand::MO_MachineBasicBlock:
    MO.getMBB()->getSymbol()->print(OS, MAI);
    break;
  default:
    llvm_unreachable("VAXAsmPrinter::printOperand: unsupported operand type");
  }
}

bool VAXAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                     const char *ExtraCode, raw_ostream &OS) {
  if (!ExtraCode || !ExtraCode[0]) {
    printOperand(MI, OpNo, OS);
    return false;
  }
  return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, OS);
}

bool VAXAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                                           const char *ExtraCode,
                                           raw_ostream &OS) {
  const MachineOperand &MO = MI->getOperand(OpNo);
  if (MO.isReg()) {
    OS << "(" << VAXInstPrinter::getRegisterName(MO.getReg()) << ")";
    return false;
  }
  return true;
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVAXAsmPrinter() {
  RegisterAsmPrinter<VAXAsmPrinter> X(getTheVAXTarget());
}
