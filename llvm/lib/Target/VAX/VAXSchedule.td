//===-- VAXSchedule.td - VAX Scheduling Definitions ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Scheduling model for the CVAX (DC580) microprocessor as used in the
// MicroVAX 3800/3900 (KA655 CPU module). This is the processor simulated
// by SIMH's "vax" command (MicroVAX 3900).
//
// Timing data source:
//   DEC internal engineering note 166.1, "CVAX Instruction Timing",
//   by Mike Phipps, 24-JUL-1992.
//   Archived at: https://web.mit.edu/~jfc/h/vax/cvaxt
//
// The CVAX is a single-issue, in-order, microcoded processor with no
// instruction buffering or out-of-order execution. Instruction time is:
//   inst_time = spec_1_time + ... + spec_n_time + execute_fetch_time
//
// Times below are "execute, fetch" cycles only — specifier access times
// depend on addressing mode and are handled separately by the specifier
// timing tables in the source document. The values here assume register
// operands (cheapest case); memory operands add cycles per the specifier
// tables.
//
// Floating-point timings are listed as "tbd" in the CVAX document for
// all FP instructions — the CVAX uses an optional FPA (Floating Point
// Accelerator, the CFPA chip). Without the FPA, FP is emulated in
// microcode. The values used here are estimates based on relative
// complexity; they should be refined when real measurements are available.
//
// CompleteModel = 0 means unmodeled instructions get default costs.
// This is intentional — we annotate what we know and refine over time.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Functional units — CVAX has a single execution pipeline
//===----------------------------------------------------------------------===//

def CVAX_FU : FuncUnit;

//===----------------------------------------------------------------------===//
// Itinerary classes (legacy interface, kept for compatibility)
//===----------------------------------------------------------------------===//

def IIC_ALU      : InstrItinClass;  // 1-cycle integer ALU
def IIC_ALU_Slow : InstrItinClass;  // Multi-cycle integer (shift, convert)
def IIC_MUL      : InstrItinClass;  // Integer multiply
def IIC_DIV      : InstrItinClass;  // Integer divide
def IIC_LD       : InstrItinClass;  // Load (MOV from memory)
def IIC_ST       : InstrItinClass;  // Store (MOV to memory)
def IIC_BR       : InstrItinClass;  // Branch
def IIC_CALL     : InstrItinClass;  // CALLS/CALLG
def IIC_RET      : InstrItinClass;  // RET
def IIC_FP       : InstrItinClass;  // Floating-point (estimated)
def IIC_FP_DIV   : InstrItinClass;  // FP divide (estimated)

def CVAXItinerary : ProcessorItineraries<[CVAX_FU], [], [
  InstrItinData<IIC_ALU,      [InstrStage<1, [CVAX_FU]>]>,
  InstrItinData<IIC_ALU_Slow, [InstrStage<5, [CVAX_FU]>]>,
  InstrItinData<IIC_MUL,      [InstrStage<37, [CVAX_FU]>]>,
  InstrItinData<IIC_DIV,      [InstrStage<40, [CVAX_FU]>]>,
  InstrItinData<IIC_LD,       [InstrStage<1, [CVAX_FU]>]>,
  InstrItinData<IIC_ST,       [InstrStage<1, [CVAX_FU]>]>,
  InstrItinData<IIC_BR,       [InstrStage<3, [CVAX_FU]>]>,
  InstrItinData<IIC_CALL,     [InstrStage<24, [CVAX_FU]>]>,
  InstrItinData<IIC_RET,      [InstrStage<15, [CVAX_FU]>]>,
  InstrItinData<IIC_FP,       [InstrStage<20, [CVAX_FU]>]>,
  InstrItinData<IIC_FP_DIV,   [InstrStage<60, [CVAX_FU]>]>
]>;

//===----------------------------------------------------------------------===//
// SchedMachineModel — the primary scheduling interface
//===----------------------------------------------------------------------===//

def CVAXModel : SchedMachineModel {
  let IssueWidth = 1;           // Single-issue, in-order
  let MicroOpBufferSize = 0;    // No buffering (fully in-order)
  let LoopMicroOpBufferSize = 0;
  let LoadLatency = 1;          // Cache hit = 1 microcycle; miss adds more
  let MispredictPenalty = 3;    // Branch taken = 3+1r cycles
  let PostRAScheduler = 0;      // Disabled for now; enable when validated
  let CompleteModel = 0;        // Unmodeled instructions get defaults
  let Itineraries = CVAXItinerary;
}

//===----------------------------------------------------------------------===//
// Processor resource — single execution unit
//===----------------------------------------------------------------------===//

