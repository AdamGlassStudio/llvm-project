//===-- VAXAsmParser.cpp - Parse VAX assembly to MCInst instructions ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Parses GAS-syntax VAX assembly (%-prefixed registers, $-prefixed immediates,
// displacement(reg), autoincrement/autodecrement, deferred modes).
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/VAXMCTargetDesc.h"
#include "TargetInfo/VAXTargetInfo.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "vax-asm-parser"

using namespace llvm;

namespace {

/// A parsed VAX assembly operand.
class VAXOperand : public MCParsedAsmOperand {
public:
  enum KindTy {
    k_Token,
    k_Reg,
    k_Imm,
    k_Mem, // disp(base) — 2-slot: register + displacement expression
  };

private:
  KindTy Kind;

  struct MemOp {
    MCRegister Base;
    const MCExpr *Disp;
  };

  union {
    StringRef Tok;
    MCRegister Reg;
    const MCExpr *Imm;
    MemOp Mem;
  };

  SMLoc Start, End;
  bool IsRegDirect = false; // Bare register morphed to Mem

public:
  VAXOperand(KindTy K, SMLoc S, SMLoc E) : Kind(K), Start(S), End(E) {}

  // Accessors
  bool isToken() const override { return Kind == k_Token; }
  bool isReg() const override { return Kind == k_Reg; }
  bool isImm() const override { return Kind == k_Imm; }
  bool isMem() const override { return Kind == k_Mem; }

  StringRef getToken() const {
    assert(Kind == k_Token);
    return Tok;
  }

  MCRegister getReg() const override {
    assert(Kind == k_Reg);
    return Reg;
  }

  void setReg(MCRegister R) {
    assert(Kind == k_Reg);
    Reg = R;
  }

  // Morph this operand into a Mem operand (for validateTargetOperandClass).
  // regDirect=true means this was a bare register, not register deferred.
  void morphToMem(MCRegister Base, const MCExpr *Disp, bool regDirect = false) {
    Kind = k_Mem;
    Mem = {Base, Disp};
    IsRegDirect = regDirect;
  }

  bool isRegDirectMem() const { return Kind == k_Mem && IsRegDirect; }

  const MCExpr *getImm() const {
    assert(Kind == k_Imm);
    return Imm;
  }

  SMLoc getStartLoc() const override { return Start; }
  SMLoc getEndLoc() const override { return End; }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(Reg));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExprOperand(Inst, Imm);
  }

  void addMemOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    if (IsRegDirect) {
      // Bare register morphed to Mem — encode as register direct.
      // Use sentinel displacement to tell MCCodeEmitter this is register
      // direct mode, not register deferred.
      Inst.addOperand(MCOperand::createReg(Mem.Base));
      Inst.addOperand(MCOperand::createImm(INT32_MIN));
    } else {
      Inst.addOperand(MCOperand::createReg(Mem.Base));
      addExprOperand(Inst, Mem.Disp);
    }
  }

  void print(raw_ostream &O, const MCAsmInfo &MAI) const override {
    switch (Kind) {
    case k_Token:
      O << "Token: " << Tok;
      break;
    case k_Reg:
      O << "Reg: " << Reg.id();
      break;
    case k_Imm:
      O << "Imm: ";
      MAI.printExpr(O, *Imm);
      break;
    case k_Mem:
      O << "Mem: " << Mem.Base.id() << " + ";
      MAI.printExpr(O, *Mem.Disp);
      break;
    }
  }

  static std::unique_ptr<VAXOperand> createToken(StringRef Str, SMLoc S) {
    auto Op = std::make_unique<VAXOperand>(k_Token, S, S);
    Op->Tok = Str;
    return Op;
  }

  static std::unique_ptr<VAXOperand> createReg(MCRegister R, SMLoc S,
                                               SMLoc E) {
    auto Op = std::make_unique<VAXOperand>(k_Reg, S, E);
    Op->Reg = R;
    return Op;
  }

  static std::unique_ptr<VAXOperand> createImm(const MCExpr *Val, SMLoc S,
                                               SMLoc E) {
    auto Op = std::make_unique<VAXOperand>(k_Imm, S, E);
    Op->Imm = Val;
    return Op;
  }

  static std::unique_ptr<VAXOperand> createMem(MCRegister Base,
                                               const MCExpr *Disp, SMLoc S,
                                               SMLoc E) {
    auto Op = std::make_unique<VAXOperand>(k_Mem, S, E);
    Op->Mem = {Base, Disp};
    return Op;
  }

