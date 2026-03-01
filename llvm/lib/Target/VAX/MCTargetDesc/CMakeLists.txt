add_llvm_component_library(LLVMVAXDesc
  VAXInstPrinter.cpp
  VAXMCAsmInfo.cpp
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