def CVAXUnit : ProcResource<1> { let BufferSize = 0; }

//===----------------------------------------------------------------------===//
// SchedWrite classes
//
// Latency values are execute/fetch cycles from the CVAX timing document.
// Specifier overhead (addressing mode dependent) is NOT included — it
// would require per-operand modeling which LLVM's scheduler doesn't
// support at this granularity.
//===----------------------------------------------------------------------===//

def WriteALU     : SchedWrite;  // ADD, SUB, BIS, BIC, XOR, CMP, TST, MOV, CLR
def WriteMOV     : SchedWrite;  // MOVL/MOVQ (execute = 0, fully pipelined)
def WriteCVT     : SchedWrite;  // CVTBL, CVTWL, MOVZBL etc (2 cycles)
def WriteCVTSlow : SchedWrite;  // CVTLB, CVTLW, CVTWB (4-5 cycles)
def WriteShift   : SchedWrite;  // ASHL (5-8 cycles, typ 7)
def WriteShiftQ  : SchedWrite;  // ASHQ (4-8 cycles, typ 7)
def WriteROTL    : SchedWrite;  // ROTL (3 cycles)
def WriteMULL    : SchedWrite;  // MULL (37 cycles w/o FPA)
def WriteMULW    : SchedWrite;  // MULW (21 cycles)
def WriteMULB    : SchedWrite;  // MULB (14 cycles)
def WriteEMUL    : SchedWrite;  // EMUL (36 cycles w/o FPA)
def WriteDIVL    : SchedWrite;  // DIVL (39-43, typ 40 w/o FPA)
def WriteDIVW    : SchedWrite;  // DIVW (24-27, typ 25)
def WriteDIVB    : SchedWrite;  // DIVB (16-19, typ 17)
def WriteEDIV    : SchedWrite;  // EDIV (47-52, typ 48)
def WriteBR      : SchedWrite;  // Unconditional branch (3+1r)
def WriteBRcc    : SchedWrite;  // Conditional branch not-taken (1), taken (3+1r)
def WriteCALL    : SchedWrite;  // CALLS (24+2r+6w+n+nw)
def WriteRET     : SchedWrite;  // RET (13-15+reads)
def WriteJMP     : SchedWrite;  // JMP (3+1r)
def WriteJSB     : SchedWrite;  // JSB (4+1r+1w)
def WriteRSB     : SchedWrite;  // RSB (4+2r)
def WritePUSH    : SchedWrite;  // PUSHL (2+1w)
def WritePUSHA   : SchedWrite;  // PUSHAx (2+1w)
def WriteCASE    : SchedWrite;  // CASEx (7-8+reads)
def WriteEXTZV   : SchedWrite;  // EXTZV/EXTV/CMPV/CMPZV (11-18, typ 13)
def WriteINSV    : SchedWrite;  // INSV (10-15)
def WriteFFS     : SchedWrite;  // FFS/FFC (12-37, typ 20-23)
def WriteINC     : SchedWrite;  // INC/DEC — 3 reg, 1 mem
def WriteMCOM    : SchedWrite;  // MCOM/MNEG — 0 reg, 1 mem
def WriteADWC    : SchedWrite;  // ADWC/SBWC (1 cycle)
def WriteNOP     : SchedWrite;  // NOP (1 cycle)
def WriteHALT    : SchedWrite;  // HALT (2 cycles)
def WriteMOVPSL  : SchedWrite;  // MOVPSL (1-2 cycles)
def WriteBIxPSW  : SchedWrite;  // BISPSW/BICPSW (5 cycles)
def WritePOPR    : SchedWrite;  // POPR (8+n+nr)
def WritePUSHR   : SchedWrite;  // PUSHR (5+2n+nw)
def WriteSOB     : SchedWrite;  // SOBxxx (3-4 cycles)
def WriteAOB     : SchedWrite;  // AOBxxx (4-5 cycles)
def WriteACB     : SchedWrite;  // ACBx (6-8 cycles)
def WriteBLBx    : SchedWrite;  // BLBS/BLBC (2/4+1r)
def WriteBBxx    : SchedWrite;  // BBS/BBC/BBxS/BBxC (5-9 cycles)
// Floating-point — CVAX document says "tbd" for all FP; these are estimates
def WriteFPALU   : SchedWrite;  // ADDF/SUBF/MULF (estimated ~20 cycles)
def WriteFPDIV   : SchedWrite;  // DIVF (estimated ~40 cycles)
def WriteFPDALU  : SchedWrite;  // ADDD/SUBD/MULD (estimated ~30 cycles)
def WriteFPDDIV  : SchedWrite;  // DIVD (estimated ~60 cycles)
def WriteFPMOV   : SchedWrite;  // MOVF/MOVD (estimated ~2 cycles)
def WriteFPCVT   : SchedWrite;  // FP conversions (estimated ~10 cycles)
// String/decimal — microcode-assisted emulation
def WriteSTRING  : SchedWrite;  // MOVC3/MOVC5/CMPC3/etc (variable)
def WriteEMUL_OP : SchedWrite;  // Decimal/CRC emulated (17+2r+10w)

