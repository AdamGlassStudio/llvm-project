add_llvm_component_library(LLVMVAXDesc
  VAXELFObjectWriter.cpp
  VAXInstPrinter.cpp
  VAXMCAsmBackend.cpp
  VAXMCAsmInfo.cpp
  VAXMCCodeEmitter.cpp
  VAXMCTargetDesc.cpp

  LINK_COMPONENTS
  MC
  MCDisassembler
  Support
  TargetParser
  VAXInfo

  ADD_TO_COMPONENT
  VAX
  )