private:
  static void addExprOperand(MCInst &Inst, const MCExpr *Expr) {
    if (!Expr)
      Inst.addOperand(MCOperand::createImm(0));
    else if (const auto *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }
};

/// Parses GAS-syntax VAX assembly.
class VAXAsmParser : public MCTargetAsmParser {
  MCAsmParser &Parser;

  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;

  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  ParseStatus parseDirective(AsmToken DirectiveID) override;

  bool parseLiteralValues(unsigned Size, SMLoc L);

  bool parseOperand(OperandVector &Operands);
  ParseStatus parseMemOperand(OperandVector &Operands);

  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

  MCAsmParser &getParser() const { return Parser; }
  AsmLexer &getLexer() const { return Parser.getLexer(); }

  MCRegister matchRegister(StringRef Name);

#define GET_ASSEMBLER_HEADER
#include "VAXGenAsmMatcher.inc"

public:
  VAXAsmParser(const MCSubtargetInfo &STI, MCAsmParser &P,
               const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII), Parser(P) {
    MCAsmParserExtension::Initialize(P);
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

} // end anonymous namespace

// Auto-generated by TableGen
static MCRegister MatchRegisterName(StringRef Name);
static MCRegister MatchRegisterAltName(StringRef Name);

MCRegister VAXAsmParser::matchRegister(StringRef Name) {
  // Register names in the .td include the '%' prefix (e.g., "%r0", "%ap").
  // The lexer gives us just the identifier part after '%', so prepend it.
  std::string FullName = ("%" + Name).str();
  MCRegister Reg = MatchRegisterName(FullName);
  if (Reg)
    return Reg;
  return MatchRegisterAltName(FullName);
}

bool VAXAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                 SMLoc &EndLoc) {
  auto Res = tryParseRegister(Reg, StartLoc, EndLoc);
  if (Res.isSuccess())
    return false;
  return true;
}

ParseStatus VAXAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                           SMLoc &EndLoc) {
  // VAX registers are %-prefixed: %r0, %ap, %fp, %sp, %pc
  if (getLexer().getKind() != AsmToken::Percent)
    return ParseStatus::NoMatch;

  SMLoc S = getLexer().getLoc();
  getLexer().Lex(); // eat '%'

  if (getLexer().getKind() != AsmToken::Identifier)
    return ParseStatus::Failure;

  StringRef Name = getLexer().getTok().getIdentifier();
  Reg = matchRegister(Name);
  if (!Reg)
    return ParseStatus::Failure;

  StartLoc = S;
  EndLoc = getLexer().getTok().getEndLoc();
  getLexer().Lex(); // eat register name
  return ParseStatus::Success;
}

bool VAXAsmParser::matchAndEmitInstruction(SMLoc Loc, unsigned &Opcode,
                                           OperandVector &Operands,
                                           MCStreamer &Out,
                                           uint64_t &ErrorInfo,
                                           bool MatchingInlineAsm) {
  MCInst Inst;
  unsigned MatchResult =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);

  switch (MatchResult) {
  case Match_Success:
    Inst.setLoc(Loc);
    Out.emitInstruction(Inst, *STI);
    return false;
  case Match_MnemonicFail:
    return Error(Loc, "invalid instruction mnemonic");
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = Loc;
    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(ErrorLoc, "too few operands for instruction");
      ErrorLoc =
          static_cast<VAXOperand &>(*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = Loc;
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }
  default:
    return true;
  }
}

