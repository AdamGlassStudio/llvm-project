add_llvm_component_group(VAX)

set(LLVM_TARGET_DEFINITIONS VAX.td)

tablegen(LLVM VAXGenAsmWriter.inc     -gen-asm-writer)
tablegen(LLVM VAXGenCallingConv.inc   -gen-callingconv)
tablegen(LLVM VAXGenDAGISel.inc       -gen-dag-isel)
tablegen(LLVM VAXGenInstrInfo.inc     -gen-instr-info)
tablegen(LLVM VAXGenRegisterInfo.inc  -gen-register-info)
tablegen(LLVM VAXGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(VAXCommonTableGen)

add_llvm_target(VAXCodeGen
  VAXAsmPrinter.cpp
  VAXFixupPSW.cpp
  VAXFrameLowering.cpp
  VAXInstrInfo.cpp
  VAXISelDAGToDAG.cpp
  VAXISelLowering.cpp
  VAXPeephole.cpp
  VAXRegisterInfo.cpp
  VAXSubtarget.cpp
  VAXTargetMachine.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
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

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
