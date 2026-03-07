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
#include "VAX.h"
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
    k_Mem, // 4-slot: base, disp, index, flags (VAXAM::*)
  };

private:
  KindTy Kind;

  struct MemOp {
    MCRegister Base;
    const MCExpr *Disp;
    MCRegister Index; // NoReg (0) = no indexing
    unsigned Flags;   // VAXAM::Disp, VAXAM::RegDirect, etc.
  };

  union {
    StringRef Tok;
    MCRegister Reg;
    const MCExpr *Imm;
    MemOp Mem;
  };

  SMLoc Start, End;

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
  void morphToMem(MCRegister Base, const MCExpr *Disp, unsigned Flags) {
    Kind = k_Mem;
    Mem = {Base, Disp, MCRegister(), Flags};
  }

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
    assert(N == 4 && "VAXMemOp requires 4 MCOperands");
    Inst.addOperand(MCOperand::createReg(Mem.Base));
    addExprOperand(Inst, Mem.Disp);
    Inst.addOperand(MCOperand::createReg(Mem.Index));
    Inst.addOperand(MCOperand::createImm(Mem.Flags));
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
      O << "Mem(flags=" << Mem.Flags << "): base=" << Mem.Base.id();
      if (Mem.Disp) {
        O << " disp=";
        MAI.printExpr(O, *Mem.Disp);
      }
      if (Mem.Index)
        O << " idx=" << Mem.Index.id();
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
                                               const MCExpr *Disp,
                                               MCRegister Index,
                                               unsigned Flags,
                                               SMLoc S, SMLoc E) {
    auto Op = std::make_unique<VAXOperand>(k_Mem, S, E);
    Op->Mem = {Base, Disp, Index, Flags};
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
  MCRegister tryParseIndexSuffix();
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
  // GAS branch/jump aliases: explicit mapping table.
  // GAS accepts "j<cond>" as aliases for "b<cond>" branch instructions,
  // "jbr" for "brw", and "bcc"/"bcs" for "bgequ"/"blssu".
  StringRef Mnemonic = Name;
  StringRef Alias = StringSwitch<StringRef>(Mnemonic)
    .Case("jbr",   "brw")
    .Case("jeql",  "beql")
    .Case("jneq",  "bneq")
    .Case("jgtr",  "bgtr")
    .Case("jgeq",  "bgeq")
    .Case("jlss",  "blss")
    .Case("jleq",  "bleq")
    .Case("jgtru", "bgtru")
    .Case("jgequ", "bgequ")
    .Case("jlssu", "blssu")
    .Case("jlequ", "blequ")
    .Case("jcc",   "bgequ")
    .Case("jcs",   "blssu")
    .Case("bcc",   "bgequ")
    .Case("bcs",   "blssu")
    .Default(StringRef());
  if (!Alias.empty())
    Mnemonic = Alias;

  // Mnemonic is the first token operand.
  Operands.push_back(VAXOperand::createToken(Mnemonic, NameLoc));

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

/// Try to parse an indexed suffix [%reg]. Returns the index register,
/// or NoReg if no bracket follows. Consumes '[', '%reg', ']' on success.
MCRegister VAXAsmParser::tryParseIndexSuffix() {
  if (getLexer().isNot(AsmToken::LBrac))
    return MCRegister();
  // Speculatively consume '['
  getLexer().Lex(); // eat '['
  MCRegister Reg;
  SMLoc RS, RE;
  if (tryParseRegister(Reg, RS, RE).isFailure()) {
    // Not a valid index — cannot put '[' back, but this shouldn't happen
    // in valid VAX assembly.
    Error(RS, "expected register in index");
    return MCRegister();
  }
  if (getLexer().isNot(AsmToken::RBrac)) {
    Error(getLexer().getLoc(), "expected ']'");
    return MCRegister();
  }
  getLexer().Lex(); // eat ']'
  return Reg;
}

/// Parse a single operand. VAX GAS syntax:
///   $expr          — immediate
///   %reg           — register
///   (%reg)         — register deferred
///   (%reg)+        — autoincrement
///   -(%reg)        — autodecrement
///   expr(%reg)     — displacement
///   *expr          — absolute deferred
///   *(%reg)        — autoincrement deferred (same as reg deferred for now)
///   *expr(%reg)    — displacement deferred
///   expr           — symbol/label (PC-relative)
///   (expr)         — parenthesized expression (calls uses this)
///   ....[%reg]     — indexed suffix on any of the above
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
      MCRegister Idx = tryParseIndexSuffix();
      Operands.push_back(VAXOperand::createMem(
          Reg, nullptr, Idx, VAXAM::AutoDec, MinusLoc, EndLoc));
      return false;
    }
    // Not autodecrement — already consumed '-'. Parse as negative expression.
    const MCExpr *Expr;
    if (getParser().parseExpression(Expr))
      return true;
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
      MCRegister Idx = tryParseIndexSuffix();
      Operands.push_back(VAXOperand::createMem(
          Reg, Expr, Idx, VAXAM::Disp, MinusLoc, getLexer().getLoc()));
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

    // *(%reg) or *(%reg)+ — autoincrement deferred or register deferred
    if (getLexer().is(AsmToken::LParen)) {
      getLexer().Lex(); // eat '('
      if (getLexer().is(AsmToken::Percent)) {
        MCRegister Reg;
        SMLoc RS, RE;
        if (tryParseRegister(Reg, RS, RE).isFailure())
          return Error(RS, "expected register");
        if (getLexer().isNot(AsmToken::RParen))
          return Error(getLexer().getLoc(), "expected ')'");
        getLexer().Lex(); // eat ')'
        if (getLexer().is(AsmToken::Plus)) {
          getLexer().Lex(); // eat '+'
          MCRegister Idx = tryParseIndexSuffix();
          Operands.push_back(VAXOperand::createMem(
              Reg, nullptr, Idx, VAXAM::AutoIncDef, StartLoc,
              getLexer().getLoc()));
          return false;
        }
        // *(%reg) = register deferred (same encoding as 0 displacement deferred)
        MCRegister Idx = tryParseIndexSuffix();
        Operands.push_back(VAXOperand::createMem(
            Reg, MCConstantExpr::create(0, getContext()), Idx,
            VAXAM::DispDeferred, StartLoc, getLexer().getLoc()));
        return false;
      }
      // *(expr) — parenthesized deferred expression
      const MCExpr *Expr;
      if (getParser().parseExpression(Expr))
        return true;
      if (getLexer().isNot(AsmToken::RParen))
        return Error(getLexer().getLoc(), "expected ')'");
      getLexer().Lex(); // eat ')'
      MCRegister Idx = tryParseIndexSuffix();
      Operands.push_back(VAXOperand::createMem(
          MCRegister(), Expr, Idx, VAXAM::Absolute, StartLoc,
          getLexer().getLoc()));
      return false;
    }

    // *expr or *expr(%reg) or *expr[%reg]
    const MCExpr *Expr;
    if (getParser().parseExpression(Expr))
      return true;

    // *expr(%reg) — displacement deferred
    if (getLexer().is(AsmToken::LParen)) {
      getLexer().Lex(); // eat '('
      MCRegister Reg;
      SMLoc RS, RE;
      if (tryParseRegister(Reg, RS, RE).isFailure())
        return Error(RS, "expected register");
      if (getLexer().isNot(AsmToken::RParen))
        return Error(getLexer().getLoc(), "expected ')'");
      getLexer().Lex(); // eat ')'
      MCRegister Idx = tryParseIndexSuffix();
      Operands.push_back(VAXOperand::createMem(
          Reg, Expr, Idx, VAXAM::DispDeferred, StartLoc,
          getLexer().getLoc()));
      return false;
    }

    // *expr[%reg] — absolute deferred, possibly indexed
    MCRegister Idx = tryParseIndexSuffix();
    // *expr = absolute deferred (0x9F + addr)
    Operands.push_back(VAXOperand::createMem(
        MCRegister(), Expr, Idx, VAXAM::Absolute, StartLoc,
        getLexer().getLoc()));
    return false;
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
        MCRegister Idx = tryParseIndexSuffix();
        Operands.push_back(VAXOperand::createMem(
            Reg, nullptr, Idx, VAXAM::AutoInc, StartLoc,
            getLexer().getLoc()));
        return false;
      }

      // Register deferred: (%reg)
      MCRegister Idx = tryParseIndexSuffix();
      Operands.push_back(VAXOperand::createMem(
          Reg, MCConstantExpr::create(0, getContext()), Idx,
          VAXAM::RegDeferred, StartLoc, getLexer().getLoc()));
      return false;
    }

    // Not a register — parenthesized expression like (expr) for CALLS indirect
    const MCExpr *Expr;
    if (getParser().parseExpression(Expr))
      return true;
    if (getLexer().isNot(AsmToken::RParen))
      return Error(getLexer().getLoc(), "expected ')'");
    getLexer().Lex(); // eat ')'
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
      MCRegister Idx = tryParseIndexSuffix();
      Operands.push_back(VAXOperand::createMem(
          Reg, Expr, Idx, VAXAM::Disp, StartLoc, getLexer().getLoc()));
      return false;
    }

    // expr[%reg] — indexed with PC-relative base? Or bare expression.
    // In VAX GAS, a bare symbol can be indexed: symbol[%reg].
    // This is PC-relative displacement indexed.
    MCRegister Idx = tryParseIndexSuffix();
    if (Idx) {
      // expr[Rx] = PC-relative displacement indexed
      Operands.push_back(VAXOperand::createMem(
          MCRegister(), Expr, Idx, VAXAM::Disp, StartLoc,
          getLexer().getLoc()));
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
  //   %reg  → Mem{Base=reg, Disp=null, Idx=0, Flags=RegDirect}
  //   $imm  → Mem{Base=NoReg, Disp=imm, Idx=0, Flags=Imm}
  if (Kind == MCK_Mem) {
    if (Op.isReg()) {
      Op.morphToMem(Op.getReg(), nullptr, VAXAM::RegDirect);
      return Match_Success;
    }
    if (Op.isImm()) {
      Op.morphToMem(MCRegister(), Op.getImm(), VAXAM::Imm);
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
