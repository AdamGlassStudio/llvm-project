add_llvm_component_library(LLVMVAXDisassembler
  VAXDisassembler.cpp

  LINK_COMPONENTS
  MC
  MCDisassembler
  Support
  VAXDesc
  VAXInfo

  ADD_TO_COMPONENT
  VAX
  )