bool VAXAsmParser::parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                                    SMLoc NameLoc,
                                    OperandVector &Operands) {
  // Mnemonic is the first token operand.
  Operands.push_back(VAXOperand::createToken(Name, NameLoc));

  // If no more operands, we're done (e.g., "ret", "nop", "halt").
  if (getLexer().is(AsmToken::EndOfStatement))
    return false;

  // Parse first operand.
  if (parseOperand(Operands))
    return true;

  // Parse remaining comma-separated operands.
  while (getLexer().is(AsmToken::Comma)) {
    getLexer().Lex(); // eat comma
    if (parseOperand(Operands))
      return true;
  }

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    getParser().eatToEndOfStatement();
    return Error(Loc, "unexpected token in operand list");
  }

  return false;
}

/// Parse a single operand. VAX GAS syntax:
///   $expr        — immediate
///   %reg         — register
///   (%reg)       — register deferred
///   (%reg)+      — autoincrement
///   -(%reg)      — autodecrement
///   expr(%reg)   — displacement
///   *expr        — deferred (absolute or displacement deferred)
///   expr         — symbol/label (PC-relative)
///   (expr)       — parenthesized expression (calls uses this)
bool VAXAsmParser::parseOperand(OperandVector &Operands) {
  SMLoc StartLoc = getLexer().getLoc();

  // Case 1: $expr — immediate
  if (getLexer().is(AsmToken::Dollar)) {
    getLexer().Lex(); // eat '$'
    const MCExpr *Expr;
    if (getParser().parseExpression(Expr))
      return true;
    Operands.push_back(VAXOperand::createImm(Expr, StartLoc,
                                             getLexer().getLoc()));
    return false;
  }

  // Case 2: %reg — register (possibly followed by addressing mode syntax)
  if (getLexer().is(AsmToken::Percent)) {
    MCRegister Reg;
    SMLoc RegStart, RegEnd;
    if (tryParseRegister(Reg, RegStart, RegEnd).isSuccess()) {
      Operands.push_back(VAXOperand::createReg(Reg, RegStart, RegEnd));
      return false;
    }
    return Error(StartLoc, "expected register after '%'");
  }

  // Case 3: -(%reg) — autodecrement
  if (getLexer().is(AsmToken::Minus)) {
    SMLoc MinusLoc = getLexer().getLoc();
    getLexer().Lex(); // eat '-'
    if (getLexer().is(AsmToken::LParen)) {
      getLexer().Lex(); // eat '('
      MCRegister Reg;
      SMLoc RS, RE;
      if (tryParseRegister(Reg, RS, RE).isFailure())
        return Error(RS, "expected register in autodecrement");
      if (getLexer().isNot(AsmToken::RParen))
        return Error(getLexer().getLoc(), "expected ')'");
      SMLoc EndLoc = getLexer().getLoc();
      getLexer().Lex(); // eat ')'
      // Encode autodecrement as Mem with base=Reg and magic displacement.
      // The MCCodeEmitter recognizes this pattern.
      // For now, use a register operand — the encoder handles autodecrement
      // via the addressing mode specifier byte.
      Operands.push_back(VAXOperand::createMem(
          Reg, MCConstantExpr::create(INT64_MIN, getContext()), MinusLoc,
          EndLoc));
      return false;
    }
    // Not autodecrement — put '-' back and parse as expression
    // Actually, we already consumed the '-'. Parse as negative expression.
    const MCExpr *Expr;
    if (getParser().parseExpression(Expr))
      return true;
    // Negate
    Expr = MCUnaryExpr::createMinus(Expr, getContext());
    // Check for (reg) suffix — displacement mode
    if (getLexer().is(AsmToken::LParen)) {
      getLexer().Lex(); // eat '('
      MCRegister Reg;
      SMLoc RS, RE;
      if (tryParseRegister(Reg, RS, RE).isFailure())
        return Error(RS, "expected register");
      if (getLexer().isNot(AsmToken::RParen))
        return Error(getLexer().getLoc(), "expected ')'");
      getLexer().Lex(); // eat ')'
      Operands.push_back(
          VAXOperand::createMem(Reg, Expr, MinusLoc, getLexer().getLoc()));
      return false;
    }
    // Bare negative expression — label/symbol
    Operands.push_back(
        VAXOperand::createImm(Expr, MinusLoc, getLexer().getLoc()));
    return false;
  }

  // Case 4: *... — deferred addressing
  if (getLexer().is(AsmToken::Star)) {
    getLexer().Lex(); // eat '*'
    // Deferred can wrap any address mode — for now parse the inner operand
    // and pass through. The encoder handles deferred via operand specifier.
    // For the AsmParser, treat *(%reg) as register deferred,
    // *expr(%reg) as displacement deferred, *expr as absolute deferred.
    // TODO: Full deferred mode support.
    return parseOperand(Operands);
  }

  // Case 5: (%reg) or (%reg)+ — register deferred or autoincrement
  if (getLexer().is(AsmToken::LParen)) {
    getLexer().Lex(); // eat '('

    // Check if it's a register inside
    if (getLexer().is(AsmToken::Percent)) {
      MCRegister Reg;
      SMLoc RS, RE;
      if (tryParseRegister(Reg, RS, RE).isFailure())
        return Error(RS, "expected register");
      if (getLexer().isNot(AsmToken::RParen))
        return Error(getLexer().getLoc(), "expected ')'");
      getLexer().Lex(); // eat ')'

      // Check for autoincrement '+'
      if (getLexer().is(AsmToken::Plus)) {
        getLexer().Lex(); // eat '+'
        // Encode autoincrement as Mem with base=Reg and magic displacement.
        Operands.push_back(VAXOperand::createMem(
            Reg, MCConstantExpr::create(INT64_MAX, getContext()), StartLoc,
            getLexer().getLoc()));
        return false;
      }

      // Register deferred: (%reg) = 0(%reg)
      Operands.push_back(VAXOperand::createMem(
          Reg, MCConstantExpr::create(0, getContext()), StartLoc,
          getLexer().getLoc()));
      return false;
    }

    // Not a register — could be a parenthesized expression like (expr)
    // used in e.g., "calls $2, (expr)" or "jmp (expr)".
    // Parse as expression, then expect ')'
    const MCExpr *Expr;
    if (getParser().parseExpression(Expr))
      return true;
    if (getLexer().isNot(AsmToken::RParen))
      return Error(getLexer().getLoc(), "expected ')'");
    getLexer().Lex(); // eat ')'
    // This is either register deferred via expression or a parenthesized
    // expression for CALLS indirect. Treat as immediate (the encoder
    // determines the actual addressing mode).
    Operands.push_back(
        VAXOperand::createImm(Expr, StartLoc, getLexer().getLoc()));
    return false;
  }

  // Case 6: expr or expr(%reg) — symbol/label or displacement
  {
    const MCExpr *Expr;
    if (getParser().parseExpression(Expr))
      return true;

    // Check for (%reg) suffix — displacement mode
    if (getLexer().is(AsmToken::LParen)) {
      getLexer().Lex(); // eat '('
      MCRegister Reg;
      SMLoc RS, RE;
      if (tryParseRegister(Reg, RS, RE).isFailure())
        return Error(RS, "expected register");
      if (getLexer().isNot(AsmToken::RParen))
        return Error(getLexer().getLoc(), "expected ')'");
      getLexer().Lex(); // eat ')'
      Operands.push_back(
          VAXOperand::createMem(Reg, Expr, StartLoc, getLexer().getLoc()));
      return false;
    }

    // Bare expression — immediate value or PC-relative symbol/label
    Operands.push_back(
        VAXOperand::createImm(Expr, StartLoc, getLexer().getLoc()));
    return false;
  }
}

