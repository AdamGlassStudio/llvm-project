add_llvm_component_group(VAX)

set(LLVM_TARGET_DEFINITIONS VAX.td)

tablegen(LLVM VAXGenAsmWriter.inc     -gen-asm-writer)
tablegen(LLVM VAXGenAsmMatcher.inc    -gen-asm-matcher)
tablegen(LLVM VAXGenCallingConv.inc   -gen-callingconv)
tablegen(LLVM VAXGenDAGISel.inc       -gen-dag-isel)
tablegen(LLVM VAXGenInstrInfo.inc     -gen-instr-info)
tablegen(LLVM VAXGenRegisterBank.inc  -gen-register-bank)
tablegen(LLVM VAXGenRegisterInfo.inc  -gen-register-info)
tablegen(LLVM VAXGenSubtargetInfo.inc -gen-subtarget)

set(LLVM_TARGET_DEFINITIONS VAXGISel.td)
tablegen(LLVM VAXGenGlobalISel.inc    -gen-global-isel)

add_public_tablegen_target(VAXCommonTableGen)

add_llvm_target(VAXCodeGen
  GISel/VAXCallLowering.cpp
  GISel/VAXInstructionSelector.cpp
  GISel/VAXLegalizerInfo.cpp
  GISel/VAXRegisterBankInfo.cpp
  VAXAsmPrinter.cpp
  VAXFixupPSW.cpp
  VAXFrameLowering.cpp
  VAXInstrInfo.cpp
  VAXISelDAGToDAG.cpp
  VAXISelLowering.cpp
  VAXPeephole.cpp
  VAXRegisterInfo.cpp
  VAXSelectionDAGInfo.cpp
  VAXSubtarget.cpp
  VAXTargetMachine.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  GlobalISel
  MC
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils
  VAXDesc
  VAXInfo

  ADD_TO_COMPONENT
  VAX
  )

add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
add_subdirectory(AsmParser)
