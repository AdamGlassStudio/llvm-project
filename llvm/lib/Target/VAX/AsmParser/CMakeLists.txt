add_llvm_component_library(LLVMVAXAsmParser
  VAXAsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  Support
  VAXDesc
  VAXInfo

  ADD_TO_COMPONENT
  VAX
)