ParseStatus VAXAsmParser::parseMemOperand(OperandVector &Operands) {
  // This is called by the TableGen-generated matcher for VAXMemOp operands.
  // We delegate to the general operand parser; it creates Mem operands
  // for displacement(reg) forms.
  if (parseOperand(Operands))
    return ParseStatus::Failure;

  // Verify the last operand is a memory operand.
  // Note: bare registers and immediates that should be VAXMemOp are handled
  // by validateTargetOperandClass during matching, not here.
  auto &Last = static_cast<VAXOperand &>(*Operands.back());
  if (!Last.isMem())
    return ParseStatus::NoMatch;

  return ParseStatus::Success;
}

static MCRegister convertGPRToQPR(MCRegister Reg) {
  switch (Reg.id()) {
  default: return MCRegister();
  case VAX::R0:  return VAX::R0_R1;
  case VAX::R1:  return VAX::R1_R2;
  case VAX::R2:  return VAX::R2_R3;
  case VAX::R3:  return VAX::R3_R4;
  case VAX::R4:  return VAX::R4_R5;
  case VAX::R5:  return VAX::R5_R6;
  case VAX::R6:  return VAX::R6_R7;
  case VAX::R7:  return VAX::R7_R8;
  case VAX::R8:  return VAX::R8_R9;
  case VAX::R9:  return VAX::R9_R10;
  case VAX::R10: return VAX::R10_R11;
  }
}

