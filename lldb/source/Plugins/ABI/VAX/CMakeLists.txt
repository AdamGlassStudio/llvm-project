add_lldb_library(lldbPluginABIVAX PLUGIN
  ABISysV_vax.cpp

  LINK_COMPONENTS
    Support
    TargetParser
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbValueObject
  )