let SchedModel = CVAXModel in {
  // --- Integer ALU (1 execute cycle) ---
  // Source: CVAX timing §12.2.1 — ADDx2/3, SUBx2/3, BISx2/3, BICx2/3,
  // XORx2/3, CMPx, ADWC, SBWC all have 1 execute cycle.
  def : WriteRes<WriteALU, [CVAXUnit]>     { let Latency = 1; }

  // MOVL/MOVQ execute in 0 cycles (fully overlapped with specifier fetch).
  // We use 1 as minimum since the scheduler can't represent 0-cycle ops.
  def : WriteRes<WriteMOV, [CVAXUnit]>     { let Latency = 1; }

  // --- Conversions ---
  // Source: §12.2.1 — CVTBL/CVTBW/CVTWL/MOVZxL = 2 cycles
  def : WriteRes<WriteCVT, [CVAXUnit]>     { let Latency = 2; }
  // CVTLB/CVTLW/CVTWB = 4-5, typ 4
  def : WriteRes<WriteCVTSlow, [CVAXUnit]> { let Latency = 4; }

  // --- Shifts/rotates ---
  // Source: §12.2.1 — ASHL 5-8 typ 7, ASHQ 4-8 typ 7, ROTL 3
  def : WriteRes<WriteShift, [CVAXUnit]>   { let Latency = 7; }
  def : WriteRes<WriteShiftQ, [CVAXUnit]>  { let Latency = 7; }
  def : WriteRes<WriteROTL, [CVAXUnit]>    { let Latency = 3; }

  // --- Integer multiply ---
  // Source: §12.2.1 — MULL 37 (w/o FPA), MULW 21, MULB 14, EMUL 36
  def : WriteRes<WriteMULL, [CVAXUnit]>    { let Latency = 37; }
  def : WriteRes<WriteMULW, [CVAXUnit]>    { let Latency = 21; }
  def : WriteRes<WriteMULB, [CVAXUnit]>    { let Latency = 14; }
  def : WriteRes<WriteEMUL, [CVAXUnit]>    { let Latency = 36; }

  // --- Integer divide ---
  // Source: §12.2.1 — DIVL 39-43 typ 40, DIVW 24-27 typ 25, DIVB 16-19
  //   typ 17, EDIV 47-52 typ 48
  def : WriteRes<WriteDIVL, [CVAXUnit]>    { let Latency = 40; }
  def : WriteRes<WriteDIVW, [CVAXUnit]>    { let Latency = 25; }
  def : WriteRes<WriteDIVB, [CVAXUnit]>    { let Latency = 17; }
  def : WriteRes<WriteEDIV, [CVAXUnit]>    { let Latency = 48; }

  // --- INC/DEC (3 reg, 1 mem) — use 3 as conservative ---
  // Source: §12.2.1 — INCx/DECx: 3 dst=reg, 1 dst=mem
  def : WriteRes<WriteINC, [CVAXUnit]>     { let Latency = 3; }

  // --- MCOM/MNEG (0 reg, 1 mem) — use 1 as minimum ---
  // Source: §12.2.1 — MCOMx/MNEGx: 0 dst=reg, 1 dst=mem
  def : WriteRes<WriteMCOM, [CVAXUnit]>    { let Latency = 1; }

  // --- ADWC/SBWC ---
  // Source: §12.2.1
  def : WriteRes<WriteADWC, [CVAXUnit]>    { let Latency = 1; }

  // --- TST (2 reg, 1 mem) — use 2 as conservative ---
  // Reuses WriteALU but TST is actually 2 for register. Since TST typically
  // reads from a register or memory result, the ALU cost of 1 is acceptable.

  // --- Control flow ---
  // Source: §12.2.4
  // Conditional branch: 1 not-taken, 3+1r taken. Use 1 (common case).
  def : WriteRes<WriteBRcc, [CVAXUnit]>    { let Latency = 1; }
  // Unconditional branch: 3+1r
  def : WriteRes<WriteBR, [CVAXUnit]>      { let Latency = 3; }
  // JMP: 3+1r
  def : WriteRes<WriteJMP, [CVAXUnit]>     { let Latency = 3; }
  // JSB: 4+1r+1w
  def : WriteRes<WriteJSB, [CVAXUnit]>     { let Latency = 4; }
  // RSB: 4+2r
  def : WriteRes<WriteRSB, [CVAXUnit]>     { let Latency = 4; }
  // BLBS/BLBC: 2 not-taken, 4+1r taken
  def : WriteRes<WriteBLBx, [CVAXUnit]>    { let Latency = 2; }
  // BBS/BBC and variants: 5-9 cycles
  def : WriteRes<WriteBBxx, [CVAXUnit]>    { let Latency = 6; }
  // SOBxxx: 3-4 cycles
  def : WriteRes<WriteSOB, [CVAXUnit]>     { let Latency = 4; }
  // AOBxxx: 4-5 cycles
  def : WriteRes<WriteAOB, [CVAXUnit]>     { let Latency = 5; }
  // ACBx: 6-8 cycles
  def : WriteRes<WriteACB, [CVAXUnit]>     { let Latency = 7; }

  // --- Procedure call/return ---
  // Source: §12.2.5 — CALLS 24+2r+6w+n+nw, RET 13-15+reads
  def : WriteRes<WriteCALL, [CVAXUnit]>    { let Latency = 24; }
  def : WriteRes<WriteRET, [CVAXUnit]>     { let Latency = 15; }

  // --- PUSH/address ---
  // Source: §12.2.1 — PUSHL 2+1w; §12.2.2 — PUSHAx 2+1w
  def : WriteRes<WritePUSH, [CVAXUnit]>    { let Latency = 2; }
  def : WriteRes<WritePUSHA, [CVAXUnit]>   { let Latency = 2; }

  // --- CASE ---
  // Source: §12.2.4 — CASEx: 8+2r (out-of-range) / 7+1r (in-range)
  def : WriteRes<WriteCASE, [CVAXUnit]>    { let Latency = 8; }

  // --- Bit field ---
  // Source: §12.2.3 — EXTZV/EXTV/CMPV/CMPZV: 12-18 typ 13 (reg)
  def : WriteRes<WriteEXTZV, [CVAXUnit]>   { let Latency = 13; }
  // INSV: 10-12 typ 10 (reg)
  def : WriteRes<WriteINSV, [CVAXUnit]>    { let Latency = 10; }
  // FFS/FFC: 13-37 typ 20-23
  def : WriteRes<WriteFFS, [CVAXUnit]>     { let Latency = 21; }

  // --- Misc ---
  def : WriteRes<WriteNOP, [CVAXUnit]>     { let Latency = 1; }
  def : WriteRes<WriteHALT, [CVAXUnit]>    { let Latency = 2; }
  def : WriteRes<WriteMOVPSL, [CVAXUnit]>  { let Latency = 2; }
  def : WriteRes<WriteBIxPSW, [CVAXUnit]>  { let Latency = 5; }
  // POPR: 8+n+nr (n = set bits in mask); use 12 as typical (4 regs)
  def : WriteRes<WritePOPR, [CVAXUnit]>    { let Latency = 12; }
  // PUSHR: 5+2n+nw; use 13 as typical (4 regs)
  def : WriteRes<WritePUSHR, [CVAXUnit]>   { let Latency = 13; }

  // --- Floating-point (ESTIMATED — CVAX document says "tbd") ---
  // Without FPA, all FP is microcode-emulated. These estimates are based
  // on relative integer instruction costs and typical microcode overhead.
  def : WriteRes<WriteFPALU, [CVAXUnit]>   { let Latency = 20; }
  def : WriteRes<WriteFPDIV, [CVAXUnit]>   { let Latency = 40; }
  def : WriteRes<WriteFPDALU, [CVAXUnit]>  { let Latency = 30; }
  def : WriteRes<WriteFPDDIV, [CVAXUnit]>  { let Latency = 60; }
  def : WriteRes<WriteFPMOV, [CVAXUnit]>   { let Latency = 2; }
  def : WriteRes<WriteFPCVT, [CVAXUnit]>   { let Latency = 10; }

  // --- String instructions (variable length, use typical small case) ---
  // Source: §12.2.8 — MOVC3: 16+3n+nr+nw; for n=4: ~32
  def : WriteRes<WriteSTRING, [CVAXUnit]>  { let Latency = 32; }

  // --- Microcode-assisted emulated (decimal, CRC, etc) ---
  // Source: §12.2.11 — all take 17+2r+10w for the emulation exception
  def : WriteRes<WriteEMUL_OP, [CVAXUnit]> { let Latency = 30; }
}