ParseStatus VAXAsmParser::parseDirective(AsmToken DirectiveID) {
  StringRef IDVal = DirectiveID.getIdentifier();
  if (IDVal.equals_insensitive(".word"))
    return parseLiteralValues(2, DirectiveID.getLoc());
  if (IDVal.equals_insensitive(".long"))
    return parseLiteralValues(4, DirectiveID.getLoc());
  if (IDVal.equals_insensitive(".byte"))
    return parseLiteralValues(1, DirectiveID.getLoc());
  return ParseStatus::NoMatch;
}

bool VAXAsmParser::parseLiteralValues(unsigned Size, SMLoc L) {
  auto parseOne = [&]() -> bool {
    const MCExpr *Value;
    if (getParser().parseExpression(Value))
      return true;
    getParser().getStreamer().emitValue(Value, Size, L);
    return false;
  };
  return parseMany(parseOne);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVAXAsmParser() {
  RegisterMCAsmParser<VAXAsmParser> X(getTheVAXTarget());
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "VAXGenAsmMatcher.inc"

unsigned VAXAsmParser::validateTargetOperandClass(MCParsedAsmOperand &AsmOp,
                                                  unsigned Kind) {
  VAXOperand &Op = static_cast<VAXOperand &>(AsmOp);

  // The matcher expects MCK_Mem (VAXMemOp) but we may have parsed a bare
  // register or immediate. VAX operand specifiers are uniform — convert:
  //   %reg  → Mem{Base=reg, Disp=0}   (register direct)
  //   $imm  → Mem{Base=NoReg, Disp=imm} (immediate/literal)
  if (Kind == MCK_Mem) {
    if (Op.isReg()) {
      Op.morphToMem(Op.getReg(), nullptr, /*regDirect=*/true);
      return Match_Success;
    }
    if (Op.isImm()) {
      Op.morphToMem(MCRegister(), Op.getImm());
      return Match_Success;
    }
  }

  if (!Op.isReg())
    return Match_InvalidOperand;

  // The matcher expects MCK_QPR but we parsed a GPR; convert.
  if (Kind == MCK_QPR) {
    MCRegister QPR = convertGPRToQPR(Op.getReg());
    if (QPR) {
      Op.setReg(QPR);
      return Match_Success;
    }
  }
  return Match_InvalidOperand;
}
